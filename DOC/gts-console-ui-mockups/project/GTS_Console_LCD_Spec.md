# GTS Console LCD 화면 사양서

- **Rev** 0.2 · 2026-09-21
- **대상** 4.3″ 800 × 480 LCD · ESP32-P4 (JC4880P443C) · LVGL 구현용
- **기준** 승인된 목업 `GTS Console LCD.dc.html`에서 실측
- **단위** px(= LCD 픽셀), 원점은 각 화면의 좌상단 (0, 0)

---

## 1. 하드웨어 · 화면 규격

| 항목 | 값 | 비고 |
| --- | --- | --- |
| Panel | 4.3″ TFT, 800 × 480 (H mode) | JC4880P443C EVM Kit |
| 유효 표시면 | 93.6 × 56.2 mm | 대각 109.2 mm |
| Pixel pitch | 0.117 mm/px (≈ 217 ppi) | 1 mm ≈ 8.5 px |
| 입력 | 정전식 터치 + 외부 Encoder(Jog) + Jog S/W | Jog·Jog S/W는 외부 장치, 화면에는 상태만 표시 |
| 통신 | UDP client → 218.147.152.41 : 5502 | 값 변경 시 즉시 송신 |
| 색 깊이 | RGB565 권장 | 3절 색상표에 16bit 값 병기 |

---

## 2. 공통 레이아웃

모든 화면은 3단 고정 구조. 상단바와 조그바는 화면이 바뀌어도 위치·높이가 같으므로 LVGL에서 한 번만 생성하고 내용만 교체할 것.

| 영역 | x, y, w, h | 내부 여백 |
| --- | --- | --- |
| Top bar | 0, 0, 800, 44 | 좌우 14 |
| Content | 0, 44, 800, 372 | 12 (상하좌우) |
| Jog bar | 0, 416, 800, 64 | 좌우 10, 요소 간격 8, 상단 1px 구분선 |
| Content 작업영역 | 12, 56, 776, 348 | 전 화면 공통 |

### 2.1 상단바 (전 화면 공통)

| 요소 | x, y, w, h | 폰트 | 내용 · 규칙 |
| --- | --- | --- | --- |
| Brand | 14, 10, 148, 25 | Cond 700 / 25, 자간 0.08em | "GTS CONSOLE" 고정 |
| MyID chip | 138, 9, 86, 26 | Cond 600 / 18 | "MyID : 01" · 콘솔 자기 ID, 항상 표시 |
| Screen title | 237, 13, auto, 22 | Body 400 / 14 | 화면명 (영문 / 한글) |
| Device chip | 482, 8, 105, 29 | Cond 600 / 19 | "AOS · ID 07" · 선택 장치, 1px 테두리 |
| Link dot | 601, 18, 8, 8 | — | 연결 OK 초록 / 실패 적색 |
| Link label | 615, 10, auto, 23 | Body 400 / 15 | LINK / NO REPLY |
| Server | 661, 10, 123, 23 | Body 400 / 15, 60% | "218.147.152.41:5502" |

### 2.2 조그바 (전 화면 공통)

| 요소 | x, y, w, h | 폰트 | 내용 |
| --- | --- | --- | --- |
| "Jog target" 라벨 | 10, 424, 116, 19 | Cond 600 / 13, 자간 0.1em | 고정 문자열 |
| 대상명 | 10, 443, 116, 26 | Cond 600 / 22 | 현재 jog 대상 (예: HV, DEVICE ID) |
| 구분선 | 156, 426, 1, 42 | — | divider 색 |
| 현재값 | 171, 422, auto, 46 | Cond 600 / 38, tabular | 선택 항목의 현재값 |
| 단위·범위 | 값 우측 +7, 442 | Body 400 / 16 | "V (0 – 200)" 형식 · `white-space:nowrap` 필수 |
| 전환 버튼 | 140 × 53, 간격 8, STEP 배지 좌측 | Cond 600 / 14 + 20 | P2·P3·P4·P5에 표시 — "DEVICE / 장치 변경", "GO TO / MANUAL ▸", "GO TO / 자동측정", "SCAN / 다시 검색" |
| STEP 배지 | 674, 421, 112, 53 | Cond 600 / 18·19 | **PAGE 4에만 표시** · accent 채움 · 최우측 |

