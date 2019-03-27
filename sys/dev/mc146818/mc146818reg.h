/*-
 * SPDX-License-Identifier: MIT-CMU
 *
 * Copyright (c) 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 *	$NetBSD: mc146818reg.h,v 1.9 2006/03/08 23:46:25 lukem Exp $
 *
 * $FreeBSD$
 */

/*
 * Definitions for the Motorola MC146818A Real Time Clock.
 * They also apply for the (compatible) Dallas Semiconductor DS1287A RTC.
 *
 * Though there are undoubtedly other (better) sources, this material was
 * culled from the DEC "KN121 System Module Programmer's Reference
 * Information."
 *
 * The MC146818A has 16 registers.  The first 10 contain time-of-year
 * and alarm data.  The rest contain various control and status bits.
 *
 * To read or write the registers, one writes the register number to
 * the RTC's control port, then either reads from or writes the new
 * data to the RTC's data port.  Since the locations of these ports
 * and the method used to access them can be machine-dependent, the
 * low-level details of reading and writing the RTC's registers are
 * handled by machine-specific functions.
 *
 * The time-of-year and alarm data can be expressed in either binary
 * or BCD, and they are selected by a bit in register B.
 *
 * The "hour" time-of-year and alarm fields can either be expressed in
 * AM/PM format, or in 24-hour format.  If AM/PM format is chosen, the
 * hour fields can have the values: 1-12 and 81-92 (the latter being
 * PM).  If the 24-hour format is chosen, they can have the values
 * 0-24.  The hour format is selectable by a bit in register B.
 * (XXX IS AM/PM MODE DESCRIPTION CORRECT?)
 *
 * It is assumed the if systems are going to use BCD (rather than
 * binary) mode, or AM/PM hour format, they'll do the appropriate
 * conversions in machine-dependent code.  Also, if the clock is
 * switched between BCD and binary mode, or between AM/PM mode and
 * 24-hour mode, the time-of-day and alarm registers are NOT
 * automatically reset; they must be reprogrammed with correct values.
 */

/*
 * The registers, and the bits within each register.
 */

#define	MC_SEC		0x0	/* Time of year: seconds (0-59) */
#define	MC_ASEC		0x1	/* Alarm: seconds */
#define	MC_MIN		0x2	/* Time of year: minutes (0-59) */
#define	MC_AMIN		0x3	/* Alarm: minutes */
#define	MC_HOUR		0x4	/* Time of year: hour (see above) */
#define	MC_AHOUR	0x5	/* Alarm: hour */
#define	MC_DOW		0x6	/* Time of year: day of week (1-7) */
#define	MC_DOM		0x7	/* Time of year: day of month (1-31) */
#define	MC_MONTH	0x8	/* Time of year: month (1-12) */
#define	MC_YEAR		0x9	/* Time of year: year in century (0-99) */

#define	MC_REGA		0xa	/* Control register A */

#define	 MC_REGA_RSMASK	0x0f	/* Interrupt rate select mask (see below) */
#define	 MC_REGA_DVMASK	0x70	/* Divisor select mask (see below) */
#define	 MC_REGA_DV0	0x10	/* Divisor 0 */
#define	 MC_REGA_DV1	0x20	/* Divisor 1 */
#define	 MC_REGA_DV2	0x40	/* Divisor 2 */
#define	 MC_REGA_UIP	0x80	/* Update in progress; read only. */

#define	MC_REGB		0xb	/* Control register B */

#define	 MC_REGB_DSE	0x01	/* Daylight Savings Enable */
#define	 MC_REGB_24HR	0x02	/* 24-hour mode (AM/PM mode when clear) */
#define	 MC_REGB_BINARY	0x04	/* Binary mode (BCD mode when clear) */
#define	 MC_REGB_SQWE	0x08	/* Square Wave Enable */
#define	 MC_REGB_UIE	0x10	/* Update End interrupt enable */
#define	 MC_REGB_AIE	0x20	/* Alarm interrupt enable */
#define	 MC_REGB_PIE	0x40	/* Periodic interrupt enable */
#define	 MC_REGB_SET	0x80	/* Allow time to be set; stops updates */

#define	MC_REGC		0xc	/* Control register C */

/*	 MC_REGC_UNUSED	0x0f	UNUSED */
#define	 MC_REGC_UF	0x10	/* Update End interrupt flag */
#define	 MC_REGC_AF	0x20	/* Alarm interrupt flag */
#define	 MC_REGC_PF	0x40	/* Periodic interrupt flag */
#define	 MC_REGC_IRQF	0x80	/* Interrupt request pending flag */

#define	MC_REGD		0xd	/* Control register D */

/*	 MC_REGD_UNUSED	0x7f	UNUSED */
#define	 MC_REGD_VRT	0x80	/* Valid RAM and Time bit */


#define	MC_NREGS	0xe	/* 14 registers; CMOS follows */
#define	MC_NTODREGS	0xa	/* 10 of those regs are for TOD and alarm */

#define	MC_NVRAM_START	0xe	/* start of NVRAM: offset 14 */
#define	MC_NVRAM_SIZE	50	/* 50 bytes of NVRAM */

/*
 * Periodic Interrupt Rate Select constants (Control register A)
 */
#define	MC_RATE_NONE	0x0	/* No periodic interrupt */
#define	MC_RATE_1	0x1	/* 256 Hz if MC_BASE_32_KHz, else 32768 Hz */
#define	MC_RATE_2	0x2	/* 128 Hz if MC_BASE_32_KHz, else 16384 Hz */
#define	MC_RATE_8192_Hz	0x3	/* 122.070 us period */
#define	MC_RATE_4096_Hz	0x4	/* 244.141 us period */
#define	MC_RATE_2048_Hz	0x5	/* 488.281 us period */
#define	MC_RATE_1024_Hz	0x6	/* 976.562 us period */
#define	MC_RATE_512_Hz	0x7	/* 1.953125 ms period */
#define	MC_RATE_256_Hz	0x8	/* 3.90625 ms period */
#define	MC_RATE_128_Hz	0x9	/* 7.8125 ms period */
#define	MC_RATE_64_Hz	0xa	/* 15.625 ms period */
#define	MC_RATE_32_Hz	0xb	/* 31.25 ms period */
#define	MC_RATE_16_Hz	0xc	/* 62.5 ms period */
#define	MC_RATE_8_Hz	0xd	/* 125 ms period */
#define	MC_RATE_4_Hz	0xe	/* 250 ms period */
#define	MC_RATE_2_Hz	0xf	/* 500 ms period */

/*
 * Time base (divisor select) constants (Control register A)
 */
#define	MC_BASE_4_MHz	0x00		/* 4 MHz crystal */
#define	MC_BASE_1_MHz	MC_REGA_DV0	/* 1 MHz crystal */
#define	MC_BASE_32_KHz	MC_REGA_DV1	/* 32 KHz crystal */
#define	MC_BASE_NONE	(MC_REGA_DV2 | MC_REGA_DV1) /* actually also resets */
#define	MC_BASE_RESET	(MC_REGA_DV2 | MC_REGA_DV1 | MC_REGA_DV0)
