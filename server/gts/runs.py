"""측정 run · heatmap · pair 조회/수명주기 (asyncpg)

run 수명주기 (설계안 9절)
  start  → run(status=running) + tele_gfc 파티션 attach + run_event(start)
  pause  → status=paused, 파티션 유지 (재개 시 이어서)
  resume → fn_missing_cond() 로 남은 조건, status=running, resume_count+1
  finish → status=done   ┐ GFC 요약 + 10초 다운샘플 → run_telemetry_summary
  abort  → status=aborted┘ 그 뒤 파티션 DROP

⚠ 장치에 측정 시작(0x80)을 실제로 내려보내는 경로는 아직 없다 (AOS 브리지에 heatmap
  측정 명령 미구현, 인수인계 8절).  지금은 DB 쪽 run 기록과 heatmap 적재 경로만 연다.
  0x86 UDP 수신이 구현되면 ingest_heatmap() 을 그대로 호출하면 된다.
"""
from __future__ import annotations

import json

import numpy as np

from . import heatmap as H
from . import live, pairs
from .db import store

# ── 현행 격자 정의 (DOC/GTS_DB_Design/GTS_DB_Design.md 6·7절) ─────────
LFV = [round(0.2 * i, 1) for i in range(16)]
CV = [round(-1 + 0.2 * i, 1) for i in range(11)]
DEFAULT_GRID = {
    "full8": dict(mode="full8", no_sx=10, no_sy=10, no_hx=4, no_hy=4, no_lfv=16, no_cv=11,
                  hv_list=[45, 60, 75, 90, 105, 120, 135, 150, 165, 180],
                  frq_list=[200, 266.7, 333.3, 400, 466.7, 533.3, 600, 666.7, 733.3, 800],
                  duty_list=[50, 55, 60, 65], lff_list=[50, 100, 150, 200],
                  lfv_list=LFV, cv_list=CV, param_sets=None, heatmap_count=1600),
    "hour1": dict(mode="hour1", no_sx=4, no_sy=4, no_hx=4, no_hy=4, no_lfv=16, no_cv=11,
                  hv_list=[45, 90, 135, 180], frq_list=[200, 400, 600, 800],
                  duty_list=[50, 55, 60, 65], lff_list=[50, 100, 150, 200],
                  lfv_list=LFV, cv_list=CV, param_sets=None, heatmap_count=256),
}
# 섹션 X = HV, 섹션 Y = Frq, heatmap X = Duty, heatmap Y = LFF  (.dat 샘플과 동일)


class RunError(Exception):
    def __init__(self, msg, http=400):
        super().__init__(msg)
        self.msg, self.http = msg, http


def _pool():
    if not store.ok:
        raise RunError("DB 연결 없음", 503)
    return store.pool


def _row(r):
    if r is None:
        return None
    d = dict(r)
    for k, v in list(d.items()):
        if hasattr(v, "isoformat"):
            d[k] = v.isoformat()
        elif isinstance(v, (bytes, bytearray, memoryview)):
            d.pop(k)
        elif k in ("run_uid",):
            d[k] = str(v)
        elif k in ("ambient", "param_sets", "detail", "gfc") and isinstance(v, str):
            d[k] = json.loads(v)
    return d


# ── 격자 ───────────────────────────────────────────────────────────

async def ensure_grid(c, g: dict) -> int:
    gh = H.grid_hash(g)
    gid = await c.fetchval("SELECT grid_id FROM grid_def WHERE grid_hash=$1", gh)
    if gid:
        return gid
    return await c.fetchval("""
        INSERT INTO grid_def (grid_hash, mode, no_sx, no_sy, no_hx, no_hy, no_lfv, no_cv,
                              hv_list, frq_list, duty_list, lff_list, lfv_list, cv_list,
                              param_sets, heatmap_count)
        VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16)
        ON CONFLICT (grid_hash) DO UPDATE SET grid_hash=EXCLUDED.grid_hash RETURNING grid_id""",
        gh, g["mode"], g.get("no_sx"), g.get("no_sy"), g.get("no_hx"), g.get("no_hy"),
        g["no_lfv"], g["no_cv"], g.get("hv_list"), g.get("frq_list"), g.get("duty_list"),
        g.get("lff_list"), g["lfv_list"], g["cv_list"],
        json.dumps(g["param_sets"]) if g.get("param_sets") else None, g["heatmap_count"])


