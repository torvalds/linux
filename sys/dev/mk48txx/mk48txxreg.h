/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *	$NetBSD: mk48txxreg.h,v 1.10 2008/04/28 20:23:50 martin Exp $
 *
 * $FreeBSD$
 */

/*
 * Mostek MK48Txx clocks.
 *
 * The MK48T02 has 2KB of non-volatile memory. The time-of-day clock
 * registers start at offset 0x7f8.
 *
 * The MK48T08 and MK48T18 have 8KB of non-volatile memory
 *
 * The MK48T59 also has 8KB of non-volatile memory but in addition it
 * has a battery low detection bit and a power supply wakeup alarm for
 * power management.  It's at offset 0x1ff0 in the NVRAM.
 */

/*
 * Mostek MK48TXX register definitions
 */

/*
 * The first bank of eight registers at offset (nvramsz - 16) is
 * available only on recenter (which?) MK48Txx models.
 */
#define	MK48TXX_FLAGS	0	/* flags register */
#define	MK48TXX_UNUSED	1	/* unused */
#define	MK48TXX_ASEC	2	/* alarm seconds (0..59; BCD) */
#define	MK48TXX_AMIN	3	/* alarm minutes (0..59; BCD) */
#define	MK48TXX_AHOUR	4	/* alarm hours (0..23; BCD) */
#define	MK48TXX_ADAY	5	/* alarm day in month (1..31; BCD) */
#define	MK48TXX_INTR	6	/* interrupts register */
#define	MK48TXX_WDOG	7	/* watchdog register */

#define	MK48TXX_ICSR	8	/* control register */
#define	MK48TXX_ISEC	9	/* seconds (0..59; BCD) */
#define	MK48TXX_IMIN	10	/* minutes (0..59; BCD) */
#define	MK48TXX_IHOUR	11	/* hours (0..23; BCD) */
#define	MK48TXX_IWDAY	12	/* weekday (1..7) */
#define	MK48TXX_IDAY	13	/* day in month (1..31; BCD) */
#define	MK48TXX_IMON	14	/* month (1..12; BCD) */
#define	MK48TXX_IYEAR	15	/* year (0..99; BCD) */

/*
 * Note that some of the bits below that are not in the first eight
 * registers are also only available on models with an extended
 * register set.
 */

/* Bits in the flags register (extended only) */
#define	MK48TXX_FLAGS_BL	0x10	/* battery low (read only) */
#define	MK48TXX_FLAGS_AF	0x40	/* alarm flag (read only) */
#define	MK48TXX_FLAGS_WDF	0x80	/* watchdog flag (read only) */

/* Bits in the alarm seconds register (extended only) */
#define	MK48TXX_ASEC_MASK	0x7f	/* mask for alarm seconds */
#define	MK48TXX_ASEC_RPT1	0x80	/* alarm repeat mode bit 1 */

/* Bits in the alarm minutes register (extended only) */
#define	MK48TXX_AMIN_MASK	0x7f	/* mask for alarm minutes */
#define	MK48TXX_AMIN_RPT2	0x80	/* alarm repeat mode bit 2 */

/* Bits in the alarm hours register (extended only) */
#define	MK48TXX_AHOUR_MASK	0x3f	/* mask for alarm hours */
#define	MK48TXX_AHOUR_RPT3	0x80	/* alarm repeat mode bit 3 */

/* Bits in the alarm day in month register (extended only) */
#define	MK48TXX_ADAY_MASK	0x3f	/* mask for alarm day in month */
#define	MK48TXX_ADAY_RPT4	0x80	/* alarm repeat mode bit 4 */

/* Bits in the interrupts register (extended only) */
#define	MK48TXX_INTR_ABE	0x20	/* alarm in battery back-up mode */
#define	MK48TXX_INTR_AFE	0x80	/* alarm flag enable */

/* Bits in the watchdog register (extended only) */
#define	MK48TXX_WDOG_RB_1_16	0x00	/* watchdog resolution 1/16 second */
#define	MK48TXX_WDOG_RB_1_4	0x01	/* watchdog resolution 1/4 second */
#define	MK48TXX_WDOG_RB_1	0x02	/* watchdog resolution 1 second */
#define	MK48TXX_WDOG_RB_4	0x03	/* watchdog resolution 4 seconds */
#define	MK48TXX_WDOG_BMB_MASK	0x7c	/* mask for watchdog multiplier */
#define	MK48TXX_WDOG_BMB_SHIFT	2	/* shift for watchdog multiplier */
#define	MK48TXX_WDOG_WDS	0x80	/* watchdog steering bit */

/* Bits in the control register */
#define	MK48TXX_CSR_CALIB_MASK	0x1f	/* mask for calibration step width */
#define	MK48TXX_CSR_SIGN	0x20	/* sign of above calibration witdh */
#define	MK48TXX_CSR_READ	0x40	/* want to read (freeze clock) */
#define	MK48TXX_CSR_WRITE	0x80	/* want to write */

/* Bits in the seconds register */
#define	MK48TXX_SEC_MASK	0x7f	/* mask for seconds */
#define	MK48TXX_SEC_ST		0x80	/* stop oscillator */

/* Bits in the minutes register */
#define	MK48TXX_MIN_MASK	0x7f	/* mask for minutes */

/* Bits in the hours register */
#define	MK48TXX_HOUR_MASK	0x3f	/* mask for hours */

/* Bits in the century/weekday register */
#define	MK48TXX_WDAY_MASK	0x07	/* mask for weekday */
#define	MK48TXX_WDAY_CB		0x10	/* century bit (extended only) */
#define	MK48TXX_WDAY_CB_SHIFT	4	/* shift for century bit */
#define	MK48TXX_WDAY_CEB	0x20	/* century enable bit (extended only) */
#define	MK48TXX_WDAY_FT		0x40	/* frequency test */

/* Bits in the day in month register */
#define	MK48TXX_DAY_MASK	0x3f	/* mask for day in month */

/* Bits in the month register */
#define	MK48TXX_MON_MASK	0x1f	/* mask for month */

/* Bits in the year register */
#define	MK48TXX_YEAR_MASK	0xff	/* mask for year */

/* Model specific NVRAM sizes and clock offsets */
#define	MK48T02_CLKSZ		2048
#define	MK48T02_CLKOFF		0x7f0

#define	MK48T08_CLKSZ		8192
#define	MK48T08_CLKOFF		0x1ff0

#define	MK48T18_CLKSZ		8192
#define	MK48T18_CLKOFF		0x1ff0

#define	MK48T37_CLKSZ		32768
#define	MK48T37_CLKOFF		0x1ff0

#define	MK48T59_CLKSZ		8192
#define	MK48T59_CLKOFF		0x1ff0
