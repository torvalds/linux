/*
 *  kernel/sched/MuQSS.c, was kernel/sched.c
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
 *  2007-04-15  Work begun on replacing all interactivity tuning with a
 *              fair scheduling design by Con Kolivas.
 *  2007-05-05  Load balancing (smp-nice) and other improvements
 *              by Peter Williams
 *  2007-05-06  Interactivity improvements to CFS by Mike Galbraith
 *  2007-07-01  Group scheduling enhancements by Srivatsa Vaddagiri
 *  2007-11-29  RT balancing improvements by Steven Rostedt, Gregory Haskins,
 *              Thomas Gleixner, Mike Kravetz
 *  2009-08-13	Brainfuck deadline scheduling policy by Con Kolivas deletes
 *              a whole lot of those previous things.
 *  2016-10-01  Multiple Queue Skiplist Scheduler scalable evolution of BFS
 * 		scheduler by Con Kolivas.
 */

#include <linux/kasan.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/highmem.h>
#include <linux/mmu_context.h>
#include <linux/interrupt.h>
#include <linux/capability.h>
#include <linux/completion.h>
#include <linux/kernel_stat.h>
#include <linux/debug_locks.h>
#include <linux/perf_event.h>
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
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/sched/sysctl.h>
#include <linux/times.h>
#include <linux/tsacct_kern.h>
#include <linux/kprobes.h>
#include <linux/delayacct.h>
#include <linux/log2.h>
#include <linux/bootmem.h>
#include <linux/ftrace.h>
#include <linux/slab.h>
#include <linux/init_task.h>
#include <linux/binfmts.h>
#include <linux/context_tracking.h>
#include <linux/sched/prio.h>
#include <linux/tick.h>
#include <linux/skip_list.h>

#include <asm/irq_regs.h>
#include <asm/switch_to.h>
#include <asm/tlb.h>
#include <asm/unistd.h>
#include <asm/mutex.h>
#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#endif

#include "cpupri.h"
#include "../workqueue_internal.h"
#include "../smpboot.h"

#define CREATE_TRACE_POINTS
#include <trace/events/sched.h>

#include "MuQSS.h"

#define rt_prio(prio)		unlikely((prio) < MAX_RT_PRIO)
#define rt_task(p)		rt_prio((p)->prio)
#define batch_task(p)		(unlikely((p)->policy == SCHED_BATCH))
#define is_rt_policy(policy)	((policy) == SCHED_FIFO || \
					(policy) == SCHED_RR)
#define has_rt_policy(p)	unlikely(is_rt_policy((p)->policy))

#define is_idle_policy(policy)	((policy) == SCHED_IDLEPRIO)
#define idleprio_task(p)	unlikely(is_idle_policy((p)->policy))
#define task_running_idle(p)	unlikely((p)->prio == IDLE_PRIO)

#define is_iso_policy(policy)	((policy) == SCHED_ISO)
#define iso_task(p)		unlikely(is_iso_policy((p)->policy))
#define task_running_iso(p)	unlikely((p)->prio == ISO_PRIO)

#define rq_idle(rq)		((rq)->rq_prio == PRIO_LIMIT)

#define ISO_PERIOD		(5 * HZ)

#define STOP_PRIO		(MAX_RT_PRIO - 1)

/*
 * Some helpers for converting to/from various scales. Use shifts to get
 * approximate multiples of ten for less overhead.
 */
#define JIFFIES_TO_NS(TIME)	((TIME) * (1000000000 / HZ))
#define JIFFY_NS		(1000000000 / HZ)
#define HALF_JIFFY_NS		(1000000000 / HZ / 2)
#define HALF_JIFFY_US		(1000000 / HZ / 2)
#define MS_TO_NS(TIME)		((TIME) << 20)
#define MS_TO_US(TIME)		((TIME) << 10)
#define NS_TO_MS(TIME)		((TIME) >> 20)
#define NS_TO_US(TIME)		((TIME) >> 10)

#define RESCHED_US	(100) /* Reschedule if less than this many μs left */

void print_scheduler_version(void)
{
	printk(KERN_INFO "MuQSS CPU scheduler v0.112 by Con Kolivas.\n");
}

/*
 * This is the time all tasks within the same priority round robin.
 * Value is in ms and set to a minimum of 6ms. Scales with number of cpus.
 * Tunable via /proc interface.
 */
int rr_interval __read_mostly = 6;

/* Tunable to choose whether to prioritise latency or throughput, simple
 * binary yes or no */

int sched_interactive __read_mostly = 1;

/*
 * sched_iso_cpu - sysctl which determines the cpu percentage SCHED_ISO tasks
 * are allowed to run five seconds as real time tasks. This is the total over
 * all online cpus.
 */
int sched_iso_cpu __read_mostly = 70;

/*
 * The relative length of deadline for each priority(nice) level.
 */
static int prio_ratios[NICE_WIDTH] __read_mostly;

/*
 * The quota handed out to tasks of all priority levels when refilling their
 * time_slice.
 */
static inline int timeslice(void)
{
	return MS_TO_US(rr_interval);
}

/*
 * The global runqueue data that all CPUs work off. Contains either atomic
 * variables and a cpu bitmap set atomically.
 */
struct global_rq {
	atomic_t nr_running;
	atomic_t nr_uninterruptible;
	atomic64_t nr_switches;
	atomic_t qnr; /* queued not running */
#ifdef CONFIG_SMP
	cpumask_t cpu_idle_map;
#endif
};

#ifdef CONFIG_SMP
/*
 * We add the notion of a root-domain which will be used to define per-domain
 * variables. Each exclusive cpuset essentially defines an island domain by
 * fully partitioning the member cpus from any other cpuset. Whenever a new
 * exclusive cpuset is created, we also create and attach a new root-domain
 * object.
 *
 */
struct root_domain {
	atomic_t refcount;
	atomic_t rto_count;
	struct rcu_head rcu;
	cpumask_var_t span;
	cpumask_var_t online;

	/*
	 * The "RT overload" flag: it gets set if a CPU has more than
	 * one runnable RT task.
	 */
	cpumask_var_t rto_mask;
	struct cpupri cpupri;
};

/*
 * By default the system creates a single root-domain with all cpus as
 * members (mimicking the global state we have today).
 */
static struct root_domain def_root_domain;

#endif /* CONFIG_SMP */

/* There can be only one */
static struct global_rq grq;

static DEFINE_MUTEX(sched_hotcpu_mutex);

/* cpus with isolated domains */
cpumask_var_t cpu_isolated_map;

DEFINE_PER_CPU_SHARED_ALIGNED(struct rq, runqueues);
#ifdef CONFIG_SMP
struct rq *cpu_rq(int cpu)
{
	return &per_cpu(runqueues, (cpu));
}
#define task_rq(p)		cpu_rq(task_cpu(p))
#define cpu_curr(cpu)		(cpu_rq(cpu)->curr)
/*
 * sched_domains_mutex serialises calls to init_sched_domains,
 * detach_destroy_domains and partition_sched_domains.
 */
DEFINE_MUTEX(sched_domains_mutex);

/*
 * By default the system creates a single root-domain with all cpus as
 * members (mimicking the global state we have today).
 */
static struct root_domain def_root_domain;

int __weak arch_sd_sibling_asym_packing(void)
{
       return 0*SD_ASYM_PACKING;
}
#else
struct rq *uprq;
#endif /* CONFIG_SMP */

/*
 * Sanity check should sched_clock return bogus values. We make sure it does
 * not appear to go backwards, and use jiffies to determine the maximum and
 * minimum it could possibly have increased, and round down to the nearest
 * jiffy when it falls outside this.
 */
static inline void niffy_diff(s64 *niff_diff, int jiff_diff)
{
	unsigned long min_diff, max_diff;

	if (jiff_diff > 1)
		min_diff = JIFFIES_TO_NS(jiff_diff - 1);
	else
		min_diff = 1;
	/*  Round up to the nearest tick for maximum */
	max_diff = JIFFIES_TO_NS(jiff_diff + 1);

	if (unlikely(*niff_diff < min_diff || *niff_diff > max_diff))
		*niff_diff = min_diff;
}

#ifdef CONFIG_SMP
static inline int cpu_of(struct rq *rq)
{
	return rq->cpu;
}
#else /* CONFIG_SMP */
static inline int cpu_of(struct rq *rq)
{
	return 0;
}
#endif

#include "stats.h"

#ifndef prepare_arch_switch
# define prepare_arch_switch(next)	do { } while (0)
#endif
#ifndef finish_arch_switch
# define finish_arch_switch(prev)	do { } while (0)
#endif
#ifndef finish_arch_post_lock_switch
# define finish_arch_post_lock_switch()	do { } while (0)
#endif

/*
 * All common locking functions performed on rq->lock. rq->clock is local to
 * the CPU accessing it so it can be modified just with interrupts disabled
 * when we're not updating niffies.
 * Looking up task_rq must be done under rq->lock to be safe.
 */
static void update_rq_clock_task(struct rq *rq, s64 delta);

static inline void update_rq_clock(struct rq *rq)
{
	s64 delta = sched_clock_cpu(cpu_of(rq)) - rq->clock;

	if (unlikely(delta < 0))
		return;
	rq->clock += delta;
	update_rq_clock_task(rq, delta);
}

/*
 * Niffies are a globally increasing nanosecond counter. They're only used by
 * update_load_avg and time_slice_expired, however deadlines are based on them
 * across CPUs. Update them whenever we will call one of those functions, and
 * synchronise them across CPUs whenever we hold both runqueue locks.
 */
static inline void update_clocks(struct rq *rq)
{
	s64 ndiff;
	long jdiff;

	update_rq_clock(rq);
	ndiff = rq->clock - rq->old_clock;
	if (unlikely(!ndiff))
		return;
	rq->old_clock = rq->clock;
	ndiff -= rq->niffies - rq->last_niffy;
	jdiff = jiffies - rq->last_jiffy;
	niffy_diff(&ndiff, jdiff);
	rq->last_jiffy += jdiff;
	rq->niffies += ndiff;
	rq->last_niffy = rq->niffies;
}

static inline int task_current(struct rq *rq, struct task_struct *p)
{
	return rq->curr == p;
}

static inline int task_running(struct rq *rq, struct task_struct *p)
{
#ifdef CONFIG_SMP
	return p->on_cpu;
#else
	return task_current(rq, p);
#endif
}

static inline int task_on_rq_queued(struct task_struct *p)
{
	return p->on_rq == TASK_ON_RQ_QUEUED;
}

static inline int task_on_rq_migrating(struct task_struct *p)
{
	return p->on_rq == TASK_ON_RQ_MIGRATING;
}

static inline void rq_lock(struct rq *rq)
	__acquires(rq->lock)
{
	raw_spin_lock(&rq->lock);
}

static inline int rq_trylock(struct rq *rq)
	__acquires(rq->lock)
{
	return raw_spin_trylock(&rq->lock);
}

static inline void rq_unlock(struct rq *rq)
	__releases(rq->lock)
{
	raw_spin_unlock(&rq->lock);
}

static inline struct rq *this_rq_lock(void)
	__acquires(rq->lock)
{
	struct rq *rq;

	local_irq_disable();
	rq = this_rq();
	raw_spin_lock(&rq->lock);

	return rq;
}

/*
 * Any time we have two runqueues locked we use that as an opportunity to
 * synchronise niffies to the highest value as idle ticks may have artificially
 * kept niffies low on one CPU and the truth can only be later.
 */
static inline void synchronise_niffies(struct rq *rq1, struct rq *rq2)
{
	if (rq1->niffies > rq2->niffies)
		rq2->niffies = rq1->niffies;
	else
		rq1->niffies = rq2->niffies;
}

/*
 * double_rq_lock - safely lock two runqueues
 *
 * Note this does not disable interrupts like task_rq_lock,
 * you need to do so manually before calling.
 */

/* For when we know rq1 != rq2 */
static inline void __double_rq_lock(struct rq *rq1, struct rq *rq2)
	__acquires(rq1->lock)
	__acquires(rq2->lock)
{
	if (rq1 < rq2) {
		raw_spin_lock(&rq1->lock);
		raw_spin_lock_nested(&rq2->lock, SINGLE_DEPTH_NESTING);
	} else {
		raw_spin_lock(&rq2->lock);
		raw_spin_lock_nested(&rq1->lock, SINGLE_DEPTH_NESTING);
	}
}

static inline void double_rq_lock(struct rq *rq1, struct rq *rq2)
	__acquires(rq1->lock)
	__acquires(rq2->lock)
{
	BUG_ON(!irqs_disabled());
	if (rq1 == rq2) {
		raw_spin_lock(&rq1->lock);
		__acquire(rq2->lock);	/* Fake it out ;) */
	} else
		__double_rq_lock(rq1, rq2);
	synchronise_niffies(rq1, rq2);
}

/*
 * double_rq_unlock - safely unlock two runqueues
 *
 * Note this does not restore interrupts like task_rq_unlock,
 * you need to do so manually after calling.
 */
static inline void double_rq_unlock(struct rq *rq1, struct rq *rq2)
	__releases(rq1->lock)
	__releases(rq2->lock)
{
	raw_spin_unlock(&rq1->lock);
	if (rq1 != rq2)
		raw_spin_unlock(&rq2->lock);
	else
		__release(rq2->lock);
}

/* Must be sure rq1 != rq2 and irqs are disabled */
static inline void lock_second_rq(struct rq *rq1, struct rq *rq2)
	__releases(rq1->lock)
	__acquires(rq1->lock)
	__acquires(rq2->lock)
{
	BUG_ON(!irqs_disabled());
	if (unlikely(!raw_spin_trylock(&rq2->lock))) {
		raw_spin_unlock(&rq1->lock);
		__double_rq_lock(rq1, rq2);
	}
	synchronise_niffies(rq1, rq2);
}

static inline void lock_all_rqs(void)
{
	int cpu;

	preempt_disable();
	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);

		do_raw_spin_lock(&rq->lock);
	}
}

static inline void unlock_all_rqs(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);

		do_raw_spin_unlock(&rq->lock);
	}
	preempt_enable();
}

/* Specially nest trylock an rq */
static inline bool trylock_rq(struct rq *this_rq, struct rq *rq)
{
	if (unlikely(!do_raw_spin_trylock(&rq->lock)))
		return false;
	spin_acquire(&rq->lock.dep_map, SINGLE_DEPTH_NESTING, 1, _RET_IP_);
	synchronise_niffies(this_rq, rq);
	return true;
}

/* Unlock a specially nested trylocked rq */
static inline void unlock_rq(struct rq *rq)
{
	spin_release(&rq->lock.dep_map, 1, _RET_IP_);
	do_raw_spin_unlock(&rq->lock);
}

static inline void rq_lock_irq(struct rq *rq)
	__acquires(rq->lock)
{
	raw_spin_lock_irq(&rq->lock);
}

static inline void rq_unlock_irq(struct rq *rq)
	__releases(rq->lock)
{
	raw_spin_unlock_irq(&rq->lock);
}

static inline void rq_lock_irqsave(struct rq *rq, unsigned long *flags)
	__acquires(rq->lock)
{
	raw_spin_lock_irqsave(&rq->lock, *flags);
}

static inline void rq_unlock_irqrestore(struct rq *rq, unsigned long *flags)
	__releases(rq->lock)
{
	raw_spin_unlock_irqrestore(&rq->lock, *flags);
}

static inline struct rq
*task_rq_lock(struct task_struct *p, unsigned long *flags)
	__acquires(p->pi_lock)
	__acquires(rq->lock)
{
	struct rq *rq;

	while (42) {
		raw_spin_lock_irqsave(&p->pi_lock, *flags);
		rq = task_rq(p);
		raw_spin_lock(&rq->lock);
		if (likely(rq == task_rq(p)))
			break;
		raw_spin_unlock(&rq->lock);
		raw_spin_unlock_irqrestore(&p->pi_lock, *flags);
	}
	return rq;
}

static inline void task_rq_unlock(struct rq *rq, struct task_struct *p, unsigned long *flags)
	__releases(rq->lock)
	__releases(p->pi_lock)
{
	rq_unlock(rq);
	raw_spin_unlock_irqrestore(&p->pi_lock, *flags);
}

static inline struct rq *__task_rq_lock(struct task_struct *p)
	__acquires(rq->lock)
{
	struct rq *rq;

	lockdep_assert_held(&p->pi_lock);

	while (42) {
		rq = task_rq(p);
		raw_spin_lock(&rq->lock);
		if (likely(rq == task_rq(p)))
			break;
		raw_spin_unlock(&rq->lock);
	}
	return rq;
}

static inline void __task_rq_unlock(struct rq *rq)
{
	rq_unlock(rq);
}

static inline void prepare_lock_switch(struct rq *rq, struct task_struct *next)
{
	next->on_cpu = 1;
}

/*
 * resched_task - mark a task 'to be rescheduled now'.
 *
 * On UP this means the setting of the need_resched flag, on SMP it
 * might also involve a cross-CPU call to trigger the scheduler on
 * the target CPU.
 */
void resched_task(struct task_struct *p)
{
	int cpu;
#ifdef CONFIG_LOCKDEP
	struct rq *rq = task_rq(p);

	lockdep_assert_held(&rq->lock);
#endif
	if (test_tsk_need_resched(p))
		return;

	set_tsk_need_resched(p);

	cpu = task_cpu(p);
	if (cpu == smp_processor_id()) {
		set_preempt_need_resched();
		return;
	}

	smp_send_reschedule(cpu);
}

/*
 * A task that is not running or queued will not have a node set.
 * A task that is queued but not running will have a node set.
 * A task that is currently running will have ->on_cpu set but no node set.
 */
static inline bool task_queued(struct task_struct *p)
{
	return !skiplist_node_empty(&p->node);
}

static void enqueue_task(struct rq *rq, struct task_struct *p, int flags);
static inline void resched_if_idle(struct rq *rq);

static inline void finish_lock_switch(struct rq *rq, struct task_struct *prev)
{
#ifdef CONFIG_SMP
	/*
	 * After ->on_cpu is cleared, the task can be moved to a different CPU.
	 * We must ensure this doesn't happen until the switch is completely
	 * finished.
	 *
	 * In particular, the load of prev->state in finish_task_switch() must
	 * happen before this.
	 *
	 * Pairs with the smp_cond_load_acquire() in try_to_wake_up().
	 */
	smp_store_release(&prev->on_cpu, 0);
#endif
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

#ifdef CONFIG_SMP
	/*
	 * If prev was marked as migrating to another CPU in return_task, drop
	 * the local runqueue lock but leave interrupts disabled and grab the
	 * remote lock we're migrating it to before enabling them.
	 */
	if (unlikely(task_on_rq_migrating(prev))) {
		sched_info_dequeued(rq, prev);
		/*
		 * We move the ownership of prev to the new cpu now. ttwu can't
		 * activate prev to the wrong cpu since it has to grab this
		 * runqueue in ttwu_remote.
		 */
		task_thread_info(prev)->cpu = prev->wake_cpu;
		raw_spin_unlock(&rq->lock);

		raw_spin_lock(&prev->pi_lock);
		rq = __task_rq_lock(prev);
		/* Check that someone else hasn't already queued prev */
		if (likely(!task_queued(prev))) {
			enqueue_task(rq, prev, 0);
			prev->on_rq = TASK_ON_RQ_QUEUED;
			/* Wake up the CPU if it's not already running */
			resched_if_idle(rq);
		}
		raw_spin_unlock(&prev->pi_lock);
	}
#endif
	raw_spin_unlock_irq(&rq->lock);
}

static inline bool deadline_before(u64 deadline, u64 time)
{
	return (deadline < time);
}

static inline bool deadline_after(u64 deadline, u64 time)
{
	return (deadline > time);
}

/*
 * Deadline is "now" in niffies + (offset by priority). Setting the deadline
 * is the key to everything. It distributes cpu fairly amongst tasks of the
 * same nice value, it proportions cpu according to nice level, it means the
 * task that last woke up the longest ago has the earliest deadline, thus
 * ensuring that interactive tasks get low latency on wake up. The CPU
 * proportion works out to the square of the virtual deadline difference, so
 * this equation will give nice 19 3% CPU compared to nice 0.
 */
static inline u64 prio_deadline_diff(int user_prio)
{
	return (prio_ratios[user_prio] * rr_interval * (MS_TO_NS(1) / 128));
}

static inline u64 task_deadline_diff(struct task_struct *p)
{
	return prio_deadline_diff(TASK_USER_PRIO(p));
}

static inline u64 static_deadline_diff(int static_prio)
{
	return prio_deadline_diff(USER_PRIO(static_prio));
}

static inline int longest_deadline_diff(void)
{
	return prio_deadline_diff(39);
}

static inline int ms_longest_deadline_diff(void)
{
	return NS_TO_MS(longest_deadline_diff());
}

static inline int rq_load(struct rq *rq)
{
	return rq->sl->entries + !rq_idle(rq);
}

static inline bool rq_local(struct rq *rq);

/*
 * Update the load average for feeding into cpu frequency governors. Use a
 * rough estimate of a rolling average with ~ time constant of 32ms.
 * 80/128 ~ 0.63. * 80 / 32768 / 128 == * 5 / 262144
 * Make sure a call to update_clocks has been made before calling this to get
 * an updated rq->niffies.
 */
static void update_load_avg(struct rq *rq)
{
	/* rq clock can go backwards so skip update if that happens */
	if (likely(rq->clock > rq->load_update)) {
		unsigned long us_interval = (rq->clock - rq->load_update) >> 10;
		long load, curload = rq_load(rq);

		load = rq->load_avg - (rq->load_avg * us_interval * 5 / 262144);
		if (unlikely(load < 0))
			load = 0;
		load += curload * curload * SCHED_CAPACITY_SCALE * us_interval * 5 / 262144;
		rq->load_avg = load;
	} else
		return;

	rq->load_update = rq->clock;
	if (likely(rq_local(rq)))
		cpufreq_trigger(rq->niffies, rq->load_avg);
}

/*
 * Removing from the runqueue. Enter with rq locked. Deleting a task
 * from the skip list is done via the stored node reference in the task struct
 * and does not require a full look up. Thus it occurs in O(k) time where k
 * is the "level" of the list the task was stored at - usually < 4, max 8.
 */
static void dequeue_task(struct rq *rq, struct task_struct *p, int flags)
{
	skiplist_delete(rq->sl, &p->node);
	rq->best_key = rq->node.next[0]->key;
	update_clocks(rq);
	if (!(flags & DEQUEUE_SAVE))
		sched_info_dequeued(task_rq(p), p);
	update_load_avg(rq);
}

#ifdef CONFIG_PREEMPT_RCU
static bool rcu_read_critical(struct task_struct *p)
{
	return p->rcu_read_unlock_special.b.blocked;
}
#else /* CONFIG_PREEMPT_RCU */
#define rcu_read_critical(p) (false)
#endif /* CONFIG_PREEMPT_RCU */

/*
 * To determine if it's safe for a task of SCHED_IDLEPRIO to actually run as
 * an idle task, we ensure none of the following conditions are met.
 */
static bool idleprio_suitable(struct task_struct *p)
{
	return (!(task_contributes_to_load(p)) && !(p->flags & (PF_EXITING)) &&
		!signal_pending(p) && !rcu_read_critical(p) && !freezing(p));
}

/*
 * To determine if a task of SCHED_ISO can run in pseudo-realtime, we check
 * that the iso_refractory flag is not set.
 */
static inline bool isoprio_suitable(struct rq *rq)
{
	return !rq->iso_refractory;
}

/*
 * Adding to the runqueue. Enter with rq locked.
 */
static void enqueue_task(struct rq *rq, struct task_struct *p, int flags)
{
	unsigned int randseed;
	u64 sl_id;

	if (!rt_task(p)) {
		/* Check it hasn't gotten rt from PI */
		if ((idleprio_task(p) && idleprio_suitable(p)) ||
		   (iso_task(p) && isoprio_suitable(rq)))
			p->prio = p->normal_prio;
		else
			p->prio = NORMAL_PRIO;
	}
	/*
	 * The sl_id key passed to the skiplist generates a sorted list.
	 * Realtime and sched iso tasks run FIFO so they only need be sorted
	 * according to priority. The skiplist will put tasks of the same
	 * key inserted later in FIFO order. Tasks of sched normal, batch
	 * and idleprio are sorted according to their deadlines. Idleprio
	 * tasks are offset by an impossibly large deadline value ensuring
	 * they get sorted into last positions, but still according to their
	 * own deadlines. This creates a "landscape" of skiplists running
	 * from priority 0 realtime in first place to the lowest priority
	 * idleprio tasks last. Skiplist insertion is an O(log n) process.
	 */
	if (p->prio <= ISO_PRIO)
		sl_id = p->prio;
	else {
		sl_id = p->deadline;
		if (idleprio_task(p)) {
			if (p->prio == IDLE_PRIO)
				sl_id |= 0xF000000000000000;
			else
				sl_id += longest_deadline_diff();
		}
	}
	/*
	 * Some architectures don't have better than microsecond resolution
	 * so mask out ~microseconds as the random seed for skiplist insertion.
	 */
	update_clocks(rq);
	if (!(flags & ENQUEUE_RESTORE))
		sched_info_queued(rq, p);
	randseed = (rq->niffies >> 10) & 0xFFFFFFFF;
	skiplist_insert(rq->sl, &p->node, sl_id, p, randseed);
	rq->best_key = rq->node.next[0]->key;
	update_load_avg(rq);
}

/*
 * Returns the relative length of deadline all compared to the shortest
 * deadline which is that of nice -20.
 */
static inline int task_prio_ratio(struct task_struct *p)
{
	return prio_ratios[TASK_USER_PRIO(p)];
}

/*
 * task_timeslice - all tasks of all priorities get the exact same timeslice
 * length. CPU distribution is handled by giving different deadlines to
 * tasks of different priorities. Use 128 as the base value for fast shifts.
 */
static inline int task_timeslice(struct task_struct *p)
{
	return (rr_interval * task_prio_ratio(p) / 128);
}

/*
 * qnr is the "queued but not running" count which is the total number of
 * tasks on the global runqueue list waiting for cpu time but not actually
 * currently running on a cpu.
 */
static inline void inc_qnr(void)
{
	atomic_inc(&grq.qnr);
}

static inline void dec_qnr(void)
{
	atomic_dec(&grq.qnr);
}

static inline int queued_notrunning(void)
{
	return atomic_read(&grq.qnr);
}

#ifdef CONFIG_SMP
/* Entered with rq locked */
static inline void resched_if_idle(struct rq *rq)
{
	if (rq_idle(rq) && rq->online)
		resched_task(rq->curr);
}

static inline bool rq_local(struct rq *rq)
{
	return (rq->cpu == smp_processor_id());
}
#ifdef CONFIG_SMT_NICE
static const cpumask_t *thread_cpumask(int cpu);

/* Find the best real time priority running on any SMT siblings of cpu and if
 * none are running, the static priority of the best deadline task running.
 * The lookups to the other runqueues is done lockless as the occasional wrong
 * value would be harmless. */
static int best_smt_bias(struct rq *this_rq)
{
	int other_cpu, best_bias = 0;

	for_each_cpu(other_cpu, &this_rq->thread_mask) {
		struct rq *rq = cpu_rq(other_cpu);

		if (rq_idle(rq))
			continue;
		if (unlikely(!rq->online))
			continue;
		if (!rq->rq_mm)
			continue;
		if (likely(rq->rq_smt_bias > best_bias))
			best_bias = rq->rq_smt_bias;
	}
	return best_bias;
}

static int task_prio_bias(struct task_struct *p)
{
	if (rt_task(p))
		return 1 << 30;
	else if (task_running_iso(p))
		return 1 << 29;
	else if (task_running_idle(p))
		return 0;
	return MAX_PRIO - p->static_prio;
}

static bool smt_always_schedule(struct task_struct __maybe_unused *p, struct rq __maybe_unused *this_rq)
{
	return true;
}

static bool (*smt_schedule)(struct task_struct *p, struct rq *this_rq) = &smt_always_schedule;

/* We've already decided p can run on CPU, now test if it shouldn't for SMT
 * nice reasons. */
static bool smt_should_schedule(struct task_struct *p, struct rq *this_rq)
{
	int best_bias, task_bias;

	/* Kernel threads always run */
	if (unlikely(!p->mm))
		return true;
	if (rt_task(p))
		return true;
	if (!idleprio_suitable(p))
		return true;
	best_bias = best_smt_bias(this_rq);
	/* The smt siblings are all idle or running IDLEPRIO */
	if (best_bias < 1)
		return true;
	task_bias = task_prio_bias(p);
	if (task_bias < 1)
		return false;
	if (task_bias >= best_bias)
		return true;
	/* Dither 25% cpu of normal tasks regardless of nice difference */
	if (best_bias % 4 == 1)
		return true;
	/* Sorry, you lose */
	return false;
}
#else /* CONFIG_SMT_NICE */
#define smt_schedule(p, this_rq) (true)
#endif /* CONFIG_SMT_NICE */

static inline void atomic_set_cpu(int cpu, cpumask_t *cpumask)
{
	set_bit(cpu, (volatile unsigned long *)cpumask);
}

