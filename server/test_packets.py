#!/usr/bin/env python3
"""
test_packets.py — 3자(콘솔/서버/GFC) 프레임 정합성 검증

DOC/GTS_GFC_UDP.md 11.3 "검증된 예제 패킷" 과 DOC/GTS_UDP_Protocol.md 1-2
예시 프레임을 그대로 대조한다. 서버 코드를 고치면 이 스크립트를 먼저 돌릴 것.

    python3 test_packets.py
"""
import struct
import sys

import udp_server as S

fails = []


def check(name, got, want):
    ok = got == want
    print(f"{'PASS' if ok else 'FAIL'}  {name}")
    if not ok:
        print(f"      got  {got}")
        print(f"      want {want}")
        fails.append(name)


def hexs(b):
    return b.hex(" ").upper()


print("── CRC-16/MODBUS 표준 검사값 ─────────────────────────────")
check("crc16(b'123456789') == 0x4B37", hex(S.crc16(b"123456789")), hex(0x4B37))

print()
print("── 구조체 크기 (DOC 10.3) ───────────────────────────────")
check("sensor_data_t = 52", struct.calcsize(S.SENSOR_FMT), 52)
check("src_set_t = 16", struct.calcsize(S.SRC_SET_FMT), 16)

print()
print("── float 바이트 (DOC 10.3) ──────────────────────────────")
for v, want in ((3.0, "00 00 40 40"), (1.5, "00 00 C0 3F"),
                (30.0, "00 00 F0 41"), (600.0, "00 00 16 44")):
    check(f"float {v}", hexs(struct.pack("<f", v)), want)

print()
print("── GFC UDP 프레임 (DOC 11.3 검증된 예제) ────────────────")

# SRC_SET: DTYPE=2 DID=1 CMD=0x34 SEQ=90 init=30.0 cycle=1.5 period=600.0
p = S.gfc_build(2, 1, S.G_SRC_SET, 90,
                struct.pack(S.SRC_SET_FMT, 1, 30.0, 1.5, 600.0))
check("SRC_SET(30/1.5/600)", hexs(p),
      "02 02 01 34 5A 00 10 00 01 00 00 00 00 00 F0 41 "
      "00 00 C0 3F 00 00 16 44 D0 5D")

# PUMP_SET 전체 On: SEQ=0x4D
p = S.gfc_build(2, 1, S.G_PUMP_SET, 0x4D, bytes([1, 1, 1, 0]))
check("PUMP_SET(1,1,1)", hexs(p), "02 02 01 30 4D 00 04 00 01 01 01 00 9D 19")

print()
print("── GFC 프레임 왕복 (build → parse) ──────────────────────")
f = S.gfc_parse(p)
check("parse dtype", f["dtype"], 2)
check("parse did", f["did"], 1)
check("parse cmd", f["cmd"], S.G_PUMP_SET)
check("parse seq", f["seq"], 0x4D)
check("parse payload", hexs(f["payload"]), "01 01 01 00")
check("CRC 깨진 프레임 폐기", S.gfc_parse(p[:-1] + b"\x00"), None)
check("SIZE 조작 프레임 폐기", S.gfc_parse(p[:6] + b"\xff\x00" + p[8:]), None)
check("DID=0 폐기", S.gfc_parse(S.gfc_build(2, 0, 0x30, 1, b"\0\0\0\0")), None)
check("DID=21 폐기", S.gfc_parse(S.gfc_build(2, 21, 0x30, 1, b"\0\0\0\0")), None)

print()
print("── Console UDP 프레임 (DOC/GTS_UDP_Protocol.md 1-2) ─────")
# CONNECT: TYPE=AOS(1) ID=7 CMD=0x01 src=CONSOLE(3) my_id=1 seq=5
p = S.console_build(0x01, 7, S.C_CONNECT, 0x03, 1, 5)
check("CONNECT (AOS #7, my_id=1, seq=5)", hexs(p),
      "02 01 07 01 04 00 03 01 05 00 05 16")

c = S.console_parse(p)
check("console parse type", c["type"], 1)
check("console parse id", c["id"], 7)
check("console parse cmd", c["cmd"], S.C_CONNECT)
check("console parse seq", c["seq"], 5)
check("console CRC 깨짐 폐기", S.console_parse(p[:-1] + b"\x00"), None)
check("console SIZE 조작 폐기",
      S.console_parse(p[:4] + b"\xff\x00" + p[6:]), None)

print()
print("── Console → GFC 명령 변환 ──────────────────────────────")


