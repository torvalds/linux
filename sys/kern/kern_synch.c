/*	$OpenBSD: kern_synch.c,v 1.232 2025/08/18 04:15:35 dlg Exp $	*/
/*	$NetBSD: kern_synch.c,v 1.37 1996/04/22 01:38:37 christos Exp $	*/

/*
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
 *	@(#)kern_synch.c	8.6 (Berkeley) 1/21/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/signalvar.h>
#include <sys/sched.h>
#include <sys/timeout.h>
#include <sys/mount.h>
#include <sys/syscallargs.h>
#include <sys/refcnt.h>
#include <sys/atomic.h>
#include <sys/tracepoint.h>

#include <ddb/db_output.h>

#include <machine/spinlock.h>

#ifdef DIAGNOSTIC
#include <sys/syslog.h>
#endif

#ifdef KTRACE
#include <sys/ktrace.h>
#endif

int	sleep_signal_check(struct proc *, int);

/*
 * We're only looking at 7 bits of the address; everything is
 * aligned to 4, lots of things are aligned to greater powers
 * of 2.  Shift right by 8, i.e. drop the bottom 256 worth.
 */
#define TABLESIZE	128
#define LOOKUP(x)	(((long)(x) >> 8) & (TABLESIZE - 1))
TAILQ_HEAD(slpque,proc) slpque[TABLESIZE];

void
sleep_queue_init(void)
{
	int i;

	for (i = 0; i < TABLESIZE; i++)
		TAILQ_INIT(&slpque[i]);
}

/*
 * Global sleep channel for threads that do not want to
 * receive wakeup(9) broadcasts.
 */
int nowake;

/*
 * During autoconfiguration or after a panic, a sleep will simply
 * lower the priority briefly to allow interrupts, then return.
 * The priority to be used (safepri) is machine-dependent, thus this
 * value is initialized and maintained in the machine-dependent layers.
 * This priority will typically be 0, or the lowest priority
 * that is safe for use on the interrupt stack; it can be made
 * higher to block network software interrupts after panics.
 */
extern int safepri;

/*
 * General sleep call.  Suspends the current process until a wakeup is
 * performed on the specified identifier.  The process will then be made
 * runnable with the specified priority.  Sleeps at most timo/hz seconds
 * (0 means no timeout).  If pri includes PCATCH flag, signals are checked
 * before and after sleeping, else signals are not checked.  Returns 0 if
 * awakened, EWOULDBLOCK if the timeout expires.  If PCATCH is set and a
 * signal needs to be delivered, ERESTART is returned if the current system
 * call should be restarted if possible, and EINTR is returned if the system
 * call should be interrupted by the signal (return EINTR).
 */
int
tsleep_nsec(const volatile void *ident, int priority, const char *wmesg,
    uint64_t nsecs)
{
#ifdef MULTIPROCESSOR
	int hold_count;
#endif

	KASSERT((priority & ~(PRIMASK | PCATCH)) == 0);
	KASSERT(ident != &nowake || ISSET(priority, PCATCH) || nsecs != INFSLP);

#ifdef MULTIPROCESSOR
	KASSERT(ident == &nowake || nsecs != INFSLP || _kernel_lock_held());
#endif

#ifdef DDB
	if (cold == 2)
		db_stack_dump();
#endif
	if (cold || panicstr) {
		int s;
		/*
		 * After a panic, or during autoconfiguration,
		 * just give interrupts a chance, then just return;
		 * don't run any other procs or panic below,
		 * in case this is the idle process and already asleep.
		 */
		s = splhigh();
		splx(safepri);
#ifdef MULTIPROCESSOR
		if (_kernel_lock_held()) {
			hold_count = __mp_release_all(&kernel_lock);
			__mp_acquire_count(&kernel_lock, hold_count);
		}
#endif
		splx(s);
		return (0);
	}

	sleep_setup(ident, priority, wmesg);
	return sleep_finish(nsecs, 1);
}

int
tsleep(const volatile void *ident, int priority, const char *wmesg,
    int timo)
{
	uint64_t nsecs = INFSLP;

	if (timo < 0)
		panic("%s: negative timo %d", __func__, timo);
	if (timo > 0)
		nsecs = TICKS_TO_NSEC(timo);

	return tsleep_nsec(ident, priority, wmesg, nsecs);
}

