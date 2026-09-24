#define	__UART_PC_H__
#include	"UART_PC.h"
#undef	__UART_PC_H__

#include "string.h"
#include "command.h"
#include "main.h"

#include "MyCTL.h"
#include "MyGPIOSet.h"
#include "main.h"
#include "CAL_CTL.h"
#include "Shallow.h"
#include "LF_GEN.h"
#include "AD9850.h"
#include "UART_LCD.h"
#include "AD7739.h"
#include "hmap.h"

u8 SOut[4000 + 10];
u8 CAL_Type = 0;



void RxMessage_CTL_PC(void) {
	u16 add = 0;
	u8 inB[4];

	MData.FLAG.HOST_Received = false;

	switch (RxQue1.cmd)
	{

		case CMD_HMAP_START:
			Buzzer_On(50);
			MData.HMAP.point_count=0;
			Hmap_OnReceive_Start(RxQue1.data);
			break;

		case CMD_HMAP_POINT_REQ:

			MData.HMAP.point_count++;
			Hmap_OnReceive_PointReq(RxQue1.data);

			break;

		case CMD_HMAP_ABORT:
			Hmap_OnReceive_Abort();
			break;

		// --- 측정 격자 설정 ---
		case CMD_HMAP_CFG_QUERY:
			TxMessage_CTL_PC(CMD_HMAP_CFG);
			break;

		case CMD_HMAP_CFG_SET:
			// 측정 중에는 격자를 바꾸지 않는다
			if (MData.HMAP.scan_running) { TxMessage_CTL_PC(CMD_HMAP_CFG); break; }
			{
				__hmap_cfg in_cfg;
				// 짧은 패킷이면 큐의 잔여 바이트를 읽게 되므로 크기부터 본다
				if (RxQue1.size >= (u16)sizeof(__hmap_cfg))
				{
					memcpy(&in_cfg, RxQue1.data, sizeof(__hmap_cfg));
					if (Hmap_Cfg_Validate(&in_cfg)) g_hmap_cfg = in_cfg;
				}
				// 거부해도 현재 값을 그대로 회신 → 호스트가 반영 여부를 확인한다
				TxMessage_CTL_PC(CMD_HMAP_CFG);
			}
			break;

		// --- 측정 순회 ---
		case CMD_HMAP_RUN_START:
			Hmap_Run_Start(RxQue1.data[0]);
			TxMessage_CTL_PC(CMD_HMAP_RUN_STATUS);   // 수락 여부는 status 로 판단
			break;

		case CMD_HMAP_RUN_CTRL:
			Hmap_Run_Ctrl(RxQue1.data[0]);
			TxMessage_CTL_PC(CMD_HMAP_RUN_STATUS);
			break;

		case CMD_HMAP_RUN_QUERY:
			TxMessage_CTL_PC(CMD_HMAP_RUN_STATUS);
			break;

		case CMD_HMAP_SAMPLE_SET:
			Hmap_Run_SetSample(RxQue1.data, RxQue1.size);
			TxMessage_CTL_PC(CMD_HMAP_RUN_STATUS);
			break;

		case CMD_HMAP_CFG_SAVE:
			if (!MData.HMAP.scan_running) {
				if (Hmap_Cfg_Save()) Buzzer_On(50);
			}
			TxMessage_CTL_PC(CMD_HMAP_CFG);   // 저장 후 실제 값 회신
			break;

		case CMD_HMAP_DONE:
			MData.HMAP.done_status=1;
			TxMessage_CTL_PC(CMD_HMAP_DONE);
			break;

			///////////////////////


		case CMD_BIAS_ONOFF:
			MData.BIAS_OnOff = RxQue1.data[add++];
			DO_BIAS(MData.BIAS_OnOff);
			Buzzer_On(50);
			break;

		case CMD_AD7739_RESET:
			ADC7739_Init2();
			Buzzer_On(50);
			break;

		case CMD_SCAN_FULL_MODE_START:
			MData.FLAG.SCAN_START=true;
			MData.ScanType = 0x02;//Full scan mode
			Buzzer_On(50);
			break;

		case CMD_SCAN_FULL_MODE_SET:

			Buzzer_On(50);

			MData.F_SCAN.type = RxQue1.data[add++];
			MData.F_SCAN.noX = RxQue1.data[add++];
			MData.F_SCAN.noY = RxQue1.data[add++];

			for(u8 i = 0 ; i < 2 ; i++) inB[i] = RxQue1.data[add++];
			memcpy(&MData.F_SCAN.w_delay,&inB,2);

			for(u8 i=0;i<MData.F_SCAN.noX;i++)
			{
				for(u8 j=0 ; j<4 ; j++) inB[j] = RxQue1.data[add++];
				memcpy(&MData.F_SCAN.xVal[i],&inB,4);
			}

			for(u8 i=0;i<MData.F_SCAN.noY;i++)
			{
				for(u8 j=0 ; j<4 ; j++) inB[j] = RxQue1.data[add++];
				memcpy(&MData.F_SCAN.yVal[i],&inB,4);
			}

			MData.F_SCAN.IsShallowMask = RxQue1.data[add++];
			MData.F_SCAN.ShallowType = RxQue1.data[add++];

			if(MData.F_SCAN.IsShallowMask)
			{
				for(u8 i=0 ; i<MData.F_SCAN.noY ; i++)
				{
					for(u8 j=0 ; j<MData.F_SCAN.noX ; j++)
					{
						for(u8 k=0 ; k<2 ; k++) inB[k] = RxQue1.data[add++];
						memcpy(&MData.F_SCAN.Shallow[i][j], &inB, 2);
					}
				}
			}

			TxMessage_CTL_PC(CMD_SCAN_FULL_MODE_SET_RETURN);
			break;

		case CMD_SCAN_FULL_MODE_RESULT:
			TxMessage_CTL_PC(CMD_SCAN_RESULT);
			break;


		case CMD_CURRENT_TYPE:
			MData.SET.Current_Type = RxQue1.data[add++];
			Buzzer_On(20);
			break;

		case CMD_SHALLOW_LINE_FIND:

			MData.SHA_Line.type = RxQue1.data[add++];//rising, smart

			for (u8 i = 0; i < 4; i++)
			inB[i] = RxQue1.data[add++];
			memcpy(&MData.SHA_Line.sVolt, &inB, 4);

			for (u8 i = 0; i < 4; i++)
			inB[i] = RxQue1.data[add++];
			memcpy(&MData.SHA_Line.fVolt, &inB, 4);

			for (u8 i = 0; i < 4; i++)
			inB[i] = RxQue1.data[add++];
			memcpy(&MData.SHA_Line.stepVolt, &inB, 4);

			for (u8 i = 0; i < 2; i++)
			inB[i] = RxQue1.data[add++];
			memcpy(&MData.SHA_Line.ref_Is, &inB, 4);

			for (u8 i = 0; i < 4; i++)
			inB[i] = RxQue1.data[add++];
			memcpy(&MData.SHA_Line.duty, &inB, 4);

			for (u8 i = 0; i < 2; i++)
			inB[i] = RxQue1.data[add++];
			memcpy(&MData.SHA_Line.wait_delay, &inB, 2);

			MData.SHA_Line.procTime_msec=0;
			MData.SHA_Line.no=0;//reset
			MData.SHA_Line.IsStart = true;

			Buzzer_On(20);
			break;

		case CMD_SHALLOW_SINGLE_POINT_FIND:

			MData.SHA_Single.type = RxQue1.data[add++];//rising, smart
			MData.SHA_Single.IsNegative = RxQue1.data[add++];//rising, smart

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SHA_Single.sVolt, &inB, 4);

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SHA_Single.fVolt, &inB, 4);

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SHA_Single.stepVolt, &inB, 4);

			for (u8 i = 0; i < 2; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SHA_Single.Ref_Is_adc, &inB, 2);

			for (u8 i = 0; i < 2; i++) inB[i] = RxQue1.data[add++];
			memcpy(&MData.SHA_Single.wait_delay, &inB, 2);

			MData.SHA_Single.IsStart = true;

			Buzzer_On(20);

			break;

		case CMD_ATUTO_STATUS_ONOFF:
			MData.IsAutoCurrentSend = RxQue1.data[add++];
			TxMessage_CTL_PC(CMD_RECEIVED_OK);
			Buzzer_On(20);
			break;

		case CMD_HF_MOD_SET:
			MData.SET.RF_MOD_OnOff = RxQue1.data[add++];

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.RF_MOD_frq, &inB, 4);

			if (MData.SET.RF_MOD_OnOff)	ad9850_send_freq(MData.SET.RF_MOD_frq, 124999050); //124999010
			else ad9850_send_freq(0, 124999050); //124999010

			Buzzer_On(20);

			break;

		case CMD_LF_MOD_SET:

			MData.SET.LF_MOD.type = RxQue1.data[add++];
			MData.SET.LF_MOD.OnOff = RxQue1.data[add++];

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.LF_MOD.amp, &inB, 4);

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.LF_MOD.frq, &inB, 4);

			LF_Modulator_Set(MData.SET.LF_MOD.type);

			Buzzer_On(20);
			break;

		case CMD_SET_QUERY:
			TxMessage_CTL_PC(CMD_SET_QUERY);
			break;

		case CMD_SCAN_RESULT:
			TxMessage_CTL_PC(CMD_SCAN_RESULT);
			break;

		case CMD_CV_STATUS:
			TxMessage_CTL_PC(CMD_CV_STATUS);
			break;

		case CMD_SET_CONTROL:
			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.RF_HV, &inB, 4);

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.Frq, &inB, 4);

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.Duty, &inB, 4);

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.CV, &inB, 4);

			//Buzzer_On(20);

			break;

		case CMD_SET_CONTROL_ALL:
			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.RF_HV, &inB, 4);

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.Frq, &inB, 4);

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.Duty, &inB, 4);

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.CV, &inB, 4);

			MData.SET.RF_MOD_OnOff = RxQue1.data[add++];
			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.RF_MOD_frq, &inB, 4);

			MData.SET.LF_MOD.type = RxQue1.data[add++];
			MData.SET.LF_MOD.OnOff = RxQue1.data[add++];

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.LF_MOD.amp, &inB, 4);

			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.LF_MOD.frq, &inB, 4);

			LF_Modulator_Set(MData.SET.LF_MOD.type);
			Buzzer_On(20);
			break;

		case CMD_CV_TIME_SET:
			for (u8 i = 0; i < 2; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SendCountNo, &inB, 2);

			if (MData.SendCountNo == 0)
				TxMessage_CTL_PC(CMD_CV_STATUS);
			break;

		case CMD_CV_SET: //CV Value
			for (u8 i = 0; i < 4; i++) inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.CV, &inB, 4);
			Buzzer_On(20);

			//CV_Control(MData.SET.CV);
			if (MData.SendCountNo == 0) TxMessage_CTL_PC(CMD_CV_STATUS);
			break;

		case CMD_SET_Volt:
			for (u8 i = 0; i < 4; i++) inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.RF_HV, &inB, 4);
			HV_SET_Control(MData.SET.RF_HV);
			//LCD_Data_View(eLCD_HV);
			TxMessage_CTL_PC(CMD_RECEIVED_OK);
			break;

		case CMD_FREQUENCY_SET:
			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.Frq, &inB, 4);
			break;

		case CMD_DUTY_SET:
			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&MData.SET.Duty, &inB, 4);
			break;

		case CMD_D_SCAN_SET:
			MData.D_SCAN.No = RxQue1.data[add++];
			MData.D_SCAN.xNo = RxQue1.data[add++]; //단순저
			MData.D_SCAN.yNo = RxQue1.data[add++];
			for (u8 i = 0; i < MData.D_SCAN.No; i++) {
				for (u8 j = 0; j < 4; j++)
					inB[j] = RxQue1.data[add++];
				memcpy(&MData.D_SCAN.PARA[i].hv, &inB, 4);

				for (u8 j = 0; j < 4; j++)
					inB[j] = RxQue1.data[add++];
				memcpy(&MData.D_SCAN.PARA[i].frq, &inB, 4);

				for (u8 j = 0; j < 4; j++)
					inB[j] = RxQue1.data[add++];
				memcpy(&MData.D_SCAN.PARA[i].duty, &inB, 4);

				for (u8 j = 0; j < 4; j++)
					inB[j] = RxQue1.data[add++];
				memcpy(&MData.D_SCAN.PARA[i].cv, &inB, 4);

				for (u8 j = 0; j < 2; j++)
					inB[j] = RxQue1.data[add++];
				memcpy(&MData.D_SCAN.PARA[i].delay, &inB, 2);
			}
			Buzzer_On(200);
			break;

		case CMD_SCAN_SET:

			MData.SCAN.type = RxQue1.data[add++];
			MData.SCAN.IsShallowMask = RxQue1.data[add++];
			MData.SCAN.ShallowType = RxQue1.data[add++]; /////NEW 2026.02.06

			for (u8 i = 0; i < 2; i++)	inB[i] = RxQue1.data[add++];
			memcpy(&MData.SCAN.no, &inB, 2);

			for (u8 i = 0; i < 2; i++)	inB[i] = RxQue1.data[add++];
			memcpy(&MData.SCAN.w_delay, &inB, 2);

			for (u8 i = 0; i < 4; i++) inB[i] = RxQue1.data[add++];
			memcpy(&MData.SCAN.duty, &inB, 4);
			MData.SET.Duty = MData.SCAN.duty;

			for (u8 i = 0; i < 4; i++) inB[i] = RxQue1.data[add++];
			memcpy(&MData.SCAN.startX, &inB, 4);

			for (u8 i = 0; i < 4; i++) inB[i] = RxQue1.data[add++];
			memcpy(&MData.SCAN.stepX, &inB, 4);

			if(MData.SCAN.type == eFRQ_HV) Wave_Frq_and_Duty_Update(MData.SCAN.Start_Frq, MData.SCAN.duty);


			MData.SCAN.IsStart = true;
			TxMessage_CTL_PC(CMD_SCAN_SET_RETURN);
			Buzzer_On(200);
			break;

		case CMD_SCAN_START:

			MData.ScanType = RxQue1.data[add++];
			MData.D_SCAN.Mode = RxQue1.data[add++];

			if (MData.SCAN.IsShallowMask) {
				CSHALLOW.no = RxQue1.data[add++];
				for (u8 i = 0; i < CSHALLOW.no; i++)
				{
					for (u8 j = 0; j < 4; j++) inB[j] = RxQue1.data[add++];
					memcpy(&CSHALLOW.X[i], &inB, 4);
					for (u8 j = 0; j < 4; j++) inB[j] = RxQue1.data[add++];
					memcpy(&CSHALLOW.Y[i], &inB, 4);
				}
			}

			TxMessage_CTL_PC(CMD_SCAN_START_RETURN);
			break;
		case CMD_SCAN_START_OK:
			if (MData.ScanType == 0) MData.SCAN.IsStart = true;
			else Buzzer_On(50);

			MData.FLAG.SCAN_START = true;
			TxMessage_CTL_PC(CMD_SCAN_START_OK_RETURN);
			break;

		case CMD_SCAN_STOP:
			MData.SCAN.IsStart = false;
			TxMessage_CTL_PC(CMD_SCAN_STOP_RETURN);
			Buzzer_On(200);
			break;

		case CMD_FRQ_CAL_SET:
			FRQ_CAL_CTL(); //480kHz //Default Set
			break;

		case CMD_FRQ_CAL_SAVE:
			u32 Frq_Val;
			for (u8 i = 0; i < 4; i++)
				inB[i] = RxQue1.data[add++];
			memcpy(&Frq_Val, &inB, 4);
			CAL_FRQ_Save(Frq_Val);
			MData.BUS_CLOCK_CAL = CAL_FRQ_Read();
			break;

		case CMD_CAL_WRITE:
			CAL_Type = RxQue1.data[add++];
			switch (CAL_Type) {
			case 0:
				CSET.No = RxQue1.data[add++];
				for (u8 i = 0; i < CSET.No; i++) {
					for (u8 j = 0; j < 4; j++)
						inB[j] = RxQue1.data[add++];
					memcpy(&CSET.X[i], &inB, 4);

					for (u8 j = 0; j < 2; j++)
						inB[j] = RxQue1.data[add++];
					memcpy(&CSET.Y[i], &inB, 2);
				}
				break;
			case 1:
				CSENSE_VS.No = RxQue1.data[add++];
				for (u8 i = 0; i < CSENSE_VS.No; i++) {
					for (u8 j = 0; j < 2; j++)
						inB[j] = RxQue1.data[add++];
					memcpy(&CSENSE_VS.X[i], &inB, 2);

					for (u8 j = 0; j < 4; j++)
						inB[j] = RxQue1.data[add++];
					memcpy(&CSENSE_VS.Y[i], &inB, 4);
				}
				break;
			case 2:
				CSENSE_IS.No = RxQue1.data[add++];
				for (u8 i = 0; i < CSENSE_IS.No; i++) {
					for (u8 j = 0; j < 2; j++)
						inB[j] = RxQue1.data[add++];
					memcpy(&CSENSE_IS.X[i], &inB, 2);

					for (u8 j = 0; j < 4; j++)
						inB[j] = RxQue1.data[add++];
					memcpy(&CSENSE_IS.Y[i], &inB, 4);
				}
				break;
			case 3:
				CSENSE_FAN_VS.No = RxQue1.data[add++];
				for (u8 i = 0; i < CSENSE_FAN_VS.No; i++) {
					for (u8 j = 0; j < 2; j++)
						inB[j] = RxQue1.data[add++];
					memcpy(&CSENSE_FAN_VS.X[i], &inB, 2);

					for (u8 j = 0; j < 4; j++)
						inB[j] = RxQue1.data[add++];
					memcpy(&CSENSE_FAN_VS.Y[i], &inB, 4);
				}
				break;
			case 4:
				CSET_CV.No = RxQue1.data[add++];
				for (u8 i = 0; i < CSET_CV.No; i++) {
					for (u8 j = 0; j < 4; j++)
						inB[j] = RxQue1.data[add++];
					memcpy(&CSET_CV.X[i], &inB, 4);

					for (u8 j = 0; j < 2; j++)
						inB[j] = RxQue1.data[add++];
					memcpy(&CSET_CV.Y[i], &inB, 2);
				}
				break;
			}
			CAL_Data_Write(CAL_Type);
			CAL_Data_Read(CAL_Type);
			break;
		case CMD_CAL_QUERY:
			CAL_Type = RxQue1.data[add++];
			CAL_Data_Read(CAL_Type);
			TxMessage_CTL_PC(CMD_CAL_QUERY);
			break;
		}
}


