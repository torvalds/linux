/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A simple five-level FIFO queue scheduler.
 *
 * There are five FIFOs implemented using BPF_MAP_TYPE_QUEUE. A task gets
 * assigned to one depending on its compound weight. Each CPU round robins
 * through the FIFOs and dispatches more from FIFOs with higher indices - 1 from
 * queue0, 2 from queue1, 4 from queue2 and so on.
 *
 * This scheduler demonstrates:
 *
 * - BPF-side queueing using PIDs.
 * - Sleepable per-task storage allocation using ops.prep_enable().
 * - Using ops.cpu_release() to handle a higher priority scheduling class taking
 *   the CPU away.
 * - Core-sched support.
 *
 * This scheduler is primarily for demonstration and testing of sched_ext
 * features and unlikely to be useful for actual workloads.
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#include <scx/common.bpf.h>

enum consts {
	ONE_SEC_IN_NS		= 1000000000,
	SHARED_DSQ		= 0,
};

char _license[] SEC("license") = "GPL";

const volatile u64 slice_ns = SCX_SLICE_DFL;
const volatile u32 stall_user_nth;
const volatile u32 stall_kernel_nth;
const volatile u32 dsp_inf_loop_after;
const volatile u32 dsp_batch;
const volatile s32 disallow_tgid;
const volatile bool suppress_dump;

u32 test_error_cnt;

UEI_DEFINE(uei);

struct qmap {
	__uint(type, BPF_MAP_TYPE_QUEUE);
	__uint(max_entries, 4096);
	__type(value, u32);
} queue0 SEC(".maps"),
  queue1 SEC(".maps"),
  queue2 SEC(".maps"),
  queue3 SEC(".maps"),
  queue4 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 5);
	__type(key, int);
	__array(values, struct qmap);
} queue_arr SEC(".maps") = {
	.values = {
		[0] = &queue0,
		[1] = &queue1,
		[2] = &queue2,
		[3] = &queue3,
		[4] = &queue4,
	},
};

/*
 * Per-queue sequence numbers to implement core-sched ordering.
 *
 * Tail seq is assigned to each queued task and incremented. Head seq tracks the
 * sequence number of the latest dispatched task. The distance between the a
 * task's seq and the associated queue's head seq is called the queue distance
 * and used when comparing two tasks for ordering. See qmap_core_sched_before().
 */
static u64 core_sched_head_seqs[5];
static u64 core_sched_tail_seqs[5];

/* Per-task scheduling context */
struct task_ctx {
	bool	force_local;	/* Dispatch directly to local_dsq */
	u64	core_sched_seq;
};

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct task_ctx);
} task_ctx_stor SEC(".maps");

struct cpu_ctx {
	u64	dsp_idx;	/* dispatch index */
	u64	dsp_cnt;	/* remaining count */
};

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, u32);
	__type(value, struct cpu_ctx);
} cpu_ctx_stor SEC(".maps");

/* Statistics */
u64 nr_enqueued, nr_dispatched, nr_reenqueued, nr_dequeued;
u64 nr_core_sched_execed;

s32 BPF_STRUCT_OPS(qmap_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	struct task_ctx *tctx;
	s32 cpu;

	tctx = bpf_task_storage_get(&task_ctx_stor, p, 0, 0);
	if (!tctx) {
		scx_bpf_error("task_ctx lookup failed");
		return -ESRCH;
	}

	if (p->nr_cpus_allowed == 1 ||
	    scx_bpf_test_and_clear_cpu_idle(prev_cpu)) {
		tctx->force_local = true;
		return prev_cpu;
	}

	cpu = scx_bpf_pick_idle_cpu(p->cpus_ptr, 0);
	if (cpu >= 0)
		return cpu;

	return prev_cpu;
}

static int weight_to_idx(u32 weight)
{
	/* Coarsely map the compound weight to a FIFO. */
	if (weight <= 25)
		return 0;
	else if (weight <= 50)
		return 1;
	else if (weight < 200)
		return 2;
	else if (weight < 400)
		return 3;
	else
		return 4;
}

