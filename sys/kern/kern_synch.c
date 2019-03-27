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
 *
 *	@(#)kern_synch.c	8.9 (Berkeley) 5/19/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ktrace.h"
#include "opt_sched.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/condvar.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/sched.h>
#include <sys/sdt.h>
#include <sys/signalvar.h>
#include <sys/sleepqueue.h>
#include <sys/smp.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/vmmeter.h>
#ifdef KTRACE
#include <sys/uio.h>
#include <sys/ktrace.h>
#endif

#include <machine/cpu.h>

static void synch_setup(void *dummy);
SYSINIT(synch_setup, SI_SUB_KICK_SCHEDULER, SI_ORDER_FIRST, synch_setup,
    NULL);

int	hogticks;
static uint8_t pause_wchan[MAXCPU];

static struct callout loadav_callout;

struct loadavg averunnable =
	{ {0, 0, 0}, FSCALE };	/* load average, of runnable procs */
/*
 * Constants for averages over 1, 5, and 15 minutes
 * when sampling at 5 second intervals.
 */
static fixpt_t cexp[3] = {
	0.9200444146293232 * FSCALE,	/* exp(-1/12) */
	0.9834714538216174 * FSCALE,	/* exp(-1/60) */
	0.9944598480048967 * FSCALE,	/* exp(-1/180) */
};

/* kernel uses `FSCALE', userland (SHOULD) use kern.fscale */
SYSCTL_INT(_kern, OID_AUTO, fscale, CTLFLAG_RD, SYSCTL_NULL_INT_PTR, FSCALE, "");

static void	loadav(void *arg);

SDT_PROVIDER_DECLARE(sched);
SDT_PROBE_DEFINE(sched, , , preempt);

static void
sleepinit(void *unused)
{

	hogticks = (hz / 10) * 2;	/* Default only. */
	init_sleepqueues();
}

/*
 * vmem tries to lock the sleepq mutexes when free'ing kva, so make sure
 * it is available.
 */
SYSINIT(sleepinit, SI_SUB_KMEM, SI_ORDER_ANY, sleepinit, NULL);

/*
 * General sleep call.  Suspends the current thread until a wakeup is
 * performed on the specified identifier.  The thread will then be made
 * runnable with the specified priority.  Sleeps at most sbt units of time
 * (0 means no timeout).  If pri includes the PCATCH flag, let signals
 * interrupt the sleep, otherwise ignore them while sleeping.  Returns 0 if
 * awakened, EWOULDBLOCK if the timeout expires.  If PCATCH is set and a
 * signal becomes pending, ERESTART is returned if the current system
 * call should be restarted if possible, and EINTR is returned if the system
 * call should be interrupted by the signal (return EINTR).
 *
 * The lock argument is unlocked before the caller is suspended, and
 * re-locked before _sleep() returns.  If priority includes the PDROP
 * flag the lock is not re-locked before returning.
 */
int
_sleep(void *ident, struct lock_object *lock, int priority,
    const char *wmesg, sbintime_t sbt, sbintime_t pr, int flags)
{
	struct thread *td;
	struct lock_class *class;
	uintptr_t lock_state;
	int catch, pri, rval, sleepq_flags;
	WITNESS_SAVE_DECL(lock_witness);

	td = curthread;
#ifdef KTRACE
	if (KTRPOINT(td, KTR_CSW))
		ktrcsw(1, 0, wmesg);
#endif
	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, lock,
	    "Sleeping on \"%s\"", wmesg);
	KASSERT(sbt != 0 || mtx_owned(&Giant) || lock != NULL,
	    ("sleeping without a lock"));
	KASSERT(ident != NULL, ("_sleep: NULL ident"));
	KASSERT(TD_IS_RUNNING(td), ("_sleep: curthread not running"));
	KASSERT(td->td_epochnest == 0, ("sleeping in an epoch section"));
	if (priority & PDROP)
		KASSERT(lock != NULL && lock != &Giant.lock_object,
		    ("PDROP requires a non-Giant lock"));
	if (lock != NULL)
		class = LOCK_CLASS(lock);
	else
		class = NULL;

	if (SCHEDULER_STOPPED_TD(td)) {
		if (lock != NULL && priority & PDROP)
			class->lc_unlock(lock);
		return (0);
	}
	catch = priority & PCATCH;
	pri = priority & PRIMASK;

	KASSERT(!TD_ON_SLEEPQ(td), ("recursive sleep"));

	if ((uint8_t *)ident >= &pause_wchan[0] &&
	    (uint8_t *)ident <= &pause_wchan[MAXCPU - 1])
		sleepq_flags = SLEEPQ_PAUSE;
	else
		sleepq_flags = SLEEPQ_SLEEP;
	if (catch)
		sleepq_flags |= SLEEPQ_INTERRUPTIBLE;

	sleepq_lock(ident);
	CTR5(KTR_PROC, "sleep: thread %ld (pid %ld, %s) on %s (%p)",
	    td->td_tid, td->td_proc->p_pid, td->td_name, wmesg, ident);

	if (lock == &Giant.lock_object)
		mtx_assert(&Giant, MA_OWNED);
	DROP_GIANT();
	if (lock != NULL && lock != &Giant.lock_object &&
	    !(class->lc_flags & LC_SLEEPABLE)) {
		WITNESS_SAVE(lock, lock_witness);
		lock_state = class->lc_unlock(lock);
	} else
		/* GCC needs to follow the Yellow Brick Road */
		lock_state = -1;

	/*
	 * We put ourselves on the sleep queue and start our timeout
	 * before calling thread_suspend_check, as we could stop there,
	 * and a wakeup or a SIGCONT (or both) could occur while we were
	 * stopped without resuming us.  Thus, we must be ready for sleep
	 * when cursig() is called.  If the wakeup happens while we're
	 * stopped, then td will no longer be on a sleep queue upon
	 * return from cursig().
	 */
	sleepq_add(ident, lock, wmesg, sleepq_flags, 0);
	if (sbt != 0)
		sleepq_set_timeout_sbt(ident, sbt, pr, flags);
	if (lock != NULL && class->lc_flags & LC_SLEEPABLE) {
		sleepq_release(ident);
		WITNESS_SAVE(lock, lock_witness);
		lock_state = class->lc_unlock(lock);
		sleepq_lock(ident);
	}
	if (sbt != 0 && catch)
		rval = sleepq_timedwait_sig(ident, pri);
	else if (sbt != 0)
		rval = sleepq_timedwait(ident, pri);
	else if (catch)
		rval = sleepq_wait_sig(ident, pri);
	else {
		sleepq_wait(ident, pri);
		rval = 0;
	}
#ifdef KTRACE
	if (KTRPOINT(td, KTR_CSW))
		ktrcsw(0, 0, wmesg);
#endif
	PICKUP_GIANT();
	if (lock != NULL && lock != &Giant.lock_object && !(priority & PDROP)) {
		class->lc_lock(lock, lock_state);
		WITNESS_RESTORE(lock, lock_witness);
	}
	return (rval);
}