/*
 * Same as tsleep, but if we have a mutex provided, then once we've
 * entered the sleep queue we drop the mutex. After sleeping we re-lock.
 */
int
msleep_nsec(const volatile void *ident, struct mutex *mtx, int priority,
    const char *wmesg, uint64_t nsecs)
{
	int error, spl;
#ifdef MULTIPROCESSOR
	int hold_count;
#endif

	KASSERT((priority & ~(PRIMASK | PCATCH | PNORELOCK)) == 0);
	KASSERT(ident != &nowake || ISSET(priority, PCATCH) || nsecs != INFSLP);
	KASSERT(mtx != NULL);

#ifdef DDB
	if (cold == 2)
		db_stack_dump();
#endif
	if (cold || panicstr) {
		/*
		 * After a panic, or during autoconfiguration,
		 * just give interrupts a chance, then just return;
		 * don't run any other procs or panic below,
		 * in case this is the idle process and already asleep.
		 */
		spl = MUTEX_OLDIPL(mtx);
		MUTEX_OLDIPL(mtx) = safepri;
		mtx_leave(mtx);
#ifdef MULTIPROCESSOR
		if (_kernel_lock_held()) {
			hold_count = __mp_release_all(&kernel_lock);
			__mp_acquire_count(&kernel_lock, hold_count);
		}
#endif
		if ((priority & PNORELOCK) == 0) {
			mtx_enter(mtx);
			MUTEX_OLDIPL(mtx) = spl;
		} else
			splx(spl);
		return (0);
	}

	sleep_setup(ident, priority, wmesg);

	mtx_leave(mtx);
	/* signal may stop the process, release mutex before that */
	error = sleep_finish(nsecs, 1);

	if ((priority & PNORELOCK) == 0)
		mtx_enter(mtx);

	return error;
}

int
msleep(const volatile void *ident, struct mutex *mtx, int priority,
    const char *wmesg, int timo)
{
	uint64_t nsecs = INFSLP;

	if (timo < 0)
		panic("%s: negative timo %d", __func__, timo);
	if (timo > 0)
		nsecs = TICKS_TO_NSEC(timo);

	return msleep_nsec(ident, mtx, priority, wmesg, nsecs);
}

/*
 * Same as tsleep, but if we have a rwlock provided, then once we've
 * entered the sleep queue we drop the it. After sleeping we re-lock.
 */
int
rwsleep_nsec(const volatile void *ident, struct rwlock *rwl, int priority,
    const char *wmesg, uint64_t nsecs)
{
	int error, status;

	KASSERT((priority & ~(PRIMASK | PCATCH | PNORELOCK)) == 0);
	KASSERT(ident != &nowake || ISSET(priority, PCATCH) || nsecs != INFSLP);
	KASSERT(ident != rwl);
	rw_assert_anylock(rwl);
	status = rw_status(rwl);

	sleep_setup(ident, priority, wmesg);

	rw_exit(rwl);
	/* signal may stop the process, release rwlock before that */
	error = sleep_finish(nsecs, 1);

	if ((priority & PNORELOCK) == 0)
		rw_enter(rwl, status);

	return error;
}

int
rwsleep(const volatile void *ident, struct rwlock *rwl, int priority,
    const char *wmesg, int timo)
{
	uint64_t nsecs = INFSLP;

	if (timo < 0)
		panic("%s: negative timo %d", __func__, timo);
	if (timo > 0)
		nsecs = TICKS_TO_NSEC(timo);

	return rwsleep_nsec(ident, rwl, priority, wmesg, nsecs);
}

void
sleep_setup(const volatile void *ident, int prio, const char *wmesg)
{
	struct proc *p = curproc;

#ifdef DIAGNOSTIC
	if (p->p_flag & P_CANTSLEEP)
		panic("sleep: %s failed insomnia", p->p_p->ps_comm);
	if (ident == NULL)
		panic("sleep: no ident");
	if (p->p_stat != SONPROC)
		panic("sleep: not SONPROC but %d", p->p_stat);
#endif
	/* exiting processes are not allowed to catch signals */
	if (p->p_flag & P_WEXIT)
		CLR(prio, PCATCH);

	SCHED_LOCK();

	TRACEPOINT(sched, sleep, NULL);

	p->p_wchan = ident;
	p->p_wmesg = wmesg;
	p->p_slptime = 0;
	p->p_slppri = prio & PRIMASK;
	atomic_setbits_int(&p->p_flag, P_INSCHED);
	TAILQ_INSERT_TAIL(&slpque[LOOKUP(ident)], p, p_runq);
	if (prio & PCATCH)
		atomic_setbits_int(&p->p_flag, P_SINTR);
	p->p_stat = SSLEEP;

	SCHED_UNLOCK();
}

