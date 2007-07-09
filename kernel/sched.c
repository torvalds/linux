/*
 *  kernel/sched.c
 *
 *  Kernel scheduler and related syscalls
 *
 *  Copyright (C) 1991-2002  Linus Torvalds
 *
 *  1996-12-23  Modified by Dave Grothe to fix bugs in semaphores and
 *		make semaphores SMP safe
 *  1998-11-19	Implemented schedule_timeout() and related stuff
 *		by Andrea Arcangeli
 *  2002-01-04	New ultra-scalable O(1) scheduler by Ingo Molnar:
 *		hybrid priority-list and round-robin design with
 *		an array-switch method of distributing timeslices
 *		and per-CPU runqueues.  Cleanups and useful suggestions
 *		by Davide Libenzi, preemptible kernel bits by Robert Love.
 *  2003-09-03	Interactivity tuning by Con Kolivas.
 *  2004-04-02	Scheduler domains code by Nick Piggin
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/highmem.h>
#include <linux/smp_lock.h>
#include <asm/mmu_context.h>
#include <linux/interrupt.h>
#include <linux/capability.h>
#include <linux/completion.h>
#include <linux/kernel_stat.h>
#include <linux/debug_locks.h>
#include <linux/security.h>
#include <linux/notifier.h>
#include <linux/profile.h>
#include <linux/freezer.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/timer.h>
#include <linux/rcupdate.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/percpu.h>
#include <linux/kthread.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/times.h>
#include <linux/tsacct_kern.h>
#include <linux/kprobes.h>
#include <linux/delayacct.h>
#include <linux/reciprocal_div.h>
#include <linux/unistd.h>

#include <asm/tlb.h>

/*
 * Scheduler clock - returns current time in nanosec units.
 * This is default implementation.
 * Architectures and sub-architectures can override this.
 */
unsigned long long __attribute__((weak)) sched_clock(void)
{
	return (unsigned long long)jiffies * (1000000000 / HZ);
}

/*
 * Convert user-nice values [ -20 ... 0 ... 19 ]
 * to static priority [ MAX_RT_PRIO..MAX_PRIO-1 ],
 * and back.
 */
#define NICE_TO_PRIO(nice)	(MAX_RT_PRIO + (nice) + 20)
#define PRIO_TO_NICE(prio)	((prio) - MAX_RT_PRIO - 20)
#define TASK_NICE(p)		PRIO_TO_NICE((p)->static_prio)

/*
 * 'User priority' is the nice value converted to something we
 * can work with better when scaling various scheduler parameters,
 * it's a [ 0 ... 39 ] range.
 */
#define USER_PRIO(p)		((p)-MAX_RT_PRIO)
#define TASK_USER_PRIO(p)	USER_PRIO((p)->static_prio)
#define MAX_USER_PRIO		(USER_PRIO(MAX_PRIO))

/*
 * Some helpers for converting nanosecond timing to jiffy resolution
 */
#define NS_TO_JIFFIES(TIME)	((TIME) / (1000000000 / HZ))
#define JIFFIES_TO_NS(TIME)	((TIME) * (1000000000 / HZ))

#define NICE_0_LOAD		SCHED_LOAD_SCALE
#define NICE_0_SHIFT		SCHED_LOAD_SHIFT

/*
 * These are the 'tuning knobs' of the scheduler:
 *
 * Minimum timeslice is 5 msecs (or 1 jiffy, whichever is larger),
 * default timeslice is 100 msecs, maximum timeslice is 800 msecs.
 * Timeslices get refilled after they expire.
 */
#define MIN_TIMESLICE		max(5 * HZ / 1000, 1)
#define DEF_TIMESLICE		(100 * HZ / 1000)

#ifdef CONFIG_SMP
/*
 * Divide a load by a sched group cpu_power : (load / sg->__cpu_power)
 * Since cpu_power is a 'constant', we can use a reciprocal divide.
 */
static inline u32 sg_div_cpu_power(const struct sched_group *sg, u32 load)
{
	return reciprocal_divide(load, sg->reciprocal_cpu_power);
}

/*
 * Each time a sched group cpu_power is changed,
 * we must compute its reciprocal value
 */
static inline void sg_inc_cpu_power(struct sched_group *sg, u32 val)
{
	sg->__cpu_power += val;
	sg->reciprocal_cpu_power = reciprocal_value(sg->__cpu_power);
}
#endif

#define SCALE_PRIO(x, prio) \
	max(x * (MAX_PRIO - prio) / (MAX_USER_PRIO / 2), MIN_TIMESLICE)

/*
 * static_prio_timeslice() scales user-nice values [ -20 ... 0 ... 19 ]
 * to time slice values: [800ms ... 100ms ... 5ms]
 */
static unsigned int static_prio_timeslice(int static_prio)
{
	if (static_prio == NICE_TO_PRIO(19))
		return 1;

	if (static_prio < NICE_TO_PRIO(0))
		return SCALE_PRIO(DEF_TIMESLICE * 4, static_prio);
	else
		return SCALE_PRIO(DEF_TIMESLICE, static_prio);
}

static inline int rt_policy(int policy)
{
	if (unlikely(policy == SCHED_FIFO) || unlikely(policy == SCHED_RR))
		return 1;
	return 0;
}

static inline int task_has_rt_policy(struct task_struct *p)
{
	return rt_policy(p->policy);
}

/*
 * This is the priority-queue data structure of the RT scheduling class:
 */
struct rt_prio_array {
	DECLARE_BITMAP(bitmap, MAX_RT_PRIO+1); /* include 1 bit for delimiter */
	struct list_head queue[MAX_RT_PRIO];
};

struct load_stat {
	struct load_weight load;
	u64 load_update_start, load_update_last;
	unsigned long delta_fair, delta_exec, delta_stat;
};

/* CFS-related fields in a runqueue */
struct cfs_rq {
	struct load_weight load;
	unsigned long nr_running;

	s64 fair_clock;
	u64 exec_clock;
	s64 wait_runtime;
	u64 sleeper_bonus;
	unsigned long wait_runtime_overruns, wait_runtime_underruns;

	struct rb_root tasks_timeline;
	struct rb_node *rb_leftmost;
	struct rb_node *rb_load_balance_curr;
#ifdef CONFIG_FAIR_GROUP_SCHED
	/* 'curr' points to currently running entity on this cfs_rq.
	 * It is set to NULL otherwise (i.e when none are currently running).
	 */
	struct sched_entity *curr;
	struct rq *rq;	/* cpu runqueue to which this cfs_rq is attached */

	/* leaf cfs_rqs are those that hold tasks (lowest schedulable entity in
	 * a hierarchy). Non-leaf lrqs hold other higher schedulable entities
	 * (like users, containers etc.)
	 *
	 * leaf_cfs_rq_list ties together list of leaf cfs_rq's in a cpu. This
	 * list is used during load balance.
	 */
	struct list_head leaf_cfs_rq_list; /* Better name : task_cfs_rq_list? */
#endif
};

/* Real-Time classes' related field in a runqueue: */
struct rt_rq {
	struct rt_prio_array active;
	int rt_load_balance_idx;
	struct list_head *rt_load_balance_head, *rt_load_balance_curr;
};

/*
 * This is the main, per-CPU runqueue data structure.
 *
 * Locking rule: those places that want to lock multiple runqueues
 * (such as the load balancing or the thread migration code), lock
 * acquire operations must be ordered by ascending &runqueue.
 */
struct rq {
	spinlock_t lock;	/* runqueue lock */

	/*
	 * nr_running and cpu_load should be in the same cacheline because
	 * remote CPUs use both these fields when doing load calculation.
	 */
	unsigned long nr_running;
	#define CPU_LOAD_IDX_MAX 5
	unsigned long cpu_load[CPU_LOAD_IDX_MAX];
	unsigned char idle_at_tick;
#ifdef CONFIG_NO_HZ
	unsigned char in_nohz_recently;
#endif
	struct load_stat ls;	/* capture load from *all* tasks on this cpu */
	unsigned long nr_load_updates;
	u64 nr_switches;

	struct cfs_rq cfs;
#ifdef CONFIG_FAIR_GROUP_SCHED
	struct list_head leaf_cfs_rq_list; /* list of leaf cfs_rq on this cpu */
#endif
	struct rt_rq  rt;

	/*
	 * This is part of a global counter where only the total sum
	 * over all CPUs matters. A task can increase this counter on
	 * one CPU and if it got migrated afterwards it may decrease
	 * it on another CPU. Always updated under the runqueue lock:
	 */
	unsigned long nr_uninterruptible;

	struct task_struct *curr, *idle;
	unsigned long next_balance;
	struct mm_struct *prev_mm;

	u64 clock, prev_clock_raw;
	s64 clock_max_delta;

	unsigned int clock_warps, clock_overflows;
	unsigned int clock_unstable_events;

	struct sched_class *load_balance_class;

	atomic_t nr_iowait;

#ifdef CONFIG_SMP
	struct sched_domain *sd;

	/* For active balancing */
	int active_balance;
	int push_cpu;
	int cpu;		/* cpu of this runqueue */

	struct task_struct *migration_thread;
	struct list_head migration_queue;
#endif

#ifdef CONFIG_SCHEDSTATS
	/* latency stats */
	struct sched_info rq_sched_info;

	/* sys_sched_yield() stats */
	unsigned long yld_exp_empty;
	unsigned long yld_act_empty;
	unsigned long yld_both_empty;
	unsigned long yld_cnt;

	/* schedule() stats */
	unsigned long sched_switch;
	unsigned long sched_cnt;
	unsigned long sched_goidle;

	/* try_to_wake_up() stats */
	unsigned long ttwu_cnt;
	unsigned long ttwu_local;
#endif
	struct lock_class_key rq_lock_key;
};

static DEFINE_PER_CPU(struct rq, runqueues) ____cacheline_aligned_in_smp;
static DEFINE_MUTEX(sched_hotcpu_mutex);

static inline void check_preempt_curr(struct rq *rq, struct task_struct *p)
{
	rq->curr->sched_class->check_preempt_curr(rq, p);
}

static inline int cpu_of(struct rq *rq)
{
#ifdef CONFIG_SMP
	return rq->cpu;
#else
	return 0;
#endif
}

/*
 * Per-runqueue clock, as finegrained as the platform can give us:
 */
static unsigned long long __rq_clock(struct rq *rq)
{
	u64 prev_raw = rq->prev_clock_raw;
	u64 now = sched_clock();
	s64 delta = now - prev_raw;
	u64 clock = rq->clock;

	/*
	 * Protect against sched_clock() occasionally going backwards:
	 */
	if (unlikely(delta < 0)) {
		clock++;
		rq->clock_warps++;
	} else {
		/*
		 * Catch too large forward jumps too:
		 */
		if (unlikely(delta > 2*TICK_NSEC)) {
			clock++;
			rq->clock_overflows++;
		} else {
			if (unlikely(delta > rq->clock_max_delta))
				rq->clock_max_delta = delta;
			clock += delta;
		}
	}

	rq->prev_clock_raw = now;
	rq->clock = clock;

	return clock;
}

static inline unsigned long long rq_clock(struct rq *rq)
{
	int this_cpu = smp_processor_id();

	if (this_cpu == cpu_of(rq))
		return __rq_clock(rq);

	return rq->clock;
}

/*
 * The domain tree (rq->sd) is protected by RCU's quiescent state transition.
 * See detach_destroy_domains: synchronize_sched for details.
 *
 * The domain tree of any CPU may only be accessed from within
 * preempt-disabled sections.
 */
#define for_each_domain(cpu, __sd) \
	for (__sd = rcu_dereference(cpu_rq(cpu)->sd); __sd; __sd = __sd->parent)

#define cpu_rq(cpu)		(&per_cpu(runqueues, (cpu)))
#define this_rq()		(&__get_cpu_var(runqueues))
#define task_rq(p)		cpu_rq(task_cpu(p))
#define cpu_curr(cpu)		(cpu_rq(cpu)->curr)

#ifdef CONFIG_FAIR_GROUP_SCHED
/* Change a task's ->cfs_rq if it moves across CPUs */
static inline void set_task_cfs_rq(struct task_struct *p)
{
	p->se.cfs_rq = &task_rq(p)->cfs;
}
#else
static inline void set_task_cfs_rq(struct task_struct *p)
{
}
#endif

#ifndef prepare_arch_switch
# define prepare_arch_switch(next)	do { } while (0)
#endif
#ifndef finish_arch_switch
# define finish_arch_switch(prev)	do { } while (0)
#endif

#ifndef __ARCH_WANT_UNLOCKED_CTXSW
static inline int task_running(struct rq *rq, struct task_struct *p)
{
	return rq->curr == p;
}

static inline void prepare_lock_switch(struct rq *rq, struct task_struct *next)
{
}

static inline void finish_lock_switch(struct rq *rq, struct task_struct *prev)
{
#ifdef CONFIG_DEBUG_SPINLOCK
	/* this is a valid case when another task releases the spinlock */
	rq->lock.owner = current;
#endif
	/*
	 * If we are tracking spinlock dependencies then we have to
	 * fix up the runqueue lock - which gets 'carried over' from
	 * prev into current:
	 */
	spin_acquire(&rq->lock.dep_map, 0, 0, _THIS_IP_);

	spin_unlock_irq(&rq->lock);
}

#else /* __ARCH_WANT_UNLOCKED_CTXSW */
static inline int task_running(struct rq *rq, struct task_struct *p)
{
#ifdef CONFIG_SMP
	return p->oncpu;
#else
	return rq->curr == p;
#endif
}

static inline void prepare_lock_switch(struct rq *rq, struct task_struct *next)
{
#ifdef CONFIG_SMP
	/*
	 * We can optimise this out completely for !SMP, because the
	 * SMP rebalancing from interrupt is the only thing that cares
	 * here.
	 */
	next->oncpu = 1;
#endif
#ifdef __ARCH_WANT_INTERRUPTS_ON_CTXSW
	spin_unlock_irq(&rq->lock);
#else
	spin_unlock(&rq->lock);
#endif
}

static inline void finish_lock_switch(struct rq *rq, struct task_struct *prev)
{
#ifdef CONFIG_SMP
	/*
	 * After ->oncpu is cleared, the task can be moved to a different CPU.
	 * We must ensure this doesn't happen until the switch is completely
	 * finished.
	 */
	smp_wmb();
	prev->oncpu = 0;
#endif
#ifndef __ARCH_WANT_INTERRUPTS_ON_CTXSW
	local_irq_enable();
#endif
}
#endif /* __ARCH_WANT_UNLOCKED_CTXSW */

/*
 * __task_rq_lock - lock the runqueue a given task resides on.
 * Must be called interrupts disabled.
 */
static inline struct rq *__task_rq_lock(struct task_struct *p)
	__acquires(rq->lock)
{
	struct rq *rq;

repeat_lock_task:
	rq = task_rq(p);
	spin_lock(&rq->lock);
	if (unlikely(rq != task_rq(p))) {
		spin_unlock(&rq->lock);
		goto repeat_lock_task;
	}
	return rq;
}

/*
 * task_rq_lock - lock the runqueue a given task resides on and disable
 * interrupts.  Note the ordering: we can safely lookup the task_rq without
 * explicitly disabling preemption.
 */
static struct rq *task_rq_lock(struct task_struct *p, unsigned long *flags)
	__acquires(rq->lock)
{
	struct rq *rq;

repeat_lock_task:
	local_irq_save(*flags);
	rq = task_rq(p);
	spin_lock(&rq->lock);
	if (unlikely(rq != task_rq(p))) {
		spin_unlock_irqrestore(&rq->lock, *flags);
		goto repeat_lock_task;
	}
	return rq;
}

static inline void __task_rq_unlock(struct rq *rq)
	__releases(rq->lock)
{
	spin_unlock(&rq->lock);
}

static inline void task_rq_unlock(struct rq *rq, unsigned long *flags)
	__releases(rq->lock)
{
	spin_unlock_irqrestore(&rq->lock, *flags);
}

/*
 * this_rq_lock - lock this runqueue and disable interrupts.
 */
static inline struct rq *this_rq_lock(void)
	__acquires(rq->lock)
{
	struct rq *rq;

	local_irq_disable();
	rq = this_rq();
	spin_lock(&rq->lock);

	return rq;
}

/*
 * CPU frequency is/was unstable - start new by setting prev_clock_raw:
 */
void sched_clock_unstable_event(void)
{
	unsigned long flags;
	struct rq *rq;

	rq = task_rq_lock(current, &flags);
	rq->prev_clock_raw = sched_clock();
	rq->clock_unstable_events++;
	task_rq_unlock(rq, &flags);
}

/*
 * resched_task - mark a task 'to be rescheduled now'.
 *
 * On UP this means the setting of the need_resched flag, on SMP it
 * might also involve a cross-CPU call to trigger the scheduler on
 * the target CPU.
 */
#ifdef CONFIG_SMP

#ifndef tsk_is_polling
#define tsk_is_polling(t) test_tsk_thread_flag(t, TIF_POLLING_NRFLAG)
#endif

static void resched_task(struct task_struct *p)
{
	int cpu;

	assert_spin_locked(&task_rq(p)->lock);

	if (unlikely(test_tsk_thread_flag(p, TIF_NEED_RESCHED)))
		return;

	set_tsk_thread_flag(p, TIF_NEED_RESCHED);

	cpu = task_cpu(p);
	if (cpu == smp_processor_id())
		return;

	/* NEED_RESCHED must be visible before we test polling */
	smp_mb();
	if (!tsk_is_polling(p))
		smp_send_reschedule(cpu);
}

static void resched_cpu(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	if (!spin_trylock_irqsave(&rq->lock, flags))
		return;
	resched_task(cpu_curr(cpu));
	spin_unlock_irqrestore(&rq->lock, flags);
}
#else
static inline void resched_task(struct task_struct *p)
{
	assert_spin_locked(&task_rq(p)->lock);
	set_tsk_need_resched(p);
}
#endif

static u64 div64_likely32(u64 divident, unsigned long divisor)
{
#if BITS_PER_LONG == 32
	if (likely(divident <= 0xffffffffULL))
		return (u32)divident / divisor;
	do_div(divident, divisor);

	return divident;
#else
	return divident / divisor;
#endif
}

#if BITS_PER_LONG == 32
# define WMULT_CONST	(~0UL)
#else
# define WMULT_CONST	(1UL << 32)
#endif

#define WMULT_SHIFT	32

static inline unsigned long
calc_delta_mine(unsigned long delta_exec, unsigned long weight,
		struct load_weight *lw)
{
	u64 tmp;

	if (unlikely(!lw->inv_weight))
		lw->inv_weight = WMULT_CONST / lw->weight;

	tmp = (u64)delta_exec * weight;
	/*
	 * Check whether we'd overflow the 64-bit multiplication:
	 */
	if (unlikely(tmp > WMULT_CONST)) {
		tmp = ((tmp >> WMULT_SHIFT/2) * lw->inv_weight)
				>> (WMULT_SHIFT/2);
	} else {
		tmp = (tmp * lw->inv_weight) >> WMULT_SHIFT;
	}

	return (unsigned long)min(tmp, (u64)sysctl_sched_runtime_limit);
}

static inline unsigned long
calc_delta_fair(unsigned long delta_exec, struct load_weight *lw)
{
	return calc_delta_mine(delta_exec, NICE_0_LOAD, lw);
}

static void update_load_add(struct load_weight *lw, unsigned long inc)
{
	lw->weight += inc;
	lw->inv_weight = 0;
}

static void update_load_sub(struct load_weight *lw, unsigned long dec)
{
	lw->weight -= dec;
	lw->inv_weight = 0;
}

static void __update_curr_load(struct rq *rq, struct load_stat *ls)
{
	if (rq->curr != rq->idle && ls->load.weight) {
		ls->delta_exec += ls->delta_stat;
		ls->delta_fair += calc_delta_fair(ls->delta_stat, &ls->load);
		ls->delta_stat = 0;
	}
}

/*
 * Update delta_exec, delta_fair fields for rq.
 *
 * delta_fair clock advances at a rate inversely proportional to
 * total load (rq->ls.load.weight) on the runqueue, while
 * delta_exec advances at the same rate as wall-clock (provided
 * cpu is not idle).
 *
 * delta_exec / delta_fair is a measure of the (smoothened) load on this
 * runqueue over any given interval. This (smoothened) load is used
 * during load balance.
 *
 * This function is called /before/ updating rq->ls.load
 * and when switching tasks.
 */
static void update_curr_load(struct rq *rq, u64 now)
{
	struct load_stat *ls = &rq->ls;
	u64 start;

	start = ls->load_update_start;
	ls->load_update_start = now;
	ls->delta_stat += now - start;
	/*
	 * Stagger updates to ls->delta_fair. Very frequent updates
	 * can be expensive.
	 */
	if (ls->delta_stat >= sysctl_sched_stat_granularity)
		__update_curr_load(rq, ls);
}

/*
 * To aid in avoiding the subversion of "niceness" due to uneven distribution
 * of tasks with abnormal "nice" values across CPUs the contribution that
 * each task makes to its run queue's load is weighted according to its
 * scheduling class and "nice" value.  For SCHED_NORMAL tasks this is just a
 * scaled version of the new time slice allocation that they receive on time
 * slice expiry etc.
 */

/*
 * Assume: static_prio_timeslice(NICE_TO_PRIO(0)) == DEF_TIMESLICE
 * If static_prio_timeslice() is ever changed to break this assumption then
 * this code will need modification
 */
#define TIME_SLICE_NICE_ZERO DEF_TIMESLICE
#define load_weight(lp) \
	(((lp) * SCHED_LOAD_SCALE) / TIME_SLICE_NICE_ZERO)
#define PRIO_TO_LOAD_WEIGHT(prio) \
	load_weight(static_prio_timeslice(prio))
#define RTPRIO_TO_LOAD_WEIGHT(rp) \
	(PRIO_TO_LOAD_WEIGHT(MAX_RT_PRIO) + load_weight(rp))

#define WEIGHT_IDLEPRIO		2
#define WMULT_IDLEPRIO		(1 << 31)

/*
 * Nice levels are multiplicative, with a gentle 10% change for every
 * nice level changed. I.e. when a CPU-bound task goes from nice 0 to
 * nice 1, it will get ~10% less CPU time than another CPU-bound task
 * that remained on nice 0.
 *
 * The "10% effect" is relative and cumulative: from _any_ nice level,
 * if you go up 1 level, it's -10% CPU usage, if you go down 1 level
 * it's +10% CPU usage.
 */
static const int prio_to_weight[40] = {
/* -20 */ 88818, 71054, 56843, 45475, 36380, 29104, 23283, 18626, 14901, 11921,
/* -10 */  9537,  7629,  6103,  4883,  3906,  3125,  2500,  2000,  1600,  1280,
/*   0 */  NICE_0_LOAD /* 1024 */,
/*   1 */          819,   655,   524,   419,   336,   268,   215,   172,   137,
/*  10 */   110,    87,    70,    56,    45,    36,    29,    23,    18,    15,
};

static const u32 prio_to_wmult[40] = {
	48356,   60446,   75558,   94446,  118058,  147573,
	184467,  230589,  288233,  360285,  450347,
	562979,  703746,  879575, 1099582, 1374389,
	717986, 2147483, 2684354, 3355443, 4194304,
	244160, 6557201, 8196502, 10250518, 12782640,
	16025997, 19976592, 24970740, 31350126, 39045157,
	49367440, 61356675, 76695844, 95443717, 119304647,
	148102320, 186737708, 238609294, 286331153,
};

static inline void
inc_load(struct rq *rq, const struct task_struct *p, u64 now)
{
	update_curr_load(rq, now);
	update_load_add(&rq->ls.load, p->se.load.weight);
}

static inline void
dec_load(struct rq *rq, const struct task_struct *p, u64 now)
{
	update_curr_load(rq, now);
	update_load_sub(&rq->ls.load, p->se.load.weight);
}

static inline void inc_nr_running(struct task_struct *p, struct rq *rq, u64 now)
{
	rq->nr_running++;
	inc_load(rq, p, now);
}

static inline void dec_nr_running(struct task_struct *p, struct rq *rq, u64 now)
{
	rq->nr_running--;
	dec_load(rq, p, now);
}

