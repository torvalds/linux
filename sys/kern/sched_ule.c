/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2007, Jeffrey Roberson <jeff@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

/*
 * This file implements the ULE scheduler.  ULE supports independent CPU
 * run queues and fine grain locking.  It has superior interactive
 * performance under load even on uni-processor systems.
 *
 * etymology:
 *   ULE is the last three letters in schedule.  It owes its name to a
 * generic user created for a scheduling system by Paul Mikesell at
 * Isilon Systems and a general lack of creativity on the part of the author.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_hwpmc_hooks.h"
#include "opt_sched.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/sched.h>
#include <sys/sdt.h>
#include <sys/smp.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/turnstile.h>
#include <sys/umtx.h>
#include <sys/vmmeter.h>
#include <sys/cpuset.h>
#include <sys/sbuf.h>

#ifdef HWPMC_HOOKS
#include <sys/pmckern.h>
#endif

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>
int				dtrace_vtime_active;
dtrace_vtime_switch_func_t	dtrace_vtime_switch_func;
#endif

#include <machine/cpu.h>
#include <machine/smp.h>

#define	KTR_ULE	0

#define	TS_NAME_LEN (MAXCOMLEN + sizeof(" td ") + sizeof(__XSTRING(UINT_MAX)))
#define	TDQ_NAME_LEN	(sizeof("sched lock ") + sizeof(__XSTRING(MAXCPU)))
#define	TDQ_LOADNAME_LEN	(sizeof("CPU ") + sizeof(__XSTRING(MAXCPU)) - 1 + sizeof(" load"))

/*
 * Thread scheduler specific section.  All fields are protected
 * by the thread lock.
 */
struct td_sched {	
	struct runq	*ts_runq;	/* Run-queue we're queued on. */
	short		ts_flags;	/* TSF_* flags. */
	int		ts_cpu;		/* CPU that we have affinity for. */
	int		ts_rltick;	/* Real last tick, for affinity. */
	int		ts_slice;	/* Ticks of slice remaining. */
	u_int		ts_slptime;	/* Number of ticks we vol. slept */
	u_int		ts_runtime;	/* Number of ticks we were running */
	int		ts_ltick;	/* Last tick that we were running on */
	int		ts_ftick;	/* First tick that we were running on */
	int		ts_ticks;	/* Tick count */
#ifdef KTR
	char		ts_name[TS_NAME_LEN];
#endif
};
/* flags kept in ts_flags */
#define	TSF_BOUND	0x0001		/* Thread can not migrate. */
#define	TSF_XFERABLE	0x0002		/* Thread was added as transferable. */

#define	THREAD_CAN_MIGRATE(td)	((td)->td_pinned == 0)
#define	THREAD_CAN_SCHED(td, cpu)	\
    CPU_ISSET((cpu), &(td)->td_cpuset->cs_mask)

_Static_assert(sizeof(struct thread) + sizeof(struct td_sched) <=
    sizeof(struct thread0_storage),
    "increase struct thread0_storage.t0st_sched size");

/*
 * Priority ranges used for interactive and non-interactive timeshare
 * threads.  The timeshare priorities are split up into four ranges.
 * The first range handles interactive threads.  The last three ranges
 * (NHALF, x, and NHALF) handle non-interactive threads with the outer
 * ranges supporting nice values.
 */
#define	PRI_TIMESHARE_RANGE	(PRI_MAX_TIMESHARE - PRI_MIN_TIMESHARE + 1)
#define	PRI_INTERACT_RANGE	((PRI_TIMESHARE_RANGE - SCHED_PRI_NRESV) / 2)
#define	PRI_BATCH_RANGE		(PRI_TIMESHARE_RANGE - PRI_INTERACT_RANGE)

#define	PRI_MIN_INTERACT	PRI_MIN_TIMESHARE
#define	PRI_MAX_INTERACT	(PRI_MIN_TIMESHARE + PRI_INTERACT_RANGE - 1)
#define	PRI_MIN_BATCH		(PRI_MIN_TIMESHARE + PRI_INTERACT_RANGE)
#define	PRI_MAX_BATCH		PRI_MAX_TIMESHARE

/*
 * Cpu percentage computation macros and defines.
 *
 * SCHED_TICK_SECS:	Number of seconds to average the cpu usage across.
 * SCHED_TICK_TARG:	Number of hz ticks to average the cpu usage across.
 * SCHED_TICK_MAX:	Maximum number of ticks before scaling back.
 * SCHED_TICK_SHIFT:	Shift factor to avoid rounding away results.
 * SCHED_TICK_HZ:	Compute the number of hz ticks for a given ticks count.
 * SCHED_TICK_TOTAL:	Gives the amount of time we've been recording ticks.
 */
#define	SCHED_TICK_SECS		10
#define	SCHED_TICK_TARG		(hz * SCHED_TICK_SECS)
#define	SCHED_TICK_MAX		(SCHED_TICK_TARG + hz)
#define	SCHED_TICK_SHIFT	10
#define	SCHED_TICK_HZ(ts)	((ts)->ts_ticks >> SCHED_TICK_SHIFT)
#define	SCHED_TICK_TOTAL(ts)	(max((ts)->ts_ltick - (ts)->ts_ftick, hz))

/*
 * These macros determine priorities for non-interactive threads.  They are
 * assigned a priority based on their recent cpu utilization as expressed
 * by the ratio of ticks to the tick total.  NHALF priorities at the start
 * and end of the MIN to MAX timeshare range are only reachable with negative
 * or positive nice respectively.
 *
 * PRI_RANGE:	Priority range for utilization dependent priorities.
 * PRI_NRESV:	Number of nice values.
 * PRI_TICKS:	Compute a priority in PRI_RANGE from the ticks count and total.
 * PRI_NICE:	Determines the part of the priority inherited from nice.
 */
#define	SCHED_PRI_NRESV		(PRIO_MAX - PRIO_MIN)
#define	SCHED_PRI_NHALF		(SCHED_PRI_NRESV / 2)
#define	SCHED_PRI_MIN		(PRI_MIN_BATCH + SCHED_PRI_NHALF)
#define	SCHED_PRI_MAX		(PRI_MAX_BATCH - SCHED_PRI_NHALF)
#define	SCHED_PRI_RANGE		(SCHED_PRI_MAX - SCHED_PRI_MIN + 1)
#define	SCHED_PRI_TICKS(ts)						\
    (SCHED_TICK_HZ((ts)) /						\
    (roundup(SCHED_TICK_TOTAL((ts)), SCHED_PRI_RANGE) / SCHED_PRI_RANGE))
#define	SCHED_PRI_NICE(nice)	(nice)

/*
 * These determine the interactivity of a process.  Interactivity differs from
 * cpu utilization in that it expresses the voluntary time slept vs time ran
 * while cpu utilization includes all time not running.  This more accurately
 * models the intent of the thread.
 *
 * SLP_RUN_MAX:	Maximum amount of sleep time + run time we'll accumulate
 *		before throttling back.
 * SLP_RUN_FORK:	Maximum slp+run time to inherit at fork time.
 * INTERACT_MAX:	Maximum interactivity value.  Smaller is better.
 * INTERACT_THRESH:	Threshold for placement on the current runq.
 */
#define	SCHED_SLP_RUN_MAX	((hz * 5) << SCHED_TICK_SHIFT)
#define	SCHED_SLP_RUN_FORK	((hz / 2) << SCHED_TICK_SHIFT)
#define	SCHED_INTERACT_MAX	(100)
#define	SCHED_INTERACT_HALF	(SCHED_INTERACT_MAX / 2)
#define	SCHED_INTERACT_THRESH	(30)

/*
 * These parameters determine the slice behavior for batch work.
 */
#define	SCHED_SLICE_DEFAULT_DIVISOR	10	/* ~94 ms, 12 stathz ticks. */
#define	SCHED_SLICE_MIN_DIVISOR		6	/* DEFAULT/MIN = ~16 ms. */

/* Flags kept in td_flags. */
#define	TDF_SLICEEND	TDF_SCHED2	/* Thread time slice is over. */

/*
 * tickincr:		Converts a stathz tick into a hz domain scaled by
 *			the shift factor.  Without the shift the error rate
 *			due to rounding would be unacceptably high.
 * realstathz:		stathz is sometimes 0 and run off of hz.
 * sched_slice:		Runtime of each thread before rescheduling.
 * preempt_thresh:	Priority threshold for preemption and remote IPIs.
 */
static int sched_interact = SCHED_INTERACT_THRESH;
static int tickincr = 8 << SCHED_TICK_SHIFT;
static int realstathz = 127;	/* reset during boot. */
static int sched_slice = 10;	/* reset during boot. */
static int sched_slice_min = 1;	/* reset during boot. */
#ifdef PREEMPTION
#ifdef FULL_PREEMPTION
static int preempt_thresh = PRI_MAX_IDLE;
#else
static int preempt_thresh = PRI_MIN_KERN;
#endif
#else 
static int preempt_thresh = 0;
#endif
static int static_boost = PRI_MIN_BATCH;
static int sched_idlespins = 10000;
static int sched_idlespinthresh = -1;

/*
 * tdq - per processor runqs and statistics.  All fields are protected by the
 * tdq_lock.  The load and lowpri may be accessed without to avoid excess
 * locking in sched_pickcpu();
 */
struct tdq {
	/* 
	 * Ordered to improve efficiency of cpu_search() and switch().
	 * tdq_lock is padded to avoid false sharing with tdq_load and
	 * tdq_cpu_idle.
	 */
	struct mtx_padalign tdq_lock;		/* run queue lock. */
	struct cpu_group *tdq_cg;		/* Pointer to cpu topology. */
	volatile int	tdq_load;		/* Aggregate load. */
	volatile int	tdq_cpu_idle;		/* cpu_idle() is active. */
	int		tdq_sysload;		/* For loadavg, !ITHD load. */
	volatile int	tdq_transferable;	/* Transferable thread count. */
	volatile short	tdq_switchcnt;		/* Switches this tick. */
	volatile short	tdq_oldswitchcnt;	/* Switches last tick. */
	u_char		tdq_lowpri;		/* Lowest priority thread. */
	u_char		tdq_ipipending;		/* IPI pending. */
	u_char		tdq_idx;		/* Current insert index. */
	u_char		tdq_ridx;		/* Current removal index. */
	struct runq	tdq_realtime;		/* real-time run queue. */
	struct runq	tdq_timeshare;		/* timeshare run queue. */
	struct runq	tdq_idle;		/* Queue of IDLE threads. */
	char		tdq_name[TDQ_NAME_LEN];
#ifdef KTR
	char		tdq_loadname[TDQ_LOADNAME_LEN];
#endif
} __aligned(64);

/* Idle thread states and config. */
#define	TDQ_RUNNING	1
#define	TDQ_IDLE	2

#ifdef SMP
struct cpu_group *cpu_top;		/* CPU topology */

#define	SCHED_AFFINITY_DEFAULT	(max(1, hz / 1000))
#define	SCHED_AFFINITY(ts, t)	((ts)->ts_rltick > ticks - ((t) * affinity))

/*
 * Run-time tunables.
 */
static int rebalance = 1;
static int balance_interval = 128;	/* Default set in sched_initticks(). */
static int affinity;
static int steal_idle = 1;
static int steal_thresh = 2;
static int always_steal = 0;
static int trysteal_limit = 2;

/*
 * One thread queue per processor.
 */
static struct tdq	tdq_cpu[MAXCPU];
static struct tdq	*balance_tdq;
static int balance_ticks;
DPCPU_DEFINE_STATIC(uint32_t, randomval);

#define	TDQ_SELF()	(&tdq_cpu[PCPU_GET(cpuid)])
#define	TDQ_CPU(x)	(&tdq_cpu[(x)])
#define	TDQ_ID(x)	((int)((x) - tdq_cpu))
#else	/* !SMP */
static struct tdq	tdq_cpu;

#define	TDQ_ID(x)	(0)
#define	TDQ_SELF()	(&tdq_cpu)
#define	TDQ_CPU(x)	(&tdq_cpu)
#endif

#define	TDQ_LOCK_ASSERT(t, type)	mtx_assert(TDQ_LOCKPTR((t)), (type))
#define	TDQ_LOCK(t)		mtx_lock_spin(TDQ_LOCKPTR((t)))
#define	TDQ_LOCK_FLAGS(t, f)	mtx_lock_spin_flags(TDQ_LOCKPTR((t)), (f))
#define	TDQ_UNLOCK(t)		mtx_unlock_spin(TDQ_LOCKPTR((t)))
#define	TDQ_LOCKPTR(t)		((struct mtx *)(&(t)->tdq_lock))

static void sched_priority(struct thread *);
static void sched_thread_priority(struct thread *, u_char);
static int sched_interact_score(struct thread *);
static void sched_interact_update(struct thread *);
static void sched_interact_fork(struct thread *);
static void sched_pctcpu_update(struct td_sched *, int);

