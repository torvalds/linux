// SPDX-License-Identifier: GPL-2.0
/*
 * BPF extensible scheduler class: Documentation/scheduler/sched-ext.rst
 *
 * Sub-scheduler hierarchy support.
 *
 * A sub-scheduler is an scx_sched attached to a cgroup subtree under another
 * scx_sched. This file holds the sub-scheduler implementation: the scheduler
 * tree walk, capability delegation, per-shard cap state and its sync, and the
 * sub-scheduler enable/disable paths. The core dispatch/enqueue machinery it
 * builds on lives in ext.c.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Tejun Heo <tj@kernel.org>
 */
#include <linux/rhashtable.h>
#include "internal.h"
#include "cid.h"
#include "arena.h"
#include "sub.h"
#include "inlines.h"

#ifdef CONFIG_EXT_SUB_SCHED

/*
 * On while any sub-scheduler exists so that a root-only system doesn't pay for
 * the sub-sched portions of hot paths. See scx_has_subs().
 */
DEFINE_STATIC_KEY_FALSE(__scx_has_subs);

/*
 * On while a loaded sched implements ops.sub_cid_sched_updated(), see
 * scx_ops_cid_sched_updated_enable().
 */
static DEFINE_STATIC_KEY_FALSE(__scx_ops_cid_sched_updated_enabled);
static s32 scx_nr_ops_cid_sched_updated;	/* such scheds, under scx_enable_mutex */

/* latched at root enable before any rescue runs */
s32 scx_rescue_bw_1024;			/* 0 if disabled, see scx_rescue_enabled() */
static s64 scx_rescue_quantum_ns;
static s64 scx_rescue_sat_delta_ns;
static unsigned long scx_rescue_decay_halflife;
static unsigned long scx_rescue_overload_after;

/*
 * ecaps_report() waits here for its marked syncs. A cpu going down keeps
 * picking tasks after hotplug parks its irq_work thread. Its picks can process
 * a marked sync, e.g. a core scheduling sibling's. A regular irq_work queued
 * there is left behind on PREEMPT_RT, so the wakeup is hard irq work. A hard
 * irq can't take a wait queue's lock, which sleeps on PREEMPT_RT.
 * scx_enable_mutex serializes the reports, so the single waiter sleeps on an
 * rcuwait.
 */
static struct rcuwait ecaps_report_wait = __RCUWAIT_INITIALIZER(ecaps_report_wait);

/* a cpu going down waits here for its marked syncs, see scx_offline_ecaps() */
static DECLARE_WAIT_QUEUE_HEAD(ecaps_offline_waitq);

/*
 * The cpus that suspend or hibernation took down and that haven't come back.
 * Their losses are not delivered, see scx_offline_ecaps(). Hotplug updates the
 * set under cpus_write_lock(), and scx_sub_pm_end() clears it under
 * cpus_read_lock(). A cpu that fails to come back stays in the set until
 * regular hotplug brings it back or the PM cycle ends.
 */
static struct cpumask scx_frozen_cpus;
static DECLARE_WAIT_QUEUE_HEAD(scx_frozen_cpus_waitq);

/* between the PREPARE and POST notifications of a PM cycle */
static bool scx_pm_in_progress;

/**
 * scx_skip_subtree_pre - Skip @pos's subtree in a pre-order walk
 * @pos: current position
 * @root: walk root
 *
 * In a walk started by scx_next_descendant_pre(), continue past @pos's subtree:
 * return @pos's next sibling, or the closest ancestor's next sibling, or NULL
 * if @pos's subtree is the last under @root. Same locking rules.
 */
struct scx_sched *scx_skip_subtree_pre(struct scx_sched *pos, struct scx_sched *root)
{
	struct scx_sched *next;

	lockdep_assert(lockdep_is_held(&scx_enable_mutex) ||
		       lockdep_is_held(&scx_sched_lock) ||
		       rcu_read_lock_any_held());

	while (pos != root) {
		next = list_next_or_null_rcu(&scx_parent(pos)->children, &pos->sibling,
					     struct scx_sched, sibling);
		if (next)
			return next;
		pos = scx_parent(pos);
	}
	return NULL;
}

/**
 * scx_next_descendant_pre - find the next descendant for pre-order walk
 * @pos: the current position (%NULL to initiate traversal)
 * @root: sched whose descendants to walk
 *
 * To be used by scx_for_each_descendant_pre(). Find the next descendant to
 * visit for pre-order traversal of @root's descendants. @root is included in
 * the iteration and the first node to be visited.
 */
struct scx_sched *scx_next_descendant_pre(struct scx_sched *pos, struct scx_sched *root)
{
	struct scx_sched *next;

	lockdep_assert(lockdep_is_held(&scx_enable_mutex) ||
		       lockdep_is_held(&scx_sched_lock) ||
		       rcu_read_lock_any_held());

	/* if first iteration, visit @root */
	if (!pos)
		return root;

	/* visit the first child if exists */
	next = list_first_or_null_rcu(&pos->children, struct scx_sched, sibling);
	if (next)
		return next;

	/* no child, visit my or the closest ancestor's next sibling */
	return scx_skip_subtree_pre(pos, root);
}

/**
 * scx_cid_sched_id_from - Resolve what @from sees when @sch runs on a cid
 * @sch: sched whose task runs there, %NULL for none
 * @from: sched asking
 *
 * Return %SCX_CID_SCHED_NONE when @sch is %NULL or outside @from's subtree,
 * %SCX_CID_SCHED_SELF for @from itself, or else the cgroup id of the direct
 * child of @from on the way down to @sch.
 */
static u64 scx_cid_sched_id_from(struct scx_sched *sch, struct scx_sched *from)
{
	if (!sch || !scx_is_descendant(sch, from))
		return SCX_CID_SCHED_NONE;
	if (sch == from)
		return SCX_CID_SCHED_SELF;
	return sch->ancestors[from->level + 1]->ops.sub_cgroup_id;
}

/* notify @from of what it now sees running on @rq's cid */
static void scx_cid_notify_sched_updated(struct rq *rq, struct scx_sched *sch,
					 struct scx_sched *from)
{
	if (SCX_HAS_OP(from, sub_cid_sched_updated))
		SCX_CALL_OP(from, sub_cid_sched_updated, rq, __scx_cpu_to_cid(cpu_of(rq)),
			    scx_cid_sched_id_from(sch, from));
}

/**
 * scx_cid_sched_update - Update rq->scx.sched and notify the scheds affected
 * @rq: rq whose running session opens or closes
 * @sch: sched starting a session, %NULL to close the open one
 *
 * Every sched whose scx_cid_sched_id_from() value changes is notified of the
 * new one. Above the deepest common ancestor of the two scheds, both map to the
 * same child, so the walk stops there. With root R, children A and B, and A's
 * child A1, a switch from an A1 task to a B task reports:
 *
 *         R        R:  A -> B
 *        / \
 *       A   B      A:  A1 -> NONE      B: NONE -> SELF
 *       |
 *       A1         A1: SELF -> NONE
 *
 * When the task on one side is not an ext task, there is no common ancestor and
 * only the other side is walked.
 */
void scx_cid_sched_update(struct rq *rq, struct scx_sched *sch)
{
	struct scx_sched *prev = rq->scx.sched;
	s32 level, common = -1;

	lockdep_assert_rq_held(rq);

	if (!static_branch_unlikely(&__scx_ops_cid_sched_updated_enabled) || prev == sch)
		return;

	rq->scx.sched = sch;

	if (prev && sch) {
		for (level = min(prev->level, sch->level); level >= 0; level--)
			if (prev->ancestors[level] == sch->ancestors[level])
				break;
		common = level;
		scx_cid_notify_sched_updated(rq, sch, prev->ancestors[common]);
	}
	if (prev)
		for (level = common + 1; level <= prev->level; level++)
			scx_cid_notify_sched_updated(rq, sch, prev->ancestors[level]);
	if (sch)
		for (level = common + 1; level <= sch->level; level++)
			scx_cid_notify_sched_updated(rq, sch, sch->ancestors[level]);
}

/**
 * scx_ops_cid_sched_updated_enable - Maintain rq->scx.sched if @sch has the op
 * @sch: sched being enabled, its has_op filled in
 *
 * rq->scx.sched is maintained only while a loaded sched implements
 * ops.sub_cid_sched_updated(), so that everyone else pays one static branch per
 * session open or close. The first such sched turns the key on and then
 * initializes every rq from the task running there, in that order so that no
 * session change in between goes unrecorded. While the key is off every
 * rq->scx.sched is NULL, see scx_ops_cid_sched_updated_disable().
 */
void scx_ops_cid_sched_updated_enable(struct scx_sched *sch)
{
	s32 cpu;

	lockdep_assert_held(&scx_enable_mutex);

	if (!SCX_HAS_OP(sch, sub_cid_sched_updated) || scx_nr_ops_cid_sched_updated++)
		return;

	static_branch_enable(&__scx_ops_cid_sched_updated_enabled);

	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);
		struct task_struct *p;

		guard(rq_lock_irqsave)(rq);
		p = rq->donor;
		if (p->sched_class == &ext_sched_class &&
		    (p->scx.flags & SCX_TASK_RUN_TRACKED))
			rq->scx.sched = scx_task_sched(p);
	}
}

/**
 * scx_ops_cid_sched_updated_disable - Stop maintaining rq->scx.sched for @sch
 * @sch: sched being disabled, its tasks all gone
 *
 * Close the session on every rq that still points at @sch, before ops.exit(),
 * so that no sched is notified of @sch once it is gone. The last sched with the
 * op turns the key off first and clears every rq, so while the key is off every
 * rq->scx.sched is NULL. A sched whose enable failed before its has_op fill was
 * never counted, so has_op gates the decrement.
 */
void scx_ops_cid_sched_updated_disable(struct scx_sched *sch)
{
	bool last;
	s32 cpu;

	lockdep_assert_held(&scx_enable_mutex);

	last = SCX_HAS_OP(sch, sub_cid_sched_updated) && !--scx_nr_ops_cid_sched_updated;
	if (last)
		static_branch_disable(&__scx_ops_cid_sched_updated_enabled);

	/*
	 * Re-homing restarts a running task's session under its new sched. A
	 * task that blocked while its cpu's dispatch had the rq lock dropped is
	 * put and set next again without a restart, so rq->scx.sched keeps @sch
	 * until the pick completes:
	 *
	 *   cpu 0, __schedule()                 cpu 1, scx_sub_disable(@sch)
	 *   T of @sch blocks, stops running
	 *   dispatch drops the rq lock
	 *                                       re-home T, nothing to restart
	 *                                       this sweep
	 *   pick N, N's session opens
	 *
	 * Closing the session here reports NONE to @sch and its ancestors now
	 * and lets the pick report N's sched from NONE instead of from @sch.
	 */
	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);

		guard(rq_lock_irqsave)(rq);
		if (rq->scx.sched == sch)
			scx_cid_sched_update(rq, NULL);
		if (last)
			rq->scx.sched = NULL;
	}
}

static struct scx_sched *scx_find_sub_sched(u64 cgroup_id)
{
	return rhashtable_lookup(&scx_sched_hash, &cgroup_id,
				 scx_sched_hash_params);
}

void scx_set_task_sched(struct task_struct *p, struct scx_sched *sch)
{
	rcu_assign_pointer(p->scx.sched, sch);
}

struct cgroup *sch_cgroup(struct scx_sched *sch)
{
	return sch->cgrp;
}

/* for each descendant of @cgrp including self, set ->scx_sched to @sch */
void set_cgroup_sched(struct cgroup *cgrp, struct scx_sched *sch)
{
	struct cgroup *pos;
	struct cgroup_subsys_state *css;

	cgroup_for_each_live_descendant_pre(pos, css, cgrp)
		rcu_assign_pointer(pos->scx_sched, sch);
}

static void free_pshard(struct scx_pshard *pshard)
{
	struct scx_caps_updated *cu;

	if (!pshard)
		return;
	cu = &pshard->caps_updated;
	if (cu->cmask_arena_out)
		scx_arena_free(pshard->sch, cu->cmask_arena_out,
			       struct_size_t(struct scx_cmask, bits,
					     SCX_CMASK_NR_WORDS(pshard->nr_cids)));
	kfree(pshard);
}

void scx_free_pshards(struct scx_sched *sch)
{
	s32 si;

	if (!sch->pshard)
		return;
	for (si = 0; si < sch->nr_pshards; si++)
		free_pshard(sch->pshard[si]);
	kfree(sch->pshard);
}

static struct scx_pshard *alloc_pshard(struct scx_sched *sch, s32 shard_idx, s32 node)
{
	const struct scx_cid_shard *shard =
		&rcu_dereference_protected(scx_cid_shard_ranges,
					   lockdep_is_held(&scx_enable_mutex))[shard_idx];
	size_t cmask_size = struct_size_t(struct scx_cmask, bits,
					  SCX_CMASK_NR_WORDS(shard->nr_cids));
	struct scx_pshard *pshard;
	struct scx_caps_updated *cu;
	s32 i;

	pshard = kzalloc_node(sizeof(*pshard), GFP_KERNEL, node);
	if (!pshard)
		return NULL;

	raw_spin_lock_init(&pshard->lock);
	pshard->sch = sch;
	pshard->base = shard->base_cid;
	pshard->nr_cids = shard->nr_cids;

	for (i = 0; i < __SCX_NR_CAPS; i++)
		scx_cmask_init(&pshard->caps[i].cmask, shard->base_cid, shard->nr_cids);

	cu = &pshard->caps_updated;
	raw_spin_lock_init(&cu->lock);
	INIT_LIST_HEAD(&cu->node_in_flight);
	__scx_cmask_init(&cu->cmask, shard->base_cid, shard->nr_cids, SCX_CID_SHARD_MAX_CPUS);

	cu->cmask_arena_out = scx_arena_alloc(sch, cmask_size);
	if (!cu->cmask_arena_out) {
		free_pshard(pshard);
		return NULL;
	}

	scx_cmask_init(cu->cmask_arena_out, shard->base_cid, shard->nr_cids);

	return pshard;
}

s32 scx_alloc_pshards(struct scx_sched *sch)
{
	struct scx_pshard **pshard;
	s32 *shard_node;
	s32 si;

	if (!sch->is_cid_type || !sch->arena_pool)
		return 0;

	shard_node = rcu_dereference_protected(scx_shard_node,
					       lockdep_is_held(&scx_enable_mutex));

	pshard = kzalloc_objs(pshard[0], scx_nr_cid_shards);
	if (!pshard)
		return -ENOMEM;

	for (si = 0; si < scx_nr_cid_shards; si++) {
		pshard[si] = alloc_pshard(sch, si, shard_node[si]);
		if (!pshard[si]) {
			while (--si >= 0)
				free_pshard(pshard[si]);
			kfree(pshard);
			return -ENOMEM;
		}
	}

	sch->nr_pshards = scx_nr_cid_shards;
	/*
	 * Publish only after every entry is built so a reader observing
	 * @sch->pshard never sees a partially-filled array or unpublished cid
	 * tables. Pair the store with a barrier and an acquire load on the
	 * read side.
	 */
	smp_wmb();
	WRITE_ONCE(sch->pshard, pshard);
	return 0;
}