static void activate_task(struct rq *rq, struct task_struct *p, int wakeup);

/*
 * runqueue iterator, to support SMP load-balancing between different
 * scheduling classes, without having to expose their internal data
 * structures to the load-balancing proper:
 */
struct rq_iterator {
	void *arg;
	struct task_struct *(*start)(void *);
	struct task_struct *(*next)(void *);
};

static int balance_tasks(struct rq *this_rq, int this_cpu, struct rq *busiest,
		      unsigned long max_nr_move, unsigned long max_load_move,
		      struct sched_domain *sd, enum cpu_idle_type idle,
		      int *all_pinned, unsigned long *load_moved,
		      int this_best_prio, int best_prio, int best_prio_seen,
		      struct rq_iterator *iterator);

#include "sched_stats.h"
#include "sched_rt.c"
#include "sched_fair.c"
#include "sched_idletask.c"
#ifdef CONFIG_SCHED_DEBUG
# include "sched_debug.c"
#endif

#define sched_class_highest (&rt_sched_class)

static void set_load_weight(struct task_struct *p)
{
	task_rq(p)->cfs.wait_runtime -= p->se.wait_runtime;
	p->se.wait_runtime = 0;

	if (task_has_rt_policy(p)) {
		p->se.load.weight = prio_to_weight[0] * 2;
		p->se.load.inv_weight = prio_to_wmult[0] >> 1;
		return;
	}

	/*
	 * SCHED_IDLE tasks get minimal weight:
	 */
	if (p->policy == SCHED_IDLE) {
		p->se.load.weight = WEIGHT_IDLEPRIO;
		p->se.load.inv_weight = WMULT_IDLEPRIO;
		return;
	}

	p->se.load.weight = prio_to_weight[p->static_prio - MAX_RT_PRIO];
	p->se.load.inv_weight = prio_to_wmult[p->static_prio - MAX_RT_PRIO];
}

static void
enqueue_task(struct rq *rq, struct task_struct *p, int wakeup, u64 now)
{
	sched_info_queued(p);
	p->sched_class->enqueue_task(rq, p, wakeup, now);
	p->se.on_rq = 1;
}

static void
dequeue_task(struct rq *rq, struct task_struct *p, int sleep, u64 now)
{
	p->sched_class->dequeue_task(rq, p, sleep, now);
	p->se.on_rq = 0;
}

/*
 * __normal_prio - return the priority that is based on the static prio
 */
static inline int __normal_prio(struct task_struct *p)
{
	return p->static_prio;
}

/*
 * Calculate the expected normal priority: i.e. priority
 * without taking RT-inheritance into account. Might be
 * boosted by interactivity modifiers. Changes upon fork,
 * setprio syscalls, and whenever the interactivity
 * estimator recalculates.
 */
static inline int normal_prio(struct task_struct *p)
{
	int prio;

	if (task_has_rt_policy(p))
		prio = MAX_RT_PRIO-1 - p->rt_priority;
	else
		prio = __normal_prio(p);
	return prio;
}

/*
 * Calculate the current priority, i.e. the priority
 * taken into account by the scheduler. This value might
 * be boosted by RT tasks, or might be boosted by
 * interactivity modifiers. Will be RT if the task got
 * RT-boosted. If not then it returns p->normal_prio.
 */
static int effective_prio(struct task_struct *p)
{
	p->normal_prio = normal_prio(p);
	/*
	 * If we are RT tasks or we were boosted to RT priority,
	 * keep the priority unchanged. Otherwise, update priority
	 * to the normal priority:
	 */
	if (!rt_prio(p->prio))
		return p->normal_prio;
	return p->prio;
}

/*
 * activate_task - move a task to the runqueue.
 */
static void activate_task(struct rq *rq, struct task_struct *p, int wakeup)
{
	u64 now = rq_clock(rq);

	if (p->state == TASK_UNINTERRUPTIBLE)
		rq->nr_uninterruptible--;

	enqueue_task(rq, p, wakeup, now);
	inc_nr_running(p, rq, now);
}

/*
 * activate_idle_task - move idle task to the _front_ of runqueue.
 */
static inline void activate_idle_task(struct task_struct *p, struct rq *rq)
{
	u64 now = rq_clock(rq);

	if (p->state == TASK_UNINTERRUPTIBLE)
		rq->nr_uninterruptible--;

	enqueue_task(rq, p, 0, now);
	inc_nr_running(p, rq, now);
}

/*
 * deactivate_task - remove a task from the runqueue.
 */
static void deactivate_task(struct rq *rq, struct task_struct *p, int sleep)
{
	u64 now = rq_clock(rq);

	if (p->state == TASK_UNINTERRUPTIBLE)
		rq->nr_uninterruptible++;

	dequeue_task(rq, p, sleep, now);
	dec_nr_running(p, rq, now);
}

/**
 * task_curr - is this task currently executing on a CPU?
 * @p: the task in question.
 */
inline int task_curr(const struct task_struct *p)
{
	return cpu_curr(task_cpu(p)) == p;
}

/* Used instead of source_load when we know the type == 0 */
unsigned long weighted_cpuload(const int cpu)
{
	return cpu_rq(cpu)->ls.load.weight;
}

static inline void __set_task_cpu(struct task_struct *p, unsigned int cpu)
{
#ifdef CONFIG_SMP
	task_thread_info(p)->cpu = cpu;
	set_task_cfs_rq(p);
#endif
}

#ifdef CONFIG_SMP

void set_task_cpu(struct task_struct *p, unsigned int new_cpu)
{
	int old_cpu = task_cpu(p);
	struct rq *old_rq = cpu_rq(old_cpu), *new_rq = cpu_rq(new_cpu);
	u64 clock_offset, fair_clock_offset;

	clock_offset = old_rq->clock - new_rq->clock;
	fair_clock_offset = old_rq->cfs.fair_clock -
						 new_rq->cfs.fair_clock;
	if (p->se.wait_start)
		p->se.wait_start -= clock_offset;
	if (p->se.wait_start_fair)
		p->se.wait_start_fair -= fair_clock_offset;
	if (p->se.sleep_start)
		p->se.sleep_start -= clock_offset;
	if (p->se.block_start)
		p->se.block_start -= clock_offset;
	if (p->se.sleep_start_fair)
		p->se.sleep_start_fair -= fair_clock_offset;

	__set_task_cpu(p, new_cpu);
}

struct migration_req {
	struct list_head list;

	struct task_struct *task;
	int dest_cpu;

	struct completion done;
};

/*
 * The task's runqueue lock must be held.
 * Returns true if you have to wait for migration thread.
 */
static int
migrate_task(struct task_struct *p, int dest_cpu, struct migration_req *req)
{
	struct rq *rq = task_rq(p);

	/*
	 * If the task is not on a runqueue (and not running), then
	 * it is sufficient to simply update the task's cpu field.
	 */
	if (!p->se.on_rq && !task_running(rq, p)) {
		set_task_cpu(p, dest_cpu);
		return 0;
	}

	init_completion(&req->done);
	req->task = p;
	req->dest_cpu = dest_cpu;
	list_add(&req->list, &rq->migration_queue);

	return 1;
}

/*
 * wait_task_inactive - wait for a thread to unschedule.
 *
 * The caller must ensure that the task *will* unschedule sometime soon,
 * else this function might spin for a *long* time. This function can't
 * be called with interrupts off, or it may introduce deadlock with
 * smp_call_function() if an IPI is sent by the same process we are
 * waiting to become inactive.
 */
void wait_task_inactive(struct task_struct *p)
{
	unsigned long flags;
	int running, on_rq;
	struct rq *rq;

repeat:
	/*
	 * We do the initial early heuristics without holding
	 * any task-queue locks at all. We'll only try to get
	 * the runqueue lock when things look like they will
	 * work out!
	 */
	rq = task_rq(p);

	/*
	 * If the task is actively running on another CPU
	 * still, just relax and busy-wait without holding
	 * any locks.
	 *
	 * NOTE! Since we don't hold any locks, it's not
	 * even sure that "rq" stays as the right runqueue!
	 * But we don't care, since "task_running()" will
	 * return false if the runqueue has changed and p
	 * is actually now running somewhere else!
	 */
	while (task_running(rq, p))
		cpu_relax();

	/*
	 * Ok, time to look more closely! We need the rq
	 * lock now, to be *sure*. If we're wrong, we'll
	 * just go back and repeat.
	 */
	rq = task_rq_lock(p, &flags);
	running = task_running(rq, p);
	on_rq = p->se.on_rq;
	task_rq_unlock(rq, &flags);

	/*
	 * Was it really running after all now that we
	 * checked with the proper locks actually held?
	 *
	 * Oops. Go back and try again..
	 */
	if (unlikely(running)) {
		cpu_relax();
		goto repeat;
	}

	/*
	 * It's not enough that it's not actively running,
	 * it must be off the runqueue _entirely_, and not
	 * preempted!
	 *
	 * So if it wa still runnable (but just not actively
	 * running right now), it's preempted, and we should
	 * yield - it could be a while.
	 */
	if (unlikely(on_rq)) {
		yield();
		goto repeat;
	}

	/*
	 * Ahh, all good. It wasn't running, and it wasn't
	 * runnable, which means that it will never become
	 * running in the future either. We're all done!
	 */
}

/***
 * kick_process - kick a running thread to enter/exit the kernel
 * @p: the to-be-kicked thread
 *
 * Cause a process which is running on another CPU to enter
 * kernel-mode, without any delay. (to get signals handled.)
 *
 * NOTE: this function doesnt have to take the runqueue lock,
 * because all it wants to ensure is that the remote task enters
 * the kernel. If the IPI races and the task has been migrated
 * to another CPU then no harm is done and the purpose has been
 * achieved as well.
 */
void kick_process(struct task_struct *p)
{
	int cpu;

	preempt_disable();
	cpu = task_cpu(p);
	if ((cpu != smp_processor_id()) && task_curr(p))
		smp_send_reschedule(cpu);
	preempt_enable();
}

/*
 * Return a low guess at the load of a migration-source cpu weighted
 * according to the scheduling class and "nice" value.
 *
 * We want to under-estimate the load of migration sources, to
 * balance conservatively.
 */
static inline unsigned long source_load(int cpu, int type)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long total = weighted_cpuload(cpu);

	if (type == 0)
		return total;

	return min(rq->cpu_load[type-1], total);
}

/*
 * Return a high guess at the load of a migration-target cpu weighted
 * according to the scheduling class and "nice" value.
 */
static inline unsigned long target_load(int cpu, int type)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long total = weighted_cpuload(cpu);

	if (type == 0)
		return total;

	return max(rq->cpu_load[type-1], total);
}

/*
 * Return the average load per task on the cpu's run queue
 */
static inline unsigned long cpu_avg_load_per_task(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long total = weighted_cpuload(cpu);
	unsigned long n = rq->nr_running;

	return n ? total / n : SCHED_LOAD_SCALE;
}

/*
 * find_idlest_group finds and returns the least busy CPU group within the
 * domain.
 */
static struct sched_group *
find_idlest_group(struct sched_domain *sd, struct task_struct *p, int this_cpu)
{
	struct sched_group *idlest = NULL, *this = NULL, *group = sd->groups;
	unsigned long min_load = ULONG_MAX, this_load = 0;
	int load_idx = sd->forkexec_idx;
	int imbalance = 100 + (sd->imbalance_pct-100)/2;

	do {
		unsigned long load, avg_load;
		int local_group;
		int i;

		/* Skip over this group if it has no CPUs allowed */
		if (!cpus_intersects(group->cpumask, p->cpus_allowed))
			goto nextgroup;

		local_group = cpu_isset(this_cpu, group->cpumask);

		/* Tally up the load of all CPUs in the group */
		avg_load = 0;

		for_each_cpu_mask(i, group->cpumask) {
			/* Bias balancing toward cpus of our domain */
			if (local_group)
				load = source_load(i, load_idx);
			else
				load = target_load(i, load_idx);

			avg_load += load;
		}

		/* Adjust by relative CPU power of the group */
		avg_load = sg_div_cpu_power(group,
				avg_load * SCHED_LOAD_SCALE);

		if (local_group) {
			this_load = avg_load;
			this = group;
		} else if (avg_load < min_load) {
			min_load = avg_load;
			idlest = group;
		}
nextgroup:
		group = group->next;
	} while (group != sd->groups);

	if (!idlest || 100*this_load < imbalance*min_load)
		return NULL;
	return idlest;
}

/*
 * find_idlest_cpu - find the idlest cpu among the cpus in group.
 */
static int
find_idlest_cpu(struct sched_group *group, struct task_struct *p, int this_cpu)
{
	cpumask_t tmp;
	unsigned long load, min_load = ULONG_MAX;
	int idlest = -1;
	int i;

	/* Traverse only the allowed CPUs */
	cpus_and(tmp, group->cpumask, p->cpus_allowed);

	for_each_cpu_mask(i, tmp) {
		load = weighted_cpuload(i);

		if (load < min_load || (load == min_load && i == this_cpu)) {
			min_load = load;
			idlest = i;
		}
	}

	return idlest;
}

/*
 * sched_balance_self: balance the current task (running on cpu) in domains
 * that have the 'flag' flag set. In practice, this is SD_BALANCE_FORK and
 * SD_BALANCE_EXEC.
 *
 * Balance, ie. select the least loaded group.
 *
 * Returns the target CPU number, or the same CPU if no balancing is needed.
 *
 * preempt must be disabled.
 */
static int sched_balance_self(int cpu, int flag)
{
	struct task_struct *t = current;
	struct sched_domain *tmp, *sd = NULL;

	for_each_domain(cpu, tmp) {
 		/*
 	 	 * If power savings logic is enabled for a domain, stop there.
 	 	 */
		if (tmp->flags & SD_POWERSAVINGS_BALANCE)
			break;
		if (tmp->flags & flag)
			sd = tmp;
	}

	while (sd) {
		cpumask_t span;
		struct sched_group *group;
		int new_cpu, weight;

		if (!(sd->flags & flag)) {
			sd = sd->child;
			continue;
		}

		span = sd->span;
		group = find_idlest_group(sd, t, cpu);
		if (!group) {
			sd = sd->child;
			continue;
		}

		new_cpu = find_idlest_cpu(group, t, cpu);
		if (new_cpu == -1 || new_cpu == cpu) {
			/* Now try balancing at a lower domain level of cpu */
			sd = sd->child;
			continue;
		}

		/* Now try balancing at a lower domain level of new_cpu */
		cpu = new_cpu;
		sd = NULL;
		weight = cpus_weight(span);
		for_each_domain(cpu, tmp) {
			if (weight <= cpus_weight(tmp->span))
				break;
			if (tmp->flags & flag)
				sd = tmp;
		}
		/* while loop will break here if sd == NULL */
	}

	return cpu;
}

#endif /* CONFIG_SMP */

/*
 * wake_idle() will wake a task on an idle cpu if task->cpu is
 * not idle and an idle cpu is available.  The span of cpus to
 * search starts with cpus closest then further out as needed,
 * so we always favor a closer, idle cpu.
 *
 * Returns the CPU we should wake onto.
 */
#if defined(ARCH_HAS_SCHED_WAKE_IDLE)
static int wake_idle(int cpu, struct task_struct *p)
{
	cpumask_t tmp;
	struct sched_domain *sd;
	int i;

	/*
	 * If it is idle, then it is the best cpu to run this task.
	 *
	 * This cpu is also the best, if it has more than one task already.
	 * Siblings must be also busy(in most cases) as they didn't already
	 * pickup the extra load from this cpu and hence we need not check
	 * sibling runqueue info. This will avoid the checks and cache miss
	 * penalities associated with that.
	 */
	if (idle_cpu(cpu) || cpu_rq(cpu)->nr_running > 1)
		return cpu;

	for_each_domain(cpu, sd) {
		if (sd->flags & SD_WAKE_IDLE) {
			cpus_and(tmp, sd->span, p->cpus_allowed);
			for_each_cpu_mask(i, tmp) {
				if (idle_cpu(i))
					return i;
			}
		}
		else
			break;
	}
	return cpu;
}
#else
static inline int wake_idle(int cpu, struct task_struct *p)
{
	return cpu;
}
#endif

/***
 * try_to_wake_up - wake up a thread
 * @p: the to-be-woken-up thread
 * @state: the mask of task states that can be woken
 * @sync: do a synchronous wakeup?
 *
 * Put it on the run-queue if it's not already there. The "current"
 * thread is always on the run-queue (except when the actual
 * re-schedule is in progress), and as such you're allowed to do
 * the simpler "current->state = TASK_RUNNING" to mark yourself
 * runnable without the overhead of this.
 *
 * returns failure only if the task is already active.
 */
static int try_to_wake_up(struct task_struct *p, unsigned int state, int sync)
{
	int cpu, this_cpu, success = 0;
	unsigned long flags;
	long old_state;
	struct rq *rq;
#ifdef CONFIG_SMP
	struct sched_domain *sd, *this_sd = NULL;
	unsigned long load, this_load;
	int new_cpu;
#endif

	rq = task_rq_lock(p, &flags);
	old_state = p->state;
	if (!(old_state & state))
		goto out;

	if (p->se.on_rq)
		goto out_running;

	cpu = task_cpu(p);
	this_cpu = smp_processor_id();

#ifdef CONFIG_SMP
	if (unlikely(task_running(rq, p)))
		goto out_activate;

	new_cpu = cpu;

	schedstat_inc(rq, ttwu_cnt);
	if (cpu == this_cpu) {
		schedstat_inc(rq, ttwu_local);
		goto out_set_cpu;
	}

	for_each_domain(this_cpu, sd) {
		if (cpu_isset(cpu, sd->span)) {
			schedstat_inc(sd, ttwu_wake_remote);
			this_sd = sd;
			break;
		}
	}

	if (unlikely(!cpu_isset(this_cpu, p->cpus_allowed)))
		goto out_set_cpu;

	/*
	 * Check for affine wakeup and passive balancing possibilities.
	 */
	if (this_sd) {
		int idx = this_sd->wake_idx;
		unsigned int imbalance;

		imbalance = 100 + (this_sd->imbalance_pct - 100) / 2;

		load = source_load(cpu, idx);
		this_load = target_load(this_cpu, idx);

		new_cpu = this_cpu; /* Wake to this CPU if we can */

		if (this_sd->flags & SD_WAKE_AFFINE) {
			unsigned long tl = this_load;
			unsigned long tl_per_task;

			tl_per_task = cpu_avg_load_per_task(this_cpu);

			/*
			 * If sync wakeup then subtract the (maximum possible)
			 * effect of the currently running task from the load
			 * of the current CPU:
			 */
			if (sync)
				tl -= current->se.load.weight;

			if ((tl <= load &&
				tl + target_load(cpu, idx) <= tl_per_task) ||
			       100*(tl + p->se.load.weight) <= imbalance*load) {
				/*
				 * This domain has SD_WAKE_AFFINE and
				 * p is cache cold in this domain, and
				 * there is no bad imbalance.
				 */
				schedstat_inc(this_sd, ttwu_move_affine);
				goto out_set_cpu;
			}
		}

		/*
		 * Start passive balancing when half the imbalance_pct
		 * limit is reached.
		 */
		if (this_sd->flags & SD_WAKE_BALANCE) {
			if (imbalance*this_load <= 100*load) {
				schedstat_inc(this_sd, ttwu_move_balance);
				goto out_set_cpu;
			}
		}
	}

	new_cpu = cpu; /* Could not wake to this_cpu. Wake to cpu instead */
out_set_cpu:
	new_cpu = wake_idle(new_cpu, p);
	if (new_cpu != cpu) {
		set_task_cpu(p, new_cpu);
		task_rq_unlock(rq, &flags);
		/* might preempt at this point */
		rq = task_rq_lock(p, &flags);
		old_state = p->state;
		if (!(old_state & state))
			goto out;
		if (p->se.on_rq)
			goto out_running;

		this_cpu = smp_processor_id();
		cpu = task_cpu(p);
	}

out_activate:
#endif /* CONFIG_SMP */
	activate_task(rq, p, 1);
	/*
	 * Sync wakeups (i.e. those types of wakeups where the waker
	 * has indicated that it will leave the CPU in short order)
	 * don't trigger a preemption, if the woken up task will run on
	 * this cpu. (in this case the 'I will reschedule' promise of
	 * the waker guarantees that the freshly woken up task is going
	 * to be considered on this CPU.)
	 */
	if (!sync || cpu != this_cpu)
		check_preempt_curr(rq, p);
	success = 1;

out_running:
	p->state = TASK_RUNNING;
out:
	task_rq_unlock(rq, &flags);

	return success;
}

int fastcall wake_up_process(struct task_struct *p)
{
	return try_to_wake_up(p, TASK_STOPPED | TASK_TRACED |
				 TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE, 0);
}
EXPORT_SYMBOL(wake_up_process);

int fastcall wake_up_state(struct task_struct *p, unsigned int state)
{
	return try_to_wake_up(p, state, 0);
}

/*
 * Perform scheduler related setup for a newly forked process p.
 * p is forked by current.
 *
 * __sched_fork() is basic setup used by init_idle() too:
 */
static void __sched_fork(struct task_struct *p)
{
	p->se.wait_start_fair		= 0;
	p->se.wait_start		= 0;
	p->se.exec_start		= 0;
	p->se.sum_exec_runtime		= 0;
	p->se.delta_exec		= 0;
	p->se.delta_fair_run		= 0;
	p->se.delta_fair_sleep		= 0;
	p->se.wait_runtime		= 0;
	p->se.sum_wait_runtime		= 0;
	p->se.sum_sleep_runtime		= 0;
	p->se.sleep_start		= 0;
	p->se.sleep_start_fair		= 0;
	p->se.block_start		= 0;
	p->se.sleep_max			= 0;
	p->se.block_max			= 0;
	p->se.exec_max			= 0;
	p->se.wait_max			= 0;
	p->se.wait_runtime_overruns	= 0;
	p->se.wait_runtime_underruns	= 0;

	INIT_LIST_HEAD(&p->run_list);
	p->se.on_rq = 0;

	/*
	 * We mark the process as running here, but have not actually
	 * inserted it onto the runqueue yet. This guarantees that
	 * nobody will actually run it, and a signal or other external
	 * event cannot wake it up and insert it on the runqueue either.
	 */
	p->state = TASK_RUNNING;
}

/*
 * fork()/clone()-time setup:
 */
void sched_fork(struct task_struct *p, int clone_flags)
{
	int cpu = get_cpu();

	__sched_fork(p);

#ifdef CONFIG_SMP
	cpu = sched_balance_self(cpu, SD_BALANCE_FORK);
#endif
	__set_task_cpu(p, cpu);

	/*
	 * Make sure we do not leak PI boosting priority to the child:
	 */
	p->prio = current->normal_prio;

#if defined(CONFIG_SCHEDSTATS) || defined(CONFIG_TASK_DELAY_ACCT)
	if (likely(sched_info_on()))
		memset(&p->sched_info, 0, sizeof(p->sched_info));
#endif
#if defined(CONFIG_SMP) && defined(__ARCH_WANT_UNLOCKED_CTXSW)
	p->oncpu = 0;
#endif
#ifdef CONFIG_PREEMPT
	/* Want to start with kernel preemption disabled. */
	task_thread_info(p)->preempt_count = 1;
#endif
	put_cpu();
}

/*
 * After fork, child runs first. (default) If set to 0 then
 * parent will (try to) run first.
 */
unsigned int __read_mostly sysctl_sched_child_runs_first = 1;

/*
 * wake_up_new_task - wake up a newly created task for the first time.
 *
 * This function will do some initial scheduler statistics housekeeping
 * that must be done for every newly created context, then puts the task
 * on the runqueue and wakes it.
 */
void fastcall wake_up_new_task(struct task_struct *p, unsigned long clone_flags)
{
	unsigned long flags;
	struct rq *rq;
	int this_cpu;

	rq = task_rq_lock(p, &flags);
	BUG_ON(p->state != TASK_RUNNING);
	this_cpu = smp_processor_id(); /* parent's CPU */

	p->prio = effective_prio(p);

	if (!sysctl_sched_child_runs_first || (clone_flags & CLONE_VM) ||
			task_cpu(p) != this_cpu || !current->se.on_rq) {
		activate_task(rq, p, 0);
	} else {
		/*
		 * Let the scheduling class do new task startup
		 * management (if any):
		 */
		p->sched_class->task_new(rq, p);
	}
	check_preempt_curr(rq, p);
	task_rq_unlock(rq, &flags);
}

