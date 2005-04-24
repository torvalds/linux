/* $Id: mostek.h,v 1.4 2001/01/11 15:07:09 davem Exp $
 * mostek.h:  Describes the various Mostek time of day clock registers.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 */

#ifndef _SPARC64_MOSTEK_H
#define _SPARC64_MOSTEK_H

#include <asm/idprom.h>

/*       M48T02 Register Map (adapted from Sun NVRAM/Hostid FAQ)
 *
 *                             Data
 * Address                                                 Function
 *        Bit 7 Bit 6 Bit 5 Bit 4Bit 3 Bit 2 Bit 1 Bit 0
 *   7ff  -     -     -     -    -     -     -     -       Year 00-99
 *   7fe  0     0     0     -    -     -     -     -      Month 01-12
 *   7fd  0     0     -     -    -     -     -     -       Date 01-31
 *   7fc  0     FT    0     0    0     -     -     -        Day 01-07
 *   7fb  KS    0     -     -    -     -     -     -      Hours 00-23
 *   7fa  0     -     -     -    -     -     -     -    Minutes 00-59
 *   7f9  ST    -     -     -    -     -     -     -    Seconds 00-59
 *   7f8  W     R     S     -    -     -     -     -    Control
 *
 *   * ST is STOP BIT
 *   * W is WRITE BIT
 *   * R is READ BIT
 *   * S is SIGN BIT
 *   * FT is FREQ TEST BIT
 *   * KS is KICK START BIT
 */

/* The Mostek 48t02 real time clock and NVRAM chip. The registers
 * other than the control register are in binary coded decimal. Some
 * control bits also live outside the control register.
 *
 * We now deal with physical addresses for I/O to the chip. -DaveM
 */
static __inline__ u8 mostek_read(void __iomem *addr)
{
	u8 ret;

	__asm__ __volatile__("lduba	[%1] %2, %0"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));
	return ret;
}

static __inline__ void mostek_write(void __iomem *addr, u8 val)
{
	__asm__ __volatile__("stba	%0, [%1] %2"
			     : /* no outputs */
			     : "r" (val), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));
}

#define MOSTEK_EEPROM		0x0000UL
#define MOSTEK_IDPROM		0x07d8UL
#define MOSTEK_CREG		0x07f8UL
#define MOSTEK_SEC		0x07f9UL
#define MOSTEK_MIN		0x07faUL
#define MOSTEK_HOUR		0x07fbUL
#define MOSTEK_DOW		0x07fcUL
#define MOSTEK_DOM		0x07fdUL
#define MOSTEK_MONTH		0x07feUL
#define MOSTEK_YEAR		0x07ffUL

extern spinlock_t mostek_lock;
extern void __iomem *mstk48t02_regs;

/* Control register values. */
#define	MSTK_CREG_WRITE	0x80	/* Must set this before placing values. */
#define	MSTK_CREG_READ	0x40	/* Stop updates to allow a clean read. */
#define	MSTK_CREG_SIGN	0x20	/* Slow/speed clock in calibration mode. */

/* Control bits that live in the other registers. */
#define	MSTK_STOP	0x80	/* Stop the clock oscillator. (sec) */
#define	MSTK_KICK_START	0x80	/* Kick start the clock chip. (hour) */
#define MSTK_FREQ_TEST	0x40	/* Frequency test mode. (day) */

#define MSTK_YEAR_ZERO       1968   /* If year reg has zero, it is 1968. */
#define MSTK_CVT_YEAR(yr)  ((yr) + MSTK_YEAR_ZERO)

/* Masks that define how much space each value takes up. */
#define	MSTK_SEC_MASK	0x7f
#define	MSTK_MIN_MASK	0x7f
#define	MSTK_HOUR_MASK	0x3f
#define	MSTK_DOW_MASK	0x07
#define	MSTK_DOM_MASK	0x3f
#define	MSTK_MONTH_MASK	0x1f
#define	MSTK_YEAR_MASK	0xff

/* Binary coded decimal conversion macros. */
#define MSTK_REGVAL_TO_DECIMAL(x)  (((x) & 0x0F) + 0x0A * ((x) >> 0x04))
#define MSTK_DECIMAL_TO_REGVAL(x)  ((((x) / 0x0A) << 0x04) + ((x) % 0x0A))

/* Generic register set and get macros for internal use. */
#define MSTK_GET(regs,name)	\
	(MSTK_REGVAL_TO_DECIMAL(mostek_read(regs + MOSTEK_ ## name) & MSTK_ ## name ## _MASK))
#define MSTK_SET(regs,name,value) \
do {	u8 __val = mostek_read(regs + MOSTEK_ ## name); \
	__val &= ~(MSTK_ ## name ## _MASK); \
	__val |= (MSTK_DECIMAL_TO_REGVAL(value) & \
		  (MSTK_ ## name ## _MASK)); \
	mostek_write(regs + MOSTEK_ ## name, __val); \
} while(0)

/* Macros to make register access easier on our fingers. These give you
 * the decimal value of the register requested if applicable. You pass
 * the a pointer to a 'struct mostek48t02'.
 */
#define	MSTK_REG_CREG(regs)	(mostek_read((regs) + MOSTEK_CREG))
#define	MSTK_REG_SEC(regs)	MSTK_GET(regs,SEC)
#define	MSTK_REG_MIN(regs)	MSTK_GET(regs,MIN)
#define	MSTK_REG_HOUR(regs)	MSTK_GET(regs,HOUR)
#define	MSTK_REG_DOW(regs)	MSTK_GET(regs,DOW)
#define	MSTK_REG_DOM(regs)	MSTK_GET(regs,DOM)
#define	MSTK_REG_MONTH(regs)	MSTK_GET(regs,MONTH)
#define	MSTK_REG_YEAR(regs)	MSTK_GET(regs,YEAR)

#define	MSTK_SET_REG_SEC(regs,value)	MSTK_SET(regs,SEC,value)
#define	MSTK_SET_REG_MIN(regs,value)	MSTK_SET(regs,MIN,value)
#define	MSTK_SET_REG_HOUR(regs,value)	MSTK_SET(regs,HOUR,value)
#define	MSTK_SET_REG_DOW(regs,value)	MSTK_SET(regs,DOW,value)
#define	MSTK_SET_REG_DOM(regs,value)	MSTK_SET(regs,DOM,value)
#define	MSTK_SET_REG_MONTH(regs,value)	MSTK_SET(regs,MONTH,value)
#define	MSTK_SET_REG_YEAR(regs,value)	MSTK_SET(regs,YEAR,value)


/* The Mostek 48t08 clock chip. Found on Sun4m's I think. It has the
 * same (basically) layout of the 48t02 chip except for the extra
 * NVRAM on board (8 KB against the 48t02's 2 KB).
 */
#define MOSTEK_48T08_OFFSET	0x0000UL	/* Lower NVRAM portions */
#define MOSTEK_48T08_48T02	0x1800UL	/* Offset to 48T02 chip */

/* SUN5 systems usually have 48t59 model clock chipsets.  But we keep the older
 * clock chip definitions around just in case.
 */
#define MOSTEK_48T59_OFFSET	0x0000UL	/* Lower NVRAM portions */
#define MOSTEK_48T59_48T02	0x1800UL	/* Offset to 48T02 chip */

#endif /* !(_SPARC64_MOSTEK_H) */
