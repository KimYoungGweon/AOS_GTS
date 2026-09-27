#!/usr/bin/env python3
"""
GTS UDP Server — Console <-> Server <-> GFC 제어 중계

규격
  5502 Console  : DOC/GTS_UDP_Protocol.md
                  STX TYPE ID CMD SIZE(2LE) DATA[SIZE] CRC16(2LE)
                  DATA 선두 4byte = src_type, src_id, seq(2LE)
                  CRC 범위 = TYPE ~ DATA (STX 제외)

  5501 GFC      : DOC/GTS_GFC_UDP.md 6장
                  STX DTYPE DID CMD SEQ(2LE) SIZE(2LE) DATA[SIZE] CRC16(2LE)
                  CRC 범위 = DTYPE ~ DATA (STX 제외)

  5500 AOS      : DOC/GTS_AOS_Bridge.md — 프레임은 GFC 와 동일, DTYPE=1

DB / 웹 API 연동 (2026-09-23)
  이 파일은 gts/hooks.py 의 _emit() 로 이벤트만 흘려보낸다. 저장·API 는 gts/ 가 맡는다.
  단독 실행(python3 udp_server.py)하면 예전과 똑같이 UDP 중계만 한다.
  운영은 gts_server.py (UDP + DB + FastAPI 한 프로세스).

두 프로토콜은 헤더가 다르다 (콘솔은 seq 가 DATA 안, GFC 는 헤더에).
변환은 전부 이 파일의 on_console_frame() 이 맡는다.

명령 변환표
  Console                          →  GFC
  0x20 GFC_SET_MODE  (u8 mode)     →  MANUAL 이면 0x34 SRC_SET(enable=0)
  0x21 GFC_SET_PUMP  (u8 on)       →  0x30 PUMP_SET(on,on,on)
  0x22 GFC_SET_TIMES (u16,u16 ds)  →  (저장) 동작 중이면 0x34 SRC_SET 재전송
  0x23 GFC_AUTO_RUN  (u8 run)      →  0x34 SRC_SET(enable=run,
                                         init=start_ds/10, cycle=cycle_ds/10,
                                         period=600)

역방향
  GFC 0x32 SENSOR_DATA → Console 0xA3 GFC_AUTO_STATE (500 ms 주기 push)
"""

import asyncio
import struct
import time
from datetime import datetime

try:                                    # DB/API 연동 통로 — 없으면 no-op
    from gts.hooks import emit as _emit
except Exception:                       # noqa: BLE001
    def _emit(*_a):
        pass

# ────────────────────────────────────────────────────────────────────
# 설정
# ────────────────────────────────────────────────────────────────────

PORT_AOS = 5500
PORT_GFC = 5501
PORT_CONSOLE = 5502

GFC_LOCAL_PORT = 5501          # GFC 펌웨어가 bind 하는 로컬 포트 (gfc_config.h)

PUSH_PERIOD = 0.5              # 콘솔로 상태 push 주기 [s]
DEVICE_TIMEOUT = 5.0           # 이 시간 무통신이면 OFFLINE [s]
CYCLE_PERIOD_SEC = 600.0       # 10분 주기 (콘솔 P2 고정값)

LOG_HEX = False                # True 면 모든 패킷 HEX 덤프

# 하향 제어 명령 재전송 (2026-09-23) — UDP 는 손실될 수 있고(WiFi·NAT), 브리지는
# 제어 명령마다 ACK(seq 동일)를 돌려준다. ACK 가 없으면 같은 패킷을 다시 보낸다.
# 제어 명령은 전부 멱등(같은 값 재설정)이라 중복 도착은 무해하다.
RETRY_AFTER = 0.4              # ACK 대기 [s]
RETRY_MAX = 3                  # 재전송 횟수 (총 4번 송신)


def ts() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def log(tag: str, msg: str) -> None:
    print(f"[{ts()}] [{tag}] {msg}", flush=True)


# ────────────────────────────────────────────────────────────────────
# CRC-16/MODBUS — 양쪽 프로토콜 공용
# ────────────────────────────────────────────────────────────────────

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


# ────────────────────────────────────────────────────────────────────
# Console 프로토콜 (5502)
# ────────────────────────────────────────────────────────────────────

C_STX = 0x02
C_HDR = 4                      # src_type, src_id, seq(2)

DEV_NONE, DEV_AOS, DEV_GFC, DEV_CONSOLE = 0x00, 0x01, 0x02, 0x03

C_ACK = 0x00
C_CONNECT = 0x01
C_DISCONNECT = 0x02
C_PING = 0x03
C_STATUS_REQ = 0x04
C_NAK = 0x7F
C_CONNECT_ACK = 0x81
C_DISCONNECT_ACK = 0x82
C_PONG = 0x83
C_STATUS_RESP = 0x84

C_GFC_SET_MODE = 0x20
C_GFC_SET_PUMP = 0x21
C_GFC_SET_TIMES = 0x22
C_GFC_AUTO_RUN = 0x23
C_GFC_AUTO_STATE = 0xA3

C_AOS_SET_TYPE = 0x30
C_AOS_MEAS_RUN = 0x31
C_AOS_MEAS_STATE = 0xB1
C_AOS_SET_PARAM = 0x40
C_AOS_SET_LF_MODE = 0x41
C_AOS_SET_LF_SHAPE = 0x42
C_AOS_GET_PARAMS = 0x43
C_AOS_PARAMS = 0xC3

ERR_UNKNOWN_CMD, ERR_RANGE, ERR_NO_DEVICE, ERR_BUSY, ERR_CRC = 1, 2, 3, 4, 5

CONSOLE_CMD_NAME = {
    C_CONNECT: "CONNECT", C_DISCONNECT: "DISCONNECT", C_PING: "PING",
    C_STATUS_REQ: "STATUS_REQ",
    C_GFC_SET_MODE: "GFC_SET_MODE", C_GFC_SET_PUMP: "GFC_SET_PUMP",
    C_GFC_SET_TIMES: "GFC_SET_TIMES", C_GFC_AUTO_RUN: "GFC_AUTO_RUN",
    C_AOS_SET_TYPE: "AOS_SET_TYPE", C_AOS_MEAS_RUN: "AOS_MEAS_RUN",
    C_AOS_SET_PARAM: "AOS_SET_PARAM", C_AOS_SET_LF_MODE: "AOS_SET_LF_MODE",
    C_AOS_SET_LF_SHAPE: "AOS_SET_LF_SHAPE", C_AOS_GET_PARAMS: "AOS_GET_PARAMS",
}


def console_build(dev_type, dev_id, cmd, src_type, src_id, seq, payload=b"") -> bytes:
    size = C_HDR + len(payload)
    body = bytes([dev_type, dev_id, cmd]) + struct.pack("<H", size)
    body += bytes([src_type, src_id]) + struct.pack("<H", seq) + payload
    return bytes([C_STX]) + body + struct.pack("<H", crc16(body))


