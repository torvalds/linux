/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder <jake@FreeBSD.org>
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

#include "opt_sched.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>

#include <machine/cpu.h>

/* Uncomment this to enable logging of critical_enter/exit. */
#if 0
#define	KTR_CRITICAL	KTR_SCHED
#else
#define	KTR_CRITICAL	0
#endif

#ifdef FULL_PREEMPTION
#ifndef PREEMPTION
#error "The FULL_PREEMPTION option requires the PREEMPTION option"
#endif
#endif

CTASSERT((RQB_BPW * RQB_LEN) == RQ_NQS);

/*
 * kern.sched.preemption allows user space to determine if preemption support
 * is compiled in or not.  It is not currently a boot or runtime flag that
 * can be changed.
 */
#ifdef PREEMPTION
static int kern_sched_preemption = 1;
#else
static int kern_sched_preemption = 0;
#endif
SYSCTL_INT(_kern_sched, OID_AUTO, preemption, CTLFLAG_RD,
    &kern_sched_preemption, 0, "Kernel preemption enabled");

/*
 * Support for scheduler stats exported via kern.sched.stats.  All stats may
 * be reset with kern.sched.stats.reset = 1.  Stats may be defined elsewhere
 * with SCHED_STAT_DEFINE().
 */
#ifdef SCHED_STATS
SYSCTL_NODE(_kern_sched, OID_AUTO, stats, CTLFLAG_RW, 0, "switch stats");

/* Switch reasons from mi_switch(). */
DPCPU_DEFINE(long, sched_switch_stats[SWT_COUNT]);
SCHED_STAT_DEFINE_VAR(uncategorized,
    &DPCPU_NAME(sched_switch_stats[SWT_NONE]), "");
SCHED_STAT_DEFINE_VAR(preempt,
    &DPCPU_NAME(sched_switch_stats[SWT_PREEMPT]), "");
SCHED_STAT_DEFINE_VAR(owepreempt,
    &DPCPU_NAME(sched_switch_stats[SWT_OWEPREEMPT]), "");
SCHED_STAT_DEFINE_VAR(turnstile,
    &DPCPU_NAME(sched_switch_stats[SWT_TURNSTILE]), "");
SCHED_STAT_DEFINE_VAR(sleepq,
    &DPCPU_NAME(sched_switch_stats[SWT_SLEEPQ]), "");
SCHED_STAT_DEFINE_VAR(sleepqtimo,
    &DPCPU_NAME(sched_switch_stats[SWT_SLEEPQTIMO]), "");
SCHED_STAT_DEFINE_VAR(relinquish, 
    &DPCPU_NAME(sched_switch_stats[SWT_RELINQUISH]), "");
SCHED_STAT_DEFINE_VAR(needresched,
    &DPCPU_NAME(sched_switch_stats[SWT_NEEDRESCHED]), "");
SCHED_STAT_DEFINE_VAR(idle, 
    &DPCPU_NAME(sched_switch_stats[SWT_IDLE]), "");
SCHED_STAT_DEFINE_VAR(iwait,
    &DPCPU_NAME(sched_switch_stats[SWT_IWAIT]), "");
SCHED_STAT_DEFINE_VAR(suspend,
    &DPCPU_NAME(sched_switch_stats[SWT_SUSPEND]), "");
SCHED_STAT_DEFINE_VAR(remotepreempt,
    &DPCPU_NAME(sched_switch_stats[SWT_REMOTEPREEMPT]), "");
SCHED_STAT_DEFINE_VAR(remotewakeidle,
    &DPCPU_NAME(sched_switch_stats[SWT_REMOTEWAKEIDLE]), "");

static int
sysctl_stats_reset(SYSCTL_HANDLER_ARGS)
{
	struct sysctl_oid *p;
	uintptr_t counter;
        int error;
	int val;
	int i;

        val = 0;
        error = sysctl_handle_int(oidp, &val, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);
        if (val == 0)
                return (0);
	/*
	 * Traverse the list of children of _kern_sched_stats and reset each
	 * to 0.  Skip the reset entry.
	 */
	SLIST_FOREACH(p, oidp->oid_parent, oid_link) {
		if (p == oidp || p->oid_arg1 == NULL)
			continue;
		counter = (uintptr_t)p->oid_arg1;
		CPU_FOREACH(i) {
			*(long *)(dpcpu_off[i] + counter) = 0;
		}
	}
	return (0);
}

