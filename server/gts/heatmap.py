"""Heatmap payload / 격자 / Idf 계산 — DB·API·임포터 공용 (순수 함수, I/O 없음)

규칙 (GTS_DB_Design_Proposal v1.3 · schema v1.4)
  payload     : iy = CV(외) × ix = LFV(내) row-major  (0x86 wire order)
  payload_fmt : 0 = u16 ADC raw (LE),  1 = f32 (LE, .dat 임포트)
  cond_idx    : ((sy*no_sx + sx)*no_hy + hy)*no_hx + hx      fast 는 param_no-1
  cond_hash   : sha256(hv, frq, duty, lff, lfv_list, cv_list) — 소수 3자리 정규화
  Idf         : Target_P − Air_P   (PCSW .dat 의 Idf 와 1e-6 이내 일치 확인)
  best        : |Idf| 최대 지점    (PCSW Best_idxLFV / Best_idxCV 와 일치 확인)
"""
from __future__ import annotations

import hashlib
import json
import math

import numpy as np

from . import config


# ─── 정규화 / 해시 ───────────────────────────────────────────────────

def _r(x: float) -> float:
    """해시용 정규화. 266.6667 과 266.66667 이 같은 값이 되도록."""
    v = round(float(x), 3)
    return 0.0 if v == 0 else v          # -0.0 → 0.0, 1.49e-08 → 0.0


def cond_hash(hv, frq, duty, lff, lfv_list, cv_list) -> bytes:
    key = json.dumps([_r(hv), _r(frq), _r(duty), _r(lff),
                      [_r(v) for v in lfv_list], [_r(v) for v in cv_list]],
                     separators=(",", ":"))
    return hashlib.sha256(key.encode()).digest()


def grid_hash(grid: dict) -> bytes:
    keys = ("mode", "no_sx", "no_sy", "no_hx", "no_hy", "no_lfv", "no_cv",
            "hv_list", "frq_list", "duty_list", "lff_list", "lfv_list", "cv_list",
            "param_sets")
    norm = {}
    for k in keys:
        v = grid.get(k)
        if isinstance(v, (list, tuple)):
            v = [({kk: (_r(vv) if isinstance(vv, (int, float)) else vv)
                   for kk, vv in e.items()} if isinstance(e, dict) else _r(e)) for e in v]
        norm[k] = v
    return hashlib.sha256(json.dumps(norm, sort_keys=True, separators=(",", ":"))
                          .encode()).digest()


def cond_index(sy, sx, hy, hx, no_sx, no_hy, no_hx) -> int:
    return ((sy * no_sx + sx) * no_hy + hy) * no_hx + hx


def cond_coords(cond_idx, no_sx, no_hy, no_hx):
    """cond_idx → (sy, sx, hy, hx)"""
    hx = cond_idx % no_hx
    t = cond_idx // no_hx
    hy = t % no_hy
    t //= no_hy
    sx = t % no_sx
    sy = t // no_sx
    return sy, sx, hy, hx


# ─── payload ────────────────────────────────────────────────────────

def encode_f32(arr) -> bytes:
    return np.asarray(arr, dtype="<f4").tobytes()


def encode_u16(arr) -> bytes:
    return np.asarray(arr, dtype="<u2").tobytes()


def decode(payload: bytes, fmt: int, no_cv: int, no_lfv: int,
           calib_ver: str = "v1") -> np.ndarray:
    """payload → (no_cv, no_lfv) float64, 표시 단위(V)로 변환 완료."""
    if fmt == 1:
        a = np.frombuffer(payload, dtype="<f4").astype(np.float64)
    else:
        c = config.CALIB.get(calib_ver) or config.CALIB["v1"]
        a = np.frombuffer(payload, dtype="<u2").astype(np.float64) * c["scale"] + c["offset"]
    return a.reshape(no_cv, no_lfv)


def summarize(arr: np.ndarray):
    return float(arr.min()), float(arr.max()), float(arr.mean())


def best_point(diff: np.ndarray):
    """|값| 최대 지점 → (idx_cv, idx_lfv, value)"""
    k = int(np.nanargmax(np.abs(diff)))
    iy, ix = divmod(k, diff.shape[1])
    return iy, ix, float(diff[iy, ix])


def nan_to_none(a):
    """JSON 직렬화용: ndarray → 중첩 list, NaN → None"""
    if isinstance(a, np.ndarray):
        return [nan_to_none(x) for x in a.tolist()]
    if isinstance(a, list):
        return [nan_to_none(x) for x in a]
    if isinstance(a, float) and math.isnan(a):
        return None
    return a
