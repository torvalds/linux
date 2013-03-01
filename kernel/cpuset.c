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
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/backing-dev.h>
#include <linux/sort.h>

#include <asm/uaccess.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/cgroup.h>

/*
 * Tracks how many cpusets are currently defined in system.
 * When there is only one cpuset (the root cpuset) we can
 * short circuit some hooks.
 */
int number_of_cpusets __read_mostly;

/* Forward declare cgroup structures */
struct cgroup_subsys cpuset_subsys;
struct cpuset;

/* See "Frequency meter" comments, below. */

struct fmeter {
	int cnt;		/* unprocessed events count */
	int val;		/* most recent output value */
	time_t time;		/* clock (secs) when val computed */
	spinlock_t lock;	/* guards read or write of above */
};

struct cpuset {
	struct cgroup_subsys_state css;

	unsigned long flags;		/* "unsigned long" so bitops work */
	cpumask_var_t cpus_allowed;	/* CPUs allowed to tasks in cpuset */
	nodemask_t mems_allowed;	/* Memory Nodes allowed to tasks */

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

	struct work_struct hotplug_work;
};

/* Retrieve the cpuset for a cgroup */
static inline struct cpuset *cgroup_cs(struct cgroup *cont)
{
	return container_of(cgroup_subsys_state(cont, cpuset_subsys_id),
			    struct cpuset, css);
}

/* Retrieve the cpuset for a task */
static inline struct cpuset *task_cs(struct task_struct *task)
{
	return container_of(task_subsys_state(task, cpuset_subsys_id),
			    struct cpuset, css);
}

static inline struct cpuset *parent_cs(const struct cpuset *cs)
{
	struct cgroup *pcgrp = cs->css.cgroup->parent;

	if (pcgrp)
		return cgroup_cs(pcgrp);
	return NULL;
}