/**
 * prepare_task_switch - prepare to switch tasks
 * @rq: the runqueue preparing to switch
 * @next: the task we are going to switch to.
 *
 * This is called with the rq lock held and interrupts off. It must
 * be paired with a subsequent finish_task_switch after the context
 * switch.
 *
 * prepare_task_switch sets up locking and calls architecture specific
 * hooks.
 */
static inline void prepare_task_switch(struct rq *rq, struct task_struct *next)
{
	prepare_lock_switch(rq, next);
	prepare_arch_switch(next);
}

/**
 * finish_task_switch - clean up after a task-switch
 * @rq: runqueue associated with task-switch
 * @prev: the thread we just switched away from.
 *
 * finish_task_switch must be called after the context switch, paired
 * with a prepare_task_switch call before the context switch.
 * finish_task_switch will reconcile locking set up by prepare_task_switch,
 * and do any other architecture-specific cleanup actions.
 *
 * Note that we may have delayed dropping an mm in context_switch(). If
 * so, we finish that here outside of the runqueue lock.  (Doing it
 * with the lock held can cause deadlocks; see schedule() for
 * details.)
 */
static inline void finish_task_switch(struct rq *rq, struct task_struct *prev)
	__releases(rq->lock)
{
	struct mm_struct *mm = rq->prev_mm;
	long prev_state;

	rq->prev_mm = NULL;

	/*
	 * A task struct has one reference for the use as "current".
	 * If a task dies, then it sets TASK_DEAD in tsk->state and calls
	 * schedule one last time. The schedule call will never return, and
	 * the scheduled task must drop that reference.
	 * The test for TASK_DEAD must occur while the runqueue locks are
	 * still held, otherwise prev could be scheduled on another cpu, die
	 * there before we look at prev->state, and then the reference would
	 * be dropped twice.
	 *		Manfred Spraul <manfred@colorfullife.com>
	 */
	prev_state = prev->state;
	finish_arch_switch(prev);
	finish_lock_switch(rq, prev);
	if (mm)
		mmdrop(mm);
	if (unlikely(prev_state == TASK_DEAD)) {
		/*
		 * Remove function-return probe instances associated with this
		 * task and put them back on the free list.
	 	 */
		kprobe_flush_task(prev);
		put_task_struct(prev);
	}
}

/**
 * schedule_tail - first thing a freshly forked thread must call.
 * @prev: the thread we just switched away from.
 */
asmlinkage void schedule_tail(struct task_struct *prev)
	__releases(rq->lock)
{
	struct rq *rq = this_rq();

	finish_task_switch(rq, prev);
#ifdef __ARCH_WANT_UNLOCKED_CTXSW
	/* In this case, finish_task_switch does not reenable preemption */
	preempt_enable();
#endif
	if (current->set_child_tid)
		put_user(current->pid, current->set_child_tid);
}

/*
 * context_switch - switch to the new MM and the new
 * thread's register state.
 */
static inline void
context_switch(struct rq *rq, struct task_struct *prev,
	       struct task_struct *next)
{
	struct mm_struct *mm, *oldmm;

	prepare_task_switch(rq, next);
	mm = next->mm;
	oldmm = prev->active_mm;
	/*
	 * For paravirt, this is coupled with an exit in switch_to to
	 * combine the page table reload and the switch backend into
	 * one hypercall.
	 */
	arch_enter_lazy_cpu_mode();

	if (unlikely(!mm)) {
		next->active_mm = oldmm;
		atomic_inc(&oldmm->mm_count);
		enter_lazy_tlb(oldmm, next);
	} else
		switch_mm(oldmm, mm, next);

	if (unlikely(!prev->mm)) {
		prev->active_mm = NULL;
		rq->prev_mm = oldmm;
	}
	/*
	 * Since the runqueue lock will be released by the next
	 * task (which is an invalid locking op but in the case
	 * of the scheduler it's an obvious special-case), so we
	 * do an early lockdep release here:
	 */
#ifndef __ARCH_WANT_UNLOCKED_CTXSW
	spin_release(&rq->lock.dep_map, 1, _THIS_IP_);
#endif

	/* Here we just switch the register state and the stack. */
	switch_to(prev, next, prev);

	barrier();
	/*
	 * this_rq must be evaluated again because prev may have moved
	 * CPUs since it called schedule(), thus the 'rq' on its stack
	 * frame will be invalid.
	 */
	finish_task_switch(this_rq(), prev);
}

/*
 * nr_running, nr_uninterruptible and nr_context_switches:
 *
 * externally visible scheduler statistics: current number of runnable
 * threads, current number of uninterruptible-sleeping threads, total
 * number of context switches performed since bootup.
 */
unsigned long nr_running(void)
{
	unsigned long i, sum = 0;

	for_each_online_cpu(i)
		sum += cpu_rq(i)->nr_running;

	return sum;
}

unsigned long nr_uninterruptible(void)
{
	unsigned long i, sum = 0;

	for_each_possible_cpu(i)
		sum += cpu_rq(i)->nr_uninterruptible;

	/*
	 * Since we read the counters lockless, it might be slightly
	 * inaccurate. Do not allow it to go below zero though:
	 */
	if (unlikely((long)sum < 0))
		sum = 0;

	return sum;
}

unsigned long long nr_context_switches(void)
{
	int i;
	unsigned long long sum = 0;

	for_each_possible_cpu(i)
		sum += cpu_rq(i)->nr_switches;

	return sum;
}

unsigned long nr_iowait(void)
{
	unsigned long i, sum = 0;

	for_each_possible_cpu(i)
		sum += atomic_read(&cpu_rq(i)->nr_iowait);

	return sum;
}

unsigned long nr_active(void)
{
	unsigned long i, running = 0, uninterruptible = 0;

	for_each_online_cpu(i) {
		running += cpu_rq(i)->nr_running;
		uninterruptible += cpu_rq(i)->nr_uninterruptible;
	}

	if (unlikely((long)uninterruptible < 0))
		uninterruptible = 0;

	return running + uninterruptible;
}

/*
 * Update rq->cpu_load[] statistics. This function is usually called every
 * scheduler tick (TICK_NSEC).
 */
static void update_cpu_load(struct rq *this_rq)
{
	u64 fair_delta64, exec_delta64, idle_delta64, sample_interval64, tmp64;
	unsigned long total_load = this_rq->ls.load.weight;
	unsigned long this_load =  total_load;
	struct load_stat *ls = &this_rq->ls;
	u64 now = __rq_clock(this_rq);
	int i, scale;

	this_rq->nr_load_updates++;
	if (unlikely(!(sysctl_sched_features & SCHED_FEAT_PRECISE_CPU_LOAD)))
		goto do_avg;

	/* Update delta_fair/delta_exec fields first */
	update_curr_load(this_rq, now);

	fair_delta64 = ls->delta_fair + 1;
	ls->delta_fair = 0;

	exec_delta64 = ls->delta_exec + 1;
	ls->delta_exec = 0;

	sample_interval64 = now - ls->load_update_last;
	ls->load_update_last = now;

	if ((s64)sample_interval64 < (s64)TICK_NSEC)
		sample_interval64 = TICK_NSEC;

	if (exec_delta64 > sample_interval64)
		exec_delta64 = sample_interval64;

	idle_delta64 = sample_interval64 - exec_delta64;

	tmp64 = div64_64(SCHED_LOAD_SCALE * exec_delta64, fair_delta64);
	tmp64 = div64_64(tmp64 * exec_delta64, sample_interval64);

	this_load = (unsigned long)tmp64;

do_avg:

	/* Update our load: */
	for (i = 0, scale = 1; i < CPU_LOAD_IDX_MAX; i++, scale += scale) {
		unsigned long old_load, new_load;

		/* scale is effectively 1 << i now, and >> i divides by scale */

		old_load = this_rq->cpu_load[i];
		new_load = this_load;

		this_rq->cpu_load[i] = (old_load*(scale-1) + new_load) >> i;
	}
}

#ifdef CONFIG_SMP

/*
 * double_rq_lock - safely lock two runqueues
 *
 * Note this does not disable interrupts like task_rq_lock,
 * you need to do so manually before calling.
 */
static void double_rq_lock(struct rq *rq1, struct rq *rq2)
	__acquires(rq1->lock)
	__acquires(rq2->lock)
{
	BUG_ON(!irqs_disabled());
	if (rq1 == rq2) {
		spin_lock(&rq1->lock);
		__acquire(rq2->lock);	/* Fake it out ;) */
	} else {
		if (rq1 < rq2) {
			spin_lock(&rq1->lock);
			spin_lock(&rq2->lock);
		} else {
			spin_lock(&rq2->lock);
			spin_lock(&rq1->lock);
		}
	}
}

/*
 * double_rq_unlock - safely unlock two runqueues
 *
 * Note this does not restore interrupts like task_rq_unlock,
 * you need to do so manually after calling.
 */
static void double_rq_unlock(struct rq *rq1, struct rq *rq2)
	__releases(rq1->lock)
	__releases(rq2->lock)
{
	spin_unlock(&rq1->lock);
	if (rq1 != rq2)
		spin_unlock(&rq2->lock);
	else
		__release(rq2->lock);
}

/*
 * double_lock_balance - lock the busiest runqueue, this_rq is locked already.
 */
static void double_lock_balance(struct rq *this_rq, struct rq *busiest)
	__releases(this_rq->lock)
	__acquires(busiest->lock)
	__acquires(this_rq->lock)
{
	if (unlikely(!irqs_disabled())) {
		/* printk() doesn't work good under rq->lock */
		spin_unlock(&this_rq->lock);
		BUG_ON(1);
	}
	if (unlikely(!spin_trylock(&busiest->lock))) {
		if (busiest < this_rq) {
			spin_unlock(&this_rq->lock);
			spin_lock(&busiest->lock);
			spin_lock(&this_rq->lock);
		} else
			spin_lock(&busiest->lock);
	}
}

/*
 * If dest_cpu is allowed for this process, migrate the task to it.
 * This is accomplished by forcing the cpu_allowed mask to only
 * allow dest_cpu, which will force the cpu onto dest_cpu.  Then
 * the cpu_allowed mask is restored.
 */
static void sched_migrate_task(struct task_struct *p, int dest_cpu)
{
	struct migration_req req;
	unsigned long flags;
	struct rq *rq;

	rq = task_rq_lock(p, &flags);
	if (!cpu_isset(dest_cpu, p->cpus_allowed)
	    || unlikely(cpu_is_offline(dest_cpu)))
		goto out;

	/* force the process onto the specified CPU */
	if (migrate_task(p, dest_cpu, &req)) {
		/* Need to wait for migration thread (might exit: take ref). */
		struct task_struct *mt = rq->migration_thread;

		get_task_struct(mt);
		task_rq_unlock(rq, &flags);
		wake_up_process(mt);
		put_task_struct(mt);
		wait_for_completion(&req.done);

		return;
	}
out:
	task_rq_unlock(rq, &flags);
}

/*
 * sched_exec - execve() is a valuable balancing opportunity, because at
 * this point the task has the smallest effective memory and cache footprint.
 */
void sched_exec(void)
{
	int new_cpu, this_cpu = get_cpu();
	new_cpu = sched_balance_self(this_cpu, SD_BALANCE_EXEC);
	put_cpu();
	if (new_cpu != this_cpu)
		sched_migrate_task(current, new_cpu);
}

/*
 * pull_task - move a task from a remote runqueue to the local runqueue.
 * Both runqueues must be locked.
 */
static void pull_task(struct rq *src_rq, struct task_struct *p,
		      struct rq *this_rq, int this_cpu)
{
	deactivate_task(src_rq, p, 0);
	set_task_cpu(p, this_cpu);
	activate_task(this_rq, p, 0);
	/*
	 * Note that idle threads have a prio of MAX_PRIO, for this test
	 * to be always true for them.
	 */
	check_preempt_curr(this_rq, p);
}

/*
 * can_migrate_task - may task p from runqueue rq be migrated to this_cpu?
 */
static
int can_migrate_task(struct task_struct *p, struct rq *rq, int this_cpu,
		     struct sched_domain *sd, enum cpu_idle_type idle,
		     int *all_pinned)
{
	/*
	 * We do not migrate tasks that are:
	 * 1) running (obviously), or
	 * 2) cannot be migrated to this CPU due to cpus_allowed, or
	 * 3) are cache-hot on their current CPU.
	 */
	if (!cpu_isset(this_cpu, p->cpus_allowed))
		return 0;
	*all_pinned = 0;

	if (task_running(rq, p))
		return 0;

	/*
	 * Aggressive migration if too many balance attempts have failed:
	 */
	if (sd->nr_balance_failed > sd->cache_nice_tries)
		return 1;

	return 1;
}

static int balance_tasks(struct rq *this_rq, int this_cpu, struct rq *busiest,
		      unsigned long max_nr_move, unsigned long max_load_move,
		      struct sched_domain *sd, enum cpu_idle_type idle,
		      int *all_pinned, unsigned long *load_moved,
		      int this_best_prio, int best_prio, int best_prio_seen,
		      struct rq_iterator *iterator)
{
	int pulled = 0, pinned = 0, skip_for_load;
	struct task_struct *p;
	long rem_load_move = max_load_move;

	if (max_nr_move == 0 || max_load_move == 0)
		goto out;

	pinned = 1;

	/*
	 * Start the load-balancing iterator:
	 */
	p = iterator->start(iterator->arg);
next:
	if (!p)
		goto out;
	/*
	 * To help distribute high priority tasks accross CPUs we don't
	 * skip a task if it will be the highest priority task (i.e. smallest
	 * prio value) on its new queue regardless of its load weight
	 */
	skip_for_load = (p->se.load.weight >> 1) > rem_load_move +
							 SCHED_LOAD_SCALE_FUZZ;
	if (skip_for_load && p->prio < this_best_prio)
		skip_for_load = !best_prio_seen && p->prio == best_prio;
	if (skip_for_load ||
	    !can_migrate_task(p, busiest, this_cpu, sd, idle, &pinned)) {

		best_prio_seen |= p->prio == best_prio;
		p = iterator->next(iterator->arg);
		goto next;
	}

	pull_task(busiest, p, this_rq, this_cpu);
	pulled++;
	rem_load_move -= p->se.load.weight;

	/*
	 * We only want to steal up to the prescribed number of tasks
	 * and the prescribed amount of weighted load.
	 */
	if (pulled < max_nr_move && rem_load_move > 0) {
		if (p->prio < this_best_prio)
			this_best_prio = p->prio;
		p = iterator->next(iterator->arg);
		goto next;
	}
out:
	/*
	 * Right now, this is the only place pull_task() is called,
	 * so we can safely collect pull_task() stats here rather than
	 * inside pull_task().
	 */
	schedstat_add(sd, lb_gained[idle], pulled);

	if (all_pinned)
		*all_pinned = pinned;
	*load_moved = max_load_move - rem_load_move;
	return pulled;
}

/*
 * move_tasks tries to move up to max_nr_move tasks and max_load_move weighted
 * load from busiest to this_rq, as part of a balancing operation within
 * "domain". Returns the number of tasks moved.
 *
 * Called with both runqueues locked.
 */
static int move_tasks(struct rq *this_rq, int this_cpu, struct rq *busiest,
		      unsigned long max_nr_move, unsigned long max_load_move,
		      struct sched_domain *sd, enum cpu_idle_type idle,
		      int *all_pinned)
{
	struct sched_class *class = sched_class_highest;
	unsigned long load_moved, total_nr_moved = 0, nr_moved;
	long rem_load_move = max_load_move;

	do {
		nr_moved = class->load_balance(this_rq, this_cpu, busiest,
				max_nr_move, (unsigned long)rem_load_move,
				sd, idle, all_pinned, &load_moved);
		total_nr_moved += nr_moved;
		max_nr_move -= nr_moved;
		rem_load_move -= load_moved;
		class = class->next;
	} while (class && max_nr_move && rem_load_move > 0);

	return total_nr_moved;
}

/*
 * find_busiest_group finds and returns the busiest CPU group within the
 * domain. It calculates and returns the amount of weighted load which
 * should be moved to restore balance via the imbalance parameter.
 */
static struct sched_group *
find_busiest_group(struct sched_domain *sd, int this_cpu,
		   unsigned long *imbalance, enum cpu_idle_type idle,
		   int *sd_idle, cpumask_t *cpus, int *balance)
{
	struct sched_group *busiest = NULL, *this = NULL, *group = sd->groups;
	unsigned long max_load, avg_load, total_load, this_load, total_pwr;
	unsigned long max_pull;
	unsigned long busiest_load_per_task, busiest_nr_running;
	unsigned long this_load_per_task, this_nr_running;
	int load_idx;
#if defined(CONFIG_SCHED_MC) || defined(CONFIG_SCHED_SMT)
	int power_savings_balance = 1;
	unsigned long leader_nr_running = 0, min_load_per_task = 0;
	unsigned long min_nr_running = ULONG_MAX;
	struct sched_group *group_min = NULL, *group_leader = NULL;
#endif

	max_load = this_load = total_load = total_pwr = 0;
	busiest_load_per_task = busiest_nr_running = 0;
	this_load_per_task = this_nr_running = 0;
	if (idle == CPU_NOT_IDLE)
		load_idx = sd->busy_idx;
	else if (idle == CPU_NEWLY_IDLE)
		load_idx = sd->newidle_idx;
	else
		load_idx = sd->idle_idx;

	do {
		unsigned long load, group_capacity;
		int local_group;
		int i;
		unsigned int balance_cpu = -1, first_idle_cpu = 0;
		unsigned long sum_nr_running, sum_weighted_load;

		local_group = cpu_isset(this_cpu, group->cpumask);

		if (local_group)
			balance_cpu = first_cpu(group->cpumask);

		/* Tally up the load of all CPUs in the group */
		sum_weighted_load = sum_nr_running = avg_load = 0;

		for_each_cpu_mask(i, group->cpumask) {
			struct rq *rq;

			if (!cpu_isset(i, *cpus))
				continue;

			rq = cpu_rq(i);

			if (*sd_idle && !idle_cpu(i))
				*sd_idle = 0;

			/* Bias balancing toward cpus of our domain */
			if (local_group) {
				if (idle_cpu(i) && !first_idle_cpu) {
					first_idle_cpu = 1;
					balance_cpu = i;
				}

				load = target_load(i, load_idx);
			} else
				load = source_load(i, load_idx);

			avg_load += load;
			sum_nr_running += rq->nr_running;
			sum_weighted_load += weighted_cpuload(i);
		}

		/*
		 * First idle cpu or the first cpu(busiest) in this sched group
		 * is eligible for doing load balancing at this and above
		 * domains.
		 */
		if (local_group && balance_cpu != this_cpu && balance) {
			*balance = 0;
			goto ret;
		}

		total_load += avg_load;
		total_pwr += group->__cpu_power;

		/* Adjust by relative CPU power of the group */
		avg_load = sg_div_cpu_power(group,
				avg_load * SCHED_LOAD_SCALE);

		group_capacity = group->__cpu_power / SCHED_LOAD_SCALE;

		if (local_group) {
			this_load = avg_load;
			this = group;
			this_nr_running = sum_nr_running;
			this_load_per_task = sum_weighted_load;
		} else if (avg_load > max_load &&
			   sum_nr_running > group_capacity) {
			max_load = avg_load;
			busiest = group;
			busiest_nr_running = sum_nr_running;
			busiest_load_per_task = sum_weighted_load;
		}

#if defined(CONFIG_SCHED_MC) || defined(CONFIG_SCHED_SMT)
		/*
		 * Busy processors will not participate in power savings
		 * balance.
		 */
		if (idle == CPU_NOT_IDLE ||
				!(sd->flags & SD_POWERSAVINGS_BALANCE))
			goto group_next;

		/*
		 * If the local group is idle or completely loaded
		 * no need to do power savings balance at this domain
		 */
		if (local_group && (this_nr_running >= group_capacity ||
				    !this_nr_running))
			power_savings_balance = 0;

		/*
		 * If a group is already running at full capacity or idle,
		 * don't include that group in power savings calculations
		 */
		if (!power_savings_balance || sum_nr_running >= group_capacity
		    || !sum_nr_running)
			goto group_next;

		/*
		 * Calculate the group which has the least non-idle load.
		 * This is the group from where we need to pick up the load
		 * for saving power
		 */
		if ((sum_nr_running < min_nr_running) ||
		    (sum_nr_running == min_nr_running &&
		     first_cpu(group->cpumask) <
		     first_cpu(group_min->cpumask))) {
			group_min = group;
			min_nr_running = sum_nr_running;
			min_load_per_task = sum_weighted_load /
						sum_nr_running;
		}

		/*
		 * Calculate the group which is almost near its
		 * capacity but still has some space to pick up some load
		 * from other group and save more power
		 */
		if (sum_nr_running <= group_capacity - 1) {
			if (sum_nr_running > leader_nr_running ||
			    (sum_nr_running == leader_nr_running &&
			     first_cpu(group->cpumask) >
			      first_cpu(group_leader->cpumask))) {
				group_leader = group;
				leader_nr_running = sum_nr_running;
			}
		}
group_next:
#endif
		group = group->next;
	} while (group != sd->groups);

	if (!busiest || this_load >= max_load || busiest_nr_running == 0)
		goto out_balanced;

	avg_load = (SCHED_LOAD_SCALE * total_load) / total_pwr;

	if (this_load >= avg_load ||
			100*max_load <= sd->imbalance_pct*this_load)
		goto out_balanced;

	busiest_load_per_task /= busiest_nr_running;
	/*
	 * We're trying to get all the cpus to the average_load, so we don't
	 * want to push ourselves above the average load, nor do we wish to
	 * reduce the max loaded cpu below the average load, as either of these
	 * actions would just result in more rebalancing later, and ping-pong
	 * tasks around. Thus we look for the minimum possible imbalance.
	 * Negative imbalances (*we* are more loaded than anyone else) will
	 * be counted as no imbalance for these purposes -- we can't fix that
	 * by pulling tasks to us.  Be careful of negative numbers as they'll
	 * appear as very large values with unsigned longs.
	 */
	if (max_load <= busiest_load_per_task)
		goto out_balanced;

	/*
	 * In the presence of smp nice balancing, certain scenarios can have
	 * max load less than avg load(as we skip the groups at or below
	 * its cpu_power, while calculating max_load..)
	 */
	if (max_load < avg_load) {
		*imbalance = 0;
		goto small_imbalance;
	}

	/* Don't want to pull so many tasks that a group would go idle */
	max_pull = min(max_load - avg_load, max_load - busiest_load_per_task);

	/* How much load to actually move to equalise the imbalance */
	*imbalance = min(max_pull * busiest->__cpu_power,
				(avg_load - this_load) * this->__cpu_power)
			/ SCHED_LOAD_SCALE;

	/*
	 * if *imbalance is less than the average load per runnable task
	 * there is no gaurantee that any tasks will be moved so we'll have
	 * a think about bumping its value to force at least one task to be
	 * moved
	 */
	if (*imbalance + SCHED_LOAD_SCALE_FUZZ < busiest_load_per_task/2) {
		unsigned long tmp, pwr_now, pwr_move;
		unsigned int imbn;

small_imbalance:
		pwr_move = pwr_now = 0;
		imbn = 2;
		if (this_nr_running) {
			this_load_per_task /= this_nr_running;
			if (busiest_load_per_task > this_load_per_task)
				imbn = 1;
		} else
			this_load_per_task = SCHED_LOAD_SCALE;

		if (max_load - this_load + SCHED_LOAD_SCALE_FUZZ >=
					busiest_load_per_task * imbn) {
			*imbalance = busiest_load_per_task;
			return busiest;
		}

		/*
		 * OK, we don't have enough imbalance to justify moving tasks,
		 * however we may be able to increase total CPU power used by
		 * moving them.
		 */

		pwr_now += busiest->__cpu_power *
				min(busiest_load_per_task, max_load);
		pwr_now += this->__cpu_power *
				min(this_load_per_task, this_load);
		pwr_now /= SCHED_LOAD_SCALE;

		/* Amount of load we'd subtract */
		tmp = sg_div_cpu_power(busiest,
				busiest_load_per_task * SCHED_LOAD_SCALE);
		if (max_load > tmp)
			pwr_move += busiest->__cpu_power *
				min(busiest_load_per_task, max_load - tmp);

		/* Amount of load we'd add */
		if (max_load * busiest->__cpu_power <
				busiest_load_per_task * SCHED_LOAD_SCALE)
			tmp = sg_div_cpu_power(this,
					max_load * busiest->__cpu_power);
		else
			tmp = sg_div_cpu_power(this,
				busiest_load_per_task * SCHED_LOAD_SCALE);
		pwr_move += this->__cpu_power *
				min(this_load_per_task, this_load + tmp);
		pwr_move /= SCHED_LOAD_SCALE;

		/* Move if we gain throughput */
		if (pwr_move <= pwr_now)
			goto out_balanced;

		*imbalance = busiest_load_per_task;
	}

	return busiest;

out_balanced:
#if defined(CONFIG_SCHED_MC) || defined(CONFIG_SCHED_SMT)
	if (idle == CPU_NOT_IDLE || !(sd->flags & SD_POWERSAVINGS_BALANCE))
		goto ret;

	if (this == group_leader && group_leader != group_min) {
		*imbalance = min_load_per_task;
		return group_min;
	}
#endif
ret:
	*imbalance = 0;
	return NULL;
}

