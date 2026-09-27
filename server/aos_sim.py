#!/usr/bin/env python3
"""
aos_sim.py — AOS ID=1 브리지 흉내내기 (ESP32 없이 서버를 검증할 때)

    python3 aos_sim.py [서버IP] [DID] [로컬포트]
    python3 aos_sim.py 192.168.0.6 1
    python3 aos_sim.py 127.0.0.1 1 6500   # 서버와 같은 PC 에서 돌릴 때

콘솔 P4 Manual 7종 — HV·FRQ·DUTY·CV·LF On/Off·LF_FRQ·LF_VOLT.
캘리브레이션 0x46~0x4E (2026-09-27) — STM32 보정 테이블·HV/CV 출력 모델 포함.
DMM 흉내: 환경변수 AOS_SIM_DMM=5025 이면 TCP 로 Keysight 34461A SCPI 를 흉내 낸다
  (CONF:VOLT:DC 1000 → HV 출력, 10 → CV 출력을 읽음).  서버 .env 에 GTS_DMM_HOST=127.0.0.1
  AOS_SIM_CALNO=40 이면 F/W CAL_NO 40 (지금 F/W) — 101 포인트 쓰기가 되읽기에서 틀어진다
서버가 0x40~0x43 을 내려보내면 섀도를 갱신하고 0x44 AOS_PARAMS 로 되올린다
(실제 브리지가 STM32 0x2A/0x52 를 쏘고 0x02 로 되읽는 자리다).
"""
import socket
import os
import struct
import sys
import time

sys.path.insert(0, ".")
from udp_server import (gfc_build, gfc_parse, AOS_PARAMS_FMT, AOS_STATUS_FMT,
                        A_HELLO, A_ACK, A_EVENT, A_PING, A_PARAM_SET,
                        A_LF_MODE, A_LF_SHAPE, A_PARAMS_QUERY, A_PARAMS,
                        A_STATUS, AOS_CMD_NAME, PARAM_NAME,
                        AOS_FLAG_LF_ON, AOS_FLAG_APPLIED, AOS_FLAG_UART_OK,
                        DTYPE_AOS, A_CAL_MODE, A_CAL_WRITE, A_CAL_QUERY, A_CAL_TABLE,
                        A_CAL_SENSE_Q, A_CAL_SENSE, AOS_CAL_SENSE_FMT)
import math
import random
import threading
from gts import caltable as CT

SERVER = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.6"
DID = int(sys.argv[2]) if len(sys.argv) > 2 else 1
PORT = 5500
LOCAL_PORT = int(sys.argv[3]) if len(sys.argv) > 3 else PORT

# 섀도 — aos_ctrl.c 의 기본값과 같다
P = [50.0, 500.0, 50.0, 0.0, 200.0, 2.5]        # HV FRQ DUTY CV LF_FRQ LF_VOLT
RANGE = [(0, 200), (200, 800), (20, 80), (-5, 5), (50, 200), (0, 5)]
lf_on, lf_shape, applied = 0, 0, False
# 펌웨어의 AOS_LF_SHAPE_FIXED 와 같다 — STM32 파형은 6(eSquare) 고정이라
# 콘솔이 무엇을 고르든 SQUARE(0) 로 보고한다.
LF_SHAPE_FIXED = True
seq = 0


def nseq():
    global seq
    seq = (seq + 1) & 0xFFFF
    return seq


def params_bytes():
    return struct.pack(AOS_PARAMS_FMT, P[0], P[1], P[2], P[3], P[4], P[5],
                       lf_on, lf_shape)


# ── STM32 보정 테이블 + 하드웨어 모델 (캘리브레이션 시험용) ─────────────────
CAL_NO = int(os.environ.get("AOS_SIM_CALNO", "101"))
TBL = {   # 소스 기본값 흉내 (예전 수동 입력 테이블 모양)
    0: ([0.0] + [51.2 + i * 6 for i in range(24)] + [198.6],
        [0, 1] + [int((51.2 + i * 6 + 1.2) * 327.68) for i in range(1, 24)] + [65535]),
    1: ([0] + [347 + i * 1400 for i in range(23)], [0.0] + [50.8 + i * 8.1 for i in range(23)]),
    2: ([0, 65535], [0.0, 10.0]),
    3: ([0, 65535], [0.0, 10.0]),
    4: ([-5.02 + i * 0.3347 for i in range(31)], [int(i * 2184.5) for i in range(31)]),
    5: ([0, 65535], [0.0, 10.0]),
}
cal_mode = False