/* Operations on per processor queues */
static struct thread *tdq_choose(struct tdq *);
static void tdq_setup(struct tdq *);
static void tdq_load_add(struct tdq *, struct thread *);
static void tdq_load_rem(struct tdq *, struct thread *);
static __inline void tdq_runq_add(struct tdq *, struct thread *, int);
static __inline void tdq_runq_rem(struct tdq *, struct thread *);
static inline int sched_shouldpreempt(int, int, int);
void tdq_print(int cpu);
static void runq_print(struct runq *rq);
static void tdq_add(struct tdq *, struct thread *, int);
#ifdef SMP
static struct thread *tdq_move(struct tdq *, struct tdq *);
static int tdq_idled(struct tdq *);
static void tdq_notify(struct tdq *, struct thread *);
static struct thread *tdq_steal(struct tdq *, int);
static struct thread *runq_steal(struct runq *, int);
static int sched_pickcpu(struct thread *, int);
static void sched_balance(void);
static int sched_balance_pair(struct tdq *, struct tdq *);
static inline struct tdq *sched_setcpu(struct thread *, int, int);
static inline void thread_unblock_switch(struct thread *, struct mtx *);
static struct mtx *sched_switch_migrate(struct tdq *, struct thread *, int);
static int sysctl_kern_sched_topology_spec(SYSCTL_HANDLER_ARGS);
static int sysctl_kern_sched_topology_spec_internal(struct sbuf *sb, 
    struct cpu_group *cg, int indent);
#endif

static void sched_setup(void *dummy);
SYSINIT(sched_setup, SI_SUB_RUN_QUEUE, SI_ORDER_FIRST, sched_setup, NULL);

static void sched_initticks(void *dummy);
SYSINIT(sched_initticks, SI_SUB_CLOCKS, SI_ORDER_THIRD, sched_initticks,
    NULL);

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

/*
 * Print the threads waiting on a run-queue.
 */
static void
runq_print(struct runq *rq)
{
	struct rqhead *rqh;
	struct thread *td;
	int pri;
	int j;
	int i;

	for (i = 0; i < RQB_LEN; i++) {
		printf("\t\trunq bits %d 0x%zx\n",
		    i, rq->rq_status.rqb_bits[i]);
		for (j = 0; j < RQB_BPW; j++)
			if (rq->rq_status.rqb_bits[i] & (1ul << j)) {
				pri = j + (i << RQB_L2BPW);
				rqh = &rq->rq_queues[pri];
				TAILQ_FOREACH(td, rqh, td_runq) {
					printf("\t\t\ttd %p(%s) priority %d rqindex %d pri %d\n",
					    td, td->td_name, td->td_priority,
					    td->td_rqindex, pri);
				}
			}
	}
}

/*
 * Print the status of a per-cpu thread queue.  Should be a ddb show cmd.
 */
void
tdq_print(int cpu)
{
	struct tdq *tdq;

	tdq = TDQ_CPU(cpu);

	printf("tdq %d:\n", TDQ_ID(tdq));
	printf("\tlock            %p\n", TDQ_LOCKPTR(tdq));
	printf("\tLock name:      %s\n", tdq->tdq_name);
	printf("\tload:           %d\n", tdq->tdq_load);
	printf("\tswitch cnt:     %d\n", tdq->tdq_switchcnt);
	printf("\told switch cnt: %d\n", tdq->tdq_oldswitchcnt);
	printf("\ttimeshare idx:  %d\n", tdq->tdq_idx);
	printf("\ttimeshare ridx: %d\n", tdq->tdq_ridx);
	printf("\tload transferable: %d\n", tdq->tdq_transferable);
	printf("\tlowest priority:   %d\n", tdq->tdq_lowpri);
	printf("\trealtime runq:\n");
	runq_print(&tdq->tdq_realtime);
	printf("\ttimeshare runq:\n");
	runq_print(&tdq->tdq_timeshare);
	printf("\tidle runq:\n");
	runq_print(&tdq->tdq_idle);
}

static inline int
sched_shouldpreempt(int pri, int cpri, int remote)
{
	/*
	 * If the new priority is not better than the current priority there is
	 * nothing to do.
	 */
	if (pri >= cpri)
		return (0);
	/*
	 * Always preempt idle.
	 */
	if (cpri >= PRI_MIN_IDLE)
		return (1);
	/*
	 * If preemption is disabled don't preempt others.
	 */
	if (preempt_thresh == 0)
		return (0);
	/*
	 * Preempt if we exceed the threshold.
	 */
	if (pri <= preempt_thresh)
		return (1);
	/*
	 * If we're interactive or better and there is non-interactive
	 * or worse running preempt only remote processors.
	 */
	if (remote && pri <= PRI_MAX_INTERACT && cpri > PRI_MAX_INTERACT)
		return (1);
	return (0);
}

/*
 * Add a thread to the actual run-queue.  Keeps transferable counts up to
 * date with what is actually on the run-queue.  Selects the correct
 * queue position for timeshare threads.
 */
static __inline void
tdq_runq_add(struct tdq *tdq, struct thread *td, int flags)
{
	struct td_sched *ts;
	u_char pri;

	TDQ_LOCK_ASSERT(tdq, MA_OWNED);
	THREAD_LOCK_ASSERT(td, MA_OWNED);

	pri = td->td_priority;
	ts = td_get_sched(td);
	TD_SET_RUNQ(td);
	if (THREAD_CAN_MIGRATE(td)) {
		tdq->tdq_transferable++;
		ts->ts_flags |= TSF_XFERABLE;
	}
	if (pri < PRI_MIN_BATCH) {
		ts->ts_runq = &tdq->tdq_realtime;
	} else if (pri <= PRI_MAX_BATCH) {
		ts->ts_runq = &tdq->tdq_timeshare;
		KASSERT(pri <= PRI_MAX_BATCH && pri >= PRI_MIN_BATCH,
			("Invalid priority %d on timeshare runq", pri));
		/*
		 * This queue contains only priorities between MIN and MAX
		 * realtime.  Use the whole queue to represent these values.
		 */
		if ((flags & (SRQ_BORROWING|SRQ_PREEMPTED)) == 0) {
			pri = RQ_NQS * (pri - PRI_MIN_BATCH) / PRI_BATCH_RANGE;
			pri = (pri + tdq->tdq_idx) % RQ_NQS;
			/*
			 * This effectively shortens the queue by one so we
			 * can have a one slot difference between idx and
			 * ridx while we wait for threads to drain.
			 */
			if (tdq->tdq_ridx != tdq->tdq_idx &&
			    pri == tdq->tdq_ridx)
				pri = (unsigned char)(pri - 1) % RQ_NQS;
		} else
			pri = tdq->tdq_ridx;
		runq_add_pri(ts->ts_runq, td, pri, flags);
		return;
	} else
		ts->ts_runq = &tdq->tdq_idle;
	runq_add(ts->ts_runq, td, flags);
}

/* 
 * Remove a thread from a run-queue.  This typically happens when a thread
 * is selected to run.  Running threads are not on the queue and the
 * transferable count does not reflect them.
 */
static __inline void
tdq_runq_rem(struct tdq *tdq, struct thread *td)
{
	struct td_sched *ts;

	ts = td_get_sched(td);
	TDQ_LOCK_ASSERT(tdq, MA_OWNED);
	KASSERT(ts->ts_runq != NULL,
	    ("tdq_runq_remove: thread %p null ts_runq", td));
	if (ts->ts_flags & TSF_XFERABLE) {
		tdq->tdq_transferable--;
		ts->ts_flags &= ~TSF_XFERABLE;
	}
	if (ts->ts_runq == &tdq->tdq_timeshare) {
		if (tdq->tdq_idx != tdq->tdq_ridx)
			runq_remove_idx(ts->ts_runq, td, &tdq->tdq_ridx);
		else
			runq_remove_idx(ts->ts_runq, td, NULL);
	} else
		runq_remove(ts->ts_runq, td);
}

/*
 * Load is maintained for all threads RUNNING and ON_RUNQ.  Add the load
 * for this thread to the referenced thread queue.
 */
static void
tdq_load_add(struct tdq *tdq, struct thread *td)
{

	TDQ_LOCK_ASSERT(tdq, MA_OWNED);
	THREAD_LOCK_ASSERT(td, MA_OWNED);

	tdq->tdq_load++;
	if ((td->td_flags & TDF_NOLOAD) == 0)
		tdq->tdq_sysload++;
	KTR_COUNTER0(KTR_SCHED, "load", tdq->tdq_loadname, tdq->tdq_load);
	SDT_PROBE2(sched, , , load__change, (int)TDQ_ID(tdq), tdq->tdq_load);
}

/*
 * Remove the load from a thread that is transitioning to a sleep state or
 * exiting.
 */
static void
tdq_load_rem(struct tdq *tdq, struct thread *td)
{

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	TDQ_LOCK_ASSERT(tdq, MA_OWNED);
	KASSERT(tdq->tdq_load != 0,
	    ("tdq_load_rem: Removing with 0 load on queue %d", TDQ_ID(tdq)));

	tdq->tdq_load--;
	if ((td->td_flags & TDF_NOLOAD) == 0)
		tdq->tdq_sysload--;
	KTR_COUNTER0(KTR_SCHED, "load", tdq->tdq_loadname, tdq->tdq_load);
	SDT_PROBE2(sched, , , load__change, (int)TDQ_ID(tdq), tdq->tdq_load);
}

/*
 * Bound timeshare latency by decreasing slice size as load increases.  We
 * consider the maximum latency as the sum of the threads waiting to run
 * aside from curthread and target no more than sched_slice latency but
 * no less than sched_slice_min runtime.
 */
static inline int
tdq_slice(struct tdq *tdq)
{
	int load;

	/*
	 * It is safe to use sys_load here because this is called from
	 * contexts where timeshare threads are running and so there
	 * cannot be higher priority load in the system.
	 */
	load = tdq->tdq_sysload - 1;
	if (load >= SCHED_SLICE_MIN_DIVISOR)
		return (sched_slice_min);
	if (load <= 1)
		return (sched_slice);
	return (sched_slice / load);
}

/*
 * Set lowpri to its exact value by searching the run-queue and
 * evaluating curthread.  curthread may be passed as an optimization.
 */
static void
tdq_setlowpri(struct tdq *tdq, struct thread *ctd)
{
	struct thread *td;

	TDQ_LOCK_ASSERT(tdq, MA_OWNED);
	if (ctd == NULL)
		ctd = pcpu_find(TDQ_ID(tdq))->pc_curthread;
	td = tdq_choose(tdq);
	if (td == NULL || td->td_priority > ctd->td_priority)
		tdq->tdq_lowpri = ctd->td_priority;
	else
		tdq->tdq_lowpri = td->td_priority;
}

#ifdef SMP
/*
 * We need some randomness. Implement a classic Linear Congruential
 * Generator X_{n+1}=(aX_n+c) mod m. These values are optimized for
 * m = 2^32, a = 69069 and c = 5. We only return the upper 16 bits
 * of the random state (in the low bits of our answer) to keep
 * the maximum randomness.
 */
static uint32_t
sched_random(void)
{
	uint32_t *rndptr;

	rndptr = DPCPU_PTR(randomval);
	*rndptr = *rndptr * 69069 + 5;

	return (*rndptr >> 16);
}

struct cpu_search {
	cpuset_t cs_mask;
	u_int	cs_prefer;
	int	cs_pri;		/* Min priority for low. */
	int	cs_limit;	/* Max load for low, min load for high. */
	int	cs_cpu;
	int	cs_load;
};

#define	CPU_SEARCH_LOWEST	0x1
#define	CPU_SEARCH_HIGHEST	0x2
#define	CPU_SEARCH_BOTH		(CPU_SEARCH_LOWEST|CPU_SEARCH_HIGHEST)

#define	CPUSET_FOREACH(cpu, mask)				\
	for ((cpu) = 0; (cpu) <= mp_maxid; (cpu)++)		\
		if (CPU_ISSET(cpu, &mask))

static __always_inline int cpu_search(const struct cpu_group *cg,
    struct cpu_search *low, struct cpu_search *high, const int match);
int __noinline cpu_search_lowest(const struct cpu_group *cg,
    struct cpu_search *low);
int __noinline cpu_search_highest(const struct cpu_group *cg,
    struct cpu_search *high);
int __noinline cpu_search_both(const struct cpu_group *cg,
    struct cpu_search *low, struct cpu_search *high);

/*
 * Search the tree of cpu_groups for the lowest or highest loaded cpu
 * according to the match argument.  This routine actually compares the
 * load on all paths through the tree and finds the least loaded cpu on
 * the least loaded path, which may differ from the least loaded cpu in
 * the system.  This balances work among caches and buses.
 *
 * This inline is instantiated in three forms below using constants for the
 * match argument.  It is reduced to the minimum set for each case.  It is
 * also recursive to the depth of the tree.
 */
