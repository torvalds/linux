/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_hwpmc_hooks.h"
#include "opt_sched.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpuset.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/kthread.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/sched.h>
#include <sys/sdt.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/turnstile.h>
#include <sys/umtx.h>
#include <machine/pcb.h>
#include <machine/smp.h>

#ifdef HWPMC_HOOKS
#include <sys/pmckern.h>
#endif

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>
int				dtrace_vtime_active;
dtrace_vtime_switch_func_t	dtrace_vtime_switch_func;
#endif

/*
 * INVERSE_ESTCPU_WEIGHT is only suitable for statclock() frequencies in
 * the range 100-256 Hz (approximately).
 */
#define	ESTCPULIM(e) \
    min((e), INVERSE_ESTCPU_WEIGHT * (NICE_WEIGHT * (PRIO_MAX - PRIO_MIN) - \
    RQ_PPQ) + INVERSE_ESTCPU_WEIGHT - 1)
#ifdef SMP
#define	INVERSE_ESTCPU_WEIGHT	(8 * smp_cpus)
#else
#define	INVERSE_ESTCPU_WEIGHT	8	/* 1 / (priorities per estcpu level). */
#endif
#define	NICE_WEIGHT		1	/* Priorities per nice level. */

#define	TS_NAME_LEN (MAXCOMLEN + sizeof(" td ") + sizeof(__XSTRING(UINT_MAX)))

/*
 * The schedulable entity that runs a context.
 * This is  an extension to the thread structure and is tailored to
 * the requirements of this scheduler.
 * All fields are protected by the scheduler lock.
 */
struct td_sched {
	fixpt_t		ts_pctcpu;	/* %cpu during p_swtime. */
	u_int		ts_estcpu;	/* Estimated cpu utilization. */
	int		ts_cpticks;	/* Ticks of cpu time. */
	int		ts_slptime;	/* Seconds !RUNNING. */
	int		ts_slice;	/* Remaining part of time slice. */
	int		ts_flags;
	struct runq	*ts_runq;	/* runq the thread is currently on */
#ifdef KTR
	char		ts_name[TS_NAME_LEN];
#endif
};

/* flags kept in td_flags */
#define TDF_DIDRUN	TDF_SCHED0	/* thread actually ran. */
#define TDF_BOUND	TDF_SCHED1	/* Bound to one CPU. */
#define	TDF_SLICEEND	TDF_SCHED2	/* Thread time slice is over. */

/* flags kept in ts_flags */
#define	TSF_AFFINITY	0x0001		/* Has a non-"full" CPU set. */

#define SKE_RUNQ_PCPU(ts)						\
    ((ts)->ts_runq != 0 && (ts)->ts_runq != &runq)

#define	THREAD_CAN_SCHED(td, cpu)	\
    CPU_ISSET((cpu), &(td)->td_cpuset->cs_mask)

_Static_assert(sizeof(struct thread) + sizeof(struct td_sched) <=
    sizeof(struct thread0_storage),
    "increase struct thread0_storage.t0st_sched size");

static struct mtx sched_lock;

static int	realstathz = 127; /* stathz is sometimes 0 and run off of hz. */
static int	sched_tdcnt;	/* Total runnable threads in the system. */
static int	sched_slice = 12; /* Thread run time before rescheduling. */

static void	setup_runqs(void);
static void	schedcpu(void);
static void	schedcpu_thread(void);
static void	sched_priority(struct thread *td, u_char prio);
static void	sched_setup(void *dummy);
static void	maybe_resched(struct thread *td);
static void	updatepri(struct thread *td);
static void	resetpriority(struct thread *td);
static void	resetpriority_thread(struct thread *td);
#ifdef SMP
static int	sched_pickcpu(struct thread *td);
static int	forward_wakeup(int cpunum);
static void	kick_other_cpu(int pri, int cpuid);
#endif

static struct kproc_desc sched_kp = {
        "schedcpu",
        schedcpu_thread,
        NULL
};
SYSINIT(schedcpu, SI_SUB_LAST, SI_ORDER_FIRST, kproc_start,
    &sched_kp);
SYSINIT(sched_setup, SI_SUB_RUN_QUEUE, SI_ORDER_FIRST, sched_setup, NULL);

static void sched_initticks(void *dummy);
SYSINIT(sched_initticks, SI_SUB_CLOCKS, SI_ORDER_THIRD, sched_initticks,
    NULL);

/*
 * Global run queue.
 */
static struct runq runq;

#ifdef SMP
/*
 * Per-CPU run queues
 */
static struct runq runq_pcpu[MAXCPU];
long runq_length[MAXCPU];

static cpuset_t idle_cpus_mask;
#endif

struct pcpuidlestat {
	u_int idlecalls;
	u_int oldidlecalls;
};
DPCPU_DEFINE_STATIC(struct pcpuidlestat, idlestat);

static void
setup_runqs(void)
{
#ifdef SMP
	int i;

	for (i = 0; i < MAXCPU; ++i)
		runq_init(&runq_pcpu[i]);
#endif

	runq_init(&runq);
}