---

## 3. 색상 토큰

| 역할 | HEX | RGB565 | 사용처 |
| --- | --- | --- | --- |
| bg | #F2F2F3 | 0xF79E | 화면 바탕 |
| text | #1D1F20 | 0x18E4 | 기본 글자 |
| accent | #5980A6 | 0x5C14 | 선택 채움, 주 버튼, 게이지 |
| accent-100 | #EEF6FF | 0xEFBF | 선택 타일 배경 |
| accent-700 | #416180 | 0x4310 | 강조 수치 (남은 시간 등) |
| accent-900 | #1D2D3D | 0x1967 | 상단바 바탕, ON 상태 버튼 |
| neutral-100 | #F5F5F8 | 0xF7BF | 조그바 바탕 |
| neutral-200 | #E7E7EA | 0xE73D | 비활성 배지 바탕 |
| neutral-300 | #D4D4D7 | 0xD6BA | 게이지 트랙 |
| neutral-600 | #7A7A7D | 0x7BCF | 보조 텍스트 |
| divider | #D0D0D1 | 0xD69A | 1px 테두리·구분선 |
| MyID chip 바탕 | #3F4C5A | 0x3A6B | 상단바 위 칩 |
| state OK | #9FE0B4 | 0x9F16 | LINK 표시등 |
| state FAIL | #E0A39F | 0xE513 | NO REPLY 표시등 |

테두리는 모두 1px, 선택 상태는 2px accent. 라운드 없음(radius 0). 비활성 요소는 불투명도 40–45%.

---

## 4. 타이포그래피

| 용도 | px | 실물 높이 | 폰트 |
| --- | --- | --- | --- |
| 섹션 라벨 / 배지 | 13 | 1.5 mm | Barlow Condensed 600, 자간 0.1em, 대문자 |
| 보조 설명 | 14–15 | 1.6–1.8 mm | Barlow 400 |
| 본문 · 표 값 | 16 | 1.9 mm | Barlow 400/500 |
| 항목명 (타일 라벨) | 21 | 2.5 mm | Barlow Condensed 600 |
| 조그 대상명 | 22 | 2.6 mm | Barlow Condensed 600 |
| 버튼 | 24–34 | 2.8–4.0 mm | Barlow Condensed 600, 자간 0.08em |
| 타일 값 | 40 | 4.7 mm | Barlow Condensed 600, tabular |
| 조그바 값 / 큰 수치 | 38–56 | 4.4–6.5 mm | Barlow Condensed 600, tabular |

**한글 글리프 주의.** Barlow / Barlow Condensed에는 한글이 없음. LVGL 폰트 변환 시 ① 영문·숫자는 Barlow Condensed SemiBold + Barlow Regular, ② 한글 보조 라벨은 별도 한글 폰트(Pretendard, Noto Sans KR 등)를 **사용 문자만 서브셋**하여 13·14·15·16px 4종만 생성. 숫자는 tabular(고정폭)로 변환해야 값 변경 시 흔들리지 않음.

---

## 5. 화면별 사양

화면은 5종. P1 Device Select · P2 GFC Mode · P3 AOS 가스측정 · P4 AOS Manual · P5 WiFi 접속 List.

### 5.1 PAGE 1 — Device Select / 장치 선택