/*
 * The cpu_idle_map stores a bitmap of all the CPUs currently idle to
 * allow easy lookup of whether any suitable idle CPUs are available.
 * It's cheaper to maintain a binary yes/no if there are any idle CPUs on the
 * idle_cpus variable than to do a full bitmask check when we are busy. The
 * bits are set atomically but read locklessly as occasional false positive /
 * negative is harmless.
 */
static inline void set_cpuidle_map(int cpu)
{
	if (likely(cpu_online(cpu)))
		atomic_set_cpu(cpu, &grq.cpu_idle_map);
}

static inline void atomic_clear_cpu(int cpu, cpumask_t *cpumask)
{
	clear_bit(cpu, (volatile unsigned long *)cpumask);
}

static inline void clear_cpuidle_map(int cpu)
{
	atomic_clear_cpu(cpu, &grq.cpu_idle_map);
}

static bool suitable_idle_cpus(struct task_struct *p)
{
	return (cpumask_intersects(&p->cpus_allowed, &grq.cpu_idle_map));
}

/*
 * Resched current on rq. We don't know if rq is local to this CPU nor if it
 * is locked so we do not use an intermediate variable for the task to avoid
 * having it dereferenced.
 */
static void resched_curr(struct rq *rq)
{
	if (test_tsk_need_resched(rq->curr))
		return;

	rq->preempt = rq->curr;

	/* We're doing this without holding the rq lock if it's not task_rq */
	set_tsk_need_resched(rq->curr);

	if (rq_local(rq)) {
		set_preempt_need_resched();
		return;
	}

	smp_send_reschedule(rq->cpu);
}

#define CPUIDLE_DIFF_THREAD	(1)
#define CPUIDLE_DIFF_CORE	(2)
#define CPUIDLE_CACHE_BUSY	(4)
#define CPUIDLE_DIFF_CPU	(8)
#define CPUIDLE_THREAD_BUSY	(16)
#define CPUIDLE_DIFF_NODE	(32)

/*
 * The best idle CPU is chosen according to the CPUIDLE ranking above where the
 * lowest value would give the most suitable CPU to schedule p onto next. The
 * order works out to be the following:
 *
 * Same thread, idle or busy cache, idle or busy threads
 * Other core, same cache, idle or busy cache, idle threads.
 * Same node, other CPU, idle cache, idle threads.
 * Same node, other CPU, busy cache, idle threads.
 * Other core, same cache, busy threads.
 * Same node, other CPU, busy threads.
 * Other node, other CPU, idle cache, idle threads.
 * Other node, other CPU, busy cache, idle threads.
 * Other node, other CPU, busy threads.
 */
static int best_mask_cpu(int best_cpu, struct rq *rq, cpumask_t *tmpmask)
{
	int best_ranking = CPUIDLE_DIFF_NODE | CPUIDLE_THREAD_BUSY |
		CPUIDLE_DIFF_CPU | CPUIDLE_CACHE_BUSY | CPUIDLE_DIFF_CORE |
		CPUIDLE_DIFF_THREAD;
	int cpu_tmp;

	if (cpumask_test_cpu(best_cpu, tmpmask))
		goto out;

	for_each_cpu(cpu_tmp, tmpmask) {
		int ranking, locality;
		struct rq *tmp_rq;

		ranking = 0;
		tmp_rq = cpu_rq(cpu_tmp);

		locality = rq->cpu_locality[cpu_tmp];
#ifdef CONFIG_NUMA
		if (locality > 3)
			ranking |= CPUIDLE_DIFF_NODE;
		else
#endif
		if (locality > 2)
			ranking |= CPUIDLE_DIFF_CPU;
#ifdef CONFIG_SCHED_MC
		else if (locality == 2)
			ranking |= CPUIDLE_DIFF_CORE;
		else if (!(tmp_rq->cache_idle(tmp_rq)))
			ranking |= CPUIDLE_CACHE_BUSY;
#endif
#ifdef CONFIG_SCHED_SMT
		if (locality == 1)
			ranking |= CPUIDLE_DIFF_THREAD;
		if (!(tmp_rq->siblings_idle(tmp_rq)))
			ranking |= CPUIDLE_THREAD_BUSY;
#endif
		if (ranking < best_ranking) {
			best_cpu = cpu_tmp;
			best_ranking = ranking;
		}
	}
out:
	return best_cpu;
}

bool cpus_share_cache(int this_cpu, int that_cpu)
{
	struct rq *this_rq = cpu_rq(this_cpu);

	return (this_rq->cpu_locality[that_cpu] < 3);
}

/* As per resched_curr but only will resched idle task */
static inline void resched_idle(struct rq *rq)
{
	if (test_tsk_need_resched(rq->idle))
		return;

	rq->preempt = rq->idle;

	set_tsk_need_resched(rq->idle);

	if (rq_local(rq)) {
		set_preempt_need_resched();
		return;
	}

	smp_send_reschedule(rq->cpu);
}

static struct rq *resched_best_idle(struct task_struct *p, int cpu)
{
	cpumask_t tmpmask;
	struct rq *rq;
	int best_cpu;

	cpumask_and(&tmpmask, &p->cpus_allowed, &grq.cpu_idle_map);
	best_cpu = best_mask_cpu(cpu, task_rq(p), &tmpmask);
	rq = cpu_rq(best_cpu);
	if (!smt_schedule(p, rq))
		return NULL;
	resched_idle(rq);
	return rq;
}

static inline void resched_suitable_idle(struct task_struct *p)
{
	if (suitable_idle_cpus(p))
		resched_best_idle(p, task_cpu(p));
}

static inline struct rq *rq_order(struct rq *rq, int cpu)
{
	return rq->rq_order[cpu];
}
#else /* CONFIG_SMP */
static inline void set_cpuidle_map(int cpu)
{
}

static inline void clear_cpuidle_map(int cpu)
{
}

static inline bool suitable_idle_cpus(struct task_struct *p)
{
	return uprq->curr == uprq->idle;
}

static inline void resched_suitable_idle(struct task_struct *p)
{
}

static inline void resched_curr(struct rq *rq)
{
	resched_task(rq->curr);
}

static inline void resched_if_idle(struct rq *rq)
{
}

static inline bool rq_local(struct rq *rq)
{
	return true;
}

static inline struct rq *rq_order(struct rq *rq, int cpu)
{
	return rq;
}

static inline bool smt_schedule(struct task_struct *p, struct rq *rq)
{
	return true;
}
#endif /* CONFIG_SMP */

static inline int normal_prio(struct task_struct *p)
{
	if (has_rt_policy(p))
		return MAX_RT_PRIO - 1 - p->rt_priority;
	if (idleprio_task(p))
		return IDLE_PRIO;
	if (iso_task(p))
		return ISO_PRIO;
	return NORMAL_PRIO;
}

/*
 * Calculate the current priority, i.e. the priority
 * taken into account by the scheduler. This value might
 * be boosted by RT tasks as it will be RT if the task got
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
 * activate_task - move a task to the runqueue. Enter with rq locked.
 */
static void activate_task(struct task_struct *p, struct rq *rq)
{
	resched_if_idle(rq);

	/*
	 * Sleep time is in units of nanosecs, so shift by 20 to get a
	 * milliseconds-range estimation of the amount of time that the task
	 * spent sleeping:
	 */
	if (unlikely(prof_on == SLEEP_PROFILING)) {
		if (p->state == TASK_UNINTERRUPTIBLE)
			profile_hits(SLEEP_PROFILING, (void *)get_wchan(p),
				     (rq->clock_task - p->last_ran) >> 20);
	}

	p->prio = effective_prio(p);
	if (task_contributes_to_load(p))
		atomic_dec(&grq.nr_uninterruptible);

	enqueue_task(rq, p, 0);
	p->on_rq = TASK_ON_RQ_QUEUED;
	atomic_inc(&grq.nr_running);
	inc_qnr();
}

/*
 * deactivate_task - If it's running, it's not on the runqueue and we can just
 * decrement the nr_running. Enter with rq locked.
 */
static inline void deactivate_task(struct task_struct *p, struct rq *rq)
{
	if (task_contributes_to_load(p))
		atomic_inc(&grq.nr_uninterruptible);

	p->on_rq = 0;
	atomic_dec(&grq.nr_running);
	sched_info_dequeued(rq, p);
}

#ifdef CONFIG_SMP
void set_task_cpu(struct task_struct *p, unsigned int cpu)
{
	struct rq *rq = task_rq(p);
	bool queued;

#ifdef CONFIG_LOCKDEP
	/*
	 * The caller should hold either p->pi_lock or rq->lock, when changing
	 * a task's CPU. ->pi_lock for waking tasks, rq->lock for runnable tasks.
	 *
	 * Furthermore, all task_rq users should acquire both locks, see
	 * task_rq_lock().
	 */
	WARN_ON_ONCE(debug_locks && !(lockdep_is_held(&p->pi_lock) ||
				      lockdep_is_held(&task_rq(p)->lock)));
#endif
	if (p->wake_cpu == cpu)
		return;
	trace_sched_migrate_task(p, cpu);
	perf_event_task_migrate(p);

	/*
	 * After ->cpu is set up to a new value, task_rq_lock(p, ...) can be
	 * successfully executed on another CPU. We must ensure that updates of
	 * per-task data have been completed by this moment.
	 */
	smp_wmb();

	if (task_running(rq, p)) {
		/*
		 * We should only be calling this on a running task if we're
		 * holding rq lock.
		 */
		lockdep_assert_held(&rq->lock);

		/*
		 * We can't change the task_thread_info cpu on a running task
		 * as p will still be protected by the rq lock of the cpu it
		 * is still running on so we set the wake_cpu for it to be
		 * lazily updated once off the cpu.
		 */
		p->wake_cpu = cpu;
		return;
	}

	if ((queued = task_queued(p)))
		dequeue_task(rq, p, 0);
	task_thread_info(p)->cpu = p->wake_cpu = cpu;
	if (queued)
		enqueue_task(cpu_rq(cpu), p, 0);
}
#endif /* CONFIG_SMP */

/*
 * Move a task off the runqueue and take it to a cpu for it will
 * become the running task.
 */
static inline void take_task(struct rq *rq, int cpu, struct task_struct *p)
{
	struct rq *p_rq = task_rq(p);

	dequeue_task(p_rq, p, DEQUEUE_SAVE);
	if (p_rq != rq) {
		sched_info_dequeued(p_rq, p);
		sched_info_queued(rq, p);
	}
	set_task_cpu(p, cpu);
	dec_qnr();
}

/*
 * Returns a descheduling task to the runqueue unless it is being
 * deactivated.
 */
static inline void return_task(struct task_struct *p, struct rq *rq,
			       int cpu, bool deactivate)
{
	if (deactivate)
		deactivate_task(p, rq);
	else {
		inc_qnr();
#ifdef CONFIG_SMP
		/*
		 * set_task_cpu was called on the running task that doesn't
		 * want to deactivate so it has to be enqueued to a different
		 * CPU and we need its lock. Tag it to be moved with as the
		 * lock is dropped in finish_lock_switch.
		 */
		if (unlikely(p->wake_cpu != cpu))
			p->on_rq = TASK_ON_RQ_MIGRATING;
		else
#endif
			enqueue_task(rq, p, ENQUEUE_RESTORE);
	}
}

/* Enter with rq lock held. We know p is on the local cpu */
static inline void __set_tsk_resched(struct task_struct *p)
{
	set_tsk_need_resched(p);
	set_preempt_need_resched();
}

/**
 * task_curr - is this task currently executing on a CPU?
 * @p: the task in question.
 *
 * Return: 1 if the task is currently executing. 0 otherwise.
 */
inline int task_curr(const struct task_struct *p)
{
	return cpu_curr(task_cpu(p)) == p;
}

#ifdef CONFIG_SMP
/*
 * wait_task_inactive - wait for a thread to unschedule.
 *
 * If @match_state is nonzero, it's the @p->state value just checked and
 * not expected to change.  If it changes, i.e. @p might have woken up,
 * then return zero.  When we succeed in waiting for @p to be off its CPU,
 * we return a positive number (its total switch count).  If a second call
 * a short while later returns the same number, the caller can be sure that
 * @p has remained unscheduled the whole time.
 *
 * The caller must ensure that the task *will* unschedule sometime soon,
 * else this function might spin for a *long* time. This function can't
 * be called with interrupts off, or it may introduce deadlock with
 * smp_call_function() if an IPI is sent by the same process we are
 * waiting to become inactive.
 */
unsigned long wait_task_inactive(struct task_struct *p, long match_state)
{
	int running, queued;
	unsigned long flags;
	unsigned long ncsw;
	struct rq *rq;

	for (;;) {
		rq = task_rq(p);

		/*
		 * If the task is actively running on another CPU
		 * still, just relax and busy-wait without holding
		 * any locks.
		 *
		 * NOTE! Since we don't hold any locks, it's not
		 * even sure that "rq" stays as the right runqueue!
		 * But we don't care, since this will return false
		 * if the runqueue has changed and p is actually now
		 * running somewhere else!
		 */
		while (task_running(rq, p)) {
			if (match_state && unlikely(p->state != match_state))
				return 0;
			cpu_relax();
		}

		/*
		 * Ok, time to look more closely! We need the rq
		 * lock now, to be *sure*. If we're wrong, we'll
		 * just go back and repeat.
		 */
		rq = task_rq_lock(p, &flags);
		trace_sched_wait_task(p);
		running = task_running(rq, p);
		queued = task_on_rq_queued(p);
		ncsw = 0;
		if (!match_state || p->state == match_state)
			ncsw = p->nvcsw | LONG_MIN; /* sets MSB */
		task_rq_unlock(rq, p, &flags);

		/*
		 * If it changed from the expected state, bail out now.
		 */
		if (unlikely(!ncsw))
			break;

		/*
		 * Was it really running after all now that we
		 * checked with the proper locks actually held?
		 *
		 * Oops. Go back and try again..
		 */
		if (unlikely(running)) {
			cpu_relax();
			continue;
		}

		/*
		 * It's not enough that it's not actively running,
		 * it must be off the runqueue _entirely_, and not
		 * preempted!
		 *
		 * So if it was still runnable (but just not actively
		 * running right now), it's preempted, and we should
		 * yield - it could be a while.
		 */
		if (unlikely(queued)) {
			ktime_t to = ktime_set(0, NSEC_PER_SEC / HZ);

			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_hrtimeout(&to, HRTIMER_MODE_REL);
			continue;
		}

		/*
		 * Ahh, all good. It wasn't running, and it wasn't
		 * runnable, which means that it will never become
		 * running in the future either. We're all done!
		 */
		break;
	}

	return ncsw;
}

/***
 * kick_process - kick a running thread to enter/exit the kernel
 * @p: the to-be-kicked thread
 *
 * Cause a process which is running on another CPU to enter
 * kernel-mode, without any delay. (to get signals handled.)
 *
 * NOTE: this function doesn't have to take the runqueue lock,
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
EXPORT_SYMBOL_GPL(kick_process);
#endif

/*
 * RT tasks preempt purely on priority. SCHED_NORMAL tasks preempt on the
 * basis of earlier deadlines. SCHED_IDLEPRIO don't preempt anything else or
 * between themselves, they cooperatively multitask. An idle rq scores as
 * prio PRIO_LIMIT so it is always preempted.
 */
static inline bool
can_preempt(struct task_struct *p, int prio, u64 deadline)
{
	/* Better static priority RT task or better policy preemption */
	if (p->prio < prio)
		return true;
	if (p->prio > prio)
		return false;
	if (p->policy == SCHED_BATCH)
		return false;
	/* SCHED_NORMAL and ISO will preempt based on deadline */
	if (!deadline_before(p->deadline, deadline))
		return false;
	return true;
}

#ifdef CONFIG_SMP
/*
 * Check to see if p can run on cpu, and if not, whether there are any online
 * CPUs it can run on instead.
 */
static inline bool needs_other_cpu(struct task_struct *p, int cpu)
{
	if (unlikely(!cpumask_test_cpu(cpu, &p->cpus_allowed)))
		return true;
	return false;
}
#define cpu_online_map		(*(cpumask_t *)cpu_online_mask)

static void try_preempt(struct task_struct *p, struct rq *this_rq)
{
	int i, this_entries = rq_load(this_rq);
	cpumask_t tmp;

	if (suitable_idle_cpus(p) && resched_best_idle(p, task_cpu(p)))
		return;

	/* IDLEPRIO tasks never preempt anything but idle */
	if (p->policy == SCHED_IDLEPRIO)
		return;

	cpumask_and(&tmp, &cpu_online_map, &p->cpus_allowed);

	for (i = 0; i < num_possible_cpus(); i++) {
		struct rq *rq = this_rq->rq_order[i];

		if (!cpumask_test_cpu(rq->cpu, &tmp))
			continue;

		if (!sched_interactive && rq != this_rq && rq_load(rq) <= this_entries)
			continue;
		if (smt_schedule(p, rq) && can_preempt(p, rq->rq_prio, rq->rq_deadline)) {
			resched_curr(rq);
			return;
		}
	}
}

static int __set_cpus_allowed_ptr(struct task_struct *p,
				  const struct cpumask *new_mask, bool check);
#else /* CONFIG_SMP */
static inline bool needs_other_cpu(struct task_struct *p, int cpu)
{
	return false;
}

static void try_preempt(struct task_struct *p, struct rq *this_rq)
{
	if (p->policy == SCHED_IDLEPRIO)
		return;
	if (can_preempt(p, uprq->rq_prio, uprq->rq_deadline))
		resched_curr(uprq);
}

static inline int __set_cpus_allowed_ptr(struct task_struct *p,
					 const struct cpumask *new_mask, bool check)
{
	return set_cpus_allowed_ptr(p, new_mask);
}
#endif /* CONFIG_SMP */

/*
 * wake flags
 */
#define WF_SYNC		0x01		/* waker goes to sleep after wakeup */
#define WF_FORK		0x02		/* child wakeup after fork */
#define WF_MIGRATED	0x04		/* internal use, task got migrated */

static void
ttwu_stat(struct task_struct *p, int cpu, int wake_flags)
{
#ifdef CONFIG_SCHEDSTATS
	struct rq *rq = this_rq();

#ifdef CONFIG_SMP
	int this_cpu = smp_processor_id();

	if (cpu == this_cpu)
		schedstat_inc(rq, ttwu_local);
	else {
		struct sched_domain *sd;

		rcu_read_lock();
		for_each_domain(this_cpu, sd) {
			if (cpumask_test_cpu(cpu, sched_domain_span(sd))) {
				schedstat_inc(sd, ttwu_wake_remote);
				break;
			}
		}
		rcu_read_unlock();
	}

#endif /* CONFIG_SMP */

	schedstat_inc(rq, ttwu_count);
#endif /* CONFIG_SCHEDSTATS */
}

static inline void ttwu_activate(struct rq *rq, struct task_struct *p)
{
	activate_task(p, rq);

	/* if a worker is waking up, notify workqueue */
	if (p->flags & PF_WQ_WORKER)
		wq_worker_waking_up(p, cpu_of(rq));
}

/*
 * Mark the task runnable and perform wakeup-preemption.
 */
static void ttwu_do_wakeup(struct rq *rq, struct task_struct *p, int wake_flags)
{
	/*
	 * Sync wakeups (i.e. those types of wakeups where the waker
	 * has indicated that it will leave the CPU in short order)
	 * don't trigger a preemption if there are no idle cpus,
	 * instead waiting for current to deschedule.
	 */
	if (wake_flags & WF_SYNC)
		resched_suitable_idle(p);
	else
		try_preempt(p, rq);
	p->state = TASK_RUNNING;
	trace_sched_wakeup(p);
}

static void
ttwu_do_activate(struct rq *rq, struct task_struct *p, int wake_flags)
{
	lockdep_assert_held(&rq->lock);

#ifdef CONFIG_SMP
	if (p->sched_contributes_to_load)
		atomic_dec(&grq.nr_uninterruptible);
#endif

	ttwu_activate(rq, p);
	ttwu_do_wakeup(rq, p, wake_flags);
}

/*
 * Called in case the task @p isn't fully descheduled from its runqueue,
 * in this case we must do a remote wakeup. Its a 'light' wakeup though,
 * since all we need to do is flip p->state to TASK_RUNNING, since
 * the task is still ->on_rq.
 */
static int ttwu_remote(struct task_struct *p, int wake_flags)
{
	struct rq *rq;
	int ret = 0;

	rq = __task_rq_lock(p);
	if (likely(task_on_rq_queued(p))) {
		ttwu_do_wakeup(rq, p, wake_flags);
		ret = 1;
	}
	__task_rq_unlock(rq);

	return ret;
}


void wake_up_if_idle(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	rcu_read_lock();

	if (!is_idle_task(rcu_dereference(rq->curr)))
		goto out;

	rq_lock_irqsave(rq, &flags);
	if (likely(is_idle_task(rq->curr)))
		smp_send_reschedule(cpu);
	/* Else cpu is not in idle, do nothing here */
	rq_unlock_irqrestore(rq, &flags);

out:
	rcu_read_unlock();
}

static bool sched_smp_initialized __read_mostly;

#ifdef CONFIG_SMP
void scheduler_ipi(void)
{
	/*
	 * Fold TIF_NEED_RESCHED into the preempt_count; anybody setting
	 * TIF_NEED_RESCHED remotely (for the first time) will also send
	 * this IPI.
	 */
	preempt_fold_need_resched();

	if (!idle_cpu(smp_processor_id()) || need_resched())
		return;

	irq_enter();
	irq_exit();
}

static int valid_task_cpu(struct task_struct *p)
{
	cpumask_t valid_mask;

	if (p->flags & PF_KTHREAD)
		cpumask_and(&valid_mask, tsk_cpus_allowed(p), cpu_online_mask);
	else
		cpumask_and(&valid_mask, tsk_cpus_allowed(p), cpu_active_mask);

	if (unlikely(!cpumask_weight(&valid_mask))) {
		WARN_ON(sched_smp_initialized);
		return smp_processor_id();
	}
	return cpumask_any(&valid_mask);
}

/*
 * For a task that's just being woken up we have a valuable balancing
 * opportunity so choose the nearest cache most lightly loaded runqueue.
 * Entered with rq locked and returns with the chosen runqueue locked.
 */
static inline int select_best_cpu(struct task_struct *p)
{
	unsigned int idlest = ~0U;
	struct rq *rq = NULL;
	int i;

	if (suitable_idle_cpus(p)) {
		int cpu = task_cpu(p);

		if (unlikely(needs_other_cpu(p, cpu)))
			cpu = valid_task_cpu(p);
		rq = resched_best_idle(p, cpu);
		if (likely(rq))
			return rq->cpu;
	}

	for (i = 0; i < num_possible_cpus(); i++) {
		struct rq *other_rq = task_rq(p)->rq_order[i];
		int entries;

		if (!other_rq->online)
			continue;
		if (needs_other_cpu(p, other_rq->cpu))
			continue;
		entries = rq_load(other_rq);
		if (entries >= idlest)
			continue;
		idlest = entries;
		rq = other_rq;
	}
	if (unlikely(!rq))
		return smp_processor_id();
	return rq->cpu;
}
#else /* CONFIG_SMP */
static int valid_task_cpu(struct task_struct *p)
{
	return 0;
}

static inline int select_best_cpu(struct task_struct *p)
{
	return 0;
}

static struct rq *resched_best_idle(struct task_struct *p, int cpu)
{
	return NULL;
}
#endif /* CONFIG_SMP */

static void ttwu_queue(struct task_struct *p, int cpu, int wake_flags)
{
	struct rq *rq = cpu_rq(cpu);

	rq_lock(rq);
	ttwu_do_activate(rq, p, wake_flags);
	rq_unlock(rq);
}

/***
 * try_to_wake_up - wake up a thread
 * @p: the thread to be awakened
 * @state: the mask of task states that can be woken
 * @wake_flags: wake modifier flags (WF_*)
 *
 * Put it on the run-queue if it's not already there. The "current"
 * thread is always on the run-queue (except when the actual
 * re-schedule is in progress), and as such you're allowed to do
 * the simpler "current->state = TASK_RUNNING" to mark yourself
 * runnable without the overhead of this.
 *
 * Return: %true if @p was woken up, %false if it was already running.
 * or @state didn't match @p's state.
 */
static int
try_to_wake_up(struct task_struct *p, unsigned int state, int wake_flags)
{
	unsigned long flags;
	int cpu, success = 0;

	/*
	 * If we are going to wake up a thread waiting for CONDITION we
	 * need to ensure that CONDITION=1 done by the caller can not be
	 * reordered with p->state check below. This pairs with mb() in
	 * set_current_state() the waiting thread does.
	 */
	smp_mb__before_spinlock();
	raw_spin_lock_irqsave(&p->pi_lock, flags);
	/* state is a volatile long, どうして、分からない */
	if (!((unsigned int)p->state & state))
		goto out;

	trace_sched_waking(p);

	success = 1; /* we're going to change ->state */
	cpu = task_cpu(p);

	/*
	 * Ensure we load p->on_rq _after_ p->state, otherwise it would
	 * be possible to, falsely, observe p->on_rq == 0 and get stuck
	 * in smp_cond_load_acquire() below.
	 *
	 * sched_ttwu_pending()                 try_to_wake_up()
	 *   [S] p->on_rq = 1;                  [L] P->state
	 *       UNLOCK rq->lock  -----.
	 *                              \
	 *				 +---   RMB
	 * schedule()                   /
	 *       LOCK rq->lock    -----'
	 *       UNLOCK rq->lock
	 *
	 * [task p]
	 *   [S] p->state = UNINTERRUPTIBLE     [L] p->on_rq
	 *
	 * Pairs with the UNLOCK+LOCK on rq->lock from the
	 * last wakeup of our task and the schedule that got our task
	 * current.
	 */
	smp_rmb();
	if (p->on_rq && ttwu_remote(p, wake_flags))
		goto stat;

#ifdef CONFIG_SMP
	/*
	 * Ensure we load p->on_cpu _after_ p->on_rq, otherwise it would be
	 * possible to, falsely, observe p->on_cpu == 0.
	 *
	 * One must be running (->on_cpu == 1) in order to remove oneself
	 * from the runqueue.
	 *
	 *  [S] ->on_cpu = 1;	[L] ->on_rq
	 *      UNLOCK rq->lock
	 *			RMB
	 *      LOCK   rq->lock
	 *  [S] ->on_rq = 0;    [L] ->on_cpu
	 *
	 * Pairs with the full barrier implied in the UNLOCK+LOCK on rq->lock
	 * from the consecutive calls to schedule(); the first switching to our
	 * task, the second putting it to sleep.
	 */
	smp_rmb();

	/*
	 * If the owning (remote) cpu is still in the middle of schedule() with
	 * this task as prev, wait until its done referencing the task.
	 *
	 * Pairs with the smp_store_release() in finish_lock_switch().
	 *
	 * This ensures that tasks getting woken will be fully ordered against
	 * their previous state and preserve Program Order.
	 */
	smp_cond_load_acquire(&p->on_cpu, !VAL);

	p->sched_contributes_to_load = !!task_contributes_to_load(p);
	p->state = TASK_WAKING;

	cpu = select_best_cpu(p);
	if (task_cpu(p) != cpu)
		set_task_cpu(p, cpu);
#endif /* CONFIG_SMP */

	ttwu_queue(p, cpu, wake_flags);
stat:
	if (schedstat_enabled())
		ttwu_stat(p, cpu, wake_flags);
out:
	raw_spin_unlock_irqrestore(&p->pi_lock, flags);

	return success;
}

/**
 * try_to_wake_up_local - try to wake up a local task with rq lock held
 * @p: the thread to be awakened
 *
 * Put @p on the run-queue if it's not already there. The caller must
 * ensure that rq is locked and, @p is not the current task.
 * rq stays locked over invocation.
 */
static void try_to_wake_up_local(struct task_struct *p)
{
	struct rq *rq = task_rq(p);

	if (WARN_ON_ONCE(rq != this_rq()) ||
	    WARN_ON_ONCE(p == current))
		return;

	lockdep_assert_held(&rq->lock);

	if (!raw_spin_trylock(&p->pi_lock)) {
		/*
		 * This is OK, because current is on_cpu, which avoids it being
		 * picked for load-balance and preemption/IRQs are still
		 * disabled avoiding further scheduler activity on it and we've
		 * not yet picked a replacement task.
		 */
		raw_spin_unlock(&rq->lock);
		raw_spin_lock(&p->pi_lock);
		raw_spin_lock(&rq->lock);
	}

	if (!(p->state & TASK_NORMAL))
		goto out;

	trace_sched_waking(p);

	if (!task_on_rq_queued(p))
		ttwu_activate(rq, p);

	ttwu_do_wakeup(rq, p, 0);
	if (schedstat_enabled())
		ttwu_stat(p, smp_processor_id(), 0);
out:
	raw_spin_unlock(&p->pi_lock);
}

/**
 * wake_up_process - Wake up a specific process
 * @p: The process to be woken up.
 *
 * Attempt to wake up the nominated process and move it to the set of runnable
 * processes.
 *
 * Return: 1 if the process was woken up, 0 if it was already running.
 *
 * It may be assumed that this function implies a write memory barrier before
 * changing the task state if and only if any tasks are woken up.
 */
int wake_up_process(struct task_struct *p)
{
	return try_to_wake_up(p, TASK_NORMAL, 0);
}
EXPORT_SYMBOL(wake_up_process);

