/*	$OpenBSD: clock.c,v 1.42 2023/09/17 14:50:50 cheloha Exp $	*/
/*	$NetBSD: clock.c,v 1.1 2003/04/26 18:39:50 fvdl Exp $	*/

/*-
 * Copyright (c) 1993, 1994 Charles M. Hannum.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz and Don Ahn.
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
 *	@(#)clock.c	7.2 (Berkeley) 5/12/91
 */
/*
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
  Copyright 1988, 1989 by Intel Corporation, Santa Clara, California.

		All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appears in all
copies and that both the copyright notice and this permission notice
appear in supporting documentation, and that the name of Intel
not be used in advertising or publicity pertaining to distribution
of the software without specific, written prior permission.

INTEL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS,
IN NO EVENT SHALL INTEL BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

/*
 * Primitive clock interrupt routines.
 */

/* #define CLOCK_DEBUG */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/clockintr.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/timeout.h>
#include <sys/timetc.h>

#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/pio.h>
#include <machine/cpufunc.h>

#include <dev/clock_subr.h>
#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#include <dev/ic/mc146818reg.h>
#include <dev/ic/i8253reg.h>
#include <amd64/isa/nvram.h>

/* Timecounter on the i8254 */
u_int32_t i8254_lastcount;
u_int32_t i8254_offset;
int i8254_ticked;
u_int i8254_get_timecount(struct timecounter *tc);

u_int i8254_simple_get_timecount(struct timecounter *tc);

static struct timecounter i8254_timecounter = {
	.tc_get_timecount = i8254_get_timecount,
	.tc_counter_mask = ~0u,
	.tc_frequency = TIMER_FREQ,
	.tc_name = "i8254",
	.tc_quality = 0,
	.tc_priv = NULL,
	.tc_user = 0,
};

int	clockintr(void *);
int	rtcintr(void *);
int	gettick(void);
void	rtcdrain(void *v);
int	rtcget(mc_todregs *);
void	rtcput(mc_todregs *);
int	bcdtobin(int);
int	bintobcd(int);

u_int mc146818_read(void *, u_int);
void mc146818_write(void *, u_int, u_int);

u_int
mc146818_read(void *sc, u_int reg)
{
	outb(IO_RTC, reg);
	DELAY(1);
	return (inb(IO_RTC+1));
}

void
mc146818_write(void *sc, u_int reg, u_int datum)
{
	outb(IO_RTC, reg);
	DELAY(1);
	outb(IO_RTC+1, datum);
	DELAY(1);
}

struct mutex timer_mutex = MUTEX_INITIALIZER(IPL_HIGH);

u_long rtclock_tval;

void
startclocks(void)
{
	mtx_enter(&timer_mutex);
	rtclock_tval = TIMER_DIV(hz);
	i8254_startclock();
	mtx_leave(&timer_mutex);
}

int
clockintr(void *frame)
{
	if (timecounter->tc_get_timecount == i8254_get_timecount) {
		if (i8254_ticked) {
			i8254_ticked = 0;
		} else {
			i8254_offset += rtclock_tval;
			i8254_lastcount = 0;
		}
	}

	clockintr_dispatch(frame);

	return 1;
}

int
rtcintr(void *frame)
{
	u_int stat = 0;

	/*
	 * If rtcintr is 'late', next intr may happen immediately.
	 * Get them all. (Also, see comment in cpu_initclocks().)
	 */
	while (mc146818_read(NULL, MC_REGC) & MC_REGC_PF)
		stat = 1;

	if (stat)
		clockintr_dispatch(frame);

	return (stat);
}

int
gettick(void)
{
	u_long s;
	u_char lo, hi;

	/* Don't want someone screwing with the counter while we're here. */
	mtx_enter(&timer_mutex);
	s = intr_disable();
	/* Select counter 0 and latch it. */
	outb(IO_TIMER1+TIMER_MODE, TIMER_SEL0 | TIMER_LATCH);
	lo = inb(IO_TIMER1+TIMER_CNTR0);
	hi = inb(IO_TIMER1+TIMER_CNTR0);
	intr_restore(s);
	mtx_leave(&timer_mutex);
	return ((hi << 8) | lo);
}

