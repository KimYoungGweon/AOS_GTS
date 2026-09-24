-- =====================================================================
-- GTS DB Schema v1.3  (PostgreSQL 14+)
-- 근거: GTS_DB_Design_Proposal_v1.md  (결정 D1, D11~D16 전부 확정)
--
--   계층 A = 영구 (측정결과 · 비교결과)
--   계층 B = 휘발 (GFC 1초 → run 종료 시 DROP)
--   계층 C = 비저장 (AOS Current → 서버 메모리 deque, 테이블 없음)
-- =====================================================================
CREATE EXTENSION IF NOT EXISTS pgcrypto;   -- gen_random_uuid(), crypt()

-- =====================================================================
-- 계층 A : 영구
-- =====================================================================

CREATE TABLE device (
    device_id     smallint    PRIMARY KEY,          -- UDP DID
    device_type   smallint    NOT NULL,             -- 1=AOS, 2=GFC
    name          text        NOT NULL,
    serial        text,
    net_zone      text,        -- D15: 실험실 망 2개 분리. 'lab_a' / 'lab_b' 등
    last_peer     inet,        -- 진단용. NAT 때문에 두 브리지가 같은 IP 로 보임 →
                               -- 식별은 반드시 device_id 로 한다 (인수인계 4절)
    note          text,
    first_seen    timestamptz NOT NULL DEFAULT now(),
    last_seen     timestamptz
);

-- ---------------------------------------------------------------------
-- 격자 정의 : grid_hash 로 중복제거 (D8)
-- ---------------------------------------------------------------------
CREATE TABLE grid_def (
    grid_id       bigserial   PRIMARY KEY,
    grid_hash     bytea       NOT NULL UNIQUE,      -- sha256(정규화 JSON)
    mode          text        NOT NULL CHECK (mode IN ('fast','hour1','full8')),

    no_sx         smallint,                          -- fast 모드는 NULL
    no_sy         smallint,
    no_hx         smallint,
    no_hy         smallint,
    no_lfv        smallint    NOT NULL,              -- 16
    no_cv         smallint    NOT NULL,              -- 11

    hv_list       real[],
    frq_list      real[],
    duty_list     real[],
    lff_list      real[],
    lfv_list      real[]      NOT NULL,
    cv_list       real[]      NOT NULL,

    param_sets    jsonb,                             -- fast 전용: [{no,hv,frq,duty,lff}]
    heatmap_count int         NOT NULL,              -- 1600 / 256 / 8
    created_at    timestamptz NOT NULL DEFAULT now(),

    CONSTRAINT grid_shape CHECK (
        (mode = 'fast'  AND param_sets IS NOT NULL)
     OR (mode <> 'fast' AND no_sx IS NOT NULL AND no_sy IS NOT NULL
                        AND no_hx IS NOT NULL AND no_hy IS NOT NULL)
    )
);

-- ---------------------------------------------------------------------
-- 측정 run.  Air 와 Target 은 각각 별도 run 이고, Air 는 여러 개 공존한다 (D12)
-- ---------------------------------------------------------------------
CREATE TYPE run_kind   AS ENUM ('air_ref','gas');
-- D14b: 중단하면 이어서 재실행할 수 있다.
--   paused  = 중단했으나 재개 예정 (남은 cond_idx 만 이어서 측정)
--   aborted = 영구 중단 (재개하지 않음)
CREATE TYPE run_status AS ENUM ('running','paused','done','aborted','failed');
CREATE TYPE ctl_source AS ENUM ('console','web','api','import');

-- D16(확정): 지금 있는 .dat 샘플 10~20개는 장비 개발 전 테스트용이고,
--   장비 전압 레인지가 그 뒤로 바뀌었다. 앞으로 측정될 데이터와 비교하지 않는다.
--   → 비교 후보 목록에서 구조적으로 배제하기 위한 구분값.
CREATE TYPE data_origin_t AS ENUM ('measured','legacy_import');

