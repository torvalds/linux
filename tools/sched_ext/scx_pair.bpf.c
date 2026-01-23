/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A demo sched_ext core-scheduler which always makes every sibling CPU pair
 * execute from the same CPU cgroup.
 *
 * This scheduler is a minimal implementation and would need some form of
 * priority handling both inside each cgroup and across the cgroups to be
 * practically useful.
 *
 * Each CPU in the system is paired with exactly one other CPU, according to a
 * "stride" value that can be specified when the BPF scheduler program is first
 * loaded. Throughout the runtime of the scheduler, these CPU pairs guarantee
 * that they will only ever schedule tasks that belong to the same CPU cgroup.
 *
 * Scheduler Initialization
 * ------------------------
 *
 * The scheduler BPF program is first initialized from user space, before it is
 * enabled. During this initialization process, each CPU on the system is
 * assigned several values that are constant throughout its runtime:
 *
 * 1. *Pair CPU*: The CPU that it synchronizes with when making scheduling
 *		  decisions. Paired CPUs always schedule tasks from the same
 *		  CPU cgroup, and synchronize with each other to guarantee
 *		  that this constraint is not violated.
 * 2. *Pair ID*:  Each CPU pair is assigned a Pair ID, which is used to access
 *		  a struct pair_ctx object that is shared between the pair.
 * 3. *In-pair-index*: An index, 0 or 1, that is assigned to each core in the
 *		       pair. Each struct pair_ctx has an active_mask field,
 *		       which is a bitmap used to indicate whether each core
 *		       in the pair currently has an actively running task.
 *		       This index specifies which entry in the bitmap corresponds
 *		       to each CPU in the pair.
 *
 * During this initialization, the CPUs are paired according to a "stride" that
 * may be specified when invoking the user space program that initializes and
 * loads the scheduler. By default, the stride is 1/2 the total number of CPUs.
 *
 * Tasks and cgroups
 * -----------------
 *
 * Every cgroup in the system is registered with the scheduler using the
 * pair_cgroup_init() callback, and every task in the system is associated with
 * exactly one cgroup. At a high level, the idea with the pair scheduler is to
 * always schedule tasks from the same cgroup within a given CPU pair. When a
 * task is enqueued (i.e. passed to the pair_enqueue() callback function), its
 * cgroup ID is read from its task struct, and then a corresponding queue map
 * is used to FIFO-enqueue the task for that cgroup.
 *
 * If you look through the implementation of the scheduler, you'll notice that
 * there is quite a bit of complexity involved with looking up the per-cgroup
 * FIFO queue that we enqueue tasks in. For example, there is a cgrp_q_idx_hash
 * BPF hash map that is used to map a cgroup ID to a globally unique ID that's
 * allocated in the BPF program. This is done because we use separate maps to
 * store the FIFO queue of tasks, and the length of that map, per cgroup. This
 * complexity is only present because of current deficiencies in BPF that will
 * soon be addressed. The main point to keep in mind is that newly enqueued
 * tasks are added to their cgroup's FIFO queue.
 *
 * Dispatching tasks
 * -----------------
 *
 * This section will describe how enqueued tasks are dispatched and scheduled.
 * Tasks are dispatched in pair_dispatch(), and at a high level the workflow is
 * as follows:
 *
 * 1. Fetch the struct pair_ctx for the current CPU. As mentioned above, this is
 *    the structure that's used to synchronize amongst the two pair CPUs in their
 *    scheduling decisions. After any of the following events have occurred:
 *
 * - The cgroup's slice run has expired, or
 * - The cgroup becomes empty, or
 * - Either CPU in the pair is preempted by a higher priority scheduling class
 *
 * The cgroup transitions to the draining state and stops executing new tasks
 * from the cgroup.
 *
 * 2. If the pair is still executing a task, mark the pair_ctx as draining, and
 *    wait for the pair CPU to be preempted.
 *
 * 3. Otherwise, if the pair CPU is not running a task, we can move onto
 *    scheduling new tasks. Pop the next cgroup id from the top_q queue.
 *
 * 4. Pop a task from that cgroup's FIFO task queue, and begin executing it.
 *
 * Note again that this scheduling behavior is simple, but the implementation
 * is complex mostly because this it hits several BPF shortcomings and has to
 * work around in often awkward ways. Most of the shortcomings are expected to
 * be resolved in the near future which should allow greatly simplifying this
 * scheduler.
 *
 * Dealing with preemption
 * -----------------------
 *
 * SCX is the lowest priority sched_class, and could be preempted by them at
 * any time. To address this, the scheduler implements pair_cpu_release() and
 * pair_cpu_acquire() callbacks which are invoked by the core scheduler when
 * the scheduler loses and gains control of the CPU respectively.
 *
 * In pair_cpu_release(), we mark the pair_ctx as having been preempted, and
 * then invoke:
 *
 * scx_bpf_kick_cpu(pair_cpu, SCX_KICK_PREEMPT | SCX_KICK_WAIT);
 *
 * This preempts the pair CPU, and waits until it has re-entered the scheduler
 * before returning. This is necessary to ensure that the higher priority
 * sched_class that preempted our scheduler does not schedule a task
 * concurrently with our pair CPU.
 *
 * When the CPU is re-acquired in pair_cpu_acquire(), we unmark the preemption
 * in the pair_ctx, and send another resched IPI to the pair CPU to re-enable
 * pair scheduling.
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#include <scx/common.bpf.h>
#include "scx_pair.h"