void TxMessage_CTL_PC(u8 cmd) {
u16 add = 0;
u8 inB[4];
u8 no=0;
	switch (cmd)
	{
		case CMD_HMAP_POINT_DATA:    // 12 byte payload
			//  0 u8    status
			//  1 u8    reserved
			//  2 u16   Is_P (ADC raw)   ★ 구 4ch → 1ch
			//  4 float LFV (실제 적용값)
			//  8 float CV  (실제 적용값)
			SOut[add++] = MData.HMAP.point_status;
			SOut[add++] = 0;
			SOut[add++] =  MData.HMAP.point_adc        & 0xFF;
			SOut[add++] = (MData.HMAP.point_adc >> 8)  & 0xFF;
			memcpy(&SOut[add], &MData.HMAP.lfv_start, 4); add += 4;
			memcpy(&SOut[add], &MData.HMAP.cv_start,  4); add += 4;
			break;

		case CMD_HMAP_DATA:
			// header 8 byte
			//  0 u8  SectionY   ★ 구 코드는 done_status 를 보냈다. 규격대로 정정.
			//  1 u8  SectionX      (1600장이 흘러올 때 어느 heatmap 인지 식별하는 토큰)
			//  2 u16 noCV
			//  4 u16 noLFV
			//  6 u8  HeatmapY  ★ 구 reserved. Section 안의 LFF 인덱스
			//  7 u8  HeatmapX  ★ 구 reserved. Section 안의 Duty 인덱스
			// data : noCV * noLFV * 2 byte  (Is_P 만, iy=CV 외부 / ix=LFV 내부)
			//        16x11 기준 352 byte → UDP 1 datagram 에 들어간다.
			//
			// Section 좌표만으로는 heatmap 을 특정할 수 없다. 한 Section 안에
			// Duty x LFF = 16 장이 있기 때문이다. 1,600장이 연속으로 올라오는
			// run 모드에서 서버가 cond_idx 를 복원하려면 네 좌표가 모두 필요하다.
			SOut[add++] = MData.HMAP.section_y;
			SOut[add++] = MData.HMAP.section_x;

			SOut[add++] = (u8)(MData.HMAP.cv_count % 256);
			SOut[add++] = (u8)(MData.HMAP.cv_count / 256);

			SOut[add++] = (u8)(MData.HMAP.lfv_count % 256);
			SOut[add++] = (u8)(MData.HMAP.lfv_count / 256);

			SOut[add++] = MData.HMAP.heatmap_y;
			SOut[add++] = MData.HMAP.heatmap_x;

			for (u16 i = 0; i < MData.HMAP.cv_count; i++)
			{
				for (u16 j = 0; j < MData.HMAP.lfv_count; j++)
				{
					u16 v = MData.HMAP.map_buf[i][j];
					SOut[add++] =  v        & 0xFF;
					SOut[add++] = (v >> 8)  & 0xFF;
				}
			}
			break;
		case CMD_HMAP_RUN_STATUS:    // 20 byte
			//  0 u8  state (0=idle 1=running 2=paused 3=done 4=aborted)
			//  1 u8  mode  (0=full 1=hour1 2=sample)
			//  2 u16 total
			//  4 u16 done_count
			//  6 u16 index        (다음에 측정할 cond_idx)
			//  8 u8  sy   9 u8 sx   10 u8 hy   11 u8 hx
			// 12 u32 elapsed_ms
			// 16 u8  scan_running
			// 17 u8  sample_count
			// 18 u16 reserved
			SOut[add++] = g_hmap_run.state;
			SOut[add++] = g_hmap_run.mode;
			SOut[add++] =  g_hmap_run.total        & 0xFF;
			SOut[add++] = (g_hmap_run.total  >> 8) & 0xFF;
			SOut[add++] =  g_hmap_run.done_count        & 0xFF;
			SOut[add++] = (g_hmap_run.done_count  >> 8) & 0xFF;
			SOut[add++] =  g_hmap_run.index        & 0xFF;
			SOut[add++] = (g_hmap_run.index  >> 8) & 0xFF;
			SOut[add++] = g_hmap_run.sy;
			SOut[add++] = g_hmap_run.sx;
			SOut[add++] = g_hmap_run.hy;
			SOut[add++] = g_hmap_run.hx;
			{
				u32 el = (g_hmap_run.state == HMAP_RUN_IDLE)
				       ? 0 : (u32)(HAL_GetTick() - g_hmap_run.t_start);
				SOut[add++] =  el        & 0xFF;
				SOut[add++] = (el >>  8) & 0xFF;
				SOut[add++] = (el >> 16) & 0xFF;
				SOut[add++] = (el >> 24) & 0xFF;
			}
			SOut[add++] = MData.HMAP.scan_running;
			SOut[add++] = g_hmap_run.sample_count;
			SOut[add++] = 0;
			SOut[add++] = 0;
			break;

		case CMD_HMAP_CFG:           // sizeof(__hmap_cfg) — 약 240 byte
			// 구조체를 그대로 싣는다. STM32 와 서버가 같은 배치를 쓰도록
			// AOS_H753_V1_Single_프로그램_수정_사항_정리.md 에 필드 순서를 명시해 둘 것.
			memcpy(&SOut[add], &g_hmap_cfg, sizeof(__hmap_cfg));
			add += (u16)sizeof(__hmap_cfg);
			break;

		case CMD_HMAP_DONE:          // 8 byte payload
			SOut[add++] = MData.HMAP.done_status;
			SOut[add++] = 0;
			SOut[add++] =  MData.HMAP.done_lines_done       & 0xFF;
			SOut[add++] = (MData.HMAP.done_lines_done >> 8) & 0xFF;
			SOut[add++] =  MData.HMAP.done_elapsed_ms        & 0xFF;
			SOut[add++] = (MData.HMAP.done_elapsed_ms >> 8)  & 0xFF;
			SOut[add++] = (MData.HMAP.done_elapsed_ms >> 16) & 0xFF;
			SOut[add++] = (MData.HMAP.done_elapsed_ms >> 24) & 0xFF;

		    break;

		// [삭제] 구 CMD_TWIN_SCAN_DATA (0x81) — line 단위 legacy 전송.
		//        batch CMD_HMAP_DATA(0x86) 로 일원화됨.

		case CMD_SCAN_FULL_MODE_SET_RETURN:
			SOut[add++] = MData.F_SCAN.type;
			SOut[add++] = MData.F_SCAN.noX;
			SOut[add++] = MData.F_SCAN.noY;

			memcpy(&inB, &MData.F_SCAN.w_delay, 2);
			for(u8 i=0;i<2;i++) SOut[add++] = inB[i];


			for(u8 i=0;i<MData.F_SCAN.noX;i++)
			{
				memcpy(&inB, &MData.F_SCAN.xVal[i], 4);
				for(u8 j=0;j<4;j++) SOut[add++] = inB[j];
			}

			for(u8 i=0;i<MData.F_SCAN.noY;i++)
			{
				memcpy(&inB, &MData.F_SCAN.yVal[i], 4);
				for(u8 j=0;j<4;j++) SOut[add++] = inB[j];
			}

			SOut[add++] = MData.F_SCAN.IsShallowMask;
			SOut[add++] = MData.F_SCAN.ShallowType;

			if(MData.F_SCAN.IsShallowMask)
			{
				for(u8 i=0;i<MData.F_SCAN.noY;i++)
				{
					for(u8 j=0;j<MData.F_SCAN.noX;j++)
					{
						memcpy(&inB, &MData.F_SCAN.Shallow[i][j],2);
						for(u8 k=0;k<2;k++) SOut[add++] = inB[k];
					}
				}
			}
			break;

		case CMD_SCAN_FULL_MODE_RESULT:
			for (u16 i = 0; i < MData.F_SCAN.noY; i++)
			{
				for (u16 j = 0; j < MData.F_SCAN.noX; j++)
				{
					memcpy(&inB, &MData.F_SCAN.Curr[i][j], 2);
					for (u8 k = 0; k < 2; k++) SOut[add++] = inB[k];
				}
			}
			break;

		case CMD_SHALLOW_SINGLE_POINT_FIND:
			SOut[add++] = MData.SHA_Single.IsFound;

			memcpy(&inB, &MData.SHA_Single.Current, 2);
			for (u8 i = 0; i < 2; i++) SOut[add++] = inB[i];

			memcpy(&inB, &MData.SHA_Single.ret_Volt, 4);
			for (u8 i = 0; i < 4; i++) SOut[add++] = inB[i];
			break;

		case CMD_SHALLOW_LINE_RESULT:
			no = MData.SHA_Line.no;
			SOut[add++] = no;
			SOut[add++] = MData.SHA_Line.IsFound[no];

			memcpy(&inB, &MData.SHA_Line.ret_volt[no], 4);
			for (u8 i = 0; i < 4; i++) SOut[add++] = inB[i];

			memcpy(&inB, &MData.SHA_Line.Is[no], 2);
			for (u8 i = 0; i < 2; i++) SOut[add++] = inB[i];
			break;

		case CMD_SHALLOW_LINE_RESULT_ALL:
			for(u8 i=0;i<25;i++)
			{
				SOut[add++] = MData.SHA_Line.IsFound[i];

				memcpy(&inB, &MData.SHA_Line.ret_volt[i], 4);
				for (u8 j = 0; j < 4; j++) SOut[add++] = inB[j];

				memcpy(&inB, &MData.SHA_Line.Is[i], 2);
				for (u8 j = 0; j < 2; j++) SOut[add++] = inB[j];
			}
			memcpy(&inB, &MData.SHA_Line.procTime_msec, 4);
			for (u8 i = 0; i < 4; i++) SOut[add++] = inB[i];

			break;

		case CMD_SCAN_SET_RETURN:
			break;

		case CMD_SCAN_START_RETURN:
			SOut[add++] = MData.ScanType;
			SOut[add++] = MData.D_SCAN.Mode;
			if (MData.SCAN.IsShallowMask)
			{
				SOut[add++] = CSHALLOW.no;
				for (u8 i = 0; i < CSHALLOW.no; i++)
				{
					memcpy(&inB, &CSHALLOW.X[i], 4);
					for (u8 j = 0; j < 4; j++) SOut[add++] = inB[j];

					memcpy(&inB, &CSHALLOW.Y[i], 4);
					for (u8 j = 0; j < 4; j++) SOut[add++] = inB[j];
				}
			}
			break;
		case CMD_SCAN_STOP_RETURN:
			break;

		case CMD_SCAN_START_OK_RETURN:
			break;


		case CMD_RECEIVED_OK:
			break;

		case CMD_STATUS_QUERY:

			SOut[add++] = 0x01;//Device AOS (Single)

			memcpy(&inB, &MData.adcAvg_P, 2);
			for (u8 i = 0; i < 2; i++) SOut[add++] = inB[i];

			memcpy(&inB, &MData.adcAvg_N, 2);
			for (u8 i = 0; i < 2; i++) SOut[add++] = inB[i];

			memcpy(&inB, &MData.TW_adcAvg_P, 2);
			for (u8 i = 0; i < 2; i++) SOut[add++] = inB[i];

			memcpy(&inB, &MData.TW_adcAvg_N, 2);
			for (u8 i = 0; i < 2; i++) SOut[add++] = inB[i];


			memcpy(&inB, &MData.SENSE.HV_Vs, 4);
			for (u8 i = 0; i < 4; i++)
				SOut[add++] = inB[i];

			memcpy(&inB, &MData.SENSE.FAN_Vs, 4);
			for (u8 i = 0; i < 4; i++)
				SOut[add++] = inB[i];

			memcpy(&inB, &MData.SENSE.ION_Bias_Vs, 4);
			for (u8 i = 0; i < 4; i++)
				SOut[add++] = inB[i];

			break;

		case CMD_SET_QUERY:

			memcpy(&inB, &MData.SET.RF_HV, 4);
			for (u8 i = 0; i < 4; i++)
				SOut[add++] = inB[i];

			memcpy(&inB, &MData.SET.CV, 4);
			for (u8 i = 0; i < 4; i++)
				SOut[add++] = inB[i];

			memcpy(&inB, &MData.SET.Frq, 4);
			for (u8 i = 0; i < 4; i++)
				SOut[add++] = inB[i];

			memcpy(&inB, &MData.SET.Duty, 4);
			for (u8 i = 0; i < 4; i++)
				SOut[add++] = inB[i];

			//HF Mod
			SOut[add++] = MData.SET.RF_MOD_OnOff;
			memcpy(&inB, &MData.SET.RF_MOD_frq, 4);
			for (u8 i = 0; i < 4; i++)
				SOut[add++] = inB[i];

			//LF MOD
			SOut[add++] = MData.SET.LF_MOD.OnOff;
			SOut[add++] = MData.SET.LF_MOD.type;

			memcpy(&inB, &MData.SET.LF_MOD.amp, 4);
			for (u8 i = 0; i < 4; i++)
				SOut[add++] = inB[i];

			memcpy(&inB, &MData.SET.LF_MOD.frq, 4);
			for (u8 i = 0; i < 4; i++)
				SOut[add++] = inB[i];

			SOut[add++] = MData.SET.Current_Type;

			//memcpy(&inB,&MData.SET.FAN,4);
			//for(u8 i=0; i<4; i++) SOut[add++] = inB[i];

			break;

		case CMD_CV_STATUS:

			memcpy(&inB, &MData.adcAvg_P, 2);
			for (u8 i = 0; i < 2; i++)
				SOut[add++] = inB[i];

			memcpy(&inB, &MData.CV, 4);
			for (u8 i = 0; i < 4; i++)
				SOut[add++] = inB[i];

			memcpy(&inB, &MData.SendCountNo, 2);
			for (u8 i = 0; i < 2; i++)
				SOut[add++] = inB[i];

			//DMA Average Send (2025.05.06)
			for (u8 i = 0; i < 10; i++) {
				memcpy(&inB, &MData.Buf_Avg[i], 2);
				for (u8 j = 0; j < 2; j++)
					SOut[add++] = inB[j];
			}
			break;
		case CMD_D_SCAN_RESULT:
			SOut[add++] = MData.D_SCAN.Mode;
			SOut[add++] = MData.D_SCAN.No;

			for (u8 i = 0; i < MData.D_SCAN.No; i++) {
				memcpy(&inB, &MData.D_SCAN.Curr[i], 2);
				for (u8 j = 0; j < 2; j++)
					SOut[add++] = inB[j];

				switch (MData.D_SCAN.Mode) {
				case 0:
					break;
				case 1:			//delay test
					for (u8 j = 0; j < 17; j++) {
						memcpy(&inB, &MData.D_SCAN.PARA[i].d_is[j], 2);
						for (u8 k = 0; k < 2; k++)
							SOut[add++] = inB[k];
					}
					break;
				case 2:			//smart delay
					memcpy(&inB, &MData.D_SCAN.PARA[i].det_delay, 2);
					for (u8 k = 0; k < 2; k++)
						SOut[add++] = inB[k];
					break;
				}
			}
			break;

		case CMD_SCAN_RESULT:

			for (u16 i = 0; i < MData.SCAN.no; i++)
			{
				memcpy(&inB, &MData.SCAN.Curr[i], 2);
				for (u8 j = 0; j < 2; j++) SOut[add++] = inB[j];
			}

			if(MData.SET.Current_Type>1)
			{
				for (u16 i = 0; i < MData.SCAN.no; i++)
				{
					memcpy(&inB, &MData.SCAN.Curr2[i], 2);
					for (u8 j = 0; j < 2; j++) SOut[add++] = inB[j];
				}
			}

			break;

		case CMD_SHALLOW_RESULT:
			SOut[add++] = MData.Shallow.IsFound;
			memcpy(&inB, &MData.Shallow.IS, 2);
			for (u8 i = 0; i < 2; i++)
				SOut[add++] = inB[i];
			memcpy(&inB, &MData.Shallow.val_ret, 4);
			for (u8 i = 0; i < 4; i++)
				SOut[add++] = inB[i];
			break;

		case CMD_CAL_QUERY:
			SOut[add++] = CAL_Type;
			switch (CAL_Type) {
			case 0:
				SOut[add++] = CSET.No;
				for (u8 i = 0; i < CSET.No; i++) {
					memcpy(&inB, &CSET.X[i], 4);
					for (u8 j = 0; j < 4; j++)
						SOut[add++] = inB[j];

					memcpy(&inB, &CSET.Y[i], 2);
					for (u8 j = 0; j < 2; j++)
						SOut[add++] = inB[j];
				}
				break;
			case 1:
				SOut[add++] = CSENSE_VS.No;
				for (u8 i = 0; i < CSENSE_VS.No; i++) {
					memcpy(&inB, &CSENSE_VS.X[i], 2);
					for (u8 j = 0; j < 2; j++)
						SOut[add++] = inB[j];

					memcpy(&inB, &CSENSE_VS.Y[i], 4);
					for (u8 j = 0; j < 4; j++)
						SOut[add++] = inB[j];
				}
				break;
			case 2:
				SOut[add++] = CSENSE_IS.No;
				for (u8 i = 0; i < CSENSE_IS.No; i++) {
					memcpy(&inB, &CSENSE_IS.X[i], 2);
					for (u8 j = 0; j < 2; j++)
						SOut[add++] = inB[j];

					memcpy(&inB, &CSENSE_IS.Y[i], 4);
					for (u8 j = 0; j < 4; j++)
						SOut[add++] = inB[j];
				}
				break;
			case 3:
				SOut[add++] = CSENSE_FAN_VS.No;
				for (u8 i = 0; i < CSENSE_FAN_VS.No; i++) {
					memcpy(&inB, &CSENSE_FAN_VS.X[i], 2);
					for (u8 j = 0; j < 2; j++)
						SOut[add++] = inB[j];

					memcpy(&inB, &CSENSE_FAN_VS.Y[i], 4);
					for (u8 j = 0; j < 4; j++)
						SOut[add++] = inB[j];
				}
				break;
			case 4:
				SOut[add++] = CSET_CV.No;
				for (u8 i = 0; i < CSET_CV.No; i++) {
					memcpy(&inB, &CSET_CV.X[i], 4);
					for (u8 j = 0; j < 4; j++)
						SOut[add++] = inB[j];

					memcpy(&inB, &CSET_CV.Y[i], 2);
					for (u8 j = 0; j < 2; j++)
						SOut[add++] = inB[j];
				}
				break;
			}
			break;
	}

	TxMSG_PC(cmd, add);
}

