"""설정 — 환경변수 또는 서버 디렉터리의 .env 파일 (KEY=VALUE)

.env 예 (/home/gts/gts_udp_server/.env, chmod 600)
    GTS_DB_DSN=postgresql://gts:xxxx@localhost:5432/gts
    GTS_HTTP_PORT=8081
"""
import os
from pathlib import Path

_ENV_FILE = Path(os.environ.get("GTS_ENV_FILE",
                                Path(__file__).resolve().parent.parent / ".env"))


def _load_env_file():
    if not _ENV_FILE.is_file():
        return
    for line in _ENV_FILE.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        os.environ.setdefault(k.strip(), v.strip().strip('"').strip("'"))


_load_env_file()


def _f(name, default):
    return float(os.environ.get(name, default))


def _i(name, default):
    return int(os.environ.get(name, default))


# ── DB ──────────────────────────────────────────────────────────────
DB_DSN = os.environ.get("GTS_DB_DSN", "")
DB_ENABLED = bool(DB_DSN) and os.environ.get("GTS_DB_DISABLE", "0") != "1"

# ── HTTP (웹/API) ───────────────────────────────────────────────────
HTTP_HOST = os.environ.get("GTS_HTTP_HOST", "0.0.0.0")
HTTP_PORT = _i("GTS_HTTP_PORT", 8081)
# 웹 프런트를 다른 origin 에서 띄울 때만 필요. 쉼표 구분. 비우면 CORS 없음
CORS_ORIGINS = [o for o in os.environ.get("GTS_CORS_ORIGINS", "").split(",") if o]
# 정적 웹 파일 위치 (있으면 / 로 서빙). 다음 단계 웹 프런트용
WEB_DIR = os.environ.get("GTS_WEB_DIR",
                         str(Path(__file__).resolve().parent.parent / "web"))

# ── 실시간 버퍼 (계층 C) ─────────────────────────────────────────────
AOS_RING = _i("GTS_AOS_RING", 600)      # AOS Current 1초 × 600 = 10분 (D11)
GFC_RING = _i("GTS_GFC_RING", 600)      # GFC 농도 그래프 "최근 10분"
EVENT_RING = _i("GTS_EVENT_RING", 200)

# ── 단위 변환 ───────────────────────────────────────────────────────
# AOS 0x45 STATUS 의 ADC(u16) → 화면 표시 V.   ⚠ 실제 ADC 분해능/기준전압 확인 필요
AOS_ADC_TO_V = _f("GTS_AOS_ADC_TO_V", 3.3 / 4095.0)
AOS_ADC_OFFSET_V = _f("GTS_AOS_ADC_OFFSET_V", 0.0)
# 평균전류 표시 채널: gas_p / gas_n / air_p / air_n
AOS_CURRENT_CH = os.environ.get("GTS_AOS_CURRENT_CH", "gas_p")

# heatmap payload_fmt=0 (u16 ADC raw) → V 보정식.  calib_ver 별로 둔다 (D4)
#   ⚠ 0x86 UDP 업로드가 구현되면 실제 값으로 바꿀 것
CALIB = {
    "v1": {"scale": _f("GTS_CALIB_V1_SCALE", 3.3 / 65535.0),
           "offset": _f("GTS_CALIB_V1_OFFSET", 0.0)},
}

# ── DB writer (인수인계 6-4: 수신 경로 비블로킹) ──────────────────────
QUEUE_MAX = _i("GTS_QUEUE_MAX", 20000)
GFC_FLUSH_SEC = _f("GTS_GFC_FLUSH_SEC", 5.0)          # GFC 1초 → 5초마다 COPY
GFC_UNASSIGNED_KEEP_H = _f("GTS_GFC_KEEP_H", 24.0)    # run 밖 GFC 데이터 보존
HOUSEKEEP_SEC = _f("GTS_HOUSEKEEP_SEC", 600.0)

# ── 제어권 (D14) ────────────────────────────────────────────────────
LOCK_LEASE_SEC = _f("GTS_LOCK_LEASE_SEC", 30.0)