char _license[] SEC("license") = "GPL";

/* !0 for veristat, set during init */
const volatile u32 nr_cpu_ids = 1;

/* a pair of CPUs stay on a cgroup for this duration */
const volatile u32 pair_batch_dur_ns;

/* cpu ID -> pair cpu ID */
const volatile s32 RESIZABLE_ARRAY(rodata, pair_cpu);

/* cpu ID -> pair_id */
const volatile u32 RESIZABLE_ARRAY(rodata, pair_id);

/* CPU ID -> CPU # in the pair (0 or 1) */
const volatile u32 RESIZABLE_ARRAY(rodata, in_pair_idx);

struct pair_ctx {
	struct bpf_spin_lock	lock;

	/* the cgroup the pair is currently executing */
	u64			cgid;

	/* the pair started executing the current cgroup at */
	u64			started_at;

	/* whether the current cgroup is draining */
	bool			draining;

	/* the CPUs that are currently active on the cgroup */
	u32			active_mask;

	/*
	 * the CPUs that are currently preempted and running tasks in a
	 * different scheduler.
	 */
	u32			preempted_mask;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, u32);
	__type(value, struct pair_ctx);
} pair_ctx SEC(".maps");

/* queue of cgrp_q's possibly with tasks on them */
struct {
	__uint(type, BPF_MAP_TYPE_QUEUE);
	/*
	 * Because it's difficult to build strong synchronization encompassing
	 * multiple non-trivial operations in BPF, this queue is managed in an
	 * opportunistic way so that we guarantee that a cgroup w/ active tasks
	 * is always on it but possibly multiple times. Once we have more robust
	 * synchronization constructs and e.g. linked list, we should be able to
	 * do this in a prettier way but for now just size it big enough.
	 */
	__uint(max_entries, 4 * MAX_CGRPS);
	__type(value, u64);
} top_q SEC(".maps");

/* per-cgroup q which FIFOs the tasks from the cgroup */
struct cgrp_q {
	__uint(type, BPF_MAP_TYPE_QUEUE);
	__uint(max_entries, MAX_QUEUED);
	__type(value, u32);
};

/*
 * Ideally, we want to allocate cgrp_q and cgrq_q_len in the cgroup local
 * storage; however, a cgroup local storage can only be accessed from the BPF
 * progs attached to the cgroup. For now, work around by allocating array of
 * cgrp_q's and then allocating per-cgroup indices.
 *
 * Another caveat: It's difficult to populate a large array of maps statically
 * or from BPF. Initialize it from userland.
 */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, MAX_CGRPS);
	__type(key, s32);
	__array(values, struct cgrp_q);
} cgrp_q_arr SEC(".maps");

static u64 cgrp_q_len[MAX_CGRPS];

/*
 * This and cgrp_q_idx_hash combine into a poor man's IDR. This likely would be
 * useful to have as a map type.
 */
static u32 cgrp_q_idx_cursor;
static u64 cgrp_q_idx_busy[MAX_CGRPS];

/*
 * All added up, the following is what we do:
 *
 * 1. When a cgroup is enabled, RR cgroup_q_idx_busy array doing cmpxchg looking
 *    for a free ID. If not found, fail cgroup creation with -EBUSY.
 *
 * 2. Hash the cgroup ID to the allocated cgrp_q_idx in the following
 *    cgrp_q_idx_hash.
 *
 * 3. Whenever a cgrp_q needs to be accessed, first look up the cgrp_q_idx from
 *    cgrp_q_idx_hash and then access the corresponding entry in cgrp_q_arr.
 *
 * This is sadly complicated for something pretty simple. Hopefully, we should
 * be able to simplify in the future.
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, MAX_CGRPS);
	__uint(key_size, sizeof(u64));		/* cgrp ID */
	__uint(value_size, sizeof(s32));	/* cgrp_q idx */
} cgrp_q_idx_hash SEC(".maps");

