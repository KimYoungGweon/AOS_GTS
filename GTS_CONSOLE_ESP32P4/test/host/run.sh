#!/usr/bin/env bash
#
# 호스트 테스트 — ESP32 하드웨어 없이 순수 로직을 PC gcc 로 검증한다.
# FreeRTOS·ESP-IDF API 는 stub/ 의 최소 헤더로 대체 (뮤텍스는 no-op).
#
#   ./run.sh
#
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
MAIN="$HERE/../../main"
OUT="$HERE/build"
mkdir -p "$OUT"
CFLAGS="-std=gnu11 -Wall -Wextra -Wno-unused-parameter -O1"

echo "── 프로토콜 · 상태 모델 ─────────────────────────────"
gcc $CFLAGS -I"$HERE/stub" -I"$MAIN" \
    "$MAIN/gts_protocol.c" "$MAIN/gts_state.c" "$HERE/test_main.c" \
    -o "$OUT/test_logic" -lm
"$OUT/test_logic"

echo
echo "── 화면 회전 매핑 (LVGL 공식과 대조) ────────────────"
gcc $CFLAGS -I"$MAIN" \
    "$MAIN/gts_rotate.c" "$HERE/test_rotate.c" \
    -o "$OUT/test_rotate"
"$OUT/test_rotate"
