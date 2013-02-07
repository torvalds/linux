
#include <linux/sched.h>
#include <linux/sched/sysctl.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/stop_machine.h>

#include "cpupri.h"

extern __read_mostly int scheduler_running;

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
 * Helpers for converting nanosecond timing to jiffy resolution
 */
#define NS_TO_JIFFIES(TIME)	((unsigned long)(TIME) / (NSEC_PER_SEC / HZ))

#define NICE_0_LOAD		SCHED_LOAD_SCALE
#define NICE_0_SHIFT		SCHED_LOAD_SHIFT

/*
 * These are the 'tuning knobs' of the scheduler:
 */

/*
 * single value that denotes runtime == period, ie unlimited time.
 */
#define RUNTIME_INF	((u64)~0ULL)

static inline int rt_policy(int policy)
{
	if (policy == SCHED_FIFO || policy == SCHED_RR)
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

struct rt_bandwidth {
	/* nests inside the rq lock: */
	raw_spinlock_t		rt_runtime_lock;
	ktime_t			rt_period;
	u64			rt_runtime;
	struct hrtimer		rt_period_timer;
};

extern struct mutex sched_domains_mutex;

#ifdef CONFIG_CGROUP_SCHED

#include <linux/cgroup.h>

struct cfs_rq;
struct rt_rq;

extern struct list_head task_groups;

struct cfs_bandwidth {
#ifdef CONFIG_CFS_BANDWIDTH
	raw_spinlock_t lock;
	ktime_t period;
	u64 quota, runtime;
	s64 hierarchal_quota;
	u64 runtime_expires;

	int idle, timer_active;
	struct hrtimer period_timer, slack_timer;
	struct list_head throttled_cfs_rq;

	/* statistics */
	int nr_periods, nr_throttled;
	u64 throttled_time;
#endif
};

/* task group related information */
struct task_group {
	struct cgroup_subsys_state css;

#ifdef CONFIG_FAIR_GROUP_SCHED
	/* schedulable entities of this group on each cpu */
	struct sched_entity **se;
	/* runqueue "owned" by this group on each cpu */
	struct cfs_rq **cfs_rq;
	unsigned long shares;

	atomic_t load_weight;
	atomic64_t load_avg;
	atomic_t runnable_avg;
#endif

#ifdef CONFIG_RT_GROUP_SCHED
	struct sched_rt_entity **rt_se;
	struct rt_rq **rt_rq;

	struct rt_bandwidth rt_bandwidth;
#endif

	struct rcu_head rcu;
	struct list_head list;

	struct task_group *parent;
	struct list_head siblings;
	struct list_head children;

#ifdef CONFIG_SCHED_AUTOGROUP
	struct autogroup *autogroup;
#endif

	struct cfs_bandwidth cfs_bandwidth;
};

#ifdef CONFIG_FAIR_GROUP_SCHED
#define ROOT_TASK_GROUP_LOAD	NICE_0_LOAD

/*
 * A weight of 0 or 1 can cause arithmetics problems.
 * A weight of a cfs_rq is the sum of weights of which entities
 * are queued on this cfs_rq, so a weight of a entity should not be
 * too large, so as the shares value of a task group.
 * (The default weight is 1024 - so there's no practical
 *  limitation from this.)
 */
#define MIN_SHARES	(1UL <<  1)
#define MAX_SHARES	(1UL << 18)
#endif

/* Default task group.
 *	Every task in system belong to this group at bootup.
 */
extern struct task_group root_task_group;

typedef int (*tg_visitor)(struct task_group *, void *);

extern int walk_tg_tree_from(struct task_group *from,
			     tg_visitor down, tg_visitor up, void *data);

/*
 * Iterate the full tree, calling @down when first entering a node and @up when
 * leaving it for the final time.
 *
 * Caller must hold rcu_lock or sufficient equivalent.
 */
static inline int walk_tg_tree(tg_visitor down, tg_visitor up, void *data)
{
	return walk_tg_tree_from(&root_task_group, down, up, data);
}

extern int tg_nop(struct task_group *tg, void *data);

extern void free_fair_sched_group(struct task_group *tg);
extern int alloc_fair_sched_group(struct task_group *tg, struct task_group *parent);
extern void unregister_fair_sched_group(struct task_group *tg, int cpu);
extern void init_tg_cfs_entry(struct task_group *tg, struct cfs_rq *cfs_rq,
			struct sched_entity *se, int cpu,
			struct sched_entity *parent);
extern void init_cfs_bandwidth(struct cfs_bandwidth *cfs_b);
extern int sched_group_set_shares(struct task_group *tg, unsigned long shares);

extern void __refill_cfs_bandwidth_runtime(struct cfs_bandwidth *cfs_b);
extern void __start_cfs_bandwidth(struct cfs_bandwidth *cfs_b);
extern void unthrottle_cfs_rq(struct cfs_rq *cfs_rq);

extern void free_rt_sched_group(struct task_group *tg);
extern int alloc_rt_sched_group(struct task_group *tg, struct task_group *parent);
extern void init_tg_rt_entry(struct task_group *tg, struct rt_rq *rt_rq,
		struct sched_rt_entity *rt_se, int cpu,
		struct sched_rt_entity *parent);

#else /* CONFIG_CGROUP_SCHED */

struct cfs_bandwidth { };

#endif	/* CONFIG_CGROUP_SCHED */

/* CFS-related fields in a runqueue */
struct cfs_rq {
	struct load_weight load;
	unsigned int nr_running, h_nr_running;

	u64 exec_clock;
	u64 min_vruntime;
#ifndef CONFIG_64BIT
	u64 min_vruntime_copy;
#endif

	struct rb_root tasks_timeline;
	struct rb_node *rb_leftmost;

	/*
	 * 'curr' points to currently running entity on this cfs_rq.
	 * It is set to NULL otherwise (i.e when none are currently running).
	 */
	struct sched_entity *curr, *next, *last, *skip;

#ifdef	CONFIG_SCHED_DEBUG
	unsigned int nr_spread_over;
#endif

#ifdef CONFIG_SMP
/*
 * Load-tracking only depends on SMP, FAIR_GROUP_SCHED dependency below may be
 * removed when useful for applications beyond shares distribution (e.g.
 * load-balance).
 */
#ifdef CONFIG_FAIR_GROUP_SCHED
	/*
	 * CFS Load tracking
	 * Under CFS, load is tracked on a per-entity basis and aggregated up.
	 * This allows for the description of both thread and group usage (in
	 * the FAIR_GROUP_SCHED case).
	 */
	u64 runnable_load_avg, blocked_load_avg;
	atomic64_t decay_counter, removed_load;
	u64 last_decay;
#endif /* CONFIG_FAIR_GROUP_SCHED */
/* These always depend on CONFIG_FAIR_GROUP_SCHED */
#ifdef CONFIG_FAIR_GROUP_SCHED
	u32 tg_runnable_contrib;
	u64 tg_load_contrib;
#endif /* CONFIG_FAIR_GROUP_SCHED */

	/*
	 *   h_load = weight * f(tg)
	 *
	 * Where f(tg) is the recursive weight fraction assigned to
	 * this group.
	 */
	unsigned long h_load;
#endif /* CONFIG_SMP */

#ifdef CONFIG_FAIR_GROUP_SCHED
	struct rq *rq;	/* cpu runqueue to which this cfs_rq is attached */

	/*
	 * leaf cfs_rqs are those that hold tasks (lowest schedulable entity in
	 * a hierarchy). Non-leaf lrqs hold other higher schedulable entities
	 * (like users, containers etc.)
	 *
	 * leaf_cfs_rq_list ties together list of leaf cfs_rq's in a cpu. This
	 * list is used during load balance.
	 */
	int on_list;
	struct list_head leaf_cfs_rq_list;
	struct task_group *tg;	/* group that "owns" this runqueue */

#ifdef CONFIG_CFS_BANDWIDTH
	int runtime_enabled;
	u64 runtime_expires;
	s64 runtime_remaining;

	u64 throttled_clock, throttled_clock_task;
	u64 throttled_clock_task_time;
	int throttled, throttle_count;
	struct list_head throttled_list;
#endif /* CONFIG_CFS_BANDWIDTH */
#endif /* CONFIG_FAIR_GROUP_SCHED */
};

static inline int rt_bandwidth_enabled(void)
{
	return sysctl_sched_rt_runtime >= 0;
}

/* Real-Time classes' related field in a runqueue: */
struct rt_rq {
	struct rt_prio_array active;
	unsigned int rt_nr_running;
#if defined CONFIG_SMP || defined CONFIG_RT_GROUP_SCHED
	struct {
		int curr; /* highest queued rt task prio */
#ifdef CONFIG_SMP
		int next; /* next highest */
#endif
	} highest_prio;
#endif
#ifdef CONFIG_SMP
	unsigned long rt_nr_migratory;
	unsigned long rt_nr_total;
	int overloaded;
	struct plist_head pushable_tasks;
#endif
	int rt_throttled;
	u64 rt_time;
	u64 rt_runtime;
	/* Nests inside the rq lock: */
	raw_spinlock_t rt_runtime_lock;

#ifdef CONFIG_RT_GROUP_SCHED
	unsigned long rt_nr_boosted;

	struct rq *rq;
	struct list_head leaf_rt_rq_list;
	struct task_group *tg;
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

extern struct root_domain def_root_domain;

#endif /* CONFIG_SMP */

/*
 * This is the main, per-CPU runqueue data structure.
 *
 * Locking rule: those places that want to lock multiple runqueues
 * (such as the load balancing or the thread migration code), lock
 * acquire operations must be ordered by ascending &runqueue.
 */
struct rq {
	/* runqueue lock: */
	raw_spinlock_t lock;

	/*
	 * nr_running and cpu_load should be in the same cacheline because
	 * remote CPUs use both these fields when doing load calculation.
	 */
	unsigned int nr_running;
	#define CPU_LOAD_IDX_MAX 5
	unsigned long cpu_load[CPU_LOAD_IDX_MAX];
	unsigned long last_load_update_tick;
#ifdef CONFIG_NO_HZ
	u64 nohz_stamp;
	unsigned long nohz_flags;
#endif
	int skip_clock_update;

	/* capture load from *all* tasks on this cpu: */
	struct load_weight load;
	unsigned long nr_load_updates;
	u64 nr_switches;

	struct cfs_rq cfs;
	struct rt_rq rt;

#ifdef CONFIG_FAIR_GROUP_SCHED
	/* list of leaf cfs_rq on this cpu: */
	struct list_head leaf_cfs_rq_list;
#ifdef CONFIG_SMP
	unsigned long h_load_throttle;
#endif /* CONFIG_SMP */
#endif /* CONFIG_FAIR_GROUP_SCHED */

#ifdef CONFIG_RT_GROUP_SCHED
	struct list_head leaf_rt_rq_list;
#endif

	/*
	 * This is part of a global counter where only the total sum
	 * over all CPUs matters. A task can increase this counter on
	 * one CPU and if it got migrated afterwards it may decrease
	 * it on another CPU. Always updated under the runqueue lock:
	 */
	unsigned long nr_uninterruptible;

	struct task_struct *curr, *idle, *stop;
	unsigned long next_balance;
	struct mm_struct *prev_mm;

	u64 clock;
	u64 clock_task;

	atomic_t nr_iowait;

#ifdef CONFIG_SMP
	struct root_domain *rd;
	struct sched_domain *sd;

	unsigned long cpu_power;

	unsigned char idle_balance;
	/* For active balancing */
	int post_schedule;
	int active_balance;
	int push_cpu;
	struct cpu_stop_work active_balance_work;
	/* cpu of this runqueue: */
	int cpu;
	int online;

	struct list_head cfs_tasks;

	u64 rt_avg;
	u64 age_stamp;
	u64 idle_stamp;
	u64 avg_idle;
#endif

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
	u64 prev_irq_time;
#endif
#ifdef CONFIG_PARAVIRT
	u64 prev_steal_time;
#endif
#ifdef CONFIG_PARAVIRT_TIME_ACCOUNTING
	u64 prev_steal_time_rq;
#endif

	/* calc_load related fields */
	unsigned long calc_load_update;
	long calc_load_active;

#ifdef CONFIG_SCHED_HRTICK
#ifdef CONFIG_SMP
	int hrtick_csd_pending;
	struct call_single_data hrtick_csd;
#endif
	struct hrtimer hrtick_timer;
#endif

#ifdef CONFIG_SCHEDSTATS
	/* latency stats */
	struct sched_info rq_sched_info;
	unsigned long long rq_cpu_time;
	/* could above be rq->cfs_rq.exec_clock + rq->rt_rq.rt_runtime ? */

	/* sys_sched_yield() stats */
	unsigned int yld_count;

	/* schedule() stats */
	unsigned int sched_count;
	unsigned int sched_goidle;

	/* try_to_wake_up() stats */
	unsigned int ttwu_count;
	unsigned int ttwu_local;
#endif

#ifdef CONFIG_SMP
	struct llist_head wake_list;
#endif

	struct sched_avg avg;
};

static inline int cpu_of(struct rq *rq)
{
#ifdef CONFIG_SMP
	return rq->cpu;
#else
	return 0;
#endif
}

DECLARE_PER_CPU(struct rq, runqueues);

#define cpu_rq(cpu)		(&per_cpu(runqueues, (cpu)))
#define this_rq()		(&__get_cpu_var(runqueues))
#define task_rq(p)		cpu_rq(task_cpu(p))
#define cpu_curr(cpu)		(cpu_rq(cpu)->curr)
#define raw_rq()		(&__raw_get_cpu_var(runqueues))

#ifdef CONFIG_SMP

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
	for (__sd = rcu_dereference_check_sched_domain(cpu_rq(cpu)->sd); \
			__sd; __sd = __sd->parent)

#define for_each_lower_domain(sd) for (; sd; sd = sd->child)

/**
 * highest_flag_domain - Return highest sched_domain containing flag.
 * @cpu:	The cpu whose highest level of sched domain is to
 *		be returned.
 * @flag:	The flag to check for the highest sched_domain
 *		for the given cpu.
 *
 * Returns the highest sched_domain of a cpu which contains the given flag.
 */
static inline struct sched_domain *highest_flag_domain(int cpu, int flag)
{
	struct sched_domain *sd, *hsd = NULL;

	for_each_domain(cpu, sd) {
		if (!(sd->flags & flag))
			break;
		hsd = sd;
	}

	return hsd;
}

DECLARE_PER_CPU(struct sched_domain *, sd_llc);
DECLARE_PER_CPU(int, sd_llc_id);

extern int group_balance_cpu(struct sched_group *sg);

#endif /* CONFIG_SMP */

#include "stats.h"
#include "auto_group.h"

#ifdef CONFIG_CGROUP_SCHED

/*
 * Return the group to which this tasks belongs.
 *
 * We cannot use task_subsys_state() and friends because the cgroup
 * subsystem changes that value before the cgroup_subsys::attach() method
 * is called, therefore we cannot pin it and might observe the wrong value.
 *
 * The same is true for autogroup's p->signal->autogroup->tg, the autogroup
 * core changes this before calling sched_move_task().
 *
 * Instead we use a 'copy' which is updated from sched_move_task() while
 * holding both task_struct::pi_lock and rq::lock.
 */
static inline struct task_group *task_group(struct task_struct *p)
{
	return p->sched_task_group;
}

/* Change a task's cfs_rq and parent entity if it moves across CPUs/groups */
static inline void set_task_rq(struct task_struct *p, unsigned int cpu)
{
#if defined(CONFIG_FAIR_GROUP_SCHED) || defined(CONFIG_RT_GROUP_SCHED)
	struct task_group *tg = task_group(p);
#endif

#ifdef CONFIG_FAIR_GROUP_SCHED
	p->se.cfs_rq = tg->cfs_rq[cpu];
	p->se.parent = tg->se[cpu];
#endif

#ifdef CONFIG_RT_GROUP_SCHED
	p->rt.rt_rq  = tg->rt_rq[cpu];
	p->rt.parent = tg->rt_se[cpu];
#endif
}

#else /* CONFIG_CGROUP_SCHED */

static inline void set_task_rq(struct task_struct *p, unsigned int cpu) { }
static inline struct task_group *task_group(struct task_struct *p)
{
	return NULL;
}

#endif /* CONFIG_CGROUP_SCHED */

static inline void __set_task_cpu(struct task_struct *p, unsigned int cpu)
{
	set_task_rq(p, cpu);
#ifdef CONFIG_SMP
	/*
	 * After ->cpu is set up to a new value, task_rq_lock(p, ...) can be
	 * successfuly executed on another CPU. We must ensure that updates of
	 * per-task data have been completed by this moment.
	 */
	smp_wmb();
	task_thread_info(p)->cpu = cpu;
#endif
}

/*
 * Tunables that become constants when CONFIG_SCHED_DEBUG is off:
 */
#ifdef CONFIG_SCHED_DEBUG
# include <linux/static_key.h>
# define const_debug __read_mostly
#else
# define const_debug const
#endif

extern const_debug unsigned int sysctl_sched_features;

#define SCHED_FEAT(name, enabled)	\
	__SCHED_FEAT_##name ,

enum {
#include "features.h"
	__SCHED_FEAT_NR,
};

#undef SCHED_FEAT

#if defined(CONFIG_SCHED_DEBUG) && defined(HAVE_JUMP_LABEL)
static __always_inline bool static_branch__true(struct static_key *key)
{
	return static_key_true(key); /* Not out of line branch. */
}

static __always_inline bool static_branch__false(struct static_key *key)
{
	return static_key_false(key); /* Out of line branch. */
}

#define SCHED_FEAT(name, enabled)					\
static __always_inline bool static_branch_##name(struct static_key *key) \
{									\
	return static_branch__##enabled(key);				\
}

#include "features.h"

#undef SCHED_FEAT

extern struct static_key sched_feat_keys[__SCHED_FEAT_NR];
#define sched_feat(x) (static_branch_##x(&sched_feat_keys[__SCHED_FEAT_##x]))
#else /* !(SCHED_DEBUG && HAVE_JUMP_LABEL) */
#define sched_feat(x) (sysctl_sched_features & (1UL << __SCHED_FEAT_##x))
#endif /* SCHED_DEBUG && HAVE_JUMP_LABEL */

#ifdef CONFIG_NUMA_BALANCING
#define sched_feat_numa(x) sched_feat(x)
#ifdef CONFIG_SCHED_DEBUG
#define numabalancing_enabled sched_feat_numa(NUMA)
#else
extern bool numabalancing_enabled;
#endif /* CONFIG_SCHED_DEBUG */
#else
#define sched_feat_numa(x) (0)
#define numabalancing_enabled (0)
#endif /* CONFIG_NUMA_BALANCING */

static inline u64 global_rt_period(void)
{
	return (u64)sysctl_sched_rt_period * NSEC_PER_USEC;
}

static inline u64 global_rt_runtime(void)
{
	if (sysctl_sched_rt_runtime < 0)
		return RUNTIME_INF;

	return (u64)sysctl_sched_rt_runtime * NSEC_PER_USEC;
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


#ifndef prepare_arch_switch
# define prepare_arch_switch(next)	do { } while (0)
#endif
#ifndef finish_arch_switch
# define finish_arch_switch(prev)	do { } while (0)
#endif
#ifndef finish_arch_post_lock_switch
# define finish_arch_post_lock_switch()	do { } while (0)
#endif

#ifndef __ARCH_WANT_UNLOCKED_CTXSW
static inline void prepare_lock_switch(struct rq *rq, struct task_struct *next)
{
#ifdef CONFIG_SMP
	/*
	 * We can optimise this out completely for !SMP, because the
	 * SMP rebalancing from interrupt is the only thing that cares
	 * here.
	 */
	next->on_cpu = 1;
#endif
}

static inline void finish_lock_switch(struct rq *rq, struct task_struct *prev)
{
#ifdef CONFIG_SMP
	/*
	 * After ->on_cpu is cleared, the task can be moved to a different CPU.
	 * We must ensure this doesn't happen until the switch is completely
	 * finished.
	 */
	smp_wmb();
	prev->on_cpu = 0;
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

	raw_spin_unlock_irq(&rq->lock);
}

#else /* __ARCH_WANT_UNLOCKED_CTXSW */
static inline void prepare_lock_switch(struct rq *rq, struct task_struct *next)
{
#ifdef CONFIG_SMP
	/*
	 * We can optimise this out completely for !SMP, because the
	 * SMP rebalancing from interrupt is the only thing that cares
	 * here.
	 */
	next->on_cpu = 1;
#endif
	raw_spin_unlock(&rq->lock);
}

static inline void finish_lock_switch(struct rq *rq, struct task_struct *prev)
{
#ifdef CONFIG_SMP
	/*
	 * After ->on_cpu is cleared, the task can be moved to a different CPU.
	 * We must ensure this doesn't happen until the switch is completely
	 * finished.
	 */
	smp_wmb();
	prev->on_cpu = 0;
#endif
	local_irq_enable();
}
#endif /* __ARCH_WANT_UNLOCKED_CTXSW */


static inline void update_load_add(struct load_weight *lw, unsigned long inc)
{
	lw->weight += inc;
	lw->inv_weight = 0;
}

static inline void update_load_sub(struct load_weight *lw, unsigned long dec)
{
	lw->weight -= dec;
	lw->inv_weight = 0;
}

static inline void update_load_set(struct load_weight *lw, unsigned long w)
{
	lw->weight = w;
	lw->inv_weight = 0;
}

/*
 * To aid in avoiding the subversion of "niceness" due to uneven distribution
 * of tasks with abnormal "nice" values across CPUs the contribution that
 * each task makes to its run queue's load is weighted according to its
 * scheduling class and "nice" value. For SCHED_NORMAL tasks this is just a
 * scaled version of the new time slice allocation that they receive on time
 * slice expiry etc.
 */

#define WEIGHT_IDLEPRIO                3
#define WMULT_IDLEPRIO         1431655765

/*
 * Nice levels are multiplicative, with a gentle 10% change for every
 * nice level changed. I.e. when a CPU-bound task goes from nice 0 to
 * nice 1, it will get ~10% less CPU time than another CPU-bound task
 * that remained on nice 0.
 *
 * The "10% effect" is relative and cumulative: from _any_ nice level,
 * if you go up 1 level, it's -10% CPU usage, if you go down 1 level
 * it's +10% CPU usage. (to achieve that we use a multiplier of 1.25.
 * If a task goes up by ~10% and another task goes down by ~10% then
 * the relative distance between them is ~25%.)
 */
static const int prio_to_weight[40] = {
 /* -20 */     88761,     71755,     56483,     46273,     36291,
 /* -15 */     29154,     23254,     18705,     14949,     11916,
 /* -10 */      9548,      7620,      6100,      4904,      3906,
 /*  -5 */      3121,      2501,      1991,      1586,      1277,
 /*   0 */      1024,       820,       655,       526,       423,
 /*   5 */       335,       272,       215,       172,       137,
 /*  10 */       110,        87,        70,        56,        45,
 /*  15 */        36,        29,        23,        18,        15,
};

/*
 * Inverse (2^32/x) values of the prio_to_weight[] array, precalculated.
 *
 * In cases where the weight does not change often, we can use the
 * precalculated inverse to speed up arithmetics by turning divisions
 * into multiplications:
 */
static const u32 prio_to_wmult[40] = {
 /* -20 */     48388,     59856,     76040,     92818,    118348,
 /* -15 */    147320,    184698,    229616,    287308,    360437,
 /* -10 */    449829,    563644,    704093,    875809,   1099582,
 /*  -5 */   1376151,   1717300,   2157191,   2708050,   3363326,
 /*   0 */   4194304,   5237765,   6557202,   8165337,  10153587,
 /*   5 */  12820798,  15790321,  19976592,  24970740,  31350126,
 /*  10 */  39045157,  49367440,  61356676,  76695844,  95443717,
 /*  15 */ 119304647, 148102320, 186737708, 238609294, 286331153,
};

/* Time spent by the tasks of the cpu accounting group executing in ... */
enum cpuacct_stat_index {
	CPUACCT_STAT_USER,	/* ... user mode */
	CPUACCT_STAT_SYSTEM,	/* ... kernel mode */

	CPUACCT_STAT_NSTATS,
};


#define sched_class_highest (&stop_sched_class)
#define for_each_class(class) \
   for (class = sched_class_highest; class; class = class->next)

extern const struct sched_class stop_sched_class;
extern const struct sched_class rt_sched_class;
extern const struct sched_class fair_sched_class;
extern const struct sched_class idle_sched_class;


#ifdef CONFIG_SMP

extern void trigger_load_balance(struct rq *rq, int cpu);
extern void idle_balance(int this_cpu, struct rq *this_rq);

#else	/* CONFIG_SMP */

static inline void idle_balance(int cpu, struct rq *rq)
{
}

#endif

extern void sysrq_sched_debug_show(void);
extern void sched_init_granularity(void);
extern void update_max_interval(void);
extern void update_group_power(struct sched_domain *sd, int cpu);
extern int update_runtime(struct notifier_block *nfb, unsigned long action, void *hcpu);
extern void init_sched_rt_class(void);
extern void init_sched_fair_class(void);

extern void resched_task(struct task_struct *p);
extern void resched_cpu(int cpu);

extern struct rt_bandwidth def_rt_bandwidth;
extern void init_rt_bandwidth(struct rt_bandwidth *rt_b, u64 period, u64 runtime);

extern void update_idle_cpu_load(struct rq *this_rq);

#ifdef CONFIG_CGROUP_CPUACCT
#include <linux/cgroup.h>
/* track cpu usage of a group of tasks and its child groups */
struct cpuacct {
	struct cgroup_subsys_state css;
	/* cpuusage holds pointer to a u64-type object on every cpu */
	u64 __percpu *cpuusage;
	struct kernel_cpustat __percpu *cpustat;
};

extern struct cgroup_subsys cpuacct_subsys;
extern struct cpuacct root_cpuacct;

/* return cpu accounting group corresponding to this container */
static inline struct cpuacct *cgroup_ca(struct cgroup *cgrp)
{
	return container_of(cgroup_subsys_state(cgrp, cpuacct_subsys_id),
			    struct cpuacct, css);
}

/* return cpu accounting group to which this task belongs */
static inline struct cpuacct *task_ca(struct task_struct *tsk)
{
	return container_of(task_subsys_state(tsk, cpuacct_subsys_id),
			    struct cpuacct, css);
}

static inline struct cpuacct *parent_ca(struct cpuacct *ca)
{
	if (!ca || !ca->css.cgroup->parent)
		return NULL;
	return cgroup_ca(ca->css.cgroup->parent);
}

extern void cpuacct_charge(struct task_struct *tsk, u64 cputime);
#else
static inline void cpuacct_charge(struct task_struct *tsk, u64 cputime) {}
#endif

#ifdef CONFIG_PARAVIRT
static inline u64 steal_ticks(u64 steal)
{
	if (unlikely(steal > NSEC_PER_SEC))
		return div_u64(steal, TICK_NSEC);

	return __iter_div_u64_rem(steal, TICK_NSEC, &steal);
}
#endif

static inline void inc_nr_running(struct rq *rq)
{
	rq->nr_running++;
}

static inline void dec_nr_running(struct rq *rq)
{
	rq->nr_running--;
}

extern void update_rq_clock(struct rq *rq);

extern void activate_task(struct rq *rq, struct task_struct *p, int flags);
extern void deactivate_task(struct rq *rq, struct task_struct *p, int flags);

extern void check_preempt_curr(struct rq *rq, struct task_struct *p, int flags);

extern const_debug unsigned int sysctl_sched_time_avg;
extern const_debug unsigned int sysctl_sched_nr_migrate;
extern const_debug unsigned int sysctl_sched_migration_cost;

static inline u64 sched_avg_period(void)
{
	return (u64)sysctl_sched_time_avg * NSEC_PER_MSEC / 2;
}

#ifdef CONFIG_SCHED_HRTICK

/*
 * Use hrtick when:
 *  - enabled by features
 *  - hrtimer is actually high res
 */
static inline int hrtick_enabled(struct rq *rq)
{
	if (!sched_feat(HRTICK))
		return 0;
	if (!cpu_active(cpu_of(rq)))
		return 0;
	return hrtimer_is_hres_active(&rq->hrtick_timer);
}

void hrtick_start(struct rq *rq, u64 delay);

#else

static inline int hrtick_enabled(struct rq *rq)
{
	return 0;
}

#endif /* CONFIG_SCHED_HRTICK */

#ifdef CONFIG_SMP
extern void sched_avg_update(struct rq *rq);
static inline void sched_rt_avg_update(struct rq *rq, u64 rt_delta)
{
	rq->rt_avg += rt_delta;
	sched_avg_update(rq);
}
#else
static inline void sched_rt_avg_update(struct rq *rq, u64 rt_delta) { }
static inline void sched_avg_update(struct rq *rq) { }
#endif

extern void start_bandwidth_timer(struct hrtimer *period_timer, ktime_t period);

#ifdef CONFIG_SMP
#ifdef CONFIG_PREEMPT

static inline void double_rq_lock(struct rq *rq1, struct rq *rq2);

/*
 * fair double_lock_balance: Safely acquires both rq->locks in a fair
 * way at the expense of forcing extra atomic operations in all
 * invocations.  This assures that the double_lock is acquired using the
 * same underlying policy as the spinlock_t on this architecture, which
 * reduces latency compared to the unfair variant below.  However, it
 * also adds more overhead and therefore may reduce throughput.
 */
static inline int _double_lock_balance(struct rq *this_rq, struct rq *busiest)
	__releases(this_rq->lock)
	__acquires(busiest->lock)
	__acquires(this_rq->lock)
{
	raw_spin_unlock(&this_rq->lock);
	double_rq_lock(this_rq, busiest);

	return 1;
}

#else
/*
 * Unfair double_lock_balance: Optimizes throughput at the expense of
 * latency by eliminating extra atomic operations when the locks are
 * already in proper order on entry.  This favors lower cpu-ids and will
 * grant the double lock to lower cpus over higher ids under contention,
 * regardless of entry order into the function.
 */
static inline int _double_lock_balance(struct rq *this_rq, struct rq *busiest)
	__releases(this_rq->lock)
	__acquires(busiest->lock)
	__acquires(this_rq->lock)
{
	int ret = 0;

	if (unlikely(!raw_spin_trylock(&busiest->lock))) {
		if (busiest < this_rq) {
			raw_spin_unlock(&this_rq->lock);
			raw_spin_lock(&busiest->lock);
			raw_spin_lock_nested(&this_rq->lock,
					      SINGLE_DEPTH_NESTING);
			ret = 1;
		} else
			raw_spin_lock_nested(&busiest->lock,
					      SINGLE_DEPTH_NESTING);
	}
	return ret;
}

#endif /* CONFIG_PREEMPT */

/*
 * double_lock_balance - lock the busiest runqueue, this_rq is locked already.
 */
static inline int double_lock_balance(struct rq *this_rq, struct rq *busiest)
{
	if (unlikely(!irqs_disabled())) {
		/* printk() doesn't work good under rq->lock */
		raw_spin_unlock(&this_rq->lock);
		BUG_ON(1);
	}

	return _double_lock_balance(this_rq, busiest);
}

static inline void double_unlock_balance(struct rq *this_rq, struct rq *busiest)
	__releases(busiest->lock)
{
	raw_spin_unlock(&busiest->lock);
	lock_set_subclass(&this_rq->lock.dep_map, 0, _RET_IP_);
}

/*
 * double_rq_lock - safely lock two runqueues
 *
 * Note this does not disable interrupts like task_rq_lock,
 * you need to do so manually before calling.
 */
static inline void double_rq_lock(struct rq *rq1, struct rq *rq2)
	__acquires(rq1->lock)
	__acquires(rq2->lock)
{
	BUG_ON(!irqs_disabled());
	if (rq1 == rq2) {
		raw_spin_lock(&rq1->lock);
		__acquire(rq2->lock);	/* Fake it out ;) */
	} else {
		if (rq1 < rq2) {
			raw_spin_lock(&rq1->lock);
			raw_spin_lock_nested(&rq2->lock, SINGLE_DEPTH_NESTING);
		} else {
			raw_spin_lock(&rq2->lock);
			raw_spin_lock_nested(&rq1->lock, SINGLE_DEPTH_NESTING);
		}
	}
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

#else /* CONFIG_SMP */

/*
 * double_rq_lock - safely lock two runqueues
 *
 * Note this does not disable interrupts like task_rq_lock,
 * you need to do so manually before calling.
 */
static inline void double_rq_lock(struct rq *rq1, struct rq *rq2)
	__acquires(rq1->lock)
	__acquires(rq2->lock)
{
	BUG_ON(!irqs_disabled());
	BUG_ON(rq1 != rq2);
	raw_spin_lock(&rq1->lock);
	__acquire(rq2->lock);	/* Fake it out ;) */
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
	BUG_ON(rq1 != rq2);
	raw_spin_unlock(&rq1->lock);
	__release(rq2->lock);
}

#endif

extern struct sched_entity *__pick_first_entity(struct cfs_rq *cfs_rq);
extern struct sched_entity *__pick_last_entity(struct cfs_rq *cfs_rq);
extern void print_cfs_stats(struct seq_file *m, int cpu);
extern void print_rt_stats(struct seq_file *m, int cpu);

extern void init_cfs_rq(struct cfs_rq *cfs_rq);
extern void init_rt_rq(struct rt_rq *rt_rq, struct rq *rq);

extern void account_cfs_bandwidth_used(int enabled, int was_enabled);

#ifdef CONFIG_NO_HZ
enum rq_nohz_flag_bits {
	NOHZ_TICK_STOPPED,
	NOHZ_BALANCE_KICK,
	NOHZ_IDLE,
};

#define nohz_flags(cpu)	(&cpu_rq(cpu)->nohz_flags)
#endif

#ifdef CONFIG_IRQ_TIME_ACCOUNTING

DECLARE_PER_CPU(u64, cpu_hardirq_time);
DECLARE_PER_CPU(u64, cpu_softirq_time);

#ifndef CONFIG_64BIT
DECLARE_PER_CPU(seqcount_t, irq_time_seq);

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
#endif /* CONFIG_IRQ_TIME_ACCOUNTING */
