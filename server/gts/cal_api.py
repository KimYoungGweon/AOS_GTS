"""캘리브레이션 웹 API  —  /api/cal/*   (웹 5 번째 페이지 "캘리브레이션", 목업 5-1 ~ 5-6)

  공통     GET  /api/cal                 세션 스냅샷 + DMM 상태 + 한계값 (웹이 1 초마다 조회)
           POST /api/cal/estop           비상 정지 — 작업 취소 + HV/CV 0 V  (세션 없어도 안전)
  ① 초기화 POST /api/cal/session         시작 {device_id, sn, adc, mode}
           DELETE /api/cal/session       종료 (0 V, 콘솔 제어 복귀)
           POST /api/cal/dmm/connect     {host?, port?} → *IDN?
           POST /api/cal/precheck        자동 점검 + {user_checks}
           POST /api/cal/backup          CAL_QUERY 0·1·4 → 서버 보관
           POST /api/cal/init            선형 2 점 초기화 (전체 보정만)
           POST /api/cal/restore         백업으로 되돌리기
  ② 수동   POST /api/cal/jog             {target, volt | delta | dac} → 출력 후 DMM·HV_Vs
           POST /api/cal/read            {target} 읽기만
           POST /api/cal/range-mark      {target, which: min|max}
           PUT  /api/cal/plan            범위·간격·평균·안정화 → 포인트 미리보기
  ③ 측정   POST /api/cal/sweep           시작
           POST /api/cal/job/pause|resume|continue|abort
           GET  /api/cal/points?kind=sweep|verify|jog&since=
  ④ 저장   POST /api/cal/build           새 테이블 + 검사 (일부 구간이면 병합)
           POST /api/cal/save            {types?, memo?} CAL_WRITE → 되읽기 비교
  ⑤ 검증   POST /api/cal/verify          {hv, cv, avg_n, settle_ms}
           POST /api/cal/judge           {verdict: pass|fail, memo}
  ⑥ 이력   GET  /api/cal/history · /api/cal/history/{id}
"""
from __future__ import annotations

from fastapi import APIRouter, Depends, Query
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

from . import cal as C
from .auth import Principal, need
from .dmm import DmmError, dmm

router = APIRouter(prefix="/api/cal", tags=["5. 캘리브레이션"])
READ, CONTROL = need("read"), need("control")


def install(app):
    @app.exception_handler(C.CalError)
    async def _cal_err(_req, e: C.CalError):
        return JSONResponse({"error": e.code, "detail": e.msg}, status_code=e.http)

    @app.exception_handler(DmmError)
    async def _dmm_err(_req, e: DmmError):
        return JSONResponse({"error": "dmm", "detail": str(e)}, status_code=502)

    app.include_router(router)


def _owner(p: Principal):
    s = C.CAL.s
    if s and not s.ended and s.owner_id != p.owner_id and p.scope != "admin":
        raise C.CalError(f"다른 사용자({s.actor})의 캘리브레이션 세션", 403, "owner")


# ── 모델 ─────────────────────────────────────────────────────────────

class StartIn(BaseModel):
    device_id: int = Field(ge=1, le=20)
    sn: str = ""
    adc: str = Field("ad7739", pattern="^(ad7739|mcp3202)$")
    mode: str = Field("full", pattern="^(full|partial)$")


class DmmIn(BaseModel):
    host: str | None = None
    port: int | None = None


class PrecheckIn(BaseModel):
    user_checks: dict[str, bool] | None = None


class JogIn(BaseModel):
    target: str = Field(pattern="^(hv|cv)$")
    volt: float | None = None
    delta: float | None = None
    dac: int | None = Field(None, ge=0, le=65535)
    avg_n: int = Field(3, ge=1, le=100)


class ReadIn(BaseModel):
    target: str = Field(pattern="^(hv|cv)$")
    avg_n: int = Field(3, ge=1, le=100)


class MarkIn(BaseModel):
    target: str = Field(pattern="^(hv|cv)$")
    which: str = Field(pattern="^(min|max)$")
    volt: float | None = None


class Sweep(BaseModel):
    enabled: bool | None = None
    include_zero: bool | None = None
    start: float | None = None
    end: float | None = None
    step: float | None = Field(None, gt=0)


class PlanIn(BaseModel):
    hv: Sweep | None = None
    cv: Sweep | None = None
    avg_n: int | None = Field(None, ge=1, le=100)
    settle_ms: int | None = Field(None, ge=0, le=10000)
    sense_ms: int | None = Field(None, ge=50, le=5000)


class SaveIn(BaseModel):
    types: list[str] | None = None          # ["HV_DAC","HV_VS","CV_DAC"]
    memo: str | None = None


class JudgeIn(BaseModel):
    verdict: str = Field(pattern="^(pass|fail)$")
    memo: str = ""


def _plan_dict(b: PlanIn):
    d = b.model_dump(exclude_none=True)
    return d


# ── 공통 ─────────────────────────────────────────────────────────────