def hv_real():
    """HV 실제 출력 [V] — DAC 9650 아래는 출력 없음, 위는 약간의 이득·오프셋·굴곡"""
    if P[0] <= 0:
        return 0.0
    dac = CT.fw_dac(*TBL[0], P[0])
    if dac < 9650:
        return 0.003
    vn = (dac + 1) * 200 / 65536
    return vn * 0.9986 + 0.31 + 0.22 * math.sin(vn / 17) + 0.06 * math.sin(vn / 3.1)


def hv_raw():
    return max(0, min(65535, int(hv_real() * 65535 / 200 * 1.004 + 12 + random.gauss(0, 1.5))))


def hv_vs():
    return CT.fw_sense(*TBL[1], hv_raw())


def cv_real():
    dac = CT.fw_dac(*TBL[4], P[3])
    return ((dac + 1) * 10 / 65536 - 5) * 1.002 + 0.013 + 0.004 * math.sin(dac / 5000)


def dmm_server(port):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(1)
    print(f"[SIM] DMM 34461A 흉내 TCP {port}")
    while True:
        c, a = srv.accept()
        rng, cnt, f = 1000.0, 1, c.makefile("rwb")
        try:
            for line in f:
                cmd = line.decode().strip().upper()
                if cmd == "*IDN?":
                    f.write(b"Keysight Technologies,34461A,SIM00001,A.03.03-sim\n")
                elif cmd.startswith("CONF:VOLT:DC"):
                    rng = float(cmd.split()[1])
                elif cmd.startswith("SAMP:COUN"):
                    cnt = int(cmd.split()[1])
                elif cmd == "READ?":
                    time.sleep(0.02 * cnt)
                    base = hv_real() if rng >= 100 else cv_real()
                    nz = 0.0004 if rng >= 100 else 0.00002
                    vals = [base + random.gauss(0, nz) for _ in range(cnt)]
                    if rng < 100 and abs(base) > 12:
                        vals = [9.9e37] * cnt
                    f.write((",".join(f"{v:+.8E}" for v in vals) + "\n").encode())
                f.flush()
        except OSError:
            pass
        c.close()


if os.environ.get("AOS_SIM_DMM"):
    threading.Thread(target=dmm_server, args=(int(os.environ["AOS_SIM_DMM"]),), daemon=True).start()