def console_parse(data: bytes):
    """-> dict | None"""
    if len(data) < 12:
        return None
    if data[0] != C_STX:
        return None
    size = struct.unpack_from("<H", data, 4)[0]
    if size < C_HDR or size + 8 != len(data):
        return None
    crc_rx = struct.unpack_from("<H", data, 6 + size)[0]
    if crc_rx != crc16(data[1:6 + size]):
        return None
    return {
        "type": data[1],
        "id": data[2],
        "cmd": data[3],
        "src_type": data[6],
        "src_id": data[7],
        "seq": struct.unpack_from("<H", data, 8)[0],
        "payload": data[6 + C_HDR:6 + size],
    }


# ────────────────────────────────────────────────────────────────────
# GFC 프로토콜 (5501)
# ────────────────────────────────────────────────────────────────────

G_STX = 0x02
G_HDR = 8

G_HELLO = 0x01
G_ACK = 0x02
G_CRES = 0x03
G_EVENT = 0x04
G_PING = 0x05
G_DISCOVER = 0x06
G_PUMP_SET = 0x30
G_PUMP_QUERY = 0x31
G_SENSOR_DATA = 0x32
G_SENSOR_BULK = 0x33
G_SRC_SET = 0x34
G_AUTO_SET = 0x35
G_CFG_SET = 0x36
G_NET_SET = 0x37
G_TIME_SET = 0x38
G_CFG_QUERY = 0x39
G_SYS = 0x3A

DTYPE_AOS, DTYPE_GFC, DTYPE_CONTROL, DTYPE_BCAST = 1, 2, 3, 255

GFC_CMD_NAME = {
    G_HELLO: "HELLO", G_ACK: "ACK", G_CRES: "CRES", G_EVENT: "EVENT",
    G_PING: "PING", G_DISCOVER: "DISCOVER", G_PUMP_SET: "PUMP_SET",
    G_PUMP_QUERY: "PUMP_QUERY", G_SENSOR_DATA: "SENSOR_DATA",
    G_SRC_SET: "SRC_SET", G_CFG_QUERY: "CFG_QUERY", G_SYS: "SYS",
}

EVENT_NAME = {
    0x01: "BOOT", 0x10: "SRC_ON", 0x11: "SRC_OFF",
    0x31: "LINK_LOST", 0x32: "POLL_MODE",
}

# sensor_data_t (52 byte) — DOC 6.4
SENSOR_FMT = "<I7fI4H4BHbB"
assert struct.calcsize(SENSOR_FMT) == 52, struct.calcsize(SENSOR_FMT)

SRC_SET_FMT = "<B3x3f"
assert struct.calcsize(SRC_SET_FMT) == 16

FLAG_SRC_ENABLE = 1 << 0
FLAG_SRC_ON = 1 << 1
FLAG_UART_OK = 1 << 3
FLAG_SRC_INIT = 1 << 4
FLAG_AUTO_PUSH = 1 << 7


def gfc_build(dtype, did, cmd, seq, payload=b"") -> bytes:
    body = bytes([dtype, did, cmd]) + struct.pack("<HH", seq, len(payload)) + payload
    return bytes([G_STX]) + body + struct.pack("<H", crc16(body))


def gfc_parse(data: bytes):
    if len(data) < 10 or data[0] != G_STX:
        return None
    size = struct.unpack_from("<H", data, 6)[0]
    if size + 10 != len(data):
        return None
    crc_rx = struct.unpack_from("<H", data, 8 + size)[0]
    if crc_rx != crc16(data[1:8 + size]):
        return None
    did = data[2]
    if did == 0 or (did > 20 and did != DTYPE_BCAST):
        return None
    return {
        "dtype": data[1],
        "did": did,
        "cmd": data[3],
        "seq": struct.unpack_from("<H", data, 4)[0],
        "payload": data[G_HDR:G_HDR + size],
    }


def parse_hello(p: bytes):
    """hello_t (64 byte) — model[16] fw[16] mac[6] dtype did uptime ip[16] rsv[4]"""
    if len(p) < 64:
        return None

    def _s(b):
        return b.split(b"\0")[0].decode(errors="replace")

    mac = ":".join(f"{x:02X}" for x in p[32:38])
    return dict(model=_s(p[0:16]), fw=_s(p[16:32]), mac=mac,
                dtype=p[38], did=p[39],
                uptime=struct.unpack_from("<I", p, 40)[0],
                ip=_s(p[44:60]))


def check_ip_conflict(kind, dev, addr, claimed_ip):
    """장치가 보고한 IP 와 실제 송신 IP 를 대조해 네트워크 구성을 알려 준다.

    둘이 다르면 장치가 **NAT 뒤에 있다**는 뜻이다. 예를 들어 ESP32 가
    Google Nest WiFi(기본 192.168.86.0/24)에 붙어 있으면, 서버에는
    Nest 의 WAN 주소 하나로만 보인다 — 보드가 여럿이어도 전부 같은 IP 다.
    이건 IP 충돌이 아니라 정상적인 NAT 이고, 서버가 "패킷이 온 주소 그대로"
    되돌려 보내므로 동작한다 (E2).

    다만 NAT 매핑은 무통신이 이어지면 만료된다. 브리지가 1초마다 상향을
    올리므로 유지되지만, 상향이 멎으면 하향도 같이 끊긴다.

    진짜 충돌은 자기보고 IP 까지 같은 경우다.
    """
    if not claimed_ip or claimed_ip in ("0.0.0.0", "sim"):
        return

    if claimed_ip != addr[0]:
        log(kind, f"  └ NAT 뒤 — 장치는 {claimed_ip}, 서버에는 {addr[0]}:{addr[1]} 로 보인다")

    same = []
    for reg, label in ((GFCS, "GFC"), (AOSES, "AOS")):
        for d in reg.values():
            if d is dev or not d.online or not d.addr:
                continue
            if d.addr[0] != addr[0]:
                continue
            other_ip = (d.claimed_ip or "?")
            if other_ip == claimed_ip:
                same.append(f"{label}#{d.did}")        # 진짜 충돌
            # 자기보고 IP 가 다르면 같은 NAT 뒤에 있는 것뿐이다 — 정상

    if same:
        log(kind, f"⚠ IP 충돌 — {claimed_ip} 을 {kind}#{dev.did} 와 "
                  f"{', '.join(same)} 가 같이 쓰고 있다. 하향 명령이 "
                  f"엉뚱한 장치로 갈 수 있다 — 고정 IP 를 줄 것")


def parse_sensor(p: bytes):
    if len(p) < 52:
        return None
    (tstamp, volt1, volt2, ctrl, slope, sv, src_remain, src_elapsed,
     uptime, raw1, raw2, co2_1, co2_2,
     pump1, pump2, pump3, flags, err, rssi, _rsv) = struct.unpack(SENSOR_FMT, p[:52])
    return dict(ts=tstamp, volt1=volt1, volt2=volt2, ctrl=ctrl, slope=slope, sv=sv,
                src_remain=src_remain, src_elapsed=src_elapsed, uptime=uptime,
                raw1=raw1, raw2=raw2, co2_1=co2_1, co2_2=co2_2,
                pump1=pump1, pump2=pump2, pump3=pump3, flags=flags,
                err=err, rssi=rssi)


# ────────────────────────────────────────────────────────────────────
# AOS 프로토콜 (5500) — 프레임은 GFC 와 동일, DTYPE 만 1
# ────────────────────────────────────────────────────────────────────

