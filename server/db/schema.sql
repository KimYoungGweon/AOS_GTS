-- =====================================================================
-- GTS DB Schema v1.4  (PostgreSQL 14+)
-- 근거: DOC/GTS_DB_Design/GTS_DB_Design_Proposal_v1.md  (D1, D11~D16 확정)
--
--   계층 A = 영구 (측정결과 · 비교결과)
--   계층 B = 휘발 (GFC 1초 → run 종료 시 DROP)
--   계층 C = 비저장 (AOS Current → 서버 메모리 deque, 테이블 없음)
--
-- v1.3 → v1.4 변경 (2026-09-23)
--   * device PK 를 (dev_type, device_id) 로.  AOS 1 과 GFC 1 이 같은 device_id 를
--     써서 v1.3 에서는 PK 충돌이 났다.  AOS n ↔ GFC n 은 항상 한 쌍 (고정).
--   * pair_config  : 쌍(=AOS ID) 별 사용자 지정 가스명 (웹 디자인 "가스명 변경")
--   * run.dev_type / manual_mark.dev_type  : 복합 FK 용 (값은 항상 1=AOS)
--   * run.concentration, run.source_file   : 목록 화면의 "농도"·"파일명"
--   * tele_gfc 컬럼을 GFC 0x32 SENSOR_DATA 52 byte 구조와 1:1 로 맞춤
--   * sys_event    : 대시보드 "최근 이벤트" (run 과 무관한 장치 이벤트)
--   * api_token.token_prefix : 목록에서 토큰 구분용 앞 8자
--   * schema_meta  : 스키마 버전 기록
-- =====================================================================
CREATE EXTENSION IF NOT EXISTS pgcrypto;   -- gen_random_uuid()

CREATE TABLE schema_meta (
    key    text PRIMARY KEY,
    value  text NOT NULL
);
INSERT INTO schema_meta VALUES ('version', '1.4'), ('applied_at', now()::text);

-- =====================================================================
-- 계층 A : 영구
-- =====================================================================

-- 1=AOS, 2=GFC  (UDP DTYPE 과 같은 값)
CREATE TABLE device (
    dev_type      smallint    NOT NULL CHECK (dev_type IN (1, 2)),
    device_id     smallint    NOT NULL CHECK (device_id BETWEEN 1 AND 20),  -- UDP DID
    name          text        NOT NULL,
    serial        text,
    model         text,        -- HELLO 의 model
    fw            text,        -- HELLO 의 fw
    mac           text,        -- HELLO 의 mac
    net_zone      text,        -- D15: 실험실 망 2개 분리
    last_peer     inet,        -- 진단용. NAT 때문에 두 브리지가 같은 IP 로 보임 →
                               -- 식별은 반드시 (dev_type, device_id) 로 한다
    claimed_ip    text,        -- 장치가 HELLO 로 자기 보고한 IP
    note          text,
    first_seen    timestamptz NOT NULL DEFAULT now(),
    last_seen     timestamptz,
    PRIMARY KEY (dev_type, device_id)
);

-- ---------------------------------------------------------------------
-- 쌍 설정.  AOS n ↔ GFC n 고정.  pair_id = AOS ID = GFC ID
--   gas_name : 사용자가 웹에서 바꾸는 "측정 가스명".  측정 run 시작 시
--              run.target_gas 의 기본값으로 복사된다 (run 에는 그 시점 값이 남음).
-- ---------------------------------------------------------------------
CREATE TABLE pair_config (
    pair_id     smallint    PRIMARY KEY CHECK (pair_id BETWEEN 1 AND 20),
    gas_name    text,
    note        text,
    updated_at  timestamptz NOT NULL DEFAULT now(),
    updated_by  text
);
INSERT INTO pair_config (pair_id) SELECT generate_series(1, 20);

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
-- D14b: paused = 중단했으나 재개 예정 / aborted = 영구 중단
CREATE TYPE run_status AS ENUM ('running','paused','done','aborted','failed');
CREATE TYPE ctl_source AS ENUM ('console','web','api','import');
-- D16: legacy_import 는 개발 전 테스트 데이터 — 비교 후보에서 구조적으로 배제
CREATE TYPE data_origin_t AS ENUM ('measured','legacy_import');

