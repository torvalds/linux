/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF extensible scheduler class: Documentation/scheduler/sched-ext.rst
 *
 * Sub-scheduler hierarchy support.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Tejun Heo <tj@kernel.org>
 */
#ifndef _KERNEL_SCHED_EXT_SUB_H
#define _KERNEL_SCHED_EXT_SUB_H

#include "internal.h"

#ifdef CONFIG_EXT_SUB_SCHED

struct scx_sched *scx_skip_subtree_pre(struct scx_sched *pos, struct scx_sched *root);
struct scx_sched *scx_next_descendant_pre(struct scx_sched *pos, struct scx_sched *root);
void scx_set_task_sched(struct task_struct *p, struct scx_sched *sch);
struct cgroup *sch_cgroup(struct scx_sched *sch);
void set_cgroup_sched(struct cgroup *cgrp, struct scx_sched *sch);
void scx_pstack_recursion_on_dispatch(struct bpf_prog *prog);
void scx_pstack_recursion_on_caps_updated(struct bpf_prog *prog);
void drain_descendants(struct scx_sched *sch);
void scx_sub_disable(struct scx_sched *sch);
void scx_sub_enable_workfn(struct kthread_work *work);
bool scx_bpf_sub_dispatch(u64 cgroup_id, const struct bpf_prog_aux *aux);
void scx_free_pshards(struct scx_sched *sch);
s32 scx_alloc_pshards(struct scx_sched *sch);
void scx_init_root_caps(struct scx_sched *sch);
void scx_process_sync_ecaps(struct rq *rq, struct task_struct *prev);
void scx_unbypass_replay_ecaps(struct rq *rq, struct scx_sched *sch);
void scx_online_ecaps(struct rq *rq);
void scx_offline_ecaps(struct rq *rq);
void scx_discard_ecaps_to_sync(s32 cpu, struct scx_sched_pcpu *pcpu);
void scx_discard_stale_ecaps_syncs(void);
struct scx_dispatch_q *scx_resolve_local_dsq(struct scx_sched *sch, struct rq *rq,
					     struct task_struct *p, u64 *enq_flags);
bool scx_task_reenq_on_cap_revoke(struct rq *rq, struct task_struct *p);
void scx_reenq_reject(struct rq *rq);
void scx_rescue_charge(struct rq *rq, s64 delta_exec);
void scx_rescue_end(struct rq *rq);
bool scx_rescue_keep(struct rq *rq, struct task_struct *p);
void scx_rescue_flush(struct rq *rq);
void scx_rescue_dump(struct seq_buf *s, struct rq *rq);
void scx_rescue_set_knobs(struct scx_sched *sch);
void scx_rescue_init(struct rq *rq);

/*
 * cgrp->scx_sched is written by root/sub enable/disable under all of
 * scx_enable_mutex, scx_fork_rwsem and cgroup_mutex. A new cgroup inherits the
 * parent's sched under just cgroup_mutex but is not yet reachable by the other
 * two lock holders. Any one of the three locks stabilizes the association.
 */
static inline struct scx_sched *scx_cgroup_sched(struct cgroup *cgrp)
{
	return rcu_dereference_check(cgrp->scx_sched,
				     lockdep_is_held(&cgroup_mutex) ||
				     percpu_rwsem_is_held(&scx_fork_rwsem) ||
				     lockdep_is_held(&scx_enable_mutex));
}

static inline const char *sch_cgrp_path(struct scx_sched *sch)
{
	return sch->cgrp_path;
}

/* a dying sub's hot-path influence ends in scx_sched_free_rcu_work() */
static inline void scx_dec_has_subs(struct scx_sched *sch)
{
	if (sch->level)
		static_branch_dec(&__scx_has_subs);
}

#else	/* CONFIG_EXT_SUB_SCHED */

static inline struct scx_sched *scx_next_descendant_pre(struct scx_sched *pos, struct scx_sched *root) { return pos ? NULL : root; }
static inline struct scx_sched *scx_skip_subtree_pre(struct scx_sched *pos, struct scx_sched *root) { return NULL; }
static inline void scx_set_task_sched(struct task_struct *p, struct scx_sched *sch) {}
static inline struct cgroup *sch_cgroup(struct scx_sched *sch) { return NULL; }
static inline const char *sch_cgrp_path(struct scx_sched *sch) { return "/"; }
static inline void set_cgroup_sched(struct cgroup *cgrp, struct scx_sched *sch) {}
static inline void drain_descendants(struct scx_sched *sch) { }
static inline void scx_sub_disable(struct scx_sched *sch) { }
static inline void scx_free_pshards(struct scx_sched *sch) {}
static inline s32 scx_alloc_pshards(struct scx_sched *sch) { return 0; }
static inline void scx_init_root_caps(struct scx_sched *sch) {}
static inline void scx_process_sync_ecaps(struct rq *rq, struct task_struct *prev) {}
static inline void scx_unbypass_replay_ecaps(struct rq *rq, struct scx_sched *sch) {}
static inline void scx_online_ecaps(struct rq *rq) {}
static inline void scx_offline_ecaps(struct rq *rq) {}
static inline void scx_discard_ecaps_to_sync(s32 cpu, struct scx_sched_pcpu *pcpu) {}
static inline void scx_discard_stale_ecaps_syncs(void) {}
static inline struct scx_dispatch_q *scx_resolve_local_dsq(struct scx_sched *sch, struct rq *rq, struct task_struct *p, u64 *enq_flags) { return &rq->scx.local_dsq; }
static inline bool scx_task_reenq_on_cap_revoke(struct rq *rq, struct task_struct *p) { return false; }
static inline void scx_reenq_reject(struct rq *rq) {}
static inline void scx_rescue_charge(struct rq *rq, s64 delta_exec) {}
static inline void scx_rescue_end(struct rq *rq) {}
static inline bool scx_rescue_keep(struct rq *rq, struct task_struct *p) { return false; }
static inline void scx_rescue_flush(struct rq *rq) {}
static inline void scx_rescue_dump(struct seq_buf *s, struct rq *rq) {}
static inline void scx_rescue_set_knobs(struct scx_sched *sch) {}
static inline void scx_rescue_init(struct rq *rq) {}
static inline void scx_dec_has_subs(struct scx_sched *sch) {}

