# GTS DB 설계 제안 v1.2

> 개정 이력
> - v1.1 — **Air 다중 선택**과 **장치 간 비교**를 같은 연산으로 일반화 (고정 FK 폐기)
> - v1.2 — **D11b·D13b·D14a·D14b·D15b·D16 전부 확정.** 미결 항목 없음.
>   특히 **run 중단 후 재개**(D14b)와 **Air 자동선택 금지**(D13b)가 스키마에 반영됨.
> - v1.3 — **기존 `.dat` 샘플은 개발 전 테스트용**(장비 전압 레인지가 그 뒤 변경)임을 반영.
>   `run.data_origin` 으로 구분해 **비교 후보에서 구조적으로 배제**.
>
> 근거: `GTS_DB_Design.md`, `DOC/FW_RS232_Protocol.md` 4장(0x80/0x86),
> `AOS_TWIN_File_Viewer/`(PCSW 데이터 모델), `DOC/GTS_인수인계_20260922.md` 6절
> 샘플: `air_lavender_4x4_...dat`(full8, 1600장), `Alcohol_air2_Sample_...dat`(fast, 8장)

---

## 0. 30초 요약

**데이터를 수명으로 두 계층에 가른다.**

| 계층 | 대상 | 수명 | 방법 |
|---|---|---|---|
| **A. 영구** | run / grid / heatmap / 비교결과 | 무기한 | 일반 테이블 |
| **B. 휘발** | GFC 1초 텔레메트리 | run 종료 시 | `UNLOGGED` + 파티션 `DROP` |
| **C. 비저장** | AOS Current 1초 | 수 분 | **DB 안 씀 — 서버 메모리 링** |

**핵심 5가지**

1. **Air 와 Target 은 각각 별도 run.** Air 는 **여러 개 공존**하고, 대시보드에서 **갈아끼운다**
2. `Idf = Target_P − Air_P` 는 **저장하지 않는다.** 조회 시 계산, 요약만 캐시
3. **"두 run 을 짝지어 뺀다" 를 단일 연산으로 일반화** → Idf 계산과 **장치 간 편차 비교가 같은 코드**
4. Heatmap 1장(176 point)은 **blob 1개** → full8 run 이 281,600행 대신 **1,600행**
5. 웹·콘솔이 같이 제어하므로 **장치 소유권 임대(lease)** 테이블이 필요

---

## 1. 확정된 운용 시나리오

설계는 스키마보다 이 시나리오에서 나온다. 결정 답변을 그대로 옮긴다.

### 1-1. 측정 (Full / 1hour / Fast)

- Air 측정과 Target 측정은 **서로 다른 시점의 독립 run**
- run 1건 = heatmap N장, 각 장은 **Positive 1채널(`Is_P`)** 만
- **이 구간에는 AOS Current 1초 데이터가 서버로 올라오지 않는다** ← D11
- refinement(FineTrace) **동작 자체가 삭제**되므로 대응 테이블 없음

### 1-2. 메뉴얼 모드 (전압 조절 등 장치 상태 파악) ← D11

- AOS Current 1초가 올라오고, **대시보드에서 실시간 전류 변화**를 본다
- 측정 run 과 **아무 관계가 없다**. 사후 조회 대상도 아니다
- → **DB 에 넣지 않는다.** `udp_server.py` 의 `deque` + WebSocket 푸시로 끝낸다

### 1-3. Heatmap 열람 ← D12, D13

- Air 는 **여러 개 존재**한다. 사용자가 **Air List 에서 임의로 골라** 대시보드 heatmap 을 그린다
- **사용자 선택이 필수다.** 시스템이 Air 를 자동으로 고르거나 기본값을 적용하지 않는다 ← D13b
- 같은 Target 가스를 **두 장치로 측정해 편차를 비교**한다
- → Air 선택은 **run 에 못 박는 값이 아니라 조회 파라미터**다

### 1-4. 제어 ← D14