/*
 * Seed the root's caps fully. Root owns all cids on all caps at enable time.
 * Children acquire caps via scx_bpf_sub_grant().
 */
void scx_init_root_caps(struct scx_sched *sch)
{
	s32 si, i;

	for (si = 0; si < sch->nr_pshards; si++) {
		struct scx_pshard *ps = sch->pshard[si];

		for (i = 0; i < __SCX_NR_CAPS; i++)
			scx_cmask_fill(&ps->caps[i].cmask);
	}
}

/* unserved remainder of @rq's rescuee's admitted slice, 0 once fully served */
static s64 scx_rescue_slice_remaining(struct rq *rq)
{
	return max(rq->scx.rescue.slice - rq->scx.rescue.served, 0);
}

/*
 * Decay @pcpu's rescue usage average in place, halving per the knob-derived
 * halflife, see scx_rescue_set_knobs(). The timestamp advances only by whole
 * halflives.
 */
static u64 scx_rescue_decay_avg(struct scx_sched_pcpu *pcpu)
{
	unsigned long halflife = scx_rescue_decay_halflife;
	u64 n = div_u64(get_jiffies_64() - pcpu->rescue_avg_at, halflife);

	if (n) {
		pcpu->rescue_avg = n < 64 ? pcpu->rescue_avg >> n : 0;
		pcpu->rescue_avg_at += n * halflife;
	}
	return pcpu->rescue_avg;
}

/**
 * scx_rescue_charge - Charge the rescuee's runtime
 * @rq: rq the rescuee is running on
 * @delta_exec: runtime being charged
 *
 * Also ends the rescue once the admitted slice has been served in full. Ending
 * on served time rather than slice exhaustion bounds both the rescue and the
 * charging when a scheduler extends the rescuee's slice.
 */
void scx_rescue_charge(struct rq *rq, s64 delta_exec)
{
	struct scx_sched_pcpu *pcpu;

	lockdep_assert_rq_held(rq);

	/*
	 * Count @delta_exec as served before the clamp below, which bounds only
	 * the budget charge. update_curr_scx() took the same delta off the
	 * slice, so a slice set to the unserved remainder, as escalation does,
	 * reaches zero when the remainder does, see scx_rescue_keep().
	 */
	rq->scx.rescue.served += delta_exec;

	/*
	 * A rescue slice is bounded by one quantum and tick-driven expiry can
	 * overshoot by up to a tick. Clamp to avoid wild over-charges on VMs.
	 */
	delta_exec = min_t(s64, delta_exec, scx_rescue_quantum_ns + TICK_NSEC);

	rq->scx.rescue.budget -= delta_exec;

	/*
	 * The per-cpu usage average feeds the overload victim pick. Charge the
	 * sched running the rescuee, which is the host while the rescuee's
	 * owner is bypassing.
	 */
	pcpu = per_cpu_ptr(scx_exec_sched(scx_task_sched(rq->scx.rescue.curr),
					  cpu_of(rq))->pcpu, cpu_of(rq));
	pcpu->rescue_avg = scx_rescue_decay_avg(pcpu) + delta_exec;

	if (!scx_rescue_slice_remaining(rq))
		scx_task_slice_ended(rq, rq->scx.rescue.curr);
}

/**
 * scx_rescue_end - End the rescue execution on @rq
 * @rq: rq of interest
 *
 * When no rescuee is left pending, the session is over and the balance above
 * one quantum dies with it - it would otherwise become a banked license to
 * preempt the cid owner long after the starvation ended. While waiters remain,
 * the accrued deficit belongs to the queue and carries into the next rescue.
 */
void scx_rescue_end(struct rq *rq)
{
	lockdep_assert_rq_held(rq);

	rq->scx.rescue.curr = NULL;
	if (list_empty(&rq->scx.rescue.dsq.list))
		rq->scx.rescue.budget = min(rq->scx.rescue.budget, scx_rescue_quantum_ns);
}

/**
 * scx_rescue_keep - Keep the rescue going past a zeroed slice
 * @rq: rq @p is running on
 * @p: task under rescue
 *
 * Decide what an exhausted slice means for the rescuee. dispatch_one() calls
 * this when nothing else is runnable and put_prev_task_scx() when another task
 * won the pick. scx_rescue_charge() ends the rescue the moment the admitted
 * slice is fully served, so arriving here with the rescue still open means @p
 * was preempted. Restore the unserved remainder and return %true. @p stays the
 * rescuee. dispatch_one() keeps it running and put_prev_task_scx() reinserts it
 * at the tail of the local DSQ, behind whatever preempted the rescuee.
 *
 * Return %false to end the rescue instead - the slice is already fully served
 * or @p is leaving the rq.
 */
bool scx_rescue_keep(struct rq *rq, struct task_struct *p)
{
	s64 remaining = scx_rescue_slice_remaining(rq);

	lockdep_assert_rq_held(rq);

	if (!remaining || !(p->scx.flags & SCX_TASK_QUEUED))
		return false;

	/* never called on a protected rescuee, so the write can't be refused */
	if (WARN_ON_ONCE(!scx_set_task_slice(p, remaining)))
		return false;
	return true;
}

/**
 * scx_rescue_accrue - Accrue budget at the configured fraction of elapsed time
 * @rq: rq of interest
 *
 * A session spans from the first arrival until no rescuee is left, pending or
 * admitted. While one is active the cap is three quanta and the balance drives
 * escalation, see scx_rescue_timerfn(). Outside a session the cap is one
 * quantum, so an idle gap funds the next arrival's admission but never an
 * escalation.
 */
static void scx_rescue_accrue(struct rq *rq)
{
	bool in_session = rq->scx.rescue.curr || !list_empty(&rq->scx.rescue.dsq.list);
	s64 cap = in_session ? 3 * scx_rescue_quantum_ns : scx_rescue_quantum_ns;
	s64 delta;
	u64 now;

	lockdep_assert_rq_held(rq);

	/* not every path here holds an updated rq clock, use __scx_bpf_now() */
	now = __scx_bpf_now(rq);
	delta = now - rq->scx.rescue.clock;
	rq->scx.rescue.clock = now;

	/*
	 * Avoid multiplication overflows by taking a shortcut when the gap is
	 * large enough to fill the budget.
	 */
	if (delta >= scx_rescue_sat_delta_ns)
		rq->scx.rescue.budget = cap;
	else
		rq->scx.rescue.budget =
			min(cap, rq->scx.rescue.budget +
			    ((delta * scx_rescue_bw_1024) >> SCHED_CAPACITY_SHIFT));
}

/*
 * The slice for the next admission - the quantum divided across the stranded
 * tasks so that a crowded queue round-robins on shorter slices.
 */
static s64 scx_rescue_next_slice(struct rq *rq)
{
	s64 min_slice = max_t(s64, SCX_RESCUE_MIN_SLICE_US * NSEC_PER_USEC, TICK_NSEC);
	u32 depth = rq->scx.rescue.dsq.nr ?: 1;

	return clamp(div_s64(scx_rescue_quantum_ns, depth), min_slice, scx_rescue_quantum_ns);
}

static void scx_rescue_timer_arm(struct rq *rq)
{
	struct timer_list *timer = &rq->scx.rescue.timer;
	s64 delay = scx_rescue_quantum_ns / 4;	/* should be granular enough */

	if (timer_pending(timer))
		return;

	/*
	 * While the head waiter can't be admitted because the bucket is short
	 * of a full quantum, stretch to the full funding delay.
	 */
	if (!rq->scx.rescue.curr && rq->scx.rescue.budget < scx_rescue_quantum_ns) {
		s64 deficit = scx_rescue_quantum_ns - rq->scx.rescue.budget;

		delay = max(delay,
			    div_s64(deficit << SCHED_CAPACITY_SHIFT, scx_rescue_bw_1024));
	}

	/* +1 rounds up so the beat is due by the time the timer fires */
	timer->expires = jiffies + nsecs_to_jiffies(delay) + 1;
	add_timer_on(timer, cpu_of(rq));
}

/**
 * scx_rescue_admit - Start rescuing @p on @rq
 * @rq: rq @p is being admitted on
 * @p: task being admitted, off any DSQ
 * @slice: CPU time to grant
 *
 * The slice is a kernel slice, see %SCX_TASK_KERNEL_SLICE. The schedulers may
 * preempt @p or shorten its slice, never extend it. @slice is measured on
 * served CPU time, so neither shortens the rescue, see scx_rescue_charge() and
 * scx_rescue_keep(). Prolonged denial escalates into protected execution, see
 * scx_rescue_timerfn().
 */
static void scx_rescue_admit(struct rq *rq, struct task_struct *p, s64 slice)
{
	lockdep_assert_rq_held(rq);
	WARN_ON_ONCE(rq->scx.rescue.curr);

	rq->scx.rescue.curr = p;
	rq->scx.rescue.slice = slice;
	rq->scx.rescue.served = 0;
	scx_set_task_slice(p, slice);
	p->scx.flags |= SCX_TASK_KERNEL_SLICE;
	scx_rescue_timer_arm(rq);
}

/**
 * scx_rescue_try_admit - Try to admit a freshly stranded task
 * @rq: rq @p is being inserted on
 * @p: stranded task being diverted to rescue
 *
 * One rescue at a time and earlier arrivals go first. Admission needs a full
 * quantum of budget, spent as the rescue runs. Return %true if @p was admitted
 * and should be inserted at the tail of @rq's local DSQ, %false if it has to
 * park on the rescue DSQ, with the timer armed to admit it later.
 */
static bool scx_rescue_try_admit(struct rq *rq, struct task_struct *p)
{
	scx_rescue_accrue(rq);

	if (!rq->scx.rescue.curr && list_empty(&rq->scx.rescue.dsq.list) &&
	    rq->scx.rescue.budget >= scx_rescue_quantum_ns) {
		scx_rescue_admit(rq, p, scx_rescue_quantum_ns);
		return true;
	}

	scx_rescue_timer_arm(rq);
	return false;
}

/**
 * scx_rescue_check_overload - Eject the top rescue consumer on a stuck rescue
 * @rq: rq whose rescue timer fired
 *
 * If the oldest waiter on @rq's rescue DSQ has been queued for too long, rescue
 * demand on this cpu persistently exceeds the configured bandwidth. Eject the
 * sub with the highest recent rescue consumption instead of letting the
 * scheduler stall path blame the sched running the waiter, which may just be
 * crowded out.
 */
static void scx_rescue_check_overload(struct rq *rq)
{
	struct scx_sched *victim = NULL, *pos;
	struct task_struct *p;
	int cpu = cpu_of(rq);
	u64 max_avg = 0;
	u32 dur_ms;

	lockdep_assert_rq_held(rq);

	p = list_first_entry_or_null(&rq->scx.rescue.dsq.list, struct task_struct,
				     scx.dsq_list.node);
	if (!p)
		return;

	/* has the head waiter been queued for longer than the threshold? */
	if (time_before(jiffies, p->scx.rescue_at + scx_rescue_overload_after))
		return;

	/*
	 * Grace period after the last ejection on this cpu - the freed
	 * bandwidth gets one threshold's worth of time to drain the backlog
	 * before another sub is judged.
	 */
	if (time_before64(get_jiffies_64(), rq->scx.rescue.kill_at +
			  scx_rescue_overload_after))
		return;

	list_for_each_entry_rcu(pos, &scx_sched_all, all) {
		u64 avg = scx_rescue_decay_avg(per_cpu_ptr(pos->pcpu, cpu));

		/* skip an already-exiting sub, else the ejection is wasted */
		if (pos->level && avg > max_avg &&
		    atomic_read(&pos->exit_kind) == SCX_EXIT_NONE) {
			max_avg = avg;
			victim = pos;
		}
	}
	if (!victim)
		return;

	rq->scx.rescue.kill_at = get_jiffies_64();
	dur_ms = jiffies_to_msecs(jiffies - p->scx.rescue_at);
	__scx_exit(victim, SCX_EXIT_ERROR_RESCUE, 0, cpu,
		   "used too much rescue CPU time (%llums) while %s[%d] waited %u.%03us to be rescued",
		   div_u64(max_avg, NSEC_PER_MSEC), p->comm, p->pid, dur_ms / 1000,
		   dur_ms % 1000);
}

/**
 * scx_rescue_timerfn - Drive and pace rescue execution
 * @timer: rq->scx.rescue.timer
 *
 * Runs every quarter quantum while a rescuee exists, pending or admitted, see
 * scx_rescue_timer_arm(). The head waiter is admitted once the bucket holds a
 * full quantum and granted its slice, see scx_rescue_next_slice(). A session
 * whose budget accumulates over two quanta with the admitted rescuee still
 * waiting escalates - the rescuee's remaining slice turns into protected
 * execution and it preempts the current task. An overloaded rescue queue ejects
 * the top consumer, see scx_rescue_check_overload().
 */
static void scx_rescue_timerfn(struct timer_list *timer)
{
	struct rq *rq = timer_container_of(rq, timer, scx.rescue.timer);
	struct task_struct *p;

	guard(rq_lock_irqsave)(rq);

	p = rq->scx.rescue.curr;
	if (!p && list_empty(&rq->scx.rescue.dsq.list))
		return;

	scx_rescue_accrue(rq);
	scx_rescue_check_overload(rq);

	if (!p) {
		s64 slice = scx_rescue_next_slice(rq);

		/* no rescue in progress */
		if (rq->scx.rescue.budget < scx_rescue_quantum_ns)
			goto out_arm;

		/* there's enough budget to start rescuing the next one */
		p = list_first_entry(&rq->scx.rescue.dsq.list, struct task_struct,
				     scx.dsq_list.node);
		scx_task_unlink_from_dsq(p, &rq->scx.rescue.dsq);
		scx_rescue_admit(rq, p, slice);
		scx_move_local_task_to_local_dsq(scx_task_sched(p), p,
						 SCX_ENQ_IGNORE_CAPS, rq);
		if (sched_class_above(&ext_sched_class, rq->donor->sched_class))
			resched_curr(rq);
	} else if (p->scx.dsq && rq->scx.rescue.budget > 2 * scx_rescue_quantum_ns) {
		/*
		 * The rescuee waited for the CPU for too long. Escalate - grant
		 * the unserved remainder, protect it from the schedulers and
		 * preempt the current task. The slice is set before the
		 * protection. Repeat beats only repeat the head move - the
		 * slice write is refused on a protected task.
		 */
		scx_set_task_slice(p, scx_rescue_slice_remaining(rq));
		p->scx.flags |= SCX_TASK_PROTECTED;
		scx_task_unlink_from_dsq(p, &rq->scx.local_dsq);
		scx_move_local_task_to_local_dsq(scx_task_sched(p), p,
					SCX_ENQ_HEAD | SCX_ENQ_PREEMPT | SCX_ENQ_IGNORE_CAPS,
					rq);
	}
out_arm:
	scx_rescue_timer_arm(rq);
}

/* flush out tasks waiting for rescue before a CPU goes down */
void scx_rescue_flush(struct rq *rq)
{
	struct task_struct *p, *n;

	lockdep_assert_rq_held(rq);

	/* end the current rescue */
	if (rq->scx.rescue.curr)
		scx_task_slice_ended(rq, rq->scx.rescue.curr);

	/* and flush out all pending ones */
	list_for_each_entry_safe(p, n, &rq->scx.rescue.dsq.list, scx.dsq_list.node) {
		scx_task_unlink_from_dsq(p, &rq->scx.rescue.dsq);
		scx_move_local_task_to_local_dsq(scx_task_sched(p), p,
						 SCX_ENQ_IGNORE_CAPS, rq);
	}

	timer_delete(&rq->scx.rescue.timer);
}

