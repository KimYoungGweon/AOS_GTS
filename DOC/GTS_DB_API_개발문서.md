# GTS DB · API 서버 개발문서 — 2026-09-23

> 대상: `server/` (서버 192.168.0.6 의 `/home/gts/gts_udp_server`)
> 근거: `DOC/GTS_DB_Design/GTS_DB_Design_Proposal_v1.md`(v1.3), `DOC/GTS_Web_Design.md`,
> 웹 디자인 캔버스(5화면), `DOC/GTS_인수인계_20260922.md` 6절
> 다음 단계: **웹 프런트** (이 API 위에 디자인 5화면을 올린다)

---

## 0. 30초 요약

| 항목 | 결정 | 비고 |
|---|---|---|
| W1 프로세스 | **한 프로세스** — `gts_server.py` (asyncio 한 루프에 UDP 3포트 + FastAPI) | 서비스 이름은 그대로 `gts-udp` |
| W2 DB | **PostgreSQL** (localhost 전용, 5432 비공개) | D2, D15 |
| W3 스키마 | 설계안 v1.3 → **v1.4** (`server/db/schema.sql`) | 아래 2절 |
| W4 쓰기 | 큐 + writer 태스크, GFC 1초는 **5초마다 COPY** | 수신 경로 비블로킹 |
| W5 실시간 | **WebSocket** `/ws/live` 1초 푸시 | AOS Current 는 메모리 링 (D11) |
| W6 제어 | 웹에서도 제어. **콘솔 무조건 선점**, run 중 AOS 파라미터 변경 거부 | D14, D14a |
| W7 인증 | **HTTP 8080 + 토큰** (read / control / admin) | ⚠ 외부 노출 시 평문 — 5절 |
| 장치 쌍 | **AOS n ↔ GFC n 고정** | device PK 를 (종류, ID) 로 수정 |
| 예제 데이터 | `DOC/example_data` 16개 → **Single 변환 32 run** 적재 | 4절 |

```
 콘솔/브리지 ──UDP 5500·5501·5502──▶ ┌──────────── gts_server.py ─────────────┐
                                     │ udp_server.py (변환 코드 그대로)        │
                                     │   └ _emit() ─▶ gts/wire.py              │
                                     │        ├─▶ live  (메모리 링·통계·이벤트) │──▶ WS /ws/live
                                     │        └─▶ store (큐 → writer → PG)     │
 브라우저 ──HTTP 8080 (토큰)────────▶ │ FastAPI  gts/api.py                     │
                                     │   웹 제어 → 가상 콘솔 세션 → 같은 변환  │
                                     └─────────────────┬──────────────────────┘
                                                       ▼ localhost:5432
                                                  PostgreSQL gts
```

---

## 1. 파일 구성 (`server/`)

| 파일 | 역할 |
|---|---|
| `udp_server.py` | 기존 중계 서버. **변환 코드는 그대로**, `_emit()` 훅 33줄만 추가. 단독 실행하면 예전과 동일 |
| `gts_server.py` | **운영 진입점.** UDP + DB + API 한 프로세스 |
| `gts/hooks.py` | udp_server → 외부 통로 (의존성 0, 핸들러 없으면 no-op) |
| `gts/wire.py` | 훅 → 메모리/DB 연결 |
| `gts/live.py` | 계층 C — AOS Current·GFC 링(600), 수신 통계, 최근 이벤트, 온라인 전이 |
| `gts/db.py` | asyncpg 풀 + 비블로킹 writer, GFC COPY, 장치 UPSERT, housekeeping, DB 장애 시 재접속 |
| `gts/control.py` | 웹 제어 + 최근 조작자 기록(lock, 2026-09-23.01 부터 제어를 막지 않음) |
| `gts/runs.py` | run 수명주기, heatmap 적재/조회, Idf·장치비교(pair) |
| `gts/heatmap.py` `gts/pairs.py` | payload 인코딩·cond_hash·best 계산 (순수 함수) |
| `gts/datfile.py` | PCSW Twin `.dat` 파서 + Single 변환 |
| `gts/auth.py` | 토큰 인증 |
| `gts/api.py` | REST + WebSocket |
| `gts/config.py` | `.env` 설정 |
| `db/schema.sql` | **스키마 v1.4** |
| `db/install_db.sh` | PostgreSQL 설치 · DB/계정 · 스키마 · `.env` |
| `deploy/install_server.sh` | venv 패키지 · 테스트 · systemd 교체 · ufw |
| `deploy/gts-udp.service` | 서비스 파일 (실행 파일만 `gts_server.py`) |
| `import_dat.py` | Twin `.dat` → Single → DB |
| `manage.py` | 사용자·토큰 발급/폐기 |
| `test_api.py` | API 스모크 테스트 (조회 전용) |
| `requirements.txt` | fastapi, uvicorn, asyncpg, psycopg, numpy |