//***************** Basic Functions *********************//
void Buf_Init_PC(void) {
	RxQue1.Head = RxQue1.Tail = 0;
	RxQue1.status = eCMD_STX;
	//RxQue1.count=0;
	//RxQue1.size = 0;
	//RxQue1.chksum = 0;
	//RxQue1.cmd = 0;
}

u8 Rx_GetData_PC(u8 *ch) {
	if (RxQue1.Tail != RxQue1.Head) {
		*ch = RxQue1.Buf[RxQue1.Tail++];
		RxQue1.Tail %= CMD_QUEUE_SIZE;
		return 1;
	}
	return 0;
}

char Rx_MSG_CTL_PC(void) {
u8 d;

	while (1)
	{
		if (RxQue1.status != eCMD_STX && MData.TM.uart1 >= 100)	Buf_Init_PC();
		if (Rx_GetData_PC(&d) == 0)	break;

		switch (RxQue1.status) {
			case eCMD_STX:
				if (d == 0x02)
					RxQue1.status = eCMD_CMD;
				RxQue1.chksum = 0;
				break;

			case eCMD_CMD:
				RxQue1.status = eCMD_SIZE1;
				RxQue1.cmd = d;
				RxQue1.chksum += d;
				break;

			case eCMD_SIZE1:
				RxQue1.status = eCMD_SIZE2;
				RxQue1.size = d;
				RxQue1.chksum += d;
				RxQue1.count = 0;
				break;

			case eCMD_SIZE2:
				RxQue1.status = eCMD_DATA;
				RxQue1.size += (u16) d * 256;
				RxQue1.chksum += d;
				RxQue1.count = 0;
				if (RxQue1.size == 0)
					RxQue1.status = eCMD_CS;
				break;

			case eCMD_DATA:
				RxQue1.data[RxQue1.count++] = d;
				RxQue1.chksum += d;
				if (RxQue1.count >= RxQue1.size)
					RxQue1.status = eCMD_CS;
				break;

			case eCMD_CS:
				RxQue1.status = eCMD_STX;
				if (d == 255 - RxQue1.chksum) {
					return 1;
				}
				break;
		}
	}

	return 0;
}

void TxMSG_PC(u8 cmd, u16 size) {
	u8 inDat[5];
	u8 CheckSum = 0;
	u8 inB[4];

	inDat[0] = 0x02;
	inDat[1] = cmd;
	inDat[2] = size % 256;
	inDat[3] = size / 256;

	for (u16 i = 1; i < 4; i++)
		CheckSum += inDat[i];
	for (u16 i = 0; i < size; i++)
		CheckSum += SOut[i];

	inB[0] = (~CheckSum) & 0xff;

	//inB[0] = inDat[4];

	UART_Data_Send(4, inDat);
	UART_Data_Send(size, SOut);
	UART_Data_Send(1, inB);

}