SYSCTL_PROC(_kern_sched_stats, OID_AUTO, reset, CTLTYPE_INT | CTLFLAG_WR, NULL,
    0, sysctl_stats_reset, "I", "Reset scheduler statistics");
#endif

/************************************************************************
 * Functions that manipulate runnability from a thread perspective.	*
 ************************************************************************/
/*
 * Select the thread that will be run next.
 */

static __noinline struct thread *
choosethread_panic(struct thread *td)
{

	/*
	 * If we are in panic, only allow system threads,
	 * plus the one we are running in, to be run.
	 */
retry:
	if (((td->td_proc->p_flag & P_SYSTEM) == 0 &&
	    (td->td_flags & TDF_INPANIC) == 0)) {
		/* note that it is no longer on the run queue */
		TD_SET_CAN_RUN(td);
		td = sched_choose();
		goto retry;
	}

	TD_SET_RUNNING(td);
	return (td);
}

struct thread *
choosethread(void)
{
	struct thread *td;

	td = sched_choose();

	if (__predict_false(panicstr != NULL))
		return (choosethread_panic(td));

	TD_SET_RUNNING(td);
	return (td);
}

/*
 * Kernel thread preemption implementation.  Critical sections mark
 * regions of code in which preemptions are not allowed.
 *
 * It might seem a good idea to inline critical_enter() but, in order
 * to prevent instructions reordering by the compiler, a __compiler_membar()
 * would have to be used here (the same as sched_pin()).  The performance
 * penalty imposed by the membar could, then, produce slower code than
 * the function call itself, for most cases.
 */
void
critical_enter_KBI(void)
{
#ifdef KTR
	struct thread *td = curthread;
#endif
	critical_enter();
	CTR4(KTR_CRITICAL, "critical_enter by thread %p (%ld, %s) to %d", td,
	    (long)td->td_proc->p_pid, td->td_name, td->td_critnest);
}

void __noinline
critical_exit_preempt(void)
{
	struct thread *td;
	int flags;

	/*
	 * If td_critnest is 0, it is possible that we are going to get
	 * preempted again before reaching the code below. This happens
	 * rarely and is harmless. However, this means td_owepreempt may
	 * now be unset.
	 */
	td = curthread;
	if (td->td_critnest != 0)
		return;
	if (kdb_active)
		return;

	/*
	 * Microoptimization: we committed to switch,
	 * disable preemption in interrupt handlers
	 * while spinning for the thread lock.
	 */
	td->td_critnest = 1;
	thread_lock(td);
	td->td_critnest--;
	flags = SW_INVOL | SW_PREEMPT;
	if (TD_IS_IDLETHREAD(td))
		flags |= SWT_IDLE;
	else
		flags |= SWT_OWEPREEMPT;
	mi_switch(flags, NULL);
	thread_unlock(td);
}

void
critical_exit_KBI(void)
{
#ifdef KTR
	struct thread *td = curthread;
#endif
	critical_exit();
	CTR4(KTR_CRITICAL, "critical_exit by thread %p (%ld, %s) to %d", td,
	    (long)td->td_proc->p_pid, td->td_name, td->td_critnest);
}

/************************************************************************
 * SYSTEM RUN QUEUE manipulations and tests				*
 ************************************************************************/
/*
 * Initialize a run structure.
 */
void
runq_init(struct runq *rq)
{
	int i;

	bzero(rq, sizeof *rq);
	for (i = 0; i < RQ_NQS; i++)
		TAILQ_INIT(&rq->rq_queues[i]);
}

/*
 * Clear the status bit of the queue corresponding to priority level pri,
 * indicating that it is empty.
 */
static __inline void
runq_clrbit(struct runq *rq, int pri)
{
	struct rqbits *rqb;

	rqb = &rq->rq_status;
	CTR4(KTR_RUNQ, "runq_clrbit: bits=%#x %#x bit=%#x word=%d",
	    rqb->rqb_bits[RQB_WORD(pri)],
	    rqb->rqb_bits[RQB_WORD(pri)] & ~RQB_BIT(pri),
	    RQB_BIT(pri), RQB_WORD(pri));
	rqb->rqb_bits[RQB_WORD(pri)] &= ~RQB_BIT(pri);
}

