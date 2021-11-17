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
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/mempolicy.h>
#include <linux/mm.h>
#include <linux/memory.h>
#include <linux/export.h>
#include <linux/mount.h>
#include <linux/fs_context.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/deadline.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/time64.h>
#include <linux/backing-dev.h>
#include <linux/sort.h>
#include <linux/oom.h>
#include <linux/sched/isolation.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/cgroup.h>
#include <linux/wait.h>

DEFINE_STATIC_KEY_FALSE(cpusets_pre_enable_key);
DEFINE_STATIC_KEY_FALSE(cpusets_enabled_key);

/* See "Frequency meter" comments, below. */

struct fmeter {
	int cnt;		/* unprocessed events count */
	int val;		/* most recent output value */
	time64_t time;		/* clock (secs) when val computed */
	spinlock_t lock;	/* guards read or write of above */
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
	 * CPUs allocated to child sub-partitions (default hierarchy only)
	 * - CPUs granted by the parent = effective_cpus U subparts_cpus
	 * - effective_cpus and subparts_cpus are mutually exclusive.
	 *
	 * effective_cpus contains only onlined CPUs, but subparts_cpus
	 * may have offlined ones.
	 */
	cpumask_var_t subparts_cpus;

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

	/* number of CPUs in subparts_cpus */
	int nr_subparts_cpus;

	/* partition root state */
	int partition_root_state;

	/*
	 * Default hierarchy only:
	 * use_parent_ecpus - set if using parent's effective_cpus
	 * child_ecpus_count - # of children with use_parent_ecpus set
	 */
	int use_parent_ecpus;
	int child_ecpus_count;

	/* Handle for cpuset.cpus.partition */
	struct cgroup_file partition_file;
};

/*
 * Partition root states:
 *
 *   0 - not a partition root
 *
 *   1 - partition root
 *
 *  -1 - invalid partition root
 *       None of the cpus in cpus_allowed can be put into the parent's
 *       subparts_cpus. In this case, the cpuset is not a real partition
 *       root anymore.  However, the CPU_EXCLUSIVE bit will still be set
 *       and the cpuset can be restored back to a partition root if the
 *       parent cpuset can give more CPUs back to this child cpuset.
 */
#define PRS_DISABLED		0
#define PRS_ENABLED		1
#define PRS_ERROR		-1

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

static inline int is_partition_root(const struct cpuset *cs)
{
	return cs->partition_root_state > 0;
}

/*
 * Send notification event of whenever partition_root_state changes.
 */
static inline void notify_partition_change(struct cpuset *cs,
					   int old_prs, int new_prs)
{
	if (old_prs != new_prs)
		cgroup_file_notify(&cs->partition_file);
}

static struct cpuset top_cpuset = {
	.flags = ((1 << CS_ONLINE) | (1 << CS_CPU_EXCLUSIVE) |
		  (1 << CS_MEM_EXCLUSIVE)),
	.partition_root_state = PRS_ENABLED,
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
 * There are two global locks guarding cpuset structures - cpuset_rwsem and
 * callback_lock. We also require taking task_lock() when dereferencing a
 * task's cpuset pointer. See "The task_lock() exception", at the end of this
 * comment.  The cpuset code uses only cpuset_rwsem write lock.  Other
 * kernel subsystems can use cpuset_read_lock()/cpuset_read_unlock() to
 * prevent change to cpuset structures.
 *
 * A task must hold both locks to modify cpusets.  If a task holds
 * cpuset_rwsem, it blocks others wanting that rwsem, ensuring that it
 * is the only task able to also acquire callback_lock and be able to
 * modify cpusets.  It can perform various checks on the cpuset structure
 * first, knowing nothing will change.  It can also allocate memory while
 * just holding cpuset_rwsem.  While it is performing these checks, various
 * callback routines can briefly acquire callback_lock to query cpusets.
 * Once it is ready to make the changes, it takes callback_lock, blocking
 * everyone else.
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

DEFINE_STATIC_PERCPU_RWSEM(cpuset_rwsem);

void cpuset_read_lock(void)
{
	percpu_down_read(&cpuset_rwsem);
}

void cpuset_read_unlock(void)
{
	percpu_up_read(&cpuset_rwsem);
}

static DEFINE_SPINLOCK(callback_lock);

static struct workqueue_struct *cpuset_migrate_mm_wq;

/*
 * CPU / memory hotplug is handled asynchronously.
 */
static void cpuset_hotplug_workfn(struct work_struct *work);
static DECLARE_WORK(cpuset_hotplug_work, cpuset_hotplug_workfn);

static DECLARE_WAIT_QUEUE_HEAD(cpuset_attach_wq);

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

/*
 * Return in pmask the portion of a task's cpusets's cpus_allowed that
 * are online and are capable of running the task.  If none are found,
 * walk up the cpuset hierarchy until we find one that does have some
 * appropriate cpus.
 *
 * One way or another, we guarantee to return some non-empty subset
 * of cpu_online_mask.
 *
 * Call with callback_lock or cpuset_rwsem held.
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

	while (!cpumask_intersects(cs->effective_cpus, pmask)) {
		cs = parent_cs(cs);
		if (unlikely(!cs)) {
			/*
			 * The top cpuset doesn't have any online cpu as a
			 * consequence of a race between cpuset_hotplug_work
			 * and cpu hotplug notifier.  But we know the top
			 * cpuset's effective_cpus is on its way to be
			 * identical to cpu_online_mask.
			 */
			goto out_unlock;
		}
	}
	cpumask_and(pmask, pmask, cs->effective_cpus);

out_unlock:
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
 * Call with callback_lock or cpuset_rwsem held.
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
 * Call with callback_lock or cpuset_rwsem held.
 */
static void cpuset_update_task_spread_flag(struct cpuset *cs,
					struct task_struct *tsk)
{
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
 * are only set if the other's are set.  Call holding cpuset_rwsem.
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
	cpumask_var_t *pmask1, *pmask2, *pmask3;

	if (cs) {
		pmask1 = &cs->cpus_allowed;
		pmask2 = &cs->effective_cpus;
		pmask3 = &cs->subparts_cpus;
	} else {
		pmask1 = &tmp->new_cpus;
		pmask2 = &tmp->addmask;
		pmask3 = &tmp->delmask;
	}

	if (!zalloc_cpumask_var(pmask1, GFP_KERNEL))
		return -ENOMEM;

	if (!zalloc_cpumask_var(pmask2, GFP_KERNEL))
		goto free_one;

	if (!zalloc_cpumask_var(pmask3, GFP_KERNEL))
		goto free_two;

