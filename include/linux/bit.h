/*
 *
 *  Copyright (C) 2014 Burak Köken
 *
 *  Get & set bits
 *
 */
 
#ifndef __LINUX_BITS_H__
#define __LINUX_BITS_H__

#include <linux/types.h>

/* get bit */
#define getbit8_high(x)			x >> 4
#define getbit8_low(x)			x & 0x0f

#define getbit16_high(x)		x >> 8
#define getbit16_low(x)			x & 0xff

#define getbit32_high(x)		x >> 16
#define getbit32_low(x)			x & 0xffff

#define getbit64_high(x)    x >> 32
#define getbit64_low(x)     x & 0xffffffff

/* set bit */
#define setbit8(high_4bit,low_4bit)		  ((u8)  high_4bit << 4 | low_4bit)
#define setbit16(high_8bit,low_8bit)	  ((u16) high_8bit << 8 | low_8bit)
#define setbit32(high_16bit,low_16bit)	((u32) high_16bit << 16 | low_16bit)
#define setbit64(high_32bit,low_32bit)  ((u64) high_32bit << 32 | low_32bit)



#endif  /* __LINUX_BITS_H__ */
