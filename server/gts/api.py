"""GTS 웹 API (FastAPI) — REST + WebSocket

화면 ↔ API (웹 디자인 5화면 기준, DOC/GTS_Web_Design.md)
  1. 동작 상황표      GET /api/overview  ·  WS /ws/live
  2-A. 장비 상세 Manual  GET /api/pairs/{id} · /api/aos/{id}/current · /api/gfc/{id}/telemetry
                      POST /api/aos/{id}/params · /api/gfc/{id}/* · /api/aos/{id}/marks
  2-B. 장비 상세 Auto  POST /api/runs · /api/runs/{id}/pause|resume|finish|abort
  3. Heatmap Viewer   GET /api/runs/{id}/overview · /api/runs/{id}/heatmaps · /export.csv
  4. 데이터 목록       GET /api/runs · /api/air-list · /api/gases · /api/db/stats
  5. 캘리브레이션      /api/cal/*  (gts/cal_api.py)

전체 목록과 예시: http://<서버>:8081/docs  (Swagger UI, 토큰은 Authorize 버튼)
"""
from __future__ import annotations

import asyncio
import csv
import io
import struct
import time
from pathlib import Path

from fastapi import Depends, FastAPI, HTTPException, Query, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, PlainTextResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

import udp_server as S

from . import auth, config, control, live, runs
from .auth import Principal, need
from .control import ControlError
from .db import store
from .runs import RunError

app = FastAPI(title="GTS API", version="1.0",
              description="AOS Gas Training System — DB/제어 API.  인증: Bearer 토큰 (read/control/admin)")

if config.CORS_ORIGINS:
    app.add_middleware(CORSMiddleware, allow_origins=config.CORS_ORIGINS, allow_credentials=True,
                       allow_methods=["*"], allow_headers=["*"])


@app.exception_handler(ControlError)
async def _ctl_err(_req, e: ControlError):
    return JSONResponse({"error": e.code, "detail": e.msg}, status_code=e.http)


@app.exception_handler(RunError)
async def _run_err(_req, e: RunError):
    return JSONResponse({"error": "run", "detail": e.msg}, status_code=e.http)


READ, CONTROL, ADMIN = need("read"), need("control"), need("admin")

from . import cal_api                     # noqa: E402  5. 캘리브레이션 (/api/cal/*)
cal_api.install(app)

# ── 캐시 (2초 주기 갱신) ─────────────────────────────────────────────
GAS_NAME: dict[int, str | None] = {}
RUN_PROG: dict[int, dict] = {}         # AOS id -> 진행중 run 요약


async def refresh_cache():
    if not store.ok:
        return
    try:
        async with store.pool.acquire() as c:
            for r in await c.fetch("SELECT pair_id, gas_name FROM pair_config"):
                GAS_NAME[r["pair_id"]] = r["gas_name"]
            rows = await c.fetch("""SELECT run_id, device_id, kind, mode, target_gas, status,
                                           received_heatmaps, expected_heatmaps, started_at
                                      FROM run WHERE status IN ('running','paused')""")
        RUN_PROG.clear()
        for r in rows:
            if r["status"] == "running" or r["device_id"] not in RUN_PROG:
                RUN_PROG[r["device_id"]] = dict(run_id=r["run_id"], kind=r["kind"], mode=r["mode"],
                                                gas=r["target_gas"], status=r["status"],
                                                received=r["received_heatmaps"],
                                                expected=r["expected_heatmaps"],
                                                started_at=r["started_at"].isoformat())
    except Exception as e:                       # noqa: BLE001
        store.last_error = repr(e)


# ── 스냅샷 빌더 ─────────────────────────────────────────────────────

STATE_NAME = {0: "idle", 1: "run", 2: "error", 3: "offline"}


def _age(dev):
    return None if not dev or not dev.last_seen else round(time.time() - dev.last_seen, 1)


def aos_params(dev):
    if dev is None or dev.params is None:
        return None
    hv, frq, duty, cv, lff, lfv, lf_on, shape = struct.unpack(S.AOS_PARAMS_FMT, dev.params)
    return dict(hv=round(hv, 3), frq=round(frq, 2), duty=round(duty, 3), cv=round(cv, 4),
                lf_frq=round(lff, 2), lf_volt=round(lfv, 3), lf_on=bool(lf_on), lf_shape=shape)


