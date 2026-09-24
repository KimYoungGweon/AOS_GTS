#!/usr/bin/env python3
"""GTS 통합 서버 — UDP 중계 + DB 저장 + 웹 API  (한 프로세스, asyncio 한 루프)  W1 결정

    python3 gts_server.py              # .env 의 설정으로 실행
    systemd: deploy/gts-server.service

포트
    UDP 5500 AOS · 5501 GFC · 5502 Console   (udp_server.py 와 동일, 변환 코드도 그대로 사용)
    TCP 8081 HTTP  — REST /api/*, WebSocket /ws/live, 문서 /docs
    PostgreSQL 5432 는 localhost 전용 (D15)

udp_server.py 만 단독으로 돌려도 예전처럼 동작한다 (DB/API 없이).
두 개를 동시에 띄우면 UDP 포트가 겹치므로 gts-udp 서비스는 끄고 이것만 쓸 것.
"""
import asyncio
import signal
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import uvicorn                                        # noqa: E402

import udp_server as S                                # noqa: E402
from gts import api, config, live, wire               # noqa: E402
from gts.db import store                              # noqa: E402


async def main():
    print("")
    print("========================================")
    print("   GTS SERVER  (UDP + DB + Web API)")
    print("========================================")
    print(f"UDP {S.PORT_AOS} AOS · {S.PORT_GFC} GFC · {S.PORT_CONSOLE} Console")
    print(f"HTTP {config.HTTP_HOST}:{config.HTTP_PORT}  (/docs)")
    print(f"DB  {'on' if config.DB_ENABLED else 'off'}")
    print("========================================", flush=True)

    wire.install()
    await store.start()

    loop = asyncio.get_running_loop()
    holder = {}
    t_aos, aos_proto = await loop.create_datagram_endpoint(
        S.AosProtocol, local_addr=("0.0.0.0", S.PORT_AOS))
    holder["aos"] = aos_proto
    t_gfc, gfc_proto = await loop.create_datagram_endpoint(
        S.GfcProtocol, local_addr=("0.0.0.0", S.PORT_GFC))
    holder["gfc"] = gfc_proto
    t_con, con_proto = await loop.create_datagram_endpoint(
        lambda: S.ConsoleProtocol(holder), local_addr=("0.0.0.0", S.PORT_CONSOLE))
    S.HOLDER["console"] = con_proto
    S.HOLDER["aos"] = aos_proto
    S.HOLDER["gfc"] = gfc_proto

    live.add_event("info", "server_start", "GTS 서버 시작")

    tasks = [asyncio.create_task(S.push_loop(con_proto)),
             asyncio.create_task(S.retry_loop()),
             asyncio.create_task(api.background_loop())]

    api.mount_web()
    server = uvicorn.Server(uvicorn.Config(api.app, host=config.HTTP_HOST, port=config.HTTP_PORT,
                                           log_level="warning", access_log=False, loop="none"))
    server.install_signal_handlers = lambda: None      # 신호는 아래에서 직접 처리
    stop = asyncio.Event()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, stop.set)
        except NotImplementedError:
            pass

    srv_task = asyncio.create_task(server.serve())
    await stop.wait()
    print("\n[SERVER] 종료 중…", flush=True)
    server.should_exit = True
    await srv_task
    for t in tasks:
        t.cancel()
    for t in (t_aos, t_gfc, t_con):
        t.close()
    await store.stop()
    print("[SERVER] stopped", flush=True)


if __name__ == "__main__":
    asyncio.run(main())