- **웹에서 측정 제어까지** 한다. 콘솔은 개발 초기 **2~3대** 운영
- 콘솔에는 이미 로컬 조작 우선 구간(`GTS_LOCAL_HOLD_MS` 1200ms, 인수인계 E9)이 있다
- **콘솔 조작이 무조건 선점한다** ← D14a

### 1-6. 중단과 재개 ← D14b

- 측정 run 은 **중단할 수 있고, 중단하면 이어서 재실행할 수 있다**
- 8시간 Full 을 실수로 끊어도 **처음부터 다시 하지 않는다** — 남은 조건만 이어서 측정
- → `status='paused'` 와 **남은 `cond_idx` 목록**이 스키마의 요구사항이 된다.
  이미 `fn_missing_cond()` 가 그 목록을 준다

### 1-5. 망 ← D15

- 사내 전용이지만 **실험실 망이 2개로 분리**되어 있다 (인수인계 4절: 브리지는
  `192.168.86.0/24` Nest NAT 뒤, 서버는 `192.168.0.6`, 콘솔은 헤어핀 NAT)
- → **PostgreSQL 포트는 어느 망에도 열지 않는다.** 접근은 API 한 곳으로만, 토큰 인증

---

## 2. 그래서 v1 에서 무엇을 바꿨나

| v1 | v1.1 | 이유 |
|---|---|---|
| `run.air_ref_run_id` 가 **유일한** Air 지정 | **기본값일 뿐**, 조회 때 임의 Air 지정 가능 | D12 — Air 를 갈아끼움 |
| `heatmap_idf` (gas run 당 1벌) | **`pair_stat`** — `(left_run, right_run)` 쌍 단위 | Air 가 여러 개 → 쌍마다 결과가 다름 |
| Idf 전용 계산 | **`pair` 연산으로 일반화** (`idf` / `device_diff`) | D12 — 장치 간 비교가 같은 뺄셈 |
| `tele_aos` 파티션 테이블 | **삭제.** 메모리 링으로 | D11 — 측정 중엔 올라오지도 않음 |
| `console_action` | **`control_action`** (console / web / api) + `device_lock` | D14 — 웹도 제어 |
| 인증 미정 | `app_user` / `api_token` | D15 — 망 2개 |
| `heatmap` PK = `(run_id, seq_no)` | PK = **`(run_id, cond_idx)`**, `seq_no` 는 속성 | 논리적 동일성은 조건이지 도착순서가 아님 |
| (v1.1) `run.is_default_air` | **제거** | D13b — 자동 선택이 없으므로 기본값도 없다 |
| (v1.1) `status` 4종 | **`paused` 추가** (5종) | D14b — 중단 후 재개 |

---

## 3. 데이터가 얼마나 작아지는가

Heatmap 1장 = `LFV 16 × CV 11 = 176 point`

| 저장 방식 | 1장 | full8 run(1,600장) |
|---|---|---|
| PCSW `.dat` 텍스트 5채널 | ≈ 7.2 KB | **11.3 MB** (실측) |
| f32 1채널 | 704 B | 1.07 MB |
| **u16 ADC raw 1채널 ★채택** | **352 B** | **0.54 MB** |

> **부수 효과 — UDP 단편화가 사라진다.**
> RS232 `0x86` 은 기본 격자(51×21×8B)에서 8,576 byte 라 UDP 로는 IP 단편화가 필수였다.
> `16×11×2B = 352 byte` 는 헤더를 붙여도 400 byte 미만 → **1 datagram 에 heatmap 1장.**
> 8시간 동안 1,600장이 올라오는 Full 모드에서 재조립·부분손실 처리가 통째로 불필요해진다.

---

## 4. 설계 결정 (D 시리즈)