| 요소 | x, y, w, h | 동작 |
| --- | --- | --- |
| "Device Type" 라벨 | 12, 56, 300, 19 | — |
| AOS 버튼 | 12, 81, 147, 52 | 터치 선택 · 선택 시 accent 채움 |
| GFC 버튼 | 167, 81, 145, 52 | 동일, 상호 배타 |
| "Device ID" 라벨 | 12, 147, 300, 19 | — |
| ID 버튼 그리드 | 12, 172, 301, 194 | 5열 × 4행, 셀 55 × 44, 간격 6 (열 피치 61, 행 피치 50) |
| Connection Test 카드 | 324, 56, 462, 346 | 1px 테두리 + 코너 마크 11px |
| 카드 제목 | 339, 69, 432, 19 | — |
| 정보 행 6개 | 339, 96부터 행 높이 38 | 좌: 라벨 14px / 우: 값 16px, 하단 1px 선 |
| RE-TEST 버튼 | 339, 343, 119, 46 | 재연결 시도 |
| ENTER CONTROL 버튼 | 466, 343, 305, 46 | accent 채움 · GFC → P2, **AOS → P4 (Manual Mode 기본 진입)** |
| 조그바 | 대상 = DEVICE ID | Jog 회전 시 ID 1↔20 순환, STEP 표시 없음 |

정보 행 순서: Target / UDP Server / Link / Model("GTS CONSOLE ID : n") / Device State / Last Reply.

### 5.2 PAGE 2 — GFC Mode / 펌프 제어

| 요소 | x, y, w, h | 동작 |
| --- | --- | --- |
| "Control Mode" 라벨 | 12, 67, 224, 19 | — |
| MANUAL 탭 | 545, 57, 120, 38 | 선택 시 accent 채움 |
| AUTO 탭 | 665, 57, 120, 38 | 기본 선택 |
| 시간 타일 ① Start · Pump On Time | 12, 106, 528, 144 | 0.1–60 sec, step 0.1 · 터치로 jog 대상 선택 |
| 시간 타일 ② 10min Cycle · Pump On Time | 12, 258, 528, 144 | 동일 |
| 타일 내부 | 라벨 y+26 (13px) / 값 y+62 (56px) / 단위 15px | 선택 시 2px accent 테두리 + accent-100 배경 |
| 사이드 카드 | 550, 106, 236, 296 | Auto: "Cycle / 10분 주기" + 카운트다운 48px + 진행바 6px / Manual: "Pump State" + ON·OFF 48px, 진행바 숨김 |
| 메인 버튼 | 563, 295, 210, 66 | Auto: AUTO START / AUTO STOP · Manual: PUMP ON / PUMP OFF |
| 주석 | 563, 367, 210, 22 | "Stop 시 Pump1, Pump2 모두 Off" |
| 조그바 | 대상 = 선택된 시간 타일 | Manual일 때 "—", 불투명도 40%, jog 무시 |
| "DEVICE / 장치 변경" 버튼 | 조그바 우측, 140 × 53 | 연결 해제 후 P1 복귀 |

Pump1 · Pump2는 항상 동시 구동. Manual 모드에서 시간 타일 2개는 불투명도 40%로 비활성, 터치 무반응.

### 5.3 PAGE 3 — AOS 가스 Data 측정

| 요소 | x, y, w, h | 동작 |
| --- | --- | --- |
| "Data Type" 라벨 | 12, 56, 774, 19 | — |
| 타입 카드 4개 | y 85, h 100 · x = 12 / 207 / 402 / 599, w ≈ 187 (간격 8) | preTest · 1 hour · 2 hour · 8 hour(full data) |
| 카드 내부 | 이름 28px (상단 +12) / 소요시간 15px / 비고 14px (하단) | 선택 시 2px accent 테두리 |
| 측정 상태 카드 | 12, 195, 568, 207 | 1px 테두리 + 코너 마크 |
| Elapsed | 27, 208, auto, 75 | 라벨 13px + 값 50px (hh:mm:ss) |
| Remaining | 우측 정렬, 376, 224 | 값 34px, accent-700 |
| 진행 바 | 27, 351, 538, 10 | 트랙 neutral-300 / 채움 accent |
| 진행 캡션 | 27, 367, 538, 22 | 좌: 샘플 수 / 우: "측정 종료 시 자동 Stop" |
| START·STOP 버튼 | 590, 195, 196, 207 | 실행 중 accent-900 + "STOP", 대기 accent + "START" |
| 조그바 | 대상 = DATA TYPE | Jog 회전 = 4종 순환 선택, STEP 표시 없음 |
| "DEVICE / 장치 변경" 버튼 | 조그바 우측, 140 × 53 | 연결 해제 후 P1 복귀 |
| "GO TO / MANUAL ▸" 버튼 | 조그바 최우측, 140 × 53 | P4로 전환 · 측정 중에는 비활성(45%) |

