#!/usr/bin/env python3
"""임포트(legacy) Air(Ref) 정리 — 2026-09-23

Twin 파일마다 Air 반쪽이 "<원본명> Air" 로 올라가 Air 목록에 ACV·바나나 같은 이름이 섞여 보였다.
→ 임포트 Air(Ref) run 을 모두 지우고, 지정한 파일들의 Air 반쪽만 air1, air2 … 이름으로 다시 올린다.

    python3 air_reset.py ~/import              # 기본 4개 (AIR_FILES)
    python3 air_reset.py ~/import --dry-run    # 지울 것 / 올릴 것만 보여 줌
    python3 air_reset.py ~/import --files a.dat b.dat ...   # 파일 직접 지정 (순서대로 air1..)

- 실측(measured) 데이터는 건드리지 않는다
- 가스 run 은 그대로 둔다. 기록된 짝(air_ref_run_id)은 비우고, 새 airN 과 같은 원본 파일의
  가스 run 만 그 airN 에 다시 연결한다 (Idf 요약도 계산)
- 임포트 데이터는 HV 범위가 달라도 같은 위치(cond_idx)끼리 비교한다 (runs.py 의 위치 매칭)
"""
import argparse
import sys
from datetime import datetime, timedelta
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gts import datfile                                     # noqa: E402
from import_dat import get_conn, upsert_grid, insert_heatmaps, build_pair   # noqa: E402

# 원본 파일명 앞부분 (날짜 부분이 달라도 찾도록 접두어로 비교)
AIR_FILES = [
    "암모니아_air_3_4x4",
    "acetone_air_2_4x4",
    "fishSource_air_3_4x4",
    "Sasami_air_1_4x4",
]


def find_files(folder, names):
    out = []
    allf = sorted(Path(folder).glob("*.dat"))
    for n in names:
        hit = [p for p in allf if p.name == n or p.name.startswith(n)]
        if not hit:
            sys.exit(f"파일 없음: {n}  (폴더 {folder})")
        out.append(hit[0])
    return out


def insert_air(cur, df, grid_id, aos, label):
    t1 = datetime.strptime(df.save_time, "%Y-%m-%d %H:%M:%S") if df.save_time else datetime.now()
    t0 = t1 - timedelta(seconds=df.total_sec or 0)
    status = "done" if len(df.heatmaps) >= df.expected else "aborted"
    cur.execute("""
        INSERT INTO run (device_id, kind, mode, grid_id, label, target_gas, comment, source_file,
                         data_origin, control_source, requested_by, status, started_at, ended_at,
                         expected_heatmaps)
        VALUES (%s,'air_ref',%s,%s,%s,'Air(Ref)',%s,%s,'legacy_import','import','air_reset.py',
                %s,%s,%s,%s)
        RETURNING run_id""",
        (aos, df.mode, grid_id, label, f"원본 {df.path.name} 의 Air 반쪽", df.path.name,
         status, t0, t1, df.expected))
    return cur.fetchone()[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", help="Twin .dat 폴더 (gts-import 가 올려 둔 ~/import)")
    ap.add_argument("--files", nargs="+", help="Air 로 쓸 파일 (순서대로 air1, air2 …)")
    ap.add_argument("--aos", type=int, default=1)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    files = find_files(a.folder, a.files or AIR_FILES)
    conn = get_conn()
    with conn.cursor() as cur:
        cur.execute("""SELECT run_id, label, source_file FROM run
                        WHERE kind='air_ref' AND data_origin='legacy_import' ORDER BY run_id""")
        old = cur.fetchall()
        print(f"삭제할 임포트 Air(Ref) run {len(old)}개")
        for r in old:
            print(f"   #{r[0]:<5} {r[1]}   ({r[2]})")
        print("새로 올릴 Air")
        for i, p in enumerate(files, 1):
            print(f"   air{i}  ←  {p.name}")
        if a.dry_run:
            print("--dry-run: 변경 없음")
            return

        ids = [r[0] for r in old]
        if ids:
            cur.execute("UPDATE run SET air_ref_run_id=NULL WHERE air_ref_run_id = ANY(%s)", (ids,))
            cur.execute("DELETE FROM run WHERE run_id = ANY(%s)", (ids,))   # heatmap·pair_stat 은 CASCADE

        for i, p in enumerate(files, 1):
            label = f"air{i}"
            df = datfile.parse(p, repair=True)
            gid = upsert_grid(cur, df)
            air = insert_air(cur, df, gid, a.aos, label)
            insert_heatmaps(cur, air, df, "air")
            cur.execute("""SELECT run_id FROM run WHERE kind='gas' AND data_origin='legacy_import'
                            AND source_file=%s""", (p.name,))
            g = cur.fetchone()
            msg = ""
            if g:
                cur.execute("UPDATE run SET air_ref_run_id=%s WHERE run_id=%s", (air, g[0]))
                ps = build_pair(cur, g[0], air, df.expected)
                msg = f" · 가스 run #{g[0]} 짝 연결 (Idf {ps['val_min']:.3f}~{ps['val_max']:.3f})"
            print(f"   {label} = run #{air}  {len(df.heatmaps)} heatmap{msg}", flush=True)
        cur.execute("""INSERT INTO sys_event (level, dev_type, dev_id, kind, text)
                       VALUES ('ok',1,%s,'import',%s)""",
                    (a.aos, f"임포트 Air(Ref) 정리: {len(ids)}개 삭제, air1~air{len(files)} 적재"))
    conn.commit()
    print("완료")


if __name__ == "__main__":
    main()