async def get_grid(c, grid_id):
    g = await c.fetchrow("SELECT * FROM grid_def WHERE grid_id=$1", grid_id)
    if g is None:
        raise RunError("grid 없음", 404)
    d = dict(g)
    d.pop("grid_hash", None)
    if isinstance(d.get("param_sets"), str):
        d["param_sets"] = json.loads(d["param_sets"])
    d["created_at"] = d["created_at"].isoformat()
    for k in ("hv_list", "frq_list", "duty_list", "lff_list", "lfv_list", "cv_list"):
        if d.get(k) is not None:
            d[k] = [_rnd(v) for v in d[k]]
    return d


def _rnd(v, n=4):
    return None if v is None else round(float(v), n)


# ── 수명주기 ───────────────────────────────────────────────────────

async def start_run(device_id, kind, mode, *, target_gas=None, label=None, comment=None,
                    ambient=None, air_ref_run_id=None, concentration=None, param_sets=None,
                    grid=None, source="web", actor=None):
    if kind not in ("air_ref", "gas"):
        raise RunError("kind 는 air_ref | gas")
    if mode == "fast":
        if not param_sets or len(param_sets) < 1:
            raise RunError("fast 모드는 param_sets(최대 8조합)가 필요합니다")
        g = dict(mode="fast", no_lfv=16, no_cv=11, lfv_list=LFV, cv_list=CV,
                 param_sets=[dict(no=i + 1, **{k: float(p[k]) for k in ("hv", "frq", "duty", "lff")})
                             for i, p in enumerate(param_sets)],
                 heatmap_count=len(param_sets))
    elif grid:
        g = grid
    elif mode in DEFAULT_GRID:
        g = DEFAULT_GRID[mode]
    else:
        raise RunError("mode 는 fast | hour1 | full8")
    import udp_server as _S
    if device_id in _S.CAL_BLOCK:
        raise RunError(f"AOS {device_id:02d} 캘리브레이션 중 — run 을 시작할 수 없습니다", 409)
    pool = _pool()
    async with pool.acquire() as c:
        async with c.transaction():
            await c.execute("""INSERT INTO device (dev_type, device_id, name) VALUES (1,$1,$2)
                               ON CONFLICT DO NOTHING""", device_id, f"AOS-{device_id:02d}")
            busy = await c.fetchval("SELECT run_id FROM run WHERE device_id=$1 AND status='running'",
                                    device_id)
            if busy:
                raise RunError(f"AOS {device_id:02d} 에서 run #{busy} 가 이미 진행 중", 409)
            if target_gas is None and kind == "gas":
                target_gas = await c.fetchval("SELECT gas_name FROM pair_config WHERE pair_id=$1",
                                              device_id)
            if kind == "air_ref":
                target_gas = "Air(Ref)"
                air_ref_run_id = None
            gid = await ensure_grid(c, g)
            run_id = await c.fetchval("""
                INSERT INTO run (device_id, kind, mode, grid_id, air_ref_run_id, label, ambient,
                                 target_gas, concentration, comment, operator, control_source,
                                 requested_by, expected_heatmaps)
                VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12::ctl_source,$13,$14)
                RETURNING run_id""",
                device_id, kind, mode, gid, air_ref_run_id, label,
                json.dumps(ambient) if ambient else None, target_gas, concentration, comment,
                actor, source, actor, g["heatmap_count"])
            await c.execute("SELECT tele_gfc_attach($1)", run_id)
            await c.execute("INSERT INTO run_event (run_id, kind, detail) VALUES ($1,'start',$2)",
                            run_id, json.dumps({"by": actor, "source": source}))
    store.active_run[device_id] = run_id
    live.add_event("warn" if kind == "air_ref" else "info", "run_start",
                   f"AOS {device_id:02d} · {'기준(Air)' if kind == 'air_ref' else target_gas} "
                   f"측정 시작 — {mode} (run #{run_id})", 1, device_id)
    return await get_run(run_id)


async def _set_status(run_id, allowed_from, new_status, event_kind, detail, extra_sql=""):
    pool = _pool()
    async with pool.acquire() as c:
        async with c.transaction():
            r = await c.fetchrow("SELECT * FROM run WHERE run_id=$1 FOR UPDATE", run_id)
            if r is None:
                raise RunError("run 없음", 404)
            if r["status"] not in allowed_from:
                raise RunError(f"run #{run_id} 상태가 {r['status']} — {new_status} 불가", 409)
            await c.execute(f"UPDATE run SET status=$2::run_status {extra_sql} WHERE run_id=$1",
                            run_id, new_status)
            await c.execute("INSERT INTO run_event (run_id, kind, detail) VALUES ($1,$2,$3)",
                            run_id, event_kind, json.dumps(detail))
            return r


async def pause_run(run_id, actor, reason=None):
    r = await _set_status(run_id, ("running",), "paused", "pause", {"by": actor, "reason": reason},
                          ", last_paused_at=now()")
    store.active_run.pop(r["device_id"], None)
    live.add_event("warn", "run_pause", f"AOS {r['device_id']:02d} · run #{run_id} 일시정지", 1,
                   r["device_id"])
    return await get_run(run_id)


