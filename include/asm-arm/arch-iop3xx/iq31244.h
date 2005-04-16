/*
 * linux/include/asm/arch-iop3xx/iq31244.h
 *
 * Intel IQ31244 evaluation board registers
 */

#ifndef _IQ31244_H_
#define _IQ31244_H_

#define	IQ31244_FLASHBASE	0xf0000000	/* Flash */
#define	IQ31244_FLASHSIZE	0x00800000
#define	IQ31244_FLASHWIDTH	2

#define IQ31244_UART		0xfe800000	/* UART #1 */
#define IQ31244_7SEG_1		0xfe840000	/* 7-Segment MSB */
#define IQ31244_7SEG_0		0xfe850000	/* 7-Segment LSB (WO) */
#define IQ31244_ROTARY_SW	0xfe8d0000	/* Rotary Switch */
#define IQ31244_BATT_STAT	0xfe8f0000	/* Battery Status */

#ifndef __ASSEMBLY__
extern void iq31244_map_io(void);
#endif

#endif	// _IQ31244_H_
