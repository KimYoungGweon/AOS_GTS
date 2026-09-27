# GTS 캘리브레이션 — 서버 API · 브리지 규격

작성 2026-09-27 · 근거 `DOC/GTS_HV캘리브레이션_계획.md`, 목업 5-1 ~ 5-6
구현: `server/gts/cal.py` (엔진) · `cal_api.py` (REST) · `caltable.py` (테이블 계산) · `dmm.py` (34461A) · `udp_server.py` (브리지 명령)

---

## 0. 구성

```
[브라우저] ─/api/cal/*─▶ [gts_server.py]
                            ├─ TCP 5025 SCPI ─▶ DMM 34461A 192.168.0.7   HV 1000 V · CV 10 V 레인지 고정
                            └─ UDP 5500 ─▶ gts_aos_bridge ─UART2 38400─▶ STM32
```

- DMM 이 하나이므로 **캘리브레이션 세션은 서버 전체에 하나**
- 세션 중인 AOS 는 `udp_server.CAL_BLOCK` — 콘솔·웹(장비 상세)의 파라미터 변경은 NAK / 409 `denied_cal`, 측정 run 시작도 거부
- **F/W 에 raw DAC 명령이 없어도 된다.** 서버가 F/W 테이블을 알고 있으므로
  - 원하는 DAC 가 나오도록 설정 V 를 역산해 PARAM_SET(0x40) 으로 보내고, F/W 가 낸 DAC 를 같은 float32 식(`Find_Cal_Result_for_DAC_*`)으로 계산해 기록
  - HV_ADC raw 는 HV_Vs 평균을 VS 테이블로 역산 (선형 초기화 후에는 `raw = HV_Vs × FS / 200`)
  - → 선형 초기화 후 전체 보정 / 초기화 없는 일부 구간 보정이 같은 코드

## 1. 설정 (`.env`)

| 키 | 기본 | 설명 |
|---|---|---|
| `GTS_DMM_HOST` / `GTS_DMM_PORT` | `192.168.0.7` / `5025` | 34461A |
| `GTS_DMM_RANGE_HV` / `_CV` | `1000` / `10` | **고정 (2026-09-27 결정)** |
| `GTS_DMM_NPLC` | `10` | |
| `GTS_CAL_NO_MAX` | `101` | F/W CAL_NO. **F/W 를 101 로 고치기 전에는 `40` 으로 둘 것** (40 초과 쓰기 = F/W 배열 넘침) |
| `GTS_CAL_RAMP_STEP_V` / `_DELAY` | `10` / `0.3` | HV 램프 |
| `GTS_CAL_DEV_LIMIT_HV` / `_CV` | `5` / `0.5` | \|실측−설정\| 초과 시 자동 중단 |
| `GTS_CAL_DISCHARGE_V` / `_TIMEOUT` | `1.0` / `20` | 0 V 복귀 후 방전 판정 |
| `GTS_CAL_DIR` | `server/data/cal` | 세션 기록 JSON (DB 단계 전 임시) |

## 2. REST API — `/api/cal`  (Bearer 토큰, 조회 read / 나머지 control)

| 화면 | 메서드 · 경로 | body | 설명 |
|---|---|---|---|
| 공통 | `GET /api/cal` | | `{session, dmm, limits}` — 웹이 1 초마다 조회 |
| 공통 | `POST /estop` | | 작업 취소 + HV/CV 0 V (세션 없어도 200) |
| 5-1 | `POST /session` | `{device_id, sn, adc: ad7739\|mcp3202, mode: full\|partial}` | 시작 — 오프라인·run 중·다른 세션 있으면 409 |
| 5-1 | `DELETE /session` | | 종료 — 0 V, CAL_MODE off, 콘솔 제어 복귀, 기록 저장 |
| 5-1 | `POST /dmm/connect` | `{host?, port?}` | `*IDN?` |
| 5-1 | `POST /precheck` | `{user_checks: {wiring, cv, load}}` | DMM 응답·0 V 입력, 브리지 HV_Vs, CAL_QUERY, run 없음, 출력 0 |
| 5-1 | `POST /backup` | | CAL_QUERY type 0·1·4 → 세션에 보관 |
| 5-1 | `POST /init` | | 선형 2 점 CAL_WRITE + 되읽기 (전체 보정만, 백업 필수) |
| 5-1 | `POST /restore` | | 백업 테이블로 되돌리기 |
| 5-2 | `POST /jog` | `{target: hv\|cv, volt \| delta \| dac, avg_n}` | 출력(램프) → `{setv, dac, meas, dev, hv_vs, raw}` |
| 5-2 | `POST /read` | `{target, avg_n}` | 읽기만 |
| 5-2 | `POST /range-mark` | `{target, which: min\|max, volt?}` | [이 값을 최소로/최대로] → 계획 시작·끝 |
| 5-2 | `PUT /plan` | `{hv:{enabled,include_zero,start,end,step}, cv:{…}, avg_n, settle_ms, sense_ms}` | 포인트 미리보기, 101 초과면 `error` |
| 5-3 | `POST /sweep` | | 자동 측정 시작 (계획 오류면 400) |
| 5-3 | `POST /job/pause\|resume\|continue\|abort` | | `continue` = DMM 배선 옮긴 뒤 |
| 5-3 | `GET /points?kind=sweep\|verify\|jog&since=` | | 측정점 (증분 조회) |
| 5-4 | `POST /build` | | 새 테이블 + 검사 (일부 구간이면 백업과 병합) |
| 5-4 | `POST /save` | `{types?: [HV_DAC,HV_VS,CV_DAC], memo?}` | CAL_WRITE → CAL_QUERY 비교. 검사 오류면 400, 불일치 502 |
| 5-5 | `POST /verify` | plan 과 같은 형식 | 보정된 경로로 검증 |
| 5-5 | `POST /judge` | `{verdict: pass\|fail, memo}` | |
| 5-6 | `GET /history?device_id=&sn=` · `GET /history/{id}` | | 세션 기록 (DB 단계에서 테이블로) |

