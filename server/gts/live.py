"""실시간 상태 (계층 C) — 서버 메모리.  DB 를 거치지 않는다.

  AOS Current 1초  → aos_ring[did]  deque(600)  (D11)
  GFC 1초          → gfc_ring[did]  deque(600)  (대시보드 "최근 10분" 그래프)
  수신 통계 / 최근 이벤트 / 콘솔 현황 / 온라인 전이 감지

udp_server 의 hook 에서 불린다 → 전부 O(1), 블로킹 없음.
"""
from __future__ import annotations

import time
from collections import deque

from . import config

T0 = time.time()

aos_ring: dict[int, deque] = {}     # did -> deque[(t, air_p, air_n, gas_p, gas_n)]
gfc_ring: dict[int, deque] = {}     # did -> deque[(t, volt1, volt2, pump1, pump2, pump3, flags, src_remain)]
events: deque = deque(maxlen=config.EVENT_RING)   # dict(id, at, level, dev_type, dev_id, kind, text)
_event_seq = 0

# 수신 통계
rx_count = {"aos": 0, "gfc": 0, "console": 0}
bad_count = {"aos": 0, "gfc": 0, "console": 0}
_rate_hist: deque = deque(maxlen=6)          # (t, total_rx, total_bad)
rate = {"pkt_s": 0.0, "loss_pct": 0.0}

# 콘솔 (addr -> dict)
console_info: dict = {}

# 온라인 전이 감지용
_prev_online: dict = {}

# DB 쪽이 등록하는 콜백 (이벤트 영구 저장)
on_event = None


def adc_to_v(adc: int) -> float:
    return adc * config.AOS_ADC_TO_V + config.AOS_ADC_OFFSET_V


def add_event(level, kind, text, dev_type=None, dev_id=None, detail=None):
    global _event_seq
    _event_seq += 1
    e = dict(id=_event_seq, at=time.time(), level=level, dev_type=dev_type,
             dev_id=dev_id, kind=kind, text=text)
    events.append(e)
    if on_event:
        on_event(e, detail)
    return e


# AOS STATUS(0x45) err 비트 — 브리지 aos_proto.h 와 같게
ERR_UART_LOST = 1 << 11
ERR_NO_REPLY = 1 << 12
ERR_NO_CURRENT = 1 << 13       # 2026-09-23: STM32 가 3초 넘게 전류(0x03)를 안 보냄

# 전류값 유효 여부: did -> dict(valid, reason, since, last_valid, adc)
#   "0 이 온 것" 과 "안 온 것" 을 구별하려고 둔다. 무효 샘플은 링에 넣지 않는다 → 그래프가 비고 이유가 표시된다
aos_cur: dict[int, dict] = {}


def _cur_reason(st):
    err = st.get("err") or 0
    if err & ERR_UART_LOST:
        return "uart_lost", "브리지 ↔ STM32 UART 끊김 — 전류값 없음"
    if err & ERR_NO_CURRENT:
        return "no_current", "STM32 가 전류값을 보내지 않음 (측정 순회 중이거나 자동 상태 송신 꺼짐)"
    if st["air_p"] == 0 and st["air_n"] == 0 and st["gas_p"] == 0 and st["gas_n"] == 0:
        return "all_zero", "ADC 4채널 모두 0 — 전류값을 받지 못한 것으로 판단 (브리지 펌웨어 확인)"
    return None, None


def push_aos(did, st):
    now = time.time()
    code, text = _cur_reason(st)
    c = aos_cur.get(did)
    if c is None:
        c = aos_cur[did] = dict(valid=None, code=None, reason=None, since=now, last_valid=None, adc=None)
    valid = code is None
    if c["valid"] is not None and (valid != c["valid"] or code != c["code"]):
        c["since"] = now
        if valid:
            add_event("ok", "current_ok", f"AOS {did:02d} · 전류값 수신 재개", 1, did)
        elif c["valid"]:
            add_event("warn", "current_lost", f"AOS {did:02d} · {text}", 1, did)
    elif c["valid"] is None:
        c["since"] = now
    c.update(valid=valid, code=code, reason=text,
             adc=dict(air_p=st["air_p"], air_n=st["air_n"], gas_p=st["gas_p"], gas_n=st["gas_n"]))
    if not valid:
        return
    c["last_valid"] = now
    r = aos_ring.get(did)
    if r is None:
        r = aos_ring[did] = deque(maxlen=config.AOS_RING)
    r.append((now, st["air_p"], st["air_n"], st["gas_p"], st["gas_n"]))