void scx_rescue_dump(struct seq_buf *s, struct rq *rq)
{
	struct task_struct *p = rq->scx.rescue.curr;

	scx_dump_line(s, "          rescue=%u budget=%lldus rescuing=%s[%d]",
		      rq->scx.rescue.dsq.nr,
		      div_s64(rq->scx.rescue.budget, NSEC_PER_USEC),
		      p ? p->comm : "none", p ? p->pid : -1);
}

/*
 * A scheduler whose stall watchdog is shorter than the overload threshold gets
 * stall-killed over its parked waiters before the overload check can eject the
 * actual top consumer. The root's knobs set the threshold, warn on any
 * scheduler that doesn't fit it.
 */
static void scx_rescue_check_timeout(struct scx_sched *sch)
{
	if (!scx_rescue_bw_1024 || sch->watchdog_timeout > scx_rescue_overload_after)
		return;

	pr_warn("sched_ext: %s: watchdog timeout %ums <= rescue overload threshold %ums\n",
		sch->ops.name, jiffies_to_msecs(sch->watchdog_timeout),
		jiffies_to_msecs(scx_rescue_overload_after));
}

/* latch the rescue parameters on root scheduler enable */
void scx_rescue_set_knobs(struct scx_sched *sch)
{
	s32 bw_ppt = sch->ops.rescue_bandwidth_ppt ?: SCX_RESCUE_DFL_BW_PPT;
	s64 quantum_us = sch->ops.rescue_quantum_us ?: SCX_RESCUE_DFL_QUANTUM_US;
	s64 period_ns;

	if (sch->ops.rescue_bandwidth_ppt == SCX_RESCUE_DISABLE) {
		scx_rescue_bw_1024 = 0;
		return;
	}

	scx_rescue_bw_1024 = bw_ppt * SCHED_CAPACITY_SCALE / 1000;
	scx_rescue_quantum_ns = max(quantum_us * NSEC_PER_USEC, TICK_NSEC);
	scx_rescue_sat_delta_ns =
		div_s64((4 * scx_rescue_quantum_ns + TICK_NSEC) << SCHED_CAPACITY_SHIFT,
			scx_rescue_bw_1024);

	/*
	 * The overload threshold and the decay halflife scale with the funding
	 * period - the time the bucket takes to fund one full quantum.
	 */
	period_ns = div_s64(scx_rescue_quantum_ns << SCHED_CAPACITY_SHIFT, scx_rescue_bw_1024);
	scx_rescue_overload_after =
		clamp(nsecs_to_jiffies(SCX_RESCUE_OVERLOAD_MULT * period_ns),
		      msecs_to_jiffies(SCX_RESCUE_MIN_OVERLOAD_MS),
		      msecs_to_jiffies(SCX_RESCUE_MAX_OVERLOAD_MS));
	scx_rescue_decay_halflife = scx_rescue_overload_after / 4;

	/* a single in-budget wait must not cross the overload trigger */
	if (nsecs_to_jiffies(period_ns) > scx_rescue_overload_after / 2)
		pr_warn("sched_ext: %s: rescue funding period %lldms > overload threshold %ums / 2\n",
			sch->ops.name, div_s64(period_ns, NSEC_PER_MSEC),
			jiffies_to_msecs(scx_rescue_overload_after));

	scx_rescue_check_timeout(sch);
}

void scx_rescue_init(struct rq *rq)
{
	BUG_ON(scx_init_dsq(&rq->scx.rescue.dsq, SCX_DSQ_RESCUE, NULL));
	timer_setup(&rq->scx.rescue.timer, scx_rescue_timerfn, TIMER_PINNED);
	rq->scx.rescue.kill_at = get_jiffies_64();
}

/**
 * __scx_resolve_local_dsq - Pick the local, rescue or reject DSQ for an insert
 * @sch: enqueuing sub-sched
 * @rq: rq whose local DSQ @p targets
 * @p: task being inserted
 * @enq_flags: in/out, unhonored flags are cleared
 *
 * Return @rq's local DSQ if @sch holds the required caps on @rq's cid.
 * Otherwise, return @rq's rescue DSQ if the insert carries %SCX_ENQ_RESCUE and
 * rescue is enabled, or @rq's reject DSQ after recording the reenq reason on
 * @p.
 *
 * %SCX_ENQ_IMMED, %SCX_ENQ_PREEMPT, %SCX_ENQ_PREEMPT_LAZY and %SCX_ENQ_HEAD are
 * cleared when diverting to rescue or reject. %SCX_ENQ_PREEMPT and
 * %SCX_ENQ_PREEMPT_LAZY are also cleared on a fallback migration-disabled
 * admission.
 *
 * bypass_enq_target_dsq() also calls this with the host as @sch when the host
 * lacks %SCX_CAP_BASE on @rq's cid, if rescue is enabled or @p must run on the
 * cpu, migration-disabled or with a migration pending. The bypassed task is
 * then admitted or diverted like a capless %SCX_ENQ_RESCUE insert by the host
 * itself.
 */
struct scx_dispatch_q *__scx_resolve_local_dsq(struct scx_sched *sch, struct rq *rq,
					       struct task_struct *p, u64 *enq_flags)
{
	s32 cid = __scx_cpu_to_cid(cpu_of(rq));
	struct scx_sched *asch = rq->scx.remote_activate_sch ?: sch;
	u64 needed = scx_caps_for_enq(*enq_flags);
	u64 missing;

	/*
	 * On a remote activation the scheduling sched (@asch) differs from
	 * @p's owner (@sch). Check caps against the scheduling sched.
	 */
	if (*enq_flags & (SCX_ENQ_PREEMPT | SCX_ENQ_PREEMPT_LAZY))
		needed |= scx_caps_for_preempt(asch, rq, *enq_flags);
	missing = scx_missing_caps(asch, cpu_of(rq), needed);

	/* requirements met */
	if (likely(!missing))
		return &rq->scx.local_dsq;

	/*
	 * The task must run on this CPU regardless of caps: the rq is draining
	 * offline (BPF scheduler bypassed), the task is migration-disabled, or a
	 * migration is pending. Admit despite the missing caps and count it.
	 * Refuse preemptions.
	 */
	if (unlikely(!scx_rq_online(rq) || is_migration_disabled(p) ||
		     p->migration_pending)) {
		__scx_add_event(sch, SCX_EV_SUB_FORCED_ADMIT, 1);
		*enq_flags &= ~(SCX_ENQ_PREEMPT | SCX_ENQ_PREEMPT_LAZY);
		return &rq->scx.local_dsq;
	}

	/*
	 * Diverting to rescue or reject, neither of which honors IMMED, PREEMPT
	 * or HEAD - a diversion has no priority and IMMED is not allowed on
	 * non-local DSQs. Strip the enq and task flags along with the slice.
	 */
	scx_divert_strip_flags(p, enq_flags);

	/* the enqueuer opted for rescue instead of rejection and reenqueue */
	if (scx_rescue_enabled() && (*enq_flags & SCX_ENQ_RESCUE)) {
		__scx_add_event(sch, SCX_EV_SUB_RESCUE, 1);
		if (scx_rescue_try_admit(rq, p))
			return &rq->scx.local_dsq;

		/* queueing, the overload trigger measures the wait from here */
		p->scx.rescue_at = jiffies;
		return &rq->scx.rescue.dsq;
	}

	p->scx.reenq_reason_caps = missing;
	p->scx.reenq_reason_cid = cid;
	WARN_ON_ONCE(p->scx.flags & SCX_TASK_REENQ_REASON_MASK);
	p->scx.flags |= SCX_TASK_REENQ_CAP;

	return &rq->scx.reject_dsq;
}

/* @p lost the caps needed to stay on @rq's local DSQ? Record reason if so. */
bool scx_task_reenq_on_cap_revoke(struct rq *rq, struct task_struct *p)
{
	s32 cpu = cpu_of(rq);
	u64 missing;

	/* migration-disabled tasks and the rescuee are admitted capless */
	if (is_migration_disabled(p) || p == scx_rescuee(rq))
		return false;

	/* check caps against the sched running a bypassed task */
	missing = scx_missing_caps(scx_exec_sched(scx_task_sched(p), cpu), cpu,
				   scx_caps_for_task(p));
	if (likely(!missing))
		return false;

	p->scx.reenq_reason_caps = missing;
	p->scx.reenq_reason_cid = __scx_cpu_to_cid(cpu);
	return true;
}

/* record a caps change, see struct scx_caps_updated */
static void caps_updated_record(struct scx_pshard *ps, const struct scx_cmask *cids, u64 caps,
				struct list_head *to_deliver)
{
	struct scx_caps_updated *cu = &ps->caps_updated;

	guard(raw_spinlock)(&cu->lock);
	scx_cmask_or(&cu->cmask, cids);
	cu->caps |= caps;
	if (list_empty(&cu->node_in_flight))
		list_add_tail(&cu->node_in_flight, to_deliver);
}

/* deliver queued caps_updated callbacks, see struct scx_caps_updated */
static void caps_updated_deliver(struct list_head *to_deliver)
{
	struct scx_caps_updated *cu, *tmp;

	list_for_each_entry_safe(cu, tmp, to_deliver, node_in_flight) {
		struct scx_pshard *ps = container_of(cu, struct scx_pshard, caps_updated);
		struct scx_sched *sch = ps->sch;

		while (true) {
			u64 caps = 0;

			/*
			 * During enable, has_op is set after ops.sub_attach(),
			 * so !has_op means the op is absent or the sched isn't
			 * live yet - e.g. caps grant from ops.sub_attach().
			 * Either way don't consume - leave for
			 * scx_sub_seed_caps() to deliver once has_op is set.
			 */
			scoped_guard (raw_spinlock, &cu->lock) {
				if (cu->caps && SCX_HAS_OP(sch, sub_caps_updated) &&
				    likely(!READ_ONCE(sch->aborting))) {
					struct scx_cmask_ref ref;

					caps = cu->caps;
					scx_cmask_ref_init_kern(sch, cu->cmask_arena_out,
								ps->base, ps->nr_cids, &ref);
					scx_cmask_ref_copy(&ref, &cu->cmask);
					scx_cmask_clear(&cu->cmask);
					cu->caps = 0;
				} else {
					list_del_init(&cu->node_in_flight);
				}
			}
			if (!caps)
				break;

			/* caps != 0 only when deliverable (has_op, above) */
			SCX_CALL_OP(sch, sub_caps_updated, NULL, cu->cmask_arena_out, caps);
		}
	}
}

/*
 * Deliver caps owed to @sch that couldn't be delivered earlier (e.g. a grant
 * taken during its sub_attach(), before has_op was set). Called once has_op is
 * set.
 */
static void scx_sub_seed_caps(struct scx_sched *sch)
{
	LIST_HEAD(to_deliver);
	s32 si;

	guard(irqsave)();

	for (si = 0; si < sch->nr_pshards; si++) {
		struct scx_pshard *ps = sch->pshard[si];
		struct scx_caps_updated *cu = &ps->caps_updated;

		scoped_guard (raw_spinlock, &cu->lock) {
			if (cu->caps && list_empty(&cu->node_in_flight))
				list_add_tail(&cu->node_in_flight, &to_deliver);
		}
	}
	caps_updated_deliver(&to_deliver);
}

static u64 calc_effective_caps(struct scx_pshard *ps, s32 cid)
{
	u64 ecaps = 0;
	u32 cap_bit;

	for (cap_bit = 0; cap_bit < __SCX_NR_CAPS; cap_bit++)
		if (scx_cmask_test(cid, &ps->caps[cap_bit].cmask))
			ecaps |= BIT_U64(cap_bit) | scx_caps_implied(BIT_U64(cap_bit));
	return ecaps;
}

/**
 * queue_sync_ecaps - Queue ecaps update for a (sch, cid) pair
 * @sch: sched to update
 * @cid: cid to update
 *
 * Queue an ecaps update for @sch's @cid and reschedule the cpu so that it syncs
 * in dispatch_one(). The cpu isn't rescheduled while it is inactive or the root
 * bypasses, and its next scheduling event processes the sync then. A grant or
 * revoke can run in the PM syscore phase, where irq work can malfunction, see
 * scx_kick_cpu(), and the root bypasses throughout PM. Callers that need the
 * sync processed promptly reschedule the cpu themselves, see
 * resched_sync_ecaps().
 */
static void queue_sync_ecaps(struct scx_sched *sch, s32 cid)
{
	s32 cpu = __scx_cid_to_cpu(cid);
	struct scx_sched_pcpu *pcpu = per_cpu_ptr(sch->pcpu, cpu);

	/*
	 * Pairs with smp_mb() in __scx_process_sync_ecaps(). The caller has
	 * written the state a sync reads, caps[] or cpu_active(). Either the
	 * check below finds the node off the list and queues a sync, or a sync
	 * in flight is still ahead of its reads and sees the new state.
	 */
	smp_mb();

	/* @cid's pshard->lock excludes concurrent queueing attempts */
	if (llist_on_list(&pcpu->ecaps_to_sync_node))
		return;

	/*
	 * A cpu going down processes its syncs in its own picks, see
	 * scx_offline_ecaps(), and a resched for every change on the cid would
	 * only keep preempting its hotplug thread. The irq work needs an online
	 * cpu. An active cpu stays online here, as @cid's pshard->lock keeps
	 * preemption off, which holds off the stop_machine() that takes a cpu
	 * offline.
	 */
	if (llist_add(&pcpu->ecaps_to_sync_node, &cpu_rq(cpu)->scx.ecaps_to_sync) &&
	    cpu_active(cpu) && !scx_bypassing(sch->ancestors[0], smp_processor_id()))
		scx_resched_curr_deferred(cpu_rq(cpu));
}

/* the pshard holding @sch's caps on @cid */
static struct scx_pshard *cid_pshard(struct scx_sched *sch, s32 cid)
{
	return sch->pshard[rcu_dereference_all(scx_cid_to_shard)[cid]];
}

static void ecaps_report_irq_workfn(struct irq_work *irq_work)
{
	rcuwait_wake_up(&ecaps_report_wait);
}

static struct irq_work ecaps_report_irq_work = IRQ_WORK_INIT_HARD(ecaps_report_irq_workfn);

/*
 * Complete @pcpu's report mark. The waiter in ecaps_report() can't be woken
 * under the rq lock, so the last completion wakes it through irq_work.
 */
static void ecaps_report_done(struct scx_sched_pcpu *pcpu)
{
	pcpu->flags &= ~SCX_SCHED_PCPU_ECAPS_REPORT;
	if (atomic_dec_and_test(&pcpu->sch->nr_ecaps_reports))
		irq_work_queue(&ecaps_report_irq_work);
}

static void ecaps_offline_irq_workfn(struct irq_work *irq_work)
{
	wake_up(&ecaps_offline_waitq);
}

static DEFINE_IRQ_WORK(ecaps_offline_irq_work, ecaps_offline_irq_workfn);