async def resume_run(run_id, actor):
    pool = _pool()
    async with pool.acquire() as c:
        r = await c.fetchrow("SELECT device_id FROM run WHERE run_id=$1", run_id)
        if r is None:
            raise RunError("run 없음", 404)
        busy = await c.fetchval("""SELECT run_id FROM run WHERE device_id=$1 AND status='running'
                                   AND run_id<>$2""", r["device_id"], run_id)
        if busy:
            raise RunError(f"같은 AOS 에서 run #{busy} 진행 중", 409)
        remaining = [x["cond_idx"] for x in await c.fetch("SELECT * FROM fn_missing_cond($1)", run_id)]
    await _set_status(run_id, ("paused",), "running", "resume",
                      {"by": actor, "remaining": len(remaining)},
                      ", resume_count=resume_count+1, last_resumed_at=now()")
    store.active_run[r["device_id"]] = run_id
    live.add_event("info", "run_resume", f"AOS {r['device_id']:02d} · run #{run_id} 재개 "
                                         f"(남은 {len(remaining)} heatmap)", 1, r["device_id"])
    out = await get_run(run_id)
    out["remaining_cond_idx"] = remaining
    return out


async def _close_run(run_id, status, actor, reason):
    r = await _set_status(run_id, ("running", "paused"), status, "done" if status == "done" else "abort",
                          {"by": actor, "reason": reason}, ", ended_at=now()")
    store.active_run.pop(r["device_id"], None)
    await store._flush_gfc()                 # 버퍼에 남은 이 run 의 1초 데이터부터 넣고
    await build_telemetry_summary(run_id)
    async with _pool().acquire() as c:
        await c.execute("SELECT tele_gfc_drop($1)", run_id)
    got = await get_run(run_id)
    level = "ok" if status == "done" else "warn"
    live.add_event(level, f"run_{status}",
                   f"AOS {r['device_id']:02d} · run #{run_id} {'완료' if status == 'done' else '중단'} "
                   f"({got['received_heatmaps']}/{got['expected_heatmaps']} heatmap 저장)", 1,
                   r["device_id"])
    return got


async def finish_run(run_id, actor):
    return await _close_run(run_id, "done", actor, None)


async def abort_run(run_id, actor, reason=None):
    return await _close_run(run_id, "aborted", actor, reason)


TELE_10S_COLS = ["t_rel_s", "volt1", "volt2", "ctrl", "sv", "pump1", "pump2", "pump3"]


async def build_telemetry_summary(run_id):
    """GFC 1초 → 요약 json + 10초 평균 다운샘플 (D1).  파티션 DROP 전에 호출"""
    async with _pool().acquire() as c:
        exists = await c.fetchval("SELECT to_regclass($1)", f"tele_gfc_r{run_id}")
        if not exists:
            return
        s = await c.fetchrow(f"""
            SELECT count(*) n, min(ts) t0, max(ts) t1,
                   min(volt1) v1min, max(volt1) v1max, avg(volt1) v1avg,
                   min(volt2) v2min, max(volt2) v2max, avg(volt2) v2avg,
                   sum(CASE WHEN pump2 = 1 THEN 1 ELSE 0 END) p2on
              FROM tele_gfc_r{run_id}""")
        if not s["n"]:
            return
        rows = await c.fetch(f"""
            SELECT floor(extract(epoch FROM ts - $1) / 10) * 10 AS t,
                   avg(volt1) v1, avg(volt2) v2, avg(ctrl) c, avg(sv) sv,
                   avg(pump1) p1, avg(pump2) p2, avg(pump3) p3
              FROM tele_gfc_r{run_id} GROUP BY 1 ORDER BY 1""", s["t0"])
        arr = np.array([[r["t"], r["v1"], r["v2"], r["c"], r["sv"], r["p1"], r["p2"], r["p3"]]
                        for r in rows], dtype="<f4")
        summary = dict(samples=s["n"], start=s["t0"].isoformat(), end=s["t1"].isoformat(),
                       volt1=dict(min=s["v1min"], max=s["v1max"], avg=s["v1avg"]),
                       volt2=dict(min=s["v2min"], max=s["v2max"], avg=s["v2avg"]),
                       pump2_on_sec=s["p2on"])
        await c.execute("""
            INSERT INTO run_telemetry_summary (run_id, gfc, gfc_10s, gfc_10s_cols)
            VALUES ($1,$2,$3,$4) ON CONFLICT (run_id) DO UPDATE SET
              gfc=EXCLUDED.gfc, gfc_10s=EXCLUDED.gfc_10s, gfc_10s_cols=EXCLUDED.gfc_10s_cols,
              built_at=now()""", run_id, json.dumps(summary), arr.tobytes(), TELE_10S_COLS)


