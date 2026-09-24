"""pair_stat 계산 — Idf(Target−Air) 와 장치 비교(A−B) 공용 (D12b)"""
from __future__ import annotations

import numpy as np

from . import heatmap as H


def compute(rows, heatmap_count: int, left_calib="v1", right_calib="v1") -> dict:
    """rows: fn_pair_payload() 결과 (dict-like: cond_idx, no_cv, no_lfv,
             left_payload, left_fmt, right_payload, right_fmt)
    @return pair_stat 컬럼 dict"""
    best_val = np.full(heatmap_count, np.nan, dtype="<f4")
    best_lfv = np.full(heatmap_count, 255, dtype=np.uint8)
    best_cv = np.full(heatmap_count, 255, dtype=np.uint8)
    matched = 0
    for r in rows:
        ci = r["cond_idx"]
        if ci >= heatmap_count:
            continue
        L = H.decode(bytes(r["left_payload"]), r["left_fmt"], r["no_cv"], r["no_lfv"], left_calib)
        R = H.decode(bytes(r["right_payload"]), r["right_fmt"], r["no_cv"], r["no_lfv"], right_calib)
        d = L - R
        if np.all(np.isnan(d)):
            continue
        iy, ix, v = H.best_point(d)
        best_val[ci] = v
        best_cv[ci] = iy
        best_lfv[ci] = ix
        matched += 1
    finite = best_val[np.isfinite(best_val)]
    if finite.size:
        bi = int(np.nanargmax(np.abs(best_val)))
        vmin, vmax = float(finite.min()), float(finite.max())
    else:
        bi, vmin, vmax = None, None, None
    return dict(best_val_arr=best_val.tobytes(), best_lfv_arr=best_lfv.tobytes(),
                best_cv_arr=best_cv.tobytes(), val_min=vmin, val_max=vmax,
                best_cond_idx=bi, matched=matched, missing=heatmap_count - matched)


def decode_summary(ps: dict) -> dict:
    vals = np.frombuffer(bytes(ps["best_val_arr"]), dtype="<f4").astype(float)
    lfv = np.frombuffer(bytes(ps["best_lfv_arr"]), dtype=np.uint8) if ps.get("best_lfv_arr") else None
    cv = np.frombuffer(bytes(ps["best_cv_arr"]), dtype=np.uint8) if ps.get("best_cv_arr") else None
    return dict(best_val=H.nan_to_none(vals),
                best_idx_lfv=None if lfv is None else [None if x == 255 else int(x) for x in lfv],
                best_idx_cv=None if cv is None else [None if x == 255 else int(x) for x in cv])