int
sleep_finish(uint64_t nsecs, int do_sleep)
{
	struct proc *p = curproc;
	int catch, error = 0, error1 = 0;

#ifdef DIAGNOSTIC
	if (nsecs == 0) {
		log(LOG_WARNING,
		    "%s: %s[%d]: %s: trying to sleep zero nanoseconds\n",
		    __func__, p->p_p->ps_comm, p->p_p->ps_pid, p->p_wmesg);
	}
#endif

	if (nsecs != INFSLP) {
		KASSERT(!ISSET(p->p_flag, P_TIMEOUT|P_TIMEOUTRAN));
		timeout_add_nsec(&p->p_sleep_to, nsecs);
	}

	catch = p->p_flag & P_SINTR;
	if (catch != 0) {
		if ((error = sleep_signal_check(p, 0)) != 0) {
			catch = 0;
			do_sleep = 0;
		}
	}

	SCHED_LOCK();
	/*
	 * A few checks need to happen before going to sleep:
	 * - If the wakeup happens while going to sleep, p->p_wchan
	 * will be NULL. In that case unwind immediately but still
	 * check for possible signals and timeouts.
	 * - If the sleep is aborted call unsleep and take us of the
	 * sleep queue.
	 * - If requested to stop force a switch even if the sleep
	 * condition got cleared.
	 */
	if (p->p_wchan == NULL)
		do_sleep = 0;
	if (do_sleep == 0)
		unsleep(p);
	if (p->p_stat == SSTOP)
		do_sleep = 1;
	atomic_clearbits_int(&p->p_flag, P_INSCHED);

	if (do_sleep) {
		KASSERT(p->p_stat == SSLEEP || p->p_stat == SSTOP);
		p->p_ru.ru_nvcsw++;
		mi_switch();
	} else {
		KASSERT(p->p_stat == SONPROC || p->p_stat == SSLEEP);
		p->p_stat = SONPROC;
		SCHED_UNLOCK();
	}

#ifdef DIAGNOSTIC
	if (p->p_stat != SONPROC)
		panic("sleep_finish !SONPROC");
#endif

	p->p_cpu->ci_schedstate.spc_curpriority = p->p_usrpri;

	/*
	 * Even though this belongs to the signal handling part of sleep,
	 * we need to clear it before the ktrace.
	 */
	atomic_clearbits_int(&p->p_flag, P_SINTR);

	/*
	 * There are three situations to handle when cancelling the
	 * p_sleep_to timeout:
	 *
	 * 1. The timeout has not fired yet
	 * 2. The timeout is running
	 * 3. The timeout has run
	 *
	 * If timeout_del succeeds then the timeout won't run and
	 * situation 1 is dealt with.
	 *
	 * If timeout_del does not remove the timeout, then we're
	 * handling 2 or 3, but it won't tell us which one. Instead,
	 * the P_TIMEOUTRAN flag is used to figure out when we move
	 * from 2 to 3. endtsleep() (the p_sleep_to handler) sets the
	 * flag when it's finished running, so we spin waiting for
	 * it.
	 *
	 * We spin instead of sleeping because endtsleep() takes
	 * the sched lock to do all it's work. If we wanted to go
	 * to sleep to wait for endtsleep to run, we'd also have to
	 * take the sched lock, so we'd be spinning against it anyway.
	 */
	if (nsecs != INFSLP && !timeout_del(&p->p_sleep_to)) {
		int flag;

		/* Wait for endtsleep timeout to finish running */
		while (!ISSET(flag = atomic_load_int(&p->p_flag), P_TIMEOUTRAN))
			CPU_BUSY_CYCLE();
		atomic_clearbits_int(&p->p_flag, P_TIMEOUT | P_TIMEOUTRAN);

		if (ISSET(flag, P_TIMEOUT))
			error1 = EWOULDBLOCK;
	}

	/*
	 * Check if thread was woken up because of a unwind or signal
	 * but ignore any pending stop condition.
	 */
	if (catch != 0)
		error = sleep_signal_check(p, 1);

	/* Signal errors are higher priority than timeouts. */
	if (error == 0 && error1 != 0)
		error = error1;

	return error;
}

