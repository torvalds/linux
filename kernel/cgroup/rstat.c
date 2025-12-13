// SPDX-License-Identifier: GPL-2.0-only
#include "cgroup-internal.h"

#include <linux/sched/cputime.h>

#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>

#include <trace/events/cgroup.h>

static DEFINE_SPINLOCK(rstat_base_lock);
static DEFINE_PER_CPU(struct llist_head, rstat_backlog_list);

static void cgroup_base_stat_flush(struct cgroup *cgrp, int cpu);

/*
 * Determines whether a given css can participate in rstat.
 * css's that are cgroup::self use rstat for base stats.
 * Other css's associated with a subsystem use rstat only when
 * they define the ss->css_rstat_flush callback.
 */
static inline bool css_uses_rstat(struct cgroup_subsys_state *css)
{
	return css_is_self(css) || css->ss->css_rstat_flush != NULL;
}

static struct css_rstat_cpu *css_rstat_cpu(
		struct cgroup_subsys_state *css, int cpu)
{
	return per_cpu_ptr(css->rstat_cpu, cpu);
}

static struct cgroup_rstat_base_cpu *cgroup_rstat_base_cpu(
		struct cgroup *cgrp, int cpu)
{
	return per_cpu_ptr(cgrp->rstat_base_cpu, cpu);
}

static spinlock_t *ss_rstat_lock(struct cgroup_subsys *ss)
{
	if (ss)
		return &ss->rstat_ss_lock;

	return &rstat_base_lock;
}

static inline struct llist_head *ss_lhead_cpu(struct cgroup_subsys *ss, int cpu)
{
	if (ss)
		return per_cpu_ptr(ss->lhead, cpu);
	return per_cpu_ptr(&rstat_backlog_list, cpu);
}

/**
 * css_rstat_updated - keep track of updated rstat_cpu
 * @css: target cgroup subsystem state
 * @cpu: cpu on which rstat_cpu was updated
 *
 * Atomically inserts the css in the ss's llist for the given cpu. This is
 * reentrant safe i.e. safe against softirq, hardirq and nmi. The ss's llist
 * will be processed at the flush time to create the update tree.
 *
 * NOTE: if the user needs the guarantee that the updater either add itself in
 * the lockless list or the concurrent flusher flushes its updated stats, a
 * memory barrier is needed before the call to css_rstat_updated() i.e. a
 * barrier after updating the per-cpu stats and before calling
 * css_rstat_updated().
 */
__bpf_kfunc void css_rstat_updated(struct cgroup_subsys_state *css, int cpu)
{
	struct llist_head *lhead;
	struct css_rstat_cpu *rstatc;
	struct css_rstat_cpu __percpu *rstatc_pcpu;
	struct llist_node *self;

	/*
	 * Since bpf programs can call this function, prevent access to
	 * uninitialized rstat pointers.
	 */
	if (!css_uses_rstat(css))
		return;

	lockdep_assert_preemption_disabled();

	/*
	 * For archs withnot nmi safe cmpxchg or percpu ops support, ignore
	 * the requests from nmi context.
	 */
	if ((!IS_ENABLED(CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG) ||
	     !IS_ENABLED(CONFIG_ARCH_HAS_NMI_SAFE_THIS_CPU_OPS)) && in_nmi())
		return;

	rstatc = css_rstat_cpu(css, cpu);
	/*
	 * If already on list return. This check is racy and smp_mb() is needed
	 * to pair it with the smp_mb() in css_process_update_tree() if the
	 * guarantee that the updated stats are visible to concurrent flusher is
	 * needed.
	 */
	if (llist_on_list(&rstatc->lnode))
		return;

	/*
	 * This function can be renentered by irqs and nmis for the same cgroup
	 * and may try to insert the same per-cpu lnode into the llist. Note
	 * that llist_add() does not protect against such scenarios.
	 *
	 * To protect against such stacked contexts of irqs/nmis, we use the
	 * fact that lnode points to itself when not on a list and then use
	 * this_cpu_cmpxchg() to atomically set to NULL to select the winner
	 * which will call llist_add(). The losers can assume the insertion is
	 * successful and the winner will eventually add the per-cpu lnode to
	 * the llist.
	 */
	self = &rstatc->lnode;
	rstatc_pcpu = css->rstat_cpu;
	if (this_cpu_cmpxchg(rstatc_pcpu->lnode.next, self, NULL) != self)
		return;

	lhead = ss_lhead_cpu(css->ss, cpu);
	llist_add(&rstatc->lnode, lhead);
}

