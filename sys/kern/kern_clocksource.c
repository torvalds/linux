/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2013 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Common routines to manage event timers hardware.
 */

#include "opt_device_polling.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/kdb.h>
#include <sys/ktr.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/timeet.h>
#include <sys/timetc.h>

#include <machine/atomic.h>
#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/smp.h>

int			cpu_disable_c2_sleep = 0; /* Timer dies in C2. */
int			cpu_disable_c3_sleep = 0; /* Timer dies in C3. */

static void		setuptimer(void);
static void		loadtimer(sbintime_t now, int first);
static int		doconfigtimer(void);
static void		configtimer(int start);
static int		round_freq(struct eventtimer *et, int freq);

static sbintime_t	getnextcpuevent(int idle);
static sbintime_t	getnextevent(void);
static int		handleevents(sbintime_t now, int fake);

static struct mtx	et_hw_mtx;

#define	ET_HW_LOCK(state)						\
	{								\
		if (timer->et_flags & ET_FLAGS_PERCPU)			\
			mtx_lock_spin(&(state)->et_hw_mtx);		\
		else							\
			mtx_lock_spin(&et_hw_mtx);			\
	}

#define	ET_HW_UNLOCK(state)						\
	{								\
		if (timer->et_flags & ET_FLAGS_PERCPU)			\
			mtx_unlock_spin(&(state)->et_hw_mtx);		\
		else							\
			mtx_unlock_spin(&et_hw_mtx);			\
	}

static struct eventtimer *timer = NULL;
static sbintime_t	timerperiod;	/* Timer period for periodic mode. */
static sbintime_t	statperiod;	/* statclock() events period. */
static sbintime_t	profperiod;	/* profclock() events period. */
static sbintime_t	nexttick;	/* Next global timer tick time. */
static u_int		busy = 1;	/* Reconfiguration is in progress. */
static int		profiling;	/* Profiling events enabled. */

static char		timername[32];	/* Wanted timer. */
TUNABLE_STR("kern.eventtimer.timer", timername, sizeof(timername));

static int		singlemul;	/* Multiplier for periodic mode. */
SYSCTL_INT(_kern_eventtimer, OID_AUTO, singlemul, CTLFLAG_RWTUN, &singlemul,
    0, "Multiplier for periodic mode");

static u_int		idletick;	/* Run periodic events when idle. */
SYSCTL_UINT(_kern_eventtimer, OID_AUTO, idletick, CTLFLAG_RWTUN, &idletick,
    0, "Run periodic events when idle");

static int		periodic;	/* Periodic or one-shot mode. */
static int		want_periodic;	/* What mode to prefer. */
TUNABLE_INT("kern.eventtimer.periodic", &want_periodic);

struct pcpu_state {
	struct mtx	et_hw_mtx;	/* Per-CPU timer mutex. */
	u_int		action;		/* Reconfiguration requests. */
	u_int		handle;		/* Immediate handle resuests. */
	sbintime_t	now;		/* Last tick time. */
	sbintime_t	nextevent;	/* Next scheduled event on this CPU. */
	sbintime_t	nexttick;	/* Next timer tick time. */
	sbintime_t	nexthard;	/* Next hardclock() event. */
	sbintime_t	nextstat;	/* Next statclock() event. */
	sbintime_t	nextprof;	/* Next profclock() event. */
	sbintime_t	nextcall;	/* Next callout event. */
	sbintime_t	nextcallopt;	/* Next optional callout event. */
	int		ipi;		/* This CPU needs IPI. */
	int		idle;		/* This CPU is in idle mode. */
};

DPCPU_DEFINE_STATIC(struct pcpu_state, timerstate);
DPCPU_DEFINE(sbintime_t, hardclocktime);

/*
 * Timer broadcast IPI handler.
 */
int
hardclockintr(void)
{
	sbintime_t now;
	struct pcpu_state *state;
	int done;

	if (doconfigtimer() || busy)
		return (FILTER_HANDLED);
	state = DPCPU_PTR(timerstate);
	now = state->now;
	CTR3(KTR_SPARE2, "ipi  at %d:    now  %d.%08x",
	    curcpu, (int)(now >> 32), (u_int)(now & 0xffffffff));
	done = handleevents(now, 0);
	return (done ? FILTER_HANDLED : FILTER_STRAY);
}

