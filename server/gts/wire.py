"""udp_server hook → live(메모리) / store(DB 큐) 연결"""
import time

import udp_server as S

from . import control, hooks, live
from .db import store


def _peer(addr):
    return addr[0] if addr else None


def on_rx(kind, dev, addr, first):
    live.rx_count[kind] += 1
    dt = S.DEV_AOS if kind == "aos" else S.DEV_GFC
    store.touch_device(dt, dev.did, last_seen=dev.last_seen, peer=_peer(addr))
    if first:
        live.add_event("info", "new_device", f"{kind.upper()} {dev.did:02d} · 신규 접속 "
                                             f"({addr[0]}:{addr[1]})", dt, dev.did)


def on_bad(kind, addr, data):
    live.bad_count[kind] += 1
    store.enqueue("INSERT INTO ingest_error (src, peer, reason, raw) VALUES ($1,$2,$3,$4)",
                  kind, addr[0] if addr else None, "frame", bytes(data[:256]))


def on_hello(kind, dev, h, addr):
    dt = S.DEV_AOS if kind == "aos" else S.DEV_GFC
    store.touch_device(dt, dev.did, model=h["model"], fw=h["fw"], mac=h["mac"],
                       claimed_ip=h["ip"])


def on_gfc_sensor(dev, s):
    live.push_gfc(dev.did, s)
    store.gfc_record(dev.did, s)


def on_aos_status(dev):
    live.push_aos(dev.did, dev.status)


def on_aos_params(dev):
    pass        # 최신값은 dev.params 에 있다. API 가 직접 읽는다


def on_console_rx(sess, f):
    live.rx_count["console"] += 1
    key = sess.addr
    info = live.console_info.get(key)
    if info is None:
        info = live.console_info[key] = dict(first=time.time(), rx=0, rx_hist=[])
        live.add_event("info", "console_join",
                       f"CONSOLE {sess.my_id} · 접속 ({sess.addr[0]})", S.DEV_CONSOLE, sess.my_id)
    info["rx"] += 1


def on_tx_fail(kind, did, cmd, seq):
    dt = S.DEV_AOS if kind == "aos" else S.DEV_GFC
    live.add_event("error", "tx_fail", f"{kind.upper()} {did:02d} · 명령 0x{cmd:02X} 전달 실패 "
                                       f"(ACK 없음, {S.RETRY_MAX + 1}회 송신)", dt, did)


def on_dev_event(kind, dev, code, a):
    if kind == "aos" and code == 0x22:
        live.add_event("error", "apply_fail", f"AOS {dev.did:02d} · STM32 적용 확인 실패 "
                                              f"(재전송 후에도 값 불일치)", S.DEV_AOS, dev.did)


def install():
    hooks.clear()
    hooks.register("rx", on_rx)
    hooks.register("bad", on_bad)
    hooks.register("hello", on_hello)
    hooks.register("gfc_sensor", on_gfc_sensor)
    hooks.register("aos_status", on_aos_status)
    hooks.register("aos_params", on_aos_params)
    hooks.register("console_rx", on_console_rx)
    hooks.register("console_control", control.on_console_control)
    hooks.register("tx_fail", on_tx_fail)
    hooks.register("dev_event", on_dev_event)
    from . import cal
    cal.install_hooks()
    live.on_event = store.event
