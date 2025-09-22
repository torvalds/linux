/*	$OpenBSD: clock.h,v 1.2 2008/06/26 05:42:12 ray Exp $	*/
/*	$NetBSD: clock.h,v 1.2 2002/04/28 17:10:33 uch Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by UCHIYAMA Yasushi.
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
 * void sh_clock_init(int flags, struct rtc_ops *):
 *   flags:
 *	SH_CLOCK_NORTC		... If SH RTC module is disabled, set this.
 *				    internal module don't use RTCCLK.
 *	SH_CLOCK_NOINITTODR	... Don't initialize RTC time.
 *   rtc_ops:
 *	Machine dependent RTC ops pointer. If NULL is specified, use SH
 *	internal RTC.
 *
 * void machine_clock_init(void):
 *	Implement machine specific part of clock routines.
 *	must call sh_clock_init() at exit.
 *
 * int sh_clock_get_cpuclock(void):
 *	returns CPU clock estimated by sh_clock_init().
 *
 * int sh_clock_get_pclock(void):
 *	returns PCLOCK. when PCLOCK is not specified by kernel configuration
 *	file, this value is estimated by sh_clock_init().
 *
 */
struct rtc_ops;
struct clock_ymdhms;

void sh_clock_init(int, struct rtc_ops *);
#define	SH_CLOCK_NORTC			0x00000001
#define	SH_CLOCK_NOINITTODR		0x00000002
void machine_clock_init(void);

int sh_clock_get_cpuclock(void);
int sh_clock_get_pclock(void);

/*
 * SH RTC module interface.
 */
void sh_rtc_init(void *);
void sh_rtc_get(void *, time_t, struct clock_ymdhms *);
void sh_rtc_set(void *, struct clock_ymdhms *);

/*
 * machine specific RTC ops
 */
struct clock_ymdhms;
struct rtc_ops {
	void *_cookie;
	void (*init)(void *);
	void (*get)(void *, time_t, struct clock_ymdhms *);
	void (*set)(void *, struct clock_ymdhms *);
};