static __always_inline int
cpu_search(const struct cpu_group *cg, struct cpu_search *low,
    struct cpu_search *high, const int match)
{
	struct cpu_search lgroup;
	struct cpu_search hgroup;
	cpuset_t cpumask;
	struct cpu_group *child;
	struct tdq *tdq;
	int cpu, i, hload, lload, load, total, rnd;

	total = 0;
	cpumask = cg->cg_mask;
	if (match & CPU_SEARCH_LOWEST) {
		lload = INT_MAX;
		lgroup = *low;
	}
	if (match & CPU_SEARCH_HIGHEST) {
		hload = INT_MIN;
		hgroup = *high;
	}

	/* Iterate through the child CPU groups and then remaining CPUs. */
	for (i = cg->cg_children, cpu = mp_maxid; ; ) {
		if (i == 0) {
#ifdef HAVE_INLINE_FFSL
			cpu = CPU_FFS(&cpumask) - 1;
#else
			while (cpu >= 0 && !CPU_ISSET(cpu, &cpumask))
				cpu--;
#endif
			if (cpu < 0)
				break;
			child = NULL;
		} else
			child = &cg->cg_child[i - 1];

		if (match & CPU_SEARCH_LOWEST)
			lgroup.cs_cpu = -1;
		if (match & CPU_SEARCH_HIGHEST)
			hgroup.cs_cpu = -1;
		if (child) {			/* Handle child CPU group. */
			CPU_NAND(&cpumask, &child->cg_mask);
			switch (match) {
			case CPU_SEARCH_LOWEST:
				load = cpu_search_lowest(child, &lgroup);
				break;
			case CPU_SEARCH_HIGHEST:
				load = cpu_search_highest(child, &hgroup);
				break;
			case CPU_SEARCH_BOTH:
				load = cpu_search_both(child, &lgroup, &hgroup);
				break;
			}
		} else {			/* Handle child CPU. */
			CPU_CLR(cpu, &cpumask);
			tdq = TDQ_CPU(cpu);
			load = tdq->tdq_load * 256;
			rnd = sched_random() % 32;
			if (match & CPU_SEARCH_LOWEST) {
				if (cpu == low->cs_prefer)
					load -= 64;
				/* If that CPU is allowed and get data. */
				if (tdq->tdq_lowpri > lgroup.cs_pri &&
				    tdq->tdq_load <= lgroup.cs_limit &&
				    CPU_ISSET(cpu, &lgroup.cs_mask)) {
					lgroup.cs_cpu = cpu;
					lgroup.cs_load = load - rnd;
				}
			}
			if (match & CPU_SEARCH_HIGHEST)
				if (tdq->tdq_load >= hgroup.cs_limit &&
				    tdq->tdq_transferable &&
				    CPU_ISSET(cpu, &hgroup.cs_mask)) {
					hgroup.cs_cpu = cpu;
					hgroup.cs_load = load - rnd;
				}
		}
		total += load;

		/* We have info about child item. Compare it. */
		if (match & CPU_SEARCH_LOWEST) {
			if (lgroup.cs_cpu >= 0 &&
			    (load < lload ||
			     (load == lload && lgroup.cs_load < low->cs_load))) {
				lload = load;
				low->cs_cpu = lgroup.cs_cpu;
				low->cs_load = lgroup.cs_load;
			}
		}
		if (match & CPU_SEARCH_HIGHEST)
			if (hgroup.cs_cpu >= 0 &&
			    (load > hload ||
			     (load == hload && hgroup.cs_load > high->cs_load))) {
				hload = load;
				high->cs_cpu = hgroup.cs_cpu;
				high->cs_load = hgroup.cs_load;
			}
		if (child) {
			i--;
			if (i == 0 && CPU_EMPTY(&cpumask))
				break;
		}
#ifndef HAVE_INLINE_FFSL
		else
			cpu--;
#endif
	}
	return (total);
}

/*
 * cpu_search instantiations must pass constants to maintain the inline
 * optimization.
 */
int
cpu_search_lowest(const struct cpu_group *cg, struct cpu_search *low)
{
	return cpu_search(cg, low, NULL, CPU_SEARCH_LOWEST);
}

int
cpu_search_highest(const struct cpu_group *cg, struct cpu_search *high)
{
	return cpu_search(cg, NULL, high, CPU_SEARCH_HIGHEST);
}

int
cpu_search_both(const struct cpu_group *cg, struct cpu_search *low,
    struct cpu_search *high)
{
	return cpu_search(cg, low, high, CPU_SEARCH_BOTH);
}

/*
 * Find the cpu with the least load via the least loaded path that has a
 * lowpri greater than pri  pri.  A pri of -1 indicates any priority is
 * acceptable.
 */
static inline int
sched_lowest(const struct cpu_group *cg, cpuset_t mask, int pri, int maxload,
    int prefer)
{
	struct cpu_search low;

	low.cs_cpu = -1;
	low.cs_prefer = prefer;
	low.cs_mask = mask;
	low.cs_pri = pri;
	low.cs_limit = maxload;
	cpu_search_lowest(cg, &low);
	return low.cs_cpu;
}

/*
 * Find the cpu with the highest load via the highest loaded path.
 */
static inline int
sched_highest(const struct cpu_group *cg, cpuset_t mask, int minload)
{
	struct cpu_search high;

	high.cs_cpu = -1;
	high.cs_mask = mask;
	high.cs_limit = minload;
	cpu_search_highest(cg, &high);
	return high.cs_cpu;
}

static void
sched_balance_group(struct cpu_group *cg)
{
	cpuset_t hmask, lmask;
	int high, low, anylow;

	CPU_FILL(&hmask);
	for (;;) {
		high = sched_highest(cg, hmask, 2);
		/* Stop if there is no more CPU with transferrable threads. */
		if (high == -1)
			break;
		CPU_CLR(high, &hmask);
		CPU_COPY(&hmask, &lmask);
		/* Stop if there is no more CPU left for low. */
		if (CPU_EMPTY(&lmask))
			break;
		anylow = 1;
nextlow:
		low = sched_lowest(cg, lmask, -1,
		    TDQ_CPU(high)->tdq_load - 1, high);
		/* Stop if we looked well and found no less loaded CPU. */
		if (anylow && low == -1)
			break;
		/* Go to next high if we found no less loaded CPU. */
		if (low == -1)
			continue;
		/* Transfer thread from high to low. */
		if (sched_balance_pair(TDQ_CPU(high), TDQ_CPU(low))) {
			/* CPU that got thread can no longer be a donor. */
			CPU_CLR(low, &hmask);
		} else {
			/*
			 * If failed, then there is no threads on high
			 * that can run on this low. Drop low from low
			 * mask and look for different one.
			 */
			CPU_CLR(low, &lmask);
			anylow = 0;
			goto nextlow;
		}
	}
}

static void
sched_balance(void)
{
	struct tdq *tdq;

	balance_ticks = max(balance_interval / 2, 1) +
	    (sched_random() % balance_interval);
	tdq = TDQ_SELF();
	TDQ_UNLOCK(tdq);
	sched_balance_group(cpu_top);
	TDQ_LOCK(tdq);
}

/*
 * Lock two thread queues using their address to maintain lock order.
 */
static void
tdq_lock_pair(struct tdq *one, struct tdq *two)
{
	if (one < two) {
		TDQ_LOCK(one);
		TDQ_LOCK_FLAGS(two, MTX_DUPOK);
	} else {
		TDQ_LOCK(two);
		TDQ_LOCK_FLAGS(one, MTX_DUPOK);
	}
}

/*
 * Unlock two thread queues.  Order is not important here.
 */
static void
tdq_unlock_pair(struct tdq *one, struct tdq *two)
{
	TDQ_UNLOCK(one);
	TDQ_UNLOCK(two);
}

/*
 * Transfer load between two imbalanced thread queues.
 */
static int
sched_balance_pair(struct tdq *high, struct tdq *low)
{
	struct thread *td;
	int cpu;

	tdq_lock_pair(high, low);
	td = NULL;
	/*
	 * Transfer a thread from high to low.
	 */
	if (high->tdq_transferable != 0 && high->tdq_load > low->tdq_load &&
	    (td = tdq_move(high, low)) != NULL) {
		/*
		 * In case the target isn't the current cpu notify it of the
		 * new load, possibly sending an IPI to force it to reschedule.
		 */
		cpu = TDQ_ID(low);
		if (cpu != PCPU_GET(cpuid))
			tdq_notify(low, td);
	}
	tdq_unlock_pair(high, low);
	return (td != NULL);
}

/*
 * Move a thread from one thread queue to another.
 */
static struct thread *
tdq_move(struct tdq *from, struct tdq *to)
{
	struct td_sched *ts;
	struct thread *td;
	struct tdq *tdq;
	int cpu;

	TDQ_LOCK_ASSERT(from, MA_OWNED);
	TDQ_LOCK_ASSERT(to, MA_OWNED);

	tdq = from;
	cpu = TDQ_ID(to);
	td = tdq_steal(tdq, cpu);
	if (td == NULL)
		return (NULL);
	ts = td_get_sched(td);
	/*
	 * Although the run queue is locked the thread may be blocked.  Lock
	 * it to clear this and acquire the run-queue lock.
	 */
	thread_lock(td);
	/* Drop recursive lock on from acquired via thread_lock(). */
	TDQ_UNLOCK(from);
	sched_rem(td);
	ts->ts_cpu = cpu;
	td->td_lock = TDQ_LOCKPTR(to);
	tdq_add(to, td, SRQ_YIELDING);
	return (td);
}

/*
 * This tdq has idled.  Try to steal a thread from another cpu and switch
 * to it.
 */
static int
tdq_idled(struct tdq *tdq)
{
	struct cpu_group *cg;
	struct tdq *steal;
	cpuset_t mask;
	int cpu, switchcnt;

	if (smp_started == 0 || steal_idle == 0 || tdq->tdq_cg == NULL)
		return (1);
	CPU_FILL(&mask);
	CPU_CLR(PCPU_GET(cpuid), &mask);
    restart:
	switchcnt = tdq->tdq_switchcnt + tdq->tdq_oldswitchcnt;
	for (cg = tdq->tdq_cg; ; ) {
		cpu = sched_highest(cg, mask, steal_thresh);
		/*
		 * We were assigned a thread but not preempted.  Returning
		 * 0 here will cause our caller to switch to it.
		 */
		if (tdq->tdq_load)
			return (0);
		if (cpu == -1) {
			cg = cg->cg_parent;
			if (cg == NULL)
				return (1);
			continue;
		}
		steal = TDQ_CPU(cpu);
		/*
		 * The data returned by sched_highest() is stale and
		 * the chosen CPU no longer has an eligible thread.
		 *
		 * Testing this ahead of tdq_lock_pair() only catches
		 * this situation about 20% of the time on an 8 core
		 * 16 thread Ryzen 7, but it still helps performance.
		 */
		if (steal->tdq_load < steal_thresh ||
		    steal->tdq_transferable == 0)
			goto restart;
		tdq_lock_pair(tdq, steal);
		/*
		 * We were assigned a thread while waiting for the locks.
		 * Switch to it now instead of stealing a thread.
		 */
		if (tdq->tdq_load)
			break;
		/*
		 * The data returned by sched_highest() is stale and
		 * the chosen CPU no longer has an eligible thread, or
		 * we were preempted and the CPU loading info may be out
		 * of date.  The latter is rare.  In either case restart
		 * the search.
		 */
		if (steal->tdq_load < steal_thresh ||
		    steal->tdq_transferable == 0 ||
		    switchcnt != tdq->tdq_switchcnt + tdq->tdq_oldswitchcnt) {
			tdq_unlock_pair(tdq, steal);
			goto restart;
		}
		/*
		 * Steal the thread and switch to it.
		 */
		if (tdq_move(steal, tdq) != NULL)
			break;
		/*
		 * We failed to acquire a thread even though it looked
		 * like one was available.  This could be due to affinity
		 * restrictions or for other reasons.  Loop again after
		 * removing this CPU from the set.  The restart logic
		 * above does not restore this CPU to the set due to the
		 * likelyhood of failing here again.
		 */
		CPU_CLR(cpu, &mask);
		tdq_unlock_pair(tdq, steal);
	}
	TDQ_UNLOCK(steal);
	mi_switch(SW_VOL | SWT_IDLE, NULL);
	thread_unlock(curthread);
	return (0);
}

/*
 * Notify a remote cpu of new work.  Sends an IPI if criteria are met.
 */
