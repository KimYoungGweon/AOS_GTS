"""AOS HV / CV 자동 캘리브레이션 — 세션·작업 엔진  (DOC/GTS_HV캘리브레이션_계획.md)

흐름 (웹 5-1 ~ 5-6)
  ① 초기화      start → precheck → backup(CAL_QUERY 0·1·4) → init(선형 2 점 CAL_WRITE)
  ② 수동 범위   jog(HV/CV 설정 + DMM + HV_Vs) → plan(범위·간격, ≤ 101)
  ③ 자동 측정   sweep job — HV(HV_DAC+HV_ADC 동시) → 0 V·방전 → DMM 을 CV 로 옮기기(사용자) → CV
  ④ 저장        build(새 테이블 + 검사, 일부 구간이면 병합) → save(CAL_WRITE → CAL_QUERY 되읽기 비교)
  ⑤ 검증        verify job — 보정된 경로(PARAM_SET)로 출력 → DMM / HV_Vs 오차 → judge(합격/불합격 + 메모)
  ⑥ 비교        history (지금은 JSON 파일 — DB 단계에서 테이블로 옮김)

경로: 서버 ─UDP 5500─ gts_aos_bridge ─UART2─ STM32   (브리지 명령 0x46~0x4E, udp_server.py)
DMM : gts/dmm.py (34461A, HV 1000 V / CV 10 V 레인지 고정)

출력 방법
  F/W 에 raw DAC 명령이 없어도 된다. 서버는 F/W 가 쓰고 있는 테이블을 알고 있으므로
  원하는 DAC 가 나오도록 설정 전압을 역산해 PARAM_SET 으로 보내고, F/W 가 실제로 낸 DAC 를
  같은 float32 식으로 계산해 기록한다 (caltable.setv_for_dac).  HV_ADC raw 도 HV_Vs 평균을
  VS 테이블로 역산한다.  → 선형 초기화 후 전체 보정 / 초기화 없는 일부 구간 보정 모두 같은 코드.

안전
  - 캘리브레이션 중인 AOS 는 udp_server.CAL_BLOCK — 콘솔·웹 파라미터 변경, run 시작 거부
  - HV 는 서버가 CAL_RAMP_STEP_V 씩 램프, 0~200 V / CV −5~+5 V 로 clamp
  - DMM·브리지 무응답, 설정 대비 편차 과다, 역전, 과전압 → 자동 중단 → 0 V
  - estop 은 언제든 (작업 취소 + HV/CV 0)
"""
from __future__ import annotations

import asyncio
import json
import os
import struct
import time
import uuid
from collections import deque
from pathlib import Path

import udp_server as S

from . import caltable as T
from . import config, hooks, live
from .db import store
from .dmm import DmmError, dmm

P_HV, P_CV = 0, 3                      # PARAM_SET id


class CalError(Exception):
    def __init__(self, msg, http=409, code="cal"):
        super().__init__(msg)
        self.msg, self.http, self.code = msg, http, code


class CalAbort(Exception):
    pass


# ── 브리지 요청/응답 ─────────────────────────────────────────────────

class BridgeIO:
    def __init__(self):
        self.waiters: dict = {}

    def on_ack(self, dev, ack_cmd, result, ack_seq):
        f = self.waiters.pop(("ack", dev.did, ack_seq), None)
        if f and not f.done():
            f.set_result(result)

    def on_cal(self, dev, cmd, seq, pl):
        f = self.waiters.pop(("resp", dev.did, seq), None)
        if f is None:                                   # seq 를 반사하지 않는 브리지 대비
            f = self.waiters.pop(("resp", dev.did, cmd), None)
        if f and not f.done():
            f.set_result((cmd, pl))

    def _send(self, did, cmd, payload):
        proto = S.HOLDER.get("aos")
        dev = S.AOSES.get(did)
        if proto is None:
            raise CalError("UDP 서버 준비 안 됨", 503)
        if dev is None or not dev.online:
            raise CalError(f"AOS {did:02d} 오프라인 (브리지 응답 없음)", 409, "offline")
        seq = proto.send(dev, cmd, payload)
        if not seq:
            raise CalError(f"AOS {did:02d} 주소 미확인", 409, "offline")
        return seq

    async def ack(self, did, cmd, payload, timeout=None, retries=2):
        timeout = timeout or config.CAL_BRIDGE_TIMEOUT
        loop = asyncio.get_running_loop()
        for _ in range(retries + 1):
            seq = self._send(did, cmd, payload)
            fut = loop.create_future()
            self.waiters[("ack", did, seq)] = fut
            try:
                res = await asyncio.wait_for(fut, timeout)
            except asyncio.TimeoutError:
                self.waiters.pop(("ack", did, seq), None)
                continue
            if res:
                raise CalError(f"브리지가 {S.AOS_CMD_NAME.get(cmd, hex(cmd))} 거부 (result={res})", 502,
                               "bridge")
            return
        raise CalError(f"브리지 무응답 — {S.AOS_CMD_NAME.get(cmd, hex(cmd))}", 504, "bridge")

    async def call(self, did, cmd, payload, resp_cmd, timeout=None, retries=2):
        timeout = timeout or config.CAL_BRIDGE_TIMEOUT
        loop = asyncio.get_running_loop()
        for _ in range(retries + 1):
            seq = self._send(did, cmd, payload)
            fut = loop.create_future()
            self.waiters[("resp", did, seq)] = fut
            self.waiters[("resp", did, resp_cmd)] = fut
            try:
                rcmd, pl = await asyncio.wait_for(fut, timeout)
            except asyncio.TimeoutError:
                continue
            finally:
                self.waiters.pop(("resp", did, seq), None)
                self.waiters.pop(("resp", did, resp_cmd), None)
            return pl
        raise CalError(f"브리지 무응답 — {S.AOS_CMD_NAME.get(cmd, hex(cmd))}", 504, "bridge")


