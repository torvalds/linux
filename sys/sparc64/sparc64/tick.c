/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
 * Copyright (c) 2005, 2008 Marius Strobl <marius@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/timeet.h>
#include <sys/timetc.h>

#include <dev/ofw/openfirm.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/frame.h>
#include <machine/intr_machdep.h>
#include <machine/smp.h>
#include <machine/tick.h>
#include <machine/ver.h>

#define	TICK_QUALITY_MP	10
#define	TICK_QUALITY_UP	1000

static SYSCTL_NODE(_machdep, OID_AUTO, tick, CTLFLAG_RD, 0, "tick statistics");

static int adjust_edges = 0;
SYSCTL_INT(_machdep_tick, OID_AUTO, adjust_edges, CTLFLAG_RD, &adjust_edges,
    0, "total number of times tick interrupts got more than 12.5% behind");

static int adjust_excess = 0;
SYSCTL_INT(_machdep_tick, OID_AUTO, adjust_excess, CTLFLAG_RD, &adjust_excess,
    0, "total number of ignored tick interrupts");

static int adjust_missed = 0;
SYSCTL_INT(_machdep_tick, OID_AUTO, adjust_missed, CTLFLAG_RD, &adjust_missed,
    0, "total number of missed tick interrupts");

static int adjust_ticks = 0;
SYSCTL_INT(_machdep_tick, OID_AUTO, adjust_ticks, CTLFLAG_RD, &adjust_ticks,
    0, "total number of tick interrupts with adjustment");

u_int tick_et_use_stick = 0;
SYSCTL_INT(_machdep_tick, OID_AUTO, tick_et_use_stick, CTLFLAG_RD,
    &tick_et_use_stick, 0, "tick event timer uses STICK instead of TICK");

typedef uint64_t rd_tick_t(void);
static rd_tick_t *rd_tick;
typedef void wr_tick_cmpr_t(uint64_t);
static wr_tick_cmpr_t *wr_tick_cmpr;

static struct timecounter stick_tc;
static struct eventtimer tick_et;
static struct timecounter tick_tc;

#ifdef SMP
static timecounter_get_t stick_get_timecount_mp;
#endif
static timecounter_get_t stick_get_timecount_up;
static rd_tick_t stick_rd;
static wr_tick_cmpr_t stick_wr_cmpr;
static int tick_et_start(struct eventtimer *et, sbintime_t first,
    sbintime_t period);
static int tick_et_stop(struct eventtimer *et);
#ifdef SMP
static timecounter_get_t tick_get_timecount_mp;
#endif
static timecounter_get_t tick_get_timecount_up;
static void tick_intr(struct trapframe *tf);
static inline void tick_process(struct trapframe *tf);
static rd_tick_t tick_rd;
static wr_tick_cmpr_t tick_wr_cmpr;
static wr_tick_cmpr_t tick_wr_cmpr_bbwar;
static uint64_t tick_cputicks(void);

static uint64_t
stick_rd(void)
{

	return (rdstick());
}

static void
stick_wr_cmpr(uint64_t tick)
{

	wrstickcmpr(tick, 0);
}

static uint64_t
tick_rd(void)
{

	return (rd(tick));
}

static void
tick_wr_cmpr(uint64_t tick_cmpr)
{

	wrtickcmpr(tick_cmpr, 0);
}

static void
tick_wr_cmpr_bbwar(uint64_t tick_cmpr)
{

	wrtickcmpr_bbwar(tick_cmpr, 0);
}

static uint64_t
tick_cputicks(void)
{

	return (rd(tick));
}