int wake_up_state(struct task_struct *p, unsigned int state)
{
	return try_to_wake_up(p, state, 0);
}

static void time_slice_expired(struct task_struct *p, struct rq *rq);

/*
 * Perform scheduler related setup for a newly forked process p.
 * p is forked by current.
 */
int sched_fork(unsigned long __maybe_unused clone_flags, struct task_struct *p)
{
	unsigned long flags;
	int cpu = get_cpu();

#ifdef CONFIG_PREEMPT_NOTIFIERS
	INIT_HLIST_HEAD(&p->preempt_notifiers);
#endif
	/*
	 * We mark the process as NEW here. This guarantees that
	 * nobody will actually run it, and a signal or other external
	 * event cannot wake it up and insert it on the runqueue either.
	 */
	p->state = TASK_NEW;

	/*
	 * The process state is set to the same value of the process executing
	 * do_fork() code. That is running. This guarantees that nobody will
	 * actually run it, and a signal or other external event cannot wake
	 * it up and insert it on the runqueue either.
	 */

	/* Should be reset in fork.c but done here for ease of MuQSS patching */
	p->on_cpu =
	p->on_rq =
	p->utime =
	p->stime =
	p->utimescaled =
	p->stimescaled =
	p->sched_time =
	p->stime_pc =
	p->utime_pc = 0;
	skiplist_node_init(&p->node);

	/*
	 * Revert to default priority/policy on fork if requested.
	 */
	if (unlikely(p->sched_reset_on_fork)) {
		if (p->policy == SCHED_FIFO || p->policy == SCHED_RR) {
			p->policy = SCHED_NORMAL;
			p->normal_prio = normal_prio(p);
		}

		if (PRIO_TO_NICE(p->static_prio) < 0) {
			p->static_prio = NICE_TO_PRIO(0);
			p->normal_prio = p->static_prio;
		}

		/*
		 * We don't need the reset flag anymore after the fork. It has
		 * fulfilled its duty:
		 */
		p->sched_reset_on_fork = 0;
	}

	/*
	 * Silence PROVE_RCU.
	 */
	raw_spin_lock_irqsave(&p->pi_lock, flags);
	set_task_cpu(p, cpu);
	raw_spin_unlock_irqrestore(&p->pi_lock, flags);

#ifdef CONFIG_SCHED_INFO
	if (unlikely(sched_info_on()))
		memset(&p->sched_info, 0, sizeof(p->sched_info));
#endif
	init_task_preempt_count(p);

	put_cpu();
	return 0;
}

#ifdef CONFIG_SCHEDSTATS

DEFINE_STATIC_KEY_FALSE(sched_schedstats);
static bool __initdata __sched_schedstats = false;

static void set_schedstats(bool enabled)
{
	if (enabled)
		static_branch_enable(&sched_schedstats);
	else
		static_branch_disable(&sched_schedstats);
}

void force_schedstat_enabled(void)
{
	if (!schedstat_enabled()) {
		pr_info("kernel profiling enabled schedstats, disable via kernel.sched_schedstats.\n");
		static_branch_enable(&sched_schedstats);
	}
}

static int __init setup_schedstats(char *str)
{
	int ret = 0;
	if (!str)
		goto out;

	/*
	 * This code is called before jump labels have been set up, so we can't
	 * change the static branch directly just yet.  Instead set a temporary
	 * variable so init_schedstats() can do it later.
	 */
	if (!strcmp(str, "enable")) {
		__sched_schedstats = true;
		ret = 1;
	} else if (!strcmp(str, "disable")) {
		__sched_schedstats = false;
		ret = 1;
	}
out:
	if (!ret)
		pr_warn("Unable to parse schedstats=\n");

	return ret;
}
__setup("schedstats=", setup_schedstats);

static void __init init_schedstats(void)
{
	set_schedstats(__sched_schedstats);
}

#ifdef CONFIG_PROC_SYSCTL
int sysctl_schedstats(struct ctl_table *table, int write,
			 void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table t;
	int err;
	int state = static_branch_likely(&sched_schedstats);

	if (write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	t = *table;
	t.data = &state;
	err = proc_dointvec_minmax(&t, write, buffer, lenp, ppos);
	if (err < 0)
		return err;
	if (write)
		set_schedstats(state);
	return err;
}
#endif /* CONFIG_PROC_SYSCTL */
#else  /* !CONFIG_SCHEDSTATS */
static inline void init_schedstats(void) {}
#endif /* CONFIG_SCHEDSTATS */

/*
 * wake_up_new_task - wake up a newly created task for the first time.
 *
 * This function will do some initial scheduler statistics housekeeping
 * that must be done for every newly created context, then puts the task
 * on the runqueue and wakes it.
 */
void wake_up_new_task(struct task_struct *p)
{
	struct task_struct *parent, *rq_curr;
	struct rq *rq, *new_rq;
	unsigned long flags;

	parent = p->parent;

	raw_spin_lock_irqsave(&p->pi_lock, flags);
	p->state = TASK_RUNNING;
	/* Task_rq can't change yet on a new task */
	new_rq = rq = task_rq(p);
	if (unlikely(needs_other_cpu(p, task_cpu(p)))) {
		set_task_cpu(p, valid_task_cpu(p));
		new_rq = task_rq(p);
	}

	double_rq_lock(rq, new_rq);
	update_clocks(rq);
	rq_curr = rq->curr;

	/*
	 * Make sure we do not leak PI boosting priority to the child.
	 */
	p->prio = rq_curr->normal_prio;

	activate_task(p, rq);
	trace_sched_wakeup_new(p);

	/*
	 * Share the timeslice between parent and child, thus the
	 * total amount of pending timeslices in the system doesn't change,
	 * resulting in more scheduling fairness. If it's negative, it won't
	 * matter since that's the same as being 0. rq->rq_deadline is only
	 * modified within schedule() so it is always equal to
	 * current->deadline.
	 */
	p->last_ran = rq_curr->last_ran;
	if (likely(rq_curr->policy != SCHED_FIFO)) {
		rq_curr->time_slice /= 2;
		if (unlikely(rq_curr->time_slice < RESCHED_US)) {
			/*
			 * Forking task has run out of timeslice. Reschedule it and
			 * start its child with a new time slice and deadline. The
			 * child will end up running first because its deadline will
			 * be slightly earlier.
			 */
			rq_curr->time_slice = 0;
			__set_tsk_resched(rq_curr);
			time_slice_expired(p, new_rq);
			if (suitable_idle_cpus(p))
				resched_best_idle(p, task_cpu(p));
			else if (unlikely(rq != new_rq))
				try_preempt(p, new_rq);
		} else {
			p->time_slice = rq_curr->time_slice;
			if (rq_curr == parent && rq == new_rq && !suitable_idle_cpus(p)) {
				/*
				 * The VM isn't cloned, so we're in a good position to
				 * do child-runs-first in anticipation of an exec. This
				 * usually avoids a lot of COW overhead.
				 */
				__set_tsk_resched(rq_curr);
			} else
				try_preempt(p, new_rq);
		}
	} else {
		time_slice_expired(p, new_rq);
		try_preempt(p, new_rq);
	}
	double_rq_unlock(rq, new_rq);
	raw_spin_unlock_irqrestore(&p->pi_lock, flags);
}

#ifdef CONFIG_PREEMPT_NOTIFIERS

static struct static_key preempt_notifier_key = STATIC_KEY_INIT_FALSE;

void preempt_notifier_inc(void)
{
	static_key_slow_inc(&preempt_notifier_key);
}
EXPORT_SYMBOL_GPL(preempt_notifier_inc);

void preempt_notifier_dec(void)
{
	static_key_slow_dec(&preempt_notifier_key);
}
EXPORT_SYMBOL_GPL(preempt_notifier_dec);

/**
 * preempt_notifier_register - tell me when current is being preempted & rescheduled
 * @notifier: notifier struct to register
 */
void preempt_notifier_register(struct preempt_notifier *notifier)
{
	if (!static_key_false(&preempt_notifier_key))
		WARN(1, "registering preempt_notifier while notifiers disabled\n");

	hlist_add_head(&notifier->link, &current->preempt_notifiers);
}
EXPORT_SYMBOL_GPL(preempt_notifier_register);

/**
 * preempt_notifier_unregister - no longer interested in preemption notifications
 * @notifier: notifier struct to unregister
 *
 * This is *not* safe to call from within a preemption notifier.
 */
void preempt_notifier_unregister(struct preempt_notifier *notifier)
{
	hlist_del(&notifier->link);
}
EXPORT_SYMBOL_GPL(preempt_notifier_unregister);

static void __fire_sched_in_preempt_notifiers(struct task_struct *curr)
{
	struct preempt_notifier *notifier;

	hlist_for_each_entry(notifier, &curr->preempt_notifiers, link)
		notifier->ops->sched_in(notifier, raw_smp_processor_id());
}

static __always_inline void fire_sched_in_preempt_notifiers(struct task_struct *curr)
{
	if (static_key_false(&preempt_notifier_key))
		__fire_sched_in_preempt_notifiers(curr);
}

static void
__fire_sched_out_preempt_notifiers(struct task_struct *curr,
				 struct task_struct *next)
{
	struct preempt_notifier *notifier;

	hlist_for_each_entry(notifier, &curr->preempt_notifiers, link)
		notifier->ops->sched_out(notifier, next);
}

static __always_inline void
fire_sched_out_preempt_notifiers(struct task_struct *curr,
				 struct task_struct *next)
{
	if (static_key_false(&preempt_notifier_key))
		__fire_sched_out_preempt_notifiers(curr, next);
}

#else /* !CONFIG_PREEMPT_NOTIFIERS */

static inline void fire_sched_in_preempt_notifiers(struct task_struct *curr)
{
}

static inline void
fire_sched_out_preempt_notifiers(struct task_struct *curr,
				 struct task_struct *next)
{
}

#endif /* CONFIG_PREEMPT_NOTIFIERS */

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
static inline void
prepare_task_switch(struct rq *rq, struct task_struct *prev,
		    struct task_struct *next)
{
	sched_info_switch(rq, prev, next);
	perf_event_task_sched_out(prev, next);
	fire_sched_out_preempt_notifiers(prev, next);
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
 *
 * The context switch have flipped the stack from under us and restored the
 * local variables which were saved when this task called schedule() in the
 * past. prev == current is still correct but we need to recalculate this_rq
 * because prev may have moved to another CPU.
 */
static struct rq *finish_task_switch(struct task_struct *prev)
	__releases(rq->lock)
{
	struct rq *rq = this_rq();
	struct mm_struct *mm = rq->prev_mm;
	long prev_state;

	/*
	 * The previous task will have left us with a preempt_count of 2
	 * because it left us after:
	 *
	 *	schedule()
	 *	  preempt_disable();			// 1
	 *	  __schedule()
	 *	    raw_spin_lock_irq(&rq->lock)	// 2
	 *
	 * Also, see FORK_PREEMPT_COUNT.
	 */
	if (WARN_ONCE(preempt_count() != 2*PREEMPT_DISABLE_OFFSET,
		      "corrupted preempt_count: %s/%d/0x%x\n",
		      current->comm, current->pid, preempt_count()))
		preempt_count_set(FORK_PREEMPT_COUNT);

	rq->prev_mm = NULL;

	/*
	 * A task struct has one reference for the use as "current".
	 * If a task dies, then it sets TASK_DEAD in tsk->state and calls
	 * schedule one last time. The schedule call will never return, and
	 * the scheduled task must drop that reference.
	 *
	 * We must observe prev->state before clearing prev->on_cpu (in
	 * finish_lock_switch), otherwise a concurrent wakeup can get prev
	 * running on another CPU and we could rave with its RUNNING -> DEAD
	 * transition, resulting in a double drop.
	 */
	prev_state = prev->state;
	vtime_task_switch(prev);
	perf_event_task_sched_in(prev, current);
	finish_lock_switch(rq, prev);
	finish_arch_post_lock_switch();

	fire_sched_in_preempt_notifiers(current);
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
	return rq;
}

/**
 * schedule_tail - first thing a freshly forked thread must call.
 * @prev: the thread we just switched away from.
 */
asmlinkage __visible void schedule_tail(struct task_struct *prev)
	__releases(rq->lock)
{
	struct rq *rq;

	/*
	 * New tasks start with FORK_PREEMPT_COUNT, see there and
	 * finish_task_switch() for details.
	 *
	 * finish_task_switch() will drop rq->lock() and lower preempt_count
	 * and the preempt_enable() will end up enabling preemption (on
	 * PREEMPT_COUNT kernels).
	 */

	rq = finish_task_switch(prev);
	preempt_enable();

	if (current->set_child_tid)
		put_user(task_pid_vnr(current), current->set_child_tid);
}

/*
 * context_switch - switch to the new MM and the new thread's register state.
 */
static __always_inline struct rq *
context_switch(struct rq *rq, struct task_struct *prev,
	       struct task_struct *next)
{
	struct mm_struct *mm, *oldmm;

	prepare_task_switch(rq, prev, next);

	mm = next->mm;
	oldmm = prev->active_mm;
	/*
	 * For paravirt, this is coupled with an exit in switch_to to
	 * combine the page table reload and the switch backend into
	 * one hypercall.
	 */
	arch_start_context_switch(prev);

	if (!mm) {
		next->active_mm = oldmm;
		atomic_inc(&oldmm->mm_count);
		enter_lazy_tlb(oldmm, next);
	} else
		switch_mm_irqs_off(oldmm, mm, next);

	if (!prev->mm) {
		prev->active_mm = NULL;
		rq->prev_mm = oldmm;
	}
	/*
	 * Since the runqueue lock will be released by the next
	 * task (which is an invalid locking op but in the case
	 * of the scheduler it's an obvious special-case), so we
	 * do an early lockdep release here:
	 */
	spin_release(&rq->lock.dep_map, 1, _THIS_IP_);

	/* Here we just switch the register state and the stack. */
	switch_to(prev, next, prev);
	barrier();

	return finish_task_switch(prev);
}

/*
 * nr_running, nr_uninterruptible and nr_context_switches:
 *
 * externally visible scheduler statistics: current number of runnable
 * threads, total number of context switches performed since bootup.
 */
unsigned long nr_running(void)
{
	return atomic_read(&grq.nr_running);
}

static unsigned long nr_uninterruptible(void)
{
	return atomic_read(&grq.nr_uninterruptible);
}

/*
 * Check if only the current task is running on the cpu.
 *
 * Caution: this function does not check that the caller has disabled
 * preemption, thus the result might have a time-of-check-to-time-of-use
 * race.  The caller is responsible to use it correctly, for example:
 *
 * - from a non-preemptable section (of course)
 *
 * - from a thread that is bound to a single CPU
 *
 * - in a loop with very short iterations (e.g. a polling loop)
 */
bool single_task_running(void)
{
	struct rq *rq = cpu_rq(smp_processor_id());

	if (rq_load(rq) == 1)
		return true;
	else
		return false;
}
EXPORT_SYMBOL(single_task_running);

unsigned long long nr_context_switches(void)
{
	return (unsigned long long)atomic64_read(&grq.nr_switches);
}

unsigned long nr_iowait(void)
{
	unsigned long i, sum = 0;

	for_each_possible_cpu(i)
		sum += atomic_read(&cpu_rq(i)->nr_iowait);

	return sum;
}

unsigned long nr_iowait_cpu(int cpu)
{
	struct rq *this = cpu_rq(cpu);
	return atomic_read(&this->nr_iowait);
}

unsigned long nr_active(void)
{
	return nr_running() + nr_uninterruptible();
}

/*
 * I/O wait is the number of running or queued tasks with their ->rq pointer
 * set to this cpu as being the CPU they're more likely to run on.
 */
void get_iowait_load(unsigned long *nr_waiters, unsigned long *load)
{
	struct rq *rq = this_rq();

	*nr_waiters = atomic_read(&rq->nr_iowait);
	*load = rq_load(rq);
}

/* Variables and functions for calc_load */
static unsigned long calc_load_update;
unsigned long avenrun[3];
EXPORT_SYMBOL(avenrun);

/**
 * get_avenrun - get the load average array
 * @loads:	pointer to dest load array
 * @offset:	offset to add
 * @shift:	shift count to shift the result left
 *
 * These values are estimates at best, so no need for locking.
 */
void get_avenrun(unsigned long *loads, unsigned long offset, int shift)
{
	loads[0] = (avenrun[0] + offset) << shift;
	loads[1] = (avenrun[1] + offset) << shift;
	loads[2] = (avenrun[2] + offset) << shift;
}

static unsigned long
calc_load(unsigned long load, unsigned long exp, unsigned long active)
{
	unsigned long newload;

	newload = load * exp + active * (FIXED_1 - exp);
	if (active >= load)
		newload += FIXED_1-1;

	return newload / FIXED_1;
}

/*
 * calc_load - update the avenrun load estimates every LOAD_FREQ seconds.
 */
void calc_global_load(unsigned long ticks)
{
	long active;

	if (time_before(jiffies, calc_load_update))
		return;
	active = nr_active() * FIXED_1;

	avenrun[0] = calc_load(avenrun[0], EXP_1, active);
	avenrun[1] = calc_load(avenrun[1], EXP_5, active);
	avenrun[2] = calc_load(avenrun[2], EXP_15, active);

	calc_load_update = jiffies + LOAD_FREQ;
}

DEFINE_PER_CPU(struct kernel_stat, kstat);
DEFINE_PER_CPU(struct kernel_cpustat, kernel_cpustat);

EXPORT_PER_CPU_SYMBOL(kstat);
EXPORT_PER_CPU_SYMBOL(kernel_cpustat);

#ifdef CONFIG_IRQ_TIME_ACCOUNTING

/*
 * There are no locks covering percpu hardirq/softirq time.
 * They are only modified in account_system_vtime, on corresponding CPU
 * with interrupts disabled. So, writes are safe.
 * They are read and saved off onto struct rq in update_rq_clock().
 * This may result in other CPU reading this CPU's irq time and can
 * race with irq/account_system_vtime on this CPU. We would either get old
 * or new value with a side effect of accounting a slice of irq time to wrong
 * task when irq is in progress while we read rq->clock. That is a worthy
 * compromise in place of having locks on each irq in account_system_time.
 */
static DEFINE_PER_CPU(u64, cpu_hardirq_time);
static DEFINE_PER_CPU(u64, cpu_softirq_time);

static DEFINE_PER_CPU(u64, irq_start_time);
static int sched_clock_irqtime;

void enable_sched_clock_irqtime(void)
{
	sched_clock_irqtime = 1;
}

void disable_sched_clock_irqtime(void)
{
	sched_clock_irqtime = 0;
}

#ifndef CONFIG_64BIT
static DEFINE_PER_CPU(seqcount_t, irq_time_seq);

static inline void irq_time_write_begin(void)
{
	__this_cpu_inc(irq_time_seq.sequence);
	smp_wmb();
}

static inline void irq_time_write_end(void)
{
	smp_wmb();
	__this_cpu_inc(irq_time_seq.sequence);
}

static inline u64 irq_time_read(int cpu)
{
	u64 irq_time;
	unsigned seq;

	do {
		seq = read_seqcount_begin(&per_cpu(irq_time_seq, cpu));
		irq_time = per_cpu(cpu_softirq_time, cpu) +
			   per_cpu(cpu_hardirq_time, cpu);
	} while (read_seqcount_retry(&per_cpu(irq_time_seq, cpu), seq));

	return irq_time;
}
#else /* CONFIG_64BIT */
static inline void irq_time_write_begin(void)
{
}

static inline void irq_time_write_end(void)
{
}

static inline u64 irq_time_read(int cpu)
{
	return per_cpu(cpu_softirq_time, cpu) + per_cpu(cpu_hardirq_time, cpu);
}
#endif /* CONFIG_64BIT */

/*
 * Called before incrementing preempt_count on {soft,}irq_enter
 * and before decrementing preempt_count on {soft,}irq_exit.
 */
void irqtime_account_irq(struct task_struct *curr)
{
	unsigned long flags;
	s64 delta;
	int cpu;

	if (!sched_clock_irqtime)
		return;

	local_irq_save(flags);

	cpu = smp_processor_id();
	delta = sched_clock_cpu(cpu) - __this_cpu_read(irq_start_time);
	__this_cpu_add(irq_start_time, delta);

	irq_time_write_begin();
	/*
	 * We do not account for softirq time from ksoftirqd here.
	 * We want to continue accounting softirq time to ksoftirqd thread
	 * in that case, so as not to confuse scheduler with a special task
	 * that do not consume any time, but still wants to run.
	 */
	if (hardirq_count())
		__this_cpu_add(cpu_hardirq_time, delta);
	else if (in_serving_softirq() && curr != this_cpu_ksoftirqd())
		__this_cpu_add(cpu_softirq_time, delta);

	irq_time_write_end();
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(irqtime_account_irq);

#endif /* CONFIG_IRQ_TIME_ACCOUNTING */

#ifdef CONFIG_PARAVIRT
static inline u64 steal_ticks(u64 steal)
{
	if (unlikely(steal > NSEC_PER_SEC))
		return div_u64(steal, TICK_NSEC);

	return __iter_div_u64_rem(steal, TICK_NSEC, &steal);
}
#endif

static void update_rq_clock_task(struct rq *rq, s64 delta)
{
/*
 * In theory, the compile should just see 0 here, and optimize out the call
 * to sched_rt_avg_update. But I don't trust it...
 */
#ifdef CONFIG_IRQ_TIME_ACCOUNTING
	s64 irq_delta = irq_time_read(cpu_of(rq)) - rq->prev_irq_time;

	/*
	 * Since irq_time is only updated on {soft,}irq_exit, we might run into
	 * this case when a previous update_rq_clock() happened inside a
	 * {soft,}irq region.
	 *
	 * When this happens, we stop ->clock_task and only update the
	 * prev_irq_time stamp to account for the part that fit, so that a next
	 * update will consume the rest. This ensures ->clock_task is
	 * monotonic.
	 *
	 * It does however cause some slight miss-attribution of {soft,}irq
	 * time, a more accurate solution would be to update the irq_time using
	 * the current rq->clock timestamp, except that would require using
	 * atomic ops.
	 */
	if (irq_delta > delta)
		irq_delta = delta;

	rq->prev_irq_time += irq_delta;
	delta -= irq_delta;
#endif
#ifdef CONFIG_PARAVIRT_TIME_ACCOUNTING
	if (static_key_false((&paravirt_steal_rq_enabled))) {
		s64 steal = paravirt_steal_clock(cpu_of(rq));

		steal -= rq->prev_steal_time_rq;

		if (unlikely(steal > delta))
			steal = delta;

		rq->prev_steal_time_rq += steal;

		delta -= steal;
	}
#endif

	rq->clock_task += delta;
}

#ifndef nsecs_to_cputime
# define nsecs_to_cputime(__nsecs)	nsecs_to_jiffies(__nsecs)
#endif

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
static void irqtime_account_hi_si(void)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;
	u64 latest_ns;

	latest_ns = nsecs_to_cputime64(this_cpu_read(cpu_hardirq_time));
	if (latest_ns > cpustat[CPUTIME_IRQ])
		cpustat[CPUTIME_IRQ] += (__force u64)cputime_one_jiffy;

	latest_ns = nsecs_to_cputime64(this_cpu_read(cpu_softirq_time));
	if (latest_ns > cpustat[CPUTIME_SOFTIRQ])
		cpustat[CPUTIME_SOFTIRQ] += (__force u64)cputime_one_jiffy;
}
#else /* CONFIG_IRQ_TIME_ACCOUNTING */

#define sched_clock_irqtime	(0)

static inline void irqtime_account_hi_si(void)
{
}
#endif /* CONFIG_IRQ_TIME_ACCOUNTING */

static __always_inline bool steal_account_process_tick(void)
{
#ifdef CONFIG_PARAVIRT
	if (static_key_false(&paravirt_steal_enabled)) {
		u64 steal;
		cputime_t steal_ct;

		steal = paravirt_steal_clock(smp_processor_id());
		steal -= this_rq()->prev_steal_time;

		/*
		 * cputime_t may be less precise than nsecs (eg: if it's
		 * based on jiffies). Lets cast the result to cputime
		 * granularity and account the rest on the next rounds.
		 */
		steal_ct = nsecs_to_cputime(steal);
		this_rq()->prev_steal_time += cputime_to_nsecs(steal_ct);

		account_steal_time(steal_ct);
		return steal_ct;
	}
#endif
	return false;
}

/*
 * Accumulate raw cputime values of dead tasks (sig->[us]time) and live
 * tasks (sum on group iteration) belonging to @tsk's group.
 */
void thread_group_cputime(struct task_struct *tsk, struct task_cputime *times)
{
	struct signal_struct *sig = tsk->signal;
	cputime_t utime, stime;
	struct task_struct *t;
	unsigned int seq, nextseq;
	unsigned long flags;

	rcu_read_lock();
	/* Attempt a lockless read on the first round. */
	nextseq = 0;
	do {
		seq = nextseq;
		flags = read_seqbegin_or_lock_irqsave(&sig->stats_lock, &seq);
		times->utime = sig->utime;
		times->stime = sig->stime;
		times->sum_exec_runtime = sig->sum_sched_runtime;

		for_each_thread(tsk, t) {
			task_cputime(t, &utime, &stime);
			times->utime += utime;
			times->stime += stime;
			times->sum_exec_runtime += task_sched_runtime(t);
		}
		/* If lockless access failed, take the lock. */
		nextseq = 1;
	} while (need_seqretry(&sig->stats_lock, seq));
	done_seqretry_irqrestore(&sig->stats_lock, seq, flags);
	rcu_read_unlock();
}

/*
 * On each tick, see what percentage of that tick was attributed to each
 * component and add the percentage to the _pc values. Once a _pc value has
 * accumulated one tick's worth, account for that. This means the total
 * percentage of load components will always be 128 (pseudo 100) per tick.
 */
static void pc_idle_time(struct rq *rq, struct task_struct *idle, unsigned long pc)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;

	if (atomic_read(&rq->nr_iowait) > 0) {
		rq->iowait_pc += pc;
		if (rq->iowait_pc >= 128) {
			cpustat[CPUTIME_IOWAIT] += (__force u64)cputime_one_jiffy * rq->iowait_pc / 128;
			rq->iowait_pc %= 128;
		}
	} else {
		rq->idle_pc += pc;
		if (rq->idle_pc >= 128) {
			cpustat[CPUTIME_IDLE] += (__force u64)cputime_one_jiffy * rq->idle_pc / 128;
			rq->idle_pc %= 128;
		}
	}
	acct_update_integrals(idle);
}

static void
pc_system_time(struct rq *rq, struct task_struct *p, int hardirq_offset,
	       unsigned long pc, unsigned long ns)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;
	cputime_t one_jiffy_scaled = cputime_to_scaled(cputime_one_jiffy);

	p->stime_pc += pc;
	if (p->stime_pc >= 128) {
		int jiffs = p->stime_pc / 128;

		p->stime_pc %= 128;
		p->stime += (__force u64)cputime_one_jiffy * jiffs;
		p->stimescaled += one_jiffy_scaled * jiffs;
		account_group_system_time(p, cputime_one_jiffy * jiffs);
	}
	p->sched_time += ns;
	account_group_exec_runtime(p, ns);

	if (hardirq_count() - hardirq_offset) {
		rq->irq_pc += pc;
		if (rq->irq_pc >= 128) {
			cpustat[CPUTIME_IRQ] += (__force u64)cputime_one_jiffy * rq->irq_pc / 128;
			rq->irq_pc %= 128;
		}
	} else if (in_serving_softirq()) {
		rq->softirq_pc += pc;
		if (rq->softirq_pc >= 128) {
			cpustat[CPUTIME_SOFTIRQ] += (__force u64)cputime_one_jiffy * rq->softirq_pc / 128;
			rq->softirq_pc %= 128;
		}
	} else {
		rq->system_pc += pc;
		if (rq->system_pc >= 128) {
			cpustat[CPUTIME_SYSTEM] += (__force u64)cputime_one_jiffy * rq->system_pc / 128;
			rq->system_pc %= 128;
		}
	}
	acct_update_integrals(p);
}

static void pc_user_time(struct rq *rq, struct task_struct *p,
			 unsigned long pc, unsigned long ns)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;
	cputime_t one_jiffy_scaled = cputime_to_scaled(cputime_one_jiffy);

	p->utime_pc += pc;
	if (p->utime_pc >= 128) {
		int jiffs = p->utime_pc / 128;

		p->utime_pc %= 128;
		p->utime += (__force u64)cputime_one_jiffy * jiffs;
		p->utimescaled += one_jiffy_scaled * jiffs;
		account_group_user_time(p, cputime_one_jiffy * jiffs);
	}
	p->sched_time += ns;
	account_group_exec_runtime(p, ns);

	if (this_cpu_ksoftirqd() == p) {
		/*
		 * ksoftirqd time do not get accounted in cpu_softirq_time.
		 * So, we have to handle it separately here.
		 */
		rq->softirq_pc += pc;
		if (rq->softirq_pc >= 128) {
			cpustat[CPUTIME_SOFTIRQ] += (__force u64)cputime_one_jiffy * rq->softirq_pc / 128;
			rq->softirq_pc %= 128;
		}
	}

	if (task_nice(p) > 0 || idleprio_task(p)) {
		rq->nice_pc += pc;
		if (rq->nice_pc >= 128) {
			cpustat[CPUTIME_NICE] += (__force u64)cputime_one_jiffy * rq->nice_pc / 128;
			rq->nice_pc %= 128;
		}
	} else {
		rq->user_pc += pc;
		if (rq->user_pc >= 128) {
			cpustat[CPUTIME_USER] += (__force u64)cputime_one_jiffy * rq->user_pc / 128;
			rq->user_pc %= 128;
		}
	}
	acct_update_integrals(p);
}