CREATE TABLE run (
    run_id            bigserial   PRIMARY KEY,
    run_uid           uuid        NOT NULL UNIQUE DEFAULT gen_random_uuid(),
    device_id         smallint    NOT NULL REFERENCES device,
    kind              run_kind    NOT NULL,
    mode              text        NOT NULL CHECK (mode IN ('fast','hour1','full8')),
    grid_id           bigint      NOT NULL REFERENCES grid_def,

    -- D12/D13b: Air 는 사용자가 매번 직접 고른다. 자동 선택·기본값 적용 없음.
    --   이 컬럼은 "마지막으로 사용한 Air" 기록일 뿐이며,
    --   API 는 조회 시 air_run_id 를 반드시 파라미터로 받아야 한다.
    air_ref_run_id    bigint      REFERENCES run(run_id),

    -- D13: Air List 에서 사용자가 고를 근거
    label             text,                          -- 짧은 표시명
    ambient           jsonb,                         -- {"temp_c":.., "rh":.., "filter":".."}

    target_gas        text,
    comment           text,
    operator          text,

    -- .dat 헤더 유래 측정 조건
    filter_type       smallint,
    metric_type       smallint,
    wave_type         smallint,
    duty_mode         smallint,
    lff_mode          smallint,
    wait_delay_ms     int,
    avg_count         int,
    lfv_settle_ms     int,
    cv_settle_ms      int,

    calib_ver         text        NOT NULL DEFAULT 'v1',  -- ADC raw → 전류 보정식 (D4)

    -- D16: 'legacy_import' 는 개발 전 테스트 데이터. 비교 후보에서 제외된다.
    data_origin       data_origin_t NOT NULL DEFAULT 'measured',

    -- D14: 누가 시작시켰나
    control_source    ctl_source  NOT NULL DEFAULT 'console',
    requested_by      text,

    status            run_status  NOT NULL DEFAULT 'running',
    started_at        timestamptz NOT NULL DEFAULT now(),
    ended_at          timestamptz,

    expected_heatmaps int         NOT NULL,
    received_heatmaps int         NOT NULL DEFAULT 0,

    -- D14b: 중단/재개 추적. 구간별 상세는 run_event 에 남는다.
    resume_count      int         NOT NULL DEFAULT 0,
    last_paused_at    timestamptz,
    last_resumed_at   timestamptz,

    CONSTRAINT air_has_no_ref CHECK (kind = 'gas' OR air_ref_run_id IS NULL),
    CONSTRAINT no_self_ref    CHECK (air_ref_run_id IS DISTINCT FROM run_id)
);
CREATE INDEX run_device_time ON run (device_id, started_at DESC);
CREATE INDEX run_kind_time   ON run (kind, started_at DESC);
CREATE INDEX run_air_ref     ON run (air_ref_run_id) WHERE air_ref_run_id IS NOT NULL;
CREATE INDEX run_gas_lookup  ON run (target_gas, device_id, started_at DESC)
                              WHERE kind = 'gas';
-- D13b: 기본 Air(자동 선택) 개념이 없으므로 관련 인덱스도 없다.

-- ---------------------------------------------------------------------
-- Heatmap 본체.  1장 = 1행 = blob 1개 (D3)
--   payload : iy=CV(외) x ix=LFV(내) row-major — 0x86 wire order 그대로
--   payload_fmt 0 = uint16 ADC raw (352B, 실측 경로)
--               1 = float32 전류   (704B, .dat 임포트 경로)
--   cond_idx : ((sy*no_sx + sx)*no_hy + hy)*no_hx + hx   (fast 는 param_no-1)
--              → pair_stat 의 배열 첨자로 그대로 쓰인다 (D6)
-- ---------------------------------------------------------------------
CREATE TABLE heatmap (
    run_id        bigint      NOT NULL REFERENCES run ON DELETE CASCADE,
    cond_idx      int         NOT NULL,
    seq_no        int,                    -- 업로드 도착 순번 (진단용)
    cond_hash     bytea       NOT NULL,   -- sha256(hv,frq,duty,lff,lfv_list,cv_list)

    hv            real        NOT NULL,   -- 비정규화 (D9)
    frq           real        NOT NULL,
    duty          real        NOT NULL,
    lff           real        NOT NULL,

    sy            smallint,               -- 격자 좌표 (fast 는 NULL)
    sx            smallint,
    hy            smallint,
    hx            smallint,
    param_no      smallint,               -- fast 전용 1..8

    no_lfv        smallint    NOT NULL,
    no_cv         smallint    NOT NULL,

    payload       bytea       NOT NULL,
    payload_fmt   smallint    NOT NULL DEFAULT 0 CHECK (payload_fmt IN (0,1)),

    is_measured   boolean     NOT NULL DEFAULT true,
    best_idx_lfv  smallint,
    best_idx_cv   smallint,
    raw_min       int,
    raw_max       int,
    raw_mean      real,

    measured_at   timestamptz,
    recv_at       timestamptz NOT NULL DEFAULT now(),
    proc_ms       int,

    PRIMARY KEY (run_id, cond_idx),
    CONSTRAINT payload_size CHECK (
        octet_length(payload) = no_lfv::int * no_cv::int
                                * (CASE payload_fmt WHEN 0 THEN 2 ELSE 4 END)
    )
);

