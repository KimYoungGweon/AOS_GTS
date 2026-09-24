#define __LFGEN_H__
#include "LF_GEN.h"
#undef 	__LFGEN_H__

#include "MyCTL.h"
#include "main.h"
#include "LTC2602B.h"


void LF_Modulator_DAC_Update(){
u16 value;
	value= MData.SET.LF_MOD.data[MData.SET.LF_MOD.count++];
	MData.SET.LF_MOD.count %= MData.SET.LF_MOD.no;
	//LTC2602B_COMMAND(0, value);

	DAC_Out(value);
}

void LF_Modulator_Voltage_Set(){
u8 type = MData.SET.LF_MOD.type;
float step;

u16 maxValue = (u16)(MData.SET.LF_MOD.amp/5.0*32768);
u16 midValue = 32768;
u16 minValue = midValue*2 - maxValue;

	switch(type)
	{
		case eTriangle:
			step = maxValue/25;//수정..... 2026. 1. 5
			for(u8 i=0;i<50;i++) MData.SET.LF_MOD.data[i] = step*(25-i) + midValue;
			for(u8 i=0;i<50;i++) MData.SET.LF_MOD.data[i+50] = MData.SET.LF_MOD.data[49-i];
			break;
		case eRamp:
			step = maxValue/50;//수정..... 2026. 1. 5
			for(u8 i=0;i<100;i++) MData.SET.LF_MOD.data[i] = step*(50-i) + midValue;
			break;
		case eRamp1_9:
			generateRampMid(100, 10, 90, MData.SET.LF_MOD.amp/2.0f);
			break;
		case eRamp2_8:
			generateRampMid(100, 20, 80, MData.SET.LF_MOD.amp/2.0f);
			break;

		case eRamp3_7:
			generateRampMid(100, 30, 70, MData.SET.LF_MOD.amp/2.0f);
			break;
		case eSine:
			generateSineMid(100, MData.SET.LF_MOD.amp);
			break;

		case eSquare:
			generateSquareMid(100, MData.SET.LF_MOD.amp);
			break;
		case eTPZ05_95:
			generateTrapezoidMid(100, 5, 5, MData.SET.LF_MOD.amp);
			break;
		case eTPZ1_9:
			generateTrapezoidMid(100, 10, 10, MData.SET.LF_MOD.amp);
			break;
		case eTPZ2_8:
			generateTrapezoidMid(100, 20, 20, MData.SET.LF_MOD.amp);
			break;
		case eTPZ3_7:
			generateTrapezoidMid(100, 30, 30, MData.SET.LF_MOD.amp);
			break;

		case eARB:

		default:	//all zero
			for(u8 i=0;i<100;i++) MData.SET.LF_MOD.data[i] = minValue;
			break;
	}
}


void LF_Frq_Set(float frq){
u16 period;


	TIM4_OnOff(false);
	period = 1000000/frq/100 ; //1us 단위 period 100단계 -> 100us
	MX_TIM4_Update(period-1);

	TIM4_OnOff(MData.SET.LF_MOD.OnOff);
}

void LF_Modulator_Set(eLF_Type type){
//u16 period;

	LF_Frq_Set(MData.SET.LF_MOD.frq);
	//period = 1000000/ MData.SET.LF_MOD.frq/100 ; //1us 단위 period 100단계 -> 100us
	//MX_TIM4_Update(period-1);

	MData.SET.LF_MOD.count=0;
	MData.SET.LF_MOD.no=100;
	LF_Modulator_Voltage_Set();

	TIM4_OnOff(MData.SET.LF_MOD.OnOff);
	if(MData.SET.LF_MOD.OnOff==0)
	{
		DAC_Out(32768);
		//LTC2602B_COMMAND(0, 32768);//출력
	}
}


#define DAC_FULL 65535u      // 16-bit full scale (0..65535)
#define DAC_MID  32767u      // 2.5V 중심(이상적 중점)
#define F32(x)   ((float)(x))

// x: float 코드값 -> 반올림 + [0..65535] 클램프
#define CLAMP_U16F(x) ((x) > F32(DAC_FULL) ? DAC_FULL : ((x) < 0.0f ? 0u : (uint16_t)((x) + 0.5f)))