static void __css_process_update_tree(struct cgroup_subsys_state *css, int cpu)
{
	/* put @css and all ancestors on the corresponding updated lists */
	while (true) {
		struct css_rstat_cpu *rstatc = css_rstat_cpu(css, cpu);
		struct cgroup_subsys_state *parent = css->parent;
		struct css_rstat_cpu *prstatc;

		/*
		 * Both additions and removals are bottom-up.  If a cgroup
		 * is already in the tree, all ancestors are.
		 */
		if (rstatc->updated_next)
			break;

		/* Root has no parent to link it to, but mark it busy */
		if (!parent) {
			rstatc->updated_next = css;
			break;
		}

		prstatc = css_rstat_cpu(parent, cpu);
		rstatc->updated_next = prstatc->updated_children;
		prstatc->updated_children = css;

		css = parent;
	}
}

static void css_process_update_tree(struct cgroup_subsys *ss, int cpu)
{
	struct llist_head *lhead = ss_lhead_cpu(ss, cpu);
	struct llist_node *lnode;

	while ((lnode = llist_del_first_init(lhead))) {
		struct css_rstat_cpu *rstatc;

		/*
		 * smp_mb() is needed here (more specifically in between
		 * init_llist_node() and per-cpu stats flushing) if the
		 * guarantee is required by a rstat user where etiher the
		 * updater should add itself on the lockless list or the
		 * flusher flush the stats updated by the updater who have
		 * observed that they are already on the list. The
		 * corresponding barrier pair for this one should be before
		 * css_rstat_updated() by the user.
		 *
		 * For now, there aren't any such user, so not adding the
		 * barrier here but if such a use-case arise, please add
		 * smp_mb() here.
		 */

		rstatc = container_of(lnode, struct css_rstat_cpu, lnode);
		__css_process_update_tree(rstatc->owner, cpu);
	}
}

/**
 * css_rstat_push_children - push children css's into the given list
 * @head: current head of the list (= subtree root)
 * @child: first child of the root
 * @cpu: target cpu
 * Return: A new singly linked list of css's to be flushed
 *
 * Iteratively traverse down the css_rstat_cpu updated tree level by
 * level and push all the parents first before their next level children
 * into a singly linked list via the rstat_flush_next pointer built from the
 * tail backward like "pushing" css's into a stack. The root is pushed by
 * the caller.
 */
static struct cgroup_subsys_state *css_rstat_push_children(
		struct cgroup_subsys_state *head,
		struct cgroup_subsys_state *child, int cpu)
{
	struct cgroup_subsys_state *cnext = child;	/* Next head of child css level */
	struct cgroup_subsys_state *ghead = NULL;	/* Head of grandchild css level */
	struct cgroup_subsys_state *parent, *grandchild;
	struct css_rstat_cpu *crstatc;

	child->rstat_flush_next = NULL;

	/*
	 * The subsystem rstat lock must be held for the whole duration from
	 * here as the rstat_flush_next list is being constructed to when
	 * it is consumed later in css_rstat_flush().
	 */
	lockdep_assert_held(ss_rstat_lock(head->ss));

	/*
	 * Notation: -> updated_next pointer
	 *	     => rstat_flush_next pointer
	 *
	 * Assuming the following sample updated_children lists:
	 *  P: C1 -> C2 -> P
	 *  C1: G11 -> G12 -> C1
	 *  C2: G21 -> G22 -> C2
	 *
	 * After 1st iteration:
	 *  head => C2 => C1 => NULL
	 *  ghead => G21 => G11 => NULL
	 *
	 * After 2nd iteration:
	 *  head => G12 => G11 => G22 => G21 => C2 => C1 => NULL
	 */
next_level:
	while (cnext) {
		child = cnext;
		cnext = child->rstat_flush_next;
		parent = child->parent;

		/* updated_next is parent cgroup terminated if !NULL */
		while (child != parent) {
			child->rstat_flush_next = head;
			head = child;
			crstatc = css_rstat_cpu(child, cpu);
			grandchild = crstatc->updated_children;
			if (grandchild != child) {
				/* Push the grand child to the next level */
				crstatc->updated_children = child;
				grandchild->rstat_flush_next = ghead;
				ghead = grandchild;
			}
			child = crstatc->updated_next;
			crstatc->updated_next = NULL;
		}
	}

	if (ghead) {
		cnext = ghead;
		ghead = NULL;
		goto next_level;
	}
	return head;
}

