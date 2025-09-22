/*	$OpenBSD: clock.c,v 1.14 2023/09/17 14:50:51 cheloha Exp $	*/

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

extern uint64_t tb_freq;	/* cpu.c */
uint64_t dec_nsec_cycle_ratio;
uint64_t dec_nsec_max;

struct evcount clock_count;

void dec_rearm(void *, uint64_t);
void dec_trigger(void *);

const struct intrclock dec_intrclock = {
	.ic_rearm = dec_rearm,
	.ic_trigger = dec_trigger
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

void
dec_rearm(void *unused, uint64_t nsecs)
{
	uint32_t cycles;

	if (nsecs > dec_nsec_max)
		nsecs = dec_nsec_max;
	cycles = (nsecs * dec_nsec_cycle_ratio) >> 32;
	if (cycles > UINT32_MAX >> 1)
		cycles = UINT32_MAX >> 1;
	mtdec(cycles);
}

void
dec_trigger(void *unused)
{
	u_long s;

	s = intr_disable();
	mtdec(0);
	mtdec(UINT32_MAX);
	intr_restore(s);
}

u_int
tb_get_timecount(struct timecounter *tc)
{
	return mftb();
}

void
cpu_initclocks(void)
{
	tb_timecounter.tc_frequency = tb_freq;
	tc_init(&tb_timecounter);

	dec_nsec_cycle_ratio = tb_freq * (1ULL << 32) / 1000000000;
	dec_nsec_max = UINT64_MAX / dec_nsec_cycle_ratio;

	stathz = hz;
	profhz = stathz * 10;
	statclock_is_randomized = 1;

	evcount_attach(&clock_count, "clock", NULL);
}

void
cpu_startclock(void)
{
	clockintr_cpu_init(&dec_intrclock);
	clockintr_trigger();
	intr_enable();
}

void
decr_intr(struct trapframe *frame)
{
	struct cpu_info *ci = curcpu();
	int s;

	clock_count.ec_count++;

	mtdec(UINT32_MAX >> 1);		/* clear DEC exception */

	/*
	 * If the clock interrupt is masked, postpone all work until
	 * it is unmasked in splx(9).
	 */
	if (ci->ci_cpl >= IPL_CLOCK) {
		ci->ci_dec_deferred = 1;
		return;
	}
	ci->ci_dec_deferred = 0;

	s = splclock();
	intr_enable();
	clockintr_dispatch(frame);
	intr_disable();
	splx(s);
}

void
setstatclockrate(int newhz)
{
}

void
delay(u_int us)
{
	uint64_t tb;

	tb = mftb();
	tb += (us * tb_freq + 999999) / 1000000;
	while (tb > mftb())
		continue;
}
