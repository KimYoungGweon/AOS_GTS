#!/usr/bin/env bash
# =====================================================================
# GTS 서버 전환 — UDP 전용 → UDP + DB + Web API (한 번만 실행)
#
#   cd /home/gts/gts_udp_server
#   sudo bash db/install_db.sh          # ① PostgreSQL + 스키마 (+ .env 의 GTS_DB_DSN)
#   sudo bash deploy/install_server.sh  # ② 이 스크립트
#   venv/bin/python manage.py create-admin <이름>      # ③ 관리자 토큰
#   venv/bin/python import_dat.py ~/example_data/      # ④ (선택) 예제 .dat 적재
# =====================================================================
set -euo pipefail
APP_DIR="${APP_DIR:-/home/gts/gts_udp_server}"
APP_USER="${APP_USER:-gts}"
HTTP_PORT="${HTTP_PORT:-8081}"
cd "$APP_DIR"
[[ $EUID -eq 0 ]] || { echo "sudo 로 실행할 것"; exit 1; }

echo "▶ 1. Python 패키지 (venv)"
if [[ ! -x venv/bin/python ]]; then
    apt-get install -y -q python3-venv
    sudo -u "$APP_USER" python3 -m venv venv
fi
sudo -u "$APP_USER" venv/bin/pip install -q --upgrade pip
sudo -u "$APP_USER" venv/bin/pip install -q -r requirements.txt

echo "▶ 2. 패킷 회귀 테스트 (인수인계 6-4)"
sudo -u "$APP_USER" venv/bin/python test_packets.py | tail -2

echo "▶ 3. .env 확인"
touch .env; chown "$APP_USER:$APP_USER" .env; chmod 600 .env
grep -q "^GTS_HTTP_PORT=" .env || echo "GTS_HTTP_PORT=$HTTP_PORT" >> .env
grep -q "^GTS_DB_DSN=" .env || echo "⚠ GTS_DB_DSN 없음 — 먼저 db/install_db.sh 를 실행할 것"

echo "▶ 4. systemd (gts-udp 서비스의 실행 파일을 gts_server.py 로)"
[[ -f /etc/systemd/system/gts-udp.service ]] && \
    cp /etc/systemd/system/gts-udp.service "/etc/systemd/system/gts-udp.service.bak_$(date +%Y%m%d_%H%M)"
cp deploy/gts-udp.service /etc/systemd/system/gts-udp.service
systemctl daemon-reload
systemctl enable gts-udp
systemctl restart gts-udp
sleep 3
systemctl is-active gts-udp

echo "▶ 5. 방화벽"
if command -v ufw >/dev/null && ufw status | grep -q "Status: active"; then
    ufw allow "$HTTP_PORT/tcp" comment 'GTS web/API'
    ufw status | grep -E "$HTTP_PORT|550[0-2]" || true
else
    echo "   ufw 비활성 — 건너뜀"
fi

echo "▶ 6. 확인"
curl -s "http://localhost:$HTTP_PORT/api/health" && echo
cat <<MSG

✔ 완료.
  API 문서   : http://192.168.0.6:$HTTP_PORT/docs
  로그       : journalctl -u gts-udp -f     (Mac: gts-log)
  외부 접속  : 공유기에서 외부포트 $HTTP_PORT → 192.168.0.6:$HTTP_PORT (TCP) 포워딩
               ⚠ HTTP 라 토큰이 평문으로 지나간다. 외부용으로는 read 토큰을 쓰고,
                 control/admin 토큰은 사내망에서만 쓰는 것을 권장.
MSG
