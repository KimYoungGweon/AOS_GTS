#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "gts_protocol.h"
#include "gts_state.h"

static int fails;
#define CHECK(c, msg) do { if(!(c)) { printf("FAIL: %s\n", msg); fails++; } } while(0)

int main(void)
{
    /* ── 1. CRC-16/MODBUS 알려진 벡터 ─────────────────────────── */
    const uint8_t v[] = "123456789";
    CHECK(gts_crc16(v, 9) == 0x4B37, "CRC16/MODBUS check value 0x4B37");

    /* ── 2. 문서의 예시 프레임과 바이트 단위로 일치하는가 ─────── */
    uint8_t f[GTS_MAX_FRAME];
    int n = gts_frame_build(f, sizeof(f), GTS_DEV_AOS, 7, GTS_CMD_CONNECT,
                            GTS_DEV_CONSOLE, 1, 5, NULL, 0);
    CHECK(n == 12, "CONNECT frame is 12 byte");
    const uint8_t want[12] = { 0x02, 0x01, 0x07, 0x01, 0x04, 0x00,
                               0x03, 0x01, 0x05, 0x00, 0x05, 0x16 };
    CHECK(memcmp(f, want, 12) == 0, "CONNECT frame matches spec example");
    if (memcmp(f, want, 12)) {
        printf("  got:");
        for (int i = 0; i < n; i++) printf(" %02X", f[i]);
        printf("\n");
    }

    /* ── 3. 왕복 ──────────────────────────────────────────────── */
    uint8_t pay[26];
    for (int i = 0; i < 26; i++) pay[i] = (uint8_t)(i * 7 + 1);
    n = gts_frame_build(f, sizeof(f), GTS_DEV_AOS, 3, GTS_CMD_AOS_PARAMS,
                        GTS_DEV_CONSOLE, 2, 0x1234, pay, sizeof(pay));
    CHECK(n == (int)(GTS_FRAME_OVERHEAD + GTS_HDR_LEN + sizeof(pay)), "len");

    gts_frame_t fr;
    CHECK(gts_frame_parse(f, (size_t)n, &fr), "parse ok");
    CHECK(fr.type == GTS_DEV_AOS && fr.id == 3, "type/id");
    CHECK(fr.cmd == GTS_CMD_AOS_PARAMS, "cmd");
    CHECK(fr.src_type == GTS_DEV_CONSOLE && fr.src_id == 2, "src");
    CHECK(fr.seq == 0x1234, "seq");
    CHECK(fr.payload_len == sizeof(pay), "payload_len");
    CHECK(memcmp(fr.payload, pay, sizeof(pay)) == 0, "payload bytes");

    /* ── 4. 손상 프레임 거부 ──────────────────────────────────── */
    f[9] ^= 0xFF;
    CHECK(!gts_frame_parse(f, (size_t)n, &fr), "corrupt frame rejected");
    f[9] ^= 0xFF;
    CHECK(!gts_frame_parse(f, (size_t)n - 1, &fr), "short frame rejected");
    f[0] = 0x03;
    CHECK(!gts_frame_parse(f, (size_t)n, &fr), "bad STX rejected");

    /* ── 5. 최대 payload 경계 ─────────────────────────────────── */
    static uint8_t big[GTS_MAX_PAYLOAD + 1];
    CHECK(gts_frame_build(f, sizeof(f), 1, 1, 1, 3, 1, 0, big, GTS_MAX_PAYLOAD) > 0,
          "max payload accepted");
    CHECK(gts_frame_build(f, sizeof(f), 1, 1, 1, 3, 1, 0, big, GTS_MAX_PAYLOAD + 1) < 0,
          "over-max payload rejected");

    /* ── 6. 상태 모델 ─────────────────────────────────────────── */
    gts_state_init();
    CHECK(g_gts.page == GTS_PAGE_DEVICE, "starts on P1");
    CHECK(g_gts.dev_id == 1, "dev_id default 1");

    /* P1 에서 jog 는 1..20 순환 */
    gts_state_jog(-1);
    CHECK(g_gts.dev_id == 20, "dev id wraps 1 -> 20");
    gts_state_jog(1);
    CHECK(g_gts.dev_id == 1, "dev id wraps 20 -> 1");

    /* P4 파라미터 clamp + step */
    g_gts.page      = GTS_PAGE_AOS_MANUAL;
    g_gts.param_sel = GTS_P_HV;
    g_gts.param[GTS_P_HV].value    = 0.0f;
    g_gts.param[GTS_P_HV].step_idx = 0;          /* 0.01 */
    gts_state_jog(-1);
    CHECK(g_gts.param[GTS_P_HV].value == 0.0f, "HV clamps at min");
    for (int i = 0; i < 100; i++) gts_state_jog(1);
    CHECK(g_gts.param[GTS_P_HV].value > 0.99f && g_gts.param[GTS_P_HV].value < 1.01f,
          "100 x 0.01 == 1.00 (no float drift)");

    /* step 순환 3종 */
    gts_state_jog_sw();
    CHECK(g_gts.param[GTS_P_HV].step_idx == 1, "step 0 -> 1");
    gts_state_jog_sw(); gts_state_jog_sw();
    CHECK(g_gts.param[GTS_P_HV].step_idx == 0, "step wraps 2 -> 0");

    /* LF_Frq 는 step 1종뿐이라 순환해도 그대로 */
    g_gts.param_sel = GTS_P_LF_FRQ;
    g_gts.lf_on = true;
    gts_state_jog_sw();
    CHECK(g_gts.param[GTS_P_LF_FRQ].step_idx == 0, "single-step param stays at 0");

    /* LF_Mode Off 면 LF 항목 jog 무시 */
    g_gts.lf_on = false;
    float before = g_gts.param[GTS_P_LF_FRQ].value;
    gts_state_jog(1);
    CHECK(g_gts.param[GTS_P_LF_FRQ].value == before, "LF locked when lf_on = false");

    /* CV 는 음수 범위 */
    g_gts.param_sel = GTS_P_CV;
    g_gts.param[GTS_P_CV].value = 0.0f;
    g_gts.param[GTS_P_CV].step_idx = 2;          /* 0.1 */
    for (int i = 0; i < 100; i++) gts_state_jog(-1);
    CHECK(g_gts.param[GTS_P_CV].value == -5.0f, "CV clamps at -5");

    /* P2 시간 타일 0.1 ~ 60.0 */
    g_gts.page = GTS_PAGE_GFC;
    g_gts.gfc_mode = GTS_GFC_AUTO;
    g_gts.gfc_sel  = GTS_GFC_SEL_START;
    g_gts.gfc_start_ds = 1;
    gts_state_jog(-1);
    CHECK(g_gts.gfc_start_ds == 1, "start time clamps at 0.1 sec");
    for (int i = 0; i < 1000; i++) gts_state_jog(1);
    CHECK(g_gts.gfc_start_ds == 600, "start time clamps at 60.0 sec");

    /* Manual 모드에서는 jog 무시 */
    g_gts.gfc_mode = GTS_GFC_MANUAL;
    g_gts.gfc_start_ds = 100;
    gts_state_jog(1);
    CHECK(g_gts.gfc_start_ds == 100, "manual mode ignores jog");

    /* P3 타입 4종 순환 */
    g_gts.page = GTS_PAGE_AOS_MEAS;
    g_gts.aos_running = false;
    g_gts.aos_type = GTS_AOS_PRETEST;
    gts_state_jog(-1);
    CHECK(g_gts.aos_type == GTS_AOS_8HOUR, "aos type wraps 0 -> 3");
    gts_state_jog(1);
    CHECK(g_gts.aos_type == GTS_AOS_PRETEST, "aos type wraps 3 -> 0");
    CHECK(g_gts.aos_total_s == 300u, "preTest total 300 s");
    g_gts.aos_running = true;
    gts_state_jog(1);
    CHECK(g_gts.aos_type == GTS_AOS_PRETEST, "type locked while running");

    /* ── 7. 조그바 표시 문자열 ────────────────────────────────── */
    char nm[32], vl[32], rg[48], sp[24]; bool hs, en;
    g_gts.page = GTS_PAGE_AOS_MANUAL;
    g_gts.param_sel = GTS_P_CV;
    g_gts.param[GTS_P_CV].value = -1.25f;
    g_gts.param[GTS_P_CV].step_idx = 1;
    gts_state_jog_target_text(nm,sizeof nm, vl,sizeof vl, rg,sizeof rg, sp,sizeof sp, &hs,&en);
    CHECK(strcmp(nm, "CV") == 0, "jog name CV");
    CHECK(strcmp(vl, "-1.250") == 0, "CV shows 3 decimals");
    CHECK(hs, "P4 has step badge");
    CHECK(strcmp(sp, "0.010") == 0, "CV step 0.010");

    g_gts.page = GTS_PAGE_DEVICE;
    g_gts.dev_id = 7;
    gts_state_jog_target_text(nm,sizeof nm, vl,sizeof vl, rg,sizeof rg, sp,sizeof sp, &hs,&en);
    CHECK(strcmp(vl, "07") == 0, "device id zero padded");
    CHECK(!hs, "P1 has no step badge");

    printf(fails ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", fails);
    return fails ? 1 : 0;
}
