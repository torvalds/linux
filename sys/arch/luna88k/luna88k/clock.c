/* $OpenBSD: clock.c,v 1.19 2023/09/17 14:50:51 cheloha Exp $ */
/* $NetBSD: clock.c,v 1.2 2000/01/11 10:29:35 nisimura Exp $ */

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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

/* from NetBSD/luna68k sys/arch/luna68k/luna68k/clock.c */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/evcount.h>
#include <sys/timetc.h>

#include <machine/board.h>
#include <machine/cpu.h>

#include <dev/clock_subr.h>
#include <luna88k/luna88k/clockvar.h>

struct device *clockdev;
const struct clockfns *clockfns;
struct evcount *clockevc;
int clockinitted;

extern todr_chip_handle_t todr_handle;
struct todr_chip_handle rtc_todr;

int
rtc_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct clock_ymdhms dt;

	(*clockfns->cf_get)(clockdev, tv->tv_sec, &dt);
	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return 0;
}

int
rtc_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct clock_ymdhms dt;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);
	(*clockfns->cf_set)(clockdev, &dt);
	return 0;
}

void
clockattach(struct device *dev, const struct clockfns *fns,
	struct evcount *evc)
{
	/*
	 * Just bookkeeping.
	 */
	if (clockfns != NULL)
		panic("clockattach: multiple clocks");
	clockdev = dev;
	clockfns = fns;
	clockevc = evc;
}

/*
 * Machine-dependent clock routines.
 */

u_int	clock_get_tc(struct timecounter *);

struct timecounter clock_tc = {
	.tc_get_timecount = clock_get_tc,
	.tc_counter_mask = 0xffffffff,
	.tc_frequency = 0, /* will be filled in */
	.tc_name = "clock",
	.tc_quality = 0,
	.tc_priv = NULL,
	.tc_user = 0,
};

/*
 * Start the real-time and statistics clocks.
 */
void
cpu_initclocks()
{

#ifdef DIAGNOSTIC
	if (clockfns == NULL)
		panic("cpu_initclocks: no clock attached");
#endif

	tick = 1000000 / hz;	/* number of microseconds between interrupts */
	tick_nsec = 1000000000 / hz;

	clock_tc.tc_frequency = hz;
	tc_init(&clock_tc);

	stathz = hz;
	profhz = stathz;
}

void
cpu_startclock(void)
{
	clockintr_cpu_init(NULL);

	clockinitted = 1;

	rtc_todr.todr_gettime = rtc_gettime;
	rtc_todr.todr_settime = rtc_settime;
	todr_handle = &rtc_todr;
}

void
setstatclockrate(int newhz)
{
}

/*
 * Clock interrupt routine
 */
int
clockintr(void *eframe)
{
	struct cpu_info *ci = curcpu();

#ifdef MULTIPROCESSOR
	if (CPU_IS_PRIMARY(ci))
#endif
		clockevc->ec_count++;

	*(volatile uint32_t *)(ci->ci_clock_ack) = ~0;
	if (clockinitted)
		clockintr_dispatch(eframe);
	return 1;
}

u_int
clock_get_tc(struct timecounter *tc)
{
	return (u_int)clockevc->ec_count;
}