`test_packets.py` 는 **수정 없이 전부 통과**한다 (인수인계 6-4).

---

## 2. 스키마 v1.3 → v1.4 변경

| 변경 | 이유 |
|---|---|
| `device` PK → **(dev_type, device_id)** | v1.3 은 `device_id` 단독 PK 라 **AOS 1 과 GFC 1 이 충돌** |
| `pair_config` (pair_id 1~20, gas_name) + `v_pair` | 쌍 고정 · 웹 "가스명 변경" |
| `run.dev_type`(=1) · `manual_mark.dev_type`(=1) | 복합 FK |
| `run.concentration`, `run.source_file` | 목록 화면의 "농도"·"파일명" |
| `run_one_active` 유니크 인덱스 | 한 AOS 에 running run 1개 |
| `heatmap_count` 트리거 | `received_heatmaps` 자동 집계 (중복 수신은 안 셈) |
| `tele_gfc` 컬럼 = GFC `0x32` 52 byte 와 1:1 | v1.3 컬럼이 실제 구조와 달랐음 |
| `sys_event` | 대시보드 "최근 이벤트" |
| `api_token.token_prefix`, `schema_meta` | 토큰 식별 / 버전 기록 |

나머지(D1~D16)는 설계안 그대로. 원본 설계 문서의 `schema.sql`(v1.3)은 건드리지 않았다.

---

## 3. API 요약 — 전체는 `http://192.168.0.6:8080/docs`

인증: `Authorization: Bearer <토큰>` (또는 `?token=`). 권한 read < control < admin.

| 화면 | 메서드 · 경로 | 권한 |
|---|---|---|
| 1 동작 상황표 | `GET /api/overview` — 20쌍 상태·집계 타일·콘솔·수신속도·이벤트 | read |
| | `WS /ws/live?token=` — 1초 푸시 (`{"detail":3}` 보내면 그 쌍 상세 추가) | read |
| | `GET /api/events` · `GET /api/control-actions` | read |
| 2-A Manual | `GET /api/pairs/{id}` · `GET /api/aos/{id}/current?sec=120` (V) · `GET /api/gfc/{id}/telemetry` | read |
| | `POST /api/aos/{id}/params` `{hv,frq,duty,cv,lf_frq,lf_volt,lf_on}` 부분 지정 | control |
| | `POST /api/gfc/{id}/mode·pump·times·auto-run` | control |
| | `PUT /api/pairs/{id}/gas-name` · `POST/GET /api/aos/{id}/marks` (현재 그래프 저장) | control |
| | `POST/GET/DELETE /api/devices/{aos|gfc}/{id}/lock` — 최근 조작자 표시 (30초). 동시 제어라 거부하지 않음 | control |
| 2-B 자동측정 | `POST /api/runs` · `/api/runs/{id}/pause·resume·finish·abort` · `POST /api/runs/{id}/heatmaps` | control |
| 3 Viewer | `GET /api/runs/{id}/overview?air_run_id=` — 10×10 Selection Grid 값 | read |
| | `GET /api/runs/{id}/heatmaps?sy=&sx=&value=raw|idf|air|diff&air_run_id=` — 16장/8장 | read |
| | `GET /api/runs/{id}/export.csv?...` · `GET /api/runs/{id}/missing` | read |
| 4 목록 | `GET /api/runs?kind=&gas=&device_id=&mode=&date_from=&q=&limit=&offset=` | read |
| | `GET /api/air-list?include_legacy=` · `GET /api/gases` · `GET /api/db/stats` | read |
| | `PATCH /api/runs/{id}` (라벨·가스명·메모) | control |
| 관리 | `/api/admin/users` · `/api/admin/tokens` | admin |

**지켜진 규칙**