io = BridgeIO()


# ── 세션 ─────────────────────────────────────────────────────────────

def _now():
    return time.time()


def _tbl(d):
    return {T.TYPE_NAME[t]: dict(type=t, x=list(v[0]), y=list(v[1]), no=len(v[0])) for t, v in d.items()}


DEFAULT_PLAN = dict(
    hv=dict(enabled=True, include_zero=True, start=30.0, end=200.0, step=2.0),
    cv=dict(enabled=True, start=-5.0, end=5.0, step=0.1),
    avg_n=5, settle_ms=400, sense_ms=400,
)
DEFAULT_VERIFY = dict(
    hv=dict(enabled=True, start=31.0, end=199.0, step=8.0),
    cv=dict(enabled=True, start=-4.95, end=4.95, step=0.5),
    avg_n=5, settle_ms=600, sense_ms=400,
)


class Session:
    def __init__(self, did, sn, adc, mode, actor, owner_id):
        self.id = time.strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:4]
        self.did, self.sn, self.adc, self.mode = did, sn, adc, mode
        self.actor, self.owner_id = actor, owner_id
        self.created = _now()
        self.checks: list = []
        self.user_checks: dict = {}
        self.backup: dict = {}            # type -> (xs, ys)   세션 시작 때 F/W 값
        self.backup_at = None
        self.fw: dict = {}                # type -> (xs, ys)   지금 F/W 가 쓰는 값 (서버가 아는 한)
        self.initialized = False
        self.plan = json.loads(json.dumps(DEFAULT_PLAN))
        if mode == "partial":
            self.plan["hv"].update(include_zero=False, start=150.0, end=200.0)
            self.plan["cv"].update(enabled=False)
        self.range_marks = dict(hv=dict(min=None, max=None), cv=dict(min=None, max=None))
        self.jog_log: list = []
        self.points: list = []
        self.new: dict = {}               # type -> (xs, ys)
        self.build_info: dict = {}
        self.saved: dict = {}             # type name -> ts
        self.verify_cfg = json.loads(json.dumps(DEFAULT_VERIFY))
        self.verify_points: list = []
        self.verify_summary: dict = {}
        self.verdict = None
        self.memo = ""
        self.out = dict(hv=0.0, cv=0.0)
        self.dmm_on = "hv"                # DMM 이 연결된 출력 (사전 점검 기준 HV)
        self.events = deque(maxlen=200)
        self.ev_id = 0
        self.job = None                   # dict
        self.ended = None

    def log(self, level, msg):
        self.ev_id += 1
        self.events.appendleft(dict(id=self.ev_id, t=_now(), level=level, msg=msg))
        live.add_event("info" if level == "info" else level, "cal",
                       f"AOS {self.did:02d} 캘리브레이션 · {msg}", S.DEV_AOS, self.did)

    def to_dict(self, full=True):
        j = None
        if self.job:
            j = {k: v for k, v in self.job.items() if k not in ("task", "resume", "user")}
        d = dict(id=self.id, device_id=self.did, sn=self.sn, adc=self.adc, mode=self.mode,
                 actor=self.actor, created=self.created, ended=self.ended,
                 checks=self.checks, user_checks=self.user_checks,
                 backup_at=self.backup_at, initialized=self.initialized,
                 plan=self.plan, plan_preview=plan_preview(self.plan, self.mode),
                 range_marks=self.range_marks, out=self.out, dmm_on=self.dmm_on,
                 saved=self.saved, verify_cfg=self.verify_cfg, verify_summary=self.verify_summary,
                 verdict=self.verdict, memo=self.memo, job=j,
                 counts=dict(points=len(self.points), verify=len(self.verify_points),
                             jog=len(self.jog_log)))
        if full:
            d.update(backup=_tbl(self.backup), fw=_tbl(self.fw), new=_tbl(self.new),
                     build_info=self.build_info, events=list(self.events)[:50])
        return d


def plan_preview(plan, mode="full"):
    out = {}
    for tg in ("hv", "cv"):
        c = plan[tg]
        if not c.get("enabled"):
            out[tg] = dict(enabled=False, count=0, points=[])
            continue
        p = T.plan(tg, float(c["start"]), float(c["end"]), float(c["step"]),
                   include_zero=bool(c.get("include_zero")) and tg == "hv", no_max=config.CAL_NO_MAX)
        tt = T.HV_DAC if tg == "hv" else T.CV_DAC
        pts = p["points"]
        prev = [dict(v=v, dac=T.nominal_dac(tt, v)) for v in pts]
        if tg == "hv" and c.get("include_zero") and pts and pts[0] > 0:
            prev.insert(0, dict(v=0.0, dac=0))
        out[tg] = dict(enabled=True, count=p["count"], error=p["error"], points=prev)
    n = sum(v["count"] for v in out.values())
    sec = n * (plan.get("settle_ms", 400) / 1000 + plan.get("avg_n", 5) * config.DMM_NPLC * 0.04 + 0.6)
    out["total"] = dict(count=n, est_sec=round(sec))
    return out


