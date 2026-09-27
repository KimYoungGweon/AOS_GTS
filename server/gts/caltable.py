"""보정 테이블 — 순수 함수 (F/W CAL_CTL.c 와 같은 계산, 패킹, 계획, 병합, 검사)

F/W 테이블 (CAL_CTL.h)
  type 0 HV_DAC (CSET)       X f32 실측 V  → Y u16 DAC
  type 1 HV_VS  (CSENSE_VS)  X u16 ADC raw → Y f32 실측 V
  type 4 CV_DAC (CSET_CV)    X f32 실측 V  → Y u16 DAC
  UART 0xA0/0xA1 payload: u8 type, u8 no, no × 6 byte (DAC 형: f32 X, u16 Y / VS 형: u16 X, f32 Y)
  F/W 는 구간 선형 보간, 양 끝 밖은 끝 구간으로 외삽 (Find_Cal_Result_*).

서버는 F/W 가 쓰는 테이블(백업·초기화·저장값)을 알고 있으므로, 설정 전압에 대해 F/W 가
실제로 내보낸 DAC 와 HV_Vs 표시값의 ADC raw 를 **같은 식으로 계산**할 수 있다.
→ 선형 초기화 뒤 전체 보정도, 초기화 없는 일부 구간 보정도 raw 명령 없이 된다.
"""
from __future__ import annotations

import math
import struct

import numpy as np

HV_DAC, HV_VS, CV_DAC = 0, 1, 4
TYPES = (HV_DAC, HV_VS, CV_DAC)
TYPE_NAME = {HV_DAC: "HV_DAC", HV_VS: "HV_VS", CV_DAC: "CV_DAC"}
DAC_TYPES = (HV_DAC, CV_DAC)
ADC_FS = {"ad7739": 65535, "mcp3202": 4095}
f32 = np.float32


def is_dac(t):
    return t in DAC_TYPES


# ── 패킹 ────────────────────────────────────────────────────────────

def pack(t, xs, ys) -> bytes:
    out = bytearray([t, len(xs)])
    for x, y in zip(xs, ys):
        if is_dac(t):
            out += struct.pack("<fH", float(x), int(y))
        else:
            out += struct.pack("<Hf", int(x), float(y))
    return bytes(out)


def unpack(pl: bytes):
    """@return (type, xs, ys)"""
    if len(pl) < 2:
        raise ValueError("테이블 payload 가 짧음")
    t, n = pl[0], pl[1]
    if len(pl) < 2 + 6 * n:
        raise ValueError(f"테이블 길이 부족 (no={n}, {len(pl)} B)")
    xs, ys = [], []
    for i in range(n):
        o = 2 + 6 * i
        if is_dac(t):
            x, y = struct.unpack_from("<fH", pl, o)
        else:
            x, y = struct.unpack_from("<Hf", pl, o)
        xs.append(x)
        ys.append(y)
    return t, xs, ys


def f32r(v):
    """f32 로 한 번 거친 값 (UART 로 오간 값과 비교할 때)"""
    return struct.unpack("<f", struct.pack("<f", float(v)))[0]


# ── F/W 계산 재현 (float32) ─────────────────────────────────────────

def _seg(xs, v):
    n = len(xs)
    if v >= xs[n - 1]:
        return n - 2
    if v <= xs[0]:
        return 0
    for i in range(1, n):
        if v <= xs[i]:
            return i - 1
    return n - 2


def fw_dac(xs, ys, v):
    """Find_Cal_Result_for_DAC_HV / _CV — 설정 V → 출력 DAC (u16)"""
    i = _seg(xs, f32(v))
    cx1, cx2, cy1, cy2 = f32(xs[i]), f32(xs[i + 1]), f32(ys[i]), f32(ys[i + 1])
    slope = f32((cy2 - cy1) / (cx2 - cx1))
    b = f32(cy1 - slope * cx1)
    r = f32(slope * f32(v) + b)
    if r < 0:
        return 0
    if r > 65535:
        return 65535
    return int(r)


def fw_sense(xs, ys, raw):
    """Find_Cal_Result_for_ADC_VS_HV — ADC raw → 표시 V"""
    i = _seg(xs, raw)
    cx1, cx2, cy1, cy2 = f32(xs[i]), f32(xs[i + 1]), f32(ys[i]), f32(ys[i + 1])
    slope = f32((cy2 - cy1) / (cx2 - cx1))
    b = f32(cy1 - slope * cx1)
    r = f32(slope * f32(raw) + b)
    return max(0.0, float(r))


