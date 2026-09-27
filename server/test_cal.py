#!/usr/bin/env python3
"""캘리브레이션 API 통합 시험 — aos_sim.py (AOS_SIM_DMM) 와 함께 돌린다.  ⚠ 실제 장비에 쓰지 말 것

    # 터미널 A (서버, DB 없이)
    GTS_BOOT_TOKEN=t GTS_DB_DISABLE=1 GTS_DMM_HOST=127.0.0.1 GTS_CAL_RAMP_DELAY=0.05 \\
        GTS_CAL_DIR=/tmp/gts_cal python3 gts_server.py
    # 터미널 B (AOS 브리지 + DMM 흉내)
    AOS_SIM_DMM=5025 python3 aos_sim.py 127.0.0.1 1 6500
    # 터미널 C
    python3 test_cal.py [host] [token] [full|partial|both]
"""
import json
import sys
import time
import urllib.error
import urllib.request

HOST = sys.argv[1] if len(sys.argv) > 1 else "localhost"
TOKEN = sys.argv[2] if len(sys.argv) > 2 else "t"
WHAT = sys.argv[3] if len(sys.argv) > 3 else "both"
BASE = f"http://{HOST}:8081/api/cal"
ok = fail = 0


def req(method, path, body=None, expect=200):
    data = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(BASE + path, data=data, method=method)
    r.add_header("Authorization", f"Bearer {TOKEN}")
    if data:
        r.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(r, timeout=120) as resp:
            code, out = resp.status, json.loads(resp.read() or b"null")
    except urllib.error.HTTPError as e:
        code, out = e.code, json.loads(e.read() or b"null")
    if code != expect:
        raise SystemExit(f"FAIL {method} {path} → {code} {out}")
    return out


def check(name, cond, info=""):
    global ok, fail
    ok, fail = (ok + 1, fail) if cond else (ok, fail + 1)
    print(f"{'PASS' if cond else 'FAIL'}  {name}  {info}")


def wait_job(kind):
    while True:
        st = req("GET", "")["session"]
        j = st["job"]
        if j["state"] == "wait_user":
            print(f"      … {j['wait']} → continue")
            req("POST", "/job/continue")
        elif j["state"] in ("done", "aborted", "failed"):
            return st
        time.sleep(0.5)


def run(mode):
    print(f"\n=== {mode} ===")
    if req("GET", "")["session"]:
        req("DELETE", "/session")
    s = req("POST", "/session", dict(device_id=1, sn="SIM-0001", adc="ad7739", mode=mode))
    check("세션 시작", s["mode"] == mode)
    req("POST", "/session", dict(device_id=1), expect=409)
    pc = req("POST", "/precheck", dict(user_checks=dict(wiring=True, cv=True, load=True)))
    check("사전 점검", pc["ok"], [c["key"] for c in pc["checks"] if not c["ok"]])
    b = req("POST", "/backup")
    check("백업", b["HV_DAC"]["no"] >= 2, {k: v["no"] for k, v in b.items()})
    if mode == "full":
        i = req("POST", "/init")
        check("선형 초기화", i["HV_DAC"]["no"] == 2 and i["HV_VS"]["x"][-1] == 65535)
        j = req("POST", "/jog", dict(target="hv", volt=29.0))
        check("조그 29 V (출력 없음 구간)", j["meas"] < 1, f"meas={j['meas']:.3f}")
        j = req("POST", "/jog", dict(target="hv", delta=+2))
        check("조그 +2 → 31 V", 29 < j["meas"] < 33, f"meas={j['meas']:.3f} raw={j.get('raw')}")
        pv = req("POST", "/range-mark", dict(target="hv", which="min", volt=30))
        pv = req("PUT", "/plan", dict(hv=dict(step=0.5), settle_ms=0, avg_n=2, sense_ms=100))
        check("101 초과 차단", pv["hv"]["error"] is not None, pv["hv"]["error"])
        req("POST", "/sweep", expect=400)
        pv = req("PUT", "/plan", dict(hv=dict(step=2), cv=dict(step=0.1)))
        check("계획 87 + 101", pv["hv"]["count"] == 87 and pv["cv"]["count"] == 101)
        req("POST", "/jog", dict(target="hv", volt=0))
    else:
        pv = req("PUT", "/plan", dict(hv=dict(start=150, end=200, step=2), settle_ms=0, avg_n=2, sense_ms=100))
        check("일부 구간 계획", pv["hv"]["count"] == 26, pv["hv"]["count"])
        req("POST", "/init", expect=409)
    t0 = time.time()
    req("POST", "/sweep")
    st = wait_job("sweep")
    check("자동 측정 완료", st["job"]["state"] == "done", f"{st['job']} {time.time() - t0:.0f}s")
    pts = req("GET", "/points?kind=sweep")["rows"]
    hv = [p for p in pts if p["target"] == "hv"]
    check("HV 측정점", hv and all(abs(p["dev"]) < 1.5 for p in hv), f"{len(hv)} 점")
    bi = req("POST", "/build")
    for n, inf in bi["info"].items():
        check(f"테이블 {n}", all(c["level"] != "err" for c in inf["checks"]),
              f"{inf['new_no']}p  " + ", ".join(f"{c['label']}={c['value']}" for c in inf["checks"]))
    sv = req("POST", "/save", dict(memo="sim"))
    check("저장·되읽기", len(sv["saved"]) >= 1, sv["saved"])
    req("POST", "/verify", dict(hv=dict(start=35, end=195, step=10), cv=dict(start=-4.5, end=4.5, step=1.5),
                                settle_ms=0, avg_n=2, sense_ms=100))
    st = wait_job("verify")
    vs = st["verify_summary"]
    check("검증 완료", st["job"]["state"] == "done", json.dumps(vs, ensure_ascii=False)[:200])
    if "hv" in vs:
        check("HV 보정 후 오차 < 0.1 V", abs(vs["hv"]["max_err"]) < 0.1, f"{vs['hv']['max_err']:+.4f}")
        check("HV 표시 오차 < 0.3 V", abs(vs["hv"]["max_disp_err"]) < 0.3, f"{vs['hv']['max_disp_err']:+.4f}")
    if "cv" in vs:
        check("CV 보정 후 오차 < 0.01 V", abs(vs["cv"]["max_err"]) < 0.01, f"{vs['cv']['max_err']:+.5f}")
    req("POST", "/judge", dict(verdict="pass", memo="시뮬레이터"))
    req("POST", "/estop")
    sid = req("GET", "")["session"]["id"]
    req("DELETE", "/session")
    h = req("GET", "/history?device_id=1")["rows"]
    check("이력 기록", any(r["id"] == sid and r["verdict"] == "pass" for r in h))


if WHAT in ("full", "both"):
    run("full")
if WHAT in ("partial", "both"):
    run("partial")
print(f"\n{ok} PASS / {fail} FAIL")
sys.exit(1 if fail else 0)