void BPF_STRUCT_OPS(qmap_enqueue, struct task_struct *p, u64 enq_flags)
{
	static u32 user_cnt, kernel_cnt;
	struct task_ctx *tctx;
	u32 pid = p->pid;
	int idx = weight_to_idx(p->scx.weight);
	void *ring;

	if (p->flags & PF_KTHREAD) {
		if (stall_kernel_nth && !(++kernel_cnt % stall_kernel_nth))
			return;
	} else {
		if (stall_user_nth && !(++user_cnt % stall_user_nth))
			return;
	}

	if (test_error_cnt && !--test_error_cnt)
		scx_bpf_error("test triggering error");

	tctx = bpf_task_storage_get(&task_ctx_stor, p, 0, 0);
	if (!tctx) {
		scx_bpf_error("task_ctx lookup failed");
		return;
	}

	/*
	 * All enqueued tasks must have their core_sched_seq updated for correct
	 * core-sched ordering, which is why %SCX_OPS_ENQ_LAST is specified in
	 * qmap_ops.flags.
	 */
	tctx->core_sched_seq = core_sched_tail_seqs[idx]++;

	/*
	 * If qmap_select_cpu() is telling us to or this is the last runnable
	 * task on the CPU, enqueue locally.
	 */
	if (tctx->force_local || (enq_flags & SCX_ENQ_LAST)) {
		tctx->force_local = false;
		scx_bpf_dispatch(p, SCX_DSQ_LOCAL, slice_ns, enq_flags);
		return;
	}

	/*
	 * If the task was re-enqueued due to the CPU being preempted by a
	 * higher priority scheduling class, just re-enqueue the task directly
	 * on the global DSQ. As we want another CPU to pick it up, find and
	 * kick an idle CPU.
	 */
	if (enq_flags & SCX_ENQ_REENQ) {
		s32 cpu;

		scx_bpf_dispatch(p, SHARED_DSQ, 0, enq_flags);
		cpu = scx_bpf_pick_idle_cpu(p->cpus_ptr, 0);
		if (cpu >= 0)
			scx_bpf_kick_cpu(cpu, SCX_KICK_IDLE);
		return;
	}

	ring = bpf_map_lookup_elem(&queue_arr, &idx);
	if (!ring) {
		scx_bpf_error("failed to find ring %d", idx);
		return;
	}

	/* Queue on the selected FIFO. If the FIFO overflows, punt to global. */
	if (bpf_map_push_elem(ring, &pid, 0)) {
		scx_bpf_dispatch(p, SHARED_DSQ, slice_ns, enq_flags);
		return;
	}

	__sync_fetch_and_add(&nr_enqueued, 1);
}

/*
 * The BPF queue map doesn't support removal and sched_ext can handle spurious
 * dispatches. qmap_dequeue() is only used to collect statistics.
 */
void BPF_STRUCT_OPS(qmap_dequeue, struct task_struct *p, u64 deq_flags)
{
	__sync_fetch_and_add(&nr_dequeued, 1);
	if (deq_flags & SCX_DEQ_CORE_SCHED_EXEC)
		__sync_fetch_and_add(&nr_core_sched_execed, 1);
}

static void update_core_sched_head_seq(struct task_struct *p)
{
	struct task_ctx *tctx = bpf_task_storage_get(&task_ctx_stor, p, 0, 0);
	int idx = weight_to_idx(p->scx.weight);

	if (tctx)
		core_sched_head_seqs[idx] = tctx->core_sched_seq;
	else
		scx_bpf_error("task_ctx lookup failed");
}

