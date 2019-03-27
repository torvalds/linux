/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)rtc.h	7.1 (Berkeley) 5/12/91
 * $FreeBSD$
 */

#ifndef _I386_ISA_RTC_H_
#define _I386_ISA_RTC_H_ 1

/*
 * MC146818 RTC Register locations
 */

#define RTC_SEC		0x00	/* seconds */
#define RTC_SECALRM	0x01	/* seconds alarm */
#define RTC_MIN		0x02	/* minutes */
#define RTC_MINALRM	0x03	/* minutes alarm */
#define RTC_HRS		0x04	/* hours */
#define RTC_HRSALRM	0x05	/* hours alarm */
#define RTC_WDAY	0x06	/* week day */
#define RTC_DAY		0x07	/* day of month */
#define RTC_MONTH	0x08	/* month of year */
#define RTC_YEAR	0x09	/* month of year */

#define RTC_STATUSA	0x0a	/* status register A */
#define  RTCSA_TUP	 0x80	/* time update, don't look now */
#define  RTCSA_RESET	 0x70	/* reset divider */
#define  RTCSA_DIVIDER   0x20   /* divider correct for 32768 Hz */
#define  RTCSA_8192      0x03	/* 8192 Hz interrupt */
#define  RTCSA_4096      0x04
#define  RTCSA_2048      0x05
#define  RTCSA_1024      0x06	/* default for profiling */
#define  RTCSA_PROF      RTCSA_1024
#define  RTC_PROFRATE    1024
#define  RTCSA_512       0x07
#define  RTCSA_256       0x08
#define  RTCSA_128       0x09
#define  RTCSA_NOPROF	 RTCSA_128
#define  RTC_NOPROFRATE  128
#define  RTCSA_64        0x0a
#define  RTCSA_32        0x0b	/* 32 Hz interrupt */

#define RTC_STATUSB	0x0b	/* status register B */
#define	 RTCSB_DST	 0x01	/* USA Daylight Savings Time enable */
#define	 RTCSB_24HR	 0x02	/* 0 = 12 hours, 1 = 24	hours */
#define	 RTCSB_BCD	 0x04	/* 0 = BCD, 1 =	Binary coded time */
#define	 RTCSB_SQWE	 0x08	/* 1 = output sqare wave at SQW	pin */
#define	 RTCSB_UINTR	 0x10	/* 1 = enable update-ended interrupt */
#define	 RTCSB_AINTR	 0x20	/* 1 = enable alarm interrupt */
#define	 RTCSB_PINTR	 0x40	/* 1 = enable periodic clock interrupt */
#define  RTCSB_HALT      0x80	/* stop clock updates */

#define RTC_INTR	0x0c	/* status register C (R) interrupt source */
#define  RTCIR_UPDATE	 0x10	/* update intr */
#define  RTCIR_ALARM	 0x20	/* alarm intr */
#define  RTCIR_PERIOD	 0x40	/* periodic intr */
#define  RTCIR_INT	 0x80	/* interrupt output signal */

#define RTC_STATUSD	0x0d	/* status register D (R) Lost Power */
#define  RTCSD_PWR	 0x80	/* clock power OK */

#define RTC_DIAG	0x0e	/* status register E - bios diagnostic */
#define RTCDG_BITS	"\020\010clock_battery\007ROM_cksum\006config_unit\005memory_size\004fixed_disk\003invalid_time"

#define RTC_RESET	0x0f	/* status register F - reset code byte */
#define	 RTCRS_RST	 0x00		/* normal reset */
#define	 RTCRS_LOAD	 0x04		/* load system */

#define RTC_FDISKETTE	0x10	/* diskette drive type in upper/lower nibble */
#define	 RTCFDT_NONE	 0		/* none present */
#define	 RTCFDT_360K	 0x10		/* 360K */
#define	 RTCFDT_12M	 0x20		/* 1.2M */
#define  RTCFDT_720K     0x30           /* 720K */
#define	 RTCFDT_144M	 0x40		/* 1.44M */
#define  RTCFDT_288M_1   0x50		/* 2.88M, some BIOSes */
#define	 RTCFDT_288M	 0x60		/* 2.88M */

#define RTC_BASELO	0x15	/* low byte of basemem size */
#define RTC_BASEHI	0x16	/* high byte of basemem size */
#define RTC_EXTLO	0x17	/* low byte of extended mem size */
#define RTC_EXTHI	0x18	/* low byte of extended mem size */

#define	RTC_CENTURY	0x32	/* current century */

#ifdef _KERNEL
extern  struct mtx atrtc_time_lock;
extern	int atrtcclock_disable;
int	rtcin(int reg);
void	atrtc_restore(void);
void	writertc(int reg, u_char val);
#endif

#endif /* _I386_ISA_RTC_H_ */