void generateRampMid(u8 total_count, u8 up_count, u8 down_count, float Vpp)
{
	if (up_count < 2 || down_count < 1) return;
	if (up_count + down_count != total_count) return;

	// Vpp 범위 제한
	if (Vpp < 0.0f) Vpp = 0.0f;
	if (Vpp > 5.0f) Vpp = 5.0f;


	// 전압 -> 코드 변환: amplitude = (Vpp/2) * (DAC_FULL / 5)
	const float code_per_volt = F32(DAC_FULL) / 5.0f;
	const float amplitude     = (Vpp * 0.5f) * code_per_volt;  // ±amplitude
	const float center        = F32(DAC_MID);

	// 목표 범위 계산 (이론상 0..65535 안에 들어오지만, 반올림/계산 오차 대비 클램프)
	float vmin = center - amplitude;  // 예: Vpp=5 → 0V 근처
	float vmax = center + amplitude;  // 예: Vpp=5 → 5V 근처
	if (vmin < 0.0f)         vmin = 0.0f;
	if (vmax > F32(DAC_FULL)) vmax = F32(DAC_FULL);

	// 스텝 계산
	const float up_span     = (vmax - vmin);                   // = 2*amplitude (클램프 적용 후)
	const float step_up     = up_span / (float)(up_count - 1); // 양 끝 포함
	const float step_down   = up_span / (float)down_count;     // 피크 중복 방지

	// 상승: vmin → vmax (up_count개)
	for (int i = 0; i < up_count; ++i) {
		float v = vmin + (float)i * step_up;
		MData.SET.LF_MOD.data[i] = CLAMP_U16F(v);
	}

	// 하강: vmax - step_down → vmin (down_count개)
	for (int j = 0; j < down_count; ++j) {
		float v = vmax - (float)(j + 1) * step_down;
		MData.SET.LF_MOD.data[up_count + j] = CLAMP_U16F(v);
	}
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <stdint.h>
#include <math.h>

#define DAC_FULL 65535u      // 16-bit full scale (0..65535)
#define DAC_MID  32767u      // 2.5V 중심


#define F32(x)   ((float)(x))

// float 값 -> 반올림 + [0..65535] 클램프
#define CLAMP_U16F(x) ((x) > F32(DAC_FULL) ? DAC_FULL : ((x) < 0.0f ? 0u : (uint16_t)((x) + 0.5f)))

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * data        : 출력 배열 (크기 total_count)
 * total_count : 전체 샘플 개수 (>=2)
 * Vpp         : 피크-투-피크 전압 [0..5] V (예: 5.0f -> 0V↔5V, 중심 2.5V)
 *
 * 특성:
 *  - 시작 위상: -π/2 (최소값에서 시작)
 *  - 한 주기를 N개로 균등 분할, 2π 엔드포인트는 미포함 → 순환(DMA) 재생 시 이음새 최소화
 */
void generateSineMid(u8 total_count, float Vpp)
{
    if (total_count < 2) return;


    // Vpp 제한
    if (Vpp < 0.0f) Vpp = 0.0f;
    if (Vpp > 5.0f) Vpp = 5.0f;

    // 전압->코드 변환
    const float code_per_volt = F32(DAC_FULL) / 5.0f;
    const float center        = F32(DAC_MID);
    const float amplitude_raw = (Vpp * 0.5f) * code_per_volt;

    // 안전한 진폭(경계 초과 방지)
	float amp_limit_pos = F32(DAC_FULL) - center;  // 상한 여유
	float amp_limit_neg = center;                  // 하한 여유
	float amp = amplitude_raw;
	if (amp > amp_limit_pos) amp = amp_limit_pos;
	if (amp > amp_limit_neg) amp = amp_limit_neg;

	const float two_pi = 2.0f * (float)M_PI;
	const float dtheta = two_pi / (float)total_count;

	// k=0..N-1, θ = 0 + k*dtheta  (center에서 시작, 위로 상승)
	for (int k = 0; k < total_count; ++k) {
	float theta = (float)k * dtheta;
	float v = center + amp * sinf(theta);
	MData.SET.LF_MOD.data[k] = CLAMP_U16F(v);
	}
}


void generateSquareMid(u8 total_count, float Vpp)
{
    if (total_count < 2) return;

    Vpp=MData.SET.LF_MOD.amp;
    // Vpp 제한
    if (Vpp < 0.0f) Vpp = 0.0f;
    if (Vpp > 5.0f) Vpp = 5.0f;

    // 전압->코드 변환
    const float code_per_volt = F32(DAC_FULL) / 5.0f;
    const float center        = F32(DAC_MID);
    const float amplitude_raw = (Vpp * 0.5f) * code_per_volt;

    // 중심 기준 클리핑 방지(이론상 필요 없지만 안전하게 한 번 더 한계 보정)
    float amp = amplitude_raw;
    if (center + amp > F32(DAC_FULL)) amp = F32(DAC_FULL) - center;
    if (center - amp < 0.0f)          amp = center;


    u8 add=0;
    for (int k = 0; k < total_count/2; ++k) {
        MData.SET.LF_MOD.data[add++] = center - amp;
    }
    for (int k = 0; k < total_count/2; ++k) {
            MData.SET.LF_MOD.data[add++] = center + amp;
    }
}




void generateTrapezoidMid(int total_count, int rise_count, int fall_count, float Vpp)
{
    if (total_count < 2) return;
    if (rise_count < 0) rise_count = 0;
    if (fall_count < 0) fall_count = 0;
    if (rise_count + fall_count > total_count) return;

    // Vpp 제한
    if (Vpp < 0.0f) Vpp = 0.0f;
    if (Vpp > 5.0f) Vpp = 5.0f;

    // 전압 -> 코드
    const float code_per_volt = F32(DAC_FULL) / 5.0f;
    const float center        = F32(DAC_FULL) / 2.0f;     // 32767.5 (half-LSB center)
    const float amplitude_raw = (Vpp * 0.5f) * code_per_volt;

    // 경계 안전 진폭
    float amp = amplitude_raw;
    if (amp > center)                   amp = center;
    if (amp > (F32(DAC_FULL) - center)) amp = F32(DAC_FULL) - center;

    const float vmin = center - amp;    // Vpp=2.5f → 16383.75 → 16384
    const float vmax = center + amp;    // Vpp=2.5f → 49151.25 → 49151
    const float span = vmax - vmin;

    int interior = total_count - rise_count - fall_count;
    if (interior < 0) return;
    if ((interior & 1) != 0) return;    // 동일 플랫 위해 짝수 필요

    const int low_plateau  = interior / 2;
    const int high_plateau = interior / 2;

    int idx = 0;

    // Low plateau (vmin 유지)
    for (int i = 0; i < low_plateau; ++i) {
    	MData.SET.LF_MOD.data[idx++] = CLAMP_U16F(vmin);
    }

    // Rising (끝점 제외: vmin 미중복, vmax 미중복)
    if (rise_count > 0) {
        const float rstep = span / (float)(rise_count + 1);
        for (int j = 0; j < rise_count; ++j) {
            float v = vmin + (float)(j + 1) * rstep;
            MData.SET.LF_MOD.data[idx++] = CLAMP_U16F(v);
        }
    }

    // High plateau (vmax 유지)
    for (int i = 0; i < high_plateau; ++i) {
    	MData.SET.LF_MOD.data[idx++] = CLAMP_U16F(vmax);
    }

    // Falling (끝점 제외: vmax 미중복, vmin 미중복)
    if (fall_count > 0) {
        const float fstep = span / (float)(fall_count + 1);
        for (int j = 0; j < fall_count; ++j) {
            float v = vmax - (float)(j + 1) * fstep;
            MData.SET.LF_MOD.data[idx++] = CLAMP_U16F(v);
        }
    }

    // 방어적 보정 (정상이라면 idx == total_count)
    while (idx < total_count) MData.SET.LF_MOD.data[idx++] = CLAMP_U16F(vmin);
}