A_HELLO = 0x01
A_ACK = 0x02
A_EVENT = 0x04
A_PING = 0x05
A_DISCOVER = 0x06
A_PARAM_SET = 0x40      # S→A  8  (u8 param_id, u8 pad[3], f32 value)
A_LF_MODE = 0x41        # S→A  4
A_LF_SHAPE = 0x42       # S→A  4
A_PARAMS_QUERY = 0x43   # S→A  0
A_PARAMS = 0x44         # A→S 26  (콘솔 0xC3 과 같은 배치)
A_STATUS = 0x45         # A→S 24
A_SYS = 0x4F
# ── 캘리브레이션 (DOC/GTS_HV캘리브레이션_계획.md 5.1, 2026-09-27) ──────────
#   응답(0x49/0x4B/0x4E)은 요청의 seq 를 그대로 반사한다.
A_CAL_MODE = 0x46       # S→A  4  u8 on, pad[3]                  → ACK
A_CAL_WRITE = 0x47      # S→A  N  STM32 0xA0 payload 그대로       → ACK (UART 송신 후)
A_CAL_QUERY = 0x48      # S→A  4  u8 type, pad[3]                → 0x49
A_CAL_TABLE = 0x49      # A→S  N  STM32 0xA1 응답 payload 그대로  (u8 type, u8 no, no×6)
A_CAL_SENSE_Q = 0x4A    # S→A  4  u16 window_ms, u16 rsv          → 0x4B
A_CAL_SENSE = 0x4B      # A→S 20  f32 avg, f32 min, f32 max, u16 n, u16 rsv, u32 age_ms
A_CAL_DAC_RAW = 0x4C    # S→A  4  u8 ch(0=CV,1=HV), u8 rsv, u16 dac → ACK   (F/W 신규 명령 필요, 선택)
A_CAL_ADC_RAW_Q = 0x4D  # S→A  4  u8 ch, u8 avg_n, u16 rsv          → 0x4E (F/W 신규 명령 필요, 선택)
A_CAL_ADC_RAW = 0x4E    # A→S  8  u8 ch, u8 n, u16 rsv, f32 raw_avg
AOS_CAL_SENSE_FMT = "<3fHHI"
assert struct.calcsize(AOS_CAL_SENSE_FMT) == 20
AOS_CAL_RESP = (A_CAL_TABLE, A_CAL_SENSE, A_CAL_ADC_RAW)
# 캘리브레이션 중인 AOS — 콘솔/웹의 파라미터 변경을 막는다 (gts/cal.py 가 관리)
CAL_BLOCK: set = set()

AOS_CMD_NAME = {
    A_HELLO: "HELLO", A_ACK: "ACK", A_EVENT: "EVENT", A_PING: "PING",
    A_DISCOVER: "DISCOVER", A_PARAM_SET: "PARAM_SET", A_LF_MODE: "LF_MODE",
    A_LF_SHAPE: "LF_SHAPE", A_PARAMS_QUERY: "PARAMS_QUERY",
    A_PARAMS: "PARAMS", A_STATUS: "STATUS", A_SYS: "SYS",
    A_CAL_MODE: "CAL_MODE", A_CAL_WRITE: "CAL_WRITE", A_CAL_QUERY: "CAL_QUERY",
    A_CAL_TABLE: "CAL_TABLE", A_CAL_SENSE_Q: "CAL_SENSE_Q", A_CAL_SENSE: "CAL_SENSE",
    A_CAL_DAC_RAW: "CAL_DAC_RAW", A_CAL_ADC_RAW_Q: "CAL_ADC_RAW_Q", A_CAL_ADC_RAW: "CAL_ADC_RAW",
}

AOS_EVENT_NAME = {0x01: "BOOT", 0x20: "PARAM_APPLY", 0x21: "POINT",
                  0x22: "APPLY_VERIFY_FAIL", 0x31: "LINK_LOST"}

# aos_params_t (26 byte) — 콘솔 AOS_PARAMS(0xC3) 와 바이트 배치가 같다
AOS_PARAMS_FMT = "<6f2B"
assert struct.calcsize(AOS_PARAMS_FMT) == 26, struct.calcsize(AOS_PARAMS_FMT)

# aos_status_t (24 byte)
AOS_STATUS_FMT = "<II4HIBbH"
assert struct.calcsize(AOS_STATUS_FMT) == 24, struct.calcsize(AOS_STATUS_FMT)

AOS_FLAG_LF_ON = 1 << 0
AOS_FLAG_APPLIED = 1 << 1
AOS_FLAG_UART_OK = 1 << 3
AOS_ERR_UART_LOST = 1 << 11

# 콘솔 param_id (DOC/GTS_UDP_Protocol.md 4-5)
PARAM_NAME = ["HV", "FRQ", "DUTY", "CV", "LF_FRQ", "LF_VOLT"]


# ────────────────────────────────────────────────────────────────────
# 장치 / 세션 레지스트리
# ────────────────────────────────────────────────────────────────────

class GfcDevice:
    """GFC 한 대의 최신 상태. 콘솔에 돌려줄 값은 전부 여기서 나온다."""

    def __init__(self, did):
        self.did = did
        self.addr = None
        self.claimed_ip = None
        self.last_seen = 0.0
        self.fw_ver = 0x0100
        self.uptime = 0
        self.sensor = None

        # 콘솔이 설정한 값 (SRC_SET 을 만들 재료)
        self.mode = 1                # 0 MANUAL / 1 AUTO
        self.start_ds = 300          # 30.0 s
        self.cycle_ds = 10           # 1.0 s

        # 상태 파생
        self.src_enable = False
        self.src_on = False
        self.src_init = False
        self.pump1 = 0
        self.pump2 = 0
        self.pump3 = 0
        self.remain_ds = 0
        self.cycle_count = 0
        self._prev_src_on = False

        self.seq = 0

    def next_seq(self):
        self.seq = (self.seq + 1) & 0xFFFF
        return self.seq

    @property
    def online(self):
        return self.addr is not None and (time.time() - self.last_seen) < DEVICE_TIMEOUT

    @property
    def dev_state(self):
        """0 IDLE / 1 RUN / 2 ERROR / 3 OFFLINE — 콘솔 P1 표시용"""
        if not self.online:
            return 3
        if self.sensor and (self.sensor["err"] & (1 << 11)):    # UART_LOST
            return 2
        if self.src_enable or (self.sensor and self.sensor["pump1"]):
            return 1
        return 0

    @property
    def last_reply_ms(self):
        if not self.last_seen:
            return 0xFFFFFFFF
        return min(int((time.time() - self.last_seen) * 1000), 0xFFFFFFFF)

    def apply_sensor(self, s):
        """@return 콘솔에 보이는 값(run/pump)이 바뀌었으면 True."""
        before = (self.src_enable, self.pump2)
        self.sensor = s
        self.uptime = s["uptime"]
        self.pump1, self.pump2, self.pump3 = s["pump1"], s["pump2"], s["pump3"]
        self.src_enable = bool(s["flags"] & FLAG_SRC_ENABLE)
        self.src_on = bool(s["flags"] & FLAG_SRC_ON)
        self.src_init = bool(s["flags"] & FLAG_SRC_INIT)
        self.remain_ds = max(0, min(int(round(s["src_remain"] * 10)), 0xFFFF))
        # 주기 분사의 상승 에지를 세어 cycle_count 를 만든다
        # (DOC 10.2-4: SRC_OFF 이벤트로 Pump2 구형파를 재구성하는 것과 같은 근거)
        # 초기 주입(init_on_sec) 구간은 "주기 분사"가 아니므로 세지 않는다.
        if self.src_on and not self._prev_src_on and not self.src_init:
            self.cycle_count += 1
        self._prev_src_on = self.src_on and not self.src_init
        return before != (self.src_enable, self.pump2)

    def src_set_payload(self, enable: bool) -> bytes:
        return struct.pack(SRC_SET_FMT,
                           1 if enable else 0,
                           self.start_ds / 10.0,
                           self.cycle_ds / 10.0,
                           CYCLE_PERIOD_SEC)


