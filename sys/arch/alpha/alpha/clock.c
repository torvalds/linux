/*	$OpenBSD: clock.c,v 1.32 2025/06/28 16:04:09 miod Exp $	*/
/*	$NetBSD: clock.c,v 1.29 2000/06/05 21:47:10 thorpej Exp $	*/

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and Ralph Campbell.
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
 * from: Utah Hdr: clock.c 1.18 91/01/21
 *
 *	@(#)clock.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/sched.h>
#include <sys/timetc.h>

#include <dev/clock_subr.h>

#include <machine/rpb.h>
#include <machine/autoconf.h>
#include <machine/cpuconf.h>

#include <alpha/alpha/clockvar.h>

struct device *clockdev;
const struct clockfns *clockfns;

struct evcount clk_count;
int clk_irq = 0;

u_int rpcc_get_timecount(struct timecounter *);
struct timecounter rpcc_timecounter = {
	.tc_get_timecount = rpcc_get_timecount,
	.tc_counter_mask = ~0u,
	.tc_frequency = 0,
	.tc_name = "rpcc",
	.tc_quality = 0,
	.tc_priv = NULL,
	.tc_user = 0,
};

extern todr_chip_handle_t todr_handle;
struct todr_chip_handle rtc_todr;

int
rtc_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct clock_ymdhms dt;
	struct clocktime ct;
	int year;

	(*clockfns->cf_get)(clockdev, tv->tv_sec, &ct);

	year = 1900 + ct.year;
	if (year < 1970)
		year += 100;
	dt.dt_year = year;
	dt.dt_mon = ct.mon;
	dt.dt_day = ct.day;
	dt.dt_hour = ct.hour;
	dt.dt_min = ct.min;
	dt.dt_sec = ct.sec;

	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return 0;
}

int
rtc_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct clock_ymdhms dt;
	struct clocktime ct;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	/* rt clock wants 2 digits */
	ct.year = dt.dt_year % 100;
	ct.mon = dt.dt_mon;
	ct.day = dt.dt_day;
	ct.hour = dt.dt_hour;
	ct.min = dt.dt_min;
	ct.sec = dt.dt_sec;
	ct.dow = dt.dt_wday;

	(*clockfns->cf_set)(clockdev, &ct);
	return 0;
}

void
clockattach(struct device *dev, const struct clockfns *fns)
{

	/*
	 * Just bookkeeping.
	 */
	printf("\n");

	if (clockfns != NULL)
		panic("clockattach: multiple clocks");
	clockdev = dev;
	clockfns = fns;
}

/*
 * Machine-dependent clock routines.
 */

/*
 * Start the real-time and statistics clocks.
 */
void
cpu_initclocks(void)
{
	u_int32_t cycles_per_sec;
	struct clocktime ct;
	u_int32_t first_rpcc, second_rpcc; /* only lower 32 bits are valid */
	int first_sec;

	if (clockfns == NULL)
		panic("cpu_initclocks: no clock attached");

	tick = 1000000 / hz;	/* number of microseconds between interrupts */
	tick_nsec = 1000000000 / hz;

	evcount_attach(&clk_count, "clock", &clk_irq);

	/*
	 * Get the clock started.
	 */
	(*clockfns->cf_init)(clockdev);

	/*
	 * Calibrate the cycle counter frequency.
	 */
	(*clockfns->cf_get)(clockdev, 0, &ct);
	first_sec = ct.sec;

	/* Let the clock tick one second. */
	do {
		first_rpcc = alpha_rpcc();
		(*clockfns->cf_get)(clockdev, 0, &ct);
	} while (ct.sec == first_sec);
	first_sec = ct.sec;
	/* Let the clock tick one more second. */
	do {
		second_rpcc = alpha_rpcc();
		(*clockfns->cf_get)(clockdev, 0, &ct);
	} while (ct.sec == first_sec);

	cycles_per_sec = second_rpcc - first_rpcc;

	rpcc_timecounter.tc_frequency = cycles_per_sec;
	tc_init(&rpcc_timecounter);

	stathz = hz;
	profhz = stathz;
}

void
cpu_startclock(void)
{
	clockintr_cpu_init(NULL);

	/*
	 * Establish the clock interrupt; it's a special case.
	 *
	 * We establish the clock interrupt this late because if
	 * we do it at clock attach time, we may have never been at
	 * spl0() since taking over the system.  Some versions of
	 * PALcode save a clock interrupt, which would get delivered
	 * when we spl0() in autoconf.c.  If established the clock
	 * interrupt handler earlier, that interrupt would go to
	 * hardclock, which would then fall over because the pointer
	 * to the virtual timers wasn't set at that time.
	 */
	platform.clockintr = clockintr_dispatch;

	rtc_todr.todr_gettime = rtc_gettime;
	rtc_todr.todr_settime = rtc_settime;
	todr_handle = &rtc_todr;
}

void
setstatclockrate(int newhz)
{
}

u_int
rpcc_get_timecount(struct timecounter *tc)
{
	return alpha_rpcc();
}
