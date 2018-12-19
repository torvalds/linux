/*
 * Completely Fair Scheduling (CFS) Class (SCHED_NORMAL/SCHED_BATCH)
 *
 *  Copyright (C) 2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 *  Interactivity improvements by Mike Galbraith
 *  (C) 2007 Mike Galbraith <efault@gmx.de>
 *
 *  Various enhancements by Dmitry Adamushko.
 *  (C) 2007 Dmitry Adamushko <dmitry.adamushko@gmail.com>
 *
 *  Group scheduling enhancements by Srivatsa Vaddagiri
 *  Copyright IBM Corporation, 2007
 *  Author: Srivatsa Vaddagiri <vatsa@linux.vnet.ibm.com>
 *
 *  Scaled math optimizations by Thomas Gleixner
 *  Copyright (C) 2007, Thomas Gleixner <tglx@linutronix.de>
 *
 *  Adaptive scheduling granularity, math enhancements by Peter Zijlstra
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra
 */

#include <linux/latencytop.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/cpuidle.h>
#include <linux/slab.h>
#include <linux/profile.h>
#include <linux/interrupt.h>
#include <linux/mempolicy.h>
#include <linux/migrate.h>
#include <linux/task_work.h>
#include <linux/module.h>

#include <trace/events/sched.h>

#include "sched.h"
#include "tune.h"
#include "walt.h"

/*
 * Targeted preemption latency for CPU-bound tasks:
 * (default: 6ms * (1 + ilog(ncpus)), units: nanoseconds)
 *
 * NOTE: this latency value is not the same as the concept of
 * 'timeslice length' - timeslices in CFS are of variable length
 * and have no persistent notion like in traditional, time-slice
 * based scheduling concepts.
 *
 * (to see the precise effective timeslice length of your workload,
 *  run vmstat and monitor the context-switches (cs) field)
 */
unsigned int sysctl_sched_latency = 6000000ULL;
unsigned int normalized_sysctl_sched_latency = 6000000ULL;

unsigned int sysctl_sched_sync_hint_enable = 1;
unsigned int sysctl_sched_cstate_aware = 1;

#ifdef CONFIG_SCHED_WALT
unsigned int sysctl_sched_use_walt_cpu_util = 1;
unsigned int sysctl_sched_use_walt_task_util = 1;
__read_mostly unsigned int sysctl_sched_walt_cpu_high_irqload =
    (10 * NSEC_PER_MSEC);
#endif
/*
 * The initial- and re-scaling of tunables is configurable
 * (default SCHED_TUNABLESCALING_LOG = *(1+ilog(ncpus))
 *
 * Options are:
 * SCHED_TUNABLESCALING_NONE - unscaled, always *1
 * SCHED_TUNABLESCALING_LOG - scaled logarithmical, *1+ilog(ncpus)
 * SCHED_TUNABLESCALING_LINEAR - scaled linear, *ncpus
 */
enum sched_tunable_scaling sysctl_sched_tunable_scaling
	= SCHED_TUNABLESCALING_LOG;

/*
 * Minimal preemption granularity for CPU-bound tasks:
 * (default: 0.75 msec * (1 + ilog(ncpus)), units: nanoseconds)
 */
unsigned int sysctl_sched_min_granularity = 750000ULL;
unsigned int normalized_sysctl_sched_min_granularity = 750000ULL;

/*
 * is kept at sysctl_sched_latency / sysctl_sched_min_granularity
 */
static unsigned int sched_nr_latency = 8;

/*
 * After fork, child runs first. If set to 0 (default) then
 * parent will (try to) run first.
 */
unsigned int sysctl_sched_child_runs_first __read_mostly;

/*
 * SCHED_OTHER wake-up granularity.
 * (default: 1 msec * (1 + ilog(ncpus)), units: nanoseconds)
 *
 * This option delays the preemption effects of decoupled workloads
 * and reduces their over-scheduling. Synchronous workloads will still
 * have immediate wakeup/sleep latencies.
 */
unsigned int sysctl_sched_wakeup_granularity = 1000000UL;
unsigned int normalized_sysctl_sched_wakeup_granularity = 1000000UL;

const_debug unsigned int sysctl_sched_migration_cost = 500000UL;

/*
 * The exponential sliding  window over which load is averaged for shares
 * distribution.
 * (default: 10msec)
 */
unsigned int __read_mostly sysctl_sched_shares_window = 10000000UL;

#ifdef CONFIG_CFS_BANDWIDTH
/*
 * Amount of runtime to allocate from global (tg) to local (per-cfs_rq) pool
 * each time a cfs_rq requests quota.
 *
 * Note: in the case that the slice exceeds the runtime remaining (either due
 * to consumption or the quota being specified to be smaller than the slice)
 * we will always only issue the remaining available time.
 *
 * default: 5 msec, units: microseconds
  */
unsigned int sysctl_sched_cfs_bandwidth_slice = 5000UL;
#endif

/*
 * The margin used when comparing utilization with CPU capacity:
 * util * margin < capacity * 1024
 */
unsigned int capacity_margin = 1280; /* ~20% */

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
 * Increase the granularity value when there are more CPUs,
 * because with more CPUs the 'effective latency' as visible
 * to users decreases. But the relationship is not linear,
 * so pick a second-best guess by going with the log2 of the
 * number of CPUs.
 *
 * This idea comes from the SD scheduler of Con Kolivas:
 */
static unsigned int get_update_sysctl_factor(void)
{
	unsigned int cpus = min_t(unsigned int, num_online_cpus(), 8);
	unsigned int factor;

	switch (sysctl_sched_tunable_scaling) {
	case SCHED_TUNABLESCALING_NONE:
		factor = 1;
		break;
	case SCHED_TUNABLESCALING_LINEAR:
		factor = cpus;
		break;
	case SCHED_TUNABLESCALING_LOG:
	default:
		factor = 1 + ilog2(cpus);
		break;
	}

	return factor;
}

static void update_sysctl(void)
{
	unsigned int factor = get_update_sysctl_factor();

#define SET_SYSCTL(name) \
	(sysctl_##name = (factor) * normalized_sysctl_##name)
	SET_SYSCTL(sched_min_granularity);
	SET_SYSCTL(sched_latency);
	SET_SYSCTL(sched_wakeup_granularity);
#undef SET_SYSCTL
}

void sched_init_granularity(void)
{
	update_sysctl();
}

#define WMULT_CONST	(~0U)
#define WMULT_SHIFT	32

static void __update_inv_weight(struct load_weight *lw)
{
	unsigned long w;

	if (likely(lw->inv_weight))
		return;

	w = scale_load_down(lw->weight);

	if (BITS_PER_LONG > 32 && unlikely(w >= WMULT_CONST))
		lw->inv_weight = 1;
	else if (unlikely(!w))
		lw->inv_weight = WMULT_CONST;
	else
		lw->inv_weight = WMULT_CONST / w;
}

/*
 * delta_exec * weight / lw.weight
 *   OR
 * (delta_exec * (weight * lw->inv_weight)) >> WMULT_SHIFT
 *
 * Either weight := NICE_0_LOAD and lw \e prio_to_wmult[], in which case
 * we're guaranteed shift stays positive because inv_weight is guaranteed to
 * fit 32 bits, and NICE_0_LOAD gives another 10 bits; therefore shift >= 22.
 *
 * Or, weight =< lw.weight (because lw.weight is the runqueue weight), thus
 * weight/lw.weight <= 1, and therefore our shift will also be positive.
 */
static u64 __calc_delta(u64 delta_exec, unsigned long weight, struct load_weight *lw)
{
	u64 fact = scale_load_down(weight);
	int shift = WMULT_SHIFT;

	__update_inv_weight(lw);

	if (unlikely(fact >> 32)) {
		while (fact >> 32) {
			fact >>= 1;
			shift--;
		}
	}

	/* hint to use a 32x32->64 mul */
	fact = (u64)(u32)fact * lw->inv_weight;

	while (fact >> 32) {
		fact >>= 1;
		shift--;
	}

	return mul_u64_u32_shr(delta_exec, fact, shift);
}


const struct sched_class fair_sched_class;

/**************************************************************
 * CFS operations on generic schedulable entities:
 */

#ifdef CONFIG_FAIR_GROUP_SCHED

/* cpu runqueue to which this cfs_rq is attached */
static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return cfs_rq->rq;
}

/* An entity is a task if it doesn't "own" a runqueue */
#define entity_is_task(se)	(!se->my_q)

static inline struct task_struct *task_of(struct sched_entity *se)
{
#ifdef CONFIG_SCHED_DEBUG
	WARN_ON_ONCE(!entity_is_task(se));
#endif
	return container_of(se, struct task_struct, se);
}

/* Walk up scheduling entities hierarchy */
#define for_each_sched_entity(se) \
		for (; se; se = se->parent)

static inline struct cfs_rq *task_cfs_rq(struct task_struct *p)
{
	return p->se.cfs_rq;
}

/* runqueue on which this entity is (to be) queued */
static inline struct cfs_rq *cfs_rq_of(struct sched_entity *se)
{
	return se->cfs_rq;
}

/* runqueue "owned" by this group */
static inline struct cfs_rq *group_cfs_rq(struct sched_entity *grp)
{
	return grp->my_q;
}

static inline void list_add_leaf_cfs_rq(struct cfs_rq *cfs_rq)
{
	if (!cfs_rq->on_list) {
		struct rq *rq = rq_of(cfs_rq);
		int cpu = cpu_of(rq);
		/*
		 * Ensure we either appear before our parent (if already
		 * enqueued) or force our parent to appear after us when it is
		 * enqueued. The fact that we always enqueue bottom-up
		 * reduces this to two cases and a special case for the root
		 * cfs_rq. Furthermore, it also means that we will always reset
		 * tmp_alone_branch either when the branch is connected
		 * to a tree or when we reach the beg of the tree
		 */
		if (cfs_rq->tg->parent &&
		    cfs_rq->tg->parent->cfs_rq[cpu]->on_list) {
			/*
			 * If parent is already on the list, we add the child
			 * just before. Thanks to circular linked property of
			 * the list, this means to put the child at the tail
			 * of the list that starts by parent.
			 */
			list_add_tail_rcu(&cfs_rq->leaf_cfs_rq_list,
				&(cfs_rq->tg->parent->cfs_rq[cpu]->leaf_cfs_rq_list));
			/*
			 * The branch is now connected to its tree so we can
			 * reset tmp_alone_branch to the beginning of the
			 * list.
			 */
			rq->tmp_alone_branch = &rq->leaf_cfs_rq_list;
		} else if (!cfs_rq->tg->parent) {
			/*
			 * cfs rq without parent should be put
			 * at the tail of the list.
			 */
			list_add_tail_rcu(&cfs_rq->leaf_cfs_rq_list,
				&rq->leaf_cfs_rq_list);
			/*
			 * We have reach the beg of a tree so we can reset
			 * tmp_alone_branch to the beginning of the list.
			 */
			rq->tmp_alone_branch = &rq->leaf_cfs_rq_list;
		} else {
			/*
			 * The parent has not already been added so we want to
			 * make sure that it will be put after us.
			 * tmp_alone_branch points to the beg of the branch
			 * where we will add parent.
			 */
			list_add_rcu(&cfs_rq->leaf_cfs_rq_list,
				rq->tmp_alone_branch);
			/*
			 * update tmp_alone_branch to points to the new beg
			 * of the branch
			 */
			rq->tmp_alone_branch = &cfs_rq->leaf_cfs_rq_list;
		}

		cfs_rq->on_list = 1;
	}
}

static inline void list_del_leaf_cfs_rq(struct cfs_rq *cfs_rq)
{
	if (cfs_rq->on_list) {
		list_del_rcu(&cfs_rq->leaf_cfs_rq_list);
		cfs_rq->on_list = 0;
	}
}

/* Iterate thr' all leaf cfs_rq's on a runqueue */
#define for_each_leaf_cfs_rq(rq, cfs_rq) \
	list_for_each_entry_rcu(cfs_rq, &rq->leaf_cfs_rq_list, leaf_cfs_rq_list)

/* Do the two (enqueued) entities belong to the same group ? */
static inline struct cfs_rq *
is_same_group(struct sched_entity *se, struct sched_entity *pse)
{
	if (se->cfs_rq == pse->cfs_rq)
		return se->cfs_rq;

	return NULL;
}

static inline struct sched_entity *parent_entity(struct sched_entity *se)
{
	return se->parent;
}

static void
find_matching_se(struct sched_entity **se, struct sched_entity **pse)
{
	int se_depth, pse_depth;

	/*
	 * preemption test can be made between sibling entities who are in the
	 * same cfs_rq i.e who have a common parent. Walk up the hierarchy of
	 * both tasks until we find their ancestors who are siblings of common
	 * parent.
	 */

	/* First walk up until both entities are at same depth */
	se_depth = (*se)->depth;
	pse_depth = (*pse)->depth;

	while (se_depth > pse_depth) {
		se_depth--;
		*se = parent_entity(*se);
	}

	while (pse_depth > se_depth) {
		pse_depth--;
		*pse = parent_entity(*pse);
	}

	while (!is_same_group(*se, *pse)) {
		*se = parent_entity(*se);
		*pse = parent_entity(*pse);
	}
}

#else	/* !CONFIG_FAIR_GROUP_SCHED */

static inline struct task_struct *task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
}

static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return container_of(cfs_rq, struct rq, cfs);
}

#define entity_is_task(se)	1

#define for_each_sched_entity(se) \
		for (; se; se = NULL)

static inline struct cfs_rq *task_cfs_rq(struct task_struct *p)
{
	return &task_rq(p)->cfs;
}

static inline struct cfs_rq *cfs_rq_of(struct sched_entity *se)
{
	struct task_struct *p = task_of(se);
	struct rq *rq = task_rq(p);

	return &rq->cfs;
}

/* runqueue "owned" by this group */
static inline struct cfs_rq *group_cfs_rq(struct sched_entity *grp)
{
	return NULL;
}

static inline void list_add_leaf_cfs_rq(struct cfs_rq *cfs_rq)
{
}

static inline void list_del_leaf_cfs_rq(struct cfs_rq *cfs_rq)
{
}

#define for_each_leaf_cfs_rq(rq, cfs_rq) \
		for (cfs_rq = &rq->cfs; cfs_rq; cfs_rq = NULL)

static inline struct sched_entity *parent_entity(struct sched_entity *se)
{
	return NULL;
}

static inline void
find_matching_se(struct sched_entity **se, struct sched_entity **pse)
{
}

#endif	/* CONFIG_FAIR_GROUP_SCHED */

static __always_inline
void account_cfs_rq_runtime(struct cfs_rq *cfs_rq, u64 delta_exec);

/**************************************************************
 * Scheduling class tree data structure manipulation methods:
 */

static inline u64 max_vruntime(u64 max_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - max_vruntime);
	if (delta > 0)
		max_vruntime = vruntime;

	return max_vruntime;
}

static inline u64 min_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta < 0)
		min_vruntime = vruntime;

	return min_vruntime;
}

static inline int entity_before(struct sched_entity *a,
				struct sched_entity *b)
{
	return (s64)(a->vruntime - b->vruntime) < 0;
}

static void update_min_vruntime(struct cfs_rq *cfs_rq)
{
	u64 vruntime = cfs_rq->min_vruntime;

	if (cfs_rq->curr)
		vruntime = cfs_rq->curr->vruntime;

	if (cfs_rq->rb_leftmost) {
		struct sched_entity *se = rb_entry(cfs_rq->rb_leftmost,
						   struct sched_entity,
						   run_node);

		if (!cfs_rq->curr)
			vruntime = se->vruntime;
		else
			vruntime = min_vruntime(vruntime, se->vruntime);
	}

	/* ensure we never gain time by being placed backwards. */
	cfs_rq->min_vruntime = max_vruntime(cfs_rq->min_vruntime, vruntime);
#ifndef CONFIG_64BIT
	smp_wmb();
	cfs_rq->min_vruntime_copy = cfs_rq->min_vruntime;
#endif
}

/*
 * Enqueue an entity into the rb-tree:
 */
static void __enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	struct rb_node **link = &cfs_rq->tasks_timeline.rb_node;
	struct rb_node *parent = NULL;
	struct sched_entity *entry;
	int leftmost = 1;

	/*
	 * Find the right place in the rbtree:
	 */
	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct sched_entity, run_node);
		/*
		 * We dont care about collisions. Nodes with
		 * the same key stay together.
		 */
		if (entity_before(se, entry)) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
			leftmost = 0;
		}
	}

	/*
	 * Maintain a cache of leftmost tree entries (it is frequently
	 * used):
	 */
	if (leftmost)
		cfs_rq->rb_leftmost = &se->run_node;

	rb_link_node(&se->run_node, parent, link);
	rb_insert_color(&se->run_node, &cfs_rq->tasks_timeline);
}

static void __dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (cfs_rq->rb_leftmost == &se->run_node) {
		struct rb_node *next_node;

		next_node = rb_next(&se->run_node);
		cfs_rq->rb_leftmost = next_node;
	}

	rb_erase(&se->run_node, &cfs_rq->tasks_timeline);
}

struct sched_entity *__pick_first_entity(struct cfs_rq *cfs_rq)
{
	struct rb_node *left = cfs_rq->rb_leftmost;

	if (!left)
		return NULL;

	return rb_entry(left, struct sched_entity, run_node);
}

static struct sched_entity *__pick_next_entity(struct sched_entity *se)
{
	struct rb_node *next = rb_next(&se->run_node);

	if (!next)
		return NULL;

	return rb_entry(next, struct sched_entity, run_node);
}

#ifdef CONFIG_SCHED_DEBUG
struct sched_entity *__pick_last_entity(struct cfs_rq *cfs_rq)
{
	struct rb_node *last = rb_last(&cfs_rq->tasks_timeline);

	if (!last)
		return NULL;

	return rb_entry(last, struct sched_entity, run_node);
}

/**************************************************************
 * Scheduling class statistics methods:
 */

int sched_proc_update_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	unsigned int factor = get_update_sysctl_factor();

	if (ret || !write)
		return ret;

	sched_nr_latency = DIV_ROUND_UP(sysctl_sched_latency,
					sysctl_sched_min_granularity);

#define WRT_SYSCTL(name) \
	(normalized_sysctl_##name = sysctl_##name / (factor))
	WRT_SYSCTL(sched_min_granularity);
	WRT_SYSCTL(sched_latency);
	WRT_SYSCTL(sched_wakeup_granularity);
#undef WRT_SYSCTL

	return 0;
}
#endif

/*
 * delta /= w
 */
static inline u64 calc_delta_fair(u64 delta, struct sched_entity *se)
{
	if (unlikely(se->load.weight != NICE_0_LOAD))
		delta = __calc_delta(delta, NICE_0_LOAD, &se->load);

	return delta;
}

/*
 * The idea is to set a period in which each task runs once.
 *
 * When there are too many tasks (sched_nr_latency) we have to stretch
 * this period because otherwise the slices get too small.
 *
 * p = (nr <= nl) ? l : l*nr/nl
 */
static u64 __sched_period(unsigned long nr_running)
{
	if (unlikely(nr_running > sched_nr_latency))
		return nr_running * sysctl_sched_min_granularity;
	else
		return sysctl_sched_latency;
}

/*
 * We calculate the wall-time slice from the period by taking a part
 * proportional to the weight.
 *
 * s = p*P[w/rw]
 */
static u64 sched_slice(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	u64 slice = __sched_period(cfs_rq->nr_running + !se->on_rq);

	for_each_sched_entity(se) {
		struct load_weight *load;
		struct load_weight lw;

		cfs_rq = cfs_rq_of(se);
		load = &cfs_rq->load;

		if (unlikely(!se->on_rq)) {
			lw = cfs_rq->load;

			update_load_add(&lw, se->load.weight);
			load = &lw;
		}
		slice = __calc_delta(slice, se->load.weight, load);
	}
	return slice;
}

/*
 * We calculate the vruntime slice of a to-be-inserted task.
 *
 * vs = s/w
 */
static u64 sched_vslice(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	return calc_delta_fair(sched_slice(cfs_rq, se), se);
}

#ifdef CONFIG_SMP
static int select_idle_sibling(struct task_struct *p, int prev_cpu, int cpu);
static unsigned long task_h_load(struct task_struct *p);

/*
 * We choose a half-life close to 1 scheduling period.
 * Note: The tables runnable_avg_yN_inv and runnable_avg_yN_sum are
 * dependent on this value.
 */
#define LOAD_AVG_PERIOD 32
#define LOAD_AVG_MAX 47742 /* maximum possible load avg */
#define LOAD_AVG_MAX_N 345 /* number of full periods to produce LOAD_AVG_MAX */

/* Give new sched_entity start runnable values to heavy its load in infant time */
void init_entity_runnable_average(struct sched_entity *se)
{
	struct sched_avg *sa = &se->avg;

	sa->last_update_time = 0;
	/*
	 * sched_avg's period_contrib should be strictly less then 1024, so
	 * we give it 1023 to make sure it is almost a period (1024us), and
	 * will definitely be update (after enqueue).
	 */
	sa->period_contrib = 1023;
	/*
	 * Tasks are intialized with full load to be seen as heavy tasks until
	 * they get a chance to stabilize to their real load level.
	 * Group entities are intialized with zero load to reflect the fact that
	 * nothing has been attached to the task group yet.
	 */
	if (entity_is_task(se))
		sa->load_avg = scale_load_down(se->load.weight);
	sa->load_sum = sa->load_avg * LOAD_AVG_MAX;
	/*
	 * In previous Android versions, we used to have:
	 * 	sa->util_avg = scale_load_down(SCHED_LOAD_SCALE);
	 * 	sa->util_sum = sa->util_avg * LOAD_AVG_MAX;
	 * However, that functionality has been moved to enqueue.
	 * It is unclear if we should restore this in enqueue.
	 */
	/*
	 * At this point, util_avg won't be used in select_task_rq_fair anyway
	 */
	sa->util_avg = 0;
	sa->util_sum = 0;
	/* when this task enqueue'ed, it will contribute to its cfs_rq's load_avg */
}

static inline u64 cfs_rq_clock_task(struct cfs_rq *cfs_rq);
static int update_cfs_rq_load_avg(u64 now, struct cfs_rq *cfs_rq, bool update_freq);
static void attach_entity_cfs_rq(struct sched_entity *se);
static void attach_entity_load_avg(struct cfs_rq *cfs_rq, struct sched_entity *se);

/*
 * With new tasks being created, their initial util_avgs are extrapolated
 * based on the cfs_rq's current util_avg:
 *
 *   util_avg = cfs_rq->util_avg / (cfs_rq->load_avg + 1) * se.load.weight
 *
 * However, in many cases, the above util_avg does not give a desired
 * value. Moreover, the sum of the util_avgs may be divergent, such
 * as when the series is a harmonic series.
 *
 * To solve this problem, we also cap the util_avg of successive tasks to
 * only 1/2 of the left utilization budget:
 *
 *   util_avg_cap = (1024 - cfs_rq->avg.util_avg) / 2^n
 *
 * where n denotes the nth task.
 *
 * For example, a simplest series from the beginning would be like:
 *
 *  task  util_avg: 512, 256, 128,  64,  32,   16,    8, ...
 * cfs_rq util_avg: 512, 768, 896, 960, 992, 1008, 1016, ...
 *
 * Finally, that extrapolated util_avg is clamped to the cap (util_avg_cap)
 * if util_avg > util_avg_cap.
 */
void post_init_entity_util_avg(struct sched_entity *se)
{
	struct cfs_rq *cfs_rq = cfs_rq_of(se);
	struct sched_avg *sa = &se->avg;
	long cap = (long)(SCHED_CAPACITY_SCALE - cfs_rq->avg.util_avg) / 2;

	if (cap > 0) {
		if (cfs_rq->avg.util_avg != 0) {
			sa->util_avg  = cfs_rq->avg.util_avg * se->load.weight;
			sa->util_avg /= (cfs_rq->avg.load_avg + 1);

			if (sa->util_avg > cap)
				sa->util_avg = cap;
		} else {
			sa->util_avg = cap;
		}
		/*
		 * If we wish to restore tuning via setting initial util,
		 * this is where we should do it.
		 */
		sa->util_sum = sa->util_avg * LOAD_AVG_MAX;
	}

	if (entity_is_task(se)) {
		struct task_struct *p = task_of(se);
		if (p->sched_class != &fair_sched_class) {
			/*
			 * For !fair tasks do:
			 *
			update_cfs_rq_load_avg(now, cfs_rq, false);
			attach_entity_load_avg(cfs_rq, se);
			switched_from_fair(rq, p);
			 *
			 * such that the next switched_to_fair() has the
			 * expected state.
			 */
			se->avg.last_update_time = cfs_rq_clock_task(cfs_rq);
			return;
		}
	}

	attach_entity_cfs_rq(se);
}

#else /* !CONFIG_SMP */
void init_entity_runnable_average(struct sched_entity *se)
{
}
void post_init_entity_util_avg(struct sched_entity *se)
{
}
static void update_tg_load_avg(struct cfs_rq *cfs_rq, int force)
{
}
#endif /* CONFIG_SMP */

/*
 * Update the current task's runtime statistics.
 */
static void update_curr(struct cfs_rq *cfs_rq)
{
	struct sched_entity *curr = cfs_rq->curr;
	u64 now = rq_clock_task(rq_of(cfs_rq));
	u64 delta_exec;

	if (unlikely(!curr))
		return;

	delta_exec = now - curr->exec_start;
	if (unlikely((s64)delta_exec <= 0))
		return;

	curr->exec_start = now;

	schedstat_set(curr->statistics.exec_max,
		      max(delta_exec, curr->statistics.exec_max));

	curr->sum_exec_runtime += delta_exec;
	schedstat_add(cfs_rq, exec_clock, delta_exec);

	curr->vruntime += calc_delta_fair(delta_exec, curr);
	update_min_vruntime(cfs_rq);

	if (entity_is_task(curr)) {
		struct task_struct *curtask = task_of(curr);

		trace_sched_stat_runtime(curtask, delta_exec, curr->vruntime);
		cpuacct_charge(curtask, delta_exec);
		account_group_exec_runtime(curtask, delta_exec);
	}

	account_cfs_rq_runtime(cfs_rq, delta_exec);
}

static void update_curr_fair(struct rq *rq)
{
	update_curr(cfs_rq_of(&rq->curr->se));
}

static inline void
update_stats_wait_start(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	schedstat_set(se->statistics.wait_start, rq_clock(rq_of(cfs_rq)));
}

/*
 * Task is being enqueued - update stats:
 */
static void update_stats_enqueue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Are we enqueueing a waiting task? (for current tasks
	 * a dequeue/enqueue event is a NOP)
	 */
	if (se != cfs_rq->curr)
		update_stats_wait_start(cfs_rq, se);
}

static void
update_stats_wait_end(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	schedstat_set(se->statistics.wait_max, max(se->statistics.wait_max,
			rq_clock(rq_of(cfs_rq)) - se->statistics.wait_start));
	schedstat_set(se->statistics.wait_count, se->statistics.wait_count + 1);
	schedstat_set(se->statistics.wait_sum, se->statistics.wait_sum +
			rq_clock(rq_of(cfs_rq)) - se->statistics.wait_start);
#ifdef CONFIG_SCHEDSTATS
	if (entity_is_task(se)) {
		trace_sched_stat_wait(task_of(se),
			rq_clock(rq_of(cfs_rq)) - se->statistics.wait_start);
	}
#endif
	schedstat_set(se->statistics.wait_start, 0);
}

static inline void
update_stats_dequeue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Mark the end of the wait period if dequeueing a
	 * waiting task:
	 */
	if (se != cfs_rq->curr)
		update_stats_wait_end(cfs_rq, se);
}

/*
 * We are picking a new current task - update its stats:
 */
static inline void
update_stats_curr_start(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * We are starting a new run period:
	 */
	se->exec_start = rq_clock_task(rq_of(cfs_rq));
}

/**************************************************
 * Scheduling class queueing methods:
 */

#ifdef CONFIG_NUMA_BALANCING
/*
 * Approximate time to scan a full NUMA task in ms. The task scan period is
 * calculated based on the tasks virtual memory size and
 * numa_balancing_scan_size.
 */
unsigned int sysctl_numa_balancing_scan_period_min = 1000;
unsigned int sysctl_numa_balancing_scan_period_max = 60000;

/* Portion of address space to scan in MB */
unsigned int sysctl_numa_balancing_scan_size = 256;

/* Scan @scan_size MB every @scan_period after an initial @scan_delay in ms */
unsigned int sysctl_numa_balancing_scan_delay = 1000;

static unsigned int task_nr_scan_windows(struct task_struct *p)
{
	unsigned long rss = 0;
	unsigned long nr_scan_pages;

	/*
	 * Calculations based on RSS as non-present and empty pages are skipped
	 * by the PTE scanner and NUMA hinting faults should be trapped based
	 * on resident pages
	 */
	nr_scan_pages = sysctl_numa_balancing_scan_size << (20 - PAGE_SHIFT);
	rss = get_mm_rss(p->mm);
	if (!rss)
		rss = nr_scan_pages;

	rss = round_up(rss, nr_scan_pages);
	return rss / nr_scan_pages;
}

/* For sanitys sake, never scan more PTEs than MAX_SCAN_WINDOW MB/sec. */
#define MAX_SCAN_WINDOW 2560

static unsigned int task_scan_min(struct task_struct *p)
{
	unsigned int scan_size = READ_ONCE(sysctl_numa_balancing_scan_size);
	unsigned int scan, floor;
	unsigned int windows = 1;

	if (scan_size < MAX_SCAN_WINDOW)
		windows = MAX_SCAN_WINDOW / scan_size;
	floor = 1000 / windows;

	scan = sysctl_numa_balancing_scan_period_min / task_nr_scan_windows(p);
	return max_t(unsigned int, floor, scan);
}

static unsigned int task_scan_max(struct task_struct *p)
{
	unsigned int smin = task_scan_min(p);
	unsigned int smax;

	/* Watch for min being lower than max due to floor calculations */
	smax = sysctl_numa_balancing_scan_period_max / task_nr_scan_windows(p);
	return max(smin, smax);
}

static void account_numa_enqueue(struct rq *rq, struct task_struct *p)
{
	rq->nr_numa_running += (p->numa_preferred_nid != -1);
	rq->nr_preferred_running += (p->numa_preferred_nid == task_node(p));
}

static void account_numa_dequeue(struct rq *rq, struct task_struct *p)
{
	rq->nr_numa_running -= (p->numa_preferred_nid != -1);
	rq->nr_preferred_running -= (p->numa_preferred_nid == task_node(p));
}

struct numa_group {
	atomic_t refcount;

	spinlock_t lock; /* nr_tasks, tasks */
	int nr_tasks;
	pid_t gid;

	struct rcu_head rcu;
	nodemask_t active_nodes;
	unsigned long total_faults;
	/*
	 * Faults_cpu is used to decide whether memory should move
	 * towards the CPU. As a consequence, these stats are weighted
	 * more by CPU use than by memory faults.
	 */
	unsigned long *faults_cpu;
	unsigned long faults[0];
};

/* Shared or private faults. */
#define NR_NUMA_HINT_FAULT_TYPES 2

/* Memory and CPU locality */
#define NR_NUMA_HINT_FAULT_STATS (NR_NUMA_HINT_FAULT_TYPES * 2)

/* Averaged statistics, and temporary buffers. */
#define NR_NUMA_HINT_FAULT_BUCKETS (NR_NUMA_HINT_FAULT_STATS * 2)

pid_t task_numa_group_id(struct task_struct *p)
{
	return p->numa_group ? p->numa_group->gid : 0;
}

/*
 * The averaged statistics, shared & private, memory & cpu,
 * occupy the first half of the array. The second half of the
 * array is for current counters, which are averaged into the
 * first set by task_numa_placement.
 */
static inline int task_faults_idx(enum numa_faults_stats s, int nid, int priv)
{
	return NR_NUMA_HINT_FAULT_TYPES * (s * nr_node_ids + nid) + priv;
}

static inline unsigned long task_faults(struct task_struct *p, int nid)
{
	if (!p->numa_faults)
		return 0;

	return p->numa_faults[task_faults_idx(NUMA_MEM, nid, 0)] +
		p->numa_faults[task_faults_idx(NUMA_MEM, nid, 1)];
}

static inline unsigned long group_faults(struct task_struct *p, int nid)
{
	if (!p->numa_group)
		return 0;

	return p->numa_group->faults[task_faults_idx(NUMA_MEM, nid, 0)] +
		p->numa_group->faults[task_faults_idx(NUMA_MEM, nid, 1)];
}

static inline unsigned long group_faults_cpu(struct numa_group *group, int nid)
{
	return group->faults_cpu[task_faults_idx(NUMA_MEM, nid, 0)] +
		group->faults_cpu[task_faults_idx(NUMA_MEM, nid, 1)];
}

/* Handle placement on systems where not all nodes are directly connected. */
static unsigned long score_nearby_nodes(struct task_struct *p, int nid,
					int maxdist, bool task)
{
	unsigned long score = 0;
	int node;

	/*
	 * All nodes are directly connected, and the same distance
	 * from each other. No need for fancy placement algorithms.
	 */
	if (sched_numa_topology_type == NUMA_DIRECT)
		return 0;

	/*
	 * This code is called for each node, introducing N^2 complexity,
	 * which should be ok given the number of nodes rarely exceeds 8.
	 */
	for_each_online_node(node) {
		unsigned long faults;
		int dist = node_distance(nid, node);

		/*
		 * The furthest away nodes in the system are not interesting
		 * for placement; nid was already counted.
		 */
		if (dist == sched_max_numa_distance || node == nid)
			continue;

		/*
		 * On systems with a backplane NUMA topology, compare groups
		 * of nodes, and move tasks towards the group with the most
		 * memory accesses. When comparing two nodes at distance
		 * "hoplimit", only nodes closer by than "hoplimit" are part
		 * of each group. Skip other nodes.
		 */
		if (sched_numa_topology_type == NUMA_BACKPLANE &&
					dist > maxdist)
			continue;

		/* Add up the faults from nearby nodes. */
		if (task)
			faults = task_faults(p, node);
		else
			faults = group_faults(p, node);

		/*
		 * On systems with a glueless mesh NUMA topology, there are
		 * no fixed "groups of nodes". Instead, nodes that are not
		 * directly connected bounce traffic through intermediate
		 * nodes; a numa_group can occupy any set of nodes.
		 * The further away a node is, the less the faults count.
		 * This seems to result in good task placement.
		 */
		if (sched_numa_topology_type == NUMA_GLUELESS_MESH) {
			faults *= (sched_max_numa_distance - dist);
			faults /= (sched_max_numa_distance - LOCAL_DISTANCE);
		}

		score += faults;
	}

	return score;
}

/*
 * These return the fraction of accesses done by a particular task, or
 * task group, on a particular numa node.  The group weight is given a
 * larger multiplier, in order to group tasks together that are almost
 * evenly spread out between numa nodes.
 */
static inline unsigned long task_weight(struct task_struct *p, int nid,
					int dist)
{
	unsigned long faults, total_faults;

	if (!p->numa_faults)
		return 0;

	total_faults = p->total_numa_faults;

	if (!total_faults)
		return 0;

	faults = task_faults(p, nid);
	faults += score_nearby_nodes(p, nid, dist, true);

	return 1000 * faults / total_faults;
}

static inline unsigned long group_weight(struct task_struct *p, int nid,
					 int dist)
{
	unsigned long faults, total_faults;

	if (!p->numa_group)
		return 0;

	total_faults = p->numa_group->total_faults;

	if (!total_faults)
		return 0;

	faults = group_faults(p, nid);
	faults += score_nearby_nodes(p, nid, dist, false);

	return 1000 * faults / total_faults;
}

bool should_numa_migrate_memory(struct task_struct *p, struct page * page,
				int src_nid, int dst_cpu)
{
	struct numa_group *ng = p->numa_group;
	int dst_nid = cpu_to_node(dst_cpu);
	int last_cpupid, this_cpupid;

	this_cpupid = cpu_pid_to_cpupid(dst_cpu, current->pid);

	/*
	 * Multi-stage node selection is used in conjunction with a periodic
	 * migration fault to build a temporal task<->page relation. By using
	 * a two-stage filter we remove short/unlikely relations.
	 *
	 * Using P(p) ~ n_p / n_t as per frequentist probability, we can equate
	 * a task's usage of a particular page (n_p) per total usage of this
	 * page (n_t) (in a given time-span) to a probability.
	 *
	 * Our periodic faults will sample this probability and getting the
	 * same result twice in a row, given these samples are fully
	 * independent, is then given by P(n)^2, provided our sample period
	 * is sufficiently short compared to the usage pattern.
	 *
	 * This quadric squishes small probabilities, making it less likely we
	 * act on an unlikely task<->page relation.
	 */
	last_cpupid = page_cpupid_xchg_last(page, this_cpupid);
	if (!cpupid_pid_unset(last_cpupid) &&
				cpupid_to_nid(last_cpupid) != dst_nid)
		return false;

	/* Always allow migrate on private faults */
	if (cpupid_match_pid(p, last_cpupid))
		return true;

	/* A shared fault, but p->numa_group has not been set up yet. */
	if (!ng)
		return true;

	/*
	 * Do not migrate if the destination is not a node that
	 * is actively used by this numa group.
	 */
	if (!node_isset(dst_nid, ng->active_nodes))
		return false;

	/*
	 * Source is a node that is not actively used by this
	 * numa group, while the destination is. Migrate.
	 */
	if (!node_isset(src_nid, ng->active_nodes))
		return true;

	/*
	 * Both source and destination are nodes in active
	 * use by this numa group. Maximize memory bandwidth
	 * by migrating from more heavily used groups, to less
	 * heavily used ones, spreading the load around.
	 * Use a 1/4 hysteresis to avoid spurious page movement.
	 */
	return group_faults(p, dst_nid) < (group_faults(p, src_nid) * 3 / 4);
}

static unsigned long weighted_cpuload(const int cpu);
static unsigned long source_load(int cpu, int type);
static unsigned long target_load(int cpu, int type);
static unsigned long capacity_of(int cpu);
static long effective_load(struct task_group *tg, int cpu, long wl, long wg);

/* Cached statistics for all CPUs within a node */
struct numa_stats {
	unsigned long nr_running;
	unsigned long load;

	/* Total compute capacity of CPUs on a node */
	unsigned long compute_capacity;

	/* Approximate capacity in terms of runnable tasks on a node */
	unsigned long task_capacity;
	int has_free_capacity;
};

/*
 * XXX borrowed from update_sg_lb_stats
 */
static void update_numa_stats(struct numa_stats *ns, int nid)
{
	int smt, cpu, cpus = 0;
	unsigned long capacity;

	memset(ns, 0, sizeof(*ns));
	for_each_cpu(cpu, cpumask_of_node(nid)) {
		struct rq *rq = cpu_rq(cpu);

		ns->nr_running += rq->nr_running;
		ns->load += weighted_cpuload(cpu);
		ns->compute_capacity += capacity_of(cpu);

		cpus++;
	}

	/*
	 * If we raced with hotplug and there are no CPUs left in our mask
	 * the @ns structure is NULL'ed and task_numa_compare() will
	 * not find this node attractive.
	 *
	 * We'll either bail at !has_free_capacity, or we'll detect a huge
	 * imbalance and bail there.
	 */
	if (!cpus)
		return;

	/* smt := ceil(cpus / capacity), assumes: 1 < smt_power < 2 */
	smt = DIV_ROUND_UP(SCHED_CAPACITY_SCALE * cpus, ns->compute_capacity);
	capacity = cpus / smt; /* cores */

	ns->task_capacity = min_t(unsigned, capacity,
		DIV_ROUND_CLOSEST(ns->compute_capacity, SCHED_CAPACITY_SCALE));
	ns->has_free_capacity = (ns->nr_running < ns->task_capacity);
}

struct task_numa_env {
	struct task_struct *p;

	int src_cpu, src_nid;
	int dst_cpu, dst_nid;

	struct numa_stats src_stats, dst_stats;

	int imbalance_pct;
	int dist;

	struct task_struct *best_task;
	long best_imp;
	int best_cpu;
};

static void task_numa_assign(struct task_numa_env *env,
			     struct task_struct *p, long imp)
{
	if (env->best_task)
		put_task_struct(env->best_task);

	env->best_task = p;
	env->best_imp = imp;
	env->best_cpu = env->dst_cpu;
}

static bool load_too_imbalanced(long src_load, long dst_load,
				struct task_numa_env *env)
{
	long imb, old_imb;
	long orig_src_load, orig_dst_load;
	long src_capacity, dst_capacity;

	/*
	 * The load is corrected for the CPU capacity available on each node.
	 *
	 * src_load        dst_load
	 * ------------ vs ---------
	 * src_capacity    dst_capacity
	 */
	src_capacity = env->src_stats.compute_capacity;
	dst_capacity = env->dst_stats.compute_capacity;

	/* We care about the slope of the imbalance, not the direction. */
	if (dst_load < src_load)
		swap(dst_load, src_load);

	/* Is the difference below the threshold? */
	imb = dst_load * src_capacity * 100 -
	      src_load * dst_capacity * env->imbalance_pct;
	if (imb <= 0)
		return false;

	/*
	 * The imbalance is above the allowed threshold.
	 * Compare it with the old imbalance.
	 */
	orig_src_load = env->src_stats.load;
	orig_dst_load = env->dst_stats.load;

	if (orig_dst_load < orig_src_load)
		swap(orig_dst_load, orig_src_load);

	old_imb = orig_dst_load * src_capacity * 100 -
		  orig_src_load * dst_capacity * env->imbalance_pct;

	/* Would this change make things worse? */
	return (imb > old_imb);
}

/*
 * This checks if the overall compute and NUMA accesses of the system would
 * be improved if the source tasks was migrated to the target dst_cpu taking
 * into account that it might be best if task running on the dst_cpu should
 * be exchanged with the source task
 */
static void task_numa_compare(struct task_numa_env *env,
			      long taskimp, long groupimp)
{
	struct rq *src_rq = cpu_rq(env->src_cpu);
	struct rq *dst_rq = cpu_rq(env->dst_cpu);
	struct task_struct *cur;
	long src_load, dst_load;
	long load;
	long imp = env->p->numa_group ? groupimp : taskimp;
	long moveimp = imp;
	int dist = env->dist;
	bool assigned = false;

	rcu_read_lock();

	raw_spin_lock_irq(&dst_rq->lock);
	cur = dst_rq->curr;
	/*
	 * No need to move the exiting task or idle task.
	 */
	if ((cur->flags & PF_EXITING) || is_idle_task(cur))
		cur = NULL;
	else {
		/*
		 * The task_struct must be protected here to protect the
		 * p->numa_faults access in the task_weight since the
		 * numa_faults could already be freed in the following path:
		 * finish_task_switch()
		 *     --> put_task_struct()
		 *         --> __put_task_struct()
		 *             --> task_numa_free()
		 */
		get_task_struct(cur);
	}

	raw_spin_unlock_irq(&dst_rq->lock);

	/*
	 * Because we have preemption enabled we can get migrated around and
	 * end try selecting ourselves (current == env->p) as a swap candidate.
	 */
	if (cur == env->p)
		goto unlock;

	/*
	 * "imp" is the fault differential for the source task between the
	 * source and destination node. Calculate the total differential for
	 * the source task and potential destination task. The more negative
	 * the value is, the more rmeote accesses that would be expected to
	 * be incurred if the tasks were swapped.
	 */
	if (cur) {
		/* Skip this swap candidate if cannot move to the source cpu */
		if (!cpumask_test_cpu(env->src_cpu, tsk_cpus_allowed(cur)))
			goto unlock;

		/*
		 * If dst and source tasks are in the same NUMA group, or not
		 * in any group then look only at task weights.
		 */
		if (cur->numa_group == env->p->numa_group) {
			imp = taskimp + task_weight(cur, env->src_nid, dist) -
			      task_weight(cur, env->dst_nid, dist);
			/*
			 * Add some hysteresis to prevent swapping the
			 * tasks within a group over tiny differences.
			 */
			if (cur->numa_group)
				imp -= imp/16;
		} else {
			/*
			 * Compare the group weights. If a task is all by
			 * itself (not part of a group), use the task weight
			 * instead.
			 */
			if (cur->numa_group)
				imp += group_weight(cur, env->src_nid, dist) -
				       group_weight(cur, env->dst_nid, dist);
			else
				imp += task_weight(cur, env->src_nid, dist) -
				       task_weight(cur, env->dst_nid, dist);
		}
	}

	if (imp <= env->best_imp && moveimp <= env->best_imp)
		goto unlock;

	if (!cur) {
		/* Is there capacity at our destination? */
		if (env->src_stats.nr_running <= env->src_stats.task_capacity &&
		    !env->dst_stats.has_free_capacity)
			goto unlock;

		goto balance;
	}

	/* Balance doesn't matter much if we're running a task per cpu */
	if (imp > env->best_imp && src_rq->nr_running == 1 &&
			dst_rq->nr_running == 1)
		goto assign;

	/*
	 * In the overloaded case, try and keep the load balanced.
	 */
balance:
	load = task_h_load(env->p);
	dst_load = env->dst_stats.load + load;
	src_load = env->src_stats.load - load;

	if (moveimp > imp && moveimp > env->best_imp) {
		/*
		 * If the improvement from just moving env->p direction is
		 * better than swapping tasks around, check if a move is
		 * possible. Store a slightly smaller score than moveimp,
		 * so an actually idle CPU will win.
		 */
		if (!load_too_imbalanced(src_load, dst_load, env)) {
			imp = moveimp - 1;
			put_task_struct(cur);
			cur = NULL;
			goto assign;
		}
	}

	if (imp <= env->best_imp)
		goto unlock;

	if (cur) {
		load = task_h_load(cur);
		dst_load -= load;
		src_load += load;
	}

	if (load_too_imbalanced(src_load, dst_load, env))
		goto unlock;

	/*
	 * One idle CPU per node is evaluated for a task numa move.
	 * Call select_idle_sibling to maybe find a better one.
	 */
	if (!cur)
		env->dst_cpu = select_idle_sibling(env->p, env->src_cpu,
						   env->dst_cpu);

assign:
	assigned = true;
	task_numa_assign(env, cur, imp);
unlock:
	rcu_read_unlock();
	/*
	 * The dst_rq->curr isn't assigned. The protection for task_struct is
	 * finished.
	 */
	if (cur && !assigned)
		put_task_struct(cur);
}

static void task_numa_find_cpu(struct task_numa_env *env,
				long taskimp, long groupimp)
{
	int cpu;

	for_each_cpu(cpu, cpumask_of_node(env->dst_nid)) {
		/* Skip this CPU if the source task cannot migrate */
		if (!cpumask_test_cpu(cpu, tsk_cpus_allowed(env->p)))
			continue;

		env->dst_cpu = cpu;
		task_numa_compare(env, taskimp, groupimp);
	}
}

/* Only move tasks to a NUMA node less busy than the current node. */
static bool numa_has_capacity(struct task_numa_env *env)
{
	struct numa_stats *src = &env->src_stats;
	struct numa_stats *dst = &env->dst_stats;

	if (src->has_free_capacity && !dst->has_free_capacity)
		return false;

	/*
	 * Only consider a task move if the source has a higher load
	 * than the destination, corrected for CPU capacity on each node.
	 *
	 *      src->load                dst->load
	 * --------------------- vs ---------------------
	 * src->compute_capacity    dst->compute_capacity
	 */
	if (src->load * dst->compute_capacity * env->imbalance_pct >

	    dst->load * src->compute_capacity * 100)
		return true;

	return false;
}

static int task_numa_migrate(struct task_struct *p)
{
	struct task_numa_env env = {
		.p = p,

		.src_cpu = task_cpu(p),
		.src_nid = task_node(p),

		.imbalance_pct = 112,

		.best_task = NULL,
		.best_imp = 0,
		.best_cpu = -1
	};
	struct sched_domain *sd;
	unsigned long taskweight, groupweight;
	int nid, ret, dist;
	long taskimp, groupimp;

	/*
	 * Pick the lowest SD_NUMA domain, as that would have the smallest
	 * imbalance and would be the first to start moving tasks about.
	 *
	 * And we want to avoid any moving of tasks about, as that would create
	 * random movement of tasks -- counter the numa conditions we're trying
	 * to satisfy here.
	 */
	rcu_read_lock();
	sd = rcu_dereference(per_cpu(sd_numa, env.src_cpu));
	if (sd)
		env.imbalance_pct = 100 + (sd->imbalance_pct - 100) / 2;
	rcu_read_unlock();

	/*
	 * Cpusets can break the scheduler domain tree into smaller
	 * balance domains, some of which do not cross NUMA boundaries.
	 * Tasks that are "trapped" in such domains cannot be migrated
	 * elsewhere, so there is no point in (re)trying.
	 */
	if (unlikely(!sd)) {
		p->numa_preferred_nid = task_node(p);
		return -EINVAL;
	}

	env.dst_nid = p->numa_preferred_nid;
	dist = env.dist = node_distance(env.src_nid, env.dst_nid);
	taskweight = task_weight(p, env.src_nid, dist);
	groupweight = group_weight(p, env.src_nid, dist);
	update_numa_stats(&env.src_stats, env.src_nid);
	taskimp = task_weight(p, env.dst_nid, dist) - taskweight;
	groupimp = group_weight(p, env.dst_nid, dist) - groupweight;
	update_numa_stats(&env.dst_stats, env.dst_nid);

	/* Try to find a spot on the preferred nid. */
	if (numa_has_capacity(&env))
		task_numa_find_cpu(&env, taskimp, groupimp);

	/*
	 * Look at other nodes in these cases:
	 * - there is no space available on the preferred_nid
	 * - the task is part of a numa_group that is interleaved across
	 *   multiple NUMA nodes; in order to better consolidate the group,
	 *   we need to check other locations.
	 */
	if (env.best_cpu == -1 || (p->numa_group &&
			nodes_weight(p->numa_group->active_nodes) > 1)) {
		for_each_online_node(nid) {
			if (nid == env.src_nid || nid == p->numa_preferred_nid)
				continue;

			dist = node_distance(env.src_nid, env.dst_nid);
			if (sched_numa_topology_type == NUMA_BACKPLANE &&
						dist != env.dist) {
				taskweight = task_weight(p, env.src_nid, dist);
				groupweight = group_weight(p, env.src_nid, dist);
			}

			/* Only consider nodes where both task and groups benefit */
			taskimp = task_weight(p, nid, dist) - taskweight;
			groupimp = group_weight(p, nid, dist) - groupweight;
			if (taskimp < 0 && groupimp < 0)
				continue;

			env.dist = dist;
			env.dst_nid = nid;
			update_numa_stats(&env.dst_stats, env.dst_nid);
			if (numa_has_capacity(&env))
				task_numa_find_cpu(&env, taskimp, groupimp);
		}
	}

	/*
	 * If the task is part of a workload that spans multiple NUMA nodes,
	 * and is migrating into one of the workload's active nodes, remember
	 * this node as the task's preferred numa node, so the workload can
	 * settle down.
	 * A task that migrated to a second choice node will be better off
	 * trying for a better one later. Do not set the preferred node here.
	 */
	if (p->numa_group) {
		if (env.best_cpu == -1)
			nid = env.src_nid;
		else
			nid = env.dst_nid;

		if (node_isset(nid, p->numa_group->active_nodes))
			sched_setnuma(p, env.dst_nid);
	}

	/* No better CPU than the current one was found. */
	if (env.best_cpu == -1)
		return -EAGAIN;

	/*
	 * Reset the scan period if the task is being rescheduled on an
	 * alternative node to recheck if the tasks is now properly placed.
	 */
	p->numa_scan_period = task_scan_min(p);

	if (env.best_task == NULL) {
		ret = migrate_task_to(p, env.best_cpu);
		if (ret != 0)
			trace_sched_stick_numa(p, env.src_cpu, env.best_cpu);
		return ret;
	}

	ret = migrate_swap(p, env.best_task);
	if (ret != 0)
		trace_sched_stick_numa(p, env.src_cpu, task_cpu(env.best_task));
	put_task_struct(env.best_task);
	return ret;
}

/* Attempt to migrate a task to a CPU on the preferred node. */
static void numa_migrate_preferred(struct task_struct *p)
{
	unsigned long interval = HZ;

	/* This task has no NUMA fault statistics yet */
	if (unlikely(p->numa_preferred_nid == -1 || !p->numa_faults))
		return;

	/* Periodically retry migrating the task to the preferred node */
	interval = min(interval, msecs_to_jiffies(p->numa_scan_period) / 16);
	p->numa_migrate_retry = jiffies + interval;

	/* Success if task is already running on preferred CPU */
	if (task_node(p) == p->numa_preferred_nid)
		return;

	/* Otherwise, try migrate to a CPU on the preferred node */
	task_numa_migrate(p);
}

/*
 * Find the nodes on which the workload is actively running. We do this by
 * tracking the nodes from which NUMA hinting faults are triggered. This can
 * be different from the set of nodes where the workload's memory is currently
 * located.
 *
 * The bitmask is used to make smarter decisions on when to do NUMA page
 * migrations, To prevent flip-flopping, and excessive page migrations, nodes
 * are added when they cause over 6/16 of the maximum number of faults, but
 * only removed when they drop below 3/16.
 */
static void update_numa_active_node_mask(struct numa_group *numa_group)
{
	unsigned long faults, max_faults = 0;
	int nid;

	for_each_online_node(nid) {
		faults = group_faults_cpu(numa_group, nid);
		if (faults > max_faults)
			max_faults = faults;
	}

	for_each_online_node(nid) {
		faults = group_faults_cpu(numa_group, nid);
		if (!node_isset(nid, numa_group->active_nodes)) {
			if (faults > max_faults * 6 / 16)
				node_set(nid, numa_group->active_nodes);
		} else if (faults < max_faults * 3 / 16)
			node_clear(nid, numa_group->active_nodes);
	}
}

/*
 * When adapting the scan rate, the period is divided into NUMA_PERIOD_SLOTS
 * increments. The more local the fault statistics are, the higher the scan
 * period will be for the next scan window. If local/(local+remote) ratio is
 * below NUMA_PERIOD_THRESHOLD (where range of ratio is 1..NUMA_PERIOD_SLOTS)
 * the scan period will decrease. Aim for 70% local accesses.
 */
#define NUMA_PERIOD_SLOTS 10
#define NUMA_PERIOD_THRESHOLD 7

/*
 * Increase the scan period (slow down scanning) if the majority of
 * our memory is already on our local node, or if the majority of
 * the page accesses are shared with other processes.
 * Otherwise, decrease the scan period.
 */
static void update_task_scan_period(struct task_struct *p,
			unsigned long shared, unsigned long private)
{
	unsigned int period_slot;
	int ratio;
	int diff;

	unsigned long remote = p->numa_faults_locality[0];
	unsigned long local = p->numa_faults_locality[1];

	/*
	 * If there were no record hinting faults then either the task is
	 * completely idle or all activity is areas that are not of interest
	 * to automatic numa balancing. Related to that, if there were failed
	 * migration then it implies we are migrating too quickly or the local
	 * node is overloaded. In either case, scan slower
	 */
	if (local + shared == 0 || p->numa_faults_locality[2]) {
		p->numa_scan_period = min(p->numa_scan_period_max,
			p->numa_scan_period << 1);

		p->mm->numa_next_scan = jiffies +
			msecs_to_jiffies(p->numa_scan_period);

		return;
	}

	/*
	 * Prepare to scale scan period relative to the current period.
	 *	 == NUMA_PERIOD_THRESHOLD scan period stays the same
	 *       <  NUMA_PERIOD_THRESHOLD scan period decreases (scan faster)
	 *	 >= NUMA_PERIOD_THRESHOLD scan period increases (scan slower)
	 */
	period_slot = DIV_ROUND_UP(p->numa_scan_period, NUMA_PERIOD_SLOTS);
	ratio = (local * NUMA_PERIOD_SLOTS) / (local + remote);
	if (ratio >= NUMA_PERIOD_THRESHOLD) {
		int slot = ratio - NUMA_PERIOD_THRESHOLD;
		if (!slot)
			slot = 1;
		diff = slot * period_slot;
	} else {
		diff = -(NUMA_PERIOD_THRESHOLD - ratio) * period_slot;

		/*
		 * Scale scan rate increases based on sharing. There is an
		 * inverse relationship between the degree of sharing and
		 * the adjustment made to the scanning period. Broadly
		 * speaking the intent is that there is little point
		 * scanning faster if shared accesses dominate as it may
		 * simply bounce migrations uselessly
		 */
		ratio = DIV_ROUND_UP(private * NUMA_PERIOD_SLOTS, (private + shared + 1));
		diff = (diff * ratio) / NUMA_PERIOD_SLOTS;
	}

	p->numa_scan_period = clamp(p->numa_scan_period + diff,
			task_scan_min(p), task_scan_max(p));
	memset(p->numa_faults_locality, 0, sizeof(p->numa_faults_locality));
}

/*
 * Get the fraction of time the task has been running since the last
 * NUMA placement cycle. The scheduler keeps similar statistics, but
 * decays those on a 32ms period, which is orders of magnitude off
 * from the dozens-of-seconds NUMA balancing period. Use the scheduler
 * stats only if the task is so new there are no NUMA statistics yet.
 */
static u64 numa_get_avg_runtime(struct task_struct *p, u64 *period)
{
	u64 runtime, delta, now;
	/* Use the start of this time slice to avoid calculations. */
	now = p->se.exec_start;
	runtime = p->se.sum_exec_runtime;

	if (p->last_task_numa_placement) {
		delta = runtime - p->last_sum_exec_runtime;
		*period = now - p->last_task_numa_placement;
	} else {
		delta = p->se.avg.load_sum / p->se.load.weight;
		*period = LOAD_AVG_MAX;
	}

	p->last_sum_exec_runtime = runtime;
	p->last_task_numa_placement = now;

	return delta;
}

/*
 * Determine the preferred nid for a task in a numa_group. This needs to
 * be done in a way that produces consistent results with group_weight,
 * otherwise workloads might not converge.
 */
static int preferred_group_nid(struct task_struct *p, int nid)
{
	nodemask_t nodes;
	int dist;

	/* Direct connections between all NUMA nodes. */
	if (sched_numa_topology_type == NUMA_DIRECT)
		return nid;

	/*
	 * On a system with glueless mesh NUMA topology, group_weight
	 * scores nodes according to the number of NUMA hinting faults on
	 * both the node itself, and on nearby nodes.
	 */
	if (sched_numa_topology_type == NUMA_GLUELESS_MESH) {
		unsigned long score, max_score = 0;
		int node, max_node = nid;

		dist = sched_max_numa_distance;

		for_each_online_node(node) {
			score = group_weight(p, node, dist);
			if (score > max_score) {
				max_score = score;
				max_node = node;
			}
		}
		return max_node;
	}

	/*
	 * Finding the preferred nid in a system with NUMA backplane
	 * interconnect topology is more involved. The goal is to locate
	 * tasks from numa_groups near each other in the system, and
	 * untangle workloads from different sides of the system. This requires
	 * searching down the hierarchy of node groups, recursively searching
	 * inside the highest scoring group of nodes. The nodemask tricks
	 * keep the complexity of the search down.
	 */
	nodes = node_online_map;
	for (dist = sched_max_numa_distance; dist > LOCAL_DISTANCE; dist--) {
		unsigned long max_faults = 0;
		nodemask_t max_group = NODE_MASK_NONE;
		int a, b;

		/* Are there nodes at this distance from each other? */
		if (!find_numa_distance(dist))
			continue;

		for_each_node_mask(a, nodes) {
			unsigned long faults = 0;
			nodemask_t this_group;
			nodes_clear(this_group);

			/* Sum group's NUMA faults; includes a==b case. */
			for_each_node_mask(b, nodes) {
				if (node_distance(a, b) < dist) {
					faults += group_faults(p, b);
					node_set(b, this_group);
					node_clear(b, nodes);
				}
			}

			/* Remember the top group. */
			if (faults > max_faults) {
				max_faults = faults;
				max_group = this_group;
				/*
				 * subtle: at the smallest distance there is
				 * just one node left in each "group", the
				 * winner is the preferred nid.
				 */
				nid = a;
			}
		}
		/* Next round, evaluate the nodes within max_group. */
		if (!max_faults)
			break;
		nodes = max_group;
	}
	return nid;
}

static void task_numa_placement(struct task_struct *p)
{
	int seq, nid, max_nid = -1, max_group_nid = -1;
	unsigned long max_faults = 0, max_group_faults = 0;
	unsigned long fault_types[2] = { 0, 0 };
	unsigned long total_faults;
	u64 runtime, period;
	spinlock_t *group_lock = NULL;

	/*
	 * The p->mm->numa_scan_seq field gets updated without
	 * exclusive access. Use READ_ONCE() here to ensure
	 * that the field is read in a single access:
	 */
	seq = READ_ONCE(p->mm->numa_scan_seq);
	if (p->numa_scan_seq == seq)
		return;
	p->numa_scan_seq = seq;
	p->numa_scan_period_max = task_scan_max(p);

	total_faults = p->numa_faults_locality[0] +
		       p->numa_faults_locality[1];
	runtime = numa_get_avg_runtime(p, &period);

	/* If the task is part of a group prevent parallel updates to group stats */
	if (p->numa_group) {
		group_lock = &p->numa_group->lock;
		spin_lock_irq(group_lock);
	}

	/* Find the node with the highest number of faults */
	for_each_online_node(nid) {
		/* Keep track of the offsets in numa_faults array */
		int mem_idx, membuf_idx, cpu_idx, cpubuf_idx;
		unsigned long faults = 0, group_faults = 0;
		int priv;

		for (priv = 0; priv < NR_NUMA_HINT_FAULT_TYPES; priv++) {
			long diff, f_diff, f_weight;

			mem_idx = task_faults_idx(NUMA_MEM, nid, priv);
			membuf_idx = task_faults_idx(NUMA_MEMBUF, nid, priv);
			cpu_idx = task_faults_idx(NUMA_CPU, nid, priv);
			cpubuf_idx = task_faults_idx(NUMA_CPUBUF, nid, priv);

			/* Decay existing window, copy faults since last scan */
			diff = p->numa_faults[membuf_idx] - p->numa_faults[mem_idx] / 2;
			fault_types[priv] += p->numa_faults[membuf_idx];
			p->numa_faults[membuf_idx] = 0;

			/*
			 * Normalize the faults_from, so all tasks in a group
			 * count according to CPU use, instead of by the raw
			 * number of faults. Tasks with little runtime have
			 * little over-all impact on throughput, and thus their
			 * faults are less important.
			 */
			f_weight = div64_u64(runtime << 16, period + 1);
			f_weight = (f_weight * p->numa_faults[cpubuf_idx]) /
				   (total_faults + 1);
			f_diff = f_weight - p->numa_faults[cpu_idx] / 2;
			p->numa_faults[cpubuf_idx] = 0;

			p->numa_faults[mem_idx] += diff;
			p->numa_faults[cpu_idx] += f_diff;
			faults += p->numa_faults[mem_idx];
			p->total_numa_faults += diff;
			if (p->numa_group) {
				/*
				 * safe because we can only change our own group
				 *
				 * mem_idx represents the offset for a given
				 * nid and priv in a specific region because it
				 * is at the beginning of the numa_faults array.
				 */
				p->numa_group->faults[mem_idx] += diff;
				p->numa_group->faults_cpu[mem_idx] += f_diff;
				p->numa_group->total_faults += diff;
				group_faults += p->numa_group->faults[mem_idx];
			}
		}

		if (faults > max_faults) {
			max_faults = faults;
			max_nid = nid;
		}

		if (group_faults > max_group_faults) {
			max_group_faults = group_faults;
			max_group_nid = nid;
		}
	}

	update_task_scan_period(p, fault_types[0], fault_types[1]);

	if (p->numa_group) {
		update_numa_active_node_mask(p->numa_group);
		spin_unlock_irq(group_lock);
		max_nid = preferred_group_nid(p, max_group_nid);
	}

	if (max_faults) {
		/* Set the new preferred node */
		if (max_nid != p->numa_preferred_nid)
			sched_setnuma(p, max_nid);

		if (task_node(p) != p->numa_preferred_nid)
			numa_migrate_preferred(p);
	}
}

static inline int get_numa_group(struct numa_group *grp)
{
	return atomic_inc_not_zero(&grp->refcount);
}

static inline void put_numa_group(struct numa_group *grp)
{
	if (atomic_dec_and_test(&grp->refcount))
		kfree_rcu(grp, rcu);
}

static void task_numa_group(struct task_struct *p, int cpupid, int flags,
			int *priv)
{
	struct numa_group *grp, *my_grp;
	struct task_struct *tsk;
	bool join = false;
	int cpu = cpupid_to_cpu(cpupid);
	int i;

	if (unlikely(!p->numa_group)) {
		unsigned int size = sizeof(struct numa_group) +
				    4*nr_node_ids*sizeof(unsigned long);

		grp = kzalloc(size, GFP_KERNEL | __GFP_NOWARN);
		if (!grp)
			return;

		atomic_set(&grp->refcount, 1);
		spin_lock_init(&grp->lock);
		grp->gid = p->pid;
		/* Second half of the array tracks nids where faults happen */
		grp->faults_cpu = grp->faults + NR_NUMA_HINT_FAULT_TYPES *
						nr_node_ids;

		node_set(task_node(current), grp->active_nodes);

		for (i = 0; i < NR_NUMA_HINT_FAULT_STATS * nr_node_ids; i++)
			grp->faults[i] = p->numa_faults[i];

		grp->total_faults = p->total_numa_faults;

		grp->nr_tasks++;
		rcu_assign_pointer(p->numa_group, grp);
	}

	rcu_read_lock();
	tsk = READ_ONCE(cpu_rq(cpu)->curr);

	if (!cpupid_match_pid(tsk, cpupid))
		goto no_join;

	grp = rcu_dereference(tsk->numa_group);
	if (!grp)
		goto no_join;

	my_grp = p->numa_group;
	if (grp == my_grp)
		goto no_join;

	/*
	 * Only join the other group if its bigger; if we're the bigger group,
	 * the other task will join us.
	 */
	if (my_grp->nr_tasks > grp->nr_tasks)
		goto no_join;

	/*
	 * Tie-break on the grp address.
	 */
	if (my_grp->nr_tasks == grp->nr_tasks && my_grp > grp)
		goto no_join;

	/* Always join threads in the same process. */
	if (tsk->mm == current->mm)
		join = true;

	/* Simple filter to avoid false positives due to PID collisions */
	if (flags & TNF_SHARED)
		join = true;

	/* Update priv based on whether false sharing was detected */
	*priv = !join;

	if (join && !get_numa_group(grp))
		goto no_join;

	rcu_read_unlock();

	if (!join)
		return;

	BUG_ON(irqs_disabled());
	double_lock_irq(&my_grp->lock, &grp->lock);

	for (i = 0; i < NR_NUMA_HINT_FAULT_STATS * nr_node_ids; i++) {
		my_grp->faults[i] -= p->numa_faults[i];
		grp->faults[i] += p->numa_faults[i];
	}
	my_grp->total_faults -= p->total_numa_faults;
	grp->total_faults += p->total_numa_faults;

	my_grp->nr_tasks--;
	grp->nr_tasks++;

	spin_unlock(&my_grp->lock);
	spin_unlock_irq(&grp->lock);

	rcu_assign_pointer(p->numa_group, grp);

	put_numa_group(my_grp);
	return;

no_join:
	rcu_read_unlock();
	return;
}

void task_numa_free(struct task_struct *p)
{
	struct numa_group *grp = p->numa_group;
	void *numa_faults = p->numa_faults;
	unsigned long flags;
	int i;

	if (grp) {
		spin_lock_irqsave(&grp->lock, flags);
		for (i = 0; i < NR_NUMA_HINT_FAULT_STATS * nr_node_ids; i++)
			grp->faults[i] -= p->numa_faults[i];
		grp->total_faults -= p->total_numa_faults;

		grp->nr_tasks--;
		spin_unlock_irqrestore(&grp->lock, flags);
		RCU_INIT_POINTER(p->numa_group, NULL);
		put_numa_group(grp);
	}

	p->numa_faults = NULL;
	kfree(numa_faults);
}

/*
 * Got a PROT_NONE fault for a page on @node.
 */
void task_numa_fault(int last_cpupid, int mem_node, int pages, int flags)
{
	struct task_struct *p = current;
	bool migrated = flags & TNF_MIGRATED;
	int cpu_node = task_node(current);
	int local = !!(flags & TNF_FAULT_LOCAL);
	int priv;

	if (!static_branch_likely(&sched_numa_balancing))
		return;

	/* for example, ksmd faulting in a user's mm */
	if (!p->mm)
		return;

	/* Allocate buffer to track faults on a per-node basis */
	if (unlikely(!p->numa_faults)) {
		int size = sizeof(*p->numa_faults) *
			   NR_NUMA_HINT_FAULT_BUCKETS * nr_node_ids;

		p->numa_faults = kzalloc(size, GFP_KERNEL|__GFP_NOWARN);
		if (!p->numa_faults)
			return;

		p->total_numa_faults = 0;
		memset(p->numa_faults_locality, 0, sizeof(p->numa_faults_locality));
	}

	/*
	 * First accesses are treated as private, otherwise consider accesses
	 * to be private if the accessing pid has not changed
	 */
	if (unlikely(last_cpupid == (-1 & LAST_CPUPID_MASK))) {
		priv = 1;
	} else {
		priv = cpupid_match_pid(p, last_cpupid);
		if (!priv && !(flags & TNF_NO_GROUP))
			task_numa_group(p, last_cpupid, flags, &priv);
	}

	/*
	 * If a workload spans multiple NUMA nodes, a shared fault that
	 * occurs wholly within the set of nodes that the workload is
	 * actively using should be counted as local. This allows the
	 * scan rate to slow down when a workload has settled down.
	 */
	if (!priv && !local && p->numa_group &&
			node_isset(cpu_node, p->numa_group->active_nodes) &&
			node_isset(mem_node, p->numa_group->active_nodes))
		local = 1;

	task_numa_placement(p);

	/*
	 * Retry task to preferred node migration periodically, in case it
	 * case it previously failed, or the scheduler moved us.
	 */
	if (time_after(jiffies, p->numa_migrate_retry))
		numa_migrate_preferred(p);

	if (migrated)
		p->numa_pages_migrated += pages;
	if (flags & TNF_MIGRATE_FAIL)
		p->numa_faults_locality[2] += pages;

	p->numa_faults[task_faults_idx(NUMA_MEMBUF, mem_node, priv)] += pages;
	p->numa_faults[task_faults_idx(NUMA_CPUBUF, cpu_node, priv)] += pages;
	p->numa_faults_locality[local] += pages;
}

static void reset_ptenuma_scan(struct task_struct *p)
{
	/*
	 * We only did a read acquisition of the mmap sem, so
	 * p->mm->numa_scan_seq is written to without exclusive access
	 * and the update is not guaranteed to be atomic. That's not
	 * much of an issue though, since this is just used for
	 * statistical sampling. Use READ_ONCE/WRITE_ONCE, which are not
	 * expensive, to avoid any form of compiler optimizations:
	 */
	WRITE_ONCE(p->mm->numa_scan_seq, READ_ONCE(p->mm->numa_scan_seq) + 1);
	p->mm->numa_scan_offset = 0;
}

/*
 * The expensive part of numa migration is done from task_work context.
 * Triggered from task_tick_numa().
 */
void task_numa_work(struct callback_head *work)
{
	unsigned long migrate, next_scan, now = jiffies;
	struct task_struct *p = current;
	struct mm_struct *mm = p->mm;
	struct vm_area_struct *vma;
	unsigned long start, end;
	unsigned long nr_pte_updates = 0;
	long pages, virtpages;

	WARN_ON_ONCE(p != container_of(work, struct task_struct, numa_work));

	work->next = work; /* protect against double add */
	/*
	 * Who cares about NUMA placement when they're dying.
	 *
	 * NOTE: make sure not to dereference p->mm before this check,
	 * exit_task_work() happens _after_ exit_mm() so we could be called
	 * without p->mm even though we still had it when we enqueued this
	 * work.
	 */
	if (p->flags & PF_EXITING)
		return;

	if (!mm->numa_next_scan) {
		mm->numa_next_scan = now +
			msecs_to_jiffies(sysctl_numa_balancing_scan_delay);
	}

	/*
	 * Enforce maximal scan/migration frequency..
	 */
	migrate = mm->numa_next_scan;
	if (time_before(now, migrate))
		return;

	if (p->numa_scan_period == 0) {
		p->numa_scan_period_max = task_scan_max(p);
		p->numa_scan_period = task_scan_min(p);
	}

	next_scan = now + msecs_to_jiffies(p->numa_scan_period);
	if (cmpxchg(&mm->numa_next_scan, migrate, next_scan) != migrate)
		return;

	/*
	 * Delay this task enough that another task of this mm will likely win
	 * the next time around.
	 */
	p->node_stamp += 2 * TICK_NSEC;

	start = mm->numa_scan_offset;
	pages = sysctl_numa_balancing_scan_size;
	pages <<= 20 - PAGE_SHIFT; /* MB in pages */
	virtpages = pages * 8;	   /* Scan up to this much virtual space */
	if (!pages)
		return;


	if (!down_read_trylock(&mm->mmap_sem))
		return;
	vma = find_vma(mm, start);
	if (!vma) {
		reset_ptenuma_scan(p);
		start = 0;
		vma = mm->mmap;
	}
	for (; vma; vma = vma->vm_next) {
		if (!vma_migratable(vma) || !vma_policy_mof(vma) ||
			is_vm_hugetlb_page(vma) || (vma->vm_flags & VM_MIXEDMAP)) {
			continue;
		}

		/*
		 * Shared library pages mapped by multiple processes are not
		 * migrated as it is expected they are cache replicated. Avoid
		 * hinting faults in read-only file-backed mappings or the vdso
		 * as migrating the pages will be of marginal benefit.
		 */
		if (!vma->vm_mm ||
		    (vma->vm_file && (vma->vm_flags & (VM_READ|VM_WRITE)) == (VM_READ)))
			continue;

		/*
		 * Skip inaccessible VMAs to avoid any confusion between
		 * PROT_NONE and NUMA hinting ptes
		 */
		if (!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE)))
			continue;

		do {
			start = max(start, vma->vm_start);
			end = ALIGN(start + (pages << PAGE_SHIFT), HPAGE_SIZE);
			end = min(end, vma->vm_end);
			nr_pte_updates = change_prot_numa(vma, start, end);

			/*
			 * Try to scan sysctl_numa_balancing_size worth of
			 * hpages that have at least one present PTE that
			 * is not already pte-numa. If the VMA contains
			 * areas that are unused or already full of prot_numa
			 * PTEs, scan up to virtpages, to skip through those
			 * areas faster.
			 */
			if (nr_pte_updates)
				pages -= (end - start) >> PAGE_SHIFT;
			virtpages -= (end - start) >> PAGE_SHIFT;

			start = end;
			if (pages <= 0 || virtpages <= 0)
				goto out;

			cond_resched();
		} while (end != vma->vm_end);
	}

out:
	/*
	 * It is possible to reach the end of the VMA list but the last few
	 * VMAs are not guaranteed to the vma_migratable. If they are not, we
	 * would find the !migratable VMA on the next scan but not reset the
	 * scanner to the start so check it now.
	 */
	if (vma)
		mm->numa_scan_offset = start;
	else
		reset_ptenuma_scan(p);
	up_read(&mm->mmap_sem);
}

/*
 * Drive the periodic memory faults..
 */
void task_tick_numa(struct rq *rq, struct task_struct *curr)
{
	struct callback_head *work = &curr->numa_work;
	u64 period, now;

	/*
	 * We don't care about NUMA placement if we don't have memory.
	 */
	if (!curr->mm || (curr->flags & PF_EXITING) || work->next != work)
		return;

	/*
	 * Using runtime rather than walltime has the dual advantage that
	 * we (mostly) drive the selection from busy threads and that the
	 * task needs to have done some actual work before we bother with
	 * NUMA placement.
	 */
	now = curr->se.sum_exec_runtime;
	period = (u64)curr->numa_scan_period * NSEC_PER_MSEC;

	if (now > curr->node_stamp + period) {
		if (!curr->node_stamp)
			curr->numa_scan_period = task_scan_min(curr);
		curr->node_stamp += period;

		if (!time_before(jiffies, curr->mm->numa_next_scan)) {
			init_task_work(work, task_numa_work); /* TODO: move this into sched_fork() */
			task_work_add(curr, work, true);
		}
	}
}
#else
static void task_tick_numa(struct rq *rq, struct task_struct *curr)
{
}

static inline void account_numa_enqueue(struct rq *rq, struct task_struct *p)
{
}

static inline void account_numa_dequeue(struct rq *rq, struct task_struct *p)
{
}
#endif /* CONFIG_NUMA_BALANCING */

static void
account_entity_enqueue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	update_load_add(&cfs_rq->load, se->load.weight);
	if (!parent_entity(se))
		update_load_add(&rq_of(cfs_rq)->load, se->load.weight);
#ifdef CONFIG_SMP
	if (entity_is_task(se)) {
		struct rq *rq = rq_of(cfs_rq);

		account_numa_enqueue(rq, task_of(se));
		list_add(&se->group_node, &rq->cfs_tasks);
	}
#endif
	cfs_rq->nr_running++;
}

static void
account_entity_dequeue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	update_load_sub(&cfs_rq->load, se->load.weight);
	if (!parent_entity(se))
		update_load_sub(&rq_of(cfs_rq)->load, se->load.weight);
	if (entity_is_task(se)) {
		account_numa_dequeue(rq_of(cfs_rq), task_of(se));
		list_del_init(&se->group_node);
	}
	cfs_rq->nr_running--;
}

#ifdef CONFIG_FAIR_GROUP_SCHED
# ifdef CONFIG_SMP
static long calc_cfs_shares(struct cfs_rq *cfs_rq, struct task_group *tg)
{
	long tg_weight, load, shares;

	/*
	 * This really should be: cfs_rq->avg.load_avg, but instead we use
	 * cfs_rq->load.weight, which is its upper bound. This helps ramp up
	 * the shares for small weight interactive tasks.
	 */
	load = scale_load_down(cfs_rq->load.weight);

	tg_weight = atomic_long_read(&tg->load_avg);

	/* Ensure tg_weight >= load */
	tg_weight -= cfs_rq->tg_load_avg_contrib;
	tg_weight += load;

	shares = (tg->shares * load);
	if (tg_weight)
		shares /= tg_weight;

	if (shares < MIN_SHARES)
		shares = MIN_SHARES;
	if (shares > tg->shares)
		shares = tg->shares;

	return shares;
}
# else /* CONFIG_SMP */
static inline long calc_cfs_shares(struct cfs_rq *cfs_rq, struct task_group *tg)
{
	return tg->shares;
}
# endif /* CONFIG_SMP */

static void reweight_entity(struct cfs_rq *cfs_rq, struct sched_entity *se,
			    unsigned long weight)
{
	if (se->on_rq) {
		/* commit outstanding execution time */
		if (cfs_rq->curr == se)
			update_curr(cfs_rq);
		account_entity_dequeue(cfs_rq, se);
	}

	update_load_set(&se->load, weight);

	if (se->on_rq)
		account_entity_enqueue(cfs_rq, se);
}

static inline int throttled_hierarchy(struct cfs_rq *cfs_rq);

static void update_cfs_shares(struct sched_entity *se)
{
	struct cfs_rq *cfs_rq = group_cfs_rq(se);
	struct task_group *tg;
	long shares;

	if (!cfs_rq)
		return;

	if (throttled_hierarchy(cfs_rq))
		return;

	tg = cfs_rq->tg;

#ifndef CONFIG_SMP
	if (likely(se->load.weight == tg->shares))
		return;
#endif
	shares = calc_cfs_shares(cfs_rq, tg);

	reweight_entity(cfs_rq_of(se), se, shares);
}

#else /* CONFIG_FAIR_GROUP_SCHED */
static inline void update_cfs_shares(struct sched_entity *se)
{
}
#endif /* CONFIG_FAIR_GROUP_SCHED */

#ifdef CONFIG_SMP
/* Precomputed fixed inverse multiplies for multiplication by y^n */
static const u32 runnable_avg_yN_inv[] = {
	0xffffffff, 0xfa83b2da, 0xf5257d14, 0xefe4b99a, 0xeac0c6e6, 0xe5b906e6,
	0xe0ccdeeb, 0xdbfbb796, 0xd744fcc9, 0xd2a81d91, 0xce248c14, 0xc9b9bd85,
	0xc5672a10, 0xc12c4cc9, 0xbd08a39e, 0xb8fbaf46, 0xb504f333, 0xb123f581,
	0xad583ee9, 0xa9a15ab4, 0xa5fed6a9, 0xa2704302, 0x9ef5325f, 0x9b8d39b9,
	0x9837f050, 0x94f4efa8, 0x91c3d373, 0x8ea4398a, 0x8b95c1e3, 0x88980e80,
	0x85aac367, 0x82cd8698,
};

/*
 * Precomputed \Sum y^k { 1<=k<=n }.  These are floor(true_value) to prevent
 * over-estimates when re-combining.
 */
static const u32 runnable_avg_yN_sum[] = {
	    0, 1002, 1982, 2941, 3880, 4798, 5697, 6576, 7437, 8279, 9103,
	 9909,10698,11470,12226,12966,13690,14398,15091,15769,16433,17082,
	17718,18340,18949,19545,20128,20698,21256,21802,22336,22859,23371,
};

/*
 * Approximate:
 *   val * y^n,    where y^32 ~= 0.5 (~1 scheduling period)
 */
static __always_inline u64 decay_load(u64 val, u64 n)
{
	unsigned int local_n;

	if (!n)
		return val;
	else if (unlikely(n > LOAD_AVG_PERIOD * 63))
		return 0;

	/* after bounds checking we can collapse to 32-bit */
	local_n = n;

	/*
	 * As y^PERIOD = 1/2, we can combine
	 *    y^n = 1/2^(n/PERIOD) * y^(n%PERIOD)
	 * With a look-up table which covers y^n (n<PERIOD)
	 *
	 * To achieve constant time decay_load.
	 */
	if (unlikely(local_n >= LOAD_AVG_PERIOD)) {
		val >>= local_n / LOAD_AVG_PERIOD;
		local_n %= LOAD_AVG_PERIOD;
	}

	val = mul_u64_u32_shr(val, runnable_avg_yN_inv[local_n], 32);
	return val;
}

/*
 * For updates fully spanning n periods, the contribution to runnable
 * average will be: \Sum 1024*y^n
 *
 * We can compute this reasonably efficiently by combining:
 *   y^PERIOD = 1/2 with precomputed \Sum 1024*y^n {for  n <PERIOD}
 */
static u32 __compute_runnable_contrib(u64 n)
{
	u32 contrib = 0;

	if (likely(n <= LOAD_AVG_PERIOD))
		return runnable_avg_yN_sum[n];
	else if (unlikely(n >= LOAD_AVG_MAX_N))
		return LOAD_AVG_MAX;

	/* Compute \Sum k^n combining precomputed values for k^i, \Sum k^j */
	do {
		contrib /= 2; /* y^LOAD_AVG_PERIOD = 1/2 */
		contrib += runnable_avg_yN_sum[LOAD_AVG_PERIOD];

		n -= LOAD_AVG_PERIOD;
	} while (n > LOAD_AVG_PERIOD);

	contrib = decay_load(contrib, n);
	return contrib + runnable_avg_yN_sum[n];
}

#if (SCHED_LOAD_SHIFT - SCHED_LOAD_RESOLUTION) != 10 || SCHED_CAPACITY_SHIFT != 10
#error "load tracking assumes 2^10 as unit"
#endif

#define cap_scale(v, s) ((v)*(s) >> SCHED_CAPACITY_SHIFT)

/*
 * We can represent the historical contribution to runnable average as the
 * coefficients of a geometric series.  To do this we sub-divide our runnable
 * history into segments of approximately 1ms (1024us); label the segment that
 * occurred N-ms ago p_N, with p_0 corresponding to the current period, e.g.
 *
 * [<- 1024us ->|<- 1024us ->|<- 1024us ->| ...
 *      p0            p1           p2
 *     (now)       (~1ms ago)  (~2ms ago)
 *
 * Let u_i denote the fraction of p_i that the entity was runnable.
 *
 * We then designate the fractions u_i as our co-efficients, yielding the
 * following representation of historical load:
 *   u_0 + u_1*y + u_2*y^2 + u_3*y^3 + ...
 *
 * We choose y based on the with of a reasonably scheduling period, fixing:
 *   y^32 = 0.5
 *
 * This means that the contribution to load ~32ms ago (u_32) will be weighted
 * approximately half as much as the contribution to load within the last ms
 * (u_0).
 *
 * When a period "rolls over" and we have new u_0`, multiplying the previous
 * sum again by y is sufficient to update:
 *   load_avg = u_0` + y*(u_0 + u_1*y + u_2*y^2 + ... )
 *            = u_0 + u_1*y + u_2*y^2 + ... [re-labeling u_i --> u_{i+1}]
 */
static __always_inline int
__update_load_avg(u64 now, int cpu, struct sched_avg *sa,
		  unsigned long weight, int running, struct cfs_rq *cfs_rq)
{
	u64 delta, scaled_delta, periods;
	u32 contrib;
	unsigned int delta_w, scaled_delta_w, decayed = 0;
	unsigned long scale_freq, scale_cpu;

	delta = now - sa->last_update_time;
	/*
	 * This should only happen when time goes backwards, which it
	 * unfortunately does during sched clock init when we swap over to TSC.
	 */
	if ((s64)delta < 0) {
		sa->last_update_time = now;
		return 0;
	}

	/*
	 * Use 1024ns as the unit of measurement since it's a reasonable
	 * approximation of 1us and fast to compute.
	 */
	delta >>= 10;
	if (!delta)
		return 0;
	sa->last_update_time = now;

	scale_freq = arch_scale_freq_capacity(NULL, cpu);
	scale_cpu = arch_scale_cpu_capacity(NULL, cpu);
	trace_sched_contrib_scale_f(cpu, scale_freq, scale_cpu);

	/* delta_w is the amount already accumulated against our next period */
	delta_w = sa->period_contrib;
	if (delta + delta_w >= 1024) {
		decayed = 1;

		/* how much left for next period will start over, we don't know yet */
		sa->period_contrib = 0;

		/*
		 * Now that we know we're crossing a period boundary, figure
		 * out how much from delta we need to complete the current
		 * period and accrue it.
		 */
		delta_w = 1024 - delta_w;
		scaled_delta_w = cap_scale(delta_w, scale_freq);
		if (weight) {
			sa->load_sum += weight * scaled_delta_w;
			if (cfs_rq) {
				cfs_rq->runnable_load_sum +=
						weight * scaled_delta_w;
			}
		}
		if (running)
			sa->util_sum += scaled_delta_w * scale_cpu;

		delta -= delta_w;

		/* Figure out how many additional periods this update spans */
		periods = delta / 1024;
		delta %= 1024;

		sa->load_sum = decay_load(sa->load_sum, periods + 1);
		if (cfs_rq) {
			cfs_rq->runnable_load_sum =
				decay_load(cfs_rq->runnable_load_sum, periods + 1);
		}
		sa->util_sum = decay_load((u64)(sa->util_sum), periods + 1);

		/* Efficiently calculate \sum (1..n_period) 1024*y^i */
		contrib = __compute_runnable_contrib(periods);
		contrib = cap_scale(contrib, scale_freq);
		if (weight) {
			sa->load_sum += weight * contrib;
			if (cfs_rq)
				cfs_rq->runnable_load_sum += weight * contrib;
		}
		if (running)
			sa->util_sum += contrib * scale_cpu;
	}

	/* Remainder of delta accrued against u_0` */
	scaled_delta = cap_scale(delta, scale_freq);
	if (weight) {
		sa->load_sum += weight * scaled_delta;
		if (cfs_rq)
			cfs_rq->runnable_load_sum += weight * scaled_delta;
	}
	if (running)
		sa->util_sum += scaled_delta * scale_cpu;

	sa->period_contrib += delta;

	if (decayed) {
		sa->load_avg = div_u64(sa->load_sum, LOAD_AVG_MAX);
		if (cfs_rq) {
			cfs_rq->runnable_load_avg =
				div_u64(cfs_rq->runnable_load_sum, LOAD_AVG_MAX);
		}
		sa->util_avg = sa->util_sum / LOAD_AVG_MAX;
	}

	return decayed;
}

/*
 * Signed add and clamp on underflow.
 *
 * Explicitly do a load-store to ensure the intermediate value never hits
 * memory. This allows lockless observations without ever seeing the negative
 * values.
 */
#define add_positive(_ptr, _val) do {                           \
	typeof(_ptr) ptr = (_ptr);                              \
	typeof(_val) val = (_val);                              \
	typeof(*ptr) res, var = READ_ONCE(*ptr);                \
								\
	res = var + val;                                        \
								\
	if (val < 0 && res > var)                               \
		res = 0;                                        \
								\
	WRITE_ONCE(*ptr, res);                                  \
} while (0)

#ifdef CONFIG_FAIR_GROUP_SCHED
/**
 * update_tg_load_avg - update the tg's load avg
 * @cfs_rq: the cfs_rq whose avg changed
 * @force: update regardless of how small the difference
 *
 * This function 'ensures': tg->load_avg := \Sum tg->cfs_rq[]->avg.load.
 * However, because tg->load_avg is a global value there are performance
 * considerations.
 *
 * In order to avoid having to look at the other cfs_rq's, we use a
 * differential update where we store the last value we propagated. This in
 * turn allows skipping updates if the differential is 'small'.
 *
 * Updating tg's load_avg is necessary before update_cfs_share() (which is
 * done) and effective_load() (which is not done because it is too costly).
 */
static inline void update_tg_load_avg(struct cfs_rq *cfs_rq, int force)
{
	long delta = cfs_rq->avg.load_avg - cfs_rq->tg_load_avg_contrib;

	/*
	 * No need to update load_avg for root_task_group as it is not used.
	 */
	if (cfs_rq->tg == &root_task_group)
		return;

	if (force || abs(delta) > cfs_rq->tg_load_avg_contrib / 64) {
		atomic_long_add(delta, &cfs_rq->tg->load_avg);
		cfs_rq->tg_load_avg_contrib = cfs_rq->avg.load_avg;
	}
}

/*
 * Called within set_task_rq() right before setting a task's cpu. The
 * caller only guarantees p->pi_lock is held; no other assumptions,
 * including the state of rq->lock, should be made.
 */
void set_task_rq_fair(struct sched_entity *se,
		      struct cfs_rq *prev, struct cfs_rq *next)
{
	if (!sched_feat(ATTACH_AGE_LOAD))
		return;

	/*
	 * We are supposed to update the task to "current" time, then its up to
	 * date and ready to go to new CPU/cfs_rq. But we have difficulty in
	 * getting what current time is, so simply throw away the out-of-date
	 * time. This will result in the wakee task is less decayed, but giving
	 * the wakee more load sounds not bad.
	 */
	if (se->avg.last_update_time && prev) {
		u64 p_last_update_time;
		u64 n_last_update_time;

#ifndef CONFIG_64BIT
		u64 p_last_update_time_copy;
		u64 n_last_update_time_copy;

		do {
			p_last_update_time_copy = prev->load_last_update_time_copy;
			n_last_update_time_copy = next->load_last_update_time_copy;

			smp_rmb();

			p_last_update_time = prev->avg.last_update_time;
			n_last_update_time = next->avg.last_update_time;

		} while (p_last_update_time != p_last_update_time_copy ||
			 n_last_update_time != n_last_update_time_copy);
#else
		p_last_update_time = prev->avg.last_update_time;
		n_last_update_time = next->avg.last_update_time;
#endif
		__update_load_avg(p_last_update_time, cpu_of(rq_of(prev)),
				  &se->avg, 0, 0, NULL);
		se->avg.last_update_time = n_last_update_time;
	}
}

/* Take into account change of utilization of a child task group */
static inline void
update_tg_cfs_util(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	struct cfs_rq *gcfs_rq = group_cfs_rq(se);
	long delta = gcfs_rq->avg.util_avg - se->avg.util_avg;

	/* Nothing to update */
	if (!delta)
		return;

	/* Set new sched_entity's utilization */
	se->avg.util_avg = gcfs_rq->avg.util_avg;
	se->avg.util_sum = se->avg.util_avg * LOAD_AVG_MAX;

	/* Update parent cfs_rq utilization */
	add_positive(&cfs_rq->avg.util_avg, delta);
	cfs_rq->avg.util_sum = cfs_rq->avg.util_avg * LOAD_AVG_MAX;
}

/* Take into account change of load of a child task group */
static inline void
update_tg_cfs_load(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	struct cfs_rq *gcfs_rq = group_cfs_rq(se);
	long delta, load = gcfs_rq->avg.load_avg;

	/*
	 * If the load of group cfs_rq is null, the load of the
	 * sched_entity will also be null so we can skip the formula
	 */
	if (load) {
		long tg_load;

		/* Get tg's load and ensure tg_load > 0 */
		tg_load = atomic_long_read(&gcfs_rq->tg->load_avg) + 1;

		/* Ensure tg_load >= load and updated with current load*/
		tg_load -= gcfs_rq->tg_load_avg_contrib;
		tg_load += load;

		/*
		 * We need to compute a correction term in the case that the
		 * task group is consuming more CPU than a task of equal
		 * weight. A task with a weight equals to tg->shares will have
		 * a load less or equal to scale_load_down(tg->shares).
		 * Similarly, the sched_entities that represent the task group
		 * at parent level, can't have a load higher than
		 * scale_load_down(tg->shares). And the Sum of sched_entities'
		 * load must be <= scale_load_down(tg->shares).
		 */
		if (tg_load > scale_load_down(gcfs_rq->tg->shares)) {
			/* scale gcfs_rq's load into tg's shares*/
			load *= scale_load_down(gcfs_rq->tg->shares);
			load /= tg_load;
		}
	}

	delta = load - se->avg.load_avg;

	/* Nothing to update */
	if (!delta)
		return;

	/* Set new sched_entity's load */
	se->avg.load_avg = load;
	se->avg.load_sum = se->avg.load_avg * LOAD_AVG_MAX;

	/* Update parent cfs_rq load */
	add_positive(&cfs_rq->avg.load_avg, delta);
	cfs_rq->avg.load_sum = cfs_rq->avg.load_avg * LOAD_AVG_MAX;

	/*
	 * If the sched_entity is already enqueued, we also have to update the
	 * runnable load avg.
	 */
	if (se->on_rq) {
		/* Update parent cfs_rq runnable_load_avg */
		add_positive(&cfs_rq->runnable_load_avg, delta);
		cfs_rq->runnable_load_sum = cfs_rq->runnable_load_avg * LOAD_AVG_MAX;
	}
}

static inline void set_tg_cfs_propagate(struct cfs_rq *cfs_rq)
{
	cfs_rq->propagate_avg = 1;
}

static inline int test_and_clear_tg_cfs_propagate(struct sched_entity *se)
{
	struct cfs_rq *cfs_rq = group_cfs_rq(se);

	if (!cfs_rq->propagate_avg)
		return 0;

	cfs_rq->propagate_avg = 0;
	return 1;
}

/* Update task and its cfs_rq load average */
static inline int propagate_entity_load_avg(struct sched_entity *se)
{
	struct cfs_rq *cfs_rq;

	if (entity_is_task(se))
		return 0;

	if (!test_and_clear_tg_cfs_propagate(se))
		return 0;

	cfs_rq = cfs_rq_of(se);

	set_tg_cfs_propagate(cfs_rq);

	update_tg_cfs_util(cfs_rq, se);
	update_tg_cfs_load(cfs_rq, se);

	return 1;
}

#else /* CONFIG_FAIR_GROUP_SCHED */

static inline void update_tg_load_avg(struct cfs_rq *cfs_rq, int force) {}

static inline int propagate_entity_load_avg(struct sched_entity *se)
{
	return 0;
}

static inline void set_tg_cfs_propagate(struct cfs_rq *cfs_rq) {}

#endif /* CONFIG_FAIR_GROUP_SCHED */

static inline void cfs_rq_util_change(struct cfs_rq *cfs_rq)
{
        if (&this_rq()->cfs == cfs_rq) {
                /*
                 * There are a few boundary cases this might miss but it should
                 * get called often enough that that should (hopefully) not be
                 * a real problem -- added to that it only calls on the local
                 * CPU, so if we enqueue remotely we'll miss an update, but
                 * the next tick/schedule should update.
                 *
                 * It will not get called when we go idle, because the idle
                 * thread is a different class (!fair), nor will the utilization
                 * number include things like RT tasks.
                 *
                 * As is, the util number is not freq-invariant (we'd have to
                 * implement arch_scale_freq_capacity() for that).
                 *
                 * See cpu_util().
                 */
                cpufreq_update_util(rq_of(cfs_rq), 0);
        }
}

static inline u64 cfs_rq_clock_task(struct cfs_rq *cfs_rq);

/*
 * Unsigned subtract and clamp on underflow.
 *
 * Explicitly do a load-store to ensure the intermediate value never hits
 * memory. This allows lockless observations without ever seeing the negative
 * values.
 */
#define sub_positive(_ptr, _val) do {				\
	typeof(_ptr) ptr = (_ptr);				\
	typeof(*ptr) val = (_val);				\
	typeof(*ptr) res, var = READ_ONCE(*ptr);		\
	res = var - val;					\
	if (res > var)						\
		res = 0;					\
	WRITE_ONCE(*ptr, res);					\
} while (0)

/**
 * update_cfs_rq_load_avg - update the cfs_rq's load/util averages
 * @now: current time, as per cfs_rq_clock_task()
 * @cfs_rq: cfs_rq to update
 * @update_freq: should we call cfs_rq_util_change() or will the call do so
 *
 * The cfs_rq avg is the direct sum of all its entities (blocked and runnable)
 * avg. The immediate corollary is that all (fair) tasks must be attached, see
 * post_init_entity_util_avg().
 *
 * cfs_rq->avg is used for task_h_load() and update_cfs_share() for example.
 *
 * Returns true if the load decayed or we removed load.
 *
 * Since both these conditions indicate a changed cfs_rq->avg.load we should
 * call update_tg_load_avg() when this function returns true.
 */
static inline int
update_cfs_rq_load_avg(u64 now, struct cfs_rq *cfs_rq, bool update_freq)
{
	struct sched_avg *sa = &cfs_rq->avg;
	int decayed, removed = 0, removed_util = 0;

	if (atomic_long_read(&cfs_rq->removed_load_avg)) {
		s64 r = atomic_long_xchg(&cfs_rq->removed_load_avg, 0);
		sub_positive(&sa->load_avg, r);
		sub_positive(&sa->load_sum, r * LOAD_AVG_MAX);
		removed = 1;
		set_tg_cfs_propagate(cfs_rq);
	}

	if (atomic_long_read(&cfs_rq->removed_util_avg)) {
		long r = atomic_long_xchg(&cfs_rq->removed_util_avg, 0);
		sub_positive(&sa->util_avg, r);
		sub_positive(&sa->util_sum, r * LOAD_AVG_MAX);
		removed_util = 1;
		set_tg_cfs_propagate(cfs_rq);
	}

	decayed = __update_load_avg(now, cpu_of(rq_of(cfs_rq)), sa,
		scale_load_down(cfs_rq->load.weight), cfs_rq->curr != NULL, cfs_rq);

#ifndef CONFIG_64BIT
	smp_wmb();
	cfs_rq->load_last_update_time_copy = sa->last_update_time;
#endif

	/* Trace CPU load, unless cfs_rq belongs to a non-root task_group */
	if (cfs_rq == &rq_of(cfs_rq)->cfs)
		trace_sched_load_avg_cpu(cpu_of(rq_of(cfs_rq)), cfs_rq);

	if (update_freq && (decayed || removed_util))
		cfs_rq_util_change(cfs_rq);

	return decayed || removed;
}

/*
 * Optional action to be done while updating the load average
 */
#define UPDATE_TG	0x1
#define SKIP_AGE_LOAD	0x2

/* Update task and its cfs_rq load average */
static inline void update_load_avg(struct sched_entity *se, int flags)
{
	struct cfs_rq *cfs_rq = cfs_rq_of(se);
	u64 now = cfs_rq_clock_task(cfs_rq);
	int cpu = cpu_of(rq_of(cfs_rq));
	int decayed;
	void *ptr = NULL;

	/*
	 * Track task load average for carrying it to new CPU after migrated, and
	 * track group sched_entity load average for task_h_load calc in migration
	 */
	if (se->avg.last_update_time && !(flags & SKIP_AGE_LOAD)) {
		__update_load_avg(now, cpu, &se->avg,
			  se->on_rq * scale_load_down(se->load.weight),
			  cfs_rq->curr == se, NULL);
	}

	decayed  = update_cfs_rq_load_avg(now, cfs_rq, true);
	decayed |= propagate_entity_load_avg(se);

	if (decayed && (flags & UPDATE_TG))
		update_tg_load_avg(cfs_rq, 0);

	if (entity_is_task(se)) {
#ifdef CONFIG_SCHED_WALT
		ptr = (void *)&(task_of(se)->ravg);
#endif
		trace_sched_load_avg_task(task_of(se), &se->avg, ptr);
	}
}

/**
 * attach_entity_load_avg - attach this entity to its cfs_rq load avg
 * @cfs_rq: cfs_rq to attach to
 * @se: sched_entity to attach
 *
 * Must call update_cfs_rq_load_avg() before this, since we rely on
 * cfs_rq->avg.last_update_time being current.
 */
static void attach_entity_load_avg(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	se->avg.last_update_time = cfs_rq->avg.last_update_time;
	cfs_rq->avg.load_avg += se->avg.load_avg;
	cfs_rq->avg.load_sum += se->avg.load_sum;
	cfs_rq->avg.util_avg += se->avg.util_avg;
	cfs_rq->avg.util_sum += se->avg.util_sum;
	set_tg_cfs_propagate(cfs_rq);

	cfs_rq_util_change(cfs_rq);
}

/**
 * detach_entity_load_avg - detach this entity from its cfs_rq load avg
 * @cfs_rq: cfs_rq to detach from
 * @se: sched_entity to detach
 *
 * Must call update_cfs_rq_load_avg() before this, since we rely on
 * cfs_rq->avg.last_update_time being current.
 */
static void detach_entity_load_avg(struct cfs_rq *cfs_rq, struct sched_entity *se)
{

	sub_positive(&cfs_rq->avg.load_avg, se->avg.load_avg);
	sub_positive(&cfs_rq->avg.load_sum, se->avg.load_sum);
	sub_positive(&cfs_rq->avg.util_avg, se->avg.util_avg);
	sub_positive(&cfs_rq->avg.util_sum, se->avg.util_sum);
	set_tg_cfs_propagate(cfs_rq);

	cfs_rq_util_change(cfs_rq);
}

/* Add the load generated by se into cfs_rq's load average */
static inline void
enqueue_entity_load_avg(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	struct sched_avg *sa = &se->avg;

	cfs_rq->runnable_load_avg += sa->load_avg;
	cfs_rq->runnable_load_sum += sa->load_sum;

	if (!sa->last_update_time) {
		attach_entity_load_avg(cfs_rq, se);
		update_tg_load_avg(cfs_rq, 0);
	}
}

/* Remove the runnable load generated by se from cfs_rq's runnable load average */
static inline void
dequeue_entity_load_avg(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	cfs_rq->runnable_load_avg =
		max_t(long, cfs_rq->runnable_load_avg - se->avg.load_avg, 0);
	cfs_rq->runnable_load_sum =
		max_t(s64,  cfs_rq->runnable_load_sum - se->avg.load_sum, 0);
}

#ifndef CONFIG_64BIT
static inline u64 cfs_rq_last_update_time(struct cfs_rq *cfs_rq)
{
	u64 last_update_time_copy;
	u64 last_update_time;

	do {
		last_update_time_copy = cfs_rq->load_last_update_time_copy;
		smp_rmb();
		last_update_time = cfs_rq->avg.last_update_time;
	} while (last_update_time != last_update_time_copy);

	return last_update_time;
}
#else
static inline u64 cfs_rq_last_update_time(struct cfs_rq *cfs_rq)
{
	return cfs_rq->avg.last_update_time;
}
#endif

/*
 * Synchronize entity load avg of dequeued entity without locking
 * the previous rq.
 */
void sync_entity_load_avg(struct sched_entity *se)
{
	struct cfs_rq *cfs_rq = cfs_rq_of(se);
	u64 last_update_time;

	last_update_time = cfs_rq_last_update_time(cfs_rq);
	__update_load_avg(last_update_time, cpu_of(rq_of(cfs_rq)), &se->avg, 0, 0, NULL);
}

/*
 * Task first catches up with cfs_rq, and then subtract
 * itself from the cfs_rq (task must be off the queue now).
 */
void remove_entity_load_avg(struct sched_entity *se)
{
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	/*
	 * tasks cannot exit without having gone through wake_up_new_task() ->
	 * post_init_entity_util_avg() which will have added things to the
	 * cfs_rq, so we can remove unconditionally.
	 *
	 * Similarly for groups, they will have passed through
	 * post_init_entity_util_avg() before unregister_sched_fair_group()
	 * calls this.
	 */

	sync_entity_load_avg(se);
	atomic_long_add(se->avg.load_avg, &cfs_rq->removed_load_avg);
	atomic_long_add(se->avg.util_avg, &cfs_rq->removed_util_avg);
}

/*
 * Update the rq's load with the elapsed running time before entering
 * idle. if the last scheduled task is not a CFS task, idle_enter will
 * be the only way to update the runnable statistic.
 */
void idle_enter_fair(struct rq *this_rq)
{
}

/*
 * Update the rq's load with the elapsed idle time before a task is
 * scheduled. if the newly scheduled task is not a CFS task, idle_exit will
 * be the only way to update the runnable statistic.
 */
void idle_exit_fair(struct rq *this_rq)
{
}

static inline unsigned long cfs_rq_runnable_load_avg(struct cfs_rq *cfs_rq)
{
	return cfs_rq->runnable_load_avg;
}

static inline unsigned long cfs_rq_load_avg(struct cfs_rq *cfs_rq)
{
	return cfs_rq->avg.load_avg;
}

static int idle_balance(struct rq *this_rq);

#else /* CONFIG_SMP */

static inline int
update_cfs_rq_load_avg(u64 now, struct cfs_rq *cfs_rq, bool update_freq)
{
	return 0;
}

#define UPDATE_TG	0x0
#define SKIP_AGE_LOAD	0x0

static inline void update_load_avg(struct sched_entity *se, int not_used1){}
static inline void
enqueue_entity_load_avg(struct cfs_rq *cfs_rq, struct sched_entity *se) {}
static inline void
dequeue_entity_load_avg(struct cfs_rq *cfs_rq, struct sched_entity *se) {}
static inline void remove_entity_load_avg(struct sched_entity *se) {}

static inline void
attach_entity_load_avg(struct cfs_rq *cfs_rq, struct sched_entity *se) {}
static inline void
detach_entity_load_avg(struct cfs_rq *cfs_rq, struct sched_entity *se) {}

static inline int idle_balance(struct rq *rq)
{
	return 0;
}

#endif /* CONFIG_SMP */

static void enqueue_sleeper(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
#ifdef CONFIG_SCHEDSTATS
	struct task_struct *tsk = NULL;

	if (entity_is_task(se))
		tsk = task_of(se);

	if (se->statistics.sleep_start) {
		u64 delta = rq_clock(rq_of(cfs_rq)) - se->statistics.sleep_start;

		if ((s64)delta < 0)
			delta = 0;

		if (unlikely(delta > se->statistics.sleep_max))
			se->statistics.sleep_max = delta;

		se->statistics.sleep_start = 0;
		se->statistics.sum_sleep_runtime += delta;

		if (tsk) {
			account_scheduler_latency(tsk, delta >> 10, 1);
			trace_sched_stat_sleep(tsk, delta);
		}
	}
	if (se->statistics.block_start) {
		u64 delta = rq_clock(rq_of(cfs_rq)) - se->statistics.block_start;

		if ((s64)delta < 0)
			delta = 0;

		if (unlikely(delta > se->statistics.block_max))
			se->statistics.block_max = delta;

		se->statistics.block_start = 0;
		se->statistics.sum_sleep_runtime += delta;

		if (tsk) {
			if (tsk->in_iowait) {
				se->statistics.iowait_sum += delta;
				se->statistics.iowait_count++;
				trace_sched_stat_iowait(tsk, delta);
			}

			trace_sched_stat_blocked(tsk, delta);
			trace_sched_blocked_reason(tsk);

			/*
			 * Blocking time is in units of nanosecs, so shift by
			 * 20 to get a milliseconds-range estimation of the
			 * amount of time that the task spent sleeping:
			 */
			if (unlikely(prof_on == SLEEP_PROFILING)) {
				profile_hits(SLEEP_PROFILING,
						(void *)get_wchan(tsk),
						delta >> 20);
			}
			account_scheduler_latency(tsk, delta >> 10, 0);
		}
	}
#endif
}

static void check_spread(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
#ifdef CONFIG_SCHED_DEBUG
	s64 d = se->vruntime - cfs_rq->min_vruntime;

	if (d < 0)
		d = -d;

	if (d > 3*sysctl_sched_latency)
		schedstat_inc(cfs_rq, nr_spread_over);
#endif
}

static void
place_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int initial)
{
	u64 vruntime = cfs_rq->min_vruntime;

	/*
	 * The 'current' period is already promised to the current tasks,
	 * however the extra weight of the new task will slow them down a
	 * little, place the new task so that it fits in the slot that
	 * stays open at the end.
	 */
	if (initial && sched_feat(START_DEBIT))
		vruntime += sched_vslice(cfs_rq, se);

	/* sleeps up to a single latency don't count. */
	if (!initial) {
		unsigned long thresh = sysctl_sched_latency;

		/*
		 * Halve their sleep time's effect, to allow
		 * for a gentler effect of sleepers:
		 */
		if (sched_feat(GENTLE_FAIR_SLEEPERS))
			thresh >>= 1;

		vruntime -= thresh;
	}

	/* ensure we never gain time by being placed backwards. */
	se->vruntime = max_vruntime(se->vruntime, vruntime);
}

static void check_enqueue_throttle(struct cfs_rq *cfs_rq);

static void
enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int flags)
{
	/*
	 * Update the normalized vruntime before updating min_vruntime
	 * through calling update_curr().
	 */
	if (!(flags & ENQUEUE_WAKEUP) || (flags & ENQUEUE_WAKING))
		se->vruntime += cfs_rq->min_vruntime;

	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);
	update_load_avg(se, UPDATE_TG);
	enqueue_entity_load_avg(cfs_rq, se);
	update_cfs_shares(se);
	account_entity_enqueue(cfs_rq, se);

	if (flags & ENQUEUE_WAKEUP) {
		place_entity(cfs_rq, se, 0);
		enqueue_sleeper(cfs_rq, se);
	}

	update_stats_enqueue(cfs_rq, se);
	check_spread(cfs_rq, se);
	if (se != cfs_rq->curr)
		__enqueue_entity(cfs_rq, se);
	se->on_rq = 1;

	if (cfs_rq->nr_running == 1) {
		list_add_leaf_cfs_rq(cfs_rq);
		check_enqueue_throttle(cfs_rq);
	}
}

static void __clear_buddies_last(struct sched_entity *se)
{
	for_each_sched_entity(se) {
		struct cfs_rq *cfs_rq = cfs_rq_of(se);
		if (cfs_rq->last != se)
			break;

		cfs_rq->last = NULL;
	}
}

static void __clear_buddies_next(struct sched_entity *se)
{
	for_each_sched_entity(se) {
		struct cfs_rq *cfs_rq = cfs_rq_of(se);
		if (cfs_rq->next != se)
			break;

		cfs_rq->next = NULL;
	}
}

static void __clear_buddies_skip(struct sched_entity *se)
{
	for_each_sched_entity(se) {
		struct cfs_rq *cfs_rq = cfs_rq_of(se);
		if (cfs_rq->skip != se)
			break;

		cfs_rq->skip = NULL;
	}
}

static void clear_buddies(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (cfs_rq->last == se)
		__clear_buddies_last(se);

	if (cfs_rq->next == se)
		__clear_buddies_next(se);

	if (cfs_rq->skip == se)
		__clear_buddies_skip(se);
}

static __always_inline void return_cfs_rq_runtime(struct cfs_rq *cfs_rq);

static void
dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int flags)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);

	/*
	 * When dequeuing a sched_entity, we must:
	 *   - Update loads to have both entity and cfs_rq synced with now.
	 *   - Substract its load from the cfs_rq->runnable_avg.
	 *   - Substract its previous weight from cfs_rq->load.weight.
	 *   - For group entity, update its weight to reflect the new share
	 *     of its group cfs_rq.
	 */
	update_load_avg(se, UPDATE_TG);
	dequeue_entity_load_avg(cfs_rq, se);

	update_stats_dequeue(cfs_rq, se);
	if (flags & DEQUEUE_SLEEP) {
#ifdef CONFIG_SCHEDSTATS
		if (entity_is_task(se)) {
			struct task_struct *tsk = task_of(se);

			if (tsk->state & TASK_INTERRUPTIBLE)
				se->statistics.sleep_start = rq_clock(rq_of(cfs_rq));
			if (tsk->state & TASK_UNINTERRUPTIBLE)
				se->statistics.block_start = rq_clock(rq_of(cfs_rq));
		}
#endif
	}

	clear_buddies(cfs_rq, se);

	if (se != cfs_rq->curr)
		__dequeue_entity(cfs_rq, se);
	se->on_rq = 0;
	account_entity_dequeue(cfs_rq, se);

	/*
	 * Normalize the entity after updating the min_vruntime because the
	 * update can refer to the ->curr item and we need to reflect this
	 * movement in our normalized position.
	 */
	if (!(flags & DEQUEUE_SLEEP))
		se->vruntime -= cfs_rq->min_vruntime;

	/* return excess runtime on last dequeue */
	return_cfs_rq_runtime(cfs_rq);

	update_min_vruntime(cfs_rq);
	update_cfs_shares(se);
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void
check_preempt_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	unsigned long ideal_runtime, delta_exec;
	struct sched_entity *se;
	s64 delta;

	ideal_runtime = sched_slice(cfs_rq, curr);
	delta_exec = curr->sum_exec_runtime - curr->prev_sum_exec_runtime;
	if (delta_exec > ideal_runtime) {
		resched_curr(rq_of(cfs_rq));
		/*
		 * The current task ran long enough, ensure it doesn't get
		 * re-elected due to buddy favours.
		 */
		clear_buddies(cfs_rq, curr);
		return;
	}

	/*
	 * Ensure that a task that missed wakeup preemption by a
	 * narrow margin doesn't have to wait for a full slice.
	 * This also mitigates buddy induced latencies under load.
	 */
	if (delta_exec < sysctl_sched_min_granularity)
		return;

	se = __pick_first_entity(cfs_rq);
	delta = curr->vruntime - se->vruntime;

	if (delta < 0)
		return;

	if (delta > ideal_runtime)
		resched_curr(rq_of(cfs_rq));
}

static void
set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/* 'current' is not kept within the tree. */
	if (se->on_rq) {
		/*
		 * Any task has to be enqueued before it get to execute on
		 * a CPU. So account for the time it spent waiting on the
		 * runqueue.
		 */
		update_stats_wait_end(cfs_rq, se);
		__dequeue_entity(cfs_rq, se);
		update_load_avg(se, UPDATE_TG);
	}

	update_stats_curr_start(cfs_rq, se);
	cfs_rq->curr = se;
#ifdef CONFIG_SCHEDSTATS
	/*
	 * Track our maximum slice length, if the CPU's load is at
	 * least twice that of our own weight (i.e. dont track it
	 * when there are only lesser-weight tasks around):
	 */
	if (rq_of(cfs_rq)->load.weight >= 2*se->load.weight) {
		se->statistics.slice_max = max(se->statistics.slice_max,
			se->sum_exec_runtime - se->prev_sum_exec_runtime);
	}
#endif
	se->prev_sum_exec_runtime = se->sum_exec_runtime;
}

static int
wakeup_preempt_entity(struct sched_entity *curr, struct sched_entity *se);

/*
 * Pick the next process, keeping these things in mind, in this order:
 * 1) keep things fair between processes/task groups
 * 2) pick the "next" process, since someone really wants that to run
 * 3) pick the "last" process, for cache locality
 * 4) do not run the "skip" process, if something else is available
 */
static struct sched_entity *
pick_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	struct sched_entity *left = __pick_first_entity(cfs_rq);
	struct sched_entity *se;

	/*
	 * If curr is set we have to see if its left of the leftmost entity
	 * still in the tree, provided there was anything in the tree at all.
	 */
	if (!left || (curr && entity_before(curr, left)))
		left = curr;

	se = left; /* ideally we run the leftmost entity */

	/*
	 * Avoid running the skip buddy, if running something else can
	 * be done without getting too unfair.
	 */
	if (cfs_rq->skip == se) {
		struct sched_entity *second;

		if (se == curr) {
			second = __pick_first_entity(cfs_rq);
		} else {
			second = __pick_next_entity(se);
			if (!second || (curr && entity_before(curr, second)))
				second = curr;
		}

		if (second && wakeup_preempt_entity(second, left) < 1)
			se = second;
	}

	/*
	 * Prefer last buddy, try to return the CPU to a preempted task.
	 */
	if (cfs_rq->last && wakeup_preempt_entity(cfs_rq->last, left) < 1)
		se = cfs_rq->last;

	/*
	 * Someone really wants this to run. If it's not unfair, run it.
	 */
	if (cfs_rq->next && wakeup_preempt_entity(cfs_rq->next, left) < 1)
		se = cfs_rq->next;

	clear_buddies(cfs_rq, se);

	return se;
}

static bool check_cfs_rq_runtime(struct cfs_rq *cfs_rq);

static void put_prev_entity(struct cfs_rq *cfs_rq, struct sched_entity *prev)
{
	/*
	 * If still on the runqueue then deactivate_task()
	 * was not called and update_curr() has to be done:
	 */
	if (prev->on_rq)
		update_curr(cfs_rq);

	/* throttle cfs_rqs exceeding runtime */
	check_cfs_rq_runtime(cfs_rq);

	check_spread(cfs_rq, prev);
	if (prev->on_rq) {
		update_stats_wait_start(cfs_rq, prev);
		/* Put 'current' back into the tree. */
		__enqueue_entity(cfs_rq, prev);
		/* in !on_rq case, update occurred at dequeue */
		update_load_avg(prev, 0);
	}
	cfs_rq->curr = NULL;
}

static void
entity_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr, int queued)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);

	/*
	 * Ensure that runnable average is periodically updated.
	 */
	update_load_avg(curr, UPDATE_TG);
	update_cfs_shares(curr);

#ifdef CONFIG_SCHED_HRTICK
	/*
	 * queued ticks are scheduled to match the slice, so don't bother
	 * validating it and just reschedule.
	 */
	if (queued) {
		resched_curr(rq_of(cfs_rq));
		return;
	}
	/*
	 * don't let the period tick interfere with the hrtick preemption
	 */
	if (!sched_feat(DOUBLE_TICK) &&
			hrtimer_active(&rq_of(cfs_rq)->hrtick_timer))
		return;
#endif

	if (cfs_rq->nr_running > 1)
		check_preempt_tick(cfs_rq, curr);
}


/**************************************************
 * CFS bandwidth control machinery
 */

#ifdef CONFIG_CFS_BANDWIDTH

#ifdef HAVE_JUMP_LABEL
static struct static_key __cfs_bandwidth_used;

static inline bool cfs_bandwidth_used(void)
{
	return static_key_false(&__cfs_bandwidth_used);
}

void cfs_bandwidth_usage_inc(void)
{
	static_key_slow_inc(&__cfs_bandwidth_used);
}

void cfs_bandwidth_usage_dec(void)
{
	static_key_slow_dec(&__cfs_bandwidth_used);
}
#else /* HAVE_JUMP_LABEL */
static bool cfs_bandwidth_used(void)
{
	return true;
}

void cfs_bandwidth_usage_inc(void) {}
void cfs_bandwidth_usage_dec(void) {}
#endif /* HAVE_JUMP_LABEL */

/*
 * default period for cfs group bandwidth.
 * default: 0.1s, units: nanoseconds
 */
static inline u64 default_cfs_period(void)
{
	return 100000000ULL;
}

static inline u64 sched_cfs_bandwidth_slice(void)
{
	return (u64)sysctl_sched_cfs_bandwidth_slice * NSEC_PER_USEC;
}

/*
 * Replenish runtime according to assigned quota and update expiration time.
 * We use sched_clock_cpu directly instead of rq->clock to avoid adding
 * additional synchronization around rq->lock.
 *
 * requires cfs_b->lock
 */
void __refill_cfs_bandwidth_runtime(struct cfs_bandwidth *cfs_b)
{
	u64 now;

	if (cfs_b->quota == RUNTIME_INF)
		return;

	now = sched_clock_cpu(smp_processor_id());
	cfs_b->runtime = cfs_b->quota;
	cfs_b->runtime_expires = now + ktime_to_ns(cfs_b->period);
}

static inline struct cfs_bandwidth *tg_cfs_bandwidth(struct task_group *tg)
{
	return &tg->cfs_bandwidth;
}

/* rq->task_clock normalized against any time this cfs_rq has spent throttled */
static inline u64 cfs_rq_clock_task(struct cfs_rq *cfs_rq)
{
	if (unlikely(cfs_rq->throttle_count))
		return cfs_rq->throttled_clock_task;

	return rq_clock_task(rq_of(cfs_rq)) - cfs_rq->throttled_clock_task_time;
}

/* returns 0 on failure to allocate runtime */
static int assign_cfs_rq_runtime(struct cfs_rq *cfs_rq)
{
	struct task_group *tg = cfs_rq->tg;
	struct cfs_bandwidth *cfs_b = tg_cfs_bandwidth(tg);
	u64 amount = 0, min_amount, expires;

	/* note: this is a positive sum as runtime_remaining <= 0 */
	min_amount = sched_cfs_bandwidth_slice() - cfs_rq->runtime_remaining;

	raw_spin_lock(&cfs_b->lock);
	if (cfs_b->quota == RUNTIME_INF)
		amount = min_amount;
	else {
		start_cfs_bandwidth(cfs_b);

		if (cfs_b->runtime > 0) {
			amount = min(cfs_b->runtime, min_amount);
			cfs_b->runtime -= amount;
			cfs_b->idle = 0;
		}
	}
	expires = cfs_b->runtime_expires;
	raw_spin_unlock(&cfs_b->lock);

	cfs_rq->runtime_remaining += amount;
	/*
	 * we may have advanced our local expiration to account for allowed
	 * spread between our sched_clock and the one on which runtime was
	 * issued.
	 */
	if ((s64)(expires - cfs_rq->runtime_expires) > 0)
		cfs_rq->runtime_expires = expires;

	return cfs_rq->runtime_remaining > 0;
}

/*
 * Note: This depends on the synchronization provided by sched_clock and the
 * fact that rq->clock snapshots this value.
 */
static void expire_cfs_rq_runtime(struct cfs_rq *cfs_rq)
{
	struct cfs_bandwidth *cfs_b = tg_cfs_bandwidth(cfs_rq->tg);

	/* if the deadline is ahead of our clock, nothing to do */
	if (likely((s64)(rq_clock(rq_of(cfs_rq)) - cfs_rq->runtime_expires) < 0))
		return;

	if (cfs_rq->runtime_remaining < 0)
		return;

	/*
	 * If the local deadline has passed we have to consider the
	 * possibility that our sched_clock is 'fast' and the global deadline
	 * has not truly expired.
	 *
	 * Fortunately we can check determine whether this the case by checking
	 * whether the global deadline has advanced. It is valid to compare
	 * cfs_b->runtime_expires without any locks since we only care about
	 * exact equality, so a partial write will still work.
	 */

	if (cfs_rq->runtime_expires != cfs_b->runtime_expires) {
		/* extend local deadline, drift is bounded above by 2 ticks */
		cfs_rq->runtime_expires += TICK_NSEC;
	} else {
		/* global deadline is ahead, expiration has passed */
		cfs_rq->runtime_remaining = 0;
	}
}

static void __account_cfs_rq_runtime(struct cfs_rq *cfs_rq, u64 delta_exec)
{
	/* dock delta_exec before expiring quota (as it could span periods) */
	cfs_rq->runtime_remaining -= delta_exec;
	expire_cfs_rq_runtime(cfs_rq);

	if (likely(cfs_rq->runtime_remaining > 0))
		return;

	/*
	 * if we're unable to extend our runtime we resched so that the active
	 * hierarchy can be throttled
	 */
	if (!assign_cfs_rq_runtime(cfs_rq) && likely(cfs_rq->curr))
		resched_curr(rq_of(cfs_rq));
}

static __always_inline
void account_cfs_rq_runtime(struct cfs_rq *cfs_rq, u64 delta_exec)
{
	if (!cfs_bandwidth_used() || !cfs_rq->runtime_enabled)
		return;

	__account_cfs_rq_runtime(cfs_rq, delta_exec);
}

static inline int cfs_rq_throttled(struct cfs_rq *cfs_rq)
{
	return cfs_bandwidth_used() && cfs_rq->throttled;
}

/* check whether cfs_rq, or any parent, is throttled */
static inline int throttled_hierarchy(struct cfs_rq *cfs_rq)
{
	return cfs_bandwidth_used() && cfs_rq->throttle_count;
}

/*
 * Ensure that neither of the group entities corresponding to src_cpu or
 * dest_cpu are members of a throttled hierarchy when performing group
 * load-balance operations.
 */
static inline int throttled_lb_pair(struct task_group *tg,
				    int src_cpu, int dest_cpu)
{
	struct cfs_rq *src_cfs_rq, *dest_cfs_rq;

	src_cfs_rq = tg->cfs_rq[src_cpu];
	dest_cfs_rq = tg->cfs_rq[dest_cpu];

	return throttled_hierarchy(src_cfs_rq) ||
	       throttled_hierarchy(dest_cfs_rq);
}

/* updated child weight may affect parent so we have to do this bottom up */
static int tg_unthrottle_up(struct task_group *tg, void *data)
{
	struct rq *rq = data;
	struct cfs_rq *cfs_rq = tg->cfs_rq[cpu_of(rq)];

	cfs_rq->throttle_count--;
#ifdef CONFIG_SMP
	if (!cfs_rq->throttle_count) {
		/* adjust cfs_rq_clock_task() */
		cfs_rq->throttled_clock_task_time += rq_clock_task(rq) -
					     cfs_rq->throttled_clock_task;
	}
#endif

	return 0;
}

static int tg_throttle_down(struct task_group *tg, void *data)
{
	struct rq *rq = data;
	struct cfs_rq *cfs_rq = tg->cfs_rq[cpu_of(rq)];

	/* group is entering throttled state, stop time */
	if (!cfs_rq->throttle_count)
		cfs_rq->throttled_clock_task = rq_clock_task(rq);
	cfs_rq->throttle_count++;

	return 0;
}

static void throttle_cfs_rq(struct cfs_rq *cfs_rq)
{
	struct rq *rq = rq_of(cfs_rq);
	struct cfs_bandwidth *cfs_b = tg_cfs_bandwidth(cfs_rq->tg);
	struct sched_entity *se;
	long task_delta, dequeue = 1;
	bool empty;

	se = cfs_rq->tg->se[cpu_of(rq_of(cfs_rq))];

	/* freeze hierarchy runnable averages while throttled */
	rcu_read_lock();
	walk_tg_tree_from(cfs_rq->tg, tg_throttle_down, tg_nop, (void *)rq);
	rcu_read_unlock();

	task_delta = cfs_rq->h_nr_running;
	for_each_sched_entity(se) {
		struct cfs_rq *qcfs_rq = cfs_rq_of(se);
		/* throttled entity or throttle-on-deactivate */
		if (!se->on_rq)
			break;

		if (dequeue)
			dequeue_entity(qcfs_rq, se, DEQUEUE_SLEEP);
		qcfs_rq->h_nr_running -= task_delta;

		if (qcfs_rq->load.weight)
			dequeue = 0;
	}

	if (!se)
		sub_nr_running(rq, task_delta);

	cfs_rq->throttled = 1;
	cfs_rq->throttled_clock = rq_clock(rq);
	raw_spin_lock(&cfs_b->lock);
	empty = list_empty(&cfs_b->throttled_cfs_rq);

	/*
	 * Add to the _head_ of the list, so that an already-started
	 * distribute_cfs_runtime will not see us. If disribute_cfs_runtime is
	 * not running add to the tail so that later runqueues don't get starved.
	 */
	if (cfs_b->distribute_running)
		list_add_rcu(&cfs_rq->throttled_list, &cfs_b->throttled_cfs_rq);
	else
		list_add_tail_rcu(&cfs_rq->throttled_list, &cfs_b->throttled_cfs_rq);

	/*
	 * If we're the first throttled task, make sure the bandwidth
	 * timer is running.
	 */
	if (empty)
		start_cfs_bandwidth(cfs_b);

	raw_spin_unlock(&cfs_b->lock);
}

void unthrottle_cfs_rq(struct cfs_rq *cfs_rq)
{
	struct rq *rq = rq_of(cfs_rq);
	struct cfs_bandwidth *cfs_b = tg_cfs_bandwidth(cfs_rq->tg);
	struct sched_entity *se;
	int enqueue = 1;
	long task_delta;

	se = cfs_rq->tg->se[cpu_of(rq)];

	cfs_rq->throttled = 0;

	update_rq_clock(rq);

	raw_spin_lock(&cfs_b->lock);
	cfs_b->throttled_time += rq_clock(rq) - cfs_rq->throttled_clock;
	list_del_rcu(&cfs_rq->throttled_list);
	raw_spin_unlock(&cfs_b->lock);

	/* update hierarchical throttle state */
	walk_tg_tree_from(cfs_rq->tg, tg_nop, tg_unthrottle_up, (void *)rq);

	if (!cfs_rq->load.weight)
		return;

	task_delta = cfs_rq->h_nr_running;
	for_each_sched_entity(se) {
		if (se->on_rq)
			enqueue = 0;

		cfs_rq = cfs_rq_of(se);
		if (enqueue)
			enqueue_entity(cfs_rq, se, ENQUEUE_WAKEUP);
		cfs_rq->h_nr_running += task_delta;

		if (cfs_rq_throttled(cfs_rq))
			break;
	}

	if (!se)
		add_nr_running(rq, task_delta);

	/* determine whether we need to wake up potentially idle cpu */
	if (rq->curr == rq->idle && rq->cfs.nr_running)
		resched_curr(rq);
}

static u64 distribute_cfs_runtime(struct cfs_bandwidth *cfs_b,
		u64 remaining, u64 expires)
{
	struct cfs_rq *cfs_rq;
	u64 runtime;
	u64 starting_runtime = remaining;

	rcu_read_lock();
	list_for_each_entry_rcu(cfs_rq, &cfs_b->throttled_cfs_rq,
				throttled_list) {
		struct rq *rq = rq_of(cfs_rq);

		raw_spin_lock(&rq->lock);
		if (!cfs_rq_throttled(cfs_rq))
			goto next;

		runtime = -cfs_rq->runtime_remaining + 1;
		if (runtime > remaining)
			runtime = remaining;
		remaining -= runtime;

		cfs_rq->runtime_remaining += runtime;
		cfs_rq->runtime_expires = expires;

		/* we check whether we're throttled above */
		if (cfs_rq->runtime_remaining > 0)
			unthrottle_cfs_rq(cfs_rq);

next:
		raw_spin_unlock(&rq->lock);

		if (!remaining)
			break;
	}
	rcu_read_unlock();

	return starting_runtime - remaining;
}

/*
 * Responsible for refilling a task_group's bandwidth and unthrottling its
 * cfs_rqs as appropriate. If there has been no activity within the last
 * period the timer is deactivated until scheduling resumes; cfs_b->idle is
 * used to track this state.
 */
static int do_sched_cfs_period_timer(struct cfs_bandwidth *cfs_b, int overrun)
{
	u64 runtime, runtime_expires;
	int throttled;

	/* no need to continue the timer with no bandwidth constraint */
	if (cfs_b->quota == RUNTIME_INF)
		goto out_deactivate;

	throttled = !list_empty(&cfs_b->throttled_cfs_rq);
	cfs_b->nr_periods += overrun;

	/*
	 * idle depends on !throttled (for the case of a large deficit), and if
	 * we're going inactive then everything else can be deferred
	 */
	if (cfs_b->idle && !throttled)
		goto out_deactivate;

	__refill_cfs_bandwidth_runtime(cfs_b);

	if (!throttled) {
		/* mark as potentially idle for the upcoming period */
		cfs_b->idle = 1;
		return 0;
	}

	/* account preceding periods in which throttling occurred */
	cfs_b->nr_throttled += overrun;

	runtime_expires = cfs_b->runtime_expires;

	/*
	 * This check is repeated as we are holding onto the new bandwidth while
	 * we unthrottle. This can potentially race with an unthrottled group
	 * trying to acquire new bandwidth from the global pool. This can result
	 * in us over-using our runtime if it is all used during this loop, but
	 * only by limited amounts in that extreme case.
	 */
	while (throttled && cfs_b->runtime > 0 && !cfs_b->distribute_running) {
		runtime = cfs_b->runtime;
		cfs_b->distribute_running = 1;
		raw_spin_unlock(&cfs_b->lock);
		/* we can't nest cfs_b->lock while distributing bandwidth */
		runtime = distribute_cfs_runtime(cfs_b, runtime,
						 runtime_expires);
		raw_spin_lock(&cfs_b->lock);

		cfs_b->distribute_running = 0;
		throttled = !list_empty(&cfs_b->throttled_cfs_rq);

		cfs_b->runtime -= min(runtime, cfs_b->runtime);
	}

	/*
	 * While we are ensured activity in the period following an
	 * unthrottle, this also covers the case in which the new bandwidth is
	 * insufficient to cover the existing bandwidth deficit.  (Forcing the
	 * timer to remain active while there are any throttled entities.)
	 */
	cfs_b->idle = 0;

	return 0;

out_deactivate:
	return 1;
}

/* a cfs_rq won't donate quota below this amount */
static const u64 min_cfs_rq_runtime = 1 * NSEC_PER_MSEC;
/* minimum remaining period time to redistribute slack quota */
static const u64 min_bandwidth_expiration = 2 * NSEC_PER_MSEC;
/* how long we wait to gather additional slack before distributing */
static const u64 cfs_bandwidth_slack_period = 5 * NSEC_PER_MSEC;

/*
 * Are we near the end of the current quota period?
 *
 * Requires cfs_b->lock for hrtimer_expires_remaining to be safe against the
 * hrtimer base being cleared by hrtimer_start. In the case of
 * migrate_hrtimers, base is never cleared, so we are fine.
 */
static int runtime_refresh_within(struct cfs_bandwidth *cfs_b, u64 min_expire)
{
	struct hrtimer *refresh_timer = &cfs_b->period_timer;
	u64 remaining;

	/* if the call-back is running a quota refresh is already occurring */
	if (hrtimer_callback_running(refresh_timer))
		return 1;

	/* is a quota refresh about to occur? */
	remaining = ktime_to_ns(hrtimer_expires_remaining(refresh_timer));
	if (remaining < min_expire)
		return 1;

	return 0;
}

static void start_cfs_slack_bandwidth(struct cfs_bandwidth *cfs_b)
{
	u64 min_left = cfs_bandwidth_slack_period + min_bandwidth_expiration;

	/* if there's a quota refresh soon don't bother with slack */
	if (runtime_refresh_within(cfs_b, min_left))
		return;

	hrtimer_start(&cfs_b->slack_timer,
			ns_to_ktime(cfs_bandwidth_slack_period),
			HRTIMER_MODE_REL);
}

/* we know any runtime found here is valid as update_curr() precedes return */
static void __return_cfs_rq_runtime(struct cfs_rq *cfs_rq)
{
	struct cfs_bandwidth *cfs_b = tg_cfs_bandwidth(cfs_rq->tg);
	s64 slack_runtime = cfs_rq->runtime_remaining - min_cfs_rq_runtime;

	if (slack_runtime <= 0)
		return;

	raw_spin_lock(&cfs_b->lock);
	if (cfs_b->quota != RUNTIME_INF &&
	    cfs_rq->runtime_expires == cfs_b->runtime_expires) {
		cfs_b->runtime += slack_runtime;

		/* we are under rq->lock, defer unthrottling using a timer */
		if (cfs_b->runtime > sched_cfs_bandwidth_slice() &&
		    !list_empty(&cfs_b->throttled_cfs_rq))
			start_cfs_slack_bandwidth(cfs_b);
	}
	raw_spin_unlock(&cfs_b->lock);

	/* even if it's not valid for return we don't want to try again */
	cfs_rq->runtime_remaining -= slack_runtime;
}

static __always_inline void return_cfs_rq_runtime(struct cfs_rq *cfs_rq)
{
	if (!cfs_bandwidth_used())
		return;

	if (!cfs_rq->runtime_enabled || cfs_rq->nr_running)
		return;

	__return_cfs_rq_runtime(cfs_rq);
}

/*
 * This is done with a timer (instead of inline with bandwidth return) since
 * it's necessary to juggle rq->locks to unthrottle their respective cfs_rqs.
 */
static void do_sched_cfs_slack_timer(struct cfs_bandwidth *cfs_b)
{
	u64 runtime = 0, slice = sched_cfs_bandwidth_slice();
	u64 expires;

	/* confirm we're still not at a refresh boundary */
	raw_spin_lock(&cfs_b->lock);
	if (cfs_b->distribute_running) {
		raw_spin_unlock(&cfs_b->lock);
		return;
	}

	if (runtime_refresh_within(cfs_b, min_bandwidth_expiration)) {
		raw_spin_unlock(&cfs_b->lock);
		return;
	}

	if (cfs_b->quota != RUNTIME_INF && cfs_b->runtime > slice)
		runtime = cfs_b->runtime;

	expires = cfs_b->runtime_expires;
	if (runtime)
		cfs_b->distribute_running = 1;

	raw_spin_unlock(&cfs_b->lock);

	if (!runtime)
		return;

	runtime = distribute_cfs_runtime(cfs_b, runtime, expires);

	raw_spin_lock(&cfs_b->lock);
	if (expires == cfs_b->runtime_expires)
		cfs_b->runtime -= min(runtime, cfs_b->runtime);
	cfs_b->distribute_running = 0;
	raw_spin_unlock(&cfs_b->lock);
}

/*
 * When a group wakes up we want to make sure that its quota is not already
 * expired/exceeded, otherwise it may be allowed to steal additional ticks of
 * runtime as update_curr() throttling can not not trigger until it's on-rq.
 */
static void check_enqueue_throttle(struct cfs_rq *cfs_rq)
{
	if (!cfs_bandwidth_used())
		return;

	/* Synchronize hierarchical throttle counter: */
	if (unlikely(!cfs_rq->throttle_uptodate)) {
		struct rq *rq = rq_of(cfs_rq);
		struct cfs_rq *pcfs_rq;
		struct task_group *tg;

		cfs_rq->throttle_uptodate = 1;

		/* Get closest up-to-date node, because leaves go first: */
		for (tg = cfs_rq->tg->parent; tg; tg = tg->parent) {
			pcfs_rq = tg->cfs_rq[cpu_of(rq)];
			if (pcfs_rq->throttle_uptodate)
				break;
		}
		if (tg) {
			cfs_rq->throttle_count = pcfs_rq->throttle_count;
			cfs_rq->throttled_clock_task = rq_clock_task(rq);
		}
	}

	/* an active group must be handled by the update_curr()->put() path */
	if (!cfs_rq->runtime_enabled || cfs_rq->curr)
		return;

	/* ensure the group is not already throttled */
	if (cfs_rq_throttled(cfs_rq))
		return;

	/* update runtime allocation */
	account_cfs_rq_runtime(cfs_rq, 0);
	if (cfs_rq->runtime_remaining <= 0)
		throttle_cfs_rq(cfs_rq);
}

/* conditionally throttle active cfs_rq's from put_prev_entity() */
static bool check_cfs_rq_runtime(struct cfs_rq *cfs_rq)
{
	if (!cfs_bandwidth_used())
		return false;

	if (likely(!cfs_rq->runtime_enabled || cfs_rq->runtime_remaining > 0))
		return false;

	/*
	 * it's possible for a throttled entity to be forced into a running
	 * state (e.g. set_curr_task), in this case we're finished.
	 */
	if (cfs_rq_throttled(cfs_rq))
		return true;

	throttle_cfs_rq(cfs_rq);
	return true;
}

static enum hrtimer_restart sched_cfs_slack_timer(struct hrtimer *timer)
{
	struct cfs_bandwidth *cfs_b =
		container_of(timer, struct cfs_bandwidth, slack_timer);

	do_sched_cfs_slack_timer(cfs_b);

	return HRTIMER_NORESTART;
}

static enum hrtimer_restart sched_cfs_period_timer(struct hrtimer *timer)
{
	struct cfs_bandwidth *cfs_b =
		container_of(timer, struct cfs_bandwidth, period_timer);
	int overrun;
	int idle = 0;

	raw_spin_lock(&cfs_b->lock);
	for (;;) {
		overrun = hrtimer_forward_now(timer, cfs_b->period);
		if (!overrun)
			break;

		idle = do_sched_cfs_period_timer(cfs_b, overrun);
	}
	if (idle)
		cfs_b->period_active = 0;
	raw_spin_unlock(&cfs_b->lock);

	return idle ? HRTIMER_NORESTART : HRTIMER_RESTART;
}

void init_cfs_bandwidth(struct cfs_bandwidth *cfs_b)
{
	raw_spin_lock_init(&cfs_b->lock);
	cfs_b->runtime = 0;
	cfs_b->quota = RUNTIME_INF;
	cfs_b->period = ns_to_ktime(default_cfs_period());

	INIT_LIST_HEAD(&cfs_b->throttled_cfs_rq);
	hrtimer_init(&cfs_b->period_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);
	cfs_b->period_timer.function = sched_cfs_period_timer;
	hrtimer_init(&cfs_b->slack_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cfs_b->slack_timer.function = sched_cfs_slack_timer;
	cfs_b->distribute_running = 0;
}

static void init_cfs_rq_runtime(struct cfs_rq *cfs_rq)
{
	cfs_rq->runtime_enabled = 0;
	INIT_LIST_HEAD(&cfs_rq->throttled_list);
}

void start_cfs_bandwidth(struct cfs_bandwidth *cfs_b)
{
	lockdep_assert_held(&cfs_b->lock);

	if (!cfs_b->period_active) {
		cfs_b->period_active = 1;
		hrtimer_forward_now(&cfs_b->period_timer, cfs_b->period);
		hrtimer_start_expires(&cfs_b->period_timer, HRTIMER_MODE_ABS_PINNED);
	}
}

static void destroy_cfs_bandwidth(struct cfs_bandwidth *cfs_b)
{
	/* init_cfs_bandwidth() was not called */
	if (!cfs_b->throttled_cfs_rq.next)
		return;

	hrtimer_cancel(&cfs_b->period_timer);
	hrtimer_cancel(&cfs_b->slack_timer);
}

static void __maybe_unused update_runtime_enabled(struct rq *rq)
{
	struct cfs_rq *cfs_rq;

	for_each_leaf_cfs_rq(rq, cfs_rq) {
		struct cfs_bandwidth *cfs_b = &cfs_rq->tg->cfs_bandwidth;

		raw_spin_lock(&cfs_b->lock);
		cfs_rq->runtime_enabled = cfs_b->quota != RUNTIME_INF;
		raw_spin_unlock(&cfs_b->lock);
	}
}

static void __maybe_unused unthrottle_offline_cfs_rqs(struct rq *rq)
{
	struct cfs_rq *cfs_rq;

	for_each_leaf_cfs_rq(rq, cfs_rq) {
		if (!cfs_rq->runtime_enabled)
			continue;

		/*
		 * clock_task is not advancing so we just need to make sure
		 * there's some valid quota amount
		 */
		cfs_rq->runtime_remaining = 1;
		/*
		 * Offline rq is schedulable till cpu is completely disabled
		 * in take_cpu_down(), so we prevent new cfs throttling here.
		 */
		cfs_rq->runtime_enabled = 0;

		if (cfs_rq_throttled(cfs_rq))
			unthrottle_cfs_rq(cfs_rq);
	}
}

#else /* CONFIG_CFS_BANDWIDTH */
static inline u64 cfs_rq_clock_task(struct cfs_rq *cfs_rq)
{
	return rq_clock_task(rq_of(cfs_rq));
}

static void account_cfs_rq_runtime(struct cfs_rq *cfs_rq, u64 delta_exec) {}
static bool check_cfs_rq_runtime(struct cfs_rq *cfs_rq) { return false; }
static void check_enqueue_throttle(struct cfs_rq *cfs_rq) {}
static __always_inline void return_cfs_rq_runtime(struct cfs_rq *cfs_rq) {}

static inline int cfs_rq_throttled(struct cfs_rq *cfs_rq)
{
	return 0;
}

static inline int throttled_hierarchy(struct cfs_rq *cfs_rq)
{
	return 0;
}

static inline int throttled_lb_pair(struct task_group *tg,
				    int src_cpu, int dest_cpu)
{
	return 0;
}

void init_cfs_bandwidth(struct cfs_bandwidth *cfs_b) {}

#ifdef CONFIG_FAIR_GROUP_SCHED
static void init_cfs_rq_runtime(struct cfs_rq *cfs_rq) {}
#endif

static inline struct cfs_bandwidth *tg_cfs_bandwidth(struct task_group *tg)
{
	return NULL;
}
static inline void destroy_cfs_bandwidth(struct cfs_bandwidth *cfs_b) {}
static inline void update_runtime_enabled(struct rq *rq) {}
static inline void unthrottle_offline_cfs_rqs(struct rq *rq) {}

#endif /* CONFIG_CFS_BANDWIDTH */

/**************************************************
 * CFS operations on tasks:
 */

#ifdef CONFIG_SCHED_HRTICK
static void hrtick_start_fair(struct rq *rq, struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	WARN_ON(task_rq(p) != rq);

	if (cfs_rq->nr_running > 1) {
		u64 slice = sched_slice(cfs_rq, se);
		u64 ran = se->sum_exec_runtime - se->prev_sum_exec_runtime;
		s64 delta = slice - ran;

		if (delta < 0) {
			if (rq->curr == p)
				resched_curr(rq);
			return;
		}
		hrtick_start(rq, delta);
	}
}

/*
 * called from enqueue/dequeue and updates the hrtick when the
 * current task is from our class and nr_running is low enough
 * to matter.
 */
static void hrtick_update(struct rq *rq)
{
	struct task_struct *curr = rq->curr;

	if (!hrtick_enabled(rq) || curr->sched_class != &fair_sched_class)
		return;

	if (cfs_rq_of(&curr->se)->nr_running < sched_nr_latency)
		hrtick_start_fair(rq, curr);
}
#else /* !CONFIG_SCHED_HRTICK */
static inline void
hrtick_start_fair(struct rq *rq, struct task_struct *p)
{
}

static inline void hrtick_update(struct rq *rq)
{
}
#endif

#ifdef CONFIG_SMP
static bool __cpu_overutilized(int cpu, int delta);
static bool cpu_overutilized(int cpu);
unsigned long boosted_cpu_util(int cpu);
#else
#define boosted_cpu_util(cpu) cpu_util_freq(cpu)
#endif

/*
 * The enqueue_task method is called before nr_running is
 * increased. Here we update the fair scheduling stats and
 * then put the task into the rbtree:
 */
static void
enqueue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;
#ifdef CONFIG_SMP
	int task_new = flags & ENQUEUE_WAKEUP_NEW;
#endif

	/*
	 * If in_iowait is set, the code below may not trigger any cpufreq
	 * utilization updates, so do it here explicitly with the IOWAIT flag
	 * passed.
	 */
	if (p->in_iowait)
		cpufreq_update_this_cpu(rq, SCHED_CPUFREQ_IOWAIT);

	for_each_sched_entity(se) {
		if (se->on_rq)
			break;
		cfs_rq = cfs_rq_of(se);
		enqueue_entity(cfs_rq, se, flags);

		/*
		 * end evaluation on encountering a throttled cfs_rq
		 *
		 * note: in the case of encountering a throttled cfs_rq we will
		 * post the final h_nr_running increment below.
		 */
		if (cfs_rq_throttled(cfs_rq))
			break;
		cfs_rq->h_nr_running++;
		walt_inc_cfs_cumulative_runnable_avg(cfs_rq, p);

		flags = ENQUEUE_WAKEUP;
	}

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		cfs_rq->h_nr_running++;
		walt_inc_cfs_cumulative_runnable_avg(cfs_rq, p);

		if (cfs_rq_throttled(cfs_rq))
			break;

		update_load_avg(se, UPDATE_TG);
		update_cfs_shares(se);
	}

	if (!se)
		add_nr_running(rq, 1);

#ifdef CONFIG_SMP

	/*
	 * Update SchedTune accounting.
	 *
	 * We do it before updating the CPU capacity to ensure the
	 * boost value of the current task is accounted for in the
	 * selection of the OPP.
	 *
	 * We do it also in the case where we enqueue a throttled task;
	 * we could argue that a throttled task should not boost a CPU,
	 * however:
	 * a) properly implementing CPU boosting considering throttled
	 *    tasks will increase a lot the complexity of the solution
	 * b) it's not easy to quantify the benefits introduced by
	 *    such a more complex solution.
	 * Thus, for the time being we go for the simple solution and boost
	 * also for throttled RQs.
	 */
	schedtune_enqueue_task(p, cpu_of(rq));

	if (!se) {
		walt_inc_cumulative_runnable_avg(rq, p);
		if (!task_new && !rq->rd->overutilized &&
		    cpu_overutilized(rq->cpu)) {
			rq->rd->overutilized = true;
			trace_sched_overutilized(true);
		}
	}

#endif /* CONFIG_SMP */
	hrtick_update(rq);
}

static void set_next_buddy(struct sched_entity *se);

/*
 * The dequeue_task method is called before nr_running is
 * decreased. We remove the task from the rbtree and
 * update the fair scheduling stats:
 */
static void dequeue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;
	int task_sleep = flags & DEQUEUE_SLEEP;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		dequeue_entity(cfs_rq, se, flags);

		/*
		 * end evaluation on encountering a throttled cfs_rq
		 *
		 * note: in the case of encountering a throttled cfs_rq we will
		 * post the final h_nr_running decrement below.
		*/
		if (cfs_rq_throttled(cfs_rq))
			break;
		cfs_rq->h_nr_running--;
		walt_dec_cfs_cumulative_runnable_avg(cfs_rq, p);

		/* Don't dequeue parent if it has other entities besides us */
		if (cfs_rq->load.weight) {
			/* Avoid re-evaluating load for this entity: */
			se = parent_entity(se);
			/*
			 * Bias pick_next to pick a task from this cfs_rq, as
			 * p is sleeping when it is within its sched_slice.
			 */
			if (task_sleep && se && !throttled_hierarchy(cfs_rq))
				set_next_buddy(se);
			break;
		}
		flags |= DEQUEUE_SLEEP;
	}

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		cfs_rq->h_nr_running--;
		walt_dec_cfs_cumulative_runnable_avg(cfs_rq, p);

		if (cfs_rq_throttled(cfs_rq))
			break;

		update_load_avg(se, UPDATE_TG);
		update_cfs_shares(se);
	}

	if (!se)
		sub_nr_running(rq, 1);

#ifdef CONFIG_SMP

	/*
	 * Update SchedTune accounting
	 *
	 * We do it before updating the CPU capacity to ensure the
	 * boost value of the current task is accounted for in the
	 * selection of the OPP.
	 */
	schedtune_dequeue_task(p, cpu_of(rq));

	if (!se)
		walt_dec_cumulative_runnable_avg(rq, p);
#endif /* CONFIG_SMP */

	hrtick_update(rq);
}

#ifdef CONFIG_SMP

/*
 * per rq 'load' arrray crap; XXX kill this.
 */

/*
 * The exact cpuload at various idx values, calculated at every tick would be
 * load = (2^idx - 1) / 2^idx * load + 1 / 2^idx * cur_load
 *
 * If a cpu misses updates for n-1 ticks (as it was idle) and update gets called
 * on nth tick when cpu may be busy, then we have:
 * load = ((2^idx - 1) / 2^idx)^(n-1) * load
 * load = (2^idx - 1) / 2^idx) * load + 1 / 2^idx * cur_load
 *
 * decay_load_missed() below does efficient calculation of
 * load = ((2^idx - 1) / 2^idx)^(n-1) * load
 * avoiding 0..n-1 loop doing load = ((2^idx - 1) / 2^idx) * load
 *
 * The calculation is approximated on a 128 point scale.
 * degrade_zero_ticks is the number of ticks after which load at any
 * particular idx is approximated to be zero.
 * degrade_factor is a precomputed table, a row for each load idx.
 * Each column corresponds to degradation factor for a power of two ticks,
 * based on 128 point scale.
 * Example:
 * row 2, col 3 (=12) says that the degradation at load idx 2 after
 * 8 ticks is 12/128 (which is an approximation of exact factor 3^8/4^8).
 *
 * With this power of 2 load factors, we can degrade the load n times
 * by looking at 1 bits in n and doing as many mult/shift instead of
 * n mult/shifts needed by the exact degradation.
 */
#define DEGRADE_SHIFT		7
static const unsigned char
		degrade_zero_ticks[CPU_LOAD_IDX_MAX] = {0, 8, 32, 64, 128};
static const unsigned char
		degrade_factor[CPU_LOAD_IDX_MAX][DEGRADE_SHIFT + 1] = {
					{0, 0, 0, 0, 0, 0, 0, 0},
					{64, 32, 8, 0, 0, 0, 0, 0},
					{96, 72, 40, 12, 1, 0, 0},
					{112, 98, 75, 43, 15, 1, 0},
					{120, 112, 98, 76, 45, 16, 2} };

/*
 * Update cpu_load for any missed ticks, due to tickless idle. The backlog
 * would be when CPU is idle and so we just decay the old load without
 * adding any new load.
 */
static unsigned long
decay_load_missed(unsigned long load, unsigned long missed_updates, int idx)
{
	int j = 0;

	if (!missed_updates)
		return load;

	if (missed_updates >= degrade_zero_ticks[idx])
		return 0;

	if (idx == 1)
		return load >> missed_updates;

	while (missed_updates) {
		if (missed_updates % 2)
			load = (load * degrade_factor[idx][j]) >> DEGRADE_SHIFT;

		missed_updates >>= 1;
		j++;
	}
	return load;
}

/*
 * Update rq->cpu_load[] statistics. This function is usually called every
 * scheduler tick (TICK_NSEC). With tickless idle this will not be called
 * every tick. We fix it up based on jiffies.
 */
static void __update_cpu_load(struct rq *this_rq, unsigned long this_load,
			      unsigned long pending_updates)
{
	int i, scale;

	this_rq->nr_load_updates++;

	/* Update our load: */
	this_rq->cpu_load[0] = this_load; /* Fasttrack for idx 0 */
	for (i = 1, scale = 2; i < CPU_LOAD_IDX_MAX; i++, scale += scale) {
		unsigned long old_load, new_load;

		/* scale is effectively 1 << i now, and >> i divides by scale */

		old_load = this_rq->cpu_load[i];
		old_load = decay_load_missed(old_load, pending_updates - 1, i);
		new_load = this_load;
		/*
		 * Round up the averaging division if load is increasing. This
		 * prevents us from getting stuck on 9 if the load is 10, for
		 * example.
		 */
		if (new_load > old_load)
			new_load += scale - 1;

		this_rq->cpu_load[i] = (old_load * (scale - 1) + new_load) >> i;
	}

	sched_avg_update(this_rq);
}

/* Used instead of source_load when we know the type == 0 */
static unsigned long weighted_cpuload(const int cpu)
{
	return cfs_rq_runnable_load_avg(&cpu_rq(cpu)->cfs);
}

#ifdef CONFIG_NO_HZ_COMMON
/*
 * There is no sane way to deal with nohz on smp when using jiffies because the
 * cpu doing the jiffies update might drift wrt the cpu doing the jiffy reading
 * causing off-by-one errors in observed deltas; {0,2} instead of {1,1}.
 *
 * Therefore we cannot use the delta approach from the regular tick since that
 * would seriously skew the load calculation. However we'll make do for those
 * updates happening while idle (nohz_idle_balance) or coming out of idle
 * (tick_nohz_idle_exit).
 *
 * This means we might still be one tick off for nohz periods.
 */

/*
 * Called from nohz_idle_balance() to update the load ratings before doing the
 * idle balance.
 */
static void update_idle_cpu_load(struct rq *this_rq)
{
	unsigned long curr_jiffies = READ_ONCE(jiffies);
	unsigned long load = weighted_cpuload(cpu_of(this_rq));
	unsigned long pending_updates;

	/*
	 * bail if there's load or we're actually up-to-date.
	 */
	if (load || curr_jiffies == this_rq->last_load_update_tick)
		return;

	pending_updates = curr_jiffies - this_rq->last_load_update_tick;
	this_rq->last_load_update_tick = curr_jiffies;

	__update_cpu_load(this_rq, load, pending_updates);
}

/*
 * Called from tick_nohz_idle_exit() -- try and fix up the ticks we missed.
 */
void update_cpu_load_nohz(void)
{
	struct rq *this_rq = this_rq();
	unsigned long curr_jiffies = READ_ONCE(jiffies);
	unsigned long pending_updates;

	if (curr_jiffies == this_rq->last_load_update_tick)
		return;

	raw_spin_lock(&this_rq->lock);
	pending_updates = curr_jiffies - this_rq->last_load_update_tick;
	if (pending_updates) {
		this_rq->last_load_update_tick = curr_jiffies;
		/*
		 * We were idle, this means load 0, the current load might be
		 * !0 due to remote wakeups and the sort.
		 */
		__update_cpu_load(this_rq, 0, pending_updates);
	}
	raw_spin_unlock(&this_rq->lock);
}
#endif /* CONFIG_NO_HZ */

/*
 * Called from scheduler_tick()
 */
void update_cpu_load_active(struct rq *this_rq)
{
	unsigned long load = weighted_cpuload(cpu_of(this_rq));
	/*
	 * See the mess around update_idle_cpu_load() / update_cpu_load_nohz().
	 */
	this_rq->last_load_update_tick = jiffies;
	__update_cpu_load(this_rq, load, 1);
}

/*
 * Return a low guess at the load of a migration-source cpu weighted
 * according to the scheduling class and "nice" value.
 *
 * We want to under-estimate the load of migration sources, to
 * balance conservatively.
 */
static unsigned long source_load(int cpu, int type)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long total = weighted_cpuload(cpu);

	if (type == 0 || !sched_feat(LB_BIAS))
		return total;

	return min(rq->cpu_load[type-1], total);
}

/*
 * Return a high guess at the load of a migration-target cpu weighted
 * according to the scheduling class and "nice" value.
 */
static unsigned long target_load(int cpu, int type)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long total = weighted_cpuload(cpu);

	if (type == 0 || !sched_feat(LB_BIAS))
		return total;

	return max(rq->cpu_load[type-1], total);
}


static unsigned long cpu_avg_load_per_task(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long nr_running = READ_ONCE(rq->cfs.h_nr_running);
	unsigned long load_avg = weighted_cpuload(cpu);

	if (nr_running)
		return load_avg / nr_running;

	return 0;
}

static void record_wakee(struct task_struct *p)
{
	/*
	 * Rough decay (wiping) for cost saving, don't worry
	 * about the boundary, really active task won't care
	 * about the loss.
	 */
	if (time_after(jiffies, current->wakee_flip_decay_ts + HZ)) {
		current->wakee_flips >>= 1;
		current->wakee_flip_decay_ts = jiffies;
	}

	if (current->last_wakee != p) {
		current->last_wakee = p;
		current->wakee_flips++;
	}
}

static void task_waking_fair(struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);
	u64 min_vruntime;

#ifndef CONFIG_64BIT
	u64 min_vruntime_copy;

	do {
		min_vruntime_copy = cfs_rq->min_vruntime_copy;
		smp_rmb();
		min_vruntime = cfs_rq->min_vruntime;
	} while (min_vruntime != min_vruntime_copy);
#else
	min_vruntime = cfs_rq->min_vruntime;
#endif

	se->vruntime -= min_vruntime;
	record_wakee(p);
}

#ifdef CONFIG_FAIR_GROUP_SCHED
/*
 * effective_load() calculates the load change as seen from the root_task_group
 *
 * Adding load to a group doesn't make a group heavier, but can cause movement
 * of group shares between cpus. Assuming the shares were perfectly aligned one
 * can calculate the shift in shares.
 *
 * Calculate the effective load difference if @wl is added (subtracted) to @tg
 * on this @cpu and results in a total addition (subtraction) of @wg to the
 * total group weight.
 *
 * Given a runqueue weight distribution (rw_i) we can compute a shares
 * distribution (s_i) using:
 *
 *   s_i = rw_i / \Sum rw_j						(1)
 *
 * Suppose we have 4 CPUs and our @tg is a direct child of the root group and
 * has 7 equal weight tasks, distributed as below (rw_i), with the resulting
 * shares distribution (s_i):
 *
 *   rw_i = {   2,   4,   1,   0 }
 *   s_i  = { 2/7, 4/7, 1/7,   0 }
 *
 * As per wake_affine() we're interested in the load of two CPUs (the CPU the
 * task used to run on and the CPU the waker is running on), we need to
 * compute the effect of waking a task on either CPU and, in case of a sync
 * wakeup, compute the effect of the current task going to sleep.
 *
 * So for a change of @wl to the local @cpu with an overall group weight change
 * of @wl we can compute the new shares distribution (s'_i) using:
 *
 *   s'_i = (rw_i + @wl) / (@wg + \Sum rw_j)				(2)
 *
 * Suppose we're interested in CPUs 0 and 1, and want to compute the load
 * differences in waking a task to CPU 0. The additional task changes the
 * weight and shares distributions like:
 *
 *   rw'_i = {   3,   4,   1,   0 }
 *   s'_i  = { 3/8, 4/8, 1/8,   0 }
 *
 * We can then compute the difference in effective weight by using:
 *
 *   dw_i = S * (s'_i - s_i)						(3)
 *
 * Where 'S' is the group weight as seen by its parent.
 *
 * Therefore the effective change in loads on CPU 0 would be 5/56 (3/8 - 2/7)
 * times the weight of the group. The effect on CPU 1 would be -4/56 (4/8 -
 * 4/7) times the weight of the group.
 */
static long effective_load(struct task_group *tg, int cpu, long wl, long wg)
{
	struct sched_entity *se = tg->se[cpu];

	if (!tg->parent)	/* the trivial, non-cgroup case */
		return wl;

	for_each_sched_entity(se) {
		struct cfs_rq *cfs_rq = se->my_q;
		long W, w = cfs_rq_load_avg(cfs_rq);

		tg = cfs_rq->tg;

		/*
		 * W = @wg + \Sum rw_j
		 */
		W = wg + atomic_long_read(&tg->load_avg);

		/* Ensure \Sum rw_j >= rw_i */
		W -= cfs_rq->tg_load_avg_contrib;
		W += w;

		/*
		 * w = rw_i + @wl
		 */
		w += wl;

		/*
		 * wl = S * s'_i; see (2)
		 */
		if (W > 0 && w < W)
			wl = (w * (long)tg->shares) / W;
		else
			wl = tg->shares;

		/*
		 * Per the above, wl is the new se->load.weight value; since
		 * those are clipped to [MIN_SHARES, ...) do so now. See
		 * calc_cfs_shares().
		 */
		if (wl < MIN_SHARES)
			wl = MIN_SHARES;

		/*
		 * wl = dw_i = S * (s'_i - s_i); see (3)
		 */
		wl -= se->avg.load_avg;

		/*
		 * Recursively apply this logic to all parent groups to compute
		 * the final effective load change on the root group. Since
		 * only the @tg group gets extra weight, all parent groups can
		 * only redistribute existing shares. @wl is the shift in shares
		 * resulting from this level per the above.
		 */
		wg = 0;
	}

	return wl;
}
#else

static long effective_load(struct task_group *tg, int cpu, long wl, long wg)
{
	return wl;
}

#endif

/*
 * Returns the current capacity of cpu after applying both
 * cpu and freq scaling.
 */
unsigned long capacity_curr_of(int cpu)
{
	return cpu_rq(cpu)->cpu_capacity_orig *
	       arch_scale_freq_capacity(NULL, cpu)
	       >> SCHED_CAPACITY_SHIFT;
}

static inline bool energy_aware(void)
{
	return sched_feat(ENERGY_AWARE);
}

struct energy_env {
	struct sched_group	*sg_top;
	struct sched_group	*sg_cap;
	int			cap_idx;
	int			util_delta;
	int			src_cpu;
	int			dst_cpu;
	int			trg_cpu;
	int			energy;
	int			payoff;
	struct task_struct	*task;
	struct {
		int before;
		int after;
		int delta;
		int diff;
	} nrg;
	struct {
		int before;
		int after;
		int delta;
	} cap;
};

static int cpu_util_wake(int cpu, struct task_struct *p);

/*
 * __cpu_norm_util() returns the cpu util relative to a specific capacity,
 * i.e. it's busy ratio, in the range [0..SCHED_LOAD_SCALE], which is useful for
 * energy calculations.
 *
 * Since util is a scale-invariant utilization defined as:
 *
 *   util ~ (curr_freq/max_freq)*1024 * capacity_orig/1024 * running_time/time
 *
 * the normalized util can be found using the specific capacity.
 *
 *   capacity = capacity_orig * curr_freq/max_freq
 *
 *   norm_util = running_time/time ~ util/capacity
 */
static unsigned long __cpu_norm_util(unsigned long util, unsigned long capacity)
{
	if (util >= capacity)
		return SCHED_CAPACITY_SCALE;

	return (util << SCHED_CAPACITY_SHIFT)/capacity;
}

static unsigned long group_max_util(struct energy_env *eenv)
{
	unsigned long max_util = 0;
	unsigned long util;
	int cpu;

	for_each_cpu(cpu, sched_group_cpus(eenv->sg_cap)) {
		util = cpu_util_wake(cpu, eenv->task);

		/*
		 * If we are looking at the target CPU specified by the eenv,
		 * then we should add the (estimated) utilization of the task
		 * assuming we will wake it up on that CPU.
		 */
		if (unlikely(cpu == eenv->trg_cpu))
			util += eenv->util_delta;

		max_util = max(max_util, util);
	}

	return max_util;
}

/*
 * group_norm_util() returns the approximated group util relative to it's
 * current capacity (busy ratio), in the range [0..SCHED_LOAD_SCALE], for use
 * in energy calculations.
 *
 * Since task executions may or may not overlap in time in the group the true
 * normalized util is between MAX(cpu_norm_util(i)) and SUM(cpu_norm_util(i))
 * when iterating over all CPUs in the group.
 * The latter estimate is used as it leads to a more pessimistic energy
 * estimate (more busy).
 */
static unsigned
long group_norm_util(struct energy_env *eenv, struct sched_group *sg)
{
	unsigned long capacity = sg->sge->cap_states[eenv->cap_idx].cap;
	unsigned long util, util_sum = 0;
	int cpu;

	for_each_cpu(cpu, sched_group_cpus(sg)) {
		util = cpu_util_wake(cpu, eenv->task);

		/*
		 * If we are looking at the target CPU specified by the eenv,
		 * then we should add the (estimated) utilization of the task
		 * assuming we will wake it up on that CPU.
		 */
		if (unlikely(cpu == eenv->trg_cpu))
			util += eenv->util_delta;

		util_sum += __cpu_norm_util(util, capacity);
	}

	return min_t(unsigned long, util_sum, SCHED_CAPACITY_SCALE);
}

static int find_new_capacity(struct energy_env *eenv,
	const struct sched_group_energy * const sge)
{
	int idx, max_idx = sge->nr_cap_states - 1;
	unsigned long util = group_max_util(eenv);

	/* default is max_cap if we don't find a match */
	eenv->cap_idx = max_idx;

	for (idx = 0; idx < sge->nr_cap_states; idx++) {
		if (sge->cap_states[idx].cap >= util) {
			eenv->cap_idx = idx;
			break;
		}
	}

	return eenv->cap_idx;
}

static int group_idle_state(struct energy_env *eenv, struct sched_group *sg)
{
	int i, state = INT_MAX;
	int src_in_grp, dst_in_grp;
	long grp_util = 0;

	/* Find the shallowest idle state in the sched group. */
	for_each_cpu(i, sched_group_cpus(sg))
		state = min(state, idle_get_state_idx(cpu_rq(i)));

	/* Take non-cpuidle idling into account (active idle/arch_cpu_idle()) */
	state++;

	src_in_grp = cpumask_test_cpu(eenv->src_cpu, sched_group_cpus(sg));
	dst_in_grp = cpumask_test_cpu(eenv->dst_cpu, sched_group_cpus(sg));
	if (src_in_grp == dst_in_grp) {
		/* both CPUs under consideration are in the same group or not in
		 * either group, migration should leave idle state the same.
		 */
		goto end;
	}

	/*
	 * Try to estimate if a deeper idle state is
	 * achievable when we move the task.
	 */
	for_each_cpu(i, sched_group_cpus(sg)) {
		grp_util += cpu_util_wake(i, eenv->task);
		if (unlikely(i == eenv->trg_cpu))
			grp_util += eenv->util_delta;
	}

	if (grp_util <=
		((long)sg->sgc->max_capacity * (int)sg->group_weight)) {
		/* after moving, this group is at most partly
		 * occupied, so it should have some idle time.
		 */
		int max_idle_state_idx = sg->sge->nr_idle_states - 2;
		int new_state = grp_util * max_idle_state_idx;
		if (grp_util <= 0)
			/* group will have no util, use lowest state */
			new_state = max_idle_state_idx + 1;
		else {
			/* for partially idle, linearly map util to idle
			 * states, excluding the lowest one. This does not
			 * correspond to the state we expect to enter in
			 * reality, but an indication of what might happen.
			 */
			new_state = min(max_idle_state_idx, (int)
					(new_state / sg->sgc->max_capacity));
			new_state = max_idle_state_idx - new_state;
		}
		state = new_state;
	} else {
		/* After moving, the group will be fully occupied
		 * so assume it will not be idle at all.
		 */
		state = 0;
	}
end:
	return state;
}

/*
 * sched_group_energy(): Computes the absolute energy consumption of cpus
 * belonging to the sched_group including shared resources shared only by
 * members of the group. Iterates over all cpus in the hierarchy below the
 * sched_group starting from the bottom working it's way up before going to
 * the next cpu until all cpus are covered at all levels. The current
 * implementation is likely to gather the same util statistics multiple times.
 * This can probably be done in a faster but more complex way.
 * Note: sched_group_energy() may fail when racing with sched_domain updates.
 */
static int sched_group_energy(struct energy_env *eenv)
{
	struct cpumask visit_cpus;
	u64 total_energy = 0;
	int cpu_count;

	WARN_ON(!eenv->sg_top->sge);

	cpumask_copy(&visit_cpus, sched_group_cpus(eenv->sg_top));
	/* If a cpu is hotplugged in while we are in this function,
	 * it does not appear in the existing visit_cpus mask
	 * which came from the sched_group pointer of the
	 * sched_domain pointed at by sd_ea for either the prev
	 * or next cpu and was dereferenced in __energy_diff.
	 * Since we will dereference sd_scs later as we iterate
	 * through the CPUs we expect to visit, new CPUs can
	 * be present which are not in the visit_cpus mask.
	 * Guard this with cpu_count.
	 */
	cpu_count = cpumask_weight(&visit_cpus);

	while (!cpumask_empty(&visit_cpus)) {
		struct sched_group *sg_shared_cap = NULL;
		int cpu = cpumask_first(&visit_cpus);
		struct sched_domain *sd;

		/*
		 * Is the group utilization affected by cpus outside this
		 * sched_group?
		 * This sd may have groups with cpus which were not present
		 * when we took visit_cpus.
		 */
		sd = rcu_dereference(per_cpu(sd_scs, cpu));

		if (sd && sd->parent)
			sg_shared_cap = sd->parent->groups;

		for_each_domain(cpu, sd) {
			struct sched_group *sg = sd->groups;

			/* Has this sched_domain already been visited? */
			if (sd->child && group_first_cpu(sg) != cpu)
				break;

			do {
				unsigned long group_util;
				int sg_busy_energy, sg_idle_energy;
				int cap_idx, idle_idx;

				if (sg_shared_cap && sg_shared_cap->group_weight >= sg->group_weight)
					eenv->sg_cap = sg_shared_cap;
				else
					eenv->sg_cap = sg;

				cap_idx = find_new_capacity(eenv, sg->sge);

				if (sg->group_weight == 1) {
					/* Remove capacity of src CPU (before task move) */
					if (eenv->trg_cpu == eenv->src_cpu &&
					    cpumask_test_cpu(eenv->src_cpu, sched_group_cpus(sg))) {
						eenv->cap.before = sg->sge->cap_states[cap_idx].cap;
						eenv->cap.delta -= eenv->cap.before;
					}
					/* Add capacity of dst CPU  (after task move) */
					if (eenv->trg_cpu == eenv->dst_cpu &&
					    cpumask_test_cpu(eenv->dst_cpu, sched_group_cpus(sg))) {
						eenv->cap.after = sg->sge->cap_states[cap_idx].cap;
						eenv->cap.delta += eenv->cap.after;
					}
				}

				idle_idx = group_idle_state(eenv, sg);
				group_util = group_norm_util(eenv, sg);

				sg_busy_energy = (group_util * sg->sge->cap_states[cap_idx].power);
				sg_idle_energy = ((SCHED_LOAD_SCALE-group_util)
								* sg->sge->idle_states[idle_idx].power);

				total_energy += sg_busy_energy + sg_idle_energy;

				if (!sd->child) {
					/*
					 * cpu_count here is the number of
					 * cpus we expect to visit in this
					 * calculation. If we race against
					 * hotplug, we can have extra cpus
					 * added to the groups we are
					 * iterating which do not appear in
					 * the visit_cpus mask. In that case
					 * we are not able to calculate energy
					 * without restarting so we will bail
					 * out and use prev_cpu this time.
					 */
					if (!cpu_count)
						return -EINVAL;
					cpumask_xor(&visit_cpus, &visit_cpus, sched_group_cpus(sg));
					cpu_count--;
				}

				if (cpumask_equal(sched_group_cpus(sg), sched_group_cpus(eenv->sg_top)))
					goto next_cpu;

			} while (sg = sg->next, sg != sd->groups);
		}

		/*
		 * If we raced with hotplug and got an sd NULL-pointer;
		 * returning a wrong energy estimation is better than
		 * entering an infinite loop.
		 * Specifically: If a cpu is unplugged after we took
		 * the visit_cpus mask, it no longer has an sd_scs
		 * pointer, so when we dereference it, we get NULL.
		 */
		if (cpumask_test_cpu(cpu, &visit_cpus))
			return -EINVAL;
next_cpu:
		cpumask_clear_cpu(cpu, &visit_cpus);
		continue;
	}

	eenv->energy = total_energy >> SCHED_CAPACITY_SHIFT;
	return 0;
}

static inline bool cpu_in_sg(struct sched_group *sg, int cpu)
{
	return cpu != -1 && cpumask_test_cpu(cpu, sched_group_cpus(sg));
}

static inline unsigned long task_util(struct task_struct *p);

/*
 * energy_diff(): Estimate the energy impact of changing the utilization
 * distribution. eenv specifies the change: utilisation amount, source, and
 * destination cpu. Source or destination cpu may be -1 in which case the
 * utilization is removed from or added to the system (e.g. task wake-up). If
 * both are specified, the utilization is migrated.
 */
static inline int __energy_diff(struct energy_env *eenv)
{
	struct sched_domain *sd;
	struct sched_group *sg;
	int sd_cpu = -1, energy_before = 0, energy_after = 0;
	int diff, margin;

	struct energy_env eenv_before = {
		.util_delta	= task_util(eenv->task),
		.src_cpu	= eenv->src_cpu,
		.dst_cpu	= eenv->dst_cpu,
		.trg_cpu	= eenv->src_cpu,
		.nrg		= { 0, 0, 0, 0},
		.cap		= { 0, 0, 0 },
		.task		= eenv->task,
	};

	if (eenv->src_cpu == eenv->dst_cpu)
		return 0;

	sd_cpu = (eenv->src_cpu != -1) ? eenv->src_cpu : eenv->dst_cpu;
	sd = rcu_dereference(per_cpu(sd_ea, sd_cpu));

	if (!sd)
		return 0; /* Error */

	sg = sd->groups;

	do {
		if (cpu_in_sg(sg, eenv->src_cpu) || cpu_in_sg(sg, eenv->dst_cpu)) {
			eenv_before.sg_top = eenv->sg_top = sg;

			if (sched_group_energy(&eenv_before))
				return 0; /* Invalid result abort */
			energy_before += eenv_before.energy;

			/* Keep track of SRC cpu (before) capacity */
			eenv->cap.before = eenv_before.cap.before;
			eenv->cap.delta = eenv_before.cap.delta;

			if (sched_group_energy(eenv))
				return 0; /* Invalid result abort */
			energy_after += eenv->energy;
		}
	} while (sg = sg->next, sg != sd->groups);

	eenv->nrg.before = energy_before;
	eenv->nrg.after = energy_after;
	eenv->nrg.diff = eenv->nrg.after - eenv->nrg.before;
	eenv->payoff = 0;
#ifndef CONFIG_SCHED_TUNE
	trace_sched_energy_diff(eenv->task,
			eenv->src_cpu, eenv->dst_cpu, eenv->util_delta,
			eenv->nrg.before, eenv->nrg.after, eenv->nrg.diff,
			eenv->cap.before, eenv->cap.after, eenv->cap.delta,
			eenv->nrg.delta, eenv->payoff);
#endif
	/*
	 * Dead-zone margin preventing too many migrations.
	 */

	margin = eenv->nrg.before >> 6; /* ~1.56% */

	diff = eenv->nrg.after - eenv->nrg.before;

	eenv->nrg.diff = (abs(diff) < margin) ? 0 : eenv->nrg.diff;

	return eenv->nrg.diff;
}

#ifdef CONFIG_SCHED_TUNE

struct target_nrg schedtune_target_nrg;

#ifdef CONFIG_CGROUP_SCHEDTUNE
extern bool schedtune_initialized;
#endif /* CONFIG_CGROUP_SCHEDTUNE */

/*
 * System energy normalization
 * Returns the normalized value, in the range [0..SCHED_CAPACITY_SCALE],
 * corresponding to the specified energy variation.
 */
static inline int
normalize_energy(int energy_diff)
{
	u32 normalized_nrg;

#ifdef CONFIG_CGROUP_SCHEDTUNE
	/* during early setup, we don't know the extents */
	if (unlikely(!schedtune_initialized))
		return energy_diff < 0 ? -1 : 1 ;
#endif /* CONFIG_CGROUP_SCHEDTUNE */

#ifdef CONFIG_SCHED_DEBUG
	{
	int max_delta;

	/* Check for boundaries */
	max_delta  = schedtune_target_nrg.max_power;
	max_delta -= schedtune_target_nrg.min_power;
	WARN_ON(abs(energy_diff) >= max_delta);
	}
#endif

	/* Do scaling using positive numbers to increase the range */
	normalized_nrg = (energy_diff < 0) ? -energy_diff : energy_diff;

	/* Scale by energy magnitude */
	normalized_nrg <<= SCHED_CAPACITY_SHIFT;

	/* Normalize on max energy for target platform */
	normalized_nrg = reciprocal_divide(
			normalized_nrg, schedtune_target_nrg.rdiv);

	return (energy_diff < 0) ? -normalized_nrg : normalized_nrg;
}

static inline int
energy_diff(struct energy_env *eenv)
{
	int boost = schedtune_task_boost(eenv->task);
	int nrg_delta;

	/* Conpute "absolute" energy diff */
	__energy_diff(eenv);

	/* Return energy diff when boost margin is 0 */
	if (boost == 0) {
		trace_sched_energy_diff(eenv->task,
				eenv->src_cpu, eenv->dst_cpu, eenv->util_delta,
				eenv->nrg.before, eenv->nrg.after, eenv->nrg.diff,
				eenv->cap.before, eenv->cap.after, eenv->cap.delta,
				0, -eenv->nrg.diff);
		return eenv->nrg.diff;
	}

	/* Compute normalized energy diff */
	nrg_delta = normalize_energy(eenv->nrg.diff);
	eenv->nrg.delta = nrg_delta;

	eenv->payoff = schedtune_accept_deltas(
			eenv->nrg.delta,
			eenv->cap.delta,
			eenv->task);

	trace_sched_energy_diff(eenv->task,
			eenv->src_cpu, eenv->dst_cpu, eenv->util_delta,
			eenv->nrg.before, eenv->nrg.after, eenv->nrg.diff,
			eenv->cap.before, eenv->cap.after, eenv->cap.delta,
			eenv->nrg.delta, eenv->payoff);

	/*
	 * When SchedTune is enabled, the energy_diff() function will return
	 * the computed energy payoff value. Since the energy_diff() return
	 * value is expected to be negative by its callers, this evaluation
	 * function return a negative value each time the evaluation return a
	 * positive payoff, which is the condition for the acceptance of
	 * a scheduling decision
	 */
	return -eenv->payoff;
}
#else /* CONFIG_SCHED_TUNE */
#define energy_diff(eenv) __energy_diff(eenv)
#endif

/*
 * Detect M:N waker/wakee relationships via a switching-frequency heuristic.
 * A waker of many should wake a different task than the one last awakened
 * at a frequency roughly N times higher than one of its wakees.  In order
 * to determine whether we should let the load spread vs consolodating to
 * shared cache, we look for a minimum 'flip' frequency of llc_size in one
 * partner, and a factor of lls_size higher frequency in the other.  With
 * both conditions met, we can be relatively sure that the relationship is
 * non-monogamous, with partner count exceeding socket size.  Waker/wakee
 * being client/server, worker/dispatcher, interrupt source or whatever is
 * irrelevant, spread criteria is apparent partner count exceeds socket size.
 */
static int wake_wide(struct task_struct *p, int sibling_count_hint)
{
	unsigned int master = current->wakee_flips;
	unsigned int slave = p->wakee_flips;
	int llc_size = this_cpu_read(sd_llc_size);

	if (sibling_count_hint >= llc_size)
		return 1;

	if (master < slave)
		swap(master, slave);
	if (slave < llc_size || master < slave * llc_size)
		return 0;
	return 1;
}

static int wake_affine(struct sched_domain *sd, struct task_struct *p,
		       int prev_cpu, int sync)
{
	s64 this_load, load;
	s64 this_eff_load, prev_eff_load;
	int idx, this_cpu;
	struct task_group *tg;
	unsigned long weight;
	int balanced;

	idx	  = sd->wake_idx;
	this_cpu  = smp_processor_id();
	load	  = source_load(prev_cpu, idx);
	this_load = target_load(this_cpu, idx);

	/*
	 * If sync wakeup then subtract the (maximum possible)
	 * effect of the currently running task from the load
	 * of the current CPU:
	 */
	if (sync) {
		tg = task_group(current);
		weight = current->se.avg.load_avg;

		this_load += effective_load(tg, this_cpu, -weight, -weight);
		load += effective_load(tg, prev_cpu, 0, -weight);
	}

	tg = task_group(p);
	weight = p->se.avg.load_avg;

	/*
	 * In low-load situations, where prev_cpu is idle and this_cpu is idle
	 * due to the sync cause above having dropped this_load to 0, we'll
	 * always have an imbalance, but there's really nothing you can do
	 * about that, so that's good too.
	 *
	 * Otherwise check if either cpus are near enough in load to allow this
	 * task to be woken on this_cpu.
	 */
	this_eff_load = 100;
	this_eff_load *= capacity_of(prev_cpu);

	prev_eff_load = 100 + (sd->imbalance_pct - 100) / 2;
	prev_eff_load *= capacity_of(this_cpu);

	if (this_load > 0) {
		this_eff_load *= this_load +
			effective_load(tg, this_cpu, weight, weight);

		prev_eff_load *= load + effective_load(tg, prev_cpu, 0, weight);
	}

	balanced = this_eff_load <= prev_eff_load;

	schedstat_inc(p, se.statistics.nr_wakeups_affine_attempts);

	if (!balanced)
		return 0;

	schedstat_inc(sd, ttwu_move_affine);
	schedstat_inc(p, se.statistics.nr_wakeups_affine);

	return 1;
}

static inline unsigned long task_util(struct task_struct *p)
{
#ifdef CONFIG_SCHED_WALT
	if (!walt_disabled && sysctl_sched_use_walt_task_util) {
		unsigned long demand = p->ravg.demand;
		return (demand << 10) / walt_ravg_window;
	}
#endif
	return p->se.avg.util_avg;
}

static inline unsigned long boosted_task_util(struct task_struct *task);

static inline bool __task_fits(struct task_struct *p, int cpu, int util)
{
	unsigned long capacity = capacity_of(cpu);

	util += boosted_task_util(p);

	return (capacity * 1024) > (util * capacity_margin);
}

static inline bool task_fits_max(struct task_struct *p, int cpu)
{
	unsigned long capacity = capacity_of(cpu);
	unsigned long max_capacity = cpu_rq(cpu)->rd->max_cpu_capacity.val;

	if (capacity == max_capacity)
		return true;

	if (capacity * capacity_margin > max_capacity * 1024)
		return true;

	return __task_fits(p, cpu, 0);
}

static bool __cpu_overutilized(int cpu, int delta)
{
	return (capacity_of(cpu) * 1024) < ((cpu_util(cpu) + delta) * capacity_margin);
}

static bool cpu_overutilized(int cpu)
{
	return __cpu_overutilized(cpu, 0);
}

#ifdef CONFIG_SCHED_TUNE

struct reciprocal_value schedtune_spc_rdiv;

static long
schedtune_margin(unsigned long signal, long boost)
{
	long long margin = 0;

	/*
	 * Signal proportional compensation (SPC)
	 *
	 * The Boost (B) value is used to compute a Margin (M) which is
	 * proportional to the complement of the original Signal (S):
	 *   M = B * (SCHED_CAPACITY_SCALE - S)
	 * The obtained M could be used by the caller to "boost" S.
	 */
	if (boost >= 0) {
		margin  = SCHED_CAPACITY_SCALE - signal;
		margin *= boost;
	} else
		margin = -signal * boost;

	margin  = reciprocal_divide(margin, schedtune_spc_rdiv);

	if (boost < 0)
		margin *= -1;
	return margin;
}

static inline int
schedtune_cpu_margin(unsigned long util, int cpu)
{
	int boost = schedtune_cpu_boost(cpu);

	if (boost == 0)
		return 0;

	return schedtune_margin(util, boost);
}

static inline long
schedtune_task_margin(struct task_struct *task)
{
	int boost = schedtune_task_boost(task);
	unsigned long util;
	long margin;

	if (boost == 0)
		return 0;

	util = task_util(task);
	margin = schedtune_margin(util, boost);

	return margin;
}

#else /* CONFIG_SCHED_TUNE */

static inline int
schedtune_cpu_margin(unsigned long util, int cpu)
{
	return 0;
}

static inline int
schedtune_task_margin(struct task_struct *task)
{
	return 0;
}

#endif /* CONFIG_SCHED_TUNE */

unsigned long
boosted_cpu_util(int cpu)
{
	unsigned long util = cpu_util_freq(cpu);
	long margin = schedtune_cpu_margin(util, cpu);

	trace_sched_boost_cpu(cpu, util, margin);

	return util + margin;
}

static inline unsigned long
boosted_task_util(struct task_struct *task)
{
	unsigned long util = task_util(task);
	long margin = schedtune_task_margin(task);

	trace_sched_boost_task(task, util, margin);

	return util + margin;
}

static unsigned long capacity_spare_wake(int cpu, struct task_struct *p)
{
	return max_t(long, capacity_of(cpu) - cpu_util_wake(cpu, p), 0);
}

/*
 * find_idlest_group finds and returns the least busy CPU group within the
 * domain.
 *
 * Assumes p is allowed on at least one CPU in sd.
 */
static struct sched_group *
find_idlest_group(struct sched_domain *sd, struct task_struct *p,
		  int this_cpu, int sd_flag)
{
	struct sched_group *idlest = NULL, *group = sd->groups;
	struct sched_group *most_spare_sg = NULL;
	unsigned long min_load = ULONG_MAX, this_load = ULONG_MAX;
	unsigned long most_spare = 0, this_spare = 0;
	int load_idx = sd->forkexec_idx;
	int imbalance = 100 + (sd->imbalance_pct-100)/2;

	if (sd_flag & SD_BALANCE_WAKE)
		load_idx = sd->wake_idx;

	do {
		unsigned long load, avg_load, spare_cap, max_spare_cap;
		int local_group;
		int i;

		/* Skip over this group if it has no CPUs allowed */
		if (!cpumask_intersects(sched_group_cpus(group),
					tsk_cpus_allowed(p)))
			continue;

		local_group = cpumask_test_cpu(this_cpu,
					       sched_group_cpus(group));

		/*
		 * Tally up the load of all CPUs in the group and find
		 * the group containing the CPU with most spare capacity.
		 */
		avg_load = 0;
		max_spare_cap = 0;

		for_each_cpu(i, sched_group_cpus(group)) {
			/* Bias balancing toward cpus of our domain */
			if (local_group)
				load = source_load(i, load_idx);
			else
				load = target_load(i, load_idx);

			avg_load += load;

			spare_cap = capacity_spare_wake(i, p);

			if (spare_cap > max_spare_cap)
				max_spare_cap = spare_cap;
		}

		/* Adjust by relative CPU capacity of the group */
		avg_load = (avg_load * SCHED_CAPACITY_SCALE) / group->sgc->capacity;

		if (local_group) {
			this_load = avg_load;
			this_spare = max_spare_cap;
		} else {
			if (avg_load < min_load) {
				min_load = avg_load;
				idlest = group;
			}

			if (most_spare < max_spare_cap) {
				most_spare = max_spare_cap;
				most_spare_sg = group;
			}
		}
	} while (group = group->next, group != sd->groups);

	/*
	 * The cross-over point between using spare capacity or least load
	 * is too conservative for high utilization tasks on partially
	 * utilized systems if we require spare_capacity > task_util(p),
	 * so we allow for some task stuffing by using
	 * spare_capacity > task_util(p)/2.
	 *
	 * Spare capacity can't be used for fork because the utilization has
	 * not been set yet, we must first select a rq to compute the initial
	 * utilization.
	 */
	if (sd_flag & SD_BALANCE_FORK)
		goto skip_spare;

	if (this_spare > task_util(p) / 2 &&
	    imbalance*this_spare > 100*most_spare)
		return NULL;
	else if (most_spare > task_util(p) / 2)
		return most_spare_sg;

skip_spare:
	if (!idlest || 100*this_load < imbalance*min_load)
		return NULL;
	return idlest;
}

/*
 * find_idlest_group_cpu - find the idlest cpu among the cpus in group.
 */
static int
find_idlest_group_cpu(struct sched_group *group, struct task_struct *p, int this_cpu)
{
	unsigned long load, min_load = ULONG_MAX;
	unsigned int min_exit_latency = UINT_MAX;
	u64 latest_idle_timestamp = 0;
	int least_loaded_cpu = this_cpu;
	int shallowest_idle_cpu = -1;
	int i;

	/* Check if we have any choice: */
	if (group->group_weight == 1)
		return cpumask_first(sched_group_cpus(group));

	/* Traverse only the allowed CPUs */
	for_each_cpu_and(i, sched_group_cpus(group), tsk_cpus_allowed(p)) {
		if (idle_cpu(i)) {
			struct rq *rq = cpu_rq(i);
			struct cpuidle_state *idle = idle_get_state(rq);
			if (idle && idle->exit_latency < min_exit_latency) {
				/*
				 * We give priority to a CPU whose idle state
				 * has the smallest exit latency irrespective
				 * of any idle timestamp.
				 */
				min_exit_latency = idle->exit_latency;
				latest_idle_timestamp = rq->idle_stamp;
				shallowest_idle_cpu = i;
			} else if ((!idle || idle->exit_latency == min_exit_latency) &&
				   rq->idle_stamp > latest_idle_timestamp) {
				/*
				 * If equal or no active idle state, then
				 * the most recently idled CPU might have
				 * a warmer cache.
				 */
				latest_idle_timestamp = rq->idle_stamp;
				shallowest_idle_cpu = i;
			}
		} else if (shallowest_idle_cpu == -1) {
			load = weighted_cpuload(i);
			if (load < min_load || (load == min_load && i == this_cpu)) {
				min_load = load;
				least_loaded_cpu = i;
			}
		}
	}

	return shallowest_idle_cpu != -1 ? shallowest_idle_cpu : least_loaded_cpu;
 }

static inline int find_idlest_cpu(struct sched_domain *sd, struct task_struct *p,
				  int cpu, int prev_cpu, int sd_flag)
{
	int new_cpu = cpu;
	int wu = sd_flag & SD_BALANCE_WAKE;
	int cas_cpu = -1;

	if (wu) {
		schedstat_inc(p, se.statistics.nr_wakeups_cas_attempts);
		schedstat_inc(this_rq(), eas_stats.cas_attempts);
	}

	if (!cpumask_intersects(sched_domain_span(sd), &p->cpus_allowed))
		return prev_cpu;

	while (sd) {
		struct sched_group *group;
		struct sched_domain *tmp;
		int weight;

		if (wu)
			schedstat_inc(sd, eas_stats.cas_attempts);

		if (!(sd->flags & sd_flag)) {
			sd = sd->child;
			continue;
		}

		group = find_idlest_group(sd, p, cpu, sd_flag);
		if (!group) {
			sd = sd->child;
			continue;
		}

		new_cpu = find_idlest_group_cpu(group, p, cpu);
		if (new_cpu == cpu) {
			/* Now try balancing at a lower domain level of cpu */
			sd = sd->child;
			continue;
		}

		/* Now try balancing at a lower domain level of new_cpu */
		cpu = cas_cpu = new_cpu;
		weight = sd->span_weight;
		sd = NULL;
		for_each_domain(cpu, tmp) {
			if (weight <= tmp->span_weight)
				break;
			if (tmp->flags & sd_flag)
				sd = tmp;
		}
		/* while loop will break here if sd == NULL */
	}

	if (wu && (cas_cpu >= 0)) {
		schedstat_inc(p, se.statistics.nr_wakeups_cas_count);
		schedstat_inc(this_rq(), eas_stats.cas_count);
	}

	return new_cpu;
}

/*
 * Try and locate an idle CPU in the sched_domain.
 */
static int select_idle_sibling(struct task_struct *p, int prev, int target)
{
	struct sched_domain *sd;
	struct sched_group *sg;
	int best_idle_cpu = -1;
	int best_idle_cstate = INT_MAX;
	unsigned long best_idle_capacity = ULONG_MAX;

	schedstat_inc(p, se.statistics.nr_wakeups_sis_attempts);
	schedstat_inc(this_rq(), eas_stats.sis_attempts);

	if (!sysctl_sched_cstate_aware) {
		if (idle_cpu(target)) {
			schedstat_inc(p, se.statistics.nr_wakeups_sis_idle);
			schedstat_inc(this_rq(), eas_stats.sis_idle);
			return target;
		}

		/*
		 * If the prevous cpu is cache affine and idle, don't be stupid.
		 */
		if (prev != target && cpus_share_cache(prev, target) && idle_cpu(prev)) {
			schedstat_inc(p, se.statistics.nr_wakeups_sis_cache_affine);
			schedstat_inc(this_rq(), eas_stats.sis_cache_affine);
			return prev;
		}
	}

	/*
	 * Otherwise, iterate the domains and find an elegible idle cpu.
	 */
	sd = rcu_dereference(per_cpu(sd_llc, target));
	for_each_lower_domain(sd) {
		sg = sd->groups;
		do {
			int i;
			if (!cpumask_intersects(sched_group_cpus(sg),
						tsk_cpus_allowed(p)))
				goto next;

			if (sysctl_sched_cstate_aware) {
				for_each_cpu_and(i, tsk_cpus_allowed(p), sched_group_cpus(sg)) {
					int idle_idx = idle_get_state_idx(cpu_rq(i));
					unsigned long new_usage = boosted_task_util(p);
					unsigned long capacity_orig = capacity_orig_of(i);

					if (new_usage > capacity_orig || !idle_cpu(i))
						goto next;

					if (i == target && new_usage <= capacity_curr_of(target)) {
						schedstat_inc(p, se.statistics.nr_wakeups_sis_suff_cap);
						schedstat_inc(this_rq(), eas_stats.sis_suff_cap);
						schedstat_inc(sd, eas_stats.sis_suff_cap);
						return target;
					}

					if (idle_idx < best_idle_cstate &&
					    capacity_orig <= best_idle_capacity) {
						best_idle_cpu = i;
						best_idle_cstate = idle_idx;
						best_idle_capacity = capacity_orig;
					}
				}
			} else {
				for_each_cpu(i, sched_group_cpus(sg)) {
					if (i == target || !idle_cpu(i))
						goto next;
				}

				target = cpumask_first_and(sched_group_cpus(sg),
					tsk_cpus_allowed(p));
				schedstat_inc(p, se.statistics.nr_wakeups_sis_idle_cpu);
				schedstat_inc(this_rq(), eas_stats.sis_idle_cpu);
				schedstat_inc(sd, eas_stats.sis_idle_cpu);
				goto done;
			}
next:
			sg = sg->next;
		} while (sg != sd->groups);
	}

	if (best_idle_cpu >= 0)
		target = best_idle_cpu;

done:
	schedstat_inc(p, se.statistics.nr_wakeups_sis_count);
	schedstat_inc(this_rq(), eas_stats.sis_count);

	return target;
}

/*
 * cpu_util_wake: Compute cpu utilization with any contributions from
 * the waking task p removed.  check_for_migration() looks for a better CPU of
 * rq->curr. For that case we should return cpu util with contributions from
 * currently running task p removed.
 */
static int cpu_util_wake(int cpu, struct task_struct *p)
{
	unsigned long util, capacity;

#ifdef CONFIG_SCHED_WALT
	/*
	 * WALT does not decay idle tasks in the same manner
	 * as PELT, so it makes little sense to subtract task
	 * utilization from cpu utilization. Instead just use
	 * cpu_util for this case.
	 */
	if (!walt_disabled && sysctl_sched_use_walt_cpu_util &&
	    p->state == TASK_WAKING)
		return cpu_util(cpu);
#endif
	/* Task has no contribution or is new */
	if (cpu != task_cpu(p) || !p->se.avg.last_update_time)
		return cpu_util(cpu);

	capacity = capacity_orig_of(cpu);
	util = max_t(long, cpu_util(cpu) - task_util(p), 0);

	return (util >= capacity) ? capacity : util;
}

static int start_cpu(bool boosted)
{
	struct root_domain *rd = cpu_rq(smp_processor_id())->rd;

	return boosted ? rd->max_cap_orig_cpu : rd->min_cap_orig_cpu;
}

static inline int find_best_target(struct task_struct *p, int *backup_cpu,
				   bool boosted, bool prefer_idle)
{
	unsigned long best_idle_min_cap_orig = ULONG_MAX;
	unsigned long min_util = boosted_task_util(p);
	unsigned long target_capacity = ULONG_MAX;
	unsigned long min_wake_util = ULONG_MAX;
	unsigned long target_max_spare_cap = 0;
	unsigned long best_active_util = ULONG_MAX;
	int best_idle_cstate = INT_MAX;
	struct sched_domain *sd;
	struct sched_group *sg;
	int best_active_cpu = -1;
	int best_idle_cpu = -1;
	int target_cpu = -1;
	int cpu, i;

	*backup_cpu = -1;

	schedstat_inc(p, se.statistics.nr_wakeups_fbt_attempts);
	schedstat_inc(this_rq(), eas_stats.fbt_attempts);

	/* Find start CPU based on boost value */
	cpu = start_cpu(boosted);
	if (cpu < 0) {
		schedstat_inc(p, se.statistics.nr_wakeups_fbt_no_cpu);
		schedstat_inc(this_rq(), eas_stats.fbt_no_cpu);
		return -1;
	}

	/* Find SD for the start CPU */
	sd = rcu_dereference(per_cpu(sd_ea, cpu));
	if (!sd) {
		schedstat_inc(p, se.statistics.nr_wakeups_fbt_no_sd);
		schedstat_inc(this_rq(), eas_stats.fbt_no_sd);
		return -1;
	}

	/* Scan CPUs in all SDs */
	sg = sd->groups;
	do {
		for_each_cpu_and(i, tsk_cpus_allowed(p), sched_group_cpus(sg)) {
			unsigned long capacity_curr = capacity_curr_of(i);
			unsigned long capacity_orig = capacity_orig_of(i);
			unsigned long wake_util, new_util;

			if (!cpu_online(i))
				continue;

			if (walt_cpu_high_irqload(i))
				continue;

			/*
			 * p's blocked utilization is still accounted for on prev_cpu
			 * so prev_cpu will receive a negative bias due to the double
			 * accounting. However, the blocked utilization may be zero.
			 */
			wake_util = cpu_util_wake(i, p);
			new_util = wake_util + task_util(p);

			/*
			 * Ensure minimum capacity to grant the required boost.
			 * The target CPU can be already at a capacity level higher
			 * than the one required to boost the task.
			 */
			new_util = max(min_util, new_util);
			if (new_util > capacity_orig)
				continue;

			/*
			 * Case A) Latency sensitive tasks
			 *
			 * Unconditionally favoring tasks that prefer idle CPU to
			 * improve latency.
			 *
			 * Looking for:
			 * - an idle CPU, whatever its idle_state is, since
			 *   the first CPUs we explore are more likely to be
			 *   reserved for latency sensitive tasks.
			 * - a non idle CPU where the task fits in its current
			 *   capacity and has the maximum spare capacity.
			 * - a non idle CPU with lower contention from other
			 *   tasks and running at the lowest possible OPP.
			 *
			 * The last two goals tries to favor a non idle CPU
			 * where the task can run as if it is "almost alone".
			 * A maximum spare capacity CPU is favoured since
			 * the task already fits into that CPU's capacity
			 * without waiting for an OPP chance.
			 *
			 * The following code path is the only one in the CPUs
			 * exploration loop which is always used by
			 * prefer_idle tasks. It exits the loop with wither a
			 * best_active_cpu or a target_cpu which should
			 * represent an optimal choice for latency sensitive
			 * tasks.
			 */
			if (prefer_idle) {

				/*
				 * Case A.1: IDLE CPU
				 * Return the first IDLE CPU we find.
				 */
				if (idle_cpu(i)) {
					schedstat_inc(p, se.statistics.nr_wakeups_fbt_pref_idle);
					schedstat_inc(this_rq(), eas_stats.fbt_pref_idle);

					trace_sched_find_best_target(p,
							prefer_idle, min_util,
							cpu, best_idle_cpu,
							best_active_cpu, i);

					return i;
				}

				/*
				 * Case A.2: Target ACTIVE CPU
				 * Favor CPUs with max spare capacity.
				 */
				if ((capacity_curr > new_util) &&
					(capacity_orig - new_util > target_max_spare_cap)) {
					target_max_spare_cap = capacity_orig - new_util;
					target_cpu = i;
					continue;
				}
				if (target_cpu != -1)
					continue;


				/*
				 * Case A.3: Backup ACTIVE CPU
				 * Favor CPUs with:
				 * - lower utilization due to other tasks
				 * - lower utilization with the task in
				 */
				if (wake_util > min_wake_util)
					continue;
				if (new_util > best_active_util)
					continue;
				min_wake_util = wake_util;
				best_active_util = new_util;
				best_active_cpu = i;
				continue;
			}

			/*
			 * Enforce EAS mode
			 *
			 * For non latency sensitive tasks, skip CPUs that
			 * will be overutilized by moving the task there.
			 *
			 * The goal here is to remain in EAS mode as long as
			 * possible at least for !prefer_idle tasks.
			 */
			if ((new_util * capacity_margin) >
			    (capacity_orig * SCHED_CAPACITY_SCALE))
				continue;

			/*
			 * Case B) Non latency sensitive tasks on IDLE CPUs.
			 *
			 * Find an optimal backup IDLE CPU for non latency
			 * sensitive tasks.
			 *
			 * Looking for:
			 * - minimizing the capacity_orig,
			 *   i.e. preferring LITTLE CPUs
			 * - favoring shallowest idle states
			 *   i.e. avoid to wakeup deep-idle CPUs
			 *
			 * The following code path is used by non latency
			 * sensitive tasks if IDLE CPUs are available. If at
			 * least one of such CPUs are available it sets the
			 * best_idle_cpu to the most suitable idle CPU to be
			 * selected.
			 *
			 * If idle CPUs are available, favour these CPUs to
			 * improve performances by spreading tasks.
			 * Indeed, the energy_diff() computed by the caller
			 * will take care to ensure the minimization of energy
			 * consumptions without affecting performance.
			 */
			if (idle_cpu(i)) {
				int idle_idx = idle_get_state_idx(cpu_rq(i));

				/* Select idle CPU with lower cap_orig */
				if (capacity_orig > best_idle_min_cap_orig)
					continue;

				/*
				 * Skip CPUs in deeper idle state, but only
				 * if they are also less energy efficient.
				 * IOW, prefer a deep IDLE LITTLE CPU vs a
				 * shallow idle big CPU.
				 */
				if (sysctl_sched_cstate_aware &&
				    best_idle_cstate <= idle_idx)
					continue;

				/* Keep track of best idle CPU */
				best_idle_min_cap_orig = capacity_orig;
				best_idle_cstate = idle_idx;
				best_idle_cpu = i;
				continue;
			}

			/*
			 * Case C) Non latency sensitive tasks on ACTIVE CPUs.
			 *
			 * Pack tasks in the most energy efficient capacities.
			 *
			 * This task packing strategy prefers more energy
			 * efficient CPUs (i.e. pack on smaller maximum
			 * capacity CPUs) while also trying to spread tasks to
			 * run them all at the lower OPP.
			 *
			 * This assumes for example that it's more energy
			 * efficient to run two tasks on two CPUs at a lower
			 * OPP than packing both on a single CPU but running
			 * that CPU at an higher OPP.
			 *
			 * Thus, this case keep track of the CPU with the
			 * smallest maximum capacity and highest spare maximum
			 * capacity.
			 */

			/* Favor CPUs with smaller capacity */
			if (capacity_orig > target_capacity)
				continue;

			/* Favor CPUs with maximum spare capacity */
			if ((capacity_orig - new_util) < target_max_spare_cap)
				continue;

			target_max_spare_cap = capacity_orig - new_util;
			target_capacity = capacity_orig;
			target_cpu = i;
		}

	} while (sg = sg->next, sg != sd->groups);

	/*
	 * For non latency sensitive tasks, cases B and C in the previous loop,
	 * we pick the best IDLE CPU only if we was not able to find a target
	 * ACTIVE CPU.
	 *
	 * Policies priorities:
	 *
	 * - prefer_idle tasks:
	 *
	 *   a) IDLE CPU available, we return immediately
	 *   b) ACTIVE CPU where task fits and has the bigger maximum spare
	 *      capacity (i.e. target_cpu)
	 *   c) ACTIVE CPU with less contention due to other tasks
	 *      (i.e. best_active_cpu)
	 *
	 * - NON prefer_idle tasks:
	 *
	 *   a) ACTIVE CPU: target_cpu
	 *   b) IDLE CPU: best_idle_cpu
	 */
	if (target_cpu == -1)
		target_cpu = prefer_idle
			? best_active_cpu
			: best_idle_cpu;
	else
		*backup_cpu = prefer_idle
		? best_active_cpu
		: best_idle_cpu;

	trace_sched_find_best_target(p, prefer_idle, min_util, cpu,
				     best_idle_cpu, best_active_cpu,
				     target_cpu);

	schedstat_inc(p, se.statistics.nr_wakeups_fbt_count);
	schedstat_inc(this_rq(), eas_stats.fbt_count);

	return target_cpu;
}

/*
 * Disable WAKE_AFFINE in the case where task @p doesn't fit in the
 * capacity of either the waking CPU @cpu or the previous CPU @prev_cpu.
 *
 * In that case WAKE_AFFINE doesn't make sense and we'll let
 * BALANCE_WAKE sort things out.
 */
static int wake_cap(struct task_struct *p, int cpu, int prev_cpu)
{
	long min_cap, max_cap;

	min_cap = min(capacity_orig_of(prev_cpu), capacity_orig_of(cpu));
	max_cap = cpu_rq(cpu)->rd->max_cpu_capacity.val;

	/* Minimum capacity is close to max, no need to abort wake_affine */
	if (max_cap - min_cap < max_cap >> 3)
		return 0;

	/* Bring task utilization in sync with prev_cpu */
	sync_entity_load_avg(&p->se);

	return min_cap * 1024 < task_util(p) * capacity_margin;
}

static int select_energy_cpu_brute(struct task_struct *p, int prev_cpu, int sync)
{
	struct sched_domain *sd;
	int target_cpu = prev_cpu, tmp_target, tmp_backup;
	bool boosted, prefer_idle;

	schedstat_inc(p, se.statistics.nr_wakeups_secb_attempts);
	schedstat_inc(this_rq(), eas_stats.secb_attempts);

	if (sysctl_sched_sync_hint_enable && sync) {
		int cpu = smp_processor_id();

		if (cpumask_test_cpu(cpu, tsk_cpus_allowed(p))) {
			schedstat_inc(p, se.statistics.nr_wakeups_secb_sync);
			schedstat_inc(this_rq(), eas_stats.secb_sync);
			return cpu;
		}
	}

	rcu_read_lock();
#ifdef CONFIG_CGROUP_SCHEDTUNE
	boosted = schedtune_task_boost(p) > 0;
	prefer_idle = schedtune_prefer_idle(p) > 0;
#else
	boosted = get_sysctl_sched_cfs_boost() > 0;
	prefer_idle = 0;
#endif

	sync_entity_load_avg(&p->se);

	sd = rcu_dereference(per_cpu(sd_ea, prev_cpu));
	/* Find a cpu with sufficient capacity */
	tmp_target = find_best_target(p, &tmp_backup, boosted, prefer_idle);

	if (!sd)
		goto unlock;
	if (tmp_target >= 0) {
		target_cpu = tmp_target;
		if ((boosted || prefer_idle) && idle_cpu(target_cpu)) {
			schedstat_inc(p, se.statistics.nr_wakeups_secb_idle_bt);
			schedstat_inc(this_rq(), eas_stats.secb_idle_bt);
			goto unlock;
		}
	}

	if (target_cpu != prev_cpu) {
		int delta = 0;
		struct energy_env eenv = {
			.util_delta     = task_util(p),
			.src_cpu        = prev_cpu,
			.dst_cpu        = target_cpu,
			.task           = p,
			.trg_cpu	= target_cpu,
		};


#ifdef CONFIG_SCHED_WALT
		if (!walt_disabled && sysctl_sched_use_walt_cpu_util &&
			p->state == TASK_WAKING)
			delta = task_util(p);
#endif
		/* Not enough spare capacity on previous cpu */
		if (__cpu_overutilized(prev_cpu, delta)) {
			schedstat_inc(p, se.statistics.nr_wakeups_secb_insuff_cap);
			schedstat_inc(this_rq(), eas_stats.secb_insuff_cap);
			goto unlock;
		}

		if (energy_diff(&eenv) >= 0) {
			/* No energy saving for target_cpu, try backup */
			target_cpu = tmp_backup;
			eenv.dst_cpu = target_cpu;
			eenv.trg_cpu = target_cpu;
			if (tmp_backup < 0 ||
			    tmp_backup == prev_cpu ||
			    energy_diff(&eenv) >= 0) {
				schedstat_inc(p, se.statistics.nr_wakeups_secb_no_nrg_sav);
				schedstat_inc(this_rq(), eas_stats.secb_no_nrg_sav);
				target_cpu = prev_cpu;
				goto unlock;
			}
		}

		schedstat_inc(p, se.statistics.nr_wakeups_secb_nrg_sav);
		schedstat_inc(this_rq(), eas_stats.secb_nrg_sav);
		goto unlock;
	}

	schedstat_inc(p, se.statistics.nr_wakeups_secb_count);
	schedstat_inc(this_rq(), eas_stats.secb_count);

unlock:
	rcu_read_unlock();

	return target_cpu;
}

/*
 * select_task_rq_fair: Select target runqueue for the waking task in domains
 * that have the 'sd_flag' flag set. In practice, this is SD_BALANCE_WAKE,
 * SD_BALANCE_FORK, or SD_BALANCE_EXEC.
 *
 * Balances load by selecting the idlest cpu in the idlest group, or under
 * certain conditions an idle sibling cpu if the domain has SD_WAKE_AFFINE set.
 *
 * Returns the target cpu number.
 *
 * preempt must be disabled.
 */
static int
select_task_rq_fair(struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags,
		    int sibling_count_hint)
{
	struct sched_domain *tmp, *affine_sd = NULL, *sd = NULL;
	int cpu = smp_processor_id();
	int new_cpu = prev_cpu;
	int want_affine = 0;
	int sync = wake_flags & WF_SYNC;

	if (sd_flag & SD_BALANCE_WAKE) {
		record_wakee(p);
		want_affine = !wake_wide(p, sibling_count_hint) &&
			      !wake_cap(p, cpu, prev_cpu) &&
			      cpumask_test_cpu(cpu, &p->cpus_allowed);
	}

	if (energy_aware() && !(cpu_rq(prev_cpu)->rd->overutilized))
		return select_energy_cpu_brute(p, prev_cpu, sync);

	rcu_read_lock();
	for_each_domain(cpu, tmp) {
		if (!(tmp->flags & SD_LOAD_BALANCE))
			break;

		/*
		 * If both cpu and prev_cpu are part of this domain,
		 * cpu is a valid SD_WAKE_AFFINE target.
		 */
		if (want_affine && (tmp->flags & SD_WAKE_AFFINE) &&
		    cpumask_test_cpu(prev_cpu, sched_domain_span(tmp))) {
			affine_sd = tmp;
			break;
		}

		if (tmp->flags & sd_flag)
			sd = tmp;
		else if (!want_affine)
			break;
	}

	if (affine_sd) {
		sd = NULL; /* Prefer wake_affine over balance flags */
		if (cpu != prev_cpu && wake_affine(affine_sd, p, prev_cpu, sync))
			new_cpu = cpu;
	}

	if (sd && !(sd_flag & SD_BALANCE_FORK)) {
		/*
		 * We're going to need the task's util for capacity_spare_wake
		 * in find_idlest_group. Sync it up to prev_cpu's
		 * last_update_time.
		 */
		sync_entity_load_avg(&p->se);
	}

	if (!sd) {
		if (sd_flag & SD_BALANCE_WAKE) /* XXX always ? */
			new_cpu = select_idle_sibling(p, prev_cpu, new_cpu);

	} else {
		new_cpu = find_idlest_cpu(sd, p, cpu, prev_cpu, sd_flag);
	}
	rcu_read_unlock();

	return new_cpu;
}

/*
 * Called immediately before a task is migrated to a new cpu; task_cpu(p) and
 * cfs_rq_of(p) references at time of call are still valid and identify the
 * previous cpu.  However, the caller only guarantees p->pi_lock is held; no
 * other assumptions, including the state of rq->lock, should be made.
 */
static void migrate_task_rq_fair(struct task_struct *p)
{
	/*
	 * We are supposed to update the task to "current" time, then its up to date
	 * and ready to go to new CPU/cfs_rq. But we have difficulty in getting
	 * what current time is, so simply throw away the out-of-date time. This
	 * will result in the wakee task is less decayed, but giving the wakee more
	 * load sounds not bad.
	 */
	remove_entity_load_avg(&p->se);

	/* Tell new CPU we are migrated */
	p->se.avg.last_update_time = 0;

	/* We have migrated, no longer consider this task hot */
	p->se.exec_start = 0;
}

static void task_dead_fair(struct task_struct *p)
{
	remove_entity_load_avg(&p->se);
}
#else
#define task_fits_max(p, cpu) true
#endif /* CONFIG_SMP */

static unsigned long
wakeup_gran(struct sched_entity *curr, struct sched_entity *se)
{
	unsigned long gran = sysctl_sched_wakeup_granularity;

	/*
	 * Since its curr running now, convert the gran from real-time
	 * to virtual-time in his units.
	 *
	 * By using 'se' instead of 'curr' we penalize light tasks, so
	 * they get preempted easier. That is, if 'se' < 'curr' then
	 * the resulting gran will be larger, therefore penalizing the
	 * lighter, if otoh 'se' > 'curr' then the resulting gran will
	 * be smaller, again penalizing the lighter task.
	 *
	 * This is especially important for buddies when the leftmost
	 * task is higher priority than the buddy.
	 */
	return calc_delta_fair(gran, se);
}

/*
 * Should 'se' preempt 'curr'.
 *
 *             |s1
 *        |s2
 *   |s3
 *         g
 *      |<--->|c
 *
 *  w(c, s1) = -1
 *  w(c, s2) =  0
 *  w(c, s3) =  1
 *
 */
static int
wakeup_preempt_entity(struct sched_entity *curr, struct sched_entity *se)
{
	s64 gran, vdiff = curr->vruntime - se->vruntime;

	if (vdiff <= 0)
		return -1;

	gran = wakeup_gran(curr, se);
	if (vdiff > gran)
		return 1;

	return 0;
}

static void set_last_buddy(struct sched_entity *se)
{
	if (entity_is_task(se) && unlikely(task_of(se)->policy == SCHED_IDLE))
		return;

	for_each_sched_entity(se)
		cfs_rq_of(se)->last = se;
}

static void set_next_buddy(struct sched_entity *se)
{
	if (entity_is_task(se) && unlikely(task_of(se)->policy == SCHED_IDLE))
		return;

	for_each_sched_entity(se)
		cfs_rq_of(se)->next = se;
}

static void set_skip_buddy(struct sched_entity *se)
{
	for_each_sched_entity(se)
		cfs_rq_of(se)->skip = se;
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void check_preempt_wakeup(struct rq *rq, struct task_struct *p, int wake_flags)
{
	struct task_struct *curr = rq->curr;
	struct sched_entity *se = &curr->se, *pse = &p->se;
	struct cfs_rq *cfs_rq = task_cfs_rq(curr);
	int scale = cfs_rq->nr_running >= sched_nr_latency;
	int next_buddy_marked = 0;

	if (unlikely(se == pse))
		return;

	/*
	 * This is possible from callers such as attach_tasks(), in which we
	 * unconditionally check_prempt_curr() after an enqueue (which may have
	 * lead to a throttle).  This both saves work and prevents false
	 * next-buddy nomination below.
	 */
	if (unlikely(throttled_hierarchy(cfs_rq_of(pse))))
		return;

	if (sched_feat(NEXT_BUDDY) && scale && !(wake_flags & WF_FORK)) {
		set_next_buddy(pse);
		next_buddy_marked = 1;
	}

	/*
	 * We can come here with TIF_NEED_RESCHED already set from new task
	 * wake up path.
	 *
	 * Note: this also catches the edge-case of curr being in a throttled
	 * group (e.g. via set_curr_task), since update_curr() (in the
	 * enqueue of curr) will have resulted in resched being set.  This
	 * prevents us from potentially nominating it as a false LAST_BUDDY
	 * below.
	 */
	if (test_tsk_need_resched(curr))
		return;

	/* Idle tasks are by definition preempted by non-idle tasks. */
	if (unlikely(curr->policy == SCHED_IDLE) &&
	    likely(p->policy != SCHED_IDLE))
		goto preempt;

	/*
	 * Batch and idle tasks do not preempt non-idle tasks (their preemption
	 * is driven by the tick):
	 */
	if (unlikely(p->policy != SCHED_NORMAL) || !sched_feat(WAKEUP_PREEMPTION))
		return;

	find_matching_se(&se, &pse);
	update_curr(cfs_rq_of(se));
	BUG_ON(!pse);
	if (wakeup_preempt_entity(se, pse) == 1) {
		/*
		 * Bias pick_next to pick the sched entity that is
		 * triggering this preemption.
		 */
		if (!next_buddy_marked)
			set_next_buddy(pse);
		goto preempt;
	}

	return;

preempt:
	resched_curr(rq);
	/*
	 * Only set the backward buddy when the current task is still
	 * on the rq. This can happen when a wakeup gets interleaved
	 * with schedule on the ->pre_schedule() or idle_balance()
	 * point, either of which can * drop the rq lock.
	 *
	 * Also, during early boot the idle thread is in the fair class,
	 * for obvious reasons its a bad idea to schedule back to it.
	 */
	if (unlikely(!se->on_rq || curr == rq->idle))
		return;

	if (sched_feat(LAST_BUDDY) && scale && entity_is_task(se))
		set_last_buddy(se);
}

static struct task_struct *
pick_next_task_fair(struct rq *rq, struct task_struct *prev)
{
	struct cfs_rq *cfs_rq = &rq->cfs;
	struct sched_entity *se;
	struct task_struct *p;
	int new_tasks;

again:
#ifdef CONFIG_FAIR_GROUP_SCHED
	if (!cfs_rq->nr_running)
		goto idle;

	if (prev->sched_class != &fair_sched_class)
		goto simple;

	/*
	 * Because of the set_next_buddy() in dequeue_task_fair() it is rather
	 * likely that a next task is from the same cgroup as the current.
	 *
	 * Therefore attempt to avoid putting and setting the entire cgroup
	 * hierarchy, only change the part that actually changes.
	 */

	do {
		struct sched_entity *curr = cfs_rq->curr;

		/*
		 * Since we got here without doing put_prev_entity() we also
		 * have to consider cfs_rq->curr. If it is still a runnable
		 * entity, update_curr() will update its vruntime, otherwise
		 * forget we've ever seen it.
		 */
		if (curr) {
			if (curr->on_rq)
				update_curr(cfs_rq);
			else
				curr = NULL;

			/*
			 * This call to check_cfs_rq_runtime() will do the
			 * throttle and dequeue its entity in the parent(s).
			 * Therefore the 'simple' nr_running test will indeed
			 * be correct.
			 */
			if (unlikely(check_cfs_rq_runtime(cfs_rq)))
				goto simple;
		}

		se = pick_next_entity(cfs_rq, curr);
		cfs_rq = group_cfs_rq(se);
	} while (cfs_rq);

	p = task_of(se);

	/*
	 * Since we haven't yet done put_prev_entity and if the selected task
	 * is a different task than we started out with, try and touch the
	 * least amount of cfs_rqs.
	 */
	if (prev != p) {
		struct sched_entity *pse = &prev->se;

		while (!(cfs_rq = is_same_group(se, pse))) {
			int se_depth = se->depth;
			int pse_depth = pse->depth;

			if (se_depth <= pse_depth) {
				put_prev_entity(cfs_rq_of(pse), pse);
				pse = parent_entity(pse);
			}
			if (se_depth >= pse_depth) {
				set_next_entity(cfs_rq_of(se), se);
				se = parent_entity(se);
			}
		}

		put_prev_entity(cfs_rq, pse);
		set_next_entity(cfs_rq, se);
	}

	if (hrtick_enabled(rq))
		hrtick_start_fair(rq, p);

	rq->misfit_task = !task_fits_max(p, rq->cpu);

	return p;
simple:
	cfs_rq = &rq->cfs;
#endif

	if (!cfs_rq->nr_running)
		goto idle;

	put_prev_task(rq, prev);

	do {
		se = pick_next_entity(cfs_rq, NULL);
		set_next_entity(cfs_rq, se);
		cfs_rq = group_cfs_rq(se);
	} while (cfs_rq);

	p = task_of(se);

	if (hrtick_enabled(rq))
		hrtick_start_fair(rq, p);

	rq->misfit_task = !task_fits_max(p, rq->cpu);

	return p;

idle:
	rq->misfit_task = 0;
	/*
	 * This is OK, because current is on_cpu, which avoids it being picked
	 * for load-balance and preemption/IRQs are still disabled avoiding
	 * further scheduler activity on it and we're being very careful to
	 * re-start the picking loop.
	 */
	lockdep_unpin_lock(&rq->lock);
	new_tasks = idle_balance(rq);
	lockdep_pin_lock(&rq->lock);
	/*
	 * Because idle_balance() releases (and re-acquires) rq->lock, it is
	 * possible for any higher priority task to appear. In that case we
	 * must re-start the pick_next_entity() loop.
	 */
	if (new_tasks < 0)
		return RETRY_TASK;

	if (new_tasks > 0)
		goto again;

	return NULL;
}

/*
 * Account for a descheduled task:
 */
static void put_prev_task_fair(struct rq *rq, struct task_struct *prev)
{
	struct sched_entity *se = &prev->se;
	struct cfs_rq *cfs_rq;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		put_prev_entity(cfs_rq, se);
	}
}

/*
 * sched_yield() is very simple
 *
 * The magic of dealing with the ->skip buddy is in pick_next_entity.
 */
static void yield_task_fair(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	struct cfs_rq *cfs_rq = task_cfs_rq(curr);
	struct sched_entity *se = &curr->se;

	/*
	 * Are we the only task in the tree?
	 */
	if (unlikely(rq->nr_running == 1))
		return;

	clear_buddies(cfs_rq, se);

	if (curr->policy != SCHED_BATCH) {
		update_rq_clock(rq);
		/*
		 * Update run-time statistics of the 'current'.
		 */
		update_curr(cfs_rq);
		/*
		 * Tell update_rq_clock() that we've just updated,
		 * so we don't do microscopic update in schedule()
		 * and double the fastpath cost.
		 */
		rq_clock_skip_update(rq, true);
	}

	set_skip_buddy(se);
}

static bool yield_to_task_fair(struct rq *rq, struct task_struct *p, bool preempt)
{
	struct sched_entity *se = &p->se;

	/* throttled hierarchies are not runnable */
	if (!se->on_rq || throttled_hierarchy(cfs_rq_of(se)))
		return false;

	/* Tell the scheduler that we'd really like pse to run next. */
	set_next_buddy(se);

	yield_task_fair(rq);

	return true;
}

#ifdef CONFIG_SMP
/**************************************************
 * Fair scheduling class load-balancing methods.
 *
 * BASICS
 *
 * The purpose of load-balancing is to achieve the same basic fairness the
 * per-cpu scheduler provides, namely provide a proportional amount of compute
 * time to each task. This is expressed in the following equation:
 *
 *   W_i,n/P_i == W_j,n/P_j for all i,j                               (1)
 *
 * Where W_i,n is the n-th weight average for cpu i. The instantaneous weight
 * W_i,0 is defined as:
 *
 *   W_i,0 = \Sum_j w_i,j                                             (2)
 *
 * Where w_i,j is the weight of the j-th runnable task on cpu i. This weight
 * is derived from the nice value as per prio_to_weight[].
 *
 * The weight average is an exponential decay average of the instantaneous
 * weight:
 *
 *   W'_i,n = (2^n - 1) / 2^n * W_i,n + 1 / 2^n * W_i,0               (3)
 *
 * C_i is the compute capacity of cpu i, typically it is the
 * fraction of 'recent' time available for SCHED_OTHER task execution. But it
 * can also include other factors [XXX].
 *
 * To achieve this balance we define a measure of imbalance which follows
 * directly from (1):
 *
 *   imb_i,j = max{ avg(W/C), W_i/C_i } - min{ avg(W/C), W_j/C_j }    (4)
 *
 * We them move tasks around to minimize the imbalance. In the continuous
 * function space it is obvious this converges, in the discrete case we get
 * a few fun cases generally called infeasible weight scenarios.
 *
 * [XXX expand on:
 *     - infeasible weights;
 *     - local vs global optima in the discrete case. ]
 *
 *
 * SCHED DOMAINS
 *
 * In order to solve the imbalance equation (4), and avoid the obvious O(n^2)
 * for all i,j solution, we create a tree of cpus that follows the hardware
 * topology where each level pairs two lower groups (or better). This results
 * in O(log n) layers. Furthermore we reduce the number of cpus going up the
 * tree to only the first of the previous level and we decrease the frequency
 * of load-balance at each level inv. proportional to the number of cpus in
 * the groups.
 *
 * This yields:
 *
 *     log_2 n     1     n
 *   \Sum       { --- * --- * 2^i } = O(n)                            (5)
 *     i = 0      2^i   2^i
 *                               `- size of each group
 *         |         |     `- number of cpus doing load-balance
 *         |         `- freq
 *         `- sum over all levels
 *
 * Coupled with a limit on how many tasks we can migrate every balance pass,
 * this makes (5) the runtime complexity of the balancer.
 *
 * An important property here is that each CPU is still (indirectly) connected
 * to every other cpu in at most O(log n) steps:
 *
 * The adjacency matrix of the resulting graph is given by:
 *
 *             log_2 n     
 *   A_i,j = \Union     (i % 2^k == 0) && i / 2^(k+1) == j / 2^(k+1)  (6)
 *             k = 0
 *
 * And you'll find that:
 *
 *   A^(log_2 n)_i,j != 0  for all i,j                                (7)
 *
 * Showing there's indeed a path between every cpu in at most O(log n) steps.
 * The task movement gives a factor of O(m), giving a convergence complexity
 * of:
 *
 *   O(nm log n),  n := nr_cpus, m := nr_tasks                        (8)
 *
 *
 * WORK CONSERVING
 *
 * In order to avoid CPUs going idle while there's still work to do, new idle
 * balancing is more aggressive and has the newly idle cpu iterate up the domain
 * tree itself instead of relying on other CPUs to bring it work.
 *
 * This adds some complexity to both (5) and (8) but it reduces the total idle
 * time.
 *
 * [XXX more?]
 *
 *
 * CGROUPS
 *
 * Cgroups make a horror show out of (2), instead of a simple sum we get:
 *
 *                                s_k,i
 *   W_i,0 = \Sum_j \Prod_k w_k * -----                               (9)
 *                                 S_k
 *
 * Where
 *
 *   s_k,i = \Sum_j w_i,j,k  and  S_k = \Sum_i s_k,i                 (10)
 *
 * w_i,j,k is the weight of the j-th runnable task in the k-th cgroup on cpu i.
 *
 * The big problem is S_k, its a global sum needed to compute a local (W_i)
 * property.
 *
 * [XXX write more on how we solve this.. _after_ merging pjt's patches that
 *      rewrite all of this once again.]
 */ 

static unsigned long __read_mostly max_load_balance_interval = HZ/10;

enum fbq_type { regular, remote, all };

enum group_type {
	group_other = 0,
	group_misfit_task,
	group_imbalanced,
	group_overloaded,
};

#define LBF_ALL_PINNED	0x01
#define LBF_NEED_BREAK	0x02
#define LBF_DST_PINNED  0x04
#define LBF_SOME_PINNED	0x08

struct lb_env {
	struct sched_domain	*sd;

	struct rq		*src_rq;
	int			src_cpu;

	int			dst_cpu;
	struct rq		*dst_rq;

	struct cpumask		*dst_grpmask;
	int			new_dst_cpu;
	enum cpu_idle_type	idle;
	long			imbalance;
	unsigned int		src_grp_nr_running;
	/* The set of CPUs under consideration for load-balancing */
	struct cpumask		*cpus;

	unsigned int		flags;

	unsigned int		loop;
	unsigned int		loop_break;
	unsigned int		loop_max;

	enum fbq_type		fbq_type;
	enum group_type		busiest_group_type;
	struct list_head	tasks;
};

/*
 * Is this task likely cache-hot:
 */
static int task_hot(struct task_struct *p, struct lb_env *env)
{
	s64 delta;

	lockdep_assert_held(&env->src_rq->lock);

	if (p->sched_class != &fair_sched_class)
		return 0;

	if (unlikely(p->policy == SCHED_IDLE))
		return 0;

	/*
	 * Buddy candidates are cache hot:
	 */
	if (sched_feat(CACHE_HOT_BUDDY) && env->dst_rq->nr_running &&
			(&p->se == cfs_rq_of(&p->se)->next ||
			 &p->se == cfs_rq_of(&p->se)->last))
		return 1;

	if (sysctl_sched_migration_cost == -1)
		return 1;
	if (sysctl_sched_migration_cost == 0)
		return 0;

	delta = rq_clock_task(env->src_rq) - p->se.exec_start;

	return delta < (s64)sysctl_sched_migration_cost;
}

#ifdef CONFIG_NUMA_BALANCING
/*
 * Returns 1, if task migration degrades locality
 * Returns 0, if task migration improves locality i.e migration preferred.
 * Returns -1, if task migration is not affected by locality.
 */
static int migrate_degrades_locality(struct task_struct *p, struct lb_env *env)
{
	struct numa_group *numa_group = rcu_dereference(p->numa_group);
	unsigned long src_faults, dst_faults;
	int src_nid, dst_nid;

	if (!static_branch_likely(&sched_numa_balancing))
		return -1;

	if (!p->numa_faults || !(env->sd->flags & SD_NUMA))
		return -1;

	src_nid = cpu_to_node(env->src_cpu);
	dst_nid = cpu_to_node(env->dst_cpu);

	if (src_nid == dst_nid)
		return -1;

	/* Migrating away from the preferred node is always bad. */
	if (src_nid == p->numa_preferred_nid) {
		if (env->src_rq->nr_running > env->src_rq->nr_preferred_running)
			return 1;
		else
			return -1;
	}

	/* Encourage migration to the preferred node. */
	if (dst_nid == p->numa_preferred_nid)
		return 0;

	if (numa_group) {
		src_faults = group_faults(p, src_nid);
		dst_faults = group_faults(p, dst_nid);
	} else {
		src_faults = task_faults(p, src_nid);
		dst_faults = task_faults(p, dst_nid);
	}

	return dst_faults < src_faults;
}

#else
static inline int migrate_degrades_locality(struct task_struct *p,
					     struct lb_env *env)
{
	return -1;
}
#endif

/*
 * can_migrate_task - may task p from runqueue rq be migrated to this_cpu?
 */
static
int can_migrate_task(struct task_struct *p, struct lb_env *env)
{
	int tsk_cache_hot;

	lockdep_assert_held(&env->src_rq->lock);

	/*
	 * We do not migrate tasks that are:
	 * 1) throttled_lb_pair, or
	 * 2) cannot be migrated to this CPU due to cpus_allowed, or
	 * 3) running (obviously), or
	 * 4) are cache-hot on their current CPU.
	 */
	if (throttled_lb_pair(task_group(p), env->src_cpu, env->dst_cpu))
		return 0;

	if (!cpumask_test_cpu(env->dst_cpu, tsk_cpus_allowed(p))) {
		int cpu;

		schedstat_inc(p, se.statistics.nr_failed_migrations_affine);

		env->flags |= LBF_SOME_PINNED;

		/*
		 * Remember if this task can be migrated to any other cpu in
		 * our sched_group. We may want to revisit it if we couldn't
		 * meet load balance goals by pulling other tasks on src_cpu.
		 *
		 * Also avoid computing new_dst_cpu if we have already computed
		 * one in current iteration.
		 */
		if (!env->dst_grpmask || (env->flags & LBF_DST_PINNED))
			return 0;

		/* Prevent to re-select dst_cpu via env's cpus */
		for_each_cpu_and(cpu, env->dst_grpmask, env->cpus) {
			if (cpumask_test_cpu(cpu, tsk_cpus_allowed(p))) {
				env->flags |= LBF_DST_PINNED;
				env->new_dst_cpu = cpu;
				break;
			}
		}

		return 0;
	}

	/* Record that we found atleast one task that could run on dst_cpu */
	env->flags &= ~LBF_ALL_PINNED;

	if (task_running(env->src_rq, p)) {
		schedstat_inc(p, se.statistics.nr_failed_migrations_running);
		return 0;
	}

	/*
	 * Aggressive migration if:
	 * 1) destination numa is preferred
	 * 2) task is cache cold, or
	 * 3) too many balance attempts have failed.
	 */
	tsk_cache_hot = migrate_degrades_locality(p, env);
	if (tsk_cache_hot == -1)
		tsk_cache_hot = task_hot(p, env);

	if (tsk_cache_hot <= 0 ||
	    env->sd->nr_balance_failed > env->sd->cache_nice_tries) {
		if (tsk_cache_hot == 1) {
			schedstat_inc(env->sd, lb_hot_gained[env->idle]);
			schedstat_inc(p, se.statistics.nr_forced_migrations);
		}
		return 1;
	}

	schedstat_inc(p, se.statistics.nr_failed_migrations_hot);
	return 0;
}

/*
 * detach_task() -- detach the task for the migration specified in env
 */
static void detach_task(struct task_struct *p, struct lb_env *env)
{
	lockdep_assert_held(&env->src_rq->lock);

	deactivate_task(env->src_rq, p, 0);
	p->on_rq = TASK_ON_RQ_MIGRATING;
	double_lock_balance(env->src_rq, env->dst_rq);
	set_task_cpu(p, env->dst_cpu);
	double_unlock_balance(env->src_rq, env->dst_rq);
}

/*
 * detach_one_task() -- tries to dequeue exactly one task from env->src_rq, as
 * part of active balancing operations within "domain".
 *
 * Returns a task if successful and NULL otherwise.
 */
static struct task_struct *detach_one_task(struct lb_env *env)
{
	struct task_struct *p, *n;

	lockdep_assert_held(&env->src_rq->lock);

	list_for_each_entry_safe(p, n, &env->src_rq->cfs_tasks, se.group_node) {
		if (!can_migrate_task(p, env))
			continue;

		detach_task(p, env);

		/*
		 * Right now, this is only the second place where
		 * lb_gained[env->idle] is updated (other is detach_tasks)
		 * so we can safely collect stats here rather than
		 * inside detach_tasks().
		 */
		schedstat_inc(env->sd, lb_gained[env->idle]);
		return p;
	}
	return NULL;
}

static const unsigned int sched_nr_migrate_break = 32;

/*
 * detach_tasks() -- tries to detach up to imbalance weighted load from
 * busiest_rq, as part of a balancing operation within domain "sd".
 *
 * Returns number of detached tasks if successful and 0 otherwise.
 */
static int detach_tasks(struct lb_env *env)
{
	struct list_head *tasks = &env->src_rq->cfs_tasks;
	struct task_struct *p;
	unsigned long load;
	int detached = 0;

	lockdep_assert_held(&env->src_rq->lock);

	if (env->imbalance <= 0)
		return 0;

	while (!list_empty(tasks)) {
		/*
		 * We don't want to steal all, otherwise we may be treated likewise,
		 * which could at worst lead to a livelock crash.
		 */
		if (env->idle != CPU_NOT_IDLE && env->src_rq->nr_running <= 1)
			break;

		p = list_first_entry(tasks, struct task_struct, se.group_node);

		env->loop++;
		/* We've more or less seen every task there is, call it quits */
		if (env->loop > env->loop_max)
			break;

		/* take a breather every nr_migrate tasks */
		if (env->loop > env->loop_break) {
			env->loop_break += sched_nr_migrate_break;
			env->flags |= LBF_NEED_BREAK;
			break;
		}

		if (!can_migrate_task(p, env))
			goto next;

		load = task_h_load(p);

		if (sched_feat(LB_MIN) && load < 16 && !env->sd->nr_balance_failed)
			goto next;

		if ((load / 2) > env->imbalance)
			goto next;

		detach_task(p, env);
		list_add(&p->se.group_node, &env->tasks);

		detached++;
		env->imbalance -= load;

#ifdef CONFIG_PREEMPT
		/*
		 * NEWIDLE balancing is a source of latency, so preemptible
		 * kernels will stop after the first task is detached to minimize
		 * the critical section.
		 */
		if (env->idle == CPU_NEWLY_IDLE)
			break;
#endif

		/*
		 * We only want to steal up to the prescribed amount of
		 * weighted load.
		 */
		if (env->imbalance <= 0)
			break;

		continue;
next:
		list_move_tail(&p->se.group_node, tasks);
	}

	/*
	 * Right now, this is one of only two places we collect this stat
	 * so we can safely collect detach_one_task() stats here rather
	 * than inside detach_one_task().
	 */
	schedstat_add(env->sd, lb_gained[env->idle], detached);

	return detached;
}

/*
 * attach_task() -- attach the task detached by detach_task() to its new rq.
 */
static void attach_task(struct rq *rq, struct task_struct *p)
{
	lockdep_assert_held(&rq->lock);

	BUG_ON(task_rq(p) != rq);
	p->on_rq = TASK_ON_RQ_QUEUED;
	activate_task(rq, p, 0);
	check_preempt_curr(rq, p, 0);
}

/*
 * attach_one_task() -- attaches the task returned from detach_one_task() to
 * its new rq.
 */
static void attach_one_task(struct rq *rq, struct task_struct *p)
{
	raw_spin_lock(&rq->lock);
	attach_task(rq, p);
	raw_spin_unlock(&rq->lock);
}

/*
 * attach_tasks() -- attaches all tasks detached by detach_tasks() to their
 * new rq.
 */
static void attach_tasks(struct lb_env *env)
{
	struct list_head *tasks = &env->tasks;
	struct task_struct *p;

	raw_spin_lock(&env->dst_rq->lock);

	while (!list_empty(tasks)) {
		p = list_first_entry(tasks, struct task_struct, se.group_node);
		list_del_init(&p->se.group_node);

		attach_task(env->dst_rq, p);
	}

	raw_spin_unlock(&env->dst_rq->lock);
}

#ifdef CONFIG_FAIR_GROUP_SCHED
static void update_blocked_averages(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	struct cfs_rq *cfs_rq;
	unsigned long flags;

	raw_spin_lock_irqsave(&rq->lock, flags);
	update_rq_clock(rq);

	/*
	 * Iterates the task_group tree in a bottom up fashion, see
	 * list_add_leaf_cfs_rq() for details.
	 */
	for_each_leaf_cfs_rq(rq, cfs_rq) {
		/* throttled entities do not contribute to load */
		if (throttled_hierarchy(cfs_rq))
			continue;

		if (update_cfs_rq_load_avg(cfs_rq_clock_task(cfs_rq), cfs_rq,
					   true))
			update_tg_load_avg(cfs_rq, 0);

		/* Propagate pending load changes to the parent */
		if (cfs_rq->tg->se[cpu])
			update_load_avg(cfs_rq->tg->se[cpu], 0);
	}
	raw_spin_unlock_irqrestore(&rq->lock, flags);
}

/*
 * Compute the hierarchical load factor for cfs_rq and all its ascendants.
 * This needs to be done in a top-down fashion because the load of a child
 * group is a fraction of its parents load.
 */
static void update_cfs_rq_h_load(struct cfs_rq *cfs_rq)
{
	struct rq *rq = rq_of(cfs_rq);
	struct sched_entity *se = cfs_rq->tg->se[cpu_of(rq)];
	unsigned long now = jiffies;
	unsigned long load;

	if (cfs_rq->last_h_load_update == now)
		return;

	cfs_rq->h_load_next = NULL;
	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		cfs_rq->h_load_next = se;
		if (cfs_rq->last_h_load_update == now)
			break;
	}

	if (!se) {
		cfs_rq->h_load = cfs_rq_load_avg(cfs_rq);
		cfs_rq->last_h_load_update = now;
	}

	while ((se = cfs_rq->h_load_next) != NULL) {
		load = cfs_rq->h_load;
		load = div64_ul(load * se->avg.load_avg,
			cfs_rq_load_avg(cfs_rq) + 1);
		cfs_rq = group_cfs_rq(se);
		cfs_rq->h_load = load;
		cfs_rq->last_h_load_update = now;
	}
}

static unsigned long task_h_load(struct task_struct *p)
{
	struct cfs_rq *cfs_rq = task_cfs_rq(p);

	update_cfs_rq_h_load(cfs_rq);
	return div64_ul(p->se.avg.load_avg * cfs_rq->h_load,
			cfs_rq_load_avg(cfs_rq) + 1);
}
#else
static inline void update_blocked_averages(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	struct cfs_rq *cfs_rq = &rq->cfs;
	unsigned long flags;

	raw_spin_lock_irqsave(&rq->lock, flags);
	update_rq_clock(rq);
	update_cfs_rq_load_avg(cfs_rq_clock_task(cfs_rq), cfs_rq, true);
	raw_spin_unlock_irqrestore(&rq->lock, flags);
}

static unsigned long task_h_load(struct task_struct *p)
{
	return p->se.avg.load_avg;
}
#endif

/********** Helpers for find_busiest_group ************************/

/*
 * sg_lb_stats - stats of a sched_group required for load_balancing
 */
struct sg_lb_stats {
	unsigned long avg_load; /*Avg load across the CPUs of the group */
	unsigned long group_load; /* Total load over the CPUs of the group */
	unsigned long sum_weighted_load; /* Weighted load of group's tasks */
	unsigned long load_per_task;
	unsigned long group_capacity;
	unsigned long group_util; /* Total utilization of the group */
	unsigned int sum_nr_running; /* Nr tasks running in the group */
	unsigned int idle_cpus;
	unsigned int group_weight;
	enum group_type group_type;
	int group_no_capacity;
	int group_misfit_task; /* A cpu has a task too big for its capacity */
#ifdef CONFIG_NUMA_BALANCING
	unsigned int nr_numa_running;
	unsigned int nr_preferred_running;
#endif
};

/*
 * sd_lb_stats - Structure to store the statistics of a sched_domain
 *		 during load balancing.
 */
struct sd_lb_stats {
	struct sched_group *busiest;	/* Busiest group in this sd */
	struct sched_group *local;	/* Local group in this sd */
	unsigned long total_load;	/* Total load of all groups in sd */
	unsigned long total_capacity;	/* Total capacity of all groups in sd */
	unsigned long avg_load;	/* Average load across all groups in sd */

	struct sg_lb_stats busiest_stat;/* Statistics of the busiest group */
	struct sg_lb_stats local_stat;	/* Statistics of the local group */
};

static inline void init_sd_lb_stats(struct sd_lb_stats *sds)
{
	/*
	 * Skimp on the clearing to avoid duplicate work. We can avoid clearing
	 * local_stat because update_sg_lb_stats() does a full clear/assignment.
	 * We must however clear busiest_stat::avg_load because
	 * update_sd_pick_busiest() reads this before assignment.
	 */
	*sds = (struct sd_lb_stats){
		.busiest = NULL,
		.local = NULL,
		.total_load = 0UL,
		.total_capacity = 0UL,
		.busiest_stat = {
			.avg_load = 0UL,
			.sum_nr_running = 0,
			.group_type = group_other,
		},
	};
}

/**
 * get_sd_load_idx - Obtain the load index for a given sched domain.
 * @sd: The sched_domain whose load_idx is to be obtained.
 * @idle: The idle status of the CPU for whose sd load_idx is obtained.
 *
 * Return: The load index.
 */
static inline int get_sd_load_idx(struct sched_domain *sd,
					enum cpu_idle_type idle)
{
	int load_idx;

	switch (idle) {
	case CPU_NOT_IDLE:
		load_idx = sd->busy_idx;
		break;

	case CPU_NEWLY_IDLE:
		load_idx = sd->newidle_idx;
		break;
	default:
		load_idx = sd->idle_idx;
		break;
	}

	return load_idx;
}

static unsigned long scale_rt_capacity(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	u64 total, used, age_stamp, avg;
	s64 delta;

	/*
	 * Since we're reading these variables without serialization make sure
	 * we read them once before doing sanity checks on them.
	 */
	age_stamp = READ_ONCE(rq->age_stamp);
	avg = READ_ONCE(rq->rt_avg);
	delta = __rq_clock_broken(rq) - age_stamp;

	if (unlikely(delta < 0))
		delta = 0;

	total = sched_avg_period() + delta;

	used = div_u64(avg, total);

	/*
	 * deadline bandwidth is defined at system level so we must
	 * weight this bandwidth with the max capacity of the system.
	 * As a reminder, avg_bw is 20bits width and
	 * scale_cpu_capacity is 10 bits width
	 */
	used += div_u64(rq->dl.avg_bw, arch_scale_cpu_capacity(NULL, cpu));

	if (likely(used < SCHED_CAPACITY_SCALE))
		return SCHED_CAPACITY_SCALE - used;

	return 1;
}

void init_max_cpu_capacity(struct max_cpu_capacity *mcc)
{
	raw_spin_lock_init(&mcc->lock);
	mcc->val = 0;
	mcc->cpu = -1;
}

static void update_cpu_capacity(struct sched_domain *sd, int cpu)
{
	unsigned long capacity = arch_scale_cpu_capacity(sd, cpu);
	struct sched_group *sdg = sd->groups;
	struct max_cpu_capacity *mcc;
	unsigned long max_capacity;
	int max_cap_cpu;
	unsigned long flags;

	cpu_rq(cpu)->cpu_capacity_orig = capacity;

	mcc = &cpu_rq(cpu)->rd->max_cpu_capacity;

	raw_spin_lock_irqsave(&mcc->lock, flags);
	max_capacity = mcc->val;
	max_cap_cpu = mcc->cpu;

	if ((max_capacity > capacity && max_cap_cpu == cpu) ||
	    (max_capacity < capacity)) {
		mcc->val = capacity;
		mcc->cpu = cpu;
#ifdef CONFIG_SCHED_DEBUG
		raw_spin_unlock_irqrestore(&mcc->lock, flags);
/*
		printk_deferred(KERN_INFO "CPU%d: update max cpu_capacity %lu\n",
				cpu, capacity);
*/
		goto skip_unlock;
#endif
	}
	raw_spin_unlock_irqrestore(&mcc->lock, flags);

skip_unlock: __attribute__ ((unused));
	capacity *= scale_rt_capacity(cpu);
	capacity >>= SCHED_CAPACITY_SHIFT;

	if (!capacity)
		capacity = 1;

	cpu_rq(cpu)->cpu_capacity = capacity;
	sdg->sgc->capacity = capacity;
	sdg->sgc->max_capacity = capacity;
	sdg->sgc->min_capacity = capacity;
}

void update_group_capacity(struct sched_domain *sd, int cpu)
{
	struct sched_domain *child = sd->child;
	struct sched_group *group, *sdg = sd->groups;
	unsigned long capacity, max_capacity, min_capacity;
	unsigned long interval;

	interval = msecs_to_jiffies(sd->balance_interval);
	interval = clamp(interval, 1UL, max_load_balance_interval);
	sdg->sgc->next_update = jiffies + interval;

	if (!child) {
		update_cpu_capacity(sd, cpu);
		return;
	}

	capacity = 0;
	max_capacity = 0;
	min_capacity = ULONG_MAX;

	if (child->flags & SD_OVERLAP) {
		/*
		 * SD_OVERLAP domains cannot assume that child groups
		 * span the current group.
		 */

		for_each_cpu(cpu, sched_group_cpus(sdg)) {
			struct sched_group_capacity *sgc;
			struct rq *rq = cpu_rq(cpu);

			/*
			 * build_sched_domains() -> init_sched_groups_capacity()
			 * gets here before we've attached the domains to the
			 * runqueues.
			 *
			 * Use capacity_of(), which is set irrespective of domains
			 * in update_cpu_capacity().
			 *
			 * This avoids capacity from being 0 and
			 * causing divide-by-zero issues on boot.
			 */
			if (unlikely(!rq->sd)) {
				capacity += capacity_of(cpu);
			} else {
				sgc = rq->sd->groups->sgc;
				capacity += sgc->capacity;
			}

			max_capacity = max(capacity, max_capacity);
			min_capacity = min(capacity, min_capacity);
		}
	} else  {
		/*
		 * !SD_OVERLAP domains can assume that child groups
		 * span the current group.
		 */ 

		group = child->groups;
		do {
			struct sched_group_capacity *sgc = group->sgc;

			capacity += sgc->capacity;
			max_capacity = max(sgc->max_capacity, max_capacity);
			min_capacity = min(sgc->min_capacity, min_capacity);
			group = group->next;
		} while (group != child->groups);
	}

	sdg->sgc->capacity = capacity;
	sdg->sgc->max_capacity = max_capacity;
	sdg->sgc->min_capacity = min_capacity;
}

/*
 * Check whether the capacity of the rq has been noticeably reduced by side
 * activity. The imbalance_pct is used for the threshold.
 * Return true is the capacity is reduced
 */
static inline int
check_cpu_capacity(struct rq *rq, struct sched_domain *sd)
{
	return ((rq->cpu_capacity * sd->imbalance_pct) <
				(rq->cpu_capacity_orig * 100));
}

/*
 * Group imbalance indicates (and tries to solve) the problem where balancing
 * groups is inadequate due to tsk_cpus_allowed() constraints.
 *
 * Imagine a situation of two groups of 4 cpus each and 4 tasks each with a
 * cpumask covering 1 cpu of the first group and 3 cpus of the second group.
 * Something like:
 *
 * 	{ 0 1 2 3 } { 4 5 6 7 }
 * 	        *     * * *
 *
 * If we were to balance group-wise we'd place two tasks in the first group and
 * two tasks in the second group. Clearly this is undesired as it will overload
 * cpu 3 and leave one of the cpus in the second group unused.
 *
 * The current solution to this issue is detecting the skew in the first group
 * by noticing the lower domain failed to reach balance and had difficulty
 * moving tasks due to affinity constraints.
 *
 * When this is so detected; this group becomes a candidate for busiest; see
 * update_sd_pick_busiest(). And calculate_imbalance() and
 * find_busiest_group() avoid some of the usual balance conditions to allow it
 * to create an effective group imbalance.
 *
 * This is a somewhat tricky proposition since the next run might not find the
 * group imbalance and decide the groups need to be balanced again. A most
 * subtle and fragile situation.
 */

static inline int sg_imbalanced(struct sched_group *group)
{
	return group->sgc->imbalance;
}

/*
 * group_has_capacity returns true if the group has spare capacity that could
 * be used by some tasks.
 * We consider that a group has spare capacity if the  * number of task is
 * smaller than the number of CPUs or if the utilization is lower than the
 * available capacity for CFS tasks.
 * For the latter, we use a threshold to stabilize the state, to take into
 * account the variance of the tasks' load and to return true if the available
 * capacity in meaningful for the load balancer.
 * As an example, an available capacity of 1% can appear but it doesn't make
 * any benefit for the load balance.
 */
static inline bool
group_has_capacity(struct lb_env *env, struct sg_lb_stats *sgs)
{
	if (sgs->sum_nr_running < sgs->group_weight)
		return true;

	if ((sgs->group_capacity * 100) >
			(sgs->group_util * env->sd->imbalance_pct))
		return true;

	return false;
}

/*
 *  group_is_overloaded returns true if the group has more tasks than it can
 *  handle.
 *  group_is_overloaded is not equals to !group_has_capacity because a group
 *  with the exact right number of tasks, has no more spare capacity but is not
 *  overloaded so both group_has_capacity and group_is_overloaded return
 *  false.
 */
static inline bool
group_is_overloaded(struct lb_env *env, struct sg_lb_stats *sgs)
{
	if (sgs->sum_nr_running <= sgs->group_weight)
		return false;

	if ((sgs->group_capacity * 100) <
			(sgs->group_util * env->sd->imbalance_pct))
		return true;

	return false;
}


/*
 * group_smaller_cpu_capacity: Returns true if sched_group sg has smaller
 * per-cpu capacity than sched_group ref.
 */
static inline bool
group_smaller_cpu_capacity(struct sched_group *sg, struct sched_group *ref)
{
	return sg->sgc->max_capacity + capacity_margin - SCHED_LOAD_SCALE <
							ref->sgc->max_capacity;
}

static inline enum
group_type group_classify(struct sched_group *group,
			  struct sg_lb_stats *sgs)
{
	if (sgs->group_no_capacity)
		return group_overloaded;

	if (sg_imbalanced(group))
		return group_imbalanced;

	if (sgs->group_misfit_task)
		return group_misfit_task;

	return group_other;
}

#ifdef CONFIG_NO_HZ_COMMON
/*
 * idle load balancing data
 *  - used by the nohz balance, but we want it available here
 *    so that we can see which CPUs have no tick.
 */
static struct {
	cpumask_var_t idle_cpus_mask;
	atomic_t nr_cpus;
	unsigned long next_balance;     /* in jiffy units */
} nohz ____cacheline_aligned;

static inline void update_cpu_stats_if_tickless(struct rq *rq)
{
	/* only called from update_sg_lb_stats when irqs are disabled */
	if (cpumask_test_cpu(rq->cpu, nohz.idle_cpus_mask)) {
		/* rate limit updates to once-per-jiffie at most */
		if (READ_ONCE(jiffies) <= rq->last_load_update_tick)
			return;

		raw_spin_lock(&rq->lock);
		update_rq_clock(rq);
		update_idle_cpu_load(rq);
		update_cfs_rq_load_avg(rq->clock_task, &rq->cfs, false);
		raw_spin_unlock(&rq->lock);
	}
}

#else
static inline void update_cpu_stats_if_tickless(struct rq *rq) { }
#endif

/**
 * update_sg_lb_stats - Update sched_group's statistics for load balancing.
 * @env: The load balancing environment.
 * @group: sched_group whose statistics are to be updated.
 * @load_idx: Load index of sched_domain of this_cpu for load calc.
 * @local_group: Does group contain this_cpu.
 * @sgs: variable to hold the statistics for this group.
 * @overload: Indicate more than one runnable task for any CPU.
 * @overutilized: Indicate overutilization for any CPU.
 */
static inline void update_sg_lb_stats(struct lb_env *env,
			struct sched_group *group, int load_idx,
			int local_group, struct sg_lb_stats *sgs,
			bool *overload, bool *overutilized)
{
	unsigned long load;
	int i, nr_running;

	memset(sgs, 0, sizeof(*sgs));

	for_each_cpu_and(i, sched_group_cpus(group), env->cpus) {
		struct rq *rq = cpu_rq(i);

		/* if we are entering idle and there are CPUs with
		 * their tick stopped, do an update for them
		 */
		if (env->idle == CPU_NEWLY_IDLE)
			update_cpu_stats_if_tickless(rq);

		/* Bias balancing toward cpus of our domain */
		if (local_group)
			load = target_load(i, load_idx);
		else
			load = source_load(i, load_idx);

		sgs->group_load += load;
		sgs->group_util += cpu_util(i);
		sgs->sum_nr_running += rq->cfs.h_nr_running;

		nr_running = rq->nr_running;
		if (nr_running > 1)
			*overload = true;

#ifdef CONFIG_NUMA_BALANCING
		sgs->nr_numa_running += rq->nr_numa_running;
		sgs->nr_preferred_running += rq->nr_preferred_running;
#endif
		sgs->sum_weighted_load += weighted_cpuload(i);
		/*
		 * No need to call idle_cpu() if nr_running is not 0
		 */
		if (!nr_running && idle_cpu(i))
			sgs->idle_cpus++;

		if (cpu_overutilized(i)) {
			*overutilized = true;
			if (!sgs->group_misfit_task && rq->misfit_task)
				sgs->group_misfit_task = capacity_of(i);
		}
	}

	/* Adjust by relative CPU capacity of the group */
	sgs->group_capacity = group->sgc->capacity;
	sgs->avg_load = (sgs->group_load*SCHED_CAPACITY_SCALE) / sgs->group_capacity;

	if (sgs->sum_nr_running)
		sgs->load_per_task = sgs->sum_weighted_load / sgs->sum_nr_running;

	sgs->group_weight = group->group_weight;

	sgs->group_no_capacity = group_is_overloaded(env, sgs);
	sgs->group_type = group_classify(group, sgs);
}

/**
 * update_sd_pick_busiest - return 1 on busiest group
 * @env: The load balancing environment.
 * @sds: sched_domain statistics
 * @sg: sched_group candidate to be checked for being the busiest
 * @sgs: sched_group statistics
 *
 * Determine if @sg is a busier group than the previously selected
 * busiest group.
 *
 * Return: %true if @sg is a busier group than the previously selected
 * busiest group. %false otherwise.
 */
static bool update_sd_pick_busiest(struct lb_env *env,
				   struct sd_lb_stats *sds,
				   struct sched_group *sg,
				   struct sg_lb_stats *sgs)
{
	struct sg_lb_stats *busiest = &sds->busiest_stat;

	if (sgs->group_type > busiest->group_type)
		return true;

	if (sgs->group_type < busiest->group_type)
		return false;

	/*
	 * Candidate sg doesn't face any serious load-balance problems
	 * so don't pick it if the local sg is already filled up.
	 */
	if (sgs->group_type == group_other &&
	    !group_has_capacity(env, &sds->local_stat))
		return false;

	if (sgs->avg_load <= busiest->avg_load)
		return false;

	if (!(env->sd->flags & SD_ASYM_CPUCAPACITY))
		goto asym_packing;

	/*
	 * Candidate sg has no more than one task per CPU and
	 * has higher per-CPU capacity. Migrating tasks to less
	 * capable CPUs may harm throughput. Maximize throughput,
	 * power/energy consequences are not considered.
	 */
	if (sgs->sum_nr_running <= sgs->group_weight &&
	    group_smaller_cpu_capacity(sds->local, sg))
		return false;

asym_packing:
	/* This is the busiest node in its class. */
	if (!(env->sd->flags & SD_ASYM_PACKING))
		return true;

	/*
	 * ASYM_PACKING needs to move all the work to the lowest
	 * numbered CPUs in the group, therefore mark all groups
	 * higher than ourself as busy.
	 */
	if (sgs->sum_nr_running && env->dst_cpu < group_first_cpu(sg)) {
		if (!sds->busiest)
			return true;

		if (group_first_cpu(sds->busiest) > group_first_cpu(sg))
			return true;
	}

	return false;
}

#ifdef CONFIG_NUMA_BALANCING
static inline enum fbq_type fbq_classify_group(struct sg_lb_stats *sgs)
{
	if (sgs->sum_nr_running > sgs->nr_numa_running)
		return regular;
	if (sgs->sum_nr_running > sgs->nr_preferred_running)
		return remote;
	return all;
}

static inline enum fbq_type fbq_classify_rq(struct rq *rq)
{
	if (rq->nr_running > rq->nr_numa_running)
		return regular;
	if (rq->nr_running > rq->nr_preferred_running)
		return remote;
	return all;
}
#else
static inline enum fbq_type fbq_classify_group(struct sg_lb_stats *sgs)
{
	return all;
}

static inline enum fbq_type fbq_classify_rq(struct rq *rq)
{
	return regular;
}
#endif /* CONFIG_NUMA_BALANCING */

#define lb_sd_parent(sd) \
	(sd->parent && sd->parent->groups != sd->parent->groups->next)

/**
 * update_sd_lb_stats - Update sched_domain's statistics for load balancing.
 * @env: The load balancing environment.
 * @sds: variable to hold the statistics for this sched_domain.
 */
static inline void update_sd_lb_stats(struct lb_env *env, struct sd_lb_stats *sds)
{
	struct sched_domain *child = env->sd->child;
	struct sched_group *sg = env->sd->groups;
	struct sg_lb_stats tmp_sgs;
	int load_idx, prefer_sibling = 0;
	bool overload = false, overutilized = false;

	if (child && child->flags & SD_PREFER_SIBLING)
		prefer_sibling = 1;

	load_idx = get_sd_load_idx(env->sd, env->idle);

	do {
		struct sg_lb_stats *sgs = &tmp_sgs;
		int local_group;

		local_group = cpumask_test_cpu(env->dst_cpu, sched_group_cpus(sg));
		if (local_group) {
			sds->local = sg;
			sgs = &sds->local_stat;

			if (env->idle != CPU_NEWLY_IDLE ||
			    time_after_eq(jiffies, sg->sgc->next_update))
				update_group_capacity(env->sd, env->dst_cpu);
		}

		update_sg_lb_stats(env, sg, load_idx, local_group, sgs,
						&overload, &overutilized);

		if (local_group)
			goto next_group;

		/*
		 * In case the child domain prefers tasks go to siblings
		 * first, lower the sg capacity so that we'll try
		 * and move all the excess tasks away. We lower the capacity
		 * of a group only if the local group has the capacity to fit
		 * these excess tasks. The extra check prevents the case where
		 * you always pull from the heaviest group when it is already
		 * under-utilized (possible with a large weight task outweighs
		 * the tasks on the system).
		 */
		if (prefer_sibling && sds->local &&
		    group_has_capacity(env, &sds->local_stat) &&
		    (sgs->sum_nr_running > 1)) {
			sgs->group_no_capacity = 1;
			sgs->group_type = group_classify(sg, sgs);
		}

		/*
		 * Ignore task groups with misfit tasks if local group has no
		 * capacity or if per-cpu capacity isn't higher.
		 */
		if (sgs->group_type == group_misfit_task &&
		    (!group_has_capacity(env, &sds->local_stat) ||
		     !group_smaller_cpu_capacity(sg, sds->local)))
			sgs->group_type = group_other;

		if (update_sd_pick_busiest(env, sds, sg, sgs)) {
			sds->busiest = sg;
			sds->busiest_stat = *sgs;
		}

next_group:
		/* Now, start updating sd_lb_stats */
		sds->total_load += sgs->group_load;
		sds->total_capacity += sgs->group_capacity;

		sg = sg->next;
	} while (sg != env->sd->groups);

	if (env->sd->flags & SD_NUMA)
		env->fbq_type = fbq_classify_group(&sds->busiest_stat);

	env->src_grp_nr_running = sds->busiest_stat.sum_nr_running;

	if (!lb_sd_parent(env->sd)) {
		/* update overload indicator if we are at root domain */
		if (env->dst_rq->rd->overload != overload)
			env->dst_rq->rd->overload = overload;

		/* Update over-utilization (tipping point, U >= 0) indicator */
		if (env->dst_rq->rd->overutilized != overutilized) {
			env->dst_rq->rd->overutilized = overutilized;
			trace_sched_overutilized(overutilized);
		}
	} else {
		if (!env->dst_rq->rd->overutilized && overutilized) {
			env->dst_rq->rd->overutilized = true;
			trace_sched_overutilized(true);
		}
	}

}

/**
 * check_asym_packing - Check to see if the group is packed into the
 *			sched doman.
 *
 * This is primarily intended to used at the sibling level.  Some
 * cores like POWER7 prefer to use lower numbered SMT threads.  In the
 * case of POWER7, it can move to lower SMT modes only when higher
 * threads are idle.  When in lower SMT modes, the threads will
 * perform better since they share less core resources.  Hence when we
 * have idle threads, we want them to be the higher ones.
 *
 * This packing function is run on idle threads.  It checks to see if
 * the busiest CPU in this domain (core in the P7 case) has a higher
 * CPU number than the packing function is being run on.  Here we are
 * assuming lower CPU number will be equivalent to lower a SMT thread
 * number.
 *
 * Return: 1 when packing is required and a task should be moved to
 * this CPU.  The amount of the imbalance is returned in *imbalance.
 *
 * @env: The load balancing environment.
 * @sds: Statistics of the sched_domain which is to be packed
 */
static int check_asym_packing(struct lb_env *env, struct sd_lb_stats *sds)
{
	int busiest_cpu;

	if (!(env->sd->flags & SD_ASYM_PACKING))
		return 0;

	if (!sds->busiest)
		return 0;

	busiest_cpu = group_first_cpu(sds->busiest);
	if (env->dst_cpu > busiest_cpu)
		return 0;

	env->imbalance = DIV_ROUND_CLOSEST(
		sds->busiest_stat.avg_load * sds->busiest_stat.group_capacity,
		SCHED_CAPACITY_SCALE);

	return 1;
}

/**
 * fix_small_imbalance - Calculate the minor imbalance that exists
 *			amongst the groups of a sched_domain, during
 *			load balancing.
 * @env: The load balancing environment.
 * @sds: Statistics of the sched_domain whose imbalance is to be calculated.
 */
static inline
void fix_small_imbalance(struct lb_env *env, struct sd_lb_stats *sds)
{
	unsigned long tmp, capa_now = 0, capa_move = 0;
	unsigned int imbn = 2;
	unsigned long scaled_busy_load_per_task;
	struct sg_lb_stats *local, *busiest;

	local = &sds->local_stat;
	busiest = &sds->busiest_stat;

	if (!local->sum_nr_running)
		local->load_per_task = cpu_avg_load_per_task(env->dst_cpu);
	else if (busiest->load_per_task > local->load_per_task)
		imbn = 1;

	scaled_busy_load_per_task =
		(busiest->load_per_task * SCHED_CAPACITY_SCALE) /
		busiest->group_capacity;

	if (busiest->avg_load + scaled_busy_load_per_task >=
	    local->avg_load + (scaled_busy_load_per_task * imbn)) {
		env->imbalance = busiest->load_per_task;
		return;
	}

	/*
	 * OK, we don't have enough imbalance to justify moving tasks,
	 * however we may be able to increase total CPU capacity used by
	 * moving them.
	 */

	capa_now += busiest->group_capacity *
			min(busiest->load_per_task, busiest->avg_load);
	capa_now += local->group_capacity *
			min(local->load_per_task, local->avg_load);
	capa_now /= SCHED_CAPACITY_SCALE;

	/* Amount of load we'd subtract */
	if (busiest->avg_load > scaled_busy_load_per_task) {
		capa_move += busiest->group_capacity *
			    min(busiest->load_per_task,
				busiest->avg_load - scaled_busy_load_per_task);
	}

	/* Amount of load we'd add */
	if (busiest->avg_load * busiest->group_capacity <
	    busiest->load_per_task * SCHED_CAPACITY_SCALE) {
		tmp = (busiest->avg_load * busiest->group_capacity) /
		      local->group_capacity;
	} else {
		tmp = (busiest->load_per_task * SCHED_CAPACITY_SCALE) /
		      local->group_capacity;
	}
	capa_move += local->group_capacity *
		    min(local->load_per_task, local->avg_load + tmp);
	capa_move /= SCHED_CAPACITY_SCALE;

	/* Move if we gain throughput */
	if (capa_move > capa_now)
		env->imbalance = busiest->load_per_task;
}

/**
 * calculate_imbalance - Calculate the amount of imbalance present within the
 *			 groups of a given sched_domain during load balance.
 * @env: load balance environment
 * @sds: statistics of the sched_domain whose imbalance is to be calculated.
 */
static inline void calculate_imbalance(struct lb_env *env, struct sd_lb_stats *sds)
{
	unsigned long max_pull, load_above_capacity = ~0UL;
	struct sg_lb_stats *local, *busiest;

	local = &sds->local_stat;
	busiest = &sds->busiest_stat;

	if (busiest->group_type == group_imbalanced) {
		/*
		 * In the group_imb case we cannot rely on group-wide averages
		 * to ensure cpu-load equilibrium, look at wider averages. XXX
		 */
		busiest->load_per_task =
			min(busiest->load_per_task, sds->avg_load);
	}

	/*
	 * In the presence of smp nice balancing, certain scenarios can have
	 * max load less than avg load(as we skip the groups at or below
	 * its cpu_capacity, while calculating max_load..)
	 */
	if (busiest->avg_load <= sds->avg_load ||
	    local->avg_load >= sds->avg_load) {
		/* Misfitting tasks should be migrated in any case */
		if (busiest->group_type == group_misfit_task) {
			env->imbalance = busiest->group_misfit_task;
			return;
		}

		/*
		 * Busiest group is overloaded, local is not, use the spare
		 * cycles to maximize throughput
		 */
		if (busiest->group_type == group_overloaded &&
		    local->group_type <= group_misfit_task) {
			env->imbalance = busiest->load_per_task;
			return;
		}

		env->imbalance = 0;
		return fix_small_imbalance(env, sds);
	}

	/*
	 * If there aren't any idle cpus, avoid creating some.
	 */
	if (busiest->group_type == group_overloaded &&
	    local->group_type   == group_overloaded) {
		load_above_capacity = busiest->sum_nr_running *
					SCHED_LOAD_SCALE;
		if (load_above_capacity > busiest->group_capacity)
			load_above_capacity -= busiest->group_capacity;
		else
			load_above_capacity = ~0UL;
	}

	/*
	 * We're trying to get all the cpus to the average_load, so we don't
	 * want to push ourselves above the average load, nor do we wish to
	 * reduce the max loaded cpu below the average load. At the same time,
	 * we also don't want to reduce the group load below the group capacity
	 * (so that we can implement power-savings policies etc). Thus we look
	 * for the minimum possible imbalance.
	 */
	max_pull = min(busiest->avg_load - sds->avg_load, load_above_capacity);

	/* How much load to actually move to equalise the imbalance */
	env->imbalance = min(
		max_pull * busiest->group_capacity,
		(sds->avg_load - local->avg_load) * local->group_capacity
	) / SCHED_CAPACITY_SCALE;

	/* Boost imbalance to allow misfit task to be balanced. */
	if (busiest->group_type == group_misfit_task)
		env->imbalance = max_t(long, env->imbalance,
				     busiest->group_misfit_task);

	/*
	 * if *imbalance is less than the average load per runnable task
	 * there is no guarantee that any tasks will be moved so we'll have
	 * a think about bumping its value to force at least one task to be
	 * moved
	 */
	if (env->imbalance < busiest->load_per_task)
		return fix_small_imbalance(env, sds);
}

/******* find_busiest_group() helpers end here *********************/

/**
 * find_busiest_group - Returns the busiest group within the sched_domain
 * if there is an imbalance. If there isn't an imbalance, and
 * the user has opted for power-savings, it returns a group whose
 * CPUs can be put to idle by rebalancing those tasks elsewhere, if
 * such a group exists.
 *
 * Also calculates the amount of weighted load which should be moved
 * to restore balance.
 *
 * @env: The load balancing environment.
 *
 * Return:	- The busiest group if imbalance exists.
 *		- If no imbalance and user has opted for power-savings balance,
 *		   return the least loaded group whose CPUs can be
 *		   put to idle by rebalancing its tasks onto our group.
 */
static struct sched_group *find_busiest_group(struct lb_env *env)
{
	struct sg_lb_stats *local, *busiest;
	struct sd_lb_stats sds;

	init_sd_lb_stats(&sds);

	/*
	 * Compute the various statistics relavent for load balancing at
	 * this level.
	 */
	update_sd_lb_stats(env, &sds);

	if (energy_aware() && !env->dst_rq->rd->overutilized)
		goto out_balanced;

	local = &sds.local_stat;
	busiest = &sds.busiest_stat;

	/* ASYM feature bypasses nice load balance check */
	if ((env->idle == CPU_IDLE || env->idle == CPU_NEWLY_IDLE) &&
	    check_asym_packing(env, &sds))
		return sds.busiest;

	/* There is no busy sibling group to pull tasks from */
	if (!sds.busiest || busiest->sum_nr_running == 0)
		goto out_balanced;

	sds.avg_load = (SCHED_CAPACITY_SCALE * sds.total_load)
						/ sds.total_capacity;

	/*
	 * If the busiest group is imbalanced the below checks don't
	 * work because they assume all things are equal, which typically
	 * isn't true due to cpus_allowed constraints and the like.
	 */
	if (busiest->group_type == group_imbalanced)
		goto force_balance;

	/*
	 * When dst_cpu is idle, prevent SMP nice and/or asymmetric group
	 * capacities from resulting in underutilization due to avg_load.
	 */
	if (env->idle != CPU_NOT_IDLE && group_has_capacity(env, local) &&
	    busiest->group_no_capacity)
		goto force_balance;

	/* Misfitting tasks should be dealt with regardless of the avg load */
	if (busiest->group_type == group_misfit_task) {
		goto force_balance;
	}

	/*
	 * If the local group is busier than the selected busiest group
	 * don't try and pull any tasks.
	 */
	if (local->avg_load >= busiest->avg_load)
		goto out_balanced;

	/*
	 * Don't pull any tasks if this group is already above the domain
	 * average load.
	 */
	if (local->avg_load >= sds.avg_load)
		goto out_balanced;

	if (env->idle == CPU_IDLE) {
		/*
		 * This cpu is idle. If the busiest group is not overloaded
		 * and there is no imbalance between this and busiest group
		 * wrt idle cpus, it is balanced. The imbalance becomes
		 * significant if the diff is greater than 1 otherwise we
		 * might end up to just move the imbalance on another group
		 */
		if ((busiest->group_type != group_overloaded) &&
		    (local->idle_cpus <= (busiest->idle_cpus + 1)) &&
		    !group_smaller_cpu_capacity(sds.busiest, sds.local))
			goto out_balanced;
	} else {
		/*
		 * In the CPU_NEWLY_IDLE, CPU_NOT_IDLE cases, use
		 * imbalance_pct to be conservative.
		 */
		if (100 * busiest->avg_load <=
				env->sd->imbalance_pct * local->avg_load)
			goto out_balanced;
	}

force_balance:
	env->busiest_group_type = busiest->group_type;
	/* Looks like there is an imbalance. Compute it */
	calculate_imbalance(env, &sds);
	return sds.busiest;

out_balanced:
	env->imbalance = 0;
	return NULL;
}

/*
 * find_busiest_queue - find the busiest runqueue among the cpus in group.
 */
static struct rq *find_busiest_queue(struct lb_env *env,
				     struct sched_group *group)
{
	struct rq *busiest = NULL, *rq;
	unsigned long busiest_load = 0, busiest_capacity = 1;
	int i;

	for_each_cpu_and(i, sched_group_cpus(group), env->cpus) {
		unsigned long capacity, wl;
		enum fbq_type rt;

		rq = cpu_rq(i);
		rt = fbq_classify_rq(rq);

		/*
		 * We classify groups/runqueues into three groups:
		 *  - regular: there are !numa tasks
		 *  - remote:  there are numa tasks that run on the 'wrong' node
		 *  - all:     there is no distinction
		 *
		 * In order to avoid migrating ideally placed numa tasks,
		 * ignore those when there's better options.
		 *
		 * If we ignore the actual busiest queue to migrate another
		 * task, the next balance pass can still reduce the busiest
		 * queue by moving tasks around inside the node.
		 *
		 * If we cannot move enough load due to this classification
		 * the next pass will adjust the group classification and
		 * allow migration of more tasks.
		 *
		 * Both cases only affect the total convergence complexity.
		 */
		if (rt > env->fbq_type)
			continue;

		capacity = capacity_of(i);

		wl = weighted_cpuload(i);

		/*
		 * When comparing with imbalance, use weighted_cpuload()
		 * which is not scaled with the cpu capacity.
		 */

		if (rq->nr_running == 1 && wl > env->imbalance &&
		    !check_cpu_capacity(rq, env->sd) &&
		    env->busiest_group_type != group_misfit_task)
			continue;

		/*
		 * For the load comparisons with the other cpu's, consider
		 * the weighted_cpuload() scaled with the cpu capacity, so
		 * that the load can be moved away from the cpu that is
		 * potentially running at a lower capacity.
		 *
		 * Thus we're looking for max(wl_i / capacity_i), crosswise
		 * multiplication to rid ourselves of the division works out
		 * to: wl_i * capacity_j > wl_j * capacity_i;  where j is
		 * our previous maximum.
		 */
		if (wl * busiest_capacity > busiest_load * capacity) {
			busiest_load = wl;
			busiest_capacity = capacity;
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

/* Working cpumask for load_balance and load_balance_newidle. */
DEFINE_PER_CPU(cpumask_var_t, load_balance_mask);

static int need_active_balance(struct lb_env *env)
{
	struct sched_domain *sd = env->sd;

	if (env->idle == CPU_NEWLY_IDLE) {

		/*
		 * ASYM_PACKING needs to force migrate tasks from busy but
		 * higher numbered CPUs in order to pack all tasks in the
		 * lowest numbered CPUs.
		 */
		if ((sd->flags & SD_ASYM_PACKING) && env->src_cpu > env->dst_cpu)
			return 1;
	}

	/*
	 * The dst_cpu is idle and the src_cpu CPU has only 1 CFS task.
	 * It's worth migrating the task if the src_cpu's capacity is reduced
	 * because of other sched_class or IRQs if more capacity stays
	 * available on dst_cpu.
	 */
	if ((env->idle != CPU_NOT_IDLE) &&
	    (env->src_rq->cfs.h_nr_running == 1)) {
		if ((check_cpu_capacity(env->src_rq, sd)) &&
		    (capacity_of(env->src_cpu)*sd->imbalance_pct < capacity_of(env->dst_cpu)*100))
			return 1;
	}

	if ((capacity_of(env->src_cpu) < capacity_of(env->dst_cpu)) &&
	    ((capacity_orig_of(env->src_cpu) < capacity_orig_of(env->dst_cpu))) &&
				env->src_rq->cfs.h_nr_running == 1 &&
				cpu_overutilized(env->src_cpu) &&
				!cpu_overutilized(env->dst_cpu)) {
			return 1;
	}

	return unlikely(sd->nr_balance_failed > sd->cache_nice_tries+2);
}

static int active_load_balance_cpu_stop(void *data);

static int should_we_balance(struct lb_env *env)
{
	struct sched_group *sg = env->sd->groups;
	struct cpumask *sg_cpus, *sg_mask;
	int cpu, balance_cpu = -1;

	/*
	 * In the newly idle case, we will allow all the cpu's
	 * to do the newly idle load balance.
	 */
	if (env->idle == CPU_NEWLY_IDLE)
		return 1;

	sg_cpus = sched_group_cpus(sg);
	sg_mask = sched_group_mask(sg);
	/* Try to find first idle cpu */
	for_each_cpu_and(cpu, sg_cpus, env->cpus) {
		if (!cpumask_test_cpu(cpu, sg_mask) || !idle_cpu(cpu))
			continue;

		balance_cpu = cpu;
		break;
	}

	if (balance_cpu == -1)
		balance_cpu = group_balance_cpu(sg);

	/*
	 * First idle cpu or the first cpu(busiest) in this sched group
	 * is eligible for doing load balancing at this and above domains.
	 */
	return balance_cpu == env->dst_cpu;
}

/*
 * Check this_cpu to ensure it is balanced within domain. Attempt to move
 * tasks if there is an imbalance.
 */
static int load_balance(int this_cpu, struct rq *this_rq,
			struct sched_domain *sd, enum cpu_idle_type idle,
			int *continue_balancing)
{
	int ld_moved, cur_ld_moved, active_balance = 0;
	struct sched_domain *sd_parent = lb_sd_parent(sd) ? sd->parent : NULL;
	struct sched_group *group;
	struct rq *busiest;
	unsigned long flags;
	struct cpumask *cpus = this_cpu_cpumask_var_ptr(load_balance_mask);

	struct lb_env env = {
		.sd		= sd,
		.dst_cpu	= this_cpu,
		.dst_rq		= this_rq,
		.dst_grpmask    = sched_group_cpus(sd->groups),
		.idle		= idle,
		.loop_break	= sched_nr_migrate_break,
		.cpus		= cpus,
		.fbq_type	= all,
		.tasks		= LIST_HEAD_INIT(env.tasks),
	};

	/*
	 * For NEWLY_IDLE load_balancing, we don't need to consider
	 * other cpus in our group
	 */
	if (idle == CPU_NEWLY_IDLE)
		env.dst_grpmask = NULL;

	cpumask_copy(cpus, cpu_active_mask);

	schedstat_inc(sd, lb_count[idle]);

redo:
	if (!should_we_balance(&env)) {
		*continue_balancing = 0;
		goto out_balanced;
	}

	group = find_busiest_group(&env);
	if (!group) {
		schedstat_inc(sd, lb_nobusyg[idle]);
		goto out_balanced;
	}

	busiest = find_busiest_queue(&env, group);
	if (!busiest) {
		schedstat_inc(sd, lb_nobusyq[idle]);
		goto out_balanced;
	}

	BUG_ON(busiest == env.dst_rq);

	schedstat_add(sd, lb_imbalance[idle], env.imbalance);

	env.src_cpu = busiest->cpu;
	env.src_rq = busiest;

	ld_moved = 0;
	if (busiest->nr_running > 1) {
		/*
		 * Attempt to move tasks. If find_busiest_group has found
		 * an imbalance but busiest->nr_running <= 1, the group is
		 * still unbalanced. ld_moved simply stays zero, so it is
		 * correctly treated as an imbalance.
		 */
		env.flags |= LBF_ALL_PINNED;
		env.loop_max  = min(sysctl_sched_nr_migrate, busiest->nr_running);

more_balance:
		raw_spin_lock_irqsave(&busiest->lock, flags);
		update_rq_clock(busiest);

		/*
		 * cur_ld_moved - load moved in current iteration
		 * ld_moved     - cumulative load moved across iterations
		 */
		cur_ld_moved = detach_tasks(&env);

		/*
		 * We've detached some tasks from busiest_rq. Every
		 * task is masked "TASK_ON_RQ_MIGRATING", so we can safely
		 * unlock busiest->lock, and we are able to be sure
		 * that nobody can manipulate the tasks in parallel.
		 * See task_rq_lock() family for the details.
		 */

		raw_spin_unlock(&busiest->lock);

		if (cur_ld_moved) {
			attach_tasks(&env);
			ld_moved += cur_ld_moved;
		}

		local_irq_restore(flags);

		if (env.flags & LBF_NEED_BREAK) {
			env.flags &= ~LBF_NEED_BREAK;
			goto more_balance;
		}

		/*
		 * Revisit (affine) tasks on src_cpu that couldn't be moved to
		 * us and move them to an alternate dst_cpu in our sched_group
		 * where they can run. The upper limit on how many times we
		 * iterate on same src_cpu is dependent on number of cpus in our
		 * sched_group.
		 *
		 * This changes load balance semantics a bit on who can move
		 * load to a given_cpu. In addition to the given_cpu itself
		 * (or a ilb_cpu acting on its behalf where given_cpu is
		 * nohz-idle), we now have balance_cpu in a position to move
		 * load to given_cpu. In rare situations, this may cause
		 * conflicts (balance_cpu and given_cpu/ilb_cpu deciding
		 * _independently_ and at _same_ time to move some load to
		 * given_cpu) causing exceess load to be moved to given_cpu.
		 * This however should not happen so much in practice and
		 * moreover subsequent load balance cycles should correct the
		 * excess load moved.
		 */
		if ((env.flags & LBF_DST_PINNED) && env.imbalance > 0) {

			/* Prevent to re-select dst_cpu via env's cpus */
			cpumask_clear_cpu(env.dst_cpu, env.cpus);

			env.dst_rq	 = cpu_rq(env.new_dst_cpu);
			env.dst_cpu	 = env.new_dst_cpu;
			env.flags	&= ~LBF_DST_PINNED;
			env.loop	 = 0;
			env.loop_break	 = sched_nr_migrate_break;

			/*
			 * Go back to "more_balance" rather than "redo" since we
			 * need to continue with same src_cpu.
			 */
			goto more_balance;
		}

		/*
		 * We failed to reach balance because of affinity.
		 */
		if (sd_parent) {
			int *group_imbalance = &sd_parent->groups->sgc->imbalance;

			if ((env.flags & LBF_SOME_PINNED) && env.imbalance > 0)
				*group_imbalance = 1;
		}

		/* All tasks on this runqueue were pinned by CPU affinity */
		if (unlikely(env.flags & LBF_ALL_PINNED)) {
			cpumask_clear_cpu(cpu_of(busiest), cpus);
			if (!cpumask_empty(cpus)) {
				env.loop = 0;
				env.loop_break = sched_nr_migrate_break;
				goto redo;
			}
			goto out_all_pinned;
		}
	}

	if (!ld_moved) {
		schedstat_inc(sd, lb_failed[idle]);
		/*
		 * Increment the failure counter only on periodic balance.
		 * We do not want newidle balance, which can be very
		 * frequent, pollute the failure counter causing
		 * excessive cache_hot migrations and active balances.
		 */
		if (idle != CPU_NEWLY_IDLE)
			if (env.src_grp_nr_running > 1)
				sd->nr_balance_failed++;

		if (need_active_balance(&env)) {
			raw_spin_lock_irqsave(&busiest->lock, flags);

			/* don't kick the active_load_balance_cpu_stop,
			 * if the curr task on busiest cpu can't be
			 * moved to this_cpu
			 */
			if (!cpumask_test_cpu(this_cpu,
					tsk_cpus_allowed(busiest->curr))) {
				raw_spin_unlock_irqrestore(&busiest->lock,
							    flags);
				env.flags |= LBF_ALL_PINNED;
				goto out_one_pinned;
			}

			/*
			 * ->active_balance synchronizes accesses to
			 * ->active_balance_work.  Once set, it's cleared
			 * only after active load balance is finished.
			 */
			if (!busiest->active_balance) {
				busiest->active_balance = 1;
				busiest->push_cpu = this_cpu;
				active_balance = 1;
			}
			raw_spin_unlock_irqrestore(&busiest->lock, flags);

			if (active_balance) {
				stop_one_cpu_nowait(cpu_of(busiest),
					active_load_balance_cpu_stop, busiest,
					&busiest->active_balance_work);
			}

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
		 * detach_tasks).
		 */
		if (sd->balance_interval < sd->max_interval)
			sd->balance_interval *= 2;
	}

	goto out;

out_balanced:
	/*
	 * We reach balance although we may have faced some affinity
	 * constraints. Clear the imbalance flag if it was set.
	 */
	if (sd_parent) {
		int *group_imbalance = &sd_parent->groups->sgc->imbalance;

		if (*group_imbalance)
			*group_imbalance = 0;
	}

out_all_pinned:
	/*
	 * We reach balance because all tasks are pinned at this level so
	 * we can't migrate them. Let the imbalance flag set so parent level
	 * can try to migrate them.
	 */
	schedstat_inc(sd, lb_balanced[idle]);

	sd->nr_balance_failed = 0;

out_one_pinned:
	/* tune up the balancing interval */
	if (((env.flags & LBF_ALL_PINNED) &&
			sd->balance_interval < MAX_PINNED_INTERVAL) ||
			(sd->balance_interval < sd->max_interval))
		sd->balance_interval *= 2;

	ld_moved = 0;
out:
	return ld_moved;
}

static inline unsigned long
get_sd_balance_interval(struct sched_domain *sd, int cpu_busy)
{
	unsigned long interval = sd->balance_interval;

	if (cpu_busy)
		interval *= sd->busy_factor;

	/* scale ms to jiffies */
	interval = msecs_to_jiffies(interval);
	interval = clamp(interval, 1UL, max_load_balance_interval);

	return interval;
}

static inline void
update_next_balance(struct sched_domain *sd, int cpu_busy, unsigned long *next_balance)
{
	unsigned long interval, next;

	interval = get_sd_balance_interval(sd, cpu_busy);
	next = sd->last_balance + interval;

	if (time_after(*next_balance, next))
		*next_balance = next;
}

/*
 * idle_balance is called by schedule() if this_cpu is about to become
 * idle. Attempts to pull tasks from other CPUs.
 */
static int idle_balance(struct rq *this_rq)
{
	unsigned long next_balance = jiffies + HZ;
	int this_cpu = this_rq->cpu;
	struct sched_domain *sd;
	int pulled_task = 0;
	u64 curr_cost = 0;

	idle_enter_fair(this_rq);

	/*
	 * We must set idle_stamp _before_ calling idle_balance(), such that we
	 * measure the duration of idle_balance() as idle time.
	 */
	this_rq->idle_stamp = rq_clock(this_rq);

	if (!energy_aware() &&
	    (this_rq->avg_idle < sysctl_sched_migration_cost ||
	     !this_rq->rd->overload)) {
		rcu_read_lock();
		sd = rcu_dereference_check_sched_domain(this_rq->sd);
		if (sd)
			update_next_balance(sd, 0, &next_balance);
		rcu_read_unlock();

		goto out;
	}

	raw_spin_unlock(&this_rq->lock);

	update_blocked_averages(this_cpu);
	rcu_read_lock();
	for_each_domain(this_cpu, sd) {
		int continue_balancing = 1;
		u64 t0, domain_cost;

		if (!(sd->flags & SD_LOAD_BALANCE))
			continue;

		if (this_rq->avg_idle < curr_cost + sd->max_newidle_lb_cost) {
			update_next_balance(sd, 0, &next_balance);
			break;
		}

		if (sd->flags & SD_BALANCE_NEWIDLE) {
			t0 = sched_clock_cpu(this_cpu);

			pulled_task = load_balance(this_cpu, this_rq,
						   sd, CPU_NEWLY_IDLE,
						   &continue_balancing);

			domain_cost = sched_clock_cpu(this_cpu) - t0;
			if (domain_cost > sd->max_newidle_lb_cost)
				sd->max_newidle_lb_cost = domain_cost;

			curr_cost += domain_cost;
		}

		update_next_balance(sd, 0, &next_balance);

		/*
		 * Stop searching for tasks to pull if there are
		 * now runnable tasks on this rq.
		 */
		if (pulled_task || this_rq->nr_running > 0)
			break;
	}
	rcu_read_unlock();

	raw_spin_lock(&this_rq->lock);

	if (curr_cost > this_rq->max_idle_balance_cost)
		this_rq->max_idle_balance_cost = curr_cost;

	/*
	 * While browsing the domains, we released the rq lock, a task could
	 * have been enqueued in the meantime. Since we're not going idle,
	 * pretend we pulled a task.
	 */
	if (this_rq->cfs.h_nr_running && !pulled_task)
		pulled_task = 1;

out:
	/* Move the next balance forward */
	if (time_after(this_rq->next_balance, next_balance))
		this_rq->next_balance = next_balance;

	/* Is there a task of a high priority class? */
	if (this_rq->nr_running != this_rq->cfs.h_nr_running)
		pulled_task = -1;

	if (pulled_task) {
		idle_exit_fair(this_rq);
		this_rq->idle_stamp = 0;
	}

	return pulled_task;
}

/*
 * active_load_balance_cpu_stop is run by cpu stopper. It pushes
 * running tasks off the busiest CPU onto idle CPUs. It requires at
 * least 1 task to be running on each physical CPU where possible, and
 * avoids physical / logical imbalances.
 */
static int active_load_balance_cpu_stop(void *data)
{
	struct rq *busiest_rq = data;
	int busiest_cpu = cpu_of(busiest_rq);
	int target_cpu = busiest_rq->push_cpu;
	struct rq *target_rq = cpu_rq(target_cpu);
	struct sched_domain *sd = NULL;
	struct task_struct *p = NULL;
	struct task_struct *push_task = NULL;
	int push_task_detached = 0;
	struct lb_env env = {
		.sd		= sd,
		.dst_cpu	= target_cpu,
		.dst_rq		= target_rq,
		.src_cpu	= busiest_rq->cpu,
		.src_rq		= busiest_rq,
		.idle		= CPU_IDLE,
	};

	raw_spin_lock_irq(&busiest_rq->lock);

	/* make sure the requested cpu hasn't gone down in the meantime */
	if (unlikely(busiest_cpu != smp_processor_id() ||
		     !busiest_rq->active_balance))
		goto out_unlock;

	/* Is there any task to move? */
	if (busiest_rq->nr_running <= 1)
		goto out_unlock;

	/*
	 * This condition is "impossible", if it occurs
	 * we need to fix it. Originally reported by
	 * Bjorn Helgaas on a 128-cpu setup.
	 */
	BUG_ON(busiest_rq == target_rq);

	push_task = busiest_rq->push_task;
	if (push_task) {
		if (task_on_rq_queued(push_task) &&
			task_cpu(push_task) == busiest_cpu &&
					cpu_online(target_cpu)) {
			detach_task(push_task, &env);
			push_task_detached = 1;
		}
		goto out_unlock;
	}

	/* Search for an sd spanning us and the target CPU. */
	rcu_read_lock();
	for_each_domain(target_cpu, sd) {
		if ((sd->flags & SD_LOAD_BALANCE) &&
		    cpumask_test_cpu(busiest_cpu, sched_domain_span(sd)))
				break;
	}

	if (likely(sd)) {
		env.sd = sd;
		schedstat_inc(sd, alb_count);
		update_rq_clock(busiest_rq);

		p = detach_one_task(&env);
		if (p)
			schedstat_inc(sd, alb_pushed);
		else
			schedstat_inc(sd, alb_failed);
	}
	rcu_read_unlock();
out_unlock:
	busiest_rq->active_balance = 0;

	if (push_task)
		busiest_rq->push_task = NULL;

	raw_spin_unlock(&busiest_rq->lock);

	if (push_task) {
		if (push_task_detached)
			attach_one_task(target_rq, push_task);
		put_task_struct(push_task);
	}

	if (p)
		attach_one_task(target_rq, p);

	local_irq_enable();

	return 0;
}

static inline int on_null_domain(struct rq *rq)
{
	return unlikely(!rcu_dereference_sched(rq->sd));
}

#ifdef CONFIG_NO_HZ_COMMON
/*
 * idle load balancing details
 * - When one of the busy CPUs notice that there may be an idle rebalancing
 *   needed, they will kick the idle load balancer, which then does idle
 *   load balancing for all the idle CPUs.
 */
static inline int find_new_ilb(void)
{
	int ilb = cpumask_first(nohz.idle_cpus_mask);

	if (ilb < nr_cpu_ids && idle_cpu(ilb))
		return ilb;

	return nr_cpu_ids;
}

/*
 * Kick a CPU to do the nohz balancing, if it is time for it. We pick the
 * nohz_load_balancer CPU (if there is one) otherwise fallback to any idle
 * CPU (if there is one).
 */
static void nohz_balancer_kick(void)
{
	int ilb_cpu;

	nohz.next_balance++;

	ilb_cpu = find_new_ilb();

	if (ilb_cpu >= nr_cpu_ids)
		return;

	if (test_and_set_bit(NOHZ_BALANCE_KICK, nohz_flags(ilb_cpu)))
		return;
	/*
	 * Use smp_send_reschedule() instead of resched_cpu().
	 * This way we generate a sched IPI on the target cpu which
	 * is idle. And the softirq performing nohz idle load balance
	 * will be run before returning from the IPI.
	 */
	smp_send_reschedule(ilb_cpu);
	return;
}

static inline void nohz_balance_exit_idle(int cpu)
{
	if (unlikely(test_bit(NOHZ_TICK_STOPPED, nohz_flags(cpu)))) {
		/*
		 * Completely isolated CPUs don't ever set, so we must test.
		 */
		if (likely(cpumask_test_cpu(cpu, nohz.idle_cpus_mask))) {
			cpumask_clear_cpu(cpu, nohz.idle_cpus_mask);
			atomic_dec(&nohz.nr_cpus);
		}
		clear_bit(NOHZ_TICK_STOPPED, nohz_flags(cpu));
	}
}

static inline void set_cpu_sd_state_busy(void)
{
	struct sched_domain *sd;
	int cpu = smp_processor_id();

	rcu_read_lock();
	sd = rcu_dereference(per_cpu(sd_busy, cpu));

	if (!sd || !sd->nohz_idle)
		goto unlock;
	sd->nohz_idle = 0;

	atomic_inc(&sd->groups->sgc->nr_busy_cpus);
unlock:
	rcu_read_unlock();
}

void set_cpu_sd_state_idle(void)
{
	struct sched_domain *sd;
	int cpu = smp_processor_id();

	rcu_read_lock();
	sd = rcu_dereference(per_cpu(sd_busy, cpu));

	if (!sd || sd->nohz_idle)
		goto unlock;
	sd->nohz_idle = 1;

	atomic_dec(&sd->groups->sgc->nr_busy_cpus);
unlock:
	rcu_read_unlock();
}

/*
 * This routine will record that the cpu is going idle with tick stopped.
 * This info will be used in performing idle load balancing in the future.
 */
void nohz_balance_enter_idle(int cpu)
{
	/*
	 * If this cpu is going down, then nothing needs to be done.
	 */
	if (!cpu_active(cpu))
		return;

	if (test_bit(NOHZ_TICK_STOPPED, nohz_flags(cpu)))
		return;

	/*
	 * If we're a completely isolated CPU, we don't play.
	 */
	if (on_null_domain(cpu_rq(cpu)))
		return;

	cpumask_set_cpu(cpu, nohz.idle_cpus_mask);
	atomic_inc(&nohz.nr_cpus);
	set_bit(NOHZ_TICK_STOPPED, nohz_flags(cpu));
}

static int sched_ilb_notifier(struct notifier_block *nfb,
					unsigned long action, void *hcpu)
{
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_DYING:
		nohz_balance_exit_idle(smp_processor_id());
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}
#endif

static DEFINE_SPINLOCK(balancing);

/*
 * Scale the max load_balance interval with the number of CPUs in the system.
 * This trades load-balance latency on larger machines for less cross talk.
 */
void update_max_interval(void)
{
	max_load_balance_interval = HZ*num_online_cpus()/10;
}

/*
 * It checks each scheduling domain to see if it is due to be balanced,
 * and initiates a balancing operation if so.
 *
 * Balancing parameters are set up in init_sched_domains.
 */
static void rebalance_domains(struct rq *rq, enum cpu_idle_type idle)
{
	int continue_balancing = 1;
	int cpu = rq->cpu;
	unsigned long interval;
	struct sched_domain *sd;
	/* Earliest time when we have to do rebalance again */
	unsigned long next_balance = jiffies + 60*HZ;
	int update_next_balance = 0;
	int need_serialize, need_decay = 0;
	u64 max_cost = 0;

	update_blocked_averages(cpu);

	rcu_read_lock();
	for_each_domain(cpu, sd) {
		/*
		 * Decay the newidle max times here because this is a regular
		 * visit to all the domains. Decay ~1% per second.
		 */
		if (time_after(jiffies, sd->next_decay_max_lb_cost)) {
			sd->max_newidle_lb_cost =
				(sd->max_newidle_lb_cost * 253) / 256;
			sd->next_decay_max_lb_cost = jiffies + HZ;
			need_decay = 1;
		}
		max_cost += sd->max_newidle_lb_cost;

		if (!(sd->flags & SD_LOAD_BALANCE))
			continue;

		/*
		 * Stop the load balance at this level. There is another
		 * CPU in our sched group which is doing load balancing more
		 * actively.
		 */
		if (!continue_balancing) {
			if (need_decay)
				continue;
			break;
		}

		interval = get_sd_balance_interval(sd, idle != CPU_IDLE);

		need_serialize = sd->flags & SD_SERIALIZE;
		if (need_serialize) {
			if (!spin_trylock(&balancing))
				goto out;
		}

		if (time_after_eq(jiffies, sd->last_balance + interval)) {
			if (load_balance(cpu, rq, sd, idle, &continue_balancing)) {
				/*
				 * The LBF_DST_PINNED logic could have changed
				 * env->dst_cpu, so we can't know our idle
				 * state even if we migrated tasks. Update it.
				 */
				idle = idle_cpu(cpu) ? CPU_IDLE : CPU_NOT_IDLE;
			}
			sd->last_balance = jiffies;
			interval = get_sd_balance_interval(sd, idle != CPU_IDLE);
		}
		if (need_serialize)
			spin_unlock(&balancing);
out:
		if (time_after(next_balance, sd->last_balance + interval)) {
			next_balance = sd->last_balance + interval;
			update_next_balance = 1;
		}
	}
	if (need_decay) {
		/*
		 * Ensure the rq-wide value also decays but keep it at a
		 * reasonable floor to avoid funnies with rq->avg_idle.
		 */
		rq->max_idle_balance_cost =
			max((u64)sysctl_sched_migration_cost, max_cost);
	}
	rcu_read_unlock();

	/*
	 * next_balance will be updated only when there is a need.
	 * When the cpu is attached to null domain for ex, it will not be
	 * updated.
	 */
	if (likely(update_next_balance)) {
		rq->next_balance = next_balance;

#ifdef CONFIG_NO_HZ_COMMON
		/*
		 * If this CPU has been elected to perform the nohz idle
		 * balance. Other idle CPUs have already rebalanced with
		 * nohz_idle_balance() and nohz.next_balance has been
		 * updated accordingly. This CPU is now running the idle load
		 * balance for itself and we need to update the
		 * nohz.next_balance accordingly.
		 */
		if ((idle == CPU_IDLE) && time_after(nohz.next_balance, rq->next_balance))
			nohz.next_balance = rq->next_balance;
#endif
	}
}

#ifdef CONFIG_NO_HZ_COMMON
/*
 * In CONFIG_NO_HZ_COMMON case, the idle balance kickee will do the
 * rebalancing for all the cpus for whom scheduler ticks are stopped.
 */
static void nohz_idle_balance(struct rq *this_rq, enum cpu_idle_type idle)
{
	int this_cpu = this_rq->cpu;
	struct rq *rq;
	int balance_cpu;
	/* Earliest time when we have to do rebalance again */
	unsigned long next_balance = jiffies + 60*HZ;
	int update_next_balance = 0;

	if (idle != CPU_IDLE ||
	    !test_bit(NOHZ_BALANCE_KICK, nohz_flags(this_cpu)))
		goto end;

	for_each_cpu(balance_cpu, nohz.idle_cpus_mask) {
		if (balance_cpu == this_cpu || !idle_cpu(balance_cpu))
			continue;

		/*
		 * If this cpu gets work to do, stop the load balancing
		 * work being done for other cpus. Next load
		 * balancing owner will pick it up.
		 */
		if (need_resched())
			break;

		rq = cpu_rq(balance_cpu);

		/*
		 * If time for next balance is due,
		 * do the balance.
		 */
		if (time_after_eq(jiffies, rq->next_balance)) {
			raw_spin_lock_irq(&rq->lock);
			update_rq_clock(rq);
			update_idle_cpu_load(rq);
			raw_spin_unlock_irq(&rq->lock);
			rebalance_domains(rq, CPU_IDLE);
		}

		if (time_after(next_balance, rq->next_balance)) {
			next_balance = rq->next_balance;
			update_next_balance = 1;
		}
	}

	/*
	 * next_balance will be updated only when there is a need.
	 * When the CPU is attached to null domain for ex, it will not be
	 * updated.
	 */
	if (likely(update_next_balance))
		nohz.next_balance = next_balance;
end:
	clear_bit(NOHZ_BALANCE_KICK, nohz_flags(this_cpu));
}

/*
 * Current heuristic for kicking the idle load balancer in the presence
 * of an idle cpu in the system.
 *   - This rq has more than one task.
 *   - This rq has at least one CFS task and the capacity of the CPU is
 *     significantly reduced because of RT tasks or IRQs.
 *   - At parent of LLC scheduler domain level, this cpu's scheduler group has
 *     multiple busy cpu.
 *   - For SD_ASYM_PACKING, if the lower numbered cpu's in the scheduler
 *     domain span are idle.
 */
static inline bool nohz_kick_needed(struct rq *rq)
{
	unsigned long now = jiffies;
	struct sched_domain *sd;
	struct sched_group_capacity *sgc;
	int nr_busy, cpu = rq->cpu;
	bool kick = false;

	if (unlikely(rq->idle_balance))
		return false;

       /*
	* We may be recently in ticked or tickless idle mode. At the first
	* busy tick after returning from idle, we will update the busy stats.
	*/
	set_cpu_sd_state_busy();
	nohz_balance_exit_idle(cpu);

	/*
	 * None are in tickless mode and hence no need for NOHZ idle load
	 * balancing.
	 */
	if (likely(!atomic_read(&nohz.nr_cpus)))
		return false;

	if (time_before(now, nohz.next_balance))
		return false;

	if (rq->nr_running >= 2 &&
	    (!energy_aware() || cpu_overutilized(cpu)))
		return true;

	/* Do idle load balance if there have misfit task */
	if (energy_aware())
		return rq->misfit_task;

	rcu_read_lock();
	sd = rcu_dereference(per_cpu(sd_busy, cpu));
	if (sd) {
		sgc = sd->groups->sgc;
		nr_busy = atomic_read(&sgc->nr_busy_cpus);

		if (nr_busy > 1) {
			kick = true;
			goto unlock;
		}

	}

	sd = rcu_dereference(rq->sd);
	if (sd) {
		if ((rq->cfs.h_nr_running >= 1) &&
				check_cpu_capacity(rq, sd)) {
			kick = true;
			goto unlock;
		}
	}

	sd = rcu_dereference(per_cpu(sd_asym, cpu));
	if (sd && (cpumask_first_and(nohz.idle_cpus_mask,
				  sched_domain_span(sd)) < cpu)) {
		kick = true;
		goto unlock;
	}

unlock:
	rcu_read_unlock();
	return kick;
}
#else
static void nohz_idle_balance(struct rq *this_rq, enum cpu_idle_type idle) { }
#endif

/*
 * run_rebalance_domains is triggered when needed from the scheduler tick.
 * Also triggered for nohz idle balancing (with nohz_balancing_kick set).
 */
static void run_rebalance_domains(struct softirq_action *h)
{
	struct rq *this_rq = this_rq();
	enum cpu_idle_type idle = this_rq->idle_balance ?
						CPU_IDLE : CPU_NOT_IDLE;

	/*
	 * If this cpu has a pending nohz_balance_kick, then do the
	 * balancing on behalf of the other idle cpus whose ticks are
	 * stopped. Do nohz_idle_balance *before* rebalance_domains to
	 * give the idle cpus a chance to load balance. Else we may
	 * load balance only within the local sched_domain hierarchy
	 * and abort nohz_idle_balance altogether if we pull some load.
	 */
	nohz_idle_balance(this_rq, idle);
	rebalance_domains(this_rq, idle);
}

/*
 * Trigger the SCHED_SOFTIRQ if it is time to do periodic load balancing.
 */
void trigger_load_balance(struct rq *rq)
{
	/* Don't need to rebalance while attached to NULL domain */
	if (unlikely(on_null_domain(rq)))
		return;

	if (time_after_eq(jiffies, rq->next_balance))
		raise_softirq(SCHED_SOFTIRQ);
#ifdef CONFIG_NO_HZ_COMMON
	if (nohz_kick_needed(rq))
		nohz_balancer_kick();
#endif
}

static void rq_online_fair(struct rq *rq)
{
	update_sysctl();

	update_runtime_enabled(rq);
}

static void rq_offline_fair(struct rq *rq)
{
	update_sysctl();

	/* Ensure any throttled groups are reachable by pick_next_task */
	unthrottle_offline_cfs_rqs(rq);
}

static inline int
kick_active_balance(struct rq *rq, struct task_struct *p, int new_cpu)
{
	int rc = 0;

	/* Invoke active balance to force migrate currently running task */
	raw_spin_lock(&rq->lock);
	if (!rq->active_balance) {
		rq->active_balance = 1;
		rq->push_cpu = new_cpu;
		get_task_struct(p);
		rq->push_task = p;
		rc = 1;
	}
	raw_spin_unlock(&rq->lock);

	return rc;
}

void check_for_migration(struct rq *rq, struct task_struct *p)
{
	int new_cpu;
	int active_balance;
	int cpu = task_cpu(p);

	if (energy_aware() && rq->misfit_task) {
		if (rq->curr->state != TASK_RUNNING ||
		    rq->curr->nr_cpus_allowed == 1)
			return;

		new_cpu = select_energy_cpu_brute(p, cpu, 0);
		if (capacity_orig_of(new_cpu) > capacity_orig_of(cpu)) {
			active_balance = kick_active_balance(rq, p, new_cpu);
			if (active_balance)
				stop_one_cpu_nowait(cpu,
						active_load_balance_cpu_stop,
						rq, &rq->active_balance_work);
		}
	}
}

#endif /* CONFIG_SMP */

/*
 * scheduler tick hitting a task of our scheduling class:
 */
static void task_tick_fair(struct rq *rq, struct task_struct *curr, int queued)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &curr->se;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		entity_tick(cfs_rq, se, queued);
	}

	if (static_branch_unlikely(&sched_numa_balancing))
		task_tick_numa(rq, curr);

#ifdef CONFIG_SMP
	if (!rq->rd->overutilized && cpu_overutilized(task_cpu(curr))) {
		rq->rd->overutilized = true;
		trace_sched_overutilized(true);
	}

	rq->misfit_task = !task_fits_max(curr, rq->cpu);
#endif

}

/*
 * called on fork with the child task as argument from the parent's context
 *  - child not yet on the tasklist
 *  - preemption disabled
 */
static void task_fork_fair(struct task_struct *p)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se, *curr;
	struct rq *rq = this_rq();

	raw_spin_lock(&rq->lock);
	update_rq_clock(rq);

	cfs_rq = task_cfs_rq(current);
	curr = cfs_rq->curr;
	if (curr) {
		update_curr(cfs_rq);
		se->vruntime = curr->vruntime;
	}
	place_entity(cfs_rq, se, 1);

	if (sysctl_sched_child_runs_first && curr && entity_before(curr, se)) {
		/*
		 * Upon rescheduling, sched_class::put_prev_task() will place
		 * 'current' within the tree based on its new key value.
		 */
		swap(curr->vruntime, se->vruntime);
		resched_curr(rq);
	}

	se->vruntime -= cfs_rq->min_vruntime;
	raw_spin_unlock(&rq->lock);
}

/*
 * Priority of the task has changed. Check to see if we preempt
 * the current task.
 */
static void
prio_changed_fair(struct rq *rq, struct task_struct *p, int oldprio)
{
	if (!task_on_rq_queued(p))
		return;

	/*
	 * Reschedule if we are currently running on this runqueue and
	 * our priority decreased, or if we are not currently running on
	 * this runqueue and our priority is higher than the current's
	 */
	if (rq->curr == p) {
		if (p->prio > oldprio)
			resched_curr(rq);
	} else
		check_preempt_curr(rq, p, 0);
}

static inline bool vruntime_normalized(struct task_struct *p)
{
	struct sched_entity *se = &p->se;

	/*
	 * In both the TASK_ON_RQ_QUEUED and TASK_ON_RQ_MIGRATING cases,
	 * the dequeue_entity(.flags=0) will already have normalized the
	 * vruntime.
	 */
	if (p->on_rq)
		return true;

	/*
	 * When !on_rq, vruntime of the task has usually NOT been normalized.
	 * But there are some cases where it has already been normalized:
	 *
	 * - A forked child which is waiting for being woken up by
	 *   wake_up_new_task().
	 * - A task which has been woken up by try_to_wake_up() and
	 *   waiting for actually being woken up by sched_ttwu_pending().
	 */
	if (!se->sum_exec_runtime || p->state == TASK_WAKING)
		return true;

	return false;
}

#ifdef CONFIG_FAIR_GROUP_SCHED
/*
 * Propagate the changes of the sched_entity across the tg tree to make it
 * visible to the root
 */
static void propagate_entity_cfs_rq(struct sched_entity *se)
{
	struct cfs_rq *cfs_rq;

	/* Start to propagate at parent */
	se = se->parent;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);

		if (cfs_rq_throttled(cfs_rq))
			break;

		update_load_avg(se, UPDATE_TG);
	}
}
#else
static void propagate_entity_cfs_rq(struct sched_entity *se) { }
#endif

static void detach_entity_cfs_rq(struct sched_entity *se)
{
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	/* Catch up with the cfs_rq and remove our load when we leave */
	update_load_avg(se, 0);
	detach_entity_load_avg(cfs_rq, se);
	update_tg_load_avg(cfs_rq, false);
	propagate_entity_cfs_rq(se);
}

static void attach_entity_cfs_rq(struct sched_entity *se)
{
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

#ifdef CONFIG_FAIR_GROUP_SCHED
	/*
	 * Since the real-depth could have been changed (only FAIR
	 * class maintain depth value), reset depth properly.
	 */
	se->depth = se->parent ? se->parent->depth + 1 : 0;
#endif

	/* Synchronize entity with its cfs_rq */
	update_load_avg(se, sched_feat(ATTACH_AGE_LOAD) ? 0 : SKIP_AGE_LOAD);
	attach_entity_load_avg(cfs_rq, se);
	update_tg_load_avg(cfs_rq, false);
	propagate_entity_cfs_rq(se);
}

static void detach_task_cfs_rq(struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	if (!vruntime_normalized(p)) {
		/*
		 * Fix up our vruntime so that the current sleep doesn't
		 * cause 'unlimited' sleep bonus.
		 */
		place_entity(cfs_rq, se, 0);
		se->vruntime -= cfs_rq->min_vruntime;
	}

	detach_entity_cfs_rq(se);
}

static void attach_task_cfs_rq(struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	attach_entity_cfs_rq(se);

	if (!vruntime_normalized(p))
		se->vruntime += cfs_rq->min_vruntime;
}

static void switched_from_fair(struct rq *rq, struct task_struct *p)
{
	detach_task_cfs_rq(p);
}

static void switched_to_fair(struct rq *rq, struct task_struct *p)
{
	attach_task_cfs_rq(p);

	if (task_on_rq_queued(p)) {
		/*
		 * We were most likely switched from sched_rt, so
		 * kick off the schedule if running, otherwise just see
		 * if we can still preempt the current task.
		 */
		if (rq->curr == p)
			resched_curr(rq);
		else
			check_preempt_curr(rq, p, 0);
	}
}

/* Account for a task changing its policy or group.
 *
 * This routine is mostly called to set cfs_rq->curr field when a task
 * migrates between groups/classes.
 */
static void set_curr_task_fair(struct rq *rq)
{
	struct sched_entity *se = &rq->curr->se;

	for_each_sched_entity(se) {
		struct cfs_rq *cfs_rq = cfs_rq_of(se);

		set_next_entity(cfs_rq, se);
		/* ensure bandwidth has been allocated on our new cfs_rq */
		account_cfs_rq_runtime(cfs_rq, 0);
	}
}

void init_cfs_rq(struct cfs_rq *cfs_rq)
{
	cfs_rq->tasks_timeline = RB_ROOT;
	cfs_rq->min_vruntime = (u64)(-(1LL << 20));
#ifndef CONFIG_64BIT
	cfs_rq->min_vruntime_copy = cfs_rq->min_vruntime;
#endif
#ifdef CONFIG_SMP
#ifdef CONFIG_FAIR_GROUP_SCHED
	cfs_rq->propagate_avg = 0;
#endif
	atomic_long_set(&cfs_rq->removed_load_avg, 0);
	atomic_long_set(&cfs_rq->removed_util_avg, 0);
#endif
}

#ifdef CONFIG_FAIR_GROUP_SCHED
static void task_set_group_fair(struct task_struct *p)
{
	struct sched_entity *se = &p->se;

	set_task_rq(p, task_cpu(p));
	se->depth = se->parent ? se->parent->depth + 1 : 0;
}

static void task_move_group_fair(struct task_struct *p)
{
	detach_task_cfs_rq(p);
	set_task_rq(p, task_cpu(p));

#ifdef CONFIG_SMP
	/* Tell se's cfs_rq has been changed -- migrated */
	p->se.avg.last_update_time = 0;
#endif
	attach_task_cfs_rq(p);
}

static void task_change_group_fair(struct task_struct *p, int type)
{
	switch (type) {
	case TASK_SET_GROUP:
		task_set_group_fair(p);
		break;

	case TASK_MOVE_GROUP:
		task_move_group_fair(p);
		break;
	}
}

void free_fair_sched_group(struct task_group *tg)
{
	int i;

	destroy_cfs_bandwidth(tg_cfs_bandwidth(tg));

	for_each_possible_cpu(i) {
		if (tg->cfs_rq)
			kfree(tg->cfs_rq[i]);
		if (tg->se)
			kfree(tg->se[i]);
	}

	kfree(tg->cfs_rq);
	kfree(tg->se);
}

int alloc_fair_sched_group(struct task_group *tg, struct task_group *parent)
{
	struct sched_entity *se;
	struct cfs_rq *cfs_rq;
	struct rq *rq;
	int i;

	tg->cfs_rq = kzalloc(sizeof(cfs_rq) * nr_cpu_ids, GFP_KERNEL);
	if (!tg->cfs_rq)
		goto err;
	tg->se = kzalloc(sizeof(se) * nr_cpu_ids, GFP_KERNEL);
	if (!tg->se)
		goto err;

	tg->shares = NICE_0_LOAD;

	init_cfs_bandwidth(tg_cfs_bandwidth(tg));

	for_each_possible_cpu(i) {
		rq = cpu_rq(i);

		cfs_rq = kzalloc_node(sizeof(struct cfs_rq),
				      GFP_KERNEL, cpu_to_node(i));
		if (!cfs_rq)
			goto err;

		se = kzalloc_node(sizeof(struct sched_entity),
				  GFP_KERNEL, cpu_to_node(i));
		if (!se)
			goto err_free_rq;

		init_cfs_rq(cfs_rq);
		init_tg_cfs_entry(tg, cfs_rq, se, i, parent->se[i]);
		init_entity_runnable_average(se);

		raw_spin_lock_irq(&rq->lock);
		post_init_entity_util_avg(se);
		raw_spin_unlock_irq(&rq->lock);
	}

	return 1;

err_free_rq:
	kfree(cfs_rq);
err:
	return 0;
}

void unregister_fair_sched_group(struct task_group *tg)
{
	unsigned long flags;
	struct rq *rq;
	int cpu;

	for_each_possible_cpu(cpu) {
		if (tg->se[cpu])
			remove_entity_load_avg(tg->se[cpu]);

		/*
		 * Only empty task groups can be destroyed; so we can speculatively
		 * check on_list without danger of it being re-added.
		 */
		if (!tg->cfs_rq[cpu]->on_list)
			continue;

		rq = cpu_rq(cpu);

		raw_spin_lock_irqsave(&rq->lock, flags);
		list_del_leaf_cfs_rq(tg->cfs_rq[cpu]);
		raw_spin_unlock_irqrestore(&rq->lock, flags);
	}
}

void init_tg_cfs_entry(struct task_group *tg, struct cfs_rq *cfs_rq,
			struct sched_entity *se, int cpu,
			struct sched_entity *parent)
{
	struct rq *rq = cpu_rq(cpu);

	cfs_rq->tg = tg;
	cfs_rq->rq = rq;
	init_cfs_rq_runtime(cfs_rq);

	tg->cfs_rq[cpu] = cfs_rq;
	tg->se[cpu] = se;

	/* se could be NULL for root_task_group */
	if (!se)
		return;

	if (!parent) {
		se->cfs_rq = &rq->cfs;
		se->depth = 0;
	} else {
		se->cfs_rq = parent->my_q;
		se->depth = parent->depth + 1;
	}

	se->my_q = cfs_rq;
	/* guarantee group entities always have weight */
	update_load_set(&se->load, NICE_0_LOAD);
	se->parent = parent;
}

static DEFINE_MUTEX(shares_mutex);

int sched_group_set_shares(struct task_group *tg, unsigned long shares)
{
	int i;
	unsigned long flags;

	/*
	 * We can't change the weight of the root cgroup.
	 */
	if (!tg->se[0])
		return -EINVAL;

	shares = clamp(shares, scale_load(MIN_SHARES), scale_load(MAX_SHARES));

	mutex_lock(&shares_mutex);
	if (tg->shares == shares)
		goto done;

	tg->shares = shares;
	for_each_possible_cpu(i) {
		struct rq *rq = cpu_rq(i);
		struct sched_entity *se;

		se = tg->se[i];
		/* Propagate contribution to hierarchy */
		raw_spin_lock_irqsave(&rq->lock, flags);

		/* Possible calls to update_curr() need rq clock */
		update_rq_clock(rq);
		for_each_sched_entity(se) {
			update_load_avg(se, UPDATE_TG);
			update_cfs_shares(se);
		}
		raw_spin_unlock_irqrestore(&rq->lock, flags);
	}

done:
	mutex_unlock(&shares_mutex);
	return 0;
}
#else /* CONFIG_FAIR_GROUP_SCHED */

void free_fair_sched_group(struct task_group *tg) { }

int alloc_fair_sched_group(struct task_group *tg, struct task_group *parent)
{
	return 1;
}

void unregister_fair_sched_group(struct task_group *tg) { }

#endif /* CONFIG_FAIR_GROUP_SCHED */


static unsigned int get_rr_interval_fair(struct rq *rq, struct task_struct *task)
{
	struct sched_entity *se = &task->se;
	unsigned int rr_interval = 0;

	/*
	 * Time slice is 0 for SCHED_OTHER tasks that are on an otherwise
	 * idle runqueue:
	 */
	if (rq->cfs.load.weight)
		rr_interval = NS_TO_JIFFIES(sched_slice(cfs_rq_of(se), se));

	return rr_interval;
}

/*
 * All the scheduling class methods:
 */
const struct sched_class fair_sched_class = {
	.next			= &idle_sched_class,
	.enqueue_task		= enqueue_task_fair,
	.dequeue_task		= dequeue_task_fair,
	.yield_task		= yield_task_fair,
	.yield_to_task		= yield_to_task_fair,

	.check_preempt_curr	= check_preempt_wakeup,

	.pick_next_task		= pick_next_task_fair,
	.put_prev_task		= put_prev_task_fair,

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_fair,
	.migrate_task_rq	= migrate_task_rq_fair,

	.rq_online		= rq_online_fair,
	.rq_offline		= rq_offline_fair,

	.task_waking		= task_waking_fair,
	.task_dead		= task_dead_fair,
	.set_cpus_allowed	= set_cpus_allowed_common,
#endif

	.set_curr_task          = set_curr_task_fair,
	.task_tick		= task_tick_fair,
	.task_fork		= task_fork_fair,

	.prio_changed		= prio_changed_fair,
	.switched_from		= switched_from_fair,
	.switched_to		= switched_to_fair,

	.get_rr_interval	= get_rr_interval_fair,

	.update_curr		= update_curr_fair,

#ifdef CONFIG_FAIR_GROUP_SCHED
	.task_change_group	= task_change_group_fair,
#endif
};

#ifdef CONFIG_SCHED_DEBUG
void print_cfs_stats(struct seq_file *m, int cpu)
{
	struct cfs_rq *cfs_rq;

	rcu_read_lock();
	for_each_leaf_cfs_rq(cpu_rq(cpu), cfs_rq)
		print_cfs_rq(m, cpu, cfs_rq);
	rcu_read_unlock();
}

#ifdef CONFIG_NUMA_BALANCING
void show_numa_stats(struct task_struct *p, struct seq_file *m)
{
	int node;
	unsigned long tsf = 0, tpf = 0, gsf = 0, gpf = 0;

	for_each_online_node(node) {
		if (p->numa_faults) {
			tsf = p->numa_faults[task_faults_idx(NUMA_MEM, node, 0)];
			tpf = p->numa_faults[task_faults_idx(NUMA_MEM, node, 1)];
		}
		if (p->numa_group) {
			gsf = p->numa_group->faults[task_faults_idx(NUMA_MEM, node, 0)],
			gpf = p->numa_group->faults[task_faults_idx(NUMA_MEM, node, 1)];
		}
		print_numa_stats(m, node, tsf, tpf, gsf, gpf);
	}
}
#endif /* CONFIG_NUMA_BALANCING */
#endif /* CONFIG_SCHED_DEBUG */

__init void init_sched_fair_class(void)
{
#ifdef CONFIG_SMP
	open_softirq(SCHED_SOFTIRQ, run_rebalance_domains);

#ifdef CONFIG_NO_HZ_COMMON
	nohz.next_balance = jiffies;
	zalloc_cpumask_var(&nohz.idle_cpus_mask, GFP_NOWAIT);
	cpu_notifier(sched_ilb_notifier, 0);
#endif
#endif /* SMP */

}
