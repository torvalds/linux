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
#include <linux/module.h>
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
#include <asm/atomic.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>
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
	cpumask_t cpus_allowed;		/* CPUs allowed to tasks in cpuset */
	nodemask_t mems_allowed;	/* Memory Nodes allowed to tasks */

	struct cpuset *parent;		/* my parent */

	/*
	 * Copy of global cpuset_mems_generation as of the most
	 * recent time this cpuset changed its mems_allowed.
	 */
	int mems_generation;

	struct fmeter fmeter;		/* memory_pressure filter */

	/* partition number for rebuild_sched_domains() */
	int pn;

	/* for custom sched domain */
	int relax_domain_level;

	/* used for walking a cpuset heirarchy */
	struct list_head stack_list;
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
struct cpuset_hotplug_scanner {
	struct cgroup_scanner scan;
	struct cgroup *to;
};

/* bits in struct cpuset flags field */
typedef enum {
	CS_CPU_EXCLUSIVE,
	CS_MEM_EXCLUSIVE,
	CS_MEMORY_MIGRATE,
	CS_SCHED_LOAD_BALANCE,
	CS_SPREAD_PAGE,
	CS_SPREAD_SLAB,
} cpuset_flagbits_t;

/* convenient tests for these bits */
static inline int is_cpu_exclusive(const struct cpuset *cs)
{
	return test_bit(CS_CPU_EXCLUSIVE, &cs->flags);
}

static inline int is_mem_exclusive(const struct cpuset *cs)
{
	return test_bit(CS_MEM_EXCLUSIVE, &cs->flags);
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

/*
 * Increment this integer everytime any cpuset changes its
 * mems_allowed value.  Users of cpusets can track this generation
 * number, and avoid having to lock and reload mems_allowed unless
 * the cpuset they're using changes generation.
 *
 * A single, global generation is needed because cpuset_attach_task() could
 * reattach a task to a different cpuset, which must not have its
 * generation numbers aliased with those of that tasks previous cpuset.
 *
 * Generations are needed for mems_allowed because one task cannot
 * modify another's memory placement.  So we must enable every task,
 * on every visit to __alloc_pages(), to efficiently check whether
 * its current->cpuset->mems_allowed has changed, requiring an update
 * of its current->mems_allowed.
 *
 * Since writes to cpuset_mems_generation are guarded by the cgroup lock
 * there is no need to mark it atomic.
 */
static int cpuset_mems_generation;

static struct cpuset top_cpuset = {
	.flags = ((1 << CS_CPU_EXCLUSIVE) | (1 << CS_MEM_EXCLUSIVE)),
	.cpus_allowed = CPU_MASK_ALL,
	.mems_allowed = NODE_MASK_ALL,
};

/*
 * There are two global mutexes guarding cpuset structures.  The first
 * is the main control groups cgroup_mutex, accessed via
 * cgroup_lock()/cgroup_unlock().  The second is the cpuset-specific
 * callback_mutex, below. They can nest.  It is ok to first take
 * cgroup_mutex, then nest callback_mutex.  We also require taking
 * task_lock() when dereferencing a task's cpuset pointer.  See "The
 * task_lock() exception", at the end of this comment.
 *
 * A task must hold both mutexes to modify cpusets.  If a task
 * holds cgroup_mutex, then it blocks others wanting that mutex,
 * ensuring that it is the only task able to also acquire callback_mutex
 * and be able to modify cpusets.  It can perform various checks on
 * the cpuset structure first, knowing nothing will change.  It can
 * also allocate memory while just holding cgroup_mutex.  While it is
 * performing these checks, various callback routines can briefly
 * acquire callback_mutex to query cpusets.  Once it is ready to make
 * the changes, it takes callback_mutex, blocking everyone else.
 *
 * Calls to the kernel memory allocator can not be made while holding
 * callback_mutex, as that would risk double tripping on callback_mutex
 * from one of the callbacks into the cpuset code from within
 * __alloc_pages().
 *
 * If a task is only holding callback_mutex, then it has read-only
 * access to cpusets.
 *
 * The task_struct fields mems_allowed and mems_generation may only
 * be accessed in the context of that task, so require no locks.
 *
 * The cpuset_common_file_write handler for operations that modify
 * the cpuset hierarchy holds cgroup_mutex across the entire operation,
 * single threading all such cpuset modifications across the system.
 *
 * The cpuset_common_file_read() handlers only hold callback_mutex across
 * small pieces of code, such as when reading out possibly multi-word
 * cpumasks and nodemasks.
 *
 * Accessing a task's cpuset should be done in accordance with the
 * guidelines for accessing subsystem state in kernel/cgroup.c
 */

static DEFINE_MUTEX(callback_mutex);

/* This is ugly, but preserves the userspace API for existing cpuset
 * users. If someone tries to mount the "cpuset" filesystem, we
 * silently switch it to mount "cgroup" instead */
static int cpuset_get_sb(struct file_system_type *fs_type,
			 int flags, const char *unused_dev_name,
			 void *data, struct vfsmount *mnt)
{
	struct file_system_type *cgroup_fs = get_fs_type("cgroup");
	int ret = -ENODEV;
	if (cgroup_fs) {
		char mountopts[] =
			"cpuset,noprefix,"
			"release_agent=/sbin/cpuset_release_agent";
		ret = cgroup_fs->get_sb(cgroup_fs, flags,
					   unused_dev_name, mountopts, mnt);
		put_filesystem(cgroup_fs);
	}
	return ret;
}

static struct file_system_type cpuset_fs_type = {
	.name = "cpuset",
	.get_sb = cpuset_get_sb,
};

/*
 * Return in *pmask the portion of a cpusets's cpus_allowed that
 * are online.  If none are online, walk up the cpuset hierarchy
 * until we find one that does have some online cpus.  If we get
 * all the way to the top and still haven't found any online cpus,
 * return cpu_online_map.  Or if passed a NULL cs from an exit'ing
 * task, return cpu_online_map.
 *
 * One way or another, we guarantee to return some non-empty subset
 * of cpu_online_map.
 *
 * Call with callback_mutex held.
 */

static void guarantee_online_cpus(const struct cpuset *cs, cpumask_t *pmask)
{
	while (cs && !cpus_intersects(cs->cpus_allowed, cpu_online_map))
		cs = cs->parent;
	if (cs)
		cpus_and(*pmask, cs->cpus_allowed, cpu_online_map);
	else
		*pmask = cpu_online_map;
	BUG_ON(!cpus_intersects(*pmask, cpu_online_map));
}

/*
 * Return in *pmask the portion of a cpusets's mems_allowed that
 * are online, with memory.  If none are online with memory, walk
 * up the cpuset hierarchy until we find one that does have some
 * online mems.  If we get all the way to the top and still haven't
 * found any online mems, return node_states[N_HIGH_MEMORY].
 *
 * One way or another, we guarantee to return some non-empty subset
 * of node_states[N_HIGH_MEMORY].
 *
 * Call with callback_mutex held.
 */

static void guarantee_online_mems(const struct cpuset *cs, nodemask_t *pmask)
{
	while (cs && !nodes_intersects(cs->mems_allowed,
					node_states[N_HIGH_MEMORY]))
		cs = cs->parent;
	if (cs)
		nodes_and(*pmask, cs->mems_allowed,
					node_states[N_HIGH_MEMORY]);
	else
		*pmask = node_states[N_HIGH_MEMORY];
	BUG_ON(!nodes_intersects(*pmask, node_states[N_HIGH_MEMORY]));
}

/**
 * cpuset_update_task_memory_state - update task memory placement
 *
 * If the current tasks cpusets mems_allowed changed behind our
 * backs, update current->mems_allowed, mems_generation and task NUMA
 * mempolicy to the new value.
 *
 * Task mempolicy is updated by rebinding it relative to the
 * current->cpuset if a task has its memory placement changed.
 * Do not call this routine if in_interrupt().
 *
 * Call without callback_mutex or task_lock() held.  May be
 * called with or without cgroup_mutex held.  Thanks in part to
 * 'the_top_cpuset_hack', the task's cpuset pointer will never
 * be NULL.  This routine also might acquire callback_mutex during
 * call.
 *
 * Reading current->cpuset->mems_generation doesn't need task_lock
 * to guard the current->cpuset derefence, because it is guarded
 * from concurrent freeing of current->cpuset using RCU.
 *
 * The rcu_dereference() is technically probably not needed,
 * as I don't actually mind if I see a new cpuset pointer but
 * an old value of mems_generation.  However this really only
 * matters on alpha systems using cpusets heavily.  If I dropped
 * that rcu_dereference(), it would save them a memory barrier.
 * For all other arch's, rcu_dereference is a no-op anyway, and for
 * alpha systems not using cpusets, another planned optimization,
 * avoiding the rcu critical section for tasks in the root cpuset
 * which is statically allocated, so can't vanish, will make this
 * irrelevant.  Better to use RCU as intended, than to engage in
 * some cute trick to save a memory barrier that is impossible to
 * test, for alpha systems using cpusets heavily, which might not
 * even exist.
 *
 * This routine is needed to update the per-task mems_allowed data,
 * within the tasks context, when it is trying to allocate memory
 * (in various mm/mempolicy.c routines) and notices that some other
 * task has been modifying its cpuset.
 */

void cpuset_update_task_memory_state(void)
{
	int my_cpusets_mem_gen;
	struct task_struct *tsk = current;
	struct cpuset *cs;

	if (task_cs(tsk) == &top_cpuset) {
		/* Don't need rcu for top_cpuset.  It's never freed. */
		my_cpusets_mem_gen = top_cpuset.mems_generation;
	} else {
		rcu_read_lock();
		my_cpusets_mem_gen = task_cs(current)->mems_generation;
		rcu_read_unlock();
	}

	if (my_cpusets_mem_gen != tsk->cpuset_mems_generation) {
		mutex_lock(&callback_mutex);
		task_lock(tsk);
		cs = task_cs(tsk); /* Maybe changed when task not locked */
		guarantee_online_mems(cs, &tsk->mems_allowed);
		tsk->cpuset_mems_generation = cs->mems_generation;
		if (is_spread_page(cs))
			tsk->flags |= PF_SPREAD_PAGE;
		else
			tsk->flags &= ~PF_SPREAD_PAGE;
		if (is_spread_slab(cs))
			tsk->flags |= PF_SPREAD_SLAB;
		else
			tsk->flags &= ~PF_SPREAD_SLAB;
		task_unlock(tsk);
		mutex_unlock(&callback_mutex);
		mpol_rebind_task(tsk, &tsk->mems_allowed);
	}
}

/*
 * is_cpuset_subset(p, q) - Is cpuset p a subset of cpuset q?
 *
 * One cpuset is a subset of another if all its allowed CPUs and
 * Memory Nodes are a subset of the other, and its exclusive flags
 * are only set if the other's are set.  Call holding cgroup_mutex.
 */

static int is_cpuset_subset(const struct cpuset *p, const struct cpuset *q)
{
	return	cpus_subset(p->cpus_allowed, q->cpus_allowed) &&
		nodes_subset(p->mems_allowed, q->mems_allowed) &&
		is_cpu_exclusive(p) <= is_cpu_exclusive(q) &&
		is_mem_exclusive(p) <= is_mem_exclusive(q);
}

/*
 * validate_change() - Used to validate that any proposed cpuset change
 *		       follows the structural rules for cpusets.
 *
 * If we replaced the flag and mask values of the current cpuset
 * (cur) with those values in the trial cpuset (trial), would
 * our various subset and exclusive rules still be valid?  Presumes
 * cgroup_mutex held.
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

	/* Each of our child cpusets must be a subset of us */
	list_for_each_entry(cont, &cur->css.cgroup->children, sibling) {
		if (!is_cpuset_subset(cgroup_cs(cont), trial))
			return -EBUSY;
	}

	/* Remaining checks don't apply to root cpuset */
	if (cur == &top_cpuset)
		return 0;

	par = cur->parent;

	/* We must be a subset of our parent cpuset */
	if (!is_cpuset_subset(trial, par))
		return -EACCES;

	/*
	 * If either I or some sibling (!= me) is exclusive, we can't
	 * overlap
	 */
	list_for_each_entry(cont, &par->css.cgroup->children, sibling) {
		c = cgroup_cs(cont);
		if ((is_cpu_exclusive(trial) || is_cpu_exclusive(c)) &&
		    c != cur &&
		    cpus_intersects(trial->cpus_allowed, c->cpus_allowed))
			return -EINVAL;
		if ((is_mem_exclusive(trial) || is_mem_exclusive(c)) &&
		    c != cur &&
		    nodes_intersects(trial->mems_allowed, c->mems_allowed))
			return -EINVAL;
	}

	/* Cpusets with tasks can't have empty cpus_allowed or mems_allowed */
	if (cgroup_task_count(cur->css.cgroup)) {
		if (cpus_empty(trial->cpus_allowed) ||
		    nodes_empty(trial->mems_allowed)) {
			return -ENOSPC;
		}
	}

	return 0;
}