class AosDevice:
    """AOS 한 대. 지금 범위는 Manual 파라미터 7종뿐이다."""

    def __init__(self, did):
        self.did = did
        self.addr = None
        self.claimed_ip = None
        self.last_seen = 0.0
        self.fw_ver = 0x0100
        self.uptime = 0
        self.status = None
        # 장비가 보고한 실제 설정값 (hv, frq, duty, cv, lf_frq, lf_volt, lf_on, lf_shape)
        self.params = None
        self.seq = 0

    def next_seq(self):
        self.seq = (self.seq + 1) & 0xFFFF
        return self.seq

    @property
    def online(self):
        return self.addr is not None and (time.time() - self.last_seen) < DEVICE_TIMEOUT

    @property
    def dev_state(self):
        if not self.online:
            return 3
        if self.status and (self.status["err"] & AOS_ERR_UART_LOST):
            return 2
        if self.status and (self.status["flags"] & AOS_FLAG_APPLIED):
            return 1
        return 0

    @property
    def last_reply_ms(self):
        if not self.last_seen:
            return 0xFFFFFFFF
        return min(int((time.time() - self.last_seen) * 1000), 0xFFFFFFFF)

    def apply_params(self, raw26):
        """@return 값이 바뀌었으면 True."""
        before = self.params
        self.params = raw26
        return before != raw26


class ConsoleSession:
    """콘솔 한 대. P1 에서 고른 (dev_type, dev_id) 를 기억한다."""

    def __init__(self, addr):
        self.addr = addr
        self.my_id = 1
        self.dev_type = DEV_GFC
        self.dev_id = 1
        self.connected = False
        self.last_seen = 0.0
        self.seq = 0

    def next_seq(self):
        self.seq = (self.seq + 1) & 0xFFFF
        return self.seq

    @property
    def alive(self):
        return (time.time() - self.last_seen) < DEVICE_TIMEOUT


GFCS = {}        # did -> GfcDevice
AOSES = {}       # did -> AosDevice
CONSOLES = {}    # addr -> ConsoleSession
HOLDER = {}      # "console"/"aos" -> Protocol (즉시 push 용)

# 하향 제어 명령 ACK 대기 목록: (kind, did, seq) -> dict(pkt, cmd, t, tries, proto, dev)
PENDING = {}


def _pending_add(kind, proto, dev, cmd, seq, pkt):
    PENDING[(kind, dev.did, seq)] = dict(pkt=pkt, cmd=cmd, t=time.time(), tries=0,
                                         proto=proto, dev=dev)


def _pending_ack(kind, did, ack_seq, ack_cmd, result):
    e = PENDING.pop((kind, did, ack_seq), None)
    if e and e["tries"]:
        log(kind.upper(), f"  └ 재전송 {e['tries']}회 만에 ACK (cmd 0x{ack_cmd:02X} seq={ack_seq})")


def retry_pending():
    """ACK 가 안 온 제어 명령을 다시 보낸다. push_loop 가 0.5초마다 부른다."""
    now = time.time()
    for key, e in list(PENDING.items()):
        if now - e["t"] < RETRY_AFTER:
            continue
        kind, did, seq = key
        dev = e["dev"]
        if e["tries"] >= RETRY_MAX or dev.addr is None:
            PENDING.pop(key, None)
            log(kind.upper(), f"⚠ DID={did} cmd 0x{e['cmd']:02X} seq={seq} — "
                              f"{e['tries'] + 1}회 보냈으나 ACK 없음 (포기)")
            _emit("tx_fail", kind, did, e["cmd"], seq)
            continue
        e["tries"] += 1
        e["t"] = now
        e["proto"].transport.sendto(e["pkt"], dev.addr)
        log(kind.upper(), f"TX-> DID={did} 재전송 {e['tries']}/{RETRY_MAX} "
                          f"cmd 0x{e['cmd']:02X} seq={seq}")


def get_gfc(did, create=True):
    d = GFCS.get(did)
    if d is None and create:
        d = GfcDevice(did)
        GFCS[did] = d
    return d


def get_aos(did, create=True):
    d = AOSES.get(did)
    if d is None and create:
        d = AosDevice(did)
        AOSES[did] = d
    return d


# ────────────────────────────────────────────────────────────────────
# 프로토콜 핸들러
# ────────────────────────────────────────────────────────────────────

