#include "cgroup-internal.h"

#include <linux/sched/cputime.h>

static DEFINE_MUTEX(cgroup_stat_mutex);
static DEFINE_PER_CPU(raw_spinlock_t, cgroup_cpu_stat_lock);

static struct cgroup_cpu_stat *cgroup_cpu_stat(struct cgroup *cgrp, int cpu)
{
	return per_cpu_ptr(cgrp->cpu_stat, cpu);
}

/**
 * cgroup_cpu_stat_updated - keep track of updated cpu_stat
 * @cgrp: target cgroup
 * @cpu: cpu on which cpu_stat was updated
 *
 * @cgrp's cpu_stat on @cpu was updated.  Put it on the parent's matching
 * cpu_stat->updated_children list.  See the comment on top of
 * cgroup_cpu_stat definition for details.
 */
static void cgroup_cpu_stat_updated(struct cgroup *cgrp, int cpu)
{
	raw_spinlock_t *cpu_lock = per_cpu_ptr(&cgroup_cpu_stat_lock, cpu);
	struct cgroup *parent;
	unsigned long flags;

	/*
	 * Speculative already-on-list test.  This may race leading to
	 * temporary inaccuracies, which is fine.
	 *
	 * Because @parent's updated_children is terminated with @parent
	 * instead of NULL, we can tell whether @cgrp is on the list by
	 * testing the next pointer for NULL.
	 */
	if (cgroup_cpu_stat(cgrp, cpu)->updated_next)
		return;

	raw_spin_lock_irqsave(cpu_lock, flags);

	/* put @cgrp and all ancestors on the corresponding updated lists */
	for (parent = cgroup_parent(cgrp); parent;
	     cgrp = parent, parent = cgroup_parent(cgrp)) {
		struct cgroup_cpu_stat *cstat = cgroup_cpu_stat(cgrp, cpu);
		struct cgroup_cpu_stat *pcstat = cgroup_cpu_stat(parent, cpu);

		/*
		 * Both additions and removals are bottom-up.  If a cgroup
		 * is already in the tree, all ancestors are.
		 */
		if (cstat->updated_next)
			break;

		cstat->updated_next = pcstat->updated_children;
		pcstat->updated_children = cgrp;
	}

	raw_spin_unlock_irqrestore(cpu_lock, flags);
}

/**
 * cgroup_cpu_stat_pop_updated - iterate and dismantle cpu_stat updated tree
 * @pos: current position
 * @root: root of the tree to traversal
 * @cpu: target cpu
 *
 * Walks the udpated cpu_stat tree on @cpu from @root.  %NULL @pos starts
 * the traversal and %NULL return indicates the end.  During traversal,
 * each returned cgroup is unlinked from the tree.  Must be called with the
 * matching cgroup_cpu_stat_lock held.
 *
 * The only ordering guarantee is that, for a parent and a child pair
 * covered by a given traversal, if a child is visited, its parent is
 * guaranteed to be visited afterwards.
 */
static struct cgroup *cgroup_cpu_stat_pop_updated(struct cgroup *pos,
						  struct cgroup *root, int cpu)
{
	struct cgroup_cpu_stat *cstat;
	struct cgroup *parent;

	if (pos == root)
		return NULL;

	/*
	 * We're gonna walk down to the first leaf and visit/remove it.  We
	 * can pick whatever unvisited node as the starting point.
	 */
	if (!pos)
		pos = root;
	else
		pos = cgroup_parent(pos);

	/* walk down to the first leaf */
	while (true) {
		cstat = cgroup_cpu_stat(pos, cpu);
		if (cstat->updated_children == pos)
			break;
		pos = cstat->updated_children;
	}

