/*
 *  kernel/cpuset.c
 *
 *  Processor and Memory placement constraints for sets of tasks.
 *
 *  Copyright (C) 2003 BULL SA.
 *  Copyright (C) 2004-2007 Silicon Graphics, Inc.
 *  Copyright (C) 2006 Google, Inc
 *
 *  Portions derived from Patrick Mochel's sysfs code.
 *  sysfs is Copyright (c) 2001-3 Patrick Mochel
 *
 *  2003-10-10 Written by Simon Derr.
 *  2003-10-22 Updates by Stephen Hemminger.
 *  2004 May-July Rework by Paul Jackson.
 *  2006 Rework by Paul Menage to use generic cgroups
 *  2008 Rework of the scheduler domains and CPU hotplug handling
 *       by Max Krasnyansky
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of the Linux
 *  distribution for more details.
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpuset.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mempolicy.h>
#include <linux/mm.h>
#include <linux/memory.h>
#include <linux/export.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/deadline.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/security.h>
#include <linux/spinlock.h>
#include <linux/oom.h>
#include <linux/sched/isolation.h>
#include <linux/cgroup.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

DEFINE_STATIC_KEY_FALSE(cpusets_pre_enable_key);
DEFINE_STATIC_KEY_FALSE(cpusets_enabled_key);

/*
 * There could be abnormal cpuset configurations for cpu or memory
 * node binding, add this key to provide a quick low-cost judgment
 * of the situation.
 */
DEFINE_STATIC_KEY_FALSE(cpusets_insane_config_key);

/* See "Frequency meter" comments, below. */

struct fmeter {
	int cnt;		/* unprocessed events count */
	int val;		/* most recent output value */
	time64_t time;		/* clock (secs) when val computed */
	spinlock_t lock;	/* guards read or write of above */
};

/*
 * Invalid partition error code
 */
enum prs_errcode {
	PERR_NONE = 0,
	PERR_INVCPUS,
	PERR_INVPARENT,
	PERR_NOTPART,
	PERR_NOTEXCL,
	PERR_NOCPUS,
	PERR_HOTPLUG,
	PERR_CPUSEMPTY,
	PERR_HKEEPING,
};

static const char * const perr_strings[] = {
	[PERR_INVCPUS]   = "Invalid cpu list in cpuset.cpus.exclusive",
	[PERR_INVPARENT] = "Parent is an invalid partition root",
	[PERR_NOTPART]   = "Parent is not a partition root",
	[PERR_NOTEXCL]   = "Cpu list in cpuset.cpus not exclusive",
	[PERR_NOCPUS]    = "Parent unable to distribute cpu downstream",
	[PERR_HOTPLUG]   = "No cpu available due to hotplug",
	[PERR_CPUSEMPTY] = "cpuset.cpus is empty",
	[PERR_HKEEPING]  = "partition config conflicts with housekeeping setup",
};

struct cpuset {
	struct cgroup_subsys_state css;

	unsigned long flags;		/* "unsigned long" so bitops work */

	/*
	 * On default hierarchy:
	 *
	 * The user-configured masks can only be changed by writing to
	 * cpuset.cpus and cpuset.mems, and won't be limited by the
	 * parent masks.
	 *
	 * The effective masks is the real masks that apply to the tasks
	 * in the cpuset. They may be changed if the configured masks are
	 * changed or hotplug happens.
	 *
	 * effective_mask == configured_mask & parent's effective_mask,
	 * and if it ends up empty, it will inherit the parent's mask.
	 *
	 *
	 * On legacy hierarchy:
	 *
	 * The user-configured masks are always the same with effective masks.
	 */

	/* user-configured CPUs and Memory Nodes allow to tasks */
	cpumask_var_t cpus_allowed;
	nodemask_t mems_allowed;

	/* effective CPUs and Memory Nodes allow to tasks */
	cpumask_var_t effective_cpus;
	nodemask_t effective_mems;

	/*
	 * Exclusive CPUs dedicated to current cgroup (default hierarchy only)
	 *
	 * This exclusive CPUs must be a subset of cpus_allowed. A parent
	 * cgroup can only grant exclusive CPUs to one of its children.
	 *
	 * When the cgroup becomes a valid partition root, effective_xcpus
	 * defaults to cpus_allowed if not set. The effective_cpus of a valid
	 * partition root comes solely from its effective_xcpus and some of the
	 * effective_xcpus may be distributed to sub-partitions below & hence
	 * excluded from its effective_cpus.
	 */
	cpumask_var_t effective_xcpus;

	/*
	 * Exclusive CPUs as requested by the user (default hierarchy only)
	 */
	cpumask_var_t exclusive_cpus;

	/*
	 * This is old Memory Nodes tasks took on.
	 *
	 * - top_cpuset.old_mems_allowed is initialized to mems_allowed.
	 * - A new cpuset's old_mems_allowed is initialized when some
	 *   task is moved into it.
	 * - old_mems_allowed is used in cpuset_migrate_mm() when we change
	 *   cpuset.mems_allowed and have tasks' nodemask updated, and
	 *   then old_mems_allowed is updated to mems_allowed.
	 */
	nodemask_t old_mems_allowed;

	struct fmeter fmeter;		/* memory_pressure filter */

	/*
	 * Tasks are being attached to this cpuset.  Used to prevent
	 * zeroing cpus/mems_allowed between ->can_attach() and ->attach().
	 */
	int attach_in_progress;

	/* partition number for rebuild_sched_domains() */
	int pn;

	/* for custom sched domain */
	int relax_domain_level;

	/* number of valid sub-partitions */
	int nr_subparts;

	/* partition root state */
	int partition_root_state;

	/*
	 * Default hierarchy only:
	 * use_parent_ecpus - set if using parent's effective_cpus
	 * child_ecpus_count - # of children with use_parent_ecpus set
	 */
	int use_parent_ecpus;
	int child_ecpus_count;

	/*
	 * number of SCHED_DEADLINE tasks attached to this cpuset, so that we
	 * know when to rebuild associated root domain bandwidth information.
	 */
	int nr_deadline_tasks;
	int nr_migrate_dl_tasks;
	u64 sum_migrate_dl_bw;

	/* Invalid partition error code, not lock protected */
	enum prs_errcode prs_err;

	/* Handle for cpuset.cpus.partition */
	struct cgroup_file partition_file;

	/* Remote partition silbling list anchored at remote_children */
	struct list_head remote_sibling;
};

/*
 * Legacy hierarchy call to cgroup_transfer_tasks() is handled asynchrously
 */
struct cpuset_remove_tasks_struct {
	struct work_struct work;
	struct cpuset *cs;
};

/*
 * Exclusive CPUs distributed out to sub-partitions of top_cpuset
 */
static cpumask_var_t	subpartitions_cpus;

/*
 * Exclusive CPUs in isolated partitions
 */
static cpumask_var_t	isolated_cpus;

/* List of remote partition root children */
static struct list_head remote_children;

/*
 * Partition root states:
 *
 *   0 - member (not a partition root)
 *   1 - partition root
 *   2 - partition root without load balancing (isolated)
 *  -1 - invalid partition root
 *  -2 - invalid isolated partition root
 */
#define PRS_MEMBER		0
#define PRS_ROOT		1
#define PRS_ISOLATED		2
#define PRS_INVALID_ROOT	-1
#define PRS_INVALID_ISOLATED	-2

static inline bool is_prs_invalid(int prs_state)
{
	return prs_state < 0;
}

/*
 * Temporary cpumasks for working with partitions that are passed among
 * functions to avoid memory allocation in inner functions.
 */
struct tmpmasks {
	cpumask_var_t addmask, delmask;	/* For partition root */
	cpumask_var_t new_cpus;		/* For update_cpumasks_hier() */
};

static inline struct cpuset *css_cs(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct cpuset, css) : NULL;
}

/* Retrieve the cpuset for a task */
static inline struct cpuset *task_cs(struct task_struct *task)
{
	return css_cs(task_css(task, cpuset_cgrp_id));
}

static inline struct cpuset *parent_cs(struct cpuset *cs)
{
	return css_cs(cs->css.parent);
}

void inc_dl_tasks_cs(struct task_struct *p)
{
	struct cpuset *cs = task_cs(p);

	cs->nr_deadline_tasks++;
}

void dec_dl_tasks_cs(struct task_struct *p)
{
	struct cpuset *cs = task_cs(p);

	cs->nr_deadline_tasks--;
}

/* bits in struct cpuset flags field */
typedef enum {
	CS_ONLINE,
	CS_CPU_EXCLUSIVE,
	CS_MEM_EXCLUSIVE,
	CS_MEM_HARDWALL,
	CS_MEMORY_MIGRATE,
	CS_SCHED_LOAD_BALANCE,
	CS_SPREAD_PAGE,
	CS_SPREAD_SLAB,
} cpuset_flagbits_t;

/* convenient tests for these bits */
static inline bool is_cpuset_online(struct cpuset *cs)
{
	return test_bit(CS_ONLINE, &cs->flags) && !css_is_dying(&cs->css);
}

static inline int is_cpu_exclusive(const struct cpuset *cs)
{
	return test_bit(CS_CPU_EXCLUSIVE, &cs->flags);
}

static inline int is_mem_exclusive(const struct cpuset *cs)
{
	return test_bit(CS_MEM_EXCLUSIVE, &cs->flags);
}

static inline int is_mem_hardwall(const struct cpuset *cs)
{
	return test_bit(CS_MEM_HARDWALL, &cs->flags);
}

static inline int is_sched_load_balance(const struct cpuset *cs)
{
	return test_bit(CS_SCHED_LOAD_BALANCE, &cs->flags);
}

static inline int is_memory_migrate(const struct cpuset *cs)
{
	return test_bit(CS_MEMORY_MIGRATE, &cs->flags);
}

static inline int is_spread_page(const struct cpuset *cs)
{
	return test_bit(CS_SPREAD_PAGE, &cs->flags);
}

static inline int is_spread_slab(const struct cpuset *cs)
{
	return test_bit(CS_SPREAD_SLAB, &cs->flags);
}

static inline int is_partition_valid(const struct cpuset *cs)
{
	return cs->partition_root_state > 0;
}

static inline int is_partition_invalid(const struct cpuset *cs)
{
	return cs->partition_root_state < 0;
}

/*
 * Callers should hold callback_lock to modify partition_root_state.
 */
static inline void make_partition_invalid(struct cpuset *cs)
{
	if (cs->partition_root_state > 0)
		cs->partition_root_state = -cs->partition_root_state;
}

/*
 * Send notification event of whenever partition_root_state changes.
 */
static inline void notify_partition_change(struct cpuset *cs, int old_prs)
{
	if (old_prs == cs->partition_root_state)
		return;
	cgroup_file_notify(&cs->partition_file);

	/* Reset prs_err if not invalid */
	if (is_partition_valid(cs))
		WRITE_ONCE(cs->prs_err, PERR_NONE);
}

static struct cpuset top_cpuset = {
	.flags = BIT(CS_ONLINE) | BIT(CS_CPU_EXCLUSIVE) |
		 BIT(CS_MEM_EXCLUSIVE) | BIT(CS_SCHED_LOAD_BALANCE),
	.partition_root_state = PRS_ROOT,
	.relax_domain_level = -1,
	.remote_sibling = LIST_HEAD_INIT(top_cpuset.remote_sibling),
};

/**
 * cpuset_for_each_child - traverse online children of a cpuset
 * @child_cs: loop cursor pointing to the current child
 * @pos_css: used for iteration
 * @parent_cs: target cpuset to walk children of
 *
 * Walk @child_cs through the online children of @parent_cs.  Must be used
 * with RCU read locked.
 */
#define cpuset_for_each_child(child_cs, pos_css, parent_cs)		\
	css_for_each_child((pos_css), &(parent_cs)->css)		\
		if (is_cpuset_online(((child_cs) = css_cs((pos_css)))))

/**
 * cpuset_for_each_descendant_pre - pre-order walk of a cpuset's descendants
 * @des_cs: loop cursor pointing to the current descendant
 * @pos_css: used for iteration
 * @root_cs: target cpuset to walk ancestor of
 *
 * Walk @des_cs through the online descendants of @root_cs.  Must be used
 * with RCU read locked.  The caller may modify @pos_css by calling
 * css_rightmost_descendant() to skip subtree.  @root_cs is included in the
 * iteration and the first node to be visited.
 */
#define cpuset_for_each_descendant_pre(des_cs, pos_css, root_cs)	\
	css_for_each_descendant_pre((pos_css), &(root_cs)->css)		\
		if (is_cpuset_online(((des_cs) = css_cs((pos_css)))))

/*
 * There are two global locks guarding cpuset structures - cpuset_mutex and
 * callback_lock. We also require taking task_lock() when dereferencing a
 * task's cpuset pointer. See "The task_lock() exception", at the end of this
 * comment.  The cpuset code uses only cpuset_mutex. Other kernel subsystems
 * can use cpuset_lock()/cpuset_unlock() to prevent change to cpuset
 * structures. Note that cpuset_mutex needs to be a mutex as it is used in
 * paths that rely on priority inheritance (e.g. scheduler - on RT) for
 * correctness.
 *
 * A task must hold both locks to modify cpusets.  If a task holds
 * cpuset_mutex, it blocks others, ensuring that it is the only task able to
 * also acquire callback_lock and be able to modify cpusets.  It can perform
 * various checks on the cpuset structure first, knowing nothing will change.
 * It can also allocate memory while just holding cpuset_mutex.  While it is
 * performing these checks, various callback routines can briefly acquire
 * callback_lock to query cpusets.  Once it is ready to make the changes, it
 * takes callback_lock, blocking everyone else.
 *
 * Calls to the kernel memory allocator can not be made while holding
 * callback_lock, as that would risk double tripping on callback_lock
 * from one of the callbacks into the cpuset code from within
 * __alloc_pages().
 *
 * If a task is only holding callback_lock, then it has read-only
 * access to cpusets.
 *
 * Now, the task_struct fields mems_allowed and mempolicy may be changed
 * by other task, we use alloc_lock in the task_struct fields to protect
 * them.
 *
 * The cpuset_common_file_read() handlers only hold callback_lock across
 * small pieces of code, such as when reading out possibly multi-word
 * cpumasks and nodemasks.
 *
 * Accessing a task's cpuset should be done in accordance with the
 * guidelines for accessing subsystem state in kernel/cgroup.c
 */

static DEFINE_MUTEX(cpuset_mutex);

void cpuset_lock(void)
{
	mutex_lock(&cpuset_mutex);
}

void cpuset_unlock(void)
{
	mutex_unlock(&cpuset_mutex);
}

static DEFINE_SPINLOCK(callback_lock);

static struct workqueue_struct *cpuset_migrate_mm_wq;

static DECLARE_WAIT_QUEUE_HEAD(cpuset_attach_wq);

static inline void check_insane_mems_config(nodemask_t *nodes)
{
	if (!cpusets_insane_config() &&
		movable_only_nodes(nodes)) {
		static_branch_enable(&cpusets_insane_config_key);
		pr_info("Unsupported (movable nodes only) cpuset configuration detected (nmask=%*pbl)!\n"
			"Cpuset allocations might fail even with a lot of memory available.\n",
			nodemask_pr_args(nodes));
	}
}

/*
 * Cgroup v2 behavior is used on the "cpus" and "mems" control files when
 * on default hierarchy or when the cpuset_v2_mode flag is set by mounting
 * the v1 cpuset cgroup filesystem with the "cpuset_v2_mode" mount option.
 * With v2 behavior, "cpus" and "mems" are always what the users have
 * requested and won't be changed by hotplug events. Only the effective
 * cpus or mems will be affected.
 */
static inline bool is_in_v2_mode(void)
{
	return cgroup_subsys_on_dfl(cpuset_cgrp_subsys) ||
	      (cpuset_cgrp_subsys.root->flags & CGRP_ROOT_CPUSET_V2_MODE);
}

/**
 * partition_is_populated - check if partition has tasks
 * @cs: partition root to be checked
 * @excluded_child: a child cpuset to be excluded in task checking
 * Return: true if there are tasks, false otherwise
 *
 * It is assumed that @cs is a valid partition root. @excluded_child should
 * be non-NULL when this cpuset is going to become a partition itself.
 */
static inline bool partition_is_populated(struct cpuset *cs,
					  struct cpuset *excluded_child)
{
	struct cgroup_subsys_state *css;
	struct cpuset *child;

	if (cs->css.cgroup->nr_populated_csets)
		return true;
	if (!excluded_child && !cs->nr_subparts)
		return cgroup_is_populated(cs->css.cgroup);

	rcu_read_lock();
	cpuset_for_each_child(child, css, cs) {
		if (child == excluded_child)
			continue;
		if (is_partition_valid(child))
			continue;
		if (cgroup_is_populated(child->css.cgroup)) {
			rcu_read_unlock();
			return true;
		}
	}
	rcu_read_unlock();
	return false;
}

/*
 * Return in pmask the portion of a task's cpusets's cpus_allowed that
 * are online and are capable of running the task.  If none are found,
 * walk up the cpuset hierarchy until we find one that does have some
 * appropriate cpus.
 *
 * One way or another, we guarantee to return some non-empty subset
 * of cpu_online_mask.
 *
 * Call with callback_lock or cpuset_mutex held.
 */
static void guarantee_online_cpus(struct task_struct *tsk,
				  struct cpumask *pmask)
{
	const struct cpumask *possible_mask = task_cpu_possible_mask(tsk);
	struct cpuset *cs;

	if (WARN_ON(!cpumask_and(pmask, possible_mask, cpu_online_mask)))
		cpumask_copy(pmask, cpu_online_mask);

	rcu_read_lock();
	cs = task_cs(tsk);

	while (!cpumask_intersects(cs->effective_cpus, pmask))
		cs = parent_cs(cs);

	cpumask_and(pmask, pmask, cs->effective_cpus);
	rcu_read_unlock();
}

/*
 * Return in *pmask the portion of a cpusets's mems_allowed that
 * are online, with memory.  If none are online with memory, walk
 * up the cpuset hierarchy until we find one that does have some
 * online mems.  The top cpuset always has some mems online.
 *
 * One way or another, we guarantee to return some non-empty subset
 * of node_states[N_MEMORY].
 *
 * Call with callback_lock or cpuset_mutex held.
 */
static void guarantee_online_mems(struct cpuset *cs, nodemask_t *pmask)
{
	while (!nodes_intersects(cs->effective_mems, node_states[N_MEMORY]))
		cs = parent_cs(cs);
	nodes_and(*pmask, cs->effective_mems, node_states[N_MEMORY]);
}

/*
 * update task's spread flag if cpuset's page/slab spread flag is set
 *
 * Call with callback_lock or cpuset_mutex held. The check can be skipped
 * if on default hierarchy.
 */