/*
 * Helper routine for rebuild_sched_domains().
 * Do cpusets a, b have overlapping cpus_allowed masks?
 */

static int cpusets_overlap(struct cpuset *a, struct cpuset *b)
{
	return cpus_intersects(a->cpus_allowed, b->cpus_allowed);
}

static void
update_domain_attr(struct sched_domain_attr *dattr, struct cpuset *c)
{
	if (!dattr)
		return;
	if (dattr->relax_domain_level < c->relax_domain_level)
		dattr->relax_domain_level = c->relax_domain_level;
	return;
}

/*
 * rebuild_sched_domains()
 *
 * If the flag 'sched_load_balance' of any cpuset with non-empty
 * 'cpus' changes, or if the 'cpus' allowed changes in any cpuset
 * which has that flag enabled, or if any cpuset with a non-empty
 * 'cpus' is removed, then call this routine to rebuild the
 * scheduler's dynamic sched domains.
 *
 * This routine builds a partial partition of the systems CPUs
 * (the set of non-overlappping cpumask_t's in the array 'part'
 * below), and passes that partial partition to the kernel/sched.c
 * partition_sched_domains() routine, which will rebuild the
 * schedulers load balancing domains (sched domains) as specified
 * by that partial partition.  A 'partial partition' is a set of
 * non-overlapping subsets whose union is a subset of that set.
 *
 * See "What is sched_load_balance" in Documentation/cpusets.txt
 * for a background explanation of this.
 *
 * Does not return errors, on the theory that the callers of this
 * routine would rather not worry about failures to rebuild sched
 * domains when operating in the severe memory shortage situations
 * that could cause allocation failures below.
 *
 * Call with cgroup_mutex held.  May take callback_mutex during
 * call due to the kfifo_alloc() and kmalloc() calls.  May nest
 * a call to the get_online_cpus()/put_online_cpus() pair.
 * Must not be called holding callback_mutex, because we must not
 * call get_online_cpus() while holding callback_mutex.  Elsewhere
 * the kernel nests callback_mutex inside get_online_cpus() calls.
 * So the reverse nesting would risk an ABBA deadlock.
 *
 * The three key local variables below are:
 *    q  - a kfifo queue of cpuset pointers, used to implement a
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

static void rebuild_sched_domains(void)
{
	struct kfifo *q;	/* queue of cpusets to be scanned */
	struct cpuset *cp;	/* scans q */
	struct cpuset **csa;	/* array of all cpuset ptrs */
	int csn;		/* how many cpuset ptrs in csa so far */
	int i, j, k;		/* indices for partition finding loops */
	cpumask_t *doms;	/* resulting partition; i.e. sched domains */
	struct sched_domain_attr *dattr;  /* attributes for custom domains */
	int ndoms;		/* number of sched domains in result */
	int nslot;		/* next empty doms[] cpumask_t slot */

	q = NULL;
	csa = NULL;
	doms = NULL;
	dattr = NULL;

	/* Special case for the 99% of systems with one, full, sched domain */
	if (is_sched_load_balance(&top_cpuset)) {
		ndoms = 1;
		doms = kmalloc(sizeof(cpumask_t), GFP_KERNEL);
		if (!doms)
			goto rebuild;
		dattr = kmalloc(sizeof(struct sched_domain_attr), GFP_KERNEL);
		if (dattr) {
			*dattr = SD_ATTR_INIT;
			update_domain_attr(dattr, &top_cpuset);
		}
		*doms = top_cpuset.cpus_allowed;
		goto rebuild;
	}

	q = kfifo_alloc(number_of_cpusets * sizeof(cp), GFP_KERNEL, NULL);
	if (IS_ERR(q))
		goto done;
	csa = kmalloc(number_of_cpusets * sizeof(cp), GFP_KERNEL);
	if (!csa)
		goto done;
	csn = 0;

	cp = &top_cpuset;
	__kfifo_put(q, (void *)&cp, sizeof(cp));
	while (__kfifo_get(q, (void *)&cp, sizeof(cp))) {
		struct cgroup *cont;
		struct cpuset *child;   /* scans child cpusets of cp */
		if (is_sched_load_balance(cp))
			csa[csn++] = cp;
		list_for_each_entry(cont, &cp->css.cgroup->children, sibling) {
			child = cgroup_cs(cont);
			__kfifo_put(q, (void *)&child, sizeof(cp));
		}
  	}

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

	/* Convert <csn, csa> to <ndoms, doms> */
	doms = kmalloc(ndoms * sizeof(cpumask_t), GFP_KERNEL);
	if (!doms)
		goto rebuild;
	dattr = kmalloc(ndoms * sizeof(struct sched_domain_attr), GFP_KERNEL);

	for (nslot = 0, i = 0; i < csn; i++) {
		struct cpuset *a = csa[i];
		int apn = a->pn;

		if (apn >= 0) {
			cpumask_t *dp = doms + nslot;

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

			cpus_clear(*dp);
			if (dattr)
				*(dattr + nslot) = SD_ATTR_INIT;
			for (j = i; j < csn; j++) {
				struct cpuset *b = csa[j];

				if (apn == b->pn) {
					cpus_or(*dp, *dp, b->cpus_allowed);
					b->pn = -1;
					update_domain_attr(dattr, b);
				}
			}
			nslot++;
		}
	}
	BUG_ON(nslot != ndoms);