class GfcProtocol(asyncio.DatagramProtocol):
    """5501 — GFC 장치와의 바이너리 채널"""

    def connection_made(self, transport):
        self.transport = transport
        log("SERVER", f"GFC UDP server started on port {PORT_GFC}")

    # 하향 송신 --------------------------------------------------------
    def send(self, dev: GfcDevice, cmd: int, payload: bytes = b"") -> bool:
        if dev.addr is None:
            log("GFC", f"DID={dev.did} 주소 미확인 — cmd 0x{cmd:02X} 버림")
            return False
        seq = dev.next_seq()
        pkt = gfc_build(DTYPE_GFC, dev.did, cmd, seq, payload)
        self.transport.sendto(pkt, dev.addr)
        if cmd in (G_PUMP_SET, G_SRC_SET):
            _pending_add("gfc", self, dev, cmd, seq, pkt)
        log("GFC", f"TX-> DID={dev.did} {GFC_CMD_NAME.get(cmd, hex(cmd))} "
                   f"seq={seq} {pkt.hex(' ')}")
        return True

    # 수신 -------------------------------------------------------------
    def datagram_received(self, data, addr):
        f = gfc_parse(data)
        if f is None:
            _emit("bad", "gfc", addr, data)
            if LOG_HEX:
                log("GFC", f"{addr[0]}:{addr[1]} 규격 외 {len(data)}B -> {data.hex(' ')}")
            else:
                try:
                    log("GFC", f"{addr[0]}:{addr[1]} -> {data.decode().strip()}")
                except UnicodeDecodeError:
                    log("GFC", f"{addr[0]}:{addr[1]} 버림 {len(data)}B HEX:{data.hex()}")
            return

        if f["dtype"] != DTYPE_GFC:
            return

        dev = get_gfc(f["did"])
        first = dev.addr is None
        # 하향 명령은 방금 패킷이 온 주소 그대로 되돌려 보낸다.
        # 실제 GFC 펌웨어는 로컬 포트를 5501 에 bind 하므로 결과적으로
        # (GFC IP, 5501) 이 되고, 시뮬레이터처럼 임의 포트를 쓰는 상대도
        # 그대로 받는다.
        if dev.addr != addr:
            dev.addr = addr
        dev.last_seen = time.time()
        if first:
            log("GFC", f"NEW DEVICE DID={dev.did} {addr[0]}:{addr[1]}")
        _emit("rx", "gfc", dev, addr, first)

        cmd = f["cmd"]
        name = GFC_CMD_NAME.get(cmd, hex(cmd))

        if cmd == G_SENSOR_DATA:
            s = parse_sensor(f["payload"])
            if s:
                changed = dev.apply_sensor(s)
                _emit("gfc_sensor", dev, s)
                if changed:
                    # 주기 push(500 ms)를 기다리면 사용자가 버튼을 누른 뒤
                    # 상태가 화면에 돌아오기까지 그만큼 더 걸린다.
                    con = HOLDER.get("console")
                    if con:
                        try:
                            con.push_states()
                        except Exception as e:
                            log("SERVER", f"즉시 push 실패: {e!r}")
                if LOG_HEX:
                    log("GFC", f"DID={dev.did} SENSOR p={s['pump1']}{s['pump2']}{s['pump3']} "
                               f"v1={s['volt1']:.3f} v2={s['volt2']:.3f} "
                               f"remain={s['src_remain']:.1f} flags=0x{s['flags']:02X}")
            self.ack(dev, cmd, f["seq"])

        elif cmd == G_HELLO:
            h = parse_hello(f["payload"])
            if h:
                log("GFC", f"HELLO DID={dev.did} model={h['model']} fw={h['fw']} "
                           f"mac={h['mac']} 자기보고IP={h['ip']} 실제={addr[0]}")
                dev.claimed_ip = h["ip"]
                check_ip_conflict("GFC", dev, addr, h["ip"])
                _emit("hello", "gfc", dev, h, addr)
            else:
                log("GFC", f"HELLO DID={dev.did} (짧은 payload) from {addr[0]}")
            self.ack(dev, cmd, f["seq"])

        elif cmd == G_EVENT:
            if len(f["payload"]) >= 16:
                _ts, code, _rsv, a, _b = struct.unpack("<IHHII", f["payload"][:16])
                log("GFC", f"EVENT DID={dev.did} {EVENT_NAME.get(code, hex(code))} a={a}")
                _emit("dev_event", "gfc", dev, code, a)
            self.ack(dev, cmd, f["seq"])

        elif cmd == G_ACK:
            if len(f["payload"]) >= 4:
                ack_cmd, result, ack_seq = struct.unpack("<BBH", f["payload"][:4])
                _pending_ack("gfc", dev.did, ack_seq, ack_cmd, result)
                log("GFC", f"ACK  DID={dev.did} for "
                           f"{GFC_CMD_NAME.get(ack_cmd, hex(ack_cmd))} "
                           f"result={result} seq={ack_seq}")

        elif cmd == G_SRC_SET:          # CFG_QUERY 응답
            if len(f["payload"]) >= 16:
                en, init_s, cyc_s, per_s = struct.unpack(SRC_SET_FMT, f["payload"][:16])
                log("GFC", f"CFG  DID={dev.did} enable={en} init={init_s:.1f} "
                           f"cycle={cyc_s:.1f} period={per_s:.1f}")

        else:
            log("GFC", f"DID={dev.did} {name} seq={f['seq']} size={len(f['payload'])}")

    def ack(self, dev, ack_cmd, ack_seq):
        """상향 패킷마다 즉시 ACK (DOC 10.2-2)."""
        payload = struct.pack("<BBH", ack_cmd, 0, ack_seq)
        pkt = gfc_build(DTYPE_GFC, dev.did, G_ACK, ack_seq, payload)
        if dev.addr:
            self.transport.sendto(pkt, dev.addr)