/*
 * Convert nanoseconds to pseudo percentage of one tick. Use 128 for fast
 * shifts instead of 100
 */
#define NS_TO_PC(NS)	(NS * 128 / JIFFY_NS)

/*
 * This is called on clock ticks.
 * Bank in p->sched_time the ns elapsed since the last tick or switch.
 * CPU scheduler quota accounting is also performed here in microseconds.
 */
static void
update_cpu_clock_tick(struct rq *rq, struct task_struct *p)
{
	long account_ns = rq->clock_task - p->last_ran;
	struct task_struct *idle = rq->idle;
	unsigned long account_pc;

	if (unlikely(account_ns < 0) || steal_account_process_tick())
		goto ts_account;

	account_pc = NS_TO_PC(account_ns);

	/* Accurate tick timekeeping */
	if (user_mode(get_irq_regs()))
		pc_user_time(rq, p, account_pc, account_ns);
	else if (p != idle || (irq_count() != HARDIRQ_OFFSET))
		pc_system_time(rq, p, HARDIRQ_OFFSET,
			       account_pc, account_ns);
	else
		pc_idle_time(rq, idle, account_pc);

	if (sched_clock_irqtime)
		irqtime_account_hi_si();

ts_account:
	/* time_slice accounting is done in usecs to avoid overflow on 32bit */
	if (p->policy != SCHED_FIFO && p != idle) {
		s64 time_diff = rq->clock - rq->timekeep_clock;

		niffy_diff(&time_diff, 1);
		p->time_slice -= NS_TO_US(time_diff);
	}

	p->last_ran = rq->clock_task;
	rq->timekeep_clock = rq->clock;
}

/*
 * This is called on context switches.
 * Bank in p->sched_time the ns elapsed since the last tick or switch.
 * CPU scheduler quota accounting is also performed here in microseconds.
 */
static void
update_cpu_clock_switch(struct rq *rq, struct task_struct *p)
{
	long account_ns = rq->clock_task - p->last_ran;
	struct task_struct *idle = rq->idle;
	unsigned long account_pc;

	if (unlikely(account_ns < 0))
		goto ts_account;

	account_pc = NS_TO_PC(account_ns);

	/* Accurate subtick timekeeping */
	if (p != idle) {
		pc_user_time(rq, p, account_pc, account_ns);
	}
	else
		pc_idle_time(rq, idle, account_pc);

ts_account:
	/* time_slice accounting is done in usecs to avoid overflow on 32bit */
	if (p->policy != SCHED_FIFO && p != idle) {
		s64 time_diff = rq->clock - rq->timekeep_clock;

		niffy_diff(&time_diff, 1);
		p->time_slice -= NS_TO_US(time_diff);
	}

	p->last_ran = rq->clock_task;
	rq->timekeep_clock = rq->clock;
}

/*
 * Return any ns on the sched_clock that have not yet been accounted in
 * @p in case that task is currently running.
 *
 * Called with task_rq_lock(p) held.
 */
static inline u64 do_task_delta_exec(struct task_struct *p, struct rq *rq)
{
	u64 ns = 0;

	/*
	 * Must be ->curr _and_ ->on_rq.  If dequeued, we would
	 * project cycles that may never be accounted to this
	 * thread, breaking clock_gettime().
	 */
	if (p == rq->curr && task_on_rq_queued(p)) {
		update_rq_clock(rq);
		ns = rq->clock_task - p->last_ran;
		if (unlikely((s64)ns < 0))
			ns = 0;
	}

	return ns;
}

/*
 * Return accounted runtime for the task.
 * Return separately the current's pending runtime that have not been
 * accounted yet.
 *
 */
unsigned long long task_sched_runtime(struct task_struct *p)
{
	unsigned long flags;
	struct rq *rq;
	u64 ns;

#if defined(CONFIG_64BIT) && defined(CONFIG_SMP)
	/*
	 * 64-bit doesn't need locks to atomically read a 64bit value.
	 * So we have a optimization chance when the task's delta_exec is 0.
	 * Reading ->on_cpu is racy, but this is ok.
	 *
	 * If we race with it leaving cpu, we'll take a lock. So we're correct.
	 * If we race with it entering cpu, unaccounted time is 0. This is
	 * indistinguishable from the read occurring a few cycles earlier.
	 * If we see ->on_cpu without ->on_rq, the task is leaving, and has
	 * been accounted, so we're correct here as well.
	 */
	if (!p->on_cpu || !task_on_rq_queued(p))
		return tsk_seruntime(p);
#endif

	rq = task_rq_lock(p, &flags);
	ns = p->sched_time + do_task_delta_exec(p, rq);
	task_rq_unlock(rq, p, &flags);

	return ns;
}

/* Compatibility crap */
void account_user_time(struct task_struct *p, cputime_t cputime,
		       cputime_t cputime_scaled)
{
}

void account_idle_time(cputime_t cputime)
{
}

/*
 * Account guest cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in virtual machine since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 */
static void account_guest_time(struct task_struct *p, cputime_t cputime,
			       cputime_t cputime_scaled)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;

	/* Add guest time to process. */
	p->utime += (__force u64)cputime;
	p->utimescaled += (__force u64)cputime_scaled;
	account_group_user_time(p, cputime);
	p->gtime += (__force u64)cputime;

	/* Add guest time to cpustat. */
	if (task_nice(p) > 0) {
		cpustat[CPUTIME_NICE] += (__force u64)cputime;
		cpustat[CPUTIME_GUEST_NICE] += (__force u64)cputime;
	} else {
		cpustat[CPUTIME_USER] += (__force u64)cputime;
		cpustat[CPUTIME_GUEST] += (__force u64)cputime;
	}
}

/*
 * Account system cpu time to a process and desired cpustat field
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in kernel space since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 * @target_cputime64: pointer to cpustat field that has to be updated
 */
static inline
void __account_system_time(struct task_struct *p, cputime_t cputime,
			cputime_t cputime_scaled, cputime64_t *target_cputime64)
{
	/* Add system time to process. */
	p->stime += (__force u64)cputime;
	p->stimescaled += (__force u64)cputime_scaled;
	account_group_system_time(p, cputime);

	/* Add system time to cpustat. */
	*target_cputime64 += (__force u64)cputime;

	/* Account for system time used */
	acct_update_integrals(p);
}

/*
 * Account system cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @hardirq_offset: the offset to subtract from hardirq_count()
 * @cputime: the cpu time spent in kernel space since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 * This is for guest only now.
 */
void account_system_time(struct task_struct *p, int hardirq_offset,
			 cputime_t cputime, cputime_t cputime_scaled)
{

	if ((p->flags & PF_VCPU) && (irq_count() - hardirq_offset == 0))
		account_guest_time(p, cputime, cputime_scaled);
}

/*
 * Account for involuntary wait time.
 * @steal: the cpu time spent in involuntary wait
 */
void account_steal_time(cputime_t cputime)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;

	cpustat[CPUTIME_STEAL] += (__force u64)cputime;
}

/*
 * Account for idle time.
 * @cputime: the cpu time spent in idle wait
 */
static void account_idle_times(cputime_t cputime)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;
	struct rq *rq = this_rq();

	if (atomic_read(&rq->nr_iowait) > 0)
		cpustat[CPUTIME_IOWAIT] += (__force u64)cputime;
	else
		cpustat[CPUTIME_IDLE] += (__force u64)cputime;
}

#ifndef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE

void account_process_tick(struct task_struct *p, int user_tick)
{
}

/*
 * Account multiple ticks of steal time.
 * @p: the process from which the cpu time has been stolen
 * @ticks: number of stolen ticks
 */
void account_steal_ticks(unsigned long ticks)
{
	account_steal_time(jiffies_to_cputime(ticks));
}

/*
 * Account multiple ticks of idle time.
 * @ticks: number of stolen ticks
 */
void account_idle_ticks(unsigned long ticks)
{
	account_idle_times(jiffies_to_cputime(ticks));
}
#endif

/*
 * Functions to test for when SCHED_ISO tasks have used their allocated
 * quota as real time scheduling and convert them back to SCHED_NORMAL. All
 * data is modified only by the local runqueue during scheduler_tick with
 * interrupts disabled.
 */

/*
 * Test if SCHED_ISO tasks have run longer than their alloted period as RT
 * tasks and set the refractory flag if necessary. There is 10% hysteresis
 * for unsetting the flag. 115/128 is ~90/100 as a fast shift instead of a
 * slow division.
 */
static inline void iso_tick(struct rq *rq)
{
	rq->iso_ticks = rq->iso_ticks * (ISO_PERIOD - 1) / ISO_PERIOD;
	rq->iso_ticks += 100;
	if (rq->iso_ticks > ISO_PERIOD * sched_iso_cpu) {
		rq->iso_refractory = true;
		if (unlikely(rq->iso_ticks > ISO_PERIOD * 100))
			rq->iso_ticks = ISO_PERIOD * 100;
	}
}

/* No SCHED_ISO task was running so decrease rq->iso_ticks */
static inline void no_iso_tick(struct rq *rq, int ticks)
{
	if (rq->iso_ticks > 0 || rq->iso_refractory) {
		rq->iso_ticks = rq->iso_ticks * (ISO_PERIOD - ticks) / ISO_PERIOD;
		if (rq->iso_ticks < ISO_PERIOD * (sched_iso_cpu * 115 / 128)) {
			rq->iso_refractory = false;
			if (unlikely(rq->iso_ticks < 0))
				rq->iso_ticks = 0;
		}
	}
}

/* This manages tasks that have run out of timeslice during a scheduler_tick */
static void task_running_tick(struct rq *rq)
{
	struct task_struct *p = rq->curr;

	/*
	 * If a SCHED_ISO task is running we increment the iso_ticks. In
	 * order to prevent SCHED_ISO tasks from causing starvation in the
	 * presence of true RT tasks we account those as iso_ticks as well.
	 */
	if (rt_task(p) || task_running_iso(p))
		iso_tick(rq);
	else
		no_iso_tick(rq, 1);

	/* SCHED_FIFO tasks never run out of timeslice. */
	if (p->policy == SCHED_FIFO)
		return;

	if (iso_task(p)) {
		if (task_running_iso(p)) {
			if (rq->iso_refractory) {
				/*
				 * SCHED_ISO task is running as RT and limit
				 * has been hit. Force it to reschedule as
				 * SCHED_NORMAL by zeroing its time_slice
				 */
				p->time_slice = 0;
			}
		} else if (!rq->iso_refractory) {
			/* Can now run again ISO. Reschedule to pick up prio */
			goto out_resched;
		}
	}

	/*
	 * Tasks that were scheduled in the first half of a tick are not
	 * allowed to run into the 2nd half of the next tick if they will
	 * run out of time slice in the interim. Otherwise, if they have
	 * less than RESCHED_US μs of time slice left they will be rescheduled.
	 */
	if (p->time_slice - rq->dither >= RESCHED_US)
		return;
out_resched:
	rq_lock(rq);
	__set_tsk_resched(p);
	rq_unlock(rq);
}

/*
 * This function gets called by the timer code, with HZ frequency.
 * We call it with interrupts disabled.
 */
void scheduler_tick(void)
{
	int cpu __maybe_unused = smp_processor_id();
	struct rq *rq = cpu_rq(cpu);

	sched_clock_tick();
	update_rq_clock(rq);
	update_load_avg(rq);
	update_cpu_clock_tick(rq, rq->curr);
	if (!rq_idle(rq))
		task_running_tick(rq);
	else
		no_iso_tick(rq, rq->last_scheduler_tick - rq->last_jiffy);
	rq->last_scheduler_tick = rq->last_jiffy;
	rq->last_tick = rq->clock;
	perf_event_task_tick();
}

#if defined(CONFIG_PREEMPT) && (defined(CONFIG_DEBUG_PREEMPT) || \
				defined(CONFIG_PREEMPT_TRACER))
/*
 * If the value passed in is equal to the current preempt count
 * then we just disabled preemption. Start timing the latency.
 */
static inline void preempt_latency_start(int val)
{
	if (preempt_count() == val) {
		unsigned long ip = get_lock_parent_ip();
#ifdef CONFIG_DEBUG_PREEMPT
		current->preempt_disable_ip = ip;
#endif
		trace_preempt_off(CALLER_ADDR0, ip);
	}
}

void preempt_count_add(int val)
{
#ifdef CONFIG_DEBUG_PREEMPT
	/*
	 * Underflow?
	 */
	if (DEBUG_LOCKS_WARN_ON((preempt_count() < 0)))
		return;
#endif
	__preempt_count_add(val);
#ifdef CONFIG_DEBUG_PREEMPT
	/*
	 * Spinlock count overflowing soon?
	 */
	DEBUG_LOCKS_WARN_ON((preempt_count() & PREEMPT_MASK) >=
				PREEMPT_MASK - 10);
#endif
	preempt_latency_start(val);
}
EXPORT_SYMBOL(preempt_count_add);
NOKPROBE_SYMBOL(preempt_count_add);

/*
 * If the value passed in equals to the current preempt count
 * then we just enabled preemption. Stop timing the latency.
 */
static inline void preempt_latency_stop(int val)
{
	if (preempt_count() == val)
		trace_preempt_on(CALLER_ADDR0, get_lock_parent_ip());
}

void preempt_count_sub(int val)
{
#ifdef CONFIG_DEBUG_PREEMPT
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
#endif

	preempt_latency_stop(val);
	__preempt_count_sub(val);
}
EXPORT_SYMBOL(preempt_count_sub);
NOKPROBE_SYMBOL(preempt_count_sub);

#else
static inline void preempt_latency_start(int val) { }
static inline void preempt_latency_stop(int val) { }
#endif

/*
 * The time_slice is only refilled when it is empty and that is when we set a
 * new deadline. Make sure update_clocks has been called recently to update
 * rq->niffies.
 */
static void time_slice_expired(struct task_struct *p, struct rq *rq)
{
	p->time_slice = timeslice();
	p->deadline = rq->niffies + task_deadline_diff(p);
#ifdef CONFIG_SMT_NICE
	if (!p->mm)
		p->smt_bias = 0;
	else if (rt_task(p))
		p->smt_bias = 1 << 30;
	else if (task_running_iso(p))
		p->smt_bias = 1 << 29;
	else if (idleprio_task(p)) {
		if (task_running_idle(p))
			p->smt_bias = 0;
		else
			p->smt_bias = 1;
	} else if (--p->smt_bias < 1)
		p->smt_bias = MAX_PRIO - p->static_prio;
#endif
}

/*
 * Timeslices below RESCHED_US are considered as good as expired as there's no
 * point rescheduling when there's so little time left. SCHED_BATCH tasks
 * have been flagged be not latency sensitive and likely to be fully CPU
 * bound so every time they're rescheduled they have their time_slice
 * refilled, but get a new later deadline to have little effect on
 * SCHED_NORMAL tasks.

 */
static inline void check_deadline(struct task_struct *p, struct rq *rq)
{
	if (p->time_slice < RESCHED_US || batch_task(p))
		time_slice_expired(p, rq);
}

#define BITOP_WORD(nr)		((nr) / BITS_PER_LONG)

/*
 * Task selection with skiplists is a simple matter of picking off the first
 * task in the sorted list, an O(1) operation. The lookup is amortised O(1)
 * being bound to the number of processors.
 *
 * Runqueues are selectively locked based on their unlocked data and then
 * unlocked if not needed. At most 3 locks will be held at any time and are
 * released as soon as they're no longer needed. All balancing between CPUs
 * is thus done here in an extremely simple first come best fit manner.
 *
 * This iterates over runqueues in cache locality order. In interactive mode
 * it iterates over all CPUs and finds the task with the best key/deadline.
 * In non-interactive mode it will only take a task if it's from the current
 * runqueue or a runqueue with more tasks than the current one with a better
 * key/deadline.
 */
static inline struct
task_struct *earliest_deadline_task(struct rq *rq, int cpu, struct task_struct *idle)
{
	struct task_struct *edt = idle;
	struct rq *locked = NULL;
	int i, best_entries = 0;
	u64 best_key = ~0ULL;

	for (i = 0; i < num_possible_cpus(); i++) {
		struct rq *other_rq = rq_order(rq, i);
		int entries = other_rq->sl->entries;
		struct task_struct *p;
		u64 key;

		/*
		 * Check for queued entres lockless first. The local runqueue
		 * is locked so entries will always be accurate.
		 */
		if (!sched_interactive) {
			if (entries <= best_entries)
				continue;
		} else if (!entries)
			continue;

		/* if (i) implies other_rq != rq */
		if (i) {
			/* Check for best id queued lockless first */
			if (other_rq->best_key >= best_key)
				continue;

			if (unlikely(!trylock_rq(rq, other_rq)))
				continue;

			/* Need to reevaluate entries after locking */
			entries = other_rq->sl->entries;
			if (unlikely(!entries)) {
				unlock_rq(other_rq);
				continue;
			}
		}
		key = other_rq->node.next[0]->key;
		/* Reevaluate key after locking */
		if (unlikely(key >= best_key)) {
			if (i)
				unlock_rq(other_rq);
			continue;
		}

		p = other_rq->node.next[0]->value;
		if (!smt_schedule(p, rq)) {
			if (i)
				unlock_rq(other_rq);
			continue;
		}

		/* Make sure affinity is ok */
		if (i) {
			if (needs_other_cpu(p, cpu)) {
				unlock_rq(other_rq);
				continue;
			}
			if (locked)
				unlock_rq(locked);
			locked = other_rq;
		}

		best_entries = entries;
		best_key = key;
		edt = p;
	}

	if (likely(edt != idle))
		take_task(rq, cpu, edt);

	if (locked)
		unlock_rq(locked);

	return edt;
}

/*
 * Print scheduling while atomic bug:
 */
static noinline void __schedule_bug(struct task_struct *prev)
{
	if (oops_in_progress)
		return;

	printk(KERN_ERR "BUG: scheduling while atomic: %s/%d/0x%08x\n",
		prev->comm, prev->pid, preempt_count());

	debug_show_held_locks(prev);
	print_modules();
	if (irqs_disabled())
		print_irqtrace_events(prev);
#ifdef CONFIG_DEBUG_PREEMPT
	if (in_atomic_preempt_off()) {
		pr_err("Preemption disabled at:");
		print_ip_sym(current->preempt_disable_ip);
		pr_cont("\n");
	}
#endif
	dump_stack();
	add_taint(TAINT_WARN, LOCKDEP_STILL_OK);
}

/*
 * Various schedule()-time debugging checks and statistics:
 */
static inline void schedule_debug(struct task_struct *prev)
{
#ifdef CONFIG_SCHED_STACK_END_CHECK
	if (task_stack_end_corrupted(prev))
		panic("corrupted stack end detected inside scheduler\n");
#endif

	if (unlikely(in_atomic_preempt_off())) {
		__schedule_bug(prev);
		preempt_count_set(PREEMPT_DISABLED);
	}
	rcu_sleep_check();

	profile_hit(SCHED_PROFILING, __builtin_return_address(0));

	schedstat_inc(this_rq(), sched_count);
}

/*
 * The currently running task's information is all stored in rq local data
 * which is only modified by the local CPU.
 */
static inline void set_rq_task(struct rq *rq, struct task_struct *p)
{
	rq->rq_deadline = p->deadline;
	p->last_ran = rq->clock_task;
	rq->rq_prio = p->prio;
#ifdef CONFIG_SMT_NICE
	rq->rq_mm = p->mm;
	rq->rq_smt_bias = p->smt_bias;
#endif
}

static void reset_rq_task(struct rq *rq, struct task_struct *p)
{
	rq->rq_deadline = p->deadline;
	rq->rq_prio = p->prio;
#ifdef CONFIG_SMT_NICE
	rq->rq_smt_bias = p->smt_bias;
#endif
}

#ifdef CONFIG_SMT_NICE
static void check_no_siblings(struct rq __maybe_unused *this_rq) {}
static void wake_no_siblings(struct rq __maybe_unused *this_rq) {}
static void (*check_siblings)(struct rq *this_rq) = &check_no_siblings;
static void (*wake_siblings)(struct rq *this_rq) = &wake_no_siblings;

/* Iterate over smt siblings when we've scheduled a process on cpu and decide
 * whether they should continue running or be descheduled. */
static void check_smt_siblings(struct rq *this_rq)
{
	int other_cpu;

	for_each_cpu(other_cpu, &this_rq->thread_mask) {
		struct task_struct *p;
		struct rq *rq;

		rq = cpu_rq(other_cpu);
		if (rq_idle(rq))
			continue;
		if (unlikely(!rq->online))
			continue;
		p = rq->curr;
		if (!smt_schedule(p, this_rq)) {
			set_tsk_need_resched(p);
			smp_send_reschedule(other_cpu);
		}
	}
}

static void wake_smt_siblings(struct rq *this_rq)
{
	int other_cpu;

	if (!queued_notrunning())
		return;

	for_each_cpu(other_cpu, &this_rq->thread_mask) {
		struct rq *rq;

		rq = cpu_rq(other_cpu);
		if (unlikely(!rq->online))
			continue;
		if (rq_idle(rq)) {
			struct task_struct *p = rq->curr;

			set_tsk_need_resched(p);
			smp_send_reschedule(other_cpu);
		}
	}
}
#else
static void check_siblings(struct rq __maybe_unused *this_rq) {}
static void wake_siblings(struct rq __maybe_unused *this_rq) {}
#endif

/*
 * schedule() is the main scheduler function.
 *
 * The main means of driving the scheduler and thus entering this function are:
 *
 *   1. Explicit blocking: mutex, semaphore, waitqueue, etc.
 *
 *   2. TIF_NEED_RESCHED flag is checked on interrupt and userspace return
 *      paths. For example, see arch/x86/entry_64.S.
 *
 *      To drive preemption between tasks, the scheduler sets the flag in timer
 *      interrupt handler scheduler_tick().
 *
 *   3. Wakeups don't really cause entry into schedule(). They add a
 *      task to the run-queue and that's it.
 *
 *      Now, if the new task added to the run-queue preempts the current
 *      task, then the wakeup sets TIF_NEED_RESCHED and schedule() gets
 *      called on the nearest possible occasion:
 *
 *       - If the kernel is preemptible (CONFIG_PREEMPT=y):
 *
 *         - in syscall or exception context, at the next outmost
 *           preempt_enable(). (this might be as soon as the wake_up()'s
 *           spin_unlock()!)
 *
 *         - in IRQ context, return from interrupt-handler to
 *           preemptible context
 *
 *       - If the kernel is not preemptible (CONFIG_PREEMPT is not set)
 *         then at the next:
 *
 *          - cond_resched() call
 *          - explicit schedule() call
 *          - return from syscall or exception to user-space
 *          - return from interrupt-handler to user-space
 *
 * WARNING: must be called with preemption disabled!
 */
static void __sched notrace __schedule(bool preempt)
{
	struct task_struct *prev, *next, *idle;
	unsigned long *switch_count;
	bool deactivate = false;
	struct rq *rq;
	int cpu;

	cpu = smp_processor_id();
	rq = cpu_rq(cpu);
	prev = rq->curr;
	idle = rq->idle;

	/*
	 * do_exit() calls schedule() with preemption disabled as an exception;
	 * however we must fix that up, otherwise the next task will see an
	 * inconsistent (higher) preempt count.
	 *
	 * It also avoids the below schedule_debug() test from complaining
	 * about this.
	 */
	if (unlikely(prev->state == TASK_DEAD))
		preempt_enable_no_resched_notrace();

	schedule_debug(prev);

	local_irq_disable();
	rcu_note_context_switch();

	/*
	 * Make sure that signal_pending_state()->signal_pending() below
	 * can't be reordered with __set_current_state(TASK_INTERRUPTIBLE)
	 * done by the caller to avoid the race with signal_wake_up().
	 */
	smp_mb__before_spinlock();
	rq_lock(rq);
#ifdef CONFIG_SMP
	if (rq->preempt) {
		/*
		 * Make sure resched_curr hasn't triggered a preemption
		 * locklessly on a task that has since scheduled away. Spurious
		 * wakeup of idle is okay though.
		 */
		if (unlikely(preempt && prev != idle && !test_tsk_need_resched(prev))) {
			rq->preempt = NULL;
			clear_preempt_need_resched();
			rq_unlock_irq(rq);
			return;
		}
		rq->preempt = NULL;
	}
#endif

	switch_count = &prev->nivcsw;
	if (!preempt && prev->state) {
		if (unlikely(signal_pending_state(prev->state, prev))) {
			prev->state = TASK_RUNNING;
		} else {
			deactivate = true;
			prev->on_rq = 0;

			/*
			 * If a worker is going to sleep, notify and
			 * ask workqueue whether it wants to wake up a
			 * task to maintain concurrency.  If so, wake
			 * up the task.
			 */
			if (prev->flags & PF_WQ_WORKER) {
				struct task_struct *to_wakeup;

				to_wakeup = wq_worker_sleeping(prev);
				if (to_wakeup) {
					/* This shouldn't happen, but does */
					if (WARN_ONCE((to_wakeup == prev), "Waking up prev as worker\n"))
						deactivate = false;
					else
						try_to_wake_up_local(to_wakeup);
				}
			}
		}
		switch_count = &prev->nvcsw;
	}

	update_clocks(rq);
	update_cpu_clock_switch(rq, prev);
	if (rq->clock - rq->last_tick > HALF_JIFFY_NS)
		rq->dither = 0;
	else
		rq->dither = HALF_JIFFY_US;

	clear_tsk_need_resched(prev);
	clear_preempt_need_resched();

	if (idle != prev) {
		check_deadline(prev, rq);
		return_task(prev, rq, cpu, deactivate);
	}

	if (unlikely(!queued_notrunning())) {
		next = idle;
		schedstat_inc(rq, sched_goidle);
		set_cpuidle_map(cpu);
		update_load_avg(rq);
	} else {
		next = earliest_deadline_task(rq, cpu, idle);
		if (likely(next->prio != PRIO_LIMIT))
			clear_cpuidle_map(cpu);
		else {
			set_cpuidle_map(cpu);
			update_load_avg(rq);
		}
	}

	if (likely(prev != next)) {
		/*
		 * Don't reschedule an idle task or deactivated tasks
		 */
		if (prev != idle && !deactivate)
			resched_suitable_idle(prev);
		set_rq_task(rq, next);
		if (next != idle)
			check_siblings(rq);
		else
			wake_siblings(rq);
		atomic64_inc(&grq.nr_switches);
		rq->curr = next;
		++*switch_count;

		trace_sched_switch(preempt, prev, next);
		rq = context_switch(rq, prev, next); /* unlocks the rq */
	} else {
		check_siblings(rq);
		rq_unlock_irq(rq);
	}
}

static inline void sched_submit_work(struct task_struct *tsk)
{
	if (!tsk->state || tsk_is_pi_blocked(tsk) ||
	    preempt_count() ||
	    signal_pending_state(tsk->state, tsk))
		return;

	/*
	 * If we are going to sleep and we have plugged IO queued,
	 * make sure to submit it to avoid deadlocks.
	 */
	if (blk_needs_flush_plug(tsk))
		blk_schedule_flush_plug(tsk);
}

asmlinkage __visible void __sched schedule(void)
{
	struct task_struct *tsk = current;

	sched_submit_work(tsk);
	do {
		preempt_disable();
		__schedule(false);
		sched_preempt_enable_no_resched();
	} while (need_resched());
}

EXPORT_SYMBOL(schedule);

#ifdef CONFIG_CONTEXT_TRACKING
asmlinkage __visible void __sched schedule_user(void)
{
	/*
	 * If we come here after a random call to set_need_resched(),
	 * or we have been woken up remotely but the IPI has not yet arrived,
	 * we haven't yet exited the RCU idle mode. Do it here manually until
	 * we find a better solution.
	 *
	 * NB: There are buggy callers of this function.  Ideally we
	 * should warn if prev_state != IN_USER, but that will trigger
	 * too frequently to make sense yet.
	 */
	enum ctx_state prev_state = exception_enter();
	schedule();
	exception_exit(prev_state);
}
#endif

/**
 * schedule_preempt_disabled - called with preemption disabled
 *
 * Returns with preemption disabled. Note: preempt_count must be 1
 */
void __sched schedule_preempt_disabled(void)
{
	sched_preempt_enable_no_resched();
	schedule();
	preempt_disable();
}

static void __sched notrace preempt_schedule_common(void)
{
	do {
		/*
		 * Because the function tracer can trace preempt_count_sub()
		 * and it also uses preempt_enable/disable_notrace(), if
		 * NEED_RESCHED is set, the preempt_enable_notrace() called
		 * by the function tracer will call this function again and
		 * cause infinite recursion.
		 *
		 * Preemption must be disabled here before the function
		 * tracer can trace. Break up preempt_disable() into two
		 * calls. One to disable preemption without fear of being
		 * traced. The other to still record the preemption latency,
		 * which can also be traced by the function tracer.
		 */
		preempt_disable_notrace();
		preempt_latency_start(1);
		__schedule(true);
		preempt_latency_stop(1);
		preempt_enable_no_resched_notrace();

		/*
		 * Check again in case we missed a preemption opportunity
		 * between schedule and now.
		 */
	} while (need_resched());
}

