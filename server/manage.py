#!/usr/bin/env python3
"""사용자·토큰 관리 (서버에서 실행, .env 의 GTS_DB_DSN 사용)

    python3 manage.py create-admin vdskim            # 사용자 + admin 토큰 발급
    python3 manage.py token vdskim read  --label 집PC  --days 365
    python3 manage.py token lab1   control           # 없는 사용자면 자동 생성
    python3 manage.py list
    python3 manage.py revoke 3
토큰 원문은 발급 시 한 번만 출력된다 (DB 에는 sha256 만).
"""
import argparse
import hashlib
import secrets
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gts import config          # noqa: E402


def conn():
    import psycopg
    if not config.DB_DSN:
        sys.exit("GTS_DB_DSN 이 없습니다 (.env)")
    return psycopg.connect(config.DB_DSN, autocommit=True)


def issue(c, login, scope, label=None, days=None):
    uid = c.execute("""INSERT INTO app_user (login, display) VALUES (%s,%s)
                       ON CONFLICT (login) DO UPDATE SET login=EXCLUDED.login RETURNING user_id""",
                    (login, login)).fetchone()[0]
    raw = "gts_" + secrets.token_urlsafe(32)
    tid = c.execute("""INSERT INTO api_token (user_id, token_hash, token_prefix, scope, label, expires_at)
                       VALUES (%s,%s,%s,%s,%s, CASE WHEN %s::int IS NULL THEN NULL
                                                    ELSE now() + make_interval(days => %s::int) END)
                       RETURNING token_id""",
                    (uid, hashlib.sha256(raw.encode()).digest(), raw[:8], scope, label, days, days)
                    ).fetchone()[0]
    print(f"토큰 #{tid}  사용자={login}  권한={scope}")
    print(f"  {raw}")
    print("  ↑ 지금만 보입니다. 안전한 곳에 보관하세요.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("create-admin"); a.add_argument("login")
    t = sub.add_parser("token"); t.add_argument("login")
    t.add_argument("scope", choices=["read", "control", "admin"])
    t.add_argument("--label"); t.add_argument("--days", type=int)
    sub.add_parser("list")
    r = sub.add_parser("revoke"); r.add_argument("token_id", type=int)
    args = ap.parse_args()

    with conn() as c:
        if args.cmd == "create-admin":
            issue(c, args.login, "admin", "admin")
        elif args.cmd == "token":
            issue(c, args.login, args.scope, args.label, args.days)
        elif args.cmd == "list":
            for row in c.execute("""SELECT t.token_id, u.login, t.scope, t.token_prefix, t.label,
                                           t.created_at::date, t.expires_at::date, t.revoked_at IS NOT NULL,
                                           t.last_used
                                      FROM api_token t JOIN app_user u USING (user_id)
                                     ORDER BY t.token_id"""):
                tid, login, scope, pre, label, cr, ex, rev, used = row
                print(f"#{tid:<3} {login:<12} {scope:<8} {pre}…  {label or '':<12} 발급 {cr} "
                      f"만료 {ex or '-'}  {'폐기됨' if rev else ''}  마지막사용 {used or '-'}")
        elif args.cmd == "revoke":
            c.execute("UPDATE api_token SET revoked_at=now() WHERE token_id=%s", (args.token_id,))
            print(f"토큰 #{args.token_id} 폐기 (서버 캐시는 최대 60초 후 반영)")


if __name__ == "__main__":
    main()
