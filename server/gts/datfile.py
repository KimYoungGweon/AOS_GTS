"""PCSW Twin .dat 파서 + Single 변환

입력  : "FAIMS Twin Scan Data V1"   (full8 10×10 / hour1 4×4 섹션 × 4×4 heatmap)
        "FAIMs Twin Sample Data V1" (fast, Parameter Sets 8조합)
출력  : Single 모델 — Twin 파일 1개를 run 2개로 가른다
          air_ref run  ← Is_Air_P
          gas     run  ← Is_Gas_P
        버리는 것: Is_*_N (전부 0), Idf (계산값), Refinement/Fine Section/Trace

데이터 보정 (DOC/AOS_H753_V1_Single_프로그램_수정_사항_정리.md 3-1)
  구 F/W 의 line_all_buf[11][15] overrun 으로 모든 행의 마지막 열(LFV=3.0V)은
  다음 행 첫 값의 복사본이거나(0~9행) 배열 밖 메모리(10행)다.
  → 마지막 열을 NaN 으로 표시한다 (repair=True).  손상 서명 일치율도 보고한다.
"""
from __future__ import annotations

import math
import re
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

RE_COARSE = re.compile(r"^=== Coarse Section \[sY=(\d+),sX=(\d+)\] ===")
RE_HM = re.compile(r"^--- Heatmap \[hY=(\d+),hX=(\d+)\] Duty=([-\d.eE+]+) LF_Frq=([-\d.eE+]+) ---")
RE_SAMPLE = re.compile(r"^=== Sample\[(\d+)\] Heatmap ===")


@dataclass
class Heatmap:
    cond_idx: int
    hv: float
    frq: float
    duty: float
    lff: float
    sy: int | None = None
    sx: int | None = None
    hy: int | None = None
    hx: int | None = None
    param_no: int | None = None
    is_measured: bool = True
    best_idx_lfv: int | None = None
    best_idx_cv: int | None = None
    best_idf: float | None = None
    air: np.ndarray | None = None      # (no_cv, no_lfv)
    gas: np.ndarray | None = None


@dataclass
class DatFile:
    path: Path
    kind: str                      # 'scan' | 'sample'
    header: dict = field(default_factory=dict)
    grid: dict = field(default_factory=dict)
    heatmaps: list = field(default_factory=list)
    summary: dict = field(default_factory=dict)
    corrupt_match: int = 0         # row[i][15] == row[i+1][0] 인 행 수
    corrupt_total: int = 0

    # ── 파생값 ────────────────────────────────────────────────────
    @property
    def mode(self) -> str:
        if self.kind == "sample":
            return "fast"
        return "full8" if self.grid["no_sx"] * self.grid["no_sy"] >= 100 else "hour1"

    @property
    def expected(self) -> int:
        return self.grid["heatmap_count"]

    @property
    def raw_name(self) -> str:
        return self.header.get("TargetGas", self.path.stem)

    @property
    def gas_name(self) -> str:
        return derive_gas_name(self.raw_name)

    @property
    def save_time(self) -> str | None:
        return self.header.get("SaveTime")

    @property
    def total_sec(self) -> int | None:
        for k in ("TotalTime_sec", "proc_sec"):
            v = self.summary.get(k) or self.header.get(k)
            if v:
                try:
                    return int(float(v))
                except ValueError:
                    pass
        return None


def derive_gas_name(raw: str) -> str:
    """'air_lavender_1' → 'lavender', 'Musk_air' → 'Musk', 'AIR-ACV_1' → 'ACV',
    '암모니아_air_3' → '암모니아', 'air_air' → 'air'.  (웹에서 언제든 수정 가능)"""
    toks = [t for t in re.split(r"[_\-\s]+", raw) if t]
    keep = [t for t in toks if t.lower() != "air" and not t.isdigit()]
    if not keep:
        return "air"
    return "_".join(keep)


def _num(s: str):
    try:
        return float(s)
    except ValueError:
        return None


def _int_first(s: str):
    return int(float(s.split("\t")[0]))


