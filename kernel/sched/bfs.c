/*
 *  kernel/sched/bfs.c, was kernel/sched.c
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
 *  now		Brainfuck deadline scheduling policy by Con Kolivas deletes
 *              a whole lot of those previous things.
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/highmem.h>
#include <asm/mmu_context.h>
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

#define rt_prio(prio)		unlikely((prio) < MAX_RT_PRIO)
#define rt_task(p)		rt_prio((p)->prio)
#define rt_queue(rq)		rt_prio((rq)->rq_prio)
#define batch_task(p)		(unlikely((p)->policy == SCHED_BATCH))
#define is_rt_policy(policy)	((policy) == SCHED_FIFO || \
					(policy) == SCHED_RR)
#define has_rt_policy(p)	unlikely(is_rt_policy((p)->policy))
#define idleprio_task(p)	unlikely((p)->policy == SCHED_IDLEPRIO)
#define iso_task(p)		unlikely((p)->policy == SCHED_ISO)
#define iso_queue(rq)		unlikely((rq)->rq_policy == SCHED_ISO)
#define rq_running_iso(rq)	((rq)->rq_prio == ISO_PRIO)

#define ISO_PERIOD		((5 * HZ * grq.noc) + 1)

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
#define USER_PRIO(p)		((p) - MAX_RT_PRIO)
#define TASK_USER_PRIO(p)	USER_PRIO((p)->static_prio)
#define MAX_USER_PRIO		(USER_PRIO(MAX_PRIO))
#define SCHED_PRIO(p)		((p) + MAX_RT_PRIO)
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
	printk(KERN_INFO "BFS CPU scheduler v0.440 by Con Kolivas.\n");
}

/*
 * This is the time all tasks within the same priority round robin.
 * Value is in ms and set to a minimum of 6ms. Scales with number of cpus.
 * Tunable via /proc interface.
 */
int rr_interval __read_mostly = 6;

/*
 * sched_iso_cpu - sysctl which determines the cpu percentage SCHED_ISO tasks
 * are allowed to run five seconds as real time tasks. This is the total over
 * all online cpus.
 */
int sched_iso_cpu __read_mostly = 70;

/*
 * The relative length of deadline for each priority(nice) level.
 */
static int prio_ratios[PRIO_RANGE] __read_mostly;

/*
 * The quota handed out to tasks of all priority levels when refilling their
 * time_slice.
 */
static inline int timeslice(void)
{
	return MS_TO_US(rr_interval);
}

/*
 * The global runqueue data that all CPUs work off. Data is protected either
 * by the global grq lock, or the discrete lock that precedes the data in this
 * struct.
 */
struct global_rq {
	raw_spinlock_t lock;
	unsigned long nr_running;
	unsigned long nr_uninterruptible;
	unsigned long long nr_switches;
	struct list_head queue[PRIO_LIMIT];
	DECLARE_BITMAP(prio_bitmap, PRIO_LIMIT + 1);
#ifdef CONFIG_SMP
	unsigned long qnr; /* queued not running */
	cpumask_t cpu_idle_map;
	bool idle_cpus;
#endif
	int noc; /* num_online_cpus stored and updated when it changes */
	u64 niffies; /* Nanosecond jiffies */
	unsigned long last_jiffy; /* Last jiffy we updated niffies */

	raw_spinlock_t iso_lock;
	int iso_ticks;
	bool iso_refractory;
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

/*
 * This is the main, per-CPU runqueue data structure.
 * This data should only be modified by the local cpu.
 */
struct rq {
	struct task_struct *curr, *idle, *stop;
	struct mm_struct *prev_mm;

	/* Stored data about rq->curr to work outside grq lock */
	u64 rq_deadline;
	unsigned int rq_policy;
	int rq_time_slice;
	u64 rq_last_ran;
	int rq_prio;
	bool rq_running; /* There is a task running */

	/* Accurate timekeeping data */
	u64 timekeep_clock;
	unsigned long user_pc, nice_pc, irq_pc, softirq_pc, system_pc,
		iowait_pc, idle_pc;
	atomic_t nr_iowait;

#ifdef CONFIG_SMP
	int cpu;		/* cpu of this runqueue */
	bool online;
	bool scaling; /* This CPU is managed by a scaling CPU freq governor */
	struct task_struct *sticky_task;

	struct root_domain *rd;
	struct sched_domain *sd;
	int *cpu_locality; /* CPU relative cache distance */
#ifdef CONFIG_SCHED_SMT
	bool (*siblings_idle)(int cpu);
	/* See if all smt siblings are idle */
	cpumask_t smt_siblings;
#endif /* CONFIG_SCHED_SMT */
#ifdef CONFIG_SCHED_MC
	bool (*cache_idle)(int cpu);
	/* See if all cache siblings are idle */
	cpumask_t cache_siblings;
#endif /* CONFIG_SCHED_MC */
	u64 last_niffy; /* Last time this RQ updated grq.niffies */
#endif /* CONFIG_SMP */
#ifdef CONFIG_IRQ_TIME_ACCOUNTING
	u64 prev_irq_time;
#endif /* CONFIG_IRQ_TIME_ACCOUNTING */
#ifdef CONFIG_PARAVIRT
	u64 prev_steal_time;
#endif /* CONFIG_PARAVIRT */
#ifdef CONFIG_PARAVIRT_TIME_ACCOUNTING
	u64 prev_steal_time_rq;
#endif /* CONFIG_PARAVIRT_TIME_ACCOUNTING */

	u64 clock, old_clock, last_tick;
	u64 clock_task;
	bool dither;

#ifdef CONFIG_SCHEDSTATS

	/* latency stats */
	struct sched_info rq_sched_info;
	unsigned long long rq_cpu_time;
	/* could above be rq->cfs_rq.exec_clock + rq->rt_rq.rt_runtime ? */

	/* sys_sched_yield() stats */
	unsigned int yld_count;

	/* schedule() stats */
	unsigned int sched_switch;
	unsigned int sched_count;
	unsigned int sched_goidle;