	/*
	 * Unlink @pos from the tree.  As the updated_children list is
	 * singly linked, we have to walk it to find the removal point.
	 * However, due to the way we traverse, @pos will be the first
	 * child in most cases. The only exception is @root.
	 */
	parent = cgroup_parent(pos);
	if (parent && cstat->updated_next) {
		struct cgroup_cpu_stat *pcstat = cgroup_cpu_stat(parent, cpu);
		struct cgroup_cpu_stat *ncstat;
		struct cgroup **nextp;

		nextp = &pcstat->updated_children;
		while (true) {
			ncstat = cgroup_cpu_stat(*nextp, cpu);
			if (*nextp == pos)
				break;

			WARN_ON_ONCE(*nextp == parent);
			nextp = &ncstat->updated_next;
		}

		*nextp = cstat->updated_next;
		cstat->updated_next = NULL;
	}

	return pos;
}

static void cgroup_stat_accumulate(struct cgroup_stat *dst_stat,
				   struct cgroup_stat *src_stat)
{
	dst_stat->cputime.utime += src_stat->cputime.utime;
	dst_stat->cputime.stime += src_stat->cputime.stime;
	dst_stat->cputime.sum_exec_runtime += src_stat->cputime.sum_exec_runtime;
}

static void cgroup_cpu_stat_flush_one(struct cgroup *cgrp, int cpu)
{
	struct cgroup *parent = cgroup_parent(cgrp);
	struct cgroup_cpu_stat *cstat = cgroup_cpu_stat(cgrp, cpu);
	struct task_cputime *last_cputime = &cstat->last_cputime;
	struct task_cputime cputime;
	struct cgroup_stat delta;
	unsigned seq;

	lockdep_assert_held(&cgroup_stat_mutex);

	/* fetch the current per-cpu values */
	do {
		seq = __u64_stats_fetch_begin(&cstat->sync);
		cputime = cstat->cputime;
	} while (__u64_stats_fetch_retry(&cstat->sync, seq));

	/* accumulate the deltas to propgate */
	delta.cputime.utime = cputime.utime - last_cputime->utime;
	delta.cputime.stime = cputime.stime - last_cputime->stime;
	delta.cputime.sum_exec_runtime = cputime.sum_exec_runtime -
					 last_cputime->sum_exec_runtime;
	*last_cputime = cputime;

	/* transfer the pending stat into delta */
	cgroup_stat_accumulate(&delta, &cgrp->pending_stat);
	memset(&cgrp->pending_stat, 0, sizeof(cgrp->pending_stat));

	/* propagate delta into the global stat and the parent's pending */
	cgroup_stat_accumulate(&cgrp->stat, &delta);
	if (parent)
		cgroup_stat_accumulate(&parent->pending_stat, &delta);
}

/* see cgroup_stat_flush() */
static void cgroup_stat_flush_locked(struct cgroup *cgrp)
{
	int cpu;

	lockdep_assert_held(&cgroup_stat_mutex);

	for_each_possible_cpu(cpu) {
		raw_spinlock_t *cpu_lock = per_cpu_ptr(&cgroup_cpu_stat_lock, cpu);
		struct cgroup *pos = NULL;

		raw_spin_lock_irq(cpu_lock);
		while ((pos = cgroup_cpu_stat_pop_updated(pos, cgrp, cpu)))
			cgroup_cpu_stat_flush_one(pos, cpu);
		raw_spin_unlock_irq(cpu_lock);
	}
}

/**
 * cgroup_stat_flush - flush stats in @cgrp's subtree
 * @cgrp: target cgroup
 *
 * Collect all per-cpu stats in @cgrp's subtree into the global counters
 * and propagate them upwards.  After this function returns, all cgroups in
 * the subtree have up-to-date ->stat.
 *
 * This also gets all cgroups in the subtree including @cgrp off the
 * ->updated_children lists.
 */
void cgroup_stat_flush(struct cgroup *cgrp)
{
	mutex_lock(&cgroup_stat_mutex);
	cgroup_stat_flush_locked(cgrp);
	mutex_unlock(&cgroup_stat_mutex);
}

static struct cgroup_cpu_stat *cgroup_cpu_stat_account_begin(struct cgroup *cgrp)
{
	struct cgroup_cpu_stat *cstat;

