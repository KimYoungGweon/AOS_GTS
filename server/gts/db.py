"""PostgreSQL 연결 + 비블로킹 writer (인수인계 6-4, 설계안 6절)

  UDP recv ─► hook ─► Store.enqueue()/gfc_buf ─► writer task ─► PostgreSQL
                  └ 수신 경로는 put_nowait 까지만. 절대 await 하지 않는다.

  GFC 1초   : 메모리 버퍼 → GFC_FLUSH_SEC(5초)마다 COPY
  장치      : last_seen 등은 dirty 표시 → 10초마다 UPSERT
  기타      : sys_event / control_action / device_lock 거울 → 큐 → 순서대로 실행
  큐 포화   : GFC 텔레메트리를 먼저 버린다. (heatmap 은 큐를 쓰지 않고 직접 await)
  DB 다운   : 서버(UDP 중계)는 계속 돈다. 10초마다 재접속 시도
"""
from __future__ import annotations

import asyncio
import json
import time
from collections import deque
from datetime import datetime, timezone

try:
    import asyncpg
except ImportError:                      # 개발 PC 등 — DB 없이도 돌아가게
    asyncpg = None

from . import config


def log(msg):
    print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]}] [DB] {msg}", flush=True)


def _ts(t: float):
    return datetime.fromtimestamp(t, tz=timezone.utc)


GFC_COLS = ["run_id", "device_id", "ts", "dev_ts", "volt1", "volt2", "ctrl", "slope", "sv",
            "src_remain", "src_elapsed", "uptime_s", "raw1", "raw2", "co2_1", "co2_2",
            "pump1", "pump2", "pump3", "flags", "err", "rssi"]


