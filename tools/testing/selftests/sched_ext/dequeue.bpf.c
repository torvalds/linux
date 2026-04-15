// SPDX-License-Identifier: GPL-2.0
/*
 * A scheduler that validates ops.dequeue() is called correctly:
 * - Tasks dispatched to terminal DSQs (local, global) bypass the BPF
 *   scheduler entirely: no ops.dequeue() should be called
 * - Tasks dispatched to user DSQs from ops.enqueue() enter BPF custody:
 *   ops.dequeue() must be called when they leave custody
 * - Every ops.enqueue() dispatch to non-terminal DSQs is followed by
 *   exactly one ops.dequeue() (validate 1:1 pairing and state machine)
 *
 * Copyright (c) 2026 NVIDIA Corporation.
 */

#include <scx/common.bpf.h>

#define SHARED_DSQ	0

/*
 * BPF internal queue.
 *
 * Tasks are stored here and consumed from ops.dispatch(), validating that
 * tasks on BPF internal structures still get ops.dequeue() when they
 * leave.
 */
struct {
	__uint(type, BPF_MAP_TYPE_QUEUE);
	__uint(max_entries, 32768);
	__type(value, s32);
} global_queue SEC(".maps");

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

/*
 * Counters to track the lifecycle of tasks:
 * - enqueue_cnt: Number of times ops.enqueue() was called
 * - dequeue_cnt: Number of times ops.dequeue() was called (any type)
 * - dispatch_dequeue_cnt: Number of regular dispatch dequeues (no flag)
 * - change_dequeue_cnt: Number of property change dequeues
 * - bpf_queue_full: Number of times the BPF internal queue was full
 */
u64 enqueue_cnt, dequeue_cnt, dispatch_dequeue_cnt, change_dequeue_cnt, bpf_queue_full;

/*
 * Test scenarios:
 * 0) Dispatch to local DSQ from ops.select_cpu() (terminal DSQ, bypasses BPF
 *    scheduler, no dequeue callbacks)
 * 1) Dispatch to global DSQ from ops.select_cpu() (terminal DSQ, bypasses BPF
 *    scheduler, no dequeue callbacks)
 * 2) Dispatch to shared user DSQ from ops.select_cpu() (enters BPF scheduler,
 *    dequeue callbacks expected)
 * 3) Dispatch to local DSQ from ops.enqueue() (terminal DSQ, bypasses BPF
 *    scheduler, no dequeue callbacks)
 * 4) Dispatch to global DSQ from ops.enqueue() (terminal DSQ, bypasses BPF
 *    scheduler, no dequeue callbacks)
 * 5) Dispatch to shared user DSQ from ops.enqueue() (enters BPF scheduler,
 *    dequeue callbacks expected)
 * 6) BPF internal queue from ops.enqueue(): store task PIDs in ops.enqueue(),
 *    consume in ops.dispatch() and dispatch to local DSQ (validates dequeue
 *    for tasks stored in internal BPF data structures)
 */
u32 test_scenario;

/*
 * Per-task state to track lifecycle and validate workflow semantics.
 * State transitions:
 *   NONE -> ENQUEUED (on enqueue)
 *   NONE -> DISPATCHED (on direct dispatch to terminal DSQ)
 *   ENQUEUED -> DISPATCHED (on dispatch dequeue)
 *   DISPATCHED -> NONE (on property change dequeue or re-enqueue)
 *   ENQUEUED -> NONE (on property change dequeue before dispatch)
 */
enum task_state {
	TASK_NONE = 0,
	TASK_ENQUEUED,
	TASK_DISPATCHED,
};

struct task_ctx {
	enum task_state state; /* Current state in the workflow */
	u64 enqueue_seq;       /* Sequence number for debugging */
};

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct task_ctx);
} task_ctx_stor SEC(".maps");

static struct task_ctx *try_lookup_task_ctx(struct task_struct *p)
{
	return bpf_task_storage_get(&task_ctx_stor, p, 0, 0);
}