# ── 매니저 (DMM 이 하나이므로 세션도 하나) ───────────────────────────

class CalManager:
    def __init__(self):
        self.s: Session | None = None
        self.lock = asyncio.Lock()        # 출력을 움직이는 동작은 하나씩

    # 상태 --------------------------------------------------------------
    def need(self, owner_id=None):
        if self.s is None or self.s.ended:
            raise CalError("캘리브레이션 세션 없음 — 먼저 시작하세요", 409, "no_session")
        return self.s

    def busy(self):
        s = self.s
        return bool(s and s.job and s.job["state"] in ("running", "paused", "wait_user"))

    def snapshot(self):
        return dict(session=self.s.to_dict() if self.s and not self.s.ended else None,
                    dmm=dmm.status(), limits=limits())

    # 시작 / 종료 -------------------------------------------------------
    async def start(self, did, sn, adc, mode, actor, owner_id):
        if self.s and not self.s.ended:
            raise CalError(f"이미 AOS {self.s.did:02d} 캘리브레이션 중 ({self.s.actor}) — DMM 은 하나뿐")
        if adc not in T.ADC_FS:
            raise CalError("adc 는 ad7739 | mcp3202", 400)
        if mode not in ("full", "partial"):
            raise CalError("mode 는 full | partial", 400)
        dev = S.AOSES.get(did)
        if dev is None or not dev.online:
            raise CalError(f"AOS {did:02d} 오프라인", 409, "offline")
        rid = store.active_run.get(did)
        if rid:
            raise CalError(f"AOS {did:02d} 에서 측정 run #{rid} 진행 중")
        s = Session(did, sn or "", adc, mode, actor, owner_id)
        self.s = s
        S.CAL_BLOCK.add(did)
        try:
            await io.ack(did, S.A_CAL_MODE, bytes([1, 0, 0, 0]))
        except CalError as e:
            s.log("warn", f"브리지 CAL_MODE 응답 없음 ({e.msg}) — 계속")
        s.log("info", f"세션 시작 — {actor}, SN {sn or '미입력'}, ADC {adc}, "
                      f"{'일부 구간' if mode == 'partial' else '전체'} 보정")
        await self._zero(ramp=False)
        return s

    async def end(self, reason="종료"):
        s = self.need()
        await self._cancel_job("세션 종료")
        try:
            await self._zero(ramp=False)
        except CalError:
            pass
        try:
            await io.ack(s.did, S.A_CAL_MODE, bytes([0, 0, 0, 0]))
        except CalError:
            pass
        S.CAL_BLOCK.discard(s.did)
        dev = S.AOSES.get(s.did)
        if dev and S.HOLDER.get("aos"):
            S.HOLDER["aos"].send(dev, S.A_PARAMS_QUERY, b"")
        s.ended = _now()
        s.log("info", f"세션 {reason}")
        persist(s)
        return s

    async def estop(self, reason="비상 정지"):
        s = self.s
        if s is None or s.ended:
            return None
        await self._cancel_job(reason)
        for tg, pid in (("hv", P_HV), ("cv", P_CV)):
            try:
                self._param(s.did, pid, 0.0)
                s.out[tg] = 0.0
            except CalError:
                pass
        s.log("error", f"{reason} — HV 0 V · CV 0 V")
        return s

    async def _cancel_job(self, why):
        s = self.s
        if s and s.job and s.job.get("task") and not s.job["task"].done():
            s.job["cancel_reason"] = why
            s.job["task"].cancel()
            try:
                await s.job["task"]
            except (asyncio.CancelledError, Exception):     # noqa: BLE001
                pass

    # 출력 --------------------------------------------------------------
    def _param(self, did, pid, v):
        proto = S.HOLDER.get("aos")
        dev = S.AOSES.get(did)
        if proto is None or dev is None or not dev.online:
            raise CalError(f"AOS {did:02d} 오프라인", 409, "offline")
        proto.send(dev, S.A_PARAM_SET, struct.pack("<B3xf", pid, float(v)))

    async def _set(self, target, v, ramp=True):
        s = self.s
        if target == "hv":
            v = max(0.0, min(config.CAL_HV_MAX, float(v)))
            cur = s.out["hv"]
            while ramp and abs(v - cur) > config.CAL_RAMP_STEP_V:
                cur += config.CAL_RAMP_STEP_V if v > cur else -config.CAL_RAMP_STEP_V
                self._param(s.did, P_HV, cur)
                s.out["hv"] = cur
                await asyncio.sleep(config.CAL_RAMP_DELAY)
            self._param(s.did, P_HV, v)
            s.out["hv"] = v
        else:
            v = max(config.CAL_CV_MIN, min(config.CAL_CV_MAX, float(v)))
            self._param(s.did, P_CV, v)
            s.out["cv"] = v
        return v

    async def _zero(self, ramp=True):
        s = self.s
        await self._set("hv", 0.0, ramp=ramp)
        await self._set("cv", 0.0, ramp=False)

    async def _sense(self, window_ms):
        s = self.s
        pl = await io.call(s.did, S.A_CAL_SENSE_Q, struct.pack("<HH", int(window_ms), 0), S.A_CAL_SENSE,
                           timeout=config.CAL_BRIDGE_TIMEOUT + window_ms / 1000)
        avg, mn, mx, n, _r, age = struct.unpack(S.AOS_CAL_SENSE_FMT, pl[:20])
        if n == 0:
            raise CalError("HV_Vs 샘플 없음 (STM32 0x03 자동 상태 꺼짐?)", 502, "bridge")
        return dict(avg=avg, min=mn, max=mx, n=n, age_ms=age)

    def _tables_now(self):
        """지금 F/W 가 쓰는 테이블 — 백업 전이면 오류"""
        s = self.s
        if not s.fw:
            raise CalError("F/W 테이블을 아직 읽지 않았습니다 — 초기화 단계에서 백업 먼저")
        return s.fw

    # ① 초기화 ----------------------------------------------------------
    async def precheck(self, user_checks: dict | None = None):
        s = self.need()
        if user_checks is not None:
            s.user_checks = {k: bool(v) for k, v in user_checks.items()}
        res = []

        def add(key, label, ok, value, how=""):
            res.append(dict(key=key, label=label, ok=bool(ok), value=value, how=how))

        try:
            if not dmm.connected:
                await dmm.connect()
            add("dmm", "DMM 응답", True, dmm.idn, "*IDN?")
        except DmmError as e:
            add("dmm", "DMM 응답", False, str(e), "*IDN?")
        async with self.lock:
            try:
                r = await dmm.read("hv", 3)
                add("dmm_zero", "DMM 0 V 입력", abs(r["v"]) < 0.5, f"{r['v']:+.4f} V", "HV 0 V · READ? × 3")
            except DmmError as e:
                add("dmm_zero", "DMM 0 V 입력", False, str(e))
            try:
                sn = await self._sense(200)
                add("bridge", "AOS 응답 (브리지 · HV_Vs)", True, f"HV_Vs {sn['avg']:.2f} V · {sn['n']} 샘플",
                    "0x4A CAL_SENSE_Q")
            except CalError as e:
                sn = None
                add("bridge", "AOS 응답 (브리지 · HV_Vs)", False, e.msg, "0x4A CAL_SENSE_Q")
            try:
                pl = await io.call(s.did, S.A_CAL_QUERY, bytes([T.HV_DAC, 0, 0, 0]), S.A_CAL_TABLE)
                t, xs, _ = T.unpack(pl)
                add("cal_cmd", "보정 테이블 명령", t == T.HV_DAC, f"CAL_QUERY 응답 {len(xs)} point",
                    "0x48 → STM32 0xA1")
            except (CalError, ValueError) as e:
                add("cal_cmd", "보정 테이블 명령", False, getattr(e, "msg", str(e)), "0x48 → STM32 0xA1")
        rid = store.active_run.get(s.did)
        add("run", "측정 run 없음", not rid, f"run #{rid}" if rid else "대기")
        z = s.out["hv"] == 0 and s.out["cv"] == 0
        add("zero", "출력 0 V 설정", z, f"HV {s.out['hv']:.1f} · CV {s.out['cv']:.3f}"
            + (f" · HV_Vs {sn['avg']:.2f} V" if sn else ""))
        s.checks = res
        ok_auto = all(c["ok"] for c in res)
        s.log("info" if ok_auto else "warn",
              f"사전 점검 {'통과' if ok_auto else '실패'} ({sum(c['ok'] for c in res)}/{len(res)})")
        return dict(checks=res, user_checks=s.user_checks, ok=ok_auto)

    async def _query(self, t):
        pl = await io.call(self.s.did, S.A_CAL_QUERY, bytes([t, 0, 0, 0]), S.A_CAL_TABLE)
        rt, xs, ys = T.unpack(pl)
        if rt != t:
            raise CalError(f"CAL_QUERY type {t} 요청에 type {rt} 응답", 502, "bridge")
        return xs, ys

    async def _write(self, t, xs, ys):
        if len(xs) > config.CAL_NO_MAX or len(xs) < 2:
            raise CalError(f"{T.TYPE_NAME[t]} 포인트 {len(xs)} 개 — 2 ~ {config.CAL_NO_MAX}", 400)
        await io.ack(self.s.did, S.A_CAL_WRITE, T.pack(t, xs, ys), timeout=config.CAL_BRIDGE_TIMEOUT + 1)
        await asyncio.sleep(0.2)                              # EEPROM 기록 시간
        back = await self._query(t)
        if not T.same(t, (xs, ys), back):
            raise CalError(f"{T.TYPE_NAME[t]} 되읽기 불일치 — F/W 가 받지 못했거나 CAL_NO 초과", 502, "verify")
        self.s.fw[t] = back
        return back

    async def backup(self):
        s = self.need()
        if self.busy():
            raise CalError("작업 진행 중")
        async with self.lock:
            got = {}
            for t in T.TYPES:
                got[t] = await self._query(t)
        s.backup = dict(got)
        s.fw = dict(got)
        s.backup_at = _now()
        s.log("info", "F/W 테이블 백업 — " + " · ".join(f"{T.TYPE_NAME[t]} {len(v[0])}p" for t, v in got.items()))
        persist(s)
        return _tbl(got)

    async def init_linear(self):
        s = self.need()
        if s.mode == "partial":
            raise CalError("일부 구간 보정은 기존 테이블을 써야 하므로 초기화하지 않습니다")
        if not s.backup:
            raise CalError("백업 먼저 — 초기화 전에 현재 테이블을 읽어 둡니다")
        if self.busy():
            raise CalError("작업 진행 중")
        async with self.lock:
            await self._zero()
            for t, (xs, ys) in T.linear_tables(s.adc).items():
                await self._write(t, xs, ys)
        s.initialized = True
        s.log("info", "EEPROM 선형 초기화 완료 (HV_DAC · HV_VS · CV_DAC 2 점)")
        return _tbl(s.fw)

    async def restore(self):
        s = self.need()
        if not s.backup:
            raise CalError("백업 없음")
        if self.busy():
            raise CalError("작업 진행 중")
        async with self.lock:
            await self._zero()
            for t, (xs, ys) in s.backup.items():
                await self._write(t, xs, ys)
        s.initialized = False
        s.log("warn", "백업 테이블로 복원")
        return _tbl(s.fw)

    # ② 수동 범위 -------------------------------------------------------
    async def jog(self, target, volt=None, delta=None, dac=None, avg_n=3, sense_ms=200):
        s = self.need()
        if target not in ("hv", "cv"):
            raise CalError("target 은 hv | cv", 400)
        if self.busy():
            raise CalError("자동 작업 진행 중 — 조그 불가")
        fw = self._tables_now()
        tt = T.HV_DAC if target == "hv" else T.CV_DAC
        if dac is not None:
            v, _ = T.setv_for_dac(*fw[tt], int(dac))
        elif delta is not None:
            v = s.out[target] + float(delta)
        elif volt is not None:
            v = float(volt)
        else:
            v = s.out[target]
        async with self.lock:
            v = await self._set(target, v)
            await asyncio.sleep(0.3)
            return await self._read(target, avg_n, sense_ms, record=True)

    async def read(self, target, avg_n=3, sense_ms=200):
        self.need()
        async with self.lock:
            return await self._read(target, avg_n, sense_ms)

    async def _read(self, target, avg_n, sense_ms, record=False):
        s = self.s
        fw = self._tables_now()
        tt = T.HV_DAC if target == "hv" else T.CV_DAC
        setv = s.out[target]
        r = dict(target=target, setv=setv, dac=T.fw_dac(*fw[tt], setv), t=_now())
        if s.dmm_on != target:
            r["dmm_warn"] = f"DMM 이 {s.dmm_on.upper()} 출력에 연결된 것으로 되어 있음"
        try:
            d = await dmm.read(target, avg_n)
            r.update(meas=d["v"], sd=d["sd"], dev=d["v"] - setv)
        except DmmError as e:
            r.update(meas=None, dmm_error=str(e))
        if target == "hv":
            try:
                sn = await self._sense(sense_ms)
                r.update(hv_vs=sn["avg"], raw=T.raw_for_sense(*fw[T.HV_VS], sn["avg"]))
            except CalError as e:
                r.update(hv_vs=None, sense_error=e.msg)
        if record:
            s.jog_log.append(r)
            del s.jog_log[:-200]
        return r

    def mark_range(self, target, which, volt=None):
        s = self.need()
        v = s.out[target] if volt is None else float(volt)
        s.range_marks[target][which] = v
        c = s.plan[target]
        if which == "min":
            c["start"] = v
        else:
            c["end"] = v
        s.log("info", f"{target.upper()} {'최소' if which == 'min' else '최대'} = {v:g} V")
        return plan_preview(s.plan, s.mode)

    def set_plan(self, cfg: dict):
        s = self.need()
        if self.busy():
            raise CalError("작업 진행 중")
        for tg in ("hv", "cv"):
            if tg in cfg and cfg[tg]:
                s.plan[tg].update({k: v for k, v in cfg[tg].items() if v is not None})
        for k in ("avg_n", "settle_ms", "sense_ms"):
            if cfg.get(k) is not None:
                s.plan[k] = cfg[k]
        return plan_preview(s.plan, s.mode)

    def jog_rows(self, since=0):
        s = self.need()
        return s.jog_log[since:]

    # ③ 자동 측정 -------------------------------------------------------
    def _new_job(self, kind, total):
        s = self.s
        s.job = dict(kind=kind, state="running", phase="", i=0, n=total, started=_now(), msg="",
                     resume=asyncio.Event(), user=asyncio.Event(), wait=None, error=None,
                     finished=None)
        s.job["resume"].set()
        return s.job

    async def start_sweep(self):
        s = self.need()
        if self.busy():
            raise CalError("이미 작업 진행 중")
        self._tables_now()
        if s.mode == "full" and not s.initialized:
            raise CalError("전체 보정은 선형 초기화 후에 시작합니다 (① 초기화)")
        pv = plan_preview(s.plan, s.mode)
        for tg in ("hv", "cv"):
            if pv[tg]["enabled"] and pv[tg].get("error"):
                raise CalError(f"{tg.upper()} 계획 오류 — {pv[tg]['error']}", 400)
        if not (pv["hv"]["enabled"] or pv["cv"]["enabled"]):
            raise CalError("HV·CV 중 하나 이상 선택", 400)
        s.points = [p for p in s.points if p["target"] not in
                    [tg for tg in ("hv", "cv") if pv[tg]["enabled"]]]
        job = self._new_job("sweep", pv["total"]["count"])
        job["task"] = asyncio.create_task(self._run_sweep(pv))
        s.log("info", f"자동 측정 시작 — {pv['total']['count']} point")
        return s.job

    async def _checkpoint(self):
        j = self.s.job
        if not j["resume"].is_set():
            j["state"] = "paused"
            await j["resume"].wait()
            j["state"] = "running"

    async def _wait_user(self, target):
        s = self.s
        j = s.job
        await self._zero()
        await self._discharge()
        j["state"], j["wait"] = "wait_user", f"DMM 을 {target.upper()} 출력으로 옮긴 뒤 '계속'"
        j["user"].clear()
        s.log("warn", j["wait"])
        await j["user"].wait()
        s.dmm_on = target
        j["state"], j["wait"] = "running", None

    async def _discharge(self):
        s = self.s
        t0 = _now()
        while _now() - t0 < config.CAL_DISCHARGE_TIMEOUT:
            try:
                sn = await self._sense(200)
                if sn["avg"] < config.CAL_DISCHARGE_V:
                    return True
            except CalError:
                pass
            await asyncio.sleep(0.5)
        s.log("warn", "방전 대기 시간 초과 — HV_Vs 가 아직 높음")
        return False

    async def _run_sweep(self, pv):
        s = self.s
        j = s.job
        plan = s.plan
        try:
            for tg in ("hv", "cv"):
                if not pv[tg]["enabled"]:
                    continue
                if s.dmm_on != tg:
                    await self._wait_user(tg)
                tt = T.HV_DAC if tg == "hv" else T.CV_DAC
                fw = self._tables_now()
                j["phase"] = f"{tg.upper()} 스윕"
                prev = None
                for p in pv[tg]["points"]:
                    await self._checkpoint()
                    if tg == "hv" and p["dac"] == 0 and p["v"] == 0:
                        j["i"] += 1                      # 0 V 기준점은 측정하지 않고 테이블에 넣는다
                        continue
                    async with self.lock:
                        setv, dac = T.setv_for_dac(*fw[tt], p["dac"])
                        await self._set(tg, setv)
                        await asyncio.sleep(plan["settle_ms"] / 1000)
                        try:
                            d = await dmm.read(tg, plan["avg_n"])
                        except DmmError as e:
                            raise CalAbort(f"DMM 오류 — {e}")
                        pt = dict(target=tg, no=j["i"] + 1, nominal=p["v"], setv=setv, dac=dac,
                                  meas=d["v"], sd=d["sd"], dev=d["v"] - setv, t=_now(), ok=True)
                        if tg == "hv":
                            try:
                                sn = await self._sense(plan["sense_ms"])
                            except CalError as e:
                                raise CalAbort(e.msg)
                            pt.update(hv_vs=sn["avg"], raw=T.raw_for_sense(*fw[T.HV_VS], sn["avg"]),
                                      sense_n=sn["n"])
                    self._guard(tg, pt, prev)
                    s.points.append(pt)
                    prev = pt
                    j["i"] += 1
                    j["msg"] = f"{tg.upper()} {p['v']:g} V → {d['v']:.4f} V"
            await self._zero()
            j["state"] = "done"
            s.log("info", f"자동 측정 완료 — {len(s.points)} point")
        except CalAbort as e:
            j["state"], j["error"] = "aborted", str(e)
            s.log("error", f"자동 중단 — {e}")
            await self._safe_zero()
        except asyncio.CancelledError:
            j["state"] = "aborted"
            j["error"] = j.get("cancel_reason", "사용자 중단")
            s.log("warn", f"측정 중단 — {j['error']}")
            await self._safe_zero()
            raise
        except Exception as e:                           # noqa: BLE001
            j["state"], j["error"] = "failed", repr(e)
            s.log("error", f"측정 실패 — {e!r}")
            await self._safe_zero()
        finally:
            j["finished"] = _now()
            persist(s)

    async def _safe_zero(self):
        s = self.s
        for pid, tg in ((P_HV, "hv"), (P_CV, "cv")):
            try:
                self._param(s.did, pid, 0.0)
                s.out[tg] = 0.0
            except CalError:
                pass

    def _guard(self, tg, pt, prev):
        lim = config.CAL_DEV_LIMIT_HV if tg == "hv" else config.CAL_DEV_LIMIT_CV
        if abs(pt["dev"]) > lim:
            raise CalAbort(f"{tg.upper()} {pt['setv']:.2f} V 설정에 실측 {pt['meas']:.3f} V — "
                           f"편차 {pt['dev']:+.2f} V > {lim:g} V (출력 안 나옴? 배선?)")
        if tg == "hv" and pt["meas"] > config.CAL_HV_MAX + 5:
            raise CalAbort(f"HV 과전압 {pt['meas']:.2f} V")
        if prev:
            tol = 0.05 if tg == "hv" else 0.005
            if pt["meas"] < prev["meas"] - tol:
                raise CalAbort(f"실측 역전 {prev['meas']:.4f} → {pt['meas']:.4f} V")
            if tg == "hv" and pt.get("raw") is not None and prev.get("raw") is not None \
                    and pt["raw"] < prev["raw"] - 2:
                raise CalAbort(f"ADC 역전 {prev['raw']:.1f} → {pt['raw']:.1f}")

    def pause(self):
        j = self._job()
        j["resume"].clear()
        self.s.log("info", "일시 정지")

    def resume(self):
        j = self._job()
        j["resume"].set()
        self.s.log("info", "재개")

    def cont(self):
        j = self._job()
        if j["state"] != "wait_user":
            raise CalError("사용자 확인을 기다리는 중이 아님")
        j["user"].set()

    async def abort(self):
        self._job()
        await self._cancel_job("사용자 중단")

    def _job(self):
        s = self.need()
        if not s.job or s.job["state"] not in ("running", "paused", "wait_user"):
            raise CalError("진행 중인 작업 없음")
        return s.job

    def point_rows(self, kind="sweep", since=0):
        s = self.need()
        return (s.points if kind == "sweep" else s.verify_points)[since:]

    # ④ 저장 ------------------------------------------------------------
    def build(self):
        s = self.need()
        if not s.points:
            raise CalError("측정점 없음")
        inc0 = bool(s.plan["hv"].get("include_zero")) and s.mode == "full"
        new = T.build(s.points, s.adc, include_zero=inc0)
        info = {}
        final = {}
        for t, tbl in new.items():
            old = s.backup.get(t)
            part = None
            if s.mode == "partial":
                if not old:
                    raise CalError("일부 구간 보정인데 백업 테이블이 없음")
                xs, ys, part = T.merge(old, tbl)
                part["old"] = old
            else:
                xs, ys = tbl
            final[t] = (xs, ys)
            info[T.TYPE_NAME[t]] = dict(
                checks=T.check(t, xs, ys, config.CAL_NO_MAX, partial_old=part),
                merge={k: v for k, v in (part or {}).items() if k != "old"} or None,
                old_no=len(old[0]) if old else None, new_no=len(xs))
        s.new, s.build_info = final, info
        return dict(new=_tbl(final), backup=_tbl(s.backup), info=info)

    async def save(self, types=None, memo=None):
        s = self.need()
        if self.busy():
            raise CalError("작업 진행 중")
        if not s.new:
            self.build()
        names = {v: k for k, v in T.TYPE_NAME.items()}
        want = [names[n] if isinstance(n, str) else int(n) for n in (types or [T.TYPE_NAME[t] for t in s.new])]
        for t in want:
            if t not in s.new:
                raise CalError(f"{T.TYPE_NAME.get(t, t)} 새 테이블 없음", 400)
            errs = [c for c in s.build_info[T.TYPE_NAME[t]]["checks"] if c["level"] == "err"]
            if errs:
                raise CalError(f"{T.TYPE_NAME[t]} 검사 실패 — {errs[0]['label']}: {errs[0]['value']}", 400)
        done = []
        async with self.lock:
            await self._zero()
            for t in want:
                await self._write(t, *s.new[t])
                s.saved[T.TYPE_NAME[t]] = _now()
                done.append(T.TYPE_NAME[t])
        if memo:
            s.memo = memo
        s.log("info", f"F/W 저장·되읽기 확인 — {', '.join(done)}")
        persist(s)
        return dict(saved=done, fw=_tbl(s.fw))

    # ⑤ 검증 ------------------------------------------------------------
    async def start_verify(self, cfg=None):
        s = self.need()
        if self.busy():
            raise CalError("이미 작업 진행 중")
        self._tables_now()
        if cfg:
            for tg in ("hv", "cv"):
                if cfg.get(tg):
                    s.verify_cfg[tg].update({k: v for k, v in cfg[tg].items() if v is not None})
            for k in ("avg_n", "settle_ms", "sense_ms"):
                if cfg.get(k) is not None:
                    s.verify_cfg[k] = cfg[k]
        c = s.verify_cfg
        lists = {}
        for tg in ("hv", "cv"):
            if c[tg].get("enabled"):
                try:
                    lists[tg] = T.grid(float(c[tg]["start"]), float(c[tg]["end"]), float(c[tg]["step"]))
                except ValueError as e:
                    raise CalError(f"{tg.upper()} 검증 범위 — {e}", 400)
        if not lists:
            raise CalError("HV·CV 중 하나 이상 선택", 400)
        s.verify_points = []
        s.verify_summary = {}
        s.verdict = None
        job = self._new_job("verify", sum(len(v) for v in lists.values()))
        job["task"] = asyncio.create_task(self._run_verify(lists))
        s.log("info", f"검증 시작 — {job['n']} point")
        return s.job

    async def _run_verify(self, lists):
        s = self.s
        j = s.job
        c = s.verify_cfg
        try:
            for tg, vals in lists.items():
                if s.dmm_on != tg:
                    await self._wait_user(tg)
                j["phase"] = f"{tg.upper()} 검증"
                for v in vals:
                    await self._checkpoint()
                    async with self.lock:
                        await self._set(tg, v)
                        await asyncio.sleep(c["settle_ms"] / 1000)
                        try:
                            d = await dmm.read(tg, c["avg_n"])
                        except DmmError as e:
                            raise CalAbort(f"DMM 오류 — {e}")
                        pt = dict(target=tg, set=v, meas=d["v"], err=d["v"] - v,
                                  pct=(d["v"] - v) / v * 100 if v else None, t=_now())
                        if tg == "hv":
                            try:
                                sn = await self._sense(c["sense_ms"])
                                pt.update(hv_vs=sn["avg"], disp_err=sn["avg"] - d["v"])
                            except CalError as e:
                                pt.update(hv_vs=None, sense_error=e.msg)
                    lim = config.CAL_DEV_LIMIT_HV if tg == "hv" else config.CAL_DEV_LIMIT_CV
                    if abs(pt["err"]) > lim:
                        raise CalAbort(f"{tg.upper()} {v:g} V 오차 {pt['err']:+.2f} V > {lim:g} V")
                    s.verify_points.append(pt)
                    j["i"] += 1
                    j["msg"] = f"{tg.upper()} {v:g} V → {d['v']:.4f} V"
            await self._zero()
            s.verify_summary = verify_summary(s.verify_points)
            j["state"] = "done"
            s.log("info", "검증 완료 — 판정을 입력하세요")
        except CalAbort as e:
            j["state"], j["error"] = "aborted", str(e)
            s.log("error", f"검증 중단 — {e}")
            await self._safe_zero()
        except asyncio.CancelledError:
            j["state"] = "aborted"
            j["error"] = j.get("cancel_reason", "사용자 중단")
            await self._safe_zero()
            raise
        except Exception as e:                           # noqa: BLE001
            j["state"], j["error"] = "failed", repr(e)
            s.log("error", f"검증 실패 — {e!r}")
            await self._safe_zero()
        finally:
            j["finished"] = _now()
            if s.verify_points and not s.verify_summary:
                s.verify_summary = verify_summary(s.verify_points)
            persist(s)

    def judge(self, verdict, memo=""):
        s = self.need()
        if verdict not in ("pass", "fail"):
            raise CalError("verdict 는 pass | fail", 400)
        s.verdict, s.memo = verdict, memo or s.memo
        s.log("info", f"판정 {'합격' if verdict == 'pass' else '불합격'} — {memo}")
        persist(s)
        return dict(verdict=s.verdict, memo=s.memo)