#ifdef CONFIG_PREEMPT
/*
 * this is the entry point to schedule() from in-kernel preemption
 * off of preempt_enable. Kernel preemptions off return from interrupt
 * occur there and call schedule directly.
 */
asmlinkage __visible void __sched notrace preempt_schedule(void)
{
	/*
	 * If there is a non-zero preempt_count or interrupts are disabled,
	 * we do not want to preempt the current task. Just return..
	 */
	if (likely(!preemptible()))
		return;

	preempt_schedule_common();
}
NOKPROBE_SYMBOL(preempt_schedule);
EXPORT_SYMBOL(preempt_schedule);

/**
 * preempt_schedule_notrace - preempt_schedule called by tracing
 *
 * The tracing infrastructure uses preempt_enable_notrace to prevent
 * recursion and tracing preempt enabling caused by the tracing
 * infrastructure itself. But as tracing can happen in areas coming
 * from userspace or just about to enter userspace, a preempt enable
 * can occur before user_exit() is called. This will cause the scheduler
 * to be called when the system is still in usermode.
 *
 * To prevent this, the preempt_enable_notrace will use this function
 * instead of preempt_schedule() to exit user context if needed before
 * calling the scheduler.
 */
asmlinkage __visible void __sched notrace preempt_schedule_notrace(void)
{
	enum ctx_state prev_ctx;

	if (likely(!preemptible()))
		return;

	do {
		/*
		 * Because the function tracer can trace preempt_count_sub()
		 * and it also uses preempt_enable/disable_notrace(), if
		 * NEED_RESCHED is set, the preempt_enable_notrace() called
		 * by the function tracer will call this function again and
		 * cause infinite recursion.
		 *
		 * Preemption must be disabled here before the function
		 * tracer can trace. Break up preempt_disable() into two
		 * calls. One to disable preemption without fear of being
		 * traced. The other to still record the preemption latency,
		 * which can also be traced by the function tracer.
		 */
		preempt_disable_notrace();
		preempt_latency_start(1);
		/*
		 * Needs preempt disabled in case user_exit() is traced
		 * and the tracer calls preempt_enable_notrace() causing
		 * an infinite recursion.
		 */
		prev_ctx = exception_enter();
		__schedule(true);
		exception_exit(prev_ctx);

		preempt_latency_stop(1);
		preempt_enable_no_resched_notrace();
	} while (need_resched());
}
EXPORT_SYMBOL_GPL(preempt_schedule_notrace);

#endif /* CONFIG_PREEMPT */

/*
 * this is the entry point to schedule() from kernel preemption
 * off of irq context.
 * Note, that this is called and return with irqs disabled. This will
 * protect us against recursive calling from irq.
 */
asmlinkage __visible void __sched preempt_schedule_irq(void)
{
	enum ctx_state prev_state;

	/* Catch callers which need to be fixed */
	BUG_ON(preempt_count() || !irqs_disabled());

	prev_state = exception_enter();

	do {
		preempt_disable();
		local_irq_enable();
		__schedule(true);
		local_irq_disable();
		sched_preempt_enable_no_resched();
	} while (need_resched());

	exception_exit(prev_state);
}

int default_wake_function(wait_queue_t *curr, unsigned mode, int wake_flags,
			  void *key)
{
	return try_to_wake_up(curr->private, mode, wake_flags);
}
EXPORT_SYMBOL(default_wake_function);

#ifdef CONFIG_RT_MUTEXES

/*
 * rt_mutex_setprio - set the current priority of a task
 * @p: task
 * @prio: prio value (kernel-internal form)
 *
 * This function changes the 'effective' priority of a task. It does
 * not touch ->normal_prio like __setscheduler().
 *
 * Used by the rt_mutex code to implement priority inheritance
 * logic. Call site only calls if the priority of the task changed.
 */
void rt_mutex_setprio(struct task_struct *p, int prio)
{
	struct rq *rq;
	int oldprio;

	BUG_ON(prio < 0 || prio > MAX_PRIO);

	rq = __task_rq_lock(p);

	/*
	 * Idle task boosting is a nono in general. There is one
	 * exception, when PREEMPT_RT and NOHZ is active:
	 *
	 * The idle task calls get_next_timer_interrupt() and holds
	 * the timer wheel base->lock on the CPU and another CPU wants
	 * to access the timer (probably to cancel it). We can safely
	 * ignore the boosting request, as the idle CPU runs this code
	 * with interrupts disabled and will complete the lock
	 * protected section without being interrupted. So there is no
	 * real need to boost.
	 */
	if (unlikely(p == rq->idle)) {
		WARN_ON(p != rq->curr);
		WARN_ON(p->pi_blocked_on);
		goto out_unlock;
	}

	trace_sched_pi_setprio(p, prio);
	oldprio = p->prio;
	p->prio = prio;
	if (task_running(rq, p)){
		if (prio > oldprio)
			resched_task(p);
	} else if (task_queued(p)) {
		dequeue_task(rq, p, DEQUEUE_SAVE);
		enqueue_task(rq, p, ENQUEUE_RESTORE);
		if (prio < oldprio)
			try_preempt(p, rq);
	}
out_unlock:
	__task_rq_unlock(rq);
}

#endif

/*
 * Adjust the deadline for when the priority is to change, before it's
 * changed.
 */
static inline void adjust_deadline(struct task_struct *p, int new_prio)
{
	p->deadline += static_deadline_diff(new_prio) - task_deadline_diff(p);
}