/**
 * css_rstat_updated_list - build a list of updated css's to be flushed
 * @root: root of the css subtree to traverse
 * @cpu: target cpu
 * Return: A singly linked list of css's to be flushed
 *
 * Walks the updated rstat_cpu tree on @cpu from @root.  During traversal,
 * each returned css is unlinked from the updated tree.
 *
 * The only ordering guarantee is that, for a parent and a child pair
 * covered by a given traversal, the child is before its parent in
 * the list.
 *
 * Note that updated_children is self terminated and points to a list of
 * child css's if not empty. Whereas updated_next is like a sibling link
 * within the children list and terminated by the parent css. An exception
 * here is the css root whose updated_next can be self terminated.
 */
static struct cgroup_subsys_state *css_rstat_updated_list(
		struct cgroup_subsys_state *root, int cpu)
{
	struct css_rstat_cpu *rstatc = css_rstat_cpu(root, cpu);
	struct cgroup_subsys_state *head = NULL, *parent, *child;

	css_process_update_tree(root->ss, cpu);

	/* Return NULL if this subtree is not on-list */
	if (!rstatc->updated_next)
		return NULL;

	/*
	 * Unlink @root from its parent. As the updated_children list is
	 * singly linked, we have to walk it to find the removal point.
	 */
	parent = root->parent;
	if (parent) {
		struct css_rstat_cpu *prstatc;
		struct cgroup_subsys_state **nextp;

		prstatc = css_rstat_cpu(parent, cpu);
		nextp = &prstatc->updated_children;
		while (*nextp != root) {
			struct css_rstat_cpu *nrstatc;

			nrstatc = css_rstat_cpu(*nextp, cpu);
			WARN_ON_ONCE(*nextp == parent);
			nextp = &nrstatc->updated_next;
		}
		*nextp = rstatc->updated_next;
	}

	rstatc->updated_next = NULL;

	/* Push @root to the list first before pushing the children */
	head = root;
	root->rstat_flush_next = NULL;
	child = rstatc->updated_children;
	rstatc->updated_children = root;
	if (child != root)
		head = css_rstat_push_children(head, child, cpu);

	return head;
}

/*
 * A hook for bpf stat collectors to attach to and flush their stats.
 * Together with providing bpf kfuncs for css_rstat_updated() and
 * css_rstat_flush(), this enables a complete workflow where bpf progs that
 * collect cgroup stats can integrate with rstat for efficient flushing.
 *
 * A static noinline declaration here could cause the compiler to optimize away
 * the function. A global noinline declaration will keep the definition, but may
 * optimize away the callsite. Therefore, __weak is needed to ensure that the
 * call is still emitted, by telling the compiler that we don't know what the
 * function might eventually be.
 */

__bpf_hook_start();

__weak noinline void bpf_rstat_flush(struct cgroup *cgrp,
				     struct cgroup *parent, int cpu)
{
}

__bpf_hook_end();

/*
 * Helper functions for locking.
 *
 * This makes it easier to diagnose locking issues and contention in
 * production environments.  The parameter @cpu_in_loop indicate lock
 * was released and re-taken when collection data from the CPUs. The
 * value -1 is used when obtaining the main lock else this is the CPU
 * number processed last.
 */
static inline void __css_rstat_lock(struct cgroup_subsys_state *css,
		int cpu_in_loop)
	__acquires(ss_rstat_lock(css->ss))
{
	struct cgroup *cgrp = css->cgroup;
	spinlock_t *lock;
	bool contended;

	lock = ss_rstat_lock(css->ss);
	contended = !spin_trylock_irq(lock);
	if (contended) {
		trace_cgroup_rstat_lock_contended(cgrp, cpu_in_loop, contended);
		spin_lock_irq(lock);
	}
	trace_cgroup_rstat_locked(cgrp, cpu_in_loop, contended);
}

static inline void __css_rstat_unlock(struct cgroup_subsys_state *css,
				      int cpu_in_loop)
	__releases(ss_rstat_lock(css->ss))
{
	struct cgroup *cgrp = css->cgroup;
	spinlock_t *lock;

	lock = ss_rstat_lock(css->ss);
	trace_cgroup_rstat_unlock(cgrp, cpu_in_loop, false);
	spin_unlock_irq(lock);
}

/**
 * css_rstat_flush - flush stats in @css's rstat subtree
 * @css: target cgroup subsystem state
 *
 * Collect all per-cpu stats in @css's subtree into the global counters
 * and propagate them upwards. After this function returns, all rstat
 * nodes in the subtree have up-to-date ->stat.
 *
 * This also gets all rstat nodes in the subtree including @css off the
 * ->updated_children lists.
 *
 * This function may block.
 */