/* statistics */
u64 nr_total, nr_dispatched, nr_missing, nr_kicks, nr_preemptions;
u64 nr_exps, nr_exp_waits, nr_exp_empty;
u64 nr_cgrp_next, nr_cgrp_coll, nr_cgrp_empty;

UEI_DEFINE(uei);

void BPF_STRUCT_OPS(pair_enqueue, struct task_struct *p, u64 enq_flags)
{
	struct cgroup *cgrp;
	struct cgrp_q *cgq;
	s32 pid = p->pid;
	u64 cgid;
	u32 *q_idx;
	u64 *cgq_len;

	__sync_fetch_and_add(&nr_total, 1);

	cgrp = scx_bpf_task_cgroup(p);
	cgid = cgrp->kn->id;
	bpf_cgroup_release(cgrp);

	/* find the cgroup's q and push @p into it */
	q_idx = bpf_map_lookup_elem(&cgrp_q_idx_hash, &cgid);
	if (!q_idx) {
		scx_bpf_error("failed to lookup q_idx for cgroup[%llu]", cgid);
		return;
	}

	cgq = bpf_map_lookup_elem(&cgrp_q_arr, q_idx);
	if (!cgq) {
		scx_bpf_error("failed to lookup q_arr for cgroup[%llu] q_idx[%u]",
			      cgid, *q_idx);
		return;
	}

	if (bpf_map_push_elem(cgq, &pid, 0)) {
		scx_bpf_error("cgroup[%llu] queue overflow", cgid);
		return;
	}

	/* bump q len, if going 0 -> 1, queue cgroup into the top_q */
	cgq_len = MEMBER_VPTR(cgrp_q_len, [*q_idx]);
	if (!cgq_len) {
		scx_bpf_error("MEMBER_VTPR malfunction");
		return;
	}

	if (!__sync_fetch_and_add(cgq_len, 1) &&
	    bpf_map_push_elem(&top_q, &cgid, 0)) {
		scx_bpf_error("top_q overflow");
		return;
	}
}

static int lookup_pairc_and_mask(s32 cpu, struct pair_ctx **pairc, u32 *mask)
{
	u32 *vptr;

	vptr = (u32 *)ARRAY_ELEM_PTR(pair_id, cpu, nr_cpu_ids);
	if (!vptr)
		return -EINVAL;

	*pairc = bpf_map_lookup_elem(&pair_ctx, vptr);
	if (!(*pairc))
		return -EINVAL;

	vptr = (u32 *)ARRAY_ELEM_PTR(in_pair_idx, cpu, nr_cpu_ids);
	if (!vptr)
		return -EINVAL;

	*mask = 1U << *vptr;

	return 0;
}