class FakeTransport:
    def __init__(self):
        self.sent = []

    def sendto(self, data, addr):
        self.sent.append((addr, data))


gfc_tr, con_tr, aos_tr = FakeTransport(), FakeTransport(), FakeTransport()
gfc = S.GfcProtocol()
gfc.connection_made(gfc_tr)
aos = S.AosProtocol()
aos.connection_made(aos_tr)
holder = {"gfc": gfc, "aos": aos}
con = S.ConsoleProtocol(holder)
con.connection_made(con_tr)

# GFC #1 이 살아 있는 것처럼 만든다 (SENSOR_DATA 한 번 수신)
sensor = struct.pack(S.SENSOR_FMT,
                     100,                       # ts
                     1.0, 2.0, 0.0, 0.0, 0.0,   # volt1 volt2 ctrl slope sv
                     12.3, 4.5,                 # src_remain src_elapsed
                     100,                       # uptime
                     1000, 2000, 0, 0,          # raw1 raw2 co2_1 co2_2
                     1, 0, 0,                   # pump1 pump2 pump3
                     S.FLAG_SRC_ENABLE | S.FLAG_UART_OK,
                     0, -55, 0)
gfc.datagram_received(S.gfc_build(2, 1, S.G_SENSOR_DATA, 1, sensor),
                      ("192.168.0.77", 5501))
dev = S.get_gfc(1, create=False)
check("GFC #1 온라인", dev is not None and dev.online, True)
check("remain_ds = 123", dev.remain_ds, 123)
check("dev_state = RUN(1)", dev.dev_state, 1)

CON_ADDR = ("192.168.0.99", 40000)

def console_send(cmd, payload=b"", dev_type=S.DEV_GFC, dev_id=1, seq=1):
    gfc_tr.sent.clear()
    con_tr.sent.clear()
    con.datagram_received(
        S.console_build(dev_type, dev_id, cmd, S.DEV_CONSOLE, 1, seq, payload),
        CON_ADDR)


def last_gfc():
    return S.gfc_parse(gfc_tr.sent[-1][1]) if gfc_tr.sent else None


def last_console():
    return S.console_parse(con_tr.sent[-1][1]) if con_tr.sent else None


# CONNECT
console_send(S.C_CONNECT, seq=11)
r = last_console()
check("CONNECT → CONNECT_ACK", r["cmd"], S.C_CONNECT_ACK)
check("CONNECT_ACK seq 반사", r["seq"], 11)
check("CONNECT_ACK result=0", r["payload"][0], 0)
check("CONNECT_ACK 길이 12", len(r["payload"]), 12)

# PING
console_send(S.C_PING, seq=12)
r = last_console()
check("PING → PONG", r["cmd"], S.C_PONG)
check("PONG seq 반사", r["seq"], 12)

# SET_PUMP On
console_send(S.C_GFC_SET_PUMP, bytes([1]), seq=13)
g = last_gfc()
check("SET_PUMP(1) → GFC PUMP_SET", g["cmd"], S.G_PUMP_SET)
check("PUMP_SET payload (1,1,1,0)", hexs(g["payload"]), "01 01 01 00")
check("PUMP_SET DID=1", g["did"], 1)

# SET_PUMP Off
console_send(S.C_GFC_SET_PUMP, bytes([0]), seq=14)
check("SET_PUMP(0) payload", hexs(last_gfc()["payload"]), "00 00 00 00")

# SET_TIMES 30.0s / 1.5s  (deciseconds 300 / 15)
console_send(S.C_GFC_SET_TIMES, struct.pack("<HH", 300, 15), seq=15)
check("SET_TIMES 저장 start_ds", dev.start_ds, 300)
check("SET_TIMES 저장 cycle_ds", dev.cycle_ds, 15)

# SET_TIMES 범위 초과 → NAK
console_send(S.C_GFC_SET_TIMES, struct.pack("<HH", 0, 700), seq=16)
r = last_console()
check("SET_TIMES 범위초과 → NAK", r["cmd"], S.C_NAK)
check("NAK err_code = RANGE", r["payload"][1], S.ERR_RANGE)