/*
 * Check and handle signals and suspensions around a sleep cycle.
 * The 2nd call in sleep_finish() sets after_sleep = 1. In this case
 * any pending suspend event came in after the wakeup / unsleep and
 * can therefor be ignored. Once the process hits userret the event
 * will be picked up again.
 */
int
sleep_signal_check(struct proc *p, int after_sleep)
{
	struct process *pr = p->p_p;
	struct sigctx ctx;
	int err, sig;

	if ((err = proc_suspend_check(p, 1)) != 0) {
		if (err != EWOULDBLOCK)
			return err;

		/* requested to stop */
		if (!after_sleep) {
			mtx_enter(&pr->ps_mtx);
			process_suspend_signal(pr);

			SCHED_LOCK();
			p->p_stat = SSTOP;
			SCHED_UNLOCK();
			mtx_leave(&pr->ps_mtx);
		}
	}

	if ((sig = cursig(p, &ctx, 1)) != 0) {
		if (ctx.sig_stop) {
			if (!after_sleep) {
				mtx_enter(&pr->ps_mtx);
				pr->ps_xsig = sig;
				/*
				 * This is for stop signals delivered before
				 * sleep_setup() was called. We need to do the
				 * full dance here before going to sleep.
				 */
				atomic_clearbits_int(&p->p_siglist,
				    sigmask(sig));
				atomic_setbits_int(&pr->ps_flags, PS_STOPPING);
				SCHED_LOCK();
				process_stop(pr, P_SUSPSIG, SINGLE_SUSPEND);
				SCHED_UNLOCK();
				atomic_setbits_int(&p->p_flag, P_SUSPSIG);
				process_suspend_signal(pr);
				SCHED_LOCK();
				p->p_stat = SSTOP;
				SCHED_UNLOCK();
				mtx_leave(&pr->ps_mtx);
			}
		} else if (ctx.sig_intr && !ctx.sig_ignore)
			return EINTR;
		else
			return ERESTART;
	}

	return 0;
}

/*
 * If process hasn't been awakened (wchan non-zero), undo the sleep.
 * If proc is stopped, just unsleep so it will remain stopped.
 */
int
wakeup_proc(struct proc *p)
{
	int awakened = 0;

	SCHED_ASSERT_LOCKED();

	if (p->p_wchan != NULL) {
		awakened = 1;
#ifdef DIAGNOSTIC
		if (p->p_stat != SSLEEP && p->p_stat != SSTOP)
			panic("thread %d p_stat is %d", p->p_tid, p->p_stat);
#endif
		unsleep(p);
		if (p->p_stat == SSLEEP)
			setrunnable(p);
	}

	return awakened;
}


/*
 * This is the timeout handler that wakes up procs that only want
 * to sleep for a period of time rather than forever (until they get
 * a wakeup from somewhere else). It is only scheduled and used by
 * sleep_finish(), which coordinates with this handler via the P_TIMEOUT
 * and P_TIMEOUTRAN flags.
 */
void
endtsleep(void *arg)
{
	struct proc *p = arg;
	int awakened;
	int flags;

	SCHED_LOCK();
	awakened = wakeup_proc(p);
	SCHED_UNLOCK();

	flags = P_TIMEOUTRAN;
	if (awakened)
		SET(flags, P_TIMEOUT);

	/* Let sleep_finish() proceed. */
	atomic_setbits_int(&p->p_flag, flags);
	/* Do not alter the proc after this point. */
}

/*
 * Remove a process from its wait queue
 */
void
unsleep(struct proc *p)
{
	SCHED_ASSERT_LOCKED();

	if (p->p_wchan != NULL) {
		TAILQ_REMOVE(&slpque[LOOKUP(p->p_wchan)], p, p_runq);
		p->p_wchan = NULL;
		p->p_wmesg = NULL;
		TRACEPOINT(sched, unsleep, p->p_tid + THREAD_PID_OFFSET,
		    p->p_p->ps_pid);
	}
}

/*
 * Make a number of processes sleeping on the specified identifier runnable.
 */