/*
 * Wait "n" microseconds.
 * Relies on timer 1 counting down from (TIMER_FREQ / hz) at TIMER_FREQ Hz.
 * Note: timer had better have been programmed before this is first used!
 * (Note that we use `rate generator' mode, which counts at 1:1; `square
 * wave' mode counts at 2:1).
 */
void
i8254_delay(int n)
{
	int limit, tick, otick;
	static const int delaytab[26] = {
		 0,  2,  3,  4,  5,  6,  7,  9, 10, 11,
		12, 13, 15, 16, 17, 18, 19, 21, 22, 23,
		24, 25, 27, 28, 29, 30,
	};

	/*
	 * Read the counter first, so that the rest of the setup overhead is
	 * counted.
	 */
	otick = gettick();

	if (n <= 25)
		n = delaytab[n];
	else {
		/* Force 64-bit math to avoid 32-bit overflow if possible. */
		n = (int64_t)n * TIMER_FREQ / 1000000;
	}

	limit = TIMER_FREQ / hz;

	while (n > 0) {
		tick = gettick();
		if (tick > otick)
			n -= limit - (tick - otick);
		else
			n -= otick - tick;
		otick = tick;
	}
}

void
rtcdrain(void *v)
{
	struct timeout *to = (struct timeout *)v;

	if (to != NULL)
		timeout_del(to);

	/* Drain any un-acknowledged RTC interrupts. */
	while (mc146818_read(NULL, MC_REGC) & MC_REGC_PF)
		; /* Nothing. */
}

void
i8254_initclocks(void)
{
	i8254_inittimecounter();	/* hook the interrupt-based i8254 tc */

	stathz = 128;
	profhz = 1024;		/* XXX does not divide into 1 billion */
}

void
i8254_start_both_clocks(void)
{
	clockintr_cpu_init(NULL);

	/*
	 * While the clock interrupt handler isn't really MPSAFE, the
	 * i8254 can't really be used as a clock on a true MP system.
	 */
	isa_intr_establish(NULL, 0, IST_PULSE, IPL_CLOCK | IPL_MPSAFE,
	    clockintr, 0, "clock");
	isa_intr_establish(NULL, 8, IST_PULSE, IPL_STATCLOCK | IPL_MPSAFE,
	    rtcintr, 0, "rtc");

	rtcstart();			/* start the mc146818 clock */
}

void
rtcstart(void)
{
	static struct timeout rtcdrain_timeout;

	mc146818_write(NULL, MC_REGA, MC_BASE_32_KHz | MC_RATE_128_Hz);
	mc146818_write(NULL, MC_REGB, MC_REGB_24HR | MC_REGB_PIE);

	/*
	 * On a number of i386 systems, the rtc will fail to start when booting
	 * the system. This is due to us missing to acknowledge an interrupt
	 * during early stages of the boot process. If we do not acknowledge
	 * the interrupt, the rtc clock will not generate further interrupts.
	 * To solve this, once interrupts are enabled, use a timeout (once)
	 * to drain any un-acknowledged rtc interrupt(s).
	 */
	timeout_set(&rtcdrain_timeout, rtcdrain, (void *)&rtcdrain_timeout);
	timeout_add(&rtcdrain_timeout, 1);
}

void
rtcstop(void)
{
	mc146818_write(NULL, MC_REGB, MC_REGB_24HR);
}

int
rtcget(mc_todregs *regs)
{
	if ((mc146818_read(NULL, MC_REGD) & MC_REGD_VRT) == 0) /* XXX softc */
		return (-1);
	MC146818_GETTOD(NULL, regs);			/* XXX softc */
	return (0);
}