- `value=idf` 는 **`air_run_id` 필수**. 없으면 400 (D13b — Air 자동 선택 없음)
- 실측(measured) ↔ 임포트(legacy) 데이터는 **서로 비교 거부**, 겹치는 조건 0 이면 거부 (D16)
- 웹 제어는 콘솔과 **같은 변환 코드**를 탄다 (가상 콘솔 세션). (2026-09-23.01) 콘솔과 웹 **동시 제어** — 서로 막지 않고 나중 명령이 적용된다. 콘솔 조작은 10초에 한 번 ctrl 이벤트로 남는다.
  장비 값이 바뀌면 0x44 → 웹(1초 tick), 같은 0x44 → 그 AOS 를 보는 콘솔에 0xC3 자동 전송
- (2026-09-23.01) **제어 명령 재전송**: 서버 → 브리지 제어 명령(AOS 0x40/0x41/0x42, GFC 0x30/0x34)은 ACK(seq 일치)를 받을 때까지
  0.4초 간격 최대 3회 재전송(`udp_server.PENDING`, `retry_loop`). 4회 모두 ACK 없으면 오류 이벤트 `tx_fail`.
  브리지가 STM32 적용 확인에 실패하면 AOS 이벤트 0x22(APPLY_VERIFY_FAIL) → 오류 이벤트 `apply_fail`
- run 진행 중에는 AOS 파라미터 변경 거부 (`denied_run`), GFC 는 허용
- 모든 제어는 `control_action` 에 감사 기록 (console / web, 결과 포함)
- DB 가 죽어도 UDP 중계·실시간 화면은 계속 동작, 10초마다 재접속

---

## 4. 예제 데이터 — Twin → Single 변환 적재

`DOC/example_data/*.dat` 16개를 `import_dat.py` 로 변환했다.

**변환 규칙**

1. Twin 파일 1개 → **run 2개**: `air_ref`(Is_Air_P) + `gas`(Is_Gas_P), `gas.air_ref_run_id` 로 연결
2. **Positive 만** 저장. `Is_*_N`(전부 0), `Idf`(계산값), Refinement/Fine/Trace 버림
3. **LFV 마지막 열(3.0 V) = NaN** — 구 F/W `line_all_buf[11][15]` overrun 으로 가짜값
   (`AOS_H753_V1_Single_프로그램_수정_사항_정리.md` 3-1). 16개 파일 **전부 손상 서명 100%** 확인
4. `data_origin='legacy_import'`, `payload_fmt=1`(f32). **Air List·비교 후보에서 기본 제외**,
   열람 시 `include_legacy=true`. 자기 짝 Air 로 Idf 조회는 가능
5. 가스명은 파일의 TargetGas 에서 `air`·숫자를 뺀 값 (`air_lavender_4_M` → `lavender_M`). 웹에서 수정 가능

**검증** — Idf 재계산(Gas−Air)이 원본 Idf 와 1e-6 이내 일치. best 위치(|Idf| 최대)가 원본
`Best_idxLFV/CV` 와 **18,703 / 18,705** 일치 (원본 best 가 가짜 열에 있던 heatmap 은 비교에서 제외,
불일치 2건은 Musk·banana 각 1장).

| 파일 | 모드 | heatmap | 가스명 |
|---|---|---|---|
| AIR-ACV_1_4x4_20260709 | full8 | 1600 | ACV |
| AIR_TEST_GAP025_Sample_20260708 | fast | 8 | TEST_GAP025 |
| Air_alcholol_4x4_20260907 | full8 | 1600 | alcholol |
| Air_alcholol_Sample_20260908 | fast | 8 | alcholol |
| Musk_air_1_4x4_20260803 | full8 | 1600 | Musk |
| Sasami_air_1_4x4_20260805 | full8 | 1600 | Sasami |
| acetone_air_2_4x4_20260806 | full8 | 1600 | acetone |
| acetone_air_2_Sample_20260808 | fast | 8 | acetone |
| air_air_4x4_20260908_131132 | full8 | **16 / 1600 (aborted)** | air |
| air_air_4x4_20260908_132419 | full8 | 1600 | air |
| air_banana_1_4x4_20260710 | full8 | 1600 | banana |
| air_lavender_4_M_4x4_20260727 | full8 | 1600 | lavender_M |
| air_pepament_1_4x4_20260711 | full8 | 1600 | pepament |
| air_sesami_1_4x4_20260710 | full8 | 1600 | sesami |
| fishSource_air_3_4x4_20260809 | full8 | 1600 | FishSource |
| 암모니아_air_3_4x4_20260813 | full8 | 1600 | 암모니아 |