def inv_linear(xs, ys, y):
    """단조 증가 테이블에서 Y → X (실수). 양 끝 밖은 끝 구간 외삽"""
    n = len(ys)
    if y >= ys[n - 1]:
        i = n - 2
    elif y <= ys[0]:
        i = 0
    else:
        i = next(k for k in range(1, n) if y <= ys[k]) - 1
    x1, x2, y1, y2 = xs[i], xs[i + 1], ys[i], ys[i + 1]
    if y2 == y1:
        return float(x1)
    return x1 + (y - y1) * (x2 - x1) / (y2 - y1)


def setv_for_dac(xs, ys, dac):
    """F/W 가 이 DAC 를 내보내게 하려면 얼마를 설정해야 하나 → (설정 V, 실제 DAC)"""
    v = f32r(inv_linear(xs, ys, dac + 0.5))          # (u16) 버림 → 가운데를 노린다
    return v, fw_dac(xs, ys, v)


def raw_for_sense(xs, ys, volt):
    """표시 V → ADC raw (실수).  HV_Vs 평균에서 raw 를 역산"""
    return inv_linear(xs, ys, volt)


# ── 공칭 식 / 선형 초기화 ───────────────────────────────────────────

def nominal_dac(t, v):
    if t == HV_DAC:
        d = 65536.0 / 200.0 * v - 1
    else:
        d = 65536.0 / 10.0 * (v + 5.0) - 1
    return int(max(0, min(65535, round(d))))


def linear_tables(adc="ad7739"):
    fs = ADC_FS[adc]
    return {
        HV_DAC: ([0.0, 200.0], [0, 65535]),
        HV_VS: ([0, fs], [0.0, 200.0]),
        CV_DAC: ([-5.0, 5.0], [0, 65535]),
    }


# ── 계획 (포인트 목록) ──────────────────────────────────────────────

def grid(start, end, step):
    if step <= 0:
        raise ValueError("간격은 0 보다 커야 합니다")
    if end < start:
        raise ValueError("끝이 시작보다 작습니다")
    n = int(math.floor((end - start) / step + 1e-9)) + 1
    vals = [round(start + i * step, 6) for i in range(n)]
    if abs(vals[-1] - end) > 1e-6:
        vals.append(round(end, 6))                   # 끝점은 항상 포함
    return vals


def plan(target, start, end, step, include_zero=False, no_max=101):
    """@return dict(points=[nominal V...], count, error)"""
    lo, hi = (0.0, 200.0) if target == "hv" else (-5.0, 5.0)
    err = None
    try:
        if start < lo or end > hi:
            raise ValueError(f"범위는 {lo:g} ~ {hi:g} V")
        pts = grid(start, end, step)
    except ValueError as e:
        return dict(points=[], count=0, error=str(e))
    if target == "hv" and include_zero and pts and pts[0] > 0:
        count = len(pts) + 1
    else:
        count = len(pts)
    if count > no_max:
        err = f"포인트 {count} 개 — F/W 테이블 최대 {no_max} 개를 넘음"
    elif count < 2:
        err = "포인트가 2 개 이상이어야 합니다"
    return dict(points=pts, count=count, error=err)


# ── 테이블 만들기 / 병합 / 검사 ─────────────────────────────────────

def _sorted_unique(pairs):
    d = {}
    for x, y in pairs:
        d[x] = y                                     # 같은 X 는 나중 값
    return sorted(d.items())


def build(points, adc="ad7739", include_zero=True):
    """측정점 → 새 테이블 3 종.
    points: [{target:'hv'|'cv', dac, meas, raw(hv만, 실수)}]
    @return {type: (xs, ys)}"""
    hv = [p for p in points if p["target"] == "hv" and p.get("ok", True)]
    cv = [p for p in points if p["target"] == "cv" and p.get("ok", True)]
    out = {}
    if hv:
        dac = [(round(p["meas"], 5), int(p["dac"])) for p in hv]
        vs = [(int(round(p["raw"])), round(p["meas"], 5)) for p in hv if p.get("raw") is not None]
        if include_zero:
            dac.append((0.0, 0))
            vs.append((0, 0.0))
        out[HV_DAC] = tuple(map(list, zip(*_sorted_unique(dac))))
        if vs:
            out[HV_VS] = tuple(map(list, zip(*_sorted_unique(vs))))
    if cv:
        out[CV_DAC] = tuple(map(list, zip(*_sorted_unique((round(p["meas"], 5), int(p["dac"])) for p in cv))))
    return out