| # | 결정 | 근거 |
|---|---|---|
| **D2** | **PostgreSQL 단일 인스턴스.** TimescaleDB·SQLite 아님 | 보존정책이 `DROP PARTITION` 으로 끝나 Timescale 이 할 일이 없음. SQLite 는 UDP writer + FastAPI **2 프로세스 동시 쓰기**에서 락 경합 |
| **D3** | Heatmap 값은 **`bytea` blob 1개** | 조회 단위가 항상 "1장 통째". 352 B 는 TOAST 임계(≈2 KB) 아래라 **inline 저장** |
| **D4** | **ADC raw `uint16` 우선 저장**, 보정은 조회 시 | `0x86` 이 raw 를 보냄 → 서버는 **파싱 없이 memcpy**. 보정식이 바뀌어도 재계산 가능 (`run.calib_ver`) |
| **D5** | `payload_fmt` 로 raw/float 혼재 허용 | 기존 `.dat` 임포트는 float 뿐 |
| **D6** | 매칭 키 2종: **`cond_idx`**(같은 격자, 배열 첨자) + **`cond_hash`**(다른 격자) | 장치 비교는 같은 격자 → 첨자. Air 가 더 촘촘/성길 수 있음 → 해시 |
| **D7** | Idf 배열 저장 안 함, **`pair_stat` 에 요약 배열만** | 목록 UI 는 `best_idf` 1,600개만 필요 → **6.4 KB/쌍** |
| **D8** | Grid 정의는 `grid_def` 에 **해시 중복제거** | full8 격자 리스트가 run 마다 반복될 이유 없음 |
| **D9** | 조건값(HV/Frq/Duty/LFF)은 heatmap 행에 **비정규화 중복** | 배열 조인 없이 조건 검색. 1,600행×16B = 26 KB |
| **D10** | 텔레메트리 테이블은 **`UNLOGGED`** | 어차피 버릴 데이터. WAL 미사용 |
| **D1** | GFC **10초 평균 다운샘플 보존** (run 당 ≈230 KB) | ✅ 채택 — 원본 1초는 버리되 사후 추적 가능 |
| **D11** | AOS Current 는 **DB 미사용**, 서버 메모리 `deque(600)` + WebSocket | ✅ 메뉴얼 모드 전용 실시간 용도. 측정 중엔 올라오지도 않음 |
| **D11b** | 메뉴얼 모드 **`manual_mark`** (운영자 스냅샷) 채택 | ✅ 좋은 동작점을 찾았을 때 영구 기록 |
| **D12** | Air 는 **다중**. 실제 계산은 **조회 시 지정한 Air** 로 | ✅ 대시보드에서 갈아끼움 |
| **D12b** | **장치 간 비교 = 같은 pair 연산** (`pair_kind='device_diff'`) | ✅ 코드·캐시·API 를 하나로 |
| **D13** | **Air List** 뷰 제공. `run.label`, `run.ambient` 추가 | ✅ 고를 근거(라벨·일시·장치·환경)가 보여야 함 |
| **D13b** | **자동 추천·기본값 없음. 사용자 선택 필수** → `is_default_air` **제거**, API 는 `air_run_id` 를 **필수 파라미터**로 | ✅ 시스템이 몰래 고르지 않는다 |
| **D14** | **`device_lock` 소유권 임대** + `control_action` 감사로그 | ✅ 웹·콘솔 2~3대 동시 제어 |
| **D14a** | **콘솔 무조건 선점** (락 정책 4개 항목 5-8 참조) | ✅ 장비 앞의 사람이 이긴다 |
| **D14b** | **run 중단 → 재개 지원.** `status='paused'`, `resume_count`, `fn_missing_cond()` | ✅ 8시간을 처음부터 다시 하지 않는다 |
| **D15** | **DB 포트 비공개.** API 토큰 인증, `device.net_zone` 기록 | ✅ 실험실 망 2개 분리 |
| **D15b** | 토큰 등급 **read / control / admin 3단계.** 장치별 권한은 두지 않음 | ✅ 장비 수가 적고 사내 전용 — 과잉설계 회피 |
| **D15b** 보강 | 3단계 확정, **장치별 권한 없음** | ✅ 확정 |
| **D16** | 비교는 **`data_origin='measured'` · 같은 `grid_id`** 끼리. 기존 샘플은 `legacy_import` 로 배제 | ✅ 기존 샘플은 **개발 전 테스트용**이라 비교 대상이 아님 |

---

## 5. 스키마

### 5-1. 전체 그림

