#!/usr/bin/env python3
"""PCSW Twin .dat → Single 변환 → DB 적재

    python3 import_dat.py FILE_OR_DIR [...]              # DB 적재 (.env 의 GTS_DB_DSN)
    python3 import_dat.py DIR --single-out OUT_DIR        # + Single 텍스트 파일 생성
    python3 import_dat.py DIR --dry-run --single-out OUT  # DB 없이 변환·검증만
    옵션: --aos N (기본 1)  --no-repair  --force (이미 들어간 파일도 다시)

Twin 파일 1개 → run 2개 + pair_stat 1행
    air_ref run  (Is_Air_P, target_gas='Air(Ref)', label='<원본명> Air')
    gas     run  (Is_Gas_P, target_gas=가스명,    air_ref_run_id → 위 air run)
    pair_stat    (gas, air, 'idf')  — 원본 Idf 와 대조 검증
모두 data_origin='legacy_import', payload_fmt=1(f32), control_source='import'.
"""
import argparse
import json
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gts import config, datfile, pairs                     # noqa: E402
from gts import heatmap as H                               # noqa: E402


def iter_files(paths):
    for p in paths:
        p = Path(p)
        if p.is_dir():
            yield from sorted(p.glob("*.dat"))
        elif p.suffix == ".dat":
            yield p


def verify(df):
    """Single 분해가 원본과 맞는지: Idf 재계산, best 위치 대조 (보정 열 제외)"""
    n_best, n_best_ok = 0, 0
    for h in df.heatmaps:
        if h.best_idx_lfv is None or h.best_idx_cv is None or not h.is_measured:
            continue
        d = h.gas - h.air
        if np.all(np.isnan(d)):
            continue
        if h.best_idx_lfv == d.shape[1] - 1:
            continue            # 원본 best 가 가짜 열에 있었음 — 비교 불가
        n_best += 1
        iy, ix, _ = H.best_point(d)
        if (iy, ix) == (h.best_idx_cv, h.best_idx_lfv):
            n_best_ok += 1
    return n_best, n_best_ok


def get_conn():
    import psycopg
    if not config.DB_DSN:
        sys.exit("GTS_DB_DSN 이 없습니다 (.env 또는 환경변수)")
    return psycopg.connect(config.DB_DSN)


def upsert_grid(cur, df):
    g = datfile.grid_record(df)
    gh = H.grid_hash(g)
    cur.execute("SELECT grid_id FROM grid_def WHERE grid_hash=%s", (gh,))
    r = cur.fetchone()
    if r:
        return r[0]
    cur.execute("""
        INSERT INTO grid_def (grid_hash, mode, no_sx, no_sy, no_hx, no_hy, no_lfv, no_cv,
                              hv_list, frq_list, duty_list, lff_list, lfv_list, cv_list,
                              param_sets, heatmap_count)
        VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s) RETURNING grid_id""",
        (gh, g["mode"], g["no_sx"], g["no_sy"], g["no_hx"], g["no_hy"], g["no_lfv"], g["no_cv"],
         g["hv_list"], g["frq_list"], g["duty_list"], g["lff_list"], g["lfv_list"], g["cv_list"],
         json.dumps(g["param_sets"]) if g["param_sets"] else None, g["heatmap_count"]))
    return cur.fetchone()[0]


def insert_run(cur, df, kind, grid_id, aos, air_run_id, t0, t1):
    h = df.header

    def hi(k):
        v = h.get(k)
        if v is None:
            return None
        try:
            return int(float(v.split("\t")[0]))
        except ValueError:
            return None
    status = "done" if len(df.heatmaps) >= df.expected else "aborted"
    cur.execute("""
        INSERT INTO run (device_id, kind, mode, grid_id, air_ref_run_id, label, target_gas,
                         comment, source_file, filter_type, metric_type, wave_type, duty_mode,
                         lff_mode, wait_delay_ms, avg_count, lfv_settle_ms, cv_settle_ms,
                         data_origin, control_source, requested_by, status, started_at,
                         ended_at, expected_heatmaps, operator)
        VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,
                'legacy_import','import','import_dat.py',%s,%s,%s,%s,%s)
        RETURNING run_id""",
        (aos, kind, df.mode, grid_id, air_run_id,
         f"{df.raw_name} Air" if kind == "air_ref" else df.raw_name,
         "Air(Ref)" if kind == "air_ref" else df.gas_name,
         h.get("Comment") or None, df.path.name,
         hi("FilterType"), hi("MetricType"), hi("WaveType"), hi("DutyMode"), hi("LFFMode"),
         hi("Wait_Delay_ms"), hi("Avg_Count"), hi("LFV_Settle_ms"), hi("CV_Settle_ms"),
         status, t0, t1, df.expected, None))
    return cur.fetchone()[0]


def insert_heatmaps(cur, run_id, df, which):
    g = df.grid
    rows = []
    for seq, h in enumerate(df.heatmaps):
        arr = h.air if which == "air" else h.gas
        fin = arr[np.isfinite(arr)]
        rows.append((run_id, h.cond_idx, seq,
                     H.cond_hash(h.hv, h.frq, h.duty, h.lff, g["lfv_list"], g["cv_list"]),
                     h.hv, h.frq, h.duty, h.lff, h.sy, h.sx, h.hy, h.hx, h.param_no,
                     g["no_lfv"], g["no_cv"], H.encode_f32(arr), 1, h.is_measured,
                     h.best_idx_lfv, h.best_idx_cv,
                     float(fin.min()) if fin.size else None,
                     float(fin.max()) if fin.size else None,
                     float(fin.mean()) if fin.size else None))
    cur.executemany("""
        INSERT INTO heatmap (run_id, cond_idx, seq_no, cond_hash, hv, frq, duty, lff,
                             sy, sx, hy, hx, param_no, no_lfv, no_cv, payload, payload_fmt,
                             is_measured, best_idx_lfv, best_idx_cv, raw_min, raw_max, raw_mean)
        VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s)
        ON CONFLICT DO NOTHING""", rows)