/*
 * @pcpu's sync marked by scx_offline_ecaps() has been processed or discarded.
 * The waiter can't be woken under the rq lock, so the last completion wakes it
 * through irq_work. The marks are all done while the cpu's hotplug thread waits
 * in scx_offline_ecaps(), before hotplug parks any irq_work thread, so a
 * regular irq_work is enough.
 */
static void ecaps_offline_done(struct rq *rq, struct scx_sched_pcpu *pcpu)
{
	lockdep_assert_rq_held(rq);

	pcpu->flags &= ~SCX_SCHED_PCPU_ECAPS_OFFLINE;
	WRITE_ONCE(rq->scx.nr_ecaps_offline, rq->scx.nr_ecaps_offline - 1);
	if (!rq->scx.nr_ecaps_offline)
		irq_work_queue(&ecaps_offline_irq_work);
}

/*
 * Test whether the hotplug operation in progress is suspend or hibernation
 * taking a cpu down or bringing it back. A kexec jump also sets
 * cpuhp_tasks_frozen but sends no PM notification, so outside a PM cycle its
 * cpu downs deliver their losses as in regular hotplug.
 */
static bool scx_pm_frozen(void)
{
	return cpuhp_tasks_frozen && READ_ONCE(scx_pm_in_progress);
}

/**
 * __scx_process_sync_ecaps - Sync this cpu's ecaps to pshard->caps[]
 * @rq: the cid's cpu rq
 * @prev: @rq's previous task from the in-progress dispatch
 *
 * pshard->caps[] is the target configuration. pcpu->ecaps is the effective
 * transposed copy owned by the cid's cpu and written only here under @rq's
 * lock.
 *
 * A sched's ecaps change only with a delivery, so that whenever an op of the
 * sched runs under @rq's lock, its ecaps are what its deliveries reported. A
 * sync that can't deliver leaves them alone, and a later one that can reports
 * the net change.
 *
 * A sched that newly gains baseline access here is owed an update_idle() so it
 * learns the cid's idle state. Such a gain arms the per-rq
 * %SCX_RQ_SUB_IDLE_RENOTIFY gate so the next idle pick delivers it.
 */
void __scx_process_sync_ecaps(struct rq *rq, struct task_struct *prev)
{
	s32 cpu = cpu_of(rq);
	s32 cid;
	struct llist_node *batch, *pos, *tmp;
	u64 lost_all = 0;

	if (likely(llist_empty(&rq->scx.ecaps_to_sync)))
		return;

	/* @cid stays valid while a root is enabled */
	cid = __scx_cpu_to_cid(cpu);

	batch = llist_del_all(&rq->scx.ecaps_to_sync);
	llist_for_each_safe(pos, tmp, batch) {
		struct scx_sched_pcpu *pcpu =
			container_of(pos, struct scx_sched_pcpu, ecaps_to_sync_node);
		struct scx_pshard *ps = cid_pshard(pcpu->sch, cid);
		bool report = pcpu->flags & SCX_SCHED_PCPU_ECAPS_REPORT;
		bool offline = pcpu->flags & SCX_SCHED_PCPU_ECAPS_OFFLINE;
		u64 old, ecaps, lost, gained;

		init_llist_node(pos);

		/* pairs with smp_mb() in queue_sync_ecaps(), see there */
		smp_mb();

		/*
		 * Before the sched's has_op is set, only the parent's op would
		 * run, and the change would never reach the sched.
		 * ecaps_enable() enables deliveries on each cpu once has_op is
		 * set. Deliveries also need the cpu online to the root, see
		 * handle_hotplug().
		 *
		 * An inactive cpu that can deliver is going down and reads as
		 * capless. A delivery below can drop the rq lock and the cpu's
		 * active state can change between syncs, so test it per sync.
		 * Suspend and hibernation don't deliver the loss of a cpu they
		 * take down, see scx_offline_ecaps().
		 *
		 * A sync that can't deliver leaves the ecaps alone. It may
		 * still carry a mark that is cleared below. Don't skip the rest
		 * of the loop for it.
		 */
		old = READ_ONCE(pcpu->ecaps);
		if (!(pcpu->flags & SCX_SCHED_PCPU_ECAPS_ENABLED) ||
		    !(rq->scx.flags & SCX_RQ_OPS_ONLINE))
			ecaps = old;
		else if (cpu_active(cpu))
			ecaps = calc_effective_caps(ps, cid);
		else if (scx_pm_frozen())
			ecaps = old;
		else
			ecaps = 0;
		WRITE_ONCE(pcpu->ecaps, ecaps);

		lost = old & ~ecaps;
		gained = ecaps & ~old;
		lost_all |= lost;

		/*
		 * The bypass DSQ on this cpu is no longer consumed, so drain
		 * it. A bypassing sched's own bypass DSQs are empty, as its
		 * tasks sit on the host's.
		 */
		if ((lost & SCX_CAP_BASE) && !scx_bypassing(pcpu->sch, cpu))
			scx_bypass_lb_kick(pcpu->sch);

		/*
		 * Tell the sched and its parent that the sched's effective caps
		 * on this cid changed. The invocations are equivalent to the
		 * dispatch path and may drop and re-acquire the rq lock
		 * temporarily while the rest of @batch is held privately, see
		 * scx_discard_ecaps_to_sync(). The dispatch kfuncs resolve
		 * their context on the executing cpu, which under core
		 * scheduling can differ from @rq's cpu, so the context is set
		 * up there. The rq recorded in it keeps the dispatches
		 * targeting @rq.
		 */
		if (ecaps != old) {
			struct scx_sched *parent = scx_parent(pcpu->sch);
			struct scx_dsp_ctx *dspc;

			/* stash @prev so nested dispatches can access it */
			rq->scx.sub_dispatch_prev = prev;

			/* a disabling sched's ops are not called */
			if (SCX_HAS_OP(pcpu->sch, sub_ecaps_updated) &&
			    likely(!READ_ONCE(pcpu->sch->aborting))) {
				dspc = &this_cpu_ptr(pcpu->sch->pcpu)->dsp_ctx;
				dspc->rq = rq;
				SCX_CALL_OP(pcpu->sch, sub_ecaps_updated, rq,
					    scx_cpu_arg(cpu), old, ecaps);
				scx_flush_dispatch_buf(pcpu->sch, rq);
			}
			if (SCX_HAS_OP(parent, sub_child_ecaps_updated) &&
			    likely(!READ_ONCE(parent->aborting))) {
				dspc = &this_cpu_ptr(parent->pcpu)->dsp_ctx;
				dspc->rq = rq;
				SCX_CALL_OP(parent, sub_child_ecaps_updated, rq,
					    pcpu->sch->ops.sub_cgroup_id, scx_cpu_arg(cpu),
					    old, ecaps);
				scx_flush_dispatch_buf(parent, rq);
			}
			rq->scx.sub_dispatch_prev = NULL;
		}

		if (report)
			ecaps_report_done(pcpu);
		if (offline)
			ecaps_offline_done(rq, pcpu);

		/*
		 * Gaining baseline access owes an update_idle() so the sched
		 * learns the cpu's idle state. Arm the per-rq gate so the next
		 * idle pick flushes it. Losing access drops any pending notify.
		 */
		if (gained & SCX_CAP_BASE) {
			pcpu->idle_renotify = true;
			rq->scx.flags |= SCX_RQ_SUB_IDLE_RENOTIFY;
		} else if (lost & SCX_CAP_BASE) {
			pcpu->idle_renotify = false;
		}
	}

	/*
	 * Losing a cap can strand already-queued tasks. Schedule a reenq scan
	 * to move the now-capless ones off the local DSQ. The scan tests
	 * against the effective caps and thus must come after the ecaps sync.
	 */
	if (lost_all & SCX_CAPS_REENQ_ON_LOSS)
		scx_schedule_reenq_local(rq, SCX_REENQ_CAP_REVOKE);
}

void scx_sub_pm_begin(void)
{
	WRITE_ONCE(scx_pm_in_progress, true);
}

/*
 * Called on every cpu down and up whether or not a root is loaded, so that the
 * set stays exact when a root is enabled or disabled during a PM cycle. A cpu
 * that failed to come back from suspend or hibernation can come back through
 * regular hotplug before the PM cycle ends, e.g. through the SMT control. The
 * reseed at its activation reports the net change, so any up removes the cpu
 * from the set.
 */
void scx_update_frozen_cpus(s32 cpu, bool online)
{
	if (online) {
		if (!cpumask_test_cpu(cpu, &scx_frozen_cpus))
			return;
		__cpumask_clear_cpu(cpu, &scx_frozen_cpus);
		if (cpumask_empty(&scx_frozen_cpus))
			wake_up_all(&scx_frozen_cpus_waitq);
	} else if (scx_pm_frozen()) {
		__cpumask_set_cpu(cpu, &scx_frozen_cpus);
	}
}

/*
 * The PM cycle is over. A cpu still in the set failed to come back, and its
 * loss can't be delivered anymore. That breaks the guarantee that every ecaps
 * change reaches the sub-scheds and their parents, so abort every sched. A root
 * without sub-scheds is owed no reports and keeps running.
 *
 * Abort before clearing the set and waking the detaches in scx_sub_disable(),
 * so that a parent whose ops.sub_detach() runs with that loss unreported is
 * already being disabled, which ops.sub_detach() allows for. No exit cpu is
 * recorded, as the cpu running this isn't the one that failed.
 */
void scx_sub_pm_end(void)
{
	/* hotplug updates the set under cpus_write_lock() */
	guard(cpus_read_lock)();

	if (unlikely(!cpumask_empty(&scx_frozen_cpus)) && scx_has_subs()) {
		struct scx_sched *root;

		guard(rcu)();
		root = rcu_dereference(scx_root);
		if (root)
			__scx_exit(root, SCX_EXIT_ERROR, 0, -1,
				   "%u cpu(s) failed to come back after suspend or hibernation",
				   cpumask_weight(&scx_frozen_cpus));
	}

	WRITE_ONCE(scx_pm_in_progress, false);
	cpumask_clear(&scx_frozen_cpus);
	wake_up_all(&scx_frozen_cpus_waitq);
}

/*
 * Called with cpus_read_lock() held before the root is published. The cpus
 * active now are online to the root without ops.cpu_online(), so they can
 * deliver ecaps. handle_hotplug() maintains %SCX_RQ_OPS_ONLINE from here on.
 */
void scx_init_rq_ops_online(void)
{
	s32 cpu;

	lockdep_assert_cpus_held();

	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);

		guard(rq_lock_irqsave)(rq);
		if (cpu_active(cpu))
			rq->scx.flags |= SCX_RQ_OPS_ONLINE;
		else
			rq->scx.flags &= ~SCX_RQ_OPS_ONLINE;
	}
}

/*
 * Make @rq's cpu dispatch through the ext class to process its ecaps syncs.
 * queue_sync_ecaps() reschedules only when the cpu has no sync queued yet,
 * never an inactive cpu, and not while the root bypasses, so reschedule here.
 * The callers run on the cpu's hotplug thread or in ecaps_report(), never in
 * the PM syscore phase. A higher class task can hold the cpu, so also start the
 * ext server, which picks the ext class within its period.
 */
static void resched_sync_ecaps(struct rq *rq)
{
	lockdep_assert_rq_held(rq);

	update_rq_clock(rq);
	dl_server_start(&rq->ext_server);
	scx_resched_curr_deferred(rq);
}

/*
 * A cpu came back and the root's ops.cpu_online() has run. Let the cpu deliver
 * ecaps again and reseed each sub-sched's ecaps on its cid. The sync recomputes
 * effective caps from the pshard and reports any change since the last
 * delivery.
 */
void scx_online_ecaps(struct rq *rq)
{
	struct scx_sched *root, *pos;
	s32 cid;

	guard(rq_lock_irqsave)(rq);

	rq->scx.flags |= SCX_RQ_OPS_ONLINE;

	/*
	 * Only a live hierarchy can have ecaps to reseed. This also keeps the
	 * table reads below away from an enable that failed before publishing
	 * the tables. A concurrent disable can't retire them, see
	 * handle_hotplug().
	 */
	if (!scx_enabled())
		return;

	root = scx_root_protected();
	cid = __scx_cpu_to_cid(cpu_of(rq));

	scx_for_each_descendant_pre(pos, root) {
		struct scx_pshard *ps;

		/* root holds every cap and never uses ecaps */
		if (!pos->level)
			continue;

		ps = cid_pshard(pos, cid);
		guard(raw_spinlock)(&ps->lock);
		queue_sync_ecaps(pos, cid);
	}

	resched_sync_ecaps(rq);
}

/*
 * A cpu is going down, and the root's ops.cpu_offline() follows. Queue a marked
 * sync for each sub-sched to report the loss and wait until each has been
 * processed. Then stop the cpu's ecaps deliveries, so that none comes after
 * ops.cpu_offline().
 *
 * This runs on the cpu's hotplug thread. While it sleeps, the cpu keeps
 * scheduling, and a pick through the ext class processes the syncs, see
 * resched_sync_ecaps(). Only the marked syncs count, so a flood of changes on
 * the cid can't hold up the hotplug. After the wait, every sub-sched's ecaps on
 * the cpu are zero, and the later syncs have nothing to deliver.
 *
 * The loss on a cpu that suspend or hibernation takes down is not delivered, so
 * that the scheds don't see a PM cycle as cpus leaving and coming back. The cpu
 * keeps the caps last delivered until it comes back, and the reseed then
 * reports the net change.
 */
void scx_offline_ecaps(struct rq *rq)
{
	s32 cpu = cpu_of(rq);
	struct scx_sched *root, *pos;
	u32 nr = 0;
	s32 cid;

	/*
	 * ecaps and the tables need a live root, see scx_online_ecaps(). The
	 * ext class isn't picked without one, so the syncs wouldn't be
	 * processed and the wait would never end. scx_enabled() can't change
	 * during hotplug.
	 */
	if (scx_enabled() && !scx_pm_frozen()) {
		scoped_guard (rq_lock_irqsave, rq) {
			root = scx_root_protected();
			cid = __scx_cpu_to_cid(cpu);

			scx_for_each_descendant_pre(pos, root) {
				struct scx_sched_pcpu *pcpu = per_cpu_ptr(pos->pcpu, cpu);
				struct scx_pshard *ps;

				/* root holds every cap and never uses ecaps */
				if (!pos->level)
					continue;

				/*
				 * A batch that has yet to reach the sync sees
				 * the mark. A sync already being processed has
				 * left the list and is queued again below.
				 */
				pcpu->flags |= SCX_SCHED_PCPU_ECAPS_OFFLINE;
				nr++;

				ps = cid_pshard(pos, cid);
				guard(raw_spinlock)(&ps->lock);
				queue_sync_ecaps(pos, cid);
			}

			WRITE_ONCE(rq->scx.nr_ecaps_offline, nr);
			resched_sync_ecaps(rq);
		}

		wait_event(ecaps_offline_waitq, !READ_ONCE(rq->scx.nr_ecaps_offline));
	}

	guard(rq_lock_irqsave)(rq);
	rq->scx.flags &= ~SCX_RQ_OPS_ONLINE;
}

/*
 * Clear every cap @sch holds in the pshards. The report that follows delivers
 * the loss and zeroes the ecaps. The caller holds off cpu hotplug across this
 * and the report.
 */