int
msleep_spin_sbt(void *ident, struct mtx *mtx, const char *wmesg,
    sbintime_t sbt, sbintime_t pr, int flags)
{
	struct thread *td;
	int rval;
	WITNESS_SAVE_DECL(mtx);

	td = curthread;
	KASSERT(mtx != NULL, ("sleeping without a mutex"));
	KASSERT(ident != NULL, ("msleep_spin_sbt: NULL ident"));
	KASSERT(TD_IS_RUNNING(td), ("msleep_spin_sbt: curthread not running"));

	if (SCHEDULER_STOPPED_TD(td))
		return (0);

	sleepq_lock(ident);
	CTR5(KTR_PROC, "msleep_spin: thread %ld (pid %ld, %s) on %s (%p)",
	    td->td_tid, td->td_proc->p_pid, td->td_name, wmesg, ident);

	DROP_GIANT();
	mtx_assert(mtx, MA_OWNED | MA_NOTRECURSED);
	WITNESS_SAVE(&mtx->lock_object, mtx);
	mtx_unlock_spin(mtx);

	/*
	 * We put ourselves on the sleep queue and start our timeout.
	 */
	sleepq_add(ident, &mtx->lock_object, wmesg, SLEEPQ_SLEEP, 0);
	if (sbt != 0)
		sleepq_set_timeout_sbt(ident, sbt, pr, flags);

	/*
	 * Can't call ktrace with any spin locks held so it can lock the
	 * ktrace_mtx lock, and WITNESS_WARN considers it an error to hold
	 * any spin lock.  Thus, we have to drop the sleepq spin lock while
	 * we handle those requests.  This is safe since we have placed our
	 * thread on the sleep queue already.
	 */
#ifdef KTRACE
	if (KTRPOINT(td, KTR_CSW)) {
		sleepq_release(ident);
		ktrcsw(1, 0, wmesg);
		sleepq_lock(ident);
	}
#endif
#ifdef WITNESS
	sleepq_release(ident);
	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL, "Sleeping on \"%s\"",
	    wmesg);
	sleepq_lock(ident);