-- UDP 중복/재전송 멱등 처리의 근거 키
CREATE UNIQUE INDEX heatmap_cond_uk     ON heatmap (run_id, cond_hash);
-- 격자가 다른 run 과 교차 매칭 (Air 가 더 성기거나 촘촘할 때)
CREATE INDEX        heatmap_cond_lookup ON heatmap (cond_hash);
CREATE INDEX        heatmap_params      ON heatmap (hv, frq, duty, lff);

-- ---------------------------------------------------------------------
-- pair_stat : 두 run 을 짝지어 뺀 결과의 요약  ★ v1.1 핵심 (D7, D12, D12b)
--
--   pair_kind='idf'         : left=Target run, right=Air run
--   pair_kind='device_diff' : left=장치A run,  right=장치B run
--
--   Air 가 여러 개이므로 "Target run 의 Idf" 는 정해지지 않는다.
--   정해지는 것은 (Target, Air) 쌍이다. 장치 비교도 같은 뺄셈이라 한 테이블로 받는다.
--
--   best_val_arr : float32[heatmap_count], 첨자 = cond_idx  → 1600장이면 6.4 KB
--   배열 자체(176 point)는 어디에도 저장하지 않는다. 필요하면 payload 에서 즉시 계산.
-- ---------------------------------------------------------------------
CREATE TYPE pair_kind_t AS ENUM ('idf','device_diff');

CREATE TABLE pair_stat (
    left_run_id   bigint      NOT NULL REFERENCES run ON DELETE CASCADE,
    right_run_id  bigint      NOT NULL REFERENCES run ON DELETE CASCADE,
    pair_kind     pair_kind_t NOT NULL,
    calib_ver     text        NOT NULL,

    best_val_arr  bytea       NOT NULL,   -- f32 x count
    best_lfv_arr  bytea,                  -- u8  x count
    best_cv_arr   bytea,                  -- u8  x count

    val_min       real,
    val_max       real,
    best_cond_idx int,                    -- 전체에서 가장 좋은 조건
    matched       int         NOT NULL,   -- 짝이 맞은 heatmap 수
    missing       int         NOT NULL DEFAULT 0,

    computed_at   timestamptz NOT NULL DEFAULT now(),

    PRIMARY KEY (left_run_id, right_run_id, pair_kind, calib_ver),
    CONSTRAINT pair_distinct CHECK (left_run_id <> right_run_id)
);
CREATE INDEX pair_stat_left  ON pair_stat (left_run_id, pair_kind);
CREATE INDEX pair_stat_right ON pair_stat (right_run_id, pair_kind);

-- ---------------------------------------------------------------------
-- run 진행 로그
-- ---------------------------------------------------------------------
CREATE TABLE run_event (
    event_id  bigserial   PRIMARY KEY,
    run_id    bigint      REFERENCES run ON DELETE CASCADE,
    at        timestamptz NOT NULL DEFAULT now(),
    kind      text        NOT NULL,   -- start/heatmap_ok/heatmap_dup/heatmap_bad/gap/abort/done/resume
    cond_idx  int,
    detail    jsonb
);
CREATE INDEX run_event_run ON run_event (run_id, at);

-- ---------------------------------------------------------------------
-- 텔레메트리를 버리기 전에 남기는 요약 + 10초 다운샘플 (D1 채택)
-- ---------------------------------------------------------------------
CREATE TABLE run_telemetry_summary (
    run_id     bigint      PRIMARY KEY REFERENCES run ON DELETE CASCADE,
    gfc        jsonb,      -- {samples, gaps, tvoc1:{min,max,avg,p50,p95}, pump2_on_sec, inject_cycles}
    gfc_10s    bytea,      -- 10초 평균 다운샘플 (2,880행/8h ≈ 230KB)
    gfc_10s_cols text[],   -- gfc_10s 의 컬럼 순서 정의
    built_at   timestamptz NOT NULL DEFAULT now()
);