void
rtcput(mc_todregs *regs)
{
	MC146818_PUTTOD(NULL, regs);			/* XXX softc */
}

int
bcdtobin(int n)
{
	return (((n >> 4) & 0x0f) * 10 + (n & 0x0f));
}

int
bintobcd(int n)
{
	return ((u_char)(((n / 10) << 4) & 0xf0) | ((n % 10) & 0x0f));
}

/*
 * check whether the CMOS layout is "standard"-like (ie, not PS/2-like),
 * to be called at splclock()
 */
static int cmoscheck(void);
static int
cmoscheck(void)
{
	int i;
	unsigned short cksum = 0;

	for (i = 0x10; i <= 0x2d; i++)
		cksum += mc146818_read(NULL, i); /* XXX softc */

	return (cksum == (mc146818_read(NULL, 0x2e) << 8)
			  + mc146818_read(NULL, 0x2f));
}

/*
 * patchable to control century byte handling:
 * 1: always update
 * -1: never touch
 * 0: try to figure out itself
 */
int rtc_update_century = 0;

/*
 * Expand a two-digit year as read from the clock chip
 * into full width.
 * Being here, deal with the CMOS century byte.
 */
static int centb = NVRAM_CENTURY;
static int clock_expandyear(int);
static int
clock_expandyear(int clockyear)
{
	int s, clockcentury, cmoscentury;

	clockcentury = (clockyear < 70) ? 20 : 19;
	clockyear += 100 * clockcentury;

	if (rtc_update_century < 0)
		return (clockyear);

	s = splclock();
	if (cmoscheck())
		cmoscentury = mc146818_read(NULL, NVRAM_CENTURY);
	else
		cmoscentury = 0;
	splx(s);
	if (!cmoscentury)
		return (clockyear);

	cmoscentury = bcdtobin(cmoscentury);

	if (cmoscentury != clockcentury) {
		/* XXX note: saying "century is 20" might confuse the naive. */
		printf("WARNING: NVRAM century is %d but RTC year is %d\n",
		       cmoscentury, clockyear);

		/* Kludge to roll over century. */
		if ((rtc_update_century > 0) ||
		    ((cmoscentury == 19) && (clockcentury == 20) &&
		     (clockyear == 2000))) {
			printf("WARNING: Setting NVRAM century to %d\n",
			       clockcentury);
			s = splclock();
			mc146818_write(NULL, centb, bintobcd(clockcentury));
			splx(s);
		}
	} else if (cmoscentury == 19 && rtc_update_century == 0)
		rtc_update_century = 1; /* will update later in resettodr() */

	return (clockyear);
}

int
rtcgettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	mc_todregs rtclk;
	struct clock_ymdhms dt;
	int s;

	s = splclock();
	if (rtcget(&rtclk)) {
		splx(s);
		return EINVAL;
	}
	splx(s);

#ifdef CLOCK_DEBUG
	printf("readclock: %x/%x/%x %x:%x:%x\n", rtclk[MC_YEAR],
	    rtclk[MC_MONTH], rtclk[MC_DOM], rtclk[MC_HOUR], rtclk[MC_MIN],
	    rtclk[MC_SEC]);
#endif

	dt.dt_sec = bcdtobin(rtclk[MC_SEC]);
	dt.dt_min = bcdtobin(rtclk[MC_MIN]);
	dt.dt_hour = bcdtobin(rtclk[MC_HOUR]);
	dt.dt_day = bcdtobin(rtclk[MC_DOM]);
	dt.dt_mon = bcdtobin(rtclk[MC_MONTH]);
	dt.dt_year = clock_expandyear(bcdtobin(rtclk[MC_YEAR]));

	tv->tv_sec = clock_ymdhms_to_secs(&dt) - utc_offset;
	tv->tv_usec = 0;
	return 0;
}