__bpf_kfunc void css_rstat_flush(struct cgroup_subsys_state *css)
{
	int cpu;
	bool is_self = css_is_self(css);

	/*
	 * Since bpf programs can call this function, prevent access to
	 * uninitialized rstat pointers.
	 */
	if (!css_uses_rstat(css))
		return;

	might_sleep();
	for_each_possible_cpu(cpu) {
		struct cgroup_subsys_state *pos;

		/* Reacquire for each CPU to avoid disabling IRQs too long */
		__css_rstat_lock(css, cpu);
		pos = css_rstat_updated_list(css, cpu);
		for (; pos; pos = pos->rstat_flush_next) {
			if (is_self) {
				cgroup_base_stat_flush(pos->cgroup, cpu);
				bpf_rstat_flush(pos->cgroup,
						cgroup_parent(pos->cgroup), cpu);
			} else
				pos->ss->css_rstat_flush(pos, cpu);
		}
		__css_rstat_unlock(css, cpu);
		if (!cond_resched())
			cpu_relax();
	}
}

int css_rstat_init(struct cgroup_subsys_state *css)
{
	struct cgroup *cgrp = css->cgroup;
	int cpu;
	bool is_self = css_is_self(css);

	if (is_self) {
		/* the root cgrp has rstat_base_cpu preallocated */
		if (!cgrp->rstat_base_cpu) {
			cgrp->rstat_base_cpu = alloc_percpu(struct cgroup_rstat_base_cpu);
			if (!cgrp->rstat_base_cpu)
				return -ENOMEM;
		}
	} else if (css->ss->css_rstat_flush == NULL)
		return 0;

	/* the root cgrp's self css has rstat_cpu preallocated */
	if (!css->rstat_cpu) {
		css->rstat_cpu = alloc_percpu(struct css_rstat_cpu);
		if (!css->rstat_cpu) {
			if (is_self)
				free_percpu(cgrp->rstat_base_cpu);

			return -ENOMEM;
		}
	}

	/* ->updated_children list is self terminated */
	for_each_possible_cpu(cpu) {
		struct css_rstat_cpu *rstatc = css_rstat_cpu(css, cpu);

		rstatc->owner = rstatc->updated_children = css;
		init_llist_node(&rstatc->lnode);

		if (is_self) {
			struct cgroup_rstat_base_cpu *rstatbc;

			rstatbc = cgroup_rstat_base_cpu(cgrp, cpu);
			u64_stats_init(&rstatbc->bsync);
		}
	}

	return 0;
}

void css_rstat_exit(struct cgroup_subsys_state *css)
{
	int cpu;

	if (!css_uses_rstat(css))
		return;

	if (!css->rstat_cpu)
		return;

	css_rstat_flush(css);

	/* sanity check */
	for_each_possible_cpu(cpu) {
		struct css_rstat_cpu *rstatc = css_rstat_cpu(css, cpu);

		if (WARN_ON_ONCE(rstatc->updated_children != css) ||
		    WARN_ON_ONCE(rstatc->updated_next))
			return;
	}

	if (css_is_self(css)) {
		struct cgroup *cgrp = css->cgroup;

		free_percpu(cgrp->rstat_base_cpu);
		cgrp->rstat_base_cpu = NULL;
	}

	free_percpu(css->rstat_cpu);
	css->rstat_cpu = NULL;
}

/**
 * ss_rstat_init - subsystem-specific rstat initialization
 * @ss: target subsystem
 *
 * If @ss is NULL, the static locks associated with the base stats
 * are initialized. If @ss is non-NULL, the subsystem-specific locks
 * are initialized.
 */
int __init ss_rstat_init(struct cgroup_subsys *ss)
{
	int cpu;

	if (ss) {
		ss->lhead = alloc_percpu(struct llist_head);
		if (!ss->lhead)
			return -ENOMEM;
	}

	spin_lock_init(ss_rstat_lock(ss));
	for_each_possible_cpu(cpu)
		init_llist_head(ss_lhead_cpu(ss, cpu));

	return 0;
}

/*
 * Functions for cgroup basic resource statistics implemented on top of
 * rstat.
 */