-- ---------------------------------------------------------------------
-- 메뉴얼 모드 운영자 스냅샷 (D11b — 채택 확정)
--   AOS Current 1초는 DB 에 저장하지 않지만, 좋은 동작점을 찾았을 때
--   운영자가 그 순간을 영구히 남기고 싶을 수 있다.
-- ---------------------------------------------------------------------
CREATE TABLE manual_mark (
    mark_id    bigserial   PRIMARY KEY,
    device_id  smallint    NOT NULL REFERENCES device,
    at         timestamptz NOT NULL DEFAULT now(),
    actor      text,
    hv         real, frq real, duty real, cv real,
    lf_on      boolean, lf_frq real, lf_volt real,
    adc0 int, adc1 int, adc2 int, adc3 int,
    note       text
);
CREATE INDEX manual_mark_dev ON manual_mark (device_id, at DESC);

-- =====================================================================
-- 계층 B : 휘발 — GFC 1초만 (UNLOGGED, WAL 미사용, D10)
--   AOS Current 는 테이블이 없다 (D11) — udp_server.py 의 deque(600) + WebSocket
-- =====================================================================
CREATE UNLOGGED TABLE tele_gfc (
    run_id        bigint      NOT NULL,
    device_id     smallint    NOT NULL,
    ts            timestamptz NOT NULL,
    tvoc_raw1     int,
    tvoc_raw2     int,
    volt          real,
    pump1         boolean,
    pump2         boolean,
    pump3         boolean,
    inj_remain_s  int,
    inj_elapsed_s int,
    uptime_s      int,
    rssi          smallint,
    err           smallint
) PARTITION BY LIST (run_id);

CREATE UNLOGGED TABLE tele_gfc_unassigned PARTITION OF tele_gfc DEFAULT;

-- run 시작 시
CREATE OR REPLACE FUNCTION tele_gfc_attach(p_run_id bigint) RETURNS void AS $$
BEGIN
    EXECUTE format(
        'CREATE UNLOGGED TABLE IF NOT EXISTS tele_gfc_r%s
           PARTITION OF tele_gfc FOR VALUES IN (%s)', p_run_id, p_run_id);
END $$ LANGUAGE plpgsql;

-- run 종료 시 : run_telemetry_summary 를 먼저 기록한 뒤 호출할 것
CREATE OR REPLACE FUNCTION tele_gfc_drop(p_run_id bigint) RETURNS void AS $$
BEGIN
    EXECUTE format('DROP TABLE IF EXISTS tele_gfc_r%s', p_run_id);
END $$ LANGUAGE plpgsql;

-- =====================================================================
-- 운영 : 인증 · 제어권 · 감사로그 (D14, D15)
-- =====================================================================

CREATE TABLE app_user (
    user_id     bigserial   PRIMARY KEY,
    login       text        NOT NULL UNIQUE,
    display     text,
    is_active   boolean     NOT NULL DEFAULT true,
    created_at  timestamptz NOT NULL DEFAULT now()
);

-- D15b (확정): 토큰 등급은 read / control / admin 3단계.
--   read    = 조회만 (대시보드)
--   control = + 파라미터 변경, 측정 시작/중단
--   admin   = + 사용자·토큰 발급
-- 장치별 개별 권한은 두지 않는다 (장비 수가 적고 사내 전용).
-- 나중에 필요하면 token_device_grant 테이블 하나를 추가해 확장한다.
CREATE TABLE api_token (
    token_id    bigserial   PRIMARY KEY,
    user_id     bigint      NOT NULL REFERENCES app_user ON DELETE CASCADE,
    token_hash  bytea       NOT NULL UNIQUE,   -- 원문은 저장하지 않는다
    scope       text        NOT NULL CHECK (scope IN ('read','control','admin')),
    label       text,
    net_zone    text,       -- 특정 망에서만 유효하게 하려면
    created_at  timestamptz NOT NULL DEFAULT now(),
    expires_at  timestamptz,
    revoked_at  timestamptz,
    last_used   timestamptz
);
CREATE INDEX api_token_user ON api_token (user_id) WHERE revoked_at IS NULL;

-- ---------------------------------------------------------------------
-- 장치 소유권 임대 (D14)
--   웹 + 콘솔 2~3대가 같은 장치를 만진다.
--   정책 (D14a — 확정):
--     1) 조작하려면 락을 잡는다. 임대 30초, 하트비트로 연장.
--     2) 콘솔 조작은 무조건 선점(preempt). 장비 앞의 사람이 이긴다.
--        인수인계 E9(GTS_LOCAL_HOLD_MS 1200ms 로컬 우선)와 같은 원칙.
--     3) 웹은 선점당하면 UI 에 즉시 표시 (E13 의 값 되돌림 증상과 구분).
--     4) 측정 run 진행 중에는 파라미터 변경 락을 웹·콘솔 모두 거부.
--        단 run 중단(pause/abort)은 예외로 허용한다 (D14b).
-- ---------------------------------------------------------------------
CREATE TABLE device_lock (
    device_id     smallint    PRIMARY KEY REFERENCES device,
    owner_kind    ctl_source  NOT NULL,
    owner_id      text        NOT NULL,      -- 콘솔 ID 또는 웹 세션 ID
    owner_label   text,
    acquired_at   timestamptz NOT NULL DEFAULT now(),
    heartbeat_at  timestamptz NOT NULL DEFAULT now(),
    expires_at    timestamptz NOT NULL,      -- 임대. 끊기면 자동 만료
    preempted_by  text,
    preempted_at  timestamptz
);

