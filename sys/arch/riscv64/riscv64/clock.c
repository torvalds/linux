/*	$OpenBSD: clock.c,v 1.14 2024/01/27 12:05:40 kettenis Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2003 Dale Rahn <drahn@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/clockintr.h>
#include <sys/evcount.h>
#include <sys/stdint.h>
#include <sys/timetc.h>

#include <machine/cpufunc.h>
#include <machine/sbi.h>

#include <riscv64/dev/riscv_cpu_intc.h>

extern uint64_t tb_freq;	/* machdep.c */
uint64_t timer_nsec_max;
uint64_t timer_nsec_cycle_ratio;

struct evcount clock_count;

void timer_startclock(void);
void timer_rearm(void *, uint64_t);
void timer_trigger(void *);

const struct intrclock timer_intrclock = {
	.ic_rearm = timer_rearm,
	.ic_trigger = timer_trigger
};

u_int	tb_get_timecount(struct timecounter *);

static struct timecounter tb_timecounter = {
	.tc_get_timecount = tb_get_timecount,
	.tc_counter_mask = 0xffffffff,
	.tc_frequency = 0,
	.tc_name = "tb",
	.tc_quality = 0,
	.tc_priv = NULL,
	.tc_user = TC_TB,
};

void	(*cpu_startclock_fcn)(void) = timer_startclock;
int	clock_intr(void *);

void
timer_rearm(void *unused, uint64_t nsecs)
{
	uint32_t cycles;

	if (nsecs > timer_nsec_max)
		nsecs = timer_nsec_max;
	cycles = (nsecs * timer_nsec_cycle_ratio) >> 32;
	sbi_set_timer(rdtime() + cycles);
}

void
timer_trigger(void *unused)
{
	sbi_set_timer(0);
}

u_int
tb_get_timecount(struct timecounter *tc)
{
	return rdtime();
}

void
cpu_initclocks(void)
{
	tb_timecounter.tc_frequency = tb_freq;
	tc_init(&tb_timecounter);

	stathz = hz;
	profhz = stathz * 10;
	statclock_is_randomized = 1;

	if (cpu_startclock_fcn == timer_startclock) {
		timer_nsec_cycle_ratio = tb_freq * (1ULL << 32) / 1000000000;
		timer_nsec_max = UINT64_MAX / timer_nsec_cycle_ratio;

		riscv_intc_intr_establish(IRQ_TIMER_SUPERVISOR, 0,
		    clock_intr, NULL, NULL);

		evcount_attach(&clock_count, "clock", NULL);
		evcount_percpu(&clock_count);
	}
}

void
timer_startclock(void)
{
	clockintr_cpu_init(&timer_intrclock);
	clockintr_trigger();
	csr_set(sie, SIE_STIE);
}

void
cpu_startclock(void)
{
	cpu_startclock_fcn();
}

int
clock_intr(void *frame)
{
	struct cpu_info *ci = curcpu();
	int s;

	sbi_set_timer(UINT64_MAX);	/* clear timer interrupt */

	/*
	 * If the clock interrupt is masked, defer all clock interrupt
	 * work until the clock interrupt is unmasked from splx(9).
	 */
	if (ci->ci_cpl >= IPL_CLOCK) {
		ci->ci_timer_deferred = 1;
		return 0;
	}
	ci->ci_timer_deferred = 0;

	s = splclock();
	intr_enable();
	clockintr_dispatch(frame);
	intr_disable();
	splx(s);

	evcount_inc(&clock_count);

	return 0;
}

void
setstatclockrate(int newhz)
{
}

void
delay(u_int us)
{
	uint64_t tb;

	tb = rdtime();
	tb += (us * tb_freq + 999999) / 1000000;
	while (tb > rdtime())
		continue;
}
