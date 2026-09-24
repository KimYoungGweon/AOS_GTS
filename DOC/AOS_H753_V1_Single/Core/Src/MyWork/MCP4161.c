#define 	__MCP4161_H__
#include	"MCP4161.h"
#undef	__MCP4161_H__

#include "MyGPIOSet.h"
#include "Util.h"

#include "MCP4161.h"
#include "stm32h7xx_hal.h"

#define MCP4161_RAB_OHMS   (5000.0f)  // 5 kΩ
#define MCP4161_RW_OHMS    (75.0f)    // 와이퍼 저항(typ.)



// --- 내부 유틸 ---

#define MCP_CS_H	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET)
#define MCP_CS_L	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET)

#define MCP_SCK_H	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET)
#define MCP_SCK_L	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_RESET)

#define MCP_SDI_H	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_SET)
#define MCP_SDI_L	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_RESET)

// SPI Mode 0 (CPOL=0, CPHA=0) 비트뱅잉: 상승엣지에서 샘플


void bb_send_byte(u8 byte){
  for(int i=7;i>=0;i--){
    if(byte & (1U<<i)) MCP_SDI_H; else MCP_SDI_L;
    delay_us(1);
    MCP_SCK_H;  // 상승엣지에서 샘플됨
    delay_us(1);
    MCP_SCK_L;
  }
}

void mcp4161_write16(u8 cmd, u8 data){
  MCP_CS_L;
  delay_us(1);
  bb_send_byte(cmd);
  bb_send_byte(data);
  delay_us(1);
  MCP_CS_H;
}


// --- 공개 API ---


void MCP4161_Init(void){
  // 기본 라인 상태
  MCP_CS_H;
  MCP_SCK_L;
  MCP_SDI_L;
  // 전원 투입 후 내부 복구 시간 여유
  HAL_Delay(1);
  // 필요 시: NV -> Volatile 자동 로드됨(공장 출하 mid-scale)  :contentReference[oaicite:3]{index=3}
}

// d9_0: 0..256 (D8은 Command Byte의 LSB로 들어감)
// Command Byte 형식: [AD3:AD0][C1:C0][D9:D8]
//  - AD=0000 (Volatile Wiper 0)
//  - C1:C0=00 (Write Data)
//  - D9=0(미사용), D8=(d>>8)&1
void MCP4161_WriteWiperRaw(u16 d9_0){
  if(d9_0 > 256) d9_0 = 256;
  uint8_t D8 = (d9_0 >> 8) & 0x1;
  uint8_t data = (uint8_t)(d9_0 & 0xFF);

  uint8_t cmd = 0x00; // AD=0000, C=00
  // D9:D8 자리 중 D9=0, D8만 반영
  cmd |= (D8 & 0x1);  // LSB에 위치

  mcp4161_write16(cmd, data);
  // 참고: D8을 사용하면 Full-Scale(100h)로 W=A 직접 연결 가능  :contentReference[oaicite:4]{index=4}
}

// WB(=W-B) 레오스타트 목표 저항(Ω) -> d 계산
// R_WB ≈ R_W + (D/256)*R_AB  →  D ≈ 256*(R_WB - R_W)/R_AB
void MCP4161_SetWB_Ohms(float target_ohms){
	// 물리 한계 보정
float minR = MCP4161_RW_OHMS;                 // 이론적 최소는 R_W
float maxR = MCP4161_RW_OHMS + MCP4161_RAB_OHMS; // Full-Scale 근처
	if(target_ohms < minR) target_ohms = minR;
	if(target_ohms > maxR) target_ohms = maxR;

	float Df = 256.0f * (target_ohms - MCP4161_RW_OHMS) / MCP4161_RAB_OHMS;
	int d = (int)(Df + 0.5f); // 반올림
	if(d < 0) d = 0;
	if(d > 256) d = 256; // D8 사용 허용(=Full Scale)

	MCP4161_WriteWiperRaw((uint16_t)d);
}

// (선택) AW(=A-W) 모드가 필요한 경우
// R_AW ≈ R_W + ((256-D)/256)*R_AB  →  D ≈ 256*(1 - (R_AW - R_W)/R_AB)
void MCP4161_SetAW_Ohms(float target_ohms){
float minR = MCP4161_RW_OHMS;
float maxR = MCP4161_RW_OHMS + MCP4161_RAB_OHMS;
	if(target_ohms < minR) target_ohms = minR;
	if(target_ohms > maxR) target_ohms = maxR;

	float Df = 256.0f * (1.0f - (target_ohms - MCP4161_RW_OHMS)/MCP4161_RAB_OHMS);
	int d = (int)(Df + 0.5f);
	if(d < 0) d = 0;
	if(d > 256) d = 256;

	MCP4161_WriteWiperRaw((uint16_t)d);
}

/*
void LTC2602_INIT(void){
	DO_LTC2602_CS(1);
	DO_LTC2602_CLK(0);
	DO_LTC2602_SDI(0);
}

void LTC2602_COMMAND(u8 Ch,u16 inVal){//CH0, CH1
u8 Cmd[4];
u8 Add[4];
u16 inData;
u8 i,ret;

	inData=inVal;
	if(inData >=65535) inData=65535;
	Cmd[0]=1; Cmd[1]=1; Cmd[2]=0; Cmd[3]=0;

	switch(Ch){
		case 0:
			Add[0]=0; Add[1]=0; Add[2]=0; Add[3]=0;
			break;
		case 1:
			Add[0]=1; Add[1]=0; Add[2]=0; Add[3]=0;
			break;
		default:
			Add[0]=1; Add[1]=1; Add[2]=1; Add[3]=1;
			break;
	}

	//COMMAND SEND
	DO_LTC2602_CS(0);
	delay_us(1);

	for(i=0;i<4;i++){
		DO_LTC2602_SDI(Cmd[3-i]);
		DO_LTC2602_CLK(1);
		delay_us(2);
		DO_LTC2602_CLK(0);
	}
	delay_us(1);
	//ADD SEND
	for(i=0;i<4;i++){
		DO_LTC2602_SDI(Add[3-i]);
		DO_LTC2602_CLK(1);
		delay_us(2);
		DO_LTC2602_CLK(0);
	}
	delay_us(1);
	for(i=0;i<16;i++){
		ret=(inData >>(15-i))&0x01;
		DO_LTC2602_SDI(ret);
		DO_LTC2602_CLK(1);
		delay_us(2);
		DO_LTC2602_CLK(0);
	}
	delay_us(1);
	DO_LTC2602_CS(1);
}
*/