void
wakeup_n(const volatile void *ident, int n)
{
	struct slpque *qp, wakeq;
	struct proc *p;
	struct proc *pnext;

	TAILQ_INIT(&wakeq);

	SCHED_LOCK();
	qp = &slpque[LOOKUP(ident)];
	for (p = TAILQ_FIRST(qp); p != NULL && n != 0; p = pnext) {
		pnext = TAILQ_NEXT(p, p_runq);
#ifdef DIAGNOSTIC
		if (p->p_stat != SSLEEP && p->p_stat != SSTOP)
			panic("thread %d p_stat is %d", p->p_tid, p->p_stat);
#endif
		KASSERT(p->p_wchan != NULL);
		if (p->p_wchan == ident) {
			TAILQ_REMOVE(qp, p, p_runq);
			p->p_wchan = NULL;
			p->p_wmesg = NULL;
			TAILQ_INSERT_TAIL(&wakeq, p, p_runq);
			--n;
		}
	}
	while ((p = TAILQ_FIRST(&wakeq))) {
		TAILQ_REMOVE(&wakeq, p, p_runq);
		TRACEPOINT(sched, unsleep, p->p_tid + THREAD_PID_OFFSET,
		    p->p_p->ps_pid);
		if (p->p_stat == SSLEEP)
			setrunnable(p);
	}
	SCHED_UNLOCK();
}

/*
 * Make all processes sleeping on the specified identifier runnable.
 */
void
wakeup(const volatile void *chan)
{
	wakeup_n(chan, -1);
}

int
sys_sched_yield(struct proc *p, void *v, register_t *retval)
{
	struct proc *q;
	uint8_t newprio;

	/*
	 * If one of the threads of a multi-threaded process called
	 * sched_yield(2), drop its priority to ensure its siblings
	 * can make some progress.
	 */
	mtx_enter(&p->p_p->ps_mtx);
	newprio = p->p_usrpri;
	TAILQ_FOREACH(q, &p->p_p->ps_threads, p_thr_link)
		newprio = max(newprio, q->p_runpri);
	mtx_leave(&p->p_p->ps_mtx);

	SCHED_LOCK();
	setrunqueue(p->p_cpu, p, newprio);
	p->p_ru.ru_nvcsw++;
	mi_switch();

	return (0);
}

static inline int
thrsleep_unlock(_atomic_lock_t *atomiclock)
{
	static _atomic_lock_t unlocked = _ATOMIC_LOCK_UNLOCKED;

	if (atomiclock == NULL)
		return 0;

	return copyout(&unlocked, atomiclock, sizeof(unlocked));
}

struct tslpentry {
	TAILQ_ENTRY(tslpentry)	 tslp_link;
	struct process		*tslp_ps;
	long			 tslp_ident;
	struct proc *volatile	 tslp_p;
};

TAILQ_HEAD(tslpqueue, tslpentry);

struct tslp_bucket {
	struct tslpqueue	 tsb_list;
	struct mutex		 tsb_lock;
} __aligned(64);

/* thrsleep queue shared between processes */
static struct tslp_bucket tsb_shared;

#define TSLP_BUCKET_BITS	6
#define TSLP_BUCKET_SIZE	(1UL << TSLP_BUCKET_BITS)
#define TSLP_BUCKET_MASK	(TSLP_BUCKET_SIZE - 1)

static struct tslp_bucket tsb_buckets[TSLP_BUCKET_SIZE];

void
tslp_init(void)
{
	struct tslp_bucket *tsb;
	size_t i;

	TAILQ_INIT(&tsb_shared.tsb_list);
	mtx_init(&tsb_shared.tsb_lock, IPL_MPFLOOR);

	for (i = 0; i < nitems(tsb_buckets); i++) {
		tsb = &tsb_buckets[i];

		TAILQ_INIT(&tsb->tsb_list);
		mtx_init(&tsb->tsb_lock, IPL_MPFLOOR);
	}
}

static struct tslp_bucket *
thrsleep_bucket(long ident)
{
	ident >>= 3;
	ident ^= ident >> TSLP_BUCKET_BITS;
	ident &= TSLP_BUCKET_MASK;

	return &tsb_buckets[ident];
} 