# ── heatmap 적재 (0x86 수신 / API 업로드 공용) ─────────────────────

async def ingest_heatmap(run_id, *, values=None, payload=None, fmt=0, cond_idx=None,
                         sy=None, sx=None, hy=None, hx=None, param_no=None,
                         best_idx_lfv=None, best_idx_cv=None, proc_ms=None, seq_no=None):
    """@return 'ok' | 'dup'.  UDP 중복·순서역전은 (run_id, cond_hash) 로 멱등 처리"""
    pool = _pool()
    async with pool.acquire() as c:
        run = await c.fetchrow("SELECT run_id, status, grid_id, calib_ver FROM run WHERE run_id=$1",
                               run_id)
        if run is None:
            raise RunError("run 없음", 404)
        if run["status"] not in ("running", "paused"):
            raise RunError(f"run 상태 {run['status']} — heatmap 적재 불가", 409)
        g = await get_grid(c, run["grid_id"])
        if g["mode"] == "fast":
            if param_no is None and cond_idx is not None:
                param_no = cond_idx + 1
            if param_no is None or not (1 <= param_no <= g["heatmap_count"]):
                raise RunError("fast 모드는 param_no(1..N) 필요")
            ps = g["param_sets"][param_no - 1]
            cond_idx = param_no - 1
            hv, frq, duty, lff = ps["hv"], ps["frq"], ps["duty"], ps["lff"]
            sy = sx = hy = hx = None
        else:
            if cond_idx is None:
                if None in (sy, sx, hy, hx):
                    raise RunError("cond_idx 또는 sy,sx,hy,hx 필요")
                cond_idx = H.cond_index(sy, sx, hy, hx, g["no_sx"], g["no_hy"], g["no_hx"])
            if not (0 <= cond_idx < g["heatmap_count"]):
                raise RunError("cond_idx 범위 밖")
            sy, sx, hy, hx = H.cond_coords(cond_idx, g["no_sx"], g["no_hy"], g["no_hx"])
            hv, frq = g["hv_list"][sx], g["frq_list"][sy]
            duty, lff = g["duty_list"][hx], g["lff_list"][hy]
        n = g["no_lfv"] * g["no_cv"]
        if payload is None:
            if values is None:
                raise RunError("values 또는 payload 필요")
            a = np.asarray(values, dtype=float).reshape(-1)
            if a.size != n:
                raise RunError(f"값 개수 {a.size} ≠ {n} (CV {g['no_cv']} × LFV {g['no_lfv']})")
            payload = H.encode_u16(a) if fmt == 0 else H.encode_f32(a)
        if len(payload) != n * (2 if fmt == 0 else 4):
            raise RunError("payload 크기 불일치")
        arr = H.decode(payload, fmt, g["no_cv"], g["no_lfv"], run["calib_ver"])
        fin = arr[np.isfinite(arr)]
        ch = H.cond_hash(hv, frq, duty, lff, g["lfv_list"], g["cv_list"])
        res = await c.execute("""
            INSERT INTO heatmap (run_id, cond_idx, seq_no, cond_hash, hv, frq, duty, lff,
                                 sy, sx, hy, hx, param_no, no_lfv, no_cv, payload, payload_fmt,
                                 best_idx_lfv, best_idx_cv, raw_min, raw_max, raw_mean,
                                 measured_at, proc_ms)
            VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19,$20,$21,$22,
                    now(),$23)
            ON CONFLICT DO NOTHING""",
            run_id, cond_idx, seq_no, ch, hv, frq, duty, lff, sy, sx, hy, hx, param_no,
            g["no_lfv"], g["no_cv"], payload, fmt, best_idx_lfv, best_idx_cv,
            float(fin.min()) if fin.size else None, float(fin.max()) if fin.size else None,
            float(fin.mean()) if fin.size else None, proc_ms)
        dup = res.endswith(" 0")
        await c.execute("INSERT INTO run_event (run_id, kind, cond_idx) VALUES ($1,$2,$3)",
                        run_id, "heatmap_dup" if dup else "heatmap_ok", cond_idx)
        return "dup" if dup else "ok", cond_idx


# ── 조회 ───────────────────────────────────────────────────────────

