
#ifndef		__COMMAND_LIST_H__
#define		__COMMAND_LIST_H__


//COMMAND Common
#define CMD_RECEIVED_OK				0x00

#define CMD_SET_Volt				0x01
#define CMD_SET_QUERY				0x02
#define CMD_STATUS_QUERY			0x03
#define CMD_ATUTO_STATUS_ONOFF		0x04


#define CMD_CV_SET					0x21
#define CMD_CV_STATUS				0x22
#define CMD_CV_TIME_SET				0x23
#define CMD_CURRENT_TYPE			0x24
#define CMD_ION_SELECTION_SET		0x25
#define CMD_DUTY_SET				0x26
#define CMD_FREQUENCY_SET			0x27
#define CMD_BIAS_SELECTION			0x28
#define CMD_SET_CONTROL				0x2A
#define CMD_SET_CONTROL_ALL			0x2B
#define CMD_FAN_SET					0x2C


#define CMD_SCAN_SET				0x30
#define CMD_SCAN_START				0x31
#define CMD_SCAN_RESULT				0x32
#define CMD_SCAN_STOP				0x33

#define CMD_D_SCAN_SET				0x35
#define CMD_D_SCAN_RESULT			0x36
#define CMD_D_SCAN_SET_QUERY		0x37

#define CMD_SHALLOW_SET				0x38
#define CMD_SHALLOW_RUN				0x39
#define CMD_SHALLOW_RESULT			0x3A

#define CMD_SHALLOW_SINGLE_POINT_FIND	0x3B
#define CMD_SHALLOW_LINE_FIND			0x3C
#define CMD_SHALLOW_LINE_RESULT			0x3D
#define CMD_SHALLOW_LINE_RESULT_ALL		0x3E


#define CMD_FRQ_CAL_SET				0x40
#define CMD_FRQ_CAL_SAVE			0x41


#define CMD_SCAN_SET_RETURN			0x42
#define CMD_SCAN_START_RETURN		0x43
#define CMD_SCAN_STOP_RETURN		0x44



#define CMD_HF_MOD_SET				0x50
#define CMD_HF_MOD_QUERY			0x51
#define CMD_LF_MOD_SET				0x52
#define CMD_LF_MOD_QUERY			0x53

#define CMD_HF_AND_LF_MOD_SET		0x54
#define CMD_HF_AND_LF_MOD_QUERY		0x55


#define CMD_SCAN_START_OK			0x60
#define CMD_SCAN_START_OK_RETURN	0x61


#define CMD_SCAN_FULL_MODE_START		0x70
#define CMD_SCAN_FULL_MODE_SET			0x71
#define CMD_SCAN_FULL_MODE_SET_RETURN	0x72
#define CMD_SCAN_FULL_MODE_RESULT		0x73


#define CMD_AD7739_RESET			0x90
#define CMD_BIAS_ONOFF				0x91




#define CMD_CAL_WRITE				0xA0
#define CMD_CAL_QUERY				0xA1

#define CMD_CAL_WRITE_SHALLOW_2D	0xA2
#define CMD_CAL_QUERY_SHALLOW_2D	0xA3



// =========================================================
// Heatmap 측정 (구 TWIN).  Single 시스템 전환으로 개명.
//   구 CMD_TWIN_SCAN_DATA(0x81, line 단위 legacy) 는 삭제됨 — batch(0x86) 로 대체.
//   0x80~0x86 대역에 다른 명령 없음 (AD7739_RESET/BIAS_ONOFF 는 0x90/0x91).
// =========================================================
#define CMD_HMAP_START           0x80   // PC/서버 → F/W  : 44 byte
//      (0x81 삭제 — 구 CMD_TWIN_SCAN_DATA)
#define CMD_HMAP_POINT_REQ       0x82   // PC/서버 → F/W  : 26 byte
#define CMD_HMAP_POINT_DATA      0x83   // F/W → PC/서버  : 12 byte
#define CMD_HMAP_ABORT           0x84   // PC/서버 → F/W  : 0 byte
#define CMD_HMAP_DONE            0x85   // F/W → PC/서버  : 8 byte
#define CMD_HMAP_DATA            0x86   // F/W → PC/서버  : 8 + noCV*noLFV*2

// --- 측정 격자 설정 (EEPROM 보존) ---
//   0x87~0x8A. 0x87 이후 대역은 비어 있어 HMAP 블록에 이어 붙였다.
#define CMD_HMAP_CFG_QUERY       0x87   // PC/서버 → F/W  : 0 byte  (현재 설정 요청)
#define CMD_HMAP_CFG             0x88   // F/W → PC/서버  : 설정 1벌
#define CMD_HMAP_CFG_SET         0x89   // PC/서버 → F/W  : 설정 1벌 (RAM 반영)
#define CMD_HMAP_CFG_SAVE        0x8A   // PC/서버 → F/W  : RAM 설정을 EEPROM 에 기록
                                        //   응답은 CMD_HMAP_CFG (저장 후 실제 값 회신)

// --- 측정 순회 (Full / 1Hour / Sample) ---
//   구 PCSW 가 1,600번 0x80 을 쏘던 것을 F/W 가 스스로 돌도록 옮겼다.
#define CMD_HMAP_RUN_START       0x8B   // PC/서버 → F/W  : u8 mode
#define CMD_HMAP_RUN_CTRL        0x8C   // PC/서버 → F/W  : u8 action (0=abort 1=pause 2=resume)
#define CMD_HMAP_RUN_QUERY       0x8D   // PC/서버 → F/W  : 0 byte
#define CMD_HMAP_RUN_STATUS      0x8E   // F/W → PC/서버  : 20 byte
#define CMD_HMAP_SAMPLE_SET      0x8F   // PC/서버 → F/W  : u8 count, u8 rsv, count*{hv,frq,duty,lff} float

// 버퍼 용량. ★ 반드시 __hmap.map_buf 의 배열 차원과 같아야 한다.
//   구 코드는 클램프 상수(51/21)와 실제 배열([11][15])이 달라 overrun 이 났다.
#define HMAP_LFV_POINTS_MAX      51
#define HMAP_CV_LINES_MAX        21


#endif
