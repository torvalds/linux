// SPDX-License-Identifier: GPL-2.0
/*
 * Verify that ops.dequeue() is called when a task leaves the BPF scheduler's
 * custody by being moved to the local DSQ of a CPU other than the one whose
 * rq it's on (move_remote_task_to_local_dsq()).
 *
 * ops.enqueue() puts every task into custody and ops.dispatch() moves it to
 * the dispatching CPU's local DSQ, so most moves cross CPUs. With
 * @test_use_move_to_local, tasks are queued on a user DSQ and consumed with
 * scx_bpf_dsq_move_to_local(). Otherwise, they are queued in a BPF queue and
 * dispatched with SCX_DSQ_LOCAL_ON.
 *
 * Copyright (c) 2026 Google LLC.
 */

#include <scx/common.bpf.h>

#define SHARED_DSQ		0
#define MAX_DISPATCH_POPS	8

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

struct {
	__uint(type, BPF_MAP_TYPE_QUEUE);
	__uint(max_entries, 32768);
	__type(value, s32);
} global_queue SEC(".maps");

enum task_state {
	TASK_NONE = 0,
	TASK_ENQUEUED,		/* in BPF custody, waiting for ops.dequeue() */
	TASK_DISPATCHED,	/* left custody */
};

struct task_ctx {
	enum task_state state;
	s32 enq_cpu;		/* scx_bpf_task_cpu() at ops.enqueue() */
	u64 enqueue_seq;
};

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct task_ctx);
} task_ctx_stor SEC(".maps");

/* core_cookie only exists with CONFIG_SCHED_CORE */
struct task_struct___core_sched {
	unsigned long core_cookie;
} __attribute__((preserve_access_index));

bool test_use_move_to_local;

u64 enqueue_cnt, dequeue_cnt, dispatch_dequeue_cnt, change_dequeue_cnt;
u64 remote_dispatch_cnt, remote_running_cnt, missed_dequeue_cnt;
u64 core_sched_exec_dequeue_cnt;

static struct task_ctx *lookup_task_ctx(struct task_struct *p)
{
	return bpf_task_storage_get(&task_ctx_stor, p, 0, 0);
}

static bool task_has_core_cookie(struct task_struct *p)
{
	struct task_struct___core_sched *t = (void *)p;

	if (!bpf_core_field_exists(t->core_cookie))
		return false;
	return BPF_CORE_READ(t, core_cookie);
}

s32 BPF_STRUCT_OPS(dequeue_remote_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	/* no direct dispatch, always go through ops.enqueue() */
	return prev_cpu;
}

void BPF_STRUCT_OPS(dequeue_remote_enqueue, struct task_struct *p, u64 enq_flags)
{
	struct task_ctx *tctx;
	s32 pid = p->pid;

	tctx = lookup_task_ctx(p);
	if (!tctx) {
		scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
		return;
	}

	/* the previous custody period must have ended with ops.dequeue() */
	if (tctx->state == TASK_ENQUEUED)
		scx_bpf_error("%d (%s): enqueue while in ENQUEUED state seq=%llu",
			      p->pid, p->comm, tctx->enqueue_seq);

	/*
	 * Mark @p as enqueued before making it visible to ops.dispatch() on
	 * other CPUs, which skips queue entries of tasks not in ENQUEUED
	 * state as stale.
	 */
	tctx->state = TASK_ENQUEUED;
	tctx->enq_cpu = scx_bpf_task_cpu(p);
	tctx->enqueue_seq++;

	if (test_use_move_to_local) {
		scx_bpf_dsq_insert(p, SHARED_DSQ, SCX_SLICE_DFL, enq_flags);
	} else if (bpf_map_push_elem(&global_queue, &pid, 0)) {
		scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
		tctx->state = TASK_DISPATCHED;
		tctx->enq_cpu = -1;
		goto out;
	}

	__sync_fetch_and_add(&enqueue_cnt, 1);
out:
	scx_bpf_kick_cpu(scx_bpf_task_cpu(p), SCX_KICK_IDLE);
}