측정 종료 시 펌웨어가 자동으로 Stop 상태로 전환하고 진행 바를 100%로 유지.

### 5.4 PAGE 4 — AOS Manual Mode

| 요소 | x, y, w, h | 동작 |
| --- | --- | --- |
| 제어 타일 6개 | 3열 × 2행 · 각 253 × 140 · x = 12 / 273 / 533, y = 56 / 204 (간격 8) | 터치 = jog 대상 선택 |
| 타일 내부 | 항목명 21px (좌상) / 범위 16px (우상) / 값 40px + 단위 16px (중앙) / 한글 16px (좌하) / STEP 배지 19px (우하, 99–114 × 27) | 선택 시 2px accent 테두리 + accent-100 |
| LF_MODE 버튼 | 12, 352, 230, 50 | On/Off 토글 · Off 시 LF_Frq·LF_Volt 타일 45% 비활성 |
| 안내 박스 | 250, 352, 536, 50 | "LF_Mode Off 시 LF_Frq · LF_Volt 항목은 비활성화됩니다." |
| "DEVICE / 장치 변경" 버튼 | 조그바 우측, STEP 배지 좌측 | 140 × 53 · 연결 해제 후 P1 복귀 |
| "GO TO / 자동측정" 버튼 | DEVICE 버튼 우측, STEP 배지 좌측 | 140 × 53 (동일 크기) · P3로 전환 |
| 조그바 STEP 배지 | 674, 425, 112, 44 | 선택 항목의 현재 step (외부 Jog S/W로 변경) |

#### 타일 배치 순서와 값 규격

| 위치 | 항목 | 범위 | step (Jog S/W 순환) | 표시 자리수 |
| --- | --- | --- | --- | --- |
| 1행 1열 | HV | 0 – 200 V | 0.01 / 0.1 / 1 | 소수 2자리 |
| 1행 2열 | Frq | 200 – 800 kHz | 0.1 / 1 / 10 | 소수 1자리 |
| 1행 3열 | Duty | 10 – 50 % | 0.01 / 0.1 | 소수 2자리 |
| 2행 1열 | CV | −5 – 5 V | 0.001 / 0.01 / 0.1 | 소수 3자리 |
| 2행 2열 | LF_Frq | 50 – 200 Hz | 1 | 정수 |
| 2행 3열 | LF_Volt | 0 – 5 V | 0.01 / 0.1 | 소수 2자리 |

LF_Shape(Square / Sine / Triangle / Tropizodal)는 화면에서 제외한다.

### 5.5 PAGE 5 — WiFi 접속 List

접속 장소가 2~3곳으로 고정되므로 전체 스캔 목록이 아니라 **등록된 지점 목록**을 보여준다. WiFi 연결이 끊기면 P1에서 이 화면으로 자동 전환되고, 연결에 성공하면 P1으로 자동 복귀한다.