#endif
	if (sbt != 0)
		rval = sleepq_timedwait(ident, 0);
	else {
		sleepq_wait(ident, 0);
		rval = 0;
	}
#ifdef KTRACE
	if (KTRPOINT(td, KTR_CSW))
		ktrcsw(0, 0, wmesg);
#endif
	PICKUP_GIANT();
	mtx_lock_spin(mtx);
	WITNESS_RESTORE(&mtx->lock_object, mtx);
	return (rval);
}

/*
 * pause_sbt() delays the calling thread by the given signed binary
 * time. During cold bootup, pause_sbt() uses the DELAY() function
 * instead of the _sleep() function to do the waiting. The "sbt"
 * argument must be greater than or equal to zero. A "sbt" value of
 * zero is equivalent to a "sbt" value of one tick.
 */
int
pause_sbt(const char *wmesg, sbintime_t sbt, sbintime_t pr, int flags)
{
	KASSERT(sbt >= 0, ("pause_sbt: timeout must be >= 0"));

	/* silently convert invalid timeouts */
	if (sbt == 0)
		sbt = tick_sbt;

	if ((cold && curthread == &thread0) || kdb_active ||
	    SCHEDULER_STOPPED()) {
		/*
		 * We delay one second at a time to avoid overflowing the
		 * system specific DELAY() function(s):
		 */
		while (sbt >= SBT_1S) {
			DELAY(1000000);
			sbt -= SBT_1S;
		}
		/* Do the delay remainder, if any */
		sbt = howmany(sbt, SBT_1US);
		if (sbt > 0)
			DELAY(sbt);
		return (EWOULDBLOCK);
	}
	return (_sleep(&pause_wchan[curcpu], NULL,
	    (flags & C_CATCH) ? PCATCH : 0, wmesg, sbt, pr, flags));
}

/*
 * Make all threads sleeping on the specified identifier runnable.
 */
void
wakeup(void *ident)
{
	int wakeup_swapper;

	sleepq_lock(ident);
	wakeup_swapper = sleepq_broadcast(ident, SLEEPQ_SLEEP, 0, 0);
	sleepq_release(ident);
	if (wakeup_swapper) {
		KASSERT(ident != &proc0,
		    ("wakeup and wakeup_swapper and proc0"));
		kick_proc0();
	}
}

/*
 * Make a thread sleeping on the specified identifier runnable.
 * May wake more than one thread if a target thread is currently
 * swapped out.
 */
void
wakeup_one(void *ident)
{
	int wakeup_swapper;

	sleepq_lock(ident);
	wakeup_swapper = sleepq_signal(ident, SLEEPQ_SLEEP, 0, 0);
	sleepq_release(ident);
	if (wakeup_swapper)
		kick_proc0();
}

static void
kdb_switch(void)
{
	thread_unlock(curthread);
	kdb_backtrace();
	kdb_reenter();
	panic("%s: did not reenter debugger", __func__);
}

/*
 * The machine independent parts of context switching.
 */
void
mi_switch(int flags, struct thread *newtd)
{
	uint64_t runtime, new_switchtime;
	struct thread *td;

	td = curthread;			/* XXX */
	THREAD_LOCK_ASSERT(td, MA_OWNED | MA_NOTRECURSED);
	KASSERT(!TD_ON_RUNQ(td), ("mi_switch: called by old code"));
#ifdef INVARIANTS
	if (!TD_ON_LOCK(td) && !TD_IS_RUNNING(td))
		mtx_assert(&Giant, MA_NOTOWNED);
#endif
	KASSERT(td->td_critnest == 1 || panicstr,
	    ("mi_switch: switch in a critical section"));
	KASSERT((flags & (SW_INVOL | SW_VOL)) != 0,
	    ("mi_switch: switch must be voluntary or involuntary"));
	KASSERT(newtd != curthread, ("mi_switch: preempting back to ourself"));

	/*
	 * Don't perform context switches from the debugger.
	 */
	if (kdb_active)
		kdb_switch();
	if (SCHEDULER_STOPPED_TD(td))
		return;
	if (flags & SW_VOL) {
		td->td_ru.ru_nvcsw++;
		td->td_swvoltick = ticks;
	} else {
		td->td_ru.ru_nivcsw++;
		td->td_swinvoltick = ticks;
	}
#ifdef SCHED_STATS
	SCHED_STAT_INC(sched_switch_stats[flags & SW_TYPE_MASK]);
#endif
	/*
	 * Compute the amount of time during which the current
	 * thread was running, and add that to its total so far.
	 */
	new_switchtime = cpu_ticks();
	runtime = new_switchtime - PCPU_GET(switchtime);
	td->td_runtime += runtime;
	td->td_incruntime += runtime;
	PCPU_SET(switchtime, new_switchtime);
	td->td_generation++;	/* bump preempt-detect counter */
	VM_CNT_INC(v_swtch);
	PCPU_SET(switchticks, ticks);
	CTR4(KTR_PROC, "mi_switch: old thread %ld (td_sched %p, pid %ld, %s)",
	    td->td_tid, td_get_sched(td), td->td_proc->p_pid, td->td_name);
#ifdef KDTRACE_HOOKS
	if (SDT_PROBES_ENABLED() &&
	    ((flags & SW_PREEMPT) != 0 || ((flags & SW_INVOL) != 0 &&
	    (flags & SW_TYPE_MASK) == SWT_NEEDRESCHED)))
		SDT_PROBE0(sched, , , preempt);
#endif
	sched_switch(td, newtd, flags);
	CTR4(KTR_PROC, "mi_switch: new thread %ld (td_sched %p, pid %ld, %s)",
	    td->td_tid, td_get_sched(td), td->td_proc->p_pid, td->td_name);

	/* 
	 * If the last thread was exiting, finish cleaning it up.
	 */
	if ((td = PCPU_GET(deadthread))) {
		PCPU_SET(deadthread, NULL);
		thread_stash(td);
	}
}