# AUTO_RUN START → SRC_SET(30.0 / 1.5 / 600)
console_send(S.C_GFC_AUTO_RUN, bytes([1]), seq=17)
g = last_gfc()
check("AUTO_RUN(1) → GFC SRC_SET", g["cmd"], S.G_SRC_SET)
en, init_s, cyc_s, per_s = struct.unpack(S.SRC_SET_FMT, g["payload"])
check("SRC_SET enable", en, 1)
check("SRC_SET init_on_sec", init_s, 30.0)
check("SRC_SET cycle_on_sec", cyc_s, 1.5)
check("SRC_SET period_sec", per_s, 600.0)
check("SRC_SET 바이트열", hexs(g["payload"]),
      "01 00 00 00 00 00 F0 41 00 00 C0 3F 00 00 16 44")

# AUTO_RUN STOP → SRC_SET(enable=0) + PUMP_SET 전부 Off
console_send(S.C_GFC_AUTO_RUN, bytes([0]), seq=18)
sent = [S.gfc_parse(d) for _, d in gfc_tr.sent]
check("AUTO_RUN(0) 패킷 2개", len(sent), 2)
check("  ① SRC_SET enable=0", (sent[0]["cmd"], sent[0]["payload"][0]),
      (S.G_SRC_SET, 0))
check("  ② PUMP_SET 전부 Off", (sent[1]["cmd"], hexs(sent[1]["payload"])),
      (S.G_PUMP_SET, "00 00 00 00"))

# SET_MODE MANUAL → SRC_SET(enable=0)
console_send(S.C_GFC_SET_MODE, bytes([0]), seq=19)
g = last_gfc()
check("SET_MODE(MANUAL) → SRC_SET stop", (g["cmd"], g["payload"][0]),
      (S.G_SRC_SET, 0))

# 오프라인 GFC → NAK
console_send(S.C_GFC_SET_PUMP, bytes([1]), dev_id=7, seq=20)
r = last_console()
check("오프라인 DID=7 → NAK NO_DEVICE",
      (r["cmd"], r["payload"][1]), (S.C_NAK, S.ERR_NO_DEVICE))

print()
print("── cycle_count — 초기 주입은 세지 않는다 ────────────────")


def feed(flags, remain=0.0, pump2=None):
    """SENSOR_DATA 한 발을 서버에 밀어 넣는다."""
    if pump2 is None:
        pump2 = 1 if flags & S.FLAG_SRC_ON else dev.pump2
    pkt = struct.pack(S.SENSOR_FMT, 0, 0., 0., 0., 0., 0., remain, 0., 0,
                      0, 0, 0, 0, 1, pump2, 0,
                      flags, 0, -50, 0)
    gfc.datagram_received(S.gfc_build(2, 1, S.G_SENSOR_DATA, 1, pkt),
                          ("192.168.0.77", 5501))


EN, ON, INIT, UOK = (S.FLAG_SRC_ENABLE, S.FLAG_SRC_ON,
                     S.FLAG_SRC_INIT, S.FLAG_UART_OK)
dev.cycle_count = 0
dev._prev_src_on = False
feed(UOK)                              # 대기
feed(EN | ON | INIT | UOK)             # 초기 주입 시작
feed(EN | ON | INIT | UOK)             # 초기 주입 계속
check("초기 주입은 0회", dev.cycle_count, 0)
feed(EN | UOK, 599.0)                  # 초기 주입 끝, 대기
check("초기 주입 종료 후에도 0회", dev.cycle_count, 0)
feed(EN | ON | UOK)                    # 1차 주기 분사
check("1차 주기 분사 → 1", dev.cycle_count, 1)
feed(EN | ON | UOK)                    # 같은 분사가 이어짐
check("같은 분사는 중복 카운트 안 함", dev.cycle_count, 1)
feed(EN | UOK, 598.0)
feed(EN | ON | UOK)                    # 2차
check("2차 주기 분사 → 2", dev.cycle_count, 2)
feed(UOK)                              # 정지
check("정지해도 누적 유지", dev.cycle_count, 2)

print()
print("── 서버 → 콘솔 주기 통지 (0xA3) ─────────────────────────")
sess = S.CONSOLES[CON_ADDR]
sess.dev_type, sess.dev_id = S.DEV_GFC, 1


def push():
    con_tr.sent.clear()
    con.push_states()
    return struct.unpack("<BBHI", last_console()["payload"])


dev.src_enable, dev.src_on, dev.pump2 = True, True, 1
dev.remain_ds, dev.cycle_count = 4567, 9
con_tr.sent.clear()
con.push_states()
r = last_console()
check("GFC_AUTO_STATE cmd", r["cmd"], S.C_GFC_AUTO_STATE)
check("GFC_AUTO_STATE 길이 8", len(r["payload"]), 8)
run, pump, remain, cnt = struct.unpack("<BBHI", r["payload"])
check("  run", run, 1)
check("  pump", pump, 1)
check("  remain_ds", remain, 4567)
check("  cycle_count", cnt, 9)