class Store:
    def __init__(self):
        self.pool = None
        self.q: asyncio.Queue | None = None
        self.gfc_buf: deque = deque(maxlen=config.QUEUE_MAX)
        self.dirty_dev: dict = {}              # (dev_type, did) -> dict
        self.active_run: dict[int, int] = {}   # AOS/쌍 id -> running run_id
        self.dropped = 0
        self.gfc_dropped = 0
        self.written = 0
        self._tasks = []
        self.last_error = None

    # ── 상태 ──────────────────────────────────────────────────────
    @property
    def ok(self):
        return self.pool is not None

    def status(self):
        return dict(enabled=config.DB_ENABLED, connected=self.ok,
                    queue=self.q.qsize() if self.q else 0, gfc_buffer=len(self.gfc_buf),
                    dropped=self.dropped, gfc_dropped=self.gfc_dropped,
                    written=self.written, last_error=self.last_error)

    # ── 수명 ──────────────────────────────────────────────────────
    async def start(self):
        self.q = asyncio.Queue(maxsize=config.QUEUE_MAX)
        if not config.DB_ENABLED:
            log("GTS_DB_DSN 없음 — DB 없이 실행 (실시간 기능만)")
            return
        if asyncpg is None:
            log("asyncpg 미설치 — DB 없이 실행")
            return
        await self._connect()
        for coro in (self._writer(), self._gfc_flusher(), self._dev_flusher(),
                     self._housekeeping(), self._reconnector()):
            self._tasks.append(asyncio.create_task(coro))

    async def stop(self):
        for t in self._tasks:
            t.cancel()
        if self.pool:
            try:
                await self._flush_gfc()
                await self._flush_devices()
            except Exception:              # noqa: BLE001
                pass
            await self.pool.close()

    async def _connect(self):
        try:
            self.pool = await asyncpg.create_pool(config.DB_DSN, min_size=1, max_size=8,
                                                  command_timeout=60)
            async with self.pool.acquire() as c:
                v = await c.fetchval("SELECT value FROM schema_meta WHERE key='version'")
                rows = await c.fetch("SELECT run_id, device_id FROM run WHERE status='running'")
            self.active_run = {r["device_id"]: r["run_id"] for r in rows}
            log(f"연결됨 (schema v{v}), running run {len(self.active_run)}건")
            self.last_error = None
        except Exception as e:             # noqa: BLE001
            self.pool = None
            self.last_error = repr(e)
            log(f"연결 실패: {e!r}")

    async def _reconnector(self):
        while True:
            await asyncio.sleep(10)
            if self.pool is None:
                await self._connect()

    # ── 비블로킹 입력 (hook 에서 호출) ─────────────────────────────
    def enqueue(self, sql: str, *args):
        if self.q is None or not config.DB_ENABLED:
            return
        try:
            self.q.put_nowait((sql, args))
        except asyncio.QueueFull:
            self.dropped += 1

    def gfc_record(self, did, s):
        if not config.DB_ENABLED:
            return
        if len(self.gfc_buf) == self.gfc_buf.maxlen:
            self.gfc_dropped += 1                     # deque 가 가장 오래된 것을 버린다
        run_id = self.active_run.get(did, 0)
        self.gfc_buf.append((run_id, did, _ts(time.time()), s["ts"], s["volt1"], s["volt2"],
                             s["ctrl"], s["slope"], s["sv"], s["src_remain"], s["src_elapsed"],
                             s["uptime"], s["raw1"], s["raw2"], s["co2_1"], s["co2_2"],
                             s["pump1"], s["pump2"], s["pump3"], s["flags"], s["err"], s["rssi"]))

    def touch_device(self, dev_type, did, **kw):
        d = self.dirty_dev.setdefault((dev_type, did), {})
        d.update(kw)

    def event(self, e, detail=None):
        self.enqueue("""INSERT INTO sys_event (at, level, dev_type, dev_id, kind, text, detail)
                        VALUES ($1,$2,$3,$4,$5,$6,$7)""",
                     _ts(e["at"]), e["level"], e["dev_type"], e["dev_id"], e["kind"], e["text"],
                     json.dumps(detail) if detail is not None else None)

    def control_action(self, source, actor, dev_type, dev_id, cmd, detail, result,
                       src_type=None, src_id=None):
        self.enqueue("""INSERT INTO control_action (source, actor, src_type, src_id, dev_type,
                                                    dev_id, cmd, detail, result)
                        VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9)""",
                     source, actor, src_type, src_id, dev_type, dev_id, cmd,
                     json.dumps(detail) if detail is not None else None, result)

    # ── 백그라운드 태스크 ─────────────────────────────────────────
    async def _writer(self):
        while True:
            sql, args = await self.q.get()
            if self.pool is None:
                await asyncio.sleep(1)
                try:
                    self.q.put_nowait((sql, args))       # 재시도
                except asyncio.QueueFull:
                    self.dropped += 1
                continue
            try:
                async with self.pool.acquire() as c:
                    await c.execute(sql, *args)
                self.written += 1
            except (OSError, asyncpg.exceptions.ConnectionDoesNotExistError,
                    asyncpg.exceptions.InterfaceError) as e:
                self.last_error = repr(e)
                self.pool = None
            except Exception as e:             # noqa: BLE001  SQL 오류는 그 건만 버린다
                self.last_error = repr(e)
                log(f"쓰기 실패 (버림): {e!r}  sql={sql.split()[0:4]}")

    async def _flush_gfc(self):
        if not self.gfc_buf or self.pool is None:
            return
        recs = list(self.gfc_buf)
        self.gfc_buf.clear()
        try:
            async with self.pool.acquire() as c:
                await c.copy_records_to_table("tele_gfc", records=recs, columns=GFC_COLS)
            self.written += len(recs)
        except Exception as e:                 # noqa: BLE001
            self.last_error = repr(e)
            self.gfc_dropped += len(recs)
            log(f"GFC COPY 실패 {len(recs)}행 버림: {e!r}")

    async def _gfc_flusher(self):
        while True:
            await asyncio.sleep(config.GFC_FLUSH_SEC)
            await self._flush_gfc()

    async def _flush_devices(self):
        if not self.dirty_dev or self.pool is None:
            return
        items = list(self.dirty_dev.items())
        self.dirty_dev.clear()
        async with self.pool.acquire() as c:
            for (dt, did), d in items:
                name = ("AOS" if dt == 1 else "GFC") + f"-{did:02d}"
                await c.execute("""
                    INSERT INTO device (dev_type, device_id, name, last_seen, last_peer,
                                        model, fw, mac, claimed_ip)
                    VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9)
                    ON CONFLICT (dev_type, device_id) DO UPDATE SET
                      last_seen  = COALESCE(EXCLUDED.last_seen, device.last_seen),
                      last_peer  = COALESCE(EXCLUDED.last_peer, device.last_peer),
                      model      = COALESCE(EXCLUDED.model, device.model),
                      fw         = COALESCE(EXCLUDED.fw, device.fw),
                      mac        = COALESCE(EXCLUDED.mac, device.mac),
                      claimed_ip = COALESCE(EXCLUDED.claimed_ip, device.claimed_ip)""",
                    dt, did, name, _ts(d["last_seen"]) if d.get("last_seen") else None,
                    d.get("peer"), d.get("model"), d.get("fw"), d.get("mac"), d.get("claimed_ip"))

    async def _dev_flusher(self):
        while True:
            await asyncio.sleep(10)
            try:
                await self._flush_devices()
            except Exception as e:             # noqa: BLE001
                self.last_error = repr(e)

    async def _housekeeping(self):
        while True:
            await asyncio.sleep(config.HOUSEKEEP_SEC)
            if self.pool is None:
                continue
            try:
                async with self.pool.acquire() as c:
                    n = await c.execute(
                        "DELETE FROM tele_gfc_unassigned WHERE ts < now() - make_interval(hours => $1)",
                        config.GFC_UNASSIGNED_KEEP_H)
                    await c.execute("DELETE FROM device_lock WHERE expires_at < now()")
                log(f"housekeeping: tele_gfc_unassigned {n}")
            except Exception as e:             # noqa: BLE001
                self.last_error = repr(e)


store = Store()