static void clear_all_caps(struct scx_sched *sch)
{
	s32 si;
	u32 cap_bit;

	lockdep_assert_cpus_held();

	/* enable may have failed before scx_alloc_pshards() */
	if (!sch->pshard)
		return;

	for (si = 0; si < sch->nr_pshards; si++) {
		struct scx_pshard *ps = sch->pshard[si];

		guard(raw_spinlock_irqsave)(&ps->lock);
		for (cap_bit = 0; cap_bit < __SCX_NR_CAPS; cap_bit++)
			scx_cmask_clear(&ps->caps[cap_bit].cmask);
	}
}

/*
 * Remove @pcpu's ecaps sync node from @cpu's pending list. The first grace
 * period has passed, so nothing queues the node again. The second grace period
 * waits for a sync batch that took the node earlier, see
 * scx_sched_quiesce_rcu_work().
 */
void scx_discard_ecaps_to_sync(s32 cpu, struct scx_sched_pcpu *pcpu)
{
	struct rq *rq = cpu_rq(cpu);
	struct llist_node *head = NULL, *tail = NULL;
	struct llist_node *pos, *tmp;

	guard(rq_lock_irqsave)(rq);

	/* ecaps_report() waited for every mark it set */
	WARN_ON_ONCE(pcpu->flags & SCX_SCHED_PCPU_ECAPS_REPORT);

	if (!llist_on_list(&pcpu->ecaps_to_sync_node))
		return;

	/*
	 * llist can't unlink a single node. Take all queued nodes, drop @pcpu's
	 * and resplice the rest. Nodes in the taken batch read as on-list
	 * throughout, so queue_sync_ecaps() stays correct.
	 */
	llist_for_each_safe(pos, tmp, llist_del_all(&rq->scx.ecaps_to_sync)) {
		if (pos == &pcpu->ecaps_to_sync_node) {
			init_llist_node(pos);
			/* a cpu going down may be waiting for the sync */
			if (pcpu->flags & SCX_SCHED_PCPU_ECAPS_OFFLINE)
				ecaps_offline_done(rq, pcpu);
		} else {
			pos->next = head;
			head = pos;
			if (!tail)
				tail = pos;
		}
	}
	if (head)
		llist_add_batch(head, tail, &rq->scx.ecaps_to_sync);
}

/**
 * scx_discard_stale_ecaps_syncs - Discard ecaps syncs from earlier schedulers
 *
 * To be called during root enable before the scheduler goes live. An earlier
 * root's sub-sched may not have gone through its RCU free path yet (e.g. a
 * still-open link fd defers it) and can leave queued ecaps syncs behind.
 * Processing them would decode the dead sched's pshards with the current cid
 * layout. Discard them instead. The backing scx_sched_pcpu's are still
 * allocated as the free path removes ecaps_to_sync_node before freeing.
 */
void scx_discard_stale_ecaps_syncs(void)
{
	s32 cpu;

	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);
		struct llist_node *pos, *tmp;

		guard(rq_lock_irqsave)(rq);
		llist_for_each_safe(pos, tmp, llist_del_all(&rq->scx.ecaps_to_sync))
			init_llist_node(pos);
	}
}

static DECLARE_WAIT_QUEUE_HEAD(scx_unlink_waitq);

void drain_descendants(struct scx_sched *sch)
{
	/*
	 * Child scheds that finished the critical part of disabling will take
	 * themselves off @sch->children. Wait for it to drain. As propagation
	 * is recursive, empty @sch->children means that all proper descendant
	 * scheds reached unlinking stage.
	 */
	wait_event(scx_unlink_waitq, list_empty(&sch->children));
}

/**
 * scx_rehome_task - Move a task to a sched it has been initialized for
 * @to: sched taking over @p, @p's init on it already complete
 * @p: task to re-home
 *
 * Exit @p from its current sched and switch it over to @to, overriding the
 * state to %SCX_TASK_READY to account for the already completed init. A task
 * on a non-ext class, possible under an %SCX_OPS_SWITCH_PARTIAL root, stays
 * %READY and is enabled by switching_to_scx() if it switches over.
 */
static void scx_rehome_task(struct scx_sched *to, struct task_struct *p)
{
	lockdep_assert_held(&p->pi_lock);
	lockdep_assert_rq_held(task_rq(p));

	scx_prepare_task_sched_change(p);
	scoped_guard (sched_change, p,
		      DEQUEUE_SAVE | DEQUEUE_MOVE | DEQUEUE_NOCLOCK) {
		scx_disable_and_exit_task(scx_task_sched(p), p);
		scx_set_task_state(p, SCX_TASK_INIT_BEGIN);
		scx_set_task_state(p, SCX_TASK_INIT);
		scx_set_task_sched(p, to);
		scx_set_task_state(p, SCX_TASK_READY);
		if (p->sched_class == &ext_sched_class)
			scx_enable_task(to, p);
	}
}

/**
 * scx_punt_task - Hand a task to a failed sched without initialization
 * @to: failed and bypassed sched taking custody of @p
 * @p: task to punt
 *
 * Take @p off its current sched and put it on @to at %SCX_TASK_NONE. @to is
 * dying and its teardown will re-home @p properly.
 *
 * Used when @to must take over @p but failed to initialize it. Bypass keeps
 * scheduling decisions away from @to but @p can still trigger its task ops,
 * which may confuse the BPF side. @to is dying anyway. The exit paths call no
 * op on a %NONE task. __scx_disable_and_exit_task() and switched_from_scx()
 * only reset its fields, see scx_reset_task_fields().
 */
static void scx_punt_task(struct scx_sched *to, struct task_struct *p)
{
	lockdep_assert_held(&p->pi_lock);
	lockdep_assert_rq_held(task_rq(p));
	WARN_ON_ONCE(!READ_ONCE(to->bypass_depth));

	scx_prepare_task_sched_change(p);
	scoped_guard (sched_change, p,
		      DEQUEUE_SAVE | DEQUEUE_MOVE | DEQUEUE_NOCLOCK) {
		scx_disable_and_exit_task(scx_task_sched(p), p);
		scx_set_task_sched(p, to);
	}
}

static void scx_fail_parent(struct scx_sched *sch,
			    struct task_struct *failed, s32 fail_code)
{
	struct scx_sched *parent = scx_parent(sch);
	struct scx_task_iter sti;
	struct task_struct *p;

	scx_error(parent, "ops.init_task() failed (%d) for %s[%d] while disabling a sub-scheduler",
		  fail_code, failed->comm, failed->pid);

	/*
	 * Once $parent is bypassed, tasks can be punted into it. This may
	 * cause downstream failures on the BPF side but $parent is dying
	 * anyway.
	 */
	scx_bypass(parent, true);

	scx_task_iter_start(&sti, sch->cgrp);
	while ((p = scx_task_iter_next_locked(&sti))) {
		if (scx_task_on_sched(parent, p))
			continue;

		scx_punt_task(parent, p);
	}
	scx_task_iter_stop(&sti);
}

#ifdef CONFIG_EXT_GROUP_SCHED
/**
 * scx_cgroup_claim_subtree - Claim the subtree's cgroups for an enabling sub
 * @sch: sub-scheduler being enabled
 *
 * Called while enabling @sch, after the subtree's cgrp->scx_sched's are pointed
 * at @sch and before any task is claimed. This mirrors root enable's
 * cgroups-before-tasks order. The ops.init_task() args are task_group-granular
 * and can still reference a cgroup outside the handed-over set when the cpu
 * controller is coarser than the sub topology or mounted on cgroup1.
 *
 * First init each of the parent sched's subtree cgroups on @sch, and only then
 * exit them from the parent, so that a failed init can be unwound with the
 * parent untouched. The both-inited transient is invisible outside
 * scx_cgroup_lock(). %SCX_TG_SUB_INIT tracks the first pass's progress.
 * %SCX_TG_INITED stays set throughout, except for a task_group whose
 * ops.cgroup_init() failed on the parent (see scx_cgroup_return_subtree()):
 * there is nothing to exit from the parent and %SCX_TG_INITED is set back with
 * the transfer.
 *
 * Dying but not yet offlined task_groups are included: a removed cgroup keeps
 * hosting scheduling events until its dying tasks finish their final context
 * switches, so it still needs to be inited on a sched, and its offline-time
 * ops.cgroup_exit() follows the last of those events.
 *
 * Return 0 on success, -errno on failure. On failure, @sch has been
 * scx_error()'d and is left with no cgroups.
 */
static s32 scx_cgroup_claim_subtree(struct scx_sched *sch)
{
	struct cgroup *sub_cgrp = sch_cgroup(sch);
	struct cgroup_subsys_state *ecss = cgroup_e_css(sub_cgrp, &cpu_cgrp_subsys);
	struct scx_sched *parent = scx_parent(sch);
	struct cgroup_subsys_state *css;
	int ret;

	css_for_each_descendant_pre(css, ecss) {
		struct task_group *tg = css_tg(css);
		struct scx_cgroup_init_args args = {
			.weight = tg->scx.weight,
			.bw_period_us = tg->scx.bw_period_us,
			.bw_quota_us = tg->scx.bw_quota_us,
			.bw_burst_us = tg->scx.bw_burst_us,
			.sched_idle = tg->scx.sched_idle,
		};

		if (tg->scx.sched != parent ||
		    !cgroup_is_descendant(css->cgroup, sub_cgrp))
			continue;

		if (SCX_HAS_OP(sch, cgroup_init)) {
			ret = SCX_CALL_OP_RET(sch, cgroup_init, NULL, css->cgroup, &args);
			if (ret) {
				scx_error(sch, "ops.cgroup_init() failed (%d)", ret);
				goto err;
			}
		}
		tg->scx.flags |= SCX_TG_SUB_INIT;
	}

	css_for_each_descendant_post(css, ecss) {
		struct task_group *tg = css_tg(css);

		/*
		 * SUB_INIT is pass 1's progress mark: pass 2 and the err path
		 * must visit exactly the tgs pass 1 inited.
		 */
		if (!(tg->scx.flags & SCX_TG_SUB_INIT))
			continue;

		/* skip the exit if the parent's ops.cgroup_init() failed */
		if ((tg->scx.flags & SCX_TG_INITED) && SCX_HAS_OP(parent, cgroup_exit))
			SCX_CALL_OP(parent, cgroup_exit, NULL, css->cgroup);
		tg->scx.sched = sch;
		tg->scx.flags |= SCX_TG_INITED;
		tg->scx.flags &= ~SCX_TG_SUB_INIT;
	}

	return 0;

err:
	css_for_each_descendant_post(css, ecss) {
		struct task_group *tg = css_tg(css);

		if (!(tg->scx.flags & SCX_TG_SUB_INIT))
			continue;

		if (SCX_HAS_OP(sch, cgroup_exit))
			SCX_CALL_OP(sch, cgroup_exit, NULL, css->cgroup);
		tg->scx.flags &= ~SCX_TG_SUB_INIT;
	}
	return ret;
}

/**
 * scx_cgroup_return_subtree - Return the subtree's cgroups to the parent sched
 * @sch: sub-scheduler being disabled
 *
 * Called while disabling @sch, after the subtree's cgrp->scx_sched's are reset
 * to the parent sched and before tasks are re-homed, mirroring root disable's
 * cgroups-before-tasks teardown order. The reverse of
 * scx_cgroup_claim_subtree(): exit @sch's cgroups from @sch, then init them on
 * the parent with the current tg->scx.* values, resyncing settings that changed
 * while @sch had them.
 *
 * When an init on the parent fails, the parent is failed - the same policy as
 * task re-homing. The remaining task_groups are punted: they move to the parent
 * anyway with %SCX_TG_INITED cleared, as ops.cgroup_init() failed or never ran
 * for them. A punted task_group gets no cgroup ops. The dying parent's own
 * disable moves it one sched up, initing it there. Root ends the chain: root
 * teardown drops cgroup ops entirely and the next enable's bulk init re-inits
 * every online task_group.
 *
 * The task re-home that follows still delivers ops.init_task() to the dying
 * parent, including for tasks in punted cgroups it never inited - tolerated
 * like the downstream failures of task punting (see scx_punt_task()).
 */
static void scx_cgroup_return_subtree(struct scx_sched *sch)
{
	struct cgroup *sub_cgrp = sch_cgroup(sch);
	struct cgroup_subsys_state *ecss = cgroup_e_css(sub_cgrp, &cpu_cgrp_subsys);
	struct scx_sched *parent = scx_parent(sch);
	struct cgroup_subsys_state *css;
	bool parent_failed = false;
	int ret;

	css_for_each_descendant_post(css, ecss) {
		struct task_group *tg = css_tg(css);

		if (tg->scx.sched != sch ||
		    !cgroup_is_descendant(css->cgroup, sub_cgrp))
			continue;

		/* skip the exit if @sch's ops.cgroup_init() failed for the tg */
		if ((tg->scx.flags & SCX_TG_INITED) && SCX_HAS_OP(sch, cgroup_exit))
			SCX_CALL_OP(sch, cgroup_exit, NULL, css->cgroup);
		tg->scx.sched = parent;
		tg->scx.flags |= SCX_TG_SUB_INIT;
	}

	css_for_each_descendant_pre(css, ecss) {
		struct task_group *tg = css_tg(css);
		struct scx_cgroup_init_args args = {
			.weight = tg->scx.weight,
			.bw_period_us = tg->scx.bw_period_us,
			.bw_quota_us = tg->scx.bw_quota_us,
			.bw_burst_us = tg->scx.bw_burst_us,
			.sched_idle = tg->scx.sched_idle,
		};

		/* the first pass must have transferred everything */
		WARN_ON_ONCE(tg->scx.sched == sch);

		/*
		 * SUB_INIT distinguishes the tgs pass 1 moved. The sched test
		 * can't: a tg punted to the parent by an earlier failure would
		 * also match.
		 */
		if (!(tg->scx.flags & SCX_TG_SUB_INIT))
			continue;
		tg->scx.flags &= ~(SCX_TG_SUB_INIT | SCX_TG_INITED);

		/*
		 * A re-init on $parent failed. The task_groups from here on are
		 * punted: they stay on the dying $parent with INITED clear and
		 * move onward when it disables.
		 */
		if (parent_failed)
			continue;

		if (SCX_HAS_OP(parent, cgroup_init)) {
			ret = SCX_CALL_OP_RET(parent, cgroup_init, NULL, css->cgroup, &args);
			if (ret) {
				scx_error(parent, "ops.cgroup_init() failed (%d) while disabling a sub-scheduler",
					  ret);
				parent_failed = true;
				continue;
			}
		}
		tg->scx.flags |= SCX_TG_INITED;
	}
}
#else
static inline s32 scx_cgroup_claim_subtree(struct scx_sched *sch) { return 0; }
static inline void scx_cgroup_return_subtree(struct scx_sched *sch) {}
#endif

/*
 * Enable @sch's ecaps deliveries on every possible cpu, so that a cpu inactive
 * during the attach report delivers from the reseed at its activation.
 */
