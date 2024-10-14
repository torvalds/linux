// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpuset-internal.h"

/*
 * Legacy hierarchy call to cgroup_transfer_tasks() is handled asynchrously
 */
struct cpuset_remove_tasks_struct {
	struct work_struct work;
	struct cpuset *cs;
};

/*
 * Frequency meter - How fast is some event occurring?
 *
 * These routines manage a digitally filtered, constant time based,
 * event frequency meter.  There are four routines:
 *   fmeter_init() - initialize a frequency meter.
 *   fmeter_markevent() - called each time the event happens.
 *   fmeter_getrate() - returns the recent rate of such events.
 *   fmeter_update() - internal routine used to update fmeter.
 *
 * A common data structure is passed to each of these routines,
 * which is used to keep track of the state required to manage the
 * frequency meter and its digital filter.
 *
 * The filter works on the number of events marked per unit time.
 * The filter is single-pole low-pass recursive (IIR).  The time unit
 * is 1 second.  Arithmetic is done using 32-bit integers scaled to
 * simulate 3 decimal digits of precision (multiplied by 1000).
 *
 * With an FM_COEF of 933, and a time base of 1 second, the filter
 * has a half-life of 10 seconds, meaning that if the events quit
 * happening, then the rate returned from the fmeter_getrate()
 * will be cut in half each 10 seconds, until it converges to zero.
 *
 * It is not worth doing a real infinitely recursive filter.  If more
 * than FM_MAXTICKS ticks have elapsed since the last filter event,
 * just compute FM_MAXTICKS ticks worth, by which point the level
 * will be stable.
 *
 * Limit the count of unprocessed events to FM_MAXCNT, so as to avoid
 * arithmetic overflow in the fmeter_update() routine.
 *
 * Given the simple 32 bit integer arithmetic used, this meter works
 * best for reporting rates between one per millisecond (msec) and
 * one per 32 (approx) seconds.  At constant rates faster than one
 * per msec it maxes out at values just under 1,000,000.  At constant
 * rates between one per msec, and one per second it will stabilize
 * to a value N*1000, where N is the rate of events per second.
 * At constant rates between one per second and one per 32 seconds,
 * it will be choppy, moving up on the seconds that have an event,
 * and then decaying until the next event.  At rates slower than
 * about one in 32 seconds, it decays all the way back to zero between
 * each event.
 */

#define FM_COEF 933		/* coefficient for half-life of 10 secs */
#define FM_MAXTICKS ((u32)99)   /* useless computing more ticks than this */
#define FM_MAXCNT 1000000	/* limit cnt to avoid overflow */
#define FM_SCALE 1000		/* faux fixed point scale */

/* Initialize a frequency meter */
void fmeter_init(struct fmeter *fmp)
{
	fmp->cnt = 0;
	fmp->val = 0;
	fmp->time = 0;
	spin_lock_init(&fmp->lock);
}

/* Internal meter update - process cnt events and update value */
static void fmeter_update(struct fmeter *fmp)
{
	time64_t now;
	u32 ticks;

	now = ktime_get_seconds();
	ticks = now - fmp->time;

	if (ticks == 0)
		return;

	ticks = min(FM_MAXTICKS, ticks);
	while (ticks-- > 0)
		fmp->val = (FM_COEF * fmp->val) / FM_SCALE;
	fmp->time = now;

	fmp->val += ((FM_SCALE - FM_COEF) * fmp->cnt) / FM_SCALE;
	fmp->cnt = 0;
}

/* Process any previous ticks, then bump cnt by one (times scale). */
static void fmeter_markevent(struct fmeter *fmp)
{
	spin_lock(&fmp->lock);
	fmeter_update(fmp);
	fmp->cnt = min(FM_MAXCNT, fmp->cnt + FM_SCALE);
	spin_unlock(&fmp->lock);
}

/* Process any previous ticks, then return current value. */
static int fmeter_getrate(struct fmeter *fmp)
{
	int val;

	spin_lock(&fmp->lock);
	fmeter_update(fmp);
	val = fmp->val;
	spin_unlock(&fmp->lock);
	return val;
}

/*
 * Collection of memory_pressure is suppressed unless
 * this flag is enabled by writing "1" to the special
 * cpuset file 'memory_pressure_enabled' in the root cpuset.
 */

int cpuset_memory_pressure_enabled __read_mostly;

