/*	$OpenBSD: clock.c,v 1.17 2023/09/17 14:50:51 cheloha Exp $	*/
/*	$NetBSD: clock.c,v 1.32 2006/09/05 11:09:36 uwe Exp $	*/

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/timetc.h>

#include <dev/clock_subr.h>

#include <sh/clock.h>
#include <sh/trap.h>
#include <sh/rtcreg.h>
#include <sh/tmureg.h>

#include <machine/intr.h>

#define	NWDOG 0

#define	MINYEAR		2002	/* "today" */
#define	SH_RTC_CLOCK	16384	/* Hz */

/*
 * OpenBSD/sh clock module
 *  + default 64Hz
 *  + use TMU channel 0 as clock interrupt source.
 *  + use TMU channel 1 as emulated software interrupt source.
 *  + use TMU channel 2 as freerunning counter for timecounter.
 *  + If RTC module is active, TMU channel 0 input source is RTC output.
 *    (1.6384kHz)
 */
struct {
	/* Hard clock */
	uint32_t hz_cnt;	/* clock interrupt interval count */
	uint32_t cpucycle_1us;	/* calibrated loop variable (1 us) */
	uint32_t tmuclk;	/* source clock of TMU0 (Hz) */

	/* RTC ops holder. default SH RTC module */
	struct rtc_ops rtc;
	int rtc_initialized;

	uint32_t pclock;	/* PCLOCK */
	uint32_t cpuclock;	/* CPU clock */
	int flags;

	struct timecounter tc;
} sh_clock = {
#ifdef PCLOCK
	.pclock = PCLOCK,
#endif
	.rtc = {
		/* SH RTC module to default RTC */
		.init	= sh_rtc_init,
		.get	= sh_rtc_get,
		.set	= sh_rtc_set
	}
};

uint32_t maxwdog;

/* TMU */
/* interrupt handler is timing critical. prepared for each. */
int sh3_clock_intr(void *);
int sh4_clock_intr(void *);
u_int sh_timecounter_get(struct timecounter *);

/*
 * Estimate CPU and Peripheral clock.
 */
