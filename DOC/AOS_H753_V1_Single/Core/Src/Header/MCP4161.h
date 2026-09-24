#ifndef __MCP4161__
#define __MCP4161__

#ifdef __MCP4161_H__
	#define EXT_MCP4161
#else
	#define EXT_MCP4161 extern
#endif
#include "mType.h"

EXT_MCP4161 void bb_send_byte(u8 byte);
EXT_MCP4161 void mcp4161_write16(u8 cmd, u8 data);
EXT_MCP4161 void MCP4161_Init(void);
EXT_MCP4161 void MCP4161_WriteWiperRaw(u16 d9_0);
EXT_MCP4161 void MCP4161_SetAW_Ohms(float target_ohms);
EXT_MCP4161 void MCP4161_SetWB_Ohms(float target_ohms);



#endif