```
[A. 영구]
  device ──┬─ run ──┬─ heatmap ─┐
           │        └─ run_event│
           └─ grid_def          │
                                │
            pair_stat (left_run, right_run) ──┘
              pair_kind = 'idf'         : Target − Air   (Air 여러 개 = 쌍 여러 개)
                        = 'device_diff' : 장치A − 장치B

           run_telemetry_summary   (버리기 전 요약 + 10초 다운샘플)
           manual_mark             (메뉴얼 모드 운영자 스냅샷)

[B. 휘발]
  tele_gfc  PARTITION BY LIST (run_id)  → run 종료 시 DROP

[C. 비저장]
  AOS Current → udp_server.py 의 deque(600) → WebSocket

[운영]
  app_user · api_token · device_lock · control_action · ingest_error
```

### 5-2. `grid_def` — 격자 정의 (해시 중복제거)

`grid_id` PK / `grid_hash` UNIQUE / `mode`(fast·hour1·full8) /
`no_sx,no_sy,no_hx,no_hy` / `no_lfv=16, no_cv=11` /
`hv_list, frq_list, duty_list, lff_list, lfv_list, cv_list` real[] /
`param_sets` jsonb **(fast 전용 8조합)** / `heatmap_count`(1600·256·8)

> fast 모드는 격자가 아니라 **임의 8조합**이다(`Alcohol_air2` 샘플의 `=== Parameter Sets ===`).
> 격자 컬럼을 NULL 로 두고 `param_sets` 로 표현하면 같은 테이블에 들어간다.

### 5-3. `run` — 측정 1회

| 컬럼 | 비고 |
|---|---|
| `run_id` PK / `run_uid` uuid | 내부키 / 외부노출키 |
| `kind` | **`air_ref`** \| **`gas`** ← Single 변경의 핵심 |
| `mode`, `grid_id`, `device_id` | |
| `air_ref_run_id` | **"마지막에 사용한 Air" 기록일 뿐** (D13b). 자동 적용하지 않는다 |
| **`label`** | Air List 에 표시할 짧은 이름 (D13) |
| **`ambient`** jsonb | 온·습도·필터상태 등 — **Air 를 고를 때의 판단 근거** (D13) |
| `target_gas`, `comment`, `operator` | |
| `filter_type, metric_type, wave_type, duty_mode, lff_mode` | `.dat` 헤더 |
| `wait_delay_ms, avg_count, lfv_settle_ms, cv_settle_ms` | `.dat` 헤더 |
| `calib_ver` | ADC→전류 보정식 버전 |
| **`control_source`**, **`requested_by`** | 웹/콘솔 중 누가 시작했나 (D14) |
| `status` | `running` \| **`paused`** \| `done` \| `aborted` \| `failed` (D14b) |
| `started_at`, `ended_at` | |
| **`resume_count`, `last_paused_at`, `last_resumed_at`** | 중단/재개 추적 (D14b) |
| `expected_heatmaps`, `received_heatmaps` | 결손 감지 · 재개 시 남은 양 계산 |

### 5-4. `heatmap` — 본체

| 컬럼 | 비고 |
|---|---|
| `run_id`, **`cond_idx`** | **PK.** `cond_idx = ((sy·no_sx + sx)·no_hy + hy)·no_hx + hx`, fast 는 `param_no−1` |
| `seq_no` | 업로드 도착 순번 (속성) |
| `cond_hash` | `(hv,frq,duty,lff,lfv_list,cv_list)` 해시 — **격자가 다른 run 과 매칭** |
| `hv, frq, duty, lff` | 비정규화 (D9) |
| `sy, sx, hy, hx`, `param_no` | |
| `no_lfv, no_cv` | |
| **`payload`** bytea | **iy=CV(외) × ix=LFV(내) row-major — `0x86` wire order 그대로** |
| `payload_fmt` | 0=`u16` ADC raw, 1=`f32` 전류 |
| `best_idx_lfv, best_idx_cv` | 장치 보고값 |
| `raw_min, raw_max, raw_mean` | 서버 계산 요약 |
| `is_measured`, `measured_at`, `recv_at`, `proc_ms` | |

