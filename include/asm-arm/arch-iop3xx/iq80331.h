/*
 * linux/include/asm/arch-iop3xx/iq80331.h
 *
 * Intel IQ80331 evaluation board registers
 */

#ifndef _IQ80331_H_
#define _IQ80331_H_

#define	IQ80331_FLASHBASE	0xc0000000	/* Flash */
#define	IQ80331_FLASHSIZE	0x00800000
#define	IQ80331_FLASHWIDTH	1

#define IQ80331_7SEG_1		0xce840000	/* 7-Segment MSB */
#define IQ80331_7SEG_0		0xce850000	/* 7-Segment LSB (WO) */
#define IQ80331_ROTARY_SW	0xce8d0000	/* Rotary Switch */
#define IQ80331_BATT_STAT	0xce8f0000	/* Battery Status */

#ifndef __ASSEMBLY__
extern void iq80331_map_io(void);
#endif

#endif	// _IQ80331_H_