def verify_summary(pts):
    out = {}
    for tg in ("hv", "cv"):
        p = [x for x in pts if x["target"] == tg]
        if not p:
            continue
        e = [x["err"] for x in p]
        d = dict(n=len(p), max_err=max(e, key=abs), avg_abs_err=sum(map(abs, e)) / len(e))
        pc = [x["pct"] for x in p if x.get("pct") is not None]
        if pc:
            d["max_pct"] = max(pc, key=abs)
        de = [x["disp_err"] for x in p if x.get("disp_err") is not None]
        if de:
            d["max_disp_err"] = max(de, key=abs)
            d["avg_abs_disp_err"] = sum(map(abs, de)) / len(de)
        out[tg] = d
    return out


def limits():
    return dict(hv_max=config.CAL_HV_MAX, cv_min=config.CAL_CV_MIN, cv_max=config.CAL_CV_MAX,
                no_max=config.CAL_NO_MAX, ramp_step_v=config.CAL_RAMP_STEP_V,
                dmm_range_hv=config.DMM_RANGE_HV, dmm_range_cv=config.DMM_RANGE_CV,
                dmm_nplc=config.DMM_NPLC, dev_limit_hv=config.CAL_DEV_LIMIT_HV,
                dev_limit_cv=config.CAL_DEV_LIMIT_CV, adc=list(T.ADC_FS),
                default_plan=DEFAULT_PLAN, default_verify=DEFAULT_VERIFY)