RUN_COLS = """r.run_id, r.run_uid, r.device_id, r.kind, r.mode, r.grid_id, r.air_ref_run_id,
              r.label, r.ambient, r.target_gas, r.concentration, r.comment, r.operator,
              r.source_file, r.calib_ver, r.data_origin, r.control_source, r.requested_by,
              r.status, r.started_at, r.ended_at, r.expected_heatmaps, r.received_heatmaps,
              r.resume_count, r.last_paused_at, r.last_resumed_at,
              r.filter_type, r.metric_type, r.wave_type, r.duty_mode, r.lff_mode,
              r.wait_delay_ms, r.avg_count, r.lfv_settle_ms, r.cv_settle_ms,
              ar.label AS air_ref_label,
              (r.received_heatmaps * (CASE WHEN r.data_origin='legacy_import' THEN 704 ELSE 352 END + 110))
                  AS approx_bytes"""


async def get_run(run_id):
    async with _pool().acquire() as c:
        r = await c.fetchrow(f"""SELECT {RUN_COLS} FROM run r
                                  LEFT JOIN run ar ON ar.run_id = r.air_ref_run_id
                                 WHERE r.run_id=$1""", run_id)
        if r is None:
            raise RunError("run 없음", 404)
        d = _row(r)
        d["grid"] = await get_grid(c, r["grid_id"])
        return d


async def list_runs(kind=None, gas=None, device_id=None, mode=None, status=None, origin=None,
                    date_from=None, date_to=None, q=None, limit=50, offset=0, order="desc"):
    cond, args = [], []

    def add(sql, v):
        args.append(v)
        cond.append(sql.replace("?", f"${len(args)}"))
    if kind:
        add("r.kind = ?::run_kind", kind)
    if gas:
        add("r.target_gas = ?", gas)
    if device_id:
        add("r.device_id = ?", int(device_id))
    if mode:
        add("r.mode = ?", mode)
    if status:
        add("r.status = ?::run_status", status)
    if origin:
        add("r.data_origin = ?::data_origin_t", origin)
    if date_from:
        add("r.started_at >= ?::date", date_from)
    if date_to:
        add("r.started_at < (?::date + 1)", date_to)
    if q:
        add("(r.label ILIKE ? OR r.source_file ILIKE $%d OR r.comment ILIKE $%d OR r.target_gas ILIKE $%d)"
            % ((len(args) + 1,) * 3), f"%{q}%")
    where = ("WHERE " + " AND ".join(cond)) if cond else ""
    od = "ASC" if order == "asc" else "DESC"
    async with _pool().acquire() as c:
        total = await c.fetchval(f"SELECT count(*) FROM run r {where}", *args)
        rows = await c.fetch(f"""SELECT {RUN_COLS} FROM run r
                                  LEFT JOIN run ar ON ar.run_id = r.air_ref_run_id
                                 {where} ORDER BY r.started_at {od}, r.run_id {od}
                                 LIMIT {int(limit)} OFFSET {int(offset)}""", *args)
    return dict(total=total, offset=offset, limit=limit, rows=[_row(r) for r in rows])


async def air_list(device_id=None, mode=None, grid_id=None, include_legacy=False):
    """D13/D13b: Air 후보 목록. 자동 선택·기본값 없음 — 사용자가 고른다.
    기본은 measured 만 (D16).  include_legacy=True 면 임포트 데이터도 (열람용)."""
    cond = ["r.kind='air_ref'", "r.status IN ('done','aborted')"]
    args = []
    if not include_legacy:
        cond.append("r.data_origin='measured'")
    for col, v in (("r.device_id", device_id), ("r.mode", mode), ("r.grid_id", grid_id)):
        if v is not None:
            args.append(int(v) if col != "r.mode" else v)
            cond.append(f"{col} = ${len(args)}")
    async with _pool().acquire() as c:
        rows = await c.fetch(f"""SELECT {RUN_COLS} FROM run r
                                  LEFT JOIN run ar ON ar.run_id = r.air_ref_run_id
                                 WHERE {' AND '.join(cond)}
                                 ORDER BY r.started_at DESC LIMIT 500""", *args)
    return [_row(r) for r in rows]


async def update_run_meta(run_id, fields: dict):
    allowed = {"label", "target_gas", "comment", "ambient", "concentration", "operator"}
    sets, args = [], [run_id]
    for k, v in fields.items():
        if k not in allowed:
            continue
        args.append(json.dumps(v) if k == "ambient" and v is not None else v)
        sets.append(f"{k} = ${len(args)}")
    if not sets:
        raise RunError("바꿀 항목 없음")
    async with _pool().acquire() as c:
        n = await c.execute(f"UPDATE run SET {', '.join(sets)} WHERE run_id=$1", *args)
    if n.endswith(" 0"):
        raise RunError("run 없음", 404)
    return await get_run(run_id)