s32 BPF_STRUCT_OPS(dequeue_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	struct task_ctx *tctx;

	tctx = try_lookup_task_ctx(p);
	if (!tctx)
		return prev_cpu;

	switch (test_scenario) {
	case 0:
		/*
		 * Direct dispatch to the local DSQ.
		 *
		 * Task bypasses BPF scheduler entirely: no enqueue
		 * tracking, no ops.dequeue() callbacks.
		 */
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);
		tctx->state = TASK_DISPATCHED;
		break;
	case 1:
		/*
		 * Direct dispatch to the global DSQ.
		 *
		 * Task bypasses BPF scheduler entirely: no enqueue
		 * tracking, no ops.dequeue() callbacks.
		 */
		scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, 0);
		tctx->state = TASK_DISPATCHED;
		break;
	case 2:
		/*
		 * Dispatch to a shared user DSQ.
		 *
		 * Task enters BPF scheduler management: track
		 * enqueue/dequeue lifecycle and validate state
		 * transitions.
		 */
		if (tctx->state == TASK_ENQUEUED)
			scx_bpf_error("%d (%s): enqueue while in ENQUEUED state seq=%llu",
				      p->pid, p->comm, tctx->enqueue_seq);

		scx_bpf_dsq_insert(p, SHARED_DSQ, SCX_SLICE_DFL, 0);

		__sync_fetch_and_add(&enqueue_cnt, 1);

		tctx->state = TASK_ENQUEUED;
		tctx->enqueue_seq++;
		break;
	}

	return prev_cpu;
}

void BPF_STRUCT_OPS(dequeue_enqueue, struct task_struct *p, u64 enq_flags)
{
	struct task_ctx *tctx;
	s32 pid = p->pid;

	tctx = try_lookup_task_ctx(p);
	if (!tctx)
		return;

	switch (test_scenario) {
	case 3:
		/*
		 * Direct dispatch to the local DSQ.
		 *
		 * Task bypasses BPF scheduler entirely: no enqueue
		 * tracking, no ops.dequeue() callbacks.
		 */
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, enq_flags);
		tctx->state = TASK_DISPATCHED;
		break;
	case 4:
		/*
		 * Direct dispatch to the global DSQ.
		 *
		 * Task bypasses BPF scheduler entirely: no enqueue
		 * tracking, no ops.dequeue() callbacks.
		 */
		scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
		tctx->state = TASK_DISPATCHED;
		break;
	case 5:
		/*
		 * Dispatch to shared user DSQ.
		 *
		 * Task enters BPF scheduler management: track
		 * enqueue/dequeue lifecycle and validate state
		 * transitions.
		 */
		if (tctx->state == TASK_ENQUEUED)
			scx_bpf_error("%d (%s): enqueue while in ENQUEUED state seq=%llu",
				      p->pid, p->comm, tctx->enqueue_seq);

		scx_bpf_dsq_insert(p, SHARED_DSQ, SCX_SLICE_DFL, enq_flags);

		__sync_fetch_and_add(&enqueue_cnt, 1);

		tctx->state = TASK_ENQUEUED;
		tctx->enqueue_seq++;
		break;
	case 6:
		/*
		 * Store task in BPF internal queue.
		 *
		 * Task enters BPF scheduler management: track
		 * enqueue/dequeue lifecycle and validate state
		 * transitions.
		 */
		if (tctx->state == TASK_ENQUEUED)
			scx_bpf_error("%d (%s): enqueue while in ENQUEUED state seq=%llu",
				      p->pid, p->comm, tctx->enqueue_seq);

		if (bpf_map_push_elem(&global_queue, &pid, 0)) {
			scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
			__sync_fetch_and_add(&bpf_queue_full, 1);

			tctx->state = TASK_DISPATCHED;
		} else {
			__sync_fetch_and_add(&enqueue_cnt, 1);

			tctx->state = TASK_ENQUEUED;
			tctx->enqueue_seq++;
		}
		break;
	default:
		/* For all other scenarios, dispatch to the global DSQ */
		scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
		tctx->state = TASK_DISPATCHED;
		break;
	}

	scx_bpf_kick_cpu(scx_bpf_task_cpu(p), SCX_KICK_IDLE);
}

