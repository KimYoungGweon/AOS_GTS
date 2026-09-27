"""Keysight 34461A — LAN SCPI raw socket (TCP 5025)

  - 명령 끝은 '\\n'. 34461A 는 동시에 한 접속만 받는다 → 서버가 접속 하나를 계속 들고 있는다.
  - 레인지 고정 (2026-09-27 결정): HV 측정 1000 V, CV 측정 10 V  (config.DMM_RANGE_*)
  - 평균: SAMP:COUN n → READ? 가 n 개를 쉼표로 돌려준다 → 서버에서 평균

연결 확인 (iMac):  printf '*IDN?\\n' | nc -w 2 192.168.0.7 5025
"""
from __future__ import annotations

import asyncio
import statistics
import time

from . import config


class DmmError(Exception):
    pass


class Dmm:
    def __init__(self, host=None, port=None):
        self.host = host or config.DMM_HOST
        self.port = port or config.DMM_PORT
        self.reader = self.writer = None
        self.idn = None
        self.lock = asyncio.Lock()
        self._cfg = None               # (range, nplc, count) — 같으면 다시 보내지 않는다
        self.last_error = None
        self.last_read = None          # dict(v, n, sd, range, t)

    @property
    def connected(self):
        return self.writer is not None and not self.writer.is_closing()

    def status(self):
        return dict(host=self.host, port=self.port, connected=self.connected, idn=self.idn,
                    range_hv=config.DMM_RANGE_HV, range_cv=config.DMM_RANGE_CV, nplc=config.DMM_NPLC,
                    last_error=self.last_error, last_read=self.last_read)

    async def connect(self, host=None, port=None):
        async with self.lock:
            await self._close()
            if host:
                self.host = host
            if port:
                self.port = int(port)
            try:
                self.reader, self.writer = await asyncio.wait_for(
                    asyncio.open_connection(self.host, self.port), config.DMM_TIMEOUT)
                self._cfg = None
                await self._write("*CLS")
                self.idn = (await self._query("*IDN?")).strip()
                self.last_error = None
            except (OSError, asyncio.TimeoutError, DmmError) as e:
                await self._close()
                self.last_error = f"접속 실패 {self.host}:{self.port} — {e!r}"
                raise DmmError(self.last_error) from e
            return self.idn

    async def close(self):
        async with self.lock:
            await self._close()

    async def _close(self):
        if self.writer:
            try:
                self.writer.close()
            except Exception:           # noqa: BLE001
                pass
        self.reader = self.writer = None

    async def _write(self, cmd):
        if not self.connected:
            raise DmmError("DMM 미연결")
        self.writer.write((cmd + "\n").encode())
        await self.writer.drain()

    async def _query(self, cmd, timeout=None):
        await self._write(cmd)
        try:
            line = await asyncio.wait_for(self.reader.readline(), timeout or config.DMM_TIMEOUT)
        except asyncio.TimeoutError as e:
            await self._close()          # 응답이 섞이지 않게 끊는다. 다음 호출에서 재접속
            raise DmmError(f"DMM 무응답 ({cmd})") from e
        if not line:
            await self._close()
            raise DmmError("DMM 접속 끊김")
        return line.decode(errors="replace")

    async def _ensure(self):
        if not self.connected:
            self.reader = self.writer = None
            try:
                self.reader, self.writer = await asyncio.wait_for(
                    asyncio.open_connection(self.host, self.port), config.DMM_TIMEOUT)
                self._cfg = None
                self.idn = (await self._query("*IDN?")).strip()
            except (OSError, asyncio.TimeoutError) as e:
                self.last_error = f"접속 실패 {self.host}:{self.port} — {e!r}"
                raise DmmError(self.last_error) from e

    async def read(self, target: str, n: int = 5):
        """target 'hv' → 1000 V 레인지, 'cv' → 10 V 레인지.  n 회 평균.
        @return dict(v=평균, n, sd, min, max, range)"""
        rng = config.DMM_RANGE_HV if target == "hv" else config.DMM_RANGE_CV
        n = max(1, min(int(n), 100))
        async with self.lock:
            try:
                await self._ensure()
                cfg = (rng, config.DMM_NPLC, n)
                if cfg != self._cfg:
                    await self._write(f"CONF:VOLT:DC {rng:g}")
                    await self._write(f"VOLT:DC:NPLC {config.DMM_NPLC:g}")
                    await self._write("VOLT:DC:ZERO:AUTO ON")
                    await self._write("TRIG:SOUR IMM")
                    await self._write("TRIG:COUN 1")
                    await self._write(f"SAMP:COUN {n}")
                    self._cfg = cfg
                # 1 PLC = 20 ms (50 Hz 기준 여유), auto-zero 로 ×2
                tmo = config.DMM_TIMEOUT + n * config.DMM_NPLC * 0.02 * 2.5
                vals = [float(x) for x in (await self._query("READ?", tmo)).strip().split(",") if x]
            except (DmmError, ValueError) as e:
                self.last_error = str(e)
                raise DmmError(str(e)) from e
        if not vals:
            raise DmmError("DMM 응답 비어 있음")
        if any(abs(v) > 9e37 for v in vals):           # 34461A overload = ±9.9E37
            raise DmmError(f"DMM 과입력 (레인지 {rng:g} V)")
        r = dict(v=statistics.fmean(vals), n=len(vals),
                 sd=statistics.pstdev(vals) if len(vals) > 1 else 0.0,
                 min=min(vals), max=max(vals), range=rng, t=time.time())
        self.last_read = r
        return r


dmm = Dmm()