# ── 이력 (임시: JSON 파일.  DB 단계에서 cal_session 테이블로) ──────────

def _dir():
    p = Path(config.CAL_DIR)
    p.mkdir(parents=True, exist_ok=True)
    return p


def persist(s: Session):
    try:
        d = s.to_dict()
        d["points"] = s.points
        d["verify_points"] = s.verify_points
        d["jog_log"] = s.jog_log[-50:]
        tmp = _dir() / f".{s.id}.json"
        tmp.write_text(json.dumps(d, ensure_ascii=False, default=str))
        os.replace(tmp, _dir() / f"{s.id}.json")
    except Exception as e:                           # noqa: BLE001
        print(f"[CAL] 기록 저장 실패: {e!r}", flush=True)


def history(device_id=None, sn=None, limit=100):
    rows = []
    for f in sorted(_dir().glob("*.json"), reverse=True):
        try:
            d = json.loads(f.read_text())
        except Exception:                            # noqa: BLE001
            continue
        if device_id and d.get("device_id") != device_id:
            continue
        if sn and d.get("sn") != sn:
            continue
        rows.append(dict(id=d["id"], device_id=d["device_id"], sn=d.get("sn"), adc=d.get("adc"),
                         mode=d.get("mode"), actor=d.get("actor"), created=d.get("created"),
                         saved=d.get("saved"), verdict=d.get("verdict"), memo=d.get("memo"),
                         verify_summary=d.get("verify_summary"),
                         points=len(d.get("points") or [])))
        if len(rows) >= limit:
            break
    return rows


def history_get(sid):
    if "/" in sid or ".." in sid:
        raise CalError("잘못된 id", 400)
    f = _dir() / f"{sid}.json"
    if not f.is_file():
        raise CalError("기록 없음", 404)
    return json.loads(f.read_text())


CAL = CalManager()


def install_hooks():
    hooks.register("aos_ack", io.on_ack)
    hooks.register("aos_cal", io.on_cal)