__attribute__((noinline))
static int try_dispatch(s32 cpu)
{
	struct pair_ctx *pairc;
	struct bpf_map *cgq_map;
	struct task_struct *p;
	u64 now = scx_bpf_now();
	bool kick_pair = false;
	bool expired, pair_preempted;
	u32 *vptr, in_pair_mask;
	s32 pid, q_idx;
	u64 cgid;
	int ret;

	ret = lookup_pairc_and_mask(cpu, &pairc, &in_pair_mask);
	if (ret) {
		scx_bpf_error("failed to lookup pairc and in_pair_mask for cpu[%d]",
			      cpu);
		return -ENOENT;
	}

	bpf_spin_lock(&pairc->lock);
	pairc->active_mask &= ~in_pair_mask;

	expired = time_before(pairc->started_at + pair_batch_dur_ns, now);
	if (expired || pairc->draining) {
		u64 new_cgid = 0;

		__sync_fetch_and_add(&nr_exps, 1);

		/*
		 * We're done with the current cgid. An obvious optimization
		 * would be not draining if the next cgroup is the current one.
		 * For now, be dumb and always expire.
		 */
		pairc->draining = true;

		pair_preempted = pairc->preempted_mask;
		if (pairc->active_mask || pair_preempted) {
			/*
			 * The other CPU is still active, or is no longer under
			 * our control due to e.g. being preempted by a higher
			 * priority sched_class. We want to wait until this
			 * cgroup expires, or until control of our pair CPU has
			 * been returned to us.
			 *
			 * If the pair controls its CPU, and the time already
			 * expired, kick.  When the other CPU arrives at
			 * dispatch and clears its active mask, it'll push the
			 * pair to the next cgroup and kick this CPU.
			 */
			__sync_fetch_and_add(&nr_exp_waits, 1);
			bpf_spin_unlock(&pairc->lock);
			if (expired && !pair_preempted)
				kick_pair = true;
			goto out_maybe_kick;
		}

		bpf_spin_unlock(&pairc->lock);

		/*
		 * Pick the next cgroup. It'd be easier / cleaner to not drop
		 * pairc->lock and use stronger synchronization here especially
		 * given that we'll be switching cgroups significantly less
		 * frequently than tasks. Unfortunately, bpf_spin_lock can't
		 * really protect anything non-trivial. Let's do opportunistic
		 * operations instead.
		 */
		bpf_repeat(BPF_MAX_LOOPS) {
			u32 *q_idx;
			u64 *cgq_len;

			if (bpf_map_pop_elem(&top_q, &new_cgid)) {
				/* no active cgroup, go idle */
				__sync_fetch_and_add(&nr_exp_empty, 1);
				return 0;
			}

			q_idx = bpf_map_lookup_elem(&cgrp_q_idx_hash, &new_cgid);
			if (!q_idx)
				continue;

			/*
			 * This is the only place where empty cgroups are taken
			 * off the top_q.
			 */
			cgq_len = MEMBER_VPTR(cgrp_q_len, [*q_idx]);
			if (!cgq_len || !*cgq_len)
				continue;

			/*
			 * If it has any tasks, requeue as we may race and not
			 * execute it.
			 */
			bpf_map_push_elem(&top_q, &new_cgid, 0);
			break;
		}

		bpf_spin_lock(&pairc->lock);

		/*
		 * The other CPU may already have started on a new cgroup while
		 * we dropped the lock. Make sure that we're still draining and
		 * start on the new cgroup.
		 */
		if (pairc->draining && !pairc->active_mask) {
			__sync_fetch_and_add(&nr_cgrp_next, 1);
			pairc->cgid = new_cgid;
			pairc->started_at = now;
			pairc->draining = false;
			kick_pair = true;
		} else {
			__sync_fetch_and_add(&nr_cgrp_coll, 1);
		}
	}

	cgid = pairc->cgid;
	pairc->active_mask |= in_pair_mask;
	bpf_spin_unlock(&pairc->lock);

	/* again, it'd be better to do all these with the lock held, oh well */
	vptr = bpf_map_lookup_elem(&cgrp_q_idx_hash, &cgid);
	if (!vptr) {
		scx_bpf_error("failed to lookup q_idx for cgroup[%llu]", cgid);
		return -ENOENT;
	}
	q_idx = *vptr;

	/* claim one task from cgrp_q w/ q_idx */
	bpf_repeat(BPF_MAX_LOOPS) {
		u64 *cgq_len, len;

		cgq_len = MEMBER_VPTR(cgrp_q_len, [q_idx]);
		if (!cgq_len || !(len = *(volatile u64 *)cgq_len)) {
			/* the cgroup must be empty, expire and repeat */
			__sync_fetch_and_add(&nr_cgrp_empty, 1);
			bpf_spin_lock(&pairc->lock);
			pairc->draining = true;
			pairc->active_mask &= ~in_pair_mask;
			bpf_spin_unlock(&pairc->lock);
			return -EAGAIN;
		}

		if (__sync_val_compare_and_swap(cgq_len, len, len - 1) != len)
			continue;

		break;
	}

	cgq_map = bpf_map_lookup_elem(&cgrp_q_arr, &q_idx);
	if (!cgq_map) {
		scx_bpf_error("failed to lookup cgq_map for cgroup[%llu] q_idx[%d]",
			      cgid, q_idx);
		return -ENOENT;
	}

	if (bpf_map_pop_elem(cgq_map, &pid)) {
		scx_bpf_error("cgq_map is empty for cgroup[%llu] q_idx[%d]",
			      cgid, q_idx);
		return -ENOENT;
	}

	p = bpf_task_from_pid(pid);
	if (p) {
		__sync_fetch_and_add(&nr_dispatched, 1);
		scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, 0);
		bpf_task_release(p);
	} else {
		/* we don't handle dequeues, retry on lost tasks */
		__sync_fetch_and_add(&nr_missing, 1);
		return -EAGAIN;
	}