rebuild:
	/* Have scheduler rebuild sched domains */
	get_online_cpus();
	partition_sched_domains(ndoms, doms, dattr);
	put_online_cpus();

done:
	if (q && !IS_ERR(q))
		kfifo_free(q);
	kfree(csa);
	/* Don't kfree(doms) -- partition_sched_domains() does that. */
	/* Don't kfree(dattr) -- partition_sched_domains() does that. */
}

static inline int started_after_time(struct task_struct *t1,
				     struct timespec *time,
				     struct task_struct *t2)
{
	int start_diff = timespec_compare(&t1->start_time, time);
	if (start_diff > 0) {
		return 1;
	} else if (start_diff < 0) {
		return 0;
	} else {
		/*
		 * Arbitrarily, if two processes started at the same
		 * time, we'll say that the lower pointer value
		 * started first. Note that t2 may have exited by now
		 * so this may not be a valid pointer any longer, but
		 * that's fine - it still serves to distinguish
		 * between two tasks started (effectively)
		 * simultaneously.
		 */
		return t1 > t2;
	}
}

static inline int started_after(void *p1, void *p2)
{
	struct task_struct *t1 = p1;
	struct task_struct *t2 = p2;
	return started_after_time(t1, &t2->start_time, t2);
}

/**
 * cpuset_test_cpumask - test a task's cpus_allowed versus its cpuset's
 * @tsk: task to test
 * @scan: struct cgroup_scanner contained in its struct cpuset_hotplug_scanner
 *
 * Call with cgroup_mutex held.  May take callback_mutex during call.
 * Called for each task in a cgroup by cgroup_scan_tasks().
 * Return nonzero if this tasks's cpus_allowed mask should be changed (in other
 * words, if its mask is not equal to its cpuset's mask).
 */
