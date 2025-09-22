/*	$OpenBSD: clock.c,v 1.54 2023/10/24 13:20:10 claudio Exp $ */

/*
 * Copyright (c) 2001-2004 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Clock code for systems using the on-cpu counter register, when both the
 * counter and comparator registers are available (i.e. everything MIPS-III
 * or MIPS-IV capable but the R8000).
 *
 * On most processors, this register counts at half the pipeline frequency.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/stdint.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <mips64/mips_cpu.h>

static struct evcount cp0_clock_count;
static int cp0_clock_irq = 5;
uint64_t cp0_nsec_cycle_ratio;
uint64_t cp0_nsec_max;

int	clockmatch(struct device *, void *, void *);
void	clockattach(struct device *, struct device *, void *);

struct cfdriver clock_cd = {
	NULL, "clock", DV_DULL
};

const struct cfattach clock_ca = {
	sizeof(struct device), clockmatch, clockattach
};

void	cp0_rearm_int5(void *, uint64_t);
void	cp0_trigger_int5_wrapper(void *);

const struct intrclock cp0_intrclock = {
	.ic_rearm = cp0_rearm_int5,
	.ic_trigger = cp0_trigger_int5_wrapper
};

void	cp0_initclock(void);
uint32_t cp0_int5(uint32_t, struct trapframe *);
void 	cp0_startclock(struct cpu_info *);
void	cp0_trigger_int5(void);
void	cp0_trigger_int5_masked(void);

int
clockmatch(struct device *parent, void *vcf, void *aux)
{
	struct mainbus_attach_args *maa = aux;

	return strcmp(maa->maa_name, clock_cd.cd_name) == 0;
}

void
clockattach(struct device *parent, struct device *self, void *aux)
{
	uint64_t cp0_freq = curcpu()->ci_hw.clock / CP0_CYCLE_DIVIDER;

	printf(": int 5\n");

	cp0_nsec_cycle_ratio = cp0_freq * (1ULL << 32) / 1000000000;
	cp0_nsec_max = UINT64_MAX / cp0_nsec_cycle_ratio;

	/*
	 * We need to register the interrupt now, for idle_mask to
	 * be computed correctly.
	 */
	set_intr(INTPRI_CLOCK, CR_INT_5, cp0_int5);
	evcount_attach(&cp0_clock_count, "clock", &cp0_clock_irq);
	evcount_percpu(&cp0_clock_count);

	/* try to avoid getting clock interrupts early */
	cp0_set_compare(cp0_get_count() - 1);

	md_initclock = cp0_initclock;
	md_startclock = cp0_startclock;
	md_triggerclock = cp0_trigger_int5;
}

/*
 *  Interrupt handler for targets using the internal count register
 *  as interval clock. Normally the system is run with the clock
 *  interrupt always enabled. Masking is done here and if the clock
 *  cannot be run the tick is handled later when the clock is logically
 *  unmasked again.
 */
uint32_t
cp0_int5(uint32_t mask, struct trapframe *tf)
{
	struct cpu_info *ci = curcpu();
	int s;

	evcount_inc(&cp0_clock_count);

	cp0_set_compare(cp0_get_count() - 1);	/* clear INT5 */

	/*
	 * Just ignore the interrupt if we're not ready to process it.
	 * cpu_initclocks() will retrigger it later.
	 */
	if (!ci->ci_clock_started)
		return CR_INT_5;

	/*
	 * If the clock interrupt is logically masked, defer all
	 * work until it is logically unmasked from splx(9).
	 */
	if (tf->ipl >= IPL_CLOCK) {
		ci->ci_clock_deferred = 1;
		return CR_INT_5;
	}
	ci->ci_clock_deferred = 0;

	/*
	 * Process clock interrupt.
	 */
	s = splclock();
#ifdef MULTIPROCESSOR
	register_t sr;

	sr = getsr();
	ENABLEIPI();
#endif
	clockintr_dispatch(tf);
#ifdef MULTIPROCESSOR
	setsr(sr);
#endif
	ci->ci_ipl = s;
	return CR_INT_5;	/* Clock is always on 5 */
}

/*
 * Arm INT5 to fire after the given number of nanoseconds have elapsed.
 * Only try once.  If we miss, let cp0_trigger_int5_masked() handle it.
 */
void
cp0_rearm_int5(void *unused, uint64_t nsecs)
{
	uint32_t cycles, t0;
	register_t sr;

	if (nsecs > cp0_nsec_max)
		nsecs = cp0_nsec_max;
	cycles = (nsecs * cp0_nsec_cycle_ratio) >> 32;

	/*
	 * Set compare, then immediately reread count.  If at least
	 * "cycles" CP0 ticks have elapsed and INT5 isn't already
	 * pending, we missed.
	 */
	sr = disableintr();
	t0 = cp0_get_count();
	cp0_set_compare(t0 + cycles);
	if (cycles <= cp0_get_count() - t0) {
		if (!ISSET(cp0_get_cause(), CR_INT_5))
			cp0_trigger_int5_masked();
	}
	setsr(sr);
}

void
cp0_trigger_int5(void)
{
	register_t sr;

	sr = disableintr();
	cp0_trigger_int5_masked();
	setsr(sr);
}

/*
 * Arm INT5 to fire as soon as possible.
 *
 * We need to spin until either (a) INT5 is pending or (b) the compare
 * register leads the count register, i.e. we know INT5 will be pending
 * very soon.
 *
 * To ensure we don't spin forever, double the compensatory offset
 * added to the compare value every time we miss the count register.
 * The initial offset of 16 cycles was chosen experimentally.  It
 * is the smallest power of two that doesn't require multiple loops
 * to arm the timer on most Octeon hardware.
 */
void
cp0_trigger_int5_masked(void)
{
	uint32_t offset = 16, t0;

	while (!ISSET(cp0_get_cause(), CR_INT_5)) {
		t0 = cp0_get_count();
		cp0_set_compare(t0 + offset);
		if (cp0_get_count() - t0 < offset)
			return;
		offset *= 2;
	}
}

void
cp0_trigger_int5_wrapper(void *unused)
{
	cp0_trigger_int5();
}

void
cp0_initclock(void)
{
	KASSERT(CPU_IS_PRIMARY(curcpu()));

	stathz = hz;
	profhz = stathz * 10;
	statclock_is_randomized = 1;
}

/*
 * Start the clock interrupt dispatch cycle.
 */
void
cp0_startclock(struct cpu_info *ci)
{
	int s;

	if (!CPU_IS_PRIMARY(ci)) {
		/* try to avoid getting clock interrupts early */
		cp0_set_compare(cp0_get_count() - 1);

		cp0_calibrate(ci);
	}

	clockintr_cpu_init(&cp0_intrclock);

	/* Start the clock. */
	s = splclock();
	ci->ci_clock_started = 1;
	clockintr_trigger();
	splx(s);
}
