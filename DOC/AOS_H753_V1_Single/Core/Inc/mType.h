#ifndef   __DEFAULT_H__
#define   __DEFAULT_H__

#include <stdio.h>
#include <string.h>



#define MHZ           *1000000l
#define KHZ           *1000l
#define HZ            *1l

#define abs32(x)        (x = (x > 0) ? x : (-1)*x)
#define SWAP16(A)	((((A << 8 ) & 0xFF00)) | ((A >> 8)& 0x00FF))
#define SWAP32(A)	((((A<<24)&0xFF000000)) | (((A<<8)&0x00FF0000)) | (((A>>8)&0x0000FF00)) | (((A>>24)&0x000000FF)))
#define HI_BYTE(x)      ( (x >> 8) & (0xFF) )
#define LO_BYTE(x)      ( (x >> 0) & (0xFF) )

typedef unsigned short const UC16;
typedef unsigned char const UC8;

typedef char int8;                        /**< The 8-bit signed data type. */
typedef volatile char vint8;              /**< The volatile 8-bit signed data type. */
typedef unsigned char uint8;              /**< The 8-bit unsigned data type. */
typedef volatile unsigned char vuint8;    /**< The volatile 8-bit unsigned data type. */
typedef short int16;                      /**< The 16-bit signed data type. */
typedef volatile short vint16;            /**< The volatile 16-bit signed data type. */
typedef unsigned short uint16;            /**< The 16-bit unsigned data type. */
typedef volatile unsigned short vuint16;  /**< The volatile 16-bit unsigned data type. */
typedef long int32;                       /**< The 32-bit signed data type. */
typedef volatile long vint32;             /**< The volatile 32-bit signed data type. */
typedef unsigned long uint32;             /**< The 32-bit unsigned data type. */
typedef volatile unsigned long vuint32;   /**< The volatile 32-bit unsigned data type. */


typedef unsigned long	ulong;
typedef unsigned short	ushort;
typedef unsigned char	uchar;
typedef unsigned int    uint;



#define	true			1
#define false			0

#define	ON				1
#define OFF				0

#ifndef NULL
#define NULL (void *)0
#endif

typedef	unsigned char           UINT8;
typedef	signed char             INT8;
typedef	unsigned short          UINT16;
typedef	signed short            INT16;
typedef	unsigned int            UINT32;
typedef	signed int              INT32;

typedef	unsigned char           u8;
typedef	signed char             s8;
typedef	unsigned short          u16;
typedef	signed short            s16;
typedef	unsigned int            u32;
typedef	signed int              s32;



#endif