```
PRIMARY KEY (run_id, cond_idx)     -- 논리적 동일성
UNIQUE      (run_id, cond_hash)    -- UDP 중복 수신 멱등 처리
INDEX       (cond_hash)            -- 격자가 다른 run 과 교차 조회
INDEX       (hv, frq, duty, lff)   -- 조건 검색
```

> **UDP 중복·순서역전은 `ON CONFLICT (run_id, cond_hash) DO NOTHING` 으로 끝난다.**
> 도착 순서가 아니라 조건으로 식별하므로 순서가 뒤바뀌어도 무해하다.

### 5-5. `pair_stat` — 두 run 을 짝지은 결과 요약 ★ v1.1 핵심

Air 가 여러 개이므로 **"Target run 하나"의 Idf 는 정해지지 않는다.**
정해지는 것은 **(Target run, Air run) 쌍**의 Idf 다. 장치 간 편차도 (runA, runB) 쌍이다.
**둘은 같은 뺄셈이므로 테이블 하나로 받는다.**

| 컬럼 | 비고 |
|---|---|
| `left_run_id, right_run_id` | PK 일부. `idf` 면 (Target, Air), `device_diff` 면 (장치A, 장치B) |
| `pair_kind` | `idf` \| `device_diff` |
| `calib_ver` | 보정식이 바뀌면 캐시 무효 |
| **`best_val_arr`** bytea | **f32 × heatmap_count** — 1600장이면 **6.4 KB** |
| `best_lfv_arr`, `best_cv_arr` | u8 × count — 각 1.6 KB |
| `val_min, val_max`, `best_cond_idx`, `matched, missing` | 전체 요약 |
| `computed_at` | |

배열 첨자는 `cond_idx`. 즉 **`best_val_arr[cond_idx]`** 가 그 조건의 best Idf 다.
10×10 섹션 개요를 색으로 칠하는 데 필요한 전부가 **한 행 10 KB** 안에 들어온다.

> 쌍 100개를 캐시해도 1 MB. **Air 를 자유롭게 갈아끼워도 비용이 사실상 없다.**
> 격자가 다른 쌍은 배열을 쓸 수 없으므로 `cond_hash` 조인으로 계산하고 캐시는 생략한다.

### 5-6. 계층 B — GFC 1초 (run 종료 시 삭제)

```
UNLOGGED TABLE tele_gfc (...) PARTITION BY LIST (run_id);
```

- run 시작 → `tele_gfc_r<run_id>` 파티션 **생성**
- run 종료 → ① 요약 + **10초 평균 다운샘플**(D1) 을 `run_telemetry_summary` 에 기록
             ② `DROP TABLE tele_gfc_r<run_id>` **(즉시, VACUUM 부담 0)**
- run 밖 데이터는 `DEFAULT` 파티션 → 주기적 `TRUNCATE`

컬럼은 GFC `0x32 SENSOR_DATA` 52 byte 그대로.
8시간 run 원본 = 28,800행 ≈ 2.3 MB → **종료 후 0**, 다운샘플 2,880행 ≈ 230 KB 만 남음.

### 5-7. 계층 C — AOS Current (DB 미사용) ← D11

메뉴얼 모드 전용 실시간 표시이므로 **테이블을 만들지 않는다.**

```
AOS 0x45 STATUS (1초) ──► deque(maxlen=600)  # 10분
                            └──► WebSocket ──► 대시보드 실시간 그래프
```

- DB 부하 0, 파티션 관리 0
- 서버 재시작 시 그래프가 비워지지만, **운영자가 보고 있는 중인 값**이라 손실 의미 없음
- **채택 (D11b)** — 좋은 동작점을 찾았을 때 운영자가 "기록" 을 누르면
  그 순간의 파라미터 + 전류를 **`manual_mark`** 에 영구 저장. 행당 수십 byte 수준

### 5-8. 제어 — `device_lock` / `control_action` ← D14

웹과 콘솔 2~3대가 같은 장치를 만지므로 **소유권 임대**가 필요하다.