void BPF_STRUCT_OPS(dequeue_remote_dequeue, struct task_struct *p, u64 deq_flags)
{
	struct task_ctx *tctx;

	__sync_fetch_and_add(&dequeue_cnt, 1);

	tctx = lookup_task_ctx(p);
	if (!tctx)
		return;

	/*
	 * Only core scheduling can pick a task straight out of custody, and
	 * only if the task has a core cookie. Otherwise, the custody exit was
	 * missed when @p was inserted into a local DSQ and got deferred until
	 * @p was picked.
	 */
	if ((deq_flags & SCX_DEQ_CORE_SCHED_EXEC) && !task_has_core_cookie(p)) {
		__sync_fetch_and_add(&core_sched_exec_dequeue_cnt, 1);
		scx_bpf_error("%d (%s): late ops.dequeue() with SCX_DEQ_CORE_SCHED_EXEC (enq_cpu=%d cpu=%d seq=%llu)",
			      p->pid, p->comm, tctx->enq_cpu,
			      scx_bpf_task_cpu(p), tctx->enqueue_seq);
	}

	/* ops.dequeue() ends the custody period started by ops.enqueue() */
	if (tctx->state != TASK_ENQUEUED)
		scx_bpf_error("%d (%s): dequeue outside custody deq_flags=0x%llx state=%d seq=%llu",
			      p->pid, p->comm, deq_flags, tctx->state,
			      tctx->enqueue_seq);

	if (deq_flags & SCX_DEQ_SCHED_CHANGE) {
		__sync_fetch_and_add(&change_dequeue_cnt, 1);
		tctx->state = TASK_NONE;
	} else {
		__sync_fetch_and_add(&dispatch_dequeue_cnt, 1);
		tctx->state = TASK_DISPATCHED;
	}
}

void BPF_STRUCT_OPS(dequeue_remote_dispatch, s32 cpu, struct task_struct *prev)
{
	struct task_ctx *tctx;
	struct task_struct *p;
	s32 pid;
	int i;

	if (test_use_move_to_local) {
		scx_bpf_dsq_move_to_local(SHARED_DSQ, 0);
		return;
	}

	/* pop past stale entries so that they don't leave this CPU idle */
	bpf_for(i, 0, MAX_DISPATCH_POPS) {
		if (bpf_map_pop_elem(&global_queue, &pid))
			return;

		p = bpf_task_from_pid(pid);
		if (!p)
			continue;

		/*
		 * Entries are stale if @p left custody through a property
		 * change dequeue or was dispatched from a duplicate entry.
		 */
		tctx = lookup_task_ctx(p);
		if (!tctx || tctx->state != TASK_ENQUEUED) {
			bpf_task_release(p);
			continue;
		}

		if (bpf_cpumask_test_cpu(cpu, p->cpus_ptr)) {
			if (scx_bpf_task_cpu(p) != cpu)
				__sync_fetch_and_add(&remote_dispatch_cnt, 1);
			scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL_ON | cpu,
					   SCX_SLICE_DFL, 0);
		} else {
			scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, 0);
		}

		bpf_task_release(p);
		return;
	}

	/* out of pops with entries left, retry instead of idling this CPU */
	if (!bpf_map_peek_elem(&global_queue, &pid))
		scx_bpf_kick_cpu(cpu, SCX_KICK_IDLE);
}

void BPF_STRUCT_OPS(dequeue_remote_running, struct task_struct *p)
{
	struct task_ctx *tctx;

	tctx = lookup_task_ctx(p);
	if (!tctx)
		return;

	/* tasks can only run from a local DSQ, i.e. after leaving custody */
	if (tctx->state == TASK_ENQUEUED) {
		__sync_fetch_and_add(&missed_dequeue_cnt, 1);
		scx_bpf_error("%d (%s): running without ops.dequeue() (enq_cpu=%d cpu=%d seq=%llu)",
			      p->pid, p->comm, tctx->enq_cpu,
			      scx_bpf_task_cpu(p), tctx->enqueue_seq);
		return;
	}

	if (tctx->enq_cpu >= 0 && tctx->enq_cpu != scx_bpf_task_cpu(p))
		__sync_fetch_and_add(&remote_running_cnt, 1);
	tctx->enq_cpu = -1;
}

s32 BPF_STRUCT_OPS(dequeue_remote_init_task, struct task_struct *p,
		   struct scx_init_task_args *args)
{
	struct task_ctx *tctx;

	tctx = bpf_task_storage_get(&task_ctx_stor, p, 0,
				    BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!tctx)
		return -ENOMEM;

	/* task storage persists across attachments, start from scratch */
	tctx->state = TASK_NONE;
	tctx->enq_cpu = -1;
	tctx->enqueue_seq = 0;

	return 0;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(dequeue_remote_init)
{
	return scx_bpf_create_dsq(SHARED_DSQ, -1);
}

void BPF_STRUCT_OPS(dequeue_remote_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops dequeue_remote_ops = {
	.select_cpu		= (void *)dequeue_remote_select_cpu,
	.enqueue		= (void *)dequeue_remote_enqueue,
	.dequeue		= (void *)dequeue_remote_dequeue,
	.dispatch		= (void *)dequeue_remote_dispatch,
	.running		= (void *)dequeue_remote_running,
	.init_task		= (void *)dequeue_remote_init_task,
	.init			= (void *)dequeue_remote_init,
	.exit			= (void *)dequeue_remote_exit,
	.flags			= SCX_OPS_ENQ_LAST,
	.name			= "dequeue_remote",
};