def aos_cur_view(did):
    c = aos_cur.get(did)
    if not c:
        return dict(valid=False, code="no_status", reason="브리지 STATUS(0x45) 미수신", since_s=None,
                    last_valid_s=None, adc=None, channel=config.AOS_CURRENT_CH)
    now = time.time()
    return dict(valid=bool(c["valid"]), code=c["code"], reason=c["reason"],
                since_s=round(now - c["since"], 1),
                last_valid_s=round(now - c["last_valid"], 1) if c["last_valid"] else None,
                adc=c["adc"], channel=config.AOS_CURRENT_CH)


def push_gfc(did, s):
    r = gfc_ring.get(did)
    if r is None:
        r = gfc_ring[did] = deque(maxlen=config.GFC_RING)
    r.append((time.time(), s["volt1"], s["volt2"], s["pump1"], s["pump2"], s["pump3"],
              s["flags"], s["src_remain"]))


CH_INDEX = {"air_p": 1, "air_n": 2, "gas_p": 3, "gas_n": 4}


def aos_series(did, sec=120, ch=None):
    """[(t, v)] 최근 sec 초, 표시 단위 V"""
    ci = CH_INDEX.get(ch or config.AOS_CURRENT_CH, 3)
    r = aos_ring.get(did) or ()
    tmin = time.time() - sec
    return [(round(x[0], 3), round(adc_to_v(x[ci]), 5)) for x in r if x[0] >= tmin]


def aos_stats(did, sec=120, ch=None):
    vals = [v for _, v in aos_series(did, sec, ch)]
    if not vals:
        return None
    n = len(vals)
    avg = sum(vals) / n
    sd = (sum((v - avg) ** 2 for v in vals) / n) ** 0.5
    return dict(now=vals[-1], min=min(vals), max=max(vals), avg=avg, sd=sd, n=n)


def gfc_series(did, sec=600):
    r = gfc_ring.get(did) or ()
    tmin = time.time() - sec
    return [dict(t=round(x[0], 3), volt1=x[1], volt2=x[2], pump1=x[3], pump2=x[4],
                 pump3=x[5], flags=x[6], src_remain=x[7]) for x in r if x[0] >= tmin]


def tick_rate():
    """1초마다 호출 — 최근 5초 pkt/s, 손실(CRC/규격 외) 비율"""
    tot = sum(rx_count.values())
    bad = sum(bad_count.values())
    now = time.time()
    _rate_hist.append((now, tot, bad))
    if len(_rate_hist) >= 2:
        t0, r0, b0 = _rate_hist[0]
        dt = max(now - t0, 1e-3)
        drx, dbad = tot - r0, bad - b0
        rate["pkt_s"] = round(drx / dt, 1)
        rate["loss_pct"] = round(100.0 * dbad / (drx + dbad), 3) if (drx + dbad) else 0.0


def check_online(kind, reg, dev_type):
    """온라인 ↔ 오프라인 전이를 이벤트로"""
    for did, dev in list(reg.items()):
        key = (dev_type, did)
        on = dev.online
        prev = _prev_online.get(key)
        if prev is None:
            _prev_online[key] = on
            continue
        if prev and not on:
            gap = int(time.time() - dev.last_seen)
            add_event("error", "offline", f"{kind} {did:02d} · UDP 패킷 {gap}초간 미수신",
                      dev_type, did)
        elif on and not prev:
            add_event("ok", "online", f"{kind} {did:02d} · 통신 재개", dev_type, did)
        _prev_online[key] = on