| `device_lock` | |
|---|---|
| `device_id` PK | |
| `owner_kind` | `console` \| `web` |
| `owner_id`, `owner_label` | 콘솔 ID / 웹 세션 |
| `acquired_at`, `expires_at`, `heartbeat_at` | **임대**이므로 끊기면 자동 만료 |
| `preempted_by`, `preempted_at` | 선점 이력 |

**정책 (D14a — 확정)**

1. 조작하려면 락을 잡는다. 임대 30초, 하트비트로 연장
2. **콘솔 조작은 무조건 선점(preempt)** — 인수인계 E9 의 로컬 우선 원칙과 정합.
   장비 앞의 사람이 이긴다
3. 웹은 선점당하면 UI 에 즉시 표시. 값이 되돌아가는 증상(E13)과 구분되어야 함
4. **측정 run 진행 중에는 파라미터 변경 락을 웹·콘솔 모두 거부.**
   단 **run 중단(pause/abort)은 예외로 허용**한다 (D14b)

`control_action` 은 `console_action` 을 일반화한 감사 로그다:
`source`(console/web/api), `actor`(app_user), `dev_type/dev_id`, `cmd`, `detail`, `result`.

### 5-9. 인증 ← D15

실험실 망이 2개로 갈려 있고 콘솔은 헤어핀 NAT 로 외부 IP 를 거친다.

- **PostgreSQL 포트(5432)는 어느 망에도 열지 않는다.** 서버 로컬에서만 접속
- 접근은 **FastAPI 한 곳**으로만. `app_user` + `api_token`(해시 저장, 만료·폐기 가능)
- 읽기 전용 토큰 / 제어 토큰을 분리 → 대시보드만 볼 사람에게 제어권을 주지 않음
- `device.net_zone` 에 어느 망 소속인지 기록. NAT 때문에 **두 브리지가 같은 IP 로 보이므로**
  (인수인계 4절) IP 가 아니라 `device_id` 로 구분한다는 점을 스키마에 못 박음
- `device.last_peer inet` 은 진단용으로만

---

## 6. 인제스트 경로 (인수인계 6-4 준수)

```
UDP recv ──► parse ──► asyncio.Queue ──► writer task ──► PostgreSQL
                              │
   수신 경로는 절대 블로킹하지 않는다 ──┘

AOS Current ──► deque (DB 경유 없음) ──► WebSocket
```

| 항목 | 방식 |
|---|---|
| Heatmap | 도착 즉시 `INSERT ... ON CONFLICT DO NOTHING` (평균 17.4초에 1건 — 부하 없음) |
| GFC 1초 | 1초 버퍼 → **5초마다 `COPY`** (`executemany` 아님) |
| AOS Current | **DB 미경유** |
| 큐 포화 | **GFC 텔레메트리를 먼저 버린다. Heatmap 은 절대 버리지 않는다** |
| 파티션 관리 | `pg_cron` 대신 **서버 내 asyncio housekeeping 태스크** (의존성 최소화) |

**결손 복구** — run 종료 시 `expected_heatmaps` vs 실제 행수 비교 → 누락 `cond_idx` 목록 산출
→ 해당 조건만 재측정 요청. UDP 는 재전송 보장이 없으므로 **장치 측 `0x86` + 서버 ACK,
미수신 3회 재시도** 패턴을 유지할 것 (RS232 `RECEIVED_OK` 관행과 동일).

---

## 7. 용량 산정

| 항목 | 크기 |
|---|---|
| heatmap 1행 | 352 B + ≈110 B ≈ **460 B** |
| full8 run 1회 | 1,600행 ≈ **0.75 MB** |
| fast run 1회 | 8행 ≈ **4 KB** |
| Air run 1회 | full8 과 동일 **0.75 MB** — **여러 Target run 이 공유** |
| **`pair_stat` 1쌍** | **≈ 10 KB** — Air 100개를 갈아끼워도 1 MB |
| 하루 3 run | ≈ **2.3 MB** |
| **연간** | ≈ **0.8 GB** + 인덱스 |
| tele_gfc (run 보관 중 최대) | 2.3 MB → **종료 시 0**, 다운샘플 230 KB 잔존 |
| AOS Current | **0** (메모리) |