/*
 * Find the index of the first non-empty run queue.  This is done by
 * scanning the status bits, a set bit indicates a non-empty queue.
 */
static __inline int
runq_findbit(struct runq *rq)
{
	struct rqbits *rqb;
	int pri;
	int i;

	rqb = &rq->rq_status;
	for (i = 0; i < RQB_LEN; i++)
		if (rqb->rqb_bits[i]) {
			pri = RQB_FFS(rqb->rqb_bits[i]) + (i << RQB_L2BPW);
			CTR3(KTR_RUNQ, "runq_findbit: bits=%#x i=%d pri=%d",
			    rqb->rqb_bits[i], i, pri);
			return (pri);
		}

	return (-1);
}

static __inline int
runq_findbit_from(struct runq *rq, u_char pri)
{
	struct rqbits *rqb;
	rqb_word_t mask;
	int i;

	/*
	 * Set the mask for the first word so we ignore priorities before 'pri'.
	 */
	mask = (rqb_word_t)-1 << (pri & (RQB_BPW - 1));
	rqb = &rq->rq_status;
again:
	for (i = RQB_WORD(pri); i < RQB_LEN; mask = -1, i++) {
		mask = rqb->rqb_bits[i] & mask;
		if (mask == 0)
			continue;
		pri = RQB_FFS(mask) + (i << RQB_L2BPW);
		CTR3(KTR_RUNQ, "runq_findbit_from: bits=%#x i=%d pri=%d",
		    mask, i, pri);
		return (pri);
	}
	if (pri == 0)
		return (-1);
	/*
	 * Wrap back around to the beginning of the list just once so we
	 * scan the whole thing.
	 */
	pri = 0;
	goto again;
}

/*
 * Set the status bit of the queue corresponding to priority level pri,
 * indicating that it is non-empty.
 */
static __inline void
runq_setbit(struct runq *rq, int pri)
{
	struct rqbits *rqb;

	rqb = &rq->rq_status;
	CTR4(KTR_RUNQ, "runq_setbit: bits=%#x %#x bit=%#x word=%d",
	    rqb->rqb_bits[RQB_WORD(pri)],
	    rqb->rqb_bits[RQB_WORD(pri)] | RQB_BIT(pri),
	    RQB_BIT(pri), RQB_WORD(pri));
	rqb->rqb_bits[RQB_WORD(pri)] |= RQB_BIT(pri);
}

/*
 * Add the thread to the queue specified by its priority, and set the
 * corresponding status bit.
 */
void
runq_add(struct runq *rq, struct thread *td, int flags)
{
	struct rqhead *rqh;
	int pri;

	pri = td->td_priority / RQ_PPQ;
	td->td_rqindex = pri;
	runq_setbit(rq, pri);
	rqh = &rq->rq_queues[pri];
	CTR4(KTR_RUNQ, "runq_add: td=%p pri=%d %d rqh=%p",
	    td, td->td_priority, pri, rqh);
	if (flags & SRQ_PREEMPTED) {
		TAILQ_INSERT_HEAD(rqh, td, td_runq);
	} else {
		TAILQ_INSERT_TAIL(rqh, td, td_runq);
	}
}

void
runq_add_pri(struct runq *rq, struct thread *td, u_char pri, int flags)
{
	struct rqhead *rqh;

	KASSERT(pri < RQ_NQS, ("runq_add_pri: %d out of range", pri));
	td->td_rqindex = pri;
	runq_setbit(rq, pri);
	rqh = &rq->rq_queues[pri];
	CTR4(KTR_RUNQ, "runq_add_pri: td=%p pri=%d idx=%d rqh=%p",
	    td, td->td_priority, pri, rqh);
	if (flags & SRQ_PREEMPTED) {
		TAILQ_INSERT_HEAD(rqh, td, td_runq);
	} else {
		TAILQ_INSERT_TAIL(rqh, td, td_runq);
	}
}
/*
 * Return true if there are runnable processes of any priority on the run
 * queue, false otherwise.  Has no side effects, does not modify the run
 * queue structure.
 */
