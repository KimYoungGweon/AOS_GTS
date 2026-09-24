#!/usr/bin/env python3
"""
console_sim.py — 콘솔 흉내내기 (ESP32-P4 없이 서버를 검증할 때)

    python3 console_sim.py [서버IP] [GFC_ID]
    python3 console_sim.py 192.168.0.6 1

대화형. 명령을 치면 콘솔이 보내는 것과 똑같은 프레임을 5502 로 던진다.

  c        CONNECT            (P1 장치 선택 = ENTER CONTROL 활성 조건)
  p        PING
  m0 / m1  SET_MODE MANUAL / AUTO
  on / off SET_PUMP 1 / 0     (P2 MANUAL 의 Pump 버튼)
  t 300 15 SET_TIMES start_ds cycle_ds   (30.0s / 1.5s)
  run/stop AUTO_RUN 1 / 0     (P2 AUTO 의 START/STOP)
  d        DISCONNECT
  q        종료

AOS (먼저 'a' 로 타깃을 AOS 로 바꾼다)
  a / g            타깃을 AOS / GFC 로 전환
  hv 120           HV      0~200 V
  frq 350          FRQ     200~800 kHz
  duty 25          DUTY    10~50 %
  cv -1.5          CV      -5~5 V
  lff 120          LF_FRQ  50~200 Hz
  lfv 2.5          LF_VOLT 0~5 V
  lfon / lfoff     LF On/Off
  shape 2          LF 파형 0~3
  get              AOS_GET_PARAMS
"""
import socket
import struct
import sys
import threading

sys.path.insert(0, ".")
from udp_server import (console_build, console_parse, DEV_CONSOLE, DEV_GFC,
                        DEV_AOS, C_CONNECT, C_DISCONNECT, C_PING,
                        C_GFC_SET_MODE, C_GFC_SET_PUMP, C_GFC_SET_TIMES,
                        C_GFC_AUTO_RUN, C_CONNECT_ACK, C_PONG, C_ACK, C_NAK,
                        C_GFC_AUTO_STATE, C_DISCONNECT_ACK,
                        C_AOS_SET_PARAM, C_AOS_SET_LF_MODE,
                        C_AOS_SET_LF_SHAPE, C_AOS_GET_PARAMS, C_AOS_PARAMS,
                        AOS_PARAMS_FMT, PARAM_NAME)

SERVER = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.6"
GFC_ID = int(sys.argv[2]) if len(sys.argv) > 2 else 1
DEV_KIND = DEV_GFC          # a / g 명령으로 전환
PORT = 5502
MY_ID = 1

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
seq = 0


def send(cmd, payload=b""):
    global seq
    seq = (seq + 1) & 0xFFFF
    pkt = console_build(DEV_KIND, GFC_ID, cmd, DEV_CONSOLE, MY_ID, seq, payload)
    s.sendto(pkt, (SERVER, PORT))
    print(f"  TX 0x{cmd:02X} seq={seq}  {pkt.hex(' ')}")


def rx_loop():
    while True:
        data, _ = s.recvfrom(1024)
        f = console_parse(data)
        if f is None:
            print(f"  RX 규격 외 {data.hex(' ')}")
            continue
        c, p = f["cmd"], f["payload"]
        if c == C_CONNECT_ACK:
            r, st, fw, up, lr = struct.unpack("<BBHII", p[:12])
            print(f"  RX CONNECT_ACK result={r} state={st} fw={fw:04X} "
                  f"uptime={up}s last_reply={lr}ms"
                  f"   → ENTER CONTROL {'활성' if r == 0 else '비활성'}")
        elif c == C_PONG:
            print(f"  RX PONG link_state={p[0]}")
        elif c == C_ACK:
            print(f"  RX ACK for 0x{p[0]:02X}")
        elif c == C_NAK:
            print(f"  RX NAK cmd=0x{p[0]:02X} err={p[1]}")
        elif c == C_DISCONNECT_ACK:
            print(f"  RX DISCONNECT_ACK result={p[0]}")
        elif c == C_GFC_AUTO_STATE:
            run, pump, remain, cnt = struct.unpack("<BBHI", p[:8])
            print(f"  RX AUTO_STATE run={run} pump={pump} "
                  f"remain={remain/10:.1f}s cycles={cnt}")
        elif c == C_AOS_PARAMS:
            hv, frq, duty, cv, lff, lfv, lf_on, shape = struct.unpack(
                AOS_PARAMS_FMT, p[:26])
            print(f"  RX AOS_PARAMS HV={hv:.2f} FRQ={frq:.1f} DUTY={duty:.2f} "
                  f"CV={cv:.3f} LF={'ON' if lf_on else 'OFF'} "
                  f"LFF={lff:.0f} LFV={lfv:.2f} shape={shape}")
        else:
            print(f"  RX 0x{c:02X} {p.hex(' ')}")


threading.Thread(target=rx_loop, daemon=True).start()

print(f"console_sim -> {SERVER}:{PORT}  target=GFC #{GFC_ID}  my_id={MY_ID}")
print(__doc__.split("대화형.")[1])

while True:
    try:
        line = input("> ").strip()
    except (EOFError, KeyboardInterrupt):
        break
    if not line:
        continue
    a = line.split()
    k = a[0].lower()
    if k == "q":
        break
    elif k == "c":
        send(C_CONNECT)
    elif k == "p":
        send(C_PING)
    elif k == "d":
        send(C_DISCONNECT)
    elif k in ("m0", "m1"):
        send(C_GFC_SET_MODE, bytes([int(k[1])]))
    elif k == "on":
        send(C_GFC_SET_PUMP, bytes([1]))
    elif k == "off":
        send(C_GFC_SET_PUMP, bytes([0]))
    elif k == "t" and len(a) == 3:
        send(C_GFC_SET_TIMES, struct.pack("<HH", int(a[1]), int(a[2])))
    elif k == "run":
        send(C_GFC_AUTO_RUN, bytes([1]))
    elif k == "stop":
        send(C_GFC_AUTO_RUN, bytes([0]))
    # ── AOS ────────────────────────────────────────────────────────
    elif k == "g":
        DEV_KIND = DEV_GFC
        print(f"  타깃 = GFC #{GFC_ID}")
    elif k == "a":
        DEV_KIND = DEV_AOS
        print(f"  타깃 = AOS #{GFC_ID}")
    elif k in ("hv", "frq", "duty", "cv", "lff", "lfv") and len(a) == 2:
        pid = {"hv": 0, "frq": 1, "duty": 2, "cv": 3,
               "lff": 4, "lfv": 5}[k]
        send(C_AOS_SET_PARAM,
             bytes([pid, 0, 0, 0]) + struct.pack("<f", float(a[1])))
    elif k in ("lfon", "lfoff"):
        send(C_AOS_SET_LF_MODE, bytes([1 if k == "lfon" else 0]))
    elif k == "shape" and len(a) == 2:
        send(C_AOS_SET_LF_SHAPE, bytes([int(a[1])]))
    elif k == "get":
        send(C_AOS_GET_PARAMS)
    else:
        print("  GFC: c p m0 m1 on off 't 300 15' run stop d")
        print("  AOS: a (타깃전환) 'hv 120' 'frq 350' 'duty 25' 'cv -1.5'")
        print("       'lff 120' 'lfv 2.5' lfon lfoff 'shape 2' get")
        print("  공통: g (GFC 로 전환) q (종료)")