static void ecaps_enable(struct scx_sched *sch)
{
	s32 cpu;

	for_each_possible_cpu(cpu) {
		guard(rq_lock_irqsave)(cpu_rq(cpu));
		per_cpu_ptr(sch->pcpu, cpu)->flags |= SCX_SCHED_PCPU_ECAPS_ENABLED;
	}
	sch->ecaps_enabled = true;
}

/**
 * ecaps_report - Report @sch's ecaps and wait
 * @sch: sub-scheduler to report
 *
 * Mark every cpu @sch was ever granted a cap on, deliver its pending change
 * through the dispatch path and wait for it. An inactive cpu is skipped. It may
 * already be offline, where the mark would never be processed and the wait
 * would never end. On attach, the reseed at its activation reports its caps. On
 * detach, its loss was reported as it went down. A cpu that suspend or
 * hibernation took down is back by then, or the hierarchy is aborting, see
 * scx_sub_disable().
 *
 * If a marked cpu goes down meanwhile, a regular down waits for the marked sync
 * in scx_offline_ecaps() while the cpu is still online, as
 * ecaps_report_irq_work would never run on an offline cpu. Suspend and
 * hibernation don't wait, but no report runs across their downs. The attach
 * report runs while the attaching task waits uninterruptibly, which holds off
 * the freezer, and the detach report holds cpus_read_lock().
 *
 * Called with scx_enable_mutex held. A sched's attach and detach reports must
 * not overlap, as the mark is one bit per pcpu.
 */
static void ecaps_report(struct scx_sched *sch)
{
	s32 cpu;

	lockdep_assert_held(&scx_enable_mutex);

	for_each_possible_cpu(cpu) {
		struct scx_sched_pcpu *pcpu = per_cpu_ptr(sch->pcpu, cpu);
		struct rq *rq = cpu_rq(cpu);
		struct scx_pshard *ps;
		s32 cid;

		guard(rq_lock_irqsave)(rq);

		if (!cpu_active(cpu))
			continue;

		cid = __scx_cpu_to_cid(cpu);
		ps = cid_pshard(sch, cid);
		guard(raw_spinlock)(&ps->lock);

		if (!pcpu->had_caps)
			continue;

		pcpu->flags |= SCX_SCHED_PCPU_ECAPS_REPORT;
		atomic_inc(&sch->nr_ecaps_reports);
		queue_sync_ecaps(sch, cid);
		resched_sync_ecaps(rq);
	}

	rcuwait_wait_event(&ecaps_report_wait, !atomic_read(&sch->nr_ecaps_reports),
			   TASK_UNINTERRUPTIBLE);
}

void scx_sub_disable(struct scx_sched *sch)
{
	struct scx_sched *parent = scx_parent(sch);
	struct scx_task_iter sti;
	struct task_struct *p;
	int ret;

	/*
	 * Guarantee forward progress and wait for descendants to be disabled.
	 * To limit disruptions, $parent is not bypassed. Tasks are fully
	 * prepped and then inserted back into $parent.
	 */
	scx_bypass(sch, true);
	drain_descendants(sch);

	/*
	 * Here, every runnable task is guaranteed to make forward progress and
	 * we can safely use blocking synchronization constructs. Actually
	 * disable ops.
	 */
	mutex_lock(&scx_enable_mutex);

	/*
	 * Clear @sch's caps and report the loss before any task goes back to
	 * the parent. A disabling sched is granted nothing, so the caps stay
	 * cleared. Hold off cpu hotplug across both so that a cpu the report
	 * skips has finished going down. Its loss was reported as it went down,
	 * see scx_offline_ecaps().
	 *
	 * The loss on a cpu that suspend or hibernation took down is not
	 * delivered. Wait for such cpus to come back so that the report reaches
	 * them. They go down only after the freezer has frozen the tasks and
	 * come back before the tasks thaw, so the wait doesn't hold up the
	 * freezer. If one fails to come back, the wait can last until the PM
	 * cycle ends, and scx_sub_pm_end() aborts every sched first.
	 *
	 * The check and the report run in one cpus_read_lock() section so that
	 * no cpu goes down for PM between them. scx_sub_pm_end() can still
	 * clear the set meanwhile, which is fine, as it aborts every sched
	 * first. The report also needs scx_enable_mutex, see ecaps_report(). A
	 * cpu coming back takes cpus_write_lock(), so the wait drops
	 * cpus_read_lock(). It drops scx_enable_mutex too so that other enables
	 * and disables don't wait behind the PM cycle, and retakes both in
	 * order.
	 *
	 * An enable that failed before its attach report has nothing to report
	 * and doesn't wait. If it failed before scx_link_sched(), the root's
	 * disable doesn't wait for it and may have retired the cid tables that
	 * the report reads.
	 */
	cpus_read_lock();
	while (sch->ecaps_enabled && !data_race(cpumask_empty(&scx_frozen_cpus))) {
		cpus_read_unlock();
		mutex_unlock(&scx_enable_mutex);
		/* lockless hint, the loop retests */
		wait_event_idle(scx_frozen_cpus_waitq,
				data_race(cpumask_empty(&scx_frozen_cpus)));
		mutex_lock(&scx_enable_mutex);
		cpus_read_lock();
	}
	clear_all_caps(sch);
	if (sch->ecaps_enabled)
		ecaps_report(sch);
	cpus_read_unlock();

	percpu_down_write(&scx_fork_rwsem);
	scx_cgroup_lock();

	/*
	 * An enable that failed before scx_link_sched() succeeded never owned a
	 * cgroup or task and won't be waited on by an ancestor's
	 * drain_descendants(). Nothing to reparent and walking the tasks can
	 * misbehave as the task ownership invariant (either owned by self or
	 * parent) does not hold. ->sibling can't identify this case - an undone
	 * link leaves it non-empty.
	 */
	if (!sch->linked)
		goto dump;

	set_cgroup_sched(sch_cgroup(sch), parent);

	/*
	 * Return the subtree's cgroups before re-homing tasks so that any
	 * ops.init_task() on $parent only sees cgroups it has initialized.
	 */
	scx_cgroup_return_subtree(sch);

	scx_task_iter_start(&sti, sch->cgrp);
	while ((p = scx_task_iter_next_locked(&sti))) {
		struct rq *rq;
		struct rq_flags rf;

		/* filter out duplicate visits */
		if (scx_task_on_sched(parent, p))
			continue;

		/*
		 * By the time control reaches here, all linked descendant
		 * schedulers should have been disabled.
		 */
		WARN_ON_ONCE(!scx_task_on_sched(sch, p));

		/*
		 * @p is pinned by the iter: css_task_iter_next() takes a
		 * reference and holds it until the next iter_next() call, so
		 * @p->usage is guaranteed > 0.
		 */
		get_task_struct(p);

		scx_task_iter_unlock(&sti);

		/*
		 * $p is READY or ENABLED on @sch. Initialize for $parent,
		 * disable and exit from @sch, and then switch over to $parent.
		 *
		 * If a task fails to initialize for $parent, the only available
		 * action is disabling $parent too. While this allows disabling
		 * of a child sched to cause the parent scheduler to fail, the
		 * failure can only originate from ops.init_task() of the
		 * parent. A child can't directly affect the parent through its
		 * own failures.
		 */
		ret = __scx_init_task(parent, p, NULL, false);
		if (ret) {
			scx_fail_parent(sch, p, ret);
			put_task_struct(p);
			break;
		}

		rq = task_rq_lock(p, &rf);

		if (scx_get_task_state(p) == SCX_TASK_DEAD) {
			/*
			 * sched_ext_dead() raced us between __scx_init_task()
			 * and this rq lock and ran exit_task() on @sch (the
			 * sched @p was on at that point), not on $parent.
			 * $parent's just-completed init is owed an exit_task()
			 * and we issue it here.
			 */
			scx_sub_init_cancel_task(parent, p);
			task_rq_unlock(rq, p, &rf);
			put_task_struct(p);
			continue;
		}

		scx_rehome_task(parent, p);

		task_rq_unlock(rq, p, &rf);
		put_task_struct(p);
	}
	scx_task_iter_stop(&sti);

dump:
	scx_disable_dump(sch);

	scx_cgroup_unlock();
	percpu_up_write(&scx_fork_rwsem);

	scx_ops_cid_sched_updated_disable(sch);

	/*
	 * All tasks are moved off of @sch but there may still be on-going
	 * operations (e.g. ops.select_cpu()). Drain them by flushing RCU. Use
	 * the expedited version as ancestors may be waiting in bypass mode.
	 * Also, tell the parent that there is no need to keep running bypass
	 * DSQs for us.
	 */
	synchronize_rcu_expedited();
	scx_disable_bypass_dsp(sch);

	scx_unlink_sched(sch);

	mutex_unlock(&scx_enable_mutex);

	/*
	 * @sch is now unlinked from the parent's children list. Notify and call
	 * ops.sub_detach/exit(). Note that ops.sub_detach/exit() must be called
	 * after unlinking and releasing all locks. See scx_claim_exit().
	 */
	wake_up_all(&scx_unlink_waitq);

	if (parent->ops.sub_detach && sch->sub_attached) {
		struct scx_sub_detach_args sub_detach_args = {
			.ops = &sch->ops,
			.cgroup_path = sch->cgrp_path,
		};
		SCX_CALL_OP(parent, sub_detach, NULL,
			    &sub_detach_args);
	}

	scx_log_sched_disable(sch);

	if (sch->ops.exit)
		SCX_CALL_OP(sch, exit, NULL, sch->exit_info);

	/*
	 * @sch's non-ops programs such as timers and tracers can fire after
	 * ops.exit(). Now that exit is complete, stop scx_prog_sched() from
	 * resolving to @sch and drain in-flight resolvers.
	 */
	WRITE_ONCE(sch->dead, true);
	synchronize_rcu();

	if (sch->sub_kset)
		kobject_del(&sch->sub_kset->kobj);
	/* not added if enable failed before scx_sched_sysfs_add() */
	if (sch->kobj.state_in_sysfs)
		kobject_del(&sch->kobj);
}

/* verify that a scheduler can be attached to @cgrp and return the parent */
static struct scx_sched *find_parent_sched(struct cgroup *cgrp)
{
	struct scx_sched *parent = scx_cgroup_sched(cgrp);
	struct scx_sched *pos;

	lockdep_assert_held(&scx_sched_lock);

	/* can't attach twice to the same cgroup */
	if (parent->cgrp == cgrp)
		return ERR_PTR(-EBUSY);

	/* does $parent allow sub-scheds? */
	if (!parent->ops.sub_attach)
		return ERR_PTR(-EOPNOTSUPP);

	/* can't insert between $parent and its exiting children */
	list_for_each_entry(pos, &parent->children, sibling)
		if (cgroup_is_descendant(pos->cgrp, cgrp))
			return ERR_PTR(-EBUSY);

	return parent;
}

static bool assert_task_ready_or_enabled(struct task_struct *p)
{
	u32 state = scx_get_task_state(p);

	switch (state) {
	case SCX_TASK_READY:
	case SCX_TASK_ENABLED:
		return true;
	default:
		WARN_ONCE(true, "sched_ext: Invalid task state %d for %s[%d] during enabling sub sched",
			  state, p->comm, p->pid);
		return false;
	}
}