int
runq_check(struct runq *rq)
{
	struct rqbits *rqb;
	int i;

	rqb = &rq->rq_status;
	for (i = 0; i < RQB_LEN; i++)
		if (rqb->rqb_bits[i]) {
			CTR2(KTR_RUNQ, "runq_check: bits=%#x i=%d",
			    rqb->rqb_bits[i], i);
			return (1);
		}
	CTR0(KTR_RUNQ, "runq_check: empty");

	return (0);
}

/*
 * Find the highest priority process on the run queue.
 */
struct thread *
runq_choose_fuzz(struct runq *rq, int fuzz)
{
	struct rqhead *rqh;
	struct thread *td;
	int pri;

	while ((pri = runq_findbit(rq)) != -1) {
		rqh = &rq->rq_queues[pri];
		/* fuzz == 1 is normal.. 0 or less are ignored */
		if (fuzz > 1) {
			/*
			 * In the first couple of entries, check if
			 * there is one for our CPU as a preference.
			 */
			int count = fuzz;
			int cpu = PCPU_GET(cpuid);
			struct thread *td2;
			td2 = td = TAILQ_FIRST(rqh);

			while (count-- && td2) {
				if (td2->td_lastcpu == cpu) {
					td = td2;
					break;
				}
				td2 = TAILQ_NEXT(td2, td_runq);
			}
		} else
			td = TAILQ_FIRST(rqh);
		KASSERT(td != NULL, ("runq_choose_fuzz: no proc on busy queue"));
		CTR3(KTR_RUNQ,
		    "runq_choose_fuzz: pri=%d thread=%p rqh=%p", pri, td, rqh);
		return (td);
	}
	CTR1(KTR_RUNQ, "runq_choose_fuzz: idleproc pri=%d", pri);

	return (NULL);
}

/*
 * Find the highest priority process on the run queue.
 */
struct thread *
runq_choose(struct runq *rq)
{
	struct rqhead *rqh;
	struct thread *td;
	int pri;

	while ((pri = runq_findbit(rq)) != -1) {
		rqh = &rq->rq_queues[pri];
		td = TAILQ_FIRST(rqh);
		KASSERT(td != NULL, ("runq_choose: no thread on busy queue"));
		CTR3(KTR_RUNQ,
		    "runq_choose: pri=%d thread=%p rqh=%p", pri, td, rqh);
		return (td);
	}
	CTR1(KTR_RUNQ, "runq_choose: idlethread pri=%d", pri);

	return (NULL);
}

struct thread *
runq_choose_from(struct runq *rq, u_char idx)
{
	struct rqhead *rqh;
	struct thread *td;
	int pri;

	if ((pri = runq_findbit_from(rq, idx)) != -1) {
		rqh = &rq->rq_queues[pri];
		td = TAILQ_FIRST(rqh);
		KASSERT(td != NULL, ("runq_choose: no thread on busy queue"));
		CTR4(KTR_RUNQ,
		    "runq_choose_from: pri=%d thread=%p idx=%d rqh=%p",
		    pri, td, td->td_rqindex, rqh);
		return (td);
	}
	CTR1(KTR_RUNQ, "runq_choose_from: idlethread pri=%d", pri);

	return (NULL);
}
/*
 * Remove the thread from the queue specified by its priority, and clear the
 * corresponding status bit if the queue becomes empty.
 * Caller must set state afterwards.
 */
void
runq_remove(struct runq *rq, struct thread *td)
{

	runq_remove_idx(rq, td, NULL);
}

void
runq_remove_idx(struct runq *rq, struct thread *td, u_char *idx)
{
	struct rqhead *rqh;
	u_char pri;

	KASSERT(td->td_flags & TDF_INMEM,
		("runq_remove_idx: thread swapped out"));
	pri = td->td_rqindex;
	KASSERT(pri < RQ_NQS, ("runq_remove_idx: Invalid index %d\n", pri));
	rqh = &rq->rq_queues[pri];
	CTR4(KTR_RUNQ, "runq_remove_idx: td=%p, pri=%d %d rqh=%p",
	    td, td->td_priority, pri, rqh);
	TAILQ_REMOVE(rqh, td, td_runq);
	if (TAILQ_EMPTY(rqh)) {
		CTR0(KTR_RUNQ, "runq_remove_idx: empty");
		runq_clrbit(rq, pri);
		if (idx != NULL && *idx == pri)
			*idx = (pri + 1) % RQ_NQS;
	}
}