static void cgroup_base_stat_add(struct cgroup_base_stat *dst_bstat,
				 struct cgroup_base_stat *src_bstat)
{
	dst_bstat->cputime.utime += src_bstat->cputime.utime;
	dst_bstat->cputime.stime += src_bstat->cputime.stime;
	dst_bstat->cputime.sum_exec_runtime += src_bstat->cputime.sum_exec_runtime;
#ifdef CONFIG_SCHED_CORE
	dst_bstat->forceidle_sum += src_bstat->forceidle_sum;
#endif
	dst_bstat->ntime += src_bstat->ntime;
}

static void cgroup_base_stat_sub(struct cgroup_base_stat *dst_bstat,
				 struct cgroup_base_stat *src_bstat)
{
	dst_bstat->cputime.utime -= src_bstat->cputime.utime;
	dst_bstat->cputime.stime -= src_bstat->cputime.stime;
	dst_bstat->cputime.sum_exec_runtime -= src_bstat->cputime.sum_exec_runtime;
#ifdef CONFIG_SCHED_CORE
	dst_bstat->forceidle_sum -= src_bstat->forceidle_sum;
#endif
	dst_bstat->ntime -= src_bstat->ntime;
}

static void cgroup_base_stat_flush(struct cgroup *cgrp, int cpu)
{
	struct cgroup_rstat_base_cpu *rstatbc = cgroup_rstat_base_cpu(cgrp, cpu);
	struct cgroup *parent = cgroup_parent(cgrp);
	struct cgroup_rstat_base_cpu *prstatbc;
	struct cgroup_base_stat delta;
	unsigned seq;

	/* Root-level stats are sourced from system-wide CPU stats */
	if (!parent)
		return;

	/* fetch the current per-cpu values */
	do {
		seq = __u64_stats_fetch_begin(&rstatbc->bsync);
		delta = rstatbc->bstat;
	} while (__u64_stats_fetch_retry(&rstatbc->bsync, seq));

	/* propagate per-cpu delta to cgroup and per-cpu global statistics */
	cgroup_base_stat_sub(&delta, &rstatbc->last_bstat);
	cgroup_base_stat_add(&cgrp->bstat, &delta);
	cgroup_base_stat_add(&rstatbc->last_bstat, &delta);
	cgroup_base_stat_add(&rstatbc->subtree_bstat, &delta);

	/* propagate cgroup and per-cpu global delta to parent (unless that's root) */
	if (cgroup_parent(parent)) {
		delta = cgrp->bstat;
		cgroup_base_stat_sub(&delta, &cgrp->last_bstat);
		cgroup_base_stat_add(&parent->bstat, &delta);
		cgroup_base_stat_add(&cgrp->last_bstat, &delta);

		delta = rstatbc->subtree_bstat;
		prstatbc = cgroup_rstat_base_cpu(parent, cpu);
		cgroup_base_stat_sub(&delta, &rstatbc->last_subtree_bstat);
		cgroup_base_stat_add(&prstatbc->subtree_bstat, &delta);
		cgroup_base_stat_add(&rstatbc->last_subtree_bstat, &delta);
	}
}

static struct cgroup_rstat_base_cpu *
cgroup_base_stat_cputime_account_begin(struct cgroup *cgrp, unsigned long *flags)
{
	struct cgroup_rstat_base_cpu *rstatbc;

	rstatbc = get_cpu_ptr(cgrp->rstat_base_cpu);
	*flags = u64_stats_update_begin_irqsave(&rstatbc->bsync);
	return rstatbc;
}

static void cgroup_base_stat_cputime_account_end(struct cgroup *cgrp,
						 struct cgroup_rstat_base_cpu *rstatbc,
						 unsigned long flags)
{
	u64_stats_update_end_irqrestore(&rstatbc->bsync, flags);
	css_rstat_updated(&cgrp->self, smp_processor_id());
	put_cpu_ptr(rstatbc);
}

void __cgroup_account_cputime(struct cgroup *cgrp, u64 delta_exec)
{
	struct cgroup_rstat_base_cpu *rstatbc;
	unsigned long flags;

	rstatbc = cgroup_base_stat_cputime_account_begin(cgrp, &flags);
	rstatbc->bstat.cputime.sum_exec_runtime += delta_exec;
	cgroup_base_stat_cputime_account_end(cgrp, rstatbc, flags);
}

void __cgroup_account_cputime_field(struct cgroup *cgrp,
				    enum cpu_usage_stat index, u64 delta_exec)
{
	struct cgroup_rstat_base_cpu *rstatbc;
	unsigned long flags;

	rstatbc = cgroup_base_stat_cputime_account_begin(cgrp, &flags);

