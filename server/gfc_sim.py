#!/usr/bin/env python3
"""
gfc_sim.py — GFC ID=1 장치 흉내내기 (ESP32 없이 서버를 검증할 때)

    python3 gfc_sim.py [서버IP] [DID] [로컬포트]
    python3 gfc_sim.py 192.168.0.6 1
    python3 gfc_sim.py 127.0.0.1 1 6501   # 서버와 같은 PC 에서 돌릴 때

로컬 포트에 bind 하고 1초마다 SENSOR_DATA 를 올린다.
서버가 PUMP_SET / SRC_SET 을 내려보내면 화면에 찍고 주입 시퀀스를
펌웨어와 같은 식으로 돌린다 (ESP32 gfc_ctrl.c 와 같은 계산식).
"""
import socket
import struct
import sys
import time

sys.path.insert(0, ".")
from udp_server import (gfc_build, gfc_parse, SENSOR_FMT, SRC_SET_FMT,
                        G_HELLO, G_ACK, G_EVENT, G_PING, G_PUMP_SET,
                        G_PUMP_QUERY, G_SENSOR_DATA, G_SRC_SET, G_CFG_QUERY,
                        GFC_CMD_NAME, FLAG_SRC_ENABLE, FLAG_SRC_ON,
                        FLAG_SRC_INIT, FLAG_UART_OK, DTYPE_GFC)

SERVER = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.6"
DID = int(sys.argv[2]) if len(sys.argv) > 2 else 1
PORT = 5501
# 서버와 같은 PC 에서 돌릴 때는 5501 이 이미 쓰이므로 로컬 포트를 따로 준다
LOCAL_PORT = int(sys.argv[3]) if len(sys.argv) > 3 else PORT

seq = 0
enable = False
init_on = 30.0
cycle_on = 1.0
period = 600.0
t0 = 0.0
in_init = False
pumps = [0, 0, 0]


def nseq():
    global seq
    seq = (seq + 1) & 0xFFFF
    return seq


def now():
    return time.time()


def tick():
    """gfc_ctrl.c 와 같은 계산식."""
    global pumps, in_init
    if not enable:
        in_init = False
        return 0.0, False
    t = now() - t0
    if t < init_on:
        in_init = True
        on, remain = True, 0.0
    else:
        in_init = False
        u = t - init_on
        n = int(u / period)
        f = u - n * period
        on = (n >= 1) and (f < cycle_on)
        remain = 0.0 if on else ((n + 1) * period - u)
    pumps = [1, 1 if on else 0, 1 if on else 0]
    return remain, on


def sensor(remain, on):
    flags = FLAG_UART_OK
    if enable:
        flags |= FLAG_SRC_ENABLE
    if on:
        flags |= FLAG_SRC_ON
    if in_init:
        flags |= FLAG_SRC_INIT
    up = int(now() - boot)
    return struct.pack(SENSOR_FMT, up, 1.23, 2.34, 0.0, 0.0, 0.0,
                       remain, (now() - t0) if enable else 0.0, up,
                       1007, 1638, 0, 0,
                       pumps[0], pumps[1], pumps[2], flags, 0, -50, 0)


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", LOCAL_PORT))
s.settimeout(0.1)
dest = (SERVER, PORT)
boot = now()

hello = (b"GTS-GFC".ljust(16, b"\0") + b"0.1.0-sim".ljust(16, b"\0")
         + bytes(6) + bytes([DTYPE_GFC, DID]) + struct.pack("<I", 0)
         + b"sim".ljust(16, b"\0") + bytes(4))
s.sendto(gfc_build(DTYPE_GFC, DID, G_HELLO, nseq(), hello), dest)
print(f"[SIM] GFC DID={DID} -> {SERVER}:{PORT}  (local {LOCAL_PORT})")

last = 0.0
while True:
    try:
        data, addr = s.recvfrom(1024)
        f = gfc_parse(data)
        if f and f["cmd"] not in (G_ACK,):
            name = GFC_CMD_NAME.get(f["cmd"], hex(f["cmd"]))
            print(f"[SIM] RX {name} seq={f['seq']} {f['payload'].hex(' ')}")

            if f["cmd"] == G_PUMP_SET and len(f["payload"]) >= 4:
                enable = False
                pumps = list(f["payload"][:3])
                print(f"      → PUMP {pumps}")
            elif f["cmd"] == G_SRC_SET and len(f["payload"]) >= 16:
                en, init_on, cycle_on, period = struct.unpack(
                    SRC_SET_FMT, f["payload"][:16])
                if en and not enable:
                    t0 = now()
                enable = bool(en)
                if not enable:
                    pumps = [0, 0, 0]
                print(f"      → SRC enable={en} init={init_on:.1f}s "
                      f"cycle={cycle_on:.1f}s period={period:.1f}s")
            elif f["cmd"] == G_PUMP_QUERY:
                s.sendto(gfc_build(DTYPE_GFC, DID, G_PUMP_QUERY, f["seq"],
                                   bytes(pumps + [0])), addr)
                continue
            elif f["cmd"] == G_CFG_QUERY:
                s.sendto(gfc_build(DTYPE_GFC, DID, G_SRC_SET, f["seq"],
                                   struct.pack(SRC_SET_FMT, int(enable),
                                               init_on, cycle_on, period)), addr)
                continue

            ack = struct.pack("<BBH", f["cmd"], 0, f["seq"])
            s.sendto(gfc_build(DTYPE_GFC, DID, G_ACK, f["seq"], ack), addr)
            if f["cmd"] in (G_PUMP_SET, G_SRC_SET):
                remain, on = tick()
                s.sendto(gfc_build(DTYPE_GFC, DID, G_SENSOR_DATA, nseq(),
                                   sensor(remain, on)), dest)
    except socket.timeout:
        pass

    if now() - last >= 1.0:
        last = now()
        remain, on = tick()
        s.sendto(gfc_build(DTYPE_GFC, DID, G_SENSOR_DATA, nseq(),
                           sensor(remain, on)), dest)
        print(f"[SIM] TX SENSOR pumps={pumps} remain={remain:6.1f}s "
              f"{'INJECTING' if on else ''}")