| 요소 | x, y, w, h | 동작 |
| --- | --- | --- |
| 상단바 우측 | Device chip 대신 WiFi 상태 표시 | WIFI OK(초록) / NO WIFI(적색) + IP 또는 "IP 미할당" |
| "Saved Networks" 라벨 | 12, 56, auto, 19 | 우측에 "n found · 시각" |
| 네트워크 행 3개 | 12, 81 / 161 / 241 · w 516, h 74, 간격 6 | 터치 = 선택 (jog 회전으로도 이동) |
| 행 내부 | SSID 26px (좌상) / 설명 15px (좌하) / 보안·연결 배지 19px / RSSI 24px tabular, 폭 108 우측 정렬 nowrap | 선택 시 2px accent 테두리 + accent-100 |
| Selected 카드 | 540, 56, 246, 346 | 1px 테두리 + 코너 마크 |
| 카드 내용 | SSID 28px, 설명 15px, 구분선, Signal / Security 행 16px | 비밀번호 입력 UI 없음 — 등록된 지점만 표시·선택 |
| CONNECT 버튼 | 555, 322, 216, 66 | 연결 시 accent-900 + "DISCONNECT" |
| 안내 | 555, 394, 216, 20 | "연결되면 PAGE 1로 자동 전환" |
| 조그바 | 대상 = NETWORK, 값 = SSID, "n / 3" | STEP 표시 없음 |
| "SCAN / 다시 검색" 버튼 | 조그바 우측, 140 × 53 | 재검색 |

비밀번호 입력 화면은 두지 않는다. 등록 지점의 SSID·비밀번호는 펌웨어(NVS)에 사전 등록하고, 화면에서는 목록 표시와 선택·접속만 수행한다.

---

## 6. 터치 · 입력 규칙

| 규칙 | 값 |
| --- | --- |
| 최소 터치 타겟 | 44 × 44 px (≈ 5.1 mm) — ID 버튼 55 × 44가 최소 크기 |
| 터치 간격 | 최소 6 px |
| 피드백 | Press 시 accent-700 채움 또는 2px 테두리, 100 ms 이내 반영 |
| Jog (외부 encoder) | 1 detent = 현재 step 1회 증감, 범위 clamp, 순환 없음 (선택형 항목만 순환) |
| Jog S/W (외부) | 선택 항목의 step 순환. 화면은 STEP 배지 값만 갱신 |
| 값 변경 → 송신 | 변경 즉시 UDP 전송. 연속 회전 시 50 ms 디바운스 권장 |

---

## 7. 화면 전이 · 상태

```
  [P5] WiFi List ──── 연결 성공 ────▶ [P1] Device Select
        ▲                                   │  ENTER CONTROL (연결 성공 시에만 활성)
        └──── WiFi 미연결 시 자동 전환 ──────┤
                                            ├─ dev_type = GFC ─▶ [P2] GFC Mode
                                            │                      ├ Manual : PUMP ON/OFF
                                            │                      └ Auto   : 시간 2종 + AUTO START/STOP
                                            └─ dev_type = AOS ─▶ [P4] AOS Manual  ◀──▶ [P3] AOS 가스측정
                                                                 (기본 진입)
                               전환: 조그바 "GO TO / MANUAL ▸" · "GO TO / 자동측정"

  [P2] GFC 내부 모드 전환 : 상단 MANUAL / AUTO 탭
  [P2·P3·P4] ─ 조그바 "DEVICE / 장치 변경" ─▶ 연결 해제 후 [P1] 복귀
  모든 화면 ─ UDP 응답 3회 없음 ─▶ [P1] 복귀 + NO REPLY 표시
  모든 화면 ─ WiFi 끊김 ─▶ [P5] 전환
```

| 전역 상태 | 값 | 비고 |
| --- | --- | --- |
| my_id | 1 – 20 | 콘솔 자기 ID, 상단바 상시 표시 |
| dev_type | AOS / GFC | P1에서 선택 |
| dev_id | 1 – 20 | P1에서 선택 |
| link_state | IDLE / TESTING / ONLINE / NO_REPLY | 상단바 표시등 |
| gfc_mode | MANUAL / AUTO | 기본 AUTO |
| pump_on | bool | Pump1·Pump2 동시 |
| gfc_start_sec / gfc_cycle_sec | 0.1 – 60.0 | 0.1 단위 |
| aos_type | 0 preTest / 1 · 1h / 2 · 2h / 3 · 8h | — |
| aos_running, aos_elapsed | bool, sec | 종료 시 자동 정지 |
| hv, frq, duty, cv, lf_frq, lf_volt | 5.4 표 참조 | 각각 step index 보유 |
| lf_on | bool | Off 시 LF 항목 비활성 |
| wifi_state | DISCONNECTED / CONNECTING / CONNECTED | 끊기면 P5로 자동 전환 |
| wifi_sel | 0 – (등록 지점 수−1) | P5 선택 인덱스 |

