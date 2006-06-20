/* $Id: mostek.h,v 1.13 2001/01/11 15:07:09 davem Exp $
 * mostek.h:  Describes the various Mostek time of day clock registers.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 * Added intersil code 05/25/98 Chris Davis (cdavis@cois.on.ca)
 */

#ifndef _SPARC_MOSTEK_H
#define _SPARC_MOSTEK_H

#include <asm/idprom.h>
#include <asm/io.h>

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
 */
#define mostek_read(_addr)		readb(_addr)
#define mostek_write(_addr,_val)	writeb(_val, _addr)
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

struct mostek48t02 {
	volatile char eeprom[2008];	/* This is the eeprom, don't touch! */
	struct idprom idprom;		/* The idprom lives here. */
	volatile unsigned char creg;	/* Control register */
	volatile unsigned char sec;	/* Seconds (0-59) */
	volatile unsigned char min;	/* Minutes (0-59) */
	volatile unsigned char hour;	/* Hour (0-23) */
	volatile unsigned char dow;	/* Day of the week (1-7) */
	volatile unsigned char dom;	/* Day of the month (1-31) */
	volatile unsigned char month;	/* Month of year (1-12) */
	volatile unsigned char year;	/* Year (0-99) */
};

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
#define MSTK_GET(regs,var,mask) (MSTK_REGVAL_TO_DECIMAL(((struct mostek48t02 *)regs)->var & MSTK_ ## mask ## _MASK))
#define MSTK_SET(regs,var,value,mask) do { ((struct mostek48t02 *)regs)->var &= ~(MSTK_ ## mask ## _MASK); ((struct mostek48t02 *)regs)->var |= MSTK_DECIMAL_TO_REGVAL(value) & (MSTK_ ## mask ## _MASK); } while (0)

/* Macros to make register access easier on our fingers. These give you
 * the decimal value of the register requested if applicable. You pass
 * the a pointer to a 'struct mostek48t02'.
 */
#define	MSTK_REG_CREG(regs)	(((struct mostek48t02 *)regs)->creg)
#define	MSTK_REG_SEC(regs)	MSTK_GET(regs,sec,SEC)
#define	MSTK_REG_MIN(regs)	MSTK_GET(regs,min,MIN)
#define	MSTK_REG_HOUR(regs)	MSTK_GET(regs,hour,HOUR)
#define	MSTK_REG_DOW(regs)	MSTK_GET(regs,dow,DOW)
#define	MSTK_REG_DOM(regs)	MSTK_GET(regs,dom,DOM)
#define	MSTK_REG_MONTH(regs)	MSTK_GET(regs,month,MONTH)
#define	MSTK_REG_YEAR(regs)	MSTK_GET(regs,year,YEAR)

#define	MSTK_SET_REG_SEC(regs,value)	MSTK_SET(regs,sec,value,SEC)
#define	MSTK_SET_REG_MIN(regs,value)	MSTK_SET(regs,min,value,MIN)
#define	MSTK_SET_REG_HOUR(regs,value)	MSTK_SET(regs,hour,value,HOUR)
#define	MSTK_SET_REG_DOW(regs,value)	MSTK_SET(regs,dow,value,DOW)
#define	MSTK_SET_REG_DOM(regs,value)	MSTK_SET(regs,dom,value,DOM)
#define	MSTK_SET_REG_MONTH(regs,value)	MSTK_SET(regs,month,value,MONTH)
#define	MSTK_SET_REG_YEAR(regs,value)	MSTK_SET(regs,year,value,YEAR)


/* The Mostek 48t08 clock chip. Found on Sun4m's I think. It has the
 * same (basically) layout of the 48t02 chip except for the extra
 * NVRAM on board (8 KB against the 48t02's 2 KB).
 */
struct mostek48t08 {
	char offset[6*1024];         /* Magic things may be here, who knows? */
	struct mostek48t02 regs;     /* Here is what we are interested in.   */
};

extern enum sparc_clock_type sp_clock_typ;

#ifdef CONFIG_SUN4
enum sparc_clock_type {	MSTK48T02, MSTK48T08, \
INTERSIL, MSTK_INVALID };
#else
enum sparc_clock_type {	MSTK48T02, MSTK48T08, \
MSTK_INVALID };
#endif

#ifdef CONFIG_SUN4
/* intersil on a sun 4/260 code  data from harris doc */
struct intersil_dt {
        volatile unsigned char int_csec;
        volatile unsigned char int_hour;
        volatile unsigned char int_min;
        volatile unsigned char int_sec;
        volatile unsigned char int_month;
        volatile unsigned char int_day;
        volatile unsigned char int_year;
        volatile unsigned char int_dow;
};

struct intersil {
	struct intersil_dt clk;
	struct intersil_dt cmp;
	volatile unsigned char int_intr_reg;
	volatile unsigned char int_cmd_reg;
};

#define INTERSIL_STOP        0x0
#define INTERSIL_START       0x8
#define INTERSIL_INTR_DISABLE   0x0
#define INTERSIL_INTR_ENABLE   0x10
#define INTERSIL_32K		0x0
#define INTERSIL_NORMAL		0x0
#define INTERSIL_24H		0x4 
#define INTERSIL_INT_100HZ	0x2

/* end of intersil info */
#endif

#endif /* !(_SPARC_MOSTEK_H) */