void set_user_nice(struct task_struct *p, long nice)
{
	int new_static, old_static;
	unsigned long flags;
	struct rq *rq;

	if (task_nice(p) == nice || nice < MIN_NICE || nice > MAX_NICE)
		return;
	new_static = NICE_TO_PRIO(nice);
	/*
	 * We have to be careful, if called from sys_setpriority(),
	 * the task might be in the middle of scheduling on another CPU.
	 */
	rq = task_rq_lock(p, &flags);
	/*
	 * The RT priorities are set via sched_setscheduler(), but we still
	 * allow the 'normal' nice value to be set - but as expected
	 * it wont have any effect on scheduling until the task is
	 * not SCHED_NORMAL/SCHED_BATCH:
	 */
	if (has_rt_policy(p)) {
		p->static_prio = new_static;
		goto out_unlock;
	}

	adjust_deadline(p, new_static);
	old_static = p->static_prio;
	p->static_prio = new_static;
	p->prio = effective_prio(p);

	if (task_queued(p)) {
		dequeue_task(rq, p, DEQUEUE_SAVE);
		enqueue_task(rq, p, ENQUEUE_RESTORE);
		if (new_static < old_static)
			try_preempt(p, rq);
	} else if (task_running(rq, p)) {
		reset_rq_task(rq, p);
		if (old_static < new_static)
			resched_task(p);
	}
out_unlock:
	task_rq_unlock(rq, p, &flags);
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
	int nice_rlim = nice_to_rlimit(nice);

	return (nice_rlim <= task_rlimit(p, RLIMIT_NICE) ||
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
SYSCALL_DEFINE1(nice, int, increment)
{
	long nice, retval;

	/*
	 * Setpriority might change our priority at the same moment.
	 * We don't have to worry. Conceptually one call occurs first
	 * and we have a single winner.
	 */

	increment = clamp(increment, -NICE_WIDTH, NICE_WIDTH);
	nice = task_nice(current) + increment;

	nice = clamp_val(nice, MIN_NICE, MAX_NICE);
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
 * Return: The priority value as seen by users in /proc.
 * RT tasks are offset by -100. Normal tasks are centered around 1, value goes
 * from 0 (SCHED_ISO) up to 82 (nice +19 SCHED_IDLEPRIO).
 */
int task_prio(const struct task_struct *p)
{
	int delta, prio = p->prio - MAX_RT_PRIO;

	/* rt tasks and iso tasks */
	if (prio <= 0)
		goto out;

	/* Convert to ms to avoid overflows */
	delta = NS_TO_MS(p->deadline - task_rq(p)->niffies);
	delta = delta * 40 / ms_longest_deadline_diff();
	if (delta > 0 && delta <= 80)
		prio += delta;
	if (idleprio_task(p))
		prio += 40;
out:
	return prio;
}

/**
 * idle_cpu - is a given cpu idle currently?
 * @cpu: the processor in question.
 *
 * Return: 1 if the CPU is currently idle. 0 otherwise.
 */
int idle_cpu(int cpu)
{
	return cpu_curr(cpu) == cpu_rq(cpu)->idle;
}

/**
 * idle_task - return the idle task for a given cpu.
 * @cpu: the processor in question.
 *
 * Return: The idle task for the cpu @cpu.
 */
struct task_struct *idle_task(int cpu)
{
	return cpu_rq(cpu)->idle;
}

/**
 * find_process_by_pid - find a process with a matching PID value.
 * @pid: the pid in question.
 *
 * The task of @pid, if found. %NULL otherwise.
 */
static inline struct task_struct *find_process_by_pid(pid_t pid)
{
	return pid ? find_task_by_vpid(pid) : current;
}

/* Actually do priority change: must hold rq lock. */
static void __setscheduler(struct task_struct *p, struct rq *rq, int policy,
			   int prio, bool keep_boost)
{
	int oldrtprio, oldprio;

	p->policy = policy;
	oldrtprio = p->rt_priority;
	p->rt_priority = prio;
	p->normal_prio = normal_prio(p);
	oldprio = p->prio;
	/*
	 * Keep a potential priority boosting if called from
	 * sched_setscheduler().
	 */
	if (keep_boost) {
		/*
		 * Take priority boosted tasks into account. If the new
		 * effective priority is unchanged, we just store the new
		 * normal parameters and do not touch the scheduler class and
		 * the runqueue. This will be done when the task deboost
		 * itself.
		 */
		p->prio = rt_mutex_get_effective_prio(p, p->normal_prio);
	} else
		p->prio = p->normal_prio;

	if (task_running(rq, p)) {
		reset_rq_task(rq, p);
		resched_task(p);
	} else if (task_queued(p)) {
		dequeue_task(rq, p, DEQUEUE_SAVE);
		enqueue_task(rq, p, ENQUEUE_RESTORE);
		if (p->prio < oldprio || p->rt_priority > oldrtprio)
			try_preempt(p, rq);
	}
}

/*
 * check the target process has a UID that matches the current process's
 */
static bool check_same_owner(struct task_struct *p)
{
	const struct cred *cred = current_cred(), *pcred;
	bool match;

	rcu_read_lock();
	pcred = __task_cred(p);
	match = (uid_eq(cred->euid, pcred->euid) ||
		 uid_eq(cred->euid, pcred->uid));
	rcu_read_unlock();
	return match;
}

static int
__sched_setscheduler(struct task_struct *p, int policy,
		     const struct sched_param *param, bool user, bool pi)
{
	struct sched_param zero_param = { .sched_priority = 0 };
	unsigned long flags, rlim_rtprio = 0;
	int retval, oldpolicy = -1;
	int reset_on_fork;
	struct rq *rq;

	/* may grab non-irq protected spin_locks */
	BUG_ON(in_interrupt());

	if (is_rt_policy(policy) && !capable(CAP_SYS_NICE)) {
		unsigned long lflags;

		if (!lock_task_sighand(p, &lflags))
			return -ESRCH;
		rlim_rtprio = task_rlimit(p, RLIMIT_RTPRIO);
		unlock_task_sighand(p, &lflags);
		if (rlim_rtprio)
			goto recheck;
		/*
		 * If the caller requested an RT policy without having the
		 * necessary rights, we downgrade the policy to SCHED_ISO.
		 * We also set the parameter to zero to pass the checks.
		 */
		policy = SCHED_ISO;
		param = &zero_param;
	}
recheck:
	/* double check policy once rq lock held */
	if (policy < 0) {
		reset_on_fork = p->sched_reset_on_fork;
		policy = oldpolicy = p->policy;
	} else {
		reset_on_fork = !!(policy & SCHED_RESET_ON_FORK);
		policy &= ~SCHED_RESET_ON_FORK;

		if (!SCHED_RANGE(policy))
			return -EINVAL;
	}

	/*
	 * Valid priorities for SCHED_FIFO and SCHED_RR are
	 * 1..MAX_USER_RT_PRIO-1, valid priority for SCHED_NORMAL and
	 * SCHED_BATCH is 0.
	 */
	if (param->sched_priority < 0 ||
	    (p->mm && param->sched_priority > MAX_USER_RT_PRIO - 1) ||
	    (!p->mm && param->sched_priority > MAX_RT_PRIO - 1))
		return -EINVAL;
	if (is_rt_policy(policy) != (param->sched_priority != 0))
		return -EINVAL;

	/*
	 * Allow unprivileged RT tasks to decrease priority:
	 */
	if (user && !capable(CAP_SYS_NICE)) {
		if (is_rt_policy(policy)) {
			unsigned long rlim_rtprio =
					task_rlimit(p, RLIMIT_RTPRIO);

			/* can't set/change the rt policy */
			if (policy != p->policy && !rlim_rtprio)
				return -EPERM;

			/* can't increase priority */
			if (param->sched_priority > p->rt_priority &&
			    param->sched_priority > rlim_rtprio)
				return -EPERM;
		} else {
			switch (p->policy) {
				/*
				 * Can only downgrade policies but not back to
				 * SCHED_NORMAL
				 */
				case SCHED_ISO:
					if (policy == SCHED_ISO)
						goto out;
					if (policy != SCHED_NORMAL)
						return -EPERM;
					break;
				case SCHED_BATCH:
					if (policy == SCHED_BATCH)
						goto out;
					if (policy != SCHED_IDLEPRIO)
						return -EPERM;
					break;
				case SCHED_IDLEPRIO:
					if (policy == SCHED_IDLEPRIO)
						goto out;
					return -EPERM;
				default:
					break;
			}
		}

		/* can't change other user's priorities */
		if (!check_same_owner(p))
			return -EPERM;

		/* Normal users shall not reset the sched_reset_on_fork flag */
		if (p->sched_reset_on_fork && !reset_on_fork)
			return -EPERM;
	}

	if (user) {
		retval = security_task_setscheduler(p);
		if (retval)
			return retval;
	}

	/*
	 * make sure no PI-waiters arrive (or leave) while we are
	 * changing the priority of the task:
	 *
	 * To be able to change p->policy safely, the runqueue lock must be
	 * held.
	 */
	rq = task_rq_lock(p, &flags);

	/*
	 * Changing the policy of the stop threads its a very bad idea
	 */
	if (p == rq->stop) {
		task_rq_unlock(rq, p, &flags);
		return -EINVAL;
	}

	/*
	 * If not changing anything there's no need to proceed further:
	 */
	if (unlikely(policy == p->policy && (!is_rt_policy(policy) ||
			param->sched_priority == p->rt_priority))) {
		task_rq_unlock(rq, p, &flags);
		return 0;
	}

	/* recheck policy now with rq lock held */
	if (unlikely(oldpolicy != -1 && oldpolicy != p->policy)) {
		policy = oldpolicy = -1;
		task_rq_unlock(rq, p, &flags);
		goto recheck;
	}
	p->sched_reset_on_fork = reset_on_fork;

	__setscheduler(p, rq, policy, param->sched_priority, pi);
	task_rq_unlock(rq, p, &flags);

	if (pi)
		rt_mutex_adjust_pi(p);
out:
	return 0;
}

/**
 * sched_setscheduler - change the scheduling policy and/or RT priority of a thread.
 * @p: the task in question.
 * @policy: new policy.
 * @param: structure containing the new RT priority.
 *
 * Return: 0 on success. An error code otherwise.
 *
 * NOTE that the task may be already dead.
 */
int sched_setscheduler(struct task_struct *p, int policy,
		       const struct sched_param *param)
{
	return __sched_setscheduler(p, policy, param, true, true);
}

EXPORT_SYMBOL_GPL(sched_setscheduler);

int sched_setattr(struct task_struct *p, const struct sched_attr *attr)
{
	const struct sched_param param = { .sched_priority = attr->sched_priority };
	int policy = attr->sched_policy;

	return __sched_setscheduler(p, policy, &param, true, true);
}
EXPORT_SYMBOL_GPL(sched_setattr);

/**
 * sched_setscheduler_nocheck - change the scheduling policy and/or RT priority of a thread from kernelspace.
 * @p: the task in question.
 * @policy: new policy.
 * @param: structure containing the new RT priority.
 *
 * Just like sched_setscheduler, only don't bother checking if the
 * current context has permission.  For example, this is needed in
 * stop_machine(): we create temporary high priority worker threads,
 * but our caller might not have that capability.
 *
 * Return: 0 on success. An error code otherwise.
 */
int sched_setscheduler_nocheck(struct task_struct *p, int policy,
			       const struct sched_param *param)
{
	return __sched_setscheduler(p, policy, param, false, true);
}
EXPORT_SYMBOL_GPL(sched_setscheduler_nocheck);

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

/*
 * Mimics kernel/events/core.c perf_copy_attr().
 */
static int sched_copy_attr(struct sched_attr __user *uattr,
			   struct sched_attr *attr)
{
	u32 size;
	int ret;

	if (!access_ok(VERIFY_WRITE, uattr, SCHED_ATTR_SIZE_VER0))
		return -EFAULT;

	/*
	 * zero the full structure, so that a short copy will be nice.
	 */
	memset(attr, 0, sizeof(*attr));

	ret = get_user(size, &uattr->size);
	if (ret)
		return ret;

	if (size > PAGE_SIZE)	/* silly large */
		goto err_size;

	if (!size)		/* abi compat */
		size = SCHED_ATTR_SIZE_VER0;

	if (size < SCHED_ATTR_SIZE_VER0)
		goto err_size;

	/*
	 * If we're handed a bigger struct than we know of,
	 * ensure all the unknown bits are 0 - i.e. new
	 * user-space does not rely on any kernel feature
	 * extensions we dont know about yet.
	 */
	if (size > sizeof(*attr)) {
		unsigned char __user *addr;
		unsigned char __user *end;
		unsigned char val;

		addr = (void __user *)uattr + sizeof(*attr);
		end  = (void __user *)uattr + size;

		for (; addr < end; addr++) {
			ret = get_user(val, addr);
			if (ret)
				return ret;
			if (val)
				goto err_size;
		}
		size = sizeof(*attr);
	}

	ret = copy_from_user(attr, uattr, size);
	if (ret)
		return -EFAULT;

	/*
	 * XXX: do we want to be lenient like existing syscalls; or do we want
	 * to be strict and return an error on out-of-bounds values?
	 */
	attr->sched_nice = clamp(attr->sched_nice, -20, 19);

	/* sched/core.c uses zero here but we already know ret is zero */
	return 0;

err_size:
	put_user(sizeof(*attr), &uattr->size);
	return -E2BIG;
}

/**
 * sys_sched_setscheduler - set/change the scheduler policy and RT priority
 * @pid: the pid in question.
 * @policy: new policy.
 *
 * Return: 0 on success. An error code otherwise.
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

/*
 * sched_setparam() passes in -1 for its policy, to let the functions
 * it calls know not to change it.
 */
#define SETPARAM_POLICY	-1

/**
 * sys_sched_setparam - set/change the RT priority of a thread
 * @pid: the pid in question.
 * @param: structure containing the new RT priority.
 *
 * Return: 0 on success. An error code otherwise.
 */
SYSCALL_DEFINE2(sched_setparam, pid_t, pid, struct sched_param __user *, param)
{
	return do_sched_setscheduler(pid, SETPARAM_POLICY, param);
}

/**
 * sys_sched_setattr - same as above, but with extended sched_attr
 * @pid: the pid in question.
 * @uattr: structure containing the extended parameters.
 */
SYSCALL_DEFINE3(sched_setattr, pid_t, pid, struct sched_attr __user *, uattr,
			       unsigned int, flags)
{
	struct sched_attr attr;
	struct task_struct *p;
	int retval;

	if (!uattr || pid < 0 || flags)
		return -EINVAL;

	retval = sched_copy_attr(uattr, &attr);
	if (retval)
		return retval;

	if ((int)attr.sched_policy < 0)
		return -EINVAL;

	rcu_read_lock();
	retval = -ESRCH;
	p = find_process_by_pid(pid);
	if (p != NULL)
		retval = sched_setattr(p, &attr);
	rcu_read_unlock();

	return retval;
}

/**
 * sys_sched_getscheduler - get the policy (scheduling class) of a thread
 * @pid: the pid in question.
 *
 * Return: On success, the policy of the thread. Otherwise, a negative error
 * code.
 */
SYSCALL_DEFINE1(sched_getscheduler, pid_t, pid)
{
	struct task_struct *p;
	int retval = -EINVAL;

	if (pid < 0)
		goto out_nounlock;

	retval = -ESRCH;
	rcu_read_lock();
	p = find_process_by_pid(pid);
	if (p) {
		retval = security_task_getscheduler(p);
		if (!retval)
			retval = p->policy;
	}
	rcu_read_unlock();

out_nounlock:
	return retval;
}

/**
 * sys_sched_getscheduler - get the RT priority of a thread
 * @pid: the pid in question.
 * @param: structure containing the RT priority.
 *
 * Return: On success, 0 and the RT priority is in @param. Otherwise, an error
 * code.
 */
SYSCALL_DEFINE2(sched_getparam, pid_t, pid, struct sched_param __user *, param)
{
	struct sched_param lp = { .sched_priority = 0 };
	struct task_struct *p;
	int retval = -EINVAL;

	if (!param || pid < 0)
		goto out_nounlock;

	rcu_read_lock();
	p = find_process_by_pid(pid);
	retval = -ESRCH;
	if (!p)
		goto out_unlock;

	retval = security_task_getscheduler(p);
	if (retval)
		goto out_unlock;

	if (has_rt_policy(p))
		lp.sched_priority = p->rt_priority;
	rcu_read_unlock();

	/*
	 * This one might sleep, we cannot do it with a spinlock held ...
	 */
	retval = copy_to_user(param, &lp, sizeof(*param)) ? -EFAULT : 0;

out_nounlock:
	return retval;

out_unlock:
	rcu_read_unlock();
	return retval;
}

static int sched_read_attr(struct sched_attr __user *uattr,
			   struct sched_attr *attr,
			   unsigned int usize)
{
	int ret;

	if (!access_ok(VERIFY_WRITE, uattr, usize))
		return -EFAULT;

	/*
	 * If we're handed a smaller struct than we know of,
	 * ensure all the unknown bits are 0 - i.e. old
	 * user-space does not get uncomplete information.
	 */
	if (usize < sizeof(*attr)) {
		unsigned char *addr;
		unsigned char *end;

		addr = (void *)attr + usize;
		end  = (void *)attr + sizeof(*attr);

		for (; addr < end; addr++) {
			if (*addr)
				return -EFBIG;
		}

		attr->size = usize;
	}

	ret = copy_to_user(uattr, attr, attr->size);
	if (ret)
		return -EFAULT;

	/* sched/core.c uses zero here but we already know ret is zero */
	return ret;
}

/**
 * sys_sched_getattr - similar to sched_getparam, but with sched_attr
 * @pid: the pid in question.
 * @uattr: structure containing the extended parameters.
 * @size: sizeof(attr) for fwd/bwd comp.
 * @flags: for future extension.
 */
SYSCALL_DEFINE4(sched_getattr, pid_t, pid, struct sched_attr __user *, uattr,
		unsigned int, size, unsigned int, flags)
{
	struct sched_attr attr = {
		.size = sizeof(struct sched_attr),
	};
	struct task_struct *p;
	int retval;

	if (!uattr || pid < 0 || size > PAGE_SIZE ||
	    size < SCHED_ATTR_SIZE_VER0 || flags)
		return -EINVAL;

	rcu_read_lock();
	p = find_process_by_pid(pid);
	retval = -ESRCH;
	if (!p)
		goto out_unlock;

	retval = security_task_getscheduler(p);
	if (retval)
		goto out_unlock;

	attr.sched_policy = p->policy;
	if (rt_task(p))
		attr.sched_priority = p->rt_priority;
	else
		attr.sched_nice = task_nice(p);

	rcu_read_unlock();

	retval = sched_read_attr(uattr, &attr, size);
	return retval;

out_unlock:
	rcu_read_unlock();
	return retval;
}

long sched_setaffinity(pid_t pid, const struct cpumask *in_mask)
{
	cpumask_var_t cpus_allowed, new_mask;
	struct task_struct *p;
	int retval;

	get_online_cpus();
	rcu_read_lock();

	p = find_process_by_pid(pid);
	if (!p) {
		rcu_read_unlock();
		put_online_cpus();
		return -ESRCH;
	}

	/* Prevent p going away */
	get_task_struct(p);
	rcu_read_unlock();

	if (p->flags & PF_NO_SETAFFINITY) {
		retval = -EINVAL;
		goto out_put_task;
	}
	if (!alloc_cpumask_var(&cpus_allowed, GFP_KERNEL)) {
		retval = -ENOMEM;
		goto out_put_task;
	}
	if (!alloc_cpumask_var(&new_mask, GFP_KERNEL)) {
		retval = -ENOMEM;
		goto out_free_cpus_allowed;
	}
	retval = -EPERM;
	if (!check_same_owner(p)) {
		rcu_read_lock();
		if (!ns_capable(__task_cred(p)->user_ns, CAP_SYS_NICE)) {
			rcu_read_unlock();
			goto out_unlock;
		}
		rcu_read_unlock();
	}

	retval = security_task_setscheduler(p);
	if (retval)
		goto out_unlock;

	cpuset_cpus_allowed(p, cpus_allowed);
	cpumask_and(new_mask, in_mask, cpus_allowed);
again:
	retval = __set_cpus_allowed_ptr(p, new_mask, true);

	if (!retval) {
		cpuset_cpus_allowed(p, cpus_allowed);
		if (!cpumask_subset(new_mask, cpus_allowed)) {
			/*
			 * We must have raced with a concurrent cpuset
			 * update. Just reset the cpus_allowed to the
			 * cpuset's cpus_allowed
			 */
			cpumask_copy(new_mask, cpus_allowed);
			goto again;
		}
	}
out_unlock:
	free_cpumask_var(new_mask);
out_free_cpus_allowed:
	free_cpumask_var(cpus_allowed);
out_put_task:
	put_task_struct(p);
	put_online_cpus();
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
 *
 * Return: 0 on success. An error code otherwise.
 */
SYSCALL_DEFINE3(sched_setaffinity, pid_t, pid, unsigned int, len,
		unsigned long __user *, user_mask_ptr)
{
	cpumask_var_t new_mask;
	int retval;

	if (!alloc_cpumask_var(&new_mask, GFP_KERNEL))
		return -ENOMEM;

	retval = get_user_cpu_mask(user_mask_ptr, len, new_mask);
	if (retval == 0)
		retval = sched_setaffinity(pid, new_mask);
	free_cpumask_var(new_mask);
	return retval;
}

long sched_getaffinity(pid_t pid, cpumask_t *mask)
{
	struct task_struct *p;
	unsigned long flags;
	int retval;

	get_online_cpus();
	rcu_read_lock();

	retval = -ESRCH;
	p = find_process_by_pid(pid);
	if (!p)
		goto out_unlock;

	retval = security_task_getscheduler(p);
	if (retval)
		goto out_unlock;

	raw_spin_lock_irqsave(&p->pi_lock, flags);
	cpumask_and(mask, tsk_cpus_allowed(p), cpu_active_mask);
	raw_spin_unlock_irqrestore(&p->pi_lock, flags);

out_unlock:
	rcu_read_unlock();
	put_online_cpus();

	return retval;
}

/**
 * sys_sched_getaffinity - get the cpu affinity of a process
 * @pid: pid of the process
 * @len: length in bytes of the bitmask pointed to by user_mask_ptr
 * @user_mask_ptr: user-space pointer to hold the current cpu mask
 *
 * Return: 0 on success. An error code otherwise.
 */
SYSCALL_DEFINE3(sched_getaffinity, pid_t, pid, unsigned int, len,
		unsigned long __user *, user_mask_ptr)
{
	int ret;
	cpumask_var_t mask;

	if ((len * BITS_PER_BYTE) < nr_cpu_ids)
		return -EINVAL;
	if (len & (sizeof(unsigned long)-1))
		return -EINVAL;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	ret = sched_getaffinity(pid, mask);
	if (ret == 0) {
		size_t retlen = min_t(size_t, len, cpumask_size());

		if (copy_to_user(user_mask_ptr, mask, retlen))
			ret = -EFAULT;
		else
			ret = retlen;
	}
	free_cpumask_var(mask);

	return ret;
}

/**
 * sys_sched_yield - yield the current processor to other threads.
 *
 * This function yields the current CPU to other tasks. It does this by
 * scheduling away the current task. If it still has the earliest deadline
 * it will be scheduled again as the next task.
 *
 * Return: 0.
 */
SYSCALL_DEFINE0(sched_yield)
{
	struct task_struct *p;
	struct rq *rq;

	p = current;
	rq = this_rq_lock();
	time_slice_expired(p, rq);
	schedstat_inc(task_rq(p), yld_count);

	/*
	 * Since we are going to call schedule() anyway, there's
	 * no need to preempt or enable interrupts:
	 */
	__release(rq->lock);
	spin_release(&rq->lock.dep_map, 1, _THIS_IP_);
	do_raw_spin_unlock(&rq->lock);
	sched_preempt_enable_no_resched();

	schedule();

	return 0;
}

int __sched _cond_resched(void)
{
	if (should_resched(0)) {
		preempt_schedule_common();
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(_cond_resched);

/*
 * __cond_resched_lock() - if a reschedule is pending, drop the given lock,
 * call schedule, and on return reacquire the lock.
 *
 * This works OK both with and without CONFIG_PREEMPT.  We do strange low-level
 * operations here to prevent schedule() from being called twice (once via
 * spin_unlock(), once by hand).
 */
int __cond_resched_lock(spinlock_t *lock)
{
	int resched = should_resched(PREEMPT_LOCK_OFFSET);
	int ret = 0;

	lockdep_assert_held(lock);

	if (spin_needbreak(lock) || resched) {
		spin_unlock(lock);
		if (resched)
			preempt_schedule_common();
		else
			cpu_relax();
		ret = 1;
		spin_lock(lock);
	}
	return ret;
}
EXPORT_SYMBOL(__cond_resched_lock);

int __sched __cond_resched_softirq(void)
{
	BUG_ON(!in_softirq());

	if (should_resched(SOFTIRQ_DISABLE_OFFSET)) {
		local_bh_enable();
		preempt_schedule_common();
		local_bh_disable();
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(__cond_resched_softirq);

/**
 * yield - yield the current processor to other threads.
 *
 * Do not ever use this function, there's a 99% chance you're doing it wrong.
 *
 * The scheduler is at all times free to pick the calling task as the most
 * eligible task to run, if removing the yield() call from your code breaks
 * it, its already broken.
 *
 * Typical broken usage is:
 *
 * while (!event)
 * 	yield();
 *
 * where one assumes that yield() will let 'the other' process run that will
 * make event true. If the current task is a SCHED_FIFO task that will never
 * happen. Never use yield() as a progress guarantee!!
 *
 * If you want to use yield() to wait for something, use wait_event().
 * If you want to use yield() to be 'nice' for others, use cond_resched().
 * If you still want to use yield(), do not!
 */
void __sched yield(void)
{
	set_current_state(TASK_RUNNING);
	sys_sched_yield();
}
EXPORT_SYMBOL(yield);

/**
 * yield_to - yield the current processor to another thread in
 * your thread group, or accelerate that thread toward the
 * processor it's on.
 * @p: target task
 * @preempt: whether task preemption is allowed or not
 *
 * It's the caller's job to ensure that the target task struct
 * can't go away on us before we can do any checks.
 *
 * Return:
 *	true (>0) if we indeed boosted the target task.
 *	false (0) if we failed to boost the target.
 *	-ESRCH if there's no task to yield to.
 */
int __sched yield_to(struct task_struct *p, bool preempt)
{
	struct task_struct *rq_p;
	struct rq *rq, *p_rq;
	unsigned long flags;
	int yielded = 0;

	local_irq_save(flags);
	rq = this_rq();

again:
	p_rq = task_rq(p);
	/*
	 * If we're the only runnable task on the rq and target rq also
	 * has only one task, there's absolutely no point in yielding.
	 */
	if (task_running(p_rq, p) || p->state) {
		yielded = -ESRCH;
		goto out_irq;
	}

	double_rq_lock(rq, p_rq);
	if (unlikely(task_rq(p) != p_rq)) {
		double_rq_unlock(rq, p_rq);
		goto again;
	}

	yielded = 1;
	rq_p = rq->curr;
	if (p->deadline > rq_p->deadline)
		p->deadline = rq_p->deadline;
	p->time_slice += rq_p->time_slice;
	if (p->time_slice > timeslice())
		p->time_slice = timeslice();
	time_slice_expired(rq_p, rq);
	if (preempt && rq != p_rq)
		resched_task(p_rq->curr);
	double_rq_unlock(rq, p_rq);
out_irq:
	local_irq_restore(flags);

	if (yielded > 0)
		schedule();
	return yielded;
}
EXPORT_SYMBOL_GPL(yield_to);

/*
 * This task is about to go to sleep on IO.  Increment rq->nr_iowait so
 * that process accounting knows that this is a task in IO wait state.
 *
 * But don't do that if it is a deliberate, throttling IO wait (this task
 * has set its backing_dev_info: the queue against which it should throttle)
 */

long __sched io_schedule_timeout(long timeout)
{
	int old_iowait = current->in_iowait;
	struct rq *rq;
	long ret;

	current->in_iowait = 1;
	blk_schedule_flush_plug(current);

	delayacct_blkio_start();
	rq = raw_rq();
	atomic_inc(&rq->nr_iowait);
	ret = schedule_timeout(timeout);
	current->in_iowait = old_iowait;
	atomic_dec(&rq->nr_iowait);
	delayacct_blkio_end();

	return ret;
}
EXPORT_SYMBOL(io_schedule_timeout);

/**
 * sys_sched_get_priority_max - return maximum RT priority.
 * @policy: scheduling class.
 *
 * Return: On success, this syscall returns the maximum
 * rt_priority that can be used by a given scheduling class.
 * On failure, a negative error code is returned.
 */
SYSCALL_DEFINE1(sched_get_priority_max, int, policy)
{
	int ret = -EINVAL;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = MAX_USER_RT_PRIO-1;
		break;
	case SCHED_NORMAL:
	case SCHED_BATCH:
	case SCHED_ISO:
	case SCHED_IDLEPRIO:
		ret = 0;
		break;
	}
	return ret;
}

/**
 * sys_sched_get_priority_min - return minimum RT priority.
 * @policy: scheduling class.
 *
 * Return: On success, this syscall returns the minimum
 * rt_priority that can be used by a given scheduling class.
 * On failure, a negative error code is returned.
 */
SYSCALL_DEFINE1(sched_get_priority_min, int, policy)
{
	int ret = -EINVAL;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = 1;
		break;
	case SCHED_NORMAL:
	case SCHED_BATCH:
	case SCHED_ISO:
	case SCHED_IDLEPRIO:
		ret = 0;
		break;
	}
	return ret;
}

/**
 * sys_sched_rr_get_interval - return the default timeslice of a process.
 * @pid: pid of the process.
 * @interval: userspace pointer to the timeslice value.
 *
 *
 * Return: On success, 0 and the timeslice is in @interval. Otherwise,
 * an error code.
 */
SYSCALL_DEFINE2(sched_rr_get_interval, pid_t, pid,
		struct timespec __user *, interval)
{
	struct task_struct *p;
	unsigned int time_slice;
	unsigned long flags;
	struct timespec t;
	struct rq *rq;
	int retval;

	if (pid < 0)
		return -EINVAL;

	retval = -ESRCH;
	rcu_read_lock();
	p = find_process_by_pid(pid);
	if (!p)
		goto out_unlock;

	retval = security_task_getscheduler(p);
	if (retval)
		goto out_unlock;

	rq = task_rq_lock(p, &flags);
	time_slice = p->policy == SCHED_FIFO ? 0 : MS_TO_NS(task_timeslice(p));
	task_rq_unlock(rq, p, &flags);

	rcu_read_unlock();
	t = ns_to_timespec(time_slice);
	retval = copy_to_user(interval, &t, sizeof(t)) ? -EFAULT : 0;
	return retval;

out_unlock:
	rcu_read_unlock();
	return retval;
}

static const char stat_nam[] = TASK_STATE_TO_CHAR_STR;

void sched_show_task(struct task_struct *p)
{
	unsigned long free = 0;
	int ppid;
	unsigned long state = p->state;

	if (state)
		state = __ffs(state) + 1;
	printk(KERN_INFO "%-15.15s %c", p->comm,
		state < sizeof(stat_nam) - 1 ? stat_nam[state] : '?');
#if BITS_PER_LONG == 32
	if (state == TASK_RUNNING)
		printk(KERN_CONT " running  ");
	else
		printk(KERN_CONT " %08lx ", thread_saved_pc(p));
#else
	if (state == TASK_RUNNING)
		printk(KERN_CONT "  running task    ");
	else
		printk(KERN_CONT " %016lx ", thread_saved_pc(p));
#endif
#ifdef CONFIG_DEBUG_STACK_USAGE
	free = stack_not_used(p);
#endif
	ppid = 0;
	rcu_read_lock();
	if (pid_alive(p))
		ppid = task_pid_nr(rcu_dereference(p->real_parent));
	rcu_read_unlock();
	printk(KERN_CONT "%5lu %5d %6d 0x%08lx\n", free,
		task_pid_nr(p), ppid,
		(unsigned long)task_thread_info(p)->flags);

	print_worker_info(KERN_INFO, p);
	show_stack(p, NULL);
}

void show_state_filter(unsigned long state_filter)
{
	struct task_struct *g, *p;

#if BITS_PER_LONG == 32
	printk(KERN_INFO
		"  task                PC stack   pid father\n");
#else
	printk(KERN_INFO
		"  task                        PC stack   pid father\n");
#endif
	rcu_read_lock();
	for_each_process_thread(g, p) {
		/*
		 * reset the NMI-timeout, listing all files on a slow
		 * console might take a lot of time:
		 * Also, reset softlockup watchdogs on all CPUs, because
		 * another CPU might be blocked waiting for us to process
		 * an IPI.
		 */
		touch_nmi_watchdog();
		touch_all_softlockup_watchdogs();
		if (!state_filter || (p->state & state_filter))
			sched_show_task(p);
	}

	rcu_read_unlock();
	/*
	 * Only show locks if all tasks are dumped:
	 */
	if (!state_filter)
		debug_show_all_locks();
}

void dump_cpu_task(int cpu)
{
	pr_info("Task dump for CPU %d:\n", cpu);
	sched_show_task(cpu_curr(cpu));
}

#ifdef CONFIG_SMP
void set_cpus_allowed_common(struct task_struct *p, const struct cpumask *new_mask)
{
	cpumask_copy(&p->cpus_allowed, new_mask);
	p->nr_cpus_allowed = cpumask_weight(new_mask);
}

void do_set_cpus_allowed(struct task_struct *p, const struct cpumask *new_mask)
{
	struct rq *rq = task_rq(p);

	lockdep_assert_held(&p->pi_lock);

	cpumask_copy(tsk_cpus_allowed(p), new_mask);

	if (task_queued(p)) {
		/*
		 * Because __kthread_bind() calls this on blocked tasks without
		 * holding rq->lock.
		 */
		lockdep_assert_held(&rq->lock);
	}
	if (needs_other_cpu(p, task_cpu(p)))
		set_task_cpu(p, valid_task_cpu(p));
}
#endif

/**
 * init_idle - set up an idle thread for a given CPU
 * @idle: task in question
 * @cpu: cpu the idle task belongs to
 *
 * NOTE: this function does not set the idle thread's NEED_RESCHED
 * flag, to make booting more robust.
 */
void init_idle(struct task_struct *idle, int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	raw_spin_lock_irqsave(&idle->pi_lock, flags);
	raw_spin_lock(&rq->lock);
	idle->last_ran = rq->clock_task;
	idle->state = TASK_RUNNING;
	/* Setting prio to illegal value shouldn't matter when never queued */
	idle->prio = PRIO_LIMIT;

	kasan_unpoison_task_stack(idle);

#ifdef CONFIG_SMP
	/*
	 * It's possible that init_idle() gets called multiple times on a task,
	 * in that case do_set_cpus_allowed() will not do the right thing.
	 *
	 * And since this is boot we can forgo the serialisation.
	 */
	set_cpus_allowed_common(idle, cpumask_of(cpu));
#ifdef CONFIG_SMT_NICE
	idle->smt_bias = 0;
#endif
#endif
	set_rq_task(rq, idle);

	/* Silence PROVE_RCU */
	rcu_read_lock();
	set_task_cpu(idle, cpu);
	rcu_read_unlock();

	rq->curr = rq->idle = idle;
	idle->on_rq = TASK_ON_RQ_QUEUED;
	raw_spin_unlock(&rq->lock);
	raw_spin_unlock_irqrestore(&idle->pi_lock, flags);

	/* Set the preempt count _outside_ the spinlocks! */
	init_idle_preempt_count(idle, cpu);

	ftrace_graph_init_idle_task(idle, cpu);
#ifdef CONFIG_SMP
	sprintf(idle->comm, "%s/%d", INIT_TASK_COMM, cpu);
#endif
}

int cpuset_cpumask_can_shrink(const struct cpumask __maybe_unused *cur,
			      const struct cpumask __maybe_unused *trial)
{
	return 1;
}

int task_can_attach(struct task_struct *p,
		    const struct cpumask *cs_cpus_allowed)
{
	int ret = 0;

	/*
	 * Kthreads which disallow setaffinity shouldn't be moved
	 * to a new cpuset; we don't want to change their cpu
	 * affinity and isolating such threads by their set of
	 * allowed nodes is unnecessary.  Thus, cpusets are not
	 * applicable for such threads.  This prevents checking for
	 * success of set_cpus_allowed_ptr() on all attached tasks
	 * before cpus_allowed may be changed.
	 */
	if (p->flags & PF_NO_SETAFFINITY)
		ret = -EINVAL;

	return ret;
}

void wake_q_add(struct wake_q_head *head, struct task_struct *task)
{
	struct wake_q_node *node = &task->wake_q;

	/*
	 * Atomically grab the task, if ->wake_q is !nil already it means
	 * its already queued (either by us or someone else) and will get the
	 * wakeup due to that.
	 *
	 * This cmpxchg() implies a full barrier, which pairs with the write
	 * barrier implied by the wakeup in wake_up_q().
	 */
	if (cmpxchg(&node->next, NULL, WAKE_Q_TAIL))
		return;

	get_task_struct(task);

	/*
	 * The head is context local, there can be no concurrency.
	 */
	*head->lastp = node;
	head->lastp = &node->next;
}

void wake_up_q(struct wake_q_head *head)
{
	struct wake_q_node *node = head->first;

	while (node != WAKE_Q_TAIL) {
		struct task_struct *task;

		task = container_of(node, struct task_struct, wake_q);
		BUG_ON(!task);
		/* task can safely be re-inserted now */
		node = node->next;
		task->wake_q.next = NULL;

		/*
		 * wake_up_process() implies a wmb() to pair with the queueing
		 * in wake_q_add() so as not to miss wakeups.
		 */
		wake_up_process(task);
		put_task_struct(task);
	}
}

void resched_cpu(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	rq_lock_irqsave(rq, &flags);
	resched_task(cpu_curr(cpu));
	rq_unlock_irqrestore(rq, &flags);
}

#ifdef CONFIG_SMP
#ifdef CONFIG_NO_HZ_COMMON
void nohz_balance_enter_idle(int cpu)
{
}

void select_nohz_load_balancer(int stop_tick)
{
}

void set_cpu_sd_state_idle(void) {}
#if defined(CONFIG_SCHED_MC) || defined(CONFIG_SCHED_SMT)
/**
 * lowest_flag_domain - Return lowest sched_domain containing flag.
 * @cpu:	The cpu whose lowest level of sched domain is to
 *		be returned.
 * @flag:	The flag to check for the lowest sched_domain
 *		for the given cpu.
 *
 * Returns the lowest sched_domain of a cpu which contains the given flag.
 */
static inline struct sched_domain *lowest_flag_domain(int cpu, int flag)
{
	struct sched_domain *sd;

	for_each_domain(cpu, sd)
		if (sd && (sd->flags & flag))
			break;

	return sd;
}

/**
 * for_each_flag_domain - Iterates over sched_domains containing the flag.
 * @cpu:	The cpu whose domains we're iterating over.
 * @sd:		variable holding the value of the power_savings_sd
 *		for cpu.
 * @flag:	The flag to filter the sched_domains to be iterated.
 *
 * Iterates over all the scheduler domains for a given cpu that has the 'flag'
 * set, starting from the lowest sched_domain to the highest.
 */
#define for_each_flag_domain(cpu, sd, flag) \
	for (sd = lowest_flag_domain(cpu, flag); \
		(sd && (sd->flags & flag)); sd = sd->parent)

#endif /*  (CONFIG_SCHED_MC || CONFIG_SCHED_SMT) */

/*
 * In the semi idle case, use the nearest busy cpu for migrating timers
 * from an idle cpu.  This is good for power-savings.
 *
 * We don't do similar optimization for completely idle system, as
 * selecting an idle cpu will add more delays to the timers than intended
 * (as that cpu's timer base may not be uptodate wrt jiffies etc).
 */
int get_nohz_timer_target(void)
{
	int i, cpu = smp_processor_id();
	struct sched_domain *sd;

	if (!idle_cpu(cpu) && is_housekeeping_cpu(cpu))
		return cpu;

	rcu_read_lock();
	for_each_domain(cpu, sd) {
		for_each_cpu(i, sched_domain_span(sd)) {
			if (cpu == i)
				continue;

			if (!idle_cpu(i) && is_housekeeping_cpu(i)) {
 				cpu = i;
				cpu = i;
				goto unlock;
			}
		}
	}

	if (!is_housekeeping_cpu(cpu))
		cpu = housekeeping_any_cpu();
unlock:
	rcu_read_unlock();
	return cpu;
}

/*
 * When add_timer_on() enqueues a timer into the timer wheel of an
 * idle CPU then this timer might expire before the next timer event
 * which is scheduled to wake up that CPU. In case of a completely
 * idle system the next event might even be infinite time into the
 * future. wake_up_idle_cpu() ensures that the CPU is woken up and
 * leaves the inner idle loop so the newly added timer is taken into
 * account when the CPU goes back to idle and evaluates the timer
 * wheel for the next timer event.
 */
void wake_up_idle_cpu(int cpu)
{
	if (cpu == smp_processor_id())
		return;

	set_tsk_need_resched(cpu_rq(cpu)->idle);
	smp_send_reschedule(cpu);
}

void wake_up_nohz_cpu(int cpu)
{
	wake_up_idle_cpu(cpu);
}
#endif /* CONFIG_NO_HZ_COMMON */

/*
 * Change a given task's CPU affinity. Migrate the thread to a
 * proper CPU and schedule it away if the CPU it's executing on
 * is removed from the allowed bitmask.
 *
 * NOTE: the caller must have a valid reference to the task, the
 * task must not exit() & deallocate itself prematurely. The
 * call is not atomic; no spinlocks may be held.
 */
static int __set_cpus_allowed_ptr(struct task_struct *p,
				  const struct cpumask *new_mask, bool check)
{
	const struct cpumask *cpu_valid_mask = cpu_active_mask;
	bool running_wrong = false;
	struct cpumask old_mask;
	bool queued = false;
	unsigned long flags;
	struct rq *rq;
	int ret = 0;

	rq = task_rq_lock(p, &flags);

	if (p->flags & PF_KTHREAD) {
		/*
		 * Kernel threads are allowed on online && !active CPUs
		 */
		cpu_valid_mask = cpu_online_mask;
	}

	/*
	 * Must re-check here, to close a race against __kthread_bind(),
	 * sched_setaffinity() is not guaranteed to observe the flag.
	 */
	if (check && (p->flags & PF_NO_SETAFFINITY)) {
		ret = -EINVAL;
		goto out;
	}

	cpumask_copy(&old_mask, tsk_cpus_allowed(p));
	if (cpumask_equal(&old_mask, new_mask))
		goto out;

	if (!cpumask_intersects(new_mask, cpu_valid_mask)) {
		ret = -EINVAL;
		goto out;
	}

	queued = task_queued(p);

	do_set_cpus_allowed(p, new_mask);

	if (p->flags & PF_KTHREAD) {
		/*
		 * For kernel threads that do indeed end up on online &&
		 * !active we want to ensure they are strict per-cpu threads.
		 */
		WARN_ON(cpumask_intersects(new_mask, cpu_online_mask) &&
			!cpumask_intersects(new_mask, cpu_active_mask) &&
			tsk_nr_cpus_allowed(p) != 1);
	}

	/* Can the task run on the task's current CPU? If so, we're done */
	if (cpumask_test_cpu(task_cpu(p), new_mask))
		goto out;

	if (task_running(rq, p)) {
		/* Task is running on the wrong cpu now, reschedule it. */
		if (rq == this_rq()) {
			set_tsk_need_resched(p);
			running_wrong = true;
		} else
			resched_task(p);
	} else {
		int dest_cpu = cpumask_any_and(cpu_valid_mask, new_mask);
		struct rq *dest_rq = cpu_rq(dest_cpu);

		/* Switch rq locks here */
		lock_second_rq(rq, dest_rq);
		set_task_cpu(p, dest_cpu);
		rq_unlock(rq);

		rq = dest_rq;
	}
out:
	if (queued && !cpumask_subset(new_mask, &old_mask))
		try_preempt(p, rq);
	if (running_wrong)
		preempt_disable();
	task_rq_unlock(rq, p, &flags);

	if (running_wrong) {
		__schedule(true);
		preempt_enable();
	}

	return ret;
}

int set_cpus_allowed_ptr(struct task_struct *p, const struct cpumask *new_mask)
{
	return __set_cpus_allowed_ptr(p, new_mask, false);
}
EXPORT_SYMBOL_GPL(set_cpus_allowed_ptr);

#ifdef CONFIG_HOTPLUG_CPU
/*
 * Run through task list and find tasks affined to the dead cpu, then remove
 * that cpu from the list, enable cpu0 and set the zerobound flag. Must hold
 * cpu 0 and src_cpu's runqueue locks.
 */
static void bind_zero(int src_cpu)
{
	struct task_struct *p, *t;
	int bound = 0;

	if (src_cpu == 0)
		return;

	do_each_thread(t, p) {
		if (cpumask_test_cpu(src_cpu, tsk_cpus_allowed(p))) {
			bool local = (task_cpu(p) == src_cpu);

			/* task_running is the cpu stopper thread */
			if (local && task_running(task_rq(p), p))
				continue;
			atomic_clear_cpu(src_cpu, tsk_cpus_allowed(p));
			atomic_set_cpu(0, tsk_cpus_allowed(p));
			p->zerobound = true;
			bound++;
			if (local)
				set_task_cpu(p, 0);
		}
	} while_each_thread(t, p);

	if (bound) {
		printk(KERN_INFO "Removed affinity for %d processes to cpu %d\n",
		       bound, src_cpu);
	}
}

/* Find processes with the zerobound flag and reenable their affinity for the
 * CPU coming alive. */
static void unbind_zero(int src_cpu)
{
	int unbound = 0, zerobound = 0;
	struct task_struct *p, *t;

	if (src_cpu == 0)
		return;

	do_each_thread(t, p) {
		if (!p->mm)
			p->zerobound = false;
		if (p->zerobound) {
			unbound++;
			cpumask_set_cpu(src_cpu, tsk_cpus_allowed(p));
			/* Once every CPU affinity has been re-enabled, remove
			 * the zerobound flag */
			if (cpumask_subset(cpu_possible_mask, tsk_cpus_allowed(p))) {
				p->zerobound = false;
				zerobound++;
			}
		}
	} while_each_thread(t, p);

	if (unbound) {
		printk(KERN_INFO "Added affinity for %d processes to cpu %d\n",
		       unbound, src_cpu);
	}
	if (zerobound) {
		printk(KERN_INFO "Released forced binding to cpu0 for %d processes\n",
		       zerobound);
	}
}

/*
 * Ensures that the idle task is using init_mm right before its cpu goes
 * offline.
 */
void idle_task_exit(void)
{
	struct mm_struct *mm = current->active_mm;

	BUG_ON(cpu_online(smp_processor_id()));

	if (mm != &init_mm) {
		switch_mm_irqs_off(mm, &init_mm, current);
		finish_arch_post_lock_switch();
	}
	mmdrop(mm);
}
#else /* CONFIG_HOTPLUG_CPU */
static void unbind_zero(int src_cpu) {}
#endif /* CONFIG_HOTPLUG_CPU */

void sched_set_stop_task(int cpu, struct task_struct *stop)
{
	struct sched_param stop_param = { .sched_priority = STOP_PRIO };
	struct sched_param start_param = { .sched_priority = 0 };
	struct task_struct *old_stop = cpu_rq(cpu)->stop;

	if (stop) {
		/*
		 * Make it appear like a SCHED_FIFO task, its something
		 * userspace knows about and won't get confused about.
		 *
		 * Also, it will make PI more or less work without too
		 * much confusion -- but then, stop work should not
		 * rely on PI working anyway.
		 */
		sched_setscheduler_nocheck(stop, SCHED_FIFO, &stop_param);
	}

	cpu_rq(cpu)->stop = stop;

	if (old_stop) {
		/*
		 * Reset it back to a normal scheduling policy so that
		 * it can die in pieces.
		 */
		sched_setscheduler_nocheck(old_stop, SCHED_NORMAL, &start_param);
	}
}

#if defined(CONFIG_SCHED_DEBUG) && defined(CONFIG_SYSCTL)

static struct ctl_table sd_ctl_dir[] = {
	{
		.procname	= "sched_domain",
		.mode		= 0555,
	},
	{}
};

static struct ctl_table sd_ctl_root[] = {
	{
		.procname	= "kernel",
		.mode		= 0555,
		.child		= sd_ctl_dir,
	},
	{}
};

static struct ctl_table *sd_alloc_ctl_entry(int n)
{
	struct ctl_table *entry =
		kcalloc(n, sizeof(struct ctl_table), GFP_KERNEL);

	return entry;
}

static void sd_free_ctl_entry(struct ctl_table **tablep)
{
	struct ctl_table *entry;

	/*
	 * In the intermediate directories, both the child directory and
	 * procname are dynamically allocated and could fail but the mode
	 * will always be set. In the lowest directory the names are
	 * static strings and all have proc handlers.
	 */
	for (entry = *tablep; entry->mode; entry++) {
		if (entry->child)
			sd_free_ctl_entry(&entry->child);
		if (entry->proc_handler == NULL)
			kfree(entry->procname);
	}

	kfree(*tablep);
	*tablep = NULL;
}

#define CPU_LOAD_IDX_MAX 5
static int min_load_idx = 0;
static int max_load_idx = CPU_LOAD_IDX_MAX-1;

static void
set_table_entry(struct ctl_table *entry,
		const char *procname, void *data, int maxlen,
		umode_t mode, proc_handler *proc_handler,
		bool load_idx)
{
	entry->procname = procname;
	entry->data = data;
	entry->maxlen = maxlen;
	entry->mode = mode;
	entry->proc_handler = proc_handler;

	if (load_idx) {
		entry->extra1 = &min_load_idx;
		entry->extra2 = &max_load_idx;
	}
}

static struct ctl_table *
sd_alloc_ctl_domain_table(struct sched_domain *sd)
{
	struct ctl_table *table = sd_alloc_ctl_entry(14);

	if (table == NULL)
		return NULL;

	set_table_entry(&table[0], "min_interval", &sd->min_interval,
		sizeof(long), 0644, proc_doulongvec_minmax, false);
	set_table_entry(&table[1], "max_interval", &sd->max_interval,
		sizeof(long), 0644, proc_doulongvec_minmax, false);
	set_table_entry(&table[2], "busy_idx", &sd->busy_idx,
		sizeof(int), 0644, proc_dointvec_minmax, true);
	set_table_entry(&table[3], "idle_idx", &sd->idle_idx,
		sizeof(int), 0644, proc_dointvec_minmax, true);
	set_table_entry(&table[4], "newidle_idx", &sd->newidle_idx,
		sizeof(int), 0644, proc_dointvec_minmax, true);
	set_table_entry(&table[5], "wake_idx", &sd->wake_idx,
		sizeof(int), 0644, proc_dointvec_minmax, true);
	set_table_entry(&table[6], "forkexec_idx", &sd->forkexec_idx,
		sizeof(int), 0644, proc_dointvec_minmax, true);
	set_table_entry(&table[7], "busy_factor", &sd->busy_factor,
		sizeof(int), 0644, proc_dointvec_minmax, false);
	set_table_entry(&table[8], "imbalance_pct", &sd->imbalance_pct,
		sizeof(int), 0644, proc_dointvec_minmax, false);
	set_table_entry(&table[9], "cache_nice_tries",
		&sd->cache_nice_tries,
		sizeof(int), 0644, proc_dointvec_minmax, false);
	set_table_entry(&table[10], "flags", &sd->flags,
		sizeof(int), 0644, proc_dointvec_minmax, false);
	set_table_entry(&table[11], "max_newidle_lb_cost",
		&sd->max_newidle_lb_cost,
		sizeof(long), 0644, proc_doulongvec_minmax, false);
	set_table_entry(&table[12], "name", sd->name,
		CORENAME_MAX_SIZE, 0444, proc_dostring, false);
	/* &table[13] is terminator */

	return table;
}

static struct ctl_table *sd_alloc_ctl_cpu_table(int cpu)
{
	struct ctl_table *entry, *table;
	struct sched_domain *sd;
	int domain_num = 0, i;
	char buf[32];

	for_each_domain(cpu, sd)
		domain_num++;
	entry = table = sd_alloc_ctl_entry(domain_num + 1);
	if (table == NULL)
		return NULL;

	i = 0;
	for_each_domain(cpu, sd) {
		snprintf(buf, 32, "domain%d", i);
		entry->procname = kstrdup(buf, GFP_KERNEL);
		entry->mode = 0555;
		entry->child = sd_alloc_ctl_domain_table(sd);
		entry++;
		i++;
	}
	return table;
}

static struct ctl_table_header *sd_sysctl_header;
void register_sched_domain_sysctl(void)
{
	int i, cpu_num = num_possible_cpus();
	struct ctl_table *entry = sd_alloc_ctl_entry(cpu_num + 1);
	char buf[32];

	WARN_ON(sd_ctl_dir[0].child);
	sd_ctl_dir[0].child = entry;

	if (entry == NULL)
		return;

	for_each_possible_cpu(i) {
		snprintf(buf, 32, "cpu%d", i);
		entry->procname = kstrdup(buf, GFP_KERNEL);
		entry->mode = 0555;
		entry->child = sd_alloc_ctl_cpu_table(i);
		entry++;
	}

	WARN_ON(sd_sysctl_header);
	sd_sysctl_header = register_sysctl_table(sd_ctl_root);
}

/* may be called multiple times per register */
void unregister_sched_domain_sysctl(void)
{
	unregister_sysctl_table(sd_sysctl_header);
	sd_sysctl_header = NULL;
	if (sd_ctl_dir[0].child)
		sd_free_ctl_entry(&sd_ctl_dir[0].child);
}
#endif /* CONFIG_SYSCTL */

static void set_rq_online(struct rq *rq)
{
	if (!rq->online) {
		cpumask_set_cpu(cpu_of(rq), rq->rd->online);
		rq->online = true;
	}
}

static void set_rq_offline(struct rq *rq)
{
	if (rq->online) {
		int cpu = cpu_of(rq);

		cpumask_clear_cpu(cpu, rq->rd->online);
		rq->online = false;
		clear_cpuidle_map(cpu);
	}
}

static cpumask_var_t sched_domains_tmpmask; /* sched_domains_mutex */

#ifdef CONFIG_SCHED_DEBUG

static __read_mostly int sched_debug_enabled;

static int __init sched_debug_setup(char *str)
{
	sched_debug_enabled = 1;

	return 0;
}
early_param("sched_debug", sched_debug_setup);

static inline bool sched_debug(void)
{
	return sched_debug_enabled;
}

static int sched_domain_debug_one(struct sched_domain *sd, int cpu, int level,
				  struct cpumask *groupmask)
{
	cpumask_clear(groupmask);

	printk(KERN_DEBUG "%*s domain %d: ", level, "", level);

	if (!(sd->flags & SD_LOAD_BALANCE)) {
		printk("does not load-balance\n");
		if (sd->parent)
			printk(KERN_ERR "ERROR: !SD_LOAD_BALANCE domain"
					" has parent");
		return -1;
	}

	printk(KERN_CONT "span %*pbl level %s\n",
	       cpumask_pr_args(sched_domain_span(sd)), sd->name);

	if (!cpumask_test_cpu(cpu, sched_domain_span(sd))) {
		printk(KERN_ERR "ERROR: domain->span does not contain "
				"CPU%d\n", cpu);
	}

	printk(KERN_CONT "\n");

	if (!cpumask_equal(sched_domain_span(sd), groupmask))
		printk(KERN_ERR "ERROR: groups don't span domain->span\n");

	if (sd->parent &&
	    !cpumask_subset(groupmask, sched_domain_span(sd->parent)))
		printk(KERN_ERR "ERROR: parent span is not a superset "
			"of domain->span\n");
	return 0;
}

static void sched_domain_debug(struct sched_domain *sd, int cpu)
{
	int level = 0;

	if (!sched_debug_enabled)
		return;

	if (!sd) {
		printk(KERN_DEBUG "CPU%d attaching NULL sched-domain.\n", cpu);
		return;
	}

	printk(KERN_DEBUG "CPU%d attaching sched-domain:\n", cpu);

	for (;;) {
		if (sched_domain_debug_one(sd, cpu, level, sched_domains_tmpmask))
			break;
		level++;
		sd = sd->parent;
		if (!sd)
			break;
	}
}
#else /* !CONFIG_SCHED_DEBUG */
# define sched_domain_debug(sd, cpu) do { } while (0)
static inline bool sched_debug(void)
{
	return false;
}
#endif /* CONFIG_SCHED_DEBUG */

static int sd_degenerate(struct sched_domain *sd)
{
	if (cpumask_weight(sched_domain_span(sd)) == 1)
		return 1;

	/* Following flags don't use groups */
	if (sd->flags & (SD_WAKE_AFFINE))
		return 0;

	return 1;
}

static int
sd_parent_degenerate(struct sched_domain *sd, struct sched_domain *parent)
{
	unsigned long cflags = sd->flags, pflags = parent->flags;

	if (sd_degenerate(parent))
		return 1;

	if (!cpumask_equal(sched_domain_span(sd), sched_domain_span(parent)))
		return 0;

	if (~cflags & pflags)
		return 0;

	return 1;
}

static void free_rootdomain(struct rcu_head *rcu)
{
	struct root_domain *rd = container_of(rcu, struct root_domain, rcu);

	cpupri_cleanup(&rd->cpupri);
	free_cpumask_var(rd->rto_mask);
	free_cpumask_var(rd->online);
	free_cpumask_var(rd->span);
	kfree(rd);
}

static void rq_attach_root(struct rq *rq, struct root_domain *rd)
{
	struct root_domain *old_rd = NULL;
	unsigned long flags;

	rq_lock_irqsave(rq, &flags);

	if (rq->rd) {
		old_rd = rq->rd;

		if (cpumask_test_cpu(rq->cpu, old_rd->online))
			set_rq_offline(rq);

		cpumask_clear_cpu(rq->cpu, old_rd->span);

		/*
		 * If we dont want to free the old_rd yet then
		 * set old_rd to NULL to skip the freeing later
		 * in this function:
		 */
		if (!atomic_dec_and_test(&old_rd->refcount))
			old_rd = NULL;
	}

	atomic_inc(&rd->refcount);
	rq->rd = rd;

	cpumask_set_cpu(rq->cpu, rd->span);
	if (cpumask_test_cpu(rq->cpu, cpu_active_mask))
		set_rq_online(rq);

	rq_unlock_irqrestore(rq, &flags);

	if (old_rd)
		call_rcu_sched(&old_rd->rcu, free_rootdomain);
}

static int init_rootdomain(struct root_domain *rd)
{
	memset(rd, 0, sizeof(*rd));

	if (!zalloc_cpumask_var(&rd->span, GFP_KERNEL))
		goto out;
	if (!zalloc_cpumask_var(&rd->online, GFP_KERNEL))
		goto free_span;
	if (!zalloc_cpumask_var(&rd->rto_mask, GFP_KERNEL))
		goto free_online;

	if (cpupri_init(&rd->cpupri) != 0)
		goto free_rto_mask;
	return 0;

free_rto_mask:
	free_cpumask_var(rd->rto_mask);
free_online:
	free_cpumask_var(rd->online);
free_span:
	free_cpumask_var(rd->span);
out:
	return -ENOMEM;
}

static void init_defrootdomain(void)
{
	init_rootdomain(&def_root_domain);

	atomic_set(&def_root_domain.refcount, 1);
}

static struct root_domain *alloc_rootdomain(void)
{
	struct root_domain *rd;

	rd = kmalloc(sizeof(*rd), GFP_KERNEL);
	if (!rd)
		return NULL;

	if (init_rootdomain(rd) != 0) {
		kfree(rd);
		return NULL;
	}

	return rd;
}

static void free_sched_domain(struct rcu_head *rcu)
{
	struct sched_domain *sd = container_of(rcu, struct sched_domain, rcu);

	kfree(sd);
}

static void destroy_sched_domain(struct sched_domain *sd, int cpu)
{
	call_rcu(&sd->rcu, free_sched_domain);
}

static void destroy_sched_domains(struct sched_domain *sd, int cpu)
{
	for (; sd; sd = sd->parent)
		destroy_sched_domain(sd, cpu);
}

/*
 * Attach the domain 'sd' to 'cpu' as its base domain. Callers must
 * hold the hotplug lock.
 */
static void
cpu_attach_domain(struct sched_domain *sd, struct root_domain *rd, int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	struct sched_domain *tmp;

	/* Remove the sched domains which do not contribute to scheduling. */
	for (tmp = sd; tmp; ) {
		struct sched_domain *parent = tmp->parent;
		if (!parent)
			break;

		if (sd_parent_degenerate(tmp, parent)) {
			tmp->parent = parent->parent;
			if (parent->parent)
				parent->parent->child = tmp;
			/*
			 * Transfer SD_PREFER_SIBLING down in case of a
			 * degenerate parent; the spans match for this
			 * so the property transfers.
			 */
			if (parent->flags & SD_PREFER_SIBLING)
				tmp->flags |= SD_PREFER_SIBLING;
			destroy_sched_domain(parent, cpu);
		} else
			tmp = tmp->parent;
	}

	if (sd && sd_degenerate(sd)) {
		tmp = sd;
		sd = sd->parent;
		destroy_sched_domain(tmp, cpu);
		if (sd)
			sd->child = NULL;
	}

	sched_domain_debug(sd, cpu);

	rq_attach_root(rq, rd);
	tmp = rq->sd;
	rcu_assign_pointer(rq->sd, sd);
	destroy_sched_domains(tmp, cpu);
}

/* Setup the mask of cpus configured for isolated domains */
static int __init isolated_cpu_setup(char *str)
{
	int ret;

	alloc_bootmem_cpumask_var(&cpu_isolated_map);
	ret = cpulist_parse(str, cpu_isolated_map);
	if (ret) {
		pr_err("sched: Error, all isolcpus= values must be between 0 and %d\n", nr_cpu_ids);
		return 0;
	}
	return 1;
}

__setup("isolcpus=", isolated_cpu_setup);

struct s_data {
	struct sched_domain ** __percpu sd;
	struct root_domain	*rd;
};

enum s_alloc {
	sa_rootdomain,
	sa_sd,
	sa_sd_storage,
	sa_none,
};

/*
 * Initializers for schedule domains
 * Non-inlined to reduce accumulated stack pressure in build_sched_domains()
 */

static int default_relax_domain_level = -1;
int sched_domain_level_max;

static int __init setup_relax_domain_level(char *str)
{
	if (kstrtoint(str, 0, &default_relax_domain_level))
		pr_warn("Unable to set relax_domain_level\n");

	return 1;
}
__setup("relax_domain_level=", setup_relax_domain_level);

static void set_domain_attribute(struct sched_domain *sd,
				 struct sched_domain_attr *attr)
{
	int request;

	if (!attr || attr->relax_domain_level < 0) {
		if (default_relax_domain_level < 0)
			return;
		else
			request = default_relax_domain_level;
	} else
		request = attr->relax_domain_level;
	if (request < sd->level) {
		/* turn off idle balance on this domain */
		sd->flags &= ~(SD_BALANCE_WAKE|SD_BALANCE_NEWIDLE);
	} else {
		/* turn on idle balance on this domain */
		sd->flags |= (SD_BALANCE_WAKE|SD_BALANCE_NEWIDLE);
	}
}

static void __sdt_free(const struct cpumask *cpu_map);
static int __sdt_alloc(const struct cpumask *cpu_map);

static void __free_domain_allocs(struct s_data *d, enum s_alloc what,
				 const struct cpumask *cpu_map)
{
	switch (what) {
	case sa_rootdomain:
		if (!atomic_read(&d->rd->refcount))
			free_rootdomain(&d->rd->rcu); /* fall through */
	case sa_sd:
		free_percpu(d->sd); /* fall through */
	case sa_sd_storage:
		__sdt_free(cpu_map); /* fall through */
	case sa_none:
		break;
	}
}

static enum s_alloc __visit_domain_allocation_hell(struct s_data *d,
						   const struct cpumask *cpu_map)
{
	memset(d, 0, sizeof(*d));

	if (__sdt_alloc(cpu_map))
		return sa_sd_storage;
	d->sd = alloc_percpu(struct sched_domain *);
	if (!d->sd)
		return sa_sd_storage;
	d->rd = alloc_rootdomain();
	if (!d->rd)
		return sa_sd;
	return sa_rootdomain;
}

/*
 * NULL the sd_data elements we've used to build the sched_domain
 * structure so that the subsequent __free_domain_allocs()
 * will not free the data we're using.
 */
static void claim_allocations(int cpu, struct sched_domain *sd)
{
	struct sd_data *sdd = sd->private;

	WARN_ON_ONCE(*per_cpu_ptr(sdd->sd, cpu) != sd);
	*per_cpu_ptr(sdd->sd, cpu) = NULL;
}

#ifdef CONFIG_NUMA
static int sched_domains_numa_levels;
static int *sched_domains_numa_distance;
static struct cpumask ***sched_domains_numa_masks;
static int sched_domains_curr_level;
#endif

/*
 * SD_flags allowed in topology descriptions.
 *
 * SD_SHARE_CPUCAPACITY      - describes SMT topologies
 * SD_SHARE_PKG_RESOURCES - describes shared caches
 * SD_NUMA                - describes NUMA topologies
 * SD_SHARE_POWERDOMAIN   - describes shared power domain
 *
 * Odd one out:
 * SD_ASYM_PACKING        - describes SMT quirks
 */
#define TOPOLOGY_SD_FLAGS		\
	(SD_SHARE_CPUCAPACITY |		\
	 SD_SHARE_PKG_RESOURCES |	\
	 SD_NUMA |			\
	 SD_ASYM_PACKING |		\
	 SD_SHARE_POWERDOMAIN)

static struct sched_domain *
sd_init(struct sched_domain_topology_level *tl, int cpu)
{
	struct sched_domain *sd = *per_cpu_ptr(tl->data.sd, cpu);
	int sd_weight, sd_flags = 0;

#ifdef CONFIG_NUMA
	/*
	 * Ugly hack to pass state to sd_numa_mask()...
	 */
	sched_domains_curr_level = tl->numa_level;
#endif

	sd_weight = cpumask_weight(tl->mask(cpu));

	if (tl->sd_flags)
		sd_flags = (*tl->sd_flags)();
	if (WARN_ONCE(sd_flags & ~TOPOLOGY_SD_FLAGS,
			"wrong sd_flags in topology description\n"))
		sd_flags &= ~TOPOLOGY_SD_FLAGS;

	*sd = (struct sched_domain){
		.min_interval		= sd_weight,
		.max_interval		= 2*sd_weight,
		.busy_factor		= 32,
		.imbalance_pct		= 125,

		.cache_nice_tries	= 0,
		.busy_idx		= 0,
		.idle_idx		= 0,
		.newidle_idx		= 0,
		.wake_idx		= 0,
		.forkexec_idx		= 0,

		.flags			= 1*SD_LOAD_BALANCE
					| 1*SD_BALANCE_NEWIDLE
					| 1*SD_BALANCE_EXEC
					| 1*SD_BALANCE_FORK
					| 0*SD_BALANCE_WAKE
					| 1*SD_WAKE_AFFINE
					| 0*SD_SHARE_CPUCAPACITY
					| 0*SD_SHARE_PKG_RESOURCES
					| 0*SD_SERIALIZE
					| 0*SD_PREFER_SIBLING
					| 0*SD_NUMA
					| sd_flags
					,

		.last_balance		= jiffies,
		.balance_interval	= sd_weight,
		.smt_gain		= 0,
		.max_newidle_lb_cost	= 0,
		.next_decay_max_lb_cost	= jiffies,
#ifdef CONFIG_SCHED_DEBUG
		.name			= tl->name,
#endif
	};

	/*
	 * Convert topological properties into behaviour.
	 */

	if (sd->flags & SD_SHARE_CPUCAPACITY) {
		sd->flags |= SD_PREFER_SIBLING;
		sd->imbalance_pct = 110;
		sd->smt_gain = 1178; /* ~15% */

	} else if (sd->flags & SD_SHARE_PKG_RESOURCES) {
		sd->imbalance_pct = 117;
		sd->cache_nice_tries = 1;
		sd->busy_idx = 2;

#ifdef CONFIG_NUMA
	} else if (sd->flags & SD_NUMA) {
		sd->cache_nice_tries = 2;
		sd->busy_idx = 3;
		sd->idle_idx = 2;

		sd->flags |= SD_SERIALIZE;
		if (sched_domains_numa_distance[tl->numa_level] > RECLAIM_DISTANCE) {
			sd->flags &= ~(SD_BALANCE_EXEC |
				       SD_BALANCE_FORK |
				       SD_WAKE_AFFINE);
		}

#endif
	} else {
		sd->flags |= SD_PREFER_SIBLING;
		sd->cache_nice_tries = 1;
		sd->busy_idx = 2;
		sd->idle_idx = 1;
	}

	sd->private = &tl->data;

	return sd;
}

/*
 * Topology list, bottom-up.
 */
static struct sched_domain_topology_level default_topology[] = {
#ifdef CONFIG_SCHED_SMT
	{ cpu_smt_mask, cpu_smt_flags, SD_INIT_NAME(SMT) },
#endif
#ifdef CONFIG_SCHED_MC
	{ cpu_coregroup_mask, cpu_core_flags, SD_INIT_NAME(MC) },
#endif
	{ cpu_cpu_mask, SD_INIT_NAME(DIE) },
	{ NULL, },
};

static struct sched_domain_topology_level *sched_domain_topology =
	default_topology;

#define for_each_sd_topology(tl)			\
	for (tl = sched_domain_topology; tl->mask; tl++)

void set_sched_topology(struct sched_domain_topology_level *tl)
{
	sched_domain_topology = tl;
}

#ifdef CONFIG_NUMA

static const struct cpumask *sd_numa_mask(int cpu)
{
	return sched_domains_numa_masks[sched_domains_curr_level][cpu_to_node(cpu)];
}

static void sched_numa_warn(const char *str)
{
	static int done = false;
	int i,j;

	if (done)
		return;

	done = true;

	printk(KERN_WARNING "ERROR: %s\n\n", str);

	for (i = 0; i < nr_node_ids; i++) {
		printk(KERN_WARNING "  ");
		for (j = 0; j < nr_node_ids; j++)
			printk(KERN_CONT "%02d ", node_distance(i,j));
		printk(KERN_CONT "\n");
	}
	printk(KERN_WARNING "\n");
}

static bool find_numa_distance(int distance)
{
	int i;

	if (distance == node_distance(0, 0))
		return true;

	for (i = 0; i < sched_domains_numa_levels; i++) {
		if (sched_domains_numa_distance[i] == distance)
			return true;
	}

	return false;
}

static void sched_init_numa(void)
{
	int next_distance, curr_distance = node_distance(0, 0);
	struct sched_domain_topology_level *tl;
	int level = 0;
	int i, j, k;

	sched_domains_numa_distance = kzalloc(sizeof(int) * nr_node_ids, GFP_KERNEL);
	if (!sched_domains_numa_distance)
		return;

	/*
	 * O(nr_nodes^2) deduplicating selection sort -- in order to find the
	 * unique distances in the node_distance() table.
	 *
	 * Assumes node_distance(0,j) includes all distances in
	 * node_distance(i,j) in order to avoid cubic time.
	 */
	next_distance = curr_distance;
	for (i = 0; i < nr_node_ids; i++) {
		for (j = 0; j < nr_node_ids; j++) {
			for (k = 0; k < nr_node_ids; k++) {
				int distance = node_distance(i, k);

				if (distance > curr_distance &&
				    (distance < next_distance ||
				     next_distance == curr_distance))
					next_distance = distance;

				/*
				 * While not a strong assumption it would be nice to know
				 * about cases where if node A is connected to B, B is not
				 * equally connected to A.
				 */
				if (sched_debug() && node_distance(k, i) != distance)
					sched_numa_warn("Node-distance not symmetric");

				if (sched_debug() && i && !find_numa_distance(distance))
					sched_numa_warn("Node-0 not representative");
			}
			if (next_distance != curr_distance) {
				sched_domains_numa_distance[level++] = next_distance;
				sched_domains_numa_levels = level;
				curr_distance = next_distance;
			} else break;
		}

		/*
		 * In case of sched_debug() we verify the above assumption.
		 */
		if (!sched_debug())
			break;
	}
	/*
	 * 'level' contains the number of unique distances, excluding the
	 * identity distance node_distance(i,i).
	 *
	 * The sched_domains_numa_distance[] array includes the actual distance
	 * numbers.
	 */

	/*
	 * Here, we should temporarily reset sched_domains_numa_levels to 0.
	 * If it fails to allocate memory for array sched_domains_numa_masks[][],
	 * the array will contain less then 'level' members. This could be
	 * dangerous when we use it to iterate array sched_domains_numa_masks[][]
	 * in other functions.
	 *
	 * We reset it to 'level' at the end of this function.
	 */
	sched_domains_numa_levels = 0;

	sched_domains_numa_masks = kzalloc(sizeof(void *) * level, GFP_KERNEL);
	if (!sched_domains_numa_masks)
		return;

	/*
	 * Now for each level, construct a mask per node which contains all
	 * cpus of nodes that are that many hops away from us.
	 */
	for (i = 0; i < level; i++) {
		sched_domains_numa_masks[i] =
			kzalloc(nr_node_ids * sizeof(void *), GFP_KERNEL);
		if (!sched_domains_numa_masks[i])
			return;

		for (j = 0; j < nr_node_ids; j++) {
			struct cpumask *mask = kzalloc(cpumask_size(), GFP_KERNEL);
			if (!mask)
				return;

			sched_domains_numa_masks[i][j] = mask;

			for_each_node(k) {
				if (node_distance(j, k) > sched_domains_numa_distance[i])
					continue;

				cpumask_or(mask, mask, cpumask_of_node(k));
			}
		}
	}

	/* Compute default topology size */
	for (i = 0; sched_domain_topology[i].mask; i++);

	tl = kzalloc((i + level + 1) *
			sizeof(struct sched_domain_topology_level), GFP_KERNEL);
	if (!tl)
		return;

	/*
	 * Copy the default topology bits..
	 */
	for (i = 0; sched_domain_topology[i].mask; i++)
		tl[i] = sched_domain_topology[i];

	/*
	 * .. and append 'j' levels of NUMA goodness.
	 */
	for (j = 0; j < level; i++, j++) {
		tl[i] = (struct sched_domain_topology_level){
			.mask = sd_numa_mask,
			.sd_flags = cpu_numa_flags,
			.flags = SDTL_OVERLAP,
			.numa_level = j,
			SD_INIT_NAME(NUMA)
		};
	}

	sched_domain_topology = tl;

	sched_domains_numa_levels = level;
}

static void sched_domains_numa_masks_set(int cpu)
{
	int node = cpu_to_node(cpu);
	int i, j;

	for (i = 0; i < sched_domains_numa_levels; i++) {
		for (j = 0; j < nr_node_ids; j++) {
			if (node_distance(j, node) <= sched_domains_numa_distance[i])
				cpumask_set_cpu(cpu, sched_domains_numa_masks[i][j]);
		}
	}
}

static void sched_domains_numa_masks_clear(int cpu)
{
	int i, j;

	for (i = 0; i < sched_domains_numa_levels; i++) {
		for (j = 0; j < nr_node_ids; j++)
			cpumask_clear_cpu(cpu, sched_domains_numa_masks[i][j]);
	}
}

#else
static inline void sched_init_numa(void) { }
static void sched_domains_numa_masks_set(unsigned int cpu) { }
static void sched_domains_numa_masks_clear(unsigned int cpu) { }
#endif /* CONFIG_NUMA */

static int __sdt_alloc(const struct cpumask *cpu_map)
{
	struct sched_domain_topology_level *tl;
	int j;

	for_each_sd_topology(tl) {
		struct sd_data *sdd = &tl->data;

		sdd->sd = alloc_percpu(struct sched_domain *);
		if (!sdd->sd)
			return -ENOMEM;

		for_each_cpu(j, cpu_map) {
			struct sched_domain *sd;

			sd = kzalloc_node(sizeof(struct sched_domain) + cpumask_size(),
					GFP_KERNEL, cpu_to_node(j));
			if (!sd)
				return -ENOMEM;

			*per_cpu_ptr(sdd->sd, j) = sd;
		}
	}

	return 0;
}

static void __sdt_free(const struct cpumask *cpu_map)
{
	struct sched_domain_topology_level *tl;
	int j;

	for_each_sd_topology(tl) {
		struct sd_data *sdd = &tl->data;

		for_each_cpu(j, cpu_map) {
			struct sched_domain *sd;

			if (sdd->sd) {
				sd = *per_cpu_ptr(sdd->sd, j);
				kfree(*per_cpu_ptr(sdd->sd, j));
			}
		}
		free_percpu(sdd->sd);
		sdd->sd = NULL;
	}
}

struct sched_domain *build_sched_domain(struct sched_domain_topology_level *tl,
		const struct cpumask *cpu_map, struct sched_domain_attr *attr,
		struct sched_domain *child, int cpu)
{
	struct sched_domain *sd = sd_init(tl, cpu);
	if (!sd)
		return child;

	cpumask_and(sched_domain_span(sd), cpu_map, tl->mask(cpu));
	if (child) {
		sd->level = child->level + 1;
		sched_domain_level_max = max(sched_domain_level_max, sd->level);
		child->parent = sd;
		sd->child = child;

		if (!cpumask_subset(sched_domain_span(child),
				    sched_domain_span(sd))) {
			pr_err("BUG: arch topology borken\n");
#ifdef CONFIG_SCHED_DEBUG
			pr_err("     the %s domain not a subset of the %s domain\n",
					child->name, sd->name);
#endif
			/* Fixup, ensure @sd has at least @child cpus. */
			cpumask_or(sched_domain_span(sd),
				   sched_domain_span(sd),
				   sched_domain_span(child));
		}

	}
	set_domain_attribute(sd, attr);

	return sd;
}

/*
 * Build sched domains for a given set of cpus and attach the sched domains
 * to the individual cpus
 */
static int build_sched_domains(const struct cpumask *cpu_map,
			       struct sched_domain_attr *attr)
{
	enum s_alloc alloc_state;
	struct sched_domain *sd;
	struct s_data d;
	int i, ret = -ENOMEM;

	alloc_state = __visit_domain_allocation_hell(&d, cpu_map);
	if (alloc_state != sa_rootdomain)
		goto error;

	/* Set up domains for cpus specified by the cpu_map. */
	for_each_cpu(i, cpu_map) {
		struct sched_domain_topology_level *tl;

		sd = NULL;
		for_each_sd_topology(tl) {
			sd = build_sched_domain(tl, cpu_map, attr, sd, i);
			if (tl == sched_domain_topology)
				*per_cpu_ptr(d.sd, i) = sd;
			if (tl->flags & SDTL_OVERLAP)
				sd->flags |= SD_OVERLAP;
			if (cpumask_equal(cpu_map, sched_domain_span(sd)))
				break;
		}
	}

	/* Calculate CPU capacity for physical packages and nodes */
	for (i = nr_cpumask_bits-1; i >= 0; i--) {
		if (!cpumask_test_cpu(i, cpu_map))
			continue;

		for (sd = *per_cpu_ptr(d.sd, i); sd; sd = sd->parent) {
			claim_allocations(i, sd);
		}
	}

	/* Attach the domains */
	rcu_read_lock();
	for_each_cpu(i, cpu_map) {
		sd = *per_cpu_ptr(d.sd, i);
		cpu_attach_domain(sd, d.rd, i);
	}
	rcu_read_unlock();

	ret = 0;
error:
	__free_domain_allocs(&d, alloc_state, cpu_map);
	return ret;
}

static cpumask_var_t *doms_cur;	/* current sched domains */
static int ndoms_cur;		/* number of sched domains in 'doms_cur' */
static struct sched_domain_attr *dattr_cur;
				/* attribues of custom domains in 'doms_cur' */

/*
 * Special case: If a kmalloc of a doms_cur partition (array of
 * cpumask) fails, then fallback to a single sched domain,
 * as determined by the single cpumask fallback_doms.
 */
static cpumask_var_t fallback_doms;

/*
 * arch_update_cpu_topology lets virtualized architectures update the
 * cpu core maps. It is supposed to return 1 if the topology changed
 * or 0 if it stayed the same.
 */
int __weak arch_update_cpu_topology(void)
{
	return 0;
}

cpumask_var_t *alloc_sched_domains(unsigned int ndoms)
{
	int i;
	cpumask_var_t *doms;

	doms = kmalloc(sizeof(*doms) * ndoms, GFP_KERNEL);
	if (!doms)
		return NULL;
	for (i = 0; i < ndoms; i++) {
		if (!alloc_cpumask_var(&doms[i], GFP_KERNEL)) {
			free_sched_domains(doms, i);
			return NULL;
		}
	}
	return doms;
}

void free_sched_domains(cpumask_var_t doms[], unsigned int ndoms)
{
	unsigned int i;
	for (i = 0; i < ndoms; i++)
		free_cpumask_var(doms[i]);
	kfree(doms);
}

/*
 * Set up scheduler domains and groups. Callers must hold the hotplug lock.
 * For now this just excludes isolated cpus, but could be used to
 * exclude other special cases in the future.
 */
static int init_sched_domains(const struct cpumask *cpu_map)
{
	int err;

	arch_update_cpu_topology();
	ndoms_cur = 1;
	doms_cur = alloc_sched_domains(ndoms_cur);
	if (!doms_cur)
		doms_cur = &fallback_doms;
	cpumask_andnot(doms_cur[0], cpu_map, cpu_isolated_map);
	err = build_sched_domains(doms_cur[0], NULL);
	register_sched_domain_sysctl();

	return err;
}

/*
 * Detach sched domains from a group of cpus specified in cpu_map
 * These cpus will now be attached to the NULL domain
 */
static void detach_destroy_domains(const struct cpumask *cpu_map)
{
	int i;

	rcu_read_lock();
	for_each_cpu(i, cpu_map)
		cpu_attach_domain(NULL, &def_root_domain, i);
	rcu_read_unlock();
}

/* handle null as "default" */
static int dattrs_equal(struct sched_domain_attr *cur, int idx_cur,
			struct sched_domain_attr *new, int idx_new)
{
	struct sched_domain_attr tmp;

	/* fast path */
	if (!new && !cur)
		return 1;

	tmp = SD_ATTR_INIT;
	return !memcmp(cur ? (cur + idx_cur) : &tmp,
			new ? (new + idx_new) : &tmp,
			sizeof(struct sched_domain_attr));
}

/*
 * Partition sched domains as specified by the 'ndoms_new'
 * cpumasks in the array doms_new[] of cpumasks. This compares
 * doms_new[] to the current sched domain partitioning, doms_cur[].
 * It destroys each deleted domain and builds each new domain.
 *
 * 'doms_new' is an array of cpumask_var_t's of length 'ndoms_new'.
 * The masks don't intersect (don't overlap.) We should setup one
 * sched domain for each mask. CPUs not in any of the cpumasks will
 * not be load balanced. If the same cpumask appears both in the
 * current 'doms_cur' domains and in the new 'doms_new', we can leave
 * it as it is.
 *
 * The passed in 'doms_new' should be allocated using
 * alloc_sched_domains.  This routine takes ownership of it and will
 * free_sched_domains it when done with it. If the caller failed the
 * alloc call, then it can pass in doms_new == NULL && ndoms_new == 1,
 * and partition_sched_domains() will fallback to the single partition
 * 'fallback_doms', it also forces the domains to be rebuilt.
 *
 * If doms_new == NULL it will be replaced with cpu_online_mask.
 * ndoms_new == 0 is a special case for destroying existing domains,
 * and it will not create the default domain.
 *
 * Call with hotplug lock held
 */
void partition_sched_domains(int ndoms_new, cpumask_var_t doms_new[],
			     struct sched_domain_attr *dattr_new)
{
	int i, j, n;
	int new_topology;

	mutex_lock(&sched_domains_mutex);

	/* always unregister in case we don't destroy any domains */
	unregister_sched_domain_sysctl();

	/* Let architecture update cpu core mappings. */
	new_topology = arch_update_cpu_topology();

	n = doms_new ? ndoms_new : 0;

	/* Destroy deleted domains */
	for (i = 0; i < ndoms_cur; i++) {
		for (j = 0; j < n && !new_topology; j++) {
			if (cpumask_equal(doms_cur[i], doms_new[j])
			    && dattrs_equal(dattr_cur, i, dattr_new, j))
				goto match1;
		}
		/* no match - a current sched domain not in new doms_new[] */
		detach_destroy_domains(doms_cur[i]);
match1:
		;
	}

	n = ndoms_cur;
	if (doms_new == NULL) {
		n = 0;
		doms_new = &fallback_doms;
		cpumask_andnot(doms_new[0], cpu_active_mask, cpu_isolated_map);
		WARN_ON_ONCE(dattr_new);
	}

	/* Build new domains */
	for (i = 0; i < ndoms_new; i++) {
		for (j = 0; j < n && !new_topology; j++) {
			if (cpumask_equal(doms_new[i], doms_cur[j])
			    && dattrs_equal(dattr_new, i, dattr_cur, j))
				goto match2;
		}
		/* no match - add a new doms_new */
		build_sched_domains(doms_new[i], dattr_new ? dattr_new + i : NULL);
match2:
		;
	}

	/* Remember the new sched domains */
	if (doms_cur != &fallback_doms)
		free_sched_domains(doms_cur, ndoms_cur);
	kfree(dattr_cur);	/* kfree(NULL) is safe */
	doms_cur = doms_new;
	dattr_cur = dattr_new;
	ndoms_cur = ndoms_new;

	register_sched_domain_sysctl();

	mutex_unlock(&sched_domains_mutex);
}

static int num_cpus_frozen;	/* used to mark begin/end of suspend/resume */

/*
 * Update cpusets according to cpu_active mask.  If cpusets are
 * disabled, cpuset_update_active_cpus() becomes a simple wrapper
 * around partition_sched_domains().
 *
 * If we come here as part of a suspend/resume, don't touch cpusets because we
 * want to restore it back to its original state upon resume anyway.
 */
static void cpuset_cpu_active(void)
{
	if (cpuhp_tasks_frozen) {
		/*
		 * num_cpus_frozen tracks how many CPUs are involved in suspend
		 * resume sequence. As long as this is not the last online
		 * operation in the resume sequence, just build a single sched
		 * domain, ignoring cpusets.
		 */
		num_cpus_frozen--;
		if (likely(num_cpus_frozen)) {
			partition_sched_domains(1, NULL, NULL);
			return;
		}
		/*
		 * This is the last CPU online operation. So fall through and
		 * restore the original sched domains by considering the
		 * cpuset configurations.
		 */
	}

	cpuset_update_active_cpus(true);
}

static int cpuset_cpu_inactive(unsigned int cpu)
{
	if (!cpuhp_tasks_frozen) {
		cpuset_update_active_cpus(false);
	} else {
		num_cpus_frozen++;
		partition_sched_domains(1, NULL, NULL);
	}
	return 0;
}

int sched_cpu_activate(unsigned int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	set_cpu_active(cpu, true);

	if (sched_smp_initialized) {
		sched_domains_numa_masks_set(cpu);
		cpuset_cpu_active();
	}

	/*
	 * Put the rq online, if not already. This happens:
	 *
	 * 1) In the early boot process, because we build the real domains
	 *    after all cpus have been brought up.
	 *
	 * 2) At runtime, if cpuset_cpu_active() fails to rebuild the
	 *    domains.
	 */
	rq_lock_irqsave(rq, &flags);
	if (rq->rd) {
		BUG_ON(!cpumask_test_cpu(cpu, rq->rd->span));
		set_rq_online(rq);
	}
	unbind_zero(cpu);
	rq_unlock_irqrestore(rq, &flags);

	return 0;
}

int sched_cpu_deactivate(unsigned int cpu)
{
	int ret;

	set_cpu_active(cpu, false);
	/*
	 * We've cleared cpu_active_mask, wait for all preempt-disabled and RCU
	 * users of this state to go away such that all new such users will
	 * observe it.
	 *
	 * For CONFIG_PREEMPT we have preemptible RCU and its sync_rcu() might
	 * not imply sync_sched(), so wait for both.
	 *
	 * Do sync before park smpboot threads to take care the rcu boost case.
	 */
	if (IS_ENABLED(CONFIG_PREEMPT))
		synchronize_rcu_mult(call_rcu, call_rcu_sched);
	else
		synchronize_rcu();

	if (!sched_smp_initialized)
		return 0;

	ret = cpuset_cpu_inactive(cpu);
	if (ret) {
		set_cpu_active(cpu, true);
		return ret;
	}
	sched_domains_numa_masks_clear(cpu);
	return 0;
}

int sched_cpu_starting(unsigned int __maybe_unused cpu)
{
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
int sched_cpu_dying(unsigned int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	local_irq_save(flags);
	double_rq_lock(rq, cpu_rq(0));
	if (rq->rd) {
		BUG_ON(!cpumask_test_cpu(cpu, rq->rd->span));
		set_rq_offline(rq);
	}
	bind_zero(cpu);
	double_rq_unlock(rq, cpu_rq(0));
	local_irq_restore(flags);

	return 0;
}
#endif

#if defined(CONFIG_SCHED_SMT) || defined(CONFIG_SCHED_MC)
/*
 * Cheaper version of the below functions in case support for SMT and MC is
 * compiled in but CPUs have no siblings.
 */
static bool sole_cpu_idle(struct rq *rq)
{
	return rq_idle(rq);
}
#endif
#ifdef CONFIG_SCHED_SMT
static const cpumask_t *thread_cpumask(int cpu)
{
	return topology_sibling_cpumask(cpu);
}
/* All this CPU's SMT siblings are idle */
static bool siblings_cpu_idle(struct rq *rq)
{
	return cpumask_subset(&rq->thread_mask, &grq.cpu_idle_map);
}
#endif
#ifdef CONFIG_SCHED_MC
static const cpumask_t *core_cpumask(int cpu)
{
	return topology_core_cpumask(cpu);
}
/* All this CPU's shared cache siblings are idle */
static bool cache_cpu_idle(struct rq *rq)
{
	return cpumask_subset(&rq->core_mask, &grq.cpu_idle_map);
}
#endif

enum sched_domain_level {
	SD_LV_NONE = 0,
	SD_LV_SIBLING,
	SD_LV_MC,
	SD_LV_BOOK,
	SD_LV_CPU,
	SD_LV_NODE,
	SD_LV_ALLNODES,
	SD_LV_MAX
};

void __init sched_init_smp(void)
{
	struct sched_domain *sd;
	int cpu, other_cpu;
#ifdef CONFIG_SCHED_SMT
	bool smt_threads = false;
#endif
	cpumask_var_t non_isolated_cpus;
	struct rq *rq;

	alloc_cpumask_var(&non_isolated_cpus, GFP_KERNEL);
	alloc_cpumask_var(&fallback_doms, GFP_KERNEL);

	sched_init_numa();

	/*
	 * There's no userspace yet to cause hotplug operations; hence all the
	 * cpu masks are stable and all blatant races in the below code cannot
	 * happen.
	 */
	mutex_lock(&sched_domains_mutex);
	init_sched_domains(cpu_active_mask);
	cpumask_andnot(non_isolated_cpus, cpu_possible_mask, cpu_isolated_map);
	if (cpumask_empty(non_isolated_cpus))
		cpumask_set_cpu(smp_processor_id(), non_isolated_cpus);
	mutex_unlock(&sched_domains_mutex);

	/* Move init over to a non-isolated CPU */
	if (set_cpus_allowed_ptr(current, non_isolated_cpus) < 0)
		BUG();
	free_cpumask_var(non_isolated_cpus);

	mutex_lock(&sched_domains_mutex);
	local_irq_disable();
	lock_all_rqs();
	/*
	 * Set up the relative cache distance of each online cpu from each
	 * other in a simple array for quick lookup. Locality is determined
	 * by the closest sched_domain that CPUs are separated by. CPUs with
	 * shared cache in SMT and MC are treated as local. Separate CPUs
	 * (within the same package or physically) within the same node are
	 * treated as not local. CPUs not even in the same domain (different
	 * nodes) are treated as very distant.
	 */
	for_each_online_cpu(cpu) {
		rq = cpu_rq(cpu);

		/* First check if this cpu is in the same node */
		for_each_domain(cpu, sd) {
			if (sd->level > SD_LV_MC)
				continue;
			/* Set locality to local node if not already found lower */
			for_each_cpu(other_cpu, sched_domain_span(sd)) {
				if (rq->cpu_locality[other_cpu] > 3)
					rq->cpu_locality[other_cpu] = 3;
			}
		}

		/*
		 * Each runqueue has its own function in case it doesn't have
		 * siblings of its own allowing mixed topologies.
		 */
#ifdef CONFIG_SCHED_MC
		for_each_cpu(other_cpu, core_cpumask(cpu)) {
			if (rq->cpu_locality[other_cpu] > 2)
				rq->cpu_locality[other_cpu] = 2;
		}
		if (cpumask_weight(core_cpumask(cpu)) > 1) {
			cpumask_copy(&rq->core_mask, core_cpumask(cpu));
			cpumask_clear_cpu(cpu, &rq->core_mask);
			rq->cache_idle = cache_cpu_idle;
		}
#endif
#ifdef CONFIG_SCHED_SMT
		if (cpumask_weight(thread_cpumask(cpu)) > 1) {
			cpumask_copy(&rq->thread_mask, thread_cpumask(cpu));
			cpumask_clear_cpu(cpu, &rq->thread_mask);
			for_each_cpu(other_cpu, thread_cpumask(cpu))
				rq->cpu_locality[other_cpu] = 1;
			rq->siblings_idle = siblings_cpu_idle;
			smt_threads = true;
		}
#endif
	}
	for_each_possible_cpu(cpu) {
		int total_cpus = 1, locality;

		rq = cpu_rq(cpu);
		for (locality = 1; locality <= 4; locality++) {
			for_each_possible_cpu(other_cpu) {
				if (rq->cpu_locality[other_cpu] == locality)
					rq->rq_order[total_cpus++] = cpu_rq(other_cpu);
			}
		}
	}
#ifdef CONFIG_SMT_NICE
	if (smt_threads) {
		check_siblings = &check_smt_siblings;
		wake_siblings = &wake_smt_siblings;
		smt_schedule = &smt_should_schedule;
	}
#endif
	unlock_all_rqs();
	local_irq_enable();
	mutex_unlock(&sched_domains_mutex);

	for_each_online_cpu(cpu) {
		rq = cpu_rq(cpu);

		for_each_online_cpu(other_cpu) {
			if (other_cpu <= cpu)
				continue;
			printk(KERN_DEBUG "MuQSS locality CPU %d to %d: %d\n", cpu, other_cpu, rq->cpu_locality[other_cpu]);
		}
	}
	sched_smp_initialized = true;
}
#else
void __init sched_init_smp(void)
{
}
#endif /* CONFIG_SMP */

int in_sched_functions(unsigned long addr)
{
	return in_lock_functions(addr) ||
		(addr >= (unsigned long)__sched_text_start
		&& addr < (unsigned long)__sched_text_end);
}

#ifdef CONFIG_CGROUP_SCHED
/* task group related information */
struct task_group {
	struct cgroup_subsys_state css;

	struct rcu_head rcu;
	struct list_head list;

	struct task_group *parent;
	struct list_head siblings;
	struct list_head children;
};

/*
 * Default task group.
 * Every task in system belongs to this group at bootup.
 */
struct task_group root_task_group;
LIST_HEAD(task_groups);

/* Cacheline aligned slab cache for task_group */
static struct kmem_cache *task_group_cache __read_mostly;
#endif /* CONFIG_CGROUP_SCHED */

void __init sched_init(void)
{
#ifdef CONFIG_SMP
	int cpu_ids;
#endif
	int i;
	struct rq *rq;

	prio_ratios[0] = 128;
	for (i = 1 ; i < NICE_WIDTH ; i++)
		prio_ratios[i] = prio_ratios[i - 1] * 11 / 10;

	atomic_set(&grq.nr_running, 0);
	atomic_set(&grq.nr_uninterruptible, 0);
	atomic64_set(&grq.nr_switches, 0);
	skiplist_node_init(&init_task.node);

#ifdef CONFIG_SMP
	init_defrootdomain();
	atomic_set(&grq.qnr, 0);
	cpumask_clear(&grq.cpu_idle_map);
#else
	uprq = &per_cpu(runqueues, 0);
#endif

#ifdef CONFIG_CGROUP_SCHED
	task_group_cache = KMEM_CACHE(task_group, 0);

	list_add(&root_task_group.list, &task_groups);
	INIT_LIST_HEAD(&root_task_group.children);
	INIT_LIST_HEAD(&root_task_group.siblings);
#endif /* CONFIG_CGROUP_SCHED */
	for_each_possible_cpu(i) {
		rq = cpu_rq(i);
		skiplist_init(&rq->node);
		rq->sl = new_skiplist(&rq->node);
		raw_spin_lock_init(&rq->lock);
		rq->niffies = 0;
		rq->last_jiffy = jiffies;
		rq->user_pc = rq->nice_pc = rq->softirq_pc = rq->system_pc =
			      rq->iowait_pc = rq->idle_pc = 0;
		rq->dither = 0;
		set_rq_task(rq, &init_task);
		rq->iso_ticks = 0;
		rq->iso_refractory = false;
#ifdef CONFIG_SMP
		rq->sd = NULL;
		rq->rd = NULL;
		rq->online = false;
		rq->cpu = i;
		rq_attach_root(rq, &def_root_domain);
#endif
		atomic_set(&rq->nr_iowait, 0);
	}

#ifdef CONFIG_SMP
	cpu_ids = i;
	/*
	 * Set the base locality for cpu cache distance calculation to
	 * "distant" (3). Make sure the distance from a CPU to itself is 0.
	 */
	for_each_possible_cpu(i) {
		int j;

		rq = cpu_rq(i);
#ifdef CONFIG_SCHED_SMT
		rq->siblings_idle = sole_cpu_idle;
#endif
#ifdef CONFIG_SCHED_MC
		rq->cache_idle = sole_cpu_idle;
#endif
		rq->cpu_locality = kmalloc(cpu_ids * sizeof(int *), GFP_ATOMIC);
		for_each_possible_cpu(j) {
			if (i == j)
				rq->cpu_locality[j] = 0;
			else
				rq->cpu_locality[j] = 4;
		}
		rq->rq_order = kmalloc(cpu_ids * sizeof(struct rq *), GFP_ATOMIC);
		rq->rq_order[0] = rq;
		for (j = 1; j < cpu_ids; j++)
			rq->rq_order[j] = cpu_rq(j);
	}
#endif

#ifdef CONFIG_PREEMPT_NOTIFIERS
	INIT_HLIST_HEAD(&init_task.preempt_notifiers);
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

#ifdef CONFIG_SMP
	zalloc_cpumask_var(&sched_domains_tmpmask, GFP_NOWAIT);
	/* May be allocated at isolcpus cmdline parse time */
	if (cpu_isolated_map == NULL)
		zalloc_cpumask_var(&cpu_isolated_map, GFP_NOWAIT);
	idle_thread_set_boot_cpu();
#endif /* SMP */

	init_schedstats();
}

#ifdef CONFIG_DEBUG_ATOMIC_SLEEP
static inline int preempt_count_equals(int preempt_offset)
{
	int nested = preempt_count() + rcu_preempt_depth();

	return (nested == preempt_offset);
}

void __might_sleep(const char *file, int line, int preempt_offset)
{
	/*
	 * Blocking primitives will set (and therefore destroy) current->state,
	 * since we will exit with TASK_RUNNING make sure we enter with it,
	 * otherwise we will destroy state.
	 */
	WARN_ONCE(current->state != TASK_RUNNING && current->task_state_change,
			"do not call blocking ops when !TASK_RUNNING; "
			"state=%lx set at [<%p>] %pS\n",
			current->state,
			(void *)current->task_state_change,
			(void *)current->task_state_change);

	___might_sleep(file, line, preempt_offset);
}
EXPORT_SYMBOL(__might_sleep);

void ___might_sleep(const char *file, int line, int preempt_offset)
{
	static unsigned long prev_jiffy;	/* ratelimiting */

	rcu_sleep_check(); /* WARN_ON_ONCE() by default, no rate limit reqd. */
	if ((preempt_count_equals(preempt_offset) && !irqs_disabled() &&
	     !is_idle_task(current)) ||
	    system_state != SYSTEM_RUNNING || oops_in_progress)
		return;
	if (time_before(jiffies, prev_jiffy + HZ) && prev_jiffy)
		return;
	prev_jiffy = jiffies;

	printk(KERN_ERR
		"BUG: sleeping function called from invalid context at %s:%d\n",
			file, line);
	printk(KERN_ERR
		"in_atomic(): %d, irqs_disabled(): %d, pid: %d, name: %s\n",
			in_atomic(), irqs_disabled(),
			current->pid, current->comm);

	if (task_stack_end_corrupted(current))
		printk(KERN_EMERG "Thread overran stack, or stack corrupted\n");

	debug_show_held_locks(current);
	if (irqs_disabled())
		print_irqtrace_events(current);
#ifdef CONFIG_DEBUG_PREEMPT
	if (!preempt_count_equals(preempt_offset)) {
		pr_err("Preemption disabled at:");
		print_ip_sym(current->preempt_disable_ip);
		pr_cont("\n");
	}
#endif
	dump_stack();
}
EXPORT_SYMBOL(___might_sleep);
#endif

#ifdef CONFIG_MAGIC_SYSRQ
static inline void normalise_rt_tasks(void)
{
	struct task_struct *g, *p;
	unsigned long flags;
	struct rq *rq;

	read_lock(&tasklist_lock);
	for_each_process_thread(g, p) {
		/*
		 * Only normalize user tasks:
		 */
		if (p->flags & PF_KTHREAD)
			continue;

		if (!rt_task(p) && !iso_task(p))
			continue;

		rq = task_rq_lock(p, &flags);
		__setscheduler(p, rq, SCHED_NORMAL, 0, false);
		task_rq_unlock(rq, p, &flags);
	}
	read_unlock(&tasklist_lock);
}

void normalize_rt_tasks(void)
{
	normalise_rt_tasks();
}
#endif /* CONFIG_MAGIC_SYSRQ */

#if defined(CONFIG_IA64) || defined(CONFIG_KGDB_KDB)
/*
 * These functions are only useful for the IA64 MCA handling, or kdb.
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
 *
 * Return: The current task for @cpu.
 */
struct task_struct *curr_task(int cpu)
{
	return cpu_curr(cpu);
}

#endif /* defined(CONFIG_IA64) || defined(CONFIG_KGDB_KDB) */

#ifdef CONFIG_IA64
/**
 * set_curr_task - set the current task for a given cpu.
 * @cpu: the processor in question.
 * @p: the task pointer to set.
 *
 * Description: This function must only be used when non-maskable interrupts
 * are serviced on a separate stack.  It allows the architecture to switch the
 * notion of the current task on a cpu in a non-blocking manner.  This function
 * must be called with all CPU's synchronised, and interrupts disabled, the
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

/*
 * Use precise platform statistics if available:
 */
#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
void task_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	*ut = p->utime;
	*st = p->stime;
}
EXPORT_SYMBOL_GPL(task_cputime_adjusted);

void thread_group_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	struct task_cputime cputime;

	thread_group_cputime(p, &cputime);

	*ut = cputime.utime;
	*st = cputime.stime;
}

void vtime_account_system_irqsafe(struct task_struct *tsk)
{
	unsigned long flags;

	local_irq_save(flags);
	vtime_account_system(tsk);
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(vtime_account_system_irqsafe);
#else /* CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */
/*
 * Perform (stime * rtime) / total, but avoid multiplication overflow by
 * losing precision when the numbers are big.
 */
static cputime_t scale_stime(u64 stime, u64 rtime, u64 total)
{
	u64 scaled;

	for (;;) {
		/* Make sure "rtime" is the bigger of stime/rtime */
		if (stime > rtime) {
			u64 tmp = rtime; rtime = stime; stime = tmp;
		}

		/* Make sure 'total' fits in 32 bits */
		if (total >> 32)
			goto drop_precision;

		/* Does rtime (and thus stime) fit in 32 bits? */
		if (!(rtime >> 32))
			break;

		/* Can we just balance rtime/stime rather than dropping bits? */
		if (stime >> 31)
			goto drop_precision;

		/* We can grow stime and shrink rtime and try to make them both fit */
		stime <<= 1;
		rtime >>= 1;
		continue;

drop_precision:
		/* We drop from rtime, it has more bits than stime */
		rtime >>= 1;
		total >>= 1;
	}

	/*
	 * Make sure gcc understands that this is a 32x32->64 multiply,
	 * followed by a 64/32->64 divide.
	 */
	scaled = div_u64((u64) (u32) stime * (u64) (u32) rtime, (u32)total);
	return (__force cputime_t) scaled;
}

/*
 * Adjust tick based cputime random precision against scheduler
 * runtime accounting.
 */
static void cputime_adjust(struct task_cputime *curr,
			   struct prev_cputime *prev,
			   cputime_t *ut, cputime_t *st)
{
	cputime_t rtime, stime, utime, total;

	stime = curr->stime;
	total = stime + curr->utime;

	/*
	 * Tick based cputime accounting depend on random scheduling
	 * timeslices of a task to be interrupted or not by the timer.
	 * Depending on these circumstances, the number of these interrupts
	 * may be over or under-optimistic, matching the real user and system
	 * cputime with a variable precision.
	 *
	 * Fix this by scaling these tick based values against the total
	 * runtime accounted by the CFS scheduler.
	 */
	rtime = nsecs_to_cputime(curr->sum_exec_runtime);

	/*
	 * Update userspace visible utime/stime values only if actual execution
	 * time is bigger than already exported. Note that can happen, that we
	 * provided bigger values due to scaling inaccuracy on big numbers.
	 */
	if (prev->stime + prev->utime >= rtime)
		goto out;

	if (total) {
		stime = scale_stime((__force u64)stime,
				    (__force u64)rtime, (__force u64)total);
		utime = rtime - stime;
	} else {
		stime = rtime;
		utime = 0;
	}

	/*
	 * If the tick based count grows faster than the scheduler one,
	 * the result of the scaling may go backward.
	 * Let's enforce monotonicity.
	 */
	prev->stime = max(prev->stime, stime);
	prev->utime = max(prev->utime, utime);

out:
	*ut = prev->utime;
	*st = prev->stime;
}

void task_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	struct task_cputime cputime = {
		.sum_exec_runtime = tsk_seruntime(p),
	};

	task_cputime(p, &cputime.utime, &cputime.stime);
	cputime_adjust(&cputime, &p->prev_cputime, ut, st);
}
EXPORT_SYMBOL_GPL(task_cputime_adjusted);

/*
 * Must be called with siglock held.
 */
void thread_group_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	struct task_cputime cputime;

	thread_group_cputime(p, &cputime);
	cputime_adjust(&cputime, &p->signal->prev_cputime, ut, st);
}
#endif /* CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */

void init_idle_bootup_task(struct task_struct *idle)
{}

#ifdef CONFIG_SCHED_DEBUG
void proc_sched_show_task(struct task_struct *p, struct seq_file *m)
{}

void proc_sched_set_task(struct task_struct *p)
{}
#endif

#ifdef CONFIG_SMP
#define SCHED_LOAD_SHIFT	(10)
#define SCHED_LOAD_SCALE	(1L << SCHED_LOAD_SHIFT)

unsigned long default_scale_freq_power(struct sched_domain *sd, int cpu)
{
	return SCHED_LOAD_SCALE;
}

unsigned long default_scale_smt_power(struct sched_domain *sd, int cpu)
{
	unsigned long weight = cpumask_weight(sched_domain_span(sd));
	unsigned long smt_gain = sd->smt_gain;

	smt_gain /= weight;

	return smt_gain;
}
#endif

#ifdef CONFIG_CGROUP_SCHED
static void sched_free_group(struct task_group *tg)
{
	kmem_cache_free(task_group_cache, tg);
}

/* allocate runqueue etc for a new task group */
struct task_group *sched_create_group(struct task_group *parent)
{
	struct task_group *tg;

	tg = kmem_cache_alloc(task_group_cache, GFP_KERNEL | __GFP_ZERO);
	if (!tg)
		return ERR_PTR(-ENOMEM);

	return tg;
}

void sched_online_group(struct task_group *tg, struct task_group *parent)
{
}

/* rcu callback to free various structures associated with a task group */
static void sched_free_group_rcu(struct rcu_head *rhp)
{
	/* now it should be safe to free those cfs_rqs */
	sched_free_group(container_of(rhp, struct task_group, rcu));
}

void sched_destroy_group(struct task_group *tg)
{
	/* wait for possible concurrent references to cfs_rqs complete */
	call_rcu(&tg->rcu, sched_free_group_rcu);
}

void sched_offline_group(struct task_group *tg)
{
}

static inline struct task_group *css_tg(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct task_group, css) : NULL;
}

static struct cgroup_subsys_state *
cpu_cgroup_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct task_group *parent = css_tg(parent_css);
	struct task_group *tg;

	if (!parent) {
		/* This is early initialization for the top cgroup */
		return &root_task_group.css;
	}

	tg = sched_create_group(parent);
	if (IS_ERR(tg))
		return ERR_PTR(-ENOMEM);
	return &tg->css;
}

static void cpu_cgroup_css_released(struct cgroup_subsys_state *css)
{
	struct task_group *tg = css_tg(css);

	sched_offline_group(tg);
}

static void cpu_cgroup_css_free(struct cgroup_subsys_state *css)
{
	struct task_group *tg = css_tg(css);

	/*
	 * Relies on the RCU grace period between css_released() and this.
	 */
	sched_free_group(tg);
}

static void cpu_cgroup_fork(struct task_struct *task)
{
}

static int cpu_cgroup_can_attach(struct cgroup_taskset *tset)
{
	return 0;
}

static void cpu_cgroup_attach(struct cgroup_taskset *tset)
{
}

static struct cftype cpu_files[] = {
	{ }	/* terminate */
};

struct cgroup_subsys cpu_cgrp_subsys = {
	.css_alloc	= cpu_cgroup_css_alloc,
	.css_released	= cpu_cgroup_css_released,
	.css_free	= cpu_cgroup_css_free,
	.fork		= cpu_cgroup_fork,
	.can_attach	= cpu_cgroup_can_attach,
	.attach		= cpu_cgroup_attach,
	.legacy_cftypes	= cpu_files,
	.early_init	= true,
};
#endif	/* CONFIG_CGROUP_SCHED */