void BPF_STRUCT_OPS(qmap_dispatch, s32 cpu, struct task_struct *prev)
{
	struct task_struct *p;
	struct cpu_ctx *cpuc;
	u32 zero = 0, batch = dsp_batch ?: 1;
	void *fifo;
	s32 i, pid;

	if (scx_bpf_consume(SHARED_DSQ))
		return;

	if (dsp_inf_loop_after && nr_dispatched > dsp_inf_loop_after) {
		/*
		 * PID 2 should be kthreadd which should mostly be idle and off
		 * the scheduler. Let's keep dispatching it to force the kernel
		 * to call this function over and over again.
		 */
		p = bpf_task_from_pid(2);
		if (p) {
			scx_bpf_dispatch(p, SCX_DSQ_LOCAL, slice_ns, 0);
			bpf_task_release(p);
			return;
		}
	}

	if (!(cpuc = bpf_map_lookup_elem(&cpu_ctx_stor, &zero))) {
		scx_bpf_error("failed to look up cpu_ctx");
		return;
	}

	for (i = 0; i < 5; i++) {
		/* Advance the dispatch cursor and pick the fifo. */
		if (!cpuc->dsp_cnt) {
			cpuc->dsp_idx = (cpuc->dsp_idx + 1) % 5;
			cpuc->dsp_cnt = 1 << cpuc->dsp_idx;
		}

		fifo = bpf_map_lookup_elem(&queue_arr, &cpuc->dsp_idx);
		if (!fifo) {
			scx_bpf_error("failed to find ring %llu", cpuc->dsp_idx);
			return;
		}

		/* Dispatch or advance. */
		bpf_repeat(BPF_MAX_LOOPS) {
			if (bpf_map_pop_elem(fifo, &pid))
				break;

			p = bpf_task_from_pid(pid);
			if (!p)
				continue;

			update_core_sched_head_seq(p);
			__sync_fetch_and_add(&nr_dispatched, 1);
			scx_bpf_dispatch(p, SHARED_DSQ, slice_ns, 0);
			bpf_task_release(p);
			batch--;
			cpuc->dsp_cnt--;
			if (!batch || !scx_bpf_dispatch_nr_slots()) {
				scx_bpf_consume(SHARED_DSQ);
				return;
			}
			if (!cpuc->dsp_cnt)
				break;
		}

		cpuc->dsp_cnt = 0;
	}
}

/*
 * The distance from the head of the queue scaled by the weight of the queue.
 * The lower the number, the older the task and the higher the priority.
 */
static s64 task_qdist(struct task_struct *p)
{
	int idx = weight_to_idx(p->scx.weight);
	struct task_ctx *tctx;
	s64 qdist;

	tctx = bpf_task_storage_get(&task_ctx_stor, p, 0, 0);
	if (!tctx) {
		scx_bpf_error("task_ctx lookup failed");
		return 0;
	}

	qdist = tctx->core_sched_seq - core_sched_head_seqs[idx];

	/*
	 * As queue index increments, the priority doubles. The queue w/ index 3
	 * is dispatched twice more frequently than 2. Reflect the difference by
	 * scaling qdists accordingly. Note that the shift amount needs to be
	 * flipped depending on the sign to avoid flipping priority direction.
	 */
	if (qdist >= 0)
		return qdist << (4 - idx);
	else
		return qdist << idx;
}

/*
 * This is called to determine the task ordering when core-sched is picking
 * tasks to execute on SMT siblings and should encode about the same ordering as
 * the regular scheduling path. Use the priority-scaled distances from the head
 * of the queues to compare the two tasks which should be consistent with the
 * dispatch path behavior.
 */
bool BPF_STRUCT_OPS(qmap_core_sched_before,
		    struct task_struct *a, struct task_struct *b)
{
	return task_qdist(a) > task_qdist(b);
}

void BPF_STRUCT_OPS(qmap_cpu_release, s32 cpu, struct scx_cpu_release_args *args)
{
	u32 cnt;

	/*
	 * Called when @cpu is taken by a higher priority scheduling class. This
	 * makes @cpu no longer available for executing sched_ext tasks. As we
	 * don't want the tasks in @cpu's local dsq to sit there until @cpu
	 * becomes available again, re-enqueue them into the global dsq. See
	 * %SCX_ENQ_REENQ handling in qmap_enqueue().
	 */
	cnt = scx_bpf_reenqueue_local();
	if (cnt)
		__sync_fetch_and_add(&nr_reenqueued, cnt);
}

s32 BPF_STRUCT_OPS(qmap_init_task, struct task_struct *p,
		   struct scx_init_task_args *args)
{
	if (p->tgid == disallow_tgid)
		p->scx.disallow = true;

	/*
	 * @p is new. Let's ensure that its task_ctx is available. We can sleep
	 * in this function and the following will automatically use GFP_KERNEL.
	 */
	if (bpf_task_storage_get(&task_ctx_stor, p, 0,
				 BPF_LOCAL_STORAGE_GET_F_CREATE))
		return 0;
	else
		return -ENOMEM;
}