/*
 * Handle all events for specified time on this CPU
 */
static int
handleevents(sbintime_t now, int fake)
{
	sbintime_t t, *hct;
	struct trapframe *frame;
	struct pcpu_state *state;
	int usermode;
	int done, runs;

	CTR3(KTR_SPARE2, "handle at %d:  now  %d.%08x",
	    curcpu, (int)(now >> 32), (u_int)(now & 0xffffffff));
	done = 0;
	if (fake) {
		frame = NULL;
		usermode = 0;
	} else {
		frame = curthread->td_intr_frame;
		usermode = TRAPF_USERMODE(frame);
	}

	state = DPCPU_PTR(timerstate);

	runs = 0;
	while (now >= state->nexthard) {
		state->nexthard += tick_sbt;
		runs++;
	}
	if (runs) {
		hct = DPCPU_PTR(hardclocktime);
		*hct = state->nexthard - tick_sbt;
		if (fake < 2) {
			hardclock(runs, usermode);
			done = 1;
		}
	}
	runs = 0;
	while (now >= state->nextstat) {
		state->nextstat += statperiod;
		runs++;
	}
	if (runs && fake < 2) {
		statclock(runs, usermode);
		done = 1;
	}
	if (profiling) {
		runs = 0;
		while (now >= state->nextprof) {
			state->nextprof += profperiod;
			runs++;
		}
		if (runs && !fake) {
			profclock(runs, usermode, TRAPF_PC(frame));
			done = 1;
		}
	} else
		state->nextprof = state->nextstat;
	if (now >= state->nextcallopt || now >= state->nextcall) {
		state->nextcall = state->nextcallopt = SBT_MAX;
		callout_process(now);
	}

	t = getnextcpuevent(0);
	ET_HW_LOCK(state);
	if (!busy) {
		state->idle = 0;
		state->nextevent = t;
		loadtimer(now, (fake == 2) &&
		    (timer->et_flags & ET_FLAGS_PERCPU));
	}
	ET_HW_UNLOCK(state);
	return (done);
}

/*
 * Schedule binuptime of the next event on current CPU.
 */
static sbintime_t
getnextcpuevent(int idle)
{
	sbintime_t event;
	struct pcpu_state *state;
	u_int hardfreq;

	state = DPCPU_PTR(timerstate);
	/* Handle hardclock() events, skipping some if CPU is idle. */
	event = state->nexthard;
	if (idle) {
		hardfreq = (u_int)hz / 2;
		if (tc_min_ticktock_freq > 2
#ifdef SMP
		    && curcpu == CPU_FIRST()
#endif
		    )
			hardfreq = hz / tc_min_ticktock_freq;
		if (hardfreq > 1)
			event += tick_sbt * (hardfreq - 1);
	}
	/* Handle callout events. */
	if (event > state->nextcall)
		event = state->nextcall;
	if (!idle) { /* If CPU is active - handle other types of events. */
		if (event > state->nextstat)
			event = state->nextstat;
		if (profiling && event > state->nextprof)
			event = state->nextprof;
	}
	return (event);
}

/*
 * Schedule binuptime of the next event on all CPUs.
 */
static sbintime_t
getnextevent(void)
{
	struct pcpu_state *state;
	sbintime_t event;
#ifdef SMP
	int	cpu;
#endif
#ifdef KTR
	int	c;

	c = -1;
#endif
	state = DPCPU_PTR(timerstate);
	event = state->nextevent;
#ifdef SMP
	if ((timer->et_flags & ET_FLAGS_PERCPU) == 0) {
		CPU_FOREACH(cpu) {
			state = DPCPU_ID_PTR(cpu, timerstate);
			if (event > state->nextevent) {
				event = state->nextevent;
#ifdef KTR
				c = cpu;
#endif
			}
		}
	}
#endif
	CTR4(KTR_SPARE2, "next at %d:    next %d.%08x by %d",
	    curcpu, (int)(event >> 32), (u_int)(event & 0xffffffff), c);
	return (event);
}