def build_pair(cur, gas_run, air_run, count):
    cur.execute("SELECT * FROM fn_pair_payload(%s,%s)", (gas_run, air_run))
    cols = [d.name for d in cur.description]
    rows = [dict(zip(cols, r)) for r in cur.fetchall()]
    ps = pairs.compute(rows, count)
    cur.execute("""
        INSERT INTO pair_stat (left_run_id, right_run_id, pair_kind, calib_ver, best_val_arr,
                               best_lfv_arr, best_cv_arr, val_min, val_max, best_cond_idx,
                               matched, missing)
        VALUES (%s,%s,'idf','v1',%s,%s,%s,%s,%s,%s,%s,%s)
        ON CONFLICT (left_run_id, right_run_id, pair_kind, calib_ver) DO UPDATE SET
          best_val_arr=EXCLUDED.best_val_arr, best_lfv_arr=EXCLUDED.best_lfv_arr,
          best_cv_arr=EXCLUDED.best_cv_arr, val_min=EXCLUDED.val_min, val_max=EXCLUDED.val_max,
          best_cond_idx=EXCLUDED.best_cond_idx, matched=EXCLUDED.matched,
          missing=EXCLUDED.missing, computed_at=now()""",
        (gas_run, air_run, ps["best_val_arr"], ps["best_lfv_arr"], ps["best_cv_arr"],
         ps["val_min"], ps["val_max"], ps["best_cond_idx"], ps["matched"], ps["missing"]))
    return ps


def import_one(conn, df, aos, force):
    with conn.cursor() as cur:
        cur.execute("SELECT run_id FROM run WHERE source_file=%s AND data_origin='legacy_import'",
                    (df.path.name,))
        old = [r[0] for r in cur.fetchall()]
        if old and not force:
            return None, "이미 적재됨 (건너뜀, --force 로 재적재)"
        if old:
            cur.execute("UPDATE run SET air_ref_run_id=NULL WHERE run_id = ANY(%s)", (old,))
            cur.execute("DELETE FROM run WHERE run_id = ANY(%s)", (old,))

        cur.execute("""INSERT INTO device (dev_type, device_id, name) VALUES (1,%s,%s)
                       ON CONFLICT DO NOTHING""", (aos, f"AOS-{aos:02d}"))
        grid_id = upsert_grid(cur, df)
        t1 = datetime.strptime(df.save_time, "%Y-%m-%d %H:%M:%S") if df.save_time else datetime.now()
        t0 = t1 - timedelta(seconds=df.total_sec or 0)
        air = insert_run(cur, df, "air_ref", grid_id, aos, None, t0, t1)
        gas = insert_run(cur, df, "gas", grid_id, aos, air, t0, t1)
        insert_heatmaps(cur, air, df, "air")
        insert_heatmaps(cur, gas, df, "gas")
        ps = build_pair(cur, gas, air, df.expected)
        for rid in (air, gas):
            cur.execute("INSERT INTO run_event (run_id, kind, detail) VALUES (%s,'done',%s)",
                        (rid, json.dumps({"source": df.path.name, "summary": df.summary,
                                          "stored": len(df.heatmaps), "expected": df.expected})))
        cur.execute("""INSERT INTO sys_event (level, dev_type, dev_id, kind, text)
                       VALUES ('ok',1,%s,'import',%s)""",
                    (aos, f"AOS {aos:02d} · {df.path.name} 임포트 ({len(df.heatmaps)} heatmap)"))
    conn.commit()
    return (air, gas, ps), None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--aos", type=int, default=1, help="적재할 AOS ID (기본 1)")
    ap.add_argument("--single-out", help="Single 텍스트 파일 출력 폴더")
    ap.add_argument("--dry-run", action="store_true", help="DB 에 쓰지 않음")
    ap.add_argument("--no-repair", action="store_true", help="LFV 마지막 열 NaN 처리 안 함")
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()

    files = list(iter_files(a.paths))
    if not files:
        sys.exit("대상 .dat 파일이 없습니다")
    conn = None if a.dry_run else get_conn()

    print(f"{'파일':<46} {'모드':<6} {'HM':>9} {'손상서명':>9} {'best일치':>9}  가스명 → 결과")
    total_t = time.time()
    for p in files:
        t = time.time()
        try:
            df = datfile.parse(p, repair=not a.no_repair)
        except Exception as e:
            print(f"{p.name:<46} 파싱 실패: {e!r}")
            continue
        nb, nb_ok = verify(df)
        cr = (f"{100 * df.corrupt_match / df.corrupt_total:.0f}%" if df.corrupt_total else "-")
        line = (f"{p.name[:46]:<46} {df.mode:<6} {len(df.heatmaps):>4}/{df.expected:<4} "
                f"{cr:>9} {nb_ok:>4}/{nb:<4}  {df.gas_name}")
        if a.single_out:
            datfile.write_single(df, a.single_out, "air")
            datfile.write_single(df, a.single_out, "gas")
        if conn:
            res, msg = import_one(conn, df, a.aos, a.force)
            if res:
                air, gas, ps = res
                line += f" → run air={air} gas={gas} (Idf {ps['val_min']:.3f}~{ps['val_max']:.3f})"
            else:
                line += f" → {msg}"
        print(line + f"  [{time.time() - t:.1f}s]", flush=True)
    print(f"완료 {len(files)}개, {time.time() - total_t:.1f}s")


if __name__ == "__main__":
    main()