print()
print("── pump 필드는 실제 펌프 상태다 (MANUAL 회귀) ───────────")
# 증상: MANUAL 로 Pump On 했는데 서버가 src_on(=0) 을 보내면 콘솔의 버튼이
#       500ms 만에 OFF 로 되돌아가고, 두 번째 누름이 같은 값을 다시 보내
#       아무 일도 일어나지 않는다.
dev.cycle_count = 0
dev._prev_src_on = False
feed(UOK, pump2=0)                          # 시퀀스 정지 + 펌프 Off 로 초기화
check("  초기 상태 pump2=0", dev.pump2, 0)
console_send(S.C_GFC_SET_PUMP, bytes([1]), seq=30)
g = last_gfc()
check("MANUAL Pump On → PUMP_SET(1,1,1)", hexs(g["payload"]), "01 01 01 00")
feed(UOK, pump2=1)                          # 장비가 pump2=1 로 보고 (src_on=0)
check("  장비 보고 pump2", dev.pump2, 1)
check("  src_on 은 0", dev.src_on, False)
run, pump, remain, cnt = push()
check("  push 의 run=0", run, 0)
check("  ★ push 의 pump=1 (src_on 아님)", pump, 1)

console_send(S.C_GFC_SET_PUMP, bytes([0]), seq=31)
check("MANUAL Pump Off → PUMP_SET(0,0,0)",
      hexs(last_gfc()["payload"]), "00 00 00 00")
feed(UOK, pump2=0)
run, pump, remain, cnt = push()
check("  push 의 pump=0", pump, 0)

print()
print("── 상태 변화 시 즉시 push ───────────────────────────────")
S.HOLDER["console"] = con
dev.pump2 = 0
dev.src_enable = False
dev._prev_src_on = False
con_tr.sent.clear()
feed(UOK, pump2=0)                          # 변화 없음
check("변화 없으면 즉시 push 안 함", len(con_tr.sent), 0)
con_tr.sent.clear()
feed(UOK, pump2=1)                          # pump2 0 → 1
check("pump2 변하면 즉시 push", len(con_tr.sent), 1)
_, pump, _, _ = struct.unpack("<BBHI", last_console()["payload"])
check("  즉시 push 의 pump=1", pump, 1)
con_tr.sent.clear()
feed(EN | UOK, 599.0, pump2=1)              # src_enable 0 → 1
check("run 변해도 즉시 push", len(con_tr.sent), 1)
run, _, _, _ = struct.unpack("<BBHI", last_console()["payload"])
check("  즉시 push 의 run=1", run, 1)
S.HOLDER.pop("console", None)

print()
print("── AOS 구조체 / 프레임 ──────────────────────────────────")
check("aos_params_t = 26", struct.calcsize(S.AOS_PARAMS_FMT), 26)
check("aos_status_t = 24", struct.calcsize(S.AOS_STATUS_FMT), 24)

AOS_ADDR = ("192.168.0.55", 5500)


def aos_feed(cmd, payload):
    aos_tr.sent.clear()
    con_tr.sent.clear()
    aos.datagram_received(S.gfc_build(S.DTYPE_AOS, 1, cmd, 1, payload), AOS_ADDR)


def aos_params_bytes(hv=120.0, frq=350.0, duty=25.0, cv=-1.5,
                     lff=120.0, lfv=2.5, lf_on=1, shape=0):
    return struct.pack(S.AOS_PARAMS_FMT, hv, frq, duty, cv, lff, lfv, lf_on, shape)


def aos_status_bytes(flags=S.AOS_FLAG_UART_OK, err=0):
    return struct.pack(S.AOS_STATUS_FMT, 10, 10, 1, 2, 3, 4, 5, flags, -50, err)


aos_feed(S.A_STATUS, aos_status_bytes())
adev = S.get_aos(1, create=False)
check("AOS #1 온라인", adev is not None and adev.online, True)
check("AOS dev_state IDLE(0)", adev.dev_state, 0)
aos_feed(S.A_STATUS, aos_status_bytes(flags=S.AOS_FLAG_UART_OK | S.AOS_FLAG_APPLIED))
check("설정 적용 후 RUN(1)", adev.dev_state, 1)
aos_feed(S.A_STATUS, aos_status_bytes(err=S.AOS_ERR_UART_LOST))
check("STM32 끊기면 ERROR(2)", adev.dev_state, 2)
aos_feed(S.A_STATUS, aos_status_bytes())