void scx_sub_enable_workfn(struct kthread_work *work)
{
	struct scx_enable_cmd *cmd = container_of(work, struct scx_enable_cmd, work);
	struct sched_ext_ops *ops = cmd->ops;
	struct cgroup *cgrp;
	struct scx_sched *parent, *sch;
	struct scx_task_iter sti;
	struct task_struct *p;
	s32 i, ret;

	mutex_lock(&scx_enable_mutex);

	if (!scx_enabled()) {
		ret = -ENODEV;
		goto out_unlock;
	}

	/* See scx_root_enable_workfn() for the @ops->priv check. */
	if (rcu_access_pointer(ops->priv)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	cgrp = cgroup_get_from_id(ops->sub_cgroup_id);
	if (IS_ERR(cgrp)) {
		ret = PTR_ERR(cgrp);
		goto out_unlock;
	}

	raw_spin_lock_irq(&scx_sched_lock);
	parent = find_parent_sched(cgrp);
	if (IS_ERR(parent)) {
		raw_spin_unlock_irq(&scx_sched_lock);
		ret = PTR_ERR(parent);
		goto out_put_cgrp;
	}
	kobject_get(&parent->kobj);
	raw_spin_unlock_irq(&scx_sched_lock);

	/*
	 * Flip the hot-path gates before ops->priv is published - the sub's
	 * programs can e.g. kick cpus from that point on. The matching dec is
	 * at the end of scx_sched_free_rcu_work().
	 */
	static_branch_inc(&__scx_has_subs);

	/* scx_alloc_and_add_sched() consumes @cgrp whether it succeeds or not */
	sch = scx_alloc_and_add_sched(cmd, cgrp, parent);
	kobject_put(&parent->kobj);
	if (IS_ERR(sch)) {
		static_branch_dec(&__scx_has_subs);
		ret = PTR_ERR(sch);
		goto out_unlock;
	}

	/*
	 * Validate before scx_link_sched() publishes @sch, so an invalid sub
	 * never becomes visible with an unallocated pshard.
	 */
	ret = scx_validate_ops(sch, ops);
	if (ret)
		goto err_disable;

	scx_rescue_check_timeout(sch);

	/*
	 * Allocate pshard[] before scx_link_sched() publishes @sch into the
	 * parent's RCU children list. A concurrent revoke walking the tree
	 * would otherwise dereference sch->pshard[si] while it's still NULL.
	 * Unlike the root path, the cid shard layout is stable at this point.
	 *
	 * scx_alloc_pshards() skips allocation when @sch's arena pool isn't
	 * initialized, so scx_arena_pool_init() must run first.
	 */
	ret = scx_arena_pool_init(sch);
	if (ret)
		goto err_disable;

	ret = scx_alloc_pshards(sch);
	if (ret)
		goto err_disable;

	ret = scx_link_sched(sch);
	if (ret)
		goto err_disable;

	ret = scx_sched_sysfs_add(sch);
	if (ret)
		goto err_disable;

	if (sch->level >= SCX_SUB_MAX_DEPTH) {
		scx_error(sch, "max nesting depth %d violated",
			  SCX_SUB_MAX_DEPTH);
		ret = -EINVAL;
		goto err_disable;
	}

	scoped_guard(cpus_read_lock) {
		ret = scx_alloc_kern_arena_objs(sch);
		if (ret)
			goto err_disable;
	}

	if (sch->ops.init) {
		ret = SCX_CALL_OP_RET(sch, init, NULL);
		if (ret) {
			ret = scx_ops_sanitize_err(sch, "init", ret);
			scx_error(sch, "ops.init() failed (%d)", ret);
			goto err_disable;
		}
		sch->exit_info->flags |= SCX_EFLAG_INITIALIZED;
	}

	struct scx_sub_attach_args sub_attach_args = {
		.ops = &sch->ops,
		.cgroup_path = sch->cgrp_path,
	};

	ret = SCX_CALL_OP_RET(parent, sub_attach, NULL,
			      &sub_attach_args);
	if (ret) {
		ret = scx_ops_sanitize_err(sch, "sub_attach", ret);
		scx_error(sch, "parent rejected (%d)", ret);
		goto err_disable;
	}
	sch->sub_attached = true;

	scx_bypass(sch, true);

	for (i = SCX_OPI_BEGIN; i < SCX_OPI_END; i++)
		if (((void (**)(void))ops)[i])
			set_bit(i, sch->has_op);

	scx_ops_cid_sched_updated_enable(sch);

	percpu_down_write(&scx_fork_rwsem);
	scx_cgroup_lock();

	/*
	 * Set cgroup->scx_sched's and check CSS_ONLINE. Either we see
	 * !CSS_ONLINE or scx_cgroup_lifetime_notify() sees and shoots us down.
	 */
	set_cgroup_sched(sch_cgroup(sch), sch);
	if (!(cgrp->self.flags & CSS_ONLINE)) {
		scx_error(sch, "cgroup is not online");
		ret = -ENODEV;
		goto err_unlock_and_disable;
	}

	/*
	 * Take over the subtree's cgroups before any task is claimed,
	 * mirroring root enable's cgroups-before-tasks order.
	 */
	ret = scx_cgroup_claim_subtree(sch);
	if (ret)
		goto err_unlock_and_disable;

	/*
	 * Initialize tasks for the new child $sch without exiting them for
	 * $parent so that the tasks can always be reverted back to $parent
	 * sched on child init failure.
	 */
	WARN_ON_ONCE(scx_enabling_sub_sched);
	scx_enabling_sub_sched = sch;

	scx_task_iter_start(&sti, sch->cgrp);
	while ((p = scx_task_iter_next_locked(&sti))) {
		struct rq *rq;
		struct rq_flags rf;

		/*
		 * Task iteration may visit the same task twice when racing
		 * against exiting. Use %SCX_TASK_SUB_INIT to mark tasks which
		 * finished __scx_init_task() and skip if set.
		 *
		 * A task may exit and get freed between __scx_init_task()
		 * completion and scx_enable_task(). In such cases,
		 * scx_disable_and_exit_task() must exit the task for both the
		 * parent and child scheds.
		 */
		if (p->scx.flags & SCX_TASK_SUB_INIT)
			continue;

		/* @p is pinned by the iter; see scx_sub_disable() */
		get_task_struct(p);

		if (!assert_task_ready_or_enabled(p)) {
			ret = -EINVAL;
			goto abort;
		}

		scx_task_iter_unlock(&sti);

		/*
		 * As $p is still on $parent, it can't be transitioned to INIT.
		 * Let's worry about task state later. Use __scx_init_task().
		 */
		ret = __scx_init_task(sch, p, NULL, false);
		if (ret)
			goto abort;

		rq = task_rq_lock(p, &rf);

		if (scx_get_task_state(p) == SCX_TASK_DEAD) {
			/*
			 * sched_ext_dead() raced us between __scx_init_task()
			 * and this rq lock and ran exit_task() on $parent (the
			 * sched @p was on at that point), not on @sch. @sch's
			 * just-completed init is owed an exit_task() and we
			 * issue it here.
			 */
			scx_sub_init_cancel_task(sch, p);
			task_rq_unlock(rq, p, &rf);
			put_task_struct(p);
			continue;
		}

		p->scx.flags |= SCX_TASK_SUB_INIT;
		task_rq_unlock(rq, p, &rf);

		put_task_struct(p);
	}
	scx_task_iter_stop(&sti);

	/*
	 * All tasks are prepped. Disable/exit tasks for $parent and enable for
	 * the new @sch.
	 */
	scx_task_iter_start(&sti, sch->cgrp);
	while ((p = scx_task_iter_next_locked(&sti))) {
		/*
		 * Use clearing of %SCX_TASK_SUB_INIT to detect and skip
		 * duplicate iterations.
		 */
		if (!(p->scx.flags & SCX_TASK_SUB_INIT))
			continue;

		scx_prepare_task_sched_change(p);
		scoped_guard (sched_change, p,
			      DEQUEUE_SAVE | DEQUEUE_MOVE | DEQUEUE_NOCLOCK) {
			/*
			 * $p must be either READY or ENABLED. If ENABLED,
			 * __scx_disabled_and_exit_task() first disables and
			 * makes it READY. However, after exiting $p, it will
			 * leave $p as READY.
			 */
			assert_task_ready_or_enabled(p);
			__scx_disable_and_exit_task(parent, p);

			/*
			 * $p is now only initialized for @sch and READY, which
			 * is what we want. Assign it to @sch and, if it's on
			 * the ext class, enable. A non-ext task, possible under
			 * an %SCX_OPS_SWITCH_PARTIAL root, stays READY and is
			 * enabled by switching_to_scx() if it switches over.
			 */
			scx_set_task_sched(p, sch);
			if (p->sched_class == &ext_sched_class)
				scx_enable_task(sch, p);

			p->scx.flags &= ~SCX_TASK_SUB_INIT;
		}
	}
	scx_task_iter_stop(&sti);

	scx_enabling_sub_sched = NULL;

	scx_cgroup_unlock();
	percpu_up_write(&scx_fork_rwsem);

	/*
	 * Report the caps granted from ops.sub_attach() before bypass lifts and
	 * @sch makes its first scheduling decision. The report sleeps until its
	 * syncs are processed, so it runs outside the fork rwsem.
	 */
	scx_sub_seed_caps(sch);
	ecaps_enable(sch);
	ecaps_report(sch);

	scx_bypass(sch, false);

	pr_info("sched_ext: BPF sub-scheduler \"%s\" enabled\n", sch->ops.name);
	kobject_uevent(&sch->kobj, KOBJ_ADD);
	ret = 0;
	goto out_unlock;

out_put_cgrp:
	cgroup_put(cgrp);
out_unlock:
	mutex_unlock(&scx_enable_mutex);
	cmd->ret = ret;
	return;

abort:
	put_task_struct(p);
	scx_task_iter_stop(&sti);

	/*
	 * Undo __scx_init_task() for tasks we marked. scx_enable_task() never
	 * ran for @sch on them, so calling scx_disable_task() here would invoke
	 * ops.disable() without a matching ops.enable(). scx_enabling_sub_sched
	 * must stay set until SUB_INIT is cleared from every marked task -
	 * scx_disable_and_exit_task() reads it when a task exits concurrently.
	 */
	scx_task_iter_start(&sti, sch->cgrp);
	while ((p = scx_task_iter_next_locked(&sti))) {
		if (p->scx.flags & SCX_TASK_SUB_INIT) {
			scx_sub_init_cancel_task(sch, p);
			p->scx.flags &= ~SCX_TASK_SUB_INIT;
		}
	}
	scx_task_iter_stop(&sti);
	scx_enabling_sub_sched = NULL;
err_unlock_and_disable:
	/* we'll soon enter disable path, keep bypass on */
	scx_cgroup_unlock();
	percpu_up_write(&scx_fork_rwsem);
err_disable:
	mutex_unlock(&scx_enable_mutex);
	/*
	 * Some enable failures only return an errno (e.g. -ENOMEM from an
	 * allocation) without calling scx_error(). Record it so
	 * scx_flush_disable_work() runs the disable and ops.exit() fires.
	 */
	scx_error(sch, "scx_sub_enable() failed (%d)", ret);
	scx_flush_disable_work(sch);
	cmd->ret = 0;
}

/**
 * scx_cgroup_task_migrating - Prepare a task for a cgroup migration
 * @ctx: migration being prepared
 *
 * A task's sched must match its cgroup's owner, so a migration that crosses a
 * sched boundary re-homes the task once committed. Run the fallible part here,
 * before the migration commits: initialize the task for the destination sched.
 * A rejection fails the cgroup.procs write.
 */
static s32 scx_cgroup_task_migrating(struct cgroup_task_migrate_ctx *ctx)
{
	struct task_struct *p = ctx->task;
	struct scx_sched *to;
	int ret;

	/*
	 * Cleared under scx_cgroup_lock() before root disable starts tearing
	 * down tasks. As cgroup_mutex is held, a set flag guarantees that the
	 * teardown loop is not running concurrently.
	 */
	if (!scx_cgroup_enabled)
		return NOTIFY_OK;

	to = scx_cgroup_sched(ctx->dst_dcgrp);
	if (scx_task_on_sched(to, p))
		return NOTIFY_OK;

	ret = __scx_init_task(to, p, ctx->dst_dcgrp, false);
	if (ret)
		return notifier_from_errno(ret);

	return NOTIFY_OK;
}

/**
 * scx_cgroup_task_migrated - Re-home a task that changed cgroups
 * @ctx: committed migration
 *
 * Move the task to its new cgroup's sched, which scx_cgroup_task_migrating()
 * already initialized it for. Can't fail.
 *
 * This is safe against all phases of the destination sched's destruction. A
 * disable resets cgroup ownership to the parent and re-homes tasks in one
 * scx_cgroup_lock() section. If that section already ran, the destination would
 * be the parent. Otherwise, the re-home loop is still ahead and guaranteed to
 * visit the task, now in the destination cgroup.
 */
static void scx_cgroup_task_migrated(struct cgroup_task_migrate_ctx *ctx)
{
	struct task_struct *p = ctx->task;
	struct scx_sched *to;
	struct rq *rq;
	struct rq_flags rf;

	if (!scx_cgroup_enabled)
		return;

	to = scx_cgroup_sched(ctx->dst_dcgrp);
	if (scx_task_on_sched(to, p))
		return;

	rq = task_rq_lock(p, &rf);
	scx_rehome_task(to, p);
	task_rq_unlock(rq, p, &rf);
}

/**
 * scx_cgroup_task_migrate_canceled - Undo migration preparation
 * @ctx: canceled migration
 *
 * The migration failed after scx_cgroup_task_migrating() initialized the task
 * for the destination sched. The task stays on its current sched in the source
 * cgroup. Undo the destination's init.
 */
static void scx_cgroup_task_migrate_canceled(struct cgroup_task_migrate_ctx *ctx)
{
	struct task_struct *p = ctx->task;
	struct scx_sched *to;
	struct rq *rq;
	struct rq_flags rf;

	if (!scx_cgroup_enabled)
		return;

	to = scx_cgroup_sched(ctx->dst_dcgrp);
	if (scx_task_on_sched(to, p))
		return;

	rq = task_rq_lock(p, &rf);
	scx_sub_init_cancel_task(to, p);
	task_rq_unlock(rq, p, &rf);
}

static s32 scx_cgroup_lifetime_notify(struct notifier_block *nb,
				      unsigned long action, void *data)
{
	struct cgroup *cgrp = data;
	struct cgroup *parent = cgroup_parent(cgrp);
	struct scx_sched *sch;

	if (!cgroup_on_dfl(cgrp))
		return NOTIFY_OK;

	switch (action) {
	case CGROUP_LIFETIME_ONLINE:
		/* inherit ->scx_sched from $parent */
		if (parent)
			rcu_assign_pointer(cgrp->scx_sched, scx_cgroup_sched(parent));
		break;
	case CGROUP_LIFETIME_OFFLINE:
		/* if there is a sched attached, shoot it down */
		sch = scx_cgroup_sched(cgrp);
		if (sch && sch->cgrp == cgrp)
			scx_exit(sch, SCX_EXIT_UNREG_KERN,
				 SCX_ECODE_RSN_CGROUP_OFFLINE,
				 "cgroup %llu going offline", cgroup_id(cgrp));
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block scx_cgroup_lifetime_nb = {
	.notifier_call = scx_cgroup_lifetime_notify,
};

static s32 scx_cgroup_task_notify(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	struct cgroup_task_migrate_ctx *ctx = data;

	switch (action) {
	case CGROUP_TASK_MIGRATING:
		return scx_cgroup_task_migrating(ctx);
	case CGROUP_TASK_MIGRATED:
		scx_cgroup_task_migrated(ctx);
		break;
	case CGROUP_TASK_MIGRATE_CANCELED:
		scx_cgroup_task_migrate_canceled(ctx);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block scx_cgroup_task_nb = {
	.notifier_call = scx_cgroup_task_notify,
};

static s32 __init scx_cgroup_notifier_init(void)
{
	s32 ret;

	ret = blocking_notifier_chain_register(&cgroup_lifetime_notifier,
					       &scx_cgroup_lifetime_nb);
	if (ret)
		return ret;

	return blocking_notifier_chain_register(&cgroup_task_notifier,
						&scx_cgroup_task_nb);
}
core_initcall(scx_cgroup_notifier_init);

static void scx_pstack_recursion(struct bpf_prog *prog, const char *op)
{
	struct scx_sched *sch;

	guard(rcu)();
	sch = scx_prog_sched(prog->aux);
	if (unlikely(!sch))
		return;

	scx_error(sch, "%s recursion detected", op);
}

void scx_pstack_recursion_on_dispatch(struct bpf_prog *prog)
{
	scx_pstack_recursion(prog, "dispatch");
}

void scx_pstack_recursion_on_caps_updated(struct bpf_prog *prog)
{
	scx_pstack_recursion(prog, "sub_caps_updated");
}

__bpf_kfunc_start_defs();

/**
 * scx_bpf_sub_dispatch - Trigger dispatching on a child scheduler
 * @cgroup_id: cgroup ID of the child scheduler to dispatch
 * @aux: implicit BPF argument to access bpf_prog_aux hidden from BPF progs
 *
 * Allows a parent scheduler to trigger dispatching on one of its direct
 * child schedulers. The child scheduler runs its dispatch operation to
 * move tasks from dispatch queues to the local runqueue.
 *
 * Dispatch into a child while the local DSQ is empty, when the cpu needs a
 * task. A child holding only %SCX_CAP_ENQ_IMMED can insert only IMMED tasks,
 * and the consume of its bypassing descendants' bypass DSQs is one such insert.
 * With a task already queued on the local DSQ, such an insert fails and the
 * task is reenqueued.
 *
 * Returns: true on success, false if cgroup_id is invalid, not a direct
 * child, or caller lacks dispatch permission.
 */
__bpf_kfunc bool scx_bpf_sub_dispatch(u64 cgroup_id, const struct bpf_prog_aux *aux)
{
	struct rq *rq = scx_locked_rq();
	struct scx_sched *parent, *child;

	guard(rcu)();
	parent = scx_prog_sched(aux);
	if (unlikely(!parent))
		return false;

	child = scx_find_sub_sched(cgroup_id);

	if (unlikely(!child))
		return false;

	if (unlikely(scx_parent(child) != parent)) {
		scx_error(parent, "trying to dispatch a distant sub-sched on cgroup %llu",
			  cgroup_id);
		return false;
	}

	/*
	 * Skip a child that does not effectively hold the base cap on this cpu:
	 * its inserts would only be rejected. ecaps are synced at the top of
	 * dispatch_one() before dispatch, so this reflects the in-effect state.
	 */
	if (scx_missing_caps(child, cpu_of(rq), SCX_CAP_BASE))
		return false;

	return scx_dispatch_sched(child, rq, rq->scx.sub_dispatch_prev, true) !=
		SCX_DSP_NONE;
}

/* Validate common inputs. On success, *parent_out and *child_out are set. */
static s32 sub_cap_preamble(u64 cgroup_id, u64 caps, const struct bpf_prog_aux *aux,
			    struct scx_sched **parent_out, struct scx_sched **child_out)
{
	struct scx_sched *parent, *child;

	parent = scx_prog_sched(aux);
	if (unlikely(!parent))
		return -ENODEV;

	if (!scx_kf_allowed_ctx(parent))
		return -EDEADLK;

	if (!scx_is_cid_type()) {
		scx_error(parent, "sub-cap kfuncs require a cid-form scheduler");
		return -EOPNOTSUPP;
	}

	child = scx_find_sub_sched(cgroup_id);
	if (unlikely(!child))
		return -ENODEV;

	if (unlikely(scx_parent(child) != parent)) {
		scx_error(parent, "%s: sub-%llu is not a direct child",
			  parent->cgrp_path, cgroup_id);
		return -EINVAL;
	}

	if (unlikely(caps & ~__SCX_CAP_ALL)) {
		scx_error(parent, "invalid caps 0x%llx", caps);
		return -EINVAL;
	}

	*parent_out = parent;
	*child_out = child;
	return 0;
}

/**
 * scx_bpf_sub_grant - Grant @caps on a cmask's cids to a direct child
 * @cgroup_id: cgroup id of the direct child sub-sched
 * @caps: bitmask of SCX_CAP_* to grant
 * @cmask__arena: cid cmask to grant @caps on
 * @denied_out__arena__nullable: optional cmask accumulating refused cids
 * @aux: implicit BPF argument
 *
 * A cid in @cmask__arena is granted to the child only if the parent holds every
 * requested cap on it. Refused cids are OR'd into the denied mask when
 * provided. Refusals outside the denied mask's range are not recorded.
 *
 * All-or-nothing keeps the caller-visible result binary per cid, so the denied
 * mask is one mask to interpret rather than a per-cap matrix.
 *
 * Return 0 on full success, -EPERM if any cid was refused, -ENODEV if the child
 * is gone or being disabled, or a negative errno on other failures.
 */
__bpf_kfunc s32 scx_bpf_sub_grant(u64 cgroup_id, u64 caps,
				  const struct scx_cmask *cmask__arena,
				  struct scx_cmask *denied_out__arena__nullable,
				  const struct bpf_prog_aux *aux)
{
	struct scx_cmask_ref ref, denied_ref;
	struct scx_sched *parent, *child;
	bool any_denied = false;
	LIST_HEAD(to_deliver);
	s32 si, ret;

	guard(irqsave)();

	ret = sub_cap_preamble(cgroup_id, caps, aux, &parent, &child);
	if (ret)
		return ret;

	ret = scx_cmask_ref_init(parent, cmask__arena, &ref);
	if (ret) {
		scx_error(parent, "invalid cmask (%d)", ret);
		return ret;
	}

	if (denied_out__arena__nullable) {
		ret = scx_cmask_ref_init(parent, denied_out__arena__nullable, &denied_ref);
		if (ret) {
			scx_error(parent, "invalid denied_out (%d)", ret);
			return ret;
		}
	}

	/* apply the grant one shard at a time */
	for (si = ref.shard_first; si < ref.shard_end; si++) {
		SCX_CMASK_DEFINE_SHARD(slice, 0, SCX_CID_SHARD_MAX_CPUS);
		struct scx_pshard *pps = parent->pshard[si];
		struct scx_pshard *cps = child->pshard[si];
		u64 granted_caps = 0;
		u32 cap_bit;

		scx_cmask_ref_shard(&ref, si, slice);
		if (scx_cmask_empty(slice))
			continue;

		SCX_CMASK_DEFINE_SHARD(granted_cids, slice->base, slice->nr_cids);
		SCX_CMASK_DEFINE_SHARD(changed_cids, slice->base, slice->nr_cids);
		SCX_CMASK_DEFINE_SHARD(delta, slice->base, slice->nr_cids);

		scx_cmask_copy(granted_cids, slice);

		scoped_guard (raw_spinlock, &pps->lock) {
			guard(raw_spinlock_nested)(&cps->lock);

			/* the child is being disabled, see clear_all_caps() */
			if (unlikely(READ_ONCE(child->aborting))) {
				ret = -ENODEV;
				goto out;
			}

			/*
			 * Narrow granted_cids to cids the parent holds every
			 * requested cap on. All-or-nothing per cid.
			 */
			scx_for_each_cap_bit(cap_bit, caps)
				scx_cmask_and(granted_cids, &pps->caps[cap_bit].cmask);

			/*
			 * For each requested cap, fold the newly-set cids into
			 * the child and accumulate the delta.
			 */
			scx_for_each_cap_bit(cap_bit, caps) {
				struct scx_cmask *ccm = &cps->caps[cap_bit].cmask;

				scx_cmask_copy(delta, granted_cids);
				scx_cmask_andnot(delta, ccm);
				if (scx_cmask_empty(delta))
					continue;

				scx_cmask_or(ccm, delta);
				scx_cmask_or(changed_cids, delta);
				granted_caps |= BIT_U64(cap_bit);
			}

			if (granted_caps) {
				s32 cid;

				caps_updated_record(cps, changed_cids, granted_caps,
						    &to_deliver);
				/*
				 * The sync arms an update_idle() re-notify if
				 * the cid gains baseline access, so the holder
				 * learns of an already-idle cid.
				 */
				scx_cmask_for_each_cid(cid, changed_cids) {
					s32 cpu = __scx_cid_to_cpu(cid);

					per_cpu_ptr(child->pcpu, cpu)->had_caps = true;
					queue_sync_ecaps(child, cid);
				}
			}
		}

		/* record cids that didn't make it into the denied mask */
		if (!scx_cmask_subset(slice, granted_cids)) {
			any_denied = true;
			if (denied_out__arena__nullable) {
				SCX_CMASK_DEFINE_SHARD(denied, slice->base, slice->nr_cids);

				scx_cmask_copy(denied, slice);
				scx_cmask_andnot(denied, granted_cids);
				scx_cmask_ref_or(&denied_ref, denied);
			}
		}
	}

	ret = any_denied ? -EPERM : 0;
out:
	caps_updated_deliver(&to_deliver);
	return ret;
}

/**
 * scx_bpf_sub_revoke - Revoke @caps on a cmask's cids from a direct child
 * @cgroup_id: cgroup id of the direct child sub-sched
 * @caps: bitmask of SCX_CAP_* to revoke
 * @cmask__arena: cid cmask to revoke @caps on
 * @aux: implicit BPF argument
 *
 * Clear @caps bits on @cmask__arena from the child named by @cgroup_id and all
 * its descendants. The origin parent's pshard lock is held across the subtree
 * walk so a concurrent grant from the origin parent observes the revoked state.
 */
__bpf_kfunc void scx_bpf_sub_revoke(u64 cgroup_id, u64 caps,
				    const struct scx_cmask *cmask__arena,
				    const struct bpf_prog_aux *aux)
{
	struct scx_cmask_ref ref;
	struct scx_sched *parent, *child, *pos;
	LIST_HEAD(to_deliver);
	s32 si, ret;

	guard(irqsave)();

	if (sub_cap_preamble(cgroup_id, caps, aux, &parent, &child))
		return;

	ret = scx_cmask_ref_init(parent, cmask__arena, &ref);
	if (ret) {
		scx_error(parent, "invalid cmask (%d)", ret);
		return;
	}

	/* per-shard, walk child's subtree and clear @caps */
	for (si = ref.shard_first; si < ref.shard_end; si++) {
		SCX_CMASK_DEFINE_SHARD(slice, 0, SCX_CID_SHARD_MAX_CPUS);

		scx_cmask_ref_shard(&ref, si, slice);
		if (scx_cmask_empty(slice))
			continue;

		/*
		 * Pre-order with subtree skip: a descendant that cleared
		 * nothing means no descendant of it can hold @caps on these
		 * cids either.
		 */
		guard(raw_spinlock)(&parent->pshard[si]->lock);
		pos = scx_next_descendant_pre(NULL, child);
		while (pos) {
			struct scx_pshard *ps = pos->pshard[si];
			SCX_CMASK_DEFINE_SHARD(changed_cids, slice->base, slice->nr_cids);
			SCX_CMASK_DEFINE_SHARD(delta, slice->base, slice->nr_cids);
			u64 revoked_caps = 0;
			u32 cap_bit;

			scoped_guard (raw_spinlock_nested, &ps->lock) {
				/*
				 * For each cap, clear lost cids and accumulate
				 * the per-cap diff for notification.
				 */
				scx_for_each_cap_bit(cap_bit, caps) {
					struct scx_cmask *cm = &ps->caps[cap_bit].cmask;

					scx_cmask_copy(delta, cm);
					scx_cmask_and(delta, slice);
					if (scx_cmask_empty(delta))
						continue;

					scx_cmask_andnot(cm, delta);
					scx_cmask_or(changed_cids, delta);
					revoked_caps |= BIT_U64(cap_bit);
				}

				if (revoked_caps) {
					s32 cid;

					caps_updated_record(ps, changed_cids, revoked_caps,
							    &to_deliver);
					scx_cmask_for_each_cid(cid, changed_cids)
						queue_sync_ecaps(pos, cid);
				}
			}

			if (revoked_caps)
				pos = scx_next_descendant_pre(pos, child);
			else
				pos = scx_skip_subtree_pre(pos, child);
		}
	}

	caps_updated_deliver(&to_deliver);
}

/**
 * scx_bpf_sub_caps - Read self's or a direct child's cap cmasks
 * @cgroup_id: 0 for self, or a direct child's cgroup id
 * @caps: one or more SCX_CAP_* bits
 * @out__arena: cmask to receive the union of @caps within its range
 * @aux: implicit BPF argument
 *
 * Read the cap cmasks granted on each cid for self (@cgroup_id 0) or a direct
 * child - the literal granted set. A sched can read only itself or a direct
 * child.
 *
 * Return 0, -ENODEV if @cgroup_id names no direct child, or -EINVAL on bad
 * inputs.
 */
__bpf_kfunc s32 scx_bpf_sub_caps(u64 cgroup_id, u64 caps, struct scx_cmask *out__arena,
				 const struct bpf_prog_aux *aux)
{
	struct scx_cmask_ref ref;
	struct scx_sched *sch, *target;
	struct scx_pshard **pshard;
	s32 si, ret;

	guard(irqsave)();

	sch = scx_prog_sched(aux);
	if (unlikely(!sch))
		return -ENODEV;

	if (!scx_is_cid_type()) {
		scx_error(sch, "sub-cap kfuncs require a cid-form scheduler");
		return -EOPNOTSUPP;
	}

	if (unlikely(caps & ~__SCX_CAP_ALL)) {
		scx_error(sch, "invalid caps 0x%llx", caps);
		return -EINVAL;
	}

	/* @cgroup_id 0 reads self, otherwise a direct child */
	if (cgroup_id) {
		target = scx_find_sub_sched(cgroup_id);
		if (unlikely(!target))
			return -ENODEV;
		if (unlikely(scx_parent(target) != sch)) {
			scx_error(sch, "%s: sub-%llu is not a direct child",
				  sch->cgrp_path, cgroup_id);
			return -EINVAL;
		}
	} else {
		target = sch;
	}

	/*
	 * The target's caps storage may not be set up yet (e.g. a self-read
	 * during ops.init_cids()). Pairs with the publish in
	 * scx_alloc_pshards(): a non-NULL pshard has every element set and the
	 * acquire also orders the cid table reads below against it.
	 */
	pshard = smp_load_acquire(&target->pshard);
	if (unlikely(!pshard)) {
		scx_error(sch, "scx_bpf_sub_caps() called before caps storage is initialized");
		return -ENODEV;
	}

	ret = scx_cmask_ref_init(sch, out__arena, &ref);
	if (ret) {
		scx_error(sch, "invalid out (%d)", ret);
		return ret;
	}

	for (si = ref.shard_first; si < ref.shard_end; si++) {
		const struct scx_cid_shard *shard =
			&rcu_dereference_all(scx_cid_shard_ranges)[si];
		SCX_CMASK_DEFINE_SHARD(local_out, shard->base_cid, shard->nr_cids);
		u32 cap_bit;

		scx_for_each_cap_bit(cap_bit, caps)
			scx_cmask_or(local_out, &pshard[si]->caps[cap_bit].cmask);
		scx_cmask_ref_copy(&ref, local_out);
	}
	return 0;
}

/**
 * scx_bpf_sub_kill_bstr - Kill a direct child sub-scheduler
 * @cgroup_id: cgroup id of the direct child to kill
 * @fmt: reason message format string
 * @data: format string parameters packaged using ___bpf_fill() macro
 * @data__sz: @data len, must end in '__sz' for the verifier
 * @aux: implicit BPF argument to access bpf_prog_aux hidden from BPF progs
 *
 * Evict a direct child sub-scheduler, disabling it with the supplied reason.
 * The child and its subtree are torn down asynchronously through the usual
 * disable path.
 *
 * Unlike scx_bpf_exit(), no exit code is taken: the child is a separate
 * scheduler with its own exit-code semantics, so a code chosen by the parent
 * would have no defined meaning. The reason string carries the intent.
 *
 * Return 0 on success or -ENODEV if @cgroup_id names no sub-scheduler, which
 * can race with the child detaching on its own and so is not a scheduler error.
 * Naming a sched that exists but is not a direct child aborts the parent.
 */
__printf(2, 0)
__bpf_kfunc s32 scx_bpf_sub_kill_bstr(u64 cgroup_id, char *fmt,
				      unsigned long long *data, u32 data__sz,
				      const struct bpf_prog_aux *aux)
{
	struct scx_sched *parent, *child;

	guard(rcu)();

	parent = scx_prog_sched(aux);
	if (unlikely(!parent))
		return -ENODEV;

	if (!scx_is_cid_type()) {
		scx_error(parent, "sub-cap kfuncs require a cid-form scheduler");
		return -EOPNOTSUPP;
	}

	child = scx_find_sub_sched(cgroup_id);
	if (unlikely(!child))
		return -ENODEV;

	if (unlikely(scx_parent(child) != parent)) {
		scx_error(parent, "%s: sub-%llu is not a direct child",
			  parent->cgrp_path, cgroup_id);
		return -EINVAL;
	}

	scx_exit_bstr(child, SCX_EXIT_PARENT_KILL, 0, parent, fmt, data, data__sz);
	return 0;
}

__bpf_kfunc_end_defs();

#else	/* !CONFIG_EXT_SUB_SCHED */

__bpf_kfunc_start_defs();

__bpf_kfunc s32 scx_bpf_sub_grant(u64 cgroup_id, u64 caps,
				  const struct scx_cmask *cmask__arena,
				  struct scx_cmask *denied_out__arena__nullable,
				  const struct bpf_prog_aux *aux)
{
	return -EOPNOTSUPP;
}

__bpf_kfunc void scx_bpf_sub_revoke(u64 cgroup_id, u64 caps,
				    const struct scx_cmask *cmask__arena,
				    const struct bpf_prog_aux *aux)
{
}

__bpf_kfunc s32 scx_bpf_sub_caps(u64 cgroup_id, u64 caps, struct scx_cmask *out__arena,
				 const struct bpf_prog_aux *aux)
{
	return -EOPNOTSUPP;
}

__bpf_kfunc s32 scx_bpf_sub_kill_bstr(u64 cgroup_id, char *fmt,
				      unsigned long long *data, u32 data__sz,
				      const struct bpf_prog_aux *aux)
{
	return -EOPNOTSUPP;
}

__bpf_kfunc_end_defs();

#endif	/* CONFIG_EXT_SUB_SCHED */