적재 후 DB 약 **60 MB** (원본 `.dat` 139 MB). Single 텍스트 사본은 `DOC/example_data_single/`.

---

## 5. 설치 (서버에서 한 번)

```bash
# Mac
gts-push                                   # server/ 전체 전송 (gts/ db/ deploy/ 포함하도록 수정됨)
# 서버 (ssh gts@192.168.0.6)
cd ~/gts_udp_server
sudo bash db/install_db.sh                 # PostgreSQL + 스키마 v1.4 + .env
sudo bash deploy/install_server.sh         # 패키지 · 테스트 · gts-udp → gts_server.py · ufw 8080
venv/bin/python manage.py create-admin vdskim     # 관리자 토큰 (한 번만 보임)
venv/bin/python manage.py token home read --label 집      # 외부용 읽기 토큰
# Mac
gts-import                                 # DOC/example_data → 서버 DB
gts-api ; gts-docs                         # 확인
```

**외부 접속 (집)** — 공유기에서 **TCP 8080 → 192.168.0.6:8080** 포워딩.
접속 주소 `http://218.147.152.41:8080` (콘솔이 쓰는 외부 IP 기준).

⚠ **HTTP 라 토큰이 평문으로 인터넷을 지난다.** 외부에서는 `read` 토큰만 쓰고,
`control`·`admin` 토큰은 사내망에서만 쓰는 것을 권한다. 나중에 HTTPS(Caddy)로 올리는 건
포트만 바꾸면 되도록 앱은 그대로 둔다.

되돌리기 — `/etc/systemd/system/gts-udp.service.bak_*` 로 복구하거나 `ExecStart` 를
`udp_server.py` 로 바꾸면 예전 UDP 전용 서버로 돌아간다.

---

## 6. 확인이 필요한 것 ⚠

| # | 항목 | 지금 값 | 위치 |
|---|---|---|---|
| Q1 | AOS `0x45 STATUS` ADC → V 환산 | `3.3 / 4095` (12bit 가정) | `.env` `GTS_AOS_ADC_TO_V` |
| Q2 | 평균전류 표시 채널 | `gas_p` (Single = sample cell) | `GTS_AOS_CURRENT_CH` |
| Q3 | heatmap u16 raw → V 보정 (`calib_ver v1`) | `3.3 / 65535` | `GTS_CALIB_V1_SCALE` |
| Q4 | 자동측정 시작 명령(0x80) · heatmap UDP 업로드(0x86) | **미구현** — run 은 DB 기록만, 적재는 `POST /api/runs/{id}/heatmaps` | 브리지/서버 프로토콜 정의 필요 |
| Q5 | full8 현행 격자 Frq 값 | 200, 266.7, 333.3 … (설계문서 값) | `gts/runs.py DEFAULT_GRID` |

Q4 가 정해지면 `AosProtocol` 에 `0x86` 핸들러를 추가하고 `runs.ingest_heatmap()` 을 부르면 된다
(중복·순서역전은 `(run_id, cond_hash)` 로 이미 멱등).

---

## 7. 검증 결과 (개발 환경, 시뮬레이터)

- `test_packets.py` 전부 통과 (udp_server 훅 추가 후)
- `test_api.py` 10/10 통과
- 시뮬레이터(gfc_sim·aos_sim) + 가짜 콘솔로:
  웹 AOS 파라미터 4종 적용 → 브리지 패킷 확인 / 콘솔 조작 후에도 웹 명령 적용(동시 제어) /
  read 토큰 제어 403 / 범위 밖 400 / 오프라인 409 / run 중 파라미터 `denied_run`
- run 수명주기: 시작 → heatmap 20장(+중복 1장 dup) → pause → resume(남은 236) → finish →
  GFC 요약 + 10초 다운샘플 저장, 파티션 DROP
- DB 중단 상태로 기동: UDP·실시간 정상, DB API 만 503, 재접속 루프 동작

---

## 8. 다음 할 일

1. 서버 설치 (5절) 후 실장비로 `test_api.py`
2. **웹 프런트** — 디자인 5화면을 이 API 에 연결 (`server/web/` 에 두면 `/` 로 자동 서빙)
3. Q1~Q3 확인 후 `.env` 수정, Q4 프로토콜 정의
4. (선택) HTTPS 전환
