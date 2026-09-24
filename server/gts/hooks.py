"""udp_server.py → DB/API 로 이벤트를 흘려보내는 얇은 통로

udp_server.py 는 이 모듈만 안다 (numpy·DB·FastAPI 의존 없음).
핸들러가 등록되지 않으면 전부 no-op 이므로 단독 실행(python3 udp_server.py)과
test_packets.py 는 예전과 똑같이 동작한다.

규칙: 핸들러는 **절대 블로킹하지 않는다.** 메모리 갱신 + 큐 put_nowait 까지만.
      예외는 여기서 삼킨다 — UDP 수신 경로가 죽으면 안 된다.
"""
import traceback

_handlers = {}


def register(name, fn):
    _handlers.setdefault(name, []).append(fn)


def clear():
    _handlers.clear()


def emit(name, *args):
    for fn in _handlers.get(name, ()):
        try:
            fn(*args)
        except Exception:                      # noqa: BLE001
            print(f"[HOOK] {name} 처리 중 예외 (무시)\n{traceback.format_exc()}", flush=True)