int
rtcsettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	mc_todregs rtclk;
	struct clock_ymdhms dt;
	int century, s;

	s = splclock();
	if (rtcget(&rtclk))
		memset(&rtclk, 0, sizeof(rtclk));
	splx(s);

	clock_secs_to_ymdhms(tv->tv_sec + utc_offset, &dt);

	rtclk[MC_SEC] = bintobcd(dt.dt_sec);
	rtclk[MC_MIN] = bintobcd(dt.dt_min);
	rtclk[MC_HOUR] = bintobcd(dt.dt_hour);
	rtclk[MC_DOW] = dt.dt_wday + 1;
	rtclk[MC_YEAR] = bintobcd(dt.dt_year % 100);
	rtclk[MC_MONTH] = bintobcd(dt.dt_mon);
	rtclk[MC_DOM] = bintobcd(dt.dt_day);

#ifdef CLOCK_DEBUG
	printf("setclock: %x/%x/%x %x:%x:%x\n", rtclk[MC_YEAR], rtclk[MC_MONTH],
	   rtclk[MC_DOM], rtclk[MC_HOUR], rtclk[MC_MIN], rtclk[MC_SEC]);
#endif

	s = splclock();
	rtcput(&rtclk);
	if (rtc_update_century > 0) {
		century = bintobcd(dt.dt_year / 100);
		mc146818_write(NULL, centb, century); /* XXX softc */
	}
	splx(s);
	return 0;
}

struct todr_chip_handle rtc_todr;

void
rtcinit(void)
{
	rtc_todr.todr_gettime = rtcgettime;
	rtc_todr.todr_settime = rtcsettime;
	rtc_todr.todr_quality = 0;
	todr_attach(&rtc_todr);
}

void
setstatclockrate(int arg)
{
	if (initclock_func == i8254_initclocks) {
		if (arg == stathz)
			mc146818_write(NULL, MC_REGA,
			    MC_BASE_32_KHz | MC_RATE_128_Hz);
		else
			mc146818_write(NULL, MC_REGA,
			    MC_BASE_32_KHz | MC_RATE_1024_Hz);
	}
}

void
i8254_inittimecounter(void)
{
	tc_init(&i8254_timecounter);
}

/*
 * If we're using lapic to drive hardclock, we can use a simpler
 * algorithm for the i8254 timecounters.
 */
void
i8254_inittimecounter_simple(void)
{
	i8254_timecounter.tc_get_timecount = i8254_simple_get_timecount;
	i8254_timecounter.tc_counter_mask = 0x7fff;
        i8254_timecounter.tc_frequency = TIMER_FREQ;

	mtx_enter(&timer_mutex);
	rtclock_tval = 0x8000;
	i8254_startclock();
	mtx_leave(&timer_mutex);

	tc_init(&i8254_timecounter);
}

void
i8254_startclock(void)
{
	u_long tval = rtclock_tval;

	outb(IO_TIMER1 + TIMER_MODE, TIMER_SEL0 | TIMER_RATEGEN | TIMER_16BIT);
	outb(IO_TIMER1 + TIMER_CNTR0, tval & 0xff);
	outb(IO_TIMER1 + TIMER_CNTR0, tval >> 8);
}

u_int
i8254_simple_get_timecount(struct timecounter *tc)
{
	return (rtclock_tval - gettick());
}

u_int
i8254_get_timecount(struct timecounter *tc)
{
	u_char hi, lo;
	u_int count;
	u_long s;

	s = intr_disable();

	outb(IO_TIMER1+TIMER_MODE, TIMER_SEL0 | TIMER_LATCH);
	lo = inb(IO_TIMER1+TIMER_CNTR0);
	hi = inb(IO_TIMER1+TIMER_CNTR0);

	count = rtclock_tval - ((hi << 8) | lo);

	if (count < i8254_lastcount) {
		i8254_ticked = 1;
		i8254_offset += rtclock_tval;
	}
	i8254_lastcount = count;
	count += i8254_offset;

	intr_restore(s);

	return (count);
}
