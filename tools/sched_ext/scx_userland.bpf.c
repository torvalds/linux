/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A minimal userland scheduler.
 *
 * In terms of scheduling, this provides two different types of behaviors:
 * 1. A global FIFO scheduling order for _any_ tasks that have CPU affinity.
 *    All such tasks are direct-dispatched from the kernel, and are never
 *    enqueued in user space.
 * 2. A primitive vruntime scheduler that is implemented in user space, for all
 *    other tasks.
 *
 * Some parts of this example user space scheduler could be implemented more
 * efficiently using more complex and sophisticated data structures. For
 * example, rather than using BPF_MAP_TYPE_QUEUE's,
 * BPF_MAP_TYPE_{USER_}RINGBUF's could be used for exchanging messages between
 * user space and kernel space. Similarly, we use a simple vruntime-sorted list
 * in user space, but an rbtree could be used instead.
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#include <scx/common.bpf.h>
#include "scx_userland.h"

/*
 * Maximum amount of tasks enqueued/dispatched between kernel and user-space.
 */
#define MAX_ENQUEUED_TASKS 4096

char _license[] SEC("license") = "GPL";

const volatile s32 usersched_pid;

/* !0 for veristat, set during init */
const volatile u32 num_possible_cpus = 64;

/* Stats that are printed by user space. */
u64 nr_failed_enqueues, nr_kernel_enqueues, nr_user_enqueues;

/*
 * Number of tasks that are queued for scheduling.
 *
 * This number is incremented by the BPF component when a task is queued to the
 * user-space scheduler and it must be decremented by the user-space scheduler
 * when a task is consumed.
 */
volatile u64 nr_queued;

/*
 * Number of tasks that are waiting for scheduling.
 *
 * This number must be updated by the user-space scheduler to keep track if
 * there is still some scheduling work to do.
 */
volatile u64 nr_scheduled;

UEI_DEFINE(uei);

/*
 * The map containing tasks that are enqueued in user space from the kernel.
 *
 * This map is drained by the user space scheduler.
 */
struct {
	__uint(type, BPF_MAP_TYPE_QUEUE);
	__uint(max_entries, MAX_ENQUEUED_TASKS);
	__type(value, struct scx_userland_enqueued_task);
} enqueued SEC(".maps");

/*
 * The map containing tasks that are dispatched to the kernel from user space.
 *
 * Drained by the kernel in userland_dispatch().
 */
struct {
	__uint(type, BPF_MAP_TYPE_QUEUE);
	__uint(max_entries, MAX_ENQUEUED_TASKS);
	__type(value, s32);
} dispatched SEC(".maps");

/* Per-task scheduling context */
struct task_ctx {
	bool force_local; /* Dispatch directly to local DSQ */
};

/* Map that contains task-local storage. */
struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct task_ctx);
} task_ctx_stor SEC(".maps");

/*
 * Flag used to wake-up the user-space scheduler.
 */
static volatile u32 usersched_needed;

/*
 * Set user-space scheduler wake-up flag (equivalent to an atomic release
 * operation).
 */
static void set_usersched_needed(void)
{
	__sync_fetch_and_or(&usersched_needed, 1);
}

/*
 * Check and clear user-space scheduler wake-up flag (equivalent to an atomic
 * acquire operation).
 */
static bool test_and_clear_usersched_needed(void)
{
	return __sync_fetch_and_and(&usersched_needed, 0) == 1;
}

static bool is_usersched_task(const struct task_struct *p)
{
	return p->pid == usersched_pid;
}

static bool keep_in_kernel(const struct task_struct *p)
{
	return p->nr_cpus_allowed < num_possible_cpus;
}

static struct task_struct *usersched_task(void)
{
	struct task_struct *p;

	p = bpf_task_from_pid(usersched_pid);
	/*
	 * Should never happen -- the usersched task should always be managed
	 * by sched_ext.
	 */
	if (!p)
		scx_bpf_error("Failed to find usersched task %d", usersched_pid);

	return p;
}

s32 BPF_STRUCT_OPS(userland_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	if (keep_in_kernel(p)) {
		s32 cpu;
		struct task_ctx *tctx;

		tctx = bpf_task_storage_get(&task_ctx_stor, p, 0, 0);
		if (!tctx) {
			scx_bpf_error("Failed to look up task-local storage for %s", p->comm);
			return -ESRCH;
		}

		if (p->nr_cpus_allowed == 1 ||
		    scx_bpf_test_and_clear_cpu_idle(prev_cpu)) {
			tctx->force_local = true;
			return prev_cpu;
		}

		cpu = scx_bpf_pick_idle_cpu(p->cpus_ptr, 0);
		if (cpu >= 0) {
			tctx->force_local = true;
			return cpu;
		}
	}

	return prev_cpu;
}

static void dispatch_user_scheduler(void)
{
	struct task_struct *p;

	p = usersched_task();
	if (p) {
		scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, 0);
		bpf_task_release(p);
	}
}