static void
tdq_notify(struct tdq *tdq, struct thread *td)
{
	struct thread *ctd;
	int pri;
	int cpu;

	if (tdq->tdq_ipipending)
		return;
	cpu = td_get_sched(td)->ts_cpu;
	pri = td->td_priority;
	ctd = pcpu_find(cpu)->pc_curthread;
	if (!sched_shouldpreempt(pri, ctd->td_priority, 1))
		return;

	/*
	 * Make sure that our caller's earlier update to tdq_load is
	 * globally visible before we read tdq_cpu_idle.  Idle thread
	 * accesses both of them without locks, and the order is important.
	 */
	atomic_thread_fence_seq_cst();

	if (TD_IS_IDLETHREAD(ctd)) {
		/*
		 * If the MD code has an idle wakeup routine try that before
		 * falling back to IPI.
		 */
		if (!tdq->tdq_cpu_idle || cpu_idle_wakeup(cpu))
			return;
	}
	tdq->tdq_ipipending = 1;
	ipi_cpu(cpu, IPI_PREEMPT);
}

/*
 * Steals load from a timeshare queue.  Honors the rotating queue head
 * index.
 */
static struct thread *
runq_steal_from(struct runq *rq, int cpu, u_char start)
{
	struct rqbits *rqb;
	struct rqhead *rqh;
	struct thread *td, *first;
	int bit;
	int i;

	rqb = &rq->rq_status;
	bit = start & (RQB_BPW -1);
	first = NULL;
again:
	for (i = RQB_WORD(start); i < RQB_LEN; bit = 0, i++) {
		if (rqb->rqb_bits[i] == 0)
			continue;
		if (bit == 0)
			bit = RQB_FFS(rqb->rqb_bits[i]);
		for (; bit < RQB_BPW; bit++) {
			if ((rqb->rqb_bits[i] & (1ul << bit)) == 0)
				continue;
			rqh = &rq->rq_queues[bit + (i << RQB_L2BPW)];
			TAILQ_FOREACH(td, rqh, td_runq) {
				if (first && THREAD_CAN_MIGRATE(td) &&
				    THREAD_CAN_SCHED(td, cpu))
					return (td);
				first = td;
			}
		}
	}
	if (start != 0) {
		start = 0;
		goto again;
	}

	if (first && THREAD_CAN_MIGRATE(first) &&
	    THREAD_CAN_SCHED(first, cpu))
		return (first);
	return (NULL);
}

/*
 * Steals load from a standard linear queue.
 */
static struct thread *
runq_steal(struct runq *rq, int cpu)
{
	struct rqhead *rqh;
	struct rqbits *rqb;
	struct thread *td;
	int word;
	int bit;

	rqb = &rq->rq_status;
	for (word = 0; word < RQB_LEN; word++) {
		if (rqb->rqb_bits[word] == 0)
			continue;
		for (bit = 0; bit < RQB_BPW; bit++) {
			if ((rqb->rqb_bits[word] & (1ul << bit)) == 0)
				continue;
			rqh = &rq->rq_queues[bit + (word << RQB_L2BPW)];
			TAILQ_FOREACH(td, rqh, td_runq)
				if (THREAD_CAN_MIGRATE(td) &&
				    THREAD_CAN_SCHED(td, cpu))
					return (td);
		}
	}
	return (NULL);
}

/*
 * Attempt to steal a thread in priority order from a thread queue.
 */
static struct thread *
tdq_steal(struct tdq *tdq, int cpu)
{
	struct thread *td;

	TDQ_LOCK_ASSERT(tdq, MA_OWNED);
	if ((td = runq_steal(&tdq->tdq_realtime, cpu)) != NULL)
		return (td);
	if ((td = runq_steal_from(&tdq->tdq_timeshare,
	    cpu, tdq->tdq_ridx)) != NULL)
		return (td);
	return (runq_steal(&tdq->tdq_idle, cpu));
}

/*
 * Sets the thread lock and ts_cpu to match the requested cpu.  Unlocks the
 * current lock and returns with the assigned queue locked.
 */
static inline struct tdq *
sched_setcpu(struct thread *td, int cpu, int flags)
{

	struct tdq *tdq;

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	tdq = TDQ_CPU(cpu);
	td_get_sched(td)->ts_cpu = cpu;
	/*
	 * If the lock matches just return the queue.
	 */
	if (td->td_lock == TDQ_LOCKPTR(tdq))
		return (tdq);
#ifdef notyet
	/*
	 * If the thread isn't running its lockptr is a
	 * turnstile or a sleepqueue.  We can just lock_set without
	 * blocking.
	 */
	if (TD_CAN_RUN(td)) {
		TDQ_LOCK(tdq);
		thread_lock_set(td, TDQ_LOCKPTR(tdq));
		return (tdq);
	}
#endif
	/*
	 * The hard case, migration, we need to block the thread first to
	 * prevent order reversals with other cpus locks.
	 */
	spinlock_enter();
	thread_lock_block(td);
	TDQ_LOCK(tdq);
	thread_lock_unblock(td, TDQ_LOCKPTR(tdq));
	spinlock_exit();
	return (tdq);
}

SCHED_STAT_DEFINE(pickcpu_intrbind, "Soft interrupt binding");
SCHED_STAT_DEFINE(pickcpu_idle_affinity, "Picked idle cpu based on affinity");
SCHED_STAT_DEFINE(pickcpu_affinity, "Picked cpu based on affinity");
SCHED_STAT_DEFINE(pickcpu_lowest, "Selected lowest load");
SCHED_STAT_DEFINE(pickcpu_local, "Migrated to current cpu");
SCHED_STAT_DEFINE(pickcpu_migration, "Selection may have caused migration");

static int
sched_pickcpu(struct thread *td, int flags)
{
	struct cpu_group *cg, *ccg;
	struct td_sched *ts;
	struct tdq *tdq;
	cpuset_t mask;
	int cpu, pri, self;

	self = PCPU_GET(cpuid);
	ts = td_get_sched(td);
	KASSERT(!CPU_ABSENT(ts->ts_cpu), ("sched_pickcpu: Start scheduler on "
	    "absent CPU %d for thread %s.", ts->ts_cpu, td->td_name));
	if (smp_started == 0)
		return (self);
	/*
	 * Don't migrate a running thread from sched_switch().
	 */
	if ((flags & SRQ_OURSELF) || !THREAD_CAN_MIGRATE(td))
		return (ts->ts_cpu);
	/*
	 * Prefer to run interrupt threads on the processors that generate
	 * the interrupt.
	 */
	pri = td->td_priority;
	if (td->td_priority <= PRI_MAX_ITHD && THREAD_CAN_SCHED(td, self) &&
	    curthread->td_intr_nesting_level && ts->ts_cpu != self) {
		SCHED_STAT_INC(pickcpu_intrbind);
		ts->ts_cpu = self;
		if (TDQ_CPU(self)->tdq_lowpri > pri) {
			SCHED_STAT_INC(pickcpu_affinity);
			return (ts->ts_cpu);
		}
	}
	/*
	 * If the thread can run on the last cpu and the affinity has not
	 * expired and it is idle, run it there.
	 */
	tdq = TDQ_CPU(ts->ts_cpu);
	cg = tdq->tdq_cg;
	if (THREAD_CAN_SCHED(td, ts->ts_cpu) &&
	    tdq->tdq_lowpri >= PRI_MIN_IDLE &&
	    SCHED_AFFINITY(ts, CG_SHARE_L2)) {
		if (cg->cg_flags & CG_FLAG_THREAD) {
			CPUSET_FOREACH(cpu, cg->cg_mask) {
				if (TDQ_CPU(cpu)->tdq_lowpri < PRI_MIN_IDLE)
					break;
			}
		} else
			cpu = INT_MAX;
		if (cpu > mp_maxid) {
			SCHED_STAT_INC(pickcpu_idle_affinity);
			return (ts->ts_cpu);
		}
	}
	/*
	 * Search for the last level cache CPU group in the tree.
	 * Skip caches with expired affinity time and SMT groups.
	 * Affinity to higher level caches will be handled less aggressively.
	 */
	for (ccg = NULL; cg != NULL; cg = cg->cg_parent) {
		if (cg->cg_flags & CG_FLAG_THREAD)
			continue;
		if (!SCHED_AFFINITY(ts, cg->cg_level))
			continue;
		ccg = cg;
	}
	if (ccg != NULL)
		cg = ccg;
	cpu = -1;
	/* Search the group for the less loaded idle CPU we can run now. */
	mask = td->td_cpuset->cs_mask;
	if (cg != NULL && cg != cpu_top &&
	    CPU_CMP(&cg->cg_mask, &cpu_top->cg_mask) != 0)
		cpu = sched_lowest(cg, mask, max(pri, PRI_MAX_TIMESHARE),
		    INT_MAX, ts->ts_cpu);
	/* Search globally for the less loaded CPU we can run now. */
	if (cpu == -1)
		cpu = sched_lowest(cpu_top, mask, pri, INT_MAX, ts->ts_cpu);
	/* Search globally for the less loaded CPU. */
	if (cpu == -1)
		cpu = sched_lowest(cpu_top, mask, -1, INT_MAX, ts->ts_cpu);
	KASSERT(cpu != -1, ("sched_pickcpu: Failed to find a cpu."));
	KASSERT(!CPU_ABSENT(cpu), ("sched_pickcpu: Picked absent CPU %d.", cpu));
	/*
	 * Compare the lowest loaded cpu to current cpu.
	 */
	if (THREAD_CAN_SCHED(td, self) && TDQ_CPU(self)->tdq_lowpri > pri &&
	    TDQ_CPU(cpu)->tdq_lowpri < PRI_MIN_IDLE &&
	    TDQ_CPU(self)->tdq_load <= TDQ_CPU(cpu)->tdq_load + 1) {
		SCHED_STAT_INC(pickcpu_local);
		cpu = self;
	} else
		SCHED_STAT_INC(pickcpu_lowest);
	if (cpu != ts->ts_cpu)
		SCHED_STAT_INC(pickcpu_migration);
	return (cpu);
}
#endif

/*
 * Pick the highest priority task we have and return it.
 */
static struct thread *
tdq_choose(struct tdq *tdq)
{
	struct thread *td;

	TDQ_LOCK_ASSERT(tdq, MA_OWNED);
	td = runq_choose(&tdq->tdq_realtime);
	if (td != NULL)
		return (td);
	td = runq_choose_from(&tdq->tdq_timeshare, tdq->tdq_ridx);
	if (td != NULL) {
		KASSERT(td->td_priority >= PRI_MIN_BATCH,
		    ("tdq_choose: Invalid priority on timeshare queue %d",
		    td->td_priority));
		return (td);
	}
	td = runq_choose(&tdq->tdq_idle);
	if (td != NULL) {
		KASSERT(td->td_priority >= PRI_MIN_IDLE,
		    ("tdq_choose: Invalid priority on idle queue %d",
		    td->td_priority));
		return (td);
	}

	return (NULL);
}

/*
 * Initialize a thread queue.
 */
static void
tdq_setup(struct tdq *tdq)
{

	if (bootverbose)
		printf("ULE: setup cpu %d\n", TDQ_ID(tdq));
	runq_init(&tdq->tdq_realtime);
	runq_init(&tdq->tdq_timeshare);
	runq_init(&tdq->tdq_idle);
	snprintf(tdq->tdq_name, sizeof(tdq->tdq_name),
	    "sched lock %d", (int)TDQ_ID(tdq));
	mtx_init(&tdq->tdq_lock, tdq->tdq_name, "sched lock",
	    MTX_SPIN | MTX_RECURSE);
#ifdef KTR
	snprintf(tdq->tdq_loadname, sizeof(tdq->tdq_loadname),
	    "CPU %d load", (int)TDQ_ID(tdq));
#endif
}

#ifdef SMP
static void
sched_setup_smp(void)
{
	struct tdq *tdq;
	int i;

	cpu_top = smp_topo();
	CPU_FOREACH(i) {
		tdq = TDQ_CPU(i);
		tdq_setup(tdq);
		tdq->tdq_cg = smp_topo_find(cpu_top, i);
		if (tdq->tdq_cg == NULL)
			panic("Can't find cpu group for %d\n", i);
	}
	balance_tdq = TDQ_SELF();
}
#endif

/*
 * Setup the thread queues and initialize the topology based on MD
 * information.
 */
static void
sched_setup(void *dummy)
{
	struct tdq *tdq;

	tdq = TDQ_SELF();
#ifdef SMP
	sched_setup_smp();
#else
	tdq_setup(tdq);
#endif

	/* Add thread0's load since it's running. */
	TDQ_LOCK(tdq);
	thread0.td_lock = TDQ_LOCKPTR(TDQ_SELF());
	tdq_load_add(tdq, &thread0);
	tdq->tdq_lowpri = thread0.td_priority;
	TDQ_UNLOCK(tdq);
}

/*
 * This routine determines time constants after stathz and hz are setup.
 */
