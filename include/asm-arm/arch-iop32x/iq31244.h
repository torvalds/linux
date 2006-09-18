/*
 * linux/include/asm/arch-iop32x/iq31244.h
 *
 * Intel IQ31244 evaluation board registers
 */

#ifndef _IQ31244_H_
#define _IQ31244_H_

#define IQ31244_UART		0xfe800000	/* UART #1 */
#define IQ31244_7SEG_1		0xfe840000	/* 7-Segment MSB */
#define IQ31244_7SEG_0		0xfe850000	/* 7-Segment LSB (WO) */
#define IQ31244_ROTARY_SW	0xfe8d0000	/* Rotary Switch */
#define IQ31244_BATT_STAT	0xfe8f0000	/* Battery Status */


#endif	// _IQ31244_H_