def aos_view(did):
    dev = S.AOSES.get(did)
    run = RUN_PROG.get(did)
    d = dict(id=did, known=dev is not None, online=bool(dev and dev.online),
             state=STATE_NAME[dev.dev_state] if dev else "offline", last_rx_s=_age(dev),
             mode="AUTO" if run else "MANUAL", run=run, params=aos_params(dev))
    if dev and dev.status:
        st = dev.status
        d["status"] = dict(flags=st["flags"], err=st["err"], rssi=st["rssi"], uptime=st["uptime"],
                           adc=dict(air_p=st["air_p"], air_n=st["air_n"],
                                    gas_p=st["gas_p"], gas_n=st["gas_n"]))
    d["cur"] = live.aos_cur_view(did) if dev else None
    stats = live.aos_stats(did, 10)
    d["current_avg_v"] = round(stats["avg"], 4) if stats else None
    d["current_now_v"] = round(stats["now"], 4) if stats else None
    return d


def gfc_view(did):
    dev = S.GFCS.get(did)
    d = dict(id=did, known=dev is not None, online=bool(dev and dev.online),
             state=STATE_NAME[dev.dev_state] if dev else "offline", last_rx_s=_age(dev))
    if dev:
        s = dev.sensor or {}
        d.update(mode="AUTO" if dev.mode else "MANUAL", src_enable=dev.src_enable,
                 src_on=dev.src_on, src_init=dev.src_init, pump=[dev.pump1, dev.pump2, dev.pump3],
                 remain_s=dev.remain_ds / 10.0, cycle_count=dev.cycle_count,
                 start_s=dev.start_ds / 10.0, cycle_s=dev.cycle_ds / 10.0,
                 conc_v=s.get("volt1"), volt2=s.get("volt2"), sv=s.get("sv"), ctrl=s.get("ctrl"),
                 rssi=s.get("rssi"), err=s.get("err"), flags=s.get("flags"))
    return d


def pair_view(i, owner_id=None):
    return dict(id=i, gas_name=GAS_NAME.get(i), aos=aos_view(i), gfc=gfc_view(i),
                lock_aos=control.lock_state(S.DEV_AOS, i, owner_id),
                lock_gfc=control.lock_state(S.DEV_GFC, i, owner_id))


def consoles_view():
    out = []
    now = time.time()
    for addr, sess in list(S.CONSOLES.items()):
        info = live.console_info.get(addr, {})
        rx = info.get("rx", 0)
        hist = info.setdefault("rate", [])
        hist.append((now, rx))
        while hist and now - hist[0][0] > 5.5:
            hist.pop(0)
        rate = (hist[-1][1] - hist[0][1]) / max(hist[-1][0] - hist[0][0], 1e-3) if len(hist) > 1 else 0
        out.append(dict(name=f"CONSOLE-{sess.my_id}", ip=addr[0], port=addr[1], alive=sess.alive,
                        connected=sess.connected, target_type="AOS" if sess.dev_type == S.DEV_AOS else "GFC",
                        target_id=sess.dev_id, pkt_s=round(rate, 1), last_rx_s=round(now - sess.last_seen, 1)))
    return out


def overview_snapshot(owner_id=None):
    ps = [pair_view(i, owner_id) for i in range(1, 21)]
    counts = dict(measuring=0, idle=0, error=0, offline=0, auto_running=0)
    for p in ps:
        a = p["aos"]
        if not a["known"]:
            continue
        if not a["online"]:
            counts["offline"] += 1
        elif a["state"] == "error":
            counts["error"] += 1
        elif a["run"] and a["run"]["status"] == "running":
            counts["measuring"] += 1
            counts["auto_running"] += 1
        elif a["state"] == "run":
            counts["measuring"] += 1
        else:
            counts["idle"] += 1
    return dict(t=time.time(), pairs=ps, counts=counts, consoles=consoles_view(),
                server=dict(uptime_s=round(time.time() - live.T0), ports=dict(aos=S.PORT_AOS, gfc=S.PORT_GFC,
                            console=S.PORT_CONSOLE, http=config.HTTP_PORT),
                            pkt_s=live.rate["pkt_s"], loss_pct=live.rate["loss_pct"],
                            rx=dict(live.rx_count), bad=dict(live.bad_count), db=store.status()),
                events=list(live.events)[-20:][::-1])


# ── 기본 ────────────────────────────────────────────────────────────

@app.get("/api/health", tags=["system"])
async def health():
    return dict(ok=True, uptime_s=round(time.time() - live.T0), db=store.ok)


@app.get("/api/me", tags=["system"])
async def me(p: Principal = Depends(READ)):
    return dict(login=p.actor, scope=p.scope, owner_id=p.owner_id)


@app.get("/api/overview", tags=["1. 동작 상황표"])
async def overview(p: Principal = Depends(READ)):
    return overview_snapshot(p.owner_id)


