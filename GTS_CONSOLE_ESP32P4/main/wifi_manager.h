/*
 * wifi_manager.h — 등록 지점 기반 Wi-Fi 관리
 *
 * 접속 장소가 2~3곳으로 고정이므로 일반적인 스캔·비밀번호 입력 흐름
 * 대신 gts_config.h 의 등록 지점 표(GTS_WIFI_NET)를 쓴다.
 * 화면(P5)은 그 표를 보여주고 선택·접속만 한다.
 *
 * 비동기
 * ──────
 * 이 모듈의 함수는 전부 즉시 돌아온다. 연결 결과는 이벤트 핸들러가
 * g_gts.wifi_* 에 쓰고 dirty 플래그를 세우며, 화면은 그것을 폴링한다.
 * (예전 버전은 연결될 때까지 블록했는데, 그러면 P5 에서 다른 지점을
 *  고르는 동안 UI 가 멎는다.)
 *
 * 최근 접속 지점
 * ──────────────
 * 성공하면 지점 인덱스와 시각을 NVS 에 남긴다. 다음 부팅 때 그 지점부터
 * 시도하고, P5 목록에 "최근 접속" 으로 표시한다.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** NVS·netif·wifi 초기화 후 최근 접속 지점(없으면 0번)으로 접속 시도. */
esp_err_t wifi_manager_start(void);

/** 지점을 골라 접속. 진행 중이던 연결은 끊는다. */
void wifi_manager_connect(uint8_t idx);

/** 연결 해제. 자동 재접속도 멈춘다. */
void wifi_manager_disconnect(void);

/** 등록 지점이 지금 잡히는지 확인하는 스캔. 결과는 g_gts.wifi_found/rssi_list. */
void wifi_manager_scan(void);

bool   wifi_manager_is_connected(void);
int8_t wifi_manager_get_rssi(void);

#ifdef __cplusplus
}
#endif
