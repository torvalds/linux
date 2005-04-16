/*
 * linux/include/asm/arch-iop3xx/iq80332.h
 *
 * Intel IQ80332 evaluation board registers
 */

#ifndef _IQ80332_H_
#define _IQ80332_H_

#define	IQ80332_FLASHBASE	0xc0000000	/* Flash */
#define	IQ80332_FLASHSIZE	0x00800000
#define	IQ80332_FLASHWIDTH	1

#define IQ80332_7SEG_1		0xce840000	/* 7-Segment MSB */
#define IQ80332_7SEG_0		0xce850000	/* 7-Segment LSB (WO) */
#define IQ80332_ROTARY_SW	0xce8d0000	/* Rotary Switch */
#define IQ80332_BATT_STAT	0xce8f0000	/* Battery Status */

#ifndef __ASSEMBLY__
extern void iq80332_map_io(void);
#endif

#endif	// _IQ80332_H_