def cal_handle(f, addr):
    """캘리브레이션 명령 — 응답했으면 True (ACK 는 호출부가)"""
    global cal_mode
    cmd, pl = f["cmd"], bytes(f["payload"])
    if cmd == A_CAL_MODE:
        cal_mode = bool(pl[0])
        print(f"      -> CAL_MODE {'ON' if cal_mode else 'OFF'}")
        return False
    if cmd == A_CAL_WRITE:
        t, xs, ys = CT.unpack(pl)
        n = min(len(xs), CAL_NO)            # F/W 는 검사 없이 CAL_NO 를 넘겨 쓴다 → 여기선 잘림으로 흉내
        TBL[t] = (xs[:n], ys[:n])
        print(f"      -> CAL_WRITE type {t} {len(xs)} point (저장 {n})")
        return False
    if cmd == A_CAL_QUERY:
        t = pl[0]
        s.sendto(gfc_build(DTYPE_AOS, DID, A_CAL_TABLE, f["seq"], CT.pack(t, *TBL[t])), addr)
        return True
    if cmd == A_CAL_SENSE_Q:
        win = struct.unpack_from("<H", pl, 0)[0]
        n = max(1, win // 25)                  # STM32 0x03 는 25 ms 마다
        v = [hv_vs() for _ in range(n)]
        time.sleep(min(win, 2000) / 1000)
        s.sendto(gfc_build(DTYPE_AOS, DID, A_CAL_SENSE, f["seq"],
                           struct.pack(AOS_CAL_SENSE_FMT, sum(v) / n, min(v), max(v), n, 0, 10)), addr)
        return True
    return None


def status_bytes():
    up = int(time.time() - boot)
    flags = AOS_FLAG_UART_OK
    if lf_on:
        flags |= AOS_FLAG_LF_ON
    if applied:
        flags |= AOS_FLAG_APPLIED
    # 시험용: 환경변수 AOS_SIM_CUR=zero → 옛 브리지처럼 0 만, nocur → err 0x2000(전류 미수신)
    mode = os.environ.get("AOS_SIM_CUR", "")
    if mode == "zero":
        return struct.pack(AOS_STATUS_FMT, up, up, 0, 0, 0, 0, 0, flags, -48, 0)
    if mode == "nocur":
        return struct.pack(AOS_STATUS_FMT, up, up, 0, 0, 0, 0, 0, flags, -48, 0x2000)
    return struct.pack(AOS_STATUS_FMT, up, up, 1000, 0, 2000, 0, 0,
                       flags, -48, 0)


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", LOCAL_PORT))
s.settimeout(0.1)
dest = (SERVER, PORT)
boot = time.time()

hello = (b"GTS-AOS".ljust(16, b"\0") + b"0.1.0-sim".ljust(16, b"\0")
         + bytes(6) + bytes([DTYPE_AOS, DID]) + struct.pack("<I", 0)
         + b"sim".ljust(16, b"\0") + bytes(4))
s.sendto(gfc_build(DTYPE_AOS, DID, A_HELLO, nseq(), hello), dest)
print(f"[SIM] AOS DID={DID} -> {SERVER}:{PORT}  (local {LOCAL_PORT})")
print(f"[SIM] HV={P[0]} FRQ={P[1]} DUTY={P[2]} CV={P[3]} "
      f"LF_FRQ={P[4]} LF_VOLT={P[5]} LF={'ON' if lf_on else 'OFF'}")

last = 0.0
while True:
    dirty = False
    try:
        data, addr = s.recvfrom(4096)
        f = gfc_parse(data)
        if f and f["cmd"] not in (A_ACK,):
            name = AOS_CMD_NAME.get(f["cmd"], hex(f["cmd"]))
            if f["cmd"] not in (A_CAL_SENSE_Q,):
                print(f"[SIM] RX {name} seq={f['seq']} "
                      f"{f['payload'].hex(' ') if len(f['payload']) < 40 else str(len(f['payload'])) + 'B'}")

            if f["cmd"] == A_PARAM_SET and len(f["payload"]) >= 8:
                pid = f["payload"][0]
                v = struct.unpack_from("<f", f["payload"], 4)[0]
                if pid < len(P):
                    lo, hi = RANGE[pid]
                    v2 = max(lo, min(hi, v))
                    if v2 != v:
                        print(f"      clamp {v:g} -> {v2:g}")
                    if P[pid] != v2:
                        P[pid] = v2
                        dirty = True
                    print(f"      -> {PARAM_NAME[pid]} = {v2:g}")
            elif f["cmd"] == A_LF_MODE and f["payload"]:
                nv = 1 if f["payload"][0] else 0
                if lf_on != nv:
                    lf_on = nv
                    dirty = True
                print(f"      -> LF {'ON' if lf_on else 'OFF'}")
            elif f["cmd"] == A_LF_SHAPE and f["payload"]:
                if LF_SHAPE_FIXED:
                    print(f"      -> LF shape 요청({f['payload'][0]}) 무시 "
                          f"— STM32 는 eSquare 고정")
                else:
                    if lf_shape != f["payload"][0]:
                        lf_shape = f["payload"][0]
                        dirty = True
                    print(f"      -> LF shape {lf_shape}")
            elif f["cmd"] in (A_CAL_MODE, A_CAL_WRITE, A_CAL_QUERY, A_CAL_SENSE_Q):
                if cal_handle(f, addr):
                    continue
            elif f["cmd"] == A_PARAMS_QUERY:
                s.sendto(gfc_build(DTYPE_AOS, DID, A_PARAMS, f["seq"],
                                   params_bytes()), addr)
                continue

            ack = struct.pack("<BBH", f["cmd"], 0, f["seq"])
            s.sendto(gfc_build(DTYPE_AOS, DID, A_ACK, f["seq"], ack), addr)

            if dirty:
                applied = True
                # 실제 브리지는 여기서 STM32 로 0x2A / 0x52 를 쏜다
                s.sendto(gfc_build(DTYPE_AOS, DID, A_PARAMS, nseq(),
                                   params_bytes()), dest)
    except socket.timeout:
        pass

    if time.time() - last >= 1.0:
        last = time.time()
        s.sendto(gfc_build(DTYPE_AOS, DID, A_STATUS, nseq(), status_bytes()), dest)
