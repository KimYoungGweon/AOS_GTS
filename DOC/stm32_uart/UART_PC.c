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
#include "twin.h"

u8 SOut[4000 + 10];
u8 CAL_Type = 0;



void RxMessage_CTL_PC(void) {
	u16 add = 0;
	u8 inB[4];

	MData.FLAG.HOST_Received = false;

	switch (RxQue1.cmd)
	{

		case CMD_TWIN_SCAN_START:
			Buzzer_On(50);
			MData.TWIN.point_count=0;
			Twin_OnReceive_ScanStart(RxQue1.data);
			break;

		case CMD_TWIN_MEASURE_POINT:

			MData.TWIN.point_count++;
			Twin_OnReceive_MeasurePoint(RxQue1.data);

			break;

		case CMD_TWIN_SCAN_ABORT:
			Twin_OnReceive_ScanAbort();
			break;

		case CMD_TWIN_SCAN_DONE:
			MData.TWIN.done_status=1;
			TxMessage_CTL_PC(CMD_TWIN_SCAN_DONE);
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
		case CMD_TWIN_POINT_DATA:    // 16 byte payload
			SOut[add++] = MData.TWIN.point_status;
			for (u8 ch = 0; ch < 2; ch++)
			{
				u16 v = MData.TWIN.point_adc[ch];
				SOut[add++] =  v        & 0xFF;
				SOut[add++] = (v >> 8)  & 0xFF;
			}
			break;

		/*
		case CMD_TWIN_SCAN_DONE:     // 8 byte payload

			SOut[add++] = MData.TWIN.done_status;
			SOut[add++] = 0;
			SOut[add++] =  MData.TWIN.done_lines_done       & 0xFF;
			SOut[add++] = (MData.TWIN.done_lines_done >> 8) & 0xFF;
			SOut[add++] =  MData.TWIN.done_elapsed_ms        & 0xFF;
			SOut[add++] = (MData.TWIN.done_elapsed_ms >> 8)  & 0xFF;
			SOut[add++] = (MData.TWIN.done_elapsed_ms >> 16) & 0xFF;
			SOut[add++] = (MData.TWIN.done_elapsed_ms >> 24) & 0xFF;

		    break;
		 */
		case CMD_TWIN_SCAN_DATA_ALL:
			SOut[add++] = MData.TWIN.done_status;
			SOut[add++] = 0;

			SOut[add++] = (u8)(MData.TWIN.cv_count % 256);
			SOut[add++] = (u8)(MData.TWIN.cv_count / 256);

			SOut[add++] = (u8)(MData.TWIN.lfv_count % 256);
			SOut[add++] = (u8)(MData.TWIN.lfv_count / 256);

			SOut[add++] = 0;
			SOut[add++] = 0;

			for(u16 i=0;i<MData.TWIN.cv_count;i++)
			{
				for (u16 j = 0; j < MData.TWIN.lfv_count; j++)
				{
					for (u8 ch = 0; ch < 2; ch++)
					{
						u16 v = MData.TWIN.line_all_buf[i][j][ch];
						SOut[add++] = v        & 0xFF;
						SOut[add++] = (v >> 8)  & 0xFF;
					}
				}
			}
			break;
		case CMD_TWIN_SCAN_DONE:     // 8 byte payload + Scan All Data //

			SOut[add++] = MData.TWIN.done_status;
			SOut[add++] = 0;
			SOut[add++] =  MData.TWIN.done_lines_done       & 0xFF;
			SOut[add++] = (MData.TWIN.done_lines_done >> 8) & 0xFF;
			SOut[add++] =  MData.TWIN.done_elapsed_ms        & 0xFF;
			SOut[add++] = (MData.TWIN.done_elapsed_ms >> 8)  & 0xFF;
			SOut[add++] = (MData.TWIN.done_elapsed_ms >> 16) & 0xFF;
			SOut[add++] = (MData.TWIN.done_elapsed_ms >> 24) & 0xFF;

		    break;

		case CMD_TWIN_SCAN_DATA:
			SOut[add++] = MData.TWIN.cur_line_index;
			SOut[add++] = 0;
			SOut[add++] = MData.TWIN.cur_point_count & 0xFF;
			SOut[add++] = (MData.TWIN.cur_point_count >> 8) & 0xFF;

			for (u16 i = 0; i < MData.TWIN.cur_point_count; i++)
			{
				for (u8 ch = 0; ch < 2; ch++)
				{
					u16 v = MData.TWIN.line_buf[i][ch];
					SOut[add++] = v        & 0xFF;
					SOut[add++] = (v >> 8)  & 0xFF;
				}
			}
			//size = 4+ 51*2byte*2ch = 208


			break;

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

			SOut[add++] = 0x01;//Device TWIN

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