/*
 * Change thread state to be runnable, placing it on the run queue if
 * it is in memory.  If it is swapped out, return true so our caller
 * will know to awaken the swapper.
 */
int
setrunnable(struct thread *td)
{

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	KASSERT(td->td_proc->p_state != PRS_ZOMBIE,
	    ("setrunnable: pid %d is a zombie", td->td_proc->p_pid));
	switch (td->td_state) {
	case TDS_RUNNING:
	case TDS_RUNQ:
		return (0);
	case TDS_INHIBITED:
		/*
		 * If we are only inhibited because we are swapped out
		 * then arange to swap in this process. Otherwise just return.
		 */
		if (td->td_inhibitors != TDI_SWAPPED)
			return (0);
		/* FALLTHROUGH */
	case TDS_CAN_RUN:
		break;
	default:
		printf("state is 0x%x", td->td_state);
		panic("setrunnable(2)");
	}
	if ((td->td_flags & TDF_INMEM) == 0) {
		if ((td->td_flags & TDF_SWAPINREQ) == 0) {
			td->td_flags |= TDF_SWAPINREQ;
			return (1);
		}
	} else
		sched_wakeup(td);
	return (0);
}

/*
 * Compute a tenex style load average of a quantity on
 * 1, 5 and 15 minute intervals.
 */
static void
loadav(void *arg)
{
	int i, nrun;
	struct loadavg *avg;

	nrun = sched_load();
	avg = &averunnable;

	for (i = 0; i < 3; i++)
		avg->ldavg[i] = (cexp[i] * avg->ldavg[i] +
		    nrun * FSCALE * (FSCALE - cexp[i])) >> FSHIFT;

	/*
	 * Schedule the next update to occur after 5 seconds, but add a
	 * random variation to avoid synchronisation with processes that
	 * run at regular intervals.
	 */
	callout_reset_sbt(&loadav_callout,
	    SBT_1US * (4000000 + (int)(random() % 2000001)), SBT_1US,
	    loadav, NULL, C_DIRECT_EXEC | C_PREL(32));
}

/* ARGSUSED */
static void
synch_setup(void *dummy)
{
	callout_init(&loadav_callout, 1);

	/* Kick off timeout driven events by calling first time. */
	loadav(NULL);
}

int
should_yield(void)
{

	return ((u_int)ticks - (u_int)curthread->td_swvoltick >= hogticks);
}

void
maybe_yield(void)
{

	if (should_yield())
		kern_yield(PRI_USER);
}

void
kern_yield(int prio)
{
	struct thread *td;

	td = curthread;
	DROP_GIANT();
	thread_lock(td);
	if (prio == PRI_USER)
		prio = td->td_user_pri;
	if (prio >= 0)
		sched_prio(td, prio);
	mi_switch(SW_VOL | SWT_RELINQUISH, NULL);
	thread_unlock(td);
	PICKUP_GIANT();
}

/*
 * General purpose yield system call.
 */
int
sys_yield(struct thread *td, struct yield_args *uap)
{

	thread_lock(td);
	if (PRI_BASE(td->td_pri_class) == PRI_TIMESHARE)
		sched_prio(td, PRI_MAX_TIMESHARE);
	mi_switch(SW_VOL | SWT_RELINQUISH, NULL);
	thread_unlock(td);
	td->td_retval[0] = 0;
	return (0);
}