print()
print("── Console ↔ AOS 명령 변환 ──────────────────────────────")
sess.dev_type, sess.dev_id = S.DEV_AOS, 1


def aos_console_send(cmd, payload=b"", seq=1, dev_id=1):
    aos_tr.sent.clear()
    con_tr.sent.clear()
    con.datagram_received(
        S.console_build(S.DEV_AOS, dev_id, cmd, S.DEV_CONSOLE, 1, seq, payload),
        CON_ADDR)


def last_aos():
    return S.gfc_parse(aos_tr.sent[-1][1]) if aos_tr.sent else None


# CONNECT (AOS)
aos_console_send(S.C_CONNECT, seq=40)
r = last_console()
check("AOS CONNECT → CONNECT_ACK", r["cmd"], S.C_CONNECT_ACK)
check("  result=0", r["payload"][0], 0)

# SET_PARAM — 콘솔 8 byte 를 그대로 브리지로
for pid, val in ((0, 123.45), (1, 350.0), (2, 25.5), (3, -1.25),
                 (4, 120.0), (5, 2.5)):
    pl = bytes([pid, 0, 0, 0]) + struct.pack("<f", val)
    aos_console_send(S.C_AOS_SET_PARAM, pl, seq=41 + pid)
    g = last_aos()
    got = struct.unpack_from("<f", g["payload"], 4)[0]
    check(f"SET_PARAM {S.PARAM_NAME[pid]}={val:g} → 브리지 0x40",
          (g["cmd"], g["payload"][0], round(got, 4)),
          (S.A_PARAM_SET, pid, round(val, 4)))

# 알 수 없는 param_id → NAK
aos_console_send(S.C_AOS_SET_PARAM, bytes([9, 0, 0, 0]) + struct.pack("<f", 1.0), seq=50)
r = last_console()
check("param_id=9 → NAK RANGE", (r["cmd"], r["payload"][1]), (S.C_NAK, S.ERR_RANGE))

# LF On/Off
aos_console_send(S.C_AOS_SET_LF_MODE, bytes([1]), seq=51)
g = last_aos()
check("LF_MODE(1) → 브리지 0x41", (g["cmd"], hexs(g["payload"])),
      (S.A_LF_MODE, "01 00 00 00"))
aos_console_send(S.C_AOS_SET_LF_MODE, bytes([0]), seq=52)
check("LF_MODE(0)", hexs(last_aos()["payload"]), "00 00 00 00")

# LF Shape
aos_console_send(S.C_AOS_SET_LF_SHAPE, bytes([2]), seq=53)
g = last_aos()
check("LF_SHAPE(2) → 브리지 0x42", (g["cmd"], hexs(g["payload"])),
      (S.A_LF_SHAPE, "02 00 00 00"))

# 오프라인 AOS → NAK
aos_console_send(S.C_AOS_SET_LF_MODE, bytes([1]), dev_id=9, seq=54)
r = last_console()
check("오프라인 DID=9 → NAK NO_DEVICE",
      (r["cmd"], r["payload"][1]), (S.C_NAK, S.ERR_NO_DEVICE))

print()
print("── AOS_PARAMS(0x44) → 콘솔 AOS_PARAMS(0xC3) ─────────────")
S.HOLDER["console"] = con
# 직전 테스트가 DID=9 로 보냈으므로 세션 타깃을 되돌린다
sess.dev_type, sess.dev_id = S.DEV_AOS, 1
raw = aos_params_bytes(hv=120.0, frq=350.0, duty=25.0, cv=-1.5,
                       lff=120.0, lfv=2.5, lf_on=1, shape=3)
aos_feed(S.A_PARAMS, raw)
r = last_console()
check("콘솔로 0xC3 전달", r["cmd"], S.C_AOS_PARAMS)
check("  payload 26 byte 그대로", hexs(r["payload"]), hexs(raw))
hv, frq, duty, cv, lff, lfv, lf_on, shape = struct.unpack(S.AOS_PARAMS_FMT, r["payload"])
check("  HV", hv, 120.0)
check("  CV", cv, -1.5)
check("  LF_VOLT", lfv, 2.5)
check("  lf_on", lf_on, 1)
check("  lf_shape", shape, 3)