@app.get("/api/events", tags=["1. 동작 상황표"])
async def events(limit: int = 100, since_id: int = 0, dev_id: int | None = None,
                 _p: Principal = Depends(READ)):
    if since_id or not store.ok:
        ev = [e for e in live.events if e["id"] > since_id and (dev_id is None or e["dev_id"] == dev_id)]
        return ev[-limit:][::-1]
    async with store.pool.acquire() as c:
        rows = await c.fetch("""SELECT event_id AS id, extract(epoch FROM at) AS at, level, dev_type,
                                       dev_id, kind, text FROM sys_event
                                 WHERE ($1::int IS NULL OR dev_id=$1)
                                 ORDER BY at DESC LIMIT $2""", dev_id, limit)
    return [dict(r) for r in rows]


@app.get("/api/control-actions", tags=["1. 동작 상황표"])
async def control_actions(limit: int = 100, dev_id: int | None = None, _p: Principal = Depends(READ)):
    if not store.ok:
        raise HTTPException(503, "DB 연결 없음")
    async with store.pool.acquire() as c:
        rows = await c.fetch("""SELECT action_id, at, source, actor, dev_type, dev_id, cmd, detail, result
                                  FROM control_action WHERE ($1::int IS NULL OR dev_id=$1)
                                 ORDER BY at DESC LIMIT $2""", dev_id, limit)
    return [runs._row(r) for r in rows]


# ── 2. 장비 상세 ────────────────────────────────────────────────────

def _chk_id(i):
    if not 1 <= i <= 20:
        raise HTTPException(404, "ID 는 1~20")


@app.get("/api/pairs/{pid}", tags=["2. 장비 상세"])
async def pair_detail(pid: int, p: Principal = Depends(READ)):
    _chk_id(pid)
    d = pair_view(pid, p.owner_id)
    d["current"] = live.aos_stats(pid, 120)
    d["gfc_recent"] = live.gfc_series(pid, 600)[-60:]
    return d


class GasName(BaseModel):
    gas_name: str = Field(..., max_length=64)
    note: str | None = None


@app.put("/api/pairs/{pid}/gas-name", tags=["2. 장비 상세"])
async def set_gas_name(pid: int, body: GasName, p: Principal = Depends(CONTROL)):
    """가스명 변경 (웹 디자인: 사용자가 변경 가능).  진행 중 run 의 target_gas 도 같이 바꾼다"""
    _chk_id(pid)
    if not store.ok:
        raise HTTPException(503, "DB 연결 없음")
    async with store.pool.acquire() as c:
        await c.execute("""UPDATE pair_config SET gas_name=$2, note=COALESCE($3, note),
                           updated_at=now(), updated_by=$4 WHERE pair_id=$1""",
                        pid, body.gas_name.strip(), body.note, p.actor)
        await c.execute("""UPDATE run SET target_gas=$2 WHERE device_id=$1 AND kind='gas'
                           AND status IN ('running','paused')""", pid, body.gas_name.strip())
    GAS_NAME[pid] = body.gas_name.strip()
    live.add_event("info", "gas_name", f"AOS {pid:02d} · 가스명 → {body.gas_name}", 1, pid)
    return dict(pair_id=pid, gas_name=GAS_NAME[pid])


@app.get("/api/aos/{did}/current", tags=["2. 장비 상세"])
async def aos_current(did: int, sec: int = Query(120, ge=5, le=600),
                      ch: str | None = Query(None, pattern="^(air_p|air_n|gas_p|gas_n)$"),
                      _p: Principal = Depends(READ)):
    """AOS Current 1초 (메모리 링, DB 미사용 — D11).  단위 V"""
    return dict(id=did, unit="V", channel=ch or config.AOS_CURRENT_CH, sec=sec,
                points=live.aos_series(did, sec, ch), stats=live.aos_stats(did, sec, ch),
                state=live.aos_cur_view(did))


@app.get("/api/gfc/{did}/telemetry", tags=["2. 장비 상세"])
async def gfc_telemetry(did: int, sec: int = Query(600, ge=10, le=86400), _p: Principal = Depends(READ)):
    """GFC 농도(volt1, V) 추이.  10분 이내는 메모리, 그 이상은 DB(tele_gfc)"""
    if sec <= config.GFC_RING or not store.ok:
        return dict(id=did, unit="V", source="memory", points=live.gfc_series(did, sec))
    async with store.pool.acquire() as c:
        rows = await c.fetch("""SELECT extract(epoch FROM ts) t, volt1, volt2, pump1, pump2, pump3,
                                       flags, src_remain FROM tele_gfc
                                 WHERE device_id=$1 AND ts > now() - make_interval(secs => $2)
                                 ORDER BY ts""", did, float(sec))
    return dict(id=did, unit="V", source="db", points=[dict(r) for r in rows])


