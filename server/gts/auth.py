"""API 토큰 인증 (D15, D15b) — read < control < admin

토큰 전달 방법 (어느 것이든)
  Authorization: Bearer gts_xxxxx
  ?token=gts_xxxxx            (WebSocket, CSV 다운로드 링크용)
  쿠키 gts_token

토큰 원문은 DB 에 저장하지 않는다 (sha256 만).  발급: manage.py 또는 /api/admin/tokens
비상용: 환경변수 GTS_BOOT_TOKEN 이 있으면 그 값은 admin 으로 통과 (DB 장애 시 복구용)
"""
from __future__ import annotations

import hashlib
import os
import secrets
import time

from fastapi import Depends, HTTPException, Request, WebSocket
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer

from .db import store

LEVEL = {"read": 1, "control": 2, "admin": 3}
_cache: dict[bytes, tuple] = {}          # hash -> (expires, principal)
CACHE_SEC = 60
BOOT = os.environ.get("GTS_BOOT_TOKEN", "")


def new_token() -> str:
    return "gts_" + secrets.token_urlsafe(32)


def hash_token(t: str) -> bytes:
    return hashlib.sha256(t.encode()).digest()


class Principal(dict):
    @property
    def scope(self):
        return self["scope"]

    @property
    def actor(self):
        return self["login"]

    @property
    def owner_id(self):
        return f"web:{self['login']}:{self['token_id']}"


async def resolve(token: str | None) -> Principal | None:
    if not token:
        return None
    if BOOT and secrets.compare_digest(token, BOOT):
        return Principal(login="boot", scope="admin", token_id=0)
    h = hash_token(token)
    hit = _cache.get(h)
    now = time.time()
    if hit and hit[0] > now:
        return hit[1]
    if not store.ok:
        return None
    async with store.pool.acquire() as c:
        r = await c.fetchrow("""
            SELECT t.token_id, t.scope, u.login, u.display
              FROM api_token t JOIN app_user u USING (user_id)
             WHERE t.token_hash=$1 AND t.revoked_at IS NULL AND u.is_active
               AND (t.expires_at IS NULL OR t.expires_at > now())""", h)
        if r is None:
            _cache.pop(h, None)
            return None
        await c.execute("UPDATE api_token SET last_used=now() WHERE token_id=$1", r["token_id"])
    p = Principal(login=r["login"], display=r["display"], scope=r["scope"], token_id=r["token_id"])
    _cache[h] = (now + CACHE_SEC, p)
    return p


def _extract(headers, query, cookies):
    a = headers.get("authorization", "")
    if a.lower().startswith("bearer "):
        return a[7:].strip()
    return query.get("token") or cookies.get("gts_token")


# /docs 의 Authorize 버튼용 — 헤더가 없어도 여기서 막지 않고 아래에서 ?token= / 쿠키도 본다
_bearer = HTTPBearer(auto_error=False, scheme_name="GTS 토큰",
                     description="manage.py 로 발급한 gts_… 토큰을 그대로 붙여 넣기 (Bearer 없이)")


def need(level: str):
    async def dep(request: Request,
                  _cred: HTTPAuthorizationCredentials | None = Depends(_bearer)) -> Principal:
        p = await resolve(_extract(request.headers, request.query_params, request.cookies))
        if p is None:
            raise HTTPException(401, "토큰이 없거나 유효하지 않습니다")
        if LEVEL[p.scope] < LEVEL[level]:
            raise HTTPException(403, f"'{level}' 권한이 필요합니다 (현재 {p.scope})")
        return p
    return dep


async def ws_principal(ws: WebSocket) -> Principal | None:
    return await resolve(_extract(ws.headers, ws.query_params, ws.cookies))


def invalidate():
    _cache.clear()