/*
 * __cpuset_memory_pressure_bump - keep stats of per-cpuset reclaims.
 *
 * Keep a running average of the rate of synchronous (direct)
 * page reclaim efforts initiated by tasks in each cpuset.
 *
 * This represents the rate at which some task in the cpuset
 * ran low on memory on all nodes it was allowed to use, and
 * had to enter the kernels page reclaim code in an effort to
 * create more free memory by tossing clean pages or swapping
 * or writing dirty pages.
 *
 * Display to user space in the per-cpuset read-only file
 * "memory_pressure".  Value displayed is an integer
 * representing the recent rate of entry into the synchronous
 * (direct) page reclaim by any task attached to the cpuset.
 */

void __cpuset_memory_pressure_bump(void)
{
	rcu_read_lock();
	fmeter_markevent(&task_cs(current)->fmeter);
	rcu_read_unlock();
}

static int update_relax_domain_level(struct cpuset *cs, s64 val)
{
#ifdef CONFIG_SMP
	if (val < -1 || val > sched_domain_level_max + 1)
		return -EINVAL;
#endif

	if (val != cs->relax_domain_level) {
		cs->relax_domain_level = val;
		if (!cpumask_empty(cs->cpus_allowed) &&
		    is_sched_load_balance(cs))
			rebuild_sched_domains_locked();
	}

	return 0;
}

static int cpuset_write_s64(struct cgroup_subsys_state *css, struct cftype *cft,
			    s64 val)
{
	struct cpuset *cs = css_cs(css);
	cpuset_filetype_t type = cft->private;
	int retval = -ENODEV;

	cpus_read_lock();
	cpuset_lock();
	if (!is_cpuset_online(cs))
		goto out_unlock;

	switch (type) {
	case FILE_SCHED_RELAX_DOMAIN_LEVEL:
		retval = update_relax_domain_level(cs, val);
		break;
	default:
		retval = -EINVAL;
		break;
	}
out_unlock:
	cpuset_unlock();
	cpus_read_unlock();
	return retval;
}

static s64 cpuset_read_s64(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct cpuset *cs = css_cs(css);
	cpuset_filetype_t type = cft->private;

	switch (type) {
	case FILE_SCHED_RELAX_DOMAIN_LEVEL:
		return cs->relax_domain_level;
	default:
		BUG();
	}

	/* Unreachable but makes gcc happy */
	return 0;
}

/*
 * update task's spread flag if cpuset's page/slab spread flag is set
 *
 * Call with callback_lock or cpuset_mutex held. The check can be skipped
 * if on default hierarchy.
 */
void cpuset1_update_task_spread_flags(struct cpuset *cs,
					struct task_struct *tsk)
{
	if (cgroup_subsys_on_dfl(cpuset_cgrp_subsys))
		return;

	if (is_spread_page(cs))
		task_set_spread_page(tsk);
	else
		task_clear_spread_page(tsk);

	if (is_spread_slab(cs))
		task_set_spread_slab(tsk);
	else
		task_clear_spread_slab(tsk);
}

/**
 * cpuset1_update_tasks_flags - update the spread flags of tasks in the cpuset.
 * @cs: the cpuset in which each task's spread flags needs to be changed
 *
 * Iterate through each task of @cs updating its spread flags.  As this
 * function is called with cpuset_mutex held, cpuset membership stays
 * stable.
 */
void cpuset1_update_tasks_flags(struct cpuset *cs)
{
	struct css_task_iter it;
	struct task_struct *task;

	css_task_iter_start(&cs->css, 0, &it);
	while ((task = css_task_iter_next(&it)))
		cpuset1_update_task_spread_flags(cs, task);
	css_task_iter_end(&it);
}

/*
 * If CPU and/or memory hotplug handlers, below, unplug any CPUs
 * or memory nodes, we need to walk over the cpuset hierarchy,
 * removing that CPU or node from all cpusets.  If this removes the
 * last CPU or node from a cpuset, then move the tasks in the empty
 * cpuset to its next-highest non-empty parent.
 */
static void remove_tasks_in_empty_cpuset(struct cpuset *cs)
{
	struct cpuset *parent;

	/*
	 * Find its next-highest non-empty parent, (top cpuset
	 * has online cpus, so can't be empty).
	 */
	parent = parent_cs(cs);
	while (cpumask_empty(parent->cpus_allowed) ||
			nodes_empty(parent->mems_allowed))
		parent = parent_cs(parent);

	if (cgroup_transfer_tasks(parent->css.cgroup, cs->css.cgroup)) {
		pr_err("cpuset: failed to transfer tasks out of empty cpuset ");
		pr_cont_cgroup_name(cs->css.cgroup);
		pr_cont("\n");
	}
}

static void cpuset_migrate_tasks_workfn(struct work_struct *work)
{
	struct cpuset_remove_tasks_struct *s;

	s = container_of(work, struct cpuset_remove_tasks_struct, work);
	remove_tasks_in_empty_cpuset(s->cs);
	css_put(&s->cs->css);
	kfree(s);
}