/*
 * find_busiest_queue - find the busiest runqueue among the cpus in group.
 */
static struct rq *
find_busiest_queue(struct sched_group *group, enum cpu_idle_type idle,
		   unsigned long imbalance, cpumask_t *cpus)
{
	struct rq *busiest = NULL, *rq;
	unsigned long max_load = 0;
	int i;

	for_each_cpu_mask(i, group->cpumask) {
		unsigned long wl;

		if (!cpu_isset(i, *cpus))
			continue;

		rq = cpu_rq(i);
		wl = weighted_cpuload(i);

		if (rq->nr_running == 1 && wl > imbalance)
			continue;

		if (wl > max_load) {
			max_load = wl;
			busiest = rq;
		}
	}

	return busiest;
}

/*
 * Max backoff if we encounter pinned tasks. Pretty arbitrary value, but
 * so long as it is large enough.
 */
#define MAX_PINNED_INTERVAL	512

static inline unsigned long minus_1_or_zero(unsigned long n)
{
	return n > 0 ? n - 1 : 0;
}

/*
 * Check this_cpu to ensure it is balanced within domain. Attempt to move
 * tasks if there is an imbalance.
 */
static int load_balance(int this_cpu, struct rq *this_rq,
			struct sched_domain *sd, enum cpu_idle_type idle,
			int *balance)
{
	int nr_moved, all_pinned = 0, active_balance = 0, sd_idle = 0;
	struct sched_group *group;
	unsigned long imbalance;
	struct rq *busiest;
	cpumask_t cpus = CPU_MASK_ALL;
	unsigned long flags;

	/*
	 * When power savings policy is enabled for the parent domain, idle
	 * sibling can pick up load irrespective of busy siblings. In this case,
	 * let the state of idle sibling percolate up as CPU_IDLE, instead of
	 * portraying it as CPU_NOT_IDLE.
	 */
	if (idle != CPU_NOT_IDLE && sd->flags & SD_SHARE_CPUPOWER &&
	    !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
		sd_idle = 1;

	schedstat_inc(sd, lb_cnt[idle]);

redo:
	group = find_busiest_group(sd, this_cpu, &imbalance, idle, &sd_idle,
				   &cpus, balance);

	if (*balance == 0)
		goto out_balanced;

	if (!group) {
		schedstat_inc(sd, lb_nobusyg[idle]);
		goto out_balanced;
	}

	busiest = find_busiest_queue(group, idle, imbalance, &cpus);
	if (!busiest) {
		schedstat_inc(sd, lb_nobusyq[idle]);
		goto out_balanced;
	}

	BUG_ON(busiest == this_rq);

	schedstat_add(sd, lb_imbalance[idle], imbalance);

	nr_moved = 0;
	if (busiest->nr_running > 1) {
		/*
		 * Attempt to move tasks. If find_busiest_group has found
		 * an imbalance but busiest->nr_running <= 1, the group is
		 * still unbalanced. nr_moved simply stays zero, so it is
		 * correctly treated as an imbalance.
		 */
		local_irq_save(flags);
		double_rq_lock(this_rq, busiest);
		nr_moved = move_tasks(this_rq, this_cpu, busiest,
				      minus_1_or_zero(busiest->nr_running),
				      imbalance, sd, idle, &all_pinned);
		double_rq_unlock(this_rq, busiest);
		local_irq_restore(flags);

		/*
		 * some other cpu did the load balance for us.
		 */
		if (nr_moved && this_cpu != smp_processor_id())
			resched_cpu(this_cpu);

		/* All tasks on this runqueue were pinned by CPU affinity */
		if (unlikely(all_pinned)) {
			cpu_clear(cpu_of(busiest), cpus);
			if (!cpus_empty(cpus))
				goto redo;
			goto out_balanced;
		}
	}

	if (!nr_moved) {
		schedstat_inc(sd, lb_failed[idle]);
		sd->nr_balance_failed++;

		if (unlikely(sd->nr_balance_failed > sd->cache_nice_tries+2)) {

			spin_lock_irqsave(&busiest->lock, flags);

			/* don't kick the migration_thread, if the curr
			 * task on busiest cpu can't be moved to this_cpu
			 */
			if (!cpu_isset(this_cpu, busiest->curr->cpus_allowed)) {
				spin_unlock_irqrestore(&busiest->lock, flags);
				all_pinned = 1;
				goto out_one_pinned;
			}

			if (!busiest->active_balance) {
				busiest->active_balance = 1;
				busiest->push_cpu = this_cpu;
				active_balance = 1;
			}
			spin_unlock_irqrestore(&busiest->lock, flags);
			if (active_balance)
				wake_up_process(busiest->migration_thread);

			/*
			 * We've kicked active balancing, reset the failure
			 * counter.
			 */
			sd->nr_balance_failed = sd->cache_nice_tries+1;
		}
	} else
		sd->nr_balance_failed = 0;

	if (likely(!active_balance)) {
		/* We were unbalanced, so reset the balancing interval */
		sd->balance_interval = sd->min_interval;
	} else {
		/*
		 * If we've begun active balancing, start to back off. This
		 * case may not be covered by the all_pinned logic if there
		 * is only 1 task on the busy runqueue (because we don't call
		 * move_tasks).
		 */
		if (sd->balance_interval < sd->max_interval)
			sd->balance_interval *= 2;
	}

	if (!nr_moved && !sd_idle && sd->flags & SD_SHARE_CPUPOWER &&
	    !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
		return -1;
	return nr_moved;

out_balanced:
	schedstat_inc(sd, lb_balanced[idle]);

	sd->nr_balance_failed = 0;

out_one_pinned:
	/* tune up the balancing interval */
	if ((all_pinned && sd->balance_interval < MAX_PINNED_INTERVAL) ||
			(sd->balance_interval < sd->max_interval))
		sd->balance_interval *= 2;

	if (!sd_idle && sd->flags & SD_SHARE_CPUPOWER &&
	    !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
		return -1;
	return 0;
}

/*
 * Check this_cpu to ensure it is balanced within domain. Attempt to move
 * tasks if there is an imbalance.
 *
 * Called from schedule when this_rq is about to become idle (CPU_NEWLY_IDLE).
 * this_rq is locked.
 */
static int
load_balance_newidle(int this_cpu, struct rq *this_rq, struct sched_domain *sd)
{
	struct sched_group *group;
	struct rq *busiest = NULL;
	unsigned long imbalance;
	int nr_moved = 0;
	int sd_idle = 0;
	cpumask_t cpus = CPU_MASK_ALL;

	/*
	 * When power savings policy is enabled for the parent domain, idle
	 * sibling can pick up load irrespective of busy siblings. In this case,
	 * let the state of idle sibling percolate up as IDLE, instead of
	 * portraying it as CPU_NOT_IDLE.
	 */
	if (sd->flags & SD_SHARE_CPUPOWER &&
	    !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
		sd_idle = 1;

	schedstat_inc(sd, lb_cnt[CPU_NEWLY_IDLE]);
redo:
	group = find_busiest_group(sd, this_cpu, &imbalance, CPU_NEWLY_IDLE,
				   &sd_idle, &cpus, NULL);
	if (!group) {
		schedstat_inc(sd, lb_nobusyg[CPU_NEWLY_IDLE]);
		goto out_balanced;
	}

	busiest = find_busiest_queue(group, CPU_NEWLY_IDLE, imbalance,
				&cpus);
	if (!busiest) {
		schedstat_inc(sd, lb_nobusyq[CPU_NEWLY_IDLE]);
		goto out_balanced;
	}

	BUG_ON(busiest == this_rq);

	schedstat_add(sd, lb_imbalance[CPU_NEWLY_IDLE], imbalance);

	nr_moved = 0;
	if (busiest->nr_running > 1) {
		/* Attempt to move tasks */
		double_lock_balance(this_rq, busiest);
		nr_moved = move_tasks(this_rq, this_cpu, busiest,
					minus_1_or_zero(busiest->nr_running),
					imbalance, sd, CPU_NEWLY_IDLE, NULL);
		spin_unlock(&busiest->lock);

		if (!nr_moved) {
			cpu_clear(cpu_of(busiest), cpus);
			if (!cpus_empty(cpus))
				goto redo;
		}
	}

	if (!nr_moved) {
		schedstat_inc(sd, lb_failed[CPU_NEWLY_IDLE]);
		if (!sd_idle && sd->flags & SD_SHARE_CPUPOWER &&
		    !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
			return -1;
	} else
		sd->nr_balance_failed = 0;

	return nr_moved;

out_balanced:
	schedstat_inc(sd, lb_balanced[CPU_NEWLY_IDLE]);
	if (!sd_idle && sd->flags & SD_SHARE_CPUPOWER &&
	    !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
		return -1;
	sd->nr_balance_failed = 0;

	return 0;
}

/*
 * idle_balance is called by schedule() if this_cpu is about to become
 * idle. Attempts to pull tasks from other CPUs.
 */
static void idle_balance(int this_cpu, struct rq *this_rq)
{
	struct sched_domain *sd;
	int pulled_task = -1;
	unsigned long next_balance = jiffies + HZ;

	for_each_domain(this_cpu, sd) {
		unsigned long interval;

		if (!(sd->flags & SD_LOAD_BALANCE))
			continue;

		if (sd->flags & SD_BALANCE_NEWIDLE)
			/* If we've pulled tasks over stop searching: */
			pulled_task = load_balance_newidle(this_cpu,
								this_rq, sd);

		interval = msecs_to_jiffies(sd->balance_interval);
		if (time_after(next_balance, sd->last_balance + interval))
			next_balance = sd->last_balance + interval;
		if (pulled_task)
			break;
	}
	if (pulled_task || time_after(jiffies, this_rq->next_balance)) {
		/*
		 * We are going idle. next_balance may be set based on
		 * a busy processor. So reset next_balance.
		 */
		this_rq->next_balance = next_balance;
	}
}

/*
 * active_load_balance is run by migration threads. It pushes running tasks
 * off the busiest CPU onto idle CPUs. It requires at least 1 task to be
 * running on each physical CPU where possible, and avoids physical /
 * logical imbalances.
 *
 * Called with busiest_rq locked.
 */
static void active_load_balance(struct rq *busiest_rq, int busiest_cpu)
{
	int target_cpu = busiest_rq->push_cpu;
	struct sched_domain *sd;
	struct rq *target_rq;

	/* Is there any task to move? */
	if (busiest_rq->nr_running <= 1)
		return;

	target_rq = cpu_rq(target_cpu);

	/*
	 * This condition is "impossible", if it occurs
	 * we need to fix it.  Originally reported by
	 * Bjorn Helgaas on a 128-cpu setup.
	 */
	BUG_ON(busiest_rq == target_rq);

	/* move a task from busiest_rq to target_rq */
	double_lock_balance(busiest_rq, target_rq);

	/* Search for an sd spanning us and the target CPU. */
	for_each_domain(target_cpu, sd) {
		if ((sd->flags & SD_LOAD_BALANCE) &&
		    cpu_isset(busiest_cpu, sd->span))
				break;
	}

	if (likely(sd)) {
		schedstat_inc(sd, alb_cnt);

		if (move_tasks(target_rq, target_cpu, busiest_rq, 1,
			       RTPRIO_TO_LOAD_WEIGHT(100), sd, CPU_IDLE,
			       NULL))
			schedstat_inc(sd, alb_pushed);
		else
			schedstat_inc(sd, alb_failed);
	}
	spin_unlock(&target_rq->lock);
}

#ifdef CONFIG_NO_HZ
static struct {
	atomic_t load_balancer;
	cpumask_t  cpu_mask;
} nohz ____cacheline_aligned = {
	.load_balancer = ATOMIC_INIT(-1),
	.cpu_mask = CPU_MASK_NONE,
};

/*
 * This routine will try to nominate the ilb (idle load balancing)
 * owner among the cpus whose ticks are stopped. ilb owner will do the idle
 * load balancing on behalf of all those cpus. If all the cpus in the system
 * go into this tickless mode, then there will be no ilb owner (as there is
 * no need for one) and all the cpus will sleep till the next wakeup event
 * arrives...
 *
 * For the ilb owner, tick is not stopped. And this tick will be used
 * for idle load balancing. ilb owner will still be part of
 * nohz.cpu_mask..
 *
 * While stopping the tick, this cpu will become the ilb owner if there
 * is no other owner. And will be the owner till that cpu becomes busy
 * or if all cpus in the system stop their ticks at which point
 * there is no need for ilb owner.
 *
 * When the ilb owner becomes busy, it nominates another owner, during the
 * next busy scheduler_tick()
 */
int select_nohz_load_balancer(int stop_tick)
{
	int cpu = smp_processor_id();

	if (stop_tick) {
		cpu_set(cpu, nohz.cpu_mask);
		cpu_rq(cpu)->in_nohz_recently = 1;

		/*
		 * If we are going offline and still the leader, give up!
		 */
		if (cpu_is_offline(cpu) &&
		    atomic_read(&nohz.load_balancer) == cpu) {
			if (atomic_cmpxchg(&nohz.load_balancer, cpu, -1) != cpu)
				BUG();
			return 0;
		}

		/* time for ilb owner also to sleep */
		if (cpus_weight(nohz.cpu_mask) == num_online_cpus()) {
			if (atomic_read(&nohz.load_balancer) == cpu)
				atomic_set(&nohz.load_balancer, -1);
			return 0;
		}

		if (atomic_read(&nohz.load_balancer) == -1) {
			/* make me the ilb owner */
			if (atomic_cmpxchg(&nohz.load_balancer, -1, cpu) == -1)
				return 1;
		} else if (atomic_read(&nohz.load_balancer) == cpu)
			return 1;
	} else {
		if (!cpu_isset(cpu, nohz.cpu_mask))
			return 0;

		cpu_clear(cpu, nohz.cpu_mask);

		if (atomic_read(&nohz.load_balancer) == cpu)
			if (atomic_cmpxchg(&nohz.load_balancer, cpu, -1) != cpu)
				BUG();
	}
	return 0;
}
#endif

static DEFINE_SPINLOCK(balancing);

/*
 * It checks each scheduling domain to see if it is due to be balanced,
 * and initiates a balancing operation if so.
 *
 * Balancing parameters are set up in arch_init_sched_domains.
 */
static inline void rebalance_domains(int cpu, enum cpu_idle_type idle)
{
	int balance = 1;
	struct rq *rq = cpu_rq(cpu);
	unsigned long interval;
	struct sched_domain *sd;
	/* Earliest time when we have to do rebalance again */
	unsigned long next_balance = jiffies + 60*HZ;

	for_each_domain(cpu, sd) {
		if (!(sd->flags & SD_LOAD_BALANCE))
			continue;

		interval = sd->balance_interval;
		if (idle != CPU_IDLE)
			interval *= sd->busy_factor;

		/* scale ms to jiffies */
		interval = msecs_to_jiffies(interval);
		if (unlikely(!interval))
			interval = 1;
		if (interval > HZ*NR_CPUS/10)
			interval = HZ*NR_CPUS/10;


		if (sd->flags & SD_SERIALIZE) {
			if (!spin_trylock(&balancing))
				goto out;
		}

		if (time_after_eq(jiffies, sd->last_balance + interval)) {
			if (load_balance(cpu, rq, sd, idle, &balance)) {
				/*
				 * We've pulled tasks over so either we're no
				 * longer idle, or one of our SMT siblings is
				 * not idle.
				 */
				idle = CPU_NOT_IDLE;
			}
			sd->last_balance = jiffies;
		}
		if (sd->flags & SD_SERIALIZE)
			spin_unlock(&balancing);
out:
		if (time_after(next_balance, sd->last_balance + interval))
			next_balance = sd->last_balance + interval;

		/*
		 * Stop the load balance at this level. There is another
		 * CPU in our sched group which is doing load balancing more
		 * actively.
		 */
		if (!balance)
			break;
	}
	rq->next_balance = next_balance;
}

/*
 * run_rebalance_domains is triggered when needed from the scheduler tick.
 * In CONFIG_NO_HZ case, the idle load balance owner will do the
 * rebalancing for all the cpus for whom scheduler ticks are stopped.
 */
static void run_rebalance_domains(struct softirq_action *h)
{
	int this_cpu = smp_processor_id();
	struct rq *this_rq = cpu_rq(this_cpu);
	enum cpu_idle_type idle = this_rq->idle_at_tick ?
						CPU_IDLE : CPU_NOT_IDLE;

	rebalance_domains(this_cpu, idle);

#ifdef CONFIG_NO_HZ
	/*
	 * If this cpu is the owner for idle load balancing, then do the
	 * balancing on behalf of the other idle cpus whose ticks are
	 * stopped.
	 */
	if (this_rq->idle_at_tick &&
	    atomic_read(&nohz.load_balancer) == this_cpu) {
		cpumask_t cpus = nohz.cpu_mask;
		struct rq *rq;
		int balance_cpu;

		cpu_clear(this_cpu, cpus);
		for_each_cpu_mask(balance_cpu, cpus) {
			/*
			 * If this cpu gets work to do, stop the load balancing
			 * work being done for other cpus. Next load
			 * balancing owner will pick it up.
			 */
			if (need_resched())
				break;

			rebalance_domains(balance_cpu, SCHED_IDLE);

			rq = cpu_rq(balance_cpu);
			if (time_after(this_rq->next_balance, rq->next_balance))
				this_rq->next_balance = rq->next_balance;
		}
	}
#endif
}

/*
 * Trigger the SCHED_SOFTIRQ if it is time to do periodic load balancing.
 *
 * In case of CONFIG_NO_HZ, this is the place where we nominate a new
 * idle load balancing owner or decide to stop the periodic load balancing,
 * if the whole system is idle.
 */
static inline void trigger_load_balance(struct rq *rq, int cpu)
{
#ifdef CONFIG_NO_HZ
	/*
	 * If we were in the nohz mode recently and busy at the current
	 * scheduler tick, then check if we need to nominate new idle
	 * load balancer.
	 */
	if (rq->in_nohz_recently && !rq->idle_at_tick) {
		rq->in_nohz_recently = 0;

		if (atomic_read(&nohz.load_balancer) == cpu) {
			cpu_clear(cpu, nohz.cpu_mask);
			atomic_set(&nohz.load_balancer, -1);
		}

		if (atomic_read(&nohz.load_balancer) == -1) {
			/*
			 * simple selection for now: Nominate the
			 * first cpu in the nohz list to be the next
			 * ilb owner.
			 *
			 * TBD: Traverse the sched domains and nominate
			 * the nearest cpu in the nohz.cpu_mask.
			 */
			int ilb = first_cpu(nohz.cpu_mask);

			if (ilb != NR_CPUS)
				resched_cpu(ilb);
		}
	}

	/*
	 * If this cpu is idle and doing idle load balancing for all the
	 * cpus with ticks stopped, is it time for that to stop?
	 */
	if (rq->idle_at_tick && atomic_read(&nohz.load_balancer) == cpu &&
	    cpus_weight(nohz.cpu_mask) == num_online_cpus()) {
		resched_cpu(cpu);
		return;
	}

	/*
	 * If this cpu is idle and the idle load balancing is done by
	 * someone else, then no need raise the SCHED_SOFTIRQ
	 */
	if (rq->idle_at_tick && atomic_read(&nohz.load_balancer) != cpu &&
	    cpu_isset(cpu, nohz.cpu_mask))
		return;
#endif
	if (time_after_eq(jiffies, rq->next_balance))
		raise_softirq(SCHED_SOFTIRQ);
}

#else	/* CONFIG_SMP */

/*
 * on UP we do not need to balance between CPUs:
 */
static inline void idle_balance(int cpu, struct rq *rq)
{
}

/* Avoid "used but not defined" warning on UP */
static int balance_tasks(struct rq *this_rq, int this_cpu, struct rq *busiest,
		      unsigned long max_nr_move, unsigned long max_load_move,
		      struct sched_domain *sd, enum cpu_idle_type idle,
		      int *all_pinned, unsigned long *load_moved,
		      int this_best_prio, int best_prio, int best_prio_seen,
		      struct rq_iterator *iterator)
{
	*load_moved = 0;

	return 0;
}

#endif

DEFINE_PER_CPU(struct kernel_stat, kstat);

EXPORT_PER_CPU_SYMBOL(kstat);

/*
 * Return p->sum_exec_runtime plus any more ns on the sched_clock
 * that have not yet been banked in case the task is currently running.
 */
unsigned long long task_sched_runtime(struct task_struct *p)
{
	unsigned long flags;
	u64 ns, delta_exec;
	struct rq *rq;

	rq = task_rq_lock(p, &flags);
	ns = p->se.sum_exec_runtime;
	if (rq->curr == p) {
		delta_exec = rq_clock(rq) - p->se.exec_start;
		if ((s64)delta_exec > 0)
			ns += delta_exec;
	}
	task_rq_unlock(rq, &flags);

	return ns;
}

/*
 * Account user cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @hardirq_offset: the offset to subtract from hardirq_count()
 * @cputime: the cpu time spent in user space since the last update
 */
void account_user_time(struct task_struct *p, cputime_t cputime)
{
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;
	cputime64_t tmp;

	p->utime = cputime_add(p->utime, cputime);

	/* Add user time to cpustat. */
	tmp = cputime_to_cputime64(cputime);
	if (TASK_NICE(p) > 0)
		cpustat->nice = cputime64_add(cpustat->nice, tmp);
	else
		cpustat->user = cputime64_add(cpustat->user, tmp);
}

/*
 * Account system cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @hardirq_offset: the offset to subtract from hardirq_count()
 * @cputime: the cpu time spent in kernel space since the last update
 */
void account_system_time(struct task_struct *p, int hardirq_offset,
			 cputime_t cputime)
{
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;
	struct rq *rq = this_rq();
	cputime64_t tmp;

	p->stime = cputime_add(p->stime, cputime);

	/* Add system time to cpustat. */
	tmp = cputime_to_cputime64(cputime);
	if (hardirq_count() - hardirq_offset)
		cpustat->irq = cputime64_add(cpustat->irq, tmp);
	else if (softirq_count())
		cpustat->softirq = cputime64_add(cpustat->softirq, tmp);
	else if (p != rq->idle)
		cpustat->system = cputime64_add(cpustat->system, tmp);
	else if (atomic_read(&rq->nr_iowait) > 0)
		cpustat->iowait = cputime64_add(cpustat->iowait, tmp);
	else
		cpustat->idle = cputime64_add(cpustat->idle, tmp);
	/* Account for system time used */
	acct_update_integrals(p);
}

/*
 * Account for involuntary wait time.
 * @p: the process from which the cpu time has been stolen
 * @steal: the cpu time spent in involuntary wait
 */
void account_steal_time(struct task_struct *p, cputime_t steal)
{
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;
	cputime64_t tmp = cputime_to_cputime64(steal);
	struct rq *rq = this_rq();

	if (p == rq->idle) {
		p->stime = cputime_add(p->stime, steal);
		if (atomic_read(&rq->nr_iowait) > 0)
			cpustat->iowait = cputime64_add(cpustat->iowait, tmp);
		else
			cpustat->idle = cputime64_add(cpustat->idle, tmp);
	} else
		cpustat->steal = cputime64_add(cpustat->steal, tmp);
}

/*
 * This function gets called by the timer code, with HZ frequency.
 * We call it with interrupts disabled.
 *
 * It also gets called by the fork code, when changing the parent's
 * timeslices.
 */
void scheduler_tick(void)
{
	int cpu = smp_processor_id();
	struct rq *rq = cpu_rq(cpu);
	struct task_struct *curr = rq->curr;

	spin_lock(&rq->lock);
	if (curr != rq->idle) /* FIXME: needed? */
		curr->sched_class->task_tick(rq, curr);
	update_cpu_load(rq);
	spin_unlock(&rq->lock);

#ifdef CONFIG_SMP
	rq->idle_at_tick = idle_cpu(cpu);
	trigger_load_balance(rq, cpu);
#endif
}

#if defined(CONFIG_PREEMPT) && defined(CONFIG_DEBUG_PREEMPT)

void fastcall add_preempt_count(int val)
{
	/*
	 * Underflow?
	 */
	if (DEBUG_LOCKS_WARN_ON((preempt_count() < 0)))
		return;
	preempt_count() += val;
	/*
	 * Spinlock count overflowing soon?
	 */
	DEBUG_LOCKS_WARN_ON((preempt_count() & PREEMPT_MASK) >=
				PREEMPT_MASK - 10);
}
EXPORT_SYMBOL(add_preempt_count);

void fastcall sub_preempt_count(int val)
{
	/*
	 * Underflow?
	 */
	if (DEBUG_LOCKS_WARN_ON(val > preempt_count()))
		return;
	/*
	 * Is the spinlock portion underflowing?
	 */
	if (DEBUG_LOCKS_WARN_ON((val < PREEMPT_MASK) &&
			!(preempt_count() & PREEMPT_MASK)))
		return;

	preempt_count() -= val;
}
EXPORT_SYMBOL(sub_preempt_count);

#endif

/*
 * Print scheduling while atomic bug:
 */
static noinline void __schedule_bug(struct task_struct *prev)
{
	printk(KERN_ERR "BUG: scheduling while atomic: %s/0x%08x/%d\n",
		prev->comm, preempt_count(), prev->pid);
	debug_show_held_locks(prev);
	if (irqs_disabled())
		print_irqtrace_events(prev);
	dump_stack();
}

/*
 * Various schedule()-time debugging checks and statistics:
 */
static inline void schedule_debug(struct task_struct *prev)
{
	/*
	 * Test if we are atomic.  Since do_exit() needs to call into
	 * schedule() atomically, we ignore that path for now.
	 * Otherwise, whine if we are scheduling when we should not be.
	 */
	if (unlikely(in_atomic_preempt_off()) && unlikely(!prev->exit_state))
		__schedule_bug(prev);

	profile_hit(SCHED_PROFILING, __builtin_return_address(0));

	schedstat_inc(this_rq(), sched_cnt);
}

/*
 * Pick up the highest-prio task:
 */
static inline struct task_struct *
pick_next_task(struct rq *rq, struct task_struct *prev, u64 now)
{
	struct sched_class *class;
	struct task_struct *p;

	/*
	 * Optimization: we know that if all tasks are in
	 * the fair class we can call that function directly:
	 */
	if (likely(rq->nr_running == rq->cfs.nr_running)) {
		p = fair_sched_class.pick_next_task(rq, now);
		if (likely(p))
			return p;
	}

	class = sched_class_highest;
	for ( ; ; ) {
		p = class->pick_next_task(rq, now);
		if (p)
			return p;
		/*
		 * Will never be NULL as the idle class always
		 * returns a non-NULL p:
		 */
		class = class->next;
	}
}

/*
 * schedule() is the main scheduler function.
 */
asmlinkage void __sched schedule(void)
{
	struct task_struct *prev, *next;
	long *switch_count;
	struct rq *rq;
	u64 now;
	int cpu;

need_resched:
	preempt_disable();
	cpu = smp_processor_id();
	rq = cpu_rq(cpu);
	rcu_qsctr_inc(cpu);
	prev = rq->curr;
	switch_count = &prev->nivcsw;

	release_kernel_lock(prev);
need_resched_nonpreemptible:

	schedule_debug(prev);

	spin_lock_irq(&rq->lock);
	clear_tsk_need_resched(prev);

	if (prev->state && !(preempt_count() & PREEMPT_ACTIVE)) {
		if (unlikely((prev->state & TASK_INTERRUPTIBLE) &&
				unlikely(signal_pending(prev)))) {
			prev->state = TASK_RUNNING;
		} else {
			deactivate_task(rq, prev, 1);
		}
		switch_count = &prev->nvcsw;
	}

	if (unlikely(!rq->nr_running))
		idle_balance(cpu, rq);

	now = __rq_clock(rq);
	prev->sched_class->put_prev_task(rq, prev, now);
	next = pick_next_task(rq, prev, now);

	sched_info_switch(prev, next);

	if (likely(prev != next)) {
		rq->nr_switches++;
		rq->curr = next;
		++*switch_count;

		context_switch(rq, prev, next); /* unlocks the rq */
	} else
		spin_unlock_irq(&rq->lock);

	if (unlikely(reacquire_kernel_lock(current) < 0)) {
		cpu = smp_processor_id();
		rq = cpu_rq(cpu);
		goto need_resched_nonpreemptible;
	}
	preempt_enable_no_resched();
	if (unlikely(test_thread_flag(TIF_NEED_RESCHED)))
		goto need_resched;
}
EXPORT_SYMBOL(schedule);

#ifdef CONFIG_PREEMPT
/*
 * this is the entry point to schedule() from in-kernel preemption
 * off of preempt_enable.  Kernel preemptions off return from interrupt
 * occur there and call schedule directly.
 */
asmlinkage void __sched preempt_schedule(void)
{
	struct thread_info *ti = current_thread_info();
#ifdef CONFIG_PREEMPT_BKL
	struct task_struct *task = current;
	int saved_lock_depth;
#endif
	/*
	 * If there is a non-zero preempt_count or interrupts are disabled,
	 * we do not want to preempt the current task.  Just return..
	 */
	if (likely(ti->preempt_count || irqs_disabled()))
		return;

need_resched:
	add_preempt_count(PREEMPT_ACTIVE);
	/*
	 * We keep the big kernel semaphore locked, but we
	 * clear ->lock_depth so that schedule() doesnt
	 * auto-release the semaphore:
	 */
#ifdef CONFIG_PREEMPT_BKL
	saved_lock_depth = task->lock_depth;
	task->lock_depth = -1;
#endif
	schedule();
#ifdef CONFIG_PREEMPT_BKL
	task->lock_depth = saved_lock_depth;
#endif
	sub_preempt_count(PREEMPT_ACTIVE);

	/* we could miss a preemption opportunity between schedule and now */
	barrier();
	if (unlikely(test_thread_flag(TIF_NEED_RESCHED)))
		goto need_resched;
}
EXPORT_SYMBOL(preempt_schedule);

/*
 * this is the entry point to schedule() from kernel preemption
 * off of irq context.
 * Note, that this is called and return with irqs disabled. This will
 * protect us against recursive calling from irq.
 */
asmlinkage void __sched preempt_schedule_irq(void)
{
	struct thread_info *ti = current_thread_info();
#ifdef CONFIG_PREEMPT_BKL
	struct task_struct *task = current;
	int saved_lock_depth;
#endif
	/* Catch callers which need to be fixed */
	BUG_ON(ti->preempt_count || !irqs_disabled());

need_resched:
	add_preempt_count(PREEMPT_ACTIVE);
	/*
	 * We keep the big kernel semaphore locked, but we
	 * clear ->lock_depth so that schedule() doesnt
	 * auto-release the semaphore:
	 */
#ifdef CONFIG_PREEMPT_BKL
	saved_lock_depth = task->lock_depth;
	task->lock_depth = -1;
#endif
	local_irq_enable();
	schedule();
	local_irq_disable();
#ifdef CONFIG_PREEMPT_BKL
	task->lock_depth = saved_lock_depth;
#endif
	sub_preempt_count(PREEMPT_ACTIVE);

	/* we could miss a preemption opportunity between schedule and now */
	barrier();
	if (unlikely(test_thread_flag(TIF_NEED_RESCHED)))
		goto need_resched;
}

#endif /* CONFIG_PREEMPT */

int default_wake_function(wait_queue_t *curr, unsigned mode, int sync,
			  void *key)
{
	return try_to_wake_up(curr->private, mode, sync);
}
EXPORT_SYMBOL(default_wake_function);

/*
 * The core wakeup function.  Non-exclusive wakeups (nr_exclusive == 0) just
 * wake everything up.  If it's an exclusive wakeup (nr_exclusive == small +ve
 * number) then we wake all the non-exclusive tasks and one exclusive task.
 *
 * There are circumstances in which we can try to wake a task which has already
 * started to run but is not in state TASK_RUNNING.  try_to_wake_up() returns
 * zero in this (rare) case, and we handle it by continuing to scan the queue.
 */
static void __wake_up_common(wait_queue_head_t *q, unsigned int mode,
			     int nr_exclusive, int sync, void *key)
{
	struct list_head *tmp, *next;

	list_for_each_safe(tmp, next, &q->task_list) {
		wait_queue_t *curr = list_entry(tmp, wait_queue_t, task_list);
		unsigned flags = curr->flags;

		if (curr->func(curr, mode, sync, key) &&
				(flags & WQ_FLAG_EXCLUSIVE) && !--nr_exclusive)
			break;
	}
}

/**
 * __wake_up - wake up threads blocked on a waitqueue.
 * @q: the waitqueue
 * @mode: which threads
 * @nr_exclusive: how many wake-one or wake-many threads to wake up
 * @key: is directly passed to the wakeup function
 */
void fastcall __wake_up(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, void *key)
{
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	__wake_up_common(q, mode, nr_exclusive, 0, key);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(__wake_up);

/*
 * Same as __wake_up but called with the spinlock in wait_queue_head_t held.
 */
void fastcall __wake_up_locked(wait_queue_head_t *q, unsigned int mode)
{
	__wake_up_common(q, mode, 1, 0, NULL);
}

/**
 * __wake_up_sync - wake up threads blocked on a waitqueue.
 * @q: the waitqueue
 * @mode: which threads
 * @nr_exclusive: how many wake-one or wake-many threads to wake up
 *
 * The sync wakeup differs that the waker knows that it will schedule
 * away soon, so while the target thread will be woken up, it will not
 * be migrated to another CPU - ie. the two threads are 'synchronized'
 * with each other. This can prevent needless bouncing between CPUs.
 *
 * On UP it can prevent extra preemption.
 */
void fastcall
__wake_up_sync(wait_queue_head_t *q, unsigned int mode, int nr_exclusive)
{
	unsigned long flags;
	int sync = 1;

	if (unlikely(!q))
		return;

	if (unlikely(!nr_exclusive))
		sync = 0;

	spin_lock_irqsave(&q->lock, flags);
	__wake_up_common(q, mode, nr_exclusive, sync, NULL);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL_GPL(__wake_up_sync);	/* For internal use only */

void fastcall complete(struct completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->wait.lock, flags);
	x->done++;
	__wake_up_common(&x->wait, TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE,
			 1, 0, NULL);
	spin_unlock_irqrestore(&x->wait.lock, flags);
}
EXPORT_SYMBOL(complete);

void fastcall complete_all(struct completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->wait.lock, flags);
	x->done += UINT_MAX/2;
	__wake_up_common(&x->wait, TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE,
			 0, 0, NULL);
	spin_unlock_irqrestore(&x->wait.lock, flags);
}
EXPORT_SYMBOL(complete_all);

void fastcall __sched wait_for_completion(struct completion *x)
{
	might_sleep();

	spin_lock_irq(&x->wait.lock);
	if (!x->done) {
		DECLARE_WAITQUEUE(wait, current);

		wait.flags |= WQ_FLAG_EXCLUSIVE;
		__add_wait_queue_tail(&x->wait, &wait);
		do {
			__set_current_state(TASK_UNINTERRUPTIBLE);
			spin_unlock_irq(&x->wait.lock);
			schedule();
			spin_lock_irq(&x->wait.lock);
		} while (!x->done);
		__remove_wait_queue(&x->wait, &wait);
	}
	x->done--;
	spin_unlock_irq(&x->wait.lock);
}
EXPORT_SYMBOL(wait_for_completion);

unsigned long fastcall __sched
wait_for_completion_timeout(struct completion *x, unsigned long timeout)
{
	might_sleep();

	spin_lock_irq(&x->wait.lock);
	if (!x->done) {
		DECLARE_WAITQUEUE(wait, current);

		wait.flags |= WQ_FLAG_EXCLUSIVE;
		__add_wait_queue_tail(&x->wait, &wait);
		do {
			__set_current_state(TASK_UNINTERRUPTIBLE);
			spin_unlock_irq(&x->wait.lock);
			timeout = schedule_timeout(timeout);
			spin_lock_irq(&x->wait.lock);
			if (!timeout) {
				__remove_wait_queue(&x->wait, &wait);
				goto out;
			}
		} while (!x->done);
		__remove_wait_queue(&x->wait, &wait);
	}
	x->done--;
out:
	spin_unlock_irq(&x->wait.lock);
	return timeout;
}
EXPORT_SYMBOL(wait_for_completion_timeout);

int fastcall __sched wait_for_completion_interruptible(struct completion *x)
{
	int ret = 0;

	might_sleep();

	spin_lock_irq(&x->wait.lock);
	if (!x->done) {
		DECLARE_WAITQUEUE(wait, current);

		wait.flags |= WQ_FLAG_EXCLUSIVE;
		__add_wait_queue_tail(&x->wait, &wait);
		do {
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				__remove_wait_queue(&x->wait, &wait);
				goto out;
			}
			__set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irq(&x->wait.lock);
			schedule();
			spin_lock_irq(&x->wait.lock);
		} while (!x->done);
		__remove_wait_queue(&x->wait, &wait);
	}
	x->done--;
out:
	spin_unlock_irq(&x->wait.lock);

	return ret;
}
EXPORT_SYMBOL(wait_for_completion_interruptible);

unsigned long fastcall __sched
wait_for_completion_interruptible_timeout(struct completion *x,
					  unsigned long timeout)
{
	might_sleep();

	spin_lock_irq(&x->wait.lock);
	if (!x->done) {
		DECLARE_WAITQUEUE(wait, current);

		wait.flags |= WQ_FLAG_EXCLUSIVE;
		__add_wait_queue_tail(&x->wait, &wait);
		do {
			if (signal_pending(current)) {
				timeout = -ERESTARTSYS;
				__remove_wait_queue(&x->wait, &wait);
				goto out;
			}
			__set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irq(&x->wait.lock);
			timeout = schedule_timeout(timeout);
			spin_lock_irq(&x->wait.lock);
			if (!timeout) {
				__remove_wait_queue(&x->wait, &wait);
				goto out;
			}
		} while (!x->done);
		__remove_wait_queue(&x->wait, &wait);
	}
	x->done--;
out:
	spin_unlock_irq(&x->wait.lock);
	return timeout;
}
EXPORT_SYMBOL(wait_for_completion_interruptible_timeout);


#define	SLEEP_ON_VAR					\
	unsigned long flags;				\
	wait_queue_t wait;				\
	init_waitqueue_entry(&wait, current);

#define SLEEP_ON_HEAD					\
	spin_lock_irqsave(&q->lock,flags);		\
	__add_wait_queue(q, &wait);			\
	spin_unlock(&q->lock);

#define	SLEEP_ON_TAIL					\
	spin_lock_irq(&q->lock);			\
	__remove_wait_queue(q, &wait);			\
	spin_unlock_irqrestore(&q->lock, flags);

void fastcall __sched interruptible_sleep_on(wait_queue_head_t *q)
{
	SLEEP_ON_VAR

	current->state = TASK_INTERRUPTIBLE;

	SLEEP_ON_HEAD
	schedule();
	SLEEP_ON_TAIL
}
EXPORT_SYMBOL(interruptible_sleep_on);

long fastcall __sched
interruptible_sleep_on_timeout(wait_queue_head_t *q, long timeout)
{
	SLEEP_ON_VAR

	current->state = TASK_INTERRUPTIBLE;

	SLEEP_ON_HEAD
	timeout = schedule_timeout(timeout);
	SLEEP_ON_TAIL

	return timeout;
}
EXPORT_SYMBOL(interruptible_sleep_on_timeout);

void fastcall __sched sleep_on(wait_queue_head_t *q)
{
	SLEEP_ON_VAR

	current->state = TASK_UNINTERRUPTIBLE;

	SLEEP_ON_HEAD
	schedule();
	SLEEP_ON_TAIL
}
EXPORT_SYMBOL(sleep_on);

long fastcall __sched sleep_on_timeout(wait_queue_head_t *q, long timeout)
{
	SLEEP_ON_VAR

	current->state = TASK_UNINTERRUPTIBLE;

	SLEEP_ON_HEAD
	timeout = schedule_timeout(timeout);
	SLEEP_ON_TAIL

	return timeout;
}

EXPORT_SYMBOL(sleep_on_timeout);

#ifdef CONFIG_RT_MUTEXES

/*
 * rt_mutex_setprio - set the current priority of a task
 * @p: task
 * @prio: prio value (kernel-internal form)
 *
 * This function changes the 'effective' priority of a task. It does
 * not touch ->normal_prio like __setscheduler().
 *
 * Used by the rt_mutex code to implement priority inheritance logic.
 */
void rt_mutex_setprio(struct task_struct *p, int prio)
{
	unsigned long flags;
	int oldprio, on_rq;
	struct rq *rq;
	u64 now;

	BUG_ON(prio < 0 || prio > MAX_PRIO);

	rq = task_rq_lock(p, &flags);
	now = rq_clock(rq);

	oldprio = p->prio;
	on_rq = p->se.on_rq;
	if (on_rq)
		dequeue_task(rq, p, 0, now);

	if (rt_prio(prio))
		p->sched_class = &rt_sched_class;
	else
		p->sched_class = &fair_sched_class;

	p->prio = prio;

	if (on_rq) {
		enqueue_task(rq, p, 0, now);
		/*
		 * Reschedule if we are currently running on this runqueue and
		 * our priority decreased, or if we are not currently running on
		 * this runqueue and our priority is higher than the current's
		 */
		if (task_running(rq, p)) {
			if (p->prio > oldprio)
				resched_task(rq->curr);
		} else {
			check_preempt_curr(rq, p);
		}
	}
	task_rq_unlock(rq, &flags);
}

#endif

void set_user_nice(struct task_struct *p, long nice)
{
	int old_prio, delta, on_rq;
	unsigned long flags;
	struct rq *rq;
	u64 now;

	if (TASK_NICE(p) == nice || nice < -20 || nice > 19)
		return;
	/*
	 * We have to be careful, if called from sys_setpriority(),
	 * the task might be in the middle of scheduling on another CPU.
	 */
	rq = task_rq_lock(p, &flags);
	now = rq_clock(rq);
	/*
	 * The RT priorities are set via sched_setscheduler(), but we still
	 * allow the 'normal' nice value to be set - but as expected
	 * it wont have any effect on scheduling until the task is
	 * SCHED_FIFO/SCHED_RR:
	 */
	if (task_has_rt_policy(p)) {
		p->static_prio = NICE_TO_PRIO(nice);
		goto out_unlock;
	}
	on_rq = p->se.on_rq;
	if (on_rq) {
		dequeue_task(rq, p, 0, now);
		dec_load(rq, p, now);
	}

	p->static_prio = NICE_TO_PRIO(nice);
	set_load_weight(p);
	old_prio = p->prio;
	p->prio = effective_prio(p);
	delta = p->prio - old_prio;

	if (on_rq) {
		enqueue_task(rq, p, 0, now);
		inc_load(rq, p, now);
		/*
		 * If the task increased its priority or is running and
		 * lowered its priority, then reschedule its CPU:
		 */
		if (delta < 0 || (delta > 0 && task_running(rq, p)))
			resched_task(rq->curr);
	}
out_unlock:
	task_rq_unlock(rq, &flags);
}
EXPORT_SYMBOL(set_user_nice);

/*
 * can_nice - check if a task can reduce its nice value
 * @p: task
 * @nice: nice value
 */
int can_nice(const struct task_struct *p, const int nice)
{
	/* convert nice value [19,-20] to rlimit style value [1,40] */
	int nice_rlim = 20 - nice;

	return (nice_rlim <= p->signal->rlim[RLIMIT_NICE].rlim_cur ||
		capable(CAP_SYS_NICE));
}

#ifdef __ARCH_WANT_SYS_NICE

/*
 * sys_nice - change the priority of the current process.
 * @increment: priority increment
 *
 * sys_setpriority is a more generic, but much slower function that
 * does similar things.
 */
asmlinkage long sys_nice(int increment)
{
	long nice, retval;

	/*
	 * Setpriority might change our priority at the same moment.
	 * We don't have to worry. Conceptually one call occurs first
	 * and we have a single winner.
	 */
	if (increment < -40)
		increment = -40;
	if (increment > 40)
		increment = 40;

	nice = PRIO_TO_NICE(current->static_prio) + increment;
	if (nice < -20)
		nice = -20;
	if (nice > 19)
		nice = 19;

	if (increment < 0 && !can_nice(current, nice))
		return -EPERM;

	retval = security_task_setnice(current, nice);
	if (retval)
		return retval;

	set_user_nice(current, nice);
	return 0;
}

#endif

/**
 * task_prio - return the priority value of a given task.
 * @p: the task in question.
 *
 * This is the priority value as seen by users in /proc.
 * RT tasks are offset by -200. Normal tasks are centered
 * around 0, value goes from -16 to +15.
 */
int task_prio(const struct task_struct *p)
{
	return p->prio - MAX_RT_PRIO;
}

/**
 * task_nice - return the nice value of a given task.
 * @p: the task in question.
 */
int task_nice(const struct task_struct *p)
{
	return TASK_NICE(p);
}
EXPORT_SYMBOL_GPL(task_nice);

/**
 * idle_cpu - is a given cpu idle currently?
 * @cpu: the processor in question.
 */
int idle_cpu(int cpu)
{
	return cpu_curr(cpu) == cpu_rq(cpu)->idle;
}

/**
 * idle_task - return the idle task for a given cpu.
 * @cpu: the processor in question.
 */
struct task_struct *idle_task(int cpu)
{
	return cpu_rq(cpu)->idle;
}

/**
 * find_process_by_pid - find a process with a matching PID value.
 * @pid: the pid in question.
 */
static inline struct task_struct *find_process_by_pid(pid_t pid)
{
	return pid ? find_task_by_pid(pid) : current;
}

/* Actually do priority change: must hold rq lock. */
static void
__setscheduler(struct rq *rq, struct task_struct *p, int policy, int prio)
{
	BUG_ON(p->se.on_rq);

	p->policy = policy;
	switch (p->policy) {
	case SCHED_NORMAL:
	case SCHED_BATCH:
	case SCHED_IDLE:
		p->sched_class = &fair_sched_class;
		break;
	case SCHED_FIFO:
	case SCHED_RR:
		p->sched_class = &rt_sched_class;
		break;
	}

	p->rt_priority = prio;
	p->normal_prio = normal_prio(p);
	/* we are holding p->pi_lock already */
	p->prio = rt_mutex_getprio(p);
	set_load_weight(p);
}

/**
 * sched_setscheduler - change the scheduling policy and/or RT priority of a thread.
 * @p: the task in question.
 * @policy: new policy.
 * @param: structure containing the new RT priority.
 *
 * NOTE that the task may be already dead.
 */
int sched_setscheduler(struct task_struct *p, int policy,
		       struct sched_param *param)
{
	int retval, oldprio, oldpolicy = -1, on_rq;
	unsigned long flags;
	struct rq *rq;

	/* may grab non-irq protected spin_locks */
	BUG_ON(in_interrupt());
recheck:
	/* double check policy once rq lock held */
	if (policy < 0)
		policy = oldpolicy = p->policy;
	else if (policy != SCHED_FIFO && policy != SCHED_RR &&
			policy != SCHED_NORMAL && policy != SCHED_BATCH &&
			policy != SCHED_IDLE)
		return -EINVAL;
	/*
	 * Valid priorities for SCHED_FIFO and SCHED_RR are
	 * 1..MAX_USER_RT_PRIO-1, valid priority for SCHED_NORMAL,
	 * SCHED_BATCH and SCHED_IDLE is 0.
	 */
	if (param->sched_priority < 0 ||
	    (p->mm && param->sched_priority > MAX_USER_RT_PRIO-1) ||
	    (!p->mm && param->sched_priority > MAX_RT_PRIO-1))
		return -EINVAL;
	if (rt_policy(policy) != (param->sched_priority != 0))
		return -EINVAL;

	/*
	 * Allow unprivileged RT tasks to decrease priority:
	 */
	if (!capable(CAP_SYS_NICE)) {
		if (rt_policy(policy)) {
			unsigned long rlim_rtprio;

			if (!lock_task_sighand(p, &flags))
				return -ESRCH;
			rlim_rtprio = p->signal->rlim[RLIMIT_RTPRIO].rlim_cur;
			unlock_task_sighand(p, &flags);

			/* can't set/change the rt policy */
			if (policy != p->policy && !rlim_rtprio)
				return -EPERM;

			/* can't increase priority */
			if (param->sched_priority > p->rt_priority &&
			    param->sched_priority > rlim_rtprio)
				return -EPERM;
		}
		/*
		 * Like positive nice levels, dont allow tasks to
		 * move out of SCHED_IDLE either:
		 */
		if (p->policy == SCHED_IDLE && policy != SCHED_IDLE)
			return -EPERM;

		/* can't change other user's priorities */
		if ((current->euid != p->euid) &&
		    (current->euid != p->uid))
			return -EPERM;
	}

	retval = security_task_setscheduler(p, policy, param);
	if (retval)
		return retval;
	/*
	 * make sure no PI-waiters arrive (or leave) while we are
	 * changing the priority of the task:
	 */
	spin_lock_irqsave(&p->pi_lock, flags);
	/*
	 * To be able to change p->policy safely, the apropriate
	 * runqueue lock must be held.
	 */
	rq = __task_rq_lock(p);
	/* recheck policy now with rq lock held */
	if (unlikely(oldpolicy != -1 && oldpolicy != p->policy)) {
		policy = oldpolicy = -1;
		__task_rq_unlock(rq);
		spin_unlock_irqrestore(&p->pi_lock, flags);
		goto recheck;
	}
	on_rq = p->se.on_rq;
	if (on_rq)
		deactivate_task(rq, p, 0);
	oldprio = p->prio;
	__setscheduler(rq, p, policy, param->sched_priority);
	if (on_rq) {
		activate_task(rq, p, 0);
		/*
		 * Reschedule if we are currently running on this runqueue and
		 * our priority decreased, or if we are not currently running on
		 * this runqueue and our priority is higher than the current's
		 */
		if (task_running(rq, p)) {
			if (p->prio > oldprio)
				resched_task(rq->curr);
		} else {
			check_preempt_curr(rq, p);
		}
	}
	__task_rq_unlock(rq);
	spin_unlock_irqrestore(&p->pi_lock, flags);

	rt_mutex_adjust_pi(p);

	return 0;
}
EXPORT_SYMBOL_GPL(sched_setscheduler);

static int
do_sched_setscheduler(pid_t pid, int policy, struct sched_param __user *param)
{
	struct sched_param lparam;
	struct task_struct *p;
	int retval;

	if (!param || pid < 0)
		return -EINVAL;
	if (copy_from_user(&lparam, param, sizeof(struct sched_param)))
		return -EFAULT;

	rcu_read_lock();
	retval = -ESRCH;
	p = find_process_by_pid(pid);
	if (p != NULL)
		retval = sched_setscheduler(p, policy, &lparam);
	rcu_read_unlock();

	return retval;
}

/**
 * sys_sched_setscheduler - set/change the scheduler policy and RT priority
 * @pid: the pid in question.
 * @policy: new policy.
 * @param: structure containing the new RT priority.
 */
asmlinkage long sys_sched_setscheduler(pid_t pid, int policy,
				       struct sched_param __user *param)
{
	/* negative values for policy are not valid */
	if (policy < 0)
		return -EINVAL;

	return do_sched_setscheduler(pid, policy, param);
}

/**
 * sys_sched_setparam - set/change the RT priority of a thread
 * @pid: the pid in question.
 * @param: structure containing the new RT priority.
 */
asmlinkage long sys_sched_setparam(pid_t pid, struct sched_param __user *param)
{
	return do_sched_setscheduler(pid, -1, param);
}

/**
 * sys_sched_getscheduler - get the policy (scheduling class) of a thread
 * @pid: the pid in question.
 */
asmlinkage long sys_sched_getscheduler(pid_t pid)
{
	struct task_struct *p;
	int retval = -EINVAL;

	if (pid < 0)
		goto out_nounlock;

	retval = -ESRCH;
	read_lock(&tasklist_lock);
	p = find_process_by_pid(pid);
	if (p) {
		retval = security_task_getscheduler(p);
		if (!retval)
			retval = p->policy;
	}
	read_unlock(&tasklist_lock);

out_nounlock:
	return retval;
}

/**
 * sys_sched_getscheduler - get the RT priority of a thread
 * @pid: the pid in question.
 * @param: structure containing the RT priority.
 */
asmlinkage long sys_sched_getparam(pid_t pid, struct sched_param __user *param)
{
	struct sched_param lp;
	struct task_struct *p;
	int retval = -EINVAL;

	if (!param || pid < 0)
		goto out_nounlock;

	read_lock(&tasklist_lock);
	p = find_process_by_pid(pid);
	retval = -ESRCH;
	if (!p)
		goto out_unlock;

	retval = security_task_getscheduler(p);
	if (retval)
		goto out_unlock;

	lp.sched_priority = p->rt_priority;
	read_unlock(&tasklist_lock);

	/*
	 * This one might sleep, we cannot do it with a spinlock held ...
	 */
	retval = copy_to_user(param, &lp, sizeof(*param)) ? -EFAULT : 0;

out_nounlock:
	return retval;

out_unlock:
	read_unlock(&tasklist_lock);
	return retval;
}

long sched_setaffinity(pid_t pid, cpumask_t new_mask)
{
	cpumask_t cpus_allowed;
	struct task_struct *p;
	int retval;

	mutex_lock(&sched_hotcpu_mutex);
	read_lock(&tasklist_lock);

	p = find_process_by_pid(pid);
	if (!p) {
		read_unlock(&tasklist_lock);
		mutex_unlock(&sched_hotcpu_mutex);
		return -ESRCH;
	}

	/*
	 * It is not safe to call set_cpus_allowed with the
	 * tasklist_lock held.  We will bump the task_struct's
	 * usage count and then drop tasklist_lock.
	 */
	get_task_struct(p);
	read_unlock(&tasklist_lock);

	retval = -EPERM;
	if ((current->euid != p->euid) && (current->euid != p->uid) &&
			!capable(CAP_SYS_NICE))
		goto out_unlock;

	retval = security_task_setscheduler(p, 0, NULL);
	if (retval)
		goto out_unlock;

	cpus_allowed = cpuset_cpus_allowed(p);
	cpus_and(new_mask, new_mask, cpus_allowed);
	retval = set_cpus_allowed(p, new_mask);

out_unlock:
	put_task_struct(p);
	mutex_unlock(&sched_hotcpu_mutex);
	return retval;
}

static int get_user_cpu_mask(unsigned long __user *user_mask_ptr, unsigned len,
			     cpumask_t *new_mask)
{
	if (len < sizeof(cpumask_t)) {
		memset(new_mask, 0, sizeof(cpumask_t));
	} else if (len > sizeof(cpumask_t)) {
		len = sizeof(cpumask_t);
	}
	return copy_from_user(new_mask, user_mask_ptr, len) ? -EFAULT : 0;
}

/**
 * sys_sched_setaffinity - set the cpu affinity of a process
 * @pid: pid of the process
 * @len: length in bytes of the bitmask pointed to by user_mask_ptr
 * @user_mask_ptr: user-space pointer to the new cpu mask
 */
asmlinkage long sys_sched_setaffinity(pid_t pid, unsigned int len,
				      unsigned long __user *user_mask_ptr)
{
	cpumask_t new_mask;
	int retval;

	retval = get_user_cpu_mask(user_mask_ptr, len, &new_mask);
	if (retval)
		return retval;

	return sched_setaffinity(pid, new_mask);
}

/*
 * Represents all cpu's present in the system
 * In systems capable of hotplug, this map could dynamically grow
 * as new cpu's are detected in the system via any platform specific
 * method, such as ACPI for e.g.
 */

cpumask_t cpu_present_map __read_mostly;
EXPORT_SYMBOL(cpu_present_map);

#ifndef CONFIG_SMP
cpumask_t cpu_online_map __read_mostly = CPU_MASK_ALL;
EXPORT_SYMBOL(cpu_online_map);

cpumask_t cpu_possible_map __read_mostly = CPU_MASK_ALL;
EXPORT_SYMBOL(cpu_possible_map);
#endif

long sched_getaffinity(pid_t pid, cpumask_t *mask)
{
	struct task_struct *p;
	int retval;

	mutex_lock(&sched_hotcpu_mutex);
	read_lock(&tasklist_lock);

	retval = -ESRCH;
	p = find_process_by_pid(pid);
	if (!p)
		goto out_unlock;

	retval = security_task_getscheduler(p);
	if (retval)
		goto out_unlock;

	cpus_and(*mask, p->cpus_allowed, cpu_online_map);

out_unlock:
	read_unlock(&tasklist_lock);
	mutex_unlock(&sched_hotcpu_mutex);
	if (retval)
		return retval;

	return 0;
}

/**
 * sys_sched_getaffinity - get the cpu affinity of a process
 * @pid: pid of the process
 * @len: length in bytes of the bitmask pointed to by user_mask_ptr
 * @user_mask_ptr: user-space pointer to hold the current cpu mask
 */
asmlinkage long sys_sched_getaffinity(pid_t pid, unsigned int len,
				      unsigned long __user *user_mask_ptr)
{
	int ret;
	cpumask_t mask;

	if (len < sizeof(cpumask_t))
		return -EINVAL;

	ret = sched_getaffinity(pid, &mask);
	if (ret < 0)
		return ret;

	if (copy_to_user(user_mask_ptr, &mask, sizeof(cpumask_t)))
		return -EFAULT;

	return sizeof(cpumask_t);
}

/**
 * sys_sched_yield - yield the current processor to other threads.
 *
 * This function yields the current CPU to other tasks. If there are no
 * other threads running on this CPU then this function will return.
 */
asmlinkage long sys_sched_yield(void)
{
	struct rq *rq = this_rq_lock();

	schedstat_inc(rq, yld_cnt);
	if (unlikely(rq->nr_running == 1))
		schedstat_inc(rq, yld_act_empty);
	else
		current->sched_class->yield_task(rq, current);

	/*
	 * Since we are going to call schedule() anyway, there's
	 * no need to preempt or enable interrupts:
	 */
	__release(rq->lock);
	spin_release(&rq->lock.dep_map, 1, _THIS_IP_);
	_raw_spin_unlock(&rq->lock);
	preempt_enable_no_resched();

	schedule();

	return 0;
}

static void __cond_resched(void)
{
#ifdef CONFIG_DEBUG_SPINLOCK_SLEEP
	__might_sleep(__FILE__, __LINE__);
#endif
	/*
	 * The BKS might be reacquired before we have dropped
	 * PREEMPT_ACTIVE, which could trigger a second
	 * cond_resched() call.
	 */
	do {
		add_preempt_count(PREEMPT_ACTIVE);
		schedule();
		sub_preempt_count(PREEMPT_ACTIVE);
	} while (need_resched());
}

int __sched cond_resched(void)
{
	if (need_resched() && !(preempt_count() & PREEMPT_ACTIVE) &&
					system_state == SYSTEM_RUNNING) {
		__cond_resched();
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(cond_resched);

/*
 * cond_resched_lock() - if a reschedule is pending, drop the given lock,
 * call schedule, and on return reacquire the lock.
 *
 * This works OK both with and without CONFIG_PREEMPT.  We do strange low-level
 * operations here to prevent schedule() from being called twice (once via
 * spin_unlock(), once by hand).
 */
int cond_resched_lock(spinlock_t *lock)
{
	int ret = 0;

	if (need_lockbreak(lock)) {
		spin_unlock(lock);
		cpu_relax();
		ret = 1;
		spin_lock(lock);
	}
	if (need_resched() && system_state == SYSTEM_RUNNING) {
		spin_release(&lock->dep_map, 1, _THIS_IP_);
		_raw_spin_unlock(lock);
		preempt_enable_no_resched();
		__cond_resched();
		ret = 1;
		spin_lock(lock);
	}
	return ret;
}
EXPORT_SYMBOL(cond_resched_lock);

int __sched cond_resched_softirq(void)
{
	BUG_ON(!in_softirq());

	if (need_resched() && system_state == SYSTEM_RUNNING) {
		local_bh_enable();
		__cond_resched();
		local_bh_disable();
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(cond_resched_softirq);

/**
 * yield - yield the current processor to other threads.
 *
 * This is a shortcut for kernel-space yielding - it marks the
 * thread runnable and calls sys_sched_yield().
 */
void __sched yield(void)
{
	set_current_state(TASK_RUNNING);
	sys_sched_yield();
}
EXPORT_SYMBOL(yield);

/*
 * This task is about to go to sleep on IO.  Increment rq->nr_iowait so
 * that process accounting knows that this is a task in IO wait state.
 *
 * But don't do that if it is a deliberate, throttling IO wait (this task
 * has set its backing_dev_info: the queue against which it should throttle)
 */
void __sched io_schedule(void)
{
	struct rq *rq = &__raw_get_cpu_var(runqueues);

	delayacct_blkio_start();
	atomic_inc(&rq->nr_iowait);
	schedule();
	atomic_dec(&rq->nr_iowait);
	delayacct_blkio_end();
}
EXPORT_SYMBOL(io_schedule);

long __sched io_schedule_timeout(long timeout)
{
	struct rq *rq = &__raw_get_cpu_var(runqueues);
	long ret;

	delayacct_blkio_start();
	atomic_inc(&rq->nr_iowait);
	ret = schedule_timeout(timeout);
	atomic_dec(&rq->nr_iowait);
	delayacct_blkio_end();
	return ret;
}

/**
 * sys_sched_get_priority_max - return maximum RT priority.
 * @policy: scheduling class.
 *
 * this syscall returns the maximum rt_priority that can be used
 * by a given scheduling class.
 */
asmlinkage long sys_sched_get_priority_max(int policy)
{
	int ret = -EINVAL;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = MAX_USER_RT_PRIO-1;
		break;
	case SCHED_NORMAL:
	case SCHED_BATCH:
	case SCHED_IDLE:
		ret = 0;
		break;
	}
	return ret;
}

/**
 * sys_sched_get_priority_min - return minimum RT priority.
 * @policy: scheduling class.
 *
 * this syscall returns the minimum rt_priority that can be used
 * by a given scheduling class.
 */
asmlinkage long sys_sched_get_priority_min(int policy)
{
	int ret = -EINVAL;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = 1;
		break;
	case SCHED_NORMAL:
	case SCHED_BATCH:
	case SCHED_IDLE:
		ret = 0;
	}
	return ret;
}

/**
 * sys_sched_rr_get_interval - return the default timeslice of a process.
 * @pid: pid of the process.
 * @interval: userspace pointer to the timeslice value.
 *
 * this syscall writes the default timeslice value of a given process
 * into the user-space timespec buffer. A value of '0' means infinity.
 */
asmlinkage
long sys_sched_rr_get_interval(pid_t pid, struct timespec __user *interval)
{
	struct task_struct *p;
	int retval = -EINVAL;
	struct timespec t;

	if (pid < 0)
		goto out_nounlock;

	retval = -ESRCH;
	read_lock(&tasklist_lock);
	p = find_process_by_pid(pid);
	if (!p)
		goto out_unlock;

	retval = security_task_getscheduler(p);
	if (retval)
		goto out_unlock;

	jiffies_to_timespec(p->policy == SCHED_FIFO ?
				0 : static_prio_timeslice(p->static_prio), &t);
	read_unlock(&tasklist_lock);
	retval = copy_to_user(interval, &t, sizeof(t)) ? -EFAULT : 0;
out_nounlock:
	return retval;
out_unlock:
	read_unlock(&tasklist_lock);
	return retval;
}

static const char stat_nam[] = "RSDTtZX";

static void show_task(struct task_struct *p)
{
	unsigned long free = 0;
	unsigned state;

	state = p->state ? __ffs(p->state) + 1 : 0;
	printk("%-13.13s %c", p->comm,
		state < sizeof(stat_nam) - 1 ? stat_nam[state] : '?');
#if (BITS_PER_LONG == 32)
	if (state == TASK_RUNNING)
		printk(" running ");
	else
		printk(" %08lX ", thread_saved_pc(p));
#else
	if (state == TASK_RUNNING)
		printk("  running task   ");
	else
		printk(" %016lx ", thread_saved_pc(p));
#endif
#ifdef CONFIG_DEBUG_STACK_USAGE
	{
		unsigned long *n = end_of_stack(p);
		while (!*n)
			n++;
		free = (unsigned long)n - (unsigned long)end_of_stack(p);
	}
#endif
	printk("%5lu %5d %6d", free, p->pid, p->parent->pid);
	if (!p->mm)
		printk(" (L-TLB)\n");
	else
		printk(" (NOTLB)\n");

	if (state != TASK_RUNNING)
		show_stack(p, NULL);
}

void show_state_filter(unsigned long state_filter)
{
	struct task_struct *g, *p;

#if (BITS_PER_LONG == 32)
	printk("\n"
	       "                         free                        sibling\n");
	printk("  task             PC    stack   pid father child younger older\n");
#else
	printk("\n"
	       "                                 free                        sibling\n");
	printk("  task                 PC        stack   pid father child younger older\n");
#endif
	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		/*
		 * reset the NMI-timeout, listing all files on a slow
		 * console might take alot of time:
		 */
		touch_nmi_watchdog();
		if (!state_filter || (p->state & state_filter))
			show_task(p);
	} while_each_thread(g, p);

	touch_all_softlockup_watchdogs();

#ifdef CONFIG_SCHED_DEBUG
	sysrq_sched_debug_show();
#endif
	read_unlock(&tasklist_lock);
	/*
	 * Only show locks if all tasks are dumped:
	 */
	if (state_filter == -1)
		debug_show_all_locks();
}

void __cpuinit init_idle_bootup_task(struct task_struct *idle)
{
	idle->sched_class = &idle_sched_class;
}

/**
 * init_idle - set up an idle thread for a given CPU
 * @idle: task in question
 * @cpu: cpu the idle task belongs to
 *
 * NOTE: this function does not set the idle thread's NEED_RESCHED
 * flag, to make booting more robust.
 */
void __cpuinit init_idle(struct task_struct *idle, int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	__sched_fork(idle);
	idle->se.exec_start = sched_clock();

	idle->prio = idle->normal_prio = MAX_PRIO;
	idle->cpus_allowed = cpumask_of_cpu(cpu);
	__set_task_cpu(idle, cpu);

	spin_lock_irqsave(&rq->lock, flags);
	rq->curr = rq->idle = idle;
#if defined(CONFIG_SMP) && defined(__ARCH_WANT_UNLOCKED_CTXSW)
	idle->oncpu = 1;
#endif
	spin_unlock_irqrestore(&rq->lock, flags);

	/* Set the preempt count _outside_ the spinlocks! */
#if defined(CONFIG_PREEMPT) && !defined(CONFIG_PREEMPT_BKL)
	task_thread_info(idle)->preempt_count = (idle->lock_depth >= 0);
#else
	task_thread_info(idle)->preempt_count = 0;
#endif
	/*
	 * The idle tasks have their own, simple scheduling class:
	 */
	idle->sched_class = &idle_sched_class;
}

/*
 * In a system that switches off the HZ timer nohz_cpu_mask
 * indicates which cpus entered this state. This is used
 * in the rcu update to wait only for active cpus. For system
 * which do not switch off the HZ timer nohz_cpu_mask should
 * always be CPU_MASK_NONE.
 */
cpumask_t nohz_cpu_mask = CPU_MASK_NONE;

/*
 * Increase the granularity value when there are more CPUs,
 * because with more CPUs the 'effective latency' as visible
 * to users decreases. But the relationship is not linear,
 * so pick a second-best guess by going with the log2 of the
 * number of CPUs.
 *
 * This idea comes from the SD scheduler of Con Kolivas:
 */
static inline void sched_init_granularity(void)
{
	unsigned int factor = 1 + ilog2(num_online_cpus());
	const unsigned long gran_limit = 10000000;

	sysctl_sched_granularity *= factor;
	if (sysctl_sched_granularity > gran_limit)
		sysctl_sched_granularity = gran_limit;

	sysctl_sched_runtime_limit = sysctl_sched_granularity * 4;
	sysctl_sched_wakeup_granularity = sysctl_sched_granularity / 2;
}

#ifdef CONFIG_SMP
/*
 * This is how migration works:
 *
 * 1) we queue a struct migration_req structure in the source CPU's
 *    runqueue and wake up that CPU's migration thread.
 * 2) we down() the locked semaphore => thread blocks.
 * 3) migration thread wakes up (implicitly it forces the migrated
 *    thread off the CPU)
 * 4) it gets the migration request and checks whether the migrated
 *    task is still in the wrong runqueue.
 * 5) if it's in the wrong runqueue then the migration thread removes
 *    it and puts it into the right queue.
 * 6) migration thread up()s the semaphore.
 * 7) we wake up and the migration is done.
 */

/*
 * Change a given task's CPU affinity. Migrate the thread to a
 * proper CPU and schedule it away if the CPU it's executing on
 * is removed from the allowed bitmask.
 *
 * NOTE: the caller must have a valid reference to the task, the
 * task must not exit() & deallocate itself prematurely.  The
 * call is not atomic; no spinlocks may be held.
 */
int set_cpus_allowed(struct task_struct *p, cpumask_t new_mask)
{
	struct migration_req req;
	unsigned long flags;
	struct rq *rq;
	int ret = 0;

	rq = task_rq_lock(p, &flags);
	if (!cpus_intersects(new_mask, cpu_online_map)) {
		ret = -EINVAL;
		goto out;
	}

	p->cpus_allowed = new_mask;
	/* Can the task run on the task's current CPU? If so, we're done */
	if (cpu_isset(task_cpu(p), new_mask))
		goto out;

	if (migrate_task(p, any_online_cpu(new_mask), &req)) {
		/* Need help from migration thread: drop lock and wait. */
		task_rq_unlock(rq, &flags);
		wake_up_process(rq->migration_thread);
		wait_for_completion(&req.done);
		tlb_migrate_finish(p->mm);
		return 0;
	}
out:
	task_rq_unlock(rq, &flags);

	return ret;
}
EXPORT_SYMBOL_GPL(set_cpus_allowed);

/*
 * Move (not current) task off this cpu, onto dest cpu.  We're doing
 * this because either it can't run here any more (set_cpus_allowed()
 * away from this CPU, or CPU going down), or because we're
 * attempting to rebalance this task on exec (sched_exec).
 *
 * So we race with normal scheduler movements, but that's OK, as long
 * as the task is no longer on this CPU.
 *
 * Returns non-zero if task was successfully migrated.
 */
static int __migrate_task(struct task_struct *p, int src_cpu, int dest_cpu)
{
	struct rq *rq_dest, *rq_src;
	int ret = 0, on_rq;

	if (unlikely(cpu_is_offline(dest_cpu)))
		return ret;

	rq_src = cpu_rq(src_cpu);
	rq_dest = cpu_rq(dest_cpu);

	double_rq_lock(rq_src, rq_dest);
	/* Already moved. */
	if (task_cpu(p) != src_cpu)
		goto out;
	/* Affinity changed (again). */
	if (!cpu_isset(dest_cpu, p->cpus_allowed))
		goto out;

	on_rq = p->se.on_rq;
	if (on_rq)
		deactivate_task(rq_src, p, 0);
	set_task_cpu(p, dest_cpu);
	if (on_rq) {
		activate_task(rq_dest, p, 0);
		check_preempt_curr(rq_dest, p);
	}
	ret = 1;
out:
	double_rq_unlock(rq_src, rq_dest);
	return ret;
}

/*
 * migration_thread - this is a highprio system thread that performs
 * thread migration by bumping thread off CPU then 'pushing' onto
 * another runqueue.
 */
static int migration_thread(void *data)
{
	int cpu = (long)data;
	struct rq *rq;

	rq = cpu_rq(cpu);
	BUG_ON(rq->migration_thread != current);

	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		struct migration_req *req;
		struct list_head *head;

		try_to_freeze();

		spin_lock_irq(&rq->lock);

		if (cpu_is_offline(cpu)) {
			spin_unlock_irq(&rq->lock);
			goto wait_to_die;
		}

		if (rq->active_balance) {
			active_load_balance(rq, cpu);
			rq->active_balance = 0;
		}

		head = &rq->migration_queue;

		if (list_empty(head)) {
			spin_unlock_irq(&rq->lock);
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
			continue;
		}
		req = list_entry(head->next, struct migration_req, list);
		list_del_init(head->next);

		spin_unlock(&rq->lock);
		__migrate_task(req->task, cpu, req->dest_cpu);
		local_irq_enable();

		complete(&req->done);
	}
	__set_current_state(TASK_RUNNING);
	return 0;

wait_to_die:
	/* Wait for kthread_stop */
	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * Figure out where task on dead CPU should go, use force if neccessary.
 * NOTE: interrupts should be disabled by the caller
 */
static void move_task_off_dead_cpu(int dead_cpu, struct task_struct *p)
{
	unsigned long flags;
	cpumask_t mask;
	struct rq *rq;
	int dest_cpu;

restart:
	/* On same node? */
	mask = node_to_cpumask(cpu_to_node(dead_cpu));
	cpus_and(mask, mask, p->cpus_allowed);
	dest_cpu = any_online_cpu(mask);

	/* On any allowed CPU? */
	if (dest_cpu == NR_CPUS)
		dest_cpu = any_online_cpu(p->cpus_allowed);

	/* No more Mr. Nice Guy. */
	if (dest_cpu == NR_CPUS) {
		rq = task_rq_lock(p, &flags);
		cpus_setall(p->cpus_allowed);
		dest_cpu = any_online_cpu(p->cpus_allowed);
		task_rq_unlock(rq, &flags);

		/*
		 * Don't tell them about moving exiting tasks or
		 * kernel threads (both mm NULL), since they never
		 * leave kernel.
		 */
		if (p->mm && printk_ratelimit())
			printk(KERN_INFO "process %d (%s) no "
			       "longer affine to cpu%d\n",
			       p->pid, p->comm, dead_cpu);
	}
	if (!__migrate_task(p, dead_cpu, dest_cpu))
		goto restart;
}

/*
 * While a dead CPU has no uninterruptible tasks queued at this point,
 * it might still have a nonzero ->nr_uninterruptible counter, because
 * for performance reasons the counter is not stricly tracking tasks to
 * their home CPUs. So we just add the counter to another CPU's counter,
 * to keep the global sum constant after CPU-down:
 */
static void migrate_nr_uninterruptible(struct rq *rq_src)
{
	struct rq *rq_dest = cpu_rq(any_online_cpu(CPU_MASK_ALL));
	unsigned long flags;

	local_irq_save(flags);
	double_rq_lock(rq_src, rq_dest);
	rq_dest->nr_uninterruptible += rq_src->nr_uninterruptible;
	rq_src->nr_uninterruptible = 0;
	double_rq_unlock(rq_src, rq_dest);
	local_irq_restore(flags);
}

/* Run through task list and migrate tasks from the dead cpu. */
static void migrate_live_tasks(int src_cpu)
{
	struct task_struct *p, *t;

	write_lock_irq(&tasklist_lock);

	do_each_thread(t, p) {
		if (p == current)
			continue;

		if (task_cpu(p) == src_cpu)
			move_task_off_dead_cpu(src_cpu, p);
	} while_each_thread(t, p);

	write_unlock_irq(&tasklist_lock);
}

/*
 * Schedules idle task to be the next runnable task on current CPU.
 * It does so by boosting its priority to highest possible and adding it to
 * the _front_ of the runqueue. Used by CPU offline code.
 */
void sched_idle_next(void)
{
	int this_cpu = smp_processor_id();
	struct rq *rq = cpu_rq(this_cpu);
	struct task_struct *p = rq->idle;
	unsigned long flags;

	/* cpu has to be offline */
	BUG_ON(cpu_online(this_cpu));

	/*
	 * Strictly not necessary since rest of the CPUs are stopped by now
	 * and interrupts disabled on the current cpu.
	 */
	spin_lock_irqsave(&rq->lock, flags);

	__setscheduler(rq, p, SCHED_FIFO, MAX_RT_PRIO-1);

	/* Add idle task to the _front_ of its priority queue: */
	activate_idle_task(p, rq);

	spin_unlock_irqrestore(&rq->lock, flags);
}

/*
 * Ensures that the idle task is using init_mm right before its cpu goes
 * offline.
 */
void idle_task_exit(void)
{
	struct mm_struct *mm = current->active_mm;

	BUG_ON(cpu_online(smp_processor_id()));

	if (mm != &init_mm)
		switch_mm(mm, &init_mm, current);
	mmdrop(mm);
}

/* called under rq->lock with disabled interrupts */
static void migrate_dead(unsigned int dead_cpu, struct task_struct *p)
{
	struct rq *rq = cpu_rq(dead_cpu);

	/* Must be exiting, otherwise would be on tasklist. */
	BUG_ON(p->exit_state != EXIT_ZOMBIE && p->exit_state != EXIT_DEAD);

	/* Cannot have done final schedule yet: would have vanished. */
	BUG_ON(p->state == TASK_DEAD);

	get_task_struct(p);

	/*
	 * Drop lock around migration; if someone else moves it,
	 * that's OK.  No task can be added to this CPU, so iteration is
	 * fine.
	 * NOTE: interrupts should be left disabled  --dev@
	 */
	spin_unlock(&rq->lock);
	move_task_off_dead_cpu(dead_cpu, p);
	spin_lock(&rq->lock);

	put_task_struct(p);
}

/* release_task() removes task from tasklist, so we won't find dead tasks. */
static void migrate_dead_tasks(unsigned int dead_cpu)
{
	struct rq *rq = cpu_rq(dead_cpu);
	struct task_struct *next;

	for ( ; ; ) {
		if (!rq->nr_running)
			break;
		next = pick_next_task(rq, rq->curr, rq_clock(rq));
		if (!next)
			break;
		migrate_dead(dead_cpu, next);
	}
}
#endif /* CONFIG_HOTPLUG_CPU */

/*
 * migration_call - callback that gets triggered when a CPU is added.
 * Here we can start up the necessary migration thread for the new CPU.
 */
static int __cpuinit
migration_call(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	struct task_struct *p;
	int cpu = (long)hcpu;
	unsigned long flags;
	struct rq *rq;

	switch (action) {
	case CPU_LOCK_ACQUIRE:
		mutex_lock(&sched_hotcpu_mutex);
		break;

	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		p = kthread_create(migration_thread, hcpu, "migration/%d", cpu);
		if (IS_ERR(p))
			return NOTIFY_BAD;
		p->flags |= PF_NOFREEZE;
		kthread_bind(p, cpu);
		/* Must be high prio: stop_machine expects to yield to it. */
		rq = task_rq_lock(p, &flags);
		__setscheduler(rq, p, SCHED_FIFO, MAX_RT_PRIO-1);
		task_rq_unlock(rq, &flags);
		cpu_rq(cpu)->migration_thread = p;
		break;

	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		/* Strictly unneccessary, as first user will wake it. */
		wake_up_process(cpu_rq(cpu)->migration_thread);
		break;

#ifdef CONFIG_HOTPLUG_CPU
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		if (!cpu_rq(cpu)->migration_thread)
			break;
		/* Unbind it from offline cpu so it can run.  Fall thru. */
		kthread_bind(cpu_rq(cpu)->migration_thread,
			     any_online_cpu(cpu_online_map));
		kthread_stop(cpu_rq(cpu)->migration_thread);
		cpu_rq(cpu)->migration_thread = NULL;
		break;

	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		migrate_live_tasks(cpu);
		rq = cpu_rq(cpu);
		kthread_stop(rq->migration_thread);
		rq->migration_thread = NULL;
		/* Idle task back to normal (off runqueue, low prio) */
		rq = task_rq_lock(rq->idle, &flags);
		deactivate_task(rq, rq->idle, 0);
		rq->idle->static_prio = MAX_PRIO;
		__setscheduler(rq, rq->idle, SCHED_NORMAL, 0);
		rq->idle->sched_class = &idle_sched_class;
		migrate_dead_tasks(cpu);
		task_rq_unlock(rq, &flags);
		migrate_nr_uninterruptible(rq);
		BUG_ON(rq->nr_running != 0);

		/* No need to migrate the tasks: it was best-effort if
		 * they didn't take sched_hotcpu_mutex.  Just wake up
		 * the requestors. */
		spin_lock_irq(&rq->lock);
		while (!list_empty(&rq->migration_queue)) {
			struct migration_req *req;

			req = list_entry(rq->migration_queue.next,
					 struct migration_req, list);
			list_del_init(&req->list);
			complete(&req->done);
		}
		spin_unlock_irq(&rq->lock);
		break;
#endif
	case CPU_LOCK_RELEASE:
		mutex_unlock(&sched_hotcpu_mutex);
		break;
	}
	return NOTIFY_OK;
}

/* Register at highest priority so that task migration (migrate_all_tasks)
 * happens before everything else.
 */
static struct notifier_block __cpuinitdata migration_notifier = {
	.notifier_call = migration_call,
	.priority = 10
};

int __init migration_init(void)
{
	void *cpu = (void *)(long)smp_processor_id();
	int err;

	/* Start one for the boot CPU: */
	err = migration_call(&migration_notifier, CPU_UP_PREPARE, cpu);
	BUG_ON(err == NOTIFY_BAD);
	migration_call(&migration_notifier, CPU_ONLINE, cpu);
	register_cpu_notifier(&migration_notifier);

	return 0;
}
#endif

#ifdef CONFIG_SMP

/* Number of possible processor ids */
int nr_cpu_ids __read_mostly = NR_CPUS;
EXPORT_SYMBOL(nr_cpu_ids);

#undef SCHED_DOMAIN_DEBUG
#ifdef SCHED_DOMAIN_DEBUG
static void sched_domain_debug(struct sched_domain *sd, int cpu)
{
	int level = 0;

	if (!sd) {
		printk(KERN_DEBUG "CPU%d attaching NULL sched-domain.\n", cpu);
		return;
	}

	printk(KERN_DEBUG "CPU%d attaching sched-domain:\n", cpu);

	do {
		int i;
		char str[NR_CPUS];
		struct sched_group *group = sd->groups;
		cpumask_t groupmask;

		cpumask_scnprintf(str, NR_CPUS, sd->span);
		cpus_clear(groupmask);

		printk(KERN_DEBUG);
		for (i = 0; i < level + 1; i++)
			printk(" ");
		printk("domain %d: ", level);

		if (!(sd->flags & SD_LOAD_BALANCE)) {
			printk("does not load-balance\n");
			if (sd->parent)
				printk(KERN_ERR "ERROR: !SD_LOAD_BALANCE domain"
						" has parent");
			break;
		}

		printk("span %s\n", str);

		if (!cpu_isset(cpu, sd->span))
			printk(KERN_ERR "ERROR: domain->span does not contain "
					"CPU%d\n", cpu);
		if (!cpu_isset(cpu, group->cpumask))
			printk(KERN_ERR "ERROR: domain->groups does not contain"
					" CPU%d\n", cpu);

		printk(KERN_DEBUG);
		for (i = 0; i < level + 2; i++)
			printk(" ");
		printk("groups:");
		do {
			if (!group) {
				printk("\n");
				printk(KERN_ERR "ERROR: group is NULL\n");
				break;
			}

			if (!group->__cpu_power) {
				printk("\n");
				printk(KERN_ERR "ERROR: domain->cpu_power not "
						"set\n");
			}

			if (!cpus_weight(group->cpumask)) {
				printk("\n");
				printk(KERN_ERR "ERROR: empty group\n");
			}

			if (cpus_intersects(groupmask, group->cpumask)) {
				printk("\n");
				printk(KERN_ERR "ERROR: repeated CPUs\n");
			}

			cpus_or(groupmask, groupmask, group->cpumask);

			cpumask_scnprintf(str, NR_CPUS, group->cpumask);
			printk(" %s", str);

			group = group->next;
		} while (group != sd->groups);
		printk("\n");

		if (!cpus_equal(sd->span, groupmask))
			printk(KERN_ERR "ERROR: groups don't span "
					"domain->span\n");

		level++;
		sd = sd->parent;
		if (!sd)
			continue;

		if (!cpus_subset(groupmask, sd->span))
			printk(KERN_ERR "ERROR: parent span is not a superset "
				"of domain->span\n");

	} while (sd);
}
#else
# define sched_domain_debug(sd, cpu) do { } while (0)
#endif

static int sd_degenerate(struct sched_domain *sd)
{
	if (cpus_weight(sd->span) == 1)
		return 1;

	/* Following flags need at least 2 groups */
	if (sd->flags & (SD_LOAD_BALANCE |
			 SD_BALANCE_NEWIDLE |
			 SD_BALANCE_FORK |
			 SD_BALANCE_EXEC |
			 SD_SHARE_CPUPOWER |
			 SD_SHARE_PKG_RESOURCES)) {
		if (sd->groups != sd->groups->next)
			return 0;
	}

	/* Following flags don't use groups */
	if (sd->flags & (SD_WAKE_IDLE |
			 SD_WAKE_AFFINE |
			 SD_WAKE_BALANCE))
		return 0;

	return 1;
}

static int
sd_parent_degenerate(struct sched_domain *sd, struct sched_domain *parent)
{
	unsigned long cflags = sd->flags, pflags = parent->flags;

	if (sd_degenerate(parent))
		return 1;

	if (!cpus_equal(sd->span, parent->span))
		return 0;

	/* Does parent contain flags not in child? */
	/* WAKE_BALANCE is a subset of WAKE_AFFINE */
	if (cflags & SD_WAKE_AFFINE)
		pflags &= ~SD_WAKE_BALANCE;
	/* Flags needing groups don't count if only 1 group in parent */
	if (parent->groups == parent->groups->next) {
		pflags &= ~(SD_LOAD_BALANCE |
				SD_BALANCE_NEWIDLE |
				SD_BALANCE_FORK |
				SD_BALANCE_EXEC |
				SD_SHARE_CPUPOWER |
				SD_SHARE_PKG_RESOURCES);
	}
	if (~cflags & pflags)
		return 0;

	return 1;
}

/*
 * Attach the domain 'sd' to 'cpu' as its base domain.  Callers must
 * hold the hotplug lock.
 */
static void cpu_attach_domain(struct sched_domain *sd, int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	struct sched_domain *tmp;

	/* Remove the sched domains which do not contribute to scheduling. */
	for (tmp = sd; tmp; tmp = tmp->parent) {
		struct sched_domain *parent = tmp->parent;
		if (!parent)
			break;
		if (sd_parent_degenerate(tmp, parent)) {
			tmp->parent = parent->parent;
			if (parent->parent)
				parent->parent->child = tmp;
		}
	}

	if (sd && sd_degenerate(sd)) {
		sd = sd->parent;
		if (sd)
			sd->child = NULL;
	}

	sched_domain_debug(sd, cpu);

	rcu_assign_pointer(rq->sd, sd);
}

/* cpus with isolated domains */
static cpumask_t cpu_isolated_map = CPU_MASK_NONE;

/* Setup the mask of cpus configured for isolated domains */
static int __init isolated_cpu_setup(char *str)
{
	int ints[NR_CPUS], i;

	str = get_options(str, ARRAY_SIZE(ints), ints);
	cpus_clear(cpu_isolated_map);
	for (i = 1; i <= ints[0]; i++)
		if (ints[i] < NR_CPUS)
			cpu_set(ints[i], cpu_isolated_map);
	return 1;
}

__setup ("isolcpus=", isolated_cpu_setup);

/*
 * init_sched_build_groups takes the cpumask we wish to span, and a pointer
 * to a function which identifies what group(along with sched group) a CPU
 * belongs to. The return value of group_fn must be a >= 0 and < NR_CPUS
 * (due to the fact that we keep track of groups covered with a cpumask_t).
 *
 * init_sched_build_groups will build a circular linked list of the groups
 * covered by the given span, and will set each group's ->cpumask correctly,
 * and ->cpu_power to 0.
 */
static void
init_sched_build_groups(cpumask_t span, const cpumask_t *cpu_map,
			int (*group_fn)(int cpu, const cpumask_t *cpu_map,
					struct sched_group **sg))
{
	struct sched_group *first = NULL, *last = NULL;
	cpumask_t covered = CPU_MASK_NONE;
	int i;

	for_each_cpu_mask(i, span) {
		struct sched_group *sg;
		int group = group_fn(i, cpu_map, &sg);
		int j;

		if (cpu_isset(i, covered))
			continue;

		sg->cpumask = CPU_MASK_NONE;
		sg->__cpu_power = 0;

		for_each_cpu_mask(j, span) {
			if (group_fn(j, cpu_map, NULL) != group)
				continue;

			cpu_set(j, covered);
			cpu_set(j, sg->cpumask);
		}
		if (!first)
			first = sg;
		if (last)
			last->next = sg;
		last = sg;
	}
	last->next = first;
}

#define SD_NODES_PER_DOMAIN 16

#ifdef CONFIG_NUMA

/**
 * find_next_best_node - find the next node to include in a sched_domain
 * @node: node whose sched_domain we're building
 * @used_nodes: nodes already in the sched_domain
 *
 * Find the next node to include in a given scheduling domain.  Simply
 * finds the closest node not already in the @used_nodes map.
 *
 * Should use nodemask_t.
 */
static int find_next_best_node(int node, unsigned long *used_nodes)
{
	int i, n, val, min_val, best_node = 0;

	min_val = INT_MAX;

	for (i = 0; i < MAX_NUMNODES; i++) {
		/* Start at @node */
		n = (node + i) % MAX_NUMNODES;

		if (!nr_cpus_node(n))
			continue;

		/* Skip already used nodes */
		if (test_bit(n, used_nodes))
			continue;

		/* Simple min distance search */
		val = node_distance(node, n);

		if (val < min_val) {
			min_val = val;
			best_node = n;
		}
	}

	set_bit(best_node, used_nodes);
	return best_node;
}

/**
 * sched_domain_node_span - get a cpumask for a node's sched_domain
 * @node: node whose cpumask we're constructing
 * @size: number of nodes to include in this span
 *
 * Given a node, construct a good cpumask for its sched_domain to span.  It
 * should be one that prevents unnecessary balancing, but also spreads tasks
 * out optimally.
 */
static cpumask_t sched_domain_node_span(int node)
{
	DECLARE_BITMAP(used_nodes, MAX_NUMNODES);
	cpumask_t span, nodemask;
	int i;

	cpus_clear(span);
	bitmap_zero(used_nodes, MAX_NUMNODES);

	nodemask = node_to_cpumask(node);
	cpus_or(span, span, nodemask);
	set_bit(node, used_nodes);

	for (i = 1; i < SD_NODES_PER_DOMAIN; i++) {
		int next_node = find_next_best_node(node, used_nodes);

		nodemask = node_to_cpumask(next_node);
		cpus_or(span, span, nodemask);
	}

	return span;
}
#endif

int sched_smt_power_savings = 0, sched_mc_power_savings = 0;

/*
 * SMT sched-domains:
 */
#ifdef CONFIG_SCHED_SMT
static DEFINE_PER_CPU(struct sched_domain, cpu_domains);
static DEFINE_PER_CPU(struct sched_group, sched_group_cpus);

static int cpu_to_cpu_group(int cpu, const cpumask_t *cpu_map,
			    struct sched_group **sg)
{
	if (sg)
		*sg = &per_cpu(sched_group_cpus, cpu);
	return cpu;
}
#endif

/*
 * multi-core sched-domains:
 */
#ifdef CONFIG_SCHED_MC
static DEFINE_PER_CPU(struct sched_domain, core_domains);
static DEFINE_PER_CPU(struct sched_group, sched_group_core);
#endif

#if defined(CONFIG_SCHED_MC) && defined(CONFIG_SCHED_SMT)
static int cpu_to_core_group(int cpu, const cpumask_t *cpu_map,
			     struct sched_group **sg)
{
	int group;
	cpumask_t mask = cpu_sibling_map[cpu];
	cpus_and(mask, mask, *cpu_map);
	group = first_cpu(mask);
	if (sg)
		*sg = &per_cpu(sched_group_core, group);
	return group;
}
#elif defined(CONFIG_SCHED_MC)
static int cpu_to_core_group(int cpu, const cpumask_t *cpu_map,
			     struct sched_group **sg)
{
	if (sg)
		*sg = &per_cpu(sched_group_core, cpu);
	return cpu;
}
#endif

static DEFINE_PER_CPU(struct sched_domain, phys_domains);
static DEFINE_PER_CPU(struct sched_group, sched_group_phys);

static int cpu_to_phys_group(int cpu, const cpumask_t *cpu_map,
			     struct sched_group **sg)
{
	int group;
#ifdef CONFIG_SCHED_MC
	cpumask_t mask = cpu_coregroup_map(cpu);
	cpus_and(mask, mask, *cpu_map);
	group = first_cpu(mask);
#elif defined(CONFIG_SCHED_SMT)
	cpumask_t mask = cpu_sibling_map[cpu];
	cpus_and(mask, mask, *cpu_map);
	group = first_cpu(mask);
#else
	group = cpu;
#endif
	if (sg)
		*sg = &per_cpu(sched_group_phys, group);
	return group;
}

#ifdef CONFIG_NUMA
/*
 * The init_sched_build_groups can't handle what we want to do with node
 * groups, so roll our own. Now each node has its own list of groups which
 * gets dynamically allocated.
 */
static DEFINE_PER_CPU(struct sched_domain, node_domains);
static struct sched_group **sched_group_nodes_bycpu[NR_CPUS];

static DEFINE_PER_CPU(struct sched_domain, allnodes_domains);
static DEFINE_PER_CPU(struct sched_group, sched_group_allnodes);

static int cpu_to_allnodes_group(int cpu, const cpumask_t *cpu_map,
				 struct sched_group **sg)
{
	cpumask_t nodemask = node_to_cpumask(cpu_to_node(cpu));
	int group;

	cpus_and(nodemask, nodemask, *cpu_map);
	group = first_cpu(nodemask);

	if (sg)
		*sg = &per_cpu(sched_group_allnodes, group);
	return group;
}

static void init_numa_sched_groups_power(struct sched_group *group_head)
{
	struct sched_group *sg = group_head;
	int j;

	if (!sg)
		return;
next_sg:
	for_each_cpu_mask(j, sg->cpumask) {
		struct sched_domain *sd;

		sd = &per_cpu(phys_domains, j);
		if (j != first_cpu(sd->groups->cpumask)) {
			/*
			 * Only add "power" once for each
			 * physical package.
			 */
			continue;
		}

		sg_inc_cpu_power(sg, sd->groups->__cpu_power);
	}
	sg = sg->next;
	if (sg != group_head)
		goto next_sg;
}
#endif

#ifdef CONFIG_NUMA
/* Free memory allocated for various sched_group structures */
static void free_sched_groups(const cpumask_t *cpu_map)
{
	int cpu, i;

	for_each_cpu_mask(cpu, *cpu_map) {
		struct sched_group **sched_group_nodes
			= sched_group_nodes_bycpu[cpu];

		if (!sched_group_nodes)
			continue;

		for (i = 0; i < MAX_NUMNODES; i++) {
			cpumask_t nodemask = node_to_cpumask(i);
			struct sched_group *oldsg, *sg = sched_group_nodes[i];

			cpus_and(nodemask, nodemask, *cpu_map);
			if (cpus_empty(nodemask))
				continue;

			if (sg == NULL)
				continue;
			sg = sg->next;
next_sg:
			oldsg = sg;
			sg = sg->next;
			kfree(oldsg);
			if (oldsg != sched_group_nodes[i])
				goto next_sg;
		}
		kfree(sched_group_nodes);
		sched_group_nodes_bycpu[cpu] = NULL;
	}
}
#else
static void free_sched_groups(const cpumask_t *cpu_map)
{
}
#endif

/*
 * Initialize sched groups cpu_power.
 *
 * cpu_power indicates the capacity of sched group, which is used while
 * distributing the load between different sched groups in a sched domain.
 * Typically cpu_power for all the groups in a sched domain will be same unless
 * there are asymmetries in the topology. If there are asymmetries, group
 * having more cpu_power will pickup more load compared to the group having
 * less cpu_power.
 *
 * cpu_power will be a multiple of SCHED_LOAD_SCALE. This multiple represents
 * the maximum number of tasks a group can handle in the presence of other idle
 * or lightly loaded groups in the same sched domain.
 */
static void init_sched_groups_power(int cpu, struct sched_domain *sd)
{
	struct sched_domain *child;
	struct sched_group *group;

	WARN_ON(!sd || !sd->groups);

	if (cpu != first_cpu(sd->groups->cpumask))
		return;

	child = sd->child;

	sd->groups->__cpu_power = 0;

	/*
	 * For perf policy, if the groups in child domain share resources
	 * (for example cores sharing some portions of the cache hierarchy
	 * or SMT), then set this domain groups cpu_power such that each group
	 * can handle only one task, when there are other idle groups in the
	 * same sched domain.
	 */
	if (!child || (!(sd->flags & SD_POWERSAVINGS_BALANCE) &&
		       (child->flags &
			(SD_SHARE_CPUPOWER | SD_SHARE_PKG_RESOURCES)))) {
		sg_inc_cpu_power(sd->groups, SCHED_LOAD_SCALE);
		return;
	}

	/*
	 * add cpu_power of each child group to this groups cpu_power
	 */
	group = child->groups;
	do {
		sg_inc_cpu_power(sd->groups, group->__cpu_power);
		group = group->next;
	} while (group != child->groups);
}

/*
 * Build sched domains for a given set of cpus and attach the sched domains
 * to the individual cpus
 */
static int build_sched_domains(const cpumask_t *cpu_map)
{
	int i;
#ifdef CONFIG_NUMA
	struct sched_group **sched_group_nodes = NULL;
	int sd_allnodes = 0;

	/*
	 * Allocate the per-node list of sched groups
	 */
	sched_group_nodes = kzalloc(sizeof(struct sched_group *)*MAX_NUMNODES,
					   GFP_KERNEL);
	if (!sched_group_nodes) {
		printk(KERN_WARNING "Can not alloc sched group node list\n");
		return -ENOMEM;
	}
	sched_group_nodes_bycpu[first_cpu(*cpu_map)] = sched_group_nodes;
#endif

	/*
	 * Set up domains for cpus specified by the cpu_map.
	 */
	for_each_cpu_mask(i, *cpu_map) {
		struct sched_domain *sd = NULL, *p;
		cpumask_t nodemask = node_to_cpumask(cpu_to_node(i));

		cpus_and(nodemask, nodemask, *cpu_map);

#ifdef CONFIG_NUMA
		if (cpus_weight(*cpu_map) >
				SD_NODES_PER_DOMAIN*cpus_weight(nodemask)) {
			sd = &per_cpu(allnodes_domains, i);
			*sd = SD_ALLNODES_INIT;
			sd->span = *cpu_map;
			cpu_to_allnodes_group(i, cpu_map, &sd->groups);
			p = sd;
			sd_allnodes = 1;
		} else
			p = NULL;

		sd = &per_cpu(node_domains, i);
		*sd = SD_NODE_INIT;
		sd->span = sched_domain_node_span(cpu_to_node(i));
		sd->parent = p;
		if (p)
			p->child = sd;
		cpus_and(sd->span, sd->span, *cpu_map);
#endif

		p = sd;
		sd = &per_cpu(phys_domains, i);
		*sd = SD_CPU_INIT;
		sd->span = nodemask;
		sd->parent = p;
		if (p)
			p->child = sd;
		cpu_to_phys_group(i, cpu_map, &sd->groups);

#ifdef CONFIG_SCHED_MC
		p = sd;
		sd = &per_cpu(core_domains, i);
		*sd = SD_MC_INIT;
		sd->span = cpu_coregroup_map(i);
		cpus_and(sd->span, sd->span, *cpu_map);
		sd->parent = p;
		p->child = sd;
		cpu_to_core_group(i, cpu_map, &sd->groups);
#endif

#ifdef CONFIG_SCHED_SMT
		p = sd;
		sd = &per_cpu(cpu_domains, i);
		*sd = SD_SIBLING_INIT;
		sd->span = cpu_sibling_map[i];
		cpus_and(sd->span, sd->span, *cpu_map);
		sd->parent = p;
		p->child = sd;
		cpu_to_cpu_group(i, cpu_map, &sd->groups);
#endif
	}

#ifdef CONFIG_SCHED_SMT
	/* Set up CPU (sibling) groups */
	for_each_cpu_mask(i, *cpu_map) {
		cpumask_t this_sibling_map = cpu_sibling_map[i];
		cpus_and(this_sibling_map, this_sibling_map, *cpu_map);
		if (i != first_cpu(this_sibling_map))
			continue;

		init_sched_build_groups(this_sibling_map, cpu_map,
					&cpu_to_cpu_group);
	}
#endif

#ifdef CONFIG_SCHED_MC
	/* Set up multi-core groups */
	for_each_cpu_mask(i, *cpu_map) {
		cpumask_t this_core_map = cpu_coregroup_map(i);
		cpus_and(this_core_map, this_core_map, *cpu_map);
		if (i != first_cpu(this_core_map))
			continue;
		init_sched_build_groups(this_core_map, cpu_map,
					&cpu_to_core_group);
	}
#endif

	/* Set up physical groups */
	for (i = 0; i < MAX_NUMNODES; i++) {
		cpumask_t nodemask = node_to_cpumask(i);

		cpus_and(nodemask, nodemask, *cpu_map);
		if (cpus_empty(nodemask))
			continue;

		init_sched_build_groups(nodemask, cpu_map, &cpu_to_phys_group);
	}

#ifdef CONFIG_NUMA
	/* Set up node groups */
	if (sd_allnodes)
		init_sched_build_groups(*cpu_map, cpu_map,
					&cpu_to_allnodes_group);

	for (i = 0; i < MAX_NUMNODES; i++) {
		/* Set up node groups */
		struct sched_group *sg, *prev;
		cpumask_t nodemask = node_to_cpumask(i);
		cpumask_t domainspan;
		cpumask_t covered = CPU_MASK_NONE;
		int j;

		cpus_and(nodemask, nodemask, *cpu_map);
		if (cpus_empty(nodemask)) {
			sched_group_nodes[i] = NULL;
			continue;
		}

		domainspan = sched_domain_node_span(i);
		cpus_and(domainspan, domainspan, *cpu_map);

		sg = kmalloc_node(sizeof(struct sched_group), GFP_KERNEL, i);
		if (!sg) {
			printk(KERN_WARNING "Can not alloc domain group for "
				"node %d\n", i);
			goto error;
		}
		sched_group_nodes[i] = sg;
		for_each_cpu_mask(j, nodemask) {
			struct sched_domain *sd;
			sd = &per_cpu(node_domains, j);
			sd->groups = sg;
		}
		sg->__cpu_power = 0;
		sg->cpumask = nodemask;
		sg->next = sg;
		cpus_or(covered, covered, nodemask);
		prev = sg;

		for (j = 0; j < MAX_NUMNODES; j++) {
			cpumask_t tmp, notcovered;
			int n = (i + j) % MAX_NUMNODES;

			cpus_complement(notcovered, covered);
			cpus_and(tmp, notcovered, *cpu_map);
			cpus_and(tmp, tmp, domainspan);
			if (cpus_empty(tmp))
				break;

			nodemask = node_to_cpumask(n);
			cpus_and(tmp, tmp, nodemask);
			if (cpus_empty(tmp))
				continue;

			sg = kmalloc_node(sizeof(struct sched_group),
					  GFP_KERNEL, i);
			if (!sg) {
				printk(KERN_WARNING
				"Can not alloc domain group for node %d\n", j);
				goto error;
			}
			sg->__cpu_power = 0;
			sg->cpumask = tmp;
			sg->next = prev->next;
			cpus_or(covered, covered, tmp);
			prev->next = sg;
			prev = sg;
		}
	}
#endif

	/* Calculate CPU power for physical packages and nodes */
#ifdef CONFIG_SCHED_SMT
	for_each_cpu_mask(i, *cpu_map) {
		struct sched_domain *sd = &per_cpu(cpu_domains, i);

		init_sched_groups_power(i, sd);
	}
#endif
#ifdef CONFIG_SCHED_MC
	for_each_cpu_mask(i, *cpu_map) {
		struct sched_domain *sd = &per_cpu(core_domains, i);

		init_sched_groups_power(i, sd);
	}
#endif

	for_each_cpu_mask(i, *cpu_map) {
		struct sched_domain *sd = &per_cpu(phys_domains, i);

		init_sched_groups_power(i, sd);
	}

#ifdef CONFIG_NUMA
	for (i = 0; i < MAX_NUMNODES; i++)
		init_numa_sched_groups_power(sched_group_nodes[i]);

	if (sd_allnodes) {
		struct sched_group *sg;

		cpu_to_allnodes_group(first_cpu(*cpu_map), cpu_map, &sg);
		init_numa_sched_groups_power(sg);
	}
#endif

	/* Attach the domains */
	for_each_cpu_mask(i, *cpu_map) {
		struct sched_domain *sd;
#ifdef CONFIG_SCHED_SMT
		sd = &per_cpu(cpu_domains, i);
#elif defined(CONFIG_SCHED_MC)
		sd = &per_cpu(core_domains, i);
#else
		sd = &per_cpu(phys_domains, i);
#endif
		cpu_attach_domain(sd, i);
	}

	return 0;

#ifdef CONFIG_NUMA
error:
	free_sched_groups(cpu_map);
	return -ENOMEM;
#endif
}
/*
 * Set up scheduler domains and groups.  Callers must hold the hotplug lock.
 */
static int arch_init_sched_domains(const cpumask_t *cpu_map)
{
	cpumask_t cpu_default_map;
	int err;

	/*
	 * Setup mask for cpus without special case scheduling requirements.
	 * For now this just excludes isolated cpus, but could be used to
	 * exclude other special cases in the future.
	 */
	cpus_andnot(cpu_default_map, *cpu_map, cpu_isolated_map);

	err = build_sched_domains(&cpu_default_map);

	return err;
}

static void arch_destroy_sched_domains(const cpumask_t *cpu_map)
{
	free_sched_groups(cpu_map);
}

/*
 * Detach sched domains from a group of cpus specified in cpu_map
 * These cpus will now be attached to the NULL domain
 */
static void detach_destroy_domains(const cpumask_t *cpu_map)
{
	int i;

	for_each_cpu_mask(i, *cpu_map)
		cpu_attach_domain(NULL, i);
	synchronize_sched();
	arch_destroy_sched_domains(cpu_map);
}

/*
 * Partition sched domains as specified by the cpumasks below.
 * This attaches all cpus from the cpumasks to the NULL domain,
 * waits for a RCU quiescent period, recalculates sched
 * domain information and then attaches them back to the
 * correct sched domains
 * Call with hotplug lock held
 */
int partition_sched_domains(cpumask_t *partition1, cpumask_t *partition2)
{
	cpumask_t change_map;
	int err = 0;

	cpus_and(*partition1, *partition1, cpu_online_map);
	cpus_and(*partition2, *partition2, cpu_online_map);
	cpus_or(change_map, *partition1, *partition2);

	/* Detach sched domains from all of the affected cpus */
	detach_destroy_domains(&change_map);
	if (!cpus_empty(*partition1))
		err = build_sched_domains(partition1);
	if (!err && !cpus_empty(*partition2))
		err = build_sched_domains(partition2);

	return err;
}

#if defined(CONFIG_SCHED_MC) || defined(CONFIG_SCHED_SMT)
int arch_reinit_sched_domains(void)
{
	int err;

	mutex_lock(&sched_hotcpu_mutex);
	detach_destroy_domains(&cpu_online_map);
	err = arch_init_sched_domains(&cpu_online_map);
	mutex_unlock(&sched_hotcpu_mutex);

	return err;
}

static ssize_t sched_power_savings_store(const char *buf, size_t count, int smt)
{
	int ret;

	if (buf[0] != '0' && buf[0] != '1')
		return -EINVAL;

	if (smt)
		sched_smt_power_savings = (buf[0] == '1');
	else
		sched_mc_power_savings = (buf[0] == '1');

	ret = arch_reinit_sched_domains();

	return ret ? ret : count;
}

int sched_create_sysfs_power_savings_entries(struct sysdev_class *cls)
{
	int err = 0;

#ifdef CONFIG_SCHED_SMT
	if (smt_capable())
		err = sysfs_create_file(&cls->kset.kobj,
					&attr_sched_smt_power_savings.attr);
#endif
#ifdef CONFIG_SCHED_MC
	if (!err && mc_capable())
		err = sysfs_create_file(&cls->kset.kobj,
					&attr_sched_mc_power_savings.attr);
#endif
	return err;
}
#endif

#ifdef CONFIG_SCHED_MC
static ssize_t sched_mc_power_savings_show(struct sys_device *dev, char *page)
{
	return sprintf(page, "%u\n", sched_mc_power_savings);
}
static ssize_t sched_mc_power_savings_store(struct sys_device *dev,
					    const char *buf, size_t count)
{
	return sched_power_savings_store(buf, count, 0);
}
SYSDEV_ATTR(sched_mc_power_savings, 0644, sched_mc_power_savings_show,
	    sched_mc_power_savings_store);
#endif

#ifdef CONFIG_SCHED_SMT
static ssize_t sched_smt_power_savings_show(struct sys_device *dev, char *page)
{
	return sprintf(page, "%u\n", sched_smt_power_savings);
}
static ssize_t sched_smt_power_savings_store(struct sys_device *dev,
					     const char *buf, size_t count)
{
	return sched_power_savings_store(buf, count, 1);
}
SYSDEV_ATTR(sched_smt_power_savings, 0644, sched_smt_power_savings_show,
	    sched_smt_power_savings_store);
#endif

/*
 * Force a reinitialization of the sched domains hierarchy.  The domains
 * and groups cannot be updated in place without racing with the balancing
 * code, so we temporarily attach all running cpus to the NULL domain
 * which will prevent rebalancing while the sched domains are recalculated.
 */
static int update_sched_domains(struct notifier_block *nfb,
				unsigned long action, void *hcpu)
{
	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		detach_destroy_domains(&cpu_online_map);
		return NOTIFY_OK;

	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		/*
		 * Fall through and re-initialise the domains.
		 */
		break;
	default:
		return NOTIFY_DONE;
	}

	/* The hotplug lock is already held by cpu_up/cpu_down */
	arch_init_sched_domains(&cpu_online_map);

	return NOTIFY_OK;
}

void __init sched_init_smp(void)
{
	cpumask_t non_isolated_cpus;

	mutex_lock(&sched_hotcpu_mutex);
	arch_init_sched_domains(&cpu_online_map);
	cpus_andnot(non_isolated_cpus, cpu_possible_map, cpu_isolated_map);
	if (cpus_empty(non_isolated_cpus))
		cpu_set(smp_processor_id(), non_isolated_cpus);
	mutex_unlock(&sched_hotcpu_mutex);
	/* XXX: Theoretical race here - CPU may be hotplugged now */
	hotcpu_notifier(update_sched_domains, 0);

	/* Move init over to a non-isolated CPU */
	if (set_cpus_allowed(current, non_isolated_cpus) < 0)
		BUG();
	sched_init_granularity();
}
#else
void __init sched_init_smp(void)
{
	sched_init_granularity();
}
#endif /* CONFIG_SMP */

int in_sched_functions(unsigned long addr)
{
	/* Linker adds these: start and end of __sched functions */
	extern char __sched_text_start[], __sched_text_end[];

	return in_lock_functions(addr) ||
		(addr >= (unsigned long)__sched_text_start
		&& addr < (unsigned long)__sched_text_end);
}

static inline void init_cfs_rq(struct cfs_rq *cfs_rq, struct rq *rq)
{
	cfs_rq->tasks_timeline = RB_ROOT;
	cfs_rq->fair_clock = 1;
#ifdef CONFIG_FAIR_GROUP_SCHED
	cfs_rq->rq = rq;
#endif
}

void __init sched_init(void)
{
	u64 now = sched_clock();
	int highest_cpu = 0;
	int i, j;

	/*
	 * Link up the scheduling class hierarchy:
	 */
	rt_sched_class.next = &fair_sched_class;
	fair_sched_class.next = &idle_sched_class;
	idle_sched_class.next = NULL;

	for_each_possible_cpu(i) {
		struct rt_prio_array *array;
		struct rq *rq;

		rq = cpu_rq(i);
		spin_lock_init(&rq->lock);
		lockdep_set_class(&rq->lock, &rq->rq_lock_key);
		rq->nr_running = 0;
		rq->clock = 1;
		init_cfs_rq(&rq->cfs, rq);
#ifdef CONFIG_FAIR_GROUP_SCHED
		INIT_LIST_HEAD(&rq->leaf_cfs_rq_list);
		list_add(&rq->cfs.leaf_cfs_rq_list, &rq->leaf_cfs_rq_list);
#endif
		rq->ls.load_update_last = now;
		rq->ls.load_update_start = now;

		for (j = 0; j < CPU_LOAD_IDX_MAX; j++)
			rq->cpu_load[j] = 0;
#ifdef CONFIG_SMP
		rq->sd = NULL;
		rq->active_balance = 0;
		rq->next_balance = jiffies;
		rq->push_cpu = 0;
		rq->cpu = i;
		rq->migration_thread = NULL;
		INIT_LIST_HEAD(&rq->migration_queue);
#endif
		atomic_set(&rq->nr_iowait, 0);

		array = &rq->rt.active;
		for (j = 0; j < MAX_RT_PRIO; j++) {
			INIT_LIST_HEAD(array->queue + j);
			__clear_bit(j, array->bitmap);
		}
		highest_cpu = i;
		/* delimiter for bitsearch: */
		__set_bit(MAX_RT_PRIO, array->bitmap);
	}

	set_load_weight(&init_task);

#ifdef CONFIG_SMP
	nr_cpu_ids = highest_cpu + 1;
	open_softirq(SCHED_SOFTIRQ, run_rebalance_domains, NULL);
#endif

#ifdef CONFIG_RT_MUTEXES
	plist_head_init(&init_task.pi_waiters, &init_task.pi_lock);
#endif

	/*
	 * The boot idle thread does lazy MMU switching as well:
	 */
	atomic_inc(&init_mm.mm_count);
	enter_lazy_tlb(&init_mm, current);

	/*
	 * Make us the idle thread. Technically, schedule() should not be
	 * called from this thread, however somewhere below it might be,
	 * but because we are the idle thread, we just pick up running again
	 * when this runqueue becomes "idle".
	 */
	init_idle(current, smp_processor_id());
	/*
	 * During early bootup we pretend to be a normal task:
	 */
	current->sched_class = &fair_sched_class;
}

#ifdef CONFIG_DEBUG_SPINLOCK_SLEEP
void __might_sleep(char *file, int line)
{
#ifdef in_atomic
	static unsigned long prev_jiffy;	/* ratelimiting */

	if ((in_atomic() || irqs_disabled()) &&
	    system_state == SYSTEM_RUNNING && !oops_in_progress) {
		if (time_before(jiffies, prev_jiffy + HZ) && prev_jiffy)
			return;
		prev_jiffy = jiffies;
		printk(KERN_ERR "BUG: sleeping function called from invalid"
				" context at %s:%d\n", file, line);
		printk("in_atomic():%d, irqs_disabled():%d\n",
			in_atomic(), irqs_disabled());
		debug_show_held_locks(current);
		if (irqs_disabled())
			print_irqtrace_events(current);
		dump_stack();
	}
#endif
}
EXPORT_SYMBOL(__might_sleep);
#endif

#ifdef CONFIG_MAGIC_SYSRQ
void normalize_rt_tasks(void)
{
	struct task_struct *g, *p;
	unsigned long flags;
	struct rq *rq;
	int on_rq;

	read_lock_irq(&tasklist_lock);
	do_each_thread(g, p) {
		p->se.fair_key			= 0;
		p->se.wait_runtime		= 0;
		p->se.wait_start_fair		= 0;
		p->se.wait_start		= 0;
		p->se.exec_start		= 0;
		p->se.sleep_start		= 0;
		p->se.sleep_start_fair		= 0;
		p->se.block_start		= 0;
		task_rq(p)->cfs.fair_clock	= 0;
		task_rq(p)->clock		= 0;

		if (!rt_task(p)) {
			/*
			 * Renice negative nice level userspace
			 * tasks back to 0:
			 */
			if (TASK_NICE(p) < 0 && p->mm)
				set_user_nice(p, 0);
			continue;
		}

		spin_lock_irqsave(&p->pi_lock, flags);
		rq = __task_rq_lock(p);
#ifdef CONFIG_SMP
		/*
		 * Do not touch the migration thread:
		 */
		if (p == rq->migration_thread)
			goto out_unlock;
#endif

		on_rq = p->se.on_rq;
		if (on_rq)
			deactivate_task(task_rq(p), p, 0);
		__setscheduler(rq, p, SCHED_NORMAL, 0);
		if (on_rq) {
			activate_task(task_rq(p), p, 0);
			resched_task(rq->curr);
		}
#ifdef CONFIG_SMP
 out_unlock:
#endif
		__task_rq_unlock(rq);
		spin_unlock_irqrestore(&p->pi_lock, flags);
	} while_each_thread(g, p);

	read_unlock_irq(&tasklist_lock);
}

#endif /* CONFIG_MAGIC_SYSRQ */

#ifdef CONFIG_IA64
/*
 * These functions are only useful for the IA64 MCA handling.
 *
 * They can only be called when the whole system has been
 * stopped - every CPU needs to be quiescent, and no scheduling
 * activity can take place. Using them for anything else would
 * be a serious bug, and as a result, they aren't even visible
 * under any other configuration.
 */

/**
 * curr_task - return the current task for a given cpu.
 * @cpu: the processor in question.
 *
 * ONLY VALID WHEN THE WHOLE SYSTEM IS STOPPED!
 */
struct task_struct *curr_task(int cpu)
{
	return cpu_curr(cpu);
}

/**
 * set_curr_task - set the current task for a given cpu.
 * @cpu: the processor in question.
 * @p: the task pointer to set.
 *
 * Description: This function must only be used when non-maskable interrupts
 * are serviced on a separate stack.  It allows the architecture to switch the
 * notion of the current task on a cpu in a non-blocking manner.  This function
 * must be called with all CPU's synchronized, and interrupts disabled, the
 * and caller must save the original value of the current task (see
 * curr_task() above) and restore that value before reenabling interrupts and
 * re-starting the system.
 *
 * ONLY VALID WHEN THE WHOLE SYSTEM IS STOPPED!
 */
void set_curr_task(int cpu, struct task_struct *p)
{
	cpu_curr(cpu) = p;
}

#endif