out_maybe_kick:
	if (kick_pair) {
		s32 *pair = (s32 *)ARRAY_ELEM_PTR(pair_cpu, cpu, nr_cpu_ids);
		if (pair) {
			__sync_fetch_and_add(&nr_kicks, 1);
			scx_bpf_kick_cpu(*pair, SCX_KICK_PREEMPT);
		}
	}
	return 0;
}

void BPF_STRUCT_OPS(pair_dispatch, s32 cpu, struct task_struct *prev)
{
	bpf_repeat(BPF_MAX_LOOPS) {
		if (try_dispatch(cpu) != -EAGAIN)
			break;
	}
}

void BPF_STRUCT_OPS(pair_cpu_acquire, s32 cpu, struct scx_cpu_acquire_args *args)
{
	int ret;
	u32 in_pair_mask;
	struct pair_ctx *pairc;
	bool kick_pair;

	ret = lookup_pairc_and_mask(cpu, &pairc, &in_pair_mask);
	if (ret)
		return;

	bpf_spin_lock(&pairc->lock);
	pairc->preempted_mask &= ~in_pair_mask;
	/* Kick the pair CPU, unless it was also preempted. */
	kick_pair = !pairc->preempted_mask;
	bpf_spin_unlock(&pairc->lock);

	if (kick_pair) {
		s32 *pair = (s32 *)ARRAY_ELEM_PTR(pair_cpu, cpu, nr_cpu_ids);

		if (pair) {
			__sync_fetch_and_add(&nr_kicks, 1);
			scx_bpf_kick_cpu(*pair, SCX_KICK_PREEMPT);
		}
	}
}

void BPF_STRUCT_OPS(pair_cpu_release, s32 cpu, struct scx_cpu_release_args *args)
{
	int ret;
	u32 in_pair_mask;
	struct pair_ctx *pairc;
	bool kick_pair;

	ret = lookup_pairc_and_mask(cpu, &pairc, &in_pair_mask);
	if (ret)
		return;

	bpf_spin_lock(&pairc->lock);
	pairc->preempted_mask |= in_pair_mask;
	pairc->active_mask &= ~in_pair_mask;
	/* Kick the pair CPU if it's still running. */
	kick_pair = pairc->active_mask;
	pairc->draining = true;
	bpf_spin_unlock(&pairc->lock);

	if (kick_pair) {
		s32 *pair = (s32 *)ARRAY_ELEM_PTR(pair_cpu, cpu, nr_cpu_ids);

		if (pair) {
			__sync_fetch_and_add(&nr_kicks, 1);
			scx_bpf_kick_cpu(*pair, SCX_KICK_PREEMPT | SCX_KICK_WAIT);
		}
	}
	__sync_fetch_and_add(&nr_preemptions, 1);
}

s32 BPF_STRUCT_OPS(pair_cgroup_init, struct cgroup *cgrp)
{
	u64 cgid = cgrp->kn->id;
	s32 i, q_idx;

	bpf_for(i, 0, MAX_CGRPS) {
		q_idx = __sync_fetch_and_add(&cgrp_q_idx_cursor, 1) % MAX_CGRPS;
		if (!__sync_val_compare_and_swap(&cgrp_q_idx_busy[q_idx], 0, 1))
			break;
	}
	if (i == MAX_CGRPS)
		return -EBUSY;

	if (bpf_map_update_elem(&cgrp_q_idx_hash, &cgid, &q_idx, BPF_ANY)) {
		u64 *busy = MEMBER_VPTR(cgrp_q_idx_busy, [q_idx]);
		if (busy)
			*busy = 0;
		return -EBUSY;
	}

	return 0;
}

void BPF_STRUCT_OPS(pair_cgroup_exit, struct cgroup *cgrp)
{
	u64 cgid = cgrp->kn->id;
	s32 *q_idx;

	q_idx = bpf_map_lookup_elem(&cgrp_q_idx_hash, &cgid);
	if (q_idx) {
		u64 *busy = MEMBER_VPTR(cgrp_q_idx_busy, [*q_idx]);
		if (busy)
			*busy = 0;
		bpf_map_delete_elem(&cgrp_q_idx_hash, &cgid);
	}
}

void BPF_STRUCT_OPS(pair_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(pair_ops,
	       .enqueue			= (void *)pair_enqueue,
	       .dispatch		= (void *)pair_dispatch,
	       .cpu_acquire		= (void *)pair_cpu_acquire,
	       .cpu_release		= (void *)pair_cpu_release,
	       .cgroup_init		= (void *)pair_cgroup_init,
	       .cgroup_exit		= (void *)pair_cgroup_exit,
	       .exit			= (void *)pair_exit,
	       .name			= "pair");
