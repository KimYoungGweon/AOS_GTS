"""웹 제어 + 장치 소유권 임대 (D14, D14a, D14b)

정책 (2026-09-23.01 변경 — 콘솔과 웹 동시 제어)
  1) 콘솔과 웹은 **동시에** 제어할 수 있다. 서로 막지 않는다 (나중 명령이 이긴다).
     "락" 은 이제 **최근 조작자 표시**다 — 누가 방금 만졌는지 화면에 보여 준다.
  2) 콘솔이 바꾸면 장비가 올려 보내는 0x44 로 웹 화면이 따라가고,
     웹이 바꾸면 같은 0x44 가 콘솔(0xC3)로 내려가 콘솔 화면이 따라간다.
  3) 측정 run 진행 중(running)에는 AOS 파라미터 변경을 거부. pause/abort 는 허용

웹 명령은 콘솔과 **같은 변환 코드**(udp_server.ConsoleProtocol.handle_*_cmd)를 탄다.
가상 세션(virtual=True)을 넘겨 UDP 로 응답을 보내지 않고 ACK/NAK 만 받아 온다.
"""
from __future__ import annotations

import struct
import time
from dataclasses import dataclass

import udp_server as S

from . import config, live
from .db import store, _ts

AOS, GFC = S.DEV_AOS, S.DEV_GFC
TYPE_NAME = {AOS: "AOS", GFC: "GFC"}

# AOS 파라미터 (GTS_UDP_Protocol.md 4-5)
AOS_PARAM = {  # name: (param_id, min, max)
    "hv": (0, 0.0, 200.0), "frq": (1, 200.0, 800.0), "duty": (2, 20.0, 80.0),
    "cv": (3, -5.0, 5.0), "lf_frq": (4, 50.0, 200.0), "lf_volt": (5, 0.0, 5.0),
}

class ControlError(Exception):
    def __init__(self, code: str, msg: str, http=409):
        super().__init__(msg)
        self.code, self.msg, self.http = code, msg, http


@dataclass
class Lock:
    owner_kind: str        # 'console' | 'web'
    owner_id: str
    owner_label: str
    acquired_at: float
    heartbeat_at: float
    expires_at: float
    preempted_by: str | None = None
    preempted_at: float | None = None

    def alive(self):
        return time.time() < self.expires_at

    def as_dict(self):
        return dict(owner_kind=self.owner_kind, owner_id=self.owner_id,
                    owner_label=self.owner_label, acquired_at=self.acquired_at,
                    expires_at=self.expires_at, remain_s=max(0.0, round(self.expires_at - time.time(), 1)))


LOCKS: dict[tuple, Lock] = {}
_last_console_ev: dict[tuple, float] = {}
# 선점당한 웹 세션에게 알려 줄 기록: (dev_type, did) -> dict
PREEMPTED: dict[tuple, dict] = {}


def _mirror(key, lk: Lock | None):
    dt, did = key
    if lk is None:
        store.enqueue("DELETE FROM device_lock WHERE dev_type=$1 AND device_id=$2", dt, did)
        return
    store.enqueue("""
        INSERT INTO device_lock (dev_type, device_id, owner_kind, owner_id, owner_label,
                                 acquired_at, heartbeat_at, expires_at, preempted_by, preempted_at)
        SELECT $1,$2,$3::ctl_source,$4,$5,$6,$7,$8,$9,$10
         WHERE EXISTS (SELECT 1 FROM device WHERE dev_type=$1 AND device_id=$2)
        ON CONFLICT (dev_type, device_id) DO UPDATE SET
          owner_kind=EXCLUDED.owner_kind, owner_id=EXCLUDED.owner_id,
          owner_label=EXCLUDED.owner_label, acquired_at=EXCLUDED.acquired_at,
          heartbeat_at=EXCLUDED.heartbeat_at, expires_at=EXCLUDED.expires_at,
          preempted_by=EXCLUDED.preempted_by, preempted_at=EXCLUDED.preempted_at""",
        dt, did, lk.owner_kind, lk.owner_id, lk.owner_label, _ts(lk.acquired_at),
        _ts(lk.heartbeat_at), _ts(lk.expires_at), lk.preempted_by,
        _ts(lk.preempted_at) if lk.preempted_at else None)