static int
thrsleep(struct proc *p, struct sys___thrsleep_args *v)
{
	struct sys___thrsleep_args /* {
		syscallarg(const volatile void *) ident;
		syscallarg(clockid_t) clock_id;
		syscallarg(const struct timespec *) tp;
		syscallarg(void *) lock;
		syscallarg(const int *) abort;
	} */ *uap = v;
	long ident = (long)SCARG(uap, ident);
	struct tslpentry entry;
	struct tslp_bucket *tsb;
	struct timespec *tsp = (struct timespec *)SCARG(uap, tp);
	void *lock = SCARG(uap, lock);
	const uint32_t *abortp = SCARG(uap, abort);
	clockid_t clock_id = SCARG(uap, clock_id);
	uint64_t nsecs = INFSLP;
	int error = 0;

	if (ident == 0)
		return (EINVAL);
	if (tsp != NULL) {
		struct timespec now;

		if ((error = clock_gettime(p, clock_id, &now)))
			return (error);
#ifdef KTRACE
		if (KTRPOINT(p, KTR_STRUCT))
			ktrabstimespec(p, tsp);
#endif

		if (timespeccmp(tsp, &now, <=)) {
			/* already passed: still do the unlock */
			if ((error = thrsleep_unlock(lock)))
				return (error);
			return (EWOULDBLOCK);
		}

		timespecsub(tsp, &now, tsp);
		nsecs = MAX(1, MIN(TIMESPEC_TO_NSEC(tsp), MAXTSLP));
	}

	tsb = (ident == -1) ? &tsb_shared : thrsleep_bucket(ident);

	/* Interlock with wakeup. */
	entry.tslp_ps = p->p_p;
	entry.tslp_ident = ident;
	entry.tslp_p = p;

	mtx_enter(&tsb->tsb_lock);
	TAILQ_INSERT_TAIL(&tsb->tsb_list, &entry, tslp_link);
	mtx_leave(&tsb->tsb_lock);

	error = thrsleep_unlock(lock);
	if (error != 0)
		goto leave;

	if (abortp != NULL) {
		uint32_t abort;
		error = copyin32(abortp, &abort);
		if (error != 0)
			goto leave;
		if (abort) {
			error = EINTR;
			goto leave;
		}
	}

	sleep_setup(&entry, PWAIT|PCATCH, "thrsleep");
	error = sleep_finish(nsecs, entry.tslp_p != NULL);
	if (error != 0 || entry.tslp_p != NULL) {
		mtx_enter(&tsb->tsb_lock);
		if (entry.tslp_p != NULL)
			TAILQ_REMOVE(&tsb->tsb_list, &entry, tslp_link);
		else
			error = 0;
		mtx_leave(&tsb->tsb_lock);

		if (error == ERESTART)
			error = ECANCELED;
	}

	return (error);

leave:
	if (entry.tslp_p != NULL) {
		mtx_enter(&tsb->tsb_lock);
		if (entry.tslp_p != NULL)
			TAILQ_REMOVE(&tsb->tsb_list, &entry, tslp_link);
		mtx_leave(&tsb->tsb_lock);
	}

	return (error);
}

int
sys___thrsleep(struct proc *p, void *v, register_t *retval)
{
	struct sys___thrsleep_args /* {
		syscallarg(const volatile void *) ident;
		syscallarg(clockid_t) clock_id;
		syscallarg(struct timespec *) tp;
		syscallarg(void *) lock;
		syscallarg(const int *) abort;
	} */ *uap = v;
	struct timespec ts;
	int error;

	if (SCARG(uap, tp) != NULL) {
		if ((error = copyin(SCARG(uap, tp), &ts, sizeof(ts)))) {
			*retval = error;
			return 0;
		}
		if (!timespecisvalid(&ts)) {
			*retval = EINVAL;
			return 0;
		}
		SCARG(uap, tp) = &ts;
	}

	*retval = thrsleep(p, uap);
	return 0;
}

static void
tslp_wakeups(struct tslpqueue *tslpq)
{
	struct tslpentry *entry, *nentry;
	struct proc *p;

	SCHED_LOCK();
	TAILQ_FOREACH_SAFE(entry, tslpq, tslp_link, nentry) {
		p = entry->tslp_p;
		entry->tslp_p = NULL;
		wakeup_proc(p);
	}
	SCHED_UNLOCK();
}