void
cpu_initclocks(void)
{
	uint32_t clock, sclock;

	clock = PCPU_GET(clock);
	sclock = 0;
	if (PCPU_GET(impl) == CPU_IMPL_SPARC64V ||
	    PCPU_GET(impl) >= CPU_IMPL_ULTRASPARCIII) {
		if (OF_getprop(OF_peer(0), "stick-frequency", &sclock,
		    sizeof(sclock)) == -1) {
			panic("%s: could not determine STICK frequency",
			    __func__);
		}
	}
	/*
	 * Given that the STICK timers typically are driven at rather low
	 * frequencies they shouldn't be used except when really necessary.
	 */
	if (tick_et_use_stick != 0) {
		rd_tick = stick_rd;
		wr_tick_cmpr = stick_wr_cmpr;
		/*
		 * We don't provide a CPU ticker as long as the frequency
		 * supplied isn't actually used per-CPU.
		 */
	} else {
		rd_tick = tick_rd;
		if (PCPU_GET(impl) >= CPU_IMPL_ULTRASPARCI &&
		    PCPU_GET(impl) < CPU_IMPL_ULTRASPARCIII)
			wr_tick_cmpr = tick_wr_cmpr_bbwar;
		else
			wr_tick_cmpr = tick_wr_cmpr;
		set_cputicker(tick_cputicks, clock, 0);
	}
	intr_setup(PIL_TICK, tick_intr, -1, NULL, NULL);

	/*
	 * Initialize the (S)TICK-based timecounter(s).
	 * Note that we (try to) sync the (S)TICK timers of APs with the BSP
	 * during their startup but not afterwards.  The resulting drift can
	 * cause problems when the time is calculated based on (S)TICK values
	 * read on different CPUs.  Thus we always read the register on the
	 * BSP (if necessary via an IPI as sched_bind(9) isn't available in
	 * all circumstances) and use a low quality for the otherwise high
	 * quality (S)TICK timers in the MP case.
	 */
	tick_tc.tc_get_timecount = tick_get_timecount_up;
	tick_tc.tc_counter_mask = ~0u;
	tick_tc.tc_frequency = clock;
	tick_tc.tc_name = "tick";
	tick_tc.tc_quality = TICK_QUALITY_UP;
#ifdef SMP
	if (cpu_mp_probe()) {
		tick_tc.tc_get_timecount = tick_get_timecount_mp;
		tick_tc.tc_quality = TICK_QUALITY_MP;
	}
#endif
	tc_init(&tick_tc);
	if (sclock != 0) {
		stick_tc.tc_get_timecount = stick_get_timecount_up;
		stick_tc.tc_counter_mask = ~0u;
		stick_tc.tc_frequency = sclock;
		stick_tc.tc_name = "stick";
		stick_tc.tc_quality = TICK_QUALITY_UP;
#ifdef SMP
		if (cpu_mp_probe()) {
			stick_tc.tc_get_timecount = stick_get_timecount_mp;
			stick_tc.tc_quality = TICK_QUALITY_MP;
		}
#endif
		tc_init(&stick_tc);
	}
	tick_et.et_name = tick_et_use_stick ? "stick" : "tick";
	tick_et.et_flags = ET_FLAGS_PERIODIC | ET_FLAGS_ONESHOT |
	    ET_FLAGS_PERCPU;
	tick_et.et_quality = 1000;
	tick_et.et_frequency = tick_et_use_stick ? sclock : clock;
	tick_et.et_min_period = 0x00010000LLU; /* To be safe. */
	tick_et.et_max_period = (0xfffffffeLLU << 32) / tick_et.et_frequency;
	tick_et.et_start = tick_et_start;
	tick_et.et_stop = tick_et_stop;
	tick_et.et_priv = NULL;
	et_register(&tick_et);

	cpu_initclocks_bsp();
}

static inline void
tick_process(struct trapframe *tf)
{
	struct trapframe *oldframe;
	struct thread *td;

	td = curthread;
	td->td_intr_nesting_level++;
	critical_enter();
	if (tick_et.et_active) {
		oldframe = td->td_intr_frame;
		td->td_intr_frame = tf;
		tick_et.et_event_cb(&tick_et, tick_et.et_arg);
		td->td_intr_frame = oldframe;
	}
	td->td_intr_nesting_level--;
	critical_exit();
}