# 제어 ---------------------------------------------------------------

class AosParams(BaseModel):
    hv: float | None = Field(None, ge=0, le=200)
    frq: float | None = Field(None, ge=200, le=800)
    duty: float | None = Field(None, ge=20, le=80)
    cv: float | None = Field(None, ge=-5, le=5)
    lf_frq: float | None = Field(None, ge=50, le=200)
    lf_volt: float | None = Field(None, ge=0, le=5)
    lf_on: bool | None = None


@app.post("/api/aos/{did}/params", tags=["2. 장비 상세 · 제어"])
async def set_aos_params(did: int, body: AosParams, p: Principal = Depends(CONTROL)):
    """HV·Frq·Duty·CV·LF On/Off·LF_Frq·LF_Volt — 지정한 항목만 전송 (일괄 적용 = 전부 지정)"""
    _chk_id(did)
    return control.aos_set_params(did, body.model_dump(exclude_none=True), p.actor, p.owner_id)


@app.post("/api/aos/{did}/params/query", tags=["2. 장비 상세 · 제어"])
async def query_aos_params(did: int, p: Principal = Depends(READ)):
    """장비에 현재 설정값을 다시 묻는다 (초기값 표시용)"""
    return control.aos_query(did, p.actor, p.owner_id)


class GfcMode(BaseModel):
    mode: str = Field(..., pattern="^(auto|manual)$")


class OnOff(BaseModel):
    on: bool


class GfcTimes(BaseModel):
    start_s: float = Field(..., ge=0.1, le=60)
    cycle_s: float = Field(..., ge=0.1, le=60)


class Run(BaseModel):
    run: bool


@app.post("/api/gfc/{did}/mode", tags=["2. 장비 상세 · 제어"])
async def gfc_mode(did: int, body: GfcMode, p: Principal = Depends(CONTROL)):
    return control.execute(S.DEV_GFC, did, S.C_GFC_SET_MODE, bytes([1 if body.mode == "auto" else 0]),
                           p.actor, p.owner_id, detail={"mode": body.mode})


@app.post("/api/gfc/{did}/pump", tags=["2. 장비 상세 · 제어"])
async def gfc_pump(did: int, body: OnOff, p: Principal = Depends(CONTROL)):
    return control.execute(S.DEV_GFC, did, S.C_GFC_SET_PUMP, bytes([1 if body.on else 0]),
                           p.actor, p.owner_id, detail={"pump": body.on})


@app.post("/api/gfc/{did}/times", tags=["2. 장비 상세 · 제어"])
async def gfc_times(did: int, body: GfcTimes, p: Principal = Depends(CONTROL)):
    pl = struct.pack("<HH", int(round(body.start_s * 10)), int(round(body.cycle_s * 10)))
    return control.execute(S.DEV_GFC, did, S.C_GFC_SET_TIMES, pl, p.actor, p.owner_id,
                           detail=body.model_dump())


@app.post("/api/gfc/{did}/auto-run", tags=["2. 장비 상세 · 제어"])
async def gfc_auto_run(did: int, body: Run, p: Principal = Depends(CONTROL)):
    return control.execute(S.DEV_GFC, did, S.C_GFC_AUTO_RUN, bytes([1 if body.run else 0]),
                           p.actor, p.owner_id, detail={"run": body.run})


@app.post("/api/devices/{kind}/{did}/lock", tags=["2. 장비 상세 · 제어"])
async def lock_acquire(kind: str, did: int, p: Principal = Depends(CONTROL)):
    """제어권 획득/하트비트 (30초 임대).  상세 화면을 열어 둔 동안 ~10초마다 호출"""
    dt = S.DEV_AOS if kind == "aos" else S.DEV_GFC
    control.acquire_web(dt, did, p.owner_id, p.actor)
    return control.lock_state(dt, did, p.owner_id)


@app.delete("/api/devices/{kind}/{did}/lock", tags=["2. 장비 상세 · 제어"])
async def lock_release(kind: str, did: int, p: Principal = Depends(CONTROL)):
    dt = S.DEV_AOS if kind == "aos" else S.DEV_GFC
    return dict(released=control.release_web(dt, did, p.owner_id))


@app.get("/api/devices/{kind}/{did}/lock", tags=["2. 장비 상세 · 제어"])
async def lock_get(kind: str, did: int, p: Principal = Depends(READ)):
    dt = S.DEV_AOS if kind == "aos" else S.DEV_GFC
    return control.lock_state(dt, did, p.owner_id)