	switch (index) {
	case CPUTIME_NICE:
		rstatbc->bstat.ntime += delta_exec;
		fallthrough;
	case CPUTIME_USER:
		rstatbc->bstat.cputime.utime += delta_exec;
		break;
	case CPUTIME_SYSTEM:
	case CPUTIME_IRQ:
	case CPUTIME_SOFTIRQ:
		rstatbc->bstat.cputime.stime += delta_exec;
		break;
#ifdef CONFIG_SCHED_CORE
	case CPUTIME_FORCEIDLE:
		rstatbc->bstat.forceidle_sum += delta_exec;
		break;
#endif
	default:
		break;
	}

	cgroup_base_stat_cputime_account_end(cgrp, rstatbc, flags);
}

/*
 * compute the cputime for the root cgroup by getting the per cpu data
 * at a global level, then categorizing the fields in a manner consistent
 * with how it is done by __cgroup_account_cputime_field for each bit of
 * cpu time attributed to a cgroup.
 */
static void root_cgroup_cputime(struct cgroup_base_stat *bstat)
{
	struct task_cputime *cputime = &bstat->cputime;
	int i;

	memset(bstat, 0, sizeof(*bstat));
	for_each_possible_cpu(i) {
		struct kernel_cpustat kcpustat;
		u64 *cpustat = kcpustat.cpustat;
		u64 user = 0;
		u64 sys = 0;

		kcpustat_cpu_fetch(&kcpustat, i);

		user += cpustat[CPUTIME_USER];
		user += cpustat[CPUTIME_NICE];
		cputime->utime += user;

		sys += cpustat[CPUTIME_SYSTEM];
		sys += cpustat[CPUTIME_IRQ];
		sys += cpustat[CPUTIME_SOFTIRQ];
		cputime->stime += sys;

		cputime->sum_exec_runtime += user;
		cputime->sum_exec_runtime += sys;

#ifdef CONFIG_SCHED_CORE
		bstat->forceidle_sum += cpustat[CPUTIME_FORCEIDLE];
#endif
		bstat->ntime += cpustat[CPUTIME_NICE];
	}
}


static void cgroup_force_idle_show(struct seq_file *seq, struct cgroup_base_stat *bstat)
{
#ifdef CONFIG_SCHED_CORE
	u64 forceidle_time = bstat->forceidle_sum;

	do_div(forceidle_time, NSEC_PER_USEC);
	seq_printf(seq, "core_sched.force_idle_usec %llu\n", forceidle_time);
#endif
}

void cgroup_base_stat_cputime_show(struct seq_file *seq)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;
	struct cgroup_base_stat bstat;

	if (cgroup_parent(cgrp)) {
		css_rstat_flush(&cgrp->self);
		__css_rstat_lock(&cgrp->self, -1);
		bstat = cgrp->bstat;
		cputime_adjust(&cgrp->bstat.cputime, &cgrp->prev_cputime,
			       &bstat.cputime.utime, &bstat.cputime.stime);
		__css_rstat_unlock(&cgrp->self, -1);
	} else {
		root_cgroup_cputime(&bstat);
	}

	do_div(bstat.cputime.sum_exec_runtime, NSEC_PER_USEC);
	do_div(bstat.cputime.utime, NSEC_PER_USEC);
	do_div(bstat.cputime.stime, NSEC_PER_USEC);
	do_div(bstat.ntime, NSEC_PER_USEC);

	seq_printf(seq, "usage_usec %llu\n"
			"user_usec %llu\n"
			"system_usec %llu\n"
			"nice_usec %llu\n",
			bstat.cputime.sum_exec_runtime,
			bstat.cputime.utime,
			bstat.cputime.stime,
			bstat.ntime);

	cgroup_force_idle_show(seq, &bstat);
}

/* Add bpf kfuncs for css_rstat_updated() and css_rstat_flush() */
BTF_KFUNCS_START(bpf_rstat_kfunc_ids)
BTF_ID_FLAGS(func, css_rstat_updated)
BTF_ID_FLAGS(func, css_rstat_flush, KF_SLEEPABLE)
BTF_KFUNCS_END(bpf_rstat_kfunc_ids)

static const struct btf_kfunc_id_set bpf_rstat_kfunc_set = {
	.owner          = THIS_MODULE,
	.set            = &bpf_rstat_kfunc_ids,
};

static int __init bpf_rstat_kfunc_init(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING,
					 &bpf_rstat_kfunc_set);
}
late_initcall(bpf_rstat_kfunc_init);
