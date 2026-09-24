#!/usr/bin/env bash
# =====================================================================
# GTS DB 설치 — Ubuntu 서버(192.168.0.6)에서 한 번 실행
#
#   sudo bash db/install_db.sh            # PostgreSQL 설치 + DB/계정 생성 + 스키마 적용
#   sudo bash db/install_db.sh --reset    # ⚠ DB 를 지우고 스키마부터 다시 (개발용)
#
# 결과
#   DB 이름 gts / 계정 gts / 비밀번호는 /home/gts/gts_udp_server/.env 의 GTS_DB_DSN 에 기록
#   PostgreSQL 은 localhost(5432)에서만 listen — D15: 어느 망에도 열지 않는다
# =====================================================================
set -euo pipefail

APP_DIR="${APP_DIR:-/home/gts/gts_udp_server}"
APP_USER="${APP_USER:-gts}"
DB_NAME="${DB_NAME:-gts}"
DB_USER="${DB_USER:-gts}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SCHEMA="$HERE/schema.sql"
ENV_FILE="$APP_DIR/.env"

[[ $EUID -eq 0 ]] || { echo "sudo 로 실행할 것"; exit 1; }
[[ -f "$SCHEMA" ]] || { echo "schema.sql 없음: $SCHEMA"; exit 1; }

echo "▶ 1. PostgreSQL 설치"
if ! command -v psql >/dev/null; then
    apt-get update -q
    apt-get install -y -q postgresql postgresql-contrib
fi
systemctl enable --now postgresql

PGVER=$(ls /etc/postgresql | sort -V | tail -1)
PGCONF="/etc/postgresql/$PGVER/main/postgresql.conf"
echo "   PostgreSQL $PGVER"

echo "▶ 2. localhost 전용 listen 확인 (D15)"
if grep -qE "^\s*listen_addresses" "$PGCONF"; then
    sed -i -E "s/^\s*listen_addresses.*/listen_addresses = 'localhost'/" "$PGCONF"
else
    echo "listen_addresses = 'localhost'" >> "$PGCONF"
fi
systemctl restart postgresql

echo "▶ 3. 계정 / DB"
if [[ "${1:-}" == "--reset" ]]; then
    read -r -p "⚠ DB '$DB_NAME' 를 삭제합니다. 계속? (yes) " ans
    [[ "$ans" == "yes" ]] || exit 1
    sudo -u postgres psql -qc "DROP DATABASE IF EXISTS $DB_NAME"
fi

# 비밀번호: .env 에 이미 있으면 재사용, 없으면 새로 생성
DB_PASS=""
if [[ -f "$ENV_FILE" ]] && grep -q "^GTS_DB_DSN=" "$ENV_FILE"; then
    DB_PASS=$(grep "^GTS_DB_DSN=" "$ENV_FILE" | sed -E 's#.*://[^:]+:([^@]+)@.*#\1#')
fi
[[ -n "$DB_PASS" ]] || DB_PASS=$(head -c 24 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' | head -c 24)

if sudo -u postgres psql -tAc "SELECT 1 FROM pg_roles WHERE rolname='$DB_USER'" | grep -q 1; then
    sudo -u postgres psql -qc "ALTER ROLE $DB_USER WITH LOGIN PASSWORD '$DB_PASS'"
else
    sudo -u postgres psql -qc "CREATE ROLE $DB_USER WITH LOGIN PASSWORD '$DB_PASS'"
fi
if ! sudo -u postgres psql -tAc "SELECT 1 FROM pg_database WHERE datname='$DB_NAME'" | grep -q 1; then
    sudo -u postgres psql -qc "CREATE DATABASE $DB_NAME OWNER $DB_USER ENCODING 'UTF8'"
fi
sudo -u postgres psql -d "$DB_NAME" -qc "CREATE EXTENSION IF NOT EXISTS pgcrypto"

echo "▶ 4. 스키마"
if sudo -u postgres psql -d "$DB_NAME" -tAc "SELECT to_regclass('public.schema_meta')" | grep -q schema_meta; then
    V=$(sudo -u postgres psql -d "$DB_NAME" -tAc "SELECT value FROM schema_meta WHERE key='version'")
    echo "   이미 적용됨 (v$V) — 건너뜀.  다시 만들려면 --reset"
else
    PGPASSWORD="$DB_PASS" psql -h localhost -U "$DB_USER" -d "$DB_NAME" \
        -v ON_ERROR_STOP=1 -q -f "$SCHEMA"
    echo "   적용 완료"
fi

echo "▶ 5. .env 기록 ($ENV_FILE)"
mkdir -p "$APP_DIR"
touch "$ENV_FILE"
DSN="postgresql://$DB_USER:$DB_PASS@localhost:5432/$DB_NAME"
if grep -q "^GTS_DB_DSN=" "$ENV_FILE"; then
    sed -i "s#^GTS_DB_DSN=.*#GTS_DB_DSN=$DSN#" "$ENV_FILE"
else
    echo "GTS_DB_DSN=$DSN" >> "$ENV_FILE"
fi
chown "$APP_USER:$APP_USER" "$ENV_FILE"
chmod 600 "$ENV_FILE"

echo
sudo -u postgres psql -d "$DB_NAME" -c "SELECT key, value FROM schema_meta"
echo "✔ DB 준비 완료.  다음: python3 manage.py create-admin <이름>  으로 관리자 토큰 발급"