void cpuset1_hotplug_update_tasks(struct cpuset *cs,
			    struct cpumask *new_cpus, nodemask_t *new_mems,
			    bool cpus_updated, bool mems_updated)
{
	bool is_empty;

	cpuset_callback_lock_irq();
	cpumask_copy(cs->cpus_allowed, new_cpus);
	cpumask_copy(cs->effective_cpus, new_cpus);
	cs->mems_allowed = *new_mems;
	cs->effective_mems = *new_mems;
	cpuset_callback_unlock_irq();

	/*
	 * Don't call cpuset_update_tasks_cpumask() if the cpuset becomes empty,
	 * as the tasks will be migrated to an ancestor.
	 */
	if (cpus_updated && !cpumask_empty(cs->cpus_allowed))
		cpuset_update_tasks_cpumask(cs, new_cpus);
	if (mems_updated && !nodes_empty(cs->mems_allowed))
		cpuset_update_tasks_nodemask(cs);

	is_empty = cpumask_empty(cs->cpus_allowed) ||
		   nodes_empty(cs->mems_allowed);

	/*
	 * Move tasks to the nearest ancestor with execution resources,
	 * This is full cgroup operation which will also call back into
	 * cpuset. Execute it asynchronously using workqueue.
	 */
	if (is_empty && cs->css.cgroup->nr_populated_csets &&
	    css_tryget_online(&cs->css)) {
		struct cpuset_remove_tasks_struct *s;

		s = kzalloc(sizeof(*s), GFP_KERNEL);
		if (WARN_ON_ONCE(!s)) {
			css_put(&cs->css);
			return;
		}

		s->cs = cs;
		INIT_WORK(&s->work, cpuset_migrate_tasks_workfn);
		schedule_work(&s->work);
	}
}

/*
 * is_cpuset_subset(p, q) - Is cpuset p a subset of cpuset q?
 *
 * One cpuset is a subset of another if all its allowed CPUs and
 * Memory Nodes are a subset of the other, and its exclusive flags
 * are only set if the other's are set.  Call holding cpuset_mutex.
 */

static int is_cpuset_subset(const struct cpuset *p, const struct cpuset *q)
{
	return	cpumask_subset(p->cpus_allowed, q->cpus_allowed) &&
		nodes_subset(p->mems_allowed, q->mems_allowed) &&
		is_cpu_exclusive(p) <= is_cpu_exclusive(q) &&
		is_mem_exclusive(p) <= is_mem_exclusive(q);
}

/*
 * cpuset1_validate_change() - Validate conditions specific to legacy (v1)
 *                            behavior.
 */
int cpuset1_validate_change(struct cpuset *cur, struct cpuset *trial)
{
	struct cgroup_subsys_state *css;
	struct cpuset *c, *par;
	int ret;

	WARN_ON_ONCE(!rcu_read_lock_held());

	/* Each of our child cpusets must be a subset of us */
	ret = -EBUSY;
	cpuset_for_each_child(c, css, cur)
		if (!is_cpuset_subset(c, trial))
			goto out;

	/* On legacy hierarchy, we must be a subset of our parent cpuset. */
	ret = -EACCES;
	par = parent_cs(cur);
	if (par && !is_cpuset_subset(trial, par))
		goto out;

	ret = 0;
out:
	return ret;
}

static u64 cpuset_read_u64(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct cpuset *cs = css_cs(css);
	cpuset_filetype_t type = cft->private;

	switch (type) {
	case FILE_CPU_EXCLUSIVE:
		return is_cpu_exclusive(cs);
	case FILE_MEM_EXCLUSIVE:
		return is_mem_exclusive(cs);
	case FILE_MEM_HARDWALL:
		return is_mem_hardwall(cs);
	case FILE_SCHED_LOAD_BALANCE:
		return is_sched_load_balance(cs);
	case FILE_MEMORY_MIGRATE:
		return is_memory_migrate(cs);
	case FILE_MEMORY_PRESSURE_ENABLED:
		return cpuset_memory_pressure_enabled;
	case FILE_MEMORY_PRESSURE:
		return fmeter_getrate(&cs->fmeter);
	case FILE_SPREAD_PAGE:
		return is_spread_page(cs);
	case FILE_SPREAD_SLAB:
		return is_spread_slab(cs);
	default:
		BUG();
	}

	/* Unreachable but makes gcc happy */
	return 0;
}