def get_lock(dev_type, did):
    lk = LOCKS.get((dev_type, did))
    if lk and not lk.alive():
        del LOCKS[(dev_type, did)]
        return None
    return lk


def lock_state(dev_type, did, owner_id=None):
    lk = get_lock(dev_type, did)
    d = dict(locked=lk is not None, lock=lk.as_dict() if lk else None, mine=False)
    if lk and owner_id and lk.owner_kind == "web" and lk.owner_id == owner_id:
        d["mine"] = True
    p = PREEMPTED.get((dev_type, did))
    if p and owner_id and p["victim"] == owner_id:
        d["preempted"] = p
    return d


def acquire_web(dev_type, did, owner_id, label):
    """웹 조작 기록(최근 조작자) 획득/연장.  다른 조작자가 있어도 막지 않는다 (동시 제어)"""
    key = (dev_type, did)
    now = time.time()
    lk = get_lock(dev_type, did)
    if lk and not (lk.owner_kind == "web" and lk.owner_id == owner_id):
        lk = None                       # 동시 제어 허용 — 최근 조작자를 나로 바꾼다
    if lk:
        lk.heartbeat_at = now
        lk.expires_at = now + config.LOCK_LEASE_SEC
    else:
        lk = LOCKS[key] = Lock("web", owner_id, label, now, now, now + config.LOCK_LEASE_SEC)
        PREEMPTED.pop(key, None)
    _mirror(key, lk)
    return lk


def release_web(dev_type, did, owner_id):
    key = (dev_type, did)
    lk = get_lock(dev_type, did)
    if lk and lk.owner_kind == "web" and lk.owner_id == owner_id:
        del LOCKS[key]
        _mirror(key, None)
        return True
    return False


def on_console_control(sess, dev_type, did, cmd, pl):
    """udp_server hook — 콘솔 제어 명령.  최근 조작자를 콘솔로 바꾸고 감사 로그를 남긴다."""
    key = (dev_type, did)
    now = time.time()
    cid = f"console@{sess.addr[0]}:{sess.addr[1]}"
    label = f"CONSOLE {sess.my_id}"
    lk = get_lock(dev_type, did)
    if lk and lk.owner_kind == "web":
        PREEMPTED[key] = dict(victim=lk.owner_id, by=label, at=now)   # 웹 화면에 "콘솔이 바꿈" 알림
        lk = None
    if now - _last_console_ev.get(key, 0) > 10:                        # 이벤트는 10초에 한 번
        _last_console_ev[key] = now
        live.add_event("ctrl", "console_control",
                       f"{TYPE_NAME.get(dev_type)} {did:02d} · {label} 조작 "
                       f"({S.CONSOLE_CMD_NAME.get(cmd, hex(cmd))})", dev_type, did)
    if lk and lk.owner_kind == "console" and lk.owner_id == cid:
        lk.heartbeat_at, lk.expires_at = now, now + config.LOCK_LEASE_SEC
    else:
        lk = LOCKS[key] = Lock("console", cid, label, now, now, now + config.LOCK_LEASE_SEC)
    _mirror(key, lk)
    store.control_action("console", label, dev_type, did, cmd,
                         {"payload": pl.hex()} if pl else None, "ok",
                         src_type=S.DEV_CONSOLE, src_id=sess.my_id)


# ── 웹 명령 실행 ────────────────────────────────────────────────────

class WebSession:
    """udp_server 에 넘기는 가상 콘솔 세션 — 응답을 UDP 대신 results 에 모은다."""
    virtual = True

    def __init__(self, dev_type, did):
        self.addr = None
        self.my_id = 0
        self.dev_type, self.dev_id = dev_type, did
        self.results = []
        self.seq = 0
        self.last_seen = time.time()
        self.connected = True

    def next_seq(self):
        self.seq = (self.seq + 1) & 0xFFFF
        return self.seq