	cstat = get_cpu_ptr(cgrp->cpu_stat);
	u64_stats_update_begin(&cstat->sync);
	return cstat;
}

static void cgroup_cpu_stat_account_end(struct cgroup *cgrp,
					struct cgroup_cpu_stat *cstat)
{
	u64_stats_update_end(&cstat->sync);
	cgroup_cpu_stat_updated(cgrp, smp_processor_id());
	put_cpu_ptr(cstat);
}

void __cgroup_account_cputime(struct cgroup *cgrp, u64 delta_exec)
{
	struct cgroup_cpu_stat *cstat;

	cstat = cgroup_cpu_stat_account_begin(cgrp);
	cstat->cputime.sum_exec_runtime += delta_exec;
	cgroup_cpu_stat_account_end(cgrp, cstat);
}

void __cgroup_account_cputime_field(struct cgroup *cgrp,
				    enum cpu_usage_stat index, u64 delta_exec)
{
	struct cgroup_cpu_stat *cstat;

	cstat = cgroup_cpu_stat_account_begin(cgrp);

	switch (index) {
	case CPUTIME_USER:
	case CPUTIME_NICE:
		cstat->cputime.utime += delta_exec;
		break;
	case CPUTIME_SYSTEM:
	case CPUTIME_IRQ:
	case CPUTIME_SOFTIRQ:
		cstat->cputime.stime += delta_exec;
		break;
	default:
		break;
	}

	cgroup_cpu_stat_account_end(cgrp, cstat);
}

void cgroup_stat_show_cputime(struct seq_file *seq)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;
	u64 usage, utime, stime;

	if (!cgroup_parent(cgrp))
		return;

	mutex_lock(&cgroup_stat_mutex);

	cgroup_stat_flush_locked(cgrp);

	usage = cgrp->stat.cputime.sum_exec_runtime;
	cputime_adjust(&cgrp->stat.cputime, &cgrp->stat.prev_cputime,
		       &utime, &stime);

	mutex_unlock(&cgroup_stat_mutex);

	do_div(usage, NSEC_PER_USEC);
	do_div(utime, NSEC_PER_USEC);
	do_div(stime, NSEC_PER_USEC);

	seq_printf(seq, "usage_usec %llu\n"
		   "user_usec %llu\n"
		   "system_usec %llu\n",
		   usage, utime, stime);
}

int cgroup_stat_init(struct cgroup *cgrp)
{
	int cpu;

	/* the root cgrp has cpu_stat preallocated */
	if (!cgrp->cpu_stat) {
		cgrp->cpu_stat = alloc_percpu(struct cgroup_cpu_stat);
		if (!cgrp->cpu_stat)
			return -ENOMEM;
	}

	/* ->updated_children list is self terminated */
	for_each_possible_cpu(cpu) {
		struct cgroup_cpu_stat *cstat = cgroup_cpu_stat(cgrp, cpu);

		cstat->updated_children = cgrp;
		u64_stats_init(&cstat->sync);
	}

	prev_cputime_init(&cgrp->stat.prev_cputime);

	return 0;
}

void cgroup_stat_exit(struct cgroup *cgrp)
{
	int cpu;

	cgroup_stat_flush(cgrp);

	/* sanity check */
	for_each_possible_cpu(cpu) {
		struct cgroup_cpu_stat *cstat = cgroup_cpu_stat(cgrp, cpu);

		if (WARN_ON_ONCE(cstat->updated_children != cgrp) ||
		    WARN_ON_ONCE(cstat->updated_next))
			return;
	}

	free_percpu(cgrp->cpu_stat);
	cgrp->cpu_stat = NULL;
}

void __init cgroup_stat_boot(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		raw_spin_lock_init(per_cpu_ptr(&cgroup_cpu_stat_lock, cpu));

	BUG_ON(cgroup_stat_init(&cgrp_dfl_root.cgrp));
}