async def recent(run_id, n=4):
    """가장 최근에 받은 heatmap n 장의 cond_idx (자동측정 화면 '누적 Heatmap')"""
    async with _pool().acquire() as c:
        rows = await c.fetch("""SELECT cond_idx FROM heatmap WHERE run_id=$1
                                 ORDER BY recv_at DESC, cond_idx DESC LIMIT $2""", run_id, n)
    return [r["cond_idx"] for r in rows][::-1]


async def missing(run_id):
    async with _pool().acquire() as c:
        return [r["cond_idx"] for r in await c.fetch("SELECT * FROM fn_missing_cond($1)", run_id)]


async def gases():
    async with _pool().acquire() as c:
        rows = await c.fetch("""SELECT target_gas, count(*) n,
                                       count(*) FILTER (WHERE data_origin='measured') measured
                                  FROM run WHERE kind='gas' AND target_gas IS NOT NULL
                                 GROUP BY 1 ORDER BY 1""")
    return [dict(r) for r in rows]


# ── pair (Idf / 장치 비교) ─────────────────────────────────────────

async def _pos_match(c, rl, rr):
    """임포트(legacy) 끼리 격자가 다르면 위치(cond_idx)로 맞춘다 (2026-09-23 사용자 결정 —
    확인용 샘플이라 HV 범위 차이는 무시).  모드·장수·LFV×CV 가 같아야 한다.  실측은 항상 조건(cond_hash)."""
    if rl["data_origin"] != "legacy_import" or rr["data_origin"] != "legacy_import":
        return False
    if rl["grid_id"] == rr["grid_id"]:
        return False
    g = await c.fetch("SELECT grid_id, mode, heatmap_count, no_lfv, no_cv FROM grid_def WHERE grid_id = ANY($1)",
                      [rl["grid_id"], rr["grid_id"]])
    if len(g) != 2:
        return False
    a, b = g
    return (a["mode"], a["heatmap_count"], a["no_lfv"], a["no_cv"]) == (b["mode"], b["heatmap_count"], b["no_lfv"], b["no_cv"])


_POS_OVERLAP = """
    WITH l AS (SELECT cond_idx FROM heatmap WHERE run_id = $1),
         r AS (SELECT cond_idx FROM heatmap WHERE run_id = $2),
         o AS (SELECT count(*)::int c FROM l JOIN r USING (cond_idx))
    SELECT (SELECT count(*)::int FROM l) AS left_total, (SELECT count(*)::int FROM r) AS right_total,
           (SELECT c FROM o) AS overlap,
           (SELECT count(*)::int FROM l) - (SELECT c FROM o) AS left_missing,
           (SELECT count(*)::int FROM r) - (SELECT c FROM o) AS right_missing,
           false AS same_grid"""
_POS_PAYLOAD = """
    SELECT l.cond_idx, l.hv, l.frq, l.duty, l.lff, l.sy, l.sx, l.hy, l.hx, l.no_lfv, l.no_cv,
           l.payload AS left_payload, l.payload_fmt AS left_fmt,
           r.payload AS right_payload, r.payload_fmt AS right_fmt
      FROM heatmap l JOIN heatmap r ON r.run_id = $2 AND r.cond_idx = l.cond_idx
     WHERE l.run_id = $1 ORDER BY l.cond_idx"""


async def check_pair(c, left, right, kind):
    rl = await c.fetchrow("SELECT run_id, kind, data_origin, grid_id, calib_ver FROM run WHERE run_id=$1", left)
    rr = await c.fetchrow("SELECT run_id, kind, data_origin, grid_id, calib_ver FROM run WHERE run_id=$1", right)
    if rl is None or rr is None:
        raise RunError("run 없음", 404)
    if left == right:
        raise RunError("같은 run 끼리는 비교할 수 없습니다")
    if kind == "idf" and rr["kind"] != "air_ref":
        raise RunError("air_run_id 는 Air(Ref) run 이어야 합니다")
    if rl["data_origin"] != rr["data_origin"]:
        raise RunError("임포트(legacy) 데이터와 실측 데이터는 비교하지 않습니다 (D16)")
    pos = await _pos_match(c, rl, rr)
    ov = await c.fetchrow(_POS_OVERLAP if pos else "SELECT * FROM fn_pair_overlap($1,$2)", left, right)
    if not ov["overlap"]:
        raise RunError("두 run 의 측정 조건이 하나도 겹치지 않습니다 (격자 다름)")
    ov = dict(ov)
    ov["match"] = "position" if pos else "condition"
    return rl, rr, ov