static void cpuset_update_task_spread_flags(struct cpuset *cs,
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

/**
 * alloc_cpumasks - allocate three cpumasks for cpuset
 * @cs:  the cpuset that have cpumasks to be allocated.
 * @tmp: the tmpmasks structure pointer
 * Return: 0 if successful, -ENOMEM otherwise.
 *
 * Only one of the two input arguments should be non-NULL.
 */
static inline int alloc_cpumasks(struct cpuset *cs, struct tmpmasks *tmp)
{
	cpumask_var_t *pmask1, *pmask2, *pmask3, *pmask4;

	if (cs) {
		pmask1 = &cs->cpus_allowed;
		pmask2 = &cs->effective_cpus;
		pmask3 = &cs->effective_xcpus;
		pmask4 = &cs->exclusive_cpus;
	} else {
		pmask1 = &tmp->new_cpus;
		pmask2 = &tmp->addmask;
		pmask3 = &tmp->delmask;
		pmask4 = NULL;
	}

	if (!zalloc_cpumask_var(pmask1, GFP_KERNEL))
		return -ENOMEM;

	if (!zalloc_cpumask_var(pmask2, GFP_KERNEL))
		goto free_one;

	if (!zalloc_cpumask_var(pmask3, GFP_KERNEL))
		goto free_two;

	if (pmask4 && !zalloc_cpumask_var(pmask4, GFP_KERNEL))
		goto free_three;


	return 0;

free_three:
	free_cpumask_var(*pmask3);
free_two:
	free_cpumask_var(*pmask2);
free_one:
	free_cpumask_var(*pmask1);
	return -ENOMEM;
}

/**
 * free_cpumasks - free cpumasks in a tmpmasks structure
 * @cs:  the cpuset that have cpumasks to be free.
 * @tmp: the tmpmasks structure pointer
 */
static inline void free_cpumasks(struct cpuset *cs, struct tmpmasks *tmp)
{
	if (cs) {
		free_cpumask_var(cs->cpus_allowed);
		free_cpumask_var(cs->effective_cpus);
		free_cpumask_var(cs->effective_xcpus);
		free_cpumask_var(cs->exclusive_cpus);
	}
	if (tmp) {
		free_cpumask_var(tmp->new_cpus);
		free_cpumask_var(tmp->addmask);
		free_cpumask_var(tmp->delmask);
	}
}

/**
 * alloc_trial_cpuset - allocate a trial cpuset
 * @cs: the cpuset that the trial cpuset duplicates
 */
static struct cpuset *alloc_trial_cpuset(struct cpuset *cs)
{
	struct cpuset *trial;

	trial = kmemdup(cs, sizeof(*cs), GFP_KERNEL);
	if (!trial)
		return NULL;

	if (alloc_cpumasks(trial, NULL)) {
		kfree(trial);
		return NULL;
	}

	cpumask_copy(trial->cpus_allowed, cs->cpus_allowed);
	cpumask_copy(trial->effective_cpus, cs->effective_cpus);
	cpumask_copy(trial->effective_xcpus, cs->effective_xcpus);
	cpumask_copy(trial->exclusive_cpus, cs->exclusive_cpus);
	return trial;
}

/**
 * free_cpuset - free the cpuset
 * @cs: the cpuset to be freed
 */
static inline void free_cpuset(struct cpuset *cs)
{
	free_cpumasks(cs, NULL);
	kfree(cs);
}

static inline struct cpumask *fetch_xcpus(struct cpuset *cs)
{
	return !cpumask_empty(cs->exclusive_cpus) ? cs->exclusive_cpus :
	       cpumask_empty(cs->effective_xcpus) ? cs->cpus_allowed
						  : cs->effective_xcpus;
}

/*
 * cpusets_are_exclusive() - check if two cpusets are exclusive
 *
 * Return true if exclusive, false if not
 */
static inline bool cpusets_are_exclusive(struct cpuset *cs1, struct cpuset *cs2)
{
	struct cpumask *xcpus1 = fetch_xcpus(cs1);
	struct cpumask *xcpus2 = fetch_xcpus(cs2);

	if (cpumask_intersects(xcpus1, xcpus2))
		return false;
	return true;
}

/*
 * validate_change_legacy() - Validate conditions specific to legacy (v1)
 *                            behavior.
 */
static int validate_change_legacy(struct cpuset *cur, struct cpuset *trial)
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

/*
 * validate_change() - Used to validate that any proposed cpuset change
 *		       follows the structural rules for cpusets.
 *
 * If we replaced the flag and mask values of the current cpuset
 * (cur) with those values in the trial cpuset (trial), would
 * our various subset and exclusive rules still be valid?  Presumes
 * cpuset_mutex held.
 *
 * 'cur' is the address of an actual, in-use cpuset.  Operations
 * such as list traversal that depend on the actual address of the
 * cpuset in the list must use cur below, not trial.
 *
 * 'trial' is the address of bulk structure copy of cur, with
 * perhaps one or more of the fields cpus_allowed, mems_allowed,
 * or flags changed to new, trial values.
 *
 * Return 0 if valid, -errno if not.
 */

static int validate_change(struct cpuset *cur, struct cpuset *trial)
{
	struct cgroup_subsys_state *css;
	struct cpuset *c, *par;
	int ret = 0;

	rcu_read_lock();

	if (!is_in_v2_mode())
		ret = validate_change_legacy(cur, trial);
	if (ret)
		goto out;

	/* Remaining checks don't apply to root cpuset */
	if (cur == &top_cpuset)
		goto out;

	par = parent_cs(cur);

	/*
	 * Cpusets with tasks - existing or newly being attached - can't
	 * be changed to have empty cpus_allowed or mems_allowed.
	 */
	ret = -ENOSPC;
	if ((cgroup_is_populated(cur->css.cgroup) || cur->attach_in_progress)) {
		if (!cpumask_empty(cur->cpus_allowed) &&
		    cpumask_empty(trial->cpus_allowed))
			goto out;
		if (!nodes_empty(cur->mems_allowed) &&
		    nodes_empty(trial->mems_allowed))
			goto out;
	}

	/*
	 * We can't shrink if we won't have enough room for SCHED_DEADLINE
	 * tasks.
	 */
	ret = -EBUSY;
	if (is_cpu_exclusive(cur) &&
	    !cpuset_cpumask_can_shrink(cur->cpus_allowed,
				       trial->cpus_allowed))
		goto out;

	/*
	 * If either I or some sibling (!= me) is exclusive, we can't
	 * overlap
	 */
	ret = -EINVAL;
	cpuset_for_each_child(c, css, par) {
		if ((is_cpu_exclusive(trial) || is_cpu_exclusive(c)) &&
		    c != cur) {
			if (!cpusets_are_exclusive(trial, c))
				goto out;
		}
		if ((is_mem_exclusive(trial) || is_mem_exclusive(c)) &&
		    c != cur &&
		    nodes_intersects(trial->mems_allowed, c->mems_allowed))
			goto out;
	}

	ret = 0;
out:
	rcu_read_unlock();
	return ret;
}

#ifdef CONFIG_SMP
/*
 * Helper routine for generate_sched_domains().
 * Do cpusets a, b have overlapping effective cpus_allowed masks?
 */
static int cpusets_overlap(struct cpuset *a, struct cpuset *b)
{
	return cpumask_intersects(a->effective_cpus, b->effective_cpus);
}

static void
update_domain_attr(struct sched_domain_attr *dattr, struct cpuset *c)
{
	if (dattr->relax_domain_level < c->relax_domain_level)
		dattr->relax_domain_level = c->relax_domain_level;
	return;
}

static void update_domain_attr_tree(struct sched_domain_attr *dattr,
				    struct cpuset *root_cs)
{
	struct cpuset *cp;
	struct cgroup_subsys_state *pos_css;

	rcu_read_lock();
	cpuset_for_each_descendant_pre(cp, pos_css, root_cs) {
		/* skip the whole subtree if @cp doesn't have any CPU */
		if (cpumask_empty(cp->cpus_allowed)) {
			pos_css = css_rightmost_descendant(pos_css);
			continue;
		}

		if (is_sched_load_balance(cp))
			update_domain_attr(dattr, cp);
	}
	rcu_read_unlock();
}

/* Must be called with cpuset_mutex held.  */
static inline int nr_cpusets(void)
{
	/* jump label reference count + the top-level cpuset */
	return static_key_count(&cpusets_enabled_key.key) + 1;
}

/*
 * generate_sched_domains()
 *
 * This function builds a partial partition of the systems CPUs
 * A 'partial partition' is a set of non-overlapping subsets whose
 * union is a subset of that set.
 * The output of this function needs to be passed to kernel/sched/core.c
 * partition_sched_domains() routine, which will rebuild the scheduler's
 * load balancing domains (sched domains) as specified by that partial
 * partition.
 *
 * See "What is sched_load_balance" in Documentation/admin-guide/cgroup-v1/cpusets.rst
 * for a background explanation of this.
 *
 * Does not return errors, on the theory that the callers of this
 * routine would rather not worry about failures to rebuild sched
 * domains when operating in the severe memory shortage situations
 * that could cause allocation failures below.
 *
 * Must be called with cpuset_mutex held.
 *
 * The three key local variables below are:
 *    cp - cpuset pointer, used (together with pos_css) to perform a
 *	   top-down scan of all cpusets. For our purposes, rebuilding
 *	   the schedulers sched domains, we can ignore !is_sched_load_
 *	   balance cpusets.
 *  csa  - (for CpuSet Array) Array of pointers to all the cpusets
 *	   that need to be load balanced, for convenient iterative
 *	   access by the subsequent code that finds the best partition,
 *	   i.e the set of domains (subsets) of CPUs such that the
 *	   cpus_allowed of every cpuset marked is_sched_load_balance
 *	   is a subset of one of these domains, while there are as
 *	   many such domains as possible, each as small as possible.
 * doms  - Conversion of 'csa' to an array of cpumasks, for passing to
 *	   the kernel/sched/core.c routine partition_sched_domains() in a
 *	   convenient format, that can be easily compared to the prior
 *	   value to determine what partition elements (sched domains)
 *	   were changed (added or removed.)
 *
 * Finding the best partition (set of domains):
 *	The triple nested loops below over i, j, k scan over the
 *	load balanced cpusets (using the array of cpuset pointers in
 *	csa[]) looking for pairs of cpusets that have overlapping
 *	cpus_allowed, but which don't have the same 'pn' partition
 *	number and gives them in the same partition number.  It keeps
 *	looping on the 'restart' label until it can no longer find
 *	any such pairs.
 *
 *	The union of the cpus_allowed masks from the set of
 *	all cpusets having the same 'pn' value then form the one
 *	element of the partition (one sched domain) to be passed to
 *	partition_sched_domains().
 */
static int generate_sched_domains(cpumask_var_t **domains,
			struct sched_domain_attr **attributes)
{
	struct cpuset *cp;	/* top-down scan of cpusets */
	struct cpuset **csa;	/* array of all cpuset ptrs */
	int csn;		/* how many cpuset ptrs in csa so far */
	int i, j, k;		/* indices for partition finding loops */
	cpumask_var_t *doms;	/* resulting partition; i.e. sched domains */
	struct sched_domain_attr *dattr;  /* attributes for custom domains */
	int ndoms = 0;		/* number of sched domains in result */
	int nslot;		/* next empty doms[] struct cpumask slot */
	struct cgroup_subsys_state *pos_css;
	bool root_load_balance = is_sched_load_balance(&top_cpuset);

	doms = NULL;
	dattr = NULL;
	csa = NULL;

	/* Special case for the 99% of systems with one, full, sched domain */
	if (root_load_balance && !top_cpuset.nr_subparts) {
		ndoms = 1;
		doms = alloc_sched_domains(ndoms);
		if (!doms)
			goto done;

		dattr = kmalloc(sizeof(struct sched_domain_attr), GFP_KERNEL);
		if (dattr) {
			*dattr = SD_ATTR_INIT;
			update_domain_attr_tree(dattr, &top_cpuset);
		}
		cpumask_and(doms[0], top_cpuset.effective_cpus,
			    housekeeping_cpumask(HK_TYPE_DOMAIN));

		goto done;
	}

	csa = kmalloc_array(nr_cpusets(), sizeof(cp), GFP_KERNEL);
	if (!csa)
		goto done;
	csn = 0;

	rcu_read_lock();
	if (root_load_balance)
		csa[csn++] = &top_cpuset;
	cpuset_for_each_descendant_pre(cp, pos_css, &top_cpuset) {
		if (cp == &top_cpuset)
			continue;
		/*
		 * Continue traversing beyond @cp iff @cp has some CPUs and
		 * isn't load balancing.  The former is obvious.  The
		 * latter: All child cpusets contain a subset of the
		 * parent's cpus, so just skip them, and then we call
		 * update_domain_attr_tree() to calc relax_domain_level of
		 * the corresponding sched domain.
		 *
		 * If root is load-balancing, we can skip @cp if it
		 * is a subset of the root's effective_cpus.
		 */
		if (!cpumask_empty(cp->cpus_allowed) &&
		    !(is_sched_load_balance(cp) &&
		      cpumask_intersects(cp->cpus_allowed,
					 housekeeping_cpumask(HK_TYPE_DOMAIN))))
			continue;

		if (root_load_balance &&
		    cpumask_subset(cp->cpus_allowed, top_cpuset.effective_cpus))
			continue;

		if (is_sched_load_balance(cp) &&
		    !cpumask_empty(cp->effective_cpus))
			csa[csn++] = cp;

		/* skip @cp's subtree if not a partition root */
		if (!is_partition_valid(cp))
			pos_css = css_rightmost_descendant(pos_css);
	}
	rcu_read_unlock();

	for (i = 0; i < csn; i++)
		csa[i]->pn = i;
	ndoms = csn;

restart:
	/* Find the best partition (set of sched domains) */
	for (i = 0; i < csn; i++) {
		struct cpuset *a = csa[i];
		int apn = a->pn;

		for (j = 0; j < csn; j++) {
			struct cpuset *b = csa[j];
			int bpn = b->pn;

			if (apn != bpn && cpusets_overlap(a, b)) {
				for (k = 0; k < csn; k++) {
					struct cpuset *c = csa[k];

					if (c->pn == bpn)
						c->pn = apn;
				}
				ndoms--;	/* one less element */
				goto restart;
			}
		}
	}

	/*
	 * Now we know how many domains to create.
	 * Convert <csn, csa> to <ndoms, doms> and populate cpu masks.
	 */
	doms = alloc_sched_domains(ndoms);
	if (!doms)
		goto done;

	/*
	 * The rest of the code, including the scheduler, can deal with
	 * dattr==NULL case. No need to abort if alloc fails.
	 */
	dattr = kmalloc_array(ndoms, sizeof(struct sched_domain_attr),
			      GFP_KERNEL);

	for (nslot = 0, i = 0; i < csn; i++) {
		struct cpuset *a = csa[i];
		struct cpumask *dp;
		int apn = a->pn;

		if (apn < 0) {
			/* Skip completed partitions */
			continue;
		}

		dp = doms[nslot];

		if (nslot == ndoms) {
			static int warnings = 10;
			if (warnings) {
				pr_warn("rebuild_sched_domains confused: nslot %d, ndoms %d, csn %d, i %d, apn %d\n",
					nslot, ndoms, csn, i, apn);
				warnings--;
			}
			continue;
		}

		cpumask_clear(dp);
		if (dattr)
			*(dattr + nslot) = SD_ATTR_INIT;
		for (j = i; j < csn; j++) {
			struct cpuset *b = csa[j];

			if (apn == b->pn) {
				cpumask_or(dp, dp, b->effective_cpus);
				cpumask_and(dp, dp, housekeeping_cpumask(HK_TYPE_DOMAIN));
				if (dattr)
					update_domain_attr_tree(dattr + nslot, b);

				/* Done with this partition */
				b->pn = -1;
			}
		}
		nslot++;
	}
	BUG_ON(nslot != ndoms);

done:
	kfree(csa);

	/*
	 * Fallback to the default domain if kmalloc() failed.
	 * See comments in partition_sched_domains().
	 */
	if (doms == NULL)
		ndoms = 1;

	*domains    = doms;
	*attributes = dattr;
	return ndoms;
}

static void dl_update_tasks_root_domain(struct cpuset *cs)
{
	struct css_task_iter it;
	struct task_struct *task;

	if (cs->nr_deadline_tasks == 0)
		return;

	css_task_iter_start(&cs->css, 0, &it);

	while ((task = css_task_iter_next(&it)))
		dl_add_task_root_domain(task);

	css_task_iter_end(&it);
}

static void dl_rebuild_rd_accounting(void)
{
	struct cpuset *cs = NULL;
	struct cgroup_subsys_state *pos_css;

	lockdep_assert_held(&cpuset_mutex);
	lockdep_assert_cpus_held();
	lockdep_assert_held(&sched_domains_mutex);

	rcu_read_lock();

	/*
	 * Clear default root domain DL accounting, it will be computed again
	 * if a task belongs to it.
	 */
	dl_clear_root_domain(&def_root_domain);

	cpuset_for_each_descendant_pre(cs, pos_css, &top_cpuset) {

		if (cpumask_empty(cs->effective_cpus)) {
			pos_css = css_rightmost_descendant(pos_css);
			continue;
		}

		css_get(&cs->css);

		rcu_read_unlock();

		dl_update_tasks_root_domain(cs);

		rcu_read_lock();
		css_put(&cs->css);
	}
	rcu_read_unlock();
}

static void
partition_and_rebuild_sched_domains(int ndoms_new, cpumask_var_t doms_new[],
				    struct sched_domain_attr *dattr_new)
{
	mutex_lock(&sched_domains_mutex);
	partition_sched_domains_locked(ndoms_new, doms_new, dattr_new);
	dl_rebuild_rd_accounting();
	mutex_unlock(&sched_domains_mutex);
}

/*
 * Rebuild scheduler domains.
 *
 * If the flag 'sched_load_balance' of any cpuset with non-empty
 * 'cpus' changes, or if the 'cpus' allowed changes in any cpuset
 * which has that flag enabled, or if any cpuset with a non-empty
 * 'cpus' is removed, then call this routine to rebuild the
 * scheduler's dynamic sched domains.
 *
 * Call with cpuset_mutex held.  Takes cpus_read_lock().
 */
static void rebuild_sched_domains_locked(void)
{
	struct cgroup_subsys_state *pos_css;
	struct sched_domain_attr *attr;
	cpumask_var_t *doms;
	struct cpuset *cs;
	int ndoms;

	lockdep_assert_cpus_held();
	lockdep_assert_held(&cpuset_mutex);

	/*
	 * If we have raced with CPU hotplug, return early to avoid
	 * passing doms with offlined cpu to partition_sched_domains().
	 * Anyways, cpuset_handle_hotplug() will rebuild sched domains.
	 *
	 * With no CPUs in any subpartitions, top_cpuset's effective CPUs
	 * should be the same as the active CPUs, so checking only top_cpuset
	 * is enough to detect racing CPU offlines.
	 */
	if (cpumask_empty(subpartitions_cpus) &&
	    !cpumask_equal(top_cpuset.effective_cpus, cpu_active_mask))
		return;

	/*
	 * With subpartition CPUs, however, the effective CPUs of a partition
	 * root should be only a subset of the active CPUs.  Since a CPU in any
	 * partition root could be offlined, all must be checked.
	 */
	if (top_cpuset.nr_subparts) {
		rcu_read_lock();
		cpuset_for_each_descendant_pre(cs, pos_css, &top_cpuset) {
			if (!is_partition_valid(cs)) {
				pos_css = css_rightmost_descendant(pos_css);
				continue;
			}
			if (!cpumask_subset(cs->effective_cpus,
					    cpu_active_mask)) {
				rcu_read_unlock();
				return;
			}
		}
		rcu_read_unlock();
	}

	/* Generate domain masks and attrs */
	ndoms = generate_sched_domains(&doms, &attr);

	/* Have scheduler rebuild the domains */
	partition_and_rebuild_sched_domains(ndoms, doms, attr);
}
#else /* !CONFIG_SMP */
static void rebuild_sched_domains_locked(void)
{
}
#endif /* CONFIG_SMP */

static void rebuild_sched_domains_cpuslocked(void)
{
	mutex_lock(&cpuset_mutex);
	rebuild_sched_domains_locked();
	mutex_unlock(&cpuset_mutex);
}

void rebuild_sched_domains(void)
{
	cpus_read_lock();
	rebuild_sched_domains_cpuslocked();
	cpus_read_unlock();
}

/**
 * update_tasks_cpumask - Update the cpumasks of tasks in the cpuset.
 * @cs: the cpuset in which each task's cpus_allowed mask needs to be changed
 * @new_cpus: the temp variable for the new effective_cpus mask
 *
 * Iterate through each task of @cs updating its cpus_allowed to the
 * effective cpuset's.  As this function is called with cpuset_mutex held,
 * cpuset membership stays stable. For top_cpuset, task_cpu_possible_mask()
 * is used instead of effective_cpus to make sure all offline CPUs are also
 * included as hotplug code won't update cpumasks for tasks in top_cpuset.
 */
static void update_tasks_cpumask(struct cpuset *cs, struct cpumask *new_cpus)
{
	struct css_task_iter it;
	struct task_struct *task;
	bool top_cs = cs == &top_cpuset;

	css_task_iter_start(&cs->css, 0, &it);
	while ((task = css_task_iter_next(&it))) {
		const struct cpumask *possible_mask = task_cpu_possible_mask(task);

		if (top_cs) {
			/*
			 * Percpu kthreads in top_cpuset are ignored
			 */
			if (kthread_is_per_cpu(task))
				continue;
			cpumask_andnot(new_cpus, possible_mask, subpartitions_cpus);
		} else {
			cpumask_and(new_cpus, possible_mask, cs->effective_cpus);
		}
		set_cpus_allowed_ptr(task, new_cpus);
	}
	css_task_iter_end(&it);
}

/**
 * compute_effective_cpumask - Compute the effective cpumask of the cpuset
 * @new_cpus: the temp variable for the new effective_cpus mask
 * @cs: the cpuset the need to recompute the new effective_cpus mask
 * @parent: the parent cpuset
 *
 * The result is valid only if the given cpuset isn't a partition root.
 */
static void compute_effective_cpumask(struct cpumask *new_cpus,
				      struct cpuset *cs, struct cpuset *parent)
{
	cpumask_and(new_cpus, cs->cpus_allowed, parent->effective_cpus);
}

/*
 * Commands for update_parent_effective_cpumask
 */
enum partition_cmd {
	partcmd_enable,		/* Enable partition root	  */
	partcmd_enablei,	/* Enable isolated partition root */
	partcmd_disable,	/* Disable partition root	  */
	partcmd_update,		/* Update parent's effective_cpus */
	partcmd_invalidate,	/* Make partition invalid	  */
};

static int update_flag(cpuset_flagbits_t bit, struct cpuset *cs,
		       int turning_on);
static void update_sibling_cpumasks(struct cpuset *parent, struct cpuset *cs,
				    struct tmpmasks *tmp);

/*
 * Update partition exclusive flag
 *
 * Return: 0 if successful, an error code otherwise
 */
static int update_partition_exclusive(struct cpuset *cs, int new_prs)
{
	bool exclusive = (new_prs > 0);

	if (exclusive && !is_cpu_exclusive(cs)) {
		if (update_flag(CS_CPU_EXCLUSIVE, cs, 1))
			return PERR_NOTEXCL;
	} else if (!exclusive && is_cpu_exclusive(cs)) {
		/* Turning off CS_CPU_EXCLUSIVE will not return error */
		update_flag(CS_CPU_EXCLUSIVE, cs, 0);
	}
	return 0;
}

/*
 * Update partition load balance flag and/or rebuild sched domain
 *
 * Changing load balance flag will automatically call
 * rebuild_sched_domains_locked().
 * This function is for cgroup v2 only.
 */
static void update_partition_sd_lb(struct cpuset *cs, int old_prs)
{
	int new_prs = cs->partition_root_state;
	bool rebuild_domains = (new_prs > 0) || (old_prs > 0);
	bool new_lb;

	/*
	 * If cs is not a valid partition root, the load balance state
	 * will follow its parent.
	 */
	if (new_prs > 0) {
		new_lb = (new_prs != PRS_ISOLATED);
	} else {
		new_lb = is_sched_load_balance(parent_cs(cs));
	}
	if (new_lb != !!is_sched_load_balance(cs)) {
		rebuild_domains = true;
		if (new_lb)
			set_bit(CS_SCHED_LOAD_BALANCE, &cs->flags);
		else
			clear_bit(CS_SCHED_LOAD_BALANCE, &cs->flags);
	}

	if (rebuild_domains)
		rebuild_sched_domains_locked();
}

/*
 * tasks_nocpu_error - Return true if tasks will have no effective_cpus
 */
static bool tasks_nocpu_error(struct cpuset *parent, struct cpuset *cs,
			      struct cpumask *xcpus)
{
	/*
	 * A populated partition (cs or parent) can't have empty effective_cpus
	 */
	return (cpumask_subset(parent->effective_cpus, xcpus) &&
		partition_is_populated(parent, cs)) ||
	       (!cpumask_intersects(xcpus, cpu_active_mask) &&
		partition_is_populated(cs, NULL));
}

static void reset_partition_data(struct cpuset *cs)
{
	struct cpuset *parent = parent_cs(cs);

	if (!cgroup_subsys_on_dfl(cpuset_cgrp_subsys))
		return;

	lockdep_assert_held(&callback_lock);

	cs->nr_subparts = 0;
	if (cpumask_empty(cs->exclusive_cpus)) {
		cpumask_clear(cs->effective_xcpus);
		if (is_cpu_exclusive(cs))
			clear_bit(CS_CPU_EXCLUSIVE, &cs->flags);
	}
	if (!cpumask_and(cs->effective_cpus,
			 parent->effective_cpus, cs->cpus_allowed)) {
		cs->use_parent_ecpus = true;
		parent->child_ecpus_count++;
		cpumask_copy(cs->effective_cpus, parent->effective_cpus);
	}
}

/*
 * partition_xcpus_newstate - Exclusive CPUs state change
 * @old_prs: old partition_root_state
 * @new_prs: new partition_root_state
 * @xcpus: exclusive CPUs with state change
 */
static void partition_xcpus_newstate(int old_prs, int new_prs, struct cpumask *xcpus)
{
	WARN_ON_ONCE(old_prs == new_prs);
	if (new_prs == PRS_ISOLATED)
		cpumask_or(isolated_cpus, isolated_cpus, xcpus);
	else
		cpumask_andnot(isolated_cpus, isolated_cpus, xcpus);
}

/*
 * partition_xcpus_add - Add new exclusive CPUs to partition
 * @new_prs: new partition_root_state
 * @parent: parent cpuset
 * @xcpus: exclusive CPUs to be added
 * Return: true if isolated_cpus modified, false otherwise
 *
 * Remote partition if parent == NULL
 */
static bool partition_xcpus_add(int new_prs, struct cpuset *parent,
				struct cpumask *xcpus)
{
	bool isolcpus_updated;

	WARN_ON_ONCE(new_prs < 0);
	lockdep_assert_held(&callback_lock);
	if (!parent)
		parent = &top_cpuset;


	if (parent == &top_cpuset)
		cpumask_or(subpartitions_cpus, subpartitions_cpus, xcpus);

	isolcpus_updated = (new_prs != parent->partition_root_state);
	if (isolcpus_updated)
		partition_xcpus_newstate(parent->partition_root_state, new_prs,
					 xcpus);

	cpumask_andnot(parent->effective_cpus, parent->effective_cpus, xcpus);
	return isolcpus_updated;
}

/*
 * partition_xcpus_del - Remove exclusive CPUs from partition
 * @old_prs: old partition_root_state
 * @parent: parent cpuset
 * @xcpus: exclusive CPUs to be removed
 * Return: true if isolated_cpus modified, false otherwise
 *
 * Remote partition if parent == NULL
 */
static bool partition_xcpus_del(int old_prs, struct cpuset *parent,
				struct cpumask *xcpus)
{
	bool isolcpus_updated;

	WARN_ON_ONCE(old_prs < 0);
	lockdep_assert_held(&callback_lock);
	if (!parent)
		parent = &top_cpuset;

	if (parent == &top_cpuset)
		cpumask_andnot(subpartitions_cpus, subpartitions_cpus, xcpus);

	isolcpus_updated = (old_prs != parent->partition_root_state);
	if (isolcpus_updated)
		partition_xcpus_newstate(old_prs, parent->partition_root_state,
					 xcpus);

	cpumask_and(xcpus, xcpus, cpu_active_mask);
	cpumask_or(parent->effective_cpus, parent->effective_cpus, xcpus);
	return isolcpus_updated;
}

static void update_unbound_workqueue_cpumask(bool isolcpus_updated)
{
	int ret;

	lockdep_assert_cpus_held();

	if (!isolcpus_updated)
		return;

	ret = workqueue_unbound_exclude_cpumask(isolated_cpus);
	WARN_ON_ONCE(ret < 0);
}

/**
 * cpuset_cpu_is_isolated - Check if the given CPU is isolated
 * @cpu: the CPU number to be checked
 * Return: true if CPU is used in an isolated partition, false otherwise
 */
bool cpuset_cpu_is_isolated(int cpu)
{
	return cpumask_test_cpu(cpu, isolated_cpus);
}
EXPORT_SYMBOL_GPL(cpuset_cpu_is_isolated);

/*
 * compute_effective_exclusive_cpumask - compute effective exclusive CPUs
 * @cs: cpuset
 * @xcpus: effective exclusive CPUs value to be set
 * Return: true if xcpus is not empty, false otherwise.
 *
 * Starting with exclusive_cpus (cpus_allowed if exclusive_cpus is not set),
 * it must be a subset of cpus_allowed and parent's effective_xcpus.
 */
static bool compute_effective_exclusive_cpumask(struct cpuset *cs,
						struct cpumask *xcpus)
{
	struct cpuset *parent = parent_cs(cs);

	if (!xcpus)
		xcpus = cs->effective_xcpus;

	if (!cpumask_empty(cs->exclusive_cpus))
		cpumask_and(xcpus, cs->exclusive_cpus, cs->cpus_allowed);
	else
		cpumask_copy(xcpus, cs->cpus_allowed);

	return cpumask_and(xcpus, xcpus, parent->effective_xcpus);
}

static inline bool is_remote_partition(struct cpuset *cs)
{
	return !list_empty(&cs->remote_sibling);
}

static inline bool is_local_partition(struct cpuset *cs)
{
	return is_partition_valid(cs) && !is_remote_partition(cs);
}

/*
 * remote_partition_enable - Enable current cpuset as a remote partition root
 * @cs: the cpuset to update
 * @new_prs: new partition_root_state
 * @tmp: temparary masks
 * Return: 1 if successful, 0 if error
 *
 * Enable the current cpuset to become a remote partition root taking CPUs
 * directly from the top cpuset. cpuset_mutex must be held by the caller.
 */
static int remote_partition_enable(struct cpuset *cs, int new_prs,
				   struct tmpmasks *tmp)
{
	bool isolcpus_updated;

	/*
	 * The user must have sysadmin privilege.
	 */
	if (!capable(CAP_SYS_ADMIN))
		return 0;

	/*
	 * The requested exclusive_cpus must not be allocated to other
	 * partitions and it can't use up all the root's effective_cpus.
	 *
	 * Note that if there is any local partition root above it or
	 * remote partition root underneath it, its exclusive_cpus must
	 * have overlapped with subpartitions_cpus.
	 */
	compute_effective_exclusive_cpumask(cs, tmp->new_cpus);
	if (cpumask_empty(tmp->new_cpus) ||
	    cpumask_intersects(tmp->new_cpus, subpartitions_cpus) ||
	    cpumask_subset(top_cpuset.effective_cpus, tmp->new_cpus))
		return 0;

	spin_lock_irq(&callback_lock);
	isolcpus_updated = partition_xcpus_add(new_prs, NULL, tmp->new_cpus);
	list_add(&cs->remote_sibling, &remote_children);
	if (cs->use_parent_ecpus) {
		struct cpuset *parent = parent_cs(cs);

		cs->use_parent_ecpus = false;
		parent->child_ecpus_count--;
	}
	spin_unlock_irq(&callback_lock);
	update_unbound_workqueue_cpumask(isolcpus_updated);

	/*
	 * Proprogate changes in top_cpuset's effective_cpus down the hierarchy.
	 */
	update_tasks_cpumask(&top_cpuset, tmp->new_cpus);
	update_sibling_cpumasks(&top_cpuset, NULL, tmp);
	return 1;
}

/*
 * remote_partition_disable - Remove current cpuset from remote partition list
 * @cs: the cpuset to update
 * @tmp: temparary masks
 *
 * The effective_cpus is also updated.
 *
 * cpuset_mutex must be held by the caller.
 */
static void remote_partition_disable(struct cpuset *cs, struct tmpmasks *tmp)
{
	bool isolcpus_updated;

	compute_effective_exclusive_cpumask(cs, tmp->new_cpus);
	WARN_ON_ONCE(!is_remote_partition(cs));
	WARN_ON_ONCE(!cpumask_subset(tmp->new_cpus, subpartitions_cpus));

	spin_lock_irq(&callback_lock);
	list_del_init(&cs->remote_sibling);
	isolcpus_updated = partition_xcpus_del(cs->partition_root_state,
					       NULL, tmp->new_cpus);
	cs->partition_root_state = -cs->partition_root_state;
	if (!cs->prs_err)
		cs->prs_err = PERR_INVCPUS;
	reset_partition_data(cs);
	spin_unlock_irq(&callback_lock);
	update_unbound_workqueue_cpumask(isolcpus_updated);

	/*
	 * Proprogate changes in top_cpuset's effective_cpus down the hierarchy.
	 */
	update_tasks_cpumask(&top_cpuset, tmp->new_cpus);
	update_sibling_cpumasks(&top_cpuset, NULL, tmp);
}

/*
 * remote_cpus_update - cpus_exclusive change of remote partition
 * @cs: the cpuset to be updated
 * @newmask: the new effective_xcpus mask
 * @tmp: temparary masks
 *
 * top_cpuset and subpartitions_cpus will be updated or partition can be
 * invalidated.
 */
static void remote_cpus_update(struct cpuset *cs, struct cpumask *newmask,
			       struct tmpmasks *tmp)
{
	bool adding, deleting;
	int prs = cs->partition_root_state;
	int isolcpus_updated = 0;

	if (WARN_ON_ONCE(!is_remote_partition(cs)))
		return;

	WARN_ON_ONCE(!cpumask_subset(cs->effective_xcpus, subpartitions_cpus));

	if (cpumask_empty(newmask))
		goto invalidate;

	adding   = cpumask_andnot(tmp->addmask, newmask, cs->effective_xcpus);
	deleting = cpumask_andnot(tmp->delmask, cs->effective_xcpus, newmask);

	/*
	 * Additions of remote CPUs is only allowed if those CPUs are
	 * not allocated to other partitions and there are effective_cpus
	 * left in the top cpuset.
	 */
	if (adding && (!capable(CAP_SYS_ADMIN) ||
		       cpumask_intersects(tmp->addmask, subpartitions_cpus) ||
		       cpumask_subset(top_cpuset.effective_cpus, tmp->addmask)))
		goto invalidate;

	spin_lock_irq(&callback_lock);
	if (adding)
		isolcpus_updated += partition_xcpus_add(prs, NULL, tmp->addmask);
	if (deleting)
		isolcpus_updated += partition_xcpus_del(prs, NULL, tmp->delmask);
	spin_unlock_irq(&callback_lock);
	update_unbound_workqueue_cpumask(isolcpus_updated);

	/*
	 * Proprogate changes in top_cpuset's effective_cpus down the hierarchy.
	 */
	update_tasks_cpumask(&top_cpuset, tmp->new_cpus);
	update_sibling_cpumasks(&top_cpuset, NULL, tmp);
	return;

invalidate:
	remote_partition_disable(cs, tmp);
}

/*
 * remote_partition_check - check if a child remote partition needs update
 * @cs: the cpuset to be updated
 * @newmask: the new effective_xcpus mask
 * @delmask: temporary mask for deletion (not in tmp)
 * @tmp: temparary masks
 *
 * This should be called before the given cs has updated its cpus_allowed
 * and/or effective_xcpus.
 */
static void remote_partition_check(struct cpuset *cs, struct cpumask *newmask,
				   struct cpumask *delmask, struct tmpmasks *tmp)
{
	struct cpuset *child, *next;
	int disable_cnt = 0;

	/*
	 * Compute the effective exclusive CPUs that will be deleted.
	 */
	if (!cpumask_andnot(delmask, cs->effective_xcpus, newmask) ||
	    !cpumask_intersects(delmask, subpartitions_cpus))
		return;	/* No deletion of exclusive CPUs in partitions */

	/*
	 * Searching the remote children list to look for those that will
	 * be impacted by the deletion of exclusive CPUs.
	 *
	 * Since a cpuset must be removed from the remote children list
	 * before it can go offline and holding cpuset_mutex will prevent
	 * any change in cpuset status. RCU read lock isn't needed.
	 */
	lockdep_assert_held(&cpuset_mutex);
	list_for_each_entry_safe(child, next, &remote_children, remote_sibling)
		if (cpumask_intersects(child->effective_cpus, delmask)) {
			remote_partition_disable(child, tmp);
			disable_cnt++;
		}
	if (disable_cnt)
		rebuild_sched_domains_locked();
}

/*
 * prstate_housekeeping_conflict - check for partition & housekeeping conflicts
 * @prstate: partition root state to be checked
 * @new_cpus: cpu mask
 * Return: true if there is conflict, false otherwise
 *
 * CPUs outside of housekeeping_cpumask(HK_TYPE_DOMAIN) can only be used in
 * an isolated partition.
 */
static bool prstate_housekeeping_conflict(int prstate, struct cpumask *new_cpus)
{
	const struct cpumask *hk_domain = housekeeping_cpumask(HK_TYPE_DOMAIN);
	bool all_in_hk = cpumask_subset(new_cpus, hk_domain);

	if (!all_in_hk && (prstate != PRS_ISOLATED))
		return true;

	return false;
}

/**
 * update_parent_effective_cpumask - update effective_cpus mask of parent cpuset
 * @cs:      The cpuset that requests change in partition root state
 * @cmd:     Partition root state change command
 * @newmask: Optional new cpumask for partcmd_update
 * @tmp:     Temporary addmask and delmask
 * Return:   0 or a partition root state error code
 *
 * For partcmd_enable*, the cpuset is being transformed from a non-partition
 * root to a partition root. The effective_xcpus (cpus_allowed if
 * effective_xcpus not set) mask of the given cpuset will be taken away from
 * parent's effective_cpus. The function will return 0 if all the CPUs listed
 * in effective_xcpus can be granted or an error code will be returned.
 *
 * For partcmd_disable, the cpuset is being transformed from a partition
 * root back to a non-partition root. Any CPUs in effective_xcpus will be
 * given back to parent's effective_cpus. 0 will always be returned.
 *
 * For partcmd_update, if the optional newmask is specified, the cpu list is
 * to be changed from effective_xcpus to newmask. Otherwise, effective_xcpus is
 * assumed to remain the same. The cpuset should either be a valid or invalid
 * partition root. The partition root state may change from valid to invalid
 * or vice versa. An error code will be returned if transitioning from
 * invalid to valid violates the exclusivity rule.
 *
 * For partcmd_invalidate, the current partition will be made invalid.
 *
 * The partcmd_enable* and partcmd_disable commands are used by
 * update_prstate(). An error code may be returned and the caller will check
 * for error.
 *
 * The partcmd_update command is used by update_cpumasks_hier() with newmask
 * NULL and update_cpumask() with newmask set. The partcmd_invalidate is used
 * by update_cpumask() with NULL newmask. In both cases, the callers won't
 * check for error and so partition_root_state and prs_error will be updated
 * directly.
 */
static int update_parent_effective_cpumask(struct cpuset *cs, int cmd,
					   struct cpumask *newmask,
					   struct tmpmasks *tmp)
{
	struct cpuset *parent = parent_cs(cs);
	int adding;	/* Adding cpus to parent's effective_cpus	*/
	int deleting;	/* Deleting cpus from parent's effective_cpus	*/
	int old_prs, new_prs;
	int part_error = PERR_NONE;	/* Partition error? */
	int subparts_delta = 0;
	struct cpumask *xcpus;		/* cs effective_xcpus */
	int isolcpus_updated = 0;
	bool nocpu;

	lockdep_assert_held(&cpuset_mutex);

	/*
	 * new_prs will only be changed for the partcmd_update and
	 * partcmd_invalidate commands.
	 */
	adding = deleting = false;
	old_prs = new_prs = cs->partition_root_state;
	xcpus = !cpumask_empty(cs->exclusive_cpus)
		? cs->effective_xcpus : cs->cpus_allowed;

	if (cmd == partcmd_invalidate) {
		if (is_prs_invalid(old_prs))
			return 0;

		/*
		 * Make the current partition invalid.
		 */
		if (is_partition_valid(parent))
			adding = cpumask_and(tmp->addmask,
					     xcpus, parent->effective_xcpus);
		if (old_prs > 0) {
			new_prs = -old_prs;
			subparts_delta--;
		}
		goto write_error;
	}

	/*
	 * The parent must be a partition root.
	 * The new cpumask, if present, or the current cpus_allowed must
	 * not be empty.
	 */
	if (!is_partition_valid(parent)) {
		return is_partition_invalid(parent)
		       ? PERR_INVPARENT : PERR_NOTPART;
	}
	if (!newmask && cpumask_empty(cs->cpus_allowed))
		return PERR_CPUSEMPTY;

	nocpu = tasks_nocpu_error(parent, cs, xcpus);

	if ((cmd == partcmd_enable) || (cmd == partcmd_enablei)) {
		/*
		 * Enabling partition root is not allowed if its
		 * effective_xcpus is empty or doesn't overlap with
		 * parent's effective_xcpus.
		 */
		if (cpumask_empty(xcpus) ||
		    !cpumask_intersects(xcpus, parent->effective_xcpus))
			return PERR_INVCPUS;

		if (prstate_housekeeping_conflict(new_prs, xcpus))
			return PERR_HKEEPING;

		/*
		 * A parent can be left with no CPU as long as there is no
		 * task directly associated with the parent partition.
		 */
		if (nocpu)
			return PERR_NOCPUS;

		cpumask_copy(tmp->delmask, xcpus);
		deleting = true;
		subparts_delta++;
		new_prs = (cmd == partcmd_enable) ? PRS_ROOT : PRS_ISOLATED;
	} else if (cmd == partcmd_disable) {
		/*
		 * May need to add cpus to parent's effective_cpus for
		 * valid partition root.
		 */
		adding = !is_prs_invalid(old_prs) &&
			  cpumask_and(tmp->addmask, xcpus, parent->effective_xcpus);
		if (adding)
			subparts_delta--;
		new_prs = PRS_MEMBER;
	} else if (newmask) {
		/*
		 * Empty cpumask is not allowed
		 */
		if (cpumask_empty(newmask)) {
			part_error = PERR_CPUSEMPTY;
			goto write_error;
		}

		/*
		 * partcmd_update with newmask:
		 *
		 * Compute add/delete mask to/from effective_cpus
		 *
		 * For valid partition:
		 *   addmask = exclusive_cpus & ~newmask
		 *			      & parent->effective_xcpus
		 *   delmask = newmask & ~exclusive_cpus
		 *		       & parent->effective_xcpus
		 *
		 * For invalid partition:
		 *   delmask = newmask & parent->effective_xcpus
		 */
		if (is_prs_invalid(old_prs)) {
			adding = false;
			deleting = cpumask_and(tmp->delmask,
					newmask, parent->effective_xcpus);
		} else {
			cpumask_andnot(tmp->addmask, xcpus, newmask);
			adding = cpumask_and(tmp->addmask, tmp->addmask,
					     parent->effective_xcpus);

			cpumask_andnot(tmp->delmask, newmask, xcpus);
			deleting = cpumask_and(tmp->delmask, tmp->delmask,
					       parent->effective_xcpus);
		}
		/*
		 * Make partition invalid if parent's effective_cpus could
		 * become empty and there are tasks in the parent.
		 */
		if (nocpu && (!adding ||
		    !cpumask_intersects(tmp->addmask, cpu_active_mask))) {
			part_error = PERR_NOCPUS;
			deleting = false;
			adding = cpumask_and(tmp->addmask,
					     xcpus, parent->effective_xcpus);
		}
	} else {
		/*
		 * partcmd_update w/o newmask
		 *
		 * delmask = effective_xcpus & parent->effective_cpus
		 *
		 * This can be called from:
		 * 1) update_cpumasks_hier()
		 * 2) cpuset_hotplug_update_tasks()
		 *
		 * Check to see if it can be transitioned from valid to
		 * invalid partition or vice versa.
		 *
		 * A partition error happens when parent has tasks and all
		 * its effective CPUs will have to be distributed out.
		 */
		WARN_ON_ONCE(!is_partition_valid(parent));
		if (nocpu) {
			part_error = PERR_NOCPUS;
			if (is_partition_valid(cs))
				adding = cpumask_and(tmp->addmask,
						xcpus, parent->effective_xcpus);
		} else if (is_partition_invalid(cs) &&
			   cpumask_subset(xcpus, parent->effective_xcpus)) {
			struct cgroup_subsys_state *css;
			struct cpuset *child;
			bool exclusive = true;

			/*
			 * Convert invalid partition to valid has to
			 * pass the cpu exclusivity test.
			 */
			rcu_read_lock();
			cpuset_for_each_child(child, css, parent) {
				if (child == cs)
					continue;
				if (!cpusets_are_exclusive(cs, child)) {
					exclusive = false;
					break;
				}
			}
			rcu_read_unlock();
			if (exclusive)
				deleting = cpumask_and(tmp->delmask,
						xcpus, parent->effective_cpus);
			else
				part_error = PERR_NOTEXCL;
		}
	}

write_error:
	if (part_error)
		WRITE_ONCE(cs->prs_err, part_error);

	if (cmd == partcmd_update) {
		/*
		 * Check for possible transition between valid and invalid
		 * partition root.
		 */
		switch (cs->partition_root_state) {
		case PRS_ROOT:
		case PRS_ISOLATED:
			if (part_error) {
				new_prs = -old_prs;
				subparts_delta--;
			}
			break;
		case PRS_INVALID_ROOT:
		case PRS_INVALID_ISOLATED:
			if (!part_error) {
				new_prs = -old_prs;
				subparts_delta++;
			}
			break;
		}
	}

	if (!adding && !deleting && (new_prs == old_prs))
		return 0;

	/*
	 * Transitioning between invalid to valid or vice versa may require
	 * changing CS_CPU_EXCLUSIVE. In the case of partcmd_update,
	 * validate_change() has already been successfully called and
	 * CPU lists in cs haven't been updated yet. So defer it to later.
	 */
	if ((old_prs != new_prs) && (cmd != partcmd_update))  {
		int err = update_partition_exclusive(cs, new_prs);

		if (err)
			return err;
	}

	/*
	 * Change the parent's effective_cpus & effective_xcpus (top cpuset
	 * only).
	 *
	 * Newly added CPUs will be removed from effective_cpus and
	 * newly deleted ones will be added back to effective_cpus.
	 */
	spin_lock_irq(&callback_lock);
	if (old_prs != new_prs) {
		cs->partition_root_state = new_prs;
		if (new_prs <= 0)
			cs->nr_subparts = 0;
	}
	/*
	 * Adding to parent's effective_cpus means deletion CPUs from cs
	 * and vice versa.
	 */
	if (adding)
		isolcpus_updated += partition_xcpus_del(old_prs, parent,
							tmp->addmask);
	if (deleting)
		isolcpus_updated += partition_xcpus_add(new_prs, parent,
							tmp->delmask);

	if (is_partition_valid(parent)) {
		parent->nr_subparts += subparts_delta;
		WARN_ON_ONCE(parent->nr_subparts < 0);
	}
	spin_unlock_irq(&callback_lock);
	update_unbound_workqueue_cpumask(isolcpus_updated);

	if ((old_prs != new_prs) && (cmd == partcmd_update))
		update_partition_exclusive(cs, new_prs);

	if (adding || deleting) {
		update_tasks_cpumask(parent, tmp->addmask);
		update_sibling_cpumasks(parent, cs, tmp);
	}

	/*
	 * For partcmd_update without newmask, it is being called from
	 * cpuset_handle_hotplug(). Update the load balance flag and
	 * scheduling domain accordingly.
	 */
	if ((cmd == partcmd_update) && !newmask)
		update_partition_sd_lb(cs, old_prs);

	notify_partition_change(cs, old_prs);
	return 0;
}

/**
 * compute_partition_effective_cpumask - compute effective_cpus for partition
 * @cs: partition root cpuset
 * @new_ecpus: previously computed effective_cpus to be updated
 *
 * Compute the effective_cpus of a partition root by scanning effective_xcpus
 * of child partition roots and excluding their effective_xcpus.
 *
 * This has the side effect of invalidating valid child partition roots,
 * if necessary. Since it is called from either cpuset_hotplug_update_tasks()
 * or update_cpumasks_hier() where parent and children are modified
 * successively, we don't need to call update_parent_effective_cpumask()
 * and the child's effective_cpus will be updated in later iterations.
 *
 * Note that rcu_read_lock() is assumed to be held.
 */
static void compute_partition_effective_cpumask(struct cpuset *cs,
						struct cpumask *new_ecpus)
{
	struct cgroup_subsys_state *css;
	struct cpuset *child;
	bool populated = partition_is_populated(cs, NULL);

	/*
	 * Check child partition roots to see if they should be
	 * invalidated when
	 *  1) child effective_xcpus not a subset of new
	 *     excluisve_cpus
	 *  2) All the effective_cpus will be used up and cp
	 *     has tasks
	 */
	compute_effective_exclusive_cpumask(cs, new_ecpus);
	cpumask_and(new_ecpus, new_ecpus, cpu_active_mask);

	rcu_read_lock();
	cpuset_for_each_child(child, css, cs) {
		if (!is_partition_valid(child))
			continue;

		child->prs_err = 0;
		if (!cpumask_subset(child->effective_xcpus,
				    cs->effective_xcpus))
			child->prs_err = PERR_INVCPUS;
		else if (populated &&
			 cpumask_subset(new_ecpus, child->effective_xcpus))
			child->prs_err = PERR_NOCPUS;

		if (child->prs_err) {
			int old_prs = child->partition_root_state;

			/*
			 * Invalidate child partition
			 */
			spin_lock_irq(&callback_lock);
			make_partition_invalid(child);
			cs->nr_subparts--;
			child->nr_subparts = 0;
			spin_unlock_irq(&callback_lock);
			notify_partition_change(child, old_prs);
			continue;
		}
		cpumask_andnot(new_ecpus, new_ecpus,
			       child->effective_xcpus);
	}
	rcu_read_unlock();
}

/*
 * update_cpumasks_hier() flags
 */
#define HIER_CHECKALL		0x01	/* Check all cpusets with no skipping */
#define HIER_NO_SD_REBUILD	0x02	/* Don't rebuild sched domains */

/*
 * update_cpumasks_hier - Update effective cpumasks and tasks in the subtree
 * @cs:  the cpuset to consider
 * @tmp: temp variables for calculating effective_cpus & partition setup
 * @force: don't skip any descendant cpusets if set
 *
 * When configured cpumask is changed, the effective cpumasks of this cpuset
 * and all its descendants need to be updated.
 *
 * On legacy hierarchy, effective_cpus will be the same with cpu_allowed.
 *
 * Called with cpuset_mutex held
 */
static void update_cpumasks_hier(struct cpuset *cs, struct tmpmasks *tmp,
				 int flags)
{
	struct cpuset *cp;
	struct cgroup_subsys_state *pos_css;
	bool need_rebuild_sched_domains = false;
	int old_prs, new_prs;

	rcu_read_lock();
	cpuset_for_each_descendant_pre(cp, pos_css, cs) {
		struct cpuset *parent = parent_cs(cp);
		bool remote = is_remote_partition(cp);
		bool update_parent = false;

		/*
		 * Skip descendent remote partition that acquires CPUs
		 * directly from top cpuset unless it is cs.
		 */
		if (remote && (cp != cs)) {
			pos_css = css_rightmost_descendant(pos_css);
			continue;
		}

		/*
		 * Update effective_xcpus if exclusive_cpus set.
		 * The case when exclusive_cpus isn't set is handled later.
		 */
		if (!cpumask_empty(cp->exclusive_cpus) && (cp != cs)) {
			spin_lock_irq(&callback_lock);
			compute_effective_exclusive_cpumask(cp, NULL);
			spin_unlock_irq(&callback_lock);
		}

		old_prs = new_prs = cp->partition_root_state;
		if (remote || (is_partition_valid(parent) &&
			       is_partition_valid(cp)))
			compute_partition_effective_cpumask(cp, tmp->new_cpus);
		else
			compute_effective_cpumask(tmp->new_cpus, cp, parent);

		/*
		 * A partition with no effective_cpus is allowed as long as
		 * there is no task associated with it. Call
		 * update_parent_effective_cpumask() to check it.
		 */
		if (is_partition_valid(cp) && cpumask_empty(tmp->new_cpus)) {
			update_parent = true;
			goto update_parent_effective;
		}

		/*
		 * If it becomes empty, inherit the effective mask of the
		 * parent, which is guaranteed to have some CPUs unless
		 * it is a partition root that has explicitly distributed
		 * out all its CPUs.
		 */
		if (is_in_v2_mode() && !remote && cpumask_empty(tmp->new_cpus)) {
			cpumask_copy(tmp->new_cpus, parent->effective_cpus);
			if (!cp->use_parent_ecpus) {
				cp->use_parent_ecpus = true;
				parent->child_ecpus_count++;
			}
		} else if (cp->use_parent_ecpus) {
			cp->use_parent_ecpus = false;
			WARN_ON_ONCE(!parent->child_ecpus_count);
			parent->child_ecpus_count--;
		}

		if (remote)
			goto get_css;

		/*
		 * Skip the whole subtree if
		 * 1) the cpumask remains the same,
		 * 2) has no partition root state,
		 * 3) HIER_CHECKALL flag not set, and
		 * 4) for v2 load balance state same as its parent.
		 */
		if (!cp->partition_root_state && !(flags & HIER_CHECKALL) &&
		    cpumask_equal(tmp->new_cpus, cp->effective_cpus) &&
		    (!cgroup_subsys_on_dfl(cpuset_cgrp_subsys) ||
		    (is_sched_load_balance(parent) == is_sched_load_balance(cp)))) {
			pos_css = css_rightmost_descendant(pos_css);
			continue;
		}

update_parent_effective:
		/*
		 * update_parent_effective_cpumask() should have been called
		 * for cs already in update_cpumask(). We should also call
		 * update_tasks_cpumask() again for tasks in the parent
		 * cpuset if the parent's effective_cpus changes.
		 */
		if ((cp != cs) && old_prs) {
			switch (parent->partition_root_state) {
			case PRS_ROOT:
			case PRS_ISOLATED:
				update_parent = true;
				break;

			default:
				/*
				 * When parent is not a partition root or is
				 * invalid, child partition roots become
				 * invalid too.
				 */
				if (is_partition_valid(cp))
					new_prs = -cp->partition_root_state;
				WRITE_ONCE(cp->prs_err,
					   is_partition_invalid(parent)
					   ? PERR_INVPARENT : PERR_NOTPART);
				break;
			}
		}
get_css:
		if (!css_tryget_online(&cp->css))
			continue;
		rcu_read_unlock();

		if (update_parent) {
			update_parent_effective_cpumask(cp, partcmd_update, NULL, tmp);
			/*
			 * The cpuset partition_root_state may become
			 * invalid. Capture it.
			 */
			new_prs = cp->partition_root_state;
		}

		spin_lock_irq(&callback_lock);
		cpumask_copy(cp->effective_cpus, tmp->new_cpus);
		cp->partition_root_state = new_prs;
		/*
		 * Make sure effective_xcpus is properly set for a valid
		 * partition root.
		 */
		if ((new_prs > 0) && cpumask_empty(cp->exclusive_cpus))
			cpumask_and(cp->effective_xcpus,
				    cp->cpus_allowed, parent->effective_xcpus);
		else if (new_prs < 0)
			reset_partition_data(cp);
		spin_unlock_irq(&callback_lock);

		notify_partition_change(cp, old_prs);

		WARN_ON(!is_in_v2_mode() &&
			!cpumask_equal(cp->cpus_allowed, cp->effective_cpus));

		update_tasks_cpumask(cp, cp->effective_cpus);

		/*
		 * On default hierarchy, inherit the CS_SCHED_LOAD_BALANCE
		 * from parent if current cpuset isn't a valid partition root
		 * and their load balance states differ.
		 */
		if (cgroup_subsys_on_dfl(cpuset_cgrp_subsys) &&
		    !is_partition_valid(cp) &&
		    (is_sched_load_balance(parent) != is_sched_load_balance(cp))) {
			if (is_sched_load_balance(parent))
				set_bit(CS_SCHED_LOAD_BALANCE, &cp->flags);
			else
				clear_bit(CS_SCHED_LOAD_BALANCE, &cp->flags);
		}

		/*
		 * On legacy hierarchy, if the effective cpumask of any non-
		 * empty cpuset is changed, we need to rebuild sched domains.
		 * On default hierarchy, the cpuset needs to be a partition
		 * root as well.
		 */
		if (!cpumask_empty(cp->cpus_allowed) &&
		    is_sched_load_balance(cp) &&
		   (!cgroup_subsys_on_dfl(cpuset_cgrp_subsys) ||
		    is_partition_valid(cp)))
			need_rebuild_sched_domains = true;

		rcu_read_lock();
		css_put(&cp->css);
	}
	rcu_read_unlock();

	if (need_rebuild_sched_domains && !(flags & HIER_NO_SD_REBUILD))
		rebuild_sched_domains_locked();
}

/**
 * update_sibling_cpumasks - Update siblings cpumasks
 * @parent:  Parent cpuset
 * @cs:      Current cpuset
 * @tmp:     Temp variables
 */
static void update_sibling_cpumasks(struct cpuset *parent, struct cpuset *cs,
				    struct tmpmasks *tmp)
{
	struct cpuset *sibling;
	struct cgroup_subsys_state *pos_css;

	lockdep_assert_held(&cpuset_mutex);

	/*
	 * Check all its siblings and call update_cpumasks_hier()
	 * if their effective_cpus will need to be changed.
	 *
	 * With the addition of effective_xcpus which is a subset of
	 * cpus_allowed. It is possible a change in parent's effective_cpus
	 * due to a change in a child partition's effective_xcpus will impact
	 * its siblings even if they do not inherit parent's effective_cpus
	 * directly.
	 *
	 * The update_cpumasks_hier() function may sleep. So we have to
	 * release the RCU read lock before calling it. HIER_NO_SD_REBUILD
	 * flag is used to suppress rebuild of sched domains as the callers
	 * will take care of that.
	 */
	rcu_read_lock();
	cpuset_for_each_child(sibling, pos_css, parent) {
		if (sibling == cs)
			continue;
		if (!sibling->use_parent_ecpus &&
		    !is_partition_valid(sibling)) {
			compute_effective_cpumask(tmp->new_cpus, sibling,
						  parent);
			if (cpumask_equal(tmp->new_cpus, sibling->effective_cpus))
				continue;
		}
		if (!css_tryget_online(&sibling->css))
			continue;

		rcu_read_unlock();
		update_cpumasks_hier(sibling, tmp, HIER_NO_SD_REBUILD);
		rcu_read_lock();
		css_put(&sibling->css);
	}
	rcu_read_unlock();
}

/**
 * update_cpumask - update the cpus_allowed mask of a cpuset and all tasks in it
 * @cs: the cpuset to consider
 * @trialcs: trial cpuset
 * @buf: buffer of cpu numbers written to this cpuset
 */
static int update_cpumask(struct cpuset *cs, struct cpuset *trialcs,
			  const char *buf)
{
	int retval;
	struct tmpmasks tmp;
	struct cpuset *parent = parent_cs(cs);
	bool invalidate = false;
	int hier_flags = 0;
	int old_prs = cs->partition_root_state;

	/* top_cpuset.cpus_allowed tracks cpu_online_mask; it's read-only */
	if (cs == &top_cpuset)
		return -EACCES;

	/*
	 * An empty cpus_allowed is ok only if the cpuset has no tasks.
	 * Since cpulist_parse() fails on an empty mask, we special case
	 * that parsing.  The validate_change() call ensures that cpusets
	 * with tasks have cpus.
	 */
	if (!*buf) {
		cpumask_clear(trialcs->cpus_allowed);
		cpumask_clear(trialcs->effective_xcpus);
	} else {
		retval = cpulist_parse(buf, trialcs->cpus_allowed);
		if (retval < 0)
			return retval;

		if (!cpumask_subset(trialcs->cpus_allowed,
				    top_cpuset.cpus_allowed))
			return -EINVAL;

		/*
		 * When exclusive_cpus isn't explicitly set, it is constrainted
		 * by cpus_allowed and parent's effective_xcpus. Otherwise,
		 * trialcs->effective_xcpus is used as a temporary cpumask
		 * for checking validity of the partition root.
		 */
		if (!cpumask_empty(trialcs->exclusive_cpus) || is_partition_valid(cs))
			compute_effective_exclusive_cpumask(trialcs, NULL);
	}

	/* Nothing to do if the cpus didn't change */
	if (cpumask_equal(cs->cpus_allowed, trialcs->cpus_allowed))
		return 0;

	if (alloc_cpumasks(NULL, &tmp))
		return -ENOMEM;

	if (old_prs) {
		if (is_partition_valid(cs) &&
		    cpumask_empty(trialcs->effective_xcpus)) {
			invalidate = true;
			cs->prs_err = PERR_INVCPUS;
		} else if (prstate_housekeeping_conflict(old_prs, trialcs->effective_xcpus)) {
			invalidate = true;
			cs->prs_err = PERR_HKEEPING;
		} else if (tasks_nocpu_error(parent, cs, trialcs->effective_xcpus)) {
			invalidate = true;
			cs->prs_err = PERR_NOCPUS;
		}
	}

	/*
	 * Check all the descendants in update_cpumasks_hier() if
	 * effective_xcpus is to be changed.
	 */
	if (!cpumask_equal(cs->effective_xcpus, trialcs->effective_xcpus))
		hier_flags = HIER_CHECKALL;

	retval = validate_change(cs, trialcs);

	if ((retval == -EINVAL) && cgroup_subsys_on_dfl(cpuset_cgrp_subsys)) {
		struct cgroup_subsys_state *css;
		struct cpuset *cp;

		/*
		 * The -EINVAL error code indicates that partition sibling
		 * CPU exclusivity rule has been violated. We still allow
		 * the cpumask change to proceed while invalidating the
		 * partition. However, any conflicting sibling partitions
		 * have to be marked as invalid too.
		 */
		invalidate = true;
		rcu_read_lock();
		cpuset_for_each_child(cp, css, parent) {
			struct cpumask *xcpus = fetch_xcpus(trialcs);

			if (is_partition_valid(cp) &&
			    cpumask_intersects(xcpus, cp->effective_xcpus)) {
				rcu_read_unlock();
				update_parent_effective_cpumask(cp, partcmd_invalidate, NULL, &tmp);
				rcu_read_lock();
			}
		}
		rcu_read_unlock();
		retval = 0;
	}

	if (retval < 0)
		goto out_free;

	if (is_partition_valid(cs) ||
	   (is_partition_invalid(cs) && !invalidate)) {
		struct cpumask *xcpus = trialcs->effective_xcpus;

		if (cpumask_empty(xcpus) && is_partition_invalid(cs))
			xcpus = trialcs->cpus_allowed;

		/*
		 * Call remote_cpus_update() to handle valid remote partition
		 */
		if (is_remote_partition(cs))
			remote_cpus_update(cs, xcpus, &tmp);
		else if (invalidate)
			update_parent_effective_cpumask(cs, partcmd_invalidate,
							NULL, &tmp);
		else
			update_parent_effective_cpumask(cs, partcmd_update,
							xcpus, &tmp);
	} else if (!cpumask_empty(cs->exclusive_cpus)) {
		/*
		 * Use trialcs->effective_cpus as a temp cpumask
		 */
		remote_partition_check(cs, trialcs->effective_xcpus,
				       trialcs->effective_cpus, &tmp);
	}

	spin_lock_irq(&callback_lock);
	cpumask_copy(cs->cpus_allowed, trialcs->cpus_allowed);
	cpumask_copy(cs->effective_xcpus, trialcs->effective_xcpus);
	if ((old_prs > 0) && !is_partition_valid(cs))
		reset_partition_data(cs);
	spin_unlock_irq(&callback_lock);

	/* effective_cpus/effective_xcpus will be updated here */
	update_cpumasks_hier(cs, &tmp, hier_flags);

	/* Update CS_SCHED_LOAD_BALANCE and/or sched_domains, if necessary */
	if (cs->partition_root_state)
		update_partition_sd_lb(cs, old_prs);
out_free:
	free_cpumasks(NULL, &tmp);
	return retval;
}

/**
 * update_exclusive_cpumask - update the exclusive_cpus mask of a cpuset
 * @cs: the cpuset to consider
 * @trialcs: trial cpuset
 * @buf: buffer of cpu numbers written to this cpuset
 *
 * The tasks' cpumask will be updated if cs is a valid partition root.
 */
static int update_exclusive_cpumask(struct cpuset *cs, struct cpuset *trialcs,
				    const char *buf)
{
	int retval;
	struct tmpmasks tmp;
	struct cpuset *parent = parent_cs(cs);
	bool invalidate = false;
	int hier_flags = 0;
	int old_prs = cs->partition_root_state;

	if (!*buf) {
		cpumask_clear(trialcs->exclusive_cpus);
		cpumask_clear(trialcs->effective_xcpus);
	} else {
		retval = cpulist_parse(buf, trialcs->exclusive_cpus);
		if (retval < 0)
			return retval;
		if (!is_cpu_exclusive(cs))
			set_bit(CS_CPU_EXCLUSIVE, &trialcs->flags);
	}

	/* Nothing to do if the CPUs didn't change */
	if (cpumask_equal(cs->exclusive_cpus, trialcs->exclusive_cpus))
		return 0;

	if (*buf)
		compute_effective_exclusive_cpumask(trialcs, NULL);

	/*
	 * Check all the descendants in update_cpumasks_hier() if
	 * effective_xcpus is to be changed.
	 */
	if (!cpumask_equal(cs->effective_xcpus, trialcs->effective_xcpus))
		hier_flags = HIER_CHECKALL;

	retval = validate_change(cs, trialcs);
	if (retval)
		return retval;

	if (alloc_cpumasks(NULL, &tmp))
		return -ENOMEM;

	if (old_prs) {
		if (cpumask_empty(trialcs->effective_xcpus)) {
			invalidate = true;
			cs->prs_err = PERR_INVCPUS;
		} else if (prstate_housekeeping_conflict(old_prs, trialcs->effective_xcpus)) {
			invalidate = true;
			cs->prs_err = PERR_HKEEPING;
		} else if (tasks_nocpu_error(parent, cs, trialcs->effective_xcpus)) {
			invalidate = true;
			cs->prs_err = PERR_NOCPUS;
		}

		if (is_remote_partition(cs)) {
			if (invalidate)
				remote_partition_disable(cs, &tmp);
			else
				remote_cpus_update(cs, trialcs->effective_xcpus,
						   &tmp);
		} else if (invalidate) {
			update_parent_effective_cpumask(cs, partcmd_invalidate,
							NULL, &tmp);
		} else {
			update_parent_effective_cpumask(cs, partcmd_update,
						trialcs->effective_xcpus, &tmp);
		}
	} else if (!cpumask_empty(trialcs->exclusive_cpus)) {
		/*
		 * Use trialcs->effective_cpus as a temp cpumask
		 */
		remote_partition_check(cs, trialcs->effective_xcpus,
				       trialcs->effective_cpus, &tmp);
	}
	spin_lock_irq(&callback_lock);
	cpumask_copy(cs->exclusive_cpus, trialcs->exclusive_cpus);
	cpumask_copy(cs->effective_xcpus, trialcs->effective_xcpus);
	if ((old_prs > 0) && !is_partition_valid(cs))
		reset_partition_data(cs);
	spin_unlock_irq(&callback_lock);

	/*
	 * Call update_cpumasks_hier() to update effective_cpus/effective_xcpus
	 * of the subtree when it is a valid partition root or effective_xcpus
	 * is updated.
	 */
	if (is_partition_valid(cs) || hier_flags)
		update_cpumasks_hier(cs, &tmp, hier_flags);

	/* Update CS_SCHED_LOAD_BALANCE and/or sched_domains, if necessary */
	if (cs->partition_root_state)
		update_partition_sd_lb(cs, old_prs);

	free_cpumasks(NULL, &tmp);
	return 0;
}

/*
 * Migrate memory region from one set of nodes to another.  This is
 * performed asynchronously as it can be called from process migration path
 * holding locks involved in process management.  All mm migrations are
 * performed in the queued order and can be waited for by flushing
 * cpuset_migrate_mm_wq.
 */

struct cpuset_migrate_mm_work {
	struct work_struct	work;
	struct mm_struct	*mm;
	nodemask_t		from;
	nodemask_t		to;
};

static void cpuset_migrate_mm_workfn(struct work_struct *work)
{
	struct cpuset_migrate_mm_work *mwork =
		container_of(work, struct cpuset_migrate_mm_work, work);

	/* on a wq worker, no need to worry about %current's mems_allowed */
	do_migrate_pages(mwork->mm, &mwork->from, &mwork->to, MPOL_MF_MOVE_ALL);
	mmput(mwork->mm);
	kfree(mwork);
}

static void cpuset_migrate_mm(struct mm_struct *mm, const nodemask_t *from,
							const nodemask_t *to)
{
	struct cpuset_migrate_mm_work *mwork;

	if (nodes_equal(*from, *to)) {
		mmput(mm);
		return;
	}

	mwork = kzalloc(sizeof(*mwork), GFP_KERNEL);
	if (mwork) {
		mwork->mm = mm;
		mwork->from = *from;
		mwork->to = *to;
		INIT_WORK(&mwork->work, cpuset_migrate_mm_workfn);
		queue_work(cpuset_migrate_mm_wq, &mwork->work);
	} else {
		mmput(mm);
	}
}

static void cpuset_post_attach(void)
{
	flush_workqueue(cpuset_migrate_mm_wq);
}

/*
 * cpuset_change_task_nodemask - change task's mems_allowed and mempolicy
 * @tsk: the task to change
 * @newmems: new nodes that the task will be set
 *
 * We use the mems_allowed_seq seqlock to safely update both tsk->mems_allowed
 * and rebind an eventual tasks' mempolicy. If the task is allocating in
 * parallel, it might temporarily see an empty intersection, which results in
 * a seqlock check and retry before OOM or allocation failure.
 */
static void cpuset_change_task_nodemask(struct task_struct *tsk,
					nodemask_t *newmems)
{
	task_lock(tsk);

	local_irq_disable();
	write_seqcount_begin(&tsk->mems_allowed_seq);

	nodes_or(tsk->mems_allowed, tsk->mems_allowed, *newmems);
	mpol_rebind_task(tsk, newmems);
	tsk->mems_allowed = *newmems;

	write_seqcount_end(&tsk->mems_allowed_seq);
	local_irq_enable();

	task_unlock(tsk);
}

static void *cpuset_being_rebound;

/**
 * update_tasks_nodemask - Update the nodemasks of tasks in the cpuset.
 * @cs: the cpuset in which each task's mems_allowed mask needs to be changed
 *
 * Iterate through each task of @cs updating its mems_allowed to the
 * effective cpuset's.  As this function is called with cpuset_mutex held,
 * cpuset membership stays stable.
 */
static void update_tasks_nodemask(struct cpuset *cs)
{
	static nodemask_t newmems;	/* protected by cpuset_mutex */
	struct css_task_iter it;
	struct task_struct *task;

	cpuset_being_rebound = cs;		/* causes mpol_dup() rebind */

	guarantee_online_mems(cs, &newmems);

	/*
	 * The mpol_rebind_mm() call takes mmap_lock, which we couldn't
	 * take while holding tasklist_lock.  Forks can happen - the
	 * mpol_dup() cpuset_being_rebound check will catch such forks,
	 * and rebind their vma mempolicies too.  Because we still hold
	 * the global cpuset_mutex, we know that no other rebind effort
	 * will be contending for the global variable cpuset_being_rebound.
	 * It's ok if we rebind the same mm twice; mpol_rebind_mm()
	 * is idempotent.  Also migrate pages in each mm to new nodes.
	 */
	css_task_iter_start(&cs->css, 0, &it);
	while ((task = css_task_iter_next(&it))) {
		struct mm_struct *mm;
		bool migrate;

		cpuset_change_task_nodemask(task, &newmems);

		mm = get_task_mm(task);
		if (!mm)
			continue;

		migrate = is_memory_migrate(cs);

		mpol_rebind_mm(mm, &cs->mems_allowed);
		if (migrate)
			cpuset_migrate_mm(mm, &cs->old_mems_allowed, &newmems);
		else
			mmput(mm);
	}
	css_task_iter_end(&it);

	/*
	 * All the tasks' nodemasks have been updated, update
	 * cs->old_mems_allowed.
	 */
	cs->old_mems_allowed = newmems;

	/* We're done rebinding vmas to this cpuset's new mems_allowed. */
	cpuset_being_rebound = NULL;
}

/*
 * update_nodemasks_hier - Update effective nodemasks and tasks in the subtree
 * @cs: the cpuset to consider
 * @new_mems: a temp variable for calculating new effective_mems
 *
 * When configured nodemask is changed, the effective nodemasks of this cpuset
 * and all its descendants need to be updated.
 *
 * On legacy hierarchy, effective_mems will be the same with mems_allowed.
 *
 * Called with cpuset_mutex held
 */
static void update_nodemasks_hier(struct cpuset *cs, nodemask_t *new_mems)
{
	struct cpuset *cp;
	struct cgroup_subsys_state *pos_css;

	rcu_read_lock();
	cpuset_for_each_descendant_pre(cp, pos_css, cs) {
		struct cpuset *parent = parent_cs(cp);

		nodes_and(*new_mems, cp->mems_allowed, parent->effective_mems);

		/*
		 * If it becomes empty, inherit the effective mask of the
		 * parent, which is guaranteed to have some MEMs.
		 */
		if (is_in_v2_mode() && nodes_empty(*new_mems))
			*new_mems = parent->effective_mems;

		/* Skip the whole subtree if the nodemask remains the same. */
		if (nodes_equal(*new_mems, cp->effective_mems)) {
			pos_css = css_rightmost_descendant(pos_css);
			continue;
		}

		if (!css_tryget_online(&cp->css))
			continue;
		rcu_read_unlock();

		spin_lock_irq(&callback_lock);
		cp->effective_mems = *new_mems;
		spin_unlock_irq(&callback_lock);

		WARN_ON(!is_in_v2_mode() &&
			!nodes_equal(cp->mems_allowed, cp->effective_mems));

		update_tasks_nodemask(cp);

		rcu_read_lock();
		css_put(&cp->css);
	}
	rcu_read_unlock();
}

/*
 * Handle user request to change the 'mems' memory placement
 * of a cpuset.  Needs to validate the request, update the
 * cpusets mems_allowed, and for each task in the cpuset,
 * update mems_allowed and rebind task's mempolicy and any vma
 * mempolicies and if the cpuset is marked 'memory_migrate',
 * migrate the tasks pages to the new memory.
 *
 * Call with cpuset_mutex held. May take callback_lock during call.
 * Will take tasklist_lock, scan tasklist for tasks in cpuset cs,
 * lock each such tasks mm->mmap_lock, scan its vma's and rebind
 * their mempolicies to the cpusets new mems_allowed.
 */
static int update_nodemask(struct cpuset *cs, struct cpuset *trialcs,
			   const char *buf)
{
	int retval;

	/*
	 * top_cpuset.mems_allowed tracks node_stats[N_MEMORY];
	 * it's read-only
	 */
	if (cs == &top_cpuset) {
		retval = -EACCES;
		goto done;
	}

	/*
	 * An empty mems_allowed is ok iff there are no tasks in the cpuset.
	 * Since nodelist_parse() fails on an empty mask, we special case
	 * that parsing.  The validate_change() call ensures that cpusets
	 * with tasks have memory.
	 */
	if (!*buf) {
		nodes_clear(trialcs->mems_allowed);
	} else {
		retval = nodelist_parse(buf, trialcs->mems_allowed);
		if (retval < 0)
			goto done;

		if (!nodes_subset(trialcs->mems_allowed,
				  top_cpuset.mems_allowed)) {
			retval = -EINVAL;
			goto done;
		}
	}

	if (nodes_equal(cs->mems_allowed, trialcs->mems_allowed)) {
		retval = 0;		/* Too easy - nothing to do */
		goto done;
	}
	retval = validate_change(cs, trialcs);
	if (retval < 0)
		goto done;

	check_insane_mems_config(&trialcs->mems_allowed);

	spin_lock_irq(&callback_lock);
	cs->mems_allowed = trialcs->mems_allowed;
	spin_unlock_irq(&callback_lock);

	/* use trialcs->mems_allowed as a temp variable */
	update_nodemasks_hier(cs, &trialcs->mems_allowed);
done:
	return retval;
}

bool current_cpuset_is_being_rebound(void)
{
	bool ret;

	rcu_read_lock();
	ret = task_cs(current) == cpuset_being_rebound;
	rcu_read_unlock();

	return ret;
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

/**
 * update_tasks_flags - update the spread flags of tasks in the cpuset.
 * @cs: the cpuset in which each task's spread flags needs to be changed
 *
 * Iterate through each task of @cs updating its spread flags.  As this
 * function is called with cpuset_mutex held, cpuset membership stays
 * stable.
 */
static void update_tasks_flags(struct cpuset *cs)
{
	struct css_task_iter it;
	struct task_struct *task;

	css_task_iter_start(&cs->css, 0, &it);
	while ((task = css_task_iter_next(&it)))
		cpuset_update_task_spread_flags(cs, task);
	css_task_iter_end(&it);
}

/*
 * update_flag - read a 0 or a 1 in a file and update associated flag
 * bit:		the bit to update (see cpuset_flagbits_t)
 * cs:		the cpuset to update
 * turning_on: 	whether the flag is being set or cleared
 *
 * Call with cpuset_mutex held.
 */

static int update_flag(cpuset_flagbits_t bit, struct cpuset *cs,
		       int turning_on)
{
	struct cpuset *trialcs;
	int balance_flag_changed;
	int spread_flag_changed;
	int err;

	trialcs = alloc_trial_cpuset(cs);
	if (!trialcs)
		return -ENOMEM;

	if (turning_on)
		set_bit(bit, &trialcs->flags);
	else
		clear_bit(bit, &trialcs->flags);

	err = validate_change(cs, trialcs);
	if (err < 0)
		goto out;

	balance_flag_changed = (is_sched_load_balance(cs) !=
				is_sched_load_balance(trialcs));

	spread_flag_changed = ((is_spread_slab(cs) != is_spread_slab(trialcs))
			|| (is_spread_page(cs) != is_spread_page(trialcs)));

	spin_lock_irq(&callback_lock);
	cs->flags = trialcs->flags;
	spin_unlock_irq(&callback_lock);

	if (!cpumask_empty(trialcs->cpus_allowed) && balance_flag_changed)
		rebuild_sched_domains_locked();

	if (spread_flag_changed)
		update_tasks_flags(cs);
out:
	free_cpuset(trialcs);
	return err;
}

/**
 * update_prstate - update partition_root_state
 * @cs: the cpuset to update
 * @new_prs: new partition root state
 * Return: 0 if successful, != 0 if error
 *
 * Call with cpuset_mutex held.
 */
static int update_prstate(struct cpuset *cs, int new_prs)
{
	int err = PERR_NONE, old_prs = cs->partition_root_state;
	struct cpuset *parent = parent_cs(cs);
	struct tmpmasks tmpmask;
	bool new_xcpus_state = false;

	if (old_prs == new_prs)
		return 0;

	/*
	 * Treat a previously invalid partition root as if it is a "member".
	 */
	if (new_prs && is_prs_invalid(old_prs))
		old_prs = PRS_MEMBER;

	if (alloc_cpumasks(NULL, &tmpmask))
		return -ENOMEM;

	/*
	 * Setup effective_xcpus if not properly set yet, it will be cleared
	 * later if partition becomes invalid.
	 */
	if ((new_prs > 0) && cpumask_empty(cs->exclusive_cpus)) {
		spin_lock_irq(&callback_lock);
		cpumask_and(cs->effective_xcpus,
			    cs->cpus_allowed, parent->effective_xcpus);
		spin_unlock_irq(&callback_lock);
	}

	err = update_partition_exclusive(cs, new_prs);
	if (err)
		goto out;

	if (!old_prs) {
		enum partition_cmd cmd = (new_prs == PRS_ROOT)
				       ? partcmd_enable : partcmd_enablei;

		/*
		 * cpus_allowed cannot be empty.
		 */
		if (cpumask_empty(cs->cpus_allowed)) {
			err = PERR_CPUSEMPTY;
			goto out;
		}

		err = update_parent_effective_cpumask(cs, cmd, NULL, &tmpmask);
		/*
		 * If an attempt to become local partition root fails,
		 * try to become a remote partition root instead.
		 */
		if (err && remote_partition_enable(cs, new_prs, &tmpmask))
			err = 0;
	} else if (old_prs && new_prs) {
		/*
		 * A change in load balance state only, no change in cpumasks.
		 */
		new_xcpus_state = true;
	} else {
		/*
		 * Switching back to member is always allowed even if it
		 * disables child partitions.
		 */
		if (is_remote_partition(cs))
			remote_partition_disable(cs, &tmpmask);
		else
			update_parent_effective_cpumask(cs, partcmd_disable,
							NULL, &tmpmask);

		/*
		 * Invalidation of child partitions will be done in
		 * update_cpumasks_hier().
		 */
	}
out:
	/*
	 * Make partition invalid & disable CS_CPU_EXCLUSIVE if an error
	 * happens.
	 */
	if (err) {
		new_prs = -new_prs;
		update_partition_exclusive(cs, new_prs);
	}

	spin_lock_irq(&callback_lock);
	cs->partition_root_state = new_prs;
	WRITE_ONCE(cs->prs_err, err);
	if (!is_partition_valid(cs))
		reset_partition_data(cs);
	else if (new_xcpus_state)
		partition_xcpus_newstate(old_prs, new_prs, cs->effective_xcpus);
	spin_unlock_irq(&callback_lock);
	update_unbound_workqueue_cpumask(new_xcpus_state);

	/* Force update if switching back to member */
	update_cpumasks_hier(cs, &tmpmask, !new_prs ? HIER_CHECKALL : 0);

	/* Update sched domains and load balance flag */
	update_partition_sd_lb(cs, old_prs);

	notify_partition_change(cs, old_prs);
	free_cpumasks(NULL, &tmpmask);
	return 0;
}

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
static void fmeter_init(struct fmeter *fmp)
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

static struct cpuset *cpuset_attach_old_cs;

/*
 * Check to see if a cpuset can accept a new task
 * For v1, cpus_allowed and mems_allowed can't be empty.
 * For v2, effective_cpus can't be empty.
 * Note that in v1, effective_cpus = cpus_allowed.
 */
static int cpuset_can_attach_check(struct cpuset *cs)
{
	if (cpumask_empty(cs->effective_cpus) ||
	   (!is_in_v2_mode() && nodes_empty(cs->mems_allowed)))
		return -ENOSPC;
	return 0;
}

static void reset_migrate_dl_data(struct cpuset *cs)
{
	cs->nr_migrate_dl_tasks = 0;
	cs->sum_migrate_dl_bw = 0;
}

/* Called by cgroups to determine if a cpuset is usable; cpuset_mutex held */
static int cpuset_can_attach(struct cgroup_taskset *tset)
{
	struct cgroup_subsys_state *css;
	struct cpuset *cs, *oldcs;
	struct task_struct *task;
	bool cpus_updated, mems_updated;
	int ret;

	/* used later by cpuset_attach() */
	cpuset_attach_old_cs = task_cs(cgroup_taskset_first(tset, &css));
	oldcs = cpuset_attach_old_cs;
	cs = css_cs(css);

	mutex_lock(&cpuset_mutex);

	/* Check to see if task is allowed in the cpuset */
	ret = cpuset_can_attach_check(cs);
	if (ret)
		goto out_unlock;

	cpus_updated = !cpumask_equal(cs->effective_cpus, oldcs->effective_cpus);
	mems_updated = !nodes_equal(cs->effective_mems, oldcs->effective_mems);

	cgroup_taskset_for_each(task, css, tset) {
		ret = task_can_attach(task);
		if (ret)
			goto out_unlock;

		/*
		 * Skip rights over task check in v2 when nothing changes,
		 * migration permission derives from hierarchy ownership in
		 * cgroup_procs_write_permission()).
		 */
		if (!cgroup_subsys_on_dfl(cpuset_cgrp_subsys) ||
		    (cpus_updated || mems_updated)) {
			ret = security_task_setscheduler(task);
			if (ret)
				goto out_unlock;
		}

		if (dl_task(task)) {
			cs->nr_migrate_dl_tasks++;
			cs->sum_migrate_dl_bw += task->dl.dl_bw;
		}
	}

	if (!cs->nr_migrate_dl_tasks)
		goto out_success;

	if (!cpumask_intersects(oldcs->effective_cpus, cs->effective_cpus)) {
		int cpu = cpumask_any_and(cpu_active_mask, cs->effective_cpus);

		if (unlikely(cpu >= nr_cpu_ids)) {
			reset_migrate_dl_data(cs);
			ret = -EINVAL;
			goto out_unlock;
		}

		ret = dl_bw_alloc(cpu, cs->sum_migrate_dl_bw);
		if (ret) {
			reset_migrate_dl_data(cs);
			goto out_unlock;
		}
	}

out_success:
	/*
	 * Mark attach is in progress.  This makes validate_change() fail
	 * changes which zero cpus/mems_allowed.
	 */
	cs->attach_in_progress++;
out_unlock:
	mutex_unlock(&cpuset_mutex);
	return ret;
}

static void cpuset_cancel_attach(struct cgroup_taskset *tset)
{
	struct cgroup_subsys_state *css;
	struct cpuset *cs;

	cgroup_taskset_first(tset, &css);
	cs = css_cs(css);

	mutex_lock(&cpuset_mutex);
	cs->attach_in_progress--;
	if (!cs->attach_in_progress)
		wake_up(&cpuset_attach_wq);

	if (cs->nr_migrate_dl_tasks) {
		int cpu = cpumask_any(cs->effective_cpus);

		dl_bw_free(cpu, cs->sum_migrate_dl_bw);
		reset_migrate_dl_data(cs);
	}

	mutex_unlock(&cpuset_mutex);
}

/*
 * Protected by cpuset_mutex. cpus_attach is used only by cpuset_attach_task()
 * but we can't allocate it dynamically there.  Define it global and
 * allocate from cpuset_init().
 */
static cpumask_var_t cpus_attach;
static nodemask_t cpuset_attach_nodemask_to;

static void cpuset_attach_task(struct cpuset *cs, struct task_struct *task)
{
	lockdep_assert_held(&cpuset_mutex);

	if (cs != &top_cpuset)
		guarantee_online_cpus(task, cpus_attach);
	else
		cpumask_andnot(cpus_attach, task_cpu_possible_mask(task),
			       subpartitions_cpus);
	/*
	 * can_attach beforehand should guarantee that this doesn't
	 * fail.  TODO: have a better way to handle failure here
	 */
	WARN_ON_ONCE(set_cpus_allowed_ptr(task, cpus_attach));

	cpuset_change_task_nodemask(task, &cpuset_attach_nodemask_to);
	cpuset_update_task_spread_flags(cs, task);
}

static void cpuset_attach(struct cgroup_taskset *tset)
{
	struct task_struct *task;
	struct task_struct *leader;
	struct cgroup_subsys_state *css;
	struct cpuset *cs;
	struct cpuset *oldcs = cpuset_attach_old_cs;
	bool cpus_updated, mems_updated;

	cgroup_taskset_first(tset, &css);
	cs = css_cs(css);

	lockdep_assert_cpus_held();	/* see cgroup_attach_lock() */
	mutex_lock(&cpuset_mutex);
	cpus_updated = !cpumask_equal(cs->effective_cpus,
				      oldcs->effective_cpus);
	mems_updated = !nodes_equal(cs->effective_mems, oldcs->effective_mems);

	/*
	 * In the default hierarchy, enabling cpuset in the child cgroups
	 * will trigger a number of cpuset_attach() calls with no change
	 * in effective cpus and mems. In that case, we can optimize out
	 * by skipping the task iteration and update.
	 */
	if (cgroup_subsys_on_dfl(cpuset_cgrp_subsys) &&
	    !cpus_updated && !mems_updated) {
		cpuset_attach_nodemask_to = cs->effective_mems;
		goto out;
	}

	guarantee_online_mems(cs, &cpuset_attach_nodemask_to);

	cgroup_taskset_for_each(task, css, tset)
		cpuset_attach_task(cs, task);

	/*
	 * Change mm for all threadgroup leaders. This is expensive and may
	 * sleep and should be moved outside migration path proper. Skip it
	 * if there is no change in effective_mems and CS_MEMORY_MIGRATE is
	 * not set.
	 */
	cpuset_attach_nodemask_to = cs->effective_mems;
	if (!is_memory_migrate(cs) && !mems_updated)
		goto out;

	cgroup_taskset_for_each_leader(leader, css, tset) {
		struct mm_struct *mm = get_task_mm(leader);

		if (mm) {
			mpol_rebind_mm(mm, &cpuset_attach_nodemask_to);

			/*
			 * old_mems_allowed is the same with mems_allowed
			 * here, except if this task is being moved
			 * automatically due to hotplug.  In that case
			 * @mems_allowed has been updated and is empty, so
			 * @old_mems_allowed is the right nodesets that we
			 * migrate mm from.
			 */
			if (is_memory_migrate(cs))
				cpuset_migrate_mm(mm, &oldcs->old_mems_allowed,
						  &cpuset_attach_nodemask_to);
			else
				mmput(mm);
		}
	}

out:
	cs->old_mems_allowed = cpuset_attach_nodemask_to;

	if (cs->nr_migrate_dl_tasks) {
		cs->nr_deadline_tasks += cs->nr_migrate_dl_tasks;
		oldcs->nr_deadline_tasks -= cs->nr_migrate_dl_tasks;
		reset_migrate_dl_data(cs);
	}

	cs->attach_in_progress--;
	if (!cs->attach_in_progress)
		wake_up(&cpuset_attach_wq);

	mutex_unlock(&cpuset_mutex);
}

/* The various types of files and directories in a cpuset file system */

typedef enum {
	FILE_MEMORY_MIGRATE,
	FILE_CPULIST,
	FILE_MEMLIST,
	FILE_EFFECTIVE_CPULIST,
	FILE_EFFECTIVE_MEMLIST,
	FILE_SUBPARTS_CPULIST,
	FILE_EXCLUSIVE_CPULIST,
	FILE_EFFECTIVE_XCPULIST,
	FILE_ISOLATED_CPULIST,
	FILE_CPU_EXCLUSIVE,
	FILE_MEM_EXCLUSIVE,
	FILE_MEM_HARDWALL,
	FILE_SCHED_LOAD_BALANCE,
	FILE_PARTITION_ROOT,
	FILE_SCHED_RELAX_DOMAIN_LEVEL,
	FILE_MEMORY_PRESSURE_ENABLED,
	FILE_MEMORY_PRESSURE,
	FILE_SPREAD_PAGE,
	FILE_SPREAD_SLAB,
} cpuset_filetype_t;

static int cpuset_write_u64(struct cgroup_subsys_state *css, struct cftype *cft,
			    u64 val)
{
	struct cpuset *cs = css_cs(css);
	cpuset_filetype_t type = cft->private;
	int retval = 0;

	cpus_read_lock();
	mutex_lock(&cpuset_mutex);
	if (!is_cpuset_online(cs)) {
		retval = -ENODEV;
		goto out_unlock;
	}

	switch (type) {
	case FILE_CPU_EXCLUSIVE:
		retval = update_flag(CS_CPU_EXCLUSIVE, cs, val);
		break;
	case FILE_MEM_EXCLUSIVE:
		retval = update_flag(CS_MEM_EXCLUSIVE, cs, val);
		break;
	case FILE_MEM_HARDWALL:
		retval = update_flag(CS_MEM_HARDWALL, cs, val);
		break;
	case FILE_SCHED_LOAD_BALANCE:
		retval = update_flag(CS_SCHED_LOAD_BALANCE, cs, val);
		break;
	case FILE_MEMORY_MIGRATE:
		retval = update_flag(CS_MEMORY_MIGRATE, cs, val);
		break;
	case FILE_MEMORY_PRESSURE_ENABLED:
		cpuset_memory_pressure_enabled = !!val;
		break;
	case FILE_SPREAD_PAGE:
		retval = update_flag(CS_SPREAD_PAGE, cs, val);
		break;
	case FILE_SPREAD_SLAB:
		retval = update_flag(CS_SPREAD_SLAB, cs, val);
		break;
	default:
		retval = -EINVAL;
		break;
	}
out_unlock:
	mutex_unlock(&cpuset_mutex);
	cpus_read_unlock();
	return retval;
}

static int cpuset_write_s64(struct cgroup_subsys_state *css, struct cftype *cft,
			    s64 val)
{
	struct cpuset *cs = css_cs(css);
	cpuset_filetype_t type = cft->private;
	int retval = -ENODEV;

	cpus_read_lock();
	mutex_lock(&cpuset_mutex);
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
	mutex_unlock(&cpuset_mutex);
	cpus_read_unlock();
	return retval;
}

/*
 * Common handling for a write to a "cpus" or "mems" file.
 */
static ssize_t cpuset_write_resmask(struct kernfs_open_file *of,
				    char *buf, size_t nbytes, loff_t off)
{
	struct cpuset *cs = css_cs(of_css(of));
	struct cpuset *trialcs;
	int retval = -ENODEV;

	buf = strstrip(buf);

	/*
	 * CPU or memory hotunplug may leave @cs w/o any execution
	 * resources, in which case the hotplug code asynchronously updates
	 * configuration and transfers all tasks to the nearest ancestor
	 * which can execute.
	 *
	 * As writes to "cpus" or "mems" may restore @cs's execution
	 * resources, wait for the previously scheduled operations before
	 * proceeding, so that we don't end up keep removing tasks added
	 * after execution capability is restored.
	 *
	 * cpuset_handle_hotplug may call back into cgroup core asynchronously
	 * via cgroup_transfer_tasks() and waiting for it from a cgroupfs
	 * operation like this one can lead to a deadlock through kernfs
	 * active_ref protection.  Let's break the protection.  Losing the
	 * protection is okay as we check whether @cs is online after
	 * grabbing cpuset_mutex anyway.  This only happens on the legacy
	 * hierarchies.
	 */
	css_get(&cs->css);
	kernfs_break_active_protection(of->kn);

	cpus_read_lock();
	mutex_lock(&cpuset_mutex);
	if (!is_cpuset_online(cs))
		goto out_unlock;

	trialcs = alloc_trial_cpuset(cs);
	if (!trialcs) {
		retval = -ENOMEM;
		goto out_unlock;
	}

	switch (of_cft(of)->private) {
	case FILE_CPULIST:
		retval = update_cpumask(cs, trialcs, buf);
		break;
	case FILE_EXCLUSIVE_CPULIST:
		retval = update_exclusive_cpumask(cs, trialcs, buf);
		break;
	case FILE_MEMLIST:
		retval = update_nodemask(cs, trialcs, buf);
		break;
	default:
		retval = -EINVAL;
		break;
	}

	free_cpuset(trialcs);
out_unlock:
	mutex_unlock(&cpuset_mutex);
	cpus_read_unlock();
	kernfs_unbreak_active_protection(of->kn);
	css_put(&cs->css);
	flush_workqueue(cpuset_migrate_mm_wq);
	return retval ?: nbytes;
}

/*
 * These ascii lists should be read in a single call, by using a user
 * buffer large enough to hold the entire map.  If read in smaller
 * chunks, there is no guarantee of atomicity.  Since the display format
 * used, list of ranges of sequential numbers, is variable length,
 * and since these maps can change value dynamically, one could read
 * gibberish by doing partial reads while a list was changing.
 */
static int cpuset_common_seq_show(struct seq_file *sf, void *v)
{
	struct cpuset *cs = css_cs(seq_css(sf));
	cpuset_filetype_t type = seq_cft(sf)->private;
	int ret = 0;

	spin_lock_irq(&callback_lock);

	switch (type) {
	case FILE_CPULIST:
		seq_printf(sf, "%*pbl\n", cpumask_pr_args(cs->cpus_allowed));
		break;
	case FILE_MEMLIST:
		seq_printf(sf, "%*pbl\n", nodemask_pr_args(&cs->mems_allowed));
		break;
	case FILE_EFFECTIVE_CPULIST:
		seq_printf(sf, "%*pbl\n", cpumask_pr_args(cs->effective_cpus));
		break;
	case FILE_EFFECTIVE_MEMLIST:
		seq_printf(sf, "%*pbl\n", nodemask_pr_args(&cs->effective_mems));
		break;
	case FILE_EXCLUSIVE_CPULIST:
		seq_printf(sf, "%*pbl\n", cpumask_pr_args(cs->exclusive_cpus));
		break;
	case FILE_EFFECTIVE_XCPULIST:
		seq_printf(sf, "%*pbl\n", cpumask_pr_args(cs->effective_xcpus));
		break;
	case FILE_SUBPARTS_CPULIST:
		seq_printf(sf, "%*pbl\n", cpumask_pr_args(subpartitions_cpus));
		break;
	case FILE_ISOLATED_CPULIST:
		seq_printf(sf, "%*pbl\n", cpumask_pr_args(isolated_cpus));
		break;
	default:
		ret = -EINVAL;
	}

	spin_unlock_irq(&callback_lock);
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

static int sched_partition_show(struct seq_file *seq, void *v)
{
	struct cpuset *cs = css_cs(seq_css(seq));
	const char *err, *type = NULL;

	switch (cs->partition_root_state) {
	case PRS_ROOT:
		seq_puts(seq, "root\n");
		break;
	case PRS_ISOLATED:
		seq_puts(seq, "isolated\n");
		break;
	case PRS_MEMBER:
		seq_puts(seq, "member\n");
		break;
	case PRS_INVALID_ROOT:
		type = "root";
		fallthrough;
	case PRS_INVALID_ISOLATED:
		if (!type)
			type = "isolated";
		err = perr_strings[READ_ONCE(cs->prs_err)];
		if (err)
			seq_printf(seq, "%s invalid (%s)\n", type, err);
		else
			seq_printf(seq, "%s invalid\n", type);
		break;
	}
	return 0;
}

static ssize_t sched_partition_write(struct kernfs_open_file *of, char *buf,
				     size_t nbytes, loff_t off)
{
	struct cpuset *cs = css_cs(of_css(of));
	int val;
	int retval = -ENODEV;

	buf = strstrip(buf);

	if (!strcmp(buf, "root"))
		val = PRS_ROOT;
	else if (!strcmp(buf, "member"))
		val = PRS_MEMBER;
	else if (!strcmp(buf, "isolated"))
		val = PRS_ISOLATED;
	else
		return -EINVAL;

	css_get(&cs->css);
	cpus_read_lock();
	mutex_lock(&cpuset_mutex);
	if (!is_cpuset_online(cs))
		goto out_unlock;

	retval = update_prstate(cs, val);
out_unlock:
	mutex_unlock(&cpuset_mutex);
	cpus_read_unlock();
	css_put(&cs->css);
	return retval ?: nbytes;
}

/*
 * for the common functions, 'private' gives the type of file
 */

static struct cftype legacy_files[] = {
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

/*
 * This is currently a minimal set for the default hierarchy. It can be
 * expanded later on by migrating more features and control files from v1.
 */
static struct cftype dfl_files[] = {
	{
		.name = "cpus",
		.seq_show = cpuset_common_seq_show,
		.write = cpuset_write_resmask,
		.max_write_len = (100U + 6 * NR_CPUS),
		.private = FILE_CPULIST,
		.flags = CFTYPE_NOT_ON_ROOT,
	},

	{
		.name = "mems",
		.seq_show = cpuset_common_seq_show,
		.write = cpuset_write_resmask,
		.max_write_len = (100U + 6 * MAX_NUMNODES),
		.private = FILE_MEMLIST,
		.flags = CFTYPE_NOT_ON_ROOT,
	},

	{
		.name = "cpus.effective",
		.seq_show = cpuset_common_seq_show,
		.private = FILE_EFFECTIVE_CPULIST,
	},

	{
		.name = "mems.effective",
		.seq_show = cpuset_common_seq_show,
		.private = FILE_EFFECTIVE_MEMLIST,
	},

	{
		.name = "cpus.partition",
		.seq_show = sched_partition_show,
		.write = sched_partition_write,
		.private = FILE_PARTITION_ROOT,
		.flags = CFTYPE_NOT_ON_ROOT,
		.file_offset = offsetof(struct cpuset, partition_file),
	},

	{
		.name = "cpus.exclusive",
		.seq_show = cpuset_common_seq_show,
		.write = cpuset_write_resmask,
		.max_write_len = (100U + 6 * NR_CPUS),
		.private = FILE_EXCLUSIVE_CPULIST,
		.flags = CFTYPE_NOT_ON_ROOT,
	},

	{
		.name = "cpus.exclusive.effective",
		.seq_show = cpuset_common_seq_show,
		.private = FILE_EFFECTIVE_XCPULIST,
		.flags = CFTYPE_NOT_ON_ROOT,
	},

	{
		.name = "cpus.subpartitions",
		.seq_show = cpuset_common_seq_show,
		.private = FILE_SUBPARTS_CPULIST,
		.flags = CFTYPE_ONLY_ON_ROOT | CFTYPE_DEBUG,
	},

	{
		.name = "cpus.isolated",
		.seq_show = cpuset_common_seq_show,
		.private = FILE_ISOLATED_CPULIST,
		.flags = CFTYPE_ONLY_ON_ROOT,
	},

	{ }	/* terminate */
};


/**
 * cpuset_css_alloc - Allocate a cpuset css
 * @parent_css: Parent css of the control group that the new cpuset will be
 *              part of
 * Return: cpuset css on success, -ENOMEM on failure.
 *
 * Allocate and initialize a new cpuset css, for non-NULL @parent_css, return
 * top cpuset css otherwise.
 */
static struct cgroup_subsys_state *
cpuset_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct cpuset *cs;

	if (!parent_css)
		return &top_cpuset.css;

	cs = kzalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs)
		return ERR_PTR(-ENOMEM);

	if (alloc_cpumasks(cs, NULL)) {
		kfree(cs);
		return ERR_PTR(-ENOMEM);
	}

	__set_bit(CS_SCHED_LOAD_BALANCE, &cs->flags);
	nodes_clear(cs->mems_allowed);
	nodes_clear(cs->effective_mems);
	fmeter_init(&cs->fmeter);
	cs->relax_domain_level = -1;
	INIT_LIST_HEAD(&cs->remote_sibling);

	/* Set CS_MEMORY_MIGRATE for default hierarchy */
	if (cgroup_subsys_on_dfl(cpuset_cgrp_subsys))
		__set_bit(CS_MEMORY_MIGRATE, &cs->flags);

	return &cs->css;
}

static int cpuset_css_online(struct cgroup_subsys_state *css)
{
	struct cpuset *cs = css_cs(css);
	struct cpuset *parent = parent_cs(cs);
	struct cpuset *tmp_cs;
	struct cgroup_subsys_state *pos_css;

	if (!parent)
		return 0;

	cpus_read_lock();
	mutex_lock(&cpuset_mutex);

	set_bit(CS_ONLINE, &cs->flags);
	if (is_spread_page(parent))
		set_bit(CS_SPREAD_PAGE, &cs->flags);
	if (is_spread_slab(parent))
		set_bit(CS_SPREAD_SLAB, &cs->flags);

	cpuset_inc();

	spin_lock_irq(&callback_lock);
	if (is_in_v2_mode()) {
		cpumask_copy(cs->effective_cpus, parent->effective_cpus);
		cs->effective_mems = parent->effective_mems;
		cs->use_parent_ecpus = true;
		parent->child_ecpus_count++;
	}

	/*
	 * For v2, clear CS_SCHED_LOAD_BALANCE if parent is isolated
	 */
	if (cgroup_subsys_on_dfl(cpuset_cgrp_subsys) &&
	    !is_sched_load_balance(parent))
		clear_bit(CS_SCHED_LOAD_BALANCE, &cs->flags);

	spin_unlock_irq(&callback_lock);

	if (!test_bit(CGRP_CPUSET_CLONE_CHILDREN, &css->cgroup->flags))
		goto out_unlock;

	/*
	 * Clone @parent's configuration if CGRP_CPUSET_CLONE_CHILDREN is
	 * set.  This flag handling is implemented in cgroup core for
	 * historical reasons - the flag may be specified during mount.
	 *
	 * Currently, if any sibling cpusets have exclusive cpus or mem, we
	 * refuse to clone the configuration - thereby refusing the task to
	 * be entered, and as a result refusing the sys_unshare() or
	 * clone() which initiated it.  If this becomes a problem for some
	 * users who wish to allow that scenario, then this could be
	 * changed to grant parent->cpus_allowed-sibling_cpus_exclusive
	 * (and likewise for mems) to the new cgroup.
	 */
	rcu_read_lock();
	cpuset_for_each_child(tmp_cs, pos_css, parent) {
		if (is_mem_exclusive(tmp_cs) || is_cpu_exclusive(tmp_cs)) {
			rcu_read_unlock();
			goto out_unlock;
		}
	}
	rcu_read_unlock();

	spin_lock_irq(&callback_lock);
	cs->mems_allowed = parent->mems_allowed;
	cs->effective_mems = parent->mems_allowed;
	cpumask_copy(cs->cpus_allowed, parent->cpus_allowed);
	cpumask_copy(cs->effective_cpus, parent->cpus_allowed);
	spin_unlock_irq(&callback_lock);
out_unlock:
	mutex_unlock(&cpuset_mutex);
	cpus_read_unlock();
	return 0;
}

/*
 * If the cpuset being removed has its flag 'sched_load_balance'
 * enabled, then simulate turning sched_load_balance off, which
 * will call rebuild_sched_domains_locked(). That is not needed
 * in the default hierarchy where only changes in partition
 * will cause repartitioning.
 *
 * If the cpuset has the 'sched.partition' flag enabled, simulate
 * turning 'sched.partition" off.
 */

static void cpuset_css_offline(struct cgroup_subsys_state *css)
{
	struct cpuset *cs = css_cs(css);

	cpus_read_lock();
	mutex_lock(&cpuset_mutex);

	if (is_partition_valid(cs))
		update_prstate(cs, 0);

	if (!cgroup_subsys_on_dfl(cpuset_cgrp_subsys) &&
	    is_sched_load_balance(cs))
		update_flag(CS_SCHED_LOAD_BALANCE, cs, 0);

	if (cs->use_parent_ecpus) {
		struct cpuset *parent = parent_cs(cs);

		cs->use_parent_ecpus = false;
		parent->child_ecpus_count--;
	}

	cpuset_dec();
	clear_bit(CS_ONLINE, &cs->flags);

	mutex_unlock(&cpuset_mutex);
	cpus_read_unlock();
}

static void cpuset_css_free(struct cgroup_subsys_state *css)
{
	struct cpuset *cs = css_cs(css);

	free_cpuset(cs);
}

static void cpuset_bind(struct cgroup_subsys_state *root_css)
{
	mutex_lock(&cpuset_mutex);
	spin_lock_irq(&callback_lock);

	if (is_in_v2_mode()) {
		cpumask_copy(top_cpuset.cpus_allowed, cpu_possible_mask);
		cpumask_copy(top_cpuset.effective_xcpus, cpu_possible_mask);
		top_cpuset.mems_allowed = node_possible_map;
	} else {
		cpumask_copy(top_cpuset.cpus_allowed,
			     top_cpuset.effective_cpus);
		top_cpuset.mems_allowed = top_cpuset.effective_mems;
	}

	spin_unlock_irq(&callback_lock);
	mutex_unlock(&cpuset_mutex);
}

/*
 * In case the child is cloned into a cpuset different from its parent,
 * additional checks are done to see if the move is allowed.
 */
static int cpuset_can_fork(struct task_struct *task, struct css_set *cset)
{
	struct cpuset *cs = css_cs(cset->subsys[cpuset_cgrp_id]);
	bool same_cs;
	int ret;

	rcu_read_lock();
	same_cs = (cs == task_cs(current));
	rcu_read_unlock();

	if (same_cs)
		return 0;

	lockdep_assert_held(&cgroup_mutex);
	mutex_lock(&cpuset_mutex);

	/* Check to see if task is allowed in the cpuset */
	ret = cpuset_can_attach_check(cs);
	if (ret)
		goto out_unlock;

	ret = task_can_attach(task);
	if (ret)
		goto out_unlock;

	ret = security_task_setscheduler(task);
	if (ret)
		goto out_unlock;

	/*
	 * Mark attach is in progress.  This makes validate_change() fail
	 * changes which zero cpus/mems_allowed.
	 */
	cs->attach_in_progress++;
out_unlock:
	mutex_unlock(&cpuset_mutex);
	return ret;
}

static void cpuset_cancel_fork(struct task_struct *task, struct css_set *cset)
{
	struct cpuset *cs = css_cs(cset->subsys[cpuset_cgrp_id]);
	bool same_cs;

	rcu_read_lock();
	same_cs = (cs == task_cs(current));
	rcu_read_unlock();

	if (same_cs)
		return;

	mutex_lock(&cpuset_mutex);
	cs->attach_in_progress--;
	if (!cs->attach_in_progress)
		wake_up(&cpuset_attach_wq);
	mutex_unlock(&cpuset_mutex);
}

/*
 * Make sure the new task conform to the current state of its parent,
 * which could have been changed by cpuset just after it inherits the
 * state from the parent and before it sits on the cgroup's task list.
 */
static void cpuset_fork(struct task_struct *task)
{
	struct cpuset *cs;
	bool same_cs;

	rcu_read_lock();
	cs = task_cs(task);
	same_cs = (cs == task_cs(current));
	rcu_read_unlock();

	if (same_cs) {
		if (cs == &top_cpuset)
			return;

		set_cpus_allowed_ptr(task, current->cpus_ptr);
		task->mems_allowed = current->mems_allowed;
		return;
	}

	/* CLONE_INTO_CGROUP */
	mutex_lock(&cpuset_mutex);
	guarantee_online_mems(cs, &cpuset_attach_nodemask_to);
	cpuset_attach_task(cs, task);

	cs->attach_in_progress--;
	if (!cs->attach_in_progress)
		wake_up(&cpuset_attach_wq);

	mutex_unlock(&cpuset_mutex);
}

struct cgroup_subsys cpuset_cgrp_subsys = {
	.css_alloc	= cpuset_css_alloc,
	.css_online	= cpuset_css_online,
	.css_offline	= cpuset_css_offline,
	.css_free	= cpuset_css_free,
	.can_attach	= cpuset_can_attach,
	.cancel_attach	= cpuset_cancel_attach,
	.attach		= cpuset_attach,
	.post_attach	= cpuset_post_attach,
	.bind		= cpuset_bind,
	.can_fork	= cpuset_can_fork,
	.cancel_fork	= cpuset_cancel_fork,
	.fork		= cpuset_fork,
	.legacy_cftypes	= legacy_files,
	.dfl_cftypes	= dfl_files,
	.early_init	= true,
	.threaded	= true,
};

/**
 * cpuset_init - initialize cpusets at system boot
 *
 * Description: Initialize top_cpuset
 **/

int __init cpuset_init(void)
{
	BUG_ON(!alloc_cpumask_var(&top_cpuset.cpus_allowed, GFP_KERNEL));
	BUG_ON(!alloc_cpumask_var(&top_cpuset.effective_cpus, GFP_KERNEL));
	BUG_ON(!alloc_cpumask_var(&top_cpuset.effective_xcpus, GFP_KERNEL));
	BUG_ON(!alloc_cpumask_var(&top_cpuset.exclusive_cpus, GFP_KERNEL));
	BUG_ON(!zalloc_cpumask_var(&subpartitions_cpus, GFP_KERNEL));
	BUG_ON(!zalloc_cpumask_var(&isolated_cpus, GFP_KERNEL));

	cpumask_setall(top_cpuset.cpus_allowed);
	nodes_setall(top_cpuset.mems_allowed);
	cpumask_setall(top_cpuset.effective_cpus);
	cpumask_setall(top_cpuset.effective_xcpus);
	cpumask_setall(top_cpuset.exclusive_cpus);
	nodes_setall(top_cpuset.effective_mems);

	fmeter_init(&top_cpuset.fmeter);
	INIT_LIST_HEAD(&remote_children);

	BUG_ON(!alloc_cpumask_var(&cpus_attach, GFP_KERNEL));

	return 0;
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

static void
hotplug_update_tasks_legacy(struct cpuset *cs,
			    struct cpumask *new_cpus, nodemask_t *new_mems,
			    bool cpus_updated, bool mems_updated)
{
	bool is_empty;

	spin_lock_irq(&callback_lock);
	cpumask_copy(cs->cpus_allowed, new_cpus);
	cpumask_copy(cs->effective_cpus, new_cpus);
	cs->mems_allowed = *new_mems;
	cs->effective_mems = *new_mems;
	spin_unlock_irq(&callback_lock);

	/*
	 * Don't call update_tasks_cpumask() if the cpuset becomes empty,
	 * as the tasks will be migrated to an ancestor.
	 */
	if (cpus_updated && !cpumask_empty(cs->cpus_allowed))
		update_tasks_cpumask(cs, new_cpus);
	if (mems_updated && !nodes_empty(cs->mems_allowed))
		update_tasks_nodemask(cs);

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

static void
hotplug_update_tasks(struct cpuset *cs,
		     struct cpumask *new_cpus, nodemask_t *new_mems,
		     bool cpus_updated, bool mems_updated)
{
	/* A partition root is allowed to have empty effective cpus */
	if (cpumask_empty(new_cpus) && !is_partition_valid(cs))
		cpumask_copy(new_cpus, parent_cs(cs)->effective_cpus);
	if (nodes_empty(*new_mems))
		*new_mems = parent_cs(cs)->effective_mems;

	spin_lock_irq(&callback_lock);
	cpumask_copy(cs->effective_cpus, new_cpus);
	cs->effective_mems = *new_mems;
	spin_unlock_irq(&callback_lock);

	if (cpus_updated)
		update_tasks_cpumask(cs, new_cpus);
	if (mems_updated)
		update_tasks_nodemask(cs);
}

static bool force_rebuild;

void cpuset_force_rebuild(void)
{
	force_rebuild = true;
}

/**
 * cpuset_hotplug_update_tasks - update tasks in a cpuset for hotunplug
 * @cs: cpuset in interest
 * @tmp: the tmpmasks structure pointer
 *
 * Compare @cs's cpu and mem masks against top_cpuset and if some have gone
 * offline, update @cs accordingly.  If @cs ends up with no CPU or memory,
 * all its tasks are moved to the nearest ancestor with both resources.
 */
static void cpuset_hotplug_update_tasks(struct cpuset *cs, struct tmpmasks *tmp)
{
	static cpumask_t new_cpus;
	static nodemask_t new_mems;
	bool cpus_updated;
	bool mems_updated;
	bool remote;
	int partcmd = -1;
	struct cpuset *parent;
retry:
	wait_event(cpuset_attach_wq, cs->attach_in_progress == 0);

	mutex_lock(&cpuset_mutex);

	/*
	 * We have raced with task attaching. We wait until attaching
	 * is finished, so we won't attach a task to an empty cpuset.
	 */
	if (cs->attach_in_progress) {
		mutex_unlock(&cpuset_mutex);
		goto retry;
	}

	parent = parent_cs(cs);
	compute_effective_cpumask(&new_cpus, cs, parent);
	nodes_and(new_mems, cs->mems_allowed, parent->effective_mems);

	if (!tmp || !cs->partition_root_state)
		goto update_tasks;

	/*
	 * Compute effective_cpus for valid partition root, may invalidate
	 * child partition roots if necessary.
	 */
	remote = is_remote_partition(cs);
	if (remote || (is_partition_valid(cs) && is_partition_valid(parent)))
		compute_partition_effective_cpumask(cs, &new_cpus);

	if (remote && cpumask_empty(&new_cpus) &&
	    partition_is_populated(cs, NULL)) {
		remote_partition_disable(cs, tmp);
		compute_effective_cpumask(&new_cpus, cs, parent);
		remote = false;
		cpuset_force_rebuild();
	}

	/*
	 * Force the partition to become invalid if either one of
	 * the following conditions hold:
	 * 1) empty effective cpus but not valid empty partition.
	 * 2) parent is invalid or doesn't grant any cpus to child
	 *    partitions.
	 */
	if (is_local_partition(cs) && (!is_partition_valid(parent) ||
				tasks_nocpu_error(parent, cs, &new_cpus)))
		partcmd = partcmd_invalidate;
	/*
	 * On the other hand, an invalid partition root may be transitioned
	 * back to a regular one.
	 */
	else if (is_partition_valid(parent) && is_partition_invalid(cs))
		partcmd = partcmd_update;

	if (partcmd >= 0) {
		update_parent_effective_cpumask(cs, partcmd, NULL, tmp);
		if ((partcmd == partcmd_invalidate) || is_partition_valid(cs)) {
			compute_partition_effective_cpumask(cs, &new_cpus);
			cpuset_force_rebuild();
		}
	}

update_tasks:
	cpus_updated = !cpumask_equal(&new_cpus, cs->effective_cpus);
	mems_updated = !nodes_equal(new_mems, cs->effective_mems);
	if (!cpus_updated && !mems_updated)
		goto unlock;	/* Hotplug doesn't affect this cpuset */

	if (mems_updated)
		check_insane_mems_config(&new_mems);

	if (is_in_v2_mode())
		hotplug_update_tasks(cs, &new_cpus, &new_mems,
				     cpus_updated, mems_updated);
	else
		hotplug_update_tasks_legacy(cs, &new_cpus, &new_mems,
					    cpus_updated, mems_updated);

unlock:
	mutex_unlock(&cpuset_mutex);
}

/**
 * cpuset_handle_hotplug - handle CPU/memory hot{,un}plug for a cpuset
 *
 * This function is called after either CPU or memory configuration has
 * changed and updates cpuset accordingly.  The top_cpuset is always
 * synchronized to cpu_active_mask and N_MEMORY, which is necessary in
 * order to make cpusets transparent (of no affect) on systems that are
 * actively using CPU hotplug but making no active use of cpusets.
 *
 * Non-root cpusets are only affected by offlining.  If any CPUs or memory
 * nodes have been taken down, cpuset_hotplug_update_tasks() is invoked on
 * all descendants.
 *
 * Note that CPU offlining during suspend is ignored.  We don't modify
 * cpusets across suspend/resume cycles at all.
 *
 * CPU / memory hotplug is handled synchronously.
 */
static void cpuset_handle_hotplug(void)
{
	static cpumask_t new_cpus;
	static nodemask_t new_mems;
	bool cpus_updated, mems_updated;
	bool on_dfl = is_in_v2_mode();
	struct tmpmasks tmp, *ptmp = NULL;

	if (on_dfl && !alloc_cpumasks(NULL, &tmp))
		ptmp = &tmp;

	lockdep_assert_cpus_held();
	mutex_lock(&cpuset_mutex);

	/* fetch the available cpus/mems and find out which changed how */
	cpumask_copy(&new_cpus, cpu_active_mask);
	new_mems = node_states[N_MEMORY];

	/*
	 * If subpartitions_cpus is populated, it is likely that the check
	 * below will produce a false positive on cpus_updated when the cpu
	 * list isn't changed. It is extra work, but it is better to be safe.
	 */
	cpus_updated = !cpumask_equal(top_cpuset.effective_cpus, &new_cpus) ||
		       !cpumask_empty(subpartitions_cpus);
	mems_updated = !nodes_equal(top_cpuset.effective_mems, new_mems);

	/*
	 * In the rare case that hotplug removes all the cpus in
	 * subpartitions_cpus, we assumed that cpus are updated.
	 */
	if (!cpus_updated && top_cpuset.nr_subparts)
		cpus_updated = true;

	/* For v1, synchronize cpus_allowed to cpu_active_mask */
	if (cpus_updated) {
		spin_lock_irq(&callback_lock);
		if (!on_dfl)
			cpumask_copy(top_cpuset.cpus_allowed, &new_cpus);
		/*
		 * Make sure that CPUs allocated to child partitions
		 * do not show up in effective_cpus. If no CPU is left,
		 * we clear the subpartitions_cpus & let the child partitions
		 * fight for the CPUs again.
		 */
		if (!cpumask_empty(subpartitions_cpus)) {
			if (cpumask_subset(&new_cpus, subpartitions_cpus)) {
				top_cpuset.nr_subparts = 0;
				cpumask_clear(subpartitions_cpus);
			} else {
				cpumask_andnot(&new_cpus, &new_cpus,
					       subpartitions_cpus);
			}
		}
		cpumask_copy(top_cpuset.effective_cpus, &new_cpus);
		spin_unlock_irq(&callback_lock);
		/* we don't mess with cpumasks of tasks in top_cpuset */
	}

	/* synchronize mems_allowed to N_MEMORY */
	if (mems_updated) {
		spin_lock_irq(&callback_lock);
		if (!on_dfl)
			top_cpuset.mems_allowed = new_mems;
		top_cpuset.effective_mems = new_mems;
		spin_unlock_irq(&callback_lock);
		update_tasks_nodemask(&top_cpuset);
	}

	mutex_unlock(&cpuset_mutex);

	/* if cpus or mems changed, we need to propagate to descendants */
	if (cpus_updated || mems_updated) {
		struct cpuset *cs;
		struct cgroup_subsys_state *pos_css;

		rcu_read_lock();
		cpuset_for_each_descendant_pre(cs, pos_css, &top_cpuset) {
			if (cs == &top_cpuset || !css_tryget_online(&cs->css))
				continue;
			rcu_read_unlock();

			cpuset_hotplug_update_tasks(cs, ptmp);

			rcu_read_lock();
			css_put(&cs->css);
		}
		rcu_read_unlock();
	}

	/* rebuild sched domains if cpus_allowed has changed */
	if (cpus_updated || force_rebuild) {
		force_rebuild = false;
		rebuild_sched_domains_cpuslocked();
	}

	free_cpumasks(NULL, ptmp);
}

void cpuset_update_active_cpus(void)
{
	/*
	 * We're inside cpu hotplug critical region which usually nests
	 * inside cgroup synchronization.  Bounce actual hotplug processing
	 * to a work item to avoid reverse locking order.
	 */
	cpuset_handle_hotplug();
}

/*
 * Keep top_cpuset.mems_allowed tracking node_states[N_MEMORY].
 * Call this routine anytime after node_states[N_MEMORY] changes.
 * See cpuset_update_active_cpus() for CPU hotplug handling.
 */
static int cpuset_track_online_nodes(struct notifier_block *self,
				unsigned long action, void *arg)
{
	cpuset_handle_hotplug();
	return NOTIFY_OK;
}

/**
 * cpuset_init_smp - initialize cpus_allowed
 *
 * Description: Finish top cpuset after cpu, node maps are initialized
 */
void __init cpuset_init_smp(void)
{
	/*
	 * cpus_allowd/mems_allowed set to v2 values in the initial
	 * cpuset_bind() call will be reset to v1 values in another
	 * cpuset_bind() call when v1 cpuset is mounted.
	 */
	top_cpuset.old_mems_allowed = top_cpuset.mems_allowed;

	cpumask_copy(top_cpuset.effective_cpus, cpu_active_mask);
	top_cpuset.effective_mems = node_states[N_MEMORY];

	hotplug_memory_notifier(cpuset_track_online_nodes, CPUSET_CALLBACK_PRI);

	cpuset_migrate_mm_wq = alloc_ordered_workqueue("cpuset_migrate_mm", 0);
	BUG_ON(!cpuset_migrate_mm_wq);
}

/**
 * cpuset_cpus_allowed - return cpus_allowed mask from a tasks cpuset.
 * @tsk: pointer to task_struct from which to obtain cpuset->cpus_allowed.
 * @pmask: pointer to struct cpumask variable to receive cpus_allowed set.
 *
 * Description: Returns the cpumask_var_t cpus_allowed of the cpuset
 * attached to the specified @tsk.  Guaranteed to return some non-empty
 * subset of cpu_online_mask, even if this means going outside the
 * tasks cpuset, except when the task is in the top cpuset.
 **/

void cpuset_cpus_allowed(struct task_struct *tsk, struct cpumask *pmask)
{
	unsigned long flags;
	struct cpuset *cs;

	spin_lock_irqsave(&callback_lock, flags);
	rcu_read_lock();

	cs = task_cs(tsk);
	if (cs != &top_cpuset)
		guarantee_online_cpus(tsk, pmask);
	/*
	 * Tasks in the top cpuset won't get update to their cpumasks
	 * when a hotplug online/offline event happens. So we include all
	 * offline cpus in the allowed cpu list.
	 */
	if ((cs == &top_cpuset) || cpumask_empty(pmask)) {
		const struct cpumask *possible_mask = task_cpu_possible_mask(tsk);

		/*
		 * We first exclude cpus allocated to partitions. If there is no
		 * allowable online cpu left, we fall back to all possible cpus.
		 */
		cpumask_andnot(pmask, possible_mask, subpartitions_cpus);
		if (!cpumask_intersects(pmask, cpu_online_mask))
			cpumask_copy(pmask, possible_mask);
	}

	rcu_read_unlock();
	spin_unlock_irqrestore(&callback_lock, flags);
}

/**
 * cpuset_cpus_allowed_fallback - final fallback before complete catastrophe.
 * @tsk: pointer to task_struct with which the scheduler is struggling
 *
 * Description: In the case that the scheduler cannot find an allowed cpu in
 * tsk->cpus_allowed, we fall back to task_cs(tsk)->cpus_allowed. In legacy
 * mode however, this value is the same as task_cs(tsk)->effective_cpus,
 * which will not contain a sane cpumask during cases such as cpu hotplugging.
 * This is the absolute last resort for the scheduler and it is only used if
 * _every_ other avenue has been traveled.
 *
 * Returns true if the affinity of @tsk was changed, false otherwise.
 **/

bool cpuset_cpus_allowed_fallback(struct task_struct *tsk)
{
	const struct cpumask *possible_mask = task_cpu_possible_mask(tsk);
	const struct cpumask *cs_mask;
	bool changed = false;

	rcu_read_lock();
	cs_mask = task_cs(tsk)->cpus_allowed;
	if (is_in_v2_mode() && cpumask_subset(cs_mask, possible_mask)) {
		do_set_cpus_allowed(tsk, cs_mask);
		changed = true;
	}
	rcu_read_unlock();

	/*
	 * We own tsk->cpus_allowed, nobody can change it under us.
	 *
	 * But we used cs && cs->cpus_allowed lockless and thus can
	 * race with cgroup_attach_task() or update_cpumask() and get
	 * the wrong tsk->cpus_allowed. However, both cases imply the
	 * subsequent cpuset_change_cpumask()->set_cpus_allowed_ptr()
	 * which takes task_rq_lock().
	 *
	 * If we are called after it dropped the lock we must see all
	 * changes in tsk_cs()->cpus_allowed. Otherwise we can temporary
	 * set any mask even if it is not right from task_cs() pov,
	 * the pending set_cpus_allowed_ptr() will fix things.
	 *
	 * select_fallback_rq() will fix things ups and set cpu_possible_mask
	 * if required.
	 */
	return changed;
}

void __init cpuset_init_current_mems_allowed(void)
{
	nodes_setall(current->mems_allowed);
}

/**
 * cpuset_mems_allowed - return mems_allowed mask from a tasks cpuset.
 * @tsk: pointer to task_struct from which to obtain cpuset->mems_allowed.
 *
 * Description: Returns the nodemask_t mems_allowed of the cpuset
 * attached to the specified @tsk.  Guaranteed to return some non-empty
 * subset of node_states[N_MEMORY], even if this means going outside the
 * tasks cpuset.
 **/

nodemask_t cpuset_mems_allowed(struct task_struct *tsk)
{
	nodemask_t mask;
	unsigned long flags;

	spin_lock_irqsave(&callback_lock, flags);
	rcu_read_lock();
	guarantee_online_mems(task_cs(tsk), &mask);
	rcu_read_unlock();
	spin_unlock_irqrestore(&callback_lock, flags);

	return mask;
}

/**
 * cpuset_nodemask_valid_mems_allowed - check nodemask vs. current mems_allowed
 * @nodemask: the nodemask to be checked
 *
 * Are any of the nodes in the nodemask allowed in current->mems_allowed?
 */
int cpuset_nodemask_valid_mems_allowed(nodemask_t *nodemask)
{
	return nodes_intersects(*nodemask, current->mems_allowed);
}

/*
 * nearest_hardwall_ancestor() - Returns the nearest mem_exclusive or
 * mem_hardwall ancestor to the specified cpuset.  Call holding
 * callback_lock.  If no ancestor is mem_exclusive or mem_hardwall
 * (an unusual configuration), then returns the root cpuset.
 */
static struct cpuset *nearest_hardwall_ancestor(struct cpuset *cs)
{
	while (!(is_mem_exclusive(cs) || is_mem_hardwall(cs)) && parent_cs(cs))
		cs = parent_cs(cs);
	return cs;
}

/*
 * cpuset_node_allowed - Can we allocate on a memory node?
 * @node: is this an allowed node?
 * @gfp_mask: memory allocation flags
 *
 * If we're in interrupt, yes, we can always allocate.  If @node is set in
 * current's mems_allowed, yes.  If it's not a __GFP_HARDWALL request and this
 * node is set in the nearest hardwalled cpuset ancestor to current's cpuset,
 * yes.  If current has access to memory reserves as an oom victim, yes.
 * Otherwise, no.
 *
 * GFP_USER allocations are marked with the __GFP_HARDWALL bit,
 * and do not allow allocations outside the current tasks cpuset
 * unless the task has been OOM killed.
 * GFP_KERNEL allocations are not so marked, so can escape to the
 * nearest enclosing hardwalled ancestor cpuset.
 *
 * Scanning up parent cpusets requires callback_lock.  The
 * __alloc_pages() routine only calls here with __GFP_HARDWALL bit
 * _not_ set if it's a GFP_KERNEL allocation, and all nodes in the
 * current tasks mems_allowed came up empty on the first pass over
 * the zonelist.  So only GFP_KERNEL allocations, if all nodes in the
 * cpuset are short of memory, might require taking the callback_lock.
 *
 * The first call here from mm/page_alloc:get_page_from_freelist()
 * has __GFP_HARDWALL set in gfp_mask, enforcing hardwall cpusets,
 * so no allocation on a node outside the cpuset is allowed (unless
 * in interrupt, of course).
 *
 * The second pass through get_page_from_freelist() doesn't even call
 * here for GFP_ATOMIC calls.  For those calls, the __alloc_pages()
 * variable 'wait' is not set, and the bit ALLOC_CPUSET is not set
 * in alloc_flags.  That logic and the checks below have the combined
 * affect that:
 *	in_interrupt - any node ok (current task context irrelevant)
 *	GFP_ATOMIC   - any node ok
 *	tsk_is_oom_victim   - any node ok
 *	GFP_KERNEL   - any node in enclosing hardwalled cpuset ok
 *	GFP_USER     - only nodes in current tasks mems allowed ok.
 */
bool cpuset_node_allowed(int node, gfp_t gfp_mask)
{
	struct cpuset *cs;		/* current cpuset ancestors */
	bool allowed;			/* is allocation in zone z allowed? */
	unsigned long flags;

	if (in_interrupt())
		return true;
	if (node_isset(node, current->mems_allowed))
		return true;
	/*
	 * Allow tasks that have access to memory reserves because they have
	 * been OOM killed to get memory anywhere.
	 */
	if (unlikely(tsk_is_oom_victim(current)))
		return true;
	if (gfp_mask & __GFP_HARDWALL)	/* If hardwall request, stop here */
		return false;

	if (current->flags & PF_EXITING) /* Let dying task have memory */
		return true;

	/* Not hardwall and node outside mems_allowed: scan up cpusets */
	spin_lock_irqsave(&callback_lock, flags);

	rcu_read_lock();
	cs = nearest_hardwall_ancestor(task_cs(current));
	allowed = node_isset(node, cs->mems_allowed);
	rcu_read_unlock();

	spin_unlock_irqrestore(&callback_lock, flags);
	return allowed;
}

/**
 * cpuset_spread_node() - On which node to begin search for a page
 * @rotor: round robin rotor
 *
 * If a task is marked PF_SPREAD_PAGE or PF_SPREAD_SLAB (as for
 * tasks in a cpuset with is_spread_page or is_spread_slab set),
 * and if the memory allocation used cpuset_mem_spread_node()
 * to determine on which node to start looking, as it will for
 * certain page cache or slab cache pages such as used for file
 * system buffers and inode caches, then instead of starting on the
 * local node to look for a free page, rather spread the starting
 * node around the tasks mems_allowed nodes.
 *
 * We don't have to worry about the returned node being offline
 * because "it can't happen", and even if it did, it would be ok.
 *
 * The routines calling guarantee_online_mems() are careful to
 * only set nodes in task->mems_allowed that are online.  So it
 * should not be possible for the following code to return an
 * offline node.  But if it did, that would be ok, as this routine
 * is not returning the node where the allocation must be, only
 * the node where the search should start.  The zonelist passed to
 * __alloc_pages() will include all nodes.  If the slab allocator
 * is passed an offline node, it will fall back to the local node.
 * See kmem_cache_alloc_node().
 */
static int cpuset_spread_node(int *rotor)
{
	return *rotor = next_node_in(*rotor, current->mems_allowed);
}

/**
 * cpuset_mem_spread_node() - On which node to begin search for a file page
 */
int cpuset_mem_spread_node(void)
{
	if (current->cpuset_mem_spread_rotor == NUMA_NO_NODE)
		current->cpuset_mem_spread_rotor =
			node_random(&current->mems_allowed);

	return cpuset_spread_node(&current->cpuset_mem_spread_rotor);
}

/**
 * cpuset_slab_spread_node() - On which node to begin search for a slab page
 */
int cpuset_slab_spread_node(void)
{
	if (current->cpuset_slab_spread_rotor == NUMA_NO_NODE)
		current->cpuset_slab_spread_rotor =
			node_random(&current->mems_allowed);

	return cpuset_spread_node(&current->cpuset_slab_spread_rotor);
}
EXPORT_SYMBOL_GPL(cpuset_mem_spread_node);

/**
 * cpuset_mems_allowed_intersects - Does @tsk1's mems_allowed intersect @tsk2's?
 * @tsk1: pointer to task_struct of some task.
 * @tsk2: pointer to task_struct of some other task.
 *
 * Description: Return true if @tsk1's mems_allowed intersects the
 * mems_allowed of @tsk2.  Used by the OOM killer to determine if
 * one of the task's memory usage might impact the memory available
 * to the other.
 **/

int cpuset_mems_allowed_intersects(const struct task_struct *tsk1,
				   const struct task_struct *tsk2)
{
	return nodes_intersects(tsk1->mems_allowed, tsk2->mems_allowed);
}

/**
 * cpuset_print_current_mems_allowed - prints current's cpuset and mems_allowed
 *
 * Description: Prints current's name, cpuset name, and cached copy of its
 * mems_allowed to the kernel log.
 */
void cpuset_print_current_mems_allowed(void)
{
	struct cgroup *cgrp;

	rcu_read_lock();

	cgrp = task_cs(current)->css.cgroup;
	pr_cont(",cpuset=");
	pr_cont_cgroup_name(cgrp);
	pr_cont(",mems_allowed=%*pbl",
		nodemask_pr_args(&current->mems_allowed));

	rcu_read_unlock();
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

#ifdef CONFIG_PROC_PID_CPUSET
/*
 * proc_cpuset_show()
 *  - Print tasks cpuset path into seq_file.
 *  - Used for /proc/<pid>/cpuset.
 *  - No need to task_lock(tsk) on this tsk->cpuset reference, as it
 *    doesn't really matter if tsk->cpuset changes after we read it,
 *    and we take cpuset_mutex, keeping cpuset_attach() from changing it
 *    anyway.
 */
int proc_cpuset_show(struct seq_file *m, struct pid_namespace *ns,
		     struct pid *pid, struct task_struct *tsk)
{
	char *buf;
	struct cgroup_subsys_state *css;
	int retval;

	retval = -ENOMEM;
	buf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!buf)
		goto out;

	css = task_get_css(tsk, cpuset_cgrp_id);
	retval = cgroup_path_ns(css->cgroup, buf, PATH_MAX,
				current->nsproxy->cgroup_ns);
	css_put(css);
	if (retval == -E2BIG)
		retval = -ENAMETOOLONG;
	if (retval < 0)
		goto out_free;
	seq_puts(m, buf);
	seq_putc(m, '\n');
	retval = 0;
out_free:
	kfree(buf);
out:
	return retval;
}
#endif /* CONFIG_PROC_PID_CPUSET */

/* Display task mems_allowed in /proc/<pid>/status file. */
void cpuset_task_status_allowed(struct seq_file *m, struct task_struct *task)
{
	seq_printf(m, "Mems_allowed:\t%*pb\n",
		   nodemask_pr_args(&task->mems_allowed));
	seq_printf(m, "Mems_allowed_list:\t%*pbl\n",
		   nodemask_pr_args(&task->mems_allowed));
}