def merge(old, new):
    """일부 구간 보정: 새 테이블 X 범위 안의 기존 포인트는 버리고, 밖은 유지.
    기존 테이블의 0 기준점(X=0)은 항상 유지.  @return (xs, ys, info)"""
    oxs, oys = old
    nxs, nys = new
    lo, hi = min(nxs), max(nxs)
    kept = [(x, y) for x, y in zip(oxs, oys) if not (lo <= x <= hi) or x == 0]
    removed = len(oxs) - len(kept)
    merged = _sorted_unique(kept + list(zip(nxs, nys)))
    xs, ys = map(list, zip(*merged))
    return xs, ys, dict(range=[lo, hi], removed=removed, kept=len(kept), added=len(nxs))


def check(t, xs, ys, no_max=101, partial_old=None):
    """@return [dict(key, label, ok, level('ok'|'warn'|'err'), value)]"""
    res = []
    n = len(xs)

    def add(key, label, ok, value, level=None):
        res.append(dict(key=key, label=label, ok=ok, value=value,
                        level=level or ("ok" if ok else "err")))

    asc = all(xs[i] < xs[i + 1] for i in range(n - 1))
    add("sorted", "X 오름차순", asc, "통과" if asc else "역전 있음")
    mono = all(ys[i] < ys[i + 1] for i in range(n - 1))
    add("monotonic", "Y 단조 증가 (역전 없음)", mono, "통과" if mono else "역전 있음")
    add("count", f"포인트 수 2 ~ {no_max}", 2 <= n <= no_max, f"{n} / {no_max}")
    # 공칭 식 대비 편차
    if is_dac(t):
        dev = [x - _nominal_v(t, y) for x, y in zip(xs, ys) if y > 0]
        lim = 5.0 if t == HV_DAC else 0.5
    else:
        fs = xs[-1] if xs else 1
        dev = []
        lim = 5.0
    if dev:
        m = max(dev, key=abs)
        add("deviation", f"공칭 대비 편차 < {lim:g} V", abs(m) < lim, f"최대 {m:+.3f} V")
    # 일부 구간 병합 경계 단차 — 기존 테이블로 예측한 값과 새 경계점의 차이
    if partial_old is not None and partial_old.get("range"):
        lo, hi = partial_old["range"]
        oxs, oys = partial_old["old"]
        worst = 0.0
        for x, y in zip(xs, ys):
            if x in (lo, hi):
                if is_dac(t):
                    pred = fw_dac(oxs, oys, x)
                    worst = max(worst, abs(pred - y) / (65535 / (200 if t == HV_DAC else 10)))
                else:
                    worst = max(worst, abs(fw_sense(oxs, oys, x) - y))
        warn = worst > (0.5 if t != CV_DAC else 0.05)
        add("boundary", "구간 경계 단차", not warn, f"{worst:.3f} V", "warn" if warn else "ok")
    return res


def _nominal_v(t, dac):
    if t == HV_DAC:
        return (dac + 1) * 200.0 / 65536.0
    return (dac + 1) * 10.0 / 65536.0 - 5.0


def as_rows(t, xs, ys):
    return [dict(x=x, y=y) for x, y in zip(xs, ys)]


def same(t, a, b):
    """F/W 에서 되읽은 테이블이 쓴 것과 같은가 (f32 왕복 오차 허용)"""
    (axs, ays), (bxs, bys) = a, b
    if len(axs) != len(bxs):
        return False
    for x1, y1, x2, y2 in zip(axs, ays, bxs, bys):
        if is_dac(t):
            if abs(f32r(x1) - f32r(x2)) > 1e-6 * max(1, abs(x1)) or int(y1) != int(y2):
                return False
        else:
            if int(x1) != int(x2) or abs(f32r(y1) - f32r(y2)) > 1e-6 * max(1, abs(y1)):
                return False
    return True