def _proto():
    con = S.HOLDER.get("console")
    if con is None:
        raise ControlError("not_ready", "UDP 서버가 아직 시작되지 않았습니다", 503)
    return con


def _check_run(did, allow_during_run):
    if allow_during_run:
        return
    rid = store.active_run.get(did)
    if rid:
        raise ControlError("denied_run", f"측정 run #{rid} 진행 중 — 파라미터 변경 불가 "
                                         f"(먼저 일시정지/중단)")


def execute(dev_type, did, cmd, payload: bytes, actor: str, owner_id: str,
            allow_during_run=False, detail=None):
    """웹에서 콘솔 명령 1개를 실행.  @return dict(result, ...)"""
    reg = S.GFCS if dev_type == GFC else S.AOSES
    dev = reg.get(did)
    try:
        if dev is None or not dev.online:
            raise ControlError("offline", f"{TYPE_NAME[dev_type]} {did:02d} 오프라인", 409)
        # run 잠금은 AOS 파라미터에만. GFC(가스 공급)는 측정 중에도 조작이 필요하다
        _check_run(did, allow_during_run or dev_type == GFC)
        acquire_web(dev_type, did, owner_id, actor)
        con = _proto()
        sess = WebSession(dev_type, did)
        if dev_type == GFC:
            con.handle_gfc_cmd(sess, dev, cmd, 0, payload)
        else:
            con.handle_aos_cmd(sess, dev, cmd, 0, payload)
        if not sess.results:
            raise ControlError("no_reply", "변환기가 응답하지 않았습니다", 500)
        rcmd, rpl = sess.results[0]
        if rcmd == S.C_NAK:
            code = rpl[1] if len(rpl) > 1 else 0
            raise ControlError("nak", f"거부됨 (NAK err={code})", 400)
    except ControlError as e:
        store.control_action("web", actor, dev_type, did, cmd, detail, e.code)
        raise
    store.control_action("web", actor, dev_type, did, cmd, detail, "ok")
    live.add_event("ctrl", "web_control",
                   f"{TYPE_NAME[dev_type]} {did:02d} · 웹 제어 {S.CONSOLE_CMD_NAME.get(cmd, hex(cmd))}"
                   f" {json_short(detail)} ({actor})", dev_type, did)
    return dict(result="ok", cmd=S.CONSOLE_CMD_NAME.get(cmd, hex(cmd)),
                lock=get_lock(dev_type, did).as_dict())


def json_short(d):
    if not d:
        return ""
    return ", ".join(f"{k}={v}" for k, v in d.items())


def aos_set_params(did, params: dict, actor, owner_id):
    """params: hv/frq/duty/cv/lf_frq/lf_volt (float), lf_on (bool) — 부분 지정 가능"""
    done = []
    for name, v in params.items():
        if v is None:
            continue
        if name == "lf_on":
            execute(AOS, did, S.C_AOS_SET_LF_MODE, bytes([1 if v else 0]), actor, owner_id,
                    detail={"lf_on": bool(v)})
            done.append(name)
            continue
        if name not in AOS_PARAM:
            raise ControlError("bad_param", f"알 수 없는 파라미터 {name}", 400)
        pid, lo, hi = AOS_PARAM[name]
        v = float(v)
        if not (lo <= v <= hi):
            raise ControlError("range", f"{name}={v} 범위 밖 ({lo}~{hi})", 400)
        pl = struct.pack("<B3xf", pid, v)
        execute(AOS, did, S.C_AOS_SET_PARAM, pl, actor, owner_id, detail={name: v})
        done.append(name)
    return dict(result="ok", applied=done)


def aos_query(did, actor, owner_id):
    dev = S.AOSES.get(did)
    if dev is None or not dev.online:
        raise ControlError("offline", f"AOS {did:02d} 오프라인")
    S.HOLDER["aos"].send(dev, S.A_PARAMS_QUERY, b"")
    return dict(result="ok")