#endif	/* CONFIG_EXT_SUB_SCHED */

/**
 * scx_for_each_descendant_pre - pre-order walk of a sched's descendants
 * @pos: iteration cursor
 * @root: sched to walk the descendants of
 *
 * Walk @root's descendants. @root is included in the iteration and the first
 * node to be visited. Must be called with scx_enable_mutex, scx_sched_lock, or
 * RCU read lock.
 */
#define scx_for_each_descendant_pre(pos, root)					\
	for ((pos) = scx_next_descendant_pre(NULL, (root)); (pos);		\
	     (pos) = scx_next_descendant_pre((pos), (root)))

#ifdef CONFIG_EXT_SUB_SCHED

/**
 * scx_missing_caps - The caps in @needed that @sch lacks on @cpu
 * @sch: sched to test
 * @cpu: cpu to test on
 * @needed: bitmask of SCX_CAP_* values
 *
 * Return the caps in @needed that @sch lacks for @cpu, 0 if it holds them all.
 */
static inline u64 scx_missing_caps(struct scx_sched *sch, s32 cpu, u64 needed)
{
	u64 ecaps;

	/* no sub-scheds, no missing caps */
	if (!scx_has_subs())
		return 0;

	/* root holds every cap on every cpu */
	if (!sch->level)
		return 0;

	ecaps = READ_ONCE(per_cpu_ptr(sch->pcpu, cpu)->ecaps);

	return needed & ~ecaps;
}

/*
 * Cap semantics: which caps an action requires, and which caps a cap implies.
 * Keep all such mappings collected here.
 */

/* map @enq_flags to the SCX_CAP_* bit required for the local-DSQ insert */
static inline u64 scx_caps_for_enq(u64 enq_flags)
{
	/* a restored task must be put into the local DSQ regardless of caps */
	if (unlikely(enq_flags & SCX_ENQ_IGNORE_CAPS))
		return 0;
	if (enq_flags & SCX_ENQ_IMMED)
		return SCX_CAP_ENQ_IMMED;
	return SCX_CAP_ENQ;
}

/* map queued @p to the SCX_CAP_* bit required to stay on its local DSQ */
static inline u64 scx_caps_for_task(struct task_struct *p)
{
	if (p->scx.flags & SCX_TASK_IMMED)
		return SCX_CAP_ENQ_IMMED;
	return SCX_CAP_ENQ;
}

/* the cap @sch needs to preempt @rq's current task, 0 if none */
static inline u64 scx_caps_for_preempt(struct scx_sched *sch, struct rq *rq, u64 enq_flags)
{
	struct task_struct *curr = rq->curr;

	/* a kernel-forced placement preempts regardless of caps */
	if (unlikely(enq_flags & SCX_ENQ_IGNORE_CAPS))
		return 0;
	/* a non-ext task can't be preempted by ext, own-subtree needs no cap */
	if (curr->sched_class != &ext_sched_class ||
	    scx_is_descendant(scx_task_sched(curr), sch))
		return 0;
	return SCX_CAP_PREEMPT;
}

/* caps implied by holding @cap */
static inline u64 scx_caps_implied(u64 cap)
{
	switch (cap) {
	case SCX_CAP_PREEMPT:
		return SCX_CAP_ENQ | SCX_CAP_ENQ_IMMED;
	case SCX_CAP_ENQ:
		return SCX_CAP_ENQ_IMMED;
	}
	return 0;
}

/* may @p keep running on @rq's cpu? requires baseline cpu access */
static inline bool scx_task_can_stay_on_cpu(struct rq *rq, struct task_struct *p)
{
	if (!scx_has_subs())
		return true;

	/* a migration-disabled task is let in without caps, keep it likewise */
	if (unlikely(is_migration_disabled(p)))
		return true;

	return likely(!scx_missing_caps(scx_task_sched(p), cpu_of(rq), SCX_CAP_BASE));
}

/* the task admitted for rescue on @rq, NULL if none */
static inline struct task_struct *scx_rescuee(struct rq *rq)
{
	lockdep_assert_rq_held(rq);

	if (!scx_has_subs())
		return NULL;

	return rq->scx.rescue.curr;
}

#else	/* CONFIG_EXT_SUB_SCHED */

static inline u64 scx_missing_caps(struct scx_sched *sch, s32 cpu, u64 needed) { return 0; }
static inline u64 scx_caps_for_preempt(struct scx_sched *sch, struct rq *rq, u64 enq_flags) { return 0; }
static inline bool scx_task_can_stay_on_cpu(struct rq *rq, struct task_struct *p) { return true; }
static inline struct task_struct *scx_rescuee(struct rq *rq) { return NULL; }

#endif	/* CONFIG_EXT_SUB_SCHED */

#endif /* _KERNEL_SCHED_EXT_SUB_H */