-- console_action 을 일반화 — 웹/API 조작도 같은 곳에 남긴다
CREATE TABLE control_action (
    action_id  bigserial   PRIMARY KEY,
    at         timestamptz NOT NULL DEFAULT now(),
    source     ctl_source  NOT NULL,
    actor      text,                      -- app_user.login 또는 콘솔 ID
    src_type   smallint,
    src_id     smallint,
    dev_type   smallint,
    dev_id     smallint,
    cmd        smallint,
    detail     jsonb,
    result     text                       -- 'ok' | 'denied_lock' | 'timeout' | ...
);
CREATE INDEX control_action_time ON control_action (at DESC);
CREATE INDEX control_action_dev  ON control_action (dev_type, dev_id, at DESC);

CREATE TABLE ingest_error (
    err_id     bigserial   PRIMARY KEY,
    at         timestamptz NOT NULL DEFAULT now(),
    src        text,           -- 'aos' | 'gfc' | 'console'
    peer       inet,
    reason     text,           -- 'crc' | 'size' | 'unknown_cmd' | 'no_run' ...
    raw        bytea
);
CREATE INDEX ingest_error_time ON ingest_error (at DESC);

-- =====================================================================
-- 조회 뷰
-- =====================================================================

-- D13: Air List — 사용자가 고를 근거를 한 화면에
CREATE VIEW v_air_list AS
SELECT r.run_id, r.run_uid, r.device_id, d.name AS device_name,
       r.label, r.mode, r.grid_id,
       r.started_at, r.ended_at, r.ambient, r.calib_ver, r.comment,
       r.expected_heatmaps,
       count(h.*)                                   AS stored_heatmaps,
       (count(h.*) = r.expected_heatmaps)           AS is_complete,
       EXTRACT(EPOCH FROM (now() - r.started_at))::bigint AS age_sec
  FROM run r
  JOIN device d ON d.device_id = r.device_id
  LEFT JOIN heatmap h ON h.run_id = r.run_id
 WHERE r.kind = 'air_ref' AND r.status = 'done' AND r.data_origin = 'measured'
 GROUP BY r.run_id, d.name
 ORDER BY r.started_at DESC;

-- 임의의 두 run 을 cond_hash 로 짝지어 payload 쌍을 돌려준다.
-- Idf 계산(Target−Air)과 장치 편차 비교(A−B)가 모두 이 함수 하나로 처리된다. (D12b)
CREATE OR REPLACE FUNCTION fn_pair_payload(p_left bigint, p_right bigint)
RETURNS TABLE (
    cond_idx int, hv real, frq real, duty real, lff real,
    sy smallint, sx smallint, hy smallint, hx smallint,
    no_lfv smallint, no_cv smallint,
    left_payload bytea, left_fmt smallint,
    right_payload bytea, right_fmt smallint
) AS $$
    SELECT l.cond_idx, l.hv, l.frq, l.duty, l.lff,
           l.sy, l.sx, l.hy, l.hx, l.no_lfv, l.no_cv,
           l.payload, l.payload_fmt, r.payload, r.payload_fmt
      FROM heatmap l
      JOIN heatmap r ON r.run_id = p_right AND r.cond_hash = l.cond_hash
     WHERE l.run_id = p_left
     ORDER BY l.cond_idx;