int cpuset_test_cpumask(struct task_struct *tsk, struct cgroup_scanner *scan)
{
	return !cpus_equal(tsk->cpus_allowed,
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
 * holding cgroup_lock() at this point.
 */
void cpuset_change_cpumask(struct task_struct *tsk, struct cgroup_scanner *scan)
{
	set_cpus_allowed_ptr(tsk, &((cgroup_cs(scan->cg))->cpus_allowed));
}

/**
 * update_cpumask - update the cpus_allowed mask of a cpuset and all tasks in it
 * @cs: the cpuset to consider
 * @buf: buffer of cpu numbers written to this cpuset
 */
static int update_cpumask(struct cpuset *cs, char *buf)
{
	struct cpuset trialcs;
	struct cgroup_scanner scan;
	struct ptr_heap heap;
	int retval;
	int is_load_balanced;

	/* top_cpuset.cpus_allowed tracks cpu_online_map; it's read-only */
	if (cs == &top_cpuset)
		return -EACCES;

	trialcs = *cs;

	/*
	 * An empty cpus_allowed is ok only if the cpuset has no tasks.
	 * Since cpulist_parse() fails on an empty mask, we special case
	 * that parsing.  The validate_change() call ensures that cpusets
	 * with tasks have cpus.
	 */
	buf = strstrip(buf);
	if (!*buf) {
		cpus_clear(trialcs.cpus_allowed);
	} else {
		retval = cpulist_parse(buf, trialcs.cpus_allowed);
		if (retval < 0)
			return retval;
	}
	cpus_and(trialcs.cpus_allowed, trialcs.cpus_allowed, cpu_online_map);
	retval = validate_change(cs, &trialcs);
	if (retval < 0)
		return retval;

	/* Nothing to do if the cpus didn't change */
	if (cpus_equal(cs->cpus_allowed, trialcs.cpus_allowed))
		return 0;

	retval = heap_init(&heap, PAGE_SIZE, GFP_KERNEL, &started_after);
	if (retval)
		return retval;

	is_load_balanced = is_sched_load_balance(&trialcs);

	mutex_lock(&callback_mutex);
	cs->cpus_allowed = trialcs.cpus_allowed;
	mutex_unlock(&callback_mutex);

	/*
	 * Scan tasks in the cpuset, and update the cpumasks of any
	 * that need an update.
	 */
	scan.cg = cs->css.cgroup;
	scan.test_task = cpuset_test_cpumask;
	scan.process_task = cpuset_change_cpumask;
	scan.heap = &heap;
	cgroup_scan_tasks(&scan);
	heap_free(&heap);

	if (is_load_balanced)
		rebuild_sched_domains();
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
 *    Call holding cgroup_mutex, so current's cpuset won't change
 *    during this call, as manage_mutex holds off any cpuset_attach()
 *    calls.  Therefore we don't need to take task_lock around the
 *    call to guarantee_online_mems(), as we know no one is changing
 *    our task's cpuset.
 *
 *    Hold callback_mutex around the two modifications of our tasks
 *    mems_allowed to synchronize with cpuset_mems_allowed().
 *
 *    While the mm_struct we are migrating is typically from some
 *    other task, the task_struct mems_allowed that we are hacking
 *    is for our current task, which must allocate new pages for that
 *    migrating memory region.
 *
 *    We call cpuset_update_task_memory_state() before hacking
 *    our tasks mems_allowed, so that we are assured of being in
 *    sync with our tasks cpuset, and in particular, callbacks to
 *    cpuset_update_task_memory_state() from nested page allocations
 *    won't see any mismatch of our cpuset and task mems_generation
 *    values, so won't overwrite our hacked tasks mems_allowed
 *    nodemask.
 */

static void cpuset_migrate_mm(struct mm_struct *mm, const nodemask_t *from,
							const nodemask_t *to)
{
	struct task_struct *tsk = current;

	cpuset_update_task_memory_state();

	mutex_lock(&callback_mutex);
	tsk->mems_allowed = *to;
	mutex_unlock(&callback_mutex);

	do_migrate_pages(mm, from, to, MPOL_MF_MOVE_ALL);

	mutex_lock(&callback_mutex);
	guarantee_online_mems(task_cs(tsk),&tsk->mems_allowed);
	mutex_unlock(&callback_mutex);
}

/*
 * Handle user request to change the 'mems' memory placement
 * of a cpuset.  Needs to validate the request, update the
 * cpusets mems_allowed and mems_generation, and for each
 * task in the cpuset, rebind any vma mempolicies and if
 * the cpuset is marked 'memory_migrate', migrate the tasks
 * pages to the new memory.
 *
 * Call with cgroup_mutex held.  May take callback_mutex during call.
 * Will take tasklist_lock, scan tasklist for tasks in cpuset cs,
 * lock each such tasks mm->mmap_sem, scan its vma's and rebind
 * their mempolicies to the cpusets new mems_allowed.
 */

static void *cpuset_being_rebound;

static int update_nodemask(struct cpuset *cs, char *buf)
{
	struct cpuset trialcs;
	nodemask_t oldmem;
	struct task_struct *p;
	struct mm_struct **mmarray;
	int i, n, ntasks;
	int migrate;
	int fudge;
	int retval;
	struct cgroup_iter it;

	/*
	 * top_cpuset.mems_allowed tracks node_stats[N_HIGH_MEMORY];
	 * it's read-only
	 */
	if (cs == &top_cpuset)
		return -EACCES;

	trialcs = *cs;

	/*
	 * An empty mems_allowed is ok iff there are no tasks in the cpuset.
	 * Since nodelist_parse() fails on an empty mask, we special case
	 * that parsing.  The validate_change() call ensures that cpusets
	 * with tasks have memory.
	 */
	buf = strstrip(buf);
	if (!*buf) {
		nodes_clear(trialcs.mems_allowed);
	} else {
		retval = nodelist_parse(buf, trialcs.mems_allowed);
		if (retval < 0)
			goto done;
	}
	nodes_and(trialcs.mems_allowed, trialcs.mems_allowed,
						node_states[N_HIGH_MEMORY]);
	oldmem = cs->mems_allowed;
	if (nodes_equal(oldmem, trialcs.mems_allowed)) {
		retval = 0;		/* Too easy - nothing to do */
		goto done;
	}
	retval = validate_change(cs, &trialcs);
	if (retval < 0)
		goto done;

	mutex_lock(&callback_mutex);
	cs->mems_allowed = trialcs.mems_allowed;
	cs->mems_generation = cpuset_mems_generation++;
	mutex_unlock(&callback_mutex);

	cpuset_being_rebound = cs;		/* causes mpol_dup() rebind */

	fudge = 10;				/* spare mmarray[] slots */
	fudge += cpus_weight(cs->cpus_allowed);	/* imagine one fork-bomb/cpu */
	retval = -ENOMEM;

	/*
	 * Allocate mmarray[] to hold mm reference for each task
	 * in cpuset cs.  Can't kmalloc GFP_KERNEL while holding
	 * tasklist_lock.  We could use GFP_ATOMIC, but with a
	 * few more lines of code, we can retry until we get a big
	 * enough mmarray[] w/o using GFP_ATOMIC.
	 */
	while (1) {
		ntasks = cgroup_task_count(cs->css.cgroup);  /* guess */
		ntasks += fudge;
		mmarray = kmalloc(ntasks * sizeof(*mmarray), GFP_KERNEL);
		if (!mmarray)
			goto done;
		read_lock(&tasklist_lock);		/* block fork */
		if (cgroup_task_count(cs->css.cgroup) <= ntasks)
			break;				/* got enough */
		read_unlock(&tasklist_lock);		/* try again */
		kfree(mmarray);
	}

	n = 0;

	/* Load up mmarray[] with mm reference for each task in cpuset. */
	cgroup_iter_start(cs->css.cgroup, &it);
	while ((p = cgroup_iter_next(cs->css.cgroup, &it))) {
		struct mm_struct *mm;

		if (n >= ntasks) {
			printk(KERN_WARNING
				"Cpuset mempolicy rebind incomplete.\n");
			break;
		}
		mm = get_task_mm(p);
		if (!mm)
			continue;
		mmarray[n++] = mm;
	}
	cgroup_iter_end(cs->css.cgroup, &it);
	read_unlock(&tasklist_lock);

	/*
	 * Now that we've dropped the tasklist spinlock, we can
	 * rebind the vma mempolicies of each mm in mmarray[] to their
	 * new cpuset, and release that mm.  The mpol_rebind_mm()
	 * call takes mmap_sem, which we couldn't take while holding
	 * tasklist_lock.  Forks can happen again now - the mpol_dup()
	 * cpuset_being_rebound check will catch such forks, and rebind
	 * their vma mempolicies too.  Because we still hold the global
	 * cgroup_mutex, we know that no other rebind effort will
	 * be contending for the global variable cpuset_being_rebound.
	 * It's ok if we rebind the same mm twice; mpol_rebind_mm()
	 * is idempotent.  Also migrate pages in each mm to new nodes.
	 */
	migrate = is_memory_migrate(cs);
	for (i = 0; i < n; i++) {
		struct mm_struct *mm = mmarray[i];

		mpol_rebind_mm(mm, &cs->mems_allowed);
		if (migrate)
			cpuset_migrate_mm(mm, &oldmem, &cs->mems_allowed);
		mmput(mm);
	}

	/* We're done rebinding vmas to this cpuset's new mems_allowed. */
	kfree(mmarray);
	cpuset_being_rebound = NULL;
	retval = 0;
done:
	return retval;
}

int current_cpuset_is_being_rebound(void)
{
	return task_cs(current) == cpuset_being_rebound;
}

/*
 * Call with cgroup_mutex held.
 */

static int update_memory_pressure_enabled(struct cpuset *cs, char *buf)
{
	if (simple_strtoul(buf, NULL, 10) != 0)
		cpuset_memory_pressure_enabled = 1;
	else
		cpuset_memory_pressure_enabled = 0;
	return 0;
}

static int update_relax_domain_level(struct cpuset *cs, char *buf)
{
	int val = simple_strtol(buf, NULL, 10);

	if (val < 0)
		val = -1;

	if (val != cs->relax_domain_level) {
		cs->relax_domain_level = val;
		rebuild_sched_domains();
	}

	return 0;
}

/*
 * update_flag - read a 0 or a 1 in a file and update associated flag
 * bit:	the bit to update (CS_CPU_EXCLUSIVE, CS_MEM_EXCLUSIVE,
 *				CS_SCHED_LOAD_BALANCE,
 *				CS_NOTIFY_ON_RELEASE, CS_MEMORY_MIGRATE,
 *				CS_SPREAD_PAGE, CS_SPREAD_SLAB)
 * cs:	the cpuset to update
 * buf:	the buffer where we read the 0 or 1
 *
 * Call with cgroup_mutex held.
 */

static int update_flag(cpuset_flagbits_t bit, struct cpuset *cs, char *buf)
{
	int turning_on;
	struct cpuset trialcs;
	int err;
	int cpus_nonempty, balance_flag_changed;

	turning_on = (simple_strtoul(buf, NULL, 10) != 0);

	trialcs = *cs;
	if (turning_on)
		set_bit(bit, &trialcs.flags);
	else
		clear_bit(bit, &trialcs.flags);

	err = validate_change(cs, &trialcs);
	if (err < 0)
		return err;

	cpus_nonempty = !cpus_empty(trialcs.cpus_allowed);
	balance_flag_changed = (is_sched_load_balance(cs) !=
		 			is_sched_load_balance(&trialcs));

	mutex_lock(&callback_mutex);
	cs->flags = trialcs.flags;
	mutex_unlock(&callback_mutex);

	if (cpus_nonempty && balance_flag_changed)
		rebuild_sched_domains();

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

/* Called by cgroups to determine if a cpuset is usable; cgroup_mutex held */
static int cpuset_can_attach(struct cgroup_subsys *ss,
			     struct cgroup *cont, struct task_struct *tsk)
{
	struct cpuset *cs = cgroup_cs(cont);

	if (cpus_empty(cs->cpus_allowed) || nodes_empty(cs->mems_allowed))
		return -ENOSPC;

	return security_task_setscheduler(tsk, 0, NULL);
}

static void cpuset_attach(struct cgroup_subsys *ss,
			  struct cgroup *cont, struct cgroup *oldcont,
			  struct task_struct *tsk)
{
	cpumask_t cpus;
	nodemask_t from, to;
	struct mm_struct *mm;
	struct cpuset *cs = cgroup_cs(cont);
	struct cpuset *oldcs = cgroup_cs(oldcont);

	mutex_lock(&callback_mutex);
	guarantee_online_cpus(cs, &cpus);
	set_cpus_allowed_ptr(tsk, &cpus);
	mutex_unlock(&callback_mutex);

	from = oldcs->mems_allowed;
	to = cs->mems_allowed;
	mm = get_task_mm(tsk);
	if (mm) {
		mpol_rebind_mm(mm, &to);
		if (is_memory_migrate(cs))
			cpuset_migrate_mm(mm, &from, &to);
		mmput(mm);
	}

}

/* The various types of files and directories in a cpuset file system */

typedef enum {
	FILE_MEMORY_MIGRATE,
	FILE_CPULIST,
	FILE_MEMLIST,
	FILE_CPU_EXCLUSIVE,
	FILE_MEM_EXCLUSIVE,
	FILE_SCHED_LOAD_BALANCE,
	FILE_SCHED_RELAX_DOMAIN_LEVEL,
	FILE_MEMORY_PRESSURE_ENABLED,
	FILE_MEMORY_PRESSURE,
	FILE_SPREAD_PAGE,
	FILE_SPREAD_SLAB,
} cpuset_filetype_t;

static ssize_t cpuset_common_file_write(struct cgroup *cont,
					struct cftype *cft,
					struct file *file,
					const char __user *userbuf,
					size_t nbytes, loff_t *unused_ppos)
{
	struct cpuset *cs = cgroup_cs(cont);
	cpuset_filetype_t type = cft->private;
	char *buffer;
	int retval = 0;

	/* Crude upper limit on largest legitimate cpulist user might write. */
	if (nbytes > 100U + 6 * max(NR_CPUS, MAX_NUMNODES))
		return -E2BIG;

	/* +1 for nul-terminator */
	buffer = kmalloc(nbytes + 1, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	if (copy_from_user(buffer, userbuf, nbytes)) {
		retval = -EFAULT;
		goto out1;
	}
	buffer[nbytes] = 0;	/* nul-terminate */

	cgroup_lock();

	if (cgroup_is_removed(cont)) {
		retval = -ENODEV;
		goto out2;
	}

	switch (type) {
	case FILE_CPULIST:
		retval = update_cpumask(cs, buffer);
		break;
	case FILE_MEMLIST:
		retval = update_nodemask(cs, buffer);
		break;
	case FILE_CPU_EXCLUSIVE:
		retval = update_flag(CS_CPU_EXCLUSIVE, cs, buffer);
		break;
	case FILE_MEM_EXCLUSIVE:
		retval = update_flag(CS_MEM_EXCLUSIVE, cs, buffer);
		break;
	case FILE_SCHED_LOAD_BALANCE:
		retval = update_flag(CS_SCHED_LOAD_BALANCE, cs, buffer);
		break;
	case FILE_SCHED_RELAX_DOMAIN_LEVEL:
		retval = update_relax_domain_level(cs, buffer);
		break;
	case FILE_MEMORY_MIGRATE:
		retval = update_flag(CS_MEMORY_MIGRATE, cs, buffer);
		break;
	case FILE_MEMORY_PRESSURE_ENABLED:
		retval = update_memory_pressure_enabled(cs, buffer);
		break;
	case FILE_MEMORY_PRESSURE:
		retval = -EACCES;
		break;
	case FILE_SPREAD_PAGE:
		retval = update_flag(CS_SPREAD_PAGE, cs, buffer);
		cs->mems_generation = cpuset_mems_generation++;
		break;
	case FILE_SPREAD_SLAB:
		retval = update_flag(CS_SPREAD_SLAB, cs, buffer);
		cs->mems_generation = cpuset_mems_generation++;
		break;
	default:
		retval = -EINVAL;
		goto out2;
	}

	if (retval == 0)
		retval = nbytes;
out2:
	cgroup_unlock();
out1:
	kfree(buffer);
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

static int cpuset_sprintf_cpulist(char *page, struct cpuset *cs)
{
	cpumask_t mask;

	mutex_lock(&callback_mutex);
	mask = cs->cpus_allowed;
	mutex_unlock(&callback_mutex);

	return cpulist_scnprintf(page, PAGE_SIZE, mask);
}

static int cpuset_sprintf_memlist(char *page, struct cpuset *cs)
{
	nodemask_t mask;

	mutex_lock(&callback_mutex);
	mask = cs->mems_allowed;
	mutex_unlock(&callback_mutex);

	return nodelist_scnprintf(page, PAGE_SIZE, mask);
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
	case FILE_CPU_EXCLUSIVE:
		*s++ = is_cpu_exclusive(cs) ? '1' : '0';
		break;
	case FILE_MEM_EXCLUSIVE:
		*s++ = is_mem_exclusive(cs) ? '1' : '0';
		break;
	case FILE_SCHED_LOAD_BALANCE:
		*s++ = is_sched_load_balance(cs) ? '1' : '0';
		break;
	case FILE_SCHED_RELAX_DOMAIN_LEVEL:
		s += sprintf(s, "%d", cs->relax_domain_level);
		break;
	case FILE_MEMORY_MIGRATE:
		*s++ = is_memory_migrate(cs) ? '1' : '0';
		break;
	case FILE_MEMORY_PRESSURE_ENABLED:
		*s++ = cpuset_memory_pressure_enabled ? '1' : '0';
		break;
	case FILE_MEMORY_PRESSURE:
		s += sprintf(s, "%d", fmeter_getrate(&cs->fmeter));
		break;
	case FILE_SPREAD_PAGE:
		*s++ = is_spread_page(cs) ? '1' : '0';
		break;
	case FILE_SPREAD_SLAB:
		*s++ = is_spread_slab(cs) ? '1' : '0';
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





/*
 * for the common functions, 'private' gives the type of file
 */

static struct cftype cft_cpus = {
	.name = "cpus",
	.read = cpuset_common_file_read,
	.write = cpuset_common_file_write,
	.private = FILE_CPULIST,
};

static struct cftype cft_mems = {
	.name = "mems",
	.read = cpuset_common_file_read,
	.write = cpuset_common_file_write,
	.private = FILE_MEMLIST,
};

static struct cftype cft_cpu_exclusive = {
	.name = "cpu_exclusive",
	.read = cpuset_common_file_read,
	.write = cpuset_common_file_write,
	.private = FILE_CPU_EXCLUSIVE,
};

static struct cftype cft_mem_exclusive = {
	.name = "mem_exclusive",
	.read = cpuset_common_file_read,
	.write = cpuset_common_file_write,
	.private = FILE_MEM_EXCLUSIVE,
};

static struct cftype cft_sched_load_balance = {
	.name = "sched_load_balance",
	.read = cpuset_common_file_read,
	.write = cpuset_common_file_write,
	.private = FILE_SCHED_LOAD_BALANCE,
};

static struct cftype cft_sched_relax_domain_level = {
	.name = "sched_relax_domain_level",
	.read = cpuset_common_file_read,
	.write = cpuset_common_file_write,
	.private = FILE_SCHED_RELAX_DOMAIN_LEVEL,
};

static struct cftype cft_memory_migrate = {
	.name = "memory_migrate",
	.read = cpuset_common_file_read,
	.write = cpuset_common_file_write,
	.private = FILE_MEMORY_MIGRATE,
};

static struct cftype cft_memory_pressure_enabled = {
	.name = "memory_pressure_enabled",
	.read = cpuset_common_file_read,
	.write = cpuset_common_file_write,
	.private = FILE_MEMORY_PRESSURE_ENABLED,
};

static struct cftype cft_memory_pressure = {
	.name = "memory_pressure",
	.read = cpuset_common_file_read,
	.write = cpuset_common_file_write,
	.private = FILE_MEMORY_PRESSURE,
};

static struct cftype cft_spread_page = {
	.name = "memory_spread_page",
	.read = cpuset_common_file_read,
	.write = cpuset_common_file_write,
	.private = FILE_SPREAD_PAGE,
};

static struct cftype cft_spread_slab = {
	.name = "memory_spread_slab",
	.read = cpuset_common_file_read,
	.write = cpuset_common_file_write,
	.private = FILE_SPREAD_SLAB,
};

static int cpuset_populate(struct cgroup_subsys *ss, struct cgroup *cont)
{
	int err;

	if ((err = cgroup_add_file(cont, ss, &cft_cpus)) < 0)
		return err;
	if ((err = cgroup_add_file(cont, ss, &cft_mems)) < 0)
		return err;
	if ((err = cgroup_add_file(cont, ss, &cft_cpu_exclusive)) < 0)
		return err;
	if ((err = cgroup_add_file(cont, ss, &cft_mem_exclusive)) < 0)
		return err;
	if ((err = cgroup_add_file(cont, ss, &cft_memory_migrate)) < 0)
		return err;
	if ((err = cgroup_add_file(cont, ss, &cft_sched_load_balance)) < 0)
		return err;
	if ((err = cgroup_add_file(cont, ss,
					&cft_sched_relax_domain_level)) < 0)
		return err;
	if ((err = cgroup_add_file(cont, ss, &cft_memory_pressure)) < 0)
		return err;
	if ((err = cgroup_add_file(cont, ss, &cft_spread_page)) < 0)
		return err;
	if ((err = cgroup_add_file(cont, ss, &cft_spread_slab)) < 0)
		return err;
	/* memory_pressure_enabled is in root cpuset only */
	if (err == 0 && !cont->parent)
		err = cgroup_add_file(cont, ss,
					 &cft_memory_pressure_enabled);
	return 0;
}

/*
 * post_clone() is called at the end of cgroup_clone().
 * 'cgroup' was just created automatically as a result of
 * a cgroup_clone(), and the current task is about to
 * be moved into 'cgroup'.
 *
 * Currently we refuse to set up the cgroup - thereby
 * refusing the task to be entered, and as a result refusing
 * the sys_unshare() or clone() which initiated it - if any
 * sibling cpusets have exclusive cpus or mem.
 *
 * If this becomes a problem for some users who wish to
 * allow that scenario, then cpuset_post_clone() could be
 * changed to grant parent->cpus_allowed-sibling_cpus_exclusive
 * (and likewise for mems) to the new cgroup. Called with cgroup_mutex
 * held.
 */
static void cpuset_post_clone(struct cgroup_subsys *ss,
			      struct cgroup *cgroup)
{
	struct cgroup *parent, *child;
	struct cpuset *cs, *parent_cs;

	parent = cgroup->parent;
	list_for_each_entry(child, &parent->children, sibling) {
		cs = cgroup_cs(child);
		if (is_mem_exclusive(cs) || is_cpu_exclusive(cs))
			return;
	}
	cs = cgroup_cs(cgroup);
	parent_cs = cgroup_cs(parent);

	cs->mems_allowed = parent_cs->mems_allowed;
	cs->cpus_allowed = parent_cs->cpus_allowed;
	return;
}

/*
 *	cpuset_create - create a cpuset
 *	ss:	cpuset cgroup subsystem
 *	cont:	control group that the new cpuset will be part of
 */

static struct cgroup_subsys_state *cpuset_create(
	struct cgroup_subsys *ss,
	struct cgroup *cont)
{
	struct cpuset *cs;
	struct cpuset *parent;

	if (!cont->parent) {
		/* This is early initialization for the top cgroup */
		top_cpuset.mems_generation = cpuset_mems_generation++;
		return &top_cpuset.css;
	}
	parent = cgroup_cs(cont->parent);
	cs = kmalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs)
		return ERR_PTR(-ENOMEM);

	cpuset_update_task_memory_state();
	cs->flags = 0;
	if (is_spread_page(parent))
		set_bit(CS_SPREAD_PAGE, &cs->flags);
	if (is_spread_slab(parent))
		set_bit(CS_SPREAD_SLAB, &cs->flags);
	set_bit(CS_SCHED_LOAD_BALANCE, &cs->flags);
	cpus_clear(cs->cpus_allowed);
	nodes_clear(cs->mems_allowed);
	cs->mems_generation = cpuset_mems_generation++;
	fmeter_init(&cs->fmeter);
	cs->relax_domain_level = -1;

	cs->parent = parent;
	number_of_cpusets++;
	return &cs->css ;
}

/*
 * Locking note on the strange update_flag() call below:
 *
 * If the cpuset being removed has its flag 'sched_load_balance'
 * enabled, then simulate turning sched_load_balance off, which
 * will call rebuild_sched_domains().  The get_online_cpus()
 * call in rebuild_sched_domains() must not be made while holding
 * callback_mutex.  Elsewhere the kernel nests callback_mutex inside
 * get_online_cpus() calls.  So the reverse nesting would risk an
 * ABBA deadlock.
 */

static void cpuset_destroy(struct cgroup_subsys *ss, struct cgroup *cont)
{
	struct cpuset *cs = cgroup_cs(cont);

	cpuset_update_task_memory_state();

	if (is_sched_load_balance(cs))
		update_flag(CS_SCHED_LOAD_BALANCE, cs, "0");

	number_of_cpusets--;
	kfree(cs);
}

struct cgroup_subsys cpuset_subsys = {
	.name = "cpuset",
	.create = cpuset_create,
	.destroy  = cpuset_destroy,
	.can_attach = cpuset_can_attach,
	.attach = cpuset_attach,
	.populate = cpuset_populate,
	.post_clone = cpuset_post_clone,
	.subsys_id = cpuset_subsys_id,
	.early_init = 1,
};

/*
 * cpuset_init_early - just enough so that the calls to
 * cpuset_update_task_memory_state() in early init code
 * are harmless.
 */

int __init cpuset_init_early(void)
{
	top_cpuset.mems_generation = cpuset_mems_generation++;
	return 0;
}


/**
 * cpuset_init - initialize cpusets at system boot
 *
 * Description: Initialize top_cpuset and the cpuset internal file system,
 **/

int __init cpuset_init(void)
{
	int err = 0;

	cpus_setall(top_cpuset.cpus_allowed);
	nodes_setall(top_cpuset.mems_allowed);

	fmeter_init(&top_cpuset.fmeter);
	top_cpuset.mems_generation = cpuset_mems_generation++;
	set_bit(CS_SCHED_LOAD_BALANCE, &top_cpuset.flags);
	top_cpuset.relax_domain_level = -1;

	err = register_filesystem(&cpuset_fs_type);
	if (err < 0)
		return err;

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
void cpuset_do_move_task(struct task_struct *tsk, struct cgroup_scanner *scan)
{
	struct cpuset_hotplug_scanner *chsp;

	chsp = container_of(scan, struct cpuset_hotplug_scanner, scan);
	cgroup_attach_task(chsp->to, tsk);
}

/**
 * move_member_tasks_to_cpuset - move tasks from one cpuset to another
 * @from: cpuset in which the tasks currently reside
 * @to: cpuset to which the tasks will be moved
 *
 * Called with cgroup_mutex held
 * callback_mutex must not be held, as cpuset_attach() will take it.
 *
 * The cgroup_scan_tasks() function will scan all the tasks in a cgroup,
 * calling callback functions for each.
 */
static void move_member_tasks_to_cpuset(struct cpuset *from, struct cpuset *to)
{
	struct cpuset_hotplug_scanner scan;

	scan.scan.cg = from->css.cgroup;
	scan.scan.test_task = NULL; /* select all tasks in cgroup */
	scan.scan.process_task = cpuset_do_move_task;
	scan.scan.heap = NULL;
	scan.to = to->css.cgroup;

	if (cgroup_scan_tasks((struct cgroup_scanner *)&scan))
		printk(KERN_ERR "move_member_tasks_to_cpuset: "
				"cgroup_scan_tasks failed\n");
}

/*
 * If common_cpu_mem_hotplug_unplug(), below, unplugs any CPUs
 * or memory nodes, we need to walk over the cpuset hierarchy,
 * removing that CPU or node from all cpusets.  If this removes the
 * last CPU or node from a cpuset, then move the tasks in the empty
 * cpuset to its next-highest non-empty parent.
 *
 * Called with cgroup_mutex held
 * callback_mutex must not be held, as cpuset_attach() will take it.
 */
static void remove_tasks_in_empty_cpuset(struct cpuset *cs)
{
	struct cpuset *parent;

	/*
	 * The cgroup's css_sets list is in use if there are tasks
	 * in the cpuset; the list is empty if there are none;
	 * the cs->css.refcnt seems always 0.
	 */
	if (list_empty(&cs->css.cgroup->css_sets))
		return;

	/*
	 * Find its next-highest non-empty parent, (top cpuset
	 * has online cpus, so can't be empty).
	 */
	parent = cs->parent;
	while (cpus_empty(parent->cpus_allowed) ||
			nodes_empty(parent->mems_allowed))
		parent = parent->parent;

	move_member_tasks_to_cpuset(cs, parent);
}

/*
 * Walk the specified cpuset subtree and look for empty cpusets.
 * The tasks of such cpuset must be moved to a parent cpuset.
 *
 * Called with cgroup_mutex held.  We take callback_mutex to modify
 * cpus_allowed and mems_allowed.
 *
 * This walk processes the tree from top to bottom, completing one layer
 * before dropping down to the next.  It always processes a node before
 * any of its children.
 *
 * For now, since we lack memory hot unplug, we'll never see a cpuset
 * that has tasks along with an empty 'mems'.  But if we did see such
 * a cpuset, we'd handle it just like we do if its 'cpus' was empty.
 */
static void scan_for_empty_cpusets(const struct cpuset *root)
{
	struct cpuset *cp;	/* scans cpusets being updated */
	struct cpuset *child;	/* scans child cpusets of cp */
	struct list_head queue;
	struct cgroup *cont;

	INIT_LIST_HEAD(&queue);

	list_add_tail((struct list_head *)&root->stack_list, &queue);

	while (!list_empty(&queue)) {
		cp = container_of(queue.next, struct cpuset, stack_list);
		list_del(queue.next);
		list_for_each_entry(cont, &cp->css.cgroup->children, sibling) {
			child = cgroup_cs(cont);
			list_add_tail(&child->stack_list, &queue);
		}
		cont = cp->css.cgroup;

		/* Continue past cpusets with all cpus, mems online */
		if (cpus_subset(cp->cpus_allowed, cpu_online_map) &&
		    nodes_subset(cp->mems_allowed, node_states[N_HIGH_MEMORY]))
			continue;

		/* Remove offline cpus and mems from this cpuset. */
		mutex_lock(&callback_mutex);
		cpus_and(cp->cpus_allowed, cp->cpus_allowed, cpu_online_map);
		nodes_and(cp->mems_allowed, cp->mems_allowed,
						node_states[N_HIGH_MEMORY]);
		mutex_unlock(&callback_mutex);

		/* Move tasks from the empty cpuset to a parent */
		if (cpus_empty(cp->cpus_allowed) ||
		     nodes_empty(cp->mems_allowed))
			remove_tasks_in_empty_cpuset(cp);
	}
}

/*
 * The cpus_allowed and mems_allowed nodemasks in the top_cpuset track
 * cpu_online_map and node_states[N_HIGH_MEMORY].  Force the top cpuset to
 * track what's online after any CPU or memory node hotplug or unplug event.
 *
 * Since there are two callers of this routine, one for CPU hotplug
 * events and one for memory node hotplug events, we could have coded
 * two separate routines here.  We code it as a single common routine
 * in order to minimize text size.
 */

static void common_cpu_mem_hotplug_unplug(void)
{
	cgroup_lock();

	top_cpuset.cpus_allowed = cpu_online_map;
	top_cpuset.mems_allowed = node_states[N_HIGH_MEMORY];
	scan_for_empty_cpusets(&top_cpuset);

	cgroup_unlock();
}

/*
 * The top_cpuset tracks what CPUs and Memory Nodes are online,
 * period.  This is necessary in order to make cpusets transparent
 * (of no affect) on systems that are actively using CPU hotplug
 * but making no active use of cpusets.
 *
 * This routine ensures that top_cpuset.cpus_allowed tracks
 * cpu_online_map on each CPU hotplug (cpuhp) event.
 */

static int cpuset_handle_cpuhp(struct notifier_block *unused_nb,
				unsigned long phase, void *unused_cpu)
{
	if (phase == CPU_DYING || phase == CPU_DYING_FROZEN)
		return NOTIFY_DONE;

	common_cpu_mem_hotplug_unplug();
	return 0;
}

#ifdef CONFIG_MEMORY_HOTPLUG
/*
 * Keep top_cpuset.mems_allowed tracking node_states[N_HIGH_MEMORY].
 * Call this routine anytime after you change
 * node_states[N_HIGH_MEMORY].
 * See also the previous routine cpuset_handle_cpuhp().
 */

void cpuset_track_online_nodes(void)
{
	common_cpu_mem_hotplug_unplug();
}
#endif

/**
 * cpuset_init_smp - initialize cpus_allowed
 *
 * Description: Finish top cpuset after cpu, node maps are initialized
 **/

void __init cpuset_init_smp(void)
{
	top_cpuset.cpus_allowed = cpu_online_map;
	top_cpuset.mems_allowed = node_states[N_HIGH_MEMORY];

	hotcpu_notifier(cpuset_handle_cpuhp, 0);
}

/**

 * cpuset_cpus_allowed - return cpus_allowed mask from a tasks cpuset.
 * @tsk: pointer to task_struct from which to obtain cpuset->cpus_allowed.
 * @pmask: pointer to cpumask_t variable to receive cpus_allowed set.
 *
 * Description: Returns the cpumask_t cpus_allowed of the cpuset
 * attached to the specified @tsk.  Guaranteed to return some non-empty
 * subset of cpu_online_map, even if this means going outside the
 * tasks cpuset.
 **/

void cpuset_cpus_allowed(struct task_struct *tsk, cpumask_t *pmask)
{
	mutex_lock(&callback_mutex);
	cpuset_cpus_allowed_locked(tsk, pmask);
	mutex_unlock(&callback_mutex);
}

/**
 * cpuset_cpus_allowed_locked - return cpus_allowed mask from a tasks cpuset.
 * Must be called with callback_mutex held.
 **/
void cpuset_cpus_allowed_locked(struct task_struct *tsk, cpumask_t *pmask)
{
	task_lock(tsk);
	guarantee_online_cpus(task_cs(tsk), pmask);
	task_unlock(tsk);
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
 * subset of node_states[N_HIGH_MEMORY], even if this means going outside the
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
 * nearest_exclusive_ancestor() - Returns the nearest mem_exclusive
 * ancestor to the specified cpuset.  Call holding callback_mutex.
 * If no ancestor is mem_exclusive (an unusual configuration), then
 * returns the root cpuset.
 */
static const struct cpuset *nearest_exclusive_ancestor(const struct cpuset *cs)
{
	while (!is_mem_exclusive(cs) && cs->parent)
		cs = cs->parent;
	return cs;
}

/**
 * cpuset_zone_allowed_softwall - Can we allocate on zone z's memory node?
 * @z: is this zone on an allowed node?
 * @gfp_mask: memory allocation flags
 *
 * If we're in interrupt, yes, we can always allocate.  If
 * __GFP_THISNODE is set, yes, we can always allocate.  If zone
 * z's node is in our tasks mems_allowed, yes.  If it's not a
 * __GFP_HARDWALL request and this zone's nodes is in the nearest
 * mem_exclusive cpuset ancestor to this tasks cpuset, yes.
 * If the task has been OOM killed and has access to memory reserves
 * as specified by the TIF_MEMDIE flag, yes.
 * Otherwise, no.
 *
 * If __GFP_HARDWALL is set, cpuset_zone_allowed_softwall()
 * reduces to cpuset_zone_allowed_hardwall().  Otherwise,
 * cpuset_zone_allowed_softwall() might sleep, and might allow a zone
 * from an enclosing cpuset.
 *
 * cpuset_zone_allowed_hardwall() only handles the simpler case of
 * hardwall cpusets, and never sleeps.
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
 * nearest enclosing mem_exclusive ancestor cpuset.
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
 *	GFP_KERNEL   - any node in enclosing mem_exclusive cpuset ok
 *	GFP_USER     - only nodes in current tasks mems allowed ok.
 *
 * Rule:
 *    Don't call cpuset_zone_allowed_softwall if you can't sleep, unless you
 *    pass in the __GFP_HARDWALL flag set in gfp_flag, which disables
 *    the code that might scan up ancestor cpusets and sleep.
 */

int __cpuset_zone_allowed_softwall(struct zone *z, gfp_t gfp_mask)
{
	int node;			/* node that zone z is on */
	const struct cpuset *cs;	/* current cpuset ancestors */
	int allowed;			/* is allocation in zone z allowed? */

	if (in_interrupt() || (gfp_mask & __GFP_THISNODE))
		return 1;
	node = zone_to_nid(z);
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
	cs = nearest_exclusive_ancestor(task_cs(current));
	task_unlock(current);

	allowed = node_isset(node, cs->mems_allowed);
	mutex_unlock(&callback_mutex);
	return allowed;
}

/*
 * cpuset_zone_allowed_hardwall - Can we allocate on zone z's memory node?
 * @z: is this zone on an allowed node?
 * @gfp_mask: memory allocation flags
 *
 * If we're in interrupt, yes, we can always allocate.
 * If __GFP_THISNODE is set, yes, we can always allocate.  If zone
 * z's node is in our tasks mems_allowed, yes.   If the task has been
 * OOM killed and has access to memory reserves as specified by the
 * TIF_MEMDIE flag, yes.  Otherwise, no.
 *
 * The __GFP_THISNODE placement logic is really handled elsewhere,
 * by forcibly using a zonelist starting at a specified node, and by
 * (in get_page_from_freelist()) refusing to consider the zones for
 * any node on the zonelist except the first.  By the time any such
 * calls get to this routine, we should just shut up and say 'yes'.
 *
 * Unlike the cpuset_zone_allowed_softwall() variant, above,
 * this variant requires that the zone be in the current tasks
 * mems_allowed or that we're in interrupt.  It does not scan up the
 * cpuset hierarchy for the nearest enclosing mem_exclusive cpuset.
 * It never sleeps.
 */

int __cpuset_zone_allowed_hardwall(struct zone *z, gfp_t gfp_mask)
{
	int node;			/* node that zone z is on */

	if (in_interrupt() || (gfp_mask & __GFP_THISNODE))
		return 1;
	node = zone_to_nid(z);
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
 * cpuset_lock - lock out any changes to cpuset structures
 *
 * The out of memory (oom) code needs to mutex_lock cpusets
 * from being changed while it scans the tasklist looking for a
 * task in an overlapping cpuset.  Expose callback_mutex via this
 * cpuset_lock() routine, so the oom code can lock it, before
 * locking the task list.  The tasklist_lock is a spinlock, so
 * must be taken inside callback_mutex.
 */

void cpuset_lock(void)
{
	mutex_lock(&callback_mutex);
}

/**
 * cpuset_unlock - release lock on cpuset changes
 *
 * Undo the lock taken in a previous cpuset_lock() call.
 */

void cpuset_unlock(void)
{
	mutex_unlock(&callback_mutex);
}

/**
 * cpuset_mem_spread_node() - On which node to begin search for a page
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

int cpuset_mem_spread_node(void)
{
	int node;

	node = next_node(current->cpuset_mem_spread_rotor, current->mems_allowed);
	if (node == MAX_NUMNODES)
		node = first_node(current->mems_allowed);
	current->cpuset_mem_spread_rotor = node;
	return node;
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
 *    and we take cgroup_mutex, keeping cpuset_attach() from changing it
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

	retval = -EINVAL;
	cgroup_lock();
	css = task_subsys_state(tsk, cpuset_subsys_id);
	retval = cgroup_path(css->cgroup, buf, PAGE_SIZE);
	if (retval < 0)
		goto out_unlock;
	seq_puts(m, buf);
	seq_putc(m, '\n');
out_unlock:
	cgroup_unlock();
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

/* Display task cpus_allowed, mems_allowed in /proc/<pid>/status file. */
void cpuset_task_status_allowed(struct seq_file *m, struct task_struct *task)
{
	seq_printf(m, "Cpus_allowed:\t");
	m->count += cpumask_scnprintf(m->buf + m->count, m->size - m->count,
					task->cpus_allowed);
	seq_printf(m, "\n");
	seq_printf(m, "Cpus_allowed_list:\t");
	m->count += cpulist_scnprintf(m->buf + m->count, m->size - m->count,
					task->cpus_allowed);
	seq_printf(m, "\n");
	seq_printf(m, "Mems_allowed:\t");
	m->count += nodemask_scnprintf(m->buf + m->count, m->size - m->count,
					task->mems_allowed);
	seq_printf(m, "\n");
	seq_printf(m, "Mems_allowed_list:\t");
	m->count += nodelist_scnprintf(m->buf + m->count, m->size - m->count,
					task->mems_allowed);
	seq_printf(m, "\n");
}