static void enqueue_task_in_user_space(struct task_struct *p, u64 enq_flags)
{
	struct scx_userland_enqueued_task task = {};

	task.pid = p->pid;
	task.sum_exec_runtime = p->se.sum_exec_runtime;
	task.weight = p->scx.weight;

	if (bpf_map_push_elem(&enqueued, &task, 0)) {
		/*
		 * If we fail to enqueue the task in user space, put it
		 * directly on the global DSQ.
		 */
		__sync_fetch_and_add(&nr_failed_enqueues, 1);
		scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
	} else {
		__sync_fetch_and_add(&nr_user_enqueues, 1);
		set_usersched_needed();
	}
}

void BPF_STRUCT_OPS(userland_enqueue, struct task_struct *p, u64 enq_flags)
{
	if (keep_in_kernel(p)) {
		u64 dsq_id = SCX_DSQ_GLOBAL;
		struct task_ctx *tctx;

		tctx = bpf_task_storage_get(&task_ctx_stor, p, 0, 0);
		if (!tctx) {
			scx_bpf_error("Failed to lookup task ctx for %s", p->comm);
			return;
		}

		if (tctx->force_local)
			dsq_id = SCX_DSQ_LOCAL;
		tctx->force_local = false;
		scx_bpf_dsq_insert(p, dsq_id, SCX_SLICE_DFL, enq_flags);
		__sync_fetch_and_add(&nr_kernel_enqueues, 1);
		return;
	} else if (!is_usersched_task(p)) {
		enqueue_task_in_user_space(p, enq_flags);
	}
}

void BPF_STRUCT_OPS(userland_dispatch, s32 cpu, struct task_struct *prev)
{
	if (test_and_clear_usersched_needed())
		dispatch_user_scheduler();

	bpf_repeat(MAX_ENQUEUED_TASKS) {
		s32 pid;
		struct task_struct *p;

		if (bpf_map_pop_elem(&dispatched, &pid))
			break;

		/*
		 * The task could have exited by the time we get around to
		 * dispatching it. Treat this as a normal occurrence, and simply
		 * move onto the next iteration.
		 */
		p = bpf_task_from_pid(pid);
		if (!p)
			continue;

		scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, 0);
		bpf_task_release(p);
	}
}

/*
 * A CPU is about to change its idle state. If the CPU is going idle, ensure
 * that the user-space scheduler has a chance to run if there is any remaining
 * work to do.
 */
void BPF_STRUCT_OPS(userland_update_idle, s32 cpu, bool idle)
{
	/*
	 * Don't do anything if we exit from and idle state, a CPU owner will
	 * be assigned in .running().
	 */
	if (!idle)
		return;
	/*
	 * A CPU is now available, notify the user-space scheduler that tasks
	 * can be dispatched, if there is at least one task waiting to be
	 * scheduled, either queued (accounted in nr_queued) or scheduled
	 * (accounted in nr_scheduled).
	 *
	 * NOTE: nr_queued is incremented by the BPF component, more exactly in
	 * enqueue(), when a task is sent to the user-space scheduler, then
	 * the scheduler drains the queued tasks (updating nr_queued) and adds
	 * them to its internal data structures / state; at this point tasks
	 * become "scheduled" and the user-space scheduler will take care of
	 * updating nr_scheduled accordingly; lastly tasks will be dispatched
	 * and the user-space scheduler will update nr_scheduled again.
	 *
	 * Checking both counters allows to determine if there is still some
	 * pending work to do for the scheduler: new tasks have been queued
	 * since last check, or there are still tasks "queued" or "scheduled"
	 * since the previous user-space scheduler run. If the counters are
	 * both zero it is pointless to wake-up the scheduler (even if a CPU
	 * becomes idle), because there is nothing to do.
	 *
	 * Keep in mind that update_idle() doesn't run concurrently with the
	 * user-space scheduler (that is single-threaded): this function is
	 * naturally serialized with the user-space scheduler code, therefore
	 * this check here is also safe from a concurrency perspective.
	 */
	if (nr_queued || nr_scheduled) {
		/*
		 * Kick the CPU to make it immediately ready to accept
		 * dispatched tasks.
		 */
		set_usersched_needed();
		scx_bpf_kick_cpu(cpu, 0);
	}
}

s32 BPF_STRUCT_OPS(userland_init_task, struct task_struct *p,
		   struct scx_init_task_args *args)
{
	if (bpf_task_storage_get(&task_ctx_stor, p, 0,
				 BPF_LOCAL_STORAGE_GET_F_CREATE))
		return 0;
	else
		return -ENOMEM;
}

s32 BPF_STRUCT_OPS(userland_init)
{
	if (num_possible_cpus == 0) {
		scx_bpf_error("User scheduler # CPUs uninitialized (%d)",
			      num_possible_cpus);
		return -EINVAL;
	}

	if (usersched_pid <= 0) {
		scx_bpf_error("User scheduler pid uninitialized (%d)",
			      usersched_pid);
		return -EINVAL;
	}

	return 0;
}

void BPF_STRUCT_OPS(userland_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(userland_ops,
	       .select_cpu		= (void *)userland_select_cpu,
	       .enqueue			= (void *)userland_enqueue,
	       .dispatch		= (void *)userland_dispatch,
	       .update_idle		= (void *)userland_update_idle,
	       .init_task		= (void *)userland_init_task,
	       .init			= (void *)userland_init,
	       .exit			= (void *)userland_exit,
	       .flags			= SCX_OPS_ENQ_LAST |
					  SCX_OPS_KEEP_BUILTIN_IDLE,
	       .name			= "userland");