async def ensure_pair(left, right, kind="idf", recompute=False):
    async with _pool().acquire() as c:
        rl, rr, ov = await check_pair(c, left, right, kind)
        calib = rl["calib_ver"]
        if not recompute:
            ps = await c.fetchrow("""SELECT * FROM pair_stat WHERE left_run_id=$1 AND right_run_id=$2
                                     AND pair_kind=$3::pair_kind_t AND calib_ver=$4""",
                                  left, right, kind, calib)
            if ps:
                return dict(ps), ov
        g = await c.fetchrow("SELECT heatmap_count FROM grid_def WHERE grid_id=$1", rl["grid_id"])
        rows = await c.fetch(_POS_PAYLOAD if ov["match"] == "position" else "SELECT * FROM fn_pair_payload($1,$2)",
                             left, right)
        res = pairs.compute([dict(r) for r in rows], g["heatmap_count"], calib, rr["calib_ver"])
        ps = await c.fetchrow("""
            INSERT INTO pair_stat (left_run_id, right_run_id, pair_kind, calib_ver, best_val_arr,
                                   best_lfv_arr, best_cv_arr, val_min, val_max, best_cond_idx,
                                   matched, missing)
            VALUES ($1,$2,$3::pair_kind_t,$4,$5,$6,$7,$8,$9,$10,$11,$12)
            ON CONFLICT (left_run_id, right_run_id, pair_kind, calib_ver) DO UPDATE SET
              best_val_arr=EXCLUDED.best_val_arr, best_lfv_arr=EXCLUDED.best_lfv_arr,
              best_cv_arr=EXCLUDED.best_cv_arr, val_min=EXCLUDED.val_min, val_max=EXCLUDED.val_max,
              best_cond_idx=EXCLUDED.best_cond_idx, matched=EXCLUDED.matched,
              missing=EXCLUDED.missing, computed_at=now()
            RETURNING *""", left, right, kind, calib, res["best_val_arr"], res["best_lfv_arr"],
            res["best_cv_arr"], res["val_min"], res["val_max"], res["best_cond_idx"],
            res["matched"], res["missing"])
        return dict(ps), ov


async def overview(run_id, air_run_id=None, compare_run_id=None):
    """Selection Grid(섹션 개요) 용 — 조건별 best 값 1차원 배열 + 섹션별 최대 |값|"""
    run = await get_run(run_id)
    g = run["grid"]
    if air_run_id or compare_run_id:
        kind = "idf" if air_run_id else "device_diff"
        ps, ov = await ensure_pair(run_id, air_run_id or compare_run_id, kind)
        dec = pairs.decode_summary(ps)
        vals = dec["best_val"]
        meta = dict(kind=kind, right_run_id=air_run_id or compare_run_id, overlap=ov,
                    val_min=ps["val_min"], val_max=ps["val_max"], best_cond_idx=ps["best_cond_idx"],
                    matched=ps["matched"], missing=ps["missing"],
                    computed_at=ps["computed_at"].isoformat())
        best_lfv, best_cv = dec["best_idx_lfv"], dec["best_idx_cv"]
    else:   # raw: 각 heatmap 의 최대값
        async with _pool().acquire() as c:
            rows = await c.fetch("SELECT cond_idx, raw_max FROM heatmap WHERE run_id=$1", run_id)
        vals = [None] * g["heatmap_count"]
        for r in rows:
            vals[r["cond_idx"]] = r["raw_max"]
        fin = [v for v in vals if v is not None]
        meta = dict(kind="raw_max", val_min=min(fin) if fin else None, val_max=max(fin) if fin else None)
        best_lfv = best_cv = None
    sections = None
    if g["mode"] != "fast":
        per = g["no_hx"] * g["no_hy"]
        sections = []
        for s in range(g["no_sx"] * g["no_sy"]):
            chunk = [v for v in vals[s * per:(s + 1) * per] if v is not None]
            sy, sx = divmod(s, g["no_sx"])
            sections.append(dict(sy=sy, sx=sx, hv=g["hv_list"][sx], frq=g["frq_list"][sy],
                                 count=len(chunk),
                                 best=max(chunk, key=abs) if chunk else None))
    return dict(run_id=run_id, mode=g["mode"], grid=g, values=vals, best_idx_lfv=best_lfv,
                best_idx_cv=best_cv, sections=sections, **meta)


VALUE_KINDS = ("raw", "idf", "air", "diff")