	return 0;

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
		free_cpumask_var(cs->subparts_cpus);
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

/*
 * validate_change() - Used to validate that any proposed cpuset change
 *		       follows the structural rules for cpusets.
 *
 * If we replaced the flag and mask values of the current cpuset
 * (cur) with those values in the trial cpuset (trial), would
 * our various subset and exclusive rules still be valid?  Presumes
 * cpuset_rwsem held.
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
	int ret;

	rcu_read_lock();

	/* Each of our child cpusets must be a subset of us */
	ret = -EBUSY;
	cpuset_for_each_child(c, css, cur)
		if (!is_cpuset_subset(c, trial))
			goto out;

	/* Remaining checks don't apply to root cpuset */
	ret = 0;
	if (cur == &top_cpuset)
		goto out;

	par = parent_cs(cur);

	/* On legacy hierarchy, we must be a subset of our parent cpuset. */
	ret = -EACCES;
	if (!is_in_v2_mode() && !is_cpuset_subset(trial, par))
		goto out;

	/*
	 * If either I or some sibling (!= me) is exclusive, we can't
	 * overlap
	 */
	ret = -EINVAL;
	cpuset_for_each_child(c, css, par) {
		if ((is_cpu_exclusive(trial) || is_cpu_exclusive(c)) &&
		    c != cur &&
		    cpumask_intersects(trial->cpus_allowed, c->cpus_allowed))
			goto out;
		if ((is_mem_exclusive(trial) || is_mem_exclusive(c)) &&
		    c != cur &&
		    nodes_intersects(trial->mems_allowed, c->mems_allowed))
			goto out;
	}

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

/* Must be called with cpuset_rwsem held.  */
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
 * Must be called with cpuset_rwsem held.
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
	if (root_load_balance && !top_cpuset.nr_subparts_cpus) {
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
			    housekeeping_cpumask(HK_FLAG_DOMAIN));

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
					 housekeeping_cpumask(HK_FLAG_DOMAIN))))
			continue;

		if (root_load_balance &&
		    cpumask_subset(cp->cpus_allowed, top_cpuset.effective_cpus))
			continue;

		if (is_sched_load_balance(cp) &&
		    !cpumask_empty(cp->effective_cpus))
			csa[csn++] = cp;

		/* skip @cp's subtree if not a partition root */
		if (!is_partition_root(cp))
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
				cpumask_and(dp, dp, housekeeping_cpumask(HK_FLAG_DOMAIN));
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

static void update_tasks_root_domain(struct cpuset *cs)
{
	struct css_task_iter it;
	struct task_struct *task;

	css_task_iter_start(&cs->css, 0, &it);

	while ((task = css_task_iter_next(&it)))
		dl_add_task_root_domain(task);

	css_task_iter_end(&it);
}