$$ LANGUAGE sql STABLE;

-- run 진행/결손 현황
CREATE VIEW v_run_progress AS
SELECT r.run_id, r.run_uid, r.kind, r.mode, r.status, r.target_gas, r.label,
       r.device_id, r.control_source, r.started_at, r.ended_at,
       r.expected_heatmaps,
       count(h.*)                                                  AS stored_heatmaps,
       r.expected_heatmaps - count(h.*)                            AS missing,
       round(100.0 * count(h.*) / NULLIF(r.expected_heatmaps,0), 1) AS pct
  FROM run r
  LEFT JOIN heatmap h ON h.run_id = r.run_id
 GROUP BY r.run_id;

-- ---------------------------------------------------------------------
-- D16 (확정): 비교는 data_origin='measured' 끼리, 같은 grid_id 를 기본으로 한다.
--   개발 전 테스트 샘플(legacy_import)은 장비 전압 레인지가 다르므로 비교 대상이 아니다.
--   (참고: 그 샘플의 HV_List 는 80~220, 현행 정의는 45~180 — 겹치는 값이 하나도 없다.
--    이는 "격자가 자주 다르다" 는 뜻이 아니라 "구세대 데이터" 라는 뜻이다.)
--   그래도 안전장치로 겹치는 조건 수를 먼저 세고, 0 이면 UI 가 비교를 막는다.
-- ---------------------------------------------------------------------
CREATE OR REPLACE FUNCTION fn_pair_overlap(p_left bigint, p_right bigint)
RETURNS TABLE (
    left_total int, right_total int, overlap int,
    left_missing int, right_missing int, same_grid boolean
) AS $$
    WITH l AS (SELECT cond_hash FROM heatmap WHERE run_id = p_left),
         r AS (SELECT cond_hash FROM heatmap WHERE run_id = p_right),
         o AS (SELECT count(*)::int c FROM l JOIN r USING (cond_hash))
    SELECT (SELECT count(*)::int FROM l),
           (SELECT count(*)::int FROM r),
           (SELECT c FROM o),
           (SELECT count(*)::int FROM l) - (SELECT c FROM o),
           (SELECT count(*)::int FROM r) - (SELECT c FROM o),
           (SELECT a.grid_id = b.grid_id FROM run a, run b
             WHERE a.run_id = p_left AND b.run_id = p_right);
$$ LANGUAGE sql STABLE;

-- D16: 어떤 run 과 비교 가능한 상대 목록 (같은 격자 · 같은 가스 · 실측 데이터만)
CREATE OR REPLACE FUNCTION fn_compare_candidates(p_run bigint)
RETURNS TABLE (
    run_id bigint, device_id smallint, label text, target_gas text,
    started_at timestamptz, same_grid boolean
) AS $$
    SELECT o.run_id, o.device_id, o.label, o.target_gas, o.started_at, true
      FROM run me
      JOIN run o ON o.grid_id = me.grid_id
                AND o.kind    = me.kind
                AND o.run_id <> me.run_id
                AND o.status  = 'done'
                AND o.data_origin = 'measured'
                AND (me.kind = 'air_ref' OR o.target_gas IS NOT DISTINCT FROM me.target_gas)
     WHERE me.run_id = p_run
       AND me.data_origin = 'measured'
     ORDER BY o.started_at DESC;
$$ LANGUAGE sql STABLE;

-- 누락된 조건 목록 (재측정 요청용 / D14b 재개 시 남은 목록)
CREATE OR REPLACE FUNCTION fn_missing_cond(p_run bigint)
RETURNS TABLE (cond_idx int) AS $$
    SELECT gs.i
      FROM run r
      JOIN grid_def g ON g.grid_id = r.grid_id
      CROSS JOIN LATERAL generate_series(0, g.heatmap_count - 1) AS gs(i)
     WHERE r.run_id = p_run
       AND NOT EXISTS (SELECT 1 FROM heatmap h
                        WHERE h.run_id = p_run AND h.cond_idx = gs.i);
$$ LANGUAGE sql STABLE;
