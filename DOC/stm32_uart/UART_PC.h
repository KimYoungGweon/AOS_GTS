#ifndef __UART_PC__
#define __UART_PC__

#ifdef __UART_PC_H__
	#define EXT_UART_PC
#else
	#define EXT_UART_PC extern
#endif


#include "MyCTL.h"


typedef enum
{
	eCMD_STX=0,
	eCMD_CMD,
	eCMD_SIZE1,
	eCMD_SIZE2,
	eCMD_DATA,
	eCMD_CS
} CMD_RX_STATE;


typedef struct
{
	u8	int_rec;

	u8	Buf[CMD_QUEUE_SIZE];
	u16	Head,Tail;

	u8	status;
	u16	count;
	u16	size;
	u8	chksum;
	u8	data[CMD_QUEUE_SIZE];

	u8 cmd;

}__UART_QUEUE;



EXT_UART_PC __UART_QUEUE RxQue1;


EXT_UART_PC void UART_Process();
EXT_UART_PC void UART_ERROR_CHECK();

EXT_UART_PC void Buf_Init_PC(void);
EXT_UART_PC u8 Rx_GetData_PC(u8 *ch);
EXT_UART_PC char Rx_MSG_CTL_PC(void);

EXT_UART_PC void RxMessage_CTL_PC(void);
EXT_UART_PC void TxMessage_CTL_PC(u8 cmd);


EXT_UART_PC void TxMSG_PC(u8 cmd, u16 size);

#endif