---

## 8. `.dat` → DB 마이그레이션 매핑

기존 PCSW 파일이 손실 없이 들어가는지가 스키마 검증이다.

| `.dat` | → DB |
|---|---|
| `TargetGas / Comment / SaveTime` | `run.target_gas / comment / started_at` |
| `FilterType / MetricType / WaveType / DutyMode / LFFMode` | `run.*` 동명 컬럼 |
| `Wait_Delay_ms / Avg_Count / LFV_Settle_ms / CV_Settle_ms` | `run.*` |
| `=== Grid Definition ===` 6개 리스트 | `grid_def` (해시 중복제거) |
| `=== Parameter Sets ===` (fast) | `grid_def.param_sets` |
| `Coarse Section [sY,sX] → Heatmap [hY,hX]` | `heatmap.sy/sx/hy/hx` → `cond_idx` |
| **`Is_Gas_P` 11×16** | **`kind='gas'` run 의 `heatmap.payload`** (fmt=1) |
| **`Is_Air_P` 11×16** | **`kind='air_ref'` run 의 `heatmap.payload`** (쌍으로 생성) |
| `Is_Air_N` / `Is_Gas_N` | **버림** — 두 샘플 모두 **비영값 0개**로 실측 확인 |
| `Idf` | **버림** (계산값) → `pair_stat` 으로 재생성 |
| `Best_idxLFV / Best_idxCV / Best_Idf` | `heatmap.best_idx_*` / `pair_stat.best_*_arr` |
| `=== Refinement Result ===` / `Fine Section` / `FineTrace` | **버림** — refinement **동작 자체를 삭제** |
| `=== Summary ===` | `run_event(kind='done', detail)` |

> `.dat` 1개 → **run 2개(gas + air_ref) + pair_stat 1행**으로 분해된다.
> **이 구조가 그대로 Single 시스템의 실제 운용 형태다** — 마이그레이션 경로가 곧 설계 검증이다.
>
> ⚠️ **임포트된 run 은 전부 `data_origin='legacy_import'`, `payload_fmt=1`(f32) 이다.**
> 개발 전 테스트 데이터이므로 **비교 후보·Air List 에 나타나지 않는다** (D16).
> 임포트의 목적은 측정 이력 축적이 아니라 **임포터·뷰어 검증**이다.

### 검증 결과 (샘플 실측)

| | full8 | fast |
|---|---|---|
| heatmap 장수 | 1,600 | 8 |
| 채널 형상 | 전부 11×16 일관 | 동일 |
| `Is_Air_N`/`Is_Gas_N` 비영값 | **0개** | **0개** |
| `cond_hash` 유일성 | **1600/1600 충돌 없음** | — |
| 원본 → u16 | 11.3 MB → **0.54 MB** | 51 KB → 2.8 KB |

---

## 9. 중단·재개 절차 (D14b)

8시간 Full run 을 실수로 끊어도 **처음부터 다시 하지 않는다.**

```
run(status='running')
   │
   ├─ 중단 요청 (웹 또는 콘솔)
   │    ① status = 'paused',  last_paused_at = now()
   │    ② run_event(kind='abort', detail={by, reason})
   │    ③ tele_gfc 파티션은 DROP 하지 않는다  ← 재개 시 이어서 쓴다
   │
   └─ 재개 요청
        ① SELECT * FROM fn_missing_cond(run_id)   -- 남은 조건 목록
        ② 그 조건만 장치에 지시
        ③ status = 'running', resume_count += 1, last_resumed_at = now()
        ④ run_event(kind='resume', detail={remaining:N})
```

**설계상 이게 가능한 이유** — `heatmap` 의 PK 가 `(run_id, cond_idx)` 이고 도착 순서
(`seq_no`) 가 아니기 때문이다. 이미 받은 조건은 `ON CONFLICT DO NOTHING` 으로 걸러지므로
**재개 시 중복 측정이 들어와도 무해하다.**