static int cpuset_write_u64(struct cgroup_subsys_state *css, struct cftype *cft,
			    u64 val)
{
	struct cpuset *cs = css_cs(css);
	cpuset_filetype_t type = cft->private;
	int retval = 0;

	cpus_read_lock();
	cpuset_lock();
	if (!is_cpuset_online(cs)) {
		retval = -ENODEV;
		goto out_unlock;
	}

	switch (type) {
	case FILE_CPU_EXCLUSIVE:
		retval = cpuset_update_flag(CS_CPU_EXCLUSIVE, cs, val);
		break;
	case FILE_MEM_EXCLUSIVE:
		retval = cpuset_update_flag(CS_MEM_EXCLUSIVE, cs, val);
		break;
	case FILE_MEM_HARDWALL:
		retval = cpuset_update_flag(CS_MEM_HARDWALL, cs, val);
		break;
	case FILE_SCHED_LOAD_BALANCE:
		retval = cpuset_update_flag(CS_SCHED_LOAD_BALANCE, cs, val);
		break;
	case FILE_MEMORY_MIGRATE:
		retval = cpuset_update_flag(CS_MEMORY_MIGRATE, cs, val);
		break;
	case FILE_MEMORY_PRESSURE_ENABLED:
		cpuset_memory_pressure_enabled = !!val;
		break;
	case FILE_SPREAD_PAGE:
		retval = cpuset_update_flag(CS_SPREAD_PAGE, cs, val);
		break;
	case FILE_SPREAD_SLAB:
		retval = cpuset_update_flag(CS_SPREAD_SLAB, cs, val);
		break;
	default:
		retval = -EINVAL;
		break;
	}
out_unlock:
	cpuset_unlock();
	cpus_read_unlock();
	return retval;
}

/*
 * for the common functions, 'private' gives the type of file
 */

struct cftype cpuset1_files[] = {
	{
		.name = "cpus",
		.seq_show = cpuset_common_seq_show,
		.write = cpuset_write_resmask,
		.max_write_len = (100U + 6 * NR_CPUS),
		.private = FILE_CPULIST,
	},

	{
		.name = "mems",
		.seq_show = cpuset_common_seq_show,
		.write = cpuset_write_resmask,
		.max_write_len = (100U + 6 * MAX_NUMNODES),
		.private = FILE_MEMLIST,
	},

	{
		.name = "effective_cpus",
		.seq_show = cpuset_common_seq_show,
		.private = FILE_EFFECTIVE_CPULIST,
	},

	{
		.name = "effective_mems",
		.seq_show = cpuset_common_seq_show,
		.private = FILE_EFFECTIVE_MEMLIST,
	},

	{
		.name = "cpu_exclusive",
		.read_u64 = cpuset_read_u64,
		.write_u64 = cpuset_write_u64,
		.private = FILE_CPU_EXCLUSIVE,
	},

	{
		.name = "mem_exclusive",
		.read_u64 = cpuset_read_u64,
		.write_u64 = cpuset_write_u64,
		.private = FILE_MEM_EXCLUSIVE,
	},

	{
		.name = "mem_hardwall",
		.read_u64 = cpuset_read_u64,
		.write_u64 = cpuset_write_u64,
		.private = FILE_MEM_HARDWALL,
	},

	{
		.name = "sched_load_balance",
		.read_u64 = cpuset_read_u64,
		.write_u64 = cpuset_write_u64,
		.private = FILE_SCHED_LOAD_BALANCE,
	},

	{
		.name = "sched_relax_domain_level",
		.read_s64 = cpuset_read_s64,
		.write_s64 = cpuset_write_s64,
		.private = FILE_SCHED_RELAX_DOMAIN_LEVEL,
	},

	{
		.name = "memory_migrate",
		.read_u64 = cpuset_read_u64,
		.write_u64 = cpuset_write_u64,
		.private = FILE_MEMORY_MIGRATE,
	},

	{
		.name = "memory_pressure",
		.read_u64 = cpuset_read_u64,
		.private = FILE_MEMORY_PRESSURE,
	},

	{
		.name = "memory_spread_page",
		.read_u64 = cpuset_read_u64,
		.write_u64 = cpuset_write_u64,
		.private = FILE_SPREAD_PAGE,
	},

	{
		/* obsolete, may be removed in the future */
		.name = "memory_spread_slab",
		.read_u64 = cpuset_read_u64,
		.write_u64 = cpuset_write_u64,
		.private = FILE_SPREAD_SLAB,
	},

	{
		.name = "memory_pressure_enabled",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.read_u64 = cpuset_read_u64,
		.write_u64 = cpuset_write_u64,
		.private = FILE_MEMORY_PRESSURE_ENABLED,
	},

	{ }	/* terminate */
};