async def heatmaps(run_id, *, sy=None, sx=None, cond=None, value="raw", air_run_id=None,
                   compare_run_id=None):
    """heatmap 여러 장.  value:
         raw  = 이 run 의 값 (Target 또는 Air)
         air  = air_run_id 의 같은 조건 값
         idf  = 이 run − air_run_id       (Target − Air)
         diff = 이 run − compare_run_id   (장치 간 편차)
       sy,sx 를 주면 그 섹션의 4×4 = 16장, fast 는 전체 8장, cond 는 쉼표 목록"""
    if value not in VALUE_KINDS:
        raise RunError(f"value 는 {VALUE_KINDS}")
    if value in ("idf", "air") and not air_run_id:
        raise RunError("air_run_id 는 필수입니다 (D13b — Air 자동 선택 없음)")
    if value == "diff" and not compare_run_id:
        raise RunError("compare_run_id 필요")
    run = await get_run(run_id)
    g = run["grid"]
    cond_sql, args = "", [run_id]
    if cond:
        idx = [int(x) for x in str(cond).split(",") if x.strip() != ""]
        args.append(idx)
        cond_sql = "AND h.cond_idx = ANY($2)"
    elif sy is not None and sx is not None and g["mode"] != "fast":
        args += [int(sy), int(sx)]
        cond_sql = "AND h.sy=$2 AND h.sx=$3"
    other = air_run_id if value in ("idf", "air") else (compare_run_id if value == "diff" else None)
    async with _pool().acquire() as c:
        join_col = "cond_hash"
        if other:
            _, _, ov = await check_pair(c, run_id, other, "idf" if value in ("idf", "air") else "device_diff")
            if ov["match"] == "position":
                join_col = "cond_idx"
        rows = await c.fetch(f"""
            SELECT h.cond_idx, h.hv, h.frq, h.duty, h.lff, h.sy, h.sx, h.hy, h.hx, h.param_no,
                   h.no_lfv, h.no_cv, h.payload, h.payload_fmt, h.is_measured,
                   h.best_idx_lfv, h.best_idx_cv, h.measured_at,
                   o.payload AS o_payload, o.payload_fmt AS o_fmt
              FROM heatmap h
              LEFT JOIN heatmap o ON o.run_id = {'$' + str(len(args) + 1) if other else 'NULL'}
                                 AND o.{join_col} = h.{join_col}
             WHERE h.run_id=$1 {cond_sql}
             ORDER BY h.cond_idx""", *(args + ([other] if other else [])))
        o_calib = None
        if other:
            o_calib = await c.fetchval("SELECT calib_ver FROM run WHERE run_id=$1", other)
    out, gmin, gmax = [], None, None
    for r in rows:
        a = H.decode(bytes(r["payload"]), r["payload_fmt"], r["no_cv"], r["no_lfv"], run["calib_ver"])
        if other:
            if r["o_payload"] is None:
                v = None
            else:
                b = H.decode(bytes(r["o_payload"]), r["o_fmt"], r["no_cv"], r["no_lfv"], o_calib)
                v = b if value == "air" else a - b
        else:
            v = a
        item = dict(cond_idx=r["cond_idx"], hv=_rnd(r["hv"]), frq=_rnd(r["frq"]),
                    duty=_rnd(r["duty"]), lff=_rnd(r["lff"]),
                    sy=r["sy"], sx=r["sx"], hy=r["hy"], hx=r["hx"], param_no=r["param_no"],
                    is_measured=r["is_measured"],
                    measured_at=r["measured_at"].isoformat() if r["measured_at"] else None)
        if v is None:
            item.update(values=None, min=None, max=None, best=None)
        else:
            fin = v[np.isfinite(v)]
            mn, mx = (float(fin.min()), float(fin.max())) if fin.size else (None, None)
            iy, ix, bv = H.best_point(v) if fin.size else (None, None, None)
            item.update(values=H.nan_to_none(np.round(v, 6)), min=mn, max=mx,
                        best=dict(idx_cv=iy, idx_lfv=ix, value=bv))
            if mn is not None:
                gmin = mn if gmin is None else min(gmin, mn)
                gmax = mx if gmax is None else max(gmax, mx)
        out.append(item)
    return dict(run_id=run_id, value=value, air_run_id=air_run_id, compare_run_id=compare_run_id,
                mode=g["mode"], lfv_list=g["lfv_list"], cv_list=g["cv_list"],
                layout="rows=CV(no_cv), cols=LFV(no_lfv)", unit="V",
                min=gmin, max=gmax, count=len(out), heatmaps=out,
                note="LFV 마지막 열은 legacy 데이터 보정으로 null 일 수 있음"
                     if run["data_origin"] == "legacy_import" else None)


async def db_stats():
    async with _pool().acquire() as c:
        size = await c.fetchval("SELECT pg_database_size(current_database())")
        rows = await c.fetch("""SELECT kind, data_origin, count(*) n, sum(received_heatmaps) hm
                                  FROM run GROUP BY 1,2""")
        hm = await c.fetchval("SELECT count(*) FROM heatmap")
    return dict(db_bytes=size, heatmaps=hm,
                runs=[dict(kind=r["kind"], origin=r["data_origin"], count=r["n"], heatmaps=r["hm"])
                      for r in rows], writer=store.status())