| 상태 | 뜻 |
|---|---|
| `paused` | 중단했으나 **재개 예정**. 데이터·파티션 모두 보존 |
| `aborted` | **영구 중단.** 재개하지 않음 → 텔레메트리 요약 후 파티션 DROP |

> **주의** — `expected_heatmaps` 는 재개해도 바뀌지 않는다. 진행률은 항상
> `실제 행수 / expected` 다. `v_run_progress` 가 그대로 쓰인다.

---

## 10. 부록 — D15b·D16 이 무엇을 묻는 것이었나

질문이 불친절했던 두 항목을 풀어서 남긴다.

### 10-1. 토큰 등급 (D15b)

"토큰" 은 웹 대시보드나 API 에 접속할 때 쓰는 **비밀 열쇠 문자열**이다.
누가 어디까지 할 수 있는지를 이 문자열에 등급으로 붙인다.

| 등급 | 할 수 있는 일 |
|---|---|
| `read` | 조회만 — 측정 결과, heatmap, 실시간 전류 그래프 |
| `control` | + 장치 파라미터 변경, 측정 시작·중단 |
| `admin` | + 사용자 계정·토큰 발급/폐기 |

질문의 뒷부분은 **장치별 개별 권한**("A 는 AOS #1 만 제어, #2 는 조회만")까지
둘지였다. → **확정: 3단계만 두고 장치별 권한은 두지 않는다.** 장비 수가 적고 사내 전용이라 과잉이다.
나중에 필요하면 `token_device_grant` 테이블 하나로 확장한다.

토큰 원문은 저장하지 않고 **해시만** 넣는다. 만료·폐기가 가능하다.

### 10-2. 비교 대상의 범위 (D16)

원래 질문은 "격자가 다른 두 run 을 어떻게 비교할까" 였는데, **전제가 틀렸다.**

`air_lavender` 샘플의 `HV_List`(80~220)가 현행 정의(45~180)와 다른 것은
**장비 전압 레인지가 그 뒤로 바뀌었기 때문**이다. 즉 "격자가 수시로 다르다" 는 뜻이 아니라
**"그 데이터는 구세대다"** 는 뜻이다.

**확정된 사실**

- 지금 있는 `.dat` 샘플 10~20개는 **장비 개발 전 테스트용**이다
- 앞으로 새로 측정될 데이터와 **비교하지 않는다**
- 앞으로의 데이터는 같은 세대 장비·같은 격자 정의를 쓰므로 **격자 불일치는 예외 상황**이다

**스키마 반영**

`run.data_origin` (`measured` / `legacy_import`) 을 두고, 비교 후보·Air List 에서
`legacy_import` 를 **구조적으로 배제**한다. 사람이 실수로 고를 여지를 없앤다.

| | |
|---|---|
| `measured` | 실제 장비에서 측정된 데이터. 비교·Air 선택 대상 |
| `legacy_import` | 개발 전 `.dat` 샘플. **열람만 가능**, 비교 후보에 뜨지 않음 |

그래도 안전장치는 남긴다 — `fn_pair_overlap()` 으로 겹치는 조건 수를 먼저 세고,
**0 이면 비교를 막고 이유를 표시**한다. 비교 후보 목록은 `fn_compare_candidates()` 가
**같은 `grid_id` · 같은 가스 · `measured`** 만 돌려준다.

> 기존 샘플을 임포트하는 목적은 **임포터와 뷰어를 검증**하는 것이지
> 측정 이력을 쌓는 게 아니다. `payload_fmt=1`(f32) 이 곧 legacy 경로라는 점도 맞아떨어진다.

---

## 11. 다음 단계

1. `schema.sql` 을 개발 DB 에 적용
2. `.dat` 임포터 작성 → 샘플 2개 투입 → **PCSW 뷰어와 동일한 Idf 가 나오는지 대조** (회귀 기준점)
3. `udp_server.py` 에 큐 + writer + deque 삽입 (**`test_packets.py` 먼저 통과시킬 것**)
4. FastAPI 조회/제어 API → 웹 대시보드
