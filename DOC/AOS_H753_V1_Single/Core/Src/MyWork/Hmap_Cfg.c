
#include "hmap.h"
#include "MyCTL.h"
#include "EEPROM.h"
#include "command.h"
#include <string.h>

// =========================================================
// 측정 격자 설정 — default / EEPROM 저장·복원
//
//  default 값 출처 : DOC/GTS_DB_Design/GTS_DB_Design.md 6항
//    Frq_List  10 : 200 266.7 333.3 400 466.7 533.3 600 666.7 733.3 800
//    HV_List   10 : 45 60 75 90 105 120 135 150 165 180
//    Duty_List  4 : 50 55 60 65
//    LFF_List   4 : 50 100 150 200
//    LFV_List  16 : 0 ~ 3.0  (0.2 step)
//    CV_List   11 : -1 ~ +1  (0.2 step)
//
//  구 PCSW(mStruct_B.cs)는 Filter Type 별 HV Min/Max 를 균등분할해 HV_List 를
//  만들었다. 이제 위 문서 값을 default 로 두고, 바꿀 일이 생기면 대시보드에서
//  내려보낸 뒤 EEPROM 에 저장한다. 실제로 바뀔 값은 사실상 HV 뿐이다.
// =========================================================

// g_hmap_cfg 의 실체는 Hmap.c 가 갖는다 (hmap.h 의 EXT_HMAP 패턴).
// 여기서 또 정의하면 -fno-common 에서 중복 정의가 된다.

// Frq 는 200~800 kHz 를 10단계 균등분할한 값이라 계산으로 만든다.
// (문서의 266.7/333.3 등은 이 계산값을 반올림 표기한 것)
#define HMAP_FRQ_MIN   200.0f
#define HMAP_FRQ_MAX   800.0f

static const float k_hv_default[HMAP_SX_N] = {
    45.0f, 60.0f, 75.0f, 90.0f, 105.0f, 120.0f, 135.0f, 150.0f, 165.0f, 180.0f
};
static const float k_duty_default[HMAP_HX_N] = { 50.0f, 55.0f, 60.0f, 65.0f };
static const float k_lff_default [HMAP_HY_N] = { 50.0f, 100.0f, 150.0f, 200.0f };

// ---------------------------------------------------------
// 체크섬 — magic 부터 cv_list 끝까지의 u16 합
// ---------------------------------------------------------
static u16 cfg_chksum(const __hmap_cfg *c)
{
    const u8 *p = (const u8 *)c;
    u16 n = (u16)((const u8 *)&c->chksum - (const u8 *)c);
    u16 sum = 0;
    for (u16 i = 0; i < n; i++) sum = (u16)(sum + p[i]);
    return sum;
}

// ---------------------------------------------------------
void Hmap_Cfg_SetDefault(__hmap_cfg *c)
{
    memset(c, 0, sizeof(*c));

    c->magic   = HMAP_CFG_MAGIC;
    c->version = HMAP_CFG_VERSION;
    c->size    = (u16)sizeof(__hmap_cfg);

    c->no_sx   = HMAP_SX_N;
    c->no_sy   = HMAP_SY_N;
    c->no_hx   = HMAP_HX_N;
    c->no_hy   = HMAP_HY_N;
    c->no_lfv  = HMAP_LFV_N;
    c->no_cv   = HMAP_CV_N;
    c->delay_ms = 100;

    // Frq : 200~800 kHz 균등분할
    for (u8 i = 0; i < HMAP_SX_N; i++)
        c->frq_list[i] = HMAP_FRQ_MIN
                       + (HMAP_FRQ_MAX - HMAP_FRQ_MIN) * i / (HMAP_SX_N - 1);

    for (u8 i = 0; i < HMAP_SY_N;  i++) c->hv_list[i]   = k_hv_default[i];
    for (u8 i = 0; i < HMAP_HX_N;  i++) c->duty_list[i] = k_duty_default[i];
    for (u8 i = 0; i < HMAP_HY_N;  i++) c->lff_list[i]  = k_lff_default[i];

    // LFV : 0 ~ 3.0V, 0.2 step
    for (u8 i = 0; i < HMAP_LFV_N; i++) c->lfv_list[i] = 0.2f * i;
    // CV  : -1 ~ +1V, 0.2 step
    for (u8 i = 0; i < HMAP_CV_N;  i++) c->cv_list[i]  = -1.0f + 0.2f * i;

    c->chksum = cfg_chksum(c);
}

// ---------------------------------------------------------
u8 Hmap_Cfg_Validate(const __hmap_cfg *c)
{
    if (c->magic   != HMAP_CFG_MAGIC)      return 0;
    if (c->version != HMAP_CFG_VERSION)    return 0;
    if (c->size    != sizeof(__hmap_cfg))  return 0;

    if (c->no_sx  == 0 || c->no_sx  > HMAP_SX_N)  return 0;
    if (c->no_sy  == 0 || c->no_sy  > HMAP_SY_N)  return 0;
    if (c->no_hx  == 0 || c->no_hx  > HMAP_HX_N)  return 0;
    if (c->no_hy  == 0 || c->no_hy  > HMAP_HY_N)  return 0;
    if (c->no_lfv == 0 || c->no_lfv > HMAP_LFV_N) return 0;
    if (c->no_cv  == 0 || c->no_cv  > HMAP_CV_N)  return 0;

    // 측정 버퍼를 넘지 않아야 한다 (3-1 버그 재발 방지)
    if (c->no_lfv > HMAP_LFV_POINTS_MAX) return 0;
    if (c->no_cv  > HMAP_CV_LINES_MAX)   return 0;

    if (c->chksum != cfg_chksum(c))        return 0;
    return 1;
}

// ---------------------------------------------------------
// EEPROM 은 byte 단위 R/W (EEPROM.c). 설정이 240 byte 남짓이라 그대로 쓴다.
// ---------------------------------------------------------
u8 Hmap_Cfg_Load(void)
{
    __hmap_cfg tmp;
    u8 *p = (u8 *)&tmp;

    for (u16 i = 0; i < (u16)sizeof(__hmap_cfg); i++)
        p[i] = EEPROM_READ((u16)(HMAP_CFG_EE_ADDR + i));

    if (!Hmap_Cfg_Validate(&tmp)) {
        Hmap_Cfg_SetDefault(&g_hmap_cfg);   // 비어있거나 깨졌으면 default
        return 0;
    }

    g_hmap_cfg = tmp;
    return 1;
}

u8 Hmap_Cfg_Save(void)
{
    const u8 *p;
    u16 i;

    g_hmap_cfg.magic   = HMAP_CFG_MAGIC;
    g_hmap_cfg.version = HMAP_CFG_VERSION;
    g_hmap_cfg.size    = (u16)sizeof(__hmap_cfg);
    g_hmap_cfg.chksum  = cfg_chksum(&g_hmap_cfg);

    if (!Hmap_Cfg_Validate(&g_hmap_cfg)) return 0;

    p = (const u8 *)&g_hmap_cfg;
    for (i = 0; i < (u16)sizeof(__hmap_cfg); i++)
        EEPROM_WRITE((u16)(HMAP_CFG_EE_ADDR + i), p[i]);

    // write-back 확인
    for (i = 0; i < (u16)sizeof(__hmap_cfg); i++)
        if (EEPROM_READ((u16)(HMAP_CFG_EE_ADDR + i)) != p[i]) return 0;

    return 1;
}

void Hmap_Cfg_Init(void)
{
    Hmap_Cfg_Load();    // 실패하면 내부에서 default 로 채운다
}