def parse(path, repair: bool = True) -> DatFile:
    path = Path(path)
    lines = path.read_text(encoding="utf-8-sig", errors="replace").splitlines()
    first = lines[0].strip()
    if "Sample Data" in first:
        df = DatFile(path, "sample")
    elif "Scan Data" in first:
        df = DatFile(path, "scan")
    else:
        raise ValueError(f"알 수 없는 형식: {first!r}")

    n = len(lines)
    i = 1
    # ── 헤더 ─────────────────────────────────────────────────────
    while i < n and not lines[i].startswith("==="):
        if "\t" in lines[i]:
            k, v = lines[i].split("\t", 1)
            df.header[k.strip()] = v.strip()
        i += 1

    g = df.grid

    def read_block(j, no_cv):
        rows = []
        for k in range(no_cv):
            rows.append([float(x) for x in lines[j + k].split("\t") if x != ""])
        return np.array(rows, dtype=np.float64)

    cur_sec = None       # (sy, sx, hv, frq)
    cur = None
    while i < n:
        ln = lines[i]

        if ln.startswith("=== Grid Definition") or ln.startswith("=== Heatmap Grid"):
            i += 1
            while i < n and lines[i].strip():
                k, v = lines[i].split("\t", 1)
                vals = [float(x) for x in v.split("\t") if x != ""]
                g[k.strip()] = vals
                i += 1
            continue

        if ln.startswith("=== Parameter Sets"):
            i += 2               # 헤더 행 건너뜀
            sets = []
            while i < n and lines[i].strip():
                no, volt, frq, duty, lff = [float(x) for x in lines[i].split("\t")[:5]]
                sets.append(dict(no=int(no), hv=volt, frq=frq, duty=duty, lff=lff))
                i += 1
            g["param_sets"] = sets
            continue

        if ln.startswith("=== Refinement Result") or ln.startswith("=== Fine Section"):
            # Single 에서는 refinement 동작 자체가 삭제됨 → 전부 버린다
            i += 1
            while i < n and not lines[i].startswith("=== Summary"):
                i += 1
            continue

        if ln.startswith("=== Summary"):
            i += 1
            while i < n and lines[i].strip() and not lines[i].startswith("==="):
                if "\t" in lines[i]:
                    k, v = lines[i].split("\t", 1)
                    df.summary[k.strip()] = v.strip()
                i += 1
            continue

        m = RE_COARSE.match(ln)
        if m:
            sy, sx = int(m.group(1)), int(m.group(2))
            hv = frq = None
            j = i + 1
            while j < n and not lines[j].startswith("---") and not lines[j].startswith("==="):
                if lines[j].startswith("HV\t"):
                    hv = float(lines[j].split("\t")[1])
                elif lines[j].startswith("Frq\t"):
                    frq = float(lines[j].split("\t")[1])
                j += 1
            cur_sec = (sy, sx, hv, frq)
            i = j
            continue

        m = RE_HM.match(ln) or RE_SAMPLE.match(ln)
        if m:
            _finalize_grid(df)
            if df.kind == "scan":
                hy, hx = int(m.group(1)), int(m.group(2))
                sy, sx, hv, frq = cur_sec
                cond = ((sy * g["no_sx"] + sx) * g["no_hy"] + hy) * g["no_hx"] + hx
                cur = Heatmap(cond, hv, frq, float(m.group(3)), float(m.group(4)),
                              sy=sy, sx=sx, hy=hy, hx=hx)
            else:
                idx = int(m.group(1))
                ps = g["param_sets"][idx]
                cur = Heatmap(idx, ps["hv"], ps["frq"], ps["duty"], ps["lff"],
                              param_no=idx + 1)
            j = i + 1
            no_cv = g["no_cv"]
            while j < n and not lines[j].startswith("---") and not lines[j].startswith("==="):
                t = lines[j]
                if t.startswith("IsMeasured\t"):
                    v = t.split("\t")[1].strip()
                    cur.is_measured = v in ("1", "True", "true")
                elif t.startswith("Best_idxLFV\t"):
                    cur.best_idx_lfv = _int_first(t.split("\t", 1)[1])
                elif t.startswith("Best_idxCV\t"):
                    cur.best_idx_cv = _int_first(t.split("\t", 1)[1])
                elif t.startswith("Best_Idf\t"):
                    cur.best_idf = _num(t.split("\t")[1])
                elif t.strip() == "Is_Air_P":
                    cur.air = read_block(j + 1, no_cv)
                    j += no_cv
                elif t.strip() == "Is_Gas_P":
                    cur.gas = read_block(j + 1, no_cv)
                    j += no_cv
                elif t.strip() in ("Is_Air_N", "Is_Gas_N", "Idf"):
                    j += no_cv          # 버림
                j += 1
            if cur.air is not None and cur.gas is not None:
                for arr in (cur.air, cur.gas):
                    df.corrupt_total += arr.shape[0] - 1
                    df.corrupt_match += int(np.sum(arr[:-1, -1] == arr[1:, 0]))
                if repair:
                    cur.air[:, -1] = np.nan
                    cur.gas[:, -1] = np.nan
                df.heatmaps.append(cur)
            i = j
            continue

        i += 1

    _finalize_grid(df)
    return df