CREATE TABLE run (
    run_id            bigserial   PRIMARY KEY,
    run_uid           uuid        NOT NULL UNIQUE DEFAULT gen_random_uuid(),
    dev_type          smallint    NOT NULL DEFAULT 1 CHECK (dev_type = 1),  -- 항상 AOS
    device_id         smallint    NOT NULL,
    kind              run_kind    NOT NULL,
    mode              text        NOT NULL CHECK (mode IN ('fast','hour1','full8')),
    grid_id           bigint      NOT NULL REFERENCES grid_def,

    -- D12/D13b: "마지막으로 사용한 Air" 기록일 뿐. 자동 적용하지 않는다.
    air_ref_run_id    bigint      REFERENCES run(run_id),

    label             text,                          -- D13 짧은 표시명
    ambient           jsonb,                         -- {"temp_c":..,"rh":..,"filter":".."}

    target_gas        text,
    concentration     real,                          -- v1.4: 측정 시 GFC 농도 (V)
    comment           text,
    operator          text,
    source_file       text,                          -- v1.4: .dat 원본 파일명 (legacy)

    filter_type       smallint,
    metric_type       smallint,
    wave_type         smallint,
    duty_mode         smallint,
    lff_mode          smallint,
    wait_delay_ms     int,
    avg_count         int,
    lfv_settle_ms     int,
    cv_settle_ms      int,

    calib_ver         text        NOT NULL DEFAULT 'v1',  -- ADC raw → 값 보정식 (D4)
    data_origin       data_origin_t NOT NULL DEFAULT 'measured',

    control_source    ctl_source  NOT NULL DEFAULT 'console',
    requested_by      text,

    status            run_status  NOT NULL DEFAULT 'running',
    started_at        timestamptz NOT NULL DEFAULT now(),
    ended_at          timestamptz,

    expected_heatmaps int         NOT NULL,
    received_heatmaps int         NOT NULL DEFAULT 0,

    resume_count      int         NOT NULL DEFAULT 0,
    last_paused_at    timestamptz,
    last_resumed_at   timestamptz,

    FOREIGN KEY (dev_type, device_id) REFERENCES device (dev_type, device_id),
    CONSTRAINT air_has_no_ref CHECK (kind = 'gas' OR air_ref_run_id IS NULL),
    CONSTRAINT no_self_ref    CHECK (air_ref_run_id IS DISTINCT FROM run_id)
);
CREATE INDEX run_device_time ON run (device_id, started_at DESC);
CREATE INDEX run_kind_time   ON run (kind, started_at DESC);
CREATE INDEX run_air_ref     ON run (air_ref_run_id) WHERE air_ref_run_id IS NOT NULL;
CREATE INDEX run_gas_lookup  ON run (target_gas, device_id, started_at DESC)
                              WHERE kind = 'gas';
-- 한 AOS 에서 동시에 running 인 run 은 1개뿐
CREATE UNIQUE INDEX run_one_active ON run (device_id) WHERE status = 'running';

