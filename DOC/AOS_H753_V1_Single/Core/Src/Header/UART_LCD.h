#ifndef _LCD_CTL_
#define _LCD_CTL_

#ifdef _LCD_CTL_H_
	#define EXT_LCDCTL
#else
	#define EXT_LCDCTL extern
#endif

#include "mType.h"

EXT_LCDCTL void LCD_Data_View_All( );
EXT_LCDCTL void LCD_PAGE_CHANGE(u8 page);

EXT_LCDCTL void TxMSG_DISP(u8 size, char *buf);

EXT_LCDCTL void LCD_VERSION_VIEW();

EXT_LCDCTL void LCD_Data_View(u8 type);
EXT_LCDCTL void Text_Color_Change(u8 ch, u8 value);

EXT_LCDCTL void LCD_Weight(u8 weight);
EXT_LCDCTL void LCD_SCAN_OnOff_View(u8 OnOff);

EXT_LCDCTL void LCD_MODE_Set(u8 type);

#endif