class ConsoleProtocol(asyncio.DatagramProtocol):
    """5502 — 콘솔과의 채널. 여기서 GFC 프로토콜로 번역한다."""

    def __init__(self, holder):
        self.h = holder                  # {"gfc": GfcProtocol, "aos": AosProtocol}

    def connection_made(self, transport):
        self.transport = transport
        log("SERVER", f"CONSOLE UDP server started on port {PORT_CONSOLE}")

    # 콘솔로 송신 ------------------------------------------------------
    def reply(self, sess, dev_type, dev_id, cmd, seq, payload=b""):
        """요청에 대한 응답 — seq 를 그대로 반사해야 콘솔이 RTT 를 잰다."""
        if getattr(sess, "virtual", False):     # 웹 제어 — UDP 로 보내지 않고 결과만 기록
            sess.results.append((cmd, bytes(payload)))
            return
        pkt = console_build(dev_type, dev_id, cmd, dev_type, dev_id, seq, payload)
        self.transport.sendto(pkt, sess.addr)
        if LOG_HEX:
            log("CONSOLE", f"TX-> {sess.addr[0]} cmd=0x{cmd:02X} seq={seq} {pkt.hex(' ')}")

    def push(self, sess, dev_type, dev_id, cmd, payload=b""):
        """주기 통지 — 응답이 아니므로 서버 자신의 seq 를 쓴다."""
        if getattr(sess, "virtual", False):
            return
        seq = sess.next_seq()
        pkt = console_build(dev_type, dev_id, cmd, dev_type, dev_id, seq, payload)
        self.transport.sendto(pkt, sess.addr)

    def nak(self, sess, dev_type, dev_id, seq, err_cmd, err_code):
        self.reply(sess, dev_type, dev_id, C_NAK, seq,
                   bytes([err_cmd, err_code]))

    # 상태 payload -----------------------------------------------------
    @staticmethod
    def status_payload(dev_type, dev_id):
        """CONNECT_ACK(0x81) / STATUS_RESP(0x84) 의 12 byte."""
        dev = None
        if dev_type == DEV_GFC:
            dev = get_gfc(dev_id, create=False)
        elif dev_type == DEV_AOS:
            dev = get_aos(dev_id, create=False)
        if dev and dev.online:
            return struct.pack("<BBHII", 0, dev.dev_state, dev.fw_ver,
                               dev.uptime, dev.last_reply_ms)
        return struct.pack("<BBHII", ERR_NO_DEVICE, 3, 0, 0, 0xFFFFFFFF)

    def push_aos_params(self, dev):
        """AOS 가 보고한 실제 설정값을 콘솔 0xC3 으로 내린다.
        aos_params_t(26 byte) 와 콘솔 AOS_PARAMS 의 배치가 같아 그대로 싣는다."""
        if dev.params is None:
            return
        for sess in CONSOLES.values():
            if sess.dev_type == DEV_AOS and sess.dev_id == dev.did:
                self.push(sess, DEV_AOS, dev.did, C_AOS_PARAMS, dev.params)

    # 수신 -------------------------------------------------------------
    def datagram_received(self, data, addr):
        f = console_parse(data)
        if f is None:
            _emit("bad", "console", addr, data)
            try:
                log("CONSOLE", f"{addr[0]}:{addr[1]} 규격 외 -> {data.decode().strip()}")
            except UnicodeDecodeError:
                log("CONSOLE", f"{addr[0]}:{addr[1]} 버림 {len(data)}B HEX:{data.hex()}")
            return

        sess = CONSOLES.get(addr)
        if sess is None:
            sess = ConsoleSession(addr)
            CONSOLES[addr] = sess
            log("CONSOLE", f"NEW DEVICE {addr[0]}:{addr[1]}")
        sess.last_seen = time.time()
        sess.my_id = f["src_id"]
        sess.dev_type = f["type"]
        sess.dev_id = f["id"]
        _emit("console_rx", sess, f)

        cmd, seq, pl = f["cmd"], f["seq"], f["payload"]
        name = CONSOLE_CMD_NAME.get(cmd, f"0x{cmd:02X}")

        if cmd != C_PING:
            log("CONSOLE", f"RX<- {addr[0]} {name} target=({f['type']},{f['id']}) "
                           f"seq={seq} payload={pl.hex(' ') if pl else '-'}")

        # ── 시스템 ────────────────────────────────────────────────────
        if cmd == C_CONNECT:
            payload = self.status_payload(f["type"], f["id"])
            sess.connected = (payload[0] == 0)
            self.reply(sess, f["type"], f["id"], C_CONNECT_ACK, seq, payload)
            log("CONSOLE", f"CONNECT_ACK result={payload[0]} state={payload[1]}")
            return

        if cmd == C_STATUS_REQ:
            self.reply(sess, f["type"], f["id"], C_STATUS_RESP, seq,
                       self.status_payload(f["type"], f["id"]))
            return

        if cmd == C_DISCONNECT:
            sess.connected = False
            if f["type"] == DEV_GFC:
                dev = get_gfc(f["id"], create=False)
                if dev and dev.online:
                    dev.mode = 1
                    self.h["gfc"].send(dev, G_SRC_SET, dev.src_set_payload(False))
                    self.h["gfc"].send(dev, G_PUMP_SET, bytes([0, 0, 0, 0]))
            self.reply(sess, f["type"], f["id"], C_DISCONNECT_ACK, seq, bytes([0]))
            return

        if cmd == C_PING:
            state = 0
            if f["type"] == DEV_GFC:
                dev = get_gfc(f["id"], create=False)
                state = 1 if (dev and dev.online and dev.src_enable) else 0
            elif f["type"] == DEV_AOS:
                dev = get_aos(f["id"], create=False)
                state = 1 if (dev and dev.online and dev.dev_state == 1) else 0
            self.reply(sess, f["type"], f["id"], C_PONG, seq, bytes([state]))
            return

        # ── GFC 제어 ──────────────────────────────────────────────────
        if cmd in (C_GFC_SET_MODE, C_GFC_SET_PUMP,
                   C_GFC_SET_TIMES, C_GFC_AUTO_RUN):
            if f["type"] != DEV_GFC:
                self.nak(sess, f["type"], f["id"], seq, cmd, ERR_NO_DEVICE)
                return
            dev = get_gfc(f["id"], create=False)
            if dev is None or not dev.online:
                log("CONSOLE", f"GFC DID={f['id']} 오프라인 — {name} 거부")
                self.nak(sess, f["type"], f["id"], seq, cmd, ERR_NO_DEVICE)
                return
            _emit("console_control", sess, DEV_GFC, dev.did, cmd, pl)   # D14a 콘솔 선점
            self.handle_gfc_cmd(sess, dev, cmd, seq, pl)
            return

        # ── AOS Manual (이번 범위: 파라미터 7종) ──────────────────────
        if cmd in (C_AOS_SET_PARAM, C_AOS_SET_LF_MODE,
                   C_AOS_SET_LF_SHAPE, C_AOS_GET_PARAMS):
            if f["type"] != DEV_AOS:
                self.nak(sess, f["type"], f["id"], seq, cmd, ERR_NO_DEVICE)
                return
            dev = get_aos(f["id"], create=False)
            if dev is None or not dev.online:
                log("CONSOLE", f"AOS DID={f['id']} 오프라인 — {name} 거부")
                self.nak(sess, f["type"], f["id"], seq, cmd, ERR_NO_DEVICE)
                return
            if cmd != C_AOS_GET_PARAMS:
                _emit("console_control", sess, DEV_AOS, dev.did, cmd, pl)
            self.handle_aos_cmd(sess, dev, cmd, seq, pl)
            return

        # ── AOS 측정 — 아직 구현 밖 ───────────────────────────────────
        if cmd in (C_AOS_SET_TYPE, C_AOS_MEAS_RUN):
            log("CONSOLE", f"{name} — AOS 측정 미구현, 무시")
            self.nak(sess, f["type"], f["id"], seq, cmd, ERR_NO_DEVICE)
            return

        self.nak(sess, f["type"], f["id"], seq, cmd, ERR_UNKNOWN_CMD)

    # 콘솔 GFC 명령 → GFC 패킷 ------------------------------------------
    def handle_gfc_cmd(self, sess, dev, cmd, seq, pl):
        g = self.h["gfc"]

        if cmd == C_GFC_SET_MODE:
            if len(pl) < 1:
                self.nak(sess, DEV_GFC, dev.did, seq, cmd, ERR_RANGE)
                return
            dev.mode = pl[0]
            log("MAP", f"SET_MODE {'AUTO' if dev.mode else 'MANUAL'} (DID={dev.did})")
            if dev.mode == 0:                       # MANUAL 로 내려오면 시퀀스 정지
                g.send(dev, G_SRC_SET, dev.src_set_payload(False))
            self.reply(sess, DEV_GFC, dev.did, C_ACK, seq, bytes([cmd]))
            return

        if cmd == C_GFC_SET_PUMP:
            if len(pl) < 1:
                self.nak(sess, DEV_GFC, dev.did, seq, cmd, ERR_RANGE)
                return
            on = 1 if pl[0] else 0
            # Pump1·Pump2 동시, Pump3 은 Pump2 를 따라간다 (DOC 2.2-4, U1)
            log("MAP", f"SET_PUMP {on} -> GFC PUMP_SET({on},{on},{on}) DID={dev.did}")
            g.send(dev, G_PUMP_SET, bytes([on, on, on, 0]))
            self.reply(sess, DEV_GFC, dev.did, C_ACK, seq, bytes([cmd]))
            return

        if cmd == C_GFC_SET_TIMES:
            if len(pl) < 4:
                self.nak(sess, DEV_GFC, dev.did, seq, cmd, ERR_RANGE)
                return
            start_ds, cycle_ds = struct.unpack("<HH", pl[:4])
            if not (1 <= start_ds <= 600 and 1 <= cycle_ds <= 600):
                log("MAP", f"SET_TIMES 범위 초과 start={start_ds} cycle={cycle_ds}")
                self.nak(sess, DEV_GFC, dev.did, seq, cmd, ERR_RANGE)
                return
            dev.start_ds, dev.cycle_ds = start_ds, cycle_ds
            log("MAP", f"SET_TIMES start={start_ds/10:.1f}s cycle={cycle_ds/10:.1f}s "
                       f"(DID={dev.did})")
            if dev.src_enable:                      # 동작 중이면 즉시 반영
                g.send(dev, G_SRC_SET, dev.src_set_payload(True))
            self.reply(sess, DEV_GFC, dev.did, C_ACK, seq, bytes([cmd]))
            return

        if cmd == C_GFC_AUTO_RUN:
            if len(pl) < 1:
                self.nak(sess, DEV_GFC, dev.did, seq, cmd, ERR_RANGE)
                return
            run = bool(pl[0])
            if run:
                dev.cycle_count = 0
                log("MAP", f"AUTO_RUN START -> SRC_SET(enable=1, "
                           f"init={dev.start_ds/10:.1f}s, cycle={dev.cycle_ds/10:.1f}s, "
                           f"period={CYCLE_PERIOD_SEC:.0f}s) DID={dev.did}")
            else:
                log("MAP", f"AUTO_RUN STOP -> SRC_SET(enable=0) + 전 펌프 Off "
                           f"DID={dev.did}")
            g.send(dev, G_SRC_SET, dev.src_set_payload(run))
            if not run:
                g.send(dev, G_PUMP_SET, bytes([0, 0, 0, 0]))
            self.reply(sess, DEV_GFC, dev.did, C_ACK, seq, bytes([cmd]))
            return

    # 콘솔 AOS 명령 → AOS 패킷 ------------------------------------------
    def handle_aos_cmd(self, sess, dev, cmd, seq, pl):
        """콘솔 0x40~0x43 과 브리지 0x40~0x43 은 번호·payload 가 같다.
        번호를 맞춰 둔 덕에 변환이랄 게 거의 없다."""
        a = self.h["aos"]

        if dev.did in CAL_BLOCK and cmd in (C_AOS_SET_PARAM, C_AOS_SET_LF_MODE, C_AOS_SET_LF_SHAPE):
            log("MAP", f"AOS {dev.did} 캘리브레이션 중 — {CONSOLE_CMD_NAME.get(cmd, hex(cmd))} 거부")
            self.nak(sess, DEV_AOS, dev.did, seq, cmd, ERR_BUSY)
            return

        if cmd == C_AOS_SET_PARAM:
            # u8 param_id, u8 pad[3], f32 value — 8 byte 그대로 통과
            if len(pl) < 8:
                self.nak(sess, DEV_AOS, dev.did, seq, cmd, ERR_RANGE)
                return
            pid = pl[0]
            value = struct.unpack_from("<f", pl, 4)[0]
            if pid >= len(PARAM_NAME):
                log("MAP", f"AOS_SET_PARAM 알 수 없는 id={pid}")
                self.nak(sess, DEV_AOS, dev.did, seq, cmd, ERR_RANGE)
                return
            log("MAP", f"AOS_SET_PARAM {PARAM_NAME[pid]}={value:g} DID={dev.did}")
            a.send(dev, A_PARAM_SET, pl[:8])
            self.reply(sess, DEV_AOS, dev.did, C_ACK, seq, bytes([cmd]))
            return

        if cmd == C_AOS_SET_LF_MODE:
            if len(pl) < 1:
                self.nak(sess, DEV_AOS, dev.did, seq, cmd, ERR_RANGE)
                return
            on = 1 if pl[0] else 0
            log("MAP", f"AOS_SET_LF_MODE {'ON' if on else 'OFF'} DID={dev.did}")
            a.send(dev, A_LF_MODE, bytes([on, 0, 0, 0]))
            self.reply(sess, DEV_AOS, dev.did, C_ACK, seq, bytes([cmd]))
            return

        if cmd == C_AOS_SET_LF_SHAPE:
            if len(pl) < 1:
                self.nak(sess, DEV_AOS, dev.did, seq, cmd, ERR_RANGE)
                return
            log("MAP", f"AOS_SET_LF_SHAPE {pl[0]} DID={dev.did}")
            a.send(dev, A_LF_SHAPE, bytes([pl[0], 0, 0, 0]))
            self.reply(sess, DEV_AOS, dev.did, C_ACK, seq, bytes([cmd]))
            return

        if cmd == C_AOS_GET_PARAMS:
            log("MAP", f"AOS_GET_PARAMS DID={dev.did}")
            if dev.params is not None:
                # 이미 알고 있는 값을 먼저 돌려주고 (콘솔이 바로 그릴 수 있게)
                self.reply(sess, DEV_AOS, dev.did, C_AOS_PARAMS, seq, dev.params)
            a.send(dev, A_PARAMS_QUERY, b"")    # 최신값도 새로 물어본다
            return

    # 주기 push ---------------------------------------------------------
    def push_states(self):
        """GFC_AUTO_STATE(0xA3) 를 연결된 콘솔에 500 ms 주기로 보낸다."""
        dead = [a for a, s in CONSOLES.items() if not s.alive]
        for a in dead:
            del CONSOLES[a]

        for sess in CONSOLES.values():
            if sess.dev_type != DEV_GFC:
                continue
            dev = get_gfc(sess.dev_id, create=False)
            if dev is None or not dev.online:
                continue
            # pump 는 장비가 보고한 실제 Pump2 상태다.
            # src_on(주입 시퀀스 분사 중)을 쓰면 MANUAL 모드에서 항상 0 이 되어
            # 콘솔의 Pump 버튼 상태를 500ms 마다 OFF 로 덮어쓴다.
            payload = struct.pack("<BBHI",
                                  1 if dev.src_enable else 0,
                                  1 if dev.pump2 else 0,
                                  dev.remain_ds,
                                  dev.cycle_count)
            self.push(sess, DEV_GFC, dev.did, C_GFC_AUTO_STATE, payload)