오류 응답: `{"error": code, "detail": 한글 메시지}` — `no_session` · `offline` · `bridge` · `verify` · `owner` · `dmm` · `denied_cal`

### 작업(job) 상태

`session.job = {kind: sweep|verify, state, phase, i, n, msg, wait, error}`
state: `running` · `paused` · `wait_user`(DMM 배선 옮기기) · `done` · `aborted` · `failed`

### 자동 측정 순서

```
HV: 계획의 각 공칭 V → 목표 DAC(공칭 식) → 설정 V 역산 → 램프 → 안정화 → DMM(1000 V) n회 → HV_Vs 평균 → raw 역산
    0 V 기준점은 측정 없이 (0 V, DAC 0) / (raw 0, 0 V)
    → 0 V · 방전 대기 → "DMM 을 CV 출력으로" (wait_user) → continue
CV: 같은 방식, DMM 10 V 레인지
자동 중단: DMM 무응답·과입력 / 브리지 무응답 / |실측−설정| > 한계 / 실측·raw 역전 / HV > 205 V → 0 V
```

## 3. 브리지 UDP 명령 (5500, 신규 0x46 ~ 0x4E)

프레임은 기존과 같다 (STX · DTYPE 1 · DID · CMD · SEQ · SIZE · DATA · CRC16). **응답(0x49 · 0x4B · 0x4E)은 요청 seq 를 반사**한다.

| CMD | 이름 | 방향 | payload | 브리지 동작 |
|---|---|---|---|---|
| 0x46 | CAL_MODE | S→A | `u8 on, pad[3]` | ACK. on 동안 섀도 재전송(0x2A)은 서버 PARAM_SET 에만 반응 |
| 0x47 | CAL_WRITE | S→A | STM32 `0xA0` payload 그대로 (`u8 type, u8 no, no×6`) | UART 0xA0 송신 후 ACK (응답 없음 — 서버가 0x48 로 되읽음) |
| 0x48 | CAL_QUERY | S→A | `u8 type, pad[3]` | UART 0xA1 → 응답을 0x49 로 |
| 0x49 | CAL_TABLE | A→S | STM32 `0xA1` 응답 payload 그대로 | |
| 0x4A | CAL_SENSE_Q | S→A | `u16 window_ms, u16 rsv` | 이후 window 동안 STM32 0x03 의 HV_Vs(f32, offset 9) 를 모아 |
| 0x4B | CAL_SENSE | A→S | `f32 avg, f32 min, f32 max, u16 n, u16 rsv, u32 age_ms` (20 B) | n=0 이면 서버가 "0x03 꺼짐" 오류 |
| 0x4C | CAL_DAC_RAW | S→A | `u8 ch(0 CV,1 HV), u8 rsv, u16 dac` | 선택 — F/W 신규 명령이 생기면 |
| 0x4D | CAL_ADC_RAW_Q | S→A | `u8 ch, u8 avg_n, u16 rsv` | 선택 |
| 0x4E | CAL_ADC_RAW | A→S | `u8 ch, u8 n, u16 rsv, f32 raw_avg` | 선택 |

브리지에서 함께 고칠 것: `AOS_MAX_FRAME 512`·`AOS_UART_DATA_MAX 256` → 1024 이상 (101 포인트 테이블 608 B), 0x03 자동 상태와 0xA1 응답 구분, 0x47 길이 검사.

## 4. 시험 (시뮬레이터 — 장비 없이)

```bash
# A: 서버 (DB 없이)
GTS_BOOT_TOKEN=t GTS_DB_DISABLE=1 GTS_DMM_HOST=127.0.0.1 GTS_CAL_RAMP_DELAY=0.05 GTS_CAL_DIR=/tmp/gts_cal python3 gts_server.py
# B: AOS 브리지 + STM32 테이블 + DMM 흉내
AOS_SIM_DMM=5025 python3 aos_sim.py 127.0.0.1 1 6500
# C
python3 test_cal.py localhost t both
```

2026-09-27 결과 (클라우드 작업 환경): 전체 18/19 · 일부 구간 14/14 PASS
- 전체 보정: 188 point (HV 87 + CV 101), 저장·되읽기 일치, 검증 HV 출력 오차 최대 −4.7 mV · 표시 −5.2 mV · CV −0.16 mV (시뮬레이터 모델 기준)
- 일부 구간(150~200 V, 초기화 없음): 병합 87 point, 경계 단차 0.003 V
- `AOS_SIM_CALNO=40`(지금 F/W) 이면 저장 단계에서 "되읽기 불일치" 로 막힘 — F/W CAL_NO 101 수정 전 확인용
- 비상 정지·일시 정지·조그 거부·콘솔/웹 차단·차단 해제 확인, `test_packets.py` 회귀 통과
- 첫 시험의 1 FAIL(사전 점검 "출력 0 V")은 옛 VS 테이블이 raw 12 를 1.8 V 로 표시한 것 — HV_Vs 대신 설정값으로 판정하도록 수정

## 5. 다음

1. **DB** — `cal_session`, `cal_point`, `cal_table_snapshot`(backup/init/new/saved), `cal_verify_point` + 이력·비교 API (지금은 JSON 파일)
2. 웹 `pages/calibration.js` (목업 5-1 ~ 5-6)
3. 브리지 0x46 ~ 0x4B · 버퍼 확대
4. F/W: CAL_NO 101 · EEPROM 주소 재배치 · 부팅 시 EEPROM 우선 · CAL_WRITE 길이 검사 · break 누락 · HV 램프 버그