static void rebuild_root_domains(void)
{
	struct cpuset *cs = NULL;
	struct cgroup_subsys_state *pos_css;

	percpu_rwsem_assert_held(&cpuset_rwsem);
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

		update_tasks_root_domain(cs);

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
	rebuild_root_domains();
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
 * Call with cpuset_rwsem held.  Takes cpus_read_lock().
 */
static void rebuild_sched_domains_locked(void)
{
	struct cgroup_subsys_state *pos_css;
	struct sched_domain_attr *attr;
	cpumask_var_t *doms;
	struct cpuset *cs;
	int ndoms;

	lockdep_assert_cpus_held();
	percpu_rwsem_assert_held(&cpuset_rwsem);

	/*
	 * If we have raced with CPU hotplug, return early to avoid
	 * passing doms with offlined cpu to partition_sched_domains().
	 * Anyways, cpuset_hotplug_workfn() will rebuild sched domains.
	 *
	 * With no CPUs in any subpartitions, top_cpuset's effective CPUs
	 * should be the same as the active CPUs, so checking only top_cpuset
	 * is enough to detect racing CPU offlines.
	 */
	if (!top_cpuset.nr_subparts_cpus &&
	    !cpumask_equal(top_cpuset.effective_cpus, cpu_active_mask))
		return;

	/*
	 * With subpartition CPUs, however, the effective CPUs of a partition
	 * root should be only a subset of the active CPUs.  Since a CPU in any
	 * partition root could be offlined, all must be checked.
	 */
	if (top_cpuset.nr_subparts_cpus) {
		rcu_read_lock();
		cpuset_for_each_descendant_pre(cs, pos_css, &top_cpuset) {
			if (!is_partition_root(cs)) {
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

void rebuild_sched_domains(void)
{
	cpus_read_lock();
	percpu_down_write(&cpuset_rwsem);
	rebuild_sched_domains_locked();
	percpu_up_write(&cpuset_rwsem);
	cpus_read_unlock();
}

/**
 * update_tasks_cpumask - Update the cpumasks of tasks in the cpuset.
 * @cs: the cpuset in which each task's cpus_allowed mask needs to be changed
 *
 * Iterate through each task of @cs updating its cpus_allowed to the
 * effective cpuset's.  As this function is called with cpuset_rwsem held,
 * cpuset membership stays stable.
 */
static void update_tasks_cpumask(struct cpuset *cs)
{
	struct css_task_iter it;
	struct task_struct *task;

	css_task_iter_start(&cs->css, 0, &it);
	while ((task = css_task_iter_next(&it)))
		set_cpus_allowed_ptr(task, cs->effective_cpus);
	css_task_iter_end(&it);
}

/**
 * compute_effective_cpumask - Compute the effective cpumask of the cpuset
 * @new_cpus: the temp variable for the new effective_cpus mask
 * @cs: the cpuset the need to recompute the new effective_cpus mask
 * @parent: the parent cpuset
 *
 * If the parent has subpartition CPUs, include them in the list of
 * allowable CPUs in computing the new effective_cpus mask. Since offlined
 * CPUs are not removed from subparts_cpus, we have to use cpu_active_mask
 * to mask those out.
 */
static void compute_effective_cpumask(struct cpumask *new_cpus,
				      struct cpuset *cs, struct cpuset *parent)
{
	if (parent->nr_subparts_cpus) {
		cpumask_or(new_cpus, parent->effective_cpus,
			   parent->subparts_cpus);
		cpumask_and(new_cpus, new_cpus, cs->cpus_allowed);
		cpumask_and(new_cpus, new_cpus, cpu_active_mask);
	} else {
		cpumask_and(new_cpus, cs->cpus_allowed, parent->effective_cpus);
	}
}

/*
 * Commands for update_parent_subparts_cpumask
 */
enum subparts_cmd {
	partcmd_enable,		/* Enable partition root	 */
	partcmd_disable,	/* Disable partition root	 */
	partcmd_update,		/* Update parent's subparts_cpus */
};

/**
 * update_parent_subparts_cpumask - update subparts_cpus mask of parent cpuset
 * @cpuset:  The cpuset that requests change in partition root state
 * @cmd:     Partition root state change command
 * @newmask: Optional new cpumask for partcmd_update
 * @tmp:     Temporary addmask and delmask
 * Return:   0, 1 or an error code
 *
 * For partcmd_enable, the cpuset is being transformed from a non-partition
 * root to a partition root. The cpus_allowed mask of the given cpuset will
 * be put into parent's subparts_cpus and taken away from parent's
 * effective_cpus. The function will return 0 if all the CPUs listed in
 * cpus_allowed can be granted or an error code will be returned.
 *
 * For partcmd_disable, the cpuset is being transofrmed from a partition
 * root back to a non-partition root. Any CPUs in cpus_allowed that are in
 * parent's subparts_cpus will be taken away from that cpumask and put back
 * into parent's effective_cpus. 0 should always be returned.
 *
 * For partcmd_update, if the optional newmask is specified, the cpu
 * list is to be changed from cpus_allowed to newmask. Otherwise,
 * cpus_allowed is assumed to remain the same. The cpuset should either
 * be a partition root or an invalid partition root. The partition root
 * state may change if newmask is NULL and none of the requested CPUs can
 * be granted by the parent. The function will return 1 if changes to
 * parent's subparts_cpus and effective_cpus happen or 0 otherwise.
 * Error code should only be returned when newmask is non-NULL.
 *
 * The partcmd_enable and partcmd_disable commands are used by
 * update_prstate(). The partcmd_update command is used by
 * update_cpumasks_hier() with newmask NULL and update_cpumask() with
 * newmask set.
 *
 * The checking is more strict when enabling partition root than the
 * other two commands.
 *
 * Because of the implicit cpu exclusive nature of a partition root,
 * cpumask changes that violates the cpu exclusivity rule will not be
 * permitted when checked by validate_change(). The validate_change()
 * function will also prevent any changes to the cpu list if it is not
 * a superset of children's cpu lists.
 */
static int update_parent_subparts_cpumask(struct cpuset *cpuset, int cmd,
					  struct cpumask *newmask,
					  struct tmpmasks *tmp)
{
	struct cpuset *parent = parent_cs(cpuset);
	int adding;	/* Moving cpus from effective_cpus to subparts_cpus */
	int deleting;	/* Moving cpus from subparts_cpus to effective_cpus */
	int old_prs, new_prs;
	bool part_error = false;	/* Partition error? */

	percpu_rwsem_assert_held(&cpuset_rwsem);

	/*
	 * The parent must be a partition root.
	 * The new cpumask, if present, or the current cpus_allowed must
	 * not be empty.
	 */
	if (!is_partition_root(parent) ||
	   (newmask && cpumask_empty(newmask)) ||
	   (!newmask && cpumask_empty(cpuset->cpus_allowed)))
		return -EINVAL;

	/*
	 * Enabling/disabling partition root is not allowed if there are
	 * online children.
	 */
	if ((cmd != partcmd_update) && css_has_online_children(&cpuset->css))
		return -EBUSY;

	/*
	 * Enabling partition root is not allowed if not all the CPUs
	 * can be granted from parent's effective_cpus or at least one
	 * CPU will be left after that.
	 */
	if ((cmd == partcmd_enable) &&
	   (!cpumask_subset(cpuset->cpus_allowed, parent->effective_cpus) ||
	     cpumask_equal(cpuset->cpus_allowed, parent->effective_cpus)))
		return -EINVAL;

	/*
	 * A cpumask update cannot make parent's effective_cpus become empty.
	 */
	adding = deleting = false;
	old_prs = new_prs = cpuset->partition_root_state;
	if (cmd == partcmd_enable) {
		cpumask_copy(tmp->addmask, cpuset->cpus_allowed);
		adding = true;
	} else if (cmd == partcmd_disable) {
		deleting = cpumask_and(tmp->delmask, cpuset->cpus_allowed,
				       parent->subparts_cpus);
	} else if (newmask) {
		/*
		 * partcmd_update with newmask:
		 *
		 * delmask = cpus_allowed & ~newmask & parent->subparts_cpus
		 * addmask = newmask & parent->effective_cpus
		 *		     & ~parent->subparts_cpus
		 */
		cpumask_andnot(tmp->delmask, cpuset->cpus_allowed, newmask);
		deleting = cpumask_and(tmp->delmask, tmp->delmask,
				       parent->subparts_cpus);

		cpumask_and(tmp->addmask, newmask, parent->effective_cpus);
		adding = cpumask_andnot(tmp->addmask, tmp->addmask,
					parent->subparts_cpus);
		/*
		 * Return error if the new effective_cpus could become empty.
		 */
		if (adding &&
		    cpumask_equal(parent->effective_cpus, tmp->addmask)) {
			if (!deleting)
				return -EINVAL;
			/*
			 * As some of the CPUs in subparts_cpus might have
			 * been offlined, we need to compute the real delmask
			 * to confirm that.
			 */
			if (!cpumask_and(tmp->addmask, tmp->delmask,
					 cpu_active_mask))
				return -EINVAL;
			cpumask_copy(tmp->addmask, parent->effective_cpus);
		}
	} else {
		/*
		 * partcmd_update w/o newmask:
		 *
		 * addmask = cpus_allowed & parent->effective_cpus
		 *
		 * Note that parent's subparts_cpus may have been
		 * pre-shrunk in case there is a change in the cpu list.
		 * So no deletion is needed.
		 */
		adding = cpumask_and(tmp->addmask, cpuset->cpus_allowed,
				     parent->effective_cpus);
		part_error = cpumask_equal(tmp->addmask,
					   parent->effective_cpus);
	}

	if (cmd == partcmd_update) {
		int prev_prs = cpuset->partition_root_state;

		/*
		 * Check for possible transition between PRS_ENABLED
		 * and PRS_ERROR.
		 */
		switch (cpuset->partition_root_state) {
		case PRS_ENABLED:
			if (part_error)
				new_prs = PRS_ERROR;
			break;
		case PRS_ERROR:
			if (!part_error)
				new_prs = PRS_ENABLED;
			break;
		}
		/*
		 * Set part_error if previously in invalid state.
		 */
		part_error = (prev_prs == PRS_ERROR);
	}

	if (!part_error && (new_prs == PRS_ERROR))
		return 0;	/* Nothing need to be done */

	if (new_prs == PRS_ERROR) {
		/*
		 * Remove all its cpus from parent's subparts_cpus.
		 */
		adding = false;
		deleting = cpumask_and(tmp->delmask, cpuset->cpus_allowed,
				       parent->subparts_cpus);
	}

	if (!adding && !deleting && (new_prs == old_prs))
		return 0;

	/*
	 * Change the parent's subparts_cpus.
	 * Newly added CPUs will be removed from effective_cpus and
	 * newly deleted ones will be added back to effective_cpus.
	 */
	spin_lock_irq(&callback_lock);
	if (adding) {
		cpumask_or(parent->subparts_cpus,
			   parent->subparts_cpus, tmp->addmask);
		cpumask_andnot(parent->effective_cpus,
			       parent->effective_cpus, tmp->addmask);
	}
	if (deleting) {
		cpumask_andnot(parent->subparts_cpus,
			       parent->subparts_cpus, tmp->delmask);
		/*
		 * Some of the CPUs in subparts_cpus might have been offlined.
		 */
		cpumask_and(tmp->delmask, tmp->delmask, cpu_active_mask);
		cpumask_or(parent->effective_cpus,
			   parent->effective_cpus, tmp->delmask);
	}

	parent->nr_subparts_cpus = cpumask_weight(parent->subparts_cpus);

	if (old_prs != new_prs)
		cpuset->partition_root_state = new_prs;

	spin_unlock_irq(&callback_lock);
	notify_partition_change(cpuset, old_prs, new_prs);

	return cmd == partcmd_update;
}

/*
 * update_cpumasks_hier - Update effective cpumasks and tasks in the subtree
 * @cs:  the cpuset to consider
 * @tmp: temp variables for calculating effective_cpus & partition setup
 *
 * When configured cpumask is changed, the effective cpumasks of this cpuset
 * and all its descendants need to be updated.
 *
 * On legacy hierarchy, effective_cpus will be the same with cpu_allowed.
 *
 * Called with cpuset_rwsem held
 */
static void update_cpumasks_hier(struct cpuset *cs, struct tmpmasks *tmp)
{
	struct cpuset *cp;
	struct cgroup_subsys_state *pos_css;
	bool need_rebuild_sched_domains = false;
	int old_prs, new_prs;

	rcu_read_lock();
	cpuset_for_each_descendant_pre(cp, pos_css, cs) {
		struct cpuset *parent = parent_cs(cp);

		compute_effective_cpumask(tmp->new_cpus, cp, parent);

		/*
		 * If it becomes empty, inherit the effective mask of the
		 * parent, which is guaranteed to have some CPUs.
		 */
		if (is_in_v2_mode() && cpumask_empty(tmp->new_cpus)) {
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

		/*
		 * Skip the whole subtree if the cpumask remains the same
		 * and has no partition root state.
		 */
		if (!cp->partition_root_state &&
		    cpumask_equal(tmp->new_cpus, cp->effective_cpus)) {
			pos_css = css_rightmost_descendant(pos_css);
			continue;
		}

		/*
		 * update_parent_subparts_cpumask() should have been called
		 * for cs already in update_cpumask(). We should also call
		 * update_tasks_cpumask() again for tasks in the parent
		 * cpuset if the parent's subparts_cpus changes.
		 */
		old_prs = new_prs = cp->partition_root_state;
		if ((cp != cs) && old_prs) {
			switch (parent->partition_root_state) {
			case PRS_DISABLED:
				/*
				 * If parent is not a partition root or an
				 * invalid partition root, clear its state
				 * and its CS_CPU_EXCLUSIVE flag.
				 */
				WARN_ON_ONCE(cp->partition_root_state
					     != PRS_ERROR);
				new_prs = PRS_DISABLED;

				/*
				 * clear_bit() is an atomic operation and
				 * readers aren't interested in the state
				 * of CS_CPU_EXCLUSIVE anyway. So we can
				 * just update the flag without holding
				 * the callback_lock.
				 */
				clear_bit(CS_CPU_EXCLUSIVE, &cp->flags);
				break;

			case PRS_ENABLED:
				if (update_parent_subparts_cpumask(cp, partcmd_update, NULL, tmp))
					update_tasks_cpumask(parent);
				break;

			case PRS_ERROR:
				/*
				 * When parent is invalid, it has to be too.
				 */
				new_prs = PRS_ERROR;
				break;
			}
		}

		if (!css_tryget_online(&cp->css))
			continue;
		rcu_read_unlock();

		spin_lock_irq(&callback_lock);

		cpumask_copy(cp->effective_cpus, tmp->new_cpus);
		if (cp->nr_subparts_cpus && (new_prs != PRS_ENABLED)) {
			cp->nr_subparts_cpus = 0;
			cpumask_clear(cp->subparts_cpus);
		} else if (cp->nr_subparts_cpus) {
			/*
			 * Make sure that effective_cpus & subparts_cpus
			 * are mutually exclusive.
			 *
			 * In the unlikely event that effective_cpus
			 * becomes empty. we clear cp->nr_subparts_cpus and
			 * let its child partition roots to compete for
			 * CPUs again.
			 */
			cpumask_andnot(cp->effective_cpus, cp->effective_cpus,
				       cp->subparts_cpus);
			if (cpumask_empty(cp->effective_cpus)) {
				cpumask_copy(cp->effective_cpus, tmp->new_cpus);
				cpumask_clear(cp->subparts_cpus);
				cp->nr_subparts_cpus = 0;
			} else if (!cpumask_subset(cp->subparts_cpus,
						   tmp->new_cpus)) {
				cpumask_andnot(cp->subparts_cpus,
					cp->subparts_cpus, tmp->new_cpus);
				cp->nr_subparts_cpus
					= cpumask_weight(cp->subparts_cpus);
			}
		}

		if (new_prs != old_prs)
			cp->partition_root_state = new_prs;

		spin_unlock_irq(&callback_lock);
		notify_partition_change(cp, old_prs, new_prs);

		WARN_ON(!is_in_v2_mode() &&
			!cpumask_equal(cp->cpus_allowed, cp->effective_cpus));

		update_tasks_cpumask(cp);

		/*
		 * On legacy hierarchy, if the effective cpumask of any non-
		 * empty cpuset is changed, we need to rebuild sched domains.
		 * On default hierarchy, the cpuset needs to be a partition
		 * root as well.
		 */
		if (!cpumask_empty(cp->cpus_allowed) &&
		    is_sched_load_balance(cp) &&
		   (!cgroup_subsys_on_dfl(cpuset_cgrp_subsys) ||
		    is_partition_root(cp)))
			need_rebuild_sched_domains = true;

		rcu_read_lock();
		css_put(&cp->css);
	}
	rcu_read_unlock();

	if (need_rebuild_sched_domains)
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

	/*
	 * Check all its siblings and call update_cpumasks_hier()
	 * if their use_parent_ecpus flag is set in order for them
	 * to use the right effective_cpus value.
	 */
	rcu_read_lock();
	cpuset_for_each_child(sibling, pos_css, parent) {
		if (sibling == cs)
			continue;
		if (!sibling->use_parent_ecpus)
			continue;

		update_cpumasks_hier(sibling, tmp);
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
	} else {
		retval = cpulist_parse(buf, trialcs->cpus_allowed);
		if (retval < 0)
			return retval;

		if (!cpumask_subset(trialcs->cpus_allowed,
				    top_cpuset.cpus_allowed))
			return -EINVAL;
	}

	/* Nothing to do if the cpus didn't change */
	if (cpumask_equal(cs->cpus_allowed, trialcs->cpus_allowed))
		return 0;

	retval = validate_change(cs, trialcs);
	if (retval < 0)
		return retval;

#ifdef CONFIG_CPUMASK_OFFSTACK
	/*
	 * Use the cpumasks in trialcs for tmpmasks when they are pointers
	 * to allocated cpumasks.
	 */
	tmp.addmask  = trialcs->subparts_cpus;
	tmp.delmask  = trialcs->effective_cpus;
	tmp.new_cpus = trialcs->cpus_allowed;
#endif

	if (cs->partition_root_state) {
		/* Cpumask of a partition root cannot be empty */
		if (cpumask_empty(trialcs->cpus_allowed))
			return -EINVAL;
		if (update_parent_subparts_cpumask(cs, partcmd_update,
					trialcs->cpus_allowed, &tmp) < 0)
			return -EINVAL;
	}

	spin_lock_irq(&callback_lock);
	cpumask_copy(cs->cpus_allowed, trialcs->cpus_allowed);

	/*
	 * Make sure that subparts_cpus is a subset of cpus_allowed.
	 */
	if (cs->nr_subparts_cpus) {
		cpumask_andnot(cs->subparts_cpus, cs->subparts_cpus,
			       cs->cpus_allowed);
		cs->nr_subparts_cpus = cpumask_weight(cs->subparts_cpus);
	}
	spin_unlock_irq(&callback_lock);

	update_cpumasks_hier(cs, &tmp);

	if (cs->partition_root_state) {
		struct cpuset *parent = parent_cs(cs);

		/*
		 * For partition root, update the cpumasks of sibling
		 * cpusets if they use parent's effective_cpus.
		 */
		if (parent->child_ecpus_count)
			update_sibling_cpumasks(parent, cs, &tmp);
	}
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
 * effective cpuset's.  As this function is called with cpuset_rwsem held,
 * cpuset membership stays stable.
 */
static void update_tasks_nodemask(struct cpuset *cs)
{
	static nodemask_t newmems;	/* protected by cpuset_rwsem */
	struct css_task_iter it;
	struct task_struct *task;

	cpuset_being_rebound = cs;		/* causes mpol_dup() rebind */

	guarantee_online_mems(cs, &newmems);

	/*
	 * The mpol_rebind_mm() call takes mmap_lock, which we couldn't
	 * take while holding tasklist_lock.  Forks can happen - the
	 * mpol_dup() cpuset_being_rebound check will catch such forks,
	 * and rebind their vma mempolicies too.  Because we still hold
	 * the global cpuset_rwsem, we know that no other rebind effort
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
 * Called with cpuset_rwsem held
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
 * Call with cpuset_rwsem held. May take callback_lock during call.
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
	if (val < -1 || val >= sched_domain_level_max)
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
 * function is called with cpuset_rwsem held, cpuset membership stays
 * stable.
 */
static void update_tasks_flags(struct cpuset *cs)
{
	struct css_task_iter it;
	struct task_struct *task;

	css_task_iter_start(&cs->css, 0, &it);
	while ((task = css_task_iter_next(&it)))
		cpuset_update_task_spread_flag(cs, task);
	css_task_iter_end(&it);
}

/*
 * update_flag - read a 0 or a 1 in a file and update associated flag
 * bit:		the bit to update (see cpuset_flagbits_t)
 * cs:		the cpuset to update
 * turning_on: 	whether the flag is being set or cleared
 *
 * Call with cpuset_rwsem held.
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

/*
 * update_prstate - update partititon_root_state
 * cs: the cpuset to update
 * new_prs: new partition root state
 *
 * Call with cpuset_rwsem held.
 */
static int update_prstate(struct cpuset *cs, int new_prs)
{
	int err, old_prs = cs->partition_root_state;
	struct cpuset *parent = parent_cs(cs);
	struct tmpmasks tmpmask;

	if (old_prs == new_prs)
		return 0;

	/*
	 * Cannot force a partial or invalid partition root to a full
	 * partition root.
	 */
	if (new_prs && (old_prs == PRS_ERROR))
		return -EINVAL;

	if (alloc_cpumasks(NULL, &tmpmask))
		return -ENOMEM;

	err = -EINVAL;
	if (!old_prs) {
		/*
		 * Turning on partition root requires setting the
		 * CS_CPU_EXCLUSIVE bit implicitly as well and cpus_allowed
		 * cannot be NULL.
		 */
		if (cpumask_empty(cs->cpus_allowed))
			goto out;

		err = update_flag(CS_CPU_EXCLUSIVE, cs, 1);
		if (err)
			goto out;

		err = update_parent_subparts_cpumask(cs, partcmd_enable,
						     NULL, &tmpmask);
		if (err) {
			update_flag(CS_CPU_EXCLUSIVE, cs, 0);
			goto out;
		}
	} else {
		/*
		 * Turning off partition root will clear the
		 * CS_CPU_EXCLUSIVE bit.
		 */
		if (old_prs == PRS_ERROR) {
			update_flag(CS_CPU_EXCLUSIVE, cs, 0);
			err = 0;
			goto out;
		}

		err = update_parent_subparts_cpumask(cs, partcmd_disable,
						     NULL, &tmpmask);
		if (err)
			goto out;

		/* Turning off CS_CPU_EXCLUSIVE will not return error */
		update_flag(CS_CPU_EXCLUSIVE, cs, 0);
	}

	/*
	 * Update cpumask of parent's tasks except when it is the top
	 * cpuset as some system daemons cannot be mapped to other CPUs.
	 */
	if (parent != &top_cpuset)
		update_tasks_cpumask(parent);

	if (parent->child_ecpus_count)
		update_sibling_cpumasks(parent, cs, &tmpmask);

	rebuild_sched_domains_locked();
out:
	if (!err) {
		spin_lock_irq(&callback_lock);
		cs->partition_root_state = new_prs;
		spin_unlock_irq(&callback_lock);
		notify_partition_change(cs, old_prs, new_prs);
	}

	free_cpumasks(NULL, &tmpmask);
	return err;
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

/* Called by cgroups to determine if a cpuset is usable; cpuset_rwsem held */
static int cpuset_can_attach(struct cgroup_taskset *tset)
{
	struct cgroup_subsys_state *css;
	struct cpuset *cs;
	struct task_struct *task;
	int ret;

	/* used later by cpuset_attach() */
	cpuset_attach_old_cs = task_cs(cgroup_taskset_first(tset, &css));
	cs = css_cs(css);

	percpu_down_write(&cpuset_rwsem);

	/* allow moving tasks into an empty cpuset if on default hierarchy */
	ret = -ENOSPC;
	if (!is_in_v2_mode() &&
	    (cpumask_empty(cs->cpus_allowed) || nodes_empty(cs->mems_allowed)))
		goto out_unlock;

	cgroup_taskset_for_each(task, css, tset) {
		ret = task_can_attach(task, cs->cpus_allowed);
		if (ret)
			goto out_unlock;
		ret = security_task_setscheduler(task);
		if (ret)
			goto out_unlock;
	}

	/*
	 * Mark attach is in progress.  This makes validate_change() fail
	 * changes which zero cpus/mems_allowed.
	 */
	cs->attach_in_progress++;
	ret = 0;
out_unlock:
	percpu_up_write(&cpuset_rwsem);
	return ret;
}

static void cpuset_cancel_attach(struct cgroup_taskset *tset)
{
	struct cgroup_subsys_state *css;

	cgroup_taskset_first(tset, &css);

	percpu_down_write(&cpuset_rwsem);
	css_cs(css)->attach_in_progress--;
	percpu_up_write(&cpuset_rwsem);
}

/*
 * Protected by cpuset_rwsem.  cpus_attach is used only by cpuset_attach()
 * but we can't allocate it dynamically there.  Define it global and
 * allocate from cpuset_init().
 */
static cpumask_var_t cpus_attach;

static void cpuset_attach(struct cgroup_taskset *tset)
{
	/* static buf protected by cpuset_rwsem */
	static nodemask_t cpuset_attach_nodemask_to;
	struct task_struct *task;
	struct task_struct *leader;
	struct cgroup_subsys_state *css;
	struct cpuset *cs;
	struct cpuset *oldcs = cpuset_attach_old_cs;

	cgroup_taskset_first(tset, &css);
	cs = css_cs(css);

	percpu_down_write(&cpuset_rwsem);

	guarantee_online_mems(cs, &cpuset_attach_nodemask_to);

	cgroup_taskset_for_each(task, css, tset) {
		if (cs != &top_cpuset)
			guarantee_online_cpus(task, cpus_attach);
		else
			cpumask_copy(cpus_attach, task_cpu_possible_mask(task));
		/*
		 * can_attach beforehand should guarantee that this doesn't
		 * fail.  TODO: have a better way to handle failure here
		 */
		WARN_ON_ONCE(set_cpus_allowed_ptr(task, cpus_attach));

		cpuset_change_task_nodemask(task, &cpuset_attach_nodemask_to);
		cpuset_update_task_spread_flag(cs, task);
	}

	/*
	 * Change mm for all threadgroup leaders. This is expensive and may
	 * sleep and should be moved outside migration path proper.
	 */
	cpuset_attach_nodemask_to = cs->effective_mems;
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

	cs->old_mems_allowed = cpuset_attach_nodemask_to;

	cs->attach_in_progress--;
	if (!cs->attach_in_progress)
		wake_up(&cpuset_attach_wq);

	percpu_up_write(&cpuset_rwsem);
}

/* The various types of files and directories in a cpuset file system */

typedef enum {
	FILE_MEMORY_MIGRATE,
	FILE_CPULIST,
	FILE_MEMLIST,
	FILE_EFFECTIVE_CPULIST,
	FILE_EFFECTIVE_MEMLIST,
	FILE_SUBPARTS_CPULIST,
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
	percpu_down_write(&cpuset_rwsem);
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
	percpu_up_write(&cpuset_rwsem);
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
	percpu_down_write(&cpuset_rwsem);
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
	percpu_up_write(&cpuset_rwsem);
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
	 * cpuset_hotplug_work calls back into cgroup core via
	 * cgroup_transfer_tasks() and waiting for it from a cgroupfs
	 * operation like this one can lead to a deadlock through kernfs
	 * active_ref protection.  Let's break the protection.  Losing the
	 * protection is okay as we check whether @cs is online after
	 * grabbing cpuset_rwsem anyway.  This only happens on the legacy
	 * hierarchies.
	 */
	css_get(&cs->css);
	kernfs_break_active_protection(of->kn);
	flush_work(&cpuset_hotplug_work);

	cpus_read_lock();
	percpu_down_write(&cpuset_rwsem);
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
	case FILE_MEMLIST:
		retval = update_nodemask(cs, trialcs, buf);
		break;
	default:
		retval = -EINVAL;
		break;
	}

	free_cpuset(trialcs);
out_unlock:
	percpu_up_write(&cpuset_rwsem);
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
	case FILE_SUBPARTS_CPULIST:
		seq_printf(sf, "%*pbl\n", cpumask_pr_args(cs->subparts_cpus));
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

	switch (cs->partition_root_state) {
	case PRS_ENABLED:
		seq_puts(seq, "root\n");
		break;
	case PRS_DISABLED:
		seq_puts(seq, "member\n");
		break;
	case PRS_ERROR:
		seq_puts(seq, "root invalid\n");
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

	/*
	 * Convert "root" to ENABLED, and convert "member" to DISABLED.
	 */
	if (!strcmp(buf, "root"))
		val = PRS_ENABLED;
	else if (!strcmp(buf, "member"))
		val = PRS_DISABLED;
	else
		return -EINVAL;

	css_get(&cs->css);
	cpus_read_lock();
	percpu_down_write(&cpuset_rwsem);
	if (!is_cpuset_online(cs))
		goto out_unlock;

	retval = update_prstate(cs, val);
out_unlock:
	percpu_up_write(&cpuset_rwsem);
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
		.name = "cpus.subpartitions",
		.seq_show = cpuset_common_seq_show,
		.private = FILE_SUBPARTS_CPULIST,
		.flags = CFTYPE_DEBUG,
	},

	{ }	/* terminate */
};


/*
 *	cpuset_css_alloc - allocate a cpuset css
 *	cgrp:	control group that the new cpuset will be part of
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
	percpu_down_write(&cpuset_rwsem);

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
	spin_unlock_irq(&callback_lock);

	if (!test_bit(CGRP_CPUSET_CLONE_CHILDREN, &css->cgroup->flags))
		goto out_unlock;

	/*
	 * Clone @parent's configuration if CGRP_CPUSET_CLONE_CHILDREN is
	 * set.  This flag handling is implemented in cgroup core for
	 * histrical reasons - the flag may be specified during mount.
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
	percpu_up_write(&cpuset_rwsem);
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
	percpu_down_write(&cpuset_rwsem);

	if (is_partition_root(cs))
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

	percpu_up_write(&cpuset_rwsem);
	cpus_read_unlock();
}

static void cpuset_css_free(struct cgroup_subsys_state *css)
{
	struct cpuset *cs = css_cs(css);

	free_cpuset(cs);
}

static void cpuset_bind(struct cgroup_subsys_state *root_css)
{
	percpu_down_write(&cpuset_rwsem);
	spin_lock_irq(&callback_lock);

	if (is_in_v2_mode()) {
		cpumask_copy(top_cpuset.cpus_allowed, cpu_possible_mask);
		top_cpuset.mems_allowed = node_possible_map;
	} else {
		cpumask_copy(top_cpuset.cpus_allowed,
			     top_cpuset.effective_cpus);
		top_cpuset.mems_allowed = top_cpuset.effective_mems;
	}

	spin_unlock_irq(&callback_lock);
	percpu_up_write(&cpuset_rwsem);
}

/*
 * Make sure the new task conform to the current state of its parent,
 * which could have been changed by cpuset just after it inherits the
 * state from the parent and before it sits on the cgroup's task list.
 */
static void cpuset_fork(struct task_struct *task)
{
	if (task_css_is_root(task, cpuset_cgrp_id))
		return;

	set_cpus_allowed_ptr(task, current->cpus_ptr);
	task->mems_allowed = current->mems_allowed;
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
	BUG_ON(percpu_init_rwsem(&cpuset_rwsem));

	BUG_ON(!alloc_cpumask_var(&top_cpuset.cpus_allowed, GFP_KERNEL));
	BUG_ON(!alloc_cpumask_var(&top_cpuset.effective_cpus, GFP_KERNEL));
	BUG_ON(!zalloc_cpumask_var(&top_cpuset.subparts_cpus, GFP_KERNEL));

	cpumask_setall(top_cpuset.cpus_allowed);
	nodes_setall(top_cpuset.mems_allowed);
	cpumask_setall(top_cpuset.effective_cpus);
	nodes_setall(top_cpuset.effective_mems);

	fmeter_init(&top_cpuset.fmeter);
	set_bit(CS_SCHED_LOAD_BALANCE, &top_cpuset.flags);
	top_cpuset.relax_domain_level = -1;

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
	 * as the tasks will be migratecd to an ancestor.
	 */
	if (cpus_updated && !cpumask_empty(cs->cpus_allowed))
		update_tasks_cpumask(cs);
	if (mems_updated && !nodes_empty(cs->mems_allowed))
		update_tasks_nodemask(cs);

	is_empty = cpumask_empty(cs->cpus_allowed) ||
		   nodes_empty(cs->mems_allowed);

	percpu_up_write(&cpuset_rwsem);

	/*
	 * Move tasks to the nearest ancestor with execution resources,
	 * This is full cgroup operation which will also call back into
	 * cpuset. Should be done outside any lock.
	 */
	if (is_empty)
		remove_tasks_in_empty_cpuset(cs);

	percpu_down_write(&cpuset_rwsem);
}

static void
hotplug_update_tasks(struct cpuset *cs,
		     struct cpumask *new_cpus, nodemask_t *new_mems,
		     bool cpus_updated, bool mems_updated)
{
	if (cpumask_empty(new_cpus))
		cpumask_copy(new_cpus, parent_cs(cs)->effective_cpus);
	if (nodes_empty(*new_mems))
		*new_mems = parent_cs(cs)->effective_mems;

	spin_lock_irq(&callback_lock);
	cpumask_copy(cs->effective_cpus, new_cpus);
	cs->effective_mems = *new_mems;
	spin_unlock_irq(&callback_lock);

	if (cpus_updated)
		update_tasks_cpumask(cs);
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
	struct cpuset *parent;
retry:
	wait_event(cpuset_attach_wq, cs->attach_in_progress == 0);

	percpu_down_write(&cpuset_rwsem);

	/*
	 * We have raced with task attaching. We wait until attaching
	 * is finished, so we won't attach a task to an empty cpuset.
	 */
	if (cs->attach_in_progress) {
		percpu_up_write(&cpuset_rwsem);
		goto retry;
	}

	parent = parent_cs(cs);
	compute_effective_cpumask(&new_cpus, cs, parent);
	nodes_and(new_mems, cs->mems_allowed, parent->effective_mems);

	if (cs->nr_subparts_cpus)
		/*
		 * Make sure that CPUs allocated to child partitions
		 * do not show up in effective_cpus.
		 */
		cpumask_andnot(&new_cpus, &new_cpus, cs->subparts_cpus);

	if (!tmp || !cs->partition_root_state)
		goto update_tasks;

	/*
	 * In the unlikely event that a partition root has empty
	 * effective_cpus or its parent becomes erroneous, we have to
	 * transition it to the erroneous state.
	 */
	if (is_partition_root(cs) && (cpumask_empty(&new_cpus) ||
	   (parent->partition_root_state == PRS_ERROR))) {
		if (cs->nr_subparts_cpus) {
			spin_lock_irq(&callback_lock);
			cs->nr_subparts_cpus = 0;
			cpumask_clear(cs->subparts_cpus);
			spin_unlock_irq(&callback_lock);
			compute_effective_cpumask(&new_cpus, cs, parent);
		}

		/*
		 * If the effective_cpus is empty because the child
		 * partitions take away all the CPUs, we can keep
		 * the current partition and let the child partitions
		 * fight for available CPUs.
		 */
		if ((parent->partition_root_state == PRS_ERROR) ||
		     cpumask_empty(&new_cpus)) {
			int old_prs;

			update_parent_subparts_cpumask(cs, partcmd_disable,
						       NULL, tmp);
			old_prs = cs->partition_root_state;
			if (old_prs != PRS_ERROR) {
				spin_lock_irq(&callback_lock);
				cs->partition_root_state = PRS_ERROR;
				spin_unlock_irq(&callback_lock);
				notify_partition_change(cs, old_prs, PRS_ERROR);
			}
		}
		cpuset_force_rebuild();
	}

	/*
	 * On the other hand, an erroneous partition root may be transitioned
	 * back to a regular one or a partition root with no CPU allocated
	 * from the parent may change to erroneous.
	 */
	if (is_partition_root(parent) &&
	   ((cs->partition_root_state == PRS_ERROR) ||
	    !cpumask_intersects(&new_cpus, parent->subparts_cpus)) &&
	     update_parent_subparts_cpumask(cs, partcmd_update, NULL, tmp))
		cpuset_force_rebuild();

update_tasks:
	cpus_updated = !cpumask_equal(&new_cpus, cs->effective_cpus);
	mems_updated = !nodes_equal(new_mems, cs->effective_mems);

	if (is_in_v2_mode())
		hotplug_update_tasks(cs, &new_cpus, &new_mems,
				     cpus_updated, mems_updated);
	else
		hotplug_update_tasks_legacy(cs, &new_cpus, &new_mems,
					    cpus_updated, mems_updated);

	percpu_up_write(&cpuset_rwsem);
}

/**
 * cpuset_hotplug_workfn - handle CPU/memory hotunplug for a cpuset
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
 */
static void cpuset_hotplug_workfn(struct work_struct *work)
{
	static cpumask_t new_cpus;
	static nodemask_t new_mems;
	bool cpus_updated, mems_updated;
	bool on_dfl = is_in_v2_mode();
	struct tmpmasks tmp, *ptmp = NULL;

	if (on_dfl && !alloc_cpumasks(NULL, &tmp))
		ptmp = &tmp;

	percpu_down_write(&cpuset_rwsem);

	/* fetch the available cpus/mems and find out which changed how */
	cpumask_copy(&new_cpus, cpu_active_mask);
	new_mems = node_states[N_MEMORY];

	/*
	 * If subparts_cpus is populated, it is likely that the check below
	 * will produce a false positive on cpus_updated when the cpu list
	 * isn't changed. It is extra work, but it is better to be safe.
	 */
	cpus_updated = !cpumask_equal(top_cpuset.effective_cpus, &new_cpus);
	mems_updated = !nodes_equal(top_cpuset.effective_mems, new_mems);

	/*
	 * In the rare case that hotplug removes all the cpus in subparts_cpus,
	 * we assumed that cpus are updated.
	 */
	if (!cpus_updated && top_cpuset.nr_subparts_cpus)
		cpus_updated = true;

	/* synchronize cpus_allowed to cpu_active_mask */
	if (cpus_updated) {
		spin_lock_irq(&callback_lock);
		if (!on_dfl)
			cpumask_copy(top_cpuset.cpus_allowed, &new_cpus);
		/*
		 * Make sure that CPUs allocated to child partitions
		 * do not show up in effective_cpus. If no CPU is left,
		 * we clear the subparts_cpus & let the child partitions
		 * fight for the CPUs again.
		 */
		if (top_cpuset.nr_subparts_cpus) {
			if (cpumask_subset(&new_cpus,
					   top_cpuset.subparts_cpus)) {
				top_cpuset.nr_subparts_cpus = 0;
				cpumask_clear(top_cpuset.subparts_cpus);
			} else {
				cpumask_andnot(&new_cpus, &new_cpus,
					       top_cpuset.subparts_cpus);
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

	percpu_up_write(&cpuset_rwsem);

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
		rebuild_sched_domains();
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
	schedule_work(&cpuset_hotplug_work);
}

void cpuset_wait_for_hotplug(void)
{
	flush_work(&cpuset_hotplug_work);
}

/*
 * Keep top_cpuset.mems_allowed tracking node_states[N_MEMORY].
 * Call this routine anytime after node_states[N_MEMORY] changes.
 * See cpuset_update_active_cpus() for CPU hotplug handling.
 */
static int cpuset_track_online_nodes(struct notifier_block *self,
				unsigned long action, void *arg)
{
	schedule_work(&cpuset_hotplug_work);
	return NOTIFY_OK;
}

static struct notifier_block cpuset_track_online_nodes_nb = {
	.notifier_call = cpuset_track_online_nodes,
	.priority = 10,		/* ??! */
};

/**
 * cpuset_init_smp - initialize cpus_allowed
 *
 * Description: Finish top cpuset after cpu, node maps are initialized
 */
void __init cpuset_init_smp(void)
{
	cpumask_copy(top_cpuset.cpus_allowed, cpu_active_mask);
	top_cpuset.mems_allowed = node_states[N_MEMORY];
	top_cpuset.old_mems_allowed = top_cpuset.mems_allowed;

	cpumask_copy(top_cpuset.effective_cpus, cpu_active_mask);
	top_cpuset.effective_mems = node_states[N_MEMORY];

	register_hotmemory_notifier(&cpuset_track_online_nodes_nb);

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
 * tasks cpuset.
 **/

void cpuset_cpus_allowed(struct task_struct *tsk, struct cpumask *pmask)
{
	unsigned long flags;

	spin_lock_irqsave(&callback_lock, flags);
	guarantee_online_cpus(tsk, pmask);
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

/**
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
bool __cpuset_node_allowed(int node, gfp_t gfp_mask)
{
	struct cpuset *cs;		/* current cpuset ancestors */
	int allowed;			/* is allocation in zone z allowed? */
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
 * cpuset_mem_spread_node() - On which node to begin search for a file page
 * cpuset_slab_spread_node() - On which node to begin search for a slab page
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

int cpuset_mem_spread_node(void)
{
	if (current->cpuset_mem_spread_rotor == NUMA_NO_NODE)
		current->cpuset_mem_spread_rotor =
			node_random(&current->mems_allowed);

	return cpuset_spread_node(&current->cpuset_mem_spread_rotor);
}

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

/**
 * cpuset_memory_pressure_bump - keep stats of per-cpuset reclaims.
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
 **/

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
 *    and we take cpuset_rwsem, keeping cpuset_attach() from changing it
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
	if (retval >= PATH_MAX)
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