/* Hardware timer callback function. */
static void
timercb(struct eventtimer *et, void *arg)
{
	sbintime_t now;
	sbintime_t *next;
	struct pcpu_state *state;
#ifdef SMP
	int cpu, bcast;
#endif

	/* Do not touch anything if somebody reconfiguring timers. */
	if (busy)
		return;
	/* Update present and next tick times. */
	state = DPCPU_PTR(timerstate);
	if (et->et_flags & ET_FLAGS_PERCPU) {
		next = &state->nexttick;
	} else
		next = &nexttick;
	now = sbinuptime();
	if (periodic)
		*next = now + timerperiod;
	else
		*next = -1;	/* Next tick is not scheduled yet. */
	state->now = now;
	CTR3(KTR_SPARE2, "intr at %d:    now  %d.%08x",
	    curcpu, (int)(now >> 32), (u_int)(now & 0xffffffff));

#ifdef SMP
#ifdef EARLY_AP_STARTUP
	MPASS(mp_ncpus == 1 || smp_started);
#endif
	/* Prepare broadcasting to other CPUs for non-per-CPU timers. */
	bcast = 0;
#ifdef EARLY_AP_STARTUP
	if ((et->et_flags & ET_FLAGS_PERCPU) == 0) {
#else
	if ((et->et_flags & ET_FLAGS_PERCPU) == 0 && smp_started) {
#endif
		CPU_FOREACH(cpu) {
			state = DPCPU_ID_PTR(cpu, timerstate);
			ET_HW_LOCK(state);
			state->now = now;
			if (now >= state->nextevent) {
				state->nextevent += SBT_1S;
				if (curcpu != cpu) {
					state->ipi = 1;
					bcast = 1;
				}
			}
			ET_HW_UNLOCK(state);
		}
	}
#endif

	/* Handle events for this time on this CPU. */
	handleevents(now, 0);

#ifdef SMP
	/* Broadcast interrupt to other CPUs for non-per-CPU timers. */
	if (bcast) {
		CPU_FOREACH(cpu) {
			if (curcpu == cpu)
				continue;
			state = DPCPU_ID_PTR(cpu, timerstate);
			if (state->ipi) {
				state->ipi = 0;
				ipi_cpu(cpu, IPI_HARDCLOCK);
			}
		}
	}
#endif
}

/*
 * Load new value into hardware timer.
 */
static void
loadtimer(sbintime_t now, int start)
{
	struct pcpu_state *state;
	sbintime_t new;
	sbintime_t *next;
	uint64_t tmp;
	int eq;

	if (timer->et_flags & ET_FLAGS_PERCPU) {
		state = DPCPU_PTR(timerstate);
		next = &state->nexttick;
	} else
		next = &nexttick;
	if (periodic) {
		if (start) {
			/*
			 * Try to start all periodic timers aligned
			 * to period to make events synchronous.
			 */
			tmp = now % timerperiod;
			new = timerperiod - tmp;
			if (new < tmp)		/* Left less then passed. */
				new += timerperiod;
			CTR5(KTR_SPARE2, "load p at %d:   now %d.%08x first in %d.%08x",
			    curcpu, (int)(now >> 32), (u_int)(now & 0xffffffff),
			    (int)(new >> 32), (u_int)(new & 0xffffffff));
			*next = new + now;
			et_start(timer, new, timerperiod);
		}
	} else {
		new = getnextevent();
		eq = (new == *next);
		CTR4(KTR_SPARE2, "load at %d:    next %d.%08x eq %d",
		    curcpu, (int)(new >> 32), (u_int)(new & 0xffffffff), eq);
		if (!eq) {
			*next = new;
			et_start(timer, new - now, 0);
		}
	}
}

/*
 * Prepare event timer parameters after configuration changes.
 */
static void
setuptimer(void)
{
	int freq;

	if (periodic && (timer->et_flags & ET_FLAGS_PERIODIC) == 0)
		periodic = 0;
	else if (!periodic && (timer->et_flags & ET_FLAGS_ONESHOT) == 0)
		periodic = 1;
	singlemul = MIN(MAX(singlemul, 1), 20);
	freq = hz * singlemul;
	while (freq < (profiling ? profhz : stathz))
		freq += hz;
	freq = round_freq(timer, freq);
	timerperiod = SBT_1S / freq;
}

/*
 * Reconfigure specified per-CPU timer on other CPU. Called from IPI handler.
 */
static int
doconfigtimer(void)
{
	sbintime_t now;
	struct pcpu_state *state;

	state = DPCPU_PTR(timerstate);
	switch (atomic_load_acq_int(&state->action)) {
	case 1:
		now = sbinuptime();
		ET_HW_LOCK(state);
		loadtimer(now, 1);
		ET_HW_UNLOCK(state);
		state->handle = 0;
		atomic_store_rel_int(&state->action, 0);
		return (1);
	case 2:
		ET_HW_LOCK(state);
		et_stop(timer);
		ET_HW_UNLOCK(state);
		state->handle = 0;
		atomic_store_rel_int(&state->action, 0);
		return (1);
	}
	if (atomic_readandclear_int(&state->handle) && !busy) {
		now = sbinuptime();
		handleevents(now, 0);
		return (1);
	}
	return (0);
}

/*
 * Reconfigure specified timer.
 * For per-CPU timers use IPI to make other CPUs to reconfigure.
 */
static void
configtimer(int start)
{
	sbintime_t now, next;
	struct pcpu_state *state;
	int cpu;

	if (start) {
		setuptimer();
		now = sbinuptime();
	} else
		now = 0;
	critical_enter();
	ET_HW_LOCK(DPCPU_PTR(timerstate));
	if (start) {
		/* Initialize time machine parameters. */
		next = now + timerperiod;
		if (periodic)
			nexttick = next;
		else
			nexttick = -1;
#ifdef EARLY_AP_STARTUP
		MPASS(mp_ncpus == 1 || smp_started);
#endif
		CPU_FOREACH(cpu) {
			state = DPCPU_ID_PTR(cpu, timerstate);
			state->now = now;
#ifndef EARLY_AP_STARTUP
			if (!smp_started && cpu != CPU_FIRST())
				state->nextevent = SBT_MAX;
			else
#endif
				state->nextevent = next;
			if (periodic)
				state->nexttick = next;
			else
				state->nexttick = -1;
			state->nexthard = next;
			state->nextstat = next;
			state->nextprof = next;
			state->nextcall = next;
			state->nextcallopt = next;
			hardclock_sync(cpu);
		}
		busy = 0;
		/* Start global timer or per-CPU timer of this CPU. */
		loadtimer(now, 1);
	} else {
		busy = 1;
		/* Stop global timer or per-CPU timer of this CPU. */
		et_stop(timer);
	}
	ET_HW_UNLOCK(DPCPU_PTR(timerstate));
#ifdef SMP
#ifdef EARLY_AP_STARTUP
	/* If timer is global we are done. */
	if ((timer->et_flags & ET_FLAGS_PERCPU) == 0) {
#else
	/* If timer is global or there is no other CPUs yet - we are done. */
	if ((timer->et_flags & ET_FLAGS_PERCPU) == 0 || !smp_started) {
#endif
		critical_exit();
		return;
	}
	/* Set reconfigure flags for other CPUs. */
	CPU_FOREACH(cpu) {
		state = DPCPU_ID_PTR(cpu, timerstate);
		atomic_store_rel_int(&state->action,
		    (cpu == curcpu) ? 0 : ( start ? 1 : 2));
	}
	/* Broadcast reconfigure IPI. */
	ipi_all_but_self(IPI_HARDCLOCK);
	/* Wait for reconfiguration completed. */
restart:
	cpu_spinwait();
	CPU_FOREACH(cpu) {
		if (cpu == curcpu)
			continue;
		state = DPCPU_ID_PTR(cpu, timerstate);
		if (atomic_load_acq_int(&state->action))
			goto restart;
	}
#endif
	critical_exit();
}

/*
 * Calculate nearest frequency supported by hardware timer.
 */
static int
round_freq(struct eventtimer *et, int freq)
{
	uint64_t div;

	if (et->et_frequency != 0) {
		div = lmax((et->et_frequency + freq / 2) / freq, 1);
		if (et->et_flags & ET_FLAGS_POW2DIV)
			div = 1 << (flsl(div + div / 2) - 1);
		freq = (et->et_frequency + div / 2) / div;
	}
	if (et->et_min_period > SBT_1S)
		panic("Event timer \"%s\" doesn't support sub-second periods!",
		    et->et_name);
	else if (et->et_min_period != 0)
		freq = min(freq, SBT2FREQ(et->et_min_period));
	if (et->et_max_period < SBT_1S && et->et_max_period != 0)
		freq = max(freq, SBT2FREQ(et->et_max_period));
	return (freq);
}

/*
 * Configure and start event timers (BSP part).
 */
void
cpu_initclocks_bsp(void)
{
	struct pcpu_state *state;
	int base, div, cpu;

	mtx_init(&et_hw_mtx, "et_hw_mtx", NULL, MTX_SPIN);
	CPU_FOREACH(cpu) {
		state = DPCPU_ID_PTR(cpu, timerstate);
		mtx_init(&state->et_hw_mtx, "et_hw_mtx", NULL, MTX_SPIN);
		state->nextcall = SBT_MAX;
		state->nextcallopt = SBT_MAX;
	}
	periodic = want_periodic;
	/* Grab requested timer or the best of present. */
	if (timername[0])
		timer = et_find(timername, 0, 0);
	if (timer == NULL && periodic) {
		timer = et_find(NULL,
		    ET_FLAGS_PERIODIC, ET_FLAGS_PERIODIC);
	}
	if (timer == NULL) {
		timer = et_find(NULL,
		    ET_FLAGS_ONESHOT, ET_FLAGS_ONESHOT);
	}
	if (timer == NULL && !periodic) {
		timer = et_find(NULL,
		    ET_FLAGS_PERIODIC, ET_FLAGS_PERIODIC);
	}
	if (timer == NULL)
		panic("No usable event timer found!");
	et_init(timer, timercb, NULL, NULL);

	/* Adapt to timer capabilities. */
	if (periodic && (timer->et_flags & ET_FLAGS_PERIODIC) == 0)
		periodic = 0;
	else if (!periodic && (timer->et_flags & ET_FLAGS_ONESHOT) == 0)
		periodic = 1;
	if (timer->et_flags & ET_FLAGS_C3STOP)
		cpu_disable_c3_sleep++;

	/*
	 * We honor the requested 'hz' value.
	 * We want to run stathz in the neighborhood of 128hz.
	 * We would like profhz to run as often as possible.
	 */
	if (singlemul <= 0 || singlemul > 20) {
		if (hz >= 1500 || (hz % 128) == 0)
			singlemul = 1;
		else if (hz >= 750)
			singlemul = 2;
		else
			singlemul = 4;
	}
	if (periodic) {
		base = round_freq(timer, hz * singlemul);
		singlemul = max((base + hz / 2) / hz, 1);
		hz = (base + singlemul / 2) / singlemul;
		if (base <= 128)
			stathz = base;
		else {
			div = base / 128;
			if (div >= singlemul && (div % singlemul) == 0)
				div++;
			stathz = base / div;
		}
		profhz = stathz;
		while ((profhz + stathz) <= 128 * 64)
			profhz += stathz;
		profhz = round_freq(timer, profhz);
	} else {
		hz = round_freq(timer, hz);
		stathz = round_freq(timer, 127);
		profhz = round_freq(timer, stathz * 64);
	}
	tick = 1000000 / hz;
	tick_sbt = SBT_1S / hz;
	tick_bt = sbttobt(tick_sbt);
	statperiod = SBT_1S / stathz;
	profperiod = SBT_1S / profhz;
	ET_LOCK();
	configtimer(1);
	ET_UNLOCK();
}

/*
 * Start per-CPU event timers on APs.
 */
void
cpu_initclocks_ap(void)
{
	sbintime_t now;
	struct pcpu_state *state;
	struct thread *td;

	state = DPCPU_PTR(timerstate);
	now = sbinuptime();
	ET_HW_LOCK(state);
	state->now = now;
	hardclock_sync(curcpu);
	spinlock_enter();
	ET_HW_UNLOCK(state);
	td = curthread;
	td->td_intr_nesting_level++;
	handleevents(state->now, 2);
	td->td_intr_nesting_level--;
	spinlock_exit();
}

void
suspendclock(void)
{
	ET_LOCK();
	configtimer(0);
	ET_UNLOCK();
}

void
resumeclock(void)
{
	ET_LOCK();
	configtimer(1);
	ET_UNLOCK();
}

/*
 * Switch to profiling clock rates.
 */
void
cpu_startprofclock(void)
{

	ET_LOCK();
	if (profiling == 0) {
		if (periodic) {
			configtimer(0);
			profiling = 1;
			configtimer(1);
		} else
			profiling = 1;
	} else
		profiling++;
	ET_UNLOCK();
}

/*
 * Switch to regular clock rates.
 */
void
cpu_stopprofclock(void)
{

	ET_LOCK();
	if (profiling == 1) {
		if (periodic) {
			configtimer(0);
			profiling = 0;
			configtimer(1);
		} else
		profiling = 0;
	} else
		profiling--;
	ET_UNLOCK();
}

/*
 * Switch to idle mode (all ticks handled).
 */
sbintime_t
cpu_idleclock(void)
{
	sbintime_t now, t;
	struct pcpu_state *state;

	if (idletick || busy ||
	    (periodic && (timer->et_flags & ET_FLAGS_PERCPU))
#ifdef DEVICE_POLLING
	    || curcpu == CPU_FIRST()
#endif
	    )
		return (-1);
	state = DPCPU_PTR(timerstate);
	if (periodic)
		now = state->now;
	else
		now = sbinuptime();
	CTR3(KTR_SPARE2, "idle at %d:    now  %d.%08x",
	    curcpu, (int)(now >> 32), (u_int)(now & 0xffffffff));
	t = getnextcpuevent(1);
	ET_HW_LOCK(state);
	state->idle = 1;
	state->nextevent = t;
	if (!periodic)
		loadtimer(now, 0);
	ET_HW_UNLOCK(state);
	return (MAX(t - now, 0));
}

/*
 * Switch to active mode (skip empty ticks).
 */
void
cpu_activeclock(void)
{
	sbintime_t now;
	struct pcpu_state *state;
	struct thread *td;

	state = DPCPU_PTR(timerstate);
	if (state->idle == 0 || busy)
		return;
	if (periodic)
		now = state->now;
	else
		now = sbinuptime();
	CTR3(KTR_SPARE2, "active at %d:  now  %d.%08x",
	    curcpu, (int)(now >> 32), (u_int)(now & 0xffffffff));
	spinlock_enter();
	td = curthread;
	td->td_intr_nesting_level++;
	handleevents(now, 1);
	td->td_intr_nesting_level--;
	spinlock_exit();
}

/*
 * Change the frequency of the given timer.  This changes et->et_frequency and
 * if et is the active timer it reconfigures the timer on all CPUs.  This is
 * intended to be a private interface for the use of et_change_frequency() only.
 */
void
cpu_et_frequency(struct eventtimer *et, uint64_t newfreq)
{

	ET_LOCK();
	if (et == timer) {
		configtimer(0);
		et->et_frequency = newfreq;
		configtimer(1);
	} else
		et->et_frequency = newfreq;
	ET_UNLOCK();
}

void
cpu_new_callout(int cpu, sbintime_t bt, sbintime_t bt_opt)
{
	struct pcpu_state *state;

	/* Do not touch anything if somebody reconfiguring timers. */
	if (busy)
		return;
	CTR6(KTR_SPARE2, "new co at %d:    on %d at %d.%08x - %d.%08x",
	    curcpu, cpu, (int)(bt_opt >> 32), (u_int)(bt_opt & 0xffffffff),
	    (int)(bt >> 32), (u_int)(bt & 0xffffffff));

	KASSERT(!CPU_ABSENT(cpu), ("Absent CPU %d", cpu));
	state = DPCPU_ID_PTR(cpu, timerstate);
	ET_HW_LOCK(state);

	/*
	 * If there is callout time already set earlier -- do nothing.
	 * This check may appear redundant because we check already in
	 * callout_process() but this double check guarantees we're safe
	 * with respect to race conditions between interrupts execution
	 * and scheduling.
	 */
	state->nextcallopt = bt_opt;
	if (bt >= state->nextcall)
		goto done;
	state->nextcall = bt;
	/* If there is some other event set earlier -- do nothing. */
	if (bt >= state->nextevent)
		goto done;
	state->nextevent = bt;
	/* If timer is periodic -- there is nothing to reprogram. */
	if (periodic)
		goto done;
	/* If timer is global or of the current CPU -- reprogram it. */
	if ((timer->et_flags & ET_FLAGS_PERCPU) == 0 || cpu == curcpu) {
		loadtimer(sbinuptime(), 0);
done:
		ET_HW_UNLOCK(state);
		return;
	}
	/* Otherwise make other CPU to reprogram it. */
	state->handle = 1;
	ET_HW_UNLOCK(state);
#ifdef SMP
	ipi_cpu(cpu, IPI_HARDCLOCK);
#endif
}

/*
 * Report or change the active event timers hardware.
 */
static int
sysctl_kern_eventtimer_timer(SYSCTL_HANDLER_ARGS)
{
	char buf[32];
	struct eventtimer *et;
	int error;

	ET_LOCK();
	et = timer;
	snprintf(buf, sizeof(buf), "%s", et->et_name);
	ET_UNLOCK();
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	ET_LOCK();
	et = timer;
	if (error != 0 || req->newptr == NULL ||
	    strcasecmp(buf, et->et_name) == 0) {
		ET_UNLOCK();
		return (error);
	}
	et = et_find(buf, 0, 0);
	if (et == NULL) {
		ET_UNLOCK();
		return (ENOENT);
	}
	configtimer(0);
	et_free(timer);
	if (et->et_flags & ET_FLAGS_C3STOP)
		cpu_disable_c3_sleep++;
	if (timer->et_flags & ET_FLAGS_C3STOP)
		cpu_disable_c3_sleep--;
	periodic = want_periodic;
	timer = et;
	et_init(timer, timercb, NULL, NULL);
	configtimer(1);
	ET_UNLOCK();
	return (error);
}
SYSCTL_PROC(_kern_eventtimer, OID_AUTO, timer,
    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE,
    0, 0, sysctl_kern_eventtimer_timer, "A", "Chosen event timer");

/*
 * Report or change the active event timer periodicity.
 */
static int
sysctl_kern_eventtimer_periodic(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = periodic;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	ET_LOCK();
	configtimer(0);
	periodic = want_periodic = val;
	configtimer(1);
	ET_UNLOCK();
	return (error);
}
SYSCTL_PROC(_kern_eventtimer, OID_AUTO, periodic,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    0, 0, sysctl_kern_eventtimer_periodic, "I", "Enable event timer periodic mode");

#include "opt_ddb.h"

#ifdef DDB
#include <ddb/ddb.h>

DB_SHOW_COMMAND(clocksource, db_show_clocksource)
{
	struct pcpu_state *st;
	int c;

	CPU_FOREACH(c) {
		st = DPCPU_ID_PTR(c, timerstate);
		db_printf(
		    "CPU %2d: action %d handle %d  ipi %d idle %d\n"
		    "        now %#jx nevent %#jx (%jd)\n"
		    "        ntick %#jx (%jd) nhard %#jx (%jd)\n"
		    "        nstat %#jx (%jd) nprof %#jx (%jd)\n"
		    "        ncall %#jx (%jd) ncallopt %#jx (%jd)\n",
		    c, st->action, st->handle, st->ipi, st->idle,
		    (uintmax_t)st->now,
		    (uintmax_t)st->nextevent,
		    (uintmax_t)(st->nextevent - st->now) / tick_sbt,
		    (uintmax_t)st->nexttick,
		    (uintmax_t)(st->nexttick - st->now) / tick_sbt,
		    (uintmax_t)st->nexthard,
		    (uintmax_t)(st->nexthard - st->now) / tick_sbt,
		    (uintmax_t)st->nextstat,
		    (uintmax_t)(st->nextstat - st->now) / tick_sbt,
		    (uintmax_t)st->nextprof,
		    (uintmax_t)(st->nextprof - st->now) / tick_sbt,
		    (uintmax_t)st->nextcall,
		    (uintmax_t)(st->nextcall - st->now) / tick_sbt,
		    (uintmax_t)st->nextcallopt,
		    (uintmax_t)(st->nextcallopt - st->now) / tick_sbt);
	}
}

#endif