@router.get("")
async def cal_state(_p: Principal = Depends(READ)):
    return C.CAL.snapshot()


@router.post("/estop")
async def cal_estop(p: Principal = Depends(CONTROL)):
    s = await C.CAL.estop(f"비상 정지 ({p.actor})")
    return dict(result="ok", session=bool(s))


# ── ① 초기화 ─────────────────────────────────────────────────────────

@router.post("/session")
async def cal_start(b: StartIn, p: Principal = Depends(CONTROL)):
    s = await C.CAL.start(b.device_id, b.sn.strip(), b.adc, b.mode, p.actor, p.owner_id)
    return s.to_dict()


@router.delete("/session")
async def cal_end(p: Principal = Depends(CONTROL)):
    _owner(p)
    s = await C.CAL.end()
    return dict(result="ok", id=s.id)


@router.post("/dmm/connect")
async def cal_dmm(b: DmmIn, _p: Principal = Depends(CONTROL)):
    idn = await dmm.connect(b.host, b.port)
    return dict(idn=idn, dmm=dmm.status())


@router.post("/precheck")
async def cal_precheck(b: PrecheckIn, p: Principal = Depends(CONTROL)):
    _owner(p)
    return await C.CAL.precheck(b.user_checks)


@router.post("/backup")
async def cal_backup(p: Principal = Depends(CONTROL)):
    _owner(p)
    return await C.CAL.backup()


@router.post("/init")
async def cal_init(p: Principal = Depends(CONTROL)):
    _owner(p)
    return await C.CAL.init_linear()


@router.post("/restore")
async def cal_restore(p: Principal = Depends(CONTROL)):
    _owner(p)
    return await C.CAL.restore()


# ── ② 수동 범위 ──────────────────────────────────────────────────────

@router.post("/jog")
async def cal_jog(b: JogIn, p: Principal = Depends(CONTROL)):
    _owner(p)
    return await C.CAL.jog(b.target, volt=b.volt, delta=b.delta, dac=b.dac, avg_n=b.avg_n)


@router.post("/read")
async def cal_read(b: ReadIn, p: Principal = Depends(CONTROL)):
    _owner(p)
    return await C.CAL.read(b.target, b.avg_n)


@router.post("/range-mark")
async def cal_mark(b: MarkIn, p: Principal = Depends(CONTROL)):
    _owner(p)
    return C.CAL.mark_range(b.target, b.which, b.volt)


@router.put("/plan")
async def cal_plan(b: PlanIn, p: Principal = Depends(CONTROL)):
    _owner(p)
    return C.CAL.set_plan(_plan_dict(b))


# ── ③ 자동 측정 / 작업 제어 ──────────────────────────────────────────

@router.post("/sweep")
async def cal_sweep(p: Principal = Depends(CONTROL)):
    _owner(p)
    await C.CAL.start_sweep()
    return C.CAL.snapshot()


@router.post("/job/{action}")
async def cal_job(action: str, p: Principal = Depends(CONTROL)):
    _owner(p)
    if action == "pause":
        C.CAL.pause()
    elif action == "resume":
        C.CAL.resume()
    elif action == "continue":
        C.CAL.cont()
    elif action == "abort":
        await C.CAL.abort()
    else:
        raise C.CalError("action 은 pause | resume | continue | abort", 400)
    return C.CAL.snapshot()


@router.get("/points")
async def cal_points(kind: str = Query("sweep", pattern="^(sweep|verify|jog)$"), since: int = Query(0, ge=0),
                     _p: Principal = Depends(READ)):
    rows = C.CAL.jog_rows(since) if kind == "jog" else C.CAL.point_rows(kind, since)
    return dict(kind=kind, since=since, rows=rows)


# ── ④ 저장 ───────────────────────────────────────────────────────────

@router.post("/build")
async def cal_build(p: Principal = Depends(CONTROL)):
    _owner(p)
    return C.CAL.build()


@router.post("/save")
async def cal_save(b: SaveIn, p: Principal = Depends(CONTROL)):
    _owner(p)
    return await C.CAL.save(b.types, b.memo)


# ── ⑤ 검증 ───────────────────────────────────────────────────────────

@router.post("/verify")
async def cal_verify(b: PlanIn, p: Principal = Depends(CONTROL)):
    _owner(p)
    await C.CAL.start_verify(_plan_dict(b))
    return C.CAL.snapshot()


@router.post("/judge")
async def cal_judge(b: JudgeIn, p: Principal = Depends(CONTROL)):
    _owner(p)
    return C.CAL.judge(b.verdict, b.memo)


# ── ⑥ 이력 ───────────────────────────────────────────────────────────

@router.get("/history")
async def cal_history(device_id: int | None = None, sn: str | None = None,
                      limit: int = Query(100, ge=1, le=1000), _p: Principal = Depends(READ)):
    return dict(rows=C.history(device_id, sn, limit))


@router.get("/history/{sid}")
async def cal_history_get(sid: str, _p: Principal = Depends(READ)):
    return C.history_get(sid)