class AosProtocol(asyncio.DatagramProtocol):
    """5500 — AOS 장치와의 바이너리 채널 (프레임은 GFC 와 동일, DTYPE=1)"""

    def connection_made(self, transport):
        self.transport = transport
        log("SERVER", f"AOS UDP server started on port {PORT_AOS}")

    # 하향 송신 --------------------------------------------------------
    def send(self, dev: AosDevice, cmd: int, payload: bytes = b"") -> bool:
        if dev.addr is None:
            log("AOS", f"DID={dev.did} 주소 미확인 — cmd 0x{cmd:02X} 버림")
            return False
        seq = dev.next_seq() or dev.next_seq()      # 0 은 건너뛴다 (반환값을 bool 로 쓰는 호출부)
        pkt = gfc_build(DTYPE_AOS, dev.did, cmd, seq, payload)
        self.transport.sendto(pkt, dev.addr)
        self.last_seq = seq
        if cmd in (A_PARAM_SET, A_LF_MODE, A_LF_SHAPE):
            _pending_add("aos", self, dev, cmd, seq, pkt)
        log("AOS", f"TX-> DID={dev.did} {AOS_CMD_NAME.get(cmd, hex(cmd))} "
                   f"seq={seq} {pkt.hex(' ') if len(pkt) <= 48 else f'{len(pkt)}B'}")
        return seq

    def ack(self, dev, ack_cmd, ack_seq):
        payload = struct.pack("<BBH", ack_cmd, 0, ack_seq)
        pkt = gfc_build(DTYPE_AOS, dev.did, A_ACK, ack_seq, payload)
        if dev.addr:
            self.transport.sendto(pkt, dev.addr)

    # 수신 -------------------------------------------------------------
    def datagram_received(self, data, addr):
        f = gfc_parse(data)         # 프레임 규격이 같으므로 같은 파서를 쓴다
        if f is None:
            _emit("bad", "aos", addr, data)
            try:
                log("AOS", f"{addr[0]}:{addr[1]} 규격 외 -> {data.decode().strip()}")
            except UnicodeDecodeError:
                log("AOS", f"{addr[0]}:{addr[1]} 버림 {len(data)}B HEX:{data.hex()}")
            return

        if f["dtype"] != DTYPE_AOS:
            return

        dev = get_aos(f["did"])
        first = dev.addr is None
        if dev.addr != addr:
            dev.addr = addr
        dev.last_seen = time.time()
        if first:
            log("AOS", f"NEW DEVICE DID={dev.did} {addr[0]}:{addr[1]}")
        _emit("rx", "aos", dev, addr, first)

        cmd = f["cmd"]
        name = AOS_CMD_NAME.get(cmd, hex(cmd))

        if cmd == A_PARAMS:
            if len(f["payload"]) >= 26:
                raw = bytes(f["payload"][:26])
                if dev.apply_params(raw):
                    hv, frq, duty, cv, lff, lfv, lf_on, shape = struct.unpack(
                        AOS_PARAMS_FMT, raw)
                    log("AOS", f"PARAMS DID={dev.did} HV={hv:.2f} FRQ={frq:.1f} "
                               f"DUTY={duty:.2f} CV={cv:.3f} LF={'ON' if lf_on else 'OFF'} "
                               f"LFF={lff:.0f} LFV={lfv:.2f} shape={shape}")
                    _emit("aos_params", dev)
                    # 값이 바뀌었을 때만 콘솔로 내린다. 같은 값을 주기적으로
                    # 되쏘면 사용자가 jog 를 돌리는 중에 화면이 되돌아간다.
                    con = HOLDER.get("console")
                    if con:
                        try:
                            con.push_aos_params(dev)
                        except Exception as e:
                            log("SERVER", f"AOS push 실패: {e!r}")
            self.ack(dev, cmd, f["seq"])

        elif cmd == A_STATUS:
            if len(f["payload"]) >= 24:
                (ts, up, ap, an, gp, gn, pc, flags, rssi, err) = struct.unpack(
                    AOS_STATUS_FMT, f["payload"][:24])
                dev.uptime = up
                dev.status = dict(ts=ts, uptime=up, air_p=ap, air_n=an,
                                  gas_p=gp, gas_n=gn, point_count=pc,
                                  flags=flags, rssi=rssi, err=err)
                _emit("aos_status", dev)
                if LOG_HEX:
                    log("AOS", f"DID={dev.did} STATUS flags=0x{flags:02X} "
                               f"err=0x{err:04X} rssi={rssi}")
            self.ack(dev, cmd, f["seq"])

        elif cmd == A_HELLO:
            h = parse_hello(f["payload"])
            if h:
                log("AOS", f"HELLO DID={dev.did} model={h['model']} fw={h['fw']} "
                           f"mac={h['mac']} 자기보고IP={h['ip']} 실제={addr[0]}")
                dev.claimed_ip = h["ip"]
                check_ip_conflict("AOS", dev, addr, h["ip"])
                _emit("hello", "aos", dev, h, addr)
            else:
                log("AOS", f"HELLO DID={dev.did} (짧은 payload) from {addr[0]}")
            self.ack(dev, cmd, f["seq"])

        elif cmd == A_EVENT:
            if len(f["payload"]) >= 16:
                _ts, code, _rsv, a, _b = struct.unpack("<IHHII", f["payload"][:16])
                log("AOS", f"EVENT DID={dev.did} "
                           f"{AOS_EVENT_NAME.get(code, hex(code))} a=0x{a:02X}")
                _emit("dev_event", "aos", dev, code, a)
            self.ack(dev, cmd, f["seq"])

        elif cmd == A_ACK:
            if len(f["payload"]) >= 4:
                ack_cmd, result, ack_seq = struct.unpack("<BBH", f["payload"][:4])
                _pending_ack("aos", dev.did, ack_seq, ack_cmd, result)
                _emit("aos_ack", dev, ack_cmd, result, ack_seq)
                if result:
                    log("AOS", f"ACK  DID={dev.did} for "
                               f"{AOS_CMD_NAME.get(ack_cmd, hex(ack_cmd))} "
                               f"result={result} seq={ack_seq}")

        elif cmd in AOS_CAL_RESP:          # 캘리브레이션 응답 — ACK 없이 gts/cal.py 로
            _emit("aos_cal", dev, cmd, f["seq"], bytes(f["payload"]))

        else:
            log("AOS", f"DID={dev.did} {name} seq={f['seq']} size={len(f['payload'])}")