#define	TMU_START(x)							\
do {									\
	_reg_bclr_1(SH_(TSTR), TSTR_STR##x);				\
	_reg_write_4(SH_(TCNT ## x), 0xffffffff);			\
	_reg_bset_1(SH_(TSTR), TSTR_STR##x);				\
} while (/*CONSTCOND*/0)

#define	TMU_ELAPSED(x)							\
	(0xffffffff - _reg_read_4(SH_(TCNT ## x)))

void
sh_clock_init(int flags, struct rtc_ops *rtc)
{
	uint32_t s, t0, cnt_1s;

	sh_clock.flags = flags;
	if (rtc != NULL)
		sh_clock.rtc = *rtc;	/* structure copy */

	/* Initialize TMU */
	_reg_write_2(SH_(TCR0), 0);
	_reg_write_2(SH_(TCR1), 0);
	_reg_write_2(SH_(TCR2), 0);

	/* Reset RTC alarm and interrupt */
	_reg_write_1(SH_(RCR1), 0);

	/* Stop all counter */
	_reg_write_1(SH_(TSTR), 0);

	/*
	 * Estimate CPU clock.
	 */
	if (sh_clock.flags & SH_CLOCK_NORTC) {
		/* Set TMU channel 0 source to PCLOCK / 16 */
		_reg_write_2(SH_(TCR0), TCR_TPSC_P16);
		sh_clock.tmuclk = sh_clock.pclock / 16;
	} else {
		/* Set TMU channel 0 source to RTC counter clock (16.384kHz) */
		_reg_write_2(SH_(TCR0),
		    CPU_IS_SH3 ? SH3_TCR_TPSC_RTC : SH4_TCR_TPSC_RTC);
		sh_clock.tmuclk = SH_RTC_CLOCK;

		/* Make sure RTC oscillator is enabled */
		_reg_bset_1(SH_(RCR2), SH_RCR2_ENABLE);
	}

	s = _cpu_exception_suspend();
	_cpu_spin(1);	/* load function on cache. */
	TMU_START(0);
	_cpu_spin(10000000);
	t0 = TMU_ELAPSED(0);
	_cpu_exception_resume(s);

	sh_clock.cpucycle_1us = (sh_clock.tmuclk * 10) / t0;

	cnt_1s = ((uint64_t)sh_clock.tmuclk * 10000000 * 10 + t0 / 2) / t0;
	if (CPU_IS_SH4)
		sh_clock.cpuclock = cnt_1s / 2;	/* two-issue */
	else
		sh_clock.cpuclock = cnt_1s;

	/*
	 * Estimate PCLOCK
	 */
	if (sh_clock.pclock == 0) {
		uint32_t t1;

		/* set TMU channel 1 source to PCLOCK / 4 */
		_reg_write_2(SH_(TCR1), TCR_TPSC_P4);
		s = _cpu_exception_suspend();
		_cpu_spin(1);	/* load function on cache. */
		TMU_START(0);
		TMU_START(1);
		_cpu_spin(cnt_1s); /* 1 sec. */
		t0 = TMU_ELAPSED(0);
		t1 = TMU_ELAPSED(1);
		_cpu_exception_resume(s);

		sh_clock.pclock =
		    ((uint64_t)t1 * 4 * SH_RTC_CLOCK + t0 / 2) / t0;
	}

	/* Stop all counters */
	_reg_write_1(SH_(TSTR), 0);

#undef TMU_START
#undef TMU_ELAPSED
}

int
sh_clock_get_cpuclock(void)
{
	return (sh_clock.cpuclock);
}

int
sh_clock_get_pclock(void)
{
	return (sh_clock.pclock);
}

void
setstatclockrate(int newhz)
{
}

u_int
sh_timecounter_get(struct timecounter *tc)
{
	return 0xffffffff - _reg_read_4(SH_(TCNT2));
}

/*
 *  Wait at least `n' usec.
 */
void
delay(int n)
{
	_cpu_spin(sh_clock.cpucycle_1us * n);
}

extern todr_chip_handle_t todr_handle;
struct todr_chip_handle rtc_todr;

int
rtc_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct clock_ymdhms dt;

	sh_clock.rtc.get(sh_clock.rtc._cookie, tv->tv_sec, &dt);
	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return 0;
}

int
rtc_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct clock_ymdhms dt;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);
	sh_clock.rtc.set(sh_clock.rtc._cookie, &dt);
	return 0;
}

/*
 * Start the clock interrupt.
 */
void
cpu_initclocks(void)
{
	if (sh_clock.pclock == 0)
		panic("No PCLOCK information.");

	/* Set global variables. */
	tick = 1000000 / hz;
	tick_nsec = 1000000000 / hz;

	stathz = hz;
	profhz = stathz;
}

void
cpu_startclock(void)
{
	clockintr_cpu_init(NULL);

	/*
	 * Use TMU channel 0 as hard clock
	 */
	_reg_bclr_1(SH_(TSTR), TSTR_STR0);

	if (sh_clock.flags & SH_CLOCK_NORTC) {
		/* use PCLOCK/16 as TMU0 source */
		_reg_write_2(SH_(TCR0), TCR_UNIE | TCR_TPSC_P16);
	} else {
		/* use RTC clock as TMU0 source */
		_reg_write_2(SH_(TCR0), TCR_UNIE |
		    (CPU_IS_SH3 ? SH3_TCR_TPSC_RTC : SH4_TCR_TPSC_RTC));
	}
	sh_clock.hz_cnt = sh_clock.tmuclk / hz - 1;

	_reg_write_4(SH_(TCOR0), sh_clock.hz_cnt);
	_reg_write_4(SH_(TCNT0), sh_clock.hz_cnt);

	intc_intr_establish(SH_INTEVT_TMU0_TUNI0, IST_LEVEL, IPL_CLOCK,
	    CPU_IS_SH3 ? sh3_clock_intr : sh4_clock_intr, NULL, "clock");
	/* start hardclock */
	_reg_bset_1(SH_(TSTR), TSTR_STR0);

	/*
	 * TMU channel 1 is one shot timer for soft interrupts.
	 */
	_reg_write_2(SH_(TCR1), TCR_UNIE | TCR_TPSC_P4);
	_reg_write_4(SH_(TCOR1), 0xffffffff);

	/*
	 * TMU channel 2 is freerunning counter for timecounter.
	 */
	_reg_write_2(SH_(TCR2), TCR_TPSC_P4);
	_reg_write_4(SH_(TCOR2), 0xffffffff);

	/*
	 * Start and initialize timecounter.
	 */
	_reg_bset_1(SH_(TSTR), TSTR_STR2);

	sh_clock.tc.tc_get_timecount = sh_timecounter_get;
	sh_clock.tc.tc_frequency = sh_clock.pclock / 4;
	sh_clock.tc.tc_name = "tmu_pclock_4";
	sh_clock.tc.tc_quality = 100;
	sh_clock.tc.tc_counter_mask = 0xffffffff;
	tc_init(&sh_clock.tc);

	/* Make sure to start RTC */
	if (sh_clock.rtc.init != NULL)
		sh_clock.rtc.init(sh_clock.rtc._cookie);

	rtc_todr.todr_gettime = rtc_gettime;
	rtc_todr.todr_settime = rtc_settime;
	todr_handle = &rtc_todr;
}

#ifdef SH3
int
sh3_clock_intr(void *arg) /* trap frame */
{
#if (NWDOG > 0)
	uint32_t i;

	i = (uint32_t)SHREG_WTCNT_R;
	if (i > maxwdog)
		maxwdog = i;
	wdog_wr_cnt(0);			/* reset to zero */
#endif
	/* clear underflow status */
	_reg_bclr_2(SH3_TCR0, TCR_UNF);

	clockintr_dispatch(arg);

	return (1);
}
#endif /* SH3 */

#ifdef SH4
int
sh4_clock_intr(void *arg) /* trap frame */
{
#if (NWDOG > 0)
	uint32_t i;

	i = (uint32_t)SHREG_WTCNT_R;
	if (i > maxwdog)
		maxwdog = i;
	wdog_wr_cnt(0);			/* reset to zero */
#endif
	/* clear underflow status */
	_reg_bclr_2(SH4_TCR0, TCR_UNF);

	clockintr_dispatch(arg);

	return (1);
}
#endif /* SH4 */

/*
 * SH3 RTC module ops.
 */

void
sh_rtc_init(void *cookie)
{
	/* Make sure to start RTC */
	_reg_write_1(SH_(RCR2), SH_RCR2_ENABLE | SH_RCR2_START);
}

void
sh_rtc_get(void *cookie, time_t base, struct clock_ymdhms *dt)
{
	int retry = 8;

	/* disable carry interrupt */
	_reg_bclr_1(SH_(RCR1), SH_RCR1_CIE);

	do {
		uint8_t r = _reg_read_1(SH_(RCR1));
		r &= ~SH_RCR1_CF;
		r |= SH_RCR1_AF; /* don't clear alarm flag */
		_reg_write_1(SH_(RCR1), r);

		if (CPU_IS_SH3)
			dt->dt_year = FROMBCD(_reg_read_1(SH3_RYRCNT));
		else
			dt->dt_year = FROMBCD(_reg_read_2(SH4_RYRCNT) & 0x00ff);

		/* read counter */
#define	RTCGET(x, y)	dt->dt_ ## x = FROMBCD(_reg_read_1(SH_(R ## y ## CNT)))
		RTCGET(mon, MON);
		RTCGET(wday, WK);
		RTCGET(day, DAY);
		RTCGET(hour, HR);
		RTCGET(min, MIN);
		RTCGET(sec, SEC);
#undef RTCGET
	} while ((_reg_read_1(SH_(RCR1)) & SH_RCR1_CF) && --retry > 0);

	if (retry == 0) {
		printf("rtc_gettime: couldn't read RTC register.\n");
		memset(dt, 0, sizeof(*dt));
		return;
	}

	dt->dt_year = (dt->dt_year % 100) + 1900;
	if (dt->dt_year < 1970)
		dt->dt_year += 100;
}

void
sh_rtc_set(void *cookie, struct clock_ymdhms *dt)
{
	uint8_t r;

	/* stop clock */
	r = _reg_read_1(SH_(RCR2));
	r |= SH_RCR2_RESET;
	r &= ~SH_RCR2_START;
	_reg_write_1(SH_(RCR2), r);

	/* set time */
	if (CPU_IS_SH3)
		_reg_write_1(SH3_RYRCNT, TOBCD(dt->dt_year % 100));
	else
		_reg_write_2(SH4_RYRCNT, TOBCD(dt->dt_year % 100));
#define	RTCSET(x, y)	_reg_write_1(SH_(R ## x ## CNT), TOBCD(dt->dt_ ## y))
	RTCSET(MON, mon);
	RTCSET(WK, wday);
	RTCSET(DAY, day);
	RTCSET(HR, hour);
	RTCSET(MIN, min);
	RTCSET(SEC, sec);
#undef RTCSET
	/* start clock */
	_reg_write_1(SH_(RCR2), r | SH_RCR2_START);
}