-- ---------------------------------------------------------------------
-- Heatmap 본체.  1장 = 1행 = blob 1개 (D3)
--   payload : iy=CV(외) x ix=LFV(내) row-major — 0x86 wire order 그대로
--   payload_fmt 0 = uint16 ADC raw (352B, 실측 경로)
--               1 = float32        (704B, .dat 임포트 경로)
--   cond_idx : ((sy*no_sx + sx)*no_hy + hy)*no_hx + hx   (fast 는 param_no-1)
-- ---------------------------------------------------------------------
CREATE TABLE heatmap (
    run_id        bigint      NOT NULL REFERENCES run ON DELETE CASCADE,
    cond_idx      int         NOT NULL,
    seq_no        int,
    cond_hash     bytea       NOT NULL,   -- sha256(hv,frq,duty,lff,lfv_list,cv_list)

    hv            real        NOT NULL,   -- 비정규화 (D9)
    frq           real        NOT NULL,
    duty          real        NOT NULL,
    lff           real        NOT NULL,

    sy            smallint,
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
    raw_min       real,
    raw_max       real,
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
CREATE UNIQUE INDEX heatmap_cond_uk     ON heatmap (run_id, cond_hash);
CREATE INDEX        heatmap_cond_lookup ON heatmap (cond_hash);
CREATE INDEX        heatmap_params      ON heatmap (hv, frq, duty, lff);

-- received_heatmaps 는 INSERT 때마다 트리거로 맞춘다 (ON CONFLICT DO NOTHING 은 안 셈)
CREATE OR REPLACE FUNCTION trg_heatmap_count() RETURNS trigger AS $$
BEGIN
    UPDATE run SET received_heatmaps = received_heatmaps + 1 WHERE run_id = NEW.run_id;
    RETURN NULL;
END $$ LANGUAGE plpgsql;
CREATE TRIGGER heatmap_count AFTER INSERT ON heatmap
    FOR EACH ROW EXECUTE FUNCTION trg_heatmap_count();

-- ---------------------------------------------------------------------
-- pair_stat : 두 run 을 짝지어 뺀 결과의 요약 (D7, D12, D12b)
--   idf         : left=Target run, right=Air run   값 = Target_P − Air_P
--   device_diff : left=장치A run,  right=장치B run  값 = A − B
--   best_val_arr : float32[heatmap_count], 첨자 = cond_idx
-- ---------------------------------------------------------------------
CREATE TYPE pair_kind_t AS ENUM ('idf','device_diff');

CREATE TABLE pair_stat (
    left_run_id   bigint      NOT NULL REFERENCES run ON DELETE CASCADE,
    right_run_id  bigint      NOT NULL REFERENCES run ON DELETE CASCADE,
    pair_kind     pair_kind_t NOT NULL,
    calib_ver     text        NOT NULL,

    best_val_arr  bytea       NOT NULL,   -- f32 x count  (결손 조건은 NaN)
    best_lfv_arr  bytea,                  -- u8  x count
    best_cv_arr   bytea,                  -- u8  x count

    val_min       real,
    val_max       real,
    best_cond_idx int,
    matched       int         NOT NULL,
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
    kind      text        NOT NULL,   -- start/heatmap_ok/heatmap_dup/gap/pause/abort/done/resume
    cond_idx  int,
    detail    jsonb
);
CREATE INDEX run_event_run ON run_event (run_id, at);

-- ---------------------------------------------------------------------
-- 텔레메트리를 버리기 전에 남기는 요약 + 10초 다운샘플 (D1)
-- ---------------------------------------------------------------------
CREATE TABLE run_telemetry_summary (
    run_id       bigint      PRIMARY KEY REFERENCES run ON DELETE CASCADE,
    gfc          jsonb,      -- {samples, volt1:{min,max,avg}, pump2_on_sec, ...}
    gfc_10s      bytea,      -- f32 row-major  [n x len(gfc_10s_cols)]
    gfc_10s_cols text[],
    built_at     timestamptz NOT NULL DEFAULT now()
);

-- ---------------------------------------------------------------------
-- 메뉴얼 모드 운영자 스냅샷 (D11b)
-- ---------------------------------------------------------------------
CREATE TABLE manual_mark (
    mark_id    bigserial   PRIMARY KEY,
    dev_type   smallint    NOT NULL DEFAULT 1 CHECK (dev_type = 1),
    device_id  smallint    NOT NULL,
    at         timestamptz NOT NULL DEFAULT now(),
    actor      text,
    hv         real, frq real, duty real, cv real,
    lf_on      boolean, lf_frq real, lf_volt real,
    adc0 int, adc1 int, adc2 int, adc3 int,     -- STATUS air_p, air_n, gas_p, gas_n
    avg_v      real,                             -- 최근 N초 평균 (표시 단위 V)
    note       text,
    FOREIGN KEY (dev_type, device_id) REFERENCES device (dev_type, device_id)
);
CREATE INDEX manual_mark_dev ON manual_mark (device_id, at DESC);

-- =====================================================================
-- 계층 B : 휘발 — GFC 1초 (UNLOGGED, D10)
--   컬럼은 GFC 0x32 SENSOR_DATA (52 byte, "<I7fI4H4BHbB") 와 1:1
--   run 밖 데이터는 run_id = 0 → DEFAULT 파티션, housekeeping 이 보존기간 지나면 삭제
-- =====================================================================
CREATE UNLOGGED TABLE tele_gfc (
    run_id        bigint      NOT NULL,
    device_id     smallint    NOT NULL,
    ts            timestamptz NOT NULL,
    dev_ts        bigint,         -- 장치 timestamp
    volt1         real,           -- TVOC 1 (V)
    volt2         real,           -- TVOC 2 (V)
    ctrl          real,
    slope         real,
    sv            real,
    src_remain    real,
    src_elapsed   real,
    uptime_s      bigint,
    raw1          int,
    raw2          int,
    co2_1         int,
    co2_2         int,
    pump1         smallint,
    pump2         smallint,
    pump3         smallint,
    flags         smallint,
    err           int,
    rssi          smallint
) PARTITION BY LIST (run_id);

CREATE UNLOGGED TABLE tele_gfc_unassigned PARTITION OF tele_gfc DEFAULT;
CREATE INDEX tele_gfc_unassigned_ts ON tele_gfc_unassigned (device_id, ts);

CREATE OR REPLACE FUNCTION tele_gfc_attach(p_run_id bigint) RETURNS void AS $$
BEGIN
    EXECUTE format(
        'CREATE UNLOGGED TABLE IF NOT EXISTS tele_gfc_r%s
           PARTITION OF tele_gfc FOR VALUES IN (%s)', p_run_id, p_run_id);
END $$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION tele_gfc_drop(p_run_id bigint) RETURNS void AS $$
BEGIN
    EXECUTE format('DROP TABLE IF EXISTS tele_gfc_r%s', p_run_id);
END $$ LANGUAGE plpgsql;

-- =====================================================================
-- 운영 : 인증 · 제어권 · 감사로그 · 이벤트 (D14, D15)
-- =====================================================================

CREATE TABLE app_user (
    user_id     bigserial   PRIMARY KEY,
    login       text        NOT NULL UNIQUE,
    display     text,
    is_active   boolean     NOT NULL DEFAULT true,
    created_at  timestamptz NOT NULL DEFAULT now()
);

-- D15b: read / control / admin 3단계. 장치별 권한 없음.
CREATE TABLE api_token (
    token_id     bigserial   PRIMARY KEY,
    user_id      bigint      NOT NULL REFERENCES app_user ON DELETE CASCADE,
    token_hash   bytea       NOT NULL UNIQUE,   -- sha256(원문). 원문은 저장하지 않는다
    token_prefix text        NOT NULL,          -- 원문 앞 8자 (식별용)
    scope        text        NOT NULL CHECK (scope IN ('read','control','admin')),
    label        text,
    net_zone     text,
    created_at   timestamptz NOT NULL DEFAULT now(),
    expires_at   timestamptz,
    revoked_at   timestamptz,
    last_used    timestamptz
);
CREATE INDEX api_token_user ON api_token (user_id) WHERE revoked_at IS NULL;

-- ---------------------------------------------------------------------
-- 장치 소유권 임대 (D14, D14a)
--   실제 판정은 서버 메모리에서 한다 (콘솔 수신 경로를 DB 로 막지 않기 위해).
--   이 테이블은 그 상태의 거울 — 재시작 후 참고 / 조회용.
-- ---------------------------------------------------------------------
CREATE TABLE device_lock (
    dev_type      smallint    NOT NULL,
    device_id     smallint    NOT NULL,
    owner_kind    ctl_source  NOT NULL,
    owner_id      text        NOT NULL,
    owner_label   text,
    acquired_at   timestamptz NOT NULL DEFAULT now(),
    heartbeat_at  timestamptz NOT NULL DEFAULT now(),
    expires_at    timestamptz NOT NULL,
    preempted_by  text,
    preempted_at  timestamptz,
    PRIMARY KEY (dev_type, device_id),
    FOREIGN KEY (dev_type, device_id) REFERENCES device (dev_type, device_id)
);

CREATE TABLE control_action (
    action_id  bigserial   PRIMARY KEY,
    at         timestamptz NOT NULL DEFAULT now(),
    source     ctl_source  NOT NULL,
    actor      text,
    src_type   smallint,
    src_id     smallint,
    dev_type   smallint,
    dev_id     smallint,
    cmd        smallint,
    detail     jsonb,
    result     text                       -- 'ok' | 'denied_lock' | 'denied_run' | 'offline' ...
);
CREATE INDEX control_action_time ON control_action (at DESC);
CREATE INDEX control_action_dev  ON control_action (dev_type, dev_id, at DESC);

-- v1.4: 대시보드 "최근 이벤트"
CREATE TABLE sys_event (
    event_id   bigserial   PRIMARY KEY,
    at         timestamptz NOT NULL DEFAULT now(),
    level      text        NOT NULL DEFAULT 'info' CHECK (level IN ('info','ok','warn','error','ctrl')),
    dev_type   smallint,
    dev_id     smallint,
    kind       text        NOT NULL,   -- online/offline/new_device/console_join/lock_preempt/run_*
    text       text        NOT NULL,
    detail     jsonb
);
CREATE INDEX sys_event_time ON sys_event (at DESC);

CREATE TABLE ingest_error (
    err_id     bigserial   PRIMARY KEY,
    at         timestamptz NOT NULL DEFAULT now(),
    src        text,
    peer       inet,
    reason     text,
    raw        bytea
);
CREATE INDEX ingest_error_time ON ingest_error (at DESC);

-- =====================================================================
-- 조회 뷰 / 함수
-- =====================================================================

-- 쌍 뷰: AOS n ↔ GFC n
CREATE VIEW v_pair AS
SELECT p.pair_id, p.gas_name, p.note,
       a.last_seen AS aos_last_seen, g.last_seen AS gfc_last_seen,
       a.name AS aos_name, g.name AS gfc_name
  FROM pair_config p
  LEFT JOIN device a ON a.dev_type = 1 AND a.device_id = p.pair_id
  LEFT JOIN device g ON g.dev_type = 2 AND g.device_id = p.pair_id;

-- D13: Air List
CREATE VIEW v_air_list AS
SELECT r.run_id, r.run_uid, r.device_id, d.name AS device_name,
       r.label, r.mode, r.grid_id,
       r.started_at, r.ended_at, r.ambient, r.calib_ver, r.comment,
       r.expected_heatmaps, r.received_heatmaps AS stored_heatmaps,
       (r.received_heatmaps = r.expected_heatmaps) AS is_complete,
       EXTRACT(EPOCH FROM (now() - r.started_at))::bigint AS age_sec
  FROM run r
  JOIN device d ON d.dev_type = r.dev_type AND d.device_id = r.device_id
 WHERE r.kind = 'air_ref' AND r.status = 'done' AND r.data_origin = 'measured'
 ORDER BY r.started_at DESC;

-- 임의의 두 run 을 cond_hash 로 짝지어 payload 쌍을 돌려준다 (D12b)
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

CREATE VIEW v_run_progress AS
SELECT r.run_id, r.run_uid, r.kind, r.mode, r.status, r.target_gas, r.label,
       r.device_id, r.control_source, r.started_at, r.ended_at,
       r.expected_heatmaps,
       r.received_heatmaps                                          AS stored_heatmaps,
       r.expected_heatmaps - r.received_heatmaps                    AS missing,
       round(100.0 * r.received_heatmaps / NULLIF(r.expected_heatmaps,0), 1) AS pct
  FROM run r;

-- D16: 겹치는 조건 수 (0 이면 비교 금지)
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

-- D16: 비교 가능한 상대 (같은 격자 · 같은 가스 · measured · done)
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

-- 누락 조건 (재측정 / D14b 재개)
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