---

## 8. LVGL 구현 매핑

| UI 요소 | 권장 위젯 | 메모 |
| --- | --- | --- |
| 화면 4종 | `lv_obj` 4개 (screen 또는 컨테이너) | 상단바·조그바는 공용 컨테이너로 두고 내용만 갱신 |
| 상단바 / 조그바 | `lv_obj` + `lv_label` | 재생성 금지, 텍스트만 `lv_label_set_text_fmt` |
| Device ID 20개 | `lv_btnmatrix` (5 × 4) | CHECKABLE + ONE_CHECKED, `lv_btnmatrix_set_btn_width` 균등 |
| AOS / GFC, MANUAL / AUTO | `lv_btnmatrix` 1행 2열 | 세그먼트 컨트롤 대용 |
| 값 타일 (P2 · P4) | `lv_obj` + 라벨 3개 | 선택 시 `lv_obj_set_style_border_width(2)` + accent-100 배경 |
| 진행 바 | `lv_bar` | 높이 6 / 10, radius 0 |
| 큰 버튼 | `lv_btn` + `lv_label` | radius 0, 그림자 없음 |
| 연결 정보 표 | `lv_obj` flex row 6개 | `lv_table`은 스타일 제약이 커서 비권장 |
| 코너 마크 (+) | 선택 사항 | 장식 요소. 생략 가능, 유지 시 11px 라인 2개로 구현 |

```c
/* 공통 스타일 예시 */
static lv_style_t st_card, st_card_sel;
lv_style_init(&st_card);
lv_style_set_radius(&st_card, 0);
lv_style_set_bg_opa(&st_card, LV_OPA_TRANSP);
lv_style_set_border_width(&st_card, 1);
lv_style_set_border_color(&st_card, lv_color_hex(0xD0D0D1));

lv_style_init(&st_card_sel);
lv_style_set_border_width(&st_card_sel, 2);
lv_style_set_border_color(&st_card_sel, lv_color_hex(0x5980A6));
lv_style_set_bg_opa(&st_card_sel, LV_OPA_COVER);
lv_style_set_bg_color(&st_card_sel, lv_color_hex(0xEEF6FF));

/* 조그 입력 → 현재 선택 항목 값 증감 */
void jog_rotate(int8_t dir) {
    ctrl_t *c = &ctrl[sel_idx];              /* HV, Frq, Duty, CV, LF_Frq, LF_Volt */
    c->value = clampf(c->value + dir * c->step[c->step_idx], c->min, c->max);
    lv_label_set_text_fmt(c->lbl_value, "%.*f", c->decimals, c->value);
    udp_send_param(c->param_id, c->value);   /* 값 변경 즉시 송신 */
}
```

---

## 9. UDP 프로토콜

콘솔 → AOS/GFC 제어용 UDP 패킷. 기본 형식은 AOS-ESP32 간 UART 규격(`FW_RS232_통신규격.md`)을 따른다.

### 9.1 패킷 형식

```
 0      1            2          3      4        5        6 …            n
+------+------------+----------+------+--------+--------+--------------+---------+
| 0x02 | device_type| device_id| cmd  | size_L | size_H | data[size]   | CRC16   |
+------+------------+----------+------+--------+--------+--------------+---------+
```

| 필드 | 크기 | 설명 |
| --- | --- | --- |
| STX | 1 | 고정 `0x02` |
| device_type | 1 | `0x01` AOS · `0x02` GFC |
| device_id | 1 | 1 – 20 |
| cmd | 1 | 9.2 표 |
| size_L / size_H | 2 | data 길이 (little endian) |
| data | size | 명령별 payload (9.2 표) |
| CRC16 | 2 | device_type ~ data 구간 |