class Mark(BaseModel):
    note: str | None = None
    avg_sec: int = Field(10, ge=1, le=600)


@app.post("/api/aos/{did}/marks", tags=["2. 장비 상세"])
async def add_mark(did: int, body: Mark, p: Principal = Depends(CONTROL)):
    """메뉴얼 모드 '현재 그래프 저장' — 그 순간의 파라미터 + 전류를 영구 기록 (D11b)"""
    if not store.ok:
        raise HTTPException(503, "DB 연결 없음")
    dev = S.AOSES.get(did)
    prm = aos_params(dev) or {}
    st = (dev.status if dev else None) or {}
    stats = live.aos_stats(did, body.avg_sec)
    async with store.pool.acquire() as c:
        await c.execute("""INSERT INTO device (dev_type, device_id, name) VALUES (1,$1,$2)
                           ON CONFLICT DO NOTHING""", did, f"AOS-{did:02d}")
        mid = await c.fetchval("""
            INSERT INTO manual_mark (device_id, actor, hv, frq, duty, cv, lf_on, lf_frq, lf_volt,
                                     adc0, adc1, adc2, adc3, avg_v, note)
            VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15) RETURNING mark_id""",
            did, p.actor, prm.get("hv"), prm.get("frq"), prm.get("duty"), prm.get("cv"),
            prm.get("lf_on"), prm.get("lf_frq"), prm.get("lf_volt"), st.get("air_p"), st.get("air_n"),
            st.get("gas_p"), st.get("gas_n"), stats["avg"] if stats else None, body.note)
    return dict(mark_id=mid, params=prm, current=stats)


@app.get("/api/aos/{did}/marks", tags=["2. 장비 상세"])
async def list_marks(did: int, limit: int = 50, _p: Principal = Depends(READ)):
    if not store.ok:
        raise HTTPException(503, "DB 연결 없음")
    async with store.pool.acquire() as c:
        rows = await c.fetch("SELECT * FROM manual_mark WHERE device_id=$1 ORDER BY at DESC LIMIT $2",
                             did, limit)
    return [runs._row(r) for r in rows]


# ── 측정 run (2-B 자동 측정) ─────────────────────────────────────────

class ParamSet(BaseModel):
    hv: float
    frq: float
    duty: float
    lff: float


class RunStart(BaseModel):
    device_id: int = Field(..., ge=1, le=20)
    kind: str = Field(..., pattern="^(air_ref|gas)$")
    mode: str = Field(..., pattern="^(fast|hour1|full8)$")
    target_gas: str | None = None
    label: str | None = None
    comment: str | None = None
    concentration: float | None = None
    ambient: dict | None = None
    air_ref_run_id: int | None = None
    param_sets: list[ParamSet] | None = Field(None, max_length=8)


@app.post("/api/runs", tags=["2-B. 측정 run"])
async def run_start(body: RunStart, p: Principal = Depends(CONTROL)):
    """측정 run 시작 (DB 기록).  ⚠ 장치로 0x80 을 보내는 경로는 아직 없음 — 응답의 device_command 참고"""
    r = await runs.start_run(body.device_id, body.kind, body.mode, target_gas=body.target_gas,
                             label=body.label, comment=body.comment, ambient=body.ambient,
                             air_ref_run_id=body.air_ref_run_id, concentration=body.concentration,
                             param_sets=[x.model_dump() for x in body.param_sets] if body.param_sets else None,
                             source="web", actor=p.actor)
    await refresh_cache()
    r["device_command"] = "not_implemented"
    return r


class Reason(BaseModel):
    reason: str | None = None


@app.post("/api/runs/{run_id}/pause", tags=["2-B. 측정 run"])
async def run_pause(run_id: int, body: Reason | None = None, p: Principal = Depends(CONTROL)):
    r = await runs.pause_run(run_id, p.actor, body.reason if body else None)
    await refresh_cache()
    return r


@app.post("/api/runs/{run_id}/resume", tags=["2-B. 측정 run"])
async def run_resume(run_id: int, p: Principal = Depends(CONTROL)):
    r = await runs.resume_run(run_id, p.actor)
    await refresh_cache()
    return r


@app.post("/api/runs/{run_id}/finish", tags=["2-B. 측정 run"])
async def run_finish(run_id: int, p: Principal = Depends(CONTROL)):
    r = await runs.finish_run(run_id, p.actor)
    await refresh_cache()
    return r