con_tr.sent.clear()
aos.datagram_received(S.gfc_build(S.DTYPE_AOS, 1, S.A_PARAMS, 2, raw), AOS_ADDR)
check("같은 값이면 다시 안 내린다 (jog 충돌 방지)", len(con_tr.sent), 0)
raw2 = aos_params_bytes(hv=121.0)
con_tr.sent.clear()
aos.datagram_received(S.gfc_build(S.DTYPE_AOS, 1, S.A_PARAMS, 3, raw2), AOS_ADDR)
check("값이 바뀌면 내린다", len(con_tr.sent), 1)
S.HOLDER.pop("console", None)

print()
print("── AOS STM32 UART 프레임 (SIZE 2 byte) ──────────────────")


def aos_uart_frame(cmd, data=b""):
    """STM32 TxMSG_PC() 와 같은 규칙: SIZE 2byte, CHKSUM = ~(CMD+SIZE+DATA)"""
    body = bytes([cmd, len(data) % 256, len(data) // 256]) + data
    return bytes([0x02]) + body + bytes([(~sum(body)) & 0xFF])


# FW_RS232_Protocol.md 1-4 의 예시: 02 84 00 00 7B
check("0x84 SCAN_ABORT 예시", hexs(aos_uart_frame(0x84)), "02 84 00 00 7B")
check("0x02 SET_QUERY 요청", hexs(aos_uart_frame(0x02)), "02 02 00 00 FD")

# 0x2A CMD_SET_CONTROL — HV, Frq, Duty, CV 순서
ctl = struct.pack("<4f", 120.0, 350.0, 25.0, -1.5)
f2a = aos_uart_frame(0x2A, ctl)
check("0x2A 길이 = 5 + 16", len(f2a), 21)
check("0x2A SIZE 필드", hexs(f2a[2:4]), "10 00")
check("0x2A HV 바이트", hexs(f2a[4:8]), hexs(struct.pack("<f", 120.0)))
check("0x2A CV 바이트(마지막)", hexs(f2a[16:20]), hexs(struct.pack("<f", -1.5)))
check("0x2A 체크섬", f2a[-1], (~sum(f2a[1:-1])) & 0xFF)

# 0x52 CMD_LF_MOD_SET — type, OnOff, amp(f32), frq(f32)
lf = bytes([3, 1]) + struct.pack("<ff", 2.5, 120.0)
f52 = aos_uart_frame(0x52, lf)
check("0x52 길이 = 5 + 10", len(f52), 15)
check("0x52 type/OnOff", hexs(f52[4:6]), "03 01")
check("0x52 amp", hexs(f52[6:10]), hexs(struct.pack("<f", 2.5)))
check("0x52 frq", hexs(f52[10:14]), hexs(struct.pack("<f", 120.0)))

# 0x02 응답 32 byte — CV 가 2번째라는 점이 함정
q = (struct.pack("<f", 120.0) + struct.pack("<f", -1.5)
     + struct.pack("<f", 350.0) + struct.pack("<f", 25.0)
     + bytes([0]) + struct.pack("<f", 0.0)
     + bytes([1, 3]) + struct.pack("<f", 2.5) + struct.pack("<f", 120.0)
     + bytes([0]))
check("0x02 응답 = 32 byte", len(q), 32)
check("  offset 0 HV", struct.unpack_from("<f", q, 0)[0], 120.0)
check("  offset 4 CV ★순서주의", struct.unpack_from("<f", q, 4)[0], -1.5)
check("  offset 8 Frq", struct.unpack_from("<f", q, 8)[0], 350.0)
check("  offset 12 Duty", struct.unpack_from("<f", q, 12)[0], 25.0)
check("  offset 21 LF OnOff", q[21], 1)
check("  offset 22 LF type", q[22], 3)
check("  offset 23 LF amp (비정렬)", struct.unpack_from("<f", q, 23)[0], 2.5)
check("  offset 27 LF frq (비정렬)", struct.unpack_from("<f", q, 27)[0], 120.0)

print()
print("── UART 체크섬 (DOC 11.2/11.3) ──────────────────────────")


def uart_frame(cmd, data=b""):
    body = bytes([cmd, len(data)]) + data
    return bytes([0x02]) + body + bytes([255 - (sum(body) % 256)])


check("UART Pump1 On", hexs(uart_frame(0x70, bytes([1, 0, 0]))),
      "02 70 03 01 00 00 8B")
check("UART 0x72 요청", hexs(uart_frame(0x72)), "02 72 00 8D")

print()
if fails:
    print(f"❌ {len(fails)} FAILED: " + ", ".join(fails))
    sys.exit(1)
print("✅ 전부 통과")