- 실수 값은 정수 스케일로 전송 (예: HV 124.50 V → `12450` = 0.01 V 단위).
- 응답은 동일 형식, cmd 최상위 비트 set 또는 별도 ACK cmd — **확정 필요**.

### 9.2 명령 정의 (초안)

| cmd | 이름 | 방향 | data |
| --- | --- | --- | --- |
| 0x01 | CONN_REQ | → | my_id(1) |
| 0x81 | CONN_ACK | ← | state(1), rtt_ms(2) |
| 0x02 | DISCONNECT | → | — |
| 0x10 | GFC_MODE | → | 0 Manual / 1 Auto (1) |
| 0x11 | GFC_PUMP | → | on(1) · Pump1·2 동시 |
| 0x12 | GFC_TIME | → | start_0p1s(2), cycle_0p1s(2) |
| 0x13 | GFC_AUTO_RUN | → | run(1) |
| 0x93 | GFC_STATUS | ← | run(1), pump(1), next_ms(4) |
| 0x20 | AOS_TYPE | → | type(1) · 0 preTest / 1 1h / 2 2h / 3 8h |
| 0x21 | AOS_RUN | → | run(1) |
| 0xA1 | AOS_PROGRESS | ← | elapsed_s(4), samples(4), done(1) |
| 0x30 | AOS_PARAM | → | param_id(1), value(4, 스케일 정수) |
| 0x31 | AOS_LF_MODE | → | on(1) |
| 0xB0 | AOS_STATE | ← | 6종 현재값(4 × 6), lf_on(1) |

**param_id 와 스케일**

| param_id | 항목 | 단위 스케일 | 범위 (정수) |
| --- | --- | --- | --- |
| 0x01 | HV | 0.01 V | 0 – 20000 |
| 0x02 | Frq | 0.1 kHz | 2000 – 8000 |
| 0x03 | Duty | 0.01 % | 1000 – 5000 |
| 0x04 | CV | 0.001 V | −5000 – 5000 (int32) |
| 0x05 | LF_Frq | 1 Hz | 50 – 200 |
| 0x06 | LF_Volt | 0.01 V | 0 – 500 |

### 9.3 전송 시점

| 시점 | 방향 | cmd |
| --- | --- | --- |
| 장치 선택 후 연결 확인 | → | CONN_REQ |
| 장치 변경 (연결 해제) | → | DISCONNECT · 진행 중 동작은 정지 후 해제 |
| GFC Manual Pump On/Off | → | GFC_PUMP |
| GFC Auto 시간 변경 | → | GFC_TIME |
| GFC Auto Start / Stop | → | GFC_AUTO_RUN |
| AOS 측정 타입 선택 · Start / Stop | → | AOS_TYPE, AOS_RUN |
| AOS 측정 진행 | ← | AOS_PROGRESS |
| AOS Manual 값 변경 (6종) | → | AOS_PARAM (50 ms 디바운스) |
| LF_Mode On/Off | → | AOS_LF_MODE |
| 주기 상태 갱신 | ← | GFC_STATUS |

cmd 코드·CRC16 다항식·ACK 규칙은 위 정의를 기준으로 구현한다.

## 10. 구현 체크리스트

1. LVGL 폰트 변환 — 영문 Barlow Condensed 600 (13/16/19/21/22/28/34/38/40/48/56), 한글 서브셋 (13/14/15/16)
2. 공용 상단바 · 조그바 컨테이너 1회 생성, 화면 전환 시 내용만 교체
3. 화면 4종 컨테이너 생성 및 전이 로직 (7절)
4. 외부 encoder / Jog S/W 드라이버 → 선택 항목 값·step 반영 (6절)
5. 값 clamp · 자리수 포맷 (5.4 표)
6. UDP 송수신 태스크 + 무응답 3회 시 P1 복귀
7. AOS 측정 종료 자동 Stop, GFC 10분 주기 타이머
8. WiFi 매니저 — 등록 지점 목록, 자동 재접속, 끊김 시 P5 전환 (5.5)
9. 실기 시인성 확인 후 폰트 크기 조정 (필요 시 라벨 13 → 15px)