@app.post("/api/runs/{run_id}/abort", tags=["2-B. 측정 run"])
async def run_abort(run_id: int, body: Reason | None = None, p: Principal = Depends(CONTROL)):
    r = await runs.abort_run(run_id, p.actor, body.reason if body else None)
    await refresh_cache()
    return r


class HeatmapIn(BaseModel):
    cond_idx: int | None = None
    sy: int | None = None
    sx: int | None = None
    hy: int | None = None
    hx: int | None = None
    param_no: int | None = None
    fmt: int = Field(0, ge=0, le=1, description="0=u16 ADC raw, 1=f32")
    values: list[float] = Field(..., description="CV(외) × LFV(내) row-major, 176개")
    best_idx_lfv: int | None = None
    best_idx_cv: int | None = None
    proc_ms: int | None = None


@app.post("/api/runs/{run_id}/heatmaps", tags=["2-B. 측정 run"])
async def run_heatmap_upload(run_id: int, body: HeatmapIn, _p: Principal = Depends(CONTROL)):
    """heatmap 1장 적재 (0x86 UDP 수신과 같은 경로).  중복은 'dup' 으로 무해"""
    res, ci = await runs.ingest_heatmap(run_id, values=body.values, fmt=body.fmt,
                                        cond_idx=body.cond_idx, sy=body.sy, sx=body.sx, hy=body.hy,
                                        hx=body.hx, param_no=body.param_no,
                                        best_idx_lfv=body.best_idx_lfv, best_idx_cv=body.best_idx_cv,
                                        proc_ms=body.proc_ms)
    await refresh_cache()
    return dict(result=res, cond_idx=ci)


# ── 3·4. 조회 ───────────────────────────────────────────────────────

@app.get("/api/runs", tags=["4. 데이터 목록"])
async def run_list(kind: str | None = Query(None, pattern="^(air_ref|gas)$"), gas: str | None = None,
                   device_id: int | None = None, mode: str | None = None, status: str | None = None,
                   origin: str | None = Query(None, pattern="^(measured|legacy_import)$"),
                   date_from: str | None = None, date_to: str | None = None, q: str | None = None,
                   limit: int = Query(50, ge=1, le=500), offset: int = Query(0, ge=0),
                   _p: Principal = Depends(READ)):
    return await runs.list_runs(kind, gas, device_id, mode, status, origin, date_from, date_to, q,
                                limit, offset)


@app.get("/api/air-list", tags=["4. 데이터 목록"])
async def air_list(device_id: int | None = None, mode: str | None = None, grid_id: int | None = None,
                   include_legacy: bool = False, _p: Principal = Depends(READ)):
    """Air(Ref) 후보.  기본은 실측(measured)만 — 임포트 데이터는 include_legacy=true"""
    return await runs.air_list(device_id, mode, grid_id, include_legacy)


@app.get("/api/gases", tags=["4. 데이터 목록"])
async def gas_list(_p: Principal = Depends(READ)):
    return await runs.gases()


@app.get("/api/db/stats", tags=["4. 데이터 목록"])
async def db_stats(_p: Principal = Depends(READ)):
    return await runs.db_stats()


@app.get("/api/runs/{run_id}", tags=["3. Heatmap Viewer"])
async def run_get(run_id: int, _p: Principal = Depends(READ)):
    return await runs.get_run(run_id)


class RunMeta(BaseModel):
    label: str | None = None
    target_gas: str | None = None
    comment: str | None = None
    concentration: float | None = None
    ambient: dict | None = None


@app.patch("/api/runs/{run_id}", tags=["4. 데이터 목록"])
async def run_patch(run_id: int, body: RunMeta, _p: Principal = Depends(CONTROL)):
    """라벨·가스명·메모 수정"""
    return await runs.update_run_meta(run_id, body.model_dump(exclude_none=True))


@app.get("/api/runs/{run_id}/recent", tags=["2-B. 측정 run"])
async def run_recent(run_id: int, n: int = Query(4, ge=1, le=16), _p: Principal = Depends(READ)):
    """최근 수신 heatmap n 장의 cond_idx — 이어서 /heatmaps?cond= 로 값을 받는다"""
    return dict(run_id=run_id, cond_idx=await runs.recent(run_id, n))


@app.get("/api/runs/{run_id}/missing", tags=["3. Heatmap Viewer"])
async def run_missing(run_id: int, _p: Principal = Depends(READ)):
    m = await runs.missing(run_id)
    return dict(run_id=run_id, missing=len(m), cond_idx=m)


@app.get("/api/runs/{run_id}/compare-candidates", tags=["3. Heatmap Viewer"])
async def compare_candidates(run_id: int, _p: Principal = Depends(READ)):
    async with store.pool.acquire() as c:
        rows = await c.fetch("SELECT * FROM fn_compare_candidates($1)", run_id)
    return [runs._row(r) for r in rows]