def _finalize_grid(df: DatFile):
    g = df.grid
    if "no_cv" in g:
        return
    if df.kind == "scan":
        g["no_sx"] = int(g["SectionXNo"][0]); g["no_sy"] = int(g["SectionYNo"][0])
        g["no_hx"] = int(g["HeatmapXNo"][0]); g["no_hy"] = int(g["HeatmapYNo"][0])
        g["no_lfv"] = int(g["LFV_No"][0]);    g["no_cv"] = int(g["CV_No"][0])
        g["hv_list"] = g["HV_List"];   g["frq_list"] = g["Frq_List"]
        g["duty_list"] = g["Duty_List"]; g["lff_list"] = g["LFF_List"]
        g["heatmap_count"] = g["no_sx"] * g["no_sy"] * g["no_hx"] * g["no_hy"]
        g["param_sets"] = None
    else:
        g["no_lfv"] = int(g["noLFV"][0]); g["no_cv"] = int(g["noCV"][0])
        for k in ("no_sx", "no_sy", "no_hx", "no_hy", "hv_list", "frq_list",
                  "duty_list", "lff_list"):
            g[k] = None
        g["heatmap_count"] = len(g["param_sets"])
    g["lfv_list"] = g["LFV_List"]
    g["cv_list"] = g["CV_List"]
    g["mode"] = df.mode


def grid_record(df: DatFile) -> dict:
    g = df.grid
    return {k: g[k] for k in ("mode", "no_sx", "no_sy", "no_hx", "no_hy", "no_lfv", "no_cv",
                              "hv_list", "frq_list", "duty_list", "lff_list",
                              "lfv_list", "cv_list", "param_sets", "heatmap_count")}


# ─── Single 파일 쓰기 ────────────────────────────────────────────────

def _fmt(v):
    return "NaN" if (v is None or (isinstance(v, float) and math.isnan(v))) else f"{v:.6g}"


def write_single(df: DatFile, out_dir, which: str) -> Path:
    """which = 'air' | 'gas'.  AOS Single 모델(1 채널, Positive) 텍스트 파일."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    kind = "air_ref" if which == "air" else "gas"
    stem = df.path.stem
    out = out_dir / f"{stem}__{kind}.single.dat"
    g = df.grid
    with out.open("w", encoding="utf-8", newline="\n") as f:
        f.write("AOS Single Heatmap Data V1\n")
        f.write(f"Kind\t{kind}\n")
        f.write(f"TargetGas\t{'Air(Ref)' if which == 'air' else df.gas_name}\n")
        f.write(f"SourceTargetGas\t{df.raw_name}\n")
        f.write(f"SourceFile\t{df.path.name}\n")
        f.write(f"Comment\t{df.header.get('Comment', '')}\n")
        f.write(f"SaveTime\t{df.save_time or ''}\n")
        f.write(f"Mode\t{df.mode}\n")
        f.write("Channel\tIs_P (Positive only)\n")
        f.write("Repair\tLFV[15] = NaN (구 F/W line_all_buf overrun)\n")
        for k in ("FilterType", "MetricType", "WaveType", "DutyMode", "LFFMode",
                  "Wait_Delay_ms", "Avg_Count", "LFV_Settle_ms", "CV_Settle_ms"):
            if k in df.header:
                f.write(f"{k}\t{df.header[k]}\n")
        f.write(f"Expected\t{df.expected}\nStored\t{len(df.heatmaps)}\n\n")
        f.write("=== Grid ===\n")
        for k in ("no_sx", "no_sy", "no_hx", "no_hy", "no_lfv", "no_cv"):
            if g.get(k) is not None:
                f.write(f"{k}\t{g[k]}\n")
        for k in ("hv_list", "frq_list", "duty_list", "lff_list", "lfv_list", "cv_list"):
            if g.get(k) is not None:
                f.write(k + "\t" + "\t".join(_fmt(v) for v in g[k]) + "\n")
        if g.get("param_sets"):
            f.write("param_sets\tNo\tHV\tFrq\tDuty\tLFF\n")
            for p in g["param_sets"]:
                f.write(f"\t{p['no']}\t{_fmt(p['hv'])}\t{_fmt(p['frq'])}\t"
                        f"{_fmt(p['duty'])}\t{_fmt(p['lff'])}\n")
        f.write("\n")
        for h in df.heatmaps:
            arr = h.air if which == "air" else h.gas
            pos = (f"sY={h.sy},sX={h.sx},hY={h.hy},hX={h.hx}" if h.sy is not None
                   else f"No={h.param_no}")
            f.write(f"--- Heatmap cond={h.cond_idx} [{pos}] HV={_fmt(h.hv)} Frq={_fmt(h.frq)} "
                    f"Duty={_fmt(h.duty)} LFF={_fmt(h.lff)} Measured={int(h.is_measured)} ---\n")
            for row in arr:
                f.write("\t".join(_fmt(float(v)) for v in row) + "\n")
        f.write("=== END ===\n")
    return out
