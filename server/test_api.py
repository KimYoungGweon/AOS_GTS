#!/usr/bin/env python3
"""API 스모크 테스트 (조회 전용 — 장비·DB 를 바꾸지 않는다)

    python3 test_api.py                       # localhost:8081, 토큰은 GTS_TOKEN 환경변수
    python3 test_api.py 192.168.0.6 gts_xxx   # 원격
"""
import json
import os
import sys
import urllib.error
import urllib.request

HOST = sys.argv[1] if len(sys.argv) > 1 else "localhost"
TOKEN = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("GTS_TOKEN", "")
BASE = f"http://{HOST}:{os.environ.get('GTS_HTTP_PORT', '8081')}"
ok = fail = 0


def get(path, auth=True):
    req = urllib.request.Request(BASE + path)
    if auth:
        req.add_header("Authorization", f"Bearer {TOKEN}")
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            body = r.read()
            return r.status, (json.loads(body) if r.headers.get_content_type() == "application/json" else body)
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode(errors="replace")


def check(name, cond, info=""):
    global ok, fail
    if cond:
        ok += 1
        print(f"PASS  {name}  {info}")
    else:
        fail += 1
        print(f"FAIL  {name}  {info}")


s, d = get("/api/health", auth=False)
check("health", s == 200 and d.get("ok"), d)
s, _ = get("/api/overview", auth=False)
check("토큰 없으면 401", s == 401)
s, d = get("/api/me")
check("토큰 확인", s == 200, d)
s, d = get("/api/overview")
check("overview 20쌍", s == 200 and len(d["pairs"]) == 20,
      f"counts={d.get('counts') if isinstance(d, dict) else d}")
if s == 200:
    online = [p["id"] for p in d["pairs"] if p["aos"]["online"] or p["gfc"]["online"]]
    print(f"      온라인 쌍: {online}  pkt/s={d['server']['pkt_s']}  DB={d['server']['db']['connected']}")
    for i in online[:1]:
        s2, c = get(f"/api/aos/{i}/current?sec=30")
        check(f"AOS {i} current", s2 == 200, f"{len(c['points'])} pts")
s, d = get("/api/db/stats")
check("db stats", s == 200, d if s != 200 else f"{d['db_bytes'] / 1e6:.1f} MB, heatmap {d['heatmaps']}")
s, d = get("/api/runs?kind=gas&limit=5")
check("run 목록", s == 200, f"total={d.get('total') if isinstance(d, dict) else d}")
if s == 200 and d["rows"]:
    g = next((r for r in d["rows"] if r["air_ref_run_id"] and r["received_heatmaps"]), None)
    if g:
        rid, air = g["run_id"], g["air_ref_run_id"]
        s, o = get(f"/api/runs/{rid}/overview?air_run_id={air}")
        check("overview(Idf)", s == 200, f"run {rid} vs air {air}: {o.get('val_min')}~{o.get('val_max')}"
              if s == 200 else o)
        q = "" if g["mode"] == "fast" else "&sy=0&sx=0"
        s, h = get(f"/api/runs/{rid}/heatmaps?value=idf&air_run_id={air}{q}")
        check("heatmaps(Idf)", s == 200 and h["count"] > 0, f"{h.get('count')}장" if s == 200 else h)
        s, h = get(f"/api/runs/{rid}/heatmaps?value=idf{q}")
        check("air_run_id 없으면 400 (D13b)", s == 400)
print(f"\n{'✅' if not fail else '❌'}  PASS {ok} / FAIL {fail}")
sys.exit(1 if fail else 0)