@app.get("/api/runs/{run_id}/overview", tags=["3. Heatmap Viewer"])
async def run_overview(run_id: int, air_run_id: int | None = None, compare_run_id: int | None = None,
                       _p: Principal = Depends(READ)):
    """Selection Grid 용 — 조건(cond_idx)별 best Idf(또는 raw max) + 섹션별 대표값"""
    return await runs.overview(run_id, air_run_id, compare_run_id)


@app.get("/api/runs/{run_id}/heatmaps", tags=["3. Heatmap Viewer"])
async def run_heatmaps(run_id: int, sy: int | None = None, sx: int | None = None,
                       cond: str | None = Query(None, description="쉼표 구분 cond_idx"),
                       value: str = Query("raw", pattern="^(raw|idf|air|diff)$"),
                       air_run_id: int | None = None, compare_run_id: int | None = None,
                       _p: Principal = Depends(READ)):
    """Multi View — 섹션(sy,sx) 의 16장 / fast 8장.  value=idf 는 air_run_id 필수 (D13b)"""
    if sy is None and sx is None and not cond:
        r = await runs.get_run(run_id)
        if r["mode"] != "fast" and r["received_heatmaps"] > 64:
            raise HTTPException(400, "full/hour 모드는 sy,sx 또는 cond 를 지정하세요")
    return await runs.heatmaps(run_id, sy=sy, sx=sx, cond=cond, value=value,
                               air_run_id=air_run_id, compare_run_id=compare_run_id)


@app.get("/api/runs/{run_id}/export.csv", tags=["3. Heatmap Viewer"])
async def run_export(run_id: int, sy: int | None = None, sx: int | None = None, cond: str | None = None,
                     value: str = Query("raw", pattern="^(raw|idf|air|diff)$"),
                     air_run_id: int | None = None, compare_run_id: int | None = None,
                     _p: Principal = Depends(READ)):
    """CSV 내보내기 — 행: heatmap × CV, 열: LFV"""
    d = await runs.heatmaps(run_id, sy=sy, sx=sx, cond=cond, value=value, air_run_id=air_run_id,
                            compare_run_id=compare_run_id)
    buf = io.StringIO()
    w = csv.writer(buf)
    w.writerow(["run_id", "value", "cond_idx", "hv", "frq", "duty", "lff", "cv"] +
               [f"lfv_{v}" for v in d["lfv_list"]])
    for h in d["heatmaps"]:
        if h["values"] is None:
            continue
        for iy, row in enumerate(h["values"]):
            w.writerow([run_id, value, h["cond_idx"], h["hv"], h["frq"], h["duty"], h["lff"],
                        d["cv_list"][iy]] + ["" if x is None else x for x in row])
    fn = f"run{run_id}_{value}" + (f"_s{sy}-{sx}" if sy is not None else "") + ".csv"
    return StreamingResponse(iter([buf.getvalue()]), media_type="text/csv",
                             headers={"Content-Disposition": f'attachment; filename="{fn}"'})


# ── 관리 (admin) ────────────────────────────────────────────────────

class UserIn(BaseModel):
    login: str = Field(..., pattern=r"^[A-Za-z0-9_.\-]{2,32}$")
    display: str | None = None


class TokenIn(BaseModel):
    login: str
    scope: str = Field(..., pattern="^(read|control|admin)$")
    label: str | None = None
    expires_days: int | None = Field(None, ge=1, le=3650)


@app.get("/api/admin/users", tags=["admin"])
async def users(_p: Principal = Depends(ADMIN)):
    async with store.pool.acquire() as c:
        u = await c.fetch("SELECT * FROM app_user ORDER BY user_id")
        t = await c.fetch("""SELECT token_id, user_id, token_prefix, scope, label, created_at,
                                    expires_at, revoked_at, last_used FROM api_token ORDER BY token_id""")
    return dict(users=[runs._row(x) for x in u], tokens=[runs._row(x) for x in t])


@app.post("/api/admin/users", tags=["admin"])
async def user_add(body: UserIn, _p: Principal = Depends(ADMIN)):
    async with store.pool.acquire() as c:
        uid = await c.fetchval("""INSERT INTO app_user (login, display) VALUES ($1,$2)
                                  ON CONFLICT (login) DO UPDATE SET display=EXCLUDED.display
                                  RETURNING user_id""", body.login, body.display)
    return dict(user_id=uid, login=body.login)