void BPF_STRUCT_OPS(qmap_dump, struct scx_dump_ctx *dctx)
{
	s32 i, pid;

	if (suppress_dump)
		return;

	bpf_for(i, 0, 5) {
		void *fifo;

		if (!(fifo = bpf_map_lookup_elem(&queue_arr, &i)))
			return;

		scx_bpf_dump("QMAP FIFO[%d]:", i);
		bpf_repeat(4096) {
			if (bpf_map_pop_elem(fifo, &pid))
				break;
			scx_bpf_dump(" %d", pid);
		}
		scx_bpf_dump("\n");
	}
}

void BPF_STRUCT_OPS(qmap_dump_cpu, struct scx_dump_ctx *dctx, s32 cpu, bool idle)
{
	u32 zero = 0;
	struct cpu_ctx *cpuc;

	if (suppress_dump || idle)
		return;
	if (!(cpuc = bpf_map_lookup_percpu_elem(&cpu_ctx_stor, &zero, cpu)))
		return;

	scx_bpf_dump("QMAP: dsp_idx=%llu dsp_cnt=%llu",
		     cpuc->dsp_idx, cpuc->dsp_cnt);
}

void BPF_STRUCT_OPS(qmap_dump_task, struct scx_dump_ctx *dctx, struct task_struct *p)
{
	struct task_ctx *taskc;

	if (suppress_dump)
		return;
	if (!(taskc = bpf_task_storage_get(&task_ctx_stor, p, 0, 0)))
		return;

	scx_bpf_dump("QMAP: force_local=%d core_sched_seq=%llu",
		     taskc->force_local, taskc->core_sched_seq);
}

/*
 * Print out the online and possible CPU map using bpf_printk() as a
 * demonstration of using the cpumask kfuncs and ops.cpu_on/offline().
 */
static void print_cpus(void)
{
	const struct cpumask *possible, *online;
	s32 cpu;
	char buf[128] = "", *p;
	int idx;

	possible = scx_bpf_get_possible_cpumask();
	online = scx_bpf_get_online_cpumask();

	idx = 0;
	bpf_for(cpu, 0, scx_bpf_nr_cpu_ids()) {
		if (!(p = MEMBER_VPTR(buf, [idx++])))
			break;
		if (bpf_cpumask_test_cpu(cpu, online))
			*p++ = 'O';
		else if (bpf_cpumask_test_cpu(cpu, possible))
			*p++ = 'X';
		else
			*p++ = ' ';

		if ((cpu & 7) == 7) {
			if (!(p = MEMBER_VPTR(buf, [idx++])))
				break;
			*p++ = '|';
		}
	}
	buf[sizeof(buf) - 1] = '\0';

	scx_bpf_put_cpumask(online);
	scx_bpf_put_cpumask(possible);

	bpf_printk("CPUS: |%s", buf);
}

void BPF_STRUCT_OPS(qmap_cpu_online, s32 cpu)
{
	bpf_printk("CPU %d coming online", cpu);
	/* @cpu is already online at this point */
	print_cpus();
}

void BPF_STRUCT_OPS(qmap_cpu_offline, s32 cpu)
{
	bpf_printk("CPU %d going offline", cpu);
	/* @cpu is still online at this point */
	print_cpus();
}

s32 BPF_STRUCT_OPS_SLEEPABLE(qmap_init)
{
	print_cpus();

	return scx_bpf_create_dsq(SHARED_DSQ, -1);
}

void BPF_STRUCT_OPS(qmap_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(qmap_ops,
	       .select_cpu		= (void *)qmap_select_cpu,
	       .enqueue			= (void *)qmap_enqueue,
	       .dequeue			= (void *)qmap_dequeue,
	       .dispatch		= (void *)qmap_dispatch,
	       .core_sched_before	= (void *)qmap_core_sched_before,
	       .cpu_release		= (void *)qmap_cpu_release,
	       .init_task		= (void *)qmap_init_task,
	       .dump			= (void *)qmap_dump,
	       .dump_cpu		= (void *)qmap_dump_cpu,
	       .dump_task		= (void *)qmap_dump_task,
	       .cpu_online		= (void *)qmap_cpu_online,
	       .cpu_offline		= (void *)qmap_cpu_offline,
	       .init			= (void *)qmap_init,
	       .exit			= (void *)qmap_exit,
	       .flags			= SCX_OPS_ENQ_LAST,
	       .timeout_ms		= 5000U,
	       .name			= "qmap");