static int
sysctl_kern_quantum(SYSCTL_HANDLER_ARGS)
{
	int error, new_val, period;

	period = 1000000 / realstathz;
	new_val = period * sched_slice;
	error = sysctl_handle_int(oidp, &new_val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (new_val <= 0)
		return (EINVAL);
	sched_slice = imax(1, (new_val + period / 2) / period);
	hogticks = imax(1, (2 * hz * sched_slice + realstathz / 2) /
	    realstathz);
	return (0);
}

SYSCTL_NODE(_kern, OID_AUTO, sched, CTLFLAG_RD, 0, "Scheduler");

SYSCTL_STRING(_kern_sched, OID_AUTO, name, CTLFLAG_RD, "4BSD", 0,
    "Scheduler name");
SYSCTL_PROC(_kern_sched, OID_AUTO, quantum, CTLTYPE_INT | CTLFLAG_RW,
    NULL, 0, sysctl_kern_quantum, "I",
    "Quantum for timeshare threads in microseconds");
SYSCTL_INT(_kern_sched, OID_AUTO, slice, CTLFLAG_RW, &sched_slice, 0,
    "Quantum for timeshare threads in stathz ticks");
#ifdef SMP
/* Enable forwarding of wakeups to all other cpus */
static SYSCTL_NODE(_kern_sched, OID_AUTO, ipiwakeup, CTLFLAG_RD, NULL,
    "Kernel SMP");

static int runq_fuzz = 1;
SYSCTL_INT(_kern_sched, OID_AUTO, runq_fuzz, CTLFLAG_RW, &runq_fuzz, 0, "");

static int forward_wakeup_enabled = 1;
SYSCTL_INT(_kern_sched_ipiwakeup, OID_AUTO, enabled, CTLFLAG_RW,
	   &forward_wakeup_enabled, 0,
	   "Forwarding of wakeup to idle CPUs");

static int forward_wakeups_requested = 0;
SYSCTL_INT(_kern_sched_ipiwakeup, OID_AUTO, requested, CTLFLAG_RD,
	   &forward_wakeups_requested, 0,
	   "Requests for Forwarding of wakeup to idle CPUs");

static int forward_wakeups_delivered = 0;
SYSCTL_INT(_kern_sched_ipiwakeup, OID_AUTO, delivered, CTLFLAG_RD,
	   &forward_wakeups_delivered, 0,
	   "Completed Forwarding of wakeup to idle CPUs");

static int forward_wakeup_use_mask = 1;
SYSCTL_INT(_kern_sched_ipiwakeup, OID_AUTO, usemask, CTLFLAG_RW,
	   &forward_wakeup_use_mask, 0,
	   "Use the mask of idle cpus");

static int forward_wakeup_use_loop = 0;
SYSCTL_INT(_kern_sched_ipiwakeup, OID_AUTO, useloop, CTLFLAG_RW,
	   &forward_wakeup_use_loop, 0,
	   "Use a loop to find idle cpus");

#endif
#if 0
static int sched_followon = 0;
SYSCTL_INT(_kern_sched, OID_AUTO, followon, CTLFLAG_RW,
	   &sched_followon, 0,
	   "allow threads to share a quantum");
#endif

SDT_PROVIDER_DEFINE(sched);

SDT_PROBE_DEFINE3(sched, , , change__pri, "struct thread *", 
    "struct proc *", "uint8_t");
SDT_PROBE_DEFINE3(sched, , , dequeue, "struct thread *", 
    "struct proc *", "void *");
SDT_PROBE_DEFINE4(sched, , , enqueue, "struct thread *", 
    "struct proc *", "void *", "int");
SDT_PROBE_DEFINE4(sched, , , lend__pri, "struct thread *", 
    "struct proc *", "uint8_t", "struct thread *");
SDT_PROBE_DEFINE2(sched, , , load__change, "int", "int");
SDT_PROBE_DEFINE2(sched, , , off__cpu, "struct thread *",
    "struct proc *");
SDT_PROBE_DEFINE(sched, , , on__cpu);
SDT_PROBE_DEFINE(sched, , , remain__cpu);
SDT_PROBE_DEFINE2(sched, , , surrender, "struct thread *",
    "struct proc *");

static __inline void
sched_load_add(void)
{

	sched_tdcnt++;
	KTR_COUNTER0(KTR_SCHED, "load", "global load", sched_tdcnt);
	SDT_PROBE2(sched, , , load__change, NOCPU, sched_tdcnt);
}

static __inline void
sched_load_rem(void)
{

	sched_tdcnt--;
	KTR_COUNTER0(KTR_SCHED, "load", "global load", sched_tdcnt);
	SDT_PROBE2(sched, , , load__change, NOCPU, sched_tdcnt);
}
/*
 * Arrange to reschedule if necessary, taking the priorities and
 * schedulers into account.
 */
static void
maybe_resched(struct thread *td)
{

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	if (td->td_priority < curthread->td_priority)
		curthread->td_flags |= TDF_NEEDRESCHED;
}

/*
 * This function is called when a thread is about to be put on run queue
 * because it has been made runnable or its priority has been adjusted.  It
 * determines if the new thread should preempt the current thread.  If so,
 * it sets td_owepreempt to request a preemption.
 */
int
maybe_preempt(struct thread *td)
{
#ifdef PREEMPTION
	struct thread *ctd;
	int cpri, pri;

	/*
	 * The new thread should not preempt the current thread if any of the
	 * following conditions are true:
	 *
	 *  - The kernel is in the throes of crashing (panicstr).
	 *  - The current thread has a higher (numerically lower) or
	 *    equivalent priority.  Note that this prevents curthread from
	 *    trying to preempt to itself.
	 *  - The current thread has an inhibitor set or is in the process of
	 *    exiting.  In this case, the current thread is about to switch
	 *    out anyways, so there's no point in preempting.  If we did,
	 *    the current thread would not be properly resumed as well, so
	 *    just avoid that whole landmine.
	 *  - If the new thread's priority is not a realtime priority and
	 *    the current thread's priority is not an idle priority and
	 *    FULL_PREEMPTION is disabled.
	 *
	 * If all of these conditions are false, but the current thread is in
	 * a nested critical section, then we have to defer the preemption
	 * until we exit the critical section.  Otherwise, switch immediately
	 * to the new thread.
	 */
	ctd = curthread;
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	KASSERT((td->td_inhibitors == 0),
			("maybe_preempt: trying to run inhibited thread"));
	pri = td->td_priority;
	cpri = ctd->td_priority;
	if (panicstr != NULL || pri >= cpri /* || dumping */ ||
	    TD_IS_INHIBITED(ctd))
		return (0);
#ifndef FULL_PREEMPTION
	if (pri > PRI_MAX_ITHD && cpri < PRI_MIN_IDLE)
		return (0);
#endif

	CTR0(KTR_PROC, "maybe_preempt: scheduling preemption");
	ctd->td_owepreempt = 1;
	return (1);
#else
	return (0);
#endif
}

/*
 * Constants for digital decay and forget:
 *	90% of (ts_estcpu) usage in 5 * loadav time
 *	95% of (ts_pctcpu) usage in 60 seconds (load insensitive)
 *          Note that, as ps(1) mentions, this can let percentages
 *          total over 100% (I've seen 137.9% for 3 processes).
 *
 * Note that schedclock() updates ts_estcpu and p_cpticks asynchronously.
 *
 * We wish to decay away 90% of ts_estcpu in (5 * loadavg) seconds.
 * That is, the system wants to compute a value of decay such
 * that the following for loop:
 * 	for (i = 0; i < (5 * loadavg); i++)
 * 		ts_estcpu *= decay;
 * will compute
 * 	ts_estcpu *= 0.1;
 * for all values of loadavg:
 *
 * Mathematically this loop can be expressed by saying:
 * 	decay ** (5 * loadavg) ~= .1
 *
 * The system computes decay as:
 * 	decay = (2 * loadavg) / (2 * loadavg + 1)
 *
 * We wish to prove that the system's computation of decay
 * will always fulfill the equation:
 * 	decay ** (5 * loadavg) ~= .1
 *
 * If we compute b as:
 * 	b = 2 * loadavg
 * then
 * 	decay = b / (b + 1)
 *
 * We now need to prove two things:
 *	1) Given factor ** (5 * loadavg) ~= .1, prove factor == b/(b+1)
 *	2) Given b/(b+1) ** power ~= .1, prove power == (5 * loadavg)
 *
 * Facts:
 *         For x close to zero, exp(x) =~ 1 + x, since
 *              exp(x) = 0! + x**1/1! + x**2/2! + ... .
 *              therefore exp(-1/b) =~ 1 - (1/b) = (b-1)/b.
 *         For x close to zero, ln(1+x) =~ x, since
 *              ln(1+x) = x - x**2/2 + x**3/3 - ...     -1 < x < 1
 *              therefore ln(b/(b+1)) = ln(1 - 1/(b+1)) =~ -1/(b+1).
 *         ln(.1) =~ -2.30
 *
 * Proof of (1):
 *    Solve (factor)**(power) =~ .1 given power (5*loadav):
 *	solving for factor,
 *      ln(factor) =~ (-2.30/5*loadav), or
 *      factor =~ exp(-1/((5/2.30)*loadav)) =~ exp(-1/(2*loadav)) =
 *          exp(-1/b) =~ (b-1)/b =~ b/(b+1).                    QED
 *
 * Proof of (2):
 *    Solve (factor)**(power) =~ .1 given factor == (b/(b+1)):
 *	solving for power,
 *      power*ln(b/(b+1)) =~ -2.30, or
 *      power =~ 2.3 * (b + 1) = 4.6*loadav + 2.3 =~ 5*loadav.  QED
 *
 * Actual power values for the implemented algorithm are as follows:
 *      loadav: 1       2       3       4
 *      power:  5.68    10.32   14.94   19.55
 */

/* calculations for digital decay to forget 90% of usage in 5*loadav sec */
#define	loadfactor(loadav)	(2 * (loadav))
#define	decay_cpu(loadfac, cpu)	(((loadfac) * (cpu)) / ((loadfac) + FSCALE))

/* decay 95% of `ts_pctcpu' in 60 seconds; see CCPU_SHIFT before changing */
static fixpt_t	ccpu = 0.95122942450071400909 * FSCALE;	/* exp(-1/20) */
SYSCTL_UINT(_kern, OID_AUTO, ccpu, CTLFLAG_RD, &ccpu, 0, "");

/*
 * If `ccpu' is not equal to `exp(-1/20)' and you still want to use the
 * faster/more-accurate formula, you'll have to estimate CCPU_SHIFT below
 * and possibly adjust FSHIFT in "param.h" so that (FSHIFT >= CCPU_SHIFT).
 *
 * To estimate CCPU_SHIFT for exp(-1/20), the following formula was used:
 *	1 - exp(-1/20) ~= 0.0487 ~= 0.0488 == 1 (fixed pt, *11* bits).
 *
 * If you don't want to bother with the faster/more-accurate formula, you
 * can set CCPU_SHIFT to (FSHIFT + 1) which will use a slower/less-accurate
 * (more general) method of calculating the %age of CPU used by a process.
 */
#define	CCPU_SHIFT	11

/*
 * Recompute process priorities, every hz ticks.
 * MP-safe, called without the Giant mutex.
 */
/* ARGSUSED */
static void
schedcpu(void)
{
	fixpt_t loadfac = loadfactor(averunnable.ldavg[0]);
	struct thread *td;
	struct proc *p;
	struct td_sched *ts;
	int awake;

	sx_slock(&allproc_lock);
	FOREACH_PROC_IN_SYSTEM(p) {
		PROC_LOCK(p);
		if (p->p_state == PRS_NEW) {
			PROC_UNLOCK(p);
			continue;
		}
		FOREACH_THREAD_IN_PROC(p, td) {
			awake = 0;
			ts = td_get_sched(td);
			thread_lock(td);
			/*
			 * Increment sleep time (if sleeping).  We
			 * ignore overflow, as above.
			 */
			/*
			 * The td_sched slptimes are not touched in wakeup
			 * because the thread may not HAVE everything in
			 * memory? XXX I think this is out of date.
			 */
			if (TD_ON_RUNQ(td)) {
				awake = 1;
				td->td_flags &= ~TDF_DIDRUN;
			} else if (TD_IS_RUNNING(td)) {
				awake = 1;
				/* Do not clear TDF_DIDRUN */
			} else if (td->td_flags & TDF_DIDRUN) {
				awake = 1;
				td->td_flags &= ~TDF_DIDRUN;
			}

			/*
			 * ts_pctcpu is only for ps and ttyinfo().
			 */
			ts->ts_pctcpu = (ts->ts_pctcpu * ccpu) >> FSHIFT;
			/*
			 * If the td_sched has been idle the entire second,
			 * stop recalculating its priority until
			 * it wakes up.
			 */
			if (ts->ts_cpticks != 0) {
#if	(FSHIFT >= CCPU_SHIFT)
				ts->ts_pctcpu += (realstathz == 100)
				    ? ((fixpt_t) ts->ts_cpticks) <<
				    (FSHIFT - CCPU_SHIFT) :
				    100 * (((fixpt_t) ts->ts_cpticks)
				    << (FSHIFT - CCPU_SHIFT)) / realstathz;
#else
				ts->ts_pctcpu += ((FSCALE - ccpu) *
				    (ts->ts_cpticks *
				    FSCALE / realstathz)) >> FSHIFT;
#endif
				ts->ts_cpticks = 0;
			}
			/*
			 * If there are ANY running threads in this process,
			 * then don't count it as sleeping.
			 * XXX: this is broken.
			 */
			if (awake) {
				if (ts->ts_slptime > 1) {
					/*
					 * In an ideal world, this should not
					 * happen, because whoever woke us
					 * up from the long sleep should have
					 * unwound the slptime and reset our
					 * priority before we run at the stale
					 * priority.  Should KASSERT at some
					 * point when all the cases are fixed.
					 */
					updatepri(td);
				}
				ts->ts_slptime = 0;
			} else
				ts->ts_slptime++;
			if (ts->ts_slptime > 1) {
				thread_unlock(td);
				continue;
			}
			ts->ts_estcpu = decay_cpu(loadfac, ts->ts_estcpu);
		      	resetpriority(td);
			resetpriority_thread(td);
			thread_unlock(td);
		}
		PROC_UNLOCK(p);
	}
	sx_sunlock(&allproc_lock);
}

/*
 * Main loop for a kthread that executes schedcpu once a second.
 */
static void
schedcpu_thread(void)
{

	for (;;) {
		schedcpu();
		pause("-", hz);
	}
}

/*
 * Recalculate the priority of a process after it has slept for a while.
 * For all load averages >= 1 and max ts_estcpu of 255, sleeping for at
 * least six times the loadfactor will decay ts_estcpu to zero.
 */
static void
updatepri(struct thread *td)
{
	struct td_sched *ts;
	fixpt_t loadfac;
	unsigned int newcpu;

	ts = td_get_sched(td);
	loadfac = loadfactor(averunnable.ldavg[0]);
	if (ts->ts_slptime > 5 * loadfac)
		ts->ts_estcpu = 0;
	else {
		newcpu = ts->ts_estcpu;
		ts->ts_slptime--;	/* was incremented in schedcpu() */
		while (newcpu && --ts->ts_slptime)
			newcpu = decay_cpu(loadfac, newcpu);
		ts->ts_estcpu = newcpu;
	}
}

/*
 * Compute the priority of a process when running in user mode.
 * Arrange to reschedule if the resulting priority is better
 * than that of the current process.
 */
static void
resetpriority(struct thread *td)
{
	u_int newpriority;

	if (td->td_pri_class != PRI_TIMESHARE)
		return;
	newpriority = PUSER +
	    td_get_sched(td)->ts_estcpu / INVERSE_ESTCPU_WEIGHT +
	    NICE_WEIGHT * (td->td_proc->p_nice - PRIO_MIN);
	newpriority = min(max(newpriority, PRI_MIN_TIMESHARE),
	    PRI_MAX_TIMESHARE);
	sched_user_prio(td, newpriority);
}

/*
 * Update the thread's priority when the associated process's user
 * priority changes.
 */
static void
resetpriority_thread(struct thread *td)
{

	/* Only change threads with a time sharing user priority. */
	if (td->td_priority < PRI_MIN_TIMESHARE ||
	    td->td_priority > PRI_MAX_TIMESHARE)
		return;

	/* XXX the whole needresched thing is broken, but not silly. */
	maybe_resched(td);

	sched_prio(td, td->td_user_pri);
}

/* ARGSUSED */
static void
sched_setup(void *dummy)
{

	setup_runqs();

	/* Account for thread0. */
	sched_load_add();
}

/*
 * This routine determines time constants after stathz and hz are setup.
 */
static void
sched_initticks(void *dummy)
{

	realstathz = stathz ? stathz : hz;
	sched_slice = realstathz / 10;	/* ~100ms */
	hogticks = imax(1, (2 * hz * sched_slice + realstathz / 2) /
	    realstathz);
}

/* External interfaces start here */

/*
 * Very early in the boot some setup of scheduler-specific
 * parts of proc0 and of some scheduler resources needs to be done.
 * Called from:
 *  proc0_init()
 */
void
schedinit(void)
{

	/*
	 * Set up the scheduler specific parts of thread0.
	 */
	thread0.td_lock = &sched_lock;
	td_get_sched(&thread0)->ts_slice = sched_slice;
	mtx_init(&sched_lock, "sched lock", NULL, MTX_SPIN | MTX_RECURSE);
}

int
sched_runnable(void)
{
#ifdef SMP
	return runq_check(&runq) + runq_check(&runq_pcpu[PCPU_GET(cpuid)]);
#else
	return runq_check(&runq);
#endif
}

int
sched_rr_interval(void)
{

	/* Convert sched_slice from stathz to hz. */
	return (imax(1, (sched_slice * hz + realstathz / 2) / realstathz));
}

/*
 * We adjust the priority of the current process.  The priority of a
 * process gets worse as it accumulates CPU time.  The cpu usage
 * estimator (ts_estcpu) is increased here.  resetpriority() will
 * compute a different priority each time ts_estcpu increases by
 * INVERSE_ESTCPU_WEIGHT (until PRI_MAX_TIMESHARE is reached).  The
 * cpu usage estimator ramps up quite quickly when the process is
 * running (linearly), and decays away exponentially, at a rate which
 * is proportionally slower when the system is busy.  The basic
 * principle is that the system will 90% forget that the process used
 * a lot of CPU time in 5 * loadav seconds.  This causes the system to
 * favor processes which haven't run much recently, and to round-robin
 * among other processes.
 */
void
sched_clock(struct thread *td)
{
	struct pcpuidlestat *stat;
	struct td_sched *ts;

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	ts = td_get_sched(td);

	ts->ts_cpticks++;
	ts->ts_estcpu = ESTCPULIM(ts->ts_estcpu + 1);
	if ((ts->ts_estcpu % INVERSE_ESTCPU_WEIGHT) == 0) {
		resetpriority(td);
		resetpriority_thread(td);
	}

	/*
	 * Force a context switch if the current thread has used up a full
	 * time slice (default is 100ms).
	 */
	if (!TD_IS_IDLETHREAD(td) && --ts->ts_slice <= 0) {
		ts->ts_slice = sched_slice;
		td->td_flags |= TDF_NEEDRESCHED | TDF_SLICEEND;
	}

	stat = DPCPU_PTR(idlestat);
	stat->oldidlecalls = stat->idlecalls;
	stat->idlecalls = 0;
}

/*
 * Charge child's scheduling CPU usage to parent.
 */
void
sched_exit(struct proc *p, struct thread *td)
{

	KTR_STATE1(KTR_SCHED, "thread", sched_tdname(td), "proc exit",
	    "prio:%d", td->td_priority);

	PROC_LOCK_ASSERT(p, MA_OWNED);
	sched_exit_thread(FIRST_THREAD_IN_PROC(p), td);
}

void
sched_exit_thread(struct thread *td, struct thread *child)
{

	KTR_STATE1(KTR_SCHED, "thread", sched_tdname(child), "exit",
	    "prio:%d", child->td_priority);
	thread_lock(td);
	td_get_sched(td)->ts_estcpu = ESTCPULIM(td_get_sched(td)->ts_estcpu +
	    td_get_sched(child)->ts_estcpu);
	thread_unlock(td);
	thread_lock(child);
	if ((child->td_flags & TDF_NOLOAD) == 0)
		sched_load_rem();
	thread_unlock(child);
}

void
sched_fork(struct thread *td, struct thread *childtd)
{
	sched_fork_thread(td, childtd);
}

void
sched_fork_thread(struct thread *td, struct thread *childtd)
{
	struct td_sched *ts, *tsc;

	childtd->td_oncpu = NOCPU;
	childtd->td_lastcpu = NOCPU;
	childtd->td_lock = &sched_lock;
	childtd->td_cpuset = cpuset_ref(td->td_cpuset);
	childtd->td_domain.dr_policy = td->td_cpuset->cs_domain;
	childtd->td_priority = childtd->td_base_pri;
	ts = td_get_sched(childtd);
	bzero(ts, sizeof(*ts));
	tsc = td_get_sched(td);
	ts->ts_estcpu = tsc->ts_estcpu;
	ts->ts_flags |= (tsc->ts_flags & TSF_AFFINITY);
	ts->ts_slice = 1;
}

void
sched_nice(struct proc *p, int nice)
{
	struct thread *td;

	PROC_LOCK_ASSERT(p, MA_OWNED);
	p->p_nice = nice;
	FOREACH_THREAD_IN_PROC(p, td) {
		thread_lock(td);
		resetpriority(td);
		resetpriority_thread(td);
		thread_unlock(td);
	}
}

void
sched_class(struct thread *td, int class)
{
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	td->td_pri_class = class;
}

/*
 * Adjust the priority of a thread.
 */
static void
sched_priority(struct thread *td, u_char prio)
{


	KTR_POINT3(KTR_SCHED, "thread", sched_tdname(td), "priority change",
	    "prio:%d", td->td_priority, "new prio:%d", prio, KTR_ATTR_LINKED,
	    sched_tdname(curthread));
	SDT_PROBE3(sched, , , change__pri, td, td->td_proc, prio);
	if (td != curthread && prio > td->td_priority) {
		KTR_POINT3(KTR_SCHED, "thread", sched_tdname(curthread),
		    "lend prio", "prio:%d", td->td_priority, "new prio:%d",
		    prio, KTR_ATTR_LINKED, sched_tdname(td));
		SDT_PROBE4(sched, , , lend__pri, td, td->td_proc, prio, 
		    curthread);
	}
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	if (td->td_priority == prio)
		return;
	td->td_priority = prio;
	if (TD_ON_RUNQ(td) && td->td_rqindex != (prio / RQ_PPQ)) {
		sched_rem(td);
		sched_add(td, SRQ_BORING);
	}
}

/*
 * Update a thread's priority when it is lent another thread's
 * priority.
 */
void
sched_lend_prio(struct thread *td, u_char prio)
{

	td->td_flags |= TDF_BORROWING;
	sched_priority(td, prio);
}

/*
 * Restore a thread's priority when priority propagation is
 * over.  The prio argument is the minimum priority the thread
 * needs to have to satisfy other possible priority lending
 * requests.  If the thread's regulary priority is less
 * important than prio the thread will keep a priority boost
 * of prio.
 */
void
sched_unlend_prio(struct thread *td, u_char prio)
{
	u_char base_pri;

	if (td->td_base_pri >= PRI_MIN_TIMESHARE &&
	    td->td_base_pri <= PRI_MAX_TIMESHARE)
		base_pri = td->td_user_pri;
	else
		base_pri = td->td_base_pri;
	if (prio >= base_pri) {
		td->td_flags &= ~TDF_BORROWING;
		sched_prio(td, base_pri);
	} else
		sched_lend_prio(td, prio);
}

void
sched_prio(struct thread *td, u_char prio)
{
	u_char oldprio;

	/* First, update the base priority. */
	td->td_base_pri = prio;

	/*
	 * If the thread is borrowing another thread's priority, don't ever
	 * lower the priority.
	 */
	if (td->td_flags & TDF_BORROWING && td->td_priority < prio)
		return;

	/* Change the real priority. */
	oldprio = td->td_priority;
	sched_priority(td, prio);

	/*
	 * If the thread is on a turnstile, then let the turnstile update
	 * its state.
	 */
	if (TD_ON_LOCK(td) && oldprio != prio)
		turnstile_adjust(td, oldprio);
}

void
sched_user_prio(struct thread *td, u_char prio)
{

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	td->td_base_user_pri = prio;
	if (td->td_lend_user_pri <= prio)
		return;
	td->td_user_pri = prio;
}

void
sched_lend_user_prio(struct thread *td, u_char prio)
{

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	td->td_lend_user_pri = prio;
	td->td_user_pri = min(prio, td->td_base_user_pri);
	if (td->td_priority > td->td_user_pri)
		sched_prio(td, td->td_user_pri);
	else if (td->td_priority != td->td_user_pri)
		td->td_flags |= TDF_NEEDRESCHED;
}

void
sched_sleep(struct thread *td, int pri)
{

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	td->td_slptick = ticks;
	td_get_sched(td)->ts_slptime = 0;
	if (pri != 0 && PRI_BASE(td->td_pri_class) == PRI_TIMESHARE)
		sched_prio(td, pri);
	if (TD_IS_SUSPENDED(td) || pri >= PSOCK)
		td->td_flags |= TDF_CANSWAP;
}

void
sched_switch(struct thread *td, struct thread *newtd, int flags)
{
	struct mtx *tmtx;
	struct td_sched *ts;
	struct proc *p;
	int preempted;

	tmtx = NULL;
	ts = td_get_sched(td);
	p = td->td_proc;

	THREAD_LOCK_ASSERT(td, MA_OWNED);

	/* 
	 * Switch to the sched lock to fix things up and pick
	 * a new thread.
	 * Block the td_lock in order to avoid breaking the critical path.
	 */
	if (td->td_lock != &sched_lock) {
		mtx_lock_spin(&sched_lock);
		tmtx = thread_lock_block(td);
	}

	if ((td->td_flags & TDF_NOLOAD) == 0)
		sched_load_rem();

	td->td_lastcpu = td->td_oncpu;
	preempted = (td->td_flags & TDF_SLICEEND) == 0 &&
	    (flags & SW_PREEMPT) != 0;
	td->td_flags &= ~(TDF_NEEDRESCHED | TDF_SLICEEND);
	td->td_owepreempt = 0;
	td->td_oncpu = NOCPU;

	/*
	 * At the last moment, if this thread is still marked RUNNING,
	 * then put it back on the run queue as it has not been suspended
	 * or stopped or any thing else similar.  We never put the idle
	 * threads on the run queue, however.
	 */
	if (td->td_flags & TDF_IDLETD) {
		TD_SET_CAN_RUN(td);
#ifdef SMP
		CPU_CLR(PCPU_GET(cpuid), &idle_cpus_mask);
#endif
	} else {
		if (TD_IS_RUNNING(td)) {
			/* Put us back on the run queue. */
			sched_add(td, preempted ?
			    SRQ_OURSELF|SRQ_YIELDING|SRQ_PREEMPTED :
			    SRQ_OURSELF|SRQ_YIELDING);
		}
	}
	if (newtd) {
		/*
		 * The thread we are about to run needs to be counted
		 * as if it had been added to the run queue and selected.
		 * It came from:
		 * * A preemption
		 * * An upcall
		 * * A followon
		 */
		KASSERT((newtd->td_inhibitors == 0),
			("trying to run inhibited thread"));
		newtd->td_flags |= TDF_DIDRUN;
        	TD_SET_RUNNING(newtd);
		if ((newtd->td_flags & TDF_NOLOAD) == 0)
			sched_load_add();
	} else {
		newtd = choosethread();
		MPASS(newtd->td_lock == &sched_lock);
	}

#if (KTR_COMPILE & KTR_SCHED) != 0
	if (TD_IS_IDLETHREAD(td))
		KTR_STATE1(KTR_SCHED, "thread", sched_tdname(td), "idle",
		    "prio:%d", td->td_priority);
	else
		KTR_STATE3(KTR_SCHED, "thread", sched_tdname(td), KTDSTATE(td),
		    "prio:%d", td->td_priority, "wmesg:\"%s\"", td->td_wmesg,
		    "lockname:\"%s\"", td->td_lockname);
#endif

	if (td != newtd) {
#ifdef	HWPMC_HOOKS
		if (PMC_PROC_IS_USING_PMCS(td->td_proc))
			PMC_SWITCH_CONTEXT(td, PMC_FN_CSW_OUT);
#endif

		SDT_PROBE2(sched, , , off__cpu, newtd, newtd->td_proc);

                /* I feel sleepy */
		lock_profile_release_lock(&sched_lock.lock_object);
#ifdef KDTRACE_HOOKS
		/*
		 * If DTrace has set the active vtime enum to anything
		 * other than INACTIVE (0), then it should have set the
		 * function to call.
		 */
		if (dtrace_vtime_active)
			(*dtrace_vtime_switch_func)(newtd);
#endif

		cpu_switch(td, newtd, tmtx != NULL ? tmtx : td->td_lock);
		lock_profile_obtain_lock_success(&sched_lock.lock_object,
		    0, 0, __FILE__, __LINE__);
		/*
		 * Where am I?  What year is it?
		 * We are in the same thread that went to sleep above,
		 * but any amount of time may have passed. All our context
		 * will still be available as will local variables.
		 * PCPU values however may have changed as we may have
		 * changed CPU so don't trust cached values of them.
		 * New threads will go to fork_exit() instead of here
		 * so if you change things here you may need to change
		 * things there too.
		 *
		 * If the thread above was exiting it will never wake
		 * up again here, so either it has saved everything it
		 * needed to, or the thread_wait() or wait() will
		 * need to reap it.
		 */

		SDT_PROBE0(sched, , , on__cpu);
#ifdef	HWPMC_HOOKS
		if (PMC_PROC_IS_USING_PMCS(td->td_proc))
			PMC_SWITCH_CONTEXT(td, PMC_FN_CSW_IN);
#endif
	} else
		SDT_PROBE0(sched, , , remain__cpu);

	KTR_STATE1(KTR_SCHED, "thread", sched_tdname(td), "running",
	    "prio:%d", td->td_priority);

#ifdef SMP
	if (td->td_flags & TDF_IDLETD)
		CPU_SET(PCPU_GET(cpuid), &idle_cpus_mask);
#endif
	sched_lock.mtx_lock = (uintptr_t)td;
	td->td_oncpu = PCPU_GET(cpuid);
	MPASS(td->td_lock == &sched_lock);
}

void
sched_wakeup(struct thread *td)
{
	struct td_sched *ts;

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	ts = td_get_sched(td);
	td->td_flags &= ~TDF_CANSWAP;
	if (ts->ts_slptime > 1) {
		updatepri(td);
		resetpriority(td);
	}
	td->td_slptick = 0;
	ts->ts_slptime = 0;
	ts->ts_slice = sched_slice;
	sched_add(td, SRQ_BORING);
}

#ifdef SMP
static int
forward_wakeup(int cpunum)
{
	struct pcpu *pc;
	cpuset_t dontuse, map, map2;
	u_int id, me;
	int iscpuset;

	mtx_assert(&sched_lock, MA_OWNED);

	CTR0(KTR_RUNQ, "forward_wakeup()");

	if ((!forward_wakeup_enabled) ||
	     (forward_wakeup_use_mask == 0 && forward_wakeup_use_loop == 0))
		return (0);
	if (!smp_started || panicstr)
		return (0);

	forward_wakeups_requested++;

	/*
	 * Check the idle mask we received against what we calculated
	 * before in the old version.
	 */
	me = PCPU_GET(cpuid);

	/* Don't bother if we should be doing it ourself. */
	if (CPU_ISSET(me, &idle_cpus_mask) &&
	    (cpunum == NOCPU || me == cpunum))
		return (0);

	CPU_SETOF(me, &dontuse);
	CPU_OR(&dontuse, &stopped_cpus);
	CPU_OR(&dontuse, &hlt_cpus_mask);
	CPU_ZERO(&map2);
	if (forward_wakeup_use_loop) {
		STAILQ_FOREACH(pc, &cpuhead, pc_allcpu) {
			id = pc->pc_cpuid;
			if (!CPU_ISSET(id, &dontuse) &&
			    pc->pc_curthread == pc->pc_idlethread) {
				CPU_SET(id, &map2);
			}
		}
	}

	if (forward_wakeup_use_mask) {
		map = idle_cpus_mask;
		CPU_NAND(&map, &dontuse);

		/* If they are both on, compare and use loop if different. */
		if (forward_wakeup_use_loop) {
			if (CPU_CMP(&map, &map2)) {
				printf("map != map2, loop method preferred\n");
				map = map2;
			}
		}
	} else {
		map = map2;
	}

	/* If we only allow a specific CPU, then mask off all the others. */
	if (cpunum != NOCPU) {
		KASSERT((cpunum <= mp_maxcpus),("forward_wakeup: bad cpunum."));
		iscpuset = CPU_ISSET(cpunum, &map);
		if (iscpuset == 0)
			CPU_ZERO(&map);
		else
			CPU_SETOF(cpunum, &map);
	}
	if (!CPU_EMPTY(&map)) {
		forward_wakeups_delivered++;
		STAILQ_FOREACH(pc, &cpuhead, pc_allcpu) {
			id = pc->pc_cpuid;
			if (!CPU_ISSET(id, &map))
				continue;
			if (cpu_idle_wakeup(pc->pc_cpuid))
				CPU_CLR(id, &map);
		}
		if (!CPU_EMPTY(&map))
			ipi_selected(map, IPI_AST);
		return (1);
	}
	if (cpunum == NOCPU)
		printf("forward_wakeup: Idle processor not found\n");
	return (0);
}

static void
kick_other_cpu(int pri, int cpuid)
{
	struct pcpu *pcpu;
	int cpri;

	pcpu = pcpu_find(cpuid);
	if (CPU_ISSET(cpuid, &idle_cpus_mask)) {
		forward_wakeups_delivered++;
		if (!cpu_idle_wakeup(cpuid))
			ipi_cpu(cpuid, IPI_AST);
		return;
	}

	cpri = pcpu->pc_curthread->td_priority;
	if (pri >= cpri)
		return;

#if defined(IPI_PREEMPTION) && defined(PREEMPTION)
#if !defined(FULL_PREEMPTION)
	if (pri <= PRI_MAX_ITHD)
#endif /* ! FULL_PREEMPTION */
	{
		ipi_cpu(cpuid, IPI_PREEMPT);
		return;
	}
#endif /* defined(IPI_PREEMPTION) && defined(PREEMPTION) */

	pcpu->pc_curthread->td_flags |= TDF_NEEDRESCHED;
	ipi_cpu(cpuid, IPI_AST);
	return;
}
#endif /* SMP */

#ifdef SMP
static int
sched_pickcpu(struct thread *td)
{
	int best, cpu;

	mtx_assert(&sched_lock, MA_OWNED);

	if (td->td_lastcpu != NOCPU && THREAD_CAN_SCHED(td, td->td_lastcpu))
		best = td->td_lastcpu;
	else
		best = NOCPU;
	CPU_FOREACH(cpu) {
		if (!THREAD_CAN_SCHED(td, cpu))
			continue;
	
		if (best == NOCPU)
			best = cpu;
		else if (runq_length[cpu] < runq_length[best])
			best = cpu;
	}
	KASSERT(best != NOCPU, ("no valid CPUs"));

	return (best);
}
#endif

void
sched_add(struct thread *td, int flags)
#ifdef SMP
{
	cpuset_t tidlemsk;
	struct td_sched *ts;
	u_int cpu, cpuid;
	int forwarded = 0;
	int single_cpu = 0;

	ts = td_get_sched(td);
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	KASSERT((td->td_inhibitors == 0),
	    ("sched_add: trying to run inhibited thread"));
	KASSERT((TD_CAN_RUN(td) || TD_IS_RUNNING(td)),
	    ("sched_add: bad thread state"));
	KASSERT(td->td_flags & TDF_INMEM,
	    ("sched_add: thread swapped out"));

	KTR_STATE2(KTR_SCHED, "thread", sched_tdname(td), "runq add",
	    "prio:%d", td->td_priority, KTR_ATTR_LINKED,
	    sched_tdname(curthread));
	KTR_POINT1(KTR_SCHED, "thread", sched_tdname(curthread), "wokeup",
	    KTR_ATTR_LINKED, sched_tdname(td));
	SDT_PROBE4(sched, , , enqueue, td, td->td_proc, NULL, 
	    flags & SRQ_PREEMPTED);


	/*
	 * Now that the thread is moving to the run-queue, set the lock
	 * to the scheduler's lock.
	 */
	if (td->td_lock != &sched_lock) {
		mtx_lock_spin(&sched_lock);
		thread_lock_set(td, &sched_lock);
	}
	TD_SET_RUNQ(td);

	/*
	 * If SMP is started and the thread is pinned or otherwise limited to
	 * a specific set of CPUs, queue the thread to a per-CPU run queue.
	 * Otherwise, queue the thread to the global run queue.
	 *
	 * If SMP has not yet been started we must use the global run queue
	 * as per-CPU state may not be initialized yet and we may crash if we
	 * try to access the per-CPU run queues.
	 */
	if (smp_started && (td->td_pinned != 0 || td->td_flags & TDF_BOUND ||
	    ts->ts_flags & TSF_AFFINITY)) {
		if (td->td_pinned != 0)
			cpu = td->td_lastcpu;
		else if (td->td_flags & TDF_BOUND) {
			/* Find CPU from bound runq. */
			KASSERT(SKE_RUNQ_PCPU(ts),
			    ("sched_add: bound td_sched not on cpu runq"));
			cpu = ts->ts_runq - &runq_pcpu[0];
		} else
			/* Find a valid CPU for our cpuset */
			cpu = sched_pickcpu(td);
		ts->ts_runq = &runq_pcpu[cpu];
		single_cpu = 1;
		CTR3(KTR_RUNQ,
		    "sched_add: Put td_sched:%p(td:%p) on cpu%d runq", ts, td,
		    cpu);
	} else {
		CTR2(KTR_RUNQ,
		    "sched_add: adding td_sched:%p (td:%p) to gbl runq", ts,
		    td);
		cpu = NOCPU;
		ts->ts_runq = &runq;
	}

	if ((td->td_flags & TDF_NOLOAD) == 0)
		sched_load_add();
	runq_add(ts->ts_runq, td, flags);
	if (cpu != NOCPU)
		runq_length[cpu]++;

	cpuid = PCPU_GET(cpuid);
	if (single_cpu && cpu != cpuid) {
	        kick_other_cpu(td->td_priority, cpu);
	} else {
		if (!single_cpu) {
			tidlemsk = idle_cpus_mask;
			CPU_NAND(&tidlemsk, &hlt_cpus_mask);
			CPU_CLR(cpuid, &tidlemsk);

			if (!CPU_ISSET(cpuid, &idle_cpus_mask) &&
			    ((flags & SRQ_INTR) == 0) &&
			    !CPU_EMPTY(&tidlemsk))
				forwarded = forward_wakeup(cpu);
		}

		if (!forwarded) {
			if (!maybe_preempt(td))
				maybe_resched(td);
		}
	}
}
#else /* SMP */
{
	struct td_sched *ts;

	ts = td_get_sched(td);
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	KASSERT((td->td_inhibitors == 0),
	    ("sched_add: trying to run inhibited thread"));
	KASSERT((TD_CAN_RUN(td) || TD_IS_RUNNING(td)),
	    ("sched_add: bad thread state"));
	KASSERT(td->td_flags & TDF_INMEM,
	    ("sched_add: thread swapped out"));
	KTR_STATE2(KTR_SCHED, "thread", sched_tdname(td), "runq add",
	    "prio:%d", td->td_priority, KTR_ATTR_LINKED,
	    sched_tdname(curthread));
	KTR_POINT1(KTR_SCHED, "thread", sched_tdname(curthread), "wokeup",
	    KTR_ATTR_LINKED, sched_tdname(td));
	SDT_PROBE4(sched, , , enqueue, td, td->td_proc, NULL, 
	    flags & SRQ_PREEMPTED);

	/*
	 * Now that the thread is moving to the run-queue, set the lock
	 * to the scheduler's lock.
	 */
	if (td->td_lock != &sched_lock) {
		mtx_lock_spin(&sched_lock);
		thread_lock_set(td, &sched_lock);
	}
	TD_SET_RUNQ(td);
	CTR2(KTR_RUNQ, "sched_add: adding td_sched:%p (td:%p) to runq", ts, td);
	ts->ts_runq = &runq;

	if ((td->td_flags & TDF_NOLOAD) == 0)
		sched_load_add();
	runq_add(ts->ts_runq, td, flags);
	if (!maybe_preempt(td))
		maybe_resched(td);
}
#endif /* SMP */

void
sched_rem(struct thread *td)
{
	struct td_sched *ts;

	ts = td_get_sched(td);
	KASSERT(td->td_flags & TDF_INMEM,
	    ("sched_rem: thread swapped out"));
	KASSERT(TD_ON_RUNQ(td),
	    ("sched_rem: thread not on run queue"));
	mtx_assert(&sched_lock, MA_OWNED);
	KTR_STATE2(KTR_SCHED, "thread", sched_tdname(td), "runq rem",
	    "prio:%d", td->td_priority, KTR_ATTR_LINKED,
	    sched_tdname(curthread));
	SDT_PROBE3(sched, , , dequeue, td, td->td_proc, NULL);

	if ((td->td_flags & TDF_NOLOAD) == 0)
		sched_load_rem();
#ifdef SMP
	if (ts->ts_runq != &runq)
		runq_length[ts->ts_runq - runq_pcpu]--;
#endif
	runq_remove(ts->ts_runq, td);
	TD_SET_CAN_RUN(td);
}

/*
 * Select threads to run.  Note that running threads still consume a
 * slot.
 */
struct thread *
sched_choose(void)
{
	struct thread *td;
	struct runq *rq;

	mtx_assert(&sched_lock,  MA_OWNED);
#ifdef SMP
	struct thread *tdcpu;

	rq = &runq;
	td = runq_choose_fuzz(&runq, runq_fuzz);
	tdcpu = runq_choose(&runq_pcpu[PCPU_GET(cpuid)]);

	if (td == NULL ||
	    (tdcpu != NULL &&
	     tdcpu->td_priority < td->td_priority)) {
		CTR2(KTR_RUNQ, "choosing td %p from pcpu runq %d", tdcpu,
		     PCPU_GET(cpuid));
		td = tdcpu;
		rq = &runq_pcpu[PCPU_GET(cpuid)];
	} else {
		CTR1(KTR_RUNQ, "choosing td_sched %p from main runq", td);
	}

#else
	rq = &runq;
	td = runq_choose(&runq);
#endif

	if (td) {
#ifdef SMP
		if (td == tdcpu)
			runq_length[PCPU_GET(cpuid)]--;
#endif
		runq_remove(rq, td);
		td->td_flags |= TDF_DIDRUN;

		KASSERT(td->td_flags & TDF_INMEM,
		    ("sched_choose: thread swapped out"));
		return (td);
	}
	return (PCPU_GET(idlethread));
}

void
sched_preempt(struct thread *td)
{

	SDT_PROBE2(sched, , , surrender, td, td->td_proc);
	thread_lock(td);
	if (td->td_critnest > 1)
		td->td_owepreempt = 1;
	else
		mi_switch(SW_INVOL | SW_PREEMPT | SWT_PREEMPT, NULL);
	thread_unlock(td);
}

void
sched_userret_slowpath(struct thread *td)
{

	thread_lock(td);
	td->td_priority = td->td_user_pri;
	td->td_base_pri = td->td_user_pri;
	thread_unlock(td);
}

void
sched_bind(struct thread *td, int cpu)
{
	struct td_sched *ts;

	THREAD_LOCK_ASSERT(td, MA_OWNED|MA_NOTRECURSED);
	KASSERT(td == curthread, ("sched_bind: can only bind curthread"));

	ts = td_get_sched(td);

	td->td_flags |= TDF_BOUND;
#ifdef SMP
	ts->ts_runq = &runq_pcpu[cpu];
	if (PCPU_GET(cpuid) == cpu)
		return;

	mi_switch(SW_VOL, NULL);
#endif
}

void
sched_unbind(struct thread* td)
{
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	KASSERT(td == curthread, ("sched_unbind: can only bind curthread"));
	td->td_flags &= ~TDF_BOUND;
}

int
sched_is_bound(struct thread *td)
{
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	return (td->td_flags & TDF_BOUND);
}

void
sched_relinquish(struct thread *td)
{
	thread_lock(td);
	mi_switch(SW_VOL | SWT_RELINQUISH, NULL);
	thread_unlock(td);
}

int
sched_load(void)
{
	return (sched_tdcnt);
}

int
sched_sizeof_proc(void)
{
	return (sizeof(struct proc));
}

int
sched_sizeof_thread(void)
{
	return (sizeof(struct thread) + sizeof(struct td_sched));
}

fixpt_t
sched_pctcpu(struct thread *td)
{
	struct td_sched *ts;

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	ts = td_get_sched(td);
	return (ts->ts_pctcpu);
}

#ifdef RACCT
/*
 * Calculates the contribution to the thread cpu usage for the latest
 * (unfinished) second.
 */
fixpt_t
sched_pctcpu_delta(struct thread *td)
{
	struct td_sched *ts;
	fixpt_t delta;
	int realstathz;

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	ts = td_get_sched(td);
	delta = 0;
	realstathz = stathz ? stathz : hz;
	if (ts->ts_cpticks != 0) {
#if	(FSHIFT >= CCPU_SHIFT)
		delta = (realstathz == 100)
		    ? ((fixpt_t) ts->ts_cpticks) <<
		    (FSHIFT - CCPU_SHIFT) :
		    100 * (((fixpt_t) ts->ts_cpticks)
		    << (FSHIFT - CCPU_SHIFT)) / realstathz;
#else
		delta = ((FSCALE - ccpu) *
		    (ts->ts_cpticks *
		    FSCALE / realstathz)) >> FSHIFT;
#endif
	}

	return (delta);
}
#endif

u_int
sched_estcpu(struct thread *td)
{
	
	return (td_get_sched(td)->ts_estcpu);
}

/*
 * The actual idle process.
 */
void
sched_idletd(void *dummy)
{
	struct pcpuidlestat *stat;

	THREAD_NO_SLEEPING();
	stat = DPCPU_PTR(idlestat);
	for (;;) {
		mtx_assert(&Giant, MA_NOTOWNED);

		while (sched_runnable() == 0) {
			cpu_idle(stat->idlecalls + stat->oldidlecalls > 64);
			stat->idlecalls++;
		}

		mtx_lock_spin(&sched_lock);
		mi_switch(SW_VOL | SWT_IDLE, NULL);
		mtx_unlock_spin(&sched_lock);
	}
}

/*
 * A CPU is entering for the first time or a thread is exiting.
 */
void
sched_throw(struct thread *td)
{
	/*
	 * Correct spinlock nesting.  The idle thread context that we are
	 * borrowing was created so that it would start out with a single
	 * spin lock (sched_lock) held in fork_trampoline().  Since we've
	 * explicitly acquired locks in this function, the nesting count
	 * is now 2 rather than 1.  Since we are nested, calling
	 * spinlock_exit() will simply adjust the counts without allowing
	 * spin lock using code to interrupt us.
	 */
	if (td == NULL) {
		mtx_lock_spin(&sched_lock);
		spinlock_exit();
		PCPU_SET(switchtime, cpu_ticks());
		PCPU_SET(switchticks, ticks);
	} else {
		lock_profile_release_lock(&sched_lock.lock_object);
		MPASS(td->td_lock == &sched_lock);
		td->td_lastcpu = td->td_oncpu;
		td->td_oncpu = NOCPU;
	}
	mtx_assert(&sched_lock, MA_OWNED);
	KASSERT(curthread->td_md.md_spinlock_count == 1, ("invalid count"));
	cpu_throw(td, choosethread());	/* doesn't return */
}

void
sched_fork_exit(struct thread *td)
{

	/*
	 * Finish setting up thread glue so that it begins execution in a
	 * non-nested critical section with sched_lock held but not recursed.
	 */
	td->td_oncpu = PCPU_GET(cpuid);
	sched_lock.mtx_lock = (uintptr_t)td;
	lock_profile_obtain_lock_success(&sched_lock.lock_object,
	    0, 0, __FILE__, __LINE__);
	THREAD_LOCK_ASSERT(td, MA_OWNED | MA_NOTRECURSED);

	KTR_STATE1(KTR_SCHED, "thread", sched_tdname(td), "running",
	    "prio:%d", td->td_priority);
	SDT_PROBE0(sched, , , on__cpu);
}

char *
sched_tdname(struct thread *td)
{
#ifdef KTR
	struct td_sched *ts;

	ts = td_get_sched(td);
	if (ts->ts_name[0] == '\0')
		snprintf(ts->ts_name, sizeof(ts->ts_name),
		    "%s tid %d", td->td_name, td->td_tid);
	return (ts->ts_name);
#else   
	return (td->td_name);
#endif
}

#ifdef KTR
void
sched_clear_tdname(struct thread *td)
{
	struct td_sched *ts;

	ts = td_get_sched(td);
	ts->ts_name[0] = '\0';
}
#endif

void
sched_affinity(struct thread *td)
{
#ifdef SMP
	struct td_sched *ts;
	int cpu;

	THREAD_LOCK_ASSERT(td, MA_OWNED);	

	/*
	 * Set the TSF_AFFINITY flag if there is at least one CPU this
	 * thread can't run on.
	 */
	ts = td_get_sched(td);
	ts->ts_flags &= ~TSF_AFFINITY;
	CPU_FOREACH(cpu) {
		if (!THREAD_CAN_SCHED(td, cpu)) {
			ts->ts_flags |= TSF_AFFINITY;
			break;
		}
	}

	/*
	 * If this thread can run on all CPUs, nothing else to do.
	 */
	if (!(ts->ts_flags & TSF_AFFINITY))
		return;

	/* Pinned threads and bound threads should be left alone. */
	if (td->td_pinned != 0 || td->td_flags & TDF_BOUND)
		return;

	switch (td->td_state) {
	case TDS_RUNQ:
		/*
		 * If we are on a per-CPU runqueue that is in the set,
		 * then nothing needs to be done.
		 */
		if (ts->ts_runq != &runq &&
		    THREAD_CAN_SCHED(td, ts->ts_runq - runq_pcpu))
			return;

		/* Put this thread on a valid per-CPU runqueue. */
		sched_rem(td);
		sched_add(td, SRQ_BORING);
		break;
	case TDS_RUNNING:
		/*
		 * See if our current CPU is in the set.  If not, force a
		 * context switch.
		 */
		if (THREAD_CAN_SCHED(td, td->td_oncpu))
			return;

		td->td_flags |= TDF_NEEDRESCHED;
		if (td != curthread)
			ipi_cpu(cpu, IPI_AST);
		break;
	default:
		break;
	}
#endif
}