void BPF_STRUCT_OPS(dequeue_dequeue, struct task_struct *p, u64 deq_flags)
{
	struct task_ctx *tctx;

	__sync_fetch_and_add(&dequeue_cnt, 1);

	tctx = try_lookup_task_ctx(p);
	if (!tctx)
		return;

	/*
	 * For scenarios 0, 1, 3, and 4 (terminal DSQs: local and global),
	 * ops.dequeue() should never be called because tasks bypass the
	 * BPF scheduler entirely. If we get here, it's a kernel bug.
	 */
	if (test_scenario == 0 || test_scenario == 3) {
		scx_bpf_error("%d (%s): dequeue called for local DSQ scenario",
			      p->pid, p->comm);
		return;
	}

	if (test_scenario == 1 || test_scenario == 4) {
		scx_bpf_error("%d (%s): dequeue called for global DSQ scenario",
			      p->pid, p->comm);
		return;
	}

	if (deq_flags & SCX_DEQ_SCHED_CHANGE) {
		/*
		 * Property change interrupting the workflow. Valid from
		 * both ENQUEUED and DISPATCHED states. Transitions task
		 * back to NONE state.
		 */
		__sync_fetch_and_add(&change_dequeue_cnt, 1);

		/* Validate state transition */
		if (tctx->state != TASK_ENQUEUED && tctx->state != TASK_DISPATCHED)
			scx_bpf_error("%d (%s): invalid property change dequeue state=%d seq=%llu",
				      p->pid, p->comm, tctx->state, tctx->enqueue_seq);

		/*
		 * Transition back to NONE: task outside scheduler control.
		 *
		 * Scenario 6: dispatch() checks tctx->state after popping a
		 * PID, if the task is in state NONE, it was dequeued by
		 * property change and must not be dispatched (this
		 * prevents "target CPU not allowed").
		 */
		tctx->state = TASK_NONE;
	} else {
		/*
		 * Regular dispatch dequeue: kernel is moving the task from
		 * BPF custody to a terminal DSQ. Normally we come from
		 * ENQUEUED state. We can also see TASK_NONE if the task
		 * was dequeued by property change (SCX_DEQ_SCHED_CHANGE)
		 * while it was already on a DSQ (dispatched but not yet
		 * consumed); in that case we just leave state as NONE.
		 */
		__sync_fetch_and_add(&dispatch_dequeue_cnt, 1);

		/*
		 * Must be ENQUEUED (normal path) or NONE (already dequeued
		 * by property change while on a DSQ).
		 */
		if (tctx->state != TASK_ENQUEUED && tctx->state != TASK_NONE)
			scx_bpf_error("%d (%s): dispatch dequeue from state %d seq=%llu",
				      p->pid, p->comm, tctx->state, tctx->enqueue_seq);

		if (tctx->state == TASK_ENQUEUED)
			tctx->state = TASK_DISPATCHED;

		/* NONE: leave as-is, task was already property-change dequeued */
	}
}

void BPF_STRUCT_OPS(dequeue_dispatch, s32 cpu, struct task_struct *prev)
{
	if (test_scenario == 6) {
		struct task_ctx *tctx;
		struct task_struct *p;
		s32 pid;

		if (bpf_map_pop_elem(&global_queue, &pid))
			return;

		p = bpf_task_from_pid(pid);
		if (!p)
			return;

		/*
		 * If the task was dequeued by property change
		 * (ops.dequeue() set tctx->state = TASK_NONE), skip
		 * dispatch.
		 */
		tctx = try_lookup_task_ctx(p);
		if (!tctx || tctx->state == TASK_NONE) {
			bpf_task_release(p);
			return;
		}

		/*
		 * Dispatch to this CPU's local DSQ if allowed, otherwise
		 * fallback to the global DSQ.
		 */
		if (bpf_cpumask_test_cpu(cpu, p->cpus_ptr))
			scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL_ON | cpu, SCX_SLICE_DFL, 0);
		else
			scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, 0);

		bpf_task_release(p);
	} else {
		scx_bpf_dsq_move_to_local(SHARED_DSQ, 0);
	}
}

s32 BPF_STRUCT_OPS(dequeue_init_task, struct task_struct *p,
		   struct scx_init_task_args *args)
{
	struct task_ctx *tctx;

	tctx = bpf_task_storage_get(&task_ctx_stor, p, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!tctx)
		return -ENOMEM;

	return 0;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(dequeue_init)
{
	s32 ret;

	ret = scx_bpf_create_dsq(SHARED_DSQ, -1);
	if (ret)
		return ret;

	return 0;
}

void BPF_STRUCT_OPS(dequeue_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops dequeue_ops = {
	.select_cpu		= (void *)dequeue_select_cpu,
	.enqueue		= (void *)dequeue_enqueue,
	.dequeue		= (void *)dequeue_dequeue,
	.dispatch		= (void *)dequeue_dispatch,
	.init_task		= (void *)dequeue_init_task,
	.init			= (void *)dequeue_init,
	.exit			= (void *)dequeue_exit,
	.flags			= SCX_OPS_ENQ_LAST,
	.name			= "dequeue_test",
};