#ifdef CONFIG_NUMA
static inline bool task_has_mempolicy(struct task_struct *task)
{
	return task->mempolicy;
}
#else
static inline bool task_has_mempolicy(struct task_struct *task)
{
	return false;
}
#endif


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
static inline bool is_cpuset_online(const struct cpuset *cs)
{
	return test_bit(CS_ONLINE, &cs->flags);
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

static struct cpuset top_cpuset = {
	.flags = ((1 << CS_ONLINE) | (1 << CS_CPU_EXCLUSIVE) |
		  (1 << CS_MEM_EXCLUSIVE)),
};

/**
 * cpuset_for_each_child - traverse online children of a cpuset
 * @child_cs: loop cursor pointing to the current child
 * @pos_cgrp: used for iteration
 * @parent_cs: target cpuset to walk children of
 *
 * Walk @child_cs through the online children of @parent_cs.  Must be used
 * with RCU read locked.
 */
#define cpuset_for_each_child(child_cs, pos_cgrp, parent_cs)		\
	cgroup_for_each_child((pos_cgrp), (parent_cs)->css.cgroup)	\
		if (is_cpuset_online(((child_cs) = cgroup_cs((pos_cgrp)))))

/**
 * cpuset_for_each_descendant_pre - pre-order walk of a cpuset's descendants
 * @des_cs: loop cursor pointing to the current descendant
 * @pos_cgrp: used for iteration
 * @root_cs: target cpuset to walk ancestor of
 *
 * Walk @des_cs through the online descendants of @root_cs.  Must be used
 * with RCU read locked.  The caller may modify @pos_cgrp by calling
 * cgroup_rightmost_descendant() to skip subtree.
 */
#define cpuset_for_each_descendant_pre(des_cs, pos_cgrp, root_cs)	\
	cgroup_for_each_descendant_pre((pos_cgrp), (root_cs)->css.cgroup) \
		if (is_cpuset_online(((des_cs) = cgroup_cs((pos_cgrp)))))

/*
 * There are two global mutexes guarding cpuset structures - cpuset_mutex
 * and callback_mutex.  The latter may nest inside the former.  We also
 * require taking task_lock() when dereferencing a task's cpuset pointer.
 * See "The task_lock() exception", at the end of this comment.
 *
 * A task must hold both mutexes to modify cpusets.  If a task holds
 * cpuset_mutex, then it blocks others wanting that mutex, ensuring that it
 * is the only task able to also acquire callback_mutex and be able to
 * modify cpusets.  It can perform various checks on the cpuset structure
 * first, knowing nothing will change.  It can also allocate memory while
 * just holding cpuset_mutex.  While it is performing these checks, various
 * callback routines can briefly acquire callback_mutex to query cpusets.
 * Once it is ready to make the changes, it takes callback_mutex, blocking
 * everyone else.
 *
 * Calls to the kernel memory allocator can not be made while holding
 * callback_mutex, as that would risk double tripping on callback_mutex
 * from one of the callbacks into the cpuset code from within
 * __alloc_pages().
 *
 * If a task is only holding callback_mutex, then it has read-only
 * access to cpusets.
 *
 * Now, the task_struct fields mems_allowed and mempolicy may be changed
 * by other task, we use alloc_lock in the task_struct fields to protect
 * them.
 *
 * The cpuset_common_file_read() handlers only hold callback_mutex across
 * small pieces of code, such as when reading out possibly multi-word
 * cpumasks and nodemasks.
 *
 * Accessing a task's cpuset should be done in accordance with the
 * guidelines for accessing subsystem state in kernel/cgroup.c
 */

static DEFINE_MUTEX(cpuset_mutex);
static DEFINE_MUTEX(callback_mutex);

/*
 * CPU / memory hotplug is handled asynchronously.
 */
static struct workqueue_struct *cpuset_propagate_hotplug_wq;

static void cpuset_hotplug_workfn(struct work_struct *work);
static void cpuset_propagate_hotplug_workfn(struct work_struct *work);
static void schedule_cpuset_propagate_hotplug(struct cpuset *cs);

static DECLARE_WORK(cpuset_hotplug_work, cpuset_hotplug_workfn);

/*
 * This is ugly, but preserves the userspace API for existing cpuset
 * users. If someone tries to mount the "cpuset" filesystem, we
 * silently switch it to mount "cgroup" instead
 */
static struct dentry *cpuset_mount(struct file_system_type *fs_type,
			 int flags, const char *unused_dev_name, void *data)
{
	struct file_system_type *cgroup_fs = get_fs_type("cgroup");
	struct dentry *ret = ERR_PTR(-ENODEV);
	if (cgroup_fs) {
		char mountopts[] =
			"cpuset,noprefix,"
			"release_agent=/sbin/cpuset_release_agent";
		ret = cgroup_fs->mount(cgroup_fs, flags,
					   unused_dev_name, mountopts);
		put_filesystem(cgroup_fs);
	}
	return ret;
}

static struct file_system_type cpuset_fs_type = {
	.name = "cpuset",
	.mount = cpuset_mount,
};

/*
 * Return in pmask the portion of a cpusets's cpus_allowed that
 * are online.  If none are online, walk up the cpuset hierarchy
 * until we find one that does have some online cpus.  If we get
 * all the way to the top and still haven't found any online cpus,
 * return cpu_online_mask.  Or if passed a NULL cs from an exit'ing
 * task, return cpu_online_mask.
 *
 * One way or another, we guarantee to return some non-empty subset
 * of cpu_online_mask.
 *
 * Call with callback_mutex held.
 */

static void guarantee_online_cpus(const struct cpuset *cs,
				  struct cpumask *pmask)
{
	while (cs && !cpumask_intersects(cs->cpus_allowed, cpu_online_mask))
		cs = parent_cs(cs);
	if (cs)
		cpumask_and(pmask, cs->cpus_allowed, cpu_online_mask);
	else
		cpumask_copy(pmask, cpu_online_mask);
	BUG_ON(!cpumask_intersects(pmask, cpu_online_mask));
}

/*
 * Return in *pmask the portion of a cpusets's mems_allowed that
 * are online, with memory.  If none are online with memory, walk
 * up the cpuset hierarchy until we find one that does have some
 * online mems.  If we get all the way to the top and still haven't
 * found any online mems, return node_states[N_MEMORY].
 *
 * One way or another, we guarantee to return some non-empty subset
 * of node_states[N_MEMORY].
 *
 * Call with callback_mutex held.
 */

static void guarantee_online_mems(const struct cpuset *cs, nodemask_t *pmask)
{
	while (cs && !nodes_intersects(cs->mems_allowed,
					node_states[N_MEMORY]))
		cs = parent_cs(cs);
	if (cs)
		nodes_and(*pmask, cs->mems_allowed,
					node_states[N_MEMORY]);
	else
		*pmask = node_states[N_MEMORY];
	BUG_ON(!nodes_intersects(*pmask, node_states[N_MEMORY]));
}

/*
 * update task's spread flag if cpuset's page/slab spread flag is set
 *
 * Called with callback_mutex/cpuset_mutex held
 */
static void cpuset_update_task_spread_flag(struct cpuset *cs,
					struct task_struct *tsk)
{
	if (is_spread_page(cs))
		tsk->flags |= PF_SPREAD_PAGE;
	else
		tsk->flags &= ~PF_SPREAD_PAGE;
	if (is_spread_slab(cs))
		tsk->flags |= PF_SPREAD_SLAB;
	else
		tsk->flags &= ~PF_SPREAD_SLAB;
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
 * alloc_trial_cpuset - allocate a trial cpuset
 * @cs: the cpuset that the trial cpuset duplicates
 */
static struct cpuset *alloc_trial_cpuset(const struct cpuset *cs)
{
	struct cpuset *trial;

	trial = kmemdup(cs, sizeof(*cs), GFP_KERNEL);
	if (!trial)
		return NULL;

	if (!alloc_cpumask_var(&trial->cpus_allowed, GFP_KERNEL)) {
		kfree(trial);
		return NULL;
	}
	cpumask_copy(trial->cpus_allowed, cs->cpus_allowed);

	return trial;
}

/**
 * free_trial_cpuset - free the trial cpuset
 * @trial: the trial cpuset to be freed
 */
static void free_trial_cpuset(struct cpuset *trial)
{
	free_cpumask_var(trial->cpus_allowed);
	kfree(trial);
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

static int validate_change(const struct cpuset *cur, const struct cpuset *trial)
{
	struct cgroup *cont;
	struct cpuset *c, *par;
	int ret;

	rcu_read_lock();

	/* Each of our child cpusets must be a subset of us */
	ret = -EBUSY;
	cpuset_for_each_child(c, cont, cur)
		if (!is_cpuset_subset(c, trial))
			goto out;

	/* Remaining checks don't apply to root cpuset */
	ret = 0;
	if (cur == &top_cpuset)
		goto out;

	par = parent_cs(cur);

	/* We must be a subset of our parent cpuset */
	ret = -EACCES;
	if (!is_cpuset_subset(trial, par))
		goto out;

	/*
	 * If either I or some sibling (!= me) is exclusive, we can't
	 * overlap
	 */
	ret = -EINVAL;
	cpuset_for_each_child(c, cont, par) {
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
	 * have empty cpus_allowed or mems_allowed.
	 */
	ret = -ENOSPC;
	if ((cgroup_task_count(cur->css.cgroup) || cur->attach_in_progress) &&
	    (cpumask_empty(trial->cpus_allowed) ||
	     nodes_empty(trial->mems_allowed)))
		goto out;

	ret = 0;
out:
	rcu_read_unlock();
	return ret;
}

#ifdef CONFIG_SMP
/*
 * Helper routine for generate_sched_domains().
 * Do cpusets a, b have overlapping cpus_allowed masks?
 */
static int cpusets_overlap(struct cpuset *a, struct cpuset *b)
{
	return cpumask_intersects(a->cpus_allowed, b->cpus_allowed);
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
	struct cgroup *pos_cgrp;

	rcu_read_lock();
	cpuset_for_each_descendant_pre(cp, pos_cgrp, root_cs) {
		/* skip the whole subtree if @cp doesn't have any CPU */
		if (cpumask_empty(cp->cpus_allowed)) {
			pos_cgrp = cgroup_rightmost_descendant(pos_cgrp);
			continue;
		}

		if (is_sched_load_balance(cp))
			update_domain_attr(dattr, cp);
	}
	rcu_read_unlock();
}

/*
 * generate_sched_domains()
 *
 * This function builds a partial partition of the systems CPUs
 * A 'partial partition' is a set of non-overlapping subsets whose
 * union is a subset of that set.
 * The output of this function needs to be passed to kernel/sched.c
 * partition_sched_domains() routine, which will rebuild the scheduler's
 * load balancing domains (sched domains) as specified by that partial
 * partition.
 *
 * See "What is sched_load_balance" in Documentation/cgroups/cpusets.txt
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
 *    q  - a linked-list queue of cpuset pointers, used to implement a
 *	   top-down scan of all cpusets.  This scan loads a pointer
 *	   to each cpuset marked is_sched_load_balance into the
 *	   array 'csa'.  For our purposes, rebuilding the schedulers
 *	   sched domains, we can ignore !is_sched_load_balance cpusets.
 *  csa  - (for CpuSet Array) Array of pointers to all the cpusets
 *	   that need to be load balanced, for convenient iterative
 *	   access by the subsequent code that finds the best partition,
 *	   i.e the set of domains (subsets) of CPUs such that the
 *	   cpus_allowed of every cpuset marked is_sched_load_balance
 *	   is a subset of one of these domains, while there are as
 *	   many such domains as possible, each as small as possible.
 * doms  - Conversion of 'csa' to an array of cpumasks, for passing to
 *	   the kernel/sched.c routine partition_sched_domains() in a
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
	struct cpuset *cp;	/* scans q */
	struct cpuset **csa;	/* array of all cpuset ptrs */
	int csn;		/* how many cpuset ptrs in csa so far */
	int i, j, k;		/* indices for partition finding loops */
	cpumask_var_t *doms;	/* resulting partition; i.e. sched domains */
	struct sched_domain_attr *dattr;  /* attributes for custom domains */
	int ndoms = 0;		/* number of sched domains in result */
	int nslot;		/* next empty doms[] struct cpumask slot */
	struct cgroup *pos_cgrp;

	doms = NULL;
	dattr = NULL;
	csa = NULL;

	/* Special case for the 99% of systems with one, full, sched domain */
	if (is_sched_load_balance(&top_cpuset)) {
		ndoms = 1;
		doms = alloc_sched_domains(ndoms);
		if (!doms)
			goto done;

		dattr = kmalloc(sizeof(struct sched_domain_attr), GFP_KERNEL);
		if (dattr) {
			*dattr = SD_ATTR_INIT;
			update_domain_attr_tree(dattr, &top_cpuset);
		}
		cpumask_copy(doms[0], top_cpuset.cpus_allowed);

		goto done;
	}

	csa = kmalloc(number_of_cpusets * sizeof(cp), GFP_KERNEL);
	if (!csa)
		goto done;
	csn = 0;

	rcu_read_lock();
	cpuset_for_each_descendant_pre(cp, pos_cgrp, &top_cpuset) {
		/*
		 * Continue traversing beyond @cp iff @cp has some CPUs and
		 * isn't load balancing.  The former is obvious.  The
		 * latter: All child cpusets contain a subset of the
		 * parent's cpus, so just skip them, and then we call
		 * update_domain_attr_tree() to calc relax_domain_level of
		 * the corresponding sched domain.
		 */
		if (!cpumask_empty(cp->cpus_allowed) &&
		    !is_sched_load_balance(cp))
			continue;

		if (is_sched_load_balance(cp))
			csa[csn++] = cp;

		/* skip @cp's subtree */
		pos_cgrp = cgroup_rightmost_descendant(pos_cgrp);
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
	dattr = kmalloc(ndoms * sizeof(struct sched_domain_attr), GFP_KERNEL);

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
				printk(KERN_WARNING
				 "rebuild_sched_domains confused:"
				  " nslot %d, ndoms %d, csn %d, i %d,"
				  " apn %d\n",
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
				cpumask_or(dp, dp, b->cpus_allowed);
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

/*
 * Rebuild scheduler domains.
 *
 * If the flag 'sched_load_balance' of any cpuset with non-empty
 * 'cpus' changes, or if the 'cpus' allowed changes in any cpuset
 * which has that flag enabled, or if any cpuset with a non-empty
 * 'cpus' is removed, then call this routine to rebuild the
 * scheduler's dynamic sched domains.
 *
 * Call with cpuset_mutex held.  Takes get_online_cpus().
 */
static void rebuild_sched_domains_locked(void)
{
	struct sched_domain_attr *attr;
	cpumask_var_t *doms;
	int ndoms;

	lockdep_assert_held(&cpuset_mutex);
	get_online_cpus();

	/* Generate domain masks and attrs */
	ndoms = generate_sched_domains(&doms, &attr);

	/* Have scheduler rebuild the domains */
	partition_sched_domains(ndoms, doms, attr);

	put_online_cpus();
}
#else /* !CONFIG_SMP */
static void rebuild_sched_domains_locked(void)
{
}

static int generate_sched_domains(cpumask_var_t **domains,
			struct sched_domain_attr **attributes)
{
	*domains = NULL;
	return 1;
}
#endif /* CONFIG_SMP */

void rebuild_sched_domains(void)
{
	mutex_lock(&cpuset_mutex);
	rebuild_sched_domains_locked();
	mutex_unlock(&cpuset_mutex);
}

/**
 * cpuset_test_cpumask - test a task's cpus_allowed versus its cpuset's
 * @tsk: task to test
 * @scan: struct cgroup_scanner contained in its struct cpuset_hotplug_scanner
 *
 * Call with cpuset_mutex held.  May take callback_mutex during call.
 * Called for each task in a cgroup by cgroup_scan_tasks().
 * Return nonzero if this tasks's cpus_allowed mask should be changed (in other
 * words, if its mask is not equal to its cpuset's mask).
 */
static int cpuset_test_cpumask(struct task_struct *tsk,
			       struct cgroup_scanner *scan)
{
	return !cpumask_equal(&tsk->cpus_allowed,
			(cgroup_cs(scan->cg))->cpus_allowed);
}

/**
 * cpuset_change_cpumask - make a task's cpus_allowed the same as its cpuset's
 * @tsk: task to test
 * @scan: struct cgroup_scanner containing the cgroup of the task
 *
 * Called by cgroup_scan_tasks() for each task in a cgroup whose
 * cpus_allowed mask needs to be changed.
 *
 * We don't need to re-check for the cgroup/cpuset membership, since we're
 * holding cpuset_mutex at this point.
 */
static void cpuset_change_cpumask(struct task_struct *tsk,
				  struct cgroup_scanner *scan)
{
	set_cpus_allowed_ptr(tsk, ((cgroup_cs(scan->cg))->cpus_allowed));
}

/**
 * update_tasks_cpumask - Update the cpumasks of tasks in the cpuset.
 * @cs: the cpuset in which each task's cpus_allowed mask needs to be changed
 * @heap: if NULL, defer allocating heap memory to cgroup_scan_tasks()
 *
 * Called with cpuset_mutex held
 *
 * The cgroup_scan_tasks() function will scan all the tasks in a cgroup,
 * calling callback functions for each.
 *
 * No return value. It's guaranteed that cgroup_scan_tasks() always returns 0
 * if @heap != NULL.
 */
static void update_tasks_cpumask(struct cpuset *cs, struct ptr_heap *heap)
{
	struct cgroup_scanner scan;

	scan.cg = cs->css.cgroup;
	scan.test_task = cpuset_test_cpumask;
	scan.process_task = cpuset_change_cpumask;
	scan.heap = heap;
	cgroup_scan_tasks(&scan);
}

/**
 * update_cpumask - update the cpus_allowed mask of a cpuset and all tasks in it
 * @cs: the cpuset to consider
 * @buf: buffer of cpu numbers written to this cpuset
 */
static int update_cpumask(struct cpuset *cs, struct cpuset *trialcs,
			  const char *buf)
{
	struct ptr_heap heap;
	int retval;
	int is_load_balanced;

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

		if (!cpumask_subset(trialcs->cpus_allowed, cpu_active_mask))
			return -EINVAL;
	}
	retval = validate_change(cs, trialcs);
	if (retval < 0)
		return retval;

	/* Nothing to do if the cpus didn't change */
	if (cpumask_equal(cs->cpus_allowed, trialcs->cpus_allowed))
		return 0;

	retval = heap_init(&heap, PAGE_SIZE, GFP_KERNEL, NULL);
	if (retval)
		return retval;

	is_load_balanced = is_sched_load_balance(trialcs);

	mutex_lock(&callback_mutex);
	cpumask_copy(cs->cpus_allowed, trialcs->cpus_allowed);
	mutex_unlock(&callback_mutex);

	/*
	 * Scan tasks in the cpuset, and update the cpumasks of any
	 * that need an update.
	 */
	update_tasks_cpumask(cs, &heap);

	heap_free(&heap);

	if (is_load_balanced)
		rebuild_sched_domains_locked();
	return 0;
}

/*
 * cpuset_migrate_mm
 *
 *    Migrate memory region from one set of nodes to another.
 *
 *    Temporarilly set tasks mems_allowed to target nodes of migration,
 *    so that the migration code can allocate pages on these nodes.
 *
 *    Call holding cpuset_mutex, so current's cpuset won't change
 *    during this call, as manage_mutex holds off any cpuset_attach()
 *    calls.  Therefore we don't need to take task_lock around the
 *    call to guarantee_online_mems(), as we know no one is changing
 *    our task's cpuset.
 *
 *    While the mm_struct we are migrating is typically from some
 *    other task, the task_struct mems_allowed that we are hacking
 *    is for our current task, which must allocate new pages for that
 *    migrating memory region.
 */

static void cpuset_migrate_mm(struct mm_struct *mm, const nodemask_t *from,
							const nodemask_t *to)
{
	struct task_struct *tsk = current;

	tsk->mems_allowed = *to;

	do_migrate_pages(mm, from, to, MPOL_MF_MOVE_ALL);

	guarantee_online_mems(task_cs(tsk),&tsk->mems_allowed);
}

/*
 * cpuset_change_task_nodemask - change task's mems_allowed and mempolicy
 * @tsk: the task to change
 * @newmems: new nodes that the task will be set
 *
 * In order to avoid seeing no nodes if the old and new nodes are disjoint,
 * we structure updates as setting all new allowed nodes, then clearing newly
 * disallowed ones.
 */
static void cpuset_change_task_nodemask(struct task_struct *tsk,
					nodemask_t *newmems)
{
	bool need_loop;

	/*
	 * Allow tasks that have access to memory reserves because they have
	 * been OOM killed to get memory anywhere.
	 */
	if (unlikely(test_thread_flag(TIF_MEMDIE)))
		return;
	if (current->flags & PF_EXITING) /* Let dying task have memory */
		return;

	task_lock(tsk);
	/*
	 * Determine if a loop is necessary if another thread is doing
	 * get_mems_allowed().  If at least one node remains unchanged and
	 * tsk does not have a mempolicy, then an empty nodemask will not be
	 * possible when mems_allowed is larger than a word.
	 */
	need_loop = task_has_mempolicy(tsk) ||
			!nodes_intersects(*newmems, tsk->mems_allowed);

	if (need_loop)
		write_seqcount_begin(&tsk->mems_allowed_seq);

	nodes_or(tsk->mems_allowed, tsk->mems_allowed, *newmems);
	mpol_rebind_task(tsk, newmems, MPOL_REBIND_STEP1);

	mpol_rebind_task(tsk, newmems, MPOL_REBIND_STEP2);
	tsk->mems_allowed = *newmems;

	if (need_loop)
		write_seqcount_end(&tsk->mems_allowed_seq);

	task_unlock(tsk);
}

/*
 * Update task's mems_allowed and rebind its mempolicy and vmas' mempolicy
 * of it to cpuset's new mems_allowed, and migrate pages to new nodes if
 * memory_migrate flag is set. Called with cpuset_mutex held.
 */
static void cpuset_change_nodemask(struct task_struct *p,
				   struct cgroup_scanner *scan)
{
	struct mm_struct *mm;
	struct cpuset *cs;
	int migrate;
	const nodemask_t *oldmem = scan->data;
	static nodemask_t newmems;	/* protected by cpuset_mutex */

	cs = cgroup_cs(scan->cg);
	guarantee_online_mems(cs, &newmems);

	cpuset_change_task_nodemask(p, &newmems);

	mm = get_task_mm(p);
	if (!mm)
		return;

	migrate = is_memory_migrate(cs);

	mpol_rebind_mm(mm, &cs->mems_allowed);
	if (migrate)
		cpuset_migrate_mm(mm, oldmem, &cs->mems_allowed);
	mmput(mm);
}

static void *cpuset_being_rebound;

/**
 * update_tasks_nodemask - Update the nodemasks of tasks in the cpuset.
 * @cs: the cpuset in which each task's mems_allowed mask needs to be changed
 * @oldmem: old mems_allowed of cpuset cs
 * @heap: if NULL, defer allocating heap memory to cgroup_scan_tasks()
 *
 * Called with cpuset_mutex held
 * No return value. It's guaranteed that cgroup_scan_tasks() always returns 0
 * if @heap != NULL.
 */
static void update_tasks_nodemask(struct cpuset *cs, const nodemask_t *oldmem,
				 struct ptr_heap *heap)
{
	struct cgroup_scanner scan;

	cpuset_being_rebound = cs;		/* causes mpol_dup() rebind */

	scan.cg = cs->css.cgroup;
	scan.test_task = NULL;
	scan.process_task = cpuset_change_nodemask;
	scan.heap = heap;
	scan.data = (nodemask_t *)oldmem;

	/*
	 * The mpol_rebind_mm() call takes mmap_sem, which we couldn't
	 * take while holding tasklist_lock.  Forks can happen - the
	 * mpol_dup() cpuset_being_rebound check will catch such forks,
	 * and rebind their vma mempolicies too.  Because we still hold
	 * the global cpuset_mutex, we know that no other rebind effort
	 * will be contending for the global variable cpuset_being_rebound.
	 * It's ok if we rebind the same mm twice; mpol_rebind_mm()
	 * is idempotent.  Also migrate pages in each mm to new nodes.
	 */
	cgroup_scan_tasks(&scan);

	/* We're done rebinding vmas to this cpuset's new mems_allowed. */
	cpuset_being_rebound = NULL;
}

/*
 * Handle user request to change the 'mems' memory placement
 * of a cpuset.  Needs to validate the request, update the
 * cpusets mems_allowed, and for each task in the cpuset,
 * update mems_allowed and rebind task's mempolicy and any vma
 * mempolicies and if the cpuset is marked 'memory_migrate',
 * migrate the tasks pages to the new memory.
 *
 * Call with cpuset_mutex held.  May take callback_mutex during call.
 * Will take tasklist_lock, scan tasklist for tasks in cpuset cs,
 * lock each such tasks mm->mmap_sem, scan its vma's and rebind
 * their mempolicies to the cpusets new mems_allowed.
 */
static int update_nodemask(struct cpuset *cs, struct cpuset *trialcs,
			   const char *buf)
{
	NODEMASK_ALLOC(nodemask_t, oldmem, GFP_KERNEL);
	int retval;
	struct ptr_heap heap;

	if (!oldmem)
		return -ENOMEM;

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
				node_states[N_MEMORY])) {
			retval =  -EINVAL;
			goto done;
		}
	}
	*oldmem = cs->mems_allowed;
	if (nodes_equal(*oldmem, trialcs->mems_allowed)) {
		retval = 0;		/* Too easy - nothing to do */
		goto done;
	}
	retval = validate_change(cs, trialcs);
	if (retval < 0)
		goto done;

	retval = heap_init(&heap, PAGE_SIZE, GFP_KERNEL, NULL);
	if (retval < 0)
		goto done;

	mutex_lock(&callback_mutex);
	cs->mems_allowed = trialcs->mems_allowed;
	mutex_unlock(&callback_mutex);

	update_tasks_nodemask(cs, oldmem, &heap);

	heap_free(&heap);
done:
	NODEMASK_FREE(oldmem);
	return retval;
}

int current_cpuset_is_being_rebound(void)
{
	return task_cs(current) == cpuset_being_rebound;
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

/*
 * cpuset_change_flag - make a task's spread flags the same as its cpuset's
 * @tsk: task to be updated
 * @scan: struct cgroup_scanner containing the cgroup of the task
 *
 * Called by cgroup_scan_tasks() for each task in a cgroup.
 *
 * We don't need to re-check for the cgroup/cpuset membership, since we're
 * holding cpuset_mutex at this point.
 */
static void cpuset_change_flag(struct task_struct *tsk,
				struct cgroup_scanner *scan)
{
	cpuset_update_task_spread_flag(cgroup_cs(scan->cg), tsk);
}

/*
 * update_tasks_flags - update the spread flags of tasks in the cpuset.
 * @cs: the cpuset in which each task's spread flags needs to be changed
 * @heap: if NULL, defer allocating heap memory to cgroup_scan_tasks()
 *
 * Called with cpuset_mutex held
 *
 * The cgroup_scan_tasks() function will scan all the tasks in a cgroup,
 * calling callback functions for each.
 *
 * No return value. It's guaranteed that cgroup_scan_tasks() always returns 0
 * if @heap != NULL.
 */
static void update_tasks_flags(struct cpuset *cs, struct ptr_heap *heap)
{
	struct cgroup_scanner scan;

	scan.cg = cs->css.cgroup;
	scan.test_task = NULL;
	scan.process_task = cpuset_change_flag;
	scan.heap = heap;
	cgroup_scan_tasks(&scan);
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
	struct ptr_heap heap;
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

	err = heap_init(&heap, PAGE_SIZE, GFP_KERNEL, NULL);
	if (err < 0)
		goto out;

	balance_flag_changed = (is_sched_load_balance(cs) !=
				is_sched_load_balance(trialcs));

	spread_flag_changed = ((is_spread_slab(cs) != is_spread_slab(trialcs))
			|| (is_spread_page(cs) != is_spread_page(trialcs)));

	mutex_lock(&callback_mutex);
	cs->flags = trialcs->flags;
	mutex_unlock(&callback_mutex);

	if (!cpumask_empty(trialcs->cpus_allowed) && balance_flag_changed)
		rebuild_sched_domains_locked();

	if (spread_flag_changed)
		update_tasks_flags(cs, &heap);
	heap_free(&heap);
out:
	free_trial_cpuset(trialcs);
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
#define FM_MAXTICKS ((time_t)99) /* useless computing more ticks than this */
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
	time_t now = get_seconds();
	time_t ticks = now - fmp->time;

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

/* Called by cgroups to determine if a cpuset is usable; cpuset_mutex held */
static int cpuset_can_attach(struct cgroup *cgrp, struct cgroup_taskset *tset)
{
	struct cpuset *cs = cgroup_cs(cgrp);
	struct task_struct *task;
	int ret;

	mutex_lock(&cpuset_mutex);

	ret = -ENOSPC;
	if (cpumask_empty(cs->cpus_allowed) || nodes_empty(cs->mems_allowed))
		goto out_unlock;

	cgroup_taskset_for_each(task, cgrp, tset) {
		/*
		 * Kthreads bound to specific cpus cannot be moved to a new
		 * cpuset; we cannot change their cpu affinity and
		 * isolating such threads by their set of allowed nodes is
		 * unnecessary.  Thus, cpusets are not applicable for such
		 * threads.  This prevents checking for success of
		 * set_cpus_allowed_ptr() on all attached tasks before
		 * cpus_allowed may be changed.
		 */
		ret = -EINVAL;
		if (task->flags & PF_THREAD_BOUND)
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
	mutex_unlock(&cpuset_mutex);
	return ret;
}

static void cpuset_cancel_attach(struct cgroup *cgrp,
				 struct cgroup_taskset *tset)
{
	mutex_lock(&cpuset_mutex);
	cgroup_cs(cgrp)->attach_in_progress--;
	mutex_unlock(&cpuset_mutex);
}

/*
 * Protected by cpuset_mutex.  cpus_attach is used only by cpuset_attach()
 * but we can't allocate it dynamically there.  Define it global and
 * allocate from cpuset_init().
 */
static cpumask_var_t cpus_attach;

static void cpuset_attach(struct cgroup *cgrp, struct cgroup_taskset *tset)
{
	/* static bufs protected by cpuset_mutex */
	static nodemask_t cpuset_attach_nodemask_from;
	static nodemask_t cpuset_attach_nodemask_to;
	struct mm_struct *mm;
	struct task_struct *task;
	struct task_struct *leader = cgroup_taskset_first(tset);
	struct cgroup *oldcgrp = cgroup_taskset_cur_cgroup(tset);
	struct cpuset *cs = cgroup_cs(cgrp);
	struct cpuset *oldcs = cgroup_cs(oldcgrp);

	mutex_lock(&cpuset_mutex);

	/* prepare for attach */
	if (cs == &top_cpuset)
		cpumask_copy(cpus_attach, cpu_possible_mask);
	else
		guarantee_online_cpus(cs, cpus_attach);

	guarantee_online_mems(cs, &cpuset_attach_nodemask_to);

	cgroup_taskset_for_each(task, cgrp, tset) {
		/*
		 * can_attach beforehand should guarantee that this doesn't
		 * fail.  TODO: have a better way to handle failure here
		 */
		WARN_ON_ONCE(set_cpus_allowed_ptr(task, cpus_attach));

		cpuset_change_task_nodemask(task, &cpuset_attach_nodemask_to);
		cpuset_update_task_spread_flag(cs, task);
	}

	/*
	 * Change mm, possibly for multiple threads in a threadgroup. This is
	 * expensive and may sleep.
	 */
	cpuset_attach_nodemask_from = oldcs->mems_allowed;
	cpuset_attach_nodemask_to = cs->mems_allowed;
	mm = get_task_mm(leader);
	if (mm) {
		mpol_rebind_mm(mm, &cpuset_attach_nodemask_to);
		if (is_memory_migrate(cs))
			cpuset_migrate_mm(mm, &cpuset_attach_nodemask_from,
					  &cpuset_attach_nodemask_to);
		mmput(mm);
	}

	cs->attach_in_progress--;

	/*
	 * We may have raced with CPU/memory hotunplug.  Trigger hotplug
	 * propagation if @cs doesn't have any CPU or memory.  It will move
	 * the newly added tasks to the nearest parent which can execute.
	 */
	if (cpumask_empty(cs->cpus_allowed) || nodes_empty(cs->mems_allowed))
		schedule_cpuset_propagate_hotplug(cs);

	mutex_unlock(&cpuset_mutex);
}

/* The various types of files and directories in a cpuset file system */

typedef enum {
	FILE_MEMORY_MIGRATE,
	FILE_CPULIST,
	FILE_MEMLIST,
	FILE_CPU_EXCLUSIVE,
	FILE_MEM_EXCLUSIVE,
	FILE_MEM_HARDWALL,
	FILE_SCHED_LOAD_BALANCE,
	FILE_SCHED_RELAX_DOMAIN_LEVEL,
	FILE_MEMORY_PRESSURE_ENABLED,
	FILE_MEMORY_PRESSURE,
	FILE_SPREAD_PAGE,
	FILE_SPREAD_SLAB,
} cpuset_filetype_t;

static int cpuset_write_u64(struct cgroup *cgrp, struct cftype *cft, u64 val)
{
	struct cpuset *cs = cgroup_cs(cgrp);
	cpuset_filetype_t type = cft->private;
	int retval = -ENODEV;

	mutex_lock(&cpuset_mutex);
	if (!is_cpuset_online(cs))
		goto out_unlock;

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
	case FILE_MEMORY_PRESSURE:
		retval = -EACCES;
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
	return retval;
}

static int cpuset_write_s64(struct cgroup *cgrp, struct cftype *cft, s64 val)
{
	struct cpuset *cs = cgroup_cs(cgrp);
	cpuset_filetype_t type = cft->private;
	int retval = -ENODEV;

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
	return retval;
}

/*
 * Common handling for a write to a "cpus" or "mems" file.
 */
static int cpuset_write_resmask(struct cgroup *cgrp, struct cftype *cft,
				const char *buf)
{
	struct cpuset *cs = cgroup_cs(cgrp);
	struct cpuset *trialcs;
	int retval = -ENODEV;

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
	 * Flushing cpuset_hotplug_work is enough to synchronize against
	 * hotplug hanlding; however, cpuset_attach() may schedule
	 * propagation work directly.  Flush the workqueue too.
	 */
	flush_work(&cpuset_hotplug_work);
	flush_workqueue(cpuset_propagate_hotplug_wq);

	mutex_lock(&cpuset_mutex);
	if (!is_cpuset_online(cs))
		goto out_unlock;

	trialcs = alloc_trial_cpuset(cs);
	if (!trialcs) {
		retval = -ENOMEM;
		goto out_unlock;
	}

	switch (cft->private) {
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

	free_trial_cpuset(trialcs);
out_unlock:
	mutex_unlock(&cpuset_mutex);
	return retval;
}

/*
 * These ascii lists should be read in a single call, by using a user
 * buffer large enough to hold the entire map.  If read in smaller
 * chunks, there is no guarantee of atomicity.  Since the display format
 * used, list of ranges of sequential numbers, is variable length,
 * and since these maps can change value dynamically, one could read
 * gibberish by doing partial reads while a list was changing.
 * A single large read to a buffer that crosses a page boundary is
 * ok, because the result being copied to user land is not recomputed
 * across a page fault.
 */

static size_t cpuset_sprintf_cpulist(char *page, struct cpuset *cs)
{
	size_t count;

	mutex_lock(&callback_mutex);
	count = cpulist_scnprintf(page, PAGE_SIZE, cs->cpus_allowed);
	mutex_unlock(&callback_mutex);

	return count;
}

static size_t cpuset_sprintf_memlist(char *page, struct cpuset *cs)
{
	size_t count;

	mutex_lock(&callback_mutex);
	count = nodelist_scnprintf(page, PAGE_SIZE, cs->mems_allowed);
	mutex_unlock(&callback_mutex);

	return count;
}

static ssize_t cpuset_common_file_read(struct cgroup *cont,
				       struct cftype *cft,
				       struct file *file,
				       char __user *buf,
				       size_t nbytes, loff_t *ppos)
{
	struct cpuset *cs = cgroup_cs(cont);
	cpuset_filetype_t type = cft->private;
	char *page;
	ssize_t retval = 0;
	char *s;

	if (!(page = (char *)__get_free_page(GFP_TEMPORARY)))
		return -ENOMEM;

	s = page;

	switch (type) {
	case FILE_CPULIST:
		s += cpuset_sprintf_cpulist(s, cs);
		break;
	case FILE_MEMLIST:
		s += cpuset_sprintf_memlist(s, cs);
		break;
	default:
		retval = -EINVAL;
		goto out;
	}
	*s++ = '\n';

	retval = simple_read_from_buffer(buf, nbytes, ppos, page, s - page);
out:
	free_page((unsigned long)page);
	return retval;
}

static u64 cpuset_read_u64(struct cgroup *cont, struct cftype *cft)
{
	struct cpuset *cs = cgroup_cs(cont);
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

static s64 cpuset_read_s64(struct cgroup *cont, struct cftype *cft)
{
	struct cpuset *cs = cgroup_cs(cont);
	cpuset_filetype_t type = cft->private;
	switch (type) {
	case FILE_SCHED_RELAX_DOMAIN_LEVEL:
		return cs->relax_domain_level;
	default:
		BUG();
	}

	/* Unrechable but makes gcc happy */
	return 0;
}


/*
 * for the common functions, 'private' gives the type of file
 */

static struct cftype files[] = {
	{
		.name = "cpus",
		.read = cpuset_common_file_read,
		.write_string = cpuset_write_resmask,
		.max_write_len = (100U + 6 * NR_CPUS),
		.private = FILE_CPULIST,
	},

	{
		.name = "mems",
		.read = cpuset_common_file_read,
		.write_string = cpuset_write_resmask,
		.max_write_len = (100U + 6 * MAX_NUMNODES),
		.private = FILE_MEMLIST,
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
		.write_u64 = cpuset_write_u64,
		.private = FILE_MEMORY_PRESSURE,
		.mode = S_IRUGO,
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
 *	cpuset_css_alloc - allocate a cpuset css
 *	cont:	control group that the new cpuset will be part of
 */

static struct cgroup_subsys_state *cpuset_css_alloc(struct cgroup *cont)
{
	struct cpuset *cs;

	if (!cont->parent)
		return &top_cpuset.css;

	cs = kzalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs)
		return ERR_PTR(-ENOMEM);
	if (!alloc_cpumask_var(&cs->cpus_allowed, GFP_KERNEL)) {
		kfree(cs);
		return ERR_PTR(-ENOMEM);
	}

	set_bit(CS_SCHED_LOAD_BALANCE, &cs->flags);
	cpumask_clear(cs->cpus_allowed);
	nodes_clear(cs->mems_allowed);
	fmeter_init(&cs->fmeter);
	INIT_WORK(&cs->hotplug_work, cpuset_propagate_hotplug_workfn);
	cs->relax_domain_level = -1;

	return &cs->css;
}

static int cpuset_css_online(struct cgroup *cgrp)
{
	struct cpuset *cs = cgroup_cs(cgrp);
	struct cpuset *parent = parent_cs(cs);
	struct cpuset *tmp_cs;
	struct cgroup *pos_cg;

	if (!parent)
		return 0;

	mutex_lock(&cpuset_mutex);

	set_bit(CS_ONLINE, &cs->flags);
	if (is_spread_page(parent))
		set_bit(CS_SPREAD_PAGE, &cs->flags);
	if (is_spread_slab(parent))
		set_bit(CS_SPREAD_SLAB, &cs->flags);

	number_of_cpusets++;

	if (!test_bit(CGRP_CPUSET_CLONE_CHILDREN, &cgrp->flags))
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
	cpuset_for_each_child(tmp_cs, pos_cg, parent) {
		if (is_mem_exclusive(tmp_cs) || is_cpu_exclusive(tmp_cs)) {
			rcu_read_unlock();
			goto out_unlock;
		}
	}
	rcu_read_unlock();

	mutex_lock(&callback_mutex);
	cs->mems_allowed = parent->mems_allowed;
	cpumask_copy(cs->cpus_allowed, parent->cpus_allowed);
	mutex_unlock(&callback_mutex);
out_unlock:
	mutex_unlock(&cpuset_mutex);
	return 0;
}

static void cpuset_css_offline(struct cgroup *cgrp)
{
	struct cpuset *cs = cgroup_cs(cgrp);

	mutex_lock(&cpuset_mutex);

	if (is_sched_load_balance(cs))
		update_flag(CS_SCHED_LOAD_BALANCE, cs, 0);

	number_of_cpusets--;
	clear_bit(CS_ONLINE, &cs->flags);

	mutex_unlock(&cpuset_mutex);
}

/*
 * If the cpuset being removed has its flag 'sched_load_balance'
 * enabled, then simulate turning sched_load_balance off, which
 * will call rebuild_sched_domains_locked().
 */

static void cpuset_css_free(struct cgroup *cont)
{
	struct cpuset *cs = cgroup_cs(cont);

	free_cpumask_var(cs->cpus_allowed);
	kfree(cs);
}

struct cgroup_subsys cpuset_subsys = {
	.name = "cpuset",
	.css_alloc = cpuset_css_alloc,
	.css_online = cpuset_css_online,
	.css_offline = cpuset_css_offline,
	.css_free = cpuset_css_free,
	.can_attach = cpuset_can_attach,
	.cancel_attach = cpuset_cancel_attach,
	.attach = cpuset_attach,
	.subsys_id = cpuset_subsys_id,
	.base_cftypes = files,
	.early_init = 1,
};

/**
 * cpuset_init - initialize cpusets at system boot
 *
 * Description: Initialize top_cpuset and the cpuset internal file system,
 **/

int __init cpuset_init(void)
{
	int err = 0;

	if (!alloc_cpumask_var(&top_cpuset.cpus_allowed, GFP_KERNEL))
		BUG();

	cpumask_setall(top_cpuset.cpus_allowed);
	nodes_setall(top_cpuset.mems_allowed);

	fmeter_init(&top_cpuset.fmeter);
	set_bit(CS_SCHED_LOAD_BALANCE, &top_cpuset.flags);
	top_cpuset.relax_domain_level = -1;

	err = register_filesystem(&cpuset_fs_type);
	if (err < 0)
		return err;

	if (!alloc_cpumask_var(&cpus_attach, GFP_KERNEL))
		BUG();

	number_of_cpusets = 1;
	return 0;
}

/**
 * cpuset_do_move_task - move a given task to another cpuset
 * @tsk: pointer to task_struct the task to move
 * @scan: struct cgroup_scanner contained in its struct cpuset_hotplug_scanner
 *
 * Called by cgroup_scan_tasks() for each task in a cgroup.
 * Return nonzero to stop the walk through the tasks.
 */
static void cpuset_do_move_task(struct task_struct *tsk,
				struct cgroup_scanner *scan)
{
	struct cgroup *new_cgroup = scan->data;

	cgroup_lock();
	cgroup_attach_task(new_cgroup, tsk);
	cgroup_unlock();
}

/**
 * move_member_tasks_to_cpuset - move tasks from one cpuset to another
 * @from: cpuset in which the tasks currently reside
 * @to: cpuset to which the tasks will be moved
 *
 * Called with cpuset_mutex held
 * callback_mutex must not be held, as cpuset_attach() will take it.
 *
 * The cgroup_scan_tasks() function will scan all the tasks in a cgroup,
 * calling callback functions for each.
 */
static void move_member_tasks_to_cpuset(struct cpuset *from, struct cpuset *to)
{
	struct cgroup_scanner scan;

	scan.cg = from->css.cgroup;
	scan.test_task = NULL; /* select all tasks in cgroup */
	scan.process_task = cpuset_do_move_task;
	scan.heap = NULL;
	scan.data = to->css.cgroup;

	if (cgroup_scan_tasks(&scan))
		printk(KERN_ERR "move_member_tasks_to_cpuset: "
				"cgroup_scan_tasks failed\n");
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

	move_member_tasks_to_cpuset(cs, parent);
}

/**
 * cpuset_propagate_hotplug_workfn - propagate CPU/memory hotplug to a cpuset
 * @cs: cpuset in interest
 *
 * Compare @cs's cpu and mem masks against top_cpuset and if some have gone
 * offline, update @cs accordingly.  If @cs ends up with no CPU or memory,
 * all its tasks are moved to the nearest ancestor with both resources.
 */
static void cpuset_propagate_hotplug_workfn(struct work_struct *work)
{
	static cpumask_t off_cpus;
	static nodemask_t off_mems, tmp_mems;
	struct cpuset *cs = container_of(work, struct cpuset, hotplug_work);
	bool is_empty;

	mutex_lock(&cpuset_mutex);

	cpumask_andnot(&off_cpus, cs->cpus_allowed, top_cpuset.cpus_allowed);
	nodes_andnot(off_mems, cs->mems_allowed, top_cpuset.mems_allowed);

	/* remove offline cpus from @cs */
	if (!cpumask_empty(&off_cpus)) {
		mutex_lock(&callback_mutex);
		cpumask_andnot(cs->cpus_allowed, cs->cpus_allowed, &off_cpus);
		mutex_unlock(&callback_mutex);
		update_tasks_cpumask(cs, NULL);
	}

	/* remove offline mems from @cs */
	if (!nodes_empty(off_mems)) {
		tmp_mems = cs->mems_allowed;
		mutex_lock(&callback_mutex);
		nodes_andnot(cs->mems_allowed, cs->mems_allowed, off_mems);
		mutex_unlock(&callback_mutex);
		update_tasks_nodemask(cs, &tmp_mems, NULL);
	}

	is_empty = cpumask_empty(cs->cpus_allowed) ||
		nodes_empty(cs->mems_allowed);

	mutex_unlock(&cpuset_mutex);

	/*
	 * If @cs became empty, move tasks to the nearest ancestor with
	 * execution resources.  This is full cgroup operation which will
	 * also call back into cpuset.  Should be done outside any lock.
	 */
	if (is_empty)
		remove_tasks_in_empty_cpuset(cs);

	/* the following may free @cs, should be the last operation */
	css_put(&cs->css);
}

/**
 * schedule_cpuset_propagate_hotplug - schedule hotplug propagation to a cpuset
 * @cs: cpuset of interest
 *
 * Schedule cpuset_propagate_hotplug_workfn() which will update CPU and
 * memory masks according to top_cpuset.
 */
static void schedule_cpuset_propagate_hotplug(struct cpuset *cs)
{
	/*
	 * Pin @cs.  The refcnt will be released when the work item
	 * finishes executing.
	 */
	if (!css_tryget(&cs->css))
		return;

	/*
	 * Queue @cs->hotplug_work.  If already pending, lose the css ref.
	 * cpuset_propagate_hotplug_wq is ordered and propagation will
	 * happen in the order this function is called.
	 */
	if (!queue_work(cpuset_propagate_hotplug_wq, &cs->hotplug_work))
		css_put(&cs->css);
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
 * nodes have been taken down, cpuset_propagate_hotplug() is invoked on all
 * descendants.
 *
 * Note that CPU offlining during suspend is ignored.  We don't modify
 * cpusets across suspend/resume cycles at all.
 */
static void cpuset_hotplug_workfn(struct work_struct *work)
{
	static cpumask_t new_cpus, tmp_cpus;
	static nodemask_t new_mems, tmp_mems;
	bool cpus_updated, mems_updated;
	bool cpus_offlined, mems_offlined;

	mutex_lock(&cpuset_mutex);

	/* fetch the available cpus/mems and find out which changed how */
	cpumask_copy(&new_cpus, cpu_active_mask);
	new_mems = node_states[N_MEMORY];

	cpus_updated = !cpumask_equal(top_cpuset.cpus_allowed, &new_cpus);
	cpus_offlined = cpumask_andnot(&tmp_cpus, top_cpuset.cpus_allowed,
				       &new_cpus);

	mems_updated = !nodes_equal(top_cpuset.mems_allowed, new_mems);
	nodes_andnot(tmp_mems, top_cpuset.mems_allowed, new_mems);
	mems_offlined = !nodes_empty(tmp_mems);

	/* synchronize cpus_allowed to cpu_active_mask */
	if (cpus_updated) {
		mutex_lock(&callback_mutex);
		cpumask_copy(top_cpuset.cpus_allowed, &new_cpus);
		mutex_unlock(&callback_mutex);
		/* we don't mess with cpumasks of tasks in top_cpuset */
	}

	/* synchronize mems_allowed to N_MEMORY */
	if (mems_updated) {
		tmp_mems = top_cpuset.mems_allowed;
		mutex_lock(&callback_mutex);
		top_cpuset.mems_allowed = new_mems;
		mutex_unlock(&callback_mutex);
		update_tasks_nodemask(&top_cpuset, &tmp_mems, NULL);
	}

	/* if cpus or mems went down, we need to propagate to descendants */
	if (cpus_offlined || mems_offlined) {
		struct cpuset *cs;
		struct cgroup *pos_cgrp;

		rcu_read_lock();
		cpuset_for_each_descendant_pre(cs, pos_cgrp, &top_cpuset)
			schedule_cpuset_propagate_hotplug(cs);
		rcu_read_unlock();
	}

	mutex_unlock(&cpuset_mutex);

	/* wait for propagations to finish */
	flush_workqueue(cpuset_propagate_hotplug_wq);

	/* rebuild sched domains if cpus_allowed has changed */
	if (cpus_updated) {
		struct sched_domain_attr *attr;
		cpumask_var_t *doms;
		int ndoms;

		mutex_lock(&cpuset_mutex);
		ndoms = generate_sched_domains(&doms, &attr);
		mutex_unlock(&cpuset_mutex);

		partition_sched_domains(ndoms, doms, attr);
	}
}

void cpuset_update_active_cpus(bool cpu_online)
{
	/*
	 * We're inside cpu hotplug critical region which usually nests
	 * inside cgroup synchronization.  Bounce actual hotplug processing
	 * to a work item to avoid reverse locking order.
	 *
	 * We still need to do partition_sched_domains() synchronously;
	 * otherwise, the scheduler will get confused and put tasks to the
	 * dead CPU.  Fall back to the default single domain.
	 * cpuset_hotplug_workfn() will rebuild it as necessary.
	 */
	partition_sched_domains(1, NULL, NULL);
	schedule_work(&cpuset_hotplug_work);
}

#ifdef CONFIG_MEMORY_HOTPLUG
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
#endif

/**
 * cpuset_init_smp - initialize cpus_allowed
 *
 * Description: Finish top cpuset after cpu, node maps are initialized
 **/

void __init cpuset_init_smp(void)
{
	cpumask_copy(top_cpuset.cpus_allowed, cpu_active_mask);
	top_cpuset.mems_allowed = node_states[N_MEMORY];

	hotplug_memory_notifier(cpuset_track_online_nodes, 10);

	cpuset_propagate_hotplug_wq =
		alloc_ordered_workqueue("cpuset_hotplug", 0);
	BUG_ON(!cpuset_propagate_hotplug_wq);
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
	mutex_lock(&callback_mutex);
	task_lock(tsk);
	guarantee_online_cpus(task_cs(tsk), pmask);
	task_unlock(tsk);
	mutex_unlock(&callback_mutex);
}

void cpuset_cpus_allowed_fallback(struct task_struct *tsk)
{
	const struct cpuset *cs;

	rcu_read_lock();
	cs = task_cs(tsk);
	if (cs)
		do_set_cpus_allowed(tsk, cs->cpus_allowed);
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
}

void cpuset_init_current_mems_allowed(void)
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

	mutex_lock(&callback_mutex);
	task_lock(tsk);
	guarantee_online_mems(task_cs(tsk), &mask);
	task_unlock(tsk);
	mutex_unlock(&callback_mutex);

	return mask;
}

/**
 * cpuset_nodemask_valid_mems_allowed - check nodemask vs. curremt mems_allowed
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
 * callback_mutex.  If no ancestor is mem_exclusive or mem_hardwall
 * (an unusual configuration), then returns the root cpuset.
 */
static const struct cpuset *nearest_hardwall_ancestor(const struct cpuset *cs)
{
	while (!(is_mem_exclusive(cs) || is_mem_hardwall(cs)) && parent_cs(cs))
		cs = parent_cs(cs);
	return cs;
}

/**
 * cpuset_node_allowed_softwall - Can we allocate on a memory node?
 * @node: is this an allowed node?
 * @gfp_mask: memory allocation flags
 *
 * If we're in interrupt, yes, we can always allocate.  If __GFP_THISNODE is
 * set, yes, we can always allocate.  If node is in our task's mems_allowed,
 * yes.  If it's not a __GFP_HARDWALL request and this node is in the nearest
 * hardwalled cpuset ancestor to this task's cpuset, yes.  If the task has been
 * OOM killed and has access to memory reserves as specified by the TIF_MEMDIE
 * flag, yes.
 * Otherwise, no.
 *
 * If __GFP_HARDWALL is set, cpuset_node_allowed_softwall() reduces to
 * cpuset_node_allowed_hardwall().  Otherwise, cpuset_node_allowed_softwall()
 * might sleep, and might allow a node from an enclosing cpuset.
 *
 * cpuset_node_allowed_hardwall() only handles the simpler case of hardwall
 * cpusets, and never sleeps.
 *
 * The __GFP_THISNODE placement logic is really handled elsewhere,
 * by forcibly using a zonelist starting at a specified node, and by
 * (in get_page_from_freelist()) refusing to consider the zones for
 * any node on the zonelist except the first.  By the time any such
 * calls get to this routine, we should just shut up and say 'yes'.
 *
 * GFP_USER allocations are marked with the __GFP_HARDWALL bit,
 * and do not allow allocations outside the current tasks cpuset
 * unless the task has been OOM killed as is marked TIF_MEMDIE.
 * GFP_KERNEL allocations are not so marked, so can escape to the
 * nearest enclosing hardwalled ancestor cpuset.
 *
 * Scanning up parent cpusets requires callback_mutex.  The
 * __alloc_pages() routine only calls here with __GFP_HARDWALL bit
 * _not_ set if it's a GFP_KERNEL allocation, and all nodes in the
 * current tasks mems_allowed came up empty on the first pass over
 * the zonelist.  So only GFP_KERNEL allocations, if all nodes in the
 * cpuset are short of memory, might require taking the callback_mutex
 * mutex.
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
 *	TIF_MEMDIE   - any node ok
 *	GFP_KERNEL   - any node in enclosing hardwalled cpuset ok
 *	GFP_USER     - only nodes in current tasks mems allowed ok.
 *
 * Rule:
 *    Don't call cpuset_node_allowed_softwall if you can't sleep, unless you
 *    pass in the __GFP_HARDWALL flag set in gfp_flag, which disables
 *    the code that might scan up ancestor cpusets and sleep.
 */
int __cpuset_node_allowed_softwall(int node, gfp_t gfp_mask)
{
	const struct cpuset *cs;	/* current cpuset ancestors */
	int allowed;			/* is allocation in zone z allowed? */

	if (in_interrupt() || (gfp_mask & __GFP_THISNODE))
		return 1;
	might_sleep_if(!(gfp_mask & __GFP_HARDWALL));
	if (node_isset(node, current->mems_allowed))
		return 1;
	/*
	 * Allow tasks that have access to memory reserves because they have
	 * been OOM killed to get memory anywhere.
	 */
	if (unlikely(test_thread_flag(TIF_MEMDIE)))
		return 1;
	if (gfp_mask & __GFP_HARDWALL)	/* If hardwall request, stop here */
		return 0;

	if (current->flags & PF_EXITING) /* Let dying task have memory */
		return 1;

	/* Not hardwall and node outside mems_allowed: scan up cpusets */
	mutex_lock(&callback_mutex);

	task_lock(current);
	cs = nearest_hardwall_ancestor(task_cs(current));
	task_unlock(current);

	allowed = node_isset(node, cs->mems_allowed);
	mutex_unlock(&callback_mutex);
	return allowed;
}

/*
 * cpuset_node_allowed_hardwall - Can we allocate on a memory node?
 * @node: is this an allowed node?
 * @gfp_mask: memory allocation flags
 *
 * If we're in interrupt, yes, we can always allocate.  If __GFP_THISNODE is
 * set, yes, we can always allocate.  If node is in our task's mems_allowed,
 * yes.  If the task has been OOM killed and has access to memory reserves as
 * specified by the TIF_MEMDIE flag, yes.
 * Otherwise, no.
 *
 * The __GFP_THISNODE placement logic is really handled elsewhere,
 * by forcibly using a zonelist starting at a specified node, and by
 * (in get_page_from_freelist()) refusing to consider the zones for
 * any node on the zonelist except the first.  By the time any such
 * calls get to this routine, we should just shut up and say 'yes'.
 *
 * Unlike the cpuset_node_allowed_softwall() variant, above,
 * this variant requires that the node be in the current task's
 * mems_allowed or that we're in interrupt.  It does not scan up the
 * cpuset hierarchy for the nearest enclosing mem_exclusive cpuset.
 * It never sleeps.
 */
int __cpuset_node_allowed_hardwall(int node, gfp_t gfp_mask)
{
	if (in_interrupt() || (gfp_mask & __GFP_THISNODE))
		return 1;
	if (node_isset(node, current->mems_allowed))
		return 1;
	/*
	 * Allow tasks that have access to memory reserves because they have
	 * been OOM killed to get memory anywhere.
	 */
	if (unlikely(test_thread_flag(TIF_MEMDIE)))
		return 1;
	return 0;
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
	int node;

	node = next_node(*rotor, current->mems_allowed);
	if (node == MAX_NUMNODES)
		node = first_node(current->mems_allowed);
	*rotor = node;
	return node;
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

#define CPUSET_NODELIST_LEN	(256)

/**
 * cpuset_print_task_mems_allowed - prints task's cpuset and mems_allowed
 * @task: pointer to task_struct of some task.
 *
 * Description: Prints @task's name, cpuset name, and cached copy of its
 * mems_allowed to the kernel log.  Must hold task_lock(task) to allow
 * dereferencing task_cs(task).
 */
void cpuset_print_task_mems_allowed(struct task_struct *tsk)
{
	 /* Statically allocated to prevent using excess stack. */
	static char cpuset_nodelist[CPUSET_NODELIST_LEN];
	static DEFINE_SPINLOCK(cpuset_buffer_lock);

	struct cgroup *cgrp = task_cs(tsk)->css.cgroup;

	spin_lock(&cpuset_buffer_lock);

	nodelist_scnprintf(cpuset_nodelist, CPUSET_NODELIST_LEN,
			   tsk->mems_allowed);
	printk(KERN_INFO "%s cpuset=%s mems_allowed=%s\n",
	       tsk->comm, cgroup_name(cgrp), cpuset_nodelist);

	spin_unlock(&cpuset_buffer_lock);
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
	task_lock(current);
	fmeter_markevent(&task_cs(current)->fmeter);
	task_unlock(current);
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
static int proc_cpuset_show(struct seq_file *m, void *unused_v)
{
	struct pid *pid;
	struct task_struct *tsk;
	char *buf;
	struct cgroup_subsys_state *css;
	int retval;

	retval = -ENOMEM;
	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		goto out;

	retval = -ESRCH;
	pid = m->private;
	tsk = get_pid_task(pid, PIDTYPE_PID);
	if (!tsk)
		goto out_free;

	rcu_read_lock();
	css = task_subsys_state(tsk, cpuset_subsys_id);
	retval = cgroup_path(css->cgroup, buf, PAGE_SIZE);
	rcu_read_unlock();
	if (retval < 0)
		goto out_put_task;
	seq_puts(m, buf);
	seq_putc(m, '\n');
out_put_task:
	put_task_struct(tsk);
out_free:
	kfree(buf);
out:
	return retval;
}

static int cpuset_open(struct inode *inode, struct file *file)
{
	struct pid *pid = PROC_I(inode)->pid;
	return single_open(file, proc_cpuset_show, pid);
}

const struct file_operations proc_cpuset_operations = {
	.open		= cpuset_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif /* CONFIG_PROC_PID_CPUSET */

/* Display task mems_allowed in /proc/<pid>/status file. */
void cpuset_task_status_allowed(struct seq_file *m, struct task_struct *task)
{
	seq_printf(m, "Mems_allowed:\t");
	seq_nodemask(m, &task->mems_allowed);
	seq_printf(m, "\n");
	seq_printf(m, "Mems_allowed_list:\t");
	seq_nodemask_list(m, &task->mems_allowed);
	seq_printf(m, "\n");
}
