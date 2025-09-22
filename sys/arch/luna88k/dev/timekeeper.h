/* $OpenBSD: timekeeper.h,v 1.4 2017/11/03 06:54:06 aoyama Exp $ */
/* $NetBSD: timekeeper.h,v 1.1 2000/01/05 08:48:56 nisimura Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Tohru Nishimura.
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
 */

/*
 * Mostek MK48T02 for LUNA-88K
 */
#define	MK_CSR		0	/* control register */
#define	MK_SEC		1	/* seconds (0..59; BCD) */
#define	MK_MIN		2	/* minutes (0..59; BCD) */
#define	MK_HOUR		3	/* hour (0..23; BCD) */
#define	MK_DOW		4	/* weekday (1..7) */
#define	MK_DOM		5	/* day in month (1..31; BCD) */
#define	MK_MONTH	6	/* month (1..12; BCD) */
#define	MK_YEAR		7	/* year (0..99; BCD) */
/* bits in cl_csr */
#define MK_CSR_WRITE	0x80	/* want to write */
#define MK_CSR_READ	0x40	/* want to read (freeze clock) */
/* NVRAM space is mapped with 4-bytes stride */
#define MK_NVRAM_SPACE	(4 * (2048 - 8))

/*
 * Dallas Semiconductor DS1397 for LUNA-88K2
 */
#define DS_SEC		0x0	/* Time of year: seconds (0-59) */
#define DS_MIN		0x2	/* Time of year: minutes (0-59) */
#define DS_HOUR		0x4	/* Time of year: hour (see above) */
#define DS_DOW		0x6	/* Time of year: day of week (1-7) */
#define DS_DOM		0x7	/* Time of year: day of month (1-31) */
#define DS_MONTH	0x8	/* Time of year: month (1-12) */
#define DS_YEAR		0x9	/* Time of year: year in century (0-99) */

#define DS_REGA		0xa	/* Control register A */
#define  DS_REGA_RSMASK	0x0f	/* Interrupt rate select mask (see below) */
#define  DS_REGA_DVMASK	0x70	/* Divisor select mask (see below) */
#define  DS_REGA_UIP	0x80	/* Update in progress; read only. */

#define DS_REGB		0xb	/* Control register B */
#define  DS_REGB_DSE	0x01	/* Daylight Savings Enable */
#define  DS_REGB_24HR	0x02	/* 24-hour mode (AM/PM mode when clear) */
#define  DS_REGB_BINARY	0x04	/* Binary mode (BCD mode when clear) */
#define  DS_REGB_SQWE	0x08	/* Square Wave Enable */
#define  DS_REGB_UIE	0x10	/* Update End interrupt enable */
#define  DS_REGB_AIE	0x20	/* Alarm interrupt enable */
#define  DS_REGB_PIE	0x40	/* Periodic interrupt enable */
#define  DS_REGB_SET	0x80	/* Allow time to be set; stops updates */

#define DS_REGC		0xc	/* Control register C */
/*	 DS_REGC_UNUSED	0x0f	   UNUSED */
#define	 DS_REGC_UF	0x10	/* Update End interrupt flag */
#define	 DS_REGC_AF	0x20	/* Alarm interrupt flag */
#define	 DS_REGC_PF	0x40	/* Periodic interrupt flag */
#define	 DS_REGC_IRQF	0x80	/* Interrupt request pending flag */

#define	DS_REGD		0xd	/* Control register D */
/*	 DS_REGD_UNUSED	0x7f	   UNUSED */
#define	 DS_REGD_VRT	0x80	/* Valid RAM and Time bit */

#define	DS_NREGS	0xe	/* 14 registers; CMOS follows */
#define	DS_NTODREGS	0xa	/* 10 of those regs are for TOD and alarm */