# ────────────────────────────────────────────────────────────────────
# main
# ────────────────────────────────────────────────────────────────────

async def push_loop(console_proto):
    while True:
        await asyncio.sleep(PUSH_PERIOD)
        try:
            console_proto.push_states()
        except Exception as e:      # 주기 루프는 어떤 일이 있어도 죽지 않는다
            log("SERVER", f"push_states error: {e!r}")


async def retry_loop():
    """하향 제어 명령 ACK 확인 → 재전송 (0.1초 주기)"""
    while True:
        await asyncio.sleep(0.1)
        try:
            retry_pending()
        except Exception as e:
            log("SERVER", f"retry error: {e!r}")


async def main():
    print("")
    print("========================================")
    print("       GTS UDP SERVER  (rev.2 binary)")
    print("========================================")
    print(f"{PORT_AOS} : AOS          (binary 0x40~0x4F)")
    print(f"{PORT_GFC} : GFC          (binary 0x30~0x3A)")
    print(f"{PORT_CONSOLE} : Console      (binary 0x01~0xC3)")
    print("========================================")
    print("")

    loop = asyncio.get_running_loop()
    holder = {}

    t_aos, aos_proto = await loop.create_datagram_endpoint(
        AosProtocol, local_addr=("0.0.0.0", PORT_AOS))
    holder["aos"] = aos_proto

    t_gfc, gfc_proto = await loop.create_datagram_endpoint(
        GfcProtocol, local_addr=("0.0.0.0", PORT_GFC))
    holder["gfc"] = gfc_proto

    t_con, con_proto = await loop.create_datagram_endpoint(
        lambda: ConsoleProtocol(holder), local_addr=("0.0.0.0", PORT_CONSOLE))
    HOLDER["console"] = con_proto      # GFC/AOS 쪽에서 즉시 push 할 때 쓴다

    task = asyncio.create_task(push_loop(con_proto))
    rtask = asyncio.create_task(retry_loop())

    try:
        await asyncio.Future()
    finally:
        task.cancel()
        rtask.cancel()
        for t in (t_aos, t_gfc, t_con):
            t.close()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[SERVER] stopped")