static void
tick_intr(struct trapframe *tf)
{
	u_long adj, ref, tick, tick_increment;
	long delta;
	register_t s;
	int count;

	tick_increment = PCPU_GET(tickincrement);
	if (tick_increment != 0) {
		/*
		 * NB: the sequence of reading the (S)TICK register,
		 * calculating the value of the next tick and writing it to
		 * the (S)TICK_COMPARE register must not be interrupted, not
		 * even by an IPI, otherwise a value that is in the past could
		 * be written in the worst case and thus causing the periodic
		 * timer to stop.
		 */
		s = intr_disable();
		adj = PCPU_GET(tickadj);
		tick = rd_tick();
		wr_tick_cmpr(tick + tick_increment - adj);
		intr_restore(s);
		ref = PCPU_GET(tickref);
		delta = tick - ref;
		count = 0;
		while (delta >= tick_increment) {
			tick_process(tf);
			delta -= tick_increment;
			ref += tick_increment;
			if (adj != 0)
				adjust_ticks++;
			count++;
		}
		if (count > 0) {
			adjust_missed += count - 1;
			if (delta > (tick_increment >> 3)) {
				if (adj == 0)
					adjust_edges++;
				adj = tick_increment >> 4;
			} else
				adj = 0;
		} else {
			adj = 0;
			adjust_excess++;
		}
		PCPU_SET(tickref, ref);
		PCPU_SET(tickadj, adj);
	} else
		tick_process(tf);
}

static u_int
stick_get_timecount_up(struct timecounter *tc)
{

	return ((u_int)rdstick());
}

static u_int
tick_get_timecount_up(struct timecounter *tc)
{

	return ((u_int)rd(tick));
}

#ifdef SMP
static u_int
stick_get_timecount_mp(struct timecounter *tc)
{
	static u_long stick;

	sched_pin();
	if (curcpu == 0)
		stick = rdstick();
	else
		ipi_wait(ipi_rd(0, tl_ipi_stick_rd, &stick));
	sched_unpin();
	return (stick);
}

static u_int
tick_get_timecount_mp(struct timecounter *tc)
{
	static u_long tick;

	sched_pin();
	if (curcpu == 0)
		tick = rd(tick);
	else
		ipi_wait(ipi_rd(0, tl_ipi_tick_rd, &tick));
	sched_unpin();
	return (tick);
}
#endif

static int
tick_et_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{
	u_long base, div, fdiv;
	register_t s;

	if (period != 0)
		div = (tick_et.et_frequency * period) >> 32;
	else
		div = 0;
	if (first != 0)
		fdiv = (tick_et.et_frequency * first) >> 32;
	else
		fdiv = div;
	PCPU_SET(tickincrement, div);

	/*
	 * Try to make the (S)TICK interrupts as synchronously as possible
	 * on all CPUs to avoid inaccuracies for migrating processes.  Leave
	 * out one tick to make sure that it is not missed.
	 */
	s = intr_disable();
	base = rd_tick();
	if (div != 0) {
		PCPU_SET(tickadj, 0);
		base = roundup(base, div);
	}
	PCPU_SET(tickref, base);
	wr_tick_cmpr(base + fdiv);
	intr_restore(s);
	return (0);
}

static int
tick_et_stop(struct eventtimer *et)
{

	PCPU_SET(tickincrement, 0);
	tick_stop(PCPU_GET(impl));
	return (0);
}

void
tick_clear(u_int cpu_impl)
{

	if (cpu_impl == CPU_IMPL_SPARC64V ||
	    cpu_impl >= CPU_IMPL_ULTRASPARCIII)
		wrstick(0, 0);
	wrpr(tick, 0, 0);
}

void
tick_stop(u_int cpu_impl)
{

	if (cpu_impl == CPU_IMPL_SPARC64V ||
	    cpu_impl >= CPU_IMPL_ULTRASPARCIII)
		wrstickcmpr(1L << 63, 0);
	wrtickcmpr(1L << 63, 0);
}