@app.post("/api/admin/tokens", tags=["admin"])
async def token_add(body: TokenIn, _p: Principal = Depends(ADMIN)):
    """토큰 발급.  원문은 이 응답에서 한 번만 보인다"""
    raw = auth.new_token()
    async with store.pool.acquire() as c:
        uid = await c.fetchval("SELECT user_id FROM app_user WHERE login=$1", body.login)
        if uid is None:
            raise HTTPException(404, "사용자 없음 — 먼저 /api/admin/users")
        tid = await c.fetchval("""
            INSERT INTO api_token (user_id, token_hash, token_prefix, scope, label, expires_at)
            VALUES ($1,$2,$3,$4,$5, CASE WHEN $6::int IS NULL THEN NULL
                                         ELSE now() + make_interval(days => $6::int) END)
            RETURNING token_id""", uid, auth.hash_token(raw), raw[:8], body.scope, body.label,
            body.expires_days)
    return dict(token_id=tid, token=raw, scope=body.scope)


@app.delete("/api/admin/tokens/{token_id}", tags=["admin"])
async def token_revoke(token_id: int, _p: Principal = Depends(ADMIN)):
    async with store.pool.acquire() as c:
        await c.execute("UPDATE api_token SET revoked_at=now() WHERE token_id=$1", token_id)
    auth.invalidate()
    return dict(revoked=token_id)


# ── WebSocket 실시간 ────────────────────────────────────────────────

@app.websocket("/ws/live")
async def ws_live(ws: WebSocket):
    """1초마다 {type:'tick', overview, aos:{id:[t,v]}, gfc:{id:{...}}} 푸시.
    클라이언트가 {"detail": 3} 을 보내면 그 쌍의 lock/params 상세를 같이 보낸다."""
    p = await auth.ws_principal(ws)
    if p is None:
        await ws.close(code=4401)
        return
    await ws.accept()
    client = dict(ws=ws, principal=p, detail=None)
    CLIENTS[id(client)] = client
    try:
        while True:
            msg = await ws.receive_json()
            if isinstance(msg, dict) and "detail" in msg:
                client["detail"] = msg["detail"]
    except WebSocketDisconnect:
        pass
    except Exception:                          # noqa: BLE001
        pass
    finally:
        CLIENTS.pop(id(client), None)


CLIENTS: dict[int, dict] = {}


async def ws_broadcast():
    if not CLIENTS:
        return
    latest_aos = {}
    for did, r in live.aos_ring.items():
        if r:
            x = r[-1]
            ci = live.CH_INDEX.get(config.AOS_CURRENT_CH, 3)
            latest_aos[did] = [round(x[0], 3), round(live.adc_to_v(x[ci]), 5)]
    latest_gfc = {did: live.gfc_series(did, 2)[-1] for did, r in live.gfc_ring.items() if r}
    for cid, cl in list(CLIENTS.items()):
        p = cl["principal"]
        msg = dict(type="tick", overview=overview_snapshot(p.owner_id), aos=latest_aos, gfc=latest_gfc)
        if cl["detail"]:
            try:
                msg["detail"] = pair_view(int(cl["detail"]), p.owner_id)
            except (ValueError, KeyError):
                pass
        try:
            await asyncio.wait_for(cl["ws"].send_json(msg), timeout=2)
        except Exception:                      # noqa: BLE001
            CLIENTS.pop(cid, None)


async def background_loop():
    n = 0
    while True:
        await asyncio.sleep(1)
        n += 1
        try:
            live.tick_rate()
            live.check_online("AOS", S.AOSES, S.DEV_AOS)
            live.check_online("GFC", S.GFCS, S.DEV_GFC)
            if n % 2 == 0:
                await refresh_cache()
            await ws_broadcast()
        except Exception as e:                 # noqa: BLE001
            print(f"[API] background error {e!r}", flush=True)


# ── 정적 웹 (다음 단계 프런트) ──────────────────────────────────────

def mount_web():
    d = Path(config.WEB_DIR)
    if d.is_dir() and (d / "index.html").exists():
        class _NoCacheStatic(StaticFiles):
            """배포 직후 옛 JS 가 남지 않도록 — 매번 재검증 (ETag 로 304 가 오므로 부담 없음)"""
            async def get_response(self, path, scope):
                r = await super().get_response(path, scope)
                r.headers["Cache-Control"] = "no-cache"
                return r
        app.mount("/", _NoCacheStatic(directory=str(d), html=True), name="web")
    else:
        @app.get("/", include_in_schema=False)
        async def root():
            return PlainTextResponse("GTS API 서버 동작 중.  API 문서: /docs   상태: /api/health\n")