int
sys___thrwakeup(struct proc *p, void *v, register_t *retval)
{
	struct sys___thrwakeup_args /* {
		syscallarg(const volatile void *) ident;
		syscallarg(int) n;
	} */ *uap = v;
	struct tslpentry *entry, *nentry;
	struct tslp_bucket *tsb;
	long ident = (long)SCARG(uap, ident);
	int n = SCARG(uap, n);
	int found = 0;
	struct tslpqueue wq = TAILQ_HEAD_INITIALIZER(wq);

	if (ident == 0) {
		*retval = EINVAL;
		return (0);
	}

	if (ident == -1) {
		/*
		 * Wake up all waiters with ident -1. This is needed
		 * because ident -1 can be shared by multiple userspace
		 * lock state machines concurrently. The implementation
		 * has no way to direct the wakeup to a particular
		 * state machine.
		 */
		mtx_enter(&tsb_shared.tsb_lock);
		tslp_wakeups(&tsb_shared.tsb_list);
		TAILQ_INIT(&tsb_shared.tsb_list);
		mtx_leave(&tsb_shared.tsb_lock);

		*retval = 0;
		return (0);
	}

	tsb = thrsleep_bucket(ident);

	mtx_enter(&tsb->tsb_lock);
	TAILQ_FOREACH_SAFE(entry, &tsb->tsb_list, tslp_link, nentry) {
		if (entry->tslp_ident == ident && entry->tslp_ps == p->p_p) {
			TAILQ_REMOVE(&tsb->tsb_list, entry, tslp_link);
			TAILQ_INSERT_TAIL(&wq, entry, tslp_link);

			if (++found == n)
				break;
		}
	}

	if (found)
		tslp_wakeups(&wq);
	mtx_leave(&tsb->tsb_lock);

	*retval = found ? 0 : ESRCH;
	return (0);
}

void
refcnt_init(struct refcnt *r)
{
	refcnt_init_trace(r, 0);
}

void
refcnt_init_trace(struct refcnt *r, int trace)
{
	r->r_traceidx = trace;
	atomic_store_int(&r->r_refs, 1);
	TRACEINDEX(refcnt, r->r_traceidx, r, 0, +1);
}

void
refcnt_take(struct refcnt *r)
{
	u_int refs;

	refs = atomic_inc_int_nv(&r->r_refs);
	KASSERT(refs > 1);
	TRACEINDEX(refcnt, r->r_traceidx, r, refs - 1, +1);
	(void)refs;
}

int
refcnt_rele(struct refcnt *r)
{
	u_int refs;

	membar_exit_before_atomic();
	refs = atomic_dec_int_nv(&r->r_refs);
	KASSERT(refs != ~0);
	TRACEINDEX(refcnt, r->r_traceidx, r, refs + 1, -1);
	if (refs == 0) {
		membar_enter_after_atomic();
		return (1);
	}
	return (0);
}

void
refcnt_rele_wake(struct refcnt *r)
{
	if (refcnt_rele(r))
		wakeup_one(r);
}

void
refcnt_finalize(struct refcnt *r, const char *wmesg)
{
	u_int refs;

	membar_exit_before_atomic();
	refs = atomic_dec_int_nv(&r->r_refs);
	KASSERT(refs != ~0);
	TRACEINDEX(refcnt, r->r_traceidx, r, refs + 1, -1);
	while (refs) {
		sleep_setup(r, PWAIT, wmesg);
		refs = atomic_load_int(&r->r_refs);
		sleep_finish(INFSLP, refs);
	}
	TRACEINDEX(refcnt, r->r_traceidx, r, refs, 0);
	/* Order subsequent loads and stores after refs == 0 load. */
	membar_sync();
}

unsigned int
refcnt_read(const struct refcnt *r)
{
	u_int refs;

	refs = atomic_load_int(&r->r_refs);
	TRACEINDEX(refcnt, r->r_traceidx, r, refs, 0);
	return (refs);
}

void
cond_init(struct cond *c)
{
	atomic_store_int(&c->c_wait, 1);
}

void
cond_signal_handler(void *arg)
{
	struct cond *c = arg;

	atomic_store_int(&c->c_wait, 0);

	wakeup_one(c);
}

void
cond_wait(struct cond *c, const char *wmesg)
{
	unsigned int wait;

	wait = atomic_load_int(&c->c_wait);
	while (wait) {
		sleep_setup(c, PWAIT, wmesg);
		wait = atomic_load_int(&c->c_wait);
		sleep_finish(INFSLP, wait);
	}
}