/* ARGSUSED */
static void
sched_initticks(void *dummy)
{
	int incr;

	realstathz = stathz ? stathz : hz;
	sched_slice = realstathz / SCHED_SLICE_DEFAULT_DIVISOR;
	sched_slice_min = sched_slice / SCHED_SLICE_MIN_DIVISOR;
	hogticks = imax(1, (2 * hz * sched_slice + realstathz / 2) /
	    realstathz);

	/*
	 * tickincr is shifted out by 10 to avoid rounding errors due to
	 * hz not being evenly divisible by stathz on all platforms.
	 */
	incr = (hz << SCHED_TICK_SHIFT) / realstathz;
	/*
	 * This does not work for values of stathz that are more than
	 * 1 << SCHED_TICK_SHIFT * hz.  In practice this does not happen.
	 */
	if (incr == 0)
		incr = 1;
	tickincr = incr;
#ifdef SMP
	/*
	 * Set the default balance interval now that we know
	 * what realstathz is.
	 */
	balance_interval = realstathz;
	balance_ticks = balance_interval;
	affinity = SCHED_AFFINITY_DEFAULT;
#endif
	if (sched_idlespinthresh < 0)
		sched_idlespinthresh = 2 * max(10000, 6 * hz) / realstathz;
}


/*
 * This is the core of the interactivity algorithm.  Determines a score based
 * on past behavior.  It is the ratio of sleep time to run time scaled to
 * a [0, 100] integer.  This is the voluntary sleep time of a process, which
 * differs from the cpu usage because it does not account for time spent
 * waiting on a run-queue.  Would be prettier if we had floating point.
 *
 * When a thread's sleep time is greater than its run time the
 * calculation is:
 *
 *                           scaling factor 
 * interactivity score =  ---------------------
 *                        sleep time / run time
 *
 *
 * When a thread's run time is greater than its sleep time the
 * calculation is:
 *
 *                           scaling factor 
 * interactivity score =  ---------------------    + scaling factor
 *                        run time / sleep time
 */
static int
sched_interact_score(struct thread *td)
{
	struct td_sched *ts;
	int div;

	ts = td_get_sched(td);
	/*
	 * The score is only needed if this is likely to be an interactive
	 * task.  Don't go through the expense of computing it if there's
	 * no chance.
	 */
	if (sched_interact <= SCHED_INTERACT_HALF &&
		ts->ts_runtime >= ts->ts_slptime)
			return (SCHED_INTERACT_HALF);

	if (ts->ts_runtime > ts->ts_slptime) {
		div = max(1, ts->ts_runtime / SCHED_INTERACT_HALF);
		return (SCHED_INTERACT_HALF +
		    (SCHED_INTERACT_HALF - (ts->ts_slptime / div)));
	}
	if (ts->ts_slptime > ts->ts_runtime) {
		div = max(1, ts->ts_slptime / SCHED_INTERACT_HALF);
		return (ts->ts_runtime / div);
	}
	/* runtime == slptime */
	if (ts->ts_runtime)
		return (SCHED_INTERACT_HALF);

	/*
	 * This can happen if slptime and runtime are 0.
	 */
	return (0);

}

/*
 * Scale the scheduling priority according to the "interactivity" of this
 * process.
 */
static void
sched_priority(struct thread *td)
{
	int score;
	int pri;

	if (PRI_BASE(td->td_pri_class) != PRI_TIMESHARE)
		return;
	/*
	 * If the score is interactive we place the thread in the realtime
	 * queue with a priority that is less than kernel and interrupt
	 * priorities.  These threads are not subject to nice restrictions.
	 *
	 * Scores greater than this are placed on the normal timeshare queue
	 * where the priority is partially decided by the most recent cpu
	 * utilization and the rest is decided by nice value.
	 *
	 * The nice value of the process has a linear effect on the calculated
	 * score.  Negative nice values make it easier for a thread to be
	 * considered interactive.
	 */
	score = imax(0, sched_interact_score(td) + td->td_proc->p_nice);
	if (score < sched_interact) {
		pri = PRI_MIN_INTERACT;
		pri += ((PRI_MAX_INTERACT - PRI_MIN_INTERACT + 1) /
		    sched_interact) * score;
		KASSERT(pri >= PRI_MIN_INTERACT && pri <= PRI_MAX_INTERACT,
		    ("sched_priority: invalid interactive priority %d score %d",
		    pri, score));
	} else {
		pri = SCHED_PRI_MIN;
		if (td_get_sched(td)->ts_ticks)
			pri += min(SCHED_PRI_TICKS(td_get_sched(td)),
			    SCHED_PRI_RANGE - 1);
		pri += SCHED_PRI_NICE(td->td_proc->p_nice);
		KASSERT(pri >= PRI_MIN_BATCH && pri <= PRI_MAX_BATCH,
		    ("sched_priority: invalid priority %d: nice %d, " 
		    "ticks %d ftick %d ltick %d tick pri %d",
		    pri, td->td_proc->p_nice, td_get_sched(td)->ts_ticks,
		    td_get_sched(td)->ts_ftick, td_get_sched(td)->ts_ltick,
		    SCHED_PRI_TICKS(td_get_sched(td))));
	}
	sched_user_prio(td, pri);

	return;
}

/*
 * This routine enforces a maximum limit on the amount of scheduling history
 * kept.  It is called after either the slptime or runtime is adjusted.  This
 * function is ugly due to integer math.
 */
static void
sched_interact_update(struct thread *td)
{
	struct td_sched *ts;
	u_int sum;

	ts = td_get_sched(td);
	sum = ts->ts_runtime + ts->ts_slptime;
	if (sum < SCHED_SLP_RUN_MAX)
		return;
	/*
	 * This only happens from two places:
	 * 1) We have added an unusual amount of run time from fork_exit.
	 * 2) We have added an unusual amount of sleep time from sched_sleep().
	 */
	if (sum > SCHED_SLP_RUN_MAX * 2) {
		if (ts->ts_runtime > ts->ts_slptime) {
			ts->ts_runtime = SCHED_SLP_RUN_MAX;
			ts->ts_slptime = 1;
		} else {
			ts->ts_slptime = SCHED_SLP_RUN_MAX;
			ts->ts_runtime = 1;
		}
		return;
	}
	/*
	 * If we have exceeded by more than 1/5th then the algorithm below
	 * will not bring us back into range.  Dividing by two here forces
	 * us into the range of [4/5 * SCHED_INTERACT_MAX, SCHED_INTERACT_MAX]
	 */
	if (sum > (SCHED_SLP_RUN_MAX / 5) * 6) {
		ts->ts_runtime /= 2;
		ts->ts_slptime /= 2;
		return;
	}
	ts->ts_runtime = (ts->ts_runtime / 5) * 4;
	ts->ts_slptime = (ts->ts_slptime / 5) * 4;
}

/*
 * Scale back the interactivity history when a child thread is created.  The
 * history is inherited from the parent but the thread may behave totally
 * differently.  For example, a shell spawning a compiler process.  We want
 * to learn that the compiler is behaving badly very quickly.
 */
static void
sched_interact_fork(struct thread *td)
{
	struct td_sched *ts;
	int ratio;
	int sum;

	ts = td_get_sched(td);
	sum = ts->ts_runtime + ts->ts_slptime;
	if (sum > SCHED_SLP_RUN_FORK) {
		ratio = sum / SCHED_SLP_RUN_FORK;
		ts->ts_runtime /= ratio;
		ts->ts_slptime /= ratio;
	}
}

/*
 * Called from proc0_init() to setup the scheduler fields.
 */
void
schedinit(void)
{
	struct td_sched *ts0;

	/*
	 * Set up the scheduler specific parts of thread0.
	 */
	ts0 = td_get_sched(&thread0);
	ts0->ts_ltick = ticks;
	ts0->ts_ftick = ticks;
	ts0->ts_slice = 0;
	ts0->ts_cpu = curcpu;	/* set valid CPU number */
}

/*
 * This is only somewhat accurate since given many processes of the same
 * priority they will switch when their slices run out, which will be
 * at most sched_slice stathz ticks.
 */
int
sched_rr_interval(void)
{

	/* Convert sched_slice from stathz to hz. */
	return (imax(1, (sched_slice * hz + realstathz / 2) / realstathz));
}

/*
 * Update the percent cpu tracking information when it is requested or
 * the total history exceeds the maximum.  We keep a sliding history of
 * tick counts that slowly decays.  This is less precise than the 4BSD
 * mechanism since it happens with less regular and frequent events.
 */
static void
sched_pctcpu_update(struct td_sched *ts, int run)
{
	int t = ticks;

	/*
	 * The signed difference may be negative if the thread hasn't run for
	 * over half of the ticks rollover period.
	 */
	if ((u_int)(t - ts->ts_ltick) >= SCHED_TICK_TARG) {
		ts->ts_ticks = 0;
		ts->ts_ftick = t - SCHED_TICK_TARG;
	} else if (t - ts->ts_ftick >= SCHED_TICK_MAX) {
		ts->ts_ticks = (ts->ts_ticks / (ts->ts_ltick - ts->ts_ftick)) *
		    (ts->ts_ltick - (t - SCHED_TICK_TARG));
		ts->ts_ftick = t - SCHED_TICK_TARG;
	}
	if (run)
		ts->ts_ticks += (t - ts->ts_ltick) << SCHED_TICK_SHIFT;
	ts->ts_ltick = t;
}

/*
 * Adjust the priority of a thread.  Move it to the appropriate run-queue
 * if necessary.  This is the back-end for several priority related
 * functions.
 */
static void
sched_thread_priority(struct thread *td, u_char prio)
{
	struct td_sched *ts;
	struct tdq *tdq;
	int oldpri;

	KTR_POINT3(KTR_SCHED, "thread", sched_tdname(td), "prio",
	    "prio:%d", td->td_priority, "new prio:%d", prio,
	    KTR_ATTR_LINKED, sched_tdname(curthread));
	SDT_PROBE3(sched, , , change__pri, td, td->td_proc, prio);
	if (td != curthread && prio < td->td_priority) {
		KTR_POINT3(KTR_SCHED, "thread", sched_tdname(curthread),
		    "lend prio", "prio:%d", td->td_priority, "new prio:%d",
		    prio, KTR_ATTR_LINKED, sched_tdname(td));
		SDT_PROBE4(sched, , , lend__pri, td, td->td_proc, prio, 
		    curthread);
	} 
	ts = td_get_sched(td);
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	if (td->td_priority == prio)
		return;
	/*
	 * If the priority has been elevated due to priority
	 * propagation, we may have to move ourselves to a new
	 * queue.  This could be optimized to not re-add in some
	 * cases.
	 */
	if (TD_ON_RUNQ(td) && prio < td->td_priority) {
		sched_rem(td);
		td->td_priority = prio;
		sched_add(td, SRQ_BORROWING);
		return;
	}
	/*
	 * If the thread is currently running we may have to adjust the lowpri
	 * information so other cpus are aware of our current priority.
	 */
	if (TD_IS_RUNNING(td)) {
		tdq = TDQ_CPU(ts->ts_cpu);
		oldpri = td->td_priority;
		td->td_priority = prio;
		if (prio < tdq->tdq_lowpri)
			tdq->tdq_lowpri = prio;
		else if (tdq->tdq_lowpri == oldpri)
			tdq_setlowpri(tdq, td);
		return;
	}
	td->td_priority = prio;
}

/*
 * Update a thread's priority when it is lent another thread's
 * priority.
 */
void
sched_lend_prio(struct thread *td, u_char prio)
{

	td->td_flags |= TDF_BORROWING;
	sched_thread_priority(td, prio);
}

/*
 * Restore a thread's priority when priority propagation is
 * over.  The prio argument is the minimum priority the thread
 * needs to have to satisfy other possible priority lending
 * requests.  If the thread's regular priority is less
 * important than prio, the thread will keep a priority boost
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
		sched_thread_priority(td, base_pri);
	} else
		sched_lend_prio(td, prio);
}

/*
 * Standard entry for setting the priority to an absolute value.
 */
void
sched_prio(struct thread *td, u_char prio)
{
	u_char oldprio;

	/* First, update the base priority. */
	td->td_base_pri = prio;

	/*
	 * If the thread is borrowing another thread's priority, don't
	 * ever lower the priority.
	 */
	if (td->td_flags & TDF_BORROWING && td->td_priority < prio)
		return;

	/* Change the real priority. */
	oldprio = td->td_priority;
	sched_thread_priority(td, prio);

	/*
	 * If the thread is on a turnstile, then let the turnstile update
	 * its state.
	 */
	if (TD_ON_LOCK(td) && oldprio != prio)
		turnstile_adjust(td, oldprio);
}

/*
 * Set the base user priority, does not effect current running priority.
 */