	/* try_to_wake_up() stats */
	unsigned int ttwu_count;
	unsigned int ttwu_local;
#endif /* CONFIG_SCHEDSTATS */
};

DEFINE_PER_CPU_SHARED_ALIGNED(struct rq, runqueues);
static DEFINE_MUTEX(sched_hotcpu_mutex);

#ifdef CONFIG_SMP
#define cpu_rq(cpu)		(&per_cpu(runqueues, (cpu)))
#define this_rq()		(&__get_cpu_var(runqueues))
#define task_rq(p)		cpu_rq(task_cpu(p))
#define cpu_curr(cpu)		(cpu_rq(cpu)->curr)
/*
 * sched_domains_mutex serialises calls to init_sched_domains,
 * detach_destroy_domains and partition_sched_domains.
 */
static DEFINE_MUTEX(sched_domains_mutex);

/*
 * By default the system creates a single root-domain with all cpus as
 * members (mimicking the global state we have today).
 */
static struct root_domain def_root_domain;

int __weak arch_sd_sibling_asym_packing(void)
{
       return 0*SD_ASYM_PACKING;
}
#endif /* CONFIG_SMP */

#define rcu_dereference_check_sched_domain(p) \
	rcu_dereference_check((p), \
			      lockdep_is_held(&sched_domains_mutex))

/*
 * The domain tree (rq->sd) is protected by RCU's quiescent state transition.
 * See detach_destroy_domains: synchronize_sched for details.
 *
 * The domain tree of any CPU may only be accessed from within
 * preempt-disabled sections.
 */
#define for_each_domain(cpu, __sd) \
	for (__sd = rcu_dereference_check_sched_domain(cpu_rq(cpu)->sd); __sd; __sd = __sd->parent)

static inline void update_rq_clock(struct rq *rq);

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

/*
 * Niffies are a globally increasing nanosecond counter. Whenever a runqueue
 * clock is updated with the grq.lock held, it is an opportunity to update the
 * niffies value. Any CPU can update it by adding how much its clock has
 * increased since it last updated niffies, minus any added niffies by other
 * CPUs.
 */
static inline void update_clocks(struct rq *rq)
{
	s64 ndiff;
	long jdiff;

	update_rq_clock(rq);
	ndiff = rq->clock - rq->old_clock;
	/* old_clock is only updated when we are updating niffies */
	rq->old_clock = rq->clock;
	ndiff -= grq.niffies - rq->last_niffy;
	jdiff = jiffies - grq.last_jiffy;
	niffy_diff(&ndiff, jdiff);
	grq.last_jiffy += jdiff;
	grq.niffies += ndiff;
	rq->last_niffy = grq.niffies;
}
#else /* CONFIG_SMP */
static struct rq *uprq;
#define cpu_rq(cpu)	(uprq)
#define this_rq()	(uprq)
#define task_rq(p)	(uprq)
#define cpu_curr(cpu)	((uprq)->curr)
static inline int cpu_of(struct rq *rq)
{
	return 0;
}

static inline void update_clocks(struct rq *rq)
{
	s64 ndiff;
	long jdiff;

	update_rq_clock(rq);
	ndiff = rq->clock - rq->old_clock;
	rq->old_clock = rq->clock;
	jdiff = jiffies - grq.last_jiffy;
	niffy_diff(&ndiff, jdiff);
	grq.last_jiffy += jdiff;
	grq.niffies += ndiff;
}
#endif
#define raw_rq()	(&__raw_get_cpu_var(runqueues))

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
 * All common locking functions performed on grq.lock. rq->clock is local to
 * the CPU accessing it so it can be modified just with interrupts disabled
 * when we're not updating niffies.
 * Looking up task_rq must be done under grq.lock to be safe.
 */
static void update_rq_clock_task(struct rq *rq, s64 delta);

static inline void update_rq_clock(struct rq *rq)
{
	s64 delta = sched_clock_cpu(cpu_of(rq)) - rq->clock;

	rq->clock += delta;
	update_rq_clock_task(rq, delta);
}

static inline bool task_running(struct task_struct *p)
{
	return p->on_cpu;
}

static inline void grq_lock(void)
	__acquires(grq.lock)
{
	raw_spin_lock(&grq.lock);
}

static inline void grq_unlock(void)
	__releases(grq.lock)
{
	raw_spin_unlock(&grq.lock);
}

static inline void grq_lock_irq(void)
	__acquires(grq.lock)
{
	raw_spin_lock_irq(&grq.lock);
}

static inline void time_lock_grq(struct rq *rq)
	__acquires(grq.lock)
{
	grq_lock();
	update_clocks(rq);
}

static inline void grq_unlock_irq(void)
	__releases(grq.lock)
{
	raw_spin_unlock_irq(&grq.lock);
}

static inline void grq_lock_irqsave(unsigned long *flags)
	__acquires(grq.lock)
{
	raw_spin_lock_irqsave(&grq.lock, *flags);
}

static inline void grq_unlock_irqrestore(unsigned long *flags)
	__releases(grq.lock)
{
	raw_spin_unlock_irqrestore(&grq.lock, *flags);
}

static inline struct rq
*task_grq_lock(struct task_struct *p, unsigned long *flags)
	__acquires(grq.lock)
{
	grq_lock_irqsave(flags);
	return task_rq(p);
}

static inline struct rq
*time_task_grq_lock(struct task_struct *p, unsigned long *flags)
	__acquires(grq.lock)
{
	struct rq *rq = task_grq_lock(p, flags);
	update_clocks(rq);
	return rq;
}

static inline struct rq *task_grq_lock_irq(struct task_struct *p)
	__acquires(grq.lock)
{
	grq_lock_irq();
	return task_rq(p);
}

static inline void time_task_grq_lock_irq(struct task_struct *p)
	__acquires(grq.lock)
{
	struct rq *rq = task_grq_lock_irq(p);
	update_clocks(rq);
}

static inline void task_grq_unlock_irq(void)
	__releases(grq.lock)
{
	grq_unlock_irq();
}

static inline void task_grq_unlock(unsigned long *flags)
	__releases(grq.lock)
{
	grq_unlock_irqrestore(flags);
}

/**
 * grunqueue_is_locked
 *
 * Returns true if the global runqueue is locked.
 * This interface allows printk to be called with the runqueue lock
 * held and know whether or not it is OK to wake up the klogd.
 */
bool grunqueue_is_locked(void)
{
	return raw_spin_is_locked(&grq.lock);
}

void grq_unlock_wait(void)
	__releases(grq.lock)
{
	smp_mb(); /* spin-unlock-wait is not a full memory barrier */
	raw_spin_unlock_wait(&grq.lock);
}

static inline void time_grq_lock(struct rq *rq, unsigned long *flags)
	__acquires(grq.lock)
{
	local_irq_save(*flags);
	time_lock_grq(rq);
}

static inline struct rq *__task_grq_lock(struct task_struct *p)
	__acquires(grq.lock)
{
	grq_lock();
	return task_rq(p);
}

static inline void __task_grq_unlock(void)
	__releases(grq.lock)
{
	grq_unlock();
}

/*
 * Look for any tasks *anywhere* that are running nice 0 or better. We do
 * this lockless for overhead reasons since the occasional wrong result
 * is harmless.
 */
bool above_background_load(void)
{
	int cpu;

	for_each_online_cpu(cpu) {
		struct task_struct *cpu_curr = cpu_rq(cpu)->curr;

		if (unlikely(!cpu_curr))
			continue;
		if (PRIO_TO_NICE(cpu_curr->static_prio) < 1) {
			return true;
		}
	}
	return false;
}

#ifndef __ARCH_WANT_UNLOCKED_CTXSW
static inline void prepare_lock_switch(struct rq *rq, struct task_struct *next)
{
}

static inline void finish_lock_switch(struct rq *rq, struct task_struct *prev)
{
#ifdef CONFIG_DEBUG_SPINLOCK
	/* this is a valid case when another task releases the spinlock */
	grq.lock.owner = current;
#endif
	/*
	 * If we are tracking spinlock dependencies then we have to
	 * fix up the runqueue lock - which gets 'carried over' from
	 * prev into current:
	 */
	spin_acquire(&grq.lock.dep_map, 0, 0, _THIS_IP_);

	grq_unlock_irq();
}

#else /* __ARCH_WANT_UNLOCKED_CTXSW */

static inline void prepare_lock_switch(struct rq *rq, struct task_struct *next)
{
#ifdef __ARCH_WANT_INTERRUPTS_ON_CTXSW
	grq_unlock_irq();
#else
	grq_unlock();
#endif
}

static inline void finish_lock_switch(struct rq *rq, struct task_struct *prev)
{
	smp_wmb();
#ifndef __ARCH_WANT_INTERRUPTS_ON_CTXSW
	local_irq_enable();
#endif
}
#endif /* __ARCH_WANT_UNLOCKED_CTXSW */

static inline bool deadline_before(u64 deadline, u64 time)
{
	return (deadline < time);
}

static inline bool deadline_after(u64 deadline, u64 time)
{
	return (deadline > time);
}

/*
 * A task that is queued but not running will be on the grq run list.
 * A task that is not running or queued will not be on the grq run list.
 * A task that is currently running will have ->on_cpu set but not on the
 * grq run list.
 */
static inline bool task_queued(struct task_struct *p)
{
	return (!list_empty(&p->run_list));
}

/*
 * Removing from the global runqueue. Enter with grq locked.
 */
static void dequeue_task(struct task_struct *p)
{
	list_del_init(&p->run_list);
	if (list_empty(grq.queue + p->prio))
		__clear_bit(p->prio, grq.prio_bitmap);
}

/*
 * To determine if it's safe for a task of SCHED_IDLEPRIO to actually run as
 * an idle task, we ensure none of the following conditions are met.
 */
static bool idleprio_suitable(struct task_struct *p)
{
	return (!freezing(p) && !signal_pending(p) &&
		!(task_contributes_to_load(p)) && !(p->flags & (PF_EXITING)));
}

/*
 * To determine if a task of SCHED_ISO can run in pseudo-realtime, we check
 * that the iso_refractory flag is not set.
 */
static bool isoprio_suitable(void)
{
	return !grq.iso_refractory;
}

/*
 * Adding to the global runqueue. Enter with grq locked.
 */
static void enqueue_task(struct task_struct *p)
{
	if (!rt_task(p)) {
		/* Check it hasn't gotten rt from PI */
		if ((idleprio_task(p) && idleprio_suitable(p)) ||
		   (iso_task(p) && isoprio_suitable()))
			p->prio = p->normal_prio;
		else
			p->prio = NORMAL_PRIO;
	}
	__set_bit(p->prio, grq.prio_bitmap);
	list_add_tail(&p->run_list, grq.queue + p->prio);
	sched_info_queued(p);
}

/* Only idle task does this as a real time task*/
static inline void enqueue_task_head(struct task_struct *p)
{
	__set_bit(p->prio, grq.prio_bitmap);
	list_add(&p->run_list, grq.queue + p->prio);
	sched_info_queued(p);
}

static inline void requeue_task(struct task_struct *p)
{
	sched_info_queued(p);
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

#ifdef CONFIG_SMP
/*
 * qnr is the "queued but not running" count which is the total number of
 * tasks on the global runqueue list waiting for cpu time but not actually
 * currently running on a cpu.
 */
static inline void inc_qnr(void)
{
	grq.qnr++;
}

static inline void dec_qnr(void)
{
	grq.qnr--;
}

static inline int queued_notrunning(void)
{
	return grq.qnr;
}

/*
 * The cpu_idle_map stores a bitmap of all the CPUs currently idle to
 * allow easy lookup of whether any suitable idle CPUs are available.
 * It's cheaper to maintain a binary yes/no if there are any idle CPUs on the
 * idle_cpus variable than to do a full bitmask check when we are busy.
 */
static inline void set_cpuidle_map(int cpu)
{
	if (likely(cpu_online(cpu))) {
		cpu_set(cpu, grq.cpu_idle_map);
		grq.idle_cpus = true;
	}
}

static inline void clear_cpuidle_map(int cpu)
{
	cpu_clear(cpu, grq.cpu_idle_map);
	if (cpus_empty(grq.cpu_idle_map))
		grq.idle_cpus = false;
}

static bool suitable_idle_cpus(struct task_struct *p)
{
	if (!grq.idle_cpus)
		return false;
	return (cpus_intersects(p->cpus_allowed, grq.cpu_idle_map));
}

#define CPUIDLE_DIFF_THREAD	(1)
#define CPUIDLE_DIFF_CORE	(2)
#define CPUIDLE_CACHE_BUSY	(4)
#define CPUIDLE_DIFF_CPU	(8)
#define CPUIDLE_THREAD_BUSY	(16)
#define CPUIDLE_DIFF_NODE	(32)

static void resched_task(struct task_struct *p);

/*
 * The best idle CPU is chosen according to the CPUIDLE ranking above where the
 * lowest value would give the most suitable CPU to schedule p onto next. The
 * order works out to be the following:
 *
 * Same core, idle or busy cache, idle or busy threads
 * Other core, same cache, idle or busy cache, idle threads.
 * Same node, other CPU, idle cache, idle threads.
 * Same node, other CPU, busy cache, idle threads.
 * Other core, same cache, busy threads.
 * Same node, other CPU, busy threads.
 * Other node, other CPU, idle cache, idle threads.
 * Other node, other CPU, busy cache, idle threads.
 * Other node, other CPU, busy threads.
 */
static void
resched_best_mask(int best_cpu, struct rq *rq, cpumask_t *tmpmask)
{
	unsigned int best_ranking = CPUIDLE_DIFF_NODE | CPUIDLE_THREAD_BUSY |
		CPUIDLE_DIFF_CPU | CPUIDLE_CACHE_BUSY | CPUIDLE_DIFF_CORE |
		CPUIDLE_DIFF_THREAD;
	int cpu_tmp;

	if (cpu_isset(best_cpu, *tmpmask))
		goto out;

	for_each_cpu_mask(cpu_tmp, *tmpmask) {
		unsigned int ranking;
		struct rq *tmp_rq;

		ranking = 0;
		tmp_rq = cpu_rq(cpu_tmp);

#ifdef CONFIG_NUMA
		if (rq->cpu_locality[cpu_tmp] > 3)
			ranking |= CPUIDLE_DIFF_NODE;
		else
#endif
		if (rq->cpu_locality[cpu_tmp] > 2)
			ranking |= CPUIDLE_DIFF_CPU;
#ifdef CONFIG_SCHED_MC
		if (rq->cpu_locality[cpu_tmp] == 2)
			ranking |= CPUIDLE_DIFF_CORE;
		if (!(tmp_rq->cache_idle(cpu_tmp)))
			ranking |= CPUIDLE_CACHE_BUSY;
#endif
#ifdef CONFIG_SCHED_SMT
		if (rq->cpu_locality[cpu_tmp] == 1)
			ranking |= CPUIDLE_DIFF_THREAD;
		if (!(tmp_rq->siblings_idle(cpu_tmp)))
			ranking |= CPUIDLE_THREAD_BUSY;
#endif
		if (ranking < best_ranking) {
			best_cpu = cpu_tmp;
			best_ranking = ranking;
		}
	}
out:
	resched_task(cpu_rq(best_cpu)->curr);
}

bool cpus_share_cache(int this_cpu, int that_cpu)
{
	struct rq *this_rq = cpu_rq(this_cpu);

	return (this_rq->cpu_locality[that_cpu] < 3);
}

static void resched_best_idle(struct task_struct *p)
{
	cpumask_t tmpmask;

	cpus_and(tmpmask, p->cpus_allowed, grq.cpu_idle_map);
	resched_best_mask(task_cpu(p), task_rq(p), &tmpmask);
}

static inline void resched_suitable_idle(struct task_struct *p)
{
	if (suitable_idle_cpus(p))
		resched_best_idle(p);
}
/*
 * Flags to tell us whether this CPU is running a CPU frequency governor that
 * has slowed its speed or not. No locking required as the very rare wrongly
 * read value would be harmless.
 */
void cpu_scaling(int cpu)
{
	cpu_rq(cpu)->scaling = true;
}

void cpu_nonscaling(int cpu)
{
	cpu_rq(cpu)->scaling = false;
}

static inline bool scaling_rq(struct rq *rq)
{
	return rq->scaling;
}

static inline int locality_diff(struct task_struct *p, struct rq *rq)
{
	return rq->cpu_locality[task_cpu(p)];
}
#else /* CONFIG_SMP */
static inline void inc_qnr(void)
{
}

static inline void dec_qnr(void)
{
}

static inline int queued_notrunning(void)
{
	return grq.nr_running;
}

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

void cpu_scaling(int __unused)
{
}

void cpu_nonscaling(int __unused)
{
}

/*
 * Although CPUs can scale in UP, there is nowhere else for tasks to go so this
 * always returns 0.
 */
static inline bool scaling_rq(struct rq *rq)
{
	return false;
}

static inline int locality_diff(struct task_struct *p, struct rq *rq)
{
	return 0;
}
#endif /* CONFIG_SMP */
EXPORT_SYMBOL_GPL(cpu_scaling);
EXPORT_SYMBOL_GPL(cpu_nonscaling);

/*
 * activate_idle_task - move idle task to the _front_ of runqueue.
 */
static inline void activate_idle_task(struct task_struct *p)
{
	enqueue_task_head(p);
	grq.nr_running++;
	inc_qnr();
}

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
 * activate_task - move a task to the runqueue. Enter with grq locked.
 */
static void activate_task(struct task_struct *p, struct rq *rq)
{
	update_clocks(rq);

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
		grq.nr_uninterruptible--;
	enqueue_task(p);
	grq.nr_running++;
	inc_qnr();
}

static inline void clear_sticky(struct task_struct *p);

/*
 * deactivate_task - If it's running, it's not on the grq and we can just
 * decrement the nr_running. Enter with grq locked.
 */
static inline void deactivate_task(struct task_struct *p)
{
	if (task_contributes_to_load(p))
		grq.nr_uninterruptible++;
	grq.nr_running--;
	clear_sticky(p);
}

static ATOMIC_NOTIFIER_HEAD(task_migration_notifier);

void register_task_migration_notifier(struct notifier_block *n)
{
	atomic_notifier_chain_register(&task_migration_notifier, n);
}

#ifdef CONFIG_SMP
void set_task_cpu(struct task_struct *p, unsigned int cpu)
{
#ifdef CONFIG_LOCKDEP
	/*
	 * The caller should hold grq lock.
	 */
	WARN_ON_ONCE(debug_locks && !lockdep_is_held(&grq.lock));
#endif
	trace_sched_migrate_task(p, cpu);
	if (task_cpu(p) != cpu)
		perf_sw_event(PERF_COUNT_SW_CPU_MIGRATIONS, 1, NULL, 0);

	/*
	 * After ->cpu is set up to a new value, task_grq_lock(p, ...) can be
	 * successfully executed on another CPU. We must ensure that updates of
	 * per-task data have been completed by this moment.
	 */
	smp_wmb();
	task_thread_info(p)->cpu = cpu;
}

static inline void clear_sticky(struct task_struct *p)
{
	p->sticky = false;
}

static inline bool task_sticky(struct task_struct *p)
{
	return p->sticky;
}

/* Reschedule the best idle CPU that is not this one. */
static void
resched_closest_idle(struct rq *rq, int cpu, struct task_struct *p)
{
	cpumask_t tmpmask;

	cpus_and(tmpmask, p->cpus_allowed, grq.cpu_idle_map);
	cpu_clear(cpu, tmpmask);
	if (cpus_empty(tmpmask))
		return;
	resched_best_mask(cpu, rq, &tmpmask);
}

/*
 * We set the sticky flag on a task that is descheduled involuntarily meaning
 * it is awaiting further CPU time. If the last sticky task is still sticky
 * but unlucky enough to not be the next task scheduled, we unstick it and try
 * to find it an idle CPU. Realtime tasks do not stick to minimise their
 * latency at all times.
 */
static inline void
swap_sticky(struct rq *rq, int cpu, struct task_struct *p)
{
	if (rq->sticky_task) {
		if (rq->sticky_task == p) {
			p->sticky = true;
			return;
		}
		if (task_sticky(rq->sticky_task)) {
			clear_sticky(rq->sticky_task);
			resched_closest_idle(rq, cpu, rq->sticky_task);
		}
	}
	if (!rt_task(p)) {
		p->sticky = true;
		rq->sticky_task = p;
	} else {
		resched_closest_idle(rq, cpu, p);
		rq->sticky_task = NULL;
	}
}

static inline void unstick_task(struct rq *rq, struct task_struct *p)
{
	rq->sticky_task = NULL;
	clear_sticky(p);
}
#else
static inline void clear_sticky(struct task_struct *p)
{
}

static inline bool task_sticky(struct task_struct *p)
{
	return false;
}

static inline void
swap_sticky(struct rq *rq, int cpu, struct task_struct *p)
{
}

static inline void unstick_task(struct rq *rq, struct task_struct *p)
{
}
#endif

/*
 * Move a task off the global queue and take it to a cpu for it will
 * become the running task.
 */
static inline void take_task(int cpu, struct task_struct *p)
{
	set_task_cpu(p, cpu);
	dequeue_task(p);
	clear_sticky(p);
	dec_qnr();
}

/*
 * Returns a descheduling task to the grq runqueue unless it is being
 * deactivated.
 */
static inline void return_task(struct task_struct *p, bool deactivate)
{
	if (deactivate)
		deactivate_task(p);
	else {
		inc_qnr();
		enqueue_task(p);
	}
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
#define tsk_is_polling(t) 0
#endif

static void resched_task(struct task_struct *p)
{
	int cpu;

	assert_raw_spin_locked(&grq.lock);

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

#else
static inline void resched_task(struct task_struct *p)
{
	assert_raw_spin_locked(&grq.lock);
	set_tsk_need_resched(p);
}
#endif

/**
 * task_curr - is this task currently executing on a CPU?
 * @p: the task in question.
 */
inline int task_curr(const struct task_struct *p)
{
	return cpu_curr(task_cpu(p)) == p;
}

#ifdef CONFIG_SMP
struct migration_req {
	struct task_struct *task;
	int dest_cpu;
};

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
	unsigned long flags;
	bool running, on_rq;
	unsigned long ncsw;
	struct rq *rq;

	for (;;) {
		/*
		 * We do the initial early heuristics without holding
		 * any task-queue locks at all. We'll only try to get
		 * the runqueue lock when things look like they will
		 * work out! In the unlikely event rq is dereferenced
		 * since we're lockless, grab it again.
		 */
#ifdef CONFIG_SMP
retry_rq:
		rq = task_rq(p);
		if (unlikely(!rq))
			goto retry_rq;
#else /* CONFIG_SMP */
		rq = task_rq(p);
#endif
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
		while (task_running(p) && p == rq->curr) {
			if (match_state && unlikely(p->state != match_state))
				return 0;
			cpu_relax();
		}

		/*
		 * Ok, time to look more closely! We need the grq
		 * lock now, to be *sure*. If we're wrong, we'll
		 * just go back and repeat.
		 */
		rq = task_grq_lock(p, &flags);
		trace_sched_wait_task(p);
		running = task_running(p);
		on_rq = task_queued(p);
		ncsw = 0;
		if (!match_state || p->state == match_state)
			ncsw = p->nvcsw | LONG_MIN; /* sets MSB */
		task_grq_unlock(&flags);

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
		if (unlikely(on_rq)) {
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

#define rq_idle(rq)	((rq)->rq_prio == PRIO_LIMIT)

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
	/* SCHED_NORMAL, BATCH and ISO will preempt based on deadline */
	if (!deadline_before(p->deadline, deadline))
		return false;
	return true;
}

#ifdef CONFIG_SMP
#define cpu_online_map		(*(cpumask_t *)cpu_online_mask)
#ifdef CONFIG_HOTPLUG_CPU
/*
 * Check to see if there is a task that is affined only to offline CPUs but
 * still wants runtime. This happens to kernel threads during suspend/halt and
 * disabling of CPUs.
 */
static inline bool online_cpus(struct task_struct *p)
{
	return (likely(cpus_intersects(cpu_online_map, p->cpus_allowed)));
}
#else /* CONFIG_HOTPLUG_CPU */
/* All available CPUs are always online without hotplug. */
static inline bool online_cpus(struct task_struct *p)
{
	return true;
}
#endif

/*
 * Check to see if p can run on cpu, and if not, whether there are any online
 * CPUs it can run on instead.
 */
static inline bool needs_other_cpu(struct task_struct *p, int cpu)
{
	if (unlikely(!cpu_isset(cpu, p->cpus_allowed)))
		return true;
	return false;
}

/*
 * When all else is equal, still prefer this_rq.
 */
static void try_preempt(struct task_struct *p, struct rq *this_rq)
{
	struct rq *highest_prio_rq = NULL;
	int cpu, highest_prio;
	u64 latest_deadline;
	cpumask_t tmp;

	/*
	 * We clear the sticky flag here because for a task to have called
	 * try_preempt with the sticky flag enabled means some complicated
	 * re-scheduling has occurred and we should ignore the sticky flag.
	 */
	clear_sticky(p);

	if (suitable_idle_cpus(p)) {
		resched_best_idle(p);
		return;
	}

	/* IDLEPRIO tasks never preempt anything but idle */
	if (p->policy == SCHED_IDLEPRIO)
		return;

	if (likely(online_cpus(p)))
		cpus_and(tmp, cpu_online_map, p->cpus_allowed);
	else
		return;

	highest_prio = latest_deadline = 0;

	for_each_cpu_mask(cpu, tmp) {
		struct rq *rq;
		int rq_prio;

		rq = cpu_rq(cpu);
		rq_prio = rq->rq_prio;
		if (rq_prio < highest_prio)
			continue;

		if (rq_prio > highest_prio ||
		    deadline_after(rq->rq_deadline, latest_deadline)) {
			latest_deadline = rq->rq_deadline;
			highest_prio = rq_prio;
			highest_prio_rq = rq;
		}
	}

	if (likely(highest_prio_rq)) {
		if (can_preempt(p, highest_prio, highest_prio_rq->rq_deadline))
			resched_task(highest_prio_rq->curr);
	}
}
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
		resched_task(uprq->curr);
}
#endif /* CONFIG_SMP */

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

static inline void ttwu_activate(struct task_struct *p, struct rq *rq,
				 bool is_sync)
{
	activate_task(p, rq);

	/*
	 * Sync wakeups (i.e. those types of wakeups where the waker
	 * has indicated that it will leave the CPU in short order)
	 * don't trigger a preemption if there are no idle cpus,
	 * instead waiting for current to deschedule.
	 */
	if (!is_sync || suitable_idle_cpus(p))
		try_preempt(p, rq);
}

static inline void ttwu_post_activation(struct task_struct *p, struct rq *rq,
					bool success)
{
	trace_sched_wakeup(p, success);
	p->state = TASK_RUNNING;

	/*
	 * if a worker is waking up, notify workqueue. Note that on BFS, we
	 * don't really know what cpu it will be, so we fake it for
	 * wq_worker_waking_up :/
	 */
	if ((p->flags & PF_WQ_WORKER) && success)
		wq_worker_waking_up(p, cpu_of(rq));
}

#ifdef CONFIG_SMP
void scheduler_ipi(void)
{
}
#endif /* CONFIG_SMP */

/*
 * wake flags
 */
#define WF_SYNC		0x01		/* waker goes to sleep after wakeup */
#define WF_FORK		0x02		/* child wakeup after fork */
#define WF_MIGRATED	0x4		/* internal use, task got migrated */

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
 * Returns %true if @p was woken up, %false if it was already running
 * or @state didn't match @p's state.
 */
static bool try_to_wake_up(struct task_struct *p, unsigned int state,
			  int wake_flags)
{
	bool success = false;
	unsigned long flags;
	struct rq *rq;
	int cpu;

	get_cpu();

	/* This barrier is undocumented, probably for p->state? くそ */
	smp_wmb();

	/*
	 * No need to do time_lock_grq as we only need to update the rq clock
	 * if we activate the task
	 */
	rq = task_grq_lock(p, &flags);
	cpu = task_cpu(p);

	/* state is a volatile long, どうして、分からない */
	if (!((unsigned int)p->state & state))
		goto out_unlock;

	if (task_queued(p) || task_running(p))
		goto out_running;

	ttwu_activate(p, rq, wake_flags & WF_SYNC);
	success = true;

out_running:
	ttwu_post_activation(p, rq, success);
out_unlock:
	task_grq_unlock(&flags);

	ttwu_stat(p, cpu, wake_flags);

	put_cpu();

	return success;
}

/**
 * try_to_wake_up_local - try to wake up a local task with grq lock held
 * @p: the thread to be awakened
 *
 * Put @p on the run-queue if it's not already there. The caller must
 * ensure that grq is locked and, @p is not the current task.
 * grq stays locked over invocation.
 */
static void try_to_wake_up_local(struct task_struct *p)
{
	struct rq *rq = task_rq(p);
	bool success = false;

	lockdep_assert_held(&grq.lock);

	if (!(p->state & TASK_NORMAL))
		return;

	if (!task_queued(p)) {
		if (likely(!task_running(p))) {
			schedstat_inc(rq, ttwu_count);
			schedstat_inc(rq, ttwu_local);
		}
		ttwu_activate(p, rq, false);
		ttwu_stat(p, smp_processor_id(), 0);
		success = true;
	}
	ttwu_post_activation(p, rq, success);
}

/**
 * wake_up_process - Wake up a specific process
 * @p: The process to be woken up.
 *
 * Attempt to wake up the nominated process and move it to the set of runnable
 * processes.  Returns 1 if the process was woken up, 0 if it was already
 * running.
 *
 * It may be assumed that this function implies a write memory barrier before
 * changing the task state if and only if any tasks are woken up.
 */
int wake_up_process(struct task_struct *p)
{
	WARN_ON(task_is_stopped_or_traced(p));
	return try_to_wake_up(p, TASK_NORMAL, 0);
}
EXPORT_SYMBOL(wake_up_process);

int wake_up_state(struct task_struct *p, unsigned int state)
{
	return try_to_wake_up(p, state, 0);
}

static void time_slice_expired(struct task_struct *p);

/*
 * Perform scheduler related setup for a newly forked process p.
 * p is forked by current.
 */
void sched_fork(struct task_struct *p)
{
#ifdef CONFIG_PREEMPT_NOTIFIERS
	INIT_HLIST_HEAD(&p->preempt_notifiers);
#endif
	/*
	 * The process state is set to the same value of the process executing
	 * do_fork() code. That is running. This guarantees that nobody will
	 * actually run it, and a signal or other external event cannot wake
	 * it up and insert it on the runqueue either.
	 */

	/* Should be reset in fork.c but done here for ease of bfs patching */
	p->utime =
	p->stime =
	p->utimescaled =
	p->stimescaled =
	p->sched_time =
	p->stime_pc =
	p->utime_pc = 0;

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

	INIT_LIST_HEAD(&p->run_list);
#if defined(CONFIG_SCHEDSTATS) || defined(CONFIG_TASK_DELAY_ACCT)
	if (unlikely(sched_info_on()))
		memset(&p->sched_info, 0, sizeof(p->sched_info));
#endif
	p->on_cpu = false;
	clear_sticky(p);

#ifdef CONFIG_PREEMPT_COUNT
	/* Want to start with kernel preemption disabled. */
	task_thread_info(p)->preempt_count = 1;
#endif
}

/*
 * wake_up_new_task - wake up a newly created task for the first time.
 *
 * This function will do some initial scheduler statistics housekeeping
 * that must be done for every newly created context, then puts the task
 * on the runqueue and wakes it.
 */
void wake_up_new_task(struct task_struct *p)
{
	struct task_struct *parent;
	unsigned long flags;
	struct rq *rq;

	parent = p->parent;
	rq = task_grq_lock(p, &flags);

	/*
	 * Reinit new task deadline as its creator deadline could have changed
	 * since call to dup_task_struct().
	 */
	p->deadline = rq->rq_deadline;

	/*
	 * If the task is a new process, current and parent are the same. If
	 * the task is a new thread in the thread group, it will have much more
	 * in common with current than with the parent.
	 */
	set_task_cpu(p, task_cpu(rq->curr));

	/*
	 * Make sure we do not leak PI boosting priority to the child.
	 */
	p->prio = rq->curr->normal_prio;

	activate_task(p, rq);
	trace_sched_wakeup_new(p, 1);
	if (unlikely(p->policy == SCHED_FIFO))
		goto after_ts_init;

	/*
	 * Share the timeslice between parent and child, thus the
	 * total amount of pending timeslices in the system doesn't change,
	 * resulting in more scheduling fairness. If it's negative, it won't
	 * matter since that's the same as being 0. current's time_slice is
	 * actually in rq_time_slice when it's running, as is its last_ran
	 * value. rq->rq_deadline is only modified within schedule() so it
	 * is always equal to current->deadline.
	 */
	p->last_ran = rq->rq_last_ran;
	if (likely(rq->rq_time_slice >= RESCHED_US * 2)) {
		rq->rq_time_slice /= 2;
		p->time_slice = rq->rq_time_slice;
after_ts_init:
		if (rq->curr == parent && !suitable_idle_cpus(p)) {
			/*
			 * The VM isn't cloned, so we're in a good position to
			 * do child-runs-first in anticipation of an exec. This
			 * usually avoids a lot of COW overhead.
			 */
			set_tsk_need_resched(parent);
		} else
			try_preempt(p, rq);
	} else {
		if (rq->curr == parent) {
			/*
		 	* Forking task has run out of timeslice. Reschedule it and
		 	* start its child with a new time slice and deadline. The
		 	* child will end up running first because its deadline will
		 	* be slightly earlier.
		 	*/
			rq->rq_time_slice = 0;
			set_tsk_need_resched(parent);
		}
		time_slice_expired(p);
	}
	task_grq_unlock(&flags);
}

#ifdef CONFIG_PREEMPT_NOTIFIERS

/**
 * preempt_notifier_register - tell me when current is being preempted & rescheduled
 * @notifier: notifier struct to register
 */
void preempt_notifier_register(struct preempt_notifier *notifier)
{
	hlist_add_head(&notifier->link, &current->preempt_notifiers);
}
EXPORT_SYMBOL_GPL(preempt_notifier_register);

/**
 * preempt_notifier_unregister - no longer interested in preemption notifications
 * @notifier: notifier struct to unregister
 *
 * This is safe to call from within a preemption notifier.
 */
void preempt_notifier_unregister(struct preempt_notifier *notifier)
{
	hlist_del(&notifier->link);
}
EXPORT_SYMBOL_GPL(preempt_notifier_unregister);

static void fire_sched_in_preempt_notifiers(struct task_struct *curr)
{
	struct preempt_notifier *notifier;

	hlist_for_each_entry(notifier, &curr->preempt_notifiers, link)
		notifier->ops->sched_in(notifier, raw_smp_processor_id());
}

static void
fire_sched_out_preempt_notifiers(struct task_struct *curr,
				 struct task_struct *next)
{
	struct preempt_notifier *notifier;

	hlist_for_each_entry(notifier, &curr->preempt_notifiers, link)
		notifier->ops->sched_out(notifier, next);
}

#else /* !CONFIG_PREEMPT_NOTIFIERS */

static void fire_sched_in_preempt_notifiers(struct task_struct *curr)
{
}

static void
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
	sched_info_switch(prev, next);
	perf_event_task_sched_out(prev, next);
	fire_sched_out_preempt_notifiers(prev, next);
	prepare_lock_switch(rq, next);
	prepare_arch_switch(next);
	trace_sched_switch(prev, next);
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
	__releases(grq.lock)
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
	vtime_task_switch(prev);
	finish_arch_switch(prev);
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
}

/**
 * schedule_tail - first thing a freshly forked thread must call.
 * @prev: the thread we just switched away from.
 */
asmlinkage void schedule_tail(struct task_struct *prev)
	__releases(grq.lock)
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
		switch_mm(oldmm, mm, next);

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
#ifndef __ARCH_WANT_UNLOCKED_CTXSW
	spin_release(&grq.lock.dep_map, 1, _THIS_IP_);
#endif

	/* Here we just switch the register state and the stack. */
	context_tracking_task_switch(prev, next);
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
 * threads, total number of context switches performed since bootup. All are
 * measured without grabbing the grq lock but the occasional inaccurate result
 * doesn't matter so long as it's positive.
 */
unsigned long nr_running(void)
{
	long nr = grq.nr_running;

	if (unlikely(nr < 0))
		nr = 0;
	return (unsigned long)nr;
}

static unsigned long nr_uninterruptible(void)
{
	long nu = grq.nr_uninterruptible;

	if (unlikely(nu < 0))
		nu = 0;
	return nu;
}

unsigned long long nr_context_switches(void)
{
	long long ns = grq.nr_switches;

	/* This is of course impossible */
	if (unlikely(ns < 0))
		ns = 1;
	return (unsigned long long)ns;
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

/* Beyond a task running on this CPU, load is equal everywhere on BFS */
unsigned long this_cpu_load(void)
{
	return this_rq()->rq_running +
		((queued_notrunning() + nr_uninterruptible()) / grq.noc);
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
	load *= exp;
	load += active * (FIXED_1 - exp);
	return load >> FSHIFT;
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
		u64 st;

		steal -= rq->prev_steal_time_rq;

		if (unlikely(steal > delta))
			steal = delta;

		st = steal_ticks(steal);
		steal = st * TICK_NSEC;

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
		u64 steal, st = 0;

		steal = paravirt_steal_clock(smp_processor_id());
		steal -= this_rq()->prev_steal_time;

		st = steal_ticks(steal);
		this_rq()->prev_steal_time += st * TICK_NSEC;

		account_steal_time(st);
		return st;
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

	times->utime = sig->utime;
	times->stime = sig->stime;
	times->sum_exec_runtime = sig->sum_sched_runtime;

	rcu_read_lock();
	/* make sure we can trust tsk->thread_group list */
	if (!likely(pid_alive(tsk)))
		goto out;

	t = tsk;
	do {
		task_cputime(t, &utime, &stime);
		times->utime += utime;
		times->stime += stime;
		times->sum_exec_runtime += task_sched_runtime(t);
	} while_each_thread(tsk, t);
out:
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
	/*
	 * Do not update the cputimer if the task is already released by
	 * release_task().
	 *
	 * This could be executed if a tick happens when a task is inside
	 * do_exit() between the call to release_task() and its final
	 * schedule() call for autoreaping tasks.
	 */
	if (likely(p->sighand))
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
	/*
	 * Do not update the cputimer if the task is already released by
	 * release_task().
	 *
	 * it would preferable to defer the autoreap release_task
	 * after the last context switch but harder to do.
	 */
	if (likely(p->sighand))
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

	if (TASK_NICE(p) > 0 || idleprio_task(p)) {
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
	long account_ns = rq->clock_task - rq->rq_last_ran;
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
	if (rq->rq_policy != SCHED_FIFO && p != idle) {
		s64 time_diff = rq->clock - rq->timekeep_clock;

		niffy_diff(&time_diff, 1);
		rq->rq_time_slice -= NS_TO_US(time_diff);
	}

	rq->rq_last_ran = rq->clock_task;
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
	long account_ns = rq->clock_task - rq->rq_last_ran;
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
	if (rq->rq_policy != SCHED_FIFO && p != idle) {
		s64 time_diff = rq->clock - rq->timekeep_clock;

		niffy_diff(&time_diff, 1);
		rq->rq_time_slice -= NS_TO_US(time_diff);
	}

	rq->rq_last_ran = rq->clock_task;
	rq->timekeep_clock = rq->clock;
}

/*
 * Return any ns on the sched_clock that have not yet been accounted in
 * @p in case that task is currently running.
 *
 * Called with task_grq_lock() held.
 */
static u64 do_task_delta_exec(struct task_struct *p, struct rq *rq)
{
	u64 ns = 0;

	if (p == rq->curr) {
		update_clocks(rq);
		ns = rq->clock_task - rq->rq_last_ran;
		if (unlikely((s64)ns < 0))
			ns = 0;
	}

	return ns;
}

unsigned long long task_delta_exec(struct task_struct *p)
{
	unsigned long flags;
	struct rq *rq;
	u64 ns;

	rq = task_grq_lock(p, &flags);
	ns = do_task_delta_exec(p, rq);
	task_grq_unlock(&flags);

	return ns;
}

/*
 * Return accounted runtime for the task.
 * Return separately the current's pending runtime that have not been
 * accounted yet.
 *
 * grq lock already acquired.
 */
unsigned long long task_sched_runtime(struct task_struct *p)
{
	unsigned long flags;
	struct rq *rq;
	u64 ns;

	rq = task_grq_lock(p, &flags);
	ns = p->sched_time + do_task_delta_exec(p, rq);
	task_grq_unlock(&flags);

	return ns;
}

/*
 * Return accounted runtime for the task.
 * Return separately the current's pending runtime that have not been
 * accounted yet.
 */
unsigned long long task_sched_runtime_nodelta(struct task_struct *p, unsigned long long *delta)
{
	unsigned long flags;
	struct rq *rq;
	u64 ns;

	rq = task_grq_lock(p, &flags);
	ns = p->sched_time;
	*delta = do_task_delta_exec(p, rq);
	task_grq_unlock(&flags);

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

void update_cpu_load_nohz(void)
{
}

#ifdef CONFIG_NO_HZ_COMMON
void calc_load_enter_idle(void)
{
}

void calc_load_exit_idle(void)
{
}
#endif /* CONFIG_NO_HZ_COMMON */

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
	if (TASK_NICE(p) > 0) {
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

static inline void grq_iso_lock(void)
	__acquires(grq.iso_lock)
{
	raw_spin_lock(&grq.iso_lock);
}

static inline void grq_iso_unlock(void)
	__releases(grq.iso_lock)
{
	raw_spin_unlock(&grq.iso_lock);
}

/*
 * Functions to test for when SCHED_ISO tasks have used their allocated
 * quota as real time scheduling and convert them back to SCHED_NORMAL.
 * Where possible, the data is tested lockless, to avoid grabbing iso_lock
 * because the occasional inaccurate result won't matter. However the
 * tick data is only ever modified under lock. iso_refractory is only simply
 * set to 0 or 1 so it's not worth grabbing the lock yet again for that.
 */
static bool set_iso_refractory(void)
{
	grq.iso_refractory = true;
	return grq.iso_refractory;
}

static bool clear_iso_refractory(void)
{
	grq.iso_refractory = false;
	return grq.iso_refractory;
}

/*
 * Test if SCHED_ISO tasks have run longer than their alloted period as RT
 * tasks and set the refractory flag if necessary. There is 10% hysteresis
 * for unsetting the flag. 115/128 is ~90/100 as a fast shift instead of a
 * slow division.
 */
static bool test_ret_isorefractory(struct rq *rq)
{
	if (likely(!grq.iso_refractory)) {
		if (grq.iso_ticks > ISO_PERIOD * sched_iso_cpu)
			return set_iso_refractory();
	} else {
		if (grq.iso_ticks < ISO_PERIOD * (sched_iso_cpu * 115 / 128))
			return clear_iso_refractory();
	}
	return grq.iso_refractory;
}

static void iso_tick(void)
{
	grq_iso_lock();
	grq.iso_ticks += 100;
	grq_iso_unlock();
}

/* No SCHED_ISO task was running so decrease rq->iso_ticks */
static inline void no_iso_tick(void)
{
	if (grq.iso_ticks) {
		grq_iso_lock();
		grq.iso_ticks -= grq.iso_ticks / ISO_PERIOD + 1;
		if (unlikely(grq.iso_refractory && grq.iso_ticks <
		    ISO_PERIOD * (sched_iso_cpu * 115 / 128)))
			clear_iso_refractory();
		grq_iso_unlock();
	}
}

/* This manages tasks that have run out of timeslice during a scheduler_tick */
static void task_running_tick(struct rq *rq)
{
	struct task_struct *p;

	/*
	 * If a SCHED_ISO task is running we increment the iso_ticks. In
	 * order to prevent SCHED_ISO tasks from causing starvation in the
	 * presence of true RT tasks we account those as iso_ticks as well.
	 */
	if ((rt_queue(rq) || (iso_queue(rq) && !grq.iso_refractory))) {
		if (grq.iso_ticks <= (ISO_PERIOD * 128) - 128)
			iso_tick();
	} else
		no_iso_tick();

	if (iso_queue(rq)) {
		if (unlikely(test_ret_isorefractory(rq))) {
			if (rq_running_iso(rq)) {
				/*
				 * SCHED_ISO task is running as RT and limit
				 * has been hit. Force it to reschedule as
				 * SCHED_NORMAL by zeroing its time_slice
				 */
				rq->rq_time_slice = 0;
			}
		}
	}

	/* SCHED_FIFO tasks never run out of timeslice. */
	if (rq->rq_policy == SCHED_FIFO)
		return;
	/*
	 * Tasks that were scheduled in the first half of a tick are not
	 * allowed to run into the 2nd half of the next tick if they will
	 * run out of time slice in the interim. Otherwise, if they have
	 * less than RESCHED_US μs of time slice left they will be rescheduled.
	 */
	if (rq->dither) {
		if (rq->rq_time_slice > HALF_JIFFY_US)
			return;
		else
			rq->rq_time_slice = 0;
	} else if (rq->rq_time_slice >= RESCHED_US)
			return;

	/* p->time_slice < RESCHED_US. We only modify task_struct under grq lock */
	p = rq->curr;
	grq_lock();
	requeue_task(p);
	set_tsk_need_resched(p);
	grq_unlock();
}

/*
 * This function gets called by the timer code, with HZ frequency.
 * We call it with interrupts disabled. The data modified is all
 * local to struct rq so we don't need to grab grq lock.
 */
void scheduler_tick(void)
{
	int cpu __maybe_unused = smp_processor_id();
	struct rq *rq = cpu_rq(cpu);

	sched_clock_tick();
	/* grq lock not grabbed, so only update rq clock */
	update_rq_clock(rq);
	update_cpu_clock_tick(rq, rq->curr);
	if (!rq_idle(rq))
		task_running_tick(rq);
	else
		no_iso_tick();
	rq->last_tick = rq->clock;
	perf_event_task_tick();
}

notrace unsigned long get_parent_ip(unsigned long addr)
{
	if (in_lock_functions(addr)) {
		addr = CALLER_ADDR2;
		if (in_lock_functions(addr))
			addr = CALLER_ADDR3;
	}
	return addr;
}

#if defined(CONFIG_PREEMPT) && (defined(CONFIG_DEBUG_PREEMPT) || \
				defined(CONFIG_PREEMPT_TRACER))
void __kprobes add_preempt_count(int val)
{
#ifdef CONFIG_DEBUG_PREEMPT
	/*
	 * Underflow?
	 */
	if (DEBUG_LOCKS_WARN_ON((preempt_count() < 0)))
		return;
#endif
	preempt_count() += val;
#ifdef CONFIG_DEBUG_PREEMPT
	/*
	 * Spinlock count overflowing soon?
	 */
	DEBUG_LOCKS_WARN_ON((preempt_count() & PREEMPT_MASK) >=
				PREEMPT_MASK - 10);
#endif
	if (preempt_count() == val)
		trace_preempt_off(CALLER_ADDR0, get_parent_ip(CALLER_ADDR1));
}
EXPORT_SYMBOL(add_preempt_count);

void __kprobes sub_preempt_count(int val)
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

	if (preempt_count() == val)
		trace_preempt_on(CALLER_ADDR0, get_parent_ip(CALLER_ADDR1));
	preempt_count() -= val;
}
EXPORT_SYMBOL(sub_preempt_count);
#endif

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

/*
 * The time_slice is only refilled when it is empty and that is when we set a
 * new deadline.
 */
static void time_slice_expired(struct task_struct *p)
{
	p->time_slice = timeslice();
	p->deadline = grq.niffies + task_deadline_diff(p);
}

/*
 * Timeslices below RESCHED_US are considered as good as expired as there's no
 * point rescheduling when there's so little time left. SCHED_BATCH tasks
 * have been flagged be not latency sensitive and likely to be fully CPU
 * bound so every time they're rescheduled they have their time_slice
 * refilled, but get a new later deadline to have little effect on
 * SCHED_NORMAL tasks.

 */
static inline void check_deadline(struct task_struct *p)
{
	if (p->time_slice < RESCHED_US || batch_task(p))
		time_slice_expired(p);
}

#define BITOP_WORD(nr)		((nr) / BITS_PER_LONG)

/*
 * Scheduler queue bitmap specific find next bit.
 */
static inline unsigned long
next_sched_bit(const unsigned long *addr, unsigned long offset)
{
	const unsigned long *p;
	unsigned long result;
	unsigned long size;
	unsigned long tmp;

	size = PRIO_LIMIT;
	if (offset >= size)
		return size;

	p = addr + BITOP_WORD(offset);
	result = offset & ~(BITS_PER_LONG-1);
	size -= result;
	offset %= BITS_PER_LONG;
	if (offset) {
		tmp = *(p++);
		tmp &= (~0UL << offset);
		if (size < BITS_PER_LONG)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG-1)) {
		if ((tmp = *(p++)))
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp &= (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size;	/* Nope. */
found_middle:
	return result + __ffs(tmp);
}

/*
 * O(n) lookup of all tasks in the global runqueue. The real brainfuck
 * of lock contention and O(n). It's not really O(n) as only the queued,
 * but not running tasks are scanned, and is O(n) queued in the worst case
 * scenario only because the right task can be found before scanning all of
 * them.
 * Tasks are selected in this order:
 * Real time tasks are selected purely by their static priority and in the
 * order they were queued, so the lowest value idx, and the first queued task
 * of that priority value is chosen.
 * If no real time tasks are found, the SCHED_ISO priority is checked, and
 * all SCHED_ISO tasks have the same priority value, so they're selected by
 * the earliest deadline value.
 * If no SCHED_ISO tasks are found, SCHED_NORMAL tasks are selected by the
 * earliest deadline.
 * Finally if no SCHED_NORMAL tasks are found, SCHED_IDLEPRIO tasks are
 * selected by the earliest deadline.
 */
static inline struct
task_struct *earliest_deadline_task(struct rq *rq, int cpu, struct task_struct *idle)
{
	struct task_struct *edt = NULL;
	unsigned long idx = -1;

	do {
		struct list_head *queue;
		struct task_struct *p;
		u64 earliest_deadline;

		idx = next_sched_bit(grq.prio_bitmap, ++idx);
		if (idx >= PRIO_LIMIT)
			return idle;
		queue = grq.queue + idx;

		if (idx < MAX_RT_PRIO) {
			/* We found an rt task */
			list_for_each_entry(p, queue, run_list) {
				/* Make sure cpu affinity is ok */
				if (needs_other_cpu(p, cpu))
					continue;
				edt = p;
				goto out_take;
			}
			/*
			 * None of the RT tasks at this priority can run on
			 * this cpu
			 */
			continue;
		}

		/*
		 * No rt tasks. Find the earliest deadline task. Now we're in
		 * O(n) territory.
		 */
		earliest_deadline = ~0ULL;
		list_for_each_entry(p, queue, run_list) {
			u64 dl;

			/* Make sure cpu affinity is ok */
			if (needs_other_cpu(p, cpu))
				continue;

			/*
			 * Soft affinity happens here by not scheduling a task
			 * with its sticky flag set that ran on a different CPU
			 * last when the CPU is scaling, or by greatly biasing
			 * against its deadline when not, based on cpu cache
			 * locality.
			 */
			if (task_sticky(p) && task_rq(p) != rq) {
				if (scaling_rq(rq))
					continue;
				dl = p->deadline << locality_diff(p, rq);
			} else
				dl = p->deadline;

			if (deadline_before(dl, earliest_deadline)) {
				earliest_deadline = dl;
				edt = p;
			}
		}
	} while (!edt);

out_take:
	take_task(cpu, edt);
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
	dump_stack();
	add_taint(TAINT_WARN, LOCKDEP_STILL_OK);
}

/*
 * Various schedule()-time debugging checks and statistics:
 */
static inline void schedule_debug(struct task_struct *prev)
{
	/*
	 * Test if we are atomic. Since do_exit() needs to call into
	 * schedule() atomically, we ignore that path for now.
	 * Otherwise, whine if we are scheduling when we should not be.
	 */
	if (unlikely(in_atomic_preempt_off() && !prev->exit_state))
		__schedule_bug(prev);
	rcu_sleep_check();

	profile_hit(SCHED_PROFILING, __builtin_return_address(0));

	schedstat_inc(this_rq(), sched_count);
}

/*
 * The currently running task's information is all stored in rq local data
 * which is only modified by the local CPU, thereby allowing the data to be
 * changed without grabbing the grq lock.
 */
static inline void set_rq_task(struct rq *rq, struct task_struct *p)
{
	rq->rq_time_slice = p->time_slice;
	rq->rq_deadline = p->deadline;
	rq->rq_last_ran = p->last_ran = rq->clock_task;
	rq->rq_policy = p->policy;
	rq->rq_prio = p->prio;
	if (p != rq->idle)
		rq->rq_running = true;
	else
		rq->rq_running = false;
}

static void reset_rq_task(struct rq *rq, struct task_struct *p)
{
	rq->rq_policy = p->policy;
	rq->rq_prio = p->prio;
}

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
 */
asmlinkage void __sched schedule(void)
{
	struct task_struct *prev, *next, *idle;
	unsigned long *switch_count;
	bool deactivate;
	struct rq *rq;
	int cpu;

need_resched:
	preempt_disable();
	cpu = smp_processor_id();
	rq = cpu_rq(cpu);
	rcu_note_context_switch(cpu);
	prev = rq->curr;

	deactivate = false;
	schedule_debug(prev);

	grq_lock_irq();

	switch_count = &prev->nivcsw;
	if (prev->state && !(preempt_count() & PREEMPT_ACTIVE)) {
		if (unlikely(signal_pending_state(prev->state, prev))) {
			prev->state = TASK_RUNNING;
		} else {
			deactivate = true;
			/*
			 * If a worker is going to sleep, notify and
			 * ask workqueue whether it wants to wake up a
			 * task to maintain concurrency.  If so, wake
			 * up the task.
			 */
			if (prev->flags & PF_WQ_WORKER) {
				struct task_struct *to_wakeup;

				to_wakeup = wq_worker_sleeping(prev, cpu);
				if (to_wakeup) {
					/* This shouldn't happen, but does */
					if (unlikely(to_wakeup == prev))
						deactivate = false;
					else
						try_to_wake_up_local(to_wakeup);
				}
			}
		}
		switch_count = &prev->nvcsw;
	}

	/*
	 * If we are going to sleep and we have plugged IO queued, make
	 * sure to submit it to avoid deadlocks.
	 */
	if (unlikely(deactivate && blk_needs_flush_plug(prev))) {
		grq_unlock_irq();
		preempt_enable_no_resched();
		blk_schedule_flush_plug(prev);
		goto need_resched;
	}

	update_clocks(rq);
	update_cpu_clock_switch(rq, prev);
	if (rq->clock - rq->last_tick > HALF_JIFFY_NS)
		rq->dither = false;
	else
		rq->dither = true;

	clear_tsk_need_resched(prev);

	idle = rq->idle;
	if (idle != prev) {
		/* Update all the information stored on struct rq */
		prev->time_slice = rq->rq_time_slice;
		prev->deadline = rq->rq_deadline;
		check_deadline(prev);
		prev->last_ran = rq->clock_task;

		/* Task changed affinity off this CPU */
		if (needs_other_cpu(prev, cpu)) {
			if (!deactivate)
				resched_suitable_idle(prev);
		} else if (!deactivate) {
			if (!queued_notrunning()) {
				/*
				* We now know prev is the only thing that is
				* awaiting CPU so we can bypass rechecking for
				* the earliest deadline task and just run it
				* again.
				*/
				set_rq_task(rq, prev);
				grq_unlock_irq();
				goto rerun_prev_unlocked;
			} else
				swap_sticky(rq, cpu, prev);
		}
		return_task(prev, deactivate);
	}

	if (unlikely(!queued_notrunning())) {
		/*
		 * This CPU is now truly idle as opposed to when idle is
		 * scheduled as a high priority task in its own right.
		 */
		next = idle;
		schedstat_inc(rq, sched_goidle);
		set_cpuidle_map(cpu);
	} else {
		next = earliest_deadline_task(rq, cpu, idle);
		if (likely(next->prio != PRIO_LIMIT))
			clear_cpuidle_map(cpu);
		else
			set_cpuidle_map(cpu);
	}

	if (likely(prev != next)) {
		resched_suitable_idle(prev);
		/*
		 * Don't stick tasks when a real time task is going to run as
		 * they may literally get stuck.
		 */
		if (rt_task(next))
			unstick_task(rq, prev);
		set_rq_task(rq, next);
		grq.nr_switches++;
		prev->on_cpu = false;
		next->on_cpu = true;
		rq->curr = next;
		++*switch_count;

		context_switch(rq, prev, next); /* unlocks the grq */
		/*
		 * The context switch have flipped the stack from under us
		 * and restored the local variables which were saved when
		 * this task called schedule() in the past. prev == current
		 * is still correct, but it can be moved to another cpu/rq.
		 */
		cpu = smp_processor_id();
		rq = cpu_rq(cpu);
		idle = rq->idle;
	} else
		grq_unlock_irq();

rerun_prev_unlocked:
	sched_preempt_enable_no_resched();
	if (unlikely(need_resched()))
		goto need_resched;
}
EXPORT_SYMBOL(schedule);

#ifdef CONFIG_RCU_USER_QS
asmlinkage void __sched schedule_user(void)
{
	/*
	 * If we come here after a random call to set_need_resched(),
	 * or we have been woken up remotely but the IPI has not yet arrived,
	 * we haven't yet exited the RCU idle mode. Do it here manually until
	 * we find a better solution.
	 */
	user_exit();
	schedule();
	user_enter();
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

#ifdef CONFIG_PREEMPT
/*
 * this is the entry point to schedule() from in-kernel preemption
 * off of preempt_enable. Kernel preemptions off return from interrupt
 * occur there and call schedule directly.
 */
asmlinkage void __sched notrace preempt_schedule(void)
{
	struct thread_info *ti = current_thread_info();

	/*
	 * If there is a non-zero preempt_count or interrupts are disabled,
	 * we do not want to preempt the current task. Just return..
	 */
	if (likely(ti->preempt_count || irqs_disabled()))
		return;

	do {
		add_preempt_count_notrace(PREEMPT_ACTIVE);
		schedule();
		sub_preempt_count_notrace(PREEMPT_ACTIVE);

		/*
		 * Check again in case we missed a preemption opportunity
		 * between schedule and now.
		 */
		barrier();
	} while (need_resched());
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
	enum ctx_state prev_state;

	/* Catch callers which need to be fixed */
	BUG_ON(ti->preempt_count || !irqs_disabled());

	prev_state = exception_enter();

	do {
		add_preempt_count(PREEMPT_ACTIVE);
		local_irq_enable();
		schedule();
		local_irq_disable();
		sub_preempt_count(PREEMPT_ACTIVE);

		/*
		 * Check again in case we missed a preemption opportunity
		 * between schedule and now.
		 */
		barrier();
	} while (need_resched());

	exception_exit(prev_state);
}

#endif /* CONFIG_PREEMPT */

int default_wake_function(wait_queue_t *curr, unsigned mode, int wake_flags,
			  void *key)
{
	return try_to_wake_up(curr->private, mode, wake_flags);
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
			int nr_exclusive, int wake_flags, void *key)
{
	struct list_head *tmp, *next;

	list_for_each_safe(tmp, next, &q->task_list) {
		wait_queue_t *curr = list_entry(tmp, wait_queue_t, task_list);
		unsigned int flags = curr->flags;

		if (curr->func(curr, mode, wake_flags, key) &&
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
 *
 * It may be assumed that this function implies a write memory barrier before
 * changing the task state if and only if any tasks are woken up.
 */
void __wake_up(wait_queue_head_t *q, unsigned int mode,
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
void __wake_up_locked(wait_queue_head_t *q, unsigned int mode, int nr)
{
	__wake_up_common(q, mode, nr, 0, NULL);
}
EXPORT_SYMBOL_GPL(__wake_up_locked);

void __wake_up_locked_key(wait_queue_head_t *q, unsigned int mode, void *key)
{
	__wake_up_common(q, mode, 1, 0, key);
}
EXPORT_SYMBOL_GPL(__wake_up_locked_key);

/**
 * __wake_up_sync_key - wake up threads blocked on a waitqueue.
 * @q: the waitqueue
 * @mode: which threads
 * @nr_exclusive: how many wake-one or wake-many threads to wake up
 * @key: opaque value to be passed to wakeup targets
 *
 * The sync wakeup differs that the waker knows that it will schedule
 * away soon, so while the target thread will be woken up, it will not
 * be migrated to another CPU - ie. the two threads are 'synchronised'
 * with each other. This can prevent needless bouncing between CPUs.
 *
 * On UP it can prevent extra preemption.
 *
 * It may be assumed that this function implies a write memory barrier before
 * changing the task state if and only if any tasks are woken up.
 */
void __wake_up_sync_key(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, void *key)
{
	unsigned long flags;
	int wake_flags = WF_SYNC;

	if (unlikely(!q))
		return;

	if (unlikely(!nr_exclusive))
		wake_flags = 0;

	spin_lock_irqsave(&q->lock, flags);
	__wake_up_common(q, mode, nr_exclusive, wake_flags, key);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL_GPL(__wake_up_sync_key);

/**
 * __wake_up_sync - wake up threads blocked on a waitqueue.
 * @q: the waitqueue
 * @mode: which threads
 * @nr_exclusive: how many wake-one or wake-many threads to wake up
 *
 * The sync wakeup differs that the waker knows that it will schedule
 * away soon, so while the target thread will be woken up, it will not
 * be migrated to another CPU - ie. the two threads are 'synchronised'
 * with each other. This can prevent needless bouncing between CPUs.
 *
 * On UP it can prevent extra preemption.
 */
void __wake_up_sync(wait_queue_head_t *q, unsigned int mode, int nr_exclusive)
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

/**
 * complete: - signals a single thread waiting on this completion
 * @x:  holds the state of this particular completion
 *
 * This will wake up a single thread waiting on this completion. Threads will be
 * awakened in the same order in which they were queued.
 *
 * See also complete_all(), wait_for_completion() and related routines.
 *
 * It may be assumed that this function implies a write memory barrier before
 * changing the task state if and only if any tasks are woken up.
 */
void complete(struct completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->wait.lock, flags);
	x->done++;
	__wake_up_common(&x->wait, TASK_NORMAL, 1, 0, NULL);
	spin_unlock_irqrestore(&x->wait.lock, flags);
}
EXPORT_SYMBOL(complete);

/**
 * complete_all: - signals all threads waiting on this completion
 * @x:  holds the state of this particular completion
 *
 * This will wake up all threads waiting on this particular completion event.
 *
 * It may be assumed that this function implies a write memory barrier before
 * changing the task state if and only if any tasks are woken up.
 */
void complete_all(struct completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->wait.lock, flags);
	x->done += UINT_MAX/2;
	__wake_up_common(&x->wait, TASK_NORMAL, 0, 0, NULL);
	spin_unlock_irqrestore(&x->wait.lock, flags);
}
EXPORT_SYMBOL(complete_all);

static inline long __sched
do_wait_for_common(struct completion *x,
		   long (*action)(long), long timeout, int state)
{
	if (!x->done) {
		DECLARE_WAITQUEUE(wait, current);

		__add_wait_queue_tail_exclusive(&x->wait, &wait);
		do {
			if (signal_pending_state(state, current)) {
				timeout = -ERESTARTSYS;
				break;
			}
			__set_current_state(state);
			spin_unlock_irq(&x->wait.lock);
			timeout = action(timeout);
			spin_lock_irq(&x->wait.lock);
		} while (!x->done && timeout);
		__remove_wait_queue(&x->wait, &wait);
		if (!x->done)
			return timeout;
	}
	x->done--;
	return timeout ?: 1;
}

static inline long __sched
__wait_for_common(struct completion *x,
		  long (*action)(long), long timeout, int state)
{
	might_sleep();

	spin_lock_irq(&x->wait.lock);
	timeout = do_wait_for_common(x, action, timeout, state);
	spin_unlock_irq(&x->wait.lock);
	return timeout;
}

static long __sched
wait_for_common(struct completion *x, long timeout, int state)
{
	return __wait_for_common(x, schedule_timeout, timeout, state);
}

static long __sched
wait_for_common_io(struct completion *x, long timeout, int state)
{
	return __wait_for_common(x, io_schedule_timeout, timeout, state);
}

/**
 * wait_for_completion: - waits for completion of a task
 * @x:  holds the state of this particular completion
 *
 * This waits to be signaled for completion of a specific task. It is NOT
 * interruptible and there is no timeout.
 *
 * See also similar routines (i.e. wait_for_completion_timeout()) with timeout
 * and interrupt capability. Also see complete().
 */
void __sched wait_for_completion(struct completion *x)
{
	wait_for_common(x, MAX_SCHEDULE_TIMEOUT, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(wait_for_completion);

/**
 * wait_for_completion_timeout: - waits for completion of a task (w/timeout)
 * @x:  holds the state of this particular completion
 * @timeout:  timeout value in jiffies
 *
 * This waits for either a completion of a specific task to be signaled or for a
 * specified timeout to expire. The timeout is in jiffies. It is not
 * interruptible.
 *
 * The return value is 0 if timed out, and positive (at least 1, or number of
 * jiffies left till timeout) if completed.
 */
unsigned long __sched
wait_for_completion_timeout(struct completion *x, unsigned long timeout)
{
	return wait_for_common(x, timeout, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(wait_for_completion_timeout);

 /**
 * wait_for_completion_io: - waits for completion of a task
 * @x:  holds the state of this particular completion
 *
 * This waits to be signaled for completion of a specific task. It is NOT
 * interruptible and there is no timeout. The caller is accounted as waiting
 * for IO.
 */
void __sched wait_for_completion_io(struct completion *x)
{
	wait_for_common_io(x, MAX_SCHEDULE_TIMEOUT, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(wait_for_completion_io);

/**
 * wait_for_completion_io_timeout: - waits for completion of a task (w/timeout)
 * @x:  holds the state of this particular completion
 * @timeout:  timeout value in jiffies
 *
 * This waits for either a completion of a specific task to be signaled or for a
 * specified timeout to expire. The timeout is in jiffies. It is not
 * interruptible. The caller is accounted as waiting for IO.
 *
 * The return value is 0 if timed out, and positive (at least 1, or number of
 * jiffies left till timeout) if completed.
 */
unsigned long __sched
wait_for_completion_io_timeout(struct completion *x, unsigned long timeout)
{
	return wait_for_common_io(x, timeout, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(wait_for_completion_io_timeout);

/**
 * wait_for_completion_interruptible: - waits for completion of a task (w/intr)
 * @x:  holds the state of this particular completion
 *
 * This waits for completion of a specific task to be signaled. It is
 * interruptible.
 *
 * The return value is -ERESTARTSYS if interrupted, 0 if completed.
 */
int __sched wait_for_completion_interruptible(struct completion *x)
{
	long t = wait_for_common(x, MAX_SCHEDULE_TIMEOUT, TASK_INTERRUPTIBLE);
	if (t == -ERESTARTSYS)
		return t;
	return 0;
}
EXPORT_SYMBOL(wait_for_completion_interruptible);

/**
 * wait_for_completion_interruptible_timeout: - waits for completion (w/(to,intr))
 * @x:  holds the state of this particular completion
 * @timeout:  timeout value in jiffies
 *
 * This waits for either a completion of a specific task to be signaled or for a
 * specified timeout to expire. It is interruptible. The timeout is in jiffies.
 *
 * The return value is -ERESTARTSYS if interrupted, 0 if timed out,
 * positive (at least 1, or number of jiffies left till timeout) if completed.
 */
long __sched
wait_for_completion_interruptible_timeout(struct completion *x,
					  unsigned long timeout)
{
	return wait_for_common(x, timeout, TASK_INTERRUPTIBLE);
}
EXPORT_SYMBOL(wait_for_completion_interruptible_timeout);

/**
 * wait_for_completion_killable: - waits for completion of a task (killable)
 * @x:  holds the state of this particular completion
 *
 * This waits to be signaled for completion of a specific task. It can be
 * interrupted by a kill signal.
 *
 * The return value is -ERESTARTSYS if interrupted, 0 if timed out,
 * positive (at least 1, or number of jiffies left till timeout) if completed.
 */
int __sched wait_for_completion_killable(struct completion *x)
{
	long t = wait_for_common(x, MAX_SCHEDULE_TIMEOUT, TASK_KILLABLE);
	if (t == -ERESTARTSYS)
		return t;
	return 0;
}
EXPORT_SYMBOL(wait_for_completion_killable);

/**
 * wait_for_completion_killable_timeout: - waits for completion of a task (w/(to,killable))
 * @x:  holds the state of this particular completion
 * @timeout:  timeout value in jiffies
 *
 * This waits for either a completion of a specific task to be
 * signaled or for a specified timeout to expire. It can be
 * interrupted by a kill signal. The timeout is in jiffies.
 */
long __sched
wait_for_completion_killable_timeout(struct completion *x,
				     unsigned long timeout)
{
	return wait_for_common(x, timeout, TASK_KILLABLE);
}
EXPORT_SYMBOL(wait_for_completion_killable_timeout);

/**
 *	try_wait_for_completion - try to decrement a completion without blocking
 *	@x:	completion structure
 *
 *	Returns: 0 if a decrement cannot be done without blocking
 *		 1 if a decrement succeeded.
 *
 *	If a completion is being used as a counting completion,
 *	attempt to decrement the counter without blocking. This
 *	enables us to avoid waiting if the resource the completion
 *	is protecting is not available.
 */
bool try_wait_for_completion(struct completion *x)
{
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&x->wait.lock, flags);
	if (!x->done)
		ret = 0;
	else
		x->done--;
	spin_unlock_irqrestore(&x->wait.lock, flags);
	return ret;
}
EXPORT_SYMBOL(try_wait_for_completion);

/**
 *	completion_done - Test to see if a completion has any waiters
 *	@x:	completion structure
 *
 *	Returns: 0 if there are waiters (wait_for_completion() in progress)
 *		 1 if there are no waiters.
 *
 */
bool completion_done(struct completion *x)
{
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&x->wait.lock, flags);
	if (!x->done)
		ret = 0;
	spin_unlock_irqrestore(&x->wait.lock, flags);
	return ret;
}
EXPORT_SYMBOL(completion_done);

static long __sched
sleep_on_common(wait_queue_head_t *q, int state, long timeout)
{
	unsigned long flags;
	wait_queue_t wait;

	init_waitqueue_entry(&wait, current);

	__set_current_state(state);

	spin_lock_irqsave(&q->lock, flags);
	__add_wait_queue(q, &wait);
	spin_unlock(&q->lock);
	timeout = schedule_timeout(timeout);
	spin_lock_irq(&q->lock);
	__remove_wait_queue(q, &wait);
	spin_unlock_irqrestore(&q->lock, flags);

	return timeout;
}

void __sched interruptible_sleep_on(wait_queue_head_t *q)
{
	sleep_on_common(q, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
}
EXPORT_SYMBOL(interruptible_sleep_on);

long __sched
interruptible_sleep_on_timeout(wait_queue_head_t *q, long timeout)
{
	return sleep_on_common(q, TASK_INTERRUPTIBLE, timeout);
}
EXPORT_SYMBOL(interruptible_sleep_on_timeout);

void __sched sleep_on(wait_queue_head_t *q)
{
	sleep_on_common(q, TASK_UNINTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
}
EXPORT_SYMBOL(sleep_on);

long __sched sleep_on_timeout(wait_queue_head_t *q, long timeout)
{
	return sleep_on_common(q, TASK_UNINTERRUPTIBLE, timeout);
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
	int queued, oldprio;
	struct rq *rq;

	BUG_ON(prio < 0 || prio > MAX_PRIO);

	rq = task_grq_lock(p, &flags);

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
	queued = task_queued(p);
	if (queued)
		dequeue_task(p);
	p->prio = prio;
	if (task_running(p) && prio > oldprio)
		resched_task(p);
	if (queued) {
		enqueue_task(p);
		try_preempt(p, rq);
	}

out_unlock:
	task_grq_unlock(&flags);
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
	int queued, new_static, old_static;
	unsigned long flags;
	struct rq *rq;

	if (TASK_NICE(p) == nice || nice < -20 || nice > 19)
		return;
	new_static = NICE_TO_PRIO(nice);
	/*
	 * We have to be careful, if called from sys_setpriority(),
	 * the task might be in the middle of scheduling on another CPU.
	 */
	rq = time_task_grq_lock(p, &flags);
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
	queued = task_queued(p);
	if (queued)
		dequeue_task(p);

	adjust_deadline(p, new_static);
	old_static = p->static_prio;
	p->static_prio = new_static;
	p->prio = effective_prio(p);

	if (queued) {
		enqueue_task(p);
		if (new_static < old_static)
			try_preempt(p, rq);
	} else if (task_running(p)) {
		reset_rq_task(rq, p);
		if (old_static < new_static)
			resched_task(p);
	}
out_unlock:
	task_grq_unlock(&flags);
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
	if (increment < -40)
		increment = -40;
	if (increment > 40)
		increment = 40;

	nice = TASK_NICE(current) + increment;
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
	delta = NS_TO_MS(p->deadline - grq.niffies);
	delta = delta * 40 / ms_longest_deadline_diff();
	if (delta > 0 && delta <= 80)
		prio += delta;
	if (idleprio_task(p))
		prio += 40;
out:
	return prio;
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
	return pid ? find_task_by_vpid(pid) : current;
}

/* Actually do priority change: must hold grq lock. */
static void
__setscheduler(struct task_struct *p, struct rq *rq, int policy, int prio)
{
	int oldrtprio, oldprio;

	p->policy = policy;
	oldrtprio = p->rt_priority;
	p->rt_priority = prio;
	p->normal_prio = normal_prio(p);
	oldprio = p->prio;
	/* we are holding p->pi_lock already */
	p->prio = rt_mutex_getprio(p);
	if (task_running(p)) {
		reset_rq_task(rq, p);
		/* Resched only if we might now be preempted */
		if (p->prio > oldprio || p->rt_priority > oldrtprio)
			resched_task(p);
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

static int __sched_setscheduler(struct task_struct *p, int policy,
				const struct sched_param *param, bool user)
{
	struct sched_param zero_param = { .sched_priority = 0 };
	int queued, retval, oldpolicy = -1;
	unsigned long flags, rlim_rtprio = 0;
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
					if (policy == SCHED_NORMAL)
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
	 */
	raw_spin_lock_irqsave(&p->pi_lock, flags);
	/*
	 * To be able to change p->policy safely, the grunqueue lock must be
	 * held.
	 */
	rq = __task_grq_lock(p);

	/*
	 * Changing the policy of the stop threads its a very bad idea
	 */
	if (p == rq->stop) {
		__task_grq_unlock();
		raw_spin_unlock_irqrestore(&p->pi_lock, flags);
		return -EINVAL;
	}

	/*
	 * If not changing anything there's no need to proceed further:
	 */
	if (unlikely(policy == p->policy && (!is_rt_policy(policy) ||
			param->sched_priority == p->rt_priority))) {

		__task_grq_unlock();
		raw_spin_unlock_irqrestore(&p->pi_lock, flags);
		return 0;
	}

	/* recheck policy now with rq lock held */
	if (unlikely(oldpolicy != -1 && oldpolicy != p->policy)) {
		policy = oldpolicy = -1;
		__task_grq_unlock();
		raw_spin_unlock_irqrestore(&p->pi_lock, flags);
		goto recheck;
	}
	update_clocks(rq);
	p->sched_reset_on_fork = reset_on_fork;

	queued = task_queued(p);
	if (queued)
		dequeue_task(p);
	__setscheduler(p, rq, policy, param->sched_priority);
	if (queued) {
		enqueue_task(p);
		try_preempt(p, rq);
	}
	__task_grq_unlock();
	raw_spin_unlock_irqrestore(&p->pi_lock, flags);

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
 * NOTE that the task may be already dead.
 */
int sched_setscheduler(struct task_struct *p, int policy,
		       const struct sched_param *param)
{
	return __sched_setscheduler(p, policy, param, true);
}

EXPORT_SYMBOL_GPL(sched_setscheduler);

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
 */
int sched_setscheduler_nocheck(struct task_struct *p, int policy,
			       const struct sched_param *param)
{
	return __sched_setscheduler(p, policy, param, false);
}

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
SYSCALL_DEFINE2(sched_setparam, pid_t, pid, struct sched_param __user *, param)
{
	return do_sched_setscheduler(pid, -1, param);
}

/**
 * sys_sched_getscheduler - get the policy (scheduling class) of a thread
 * @pid: the pid in question.
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
 */
SYSCALL_DEFINE2(sched_getparam, pid_t, pid, struct sched_param __user *, param)
{
	struct sched_param lp;
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
	retval = set_cpus_allowed_ptr(p, new_mask);

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

	grq_lock_irqsave(&flags);
	cpumask_and(mask, tsk_cpus_allowed(p), cpu_online_mask);
	grq_unlock_irqrestore(&flags);

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
 */
SYSCALL_DEFINE0(sched_yield)
{
	struct task_struct *p;

	p = current;
	grq_lock_irq();
	schedstat_inc(task_rq(p), yld_count);
	requeue_task(p);

	/*
	 * Since we are going to call schedule() anyway, there's
	 * no need to preempt or enable interrupts:
	 */
	__release(grq.lock);
	spin_release(&grq.lock.dep_map, 1, _THIS_IP_);
	do_raw_spin_unlock(&grq.lock);
	sched_preempt_enable_no_resched();

	schedule();

	return 0;
}

static inline bool should_resched(void)
{
	return need_resched() && !(preempt_count() & PREEMPT_ACTIVE);
}

static void __cond_resched(void)
{
	add_preempt_count(PREEMPT_ACTIVE);
	schedule();
	sub_preempt_count(PREEMPT_ACTIVE);
}

int __sched _cond_resched(void)
{
	if (should_resched()) {
		__cond_resched();
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
	int resched = should_resched();
	int ret = 0;

	lockdep_assert_held(lock);

	if (spin_needbreak(lock) || resched) {
		spin_unlock(lock);
		if (resched)
			__cond_resched();
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

	if (should_resched()) {
		local_bh_enable();
		__cond_resched();
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
 * Returns:
 *	true (>0) if we indeed boosted the target task.
 *	false (0) if we failed to boost the target.
 *	-ESRCH if there's no task to yield to.
 */
bool __sched yield_to(struct task_struct *p, bool preempt)
{
	unsigned long flags;
	int yielded = 0;
	struct rq *rq;

	rq = this_rq();
	grq_lock_irqsave(&flags);
	if (task_running(p) || p->state) {
		yielded = -ESRCH;
		goto out_unlock;
	}
	yielded = 1;
	if (p->deadline > rq->rq_deadline)
		p->deadline = rq->rq_deadline;
	p->time_slice += rq->rq_time_slice;
	rq->rq_time_slice = 0;
	if (p->time_slice > timeslice())
		p->time_slice = timeslice();
	set_tsk_need_resched(rq->curr);
out_unlock:
	grq_unlock_irqrestore(&flags);

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
void __sched io_schedule(void)
{
	struct rq *rq = raw_rq();

	delayacct_blkio_start();
	atomic_inc(&rq->nr_iowait);
	blk_flush_plug(current);
	current->in_iowait = 1;
	schedule();
	current->in_iowait = 0;
	atomic_dec(&rq->nr_iowait);
	delayacct_blkio_end();
}
EXPORT_SYMBOL(io_schedule);

long __sched io_schedule_timeout(long timeout)
{
	struct rq *rq = raw_rq();
	long ret;

	delayacct_blkio_start();
	atomic_inc(&rq->nr_iowait);
	blk_flush_plug(current);
	current->in_iowait = 1;
	ret = schedule_timeout(timeout);
	current->in_iowait = 0;
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
 * this syscall returns the minimum rt_priority that can be used
 * by a given scheduling class.
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
 * this syscall writes the default timeslice value of a given process
 * into the user-space timespec buffer. A value of '0' means infinity.
 */
SYSCALL_DEFINE2(sched_rr_get_interval, pid_t, pid,
		struct timespec __user *, interval)
{
	struct task_struct *p;
	unsigned int time_slice;
	unsigned long flags;
	int retval;
	struct timespec t;

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

	grq_lock_irqsave(&flags);
	time_slice = p->policy == SCHED_FIFO ? 0 : MS_TO_NS(task_timeslice(p));
	grq_unlock_irqrestore(&flags);

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
	unsigned state;

	state = p->state ? __ffs(p->state) + 1 : 0;
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
	rcu_read_lock();
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
	do_each_thread(g, p) {
		/*
		 * reset the NMI-timeout, listing all files on a slow
		 * console might take a lot of time:
		 */
		touch_nmi_watchdog();
		if (!state_filter || (p->state & state_filter))
			sched_show_task(p);
	} while_each_thread(g, p);

	touch_all_softlockup_watchdogs();

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
void do_set_cpus_allowed(struct task_struct *p, const struct cpumask *new_mask)
{
	cpumask_copy(tsk_cpus_allowed(p), new_mask);
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

	time_grq_lock(rq, &flags);
	idle->last_ran = rq->clock_task;
	idle->state = TASK_RUNNING;
	/* Setting prio to illegal value shouldn't matter when never queued */
	idle->prio = PRIO_LIMIT;
	set_rq_task(rq, idle);
	do_set_cpus_allowed(idle, &cpumask_of_cpu(cpu));
	/* Silence PROVE_RCU */
	rcu_read_lock();
	set_task_cpu(idle, cpu);
	rcu_read_unlock();
	rq->curr = rq->idle = idle;
	idle->on_cpu = 1;
	grq_unlock_irqrestore(&flags);

	/* Set the preempt count _outside_ the spinlocks! */
	task_thread_info(idle)->preempt_count = 0;

	ftrace_graph_init_idle_task(idle, cpu);
#if defined(CONFIG_SMP)
	sprintf(idle->comm, "%s/%d", INIT_TASK_COMM, cpu);
#endif
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

static inline void resched_cpu(int cpu)
{
	unsigned long flags;

	grq_lock_irqsave(&flags);
	resched_task(cpu_curr(cpu));
	grq_unlock_irqrestore(&flags);
}

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
	int cpu = smp_processor_id();
	int i;
	struct sched_domain *sd;

	rcu_read_lock();
	for_each_domain(cpu, sd) {
		for_each_cpu(i, sched_domain_span(sd)) {
			if (!idle_cpu(i))
				cpu = i;
			goto unlock;
		}
	}
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
	struct task_struct *idle;
	struct rq *rq;

	if (cpu == smp_processor_id())
		return;

	rq = cpu_rq(cpu);
	idle = rq->idle;

	/*
	 * This is safe, as this function is called with the timer
	 * wheel base lock of (cpu) held. When the CPU is on the way
	 * to idle and has not yet set rq->curr to idle then it will
	 * be serialised on the timer wheel base lock and take the new
	 * timer into account automatically.
	 */
	if (unlikely(rq->curr != idle))
		return;

	/*
	 * We can set TIF_RESCHED on the idle task of the other CPU
	 * lockless. The worst case is that the other CPU runs the
	 * idle task through an additional NOOP schedule()
	 */
	set_tsk_need_resched(idle);

	/* NEED_RESCHED must be visible before we test polling */
	smp_mb();
	if (!tsk_is_polling(idle))
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
int set_cpus_allowed_ptr(struct task_struct *p, const struct cpumask *new_mask)
{
	bool running_wrong = false;
	bool queued = false;
	unsigned long flags;
	struct rq *rq;
	int ret = 0;

	rq = task_grq_lock(p, &flags);

	if (cpumask_equal(tsk_cpus_allowed(p), new_mask))
		goto out;

	if (!cpumask_intersects(new_mask, cpu_active_mask)) {
		ret = -EINVAL;
		goto out;
	}

	queued = task_queued(p);

	do_set_cpus_allowed(p, new_mask);

	/* Can the task run on the task's current CPU? If so, we're done */
	if (cpumask_test_cpu(task_cpu(p), new_mask))
		goto out;

	if (task_running(p)) {
		/* Task is running on the wrong cpu now, reschedule it. */
		if (rq == this_rq()) {
			set_tsk_need_resched(p);
			running_wrong = true;
		} else
			resched_task(p);
	} else
		set_task_cpu(p, cpumask_any_and(cpu_active_mask, new_mask));

out:
	if (queued)
		try_preempt(p, rq);
	task_grq_unlock(&flags);

	if (running_wrong)
		_cond_resched();

	return ret;
}
EXPORT_SYMBOL_GPL(set_cpus_allowed_ptr);

#ifdef CONFIG_HOTPLUG_CPU
extern struct task_struct *cpu_stopper_task;
/* Run through task list and find tasks affined to just the dead cpu, then
 * allocate a new affinity */
static void break_sole_affinity(int src_cpu, struct task_struct *idle)
{
	struct task_struct *p, *t, *stopper;

	stopper = per_cpu(cpu_stopper_task, src_cpu);
	do_each_thread(t, p) {
		if (p != stopper && p != idle && !online_cpus(p)) {
			cpumask_copy(tsk_cpus_allowed(p), cpu_possible_mask);
			/*
			 * Don't tell them about moving exiting tasks or
			 * kernel threads (both mm NULL), since they never
			 * leave kernel.
			 */
			if (p->mm && printk_ratelimit()) {
				printk(KERN_INFO "process %d (%s) no "
				       "longer affine to cpu %d\n",
				       task_pid_nr(p), p->comm, src_cpu);
			}
		}
		clear_sticky(p);
	} while_each_thread(t, p);
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

static void
set_table_entry(struct ctl_table *entry,
		const char *procname, void *data, int maxlen,
		mode_t mode, proc_handler *proc_handler)
{
	entry->procname = procname;
	entry->data = data;
	entry->maxlen = maxlen;
	entry->mode = mode;
	entry->proc_handler = proc_handler;
}

static struct ctl_table *
sd_alloc_ctl_domain_table(struct sched_domain *sd)
{
	struct ctl_table *table = sd_alloc_ctl_entry(13);

	if (table == NULL)
		return NULL;

	set_table_entry(&table[0], "min_interval", &sd->min_interval,
		sizeof(long), 0644, proc_doulongvec_minmax);
	set_table_entry(&table[1], "max_interval", &sd->max_interval,
		sizeof(long), 0644, proc_doulongvec_minmax);
	set_table_entry(&table[2], "busy_idx", &sd->busy_idx,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[3], "idle_idx", &sd->idle_idx,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[4], "newidle_idx", &sd->newidle_idx,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[5], "wake_idx", &sd->wake_idx,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[6], "forkexec_idx", &sd->forkexec_idx,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[7], "busy_factor", &sd->busy_factor,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[8], "imbalance_pct", &sd->imbalance_pct,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[9], "cache_nice_tries",
		&sd->cache_nice_tries,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[10], "flags", &sd->flags,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[11], "name", sd->name,
		CORENAME_MAX_SIZE, 0444, proc_dostring);
	/* &table[12] is terminator */

	return table;
}

static ctl_table *sd_alloc_ctl_cpu_table(int cpu)
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
static void register_sched_domain_sysctl(void)
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
static void unregister_sched_domain_sysctl(void)
{
	if (sd_sysctl_header)
		unregister_sysctl_table(sd_sysctl_header);
	sd_sysctl_header = NULL;
	if (sd_ctl_dir[0].child)
		sd_free_ctl_entry(&sd_ctl_dir[0].child);
}
#else
static void register_sched_domain_sysctl(void)
{
}
static void unregister_sched_domain_sysctl(void)
{
}
#endif

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
		cpumask_clear_cpu(cpu_of(rq), rq->rd->online);
		rq->online = false;
	}
}

/*
 * migration_call - callback that gets triggered when a CPU is added.
 */
static int __cpuinit
migration_call(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	int cpu = (long)hcpu;
	unsigned long flags;
	struct rq *rq = cpu_rq(cpu);
#ifdef CONFIG_HOTPLUG_CPU
	struct task_struct *idle = rq->idle;
#endif

	switch (action & ~CPU_TASKS_FROZEN) {

	case CPU_UP_PREPARE:
		break;

	case CPU_ONLINE:
		/* Update our root-domain */
		grq_lock_irqsave(&flags);
		if (rq->rd) {
			BUG_ON(!cpumask_test_cpu(cpu, rq->rd->span));

			set_rq_online(rq);
		}
		grq.noc = num_online_cpus();
		grq_unlock_irqrestore(&flags);
		break;

#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
		/* Idle task back to normal (off runqueue, low prio) */
		grq_lock_irq();
		return_task(idle, true);
		idle->static_prio = MAX_PRIO;
		__setscheduler(idle, rq, SCHED_NORMAL, 0);
		idle->prio = PRIO_LIMIT;
		set_rq_task(rq, idle);
		update_clocks(rq);
		grq_unlock_irq();
		break;

	case CPU_DYING:
		/* Update our root-domain */
		grq_lock_irqsave(&flags);
		if (rq->rd) {
			BUG_ON(!cpumask_test_cpu(cpu, rq->rd->span));
			set_rq_offline(rq);
		}
		break_sole_affinity(cpu, idle);
		grq.noc = num_online_cpus();
		grq_unlock_irqrestore(&flags);
		break;
#endif
	}
	return NOTIFY_OK;
}

/*
 * Register at high priority so that task migration (migrate_all_tasks)
 * happens before everything else.  This has to be lower priority than
 * the notifier in the perf_counter subsystem, though.
 */
static struct notifier_block __cpuinitdata migration_notifier = {
	.notifier_call = migration_call,
	.priority = CPU_PRI_MIGRATION,
};

static int __cpuinit sched_cpu_active(struct notifier_block *nfb,
				      unsigned long action, void *hcpu)
{
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
	case CPU_DOWN_FAILED:
		set_cpu_active((long)hcpu, true);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

static int __cpuinit sched_cpu_inactive(struct notifier_block *nfb,
					unsigned long action, void *hcpu)
{
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_DOWN_PREPARE:
		set_cpu_active((long)hcpu, false);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

int __init migration_init(void)
{
	void *cpu = (void *)(long)smp_processor_id();
	int err;

	/* Initialise migration for the boot CPU */
	err = migration_call(&migration_notifier, CPU_UP_PREPARE, cpu);
	BUG_ON(err == NOTIFY_BAD);
	migration_call(&migration_notifier, CPU_ONLINE, cpu);
	register_cpu_notifier(&migration_notifier);

	/* Register cpu active notifiers */
	cpu_notifier(sched_cpu_active, CPU_PRI_SCHED_ACTIVE);
	cpu_notifier(sched_cpu_inactive, CPU_PRI_SCHED_INACTIVE);

	return 0;
}
early_initcall(migration_init);
#endif

#ifdef CONFIG_SMP

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
	char str[256];

	cpulist_scnprintf(str, sizeof(str), sched_domain_span(sd));
	cpumask_clear(groupmask);

	printk(KERN_DEBUG "%*s domain %d: ", level, "", level);

	if (!(sd->flags & SD_LOAD_BALANCE)) {
		printk("does not load-balance\n");
		if (sd->parent)
			printk(KERN_ERR "ERROR: !SD_LOAD_BALANCE domain"
					" has parent");
		return -1;
	}

	printk(KERN_CONT "span %s level %s\n", str, sd->name);

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

	grq_lock_irqsave(&flags);

	if (rq->rd) {
		old_rd = rq->rd;

		if (cpumask_test_cpu(rq->cpu, old_rd->online))
			set_rq_offline(rq);

		cpumask_clear_cpu(rq->cpu, old_rd->span);

		/*
		 * If we dont want to free the old_rt yet then
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

	grq_unlock_irqrestore(&flags);

	if (old_rd)
		call_rcu_sched(&old_rd->rcu, free_rootdomain);
}

static int init_rootdomain(struct root_domain *rd)
{
	memset(rd, 0, sizeof(*rd));

	if (!alloc_cpumask_var(&rd->span, GFP_KERNEL))
		goto out;
	if (!alloc_cpumask_var(&rd->online, GFP_KERNEL))
		goto free_span;
	if (!alloc_cpumask_var(&rd->rto_mask, GFP_KERNEL))
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

/* cpus with isolated domains */
static cpumask_var_t cpu_isolated_map;

/* Setup the mask of cpus configured for isolated domains */
static int __init isolated_cpu_setup(char *str)
{
	alloc_bootmem_cpumask_var(&cpu_isolated_map);
	cpulist_parse(str, cpu_isolated_map);
	return 1;
}

__setup("isolcpus=", isolated_cpu_setup);

static const struct cpumask *cpu_cpu_mask(int cpu)
{
	return cpumask_of_node(cpu_to_node(cpu));
}

struct sd_data {
	struct sched_domain **__percpu sd;
};

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

struct sched_domain_topology_level;

typedef struct sched_domain *(*sched_domain_init_f)(struct sched_domain_topology_level *tl, int cpu);
typedef const struct cpumask *(*sched_domain_mask_f)(int cpu);

#define SDTL_OVERLAP	0x01

struct sched_domain_topology_level {
	sched_domain_init_f init;
	sched_domain_mask_f mask;
	int		    flags;
	int		    numa_level;
	struct sd_data      data;
};

/*
 * Initializers for schedule domains
 * Non-inlined to reduce accumulated stack pressure in build_sched_domains()
 */

#ifdef CONFIG_SCHED_DEBUG
# define SD_INIT_NAME(sd, type)		sd->name = #type
#else
# define SD_INIT_NAME(sd, type)		do { } while (0)
#endif

#define SD_INIT_FUNC(type)						\
static noinline struct sched_domain *					\
sd_init_##type(struct sched_domain_topology_level *tl, int cpu) 	\
{									\
	struct sched_domain *sd = *per_cpu_ptr(tl->data.sd, cpu);	\
	*sd = SD_##type##_INIT;						\
	SD_INIT_NAME(sd, type);						\
	sd->private = &tl->data;					\
	return sd;							\
}

SD_INIT_FUNC(CPU)
#ifdef CONFIG_SCHED_SMT
 SD_INIT_FUNC(SIBLING)
#endif
#ifdef CONFIG_SCHED_MC
 SD_INIT_FUNC(MC)
#endif
#ifdef CONFIG_SCHED_BOOK
 SD_INIT_FUNC(BOOK)
#endif

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

#ifdef CONFIG_SCHED_SMT
static const struct cpumask *cpu_smt_mask(int cpu)
{
	return topology_thread_cpumask(cpu);
}
#endif

/*
 * Topology list, bottom-up.
 */
static struct sched_domain_topology_level default_topology[] = {
#ifdef CONFIG_SCHED_SMT
	{ sd_init_SIBLING, cpu_smt_mask, },
#endif
#ifdef CONFIG_SCHED_MC
	{ sd_init_MC, cpu_coregroup_mask, },
#endif
#ifdef CONFIG_SCHED_BOOK
	{ sd_init_BOOK, cpu_book_mask, },
#endif
	{ sd_init_CPU, cpu_cpu_mask, },
	{ NULL, },
};

static struct sched_domain_topology_level *sched_domain_topology = default_topology;

#ifdef CONFIG_NUMA

static int sched_domains_numa_levels;
static int *sched_domains_numa_distance;
static struct cpumask ***sched_domains_numa_masks;
static int sched_domains_curr_level;

static inline int sd_local_flags(int level)
{
	if (sched_domains_numa_distance[level] > RECLAIM_DISTANCE)
		return 0;

	return SD_BALANCE_EXEC | SD_BALANCE_FORK | SD_WAKE_AFFINE;
}

static struct sched_domain *
sd_numa_init(struct sched_domain_topology_level *tl, int cpu)
{
	struct sched_domain *sd = *per_cpu_ptr(tl->data.sd, cpu);
	int level = tl->numa_level;
	int sd_weight = cpumask_weight(
			sched_domains_numa_masks[level][cpu_to_node(cpu)]);

	*sd = (struct sched_domain){
		.min_interval		= sd_weight,
		.max_interval		= 2*sd_weight,
		.busy_factor		= 32,
		.imbalance_pct		= 125,
		.cache_nice_tries	= 2,
		.busy_idx		= 3,
		.idle_idx		= 2,
		.newidle_idx		= 0,
		.wake_idx		= 0,
		.forkexec_idx		= 0,

		.flags			= 1*SD_LOAD_BALANCE
					| 1*SD_BALANCE_NEWIDLE
					| 0*SD_BALANCE_EXEC
					| 0*SD_BALANCE_FORK
					| 0*SD_BALANCE_WAKE
					| 0*SD_WAKE_AFFINE
					| 0*SD_SHARE_CPUPOWER
					| 0*SD_SHARE_PKG_RESOURCES
					| 1*SD_SERIALIZE
					| 0*SD_PREFER_SIBLING
					| sd_local_flags(level)
					,
		.last_balance		= jiffies,
		.balance_interval	= sd_weight,
	};
	SD_INIT_NAME(sd, NUMA);
	sd->private = &tl->data;

	/*
	 * Ugly hack to pass state to sd_numa_mask()...
	 */
	sched_domains_curr_level = tl->numa_level;

	return sd;
}

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

			for (k = 0; k < nr_node_ids; k++) {
				if (node_distance(j, k) > sched_domains_numa_distance[i])
					continue;

				cpumask_or(mask, mask, cpumask_of_node(k));
			}
		}
	}

	tl = kzalloc((ARRAY_SIZE(default_topology) + level) *
			sizeof(struct sched_domain_topology_level), GFP_KERNEL);
	if (!tl)
		return;

	/*
	 * Copy the default topology bits..
	 */
	for (i = 0; default_topology[i].init; i++)
		tl[i] = default_topology[i];

	/*
	 * .. and append 'j' levels of NUMA goodness.
	 */
	for (j = 0; j < level; i++, j++) {
		tl[i] = (struct sched_domain_topology_level){
			.init = sd_numa_init,
			.mask = sd_numa_mask,
			.flags = SDTL_OVERLAP,
			.numa_level = j,
		};
	}

	sched_domain_topology = tl;

	sched_domains_numa_levels = level;
}

static void sched_domains_numa_masks_set(int cpu)
{
	int i, j;
	int node = cpu_to_node(cpu);

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

/*
 * Update sched_domains_numa_masks[level][node] array when new cpus
 * are onlined.
 */
static int sched_domains_numa_masks_update(struct notifier_block *nfb,
					   unsigned long action,
					   void *hcpu)
{
	int cpu = (long)hcpu;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_ONLINE:
		sched_domains_numa_masks_set(cpu);
		break;

	case CPU_DEAD:
		sched_domains_numa_masks_clear(cpu);
		break;

	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}
#else
static inline void sched_init_numa(void)
{
}

static int sched_domains_numa_masks_update(struct notifier_block *nfb,
					   unsigned long action,
					   void *hcpu)
{
	return 0;
}
#endif /* CONFIG_NUMA */

static int __sdt_alloc(const struct cpumask *cpu_map)
{
	struct sched_domain_topology_level *tl;
	int j;

	for (tl = sched_domain_topology; tl->init; tl++) {
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

	for (tl = sched_domain_topology; tl->init; tl++) {
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
		struct s_data *d, const struct cpumask *cpu_map,
		struct sched_domain_attr *attr, struct sched_domain *child,
		int cpu)
{
	struct sched_domain *sd = tl->init(tl, cpu);
	if (!sd)
		return child;

	cpumask_and(sched_domain_span(sd), cpu_map, tl->mask(cpu));
	if (child) {
		sd->level = child->level + 1;
		sched_domain_level_max = max(sched_domain_level_max, sd->level);
		child->parent = sd;
	}
	sd->child = child;
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
	enum s_alloc alloc_state = sa_none;
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
		for (tl = sched_domain_topology; tl->init; tl++) {
			sd = build_sched_domain(tl, &d, cpu_map, attr, sd, i);
			if (tl->flags & SDTL_OVERLAP)
				sd->flags |= SD_OVERLAP;
			if (cpumask_equal(cpu_map, sched_domain_span(sd)))
				break;
		}

		while (sd->child)
			sd = sd->child;

		*per_cpu_ptr(d.sd, i) = sd;
	}

	/* Calculate CPU power for physical packages and nodes */
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
int __attribute__((weak)) arch_update_cpu_topology(void)
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

	if (doms_new == NULL) {
		ndoms_cur = 0;
		doms_new = &fallback_doms;
		cpumask_andnot(doms_new[0], cpu_active_mask, cpu_isolated_map);
		WARN_ON_ONCE(dattr_new);
	}

	/* Build new domains */
	for (i = 0; i < ndoms_new; i++) {
		for (j = 0; j < ndoms_cur && !new_topology; j++) {
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

/*
 * Update cpusets according to cpu_active mask.  If cpusets are
 * disabled, cpuset_update_active_cpus() becomes a simple wrapper
 * around partition_sched_domains().
 */
static int cpuset_cpu_active(struct notifier_block *nfb, unsigned long action,
			     void *hcpu)
{
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_ONLINE:
	case CPU_DOWN_FAILED:
		cpuset_update_active_cpus(true);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

static int cpuset_cpu_inactive(struct notifier_block *nfb, unsigned long action,
			       void *hcpu)
{
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_DOWN_PREPARE:
		cpuset_update_active_cpus(false);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

#if defined(CONFIG_SCHED_SMT) || defined(CONFIG_SCHED_MC)
/*
 * Cheaper version of the below functions in case support for SMT and MC is
 * compiled in but CPUs have no siblings.
 */
static bool sole_cpu_idle(int cpu)
{
	return rq_idle(cpu_rq(cpu));
}
#endif
#ifdef CONFIG_SCHED_SMT
/* All this CPU's SMT siblings are idle */
static bool siblings_cpu_idle(int cpu)
{
	return cpumask_subset(&(cpu_rq(cpu)->smt_siblings),
			      &grq.cpu_idle_map);
}
#endif
#ifdef CONFIG_SCHED_MC
/* All this CPU's shared cache siblings are idle */
static bool cache_cpu_idle(int cpu)
{
	return cpumask_subset(&(cpu_rq(cpu)->cache_siblings),
			      &grq.cpu_idle_map);
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
	int cpu;

	cpumask_var_t non_isolated_cpus;

	alloc_cpumask_var(&non_isolated_cpus, GFP_KERNEL);
	alloc_cpumask_var(&fallback_doms, GFP_KERNEL);

	sched_init_numa();

	get_online_cpus();
	mutex_lock(&sched_domains_mutex);
	init_sched_domains(cpu_active_mask);
	cpumask_andnot(non_isolated_cpus, cpu_possible_mask, cpu_isolated_map);
	if (cpumask_empty(non_isolated_cpus))
		cpumask_set_cpu(smp_processor_id(), non_isolated_cpus);
	mutex_unlock(&sched_domains_mutex);
	put_online_cpus();

	hotcpu_notifier(sched_domains_numa_masks_update, CPU_PRI_SCHED_ACTIVE);
	hotcpu_notifier(cpuset_cpu_active, CPU_PRI_CPUSET_ACTIVE);
	hotcpu_notifier(cpuset_cpu_inactive, CPU_PRI_CPUSET_INACTIVE);

	/* Move init over to a non-isolated CPU */
	if (set_cpus_allowed_ptr(current, non_isolated_cpus) < 0)
		BUG();
	free_cpumask_var(non_isolated_cpus);

	grq_lock_irq();
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
		struct rq *rq = cpu_rq(cpu);

		mutex_lock(&sched_domains_mutex);
		for_each_domain(cpu, sd) {
			int locality, other_cpu;

#ifdef CONFIG_SCHED_SMT
			if (sd->level == SD_LV_SIBLING) {
				for_each_cpu_mask(other_cpu, *sched_domain_span(sd))
					cpumask_set_cpu(other_cpu, &rq->smt_siblings);
			}
#endif
#ifdef CONFIG_SCHED_MC
			if (sd->level == SD_LV_MC) {
				for_each_cpu_mask(other_cpu, *sched_domain_span(sd))
					cpumask_set_cpu(other_cpu, &rq->cache_siblings);
			}
#endif
			if (sd->level <= SD_LV_SIBLING)
				locality = 1;
			else if (sd->level <= SD_LV_MC)
				locality = 2;
			else if (sd->level <= SD_LV_NODE)
				locality = 3;
			else
				continue;

			for_each_cpu_mask(other_cpu, *sched_domain_span(sd)) {
				if (locality < rq->cpu_locality[other_cpu])
					rq->cpu_locality[other_cpu] = locality;
			}
		}
		mutex_unlock(&sched_domains_mutex);

		/*
		 * Each runqueue has its own function in case it doesn't have
		 * siblings of its own allowing mixed topologies.
		 */
#ifdef CONFIG_SCHED_SMT
		if (cpus_weight(rq->smt_siblings) > 1)
			rq->siblings_idle = siblings_cpu_idle;
#endif
#ifdef CONFIG_SCHED_MC
		if (cpus_weight(rq->cache_siblings) > 1)
			rq->cache_idle = cache_cpu_idle;
#endif
	}
	grq_unlock_irq();
}
#else
void __init sched_init_smp(void)
{
}
#endif /* CONFIG_SMP */

unsigned int sysctl_timer_migration = 1;

int in_sched_functions(unsigned long addr)
{
	return in_lock_functions(addr) ||
		(addr >= (unsigned long)__sched_text_start
		&& addr < (unsigned long)__sched_text_end);
}

void __init sched_init(void)
{
	int i;
	struct rq *rq;

	prio_ratios[0] = 128;
	for (i = 1 ; i < PRIO_RANGE ; i++)
		prio_ratios[i] = prio_ratios[i - 1] * 11 / 10;

	raw_spin_lock_init(&grq.lock);
	grq.nr_running = grq.nr_uninterruptible = grq.nr_switches = 0;
	grq.niffies = 0;
	grq.last_jiffy = jiffies;
	raw_spin_lock_init(&grq.iso_lock);
	grq.iso_ticks = 0;
	grq.iso_refractory = false;
	grq.noc = 1;
#ifdef CONFIG_SMP
	init_defrootdomain();
	grq.qnr = grq.idle_cpus = 0;
	cpumask_clear(&grq.cpu_idle_map);
#else
	uprq = &per_cpu(runqueues, 0);
#endif
	for_each_possible_cpu(i) {
		rq = cpu_rq(i);
		rq->user_pc = rq->nice_pc = rq->softirq_pc = rq->system_pc =
			      rq->iowait_pc = rq->idle_pc = 0;
		rq->dither = false;
#ifdef CONFIG_SMP
		rq->sticky_task = NULL;
		rq->last_niffy = 0;
		rq->sd = NULL;
		rq->rd = NULL;
		rq->online = false;
		rq->cpu = i;
		rq_attach_root(rq, &def_root_domain);
#endif
		atomic_set(&rq->nr_iowait, 0);
	}

#ifdef CONFIG_SMP
	nr_cpu_ids = i;
	/*
	 * Set the base locality for cpu cache distance calculation to
	 * "distant" (3). Make sure the distance from a CPU to itself is 0.
	 */
	for_each_possible_cpu(i) {
		int j;

		rq = cpu_rq(i);
#ifdef CONFIG_SCHED_SMT
		cpumask_clear(&rq->smt_siblings);
		cpumask_set_cpu(i, &rq->smt_siblings);
		rq->siblings_idle = sole_cpu_idle;
		cpumask_set_cpu(i, &rq->smt_siblings);
#endif
#ifdef CONFIG_SCHED_MC
		cpumask_clear(&rq->cache_siblings);
		cpumask_set_cpu(i, &rq->cache_siblings);
		rq->cache_idle = sole_cpu_idle;
		cpumask_set_cpu(i, &rq->cache_siblings);
#endif
		rq->cpu_locality = kmalloc(nr_cpu_ids * sizeof(int *), GFP_ATOMIC);
		for_each_possible_cpu(j) {
			if (i == j)
				rq->cpu_locality[j] = 0;
			else
				rq->cpu_locality[j] = 4;
		}
	}
#endif

	for (i = 0; i < PRIO_LIMIT; i++)
		INIT_LIST_HEAD(grq.queue + i);
	/* delimiter for bitsearch */
	__set_bit(PRIO_LIMIT, grq.prio_bitmap);

#ifdef CONFIG_PREEMPT_NOTIFIERS
	INIT_HLIST_HEAD(&init_task.preempt_notifiers);
#endif

#ifdef CONFIG_RT_MUTEXES
	plist_head_init(&init_task.pi_waiters);
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
}

#ifdef CONFIG_DEBUG_ATOMIC_SLEEP
static inline int preempt_count_equals(int preempt_offset)
{
	int nested = (preempt_count() & ~PREEMPT_ACTIVE) + rcu_preempt_depth();

	return (nested == preempt_offset);
}

void __might_sleep(const char *file, int line, int preempt_offset)
{
	static unsigned long prev_jiffy;	/* ratelimiting */

	rcu_sleep_check(); /* WARN_ON_ONCE() by default, no rate limit reqd. */
	if ((preempt_count_equals(preempt_offset) && !irqs_disabled()) ||
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

	debug_show_held_locks(current);
	if (irqs_disabled())
		print_irqtrace_events(current);
	dump_stack();
}
EXPORT_SYMBOL(__might_sleep);
#endif

#ifdef CONFIG_MAGIC_SYSRQ
void normalize_rt_tasks(void)
{
	struct task_struct *g, *p;
	unsigned long flags;
	struct rq *rq;
	int queued;

	read_lock_irqsave(&tasklist_lock, flags);

	do_each_thread(g, p) {
		if (!rt_task(p) && !iso_task(p))
			continue;

		raw_spin_lock(&p->pi_lock);
		rq = __task_grq_lock(p);

		queued = task_queued(p);
		if (queued)
			dequeue_task(p);
		__setscheduler(p, rq, SCHED_NORMAL, 0);
		if (queued) {
			enqueue_task(p);
			try_preempt(p, rq);
		}

		__task_grq_unlock();
		raw_spin_unlock(&p->pi_lock);
	} while_each_thread(g, p);

	read_unlock_irqrestore(&tasklist_lock, flags);
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

#ifndef __ARCH_HAS_VTIME_TASK_SWITCH
void vtime_task_switch(struct task_struct *prev)
{
	if (is_idle_task(prev))
		vtime_account_idle(prev);
	else
		vtime_account_system(prev);

	vtime_account_user(prev);
	arch_vtime_task_switch(prev);
}
#endif

#else
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
			   struct cputime *prev,
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

/*
 * Must be called with siglock held.
 */
void thread_group_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	struct task_cputime cputime;

	thread_group_cputime(p, &cputime);
	cputime_adjust(&cputime, &p->signal->prev_cputime, ut, st);
}
#endif

void __cpuinit init_idle_bootup_task(struct task_struct *idle)
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