void
sched_user_prio(struct thread *td, u_char prio)
{

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

#ifdef SMP
/*
 * This tdq is about to idle.  Try to steal a thread from another CPU before
 * choosing the idle thread.
 */
static void
tdq_trysteal(struct tdq *tdq)
{
	struct cpu_group *cg;
	struct tdq *steal;
	cpuset_t mask;
	int cpu, i;

	if (smp_started == 0 || trysteal_limit == 0 || tdq->tdq_cg == NULL)
		return;
	CPU_FILL(&mask);
	CPU_CLR(PCPU_GET(cpuid), &mask);
	/* We don't want to be preempted while we're iterating. */
	spinlock_enter();
	TDQ_UNLOCK(tdq);
	for (i = 1, cg = tdq->tdq_cg; ; ) {
		cpu = sched_highest(cg, mask, steal_thresh);
		/*
		 * If a thread was added while interrupts were disabled don't
		 * steal one here.
		 */
		if (tdq->tdq_load > 0) {
			TDQ_LOCK(tdq);
			break;
		}
		if (cpu == -1) {
			i++;
			cg = cg->cg_parent;
			if (cg == NULL || i > trysteal_limit) {
				TDQ_LOCK(tdq);
				break;
			}
			continue;
		}
		steal = TDQ_CPU(cpu);
		/*
		 * The data returned by sched_highest() is stale and
                 * the chosen CPU no longer has an eligible thread.
		 */
		if (steal->tdq_load < steal_thresh ||
		    steal->tdq_transferable == 0)
			continue;
		tdq_lock_pair(tdq, steal);
		/*
		 * If we get to this point, unconditonally exit the loop
		 * to bound the time spent in the critcal section.
		 *
		 * If a thread was added while interrupts were disabled don't
		 * steal one here.
		 */
		if (tdq->tdq_load > 0) {
			TDQ_UNLOCK(steal);
			break;
		}
		/*
		 * The data returned by sched_highest() is stale and
                 * the chosen CPU no longer has an eligible thread.
		 */
		if (steal->tdq_load < steal_thresh ||
		    steal->tdq_transferable == 0) {
			TDQ_UNLOCK(steal);
			break;
		}
		/*
		 * If we fail to acquire one due to affinity restrictions,
		 * bail out and let the idle thread to a more complete search
		 * outside of a critical section.
		 */
		if (tdq_move(steal, tdq) == NULL) {
			TDQ_UNLOCK(steal);
			break;
		}
		TDQ_UNLOCK(steal);
		break;
	}
	spinlock_exit();
}
#endif

/*
 * Handle migration from sched_switch().  This happens only for
 * cpu binding.
 */
static struct mtx *
sched_switch_migrate(struct tdq *tdq, struct thread *td, int flags)
{
	struct tdq *tdn;

	KASSERT(!CPU_ABSENT(td_get_sched(td)->ts_cpu), ("sched_switch_migrate: "
	    "thread %s queued on absent CPU %d.", td->td_name,
	    td_get_sched(td)->ts_cpu));
	tdn = TDQ_CPU(td_get_sched(td)->ts_cpu);
#ifdef SMP
	tdq_load_rem(tdq, td);
	/*
	 * Do the lock dance required to avoid LOR.  We grab an extra
	 * spinlock nesting to prevent preemption while we're
	 * not holding either run-queue lock.
	 */
	spinlock_enter();
	thread_lock_block(td);	/* This releases the lock on tdq. */

	/*
	 * Acquire both run-queue locks before placing the thread on the new
	 * run-queue to avoid deadlocks created by placing a thread with a
	 * blocked lock on the run-queue of a remote processor.  The deadlock
	 * occurs when a third processor attempts to lock the two queues in
	 * question while the target processor is spinning with its own
	 * run-queue lock held while waiting for the blocked lock to clear.
	 */
	tdq_lock_pair(tdn, tdq);
	tdq_add(tdn, td, flags);
	tdq_notify(tdn, td);
	TDQ_UNLOCK(tdn);
	spinlock_exit();
#endif
	return (TDQ_LOCKPTR(tdn));
}

/*
 * Variadic version of thread_lock_unblock() that does not assume td_lock
 * is blocked.
 */
static inline void
thread_unblock_switch(struct thread *td, struct mtx *mtx)
{
	atomic_store_rel_ptr((volatile uintptr_t *)&td->td_lock,
	    (uintptr_t)mtx);
}

/*
 * Switch threads.  This function has to handle threads coming in while
 * blocked for some reason, running, or idle.  It also must deal with
 * migrating a thread from one queue to another as running threads may
 * be assigned elsewhere via binding.
 */
void
sched_switch(struct thread *td, struct thread *newtd, int flags)
{
	struct tdq *tdq;
	struct td_sched *ts;
	struct mtx *mtx;
	int srqflag;
	int cpuid, preempted;

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	KASSERT(newtd == NULL, ("sched_switch: Unsupported newtd argument"));

	cpuid = PCPU_GET(cpuid);
	tdq = TDQ_CPU(cpuid);
	ts = td_get_sched(td);
	mtx = td->td_lock;
	sched_pctcpu_update(ts, 1);
	ts->ts_rltick = ticks;
	td->td_lastcpu = td->td_oncpu;
	td->td_oncpu = NOCPU;
	preempted = (td->td_flags & TDF_SLICEEND) == 0 &&
	    (flags & SW_PREEMPT) != 0;
	td->td_flags &= ~(TDF_NEEDRESCHED | TDF_SLICEEND);
	td->td_owepreempt = 0;
	if (!TD_IS_IDLETHREAD(td))
		tdq->tdq_switchcnt++;
	/*
	 * The lock pointer in an idle thread should never change.  Reset it
	 * to CAN_RUN as well.
	 */
	if (TD_IS_IDLETHREAD(td)) {
		MPASS(td->td_lock == TDQ_LOCKPTR(tdq));
		TD_SET_CAN_RUN(td);
	} else if (TD_IS_RUNNING(td)) {
		MPASS(td->td_lock == TDQ_LOCKPTR(tdq));
		srqflag = preempted ?
		    SRQ_OURSELF|SRQ_YIELDING|SRQ_PREEMPTED :
		    SRQ_OURSELF|SRQ_YIELDING;
#ifdef SMP
		if (THREAD_CAN_MIGRATE(td) && !THREAD_CAN_SCHED(td, ts->ts_cpu))
			ts->ts_cpu = sched_pickcpu(td, 0);
#endif
		if (ts->ts_cpu == cpuid)
			tdq_runq_add(tdq, td, srqflag);
		else {
			KASSERT(THREAD_CAN_MIGRATE(td) ||
			    (ts->ts_flags & TSF_BOUND) != 0,
			    ("Thread %p shouldn't migrate", td));
			mtx = sched_switch_migrate(tdq, td, srqflag);
		}
	} else {
		/* This thread must be going to sleep. */
		TDQ_LOCK(tdq);
		mtx = thread_lock_block(td);
		tdq_load_rem(tdq, td);
#ifdef SMP
		if (tdq->tdq_load == 0)
			tdq_trysteal(tdq);
#endif
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

	/*
	 * We enter here with the thread blocked and assigned to the
	 * appropriate cpu run-queue or sleep-queue and with the current
	 * thread-queue locked.
	 */
	TDQ_LOCK_ASSERT(tdq, MA_OWNED | MA_NOTRECURSED);
	newtd = choosethread();
	/*
	 * Call the MD code to switch contexts if necessary.
	 */
	if (td != newtd) {
#ifdef	HWPMC_HOOKS
		if (PMC_PROC_IS_USING_PMCS(td->td_proc))
			PMC_SWITCH_CONTEXT(td, PMC_FN_CSW_OUT);
#endif
		SDT_PROBE2(sched, , , off__cpu, newtd, newtd->td_proc);
		lock_profile_release_lock(&TDQ_LOCKPTR(tdq)->lock_object);
		TDQ_LOCKPTR(tdq)->mtx_lock = (uintptr_t)newtd;
		sched_pctcpu_update(td_get_sched(newtd), 0);

#ifdef KDTRACE_HOOKS
		/*
		 * If DTrace has set the active vtime enum to anything
		 * other than INACTIVE (0), then it should have set the
		 * function to call.
		 */
		if (dtrace_vtime_active)
			(*dtrace_vtime_switch_func)(newtd);
#endif

		cpu_switch(td, newtd, mtx);
		/*
		 * We may return from cpu_switch on a different cpu.  However,
		 * we always return with td_lock pointing to the current cpu's
		 * run queue lock.
		 */
		cpuid = PCPU_GET(cpuid);
		tdq = TDQ_CPU(cpuid);
		lock_profile_obtain_lock_success(
		    &TDQ_LOCKPTR(tdq)->lock_object, 0, 0, __FILE__, __LINE__);

		SDT_PROBE0(sched, , , on__cpu);
#ifdef	HWPMC_HOOKS
		if (PMC_PROC_IS_USING_PMCS(td->td_proc))
			PMC_SWITCH_CONTEXT(td, PMC_FN_CSW_IN);
#endif
	} else {
		thread_unblock_switch(td, mtx);
		SDT_PROBE0(sched, , , remain__cpu);
	}

	KTR_STATE1(KTR_SCHED, "thread", sched_tdname(td), "running",
	    "prio:%d", td->td_priority);

	/*
	 * Assert that all went well and return.
	 */
	TDQ_LOCK_ASSERT(tdq, MA_OWNED|MA_NOTRECURSED);
	MPASS(td->td_lock == TDQ_LOCKPTR(tdq));
	td->td_oncpu = cpuid;
}

/*
 * Adjust thread priorities as a result of a nice request.
 */
void
sched_nice(struct proc *p, int nice)
{
	struct thread *td;

	PROC_LOCK_ASSERT(p, MA_OWNED);

	p->p_nice = nice;
	FOREACH_THREAD_IN_PROC(p, td) {
		thread_lock(td);
		sched_priority(td);
		sched_prio(td, td->td_base_user_pri);
		thread_unlock(td);
	}
}

/*
 * Record the sleep time for the interactivity scorer.
 */
void
sched_sleep(struct thread *td, int prio)
{

	THREAD_LOCK_ASSERT(td, MA_OWNED);

	td->td_slptick = ticks;
	if (TD_IS_SUSPENDED(td) || prio >= PSOCK)
		td->td_flags |= TDF_CANSWAP;
	if (PRI_BASE(td->td_pri_class) != PRI_TIMESHARE)
		return;
	if (static_boost == 1 && prio)
		sched_prio(td, prio);
	else if (static_boost && td->td_priority > static_boost)
		sched_prio(td, static_boost);
}

/*
 * Schedule a thread to resume execution and record how long it voluntarily
 * slept.  We also update the pctcpu, interactivity, and priority.
 */
void
sched_wakeup(struct thread *td)
{
	struct td_sched *ts;
	int slptick;

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	ts = td_get_sched(td);
	td->td_flags &= ~TDF_CANSWAP;
	/*
	 * If we slept for more than a tick update our interactivity and
	 * priority.
	 */
	slptick = td->td_slptick;
	td->td_slptick = 0;
	if (slptick && slptick != ticks) {
		ts->ts_slptime += (ticks - slptick) << SCHED_TICK_SHIFT;
		sched_interact_update(td);
		sched_pctcpu_update(ts, 0);
	}
	/*
	 * Reset the slice value since we slept and advanced the round-robin.
	 */
	ts->ts_slice = 0;
	sched_add(td, SRQ_BORING);
}

/*
 * Penalize the parent for creating a new child and initialize the child's
 * priority.
 */
void
sched_fork(struct thread *td, struct thread *child)
{
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	sched_pctcpu_update(td_get_sched(td), 1);
	sched_fork_thread(td, child);
	/*
	 * Penalize the parent and child for forking.
	 */
	sched_interact_fork(child);
	sched_priority(child);
	td_get_sched(td)->ts_runtime += tickincr;
	sched_interact_update(td);
	sched_priority(td);
}

/*
 * Fork a new thread, may be within the same process.
 */
void
sched_fork_thread(struct thread *td, struct thread *child)
{
	struct td_sched *ts;
	struct td_sched *ts2;
	struct tdq *tdq;

	tdq = TDQ_SELF();
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	/*
	 * Initialize child.
	 */
	ts = td_get_sched(td);
	ts2 = td_get_sched(child);
	child->td_oncpu = NOCPU;
	child->td_lastcpu = NOCPU;
	child->td_lock = TDQ_LOCKPTR(tdq);
	child->td_cpuset = cpuset_ref(td->td_cpuset);
	child->td_domain.dr_policy = td->td_cpuset->cs_domain;
	ts2->ts_cpu = ts->ts_cpu;
	ts2->ts_flags = 0;
	/*
	 * Grab our parents cpu estimation information.
	 */
	ts2->ts_ticks = ts->ts_ticks;
	ts2->ts_ltick = ts->ts_ltick;
	ts2->ts_ftick = ts->ts_ftick;
	/*
	 * Do not inherit any borrowed priority from the parent.
	 */
	child->td_priority = child->td_base_pri;
	/*
	 * And update interactivity score.
	 */
	ts2->ts_slptime = ts->ts_slptime;
	ts2->ts_runtime = ts->ts_runtime;
	/* Attempt to quickly learn interactivity. */
	ts2->ts_slice = tdq_slice(tdq) - sched_slice_min;
#ifdef KTR
	bzero(ts2->ts_name, sizeof(ts2->ts_name));
#endif
}

/*
 * Adjust the priority class of a thread.
 */
void
sched_class(struct thread *td, int class)
{

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	if (td->td_pri_class == class)
		return;
	td->td_pri_class = class;
}

/*
 * Return some of the child's priority and interactivity to the parent.
 */
void
sched_exit(struct proc *p, struct thread *child)
{
	struct thread *td;

	KTR_STATE1(KTR_SCHED, "thread", sched_tdname(child), "proc exit",
	    "prio:%d", child->td_priority);
	PROC_LOCK_ASSERT(p, MA_OWNED);
	td = FIRST_THREAD_IN_PROC(p);
	sched_exit_thread(td, child);
}

/*
 * Penalize another thread for the time spent on this one.  This helps to
 * worsen the priority and interactivity of processes which schedule batch
 * jobs such as make.  This has little effect on the make process itself but
 * causes new processes spawned by it to receive worse scores immediately.
 */
void
sched_exit_thread(struct thread *td, struct thread *child)
{

	KTR_STATE1(KTR_SCHED, "thread", sched_tdname(child), "thread exit",
	    "prio:%d", child->td_priority);
	/*
	 * Give the child's runtime to the parent without returning the
	 * sleep time as a penalty to the parent.  This causes shells that
	 * launch expensive things to mark their children as expensive.
	 */
	thread_lock(td);
	td_get_sched(td)->ts_runtime += td_get_sched(child)->ts_runtime;
	sched_interact_update(td);
	sched_priority(td);
	thread_unlock(td);
}

void
sched_preempt(struct thread *td)
{
	struct tdq *tdq;

	SDT_PROBE2(sched, , , surrender, td, td->td_proc);

	thread_lock(td);
	tdq = TDQ_SELF();
	TDQ_LOCK_ASSERT(tdq, MA_OWNED);
	tdq->tdq_ipipending = 0;
	if (td->td_priority > tdq->tdq_lowpri) {
		int flags;

		flags = SW_INVOL | SW_PREEMPT;
		if (td->td_critnest > 1)
			td->td_owepreempt = 1;
		else if (TD_IS_IDLETHREAD(td))
			mi_switch(flags | SWT_REMOTEWAKEIDLE, NULL);
		else
			mi_switch(flags | SWT_REMOTEPREEMPT, NULL);
	}
	thread_unlock(td);
}

/*
 * Fix priorities on return to user-space.  Priorities may be elevated due
 * to static priorities in msleep() or similar.
 */
void
sched_userret_slowpath(struct thread *td)
{

	thread_lock(td);
	td->td_priority = td->td_user_pri;
	td->td_base_pri = td->td_user_pri;
	tdq_setlowpri(TDQ_SELF(), td);
	thread_unlock(td);
}

/*
 * Handle a stathz tick.  This is really only relevant for timeshare
 * threads.
 */
void
sched_clock(struct thread *td)
{
	struct tdq *tdq;
	struct td_sched *ts;

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	tdq = TDQ_SELF();
#ifdef SMP
	/*
	 * We run the long term load balancer infrequently on the first cpu.
	 */
	if (balance_tdq == tdq && smp_started != 0 && rebalance != 0) {
		if (balance_ticks && --balance_ticks == 0)
			sched_balance();
	}
#endif
	/*
	 * Save the old switch count so we have a record of the last ticks
	 * activity.   Initialize the new switch count based on our load.
	 * If there is some activity seed it to reflect that.
	 */
	tdq->tdq_oldswitchcnt = tdq->tdq_switchcnt;
	tdq->tdq_switchcnt = tdq->tdq_load;
	/*
	 * Advance the insert index once for each tick to ensure that all
	 * threads get a chance to run.
	 */
	if (tdq->tdq_idx == tdq->tdq_ridx) {
		tdq->tdq_idx = (tdq->tdq_idx + 1) % RQ_NQS;
		if (TAILQ_EMPTY(&tdq->tdq_timeshare.rq_queues[tdq->tdq_ridx]))
			tdq->tdq_ridx = tdq->tdq_idx;
	}
	ts = td_get_sched(td);
	sched_pctcpu_update(ts, 1);
	if (td->td_pri_class & PRI_FIFO_BIT)
		return;
	if (PRI_BASE(td->td_pri_class) == PRI_TIMESHARE) {
		/*
		 * We used a tick; charge it to the thread so
		 * that we can compute our interactivity.
		 */
		td_get_sched(td)->ts_runtime += tickincr;
		sched_interact_update(td);
		sched_priority(td);
	}

	/*
	 * Force a context switch if the current thread has used up a full
	 * time slice (default is 100ms).
	 */
	if (!TD_IS_IDLETHREAD(td) && ++ts->ts_slice >= tdq_slice(tdq)) {
		ts->ts_slice = 0;
		td->td_flags |= TDF_NEEDRESCHED | TDF_SLICEEND;
	}
}

u_int
sched_estcpu(struct thread *td __unused)
{

	return (0);
}

/*
 * Return whether the current CPU has runnable tasks.  Used for in-kernel
 * cooperative idle threads.
 */
int
sched_runnable(void)
{
	struct tdq *tdq;
	int load;

	load = 1;

	tdq = TDQ_SELF();
	if ((curthread->td_flags & TDF_IDLETD) != 0) {
		if (tdq->tdq_load > 0)
			goto out;
	} else
		if (tdq->tdq_load - 1 > 0)
			goto out;
	load = 0;
out:
	return (load);
}

/*
 * Choose the highest priority thread to run.  The thread is removed from
 * the run-queue while running however the load remains.  For SMP we set
 * the tdq in the global idle bitmask if it idles here.
 */
struct thread *
sched_choose(void)
{
	struct thread *td;
	struct tdq *tdq;

	tdq = TDQ_SELF();
	TDQ_LOCK_ASSERT(tdq, MA_OWNED);
	td = tdq_choose(tdq);
	if (td) {
		tdq_runq_rem(tdq, td);
		tdq->tdq_lowpri = td->td_priority;
		return (td);
	}
	tdq->tdq_lowpri = PRI_MAX_IDLE;
	return (PCPU_GET(idlethread));
}

/*
 * Set owepreempt if necessary.  Preemption never happens directly in ULE,
 * we always request it once we exit a critical section.
 */
static inline void
sched_setpreempt(struct thread *td)
{
	struct thread *ctd;
	int cpri;
	int pri;

	THREAD_LOCK_ASSERT(curthread, MA_OWNED);

	ctd = curthread;
	pri = td->td_priority;
	cpri = ctd->td_priority;
	if (pri < cpri)
		ctd->td_flags |= TDF_NEEDRESCHED;
	if (panicstr != NULL || pri >= cpri || cold || TD_IS_INHIBITED(ctd))
		return;
	if (!sched_shouldpreempt(pri, cpri, 0))
		return;
	ctd->td_owepreempt = 1;
}

/*
 * Add a thread to a thread queue.  Select the appropriate runq and add the
 * thread to it.  This is the internal function called when the tdq is
 * predetermined.
 */
void
tdq_add(struct tdq *tdq, struct thread *td, int flags)
{

	TDQ_LOCK_ASSERT(tdq, MA_OWNED);
	KASSERT((td->td_inhibitors == 0),
	    ("sched_add: trying to run inhibited thread"));
	KASSERT((TD_CAN_RUN(td) || TD_IS_RUNNING(td)),
	    ("sched_add: bad thread state"));
	KASSERT(td->td_flags & TDF_INMEM,
	    ("sched_add: thread swapped out"));

	if (td->td_priority < tdq->tdq_lowpri)
		tdq->tdq_lowpri = td->td_priority;
	tdq_runq_add(tdq, td, flags);
	tdq_load_add(tdq, td);
}

/*
 * Select the target thread queue and add a thread to it.  Request
 * preemption or IPI a remote processor if required.
 */
void
sched_add(struct thread *td, int flags)
{
	struct tdq *tdq;
#ifdef SMP
	int cpu;
#endif

	KTR_STATE2(KTR_SCHED, "thread", sched_tdname(td), "runq add",
	    "prio:%d", td->td_priority, KTR_ATTR_LINKED,
	    sched_tdname(curthread));
	KTR_POINT1(KTR_SCHED, "thread", sched_tdname(curthread), "wokeup",
	    KTR_ATTR_LINKED, sched_tdname(td));
	SDT_PROBE4(sched, , , enqueue, td, td->td_proc, NULL, 
	    flags & SRQ_PREEMPTED);
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	/*
	 * Recalculate the priority before we select the target cpu or
	 * run-queue.
	 */
	if (PRI_BASE(td->td_pri_class) == PRI_TIMESHARE)
		sched_priority(td);
#ifdef SMP
	/*
	 * Pick the destination cpu and if it isn't ours transfer to the
	 * target cpu.
	 */
	cpu = sched_pickcpu(td, flags);
	tdq = sched_setcpu(td, cpu, flags);
	tdq_add(tdq, td, flags);
	if (cpu != PCPU_GET(cpuid)) {
		tdq_notify(tdq, td);
		return;
	}
#else
	tdq = TDQ_SELF();
	TDQ_LOCK(tdq);
	/*
	 * Now that the thread is moving to the run-queue, set the lock
	 * to the scheduler's lock.
	 */
	thread_lock_set(td, TDQ_LOCKPTR(tdq));
	tdq_add(tdq, td, flags);
#endif
	if (!(flags & SRQ_YIELDING))
		sched_setpreempt(td);
}

/*
 * Remove a thread from a run-queue without running it.  This is used
 * when we're stealing a thread from a remote queue.  Otherwise all threads
 * exit by calling sched_exit_thread() and sched_throw() themselves.
 */
void
sched_rem(struct thread *td)
{
	struct tdq *tdq;

	KTR_STATE1(KTR_SCHED, "thread", sched_tdname(td), "runq rem",
	    "prio:%d", td->td_priority);
	SDT_PROBE3(sched, , , dequeue, td, td->td_proc, NULL);
	tdq = TDQ_CPU(td_get_sched(td)->ts_cpu);
	TDQ_LOCK_ASSERT(tdq, MA_OWNED);
	MPASS(td->td_lock == TDQ_LOCKPTR(tdq));
	KASSERT(TD_ON_RUNQ(td),
	    ("sched_rem: thread not on run queue"));
	tdq_runq_rem(tdq, td);
	tdq_load_rem(tdq, td);
	TD_SET_CAN_RUN(td);
	if (td->td_priority == tdq->tdq_lowpri)
		tdq_setlowpri(tdq, NULL);
}

/*
 * Fetch cpu utilization information.  Updates on demand.
 */
fixpt_t
sched_pctcpu(struct thread *td)
{
	fixpt_t pctcpu;
	struct td_sched *ts;

	pctcpu = 0;
	ts = td_get_sched(td);

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	sched_pctcpu_update(ts, TD_IS_RUNNING(td));
	if (ts->ts_ticks) {
		int rtick;

		/* How many rtick per second ? */
		rtick = min(SCHED_TICK_HZ(ts) / SCHED_TICK_SECS, hz);
		pctcpu = (FSCALE * ((FSCALE * rtick)/hz)) >> FSHIFT;
	}

	return (pctcpu);
}

/*
 * Enforce affinity settings for a thread.  Called after adjustments to
 * cpumask.
 */
void
sched_affinity(struct thread *td)
{
#ifdef SMP
	struct td_sched *ts;

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	ts = td_get_sched(td);
	if (THREAD_CAN_SCHED(td, ts->ts_cpu))
		return;
	if (TD_ON_RUNQ(td)) {
		sched_rem(td);
		sched_add(td, SRQ_BORING);
		return;
	}
	if (!TD_IS_RUNNING(td))
		return;
	/*
	 * Force a switch before returning to userspace.  If the
	 * target thread is not running locally send an ipi to force
	 * the issue.
	 */
	td->td_flags |= TDF_NEEDRESCHED;
	if (td != curthread)
		ipi_cpu(ts->ts_cpu, IPI_PREEMPT);
#endif
}

/*
 * Bind a thread to a target cpu.
 */
void
sched_bind(struct thread *td, int cpu)
{
	struct td_sched *ts;

	THREAD_LOCK_ASSERT(td, MA_OWNED|MA_NOTRECURSED);
	KASSERT(td == curthread, ("sched_bind: can only bind curthread"));
	ts = td_get_sched(td);
	if (ts->ts_flags & TSF_BOUND)
		sched_unbind(td);
	KASSERT(THREAD_CAN_MIGRATE(td), ("%p must be migratable", td));
	ts->ts_flags |= TSF_BOUND;
	sched_pin();
	if (PCPU_GET(cpuid) == cpu)
		return;
	ts->ts_cpu = cpu;
	/* When we return from mi_switch we'll be on the correct cpu. */
	mi_switch(SW_VOL, NULL);
}

/*
 * Release a bound thread.
 */
void
sched_unbind(struct thread *td)
{
	struct td_sched *ts;

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	KASSERT(td == curthread, ("sched_unbind: can only bind curthread"));
	ts = td_get_sched(td);
	if ((ts->ts_flags & TSF_BOUND) == 0)
		return;
	ts->ts_flags &= ~TSF_BOUND;
	sched_unpin();
}

int
sched_is_bound(struct thread *td)
{
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	return (td_get_sched(td)->ts_flags & TSF_BOUND);
}

/*
 * Basic yield call.
 */
void
sched_relinquish(struct thread *td)
{
	thread_lock(td);
	mi_switch(SW_VOL | SWT_RELINQUISH, NULL);
	thread_unlock(td);
}

/*
 * Return the total system load.
 */
int
sched_load(void)
{
#ifdef SMP
	int total;
	int i;

	total = 0;
	CPU_FOREACH(i)
		total += TDQ_CPU(i)->tdq_sysload;
	return (total);
#else
	return (TDQ_SELF()->tdq_sysload);
#endif
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

#ifdef SMP
#define	TDQ_IDLESPIN(tdq)						\
    ((tdq)->tdq_cg != NULL && ((tdq)->tdq_cg->cg_flags & CG_FLAG_THREAD) == 0)
#else
#define	TDQ_IDLESPIN(tdq)	1
#endif

/*
 * The actual idle process.
 */
void
sched_idletd(void *dummy)
{
	struct thread *td;
	struct tdq *tdq;
	int oldswitchcnt, switchcnt;
	int i;

	mtx_assert(&Giant, MA_NOTOWNED);
	td = curthread;
	tdq = TDQ_SELF();
	THREAD_NO_SLEEPING();
	oldswitchcnt = -1;
	for (;;) {
		if (tdq->tdq_load) {
			thread_lock(td);
			mi_switch(SW_VOL | SWT_IDLE, NULL);
			thread_unlock(td);
		}
		switchcnt = tdq->tdq_switchcnt + tdq->tdq_oldswitchcnt;
#ifdef SMP
		if (always_steal || switchcnt != oldswitchcnt) {
			oldswitchcnt = switchcnt;
			if (tdq_idled(tdq) == 0)
				continue;
		}
		switchcnt = tdq->tdq_switchcnt + tdq->tdq_oldswitchcnt;
#else
		oldswitchcnt = switchcnt;
#endif
		/*
		 * If we're switching very frequently, spin while checking
		 * for load rather than entering a low power state that 
		 * may require an IPI.  However, don't do any busy
		 * loops while on SMT machines as this simply steals
		 * cycles from cores doing useful work.
		 */
		if (TDQ_IDLESPIN(tdq) && switchcnt > sched_idlespinthresh) {
			for (i = 0; i < sched_idlespins; i++) {
				if (tdq->tdq_load)
					break;
				cpu_spinwait();
			}
		}

		/* If there was context switch during spin, restart it. */
		switchcnt = tdq->tdq_switchcnt + tdq->tdq_oldswitchcnt;
		if (tdq->tdq_load != 0 || switchcnt != oldswitchcnt)
			continue;

		/* Run main MD idle handler. */
		tdq->tdq_cpu_idle = 1;
		/*
		 * Make sure that tdq_cpu_idle update is globally visible
		 * before cpu_idle() read tdq_load.  The order is important
		 * to avoid race with tdq_notify.
		 */
		atomic_thread_fence_seq_cst();
		/*
		 * Checking for again after the fence picks up assigned
		 * threads often enough to make it worthwhile to do so in
		 * order to avoid calling cpu_idle().
		 */
		if (tdq->tdq_load != 0) {
			tdq->tdq_cpu_idle = 0;
			continue;
		}
		cpu_idle(switchcnt * 4 > sched_idlespinthresh);
		tdq->tdq_cpu_idle = 0;

		/*
		 * Account thread-less hardware interrupts and
		 * other wakeup reasons equal to context switches.
		 */
		switchcnt = tdq->tdq_switchcnt + tdq->tdq_oldswitchcnt;
		if (switchcnt != oldswitchcnt)
			continue;
		tdq->tdq_switchcnt++;
		oldswitchcnt++;
	}
}

/*
 * A CPU is entering for the first time or a thread is exiting.
 */
void
sched_throw(struct thread *td)
{
	struct thread *newtd;
	struct tdq *tdq;

	tdq = TDQ_SELF();
	if (td == NULL) {
		/* Correct spinlock nesting and acquire the correct lock. */
		TDQ_LOCK(tdq);
		spinlock_exit();
		PCPU_SET(switchtime, cpu_ticks());
		PCPU_SET(switchticks, ticks);
	} else {
		MPASS(td->td_lock == TDQ_LOCKPTR(tdq));
		tdq_load_rem(tdq, td);
		lock_profile_release_lock(&TDQ_LOCKPTR(tdq)->lock_object);
		td->td_lastcpu = td->td_oncpu;
		td->td_oncpu = NOCPU;
	}
	KASSERT(curthread->td_md.md_spinlock_count == 1, ("invalid count"));
	newtd = choosethread();
	TDQ_LOCKPTR(tdq)->mtx_lock = (uintptr_t)newtd;
	cpu_throw(td, newtd);		/* doesn't return */
}

/*
 * This is called from fork_exit().  Just acquire the correct locks and
 * let fork do the rest of the work.
 */
void
sched_fork_exit(struct thread *td)
{
	struct tdq *tdq;
	int cpuid;

	/*
	 * Finish setting up thread glue so that it begins execution in a
	 * non-nested critical section with the scheduler lock held.
	 */
	cpuid = PCPU_GET(cpuid);
	tdq = TDQ_CPU(cpuid);
	if (TD_IS_IDLETHREAD(td))
		td->td_lock = TDQ_LOCKPTR(tdq);
	MPASS(td->td_lock == TDQ_LOCKPTR(tdq));
	td->td_oncpu = cpuid;
	TDQ_LOCK_ASSERT(tdq, MA_OWNED | MA_NOTRECURSED);
	lock_profile_obtain_lock_success(
	    &TDQ_LOCKPTR(tdq)->lock_object, 0, 0, __FILE__, __LINE__);

	KTR_STATE1(KTR_SCHED, "thread", sched_tdname(td), "running",
	    "prio:%d", td->td_priority);
	SDT_PROBE0(sched, , , on__cpu);
}

/*
 * Create on first use to catch odd startup conditons.
 */
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

#ifdef SMP

/*
 * Build the CPU topology dump string. Is recursively called to collect
 * the topology tree.
 */
static int
sysctl_kern_sched_topology_spec_internal(struct sbuf *sb, struct cpu_group *cg,
    int indent)
{
	char cpusetbuf[CPUSETBUFSIZ];
	int i, first;

	sbuf_printf(sb, "%*s<group level=\"%d\" cache-level=\"%d\">\n", indent,
	    "", 1 + indent / 2, cg->cg_level);
	sbuf_printf(sb, "%*s <cpu count=\"%d\" mask=\"%s\">", indent, "",
	    cg->cg_count, cpusetobj_strprint(cpusetbuf, &cg->cg_mask));
	first = TRUE;
	for (i = 0; i < MAXCPU; i++) {
		if (CPU_ISSET(i, &cg->cg_mask)) {
			if (!first)
				sbuf_printf(sb, ", ");
			else
				first = FALSE;
			sbuf_printf(sb, "%d", i);
		}
	}
	sbuf_printf(sb, "</cpu>\n");

	if (cg->cg_flags != 0) {
		sbuf_printf(sb, "%*s <flags>", indent, "");
		if ((cg->cg_flags & CG_FLAG_HTT) != 0)
			sbuf_printf(sb, "<flag name=\"HTT\">HTT group</flag>");
		if ((cg->cg_flags & CG_FLAG_THREAD) != 0)
			sbuf_printf(sb, "<flag name=\"THREAD\">THREAD group</flag>");
		if ((cg->cg_flags & CG_FLAG_SMT) != 0)
			sbuf_printf(sb, "<flag name=\"SMT\">SMT group</flag>");
		sbuf_printf(sb, "</flags>\n");
	}

	if (cg->cg_children > 0) {
		sbuf_printf(sb, "%*s <children>\n", indent, "");
		for (i = 0; i < cg->cg_children; i++)
			sysctl_kern_sched_topology_spec_internal(sb, 
			    &cg->cg_child[i], indent+2);
		sbuf_printf(sb, "%*s </children>\n", indent, "");
	}
	sbuf_printf(sb, "%*s</group>\n", indent, "");
	return (0);
}

/*
 * Sysctl handler for retrieving topology dump. It's a wrapper for
 * the recursive sysctl_kern_smp_topology_spec_internal().
 */
static int
sysctl_kern_sched_topology_spec(SYSCTL_HANDLER_ARGS)
{
	struct sbuf *topo;
	int err;

	KASSERT(cpu_top != NULL, ("cpu_top isn't initialized"));

	topo = sbuf_new_for_sysctl(NULL, NULL, 512, req);
	if (topo == NULL)
		return (ENOMEM);

	sbuf_printf(topo, "<groups>\n");
	err = sysctl_kern_sched_topology_spec_internal(topo, cpu_top, 1);
	sbuf_printf(topo, "</groups>\n");

	if (err == 0) {
		err = sbuf_finish(topo);
	}
	sbuf_delete(topo);
	return (err);
}

#endif

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
	sched_slice_min = sched_slice / SCHED_SLICE_MIN_DIVISOR;
	hogticks = imax(1, (2 * hz * sched_slice + realstathz / 2) /
	    realstathz);
	return (0);
}

SYSCTL_NODE(_kern, OID_AUTO, sched, CTLFLAG_RW, 0, "Scheduler");
SYSCTL_STRING(_kern_sched, OID_AUTO, name, CTLFLAG_RD, "ULE", 0,
    "Scheduler name");
SYSCTL_PROC(_kern_sched, OID_AUTO, quantum, CTLTYPE_INT | CTLFLAG_RW,
    NULL, 0, sysctl_kern_quantum, "I",
    "Quantum for timeshare threads in microseconds");
SYSCTL_INT(_kern_sched, OID_AUTO, slice, CTLFLAG_RW, &sched_slice, 0,
    "Quantum for timeshare threads in stathz ticks");
SYSCTL_INT(_kern_sched, OID_AUTO, interact, CTLFLAG_RW, &sched_interact, 0,
    "Interactivity score threshold");
SYSCTL_INT(_kern_sched, OID_AUTO, preempt_thresh, CTLFLAG_RW,
    &preempt_thresh, 0,
    "Maximal (lowest) priority for preemption");
SYSCTL_INT(_kern_sched, OID_AUTO, static_boost, CTLFLAG_RW, &static_boost, 0,
    "Assign static kernel priorities to sleeping threads");
SYSCTL_INT(_kern_sched, OID_AUTO, idlespins, CTLFLAG_RW, &sched_idlespins, 0,
    "Number of times idle thread will spin waiting for new work");
SYSCTL_INT(_kern_sched, OID_AUTO, idlespinthresh, CTLFLAG_RW,
    &sched_idlespinthresh, 0,
    "Threshold before we will permit idle thread spinning");
#ifdef SMP
SYSCTL_INT(_kern_sched, OID_AUTO, affinity, CTLFLAG_RW, &affinity, 0,
    "Number of hz ticks to keep thread affinity for");
SYSCTL_INT(_kern_sched, OID_AUTO, balance, CTLFLAG_RW, &rebalance, 0,
    "Enables the long-term load balancer");
SYSCTL_INT(_kern_sched, OID_AUTO, balance_interval, CTLFLAG_RW,
    &balance_interval, 0,
    "Average period in stathz ticks to run the long-term balancer");
SYSCTL_INT(_kern_sched, OID_AUTO, steal_idle, CTLFLAG_RW, &steal_idle, 0,
    "Attempts to steal work from other cores before idling");
SYSCTL_INT(_kern_sched, OID_AUTO, steal_thresh, CTLFLAG_RW, &steal_thresh, 0,
    "Minimum load on remote CPU before we'll steal");
SYSCTL_INT(_kern_sched, OID_AUTO, trysteal_limit, CTLFLAG_RW, &trysteal_limit,
    0, "Topological distance limit for stealing threads in sched_switch()");
SYSCTL_INT(_kern_sched, OID_AUTO, always_steal, CTLFLAG_RW, &always_steal, 0,
    "Always run the stealer from the idle thread");
SYSCTL_PROC(_kern_sched, OID_AUTO, topology_spec, CTLTYPE_STRING |
    CTLFLAG_MPSAFE | CTLFLAG_RD, NULL, 0, sysctl_kern_sched_topology_spec, "A",
    "XML dump of detected CPU topology");
#endif

/* ps compat.  All cpu percentages from ULE are weighted. */
static int ccpu = 0;
SYSCTL_INT(_kern, OID_AUTO, ccpu, CTLFLAG_RD, &ccpu, 0, "");
