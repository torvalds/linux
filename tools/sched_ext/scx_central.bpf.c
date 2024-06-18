/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A central FIFO sched_ext scheduler which demonstrates the followings:
 *
 * a. Making all scheduling decisions from one CPU:
 *
 *    The central CPU is the only one making scheduling decisions. All other
 *    CPUs kick the central CPU when they run out of tasks to run.
 *
 *    There is one global BPF queue and the central CPU schedules all CPUs by
 *    dispatching from the global queue to each CPU's local dsq from dispatch().
 *    This isn't the most straightforward. e.g. It'd be easier to bounce
 *    through per-CPU BPF queues. The current design is chosen to maximally
 *    utilize and verify various SCX mechanisms such as LOCAL_ON dispatching.
 *
 * b. Tickless operation
 *
 *    All tasks are dispatched with the infinite slice which allows stopping the
 *    ticks on CONFIG_NO_HZ_FULL kernels running with the proper nohz_full
 *    parameter. The tickless operation can be observed through
 *    /proc/interrupts.
 *
 *    Periodic switching is enforced by a periodic timer checking all CPUs and
 *    preempting them as necessary. Unfortunately, BPF timer currently doesn't
 *    have a way to pin to a specific CPU, so the periodic timer isn't pinned to
 *    the central CPU.
 *
 * c. Preemption
 *
 *    Kthreads are unconditionally queued to the head of a matching local dsq
 *    and dispatched with SCX_DSQ_PREEMPT. This ensures that a kthread is always
 *    prioritized over user threads, which is required for ensuring forward
 *    progress as e.g. the periodic timer may run on a ksoftirqd and if the
 *    ksoftirqd gets starved by a user thread, there may not be anything else to
 *    vacate that user thread.
 *
 *    SCX_KICK_PREEMPT is used to trigger scheduling and CPUs to move to the
 *    next tasks.
 *
 * This scheduler is designed to maximize usage of various SCX mechanisms. A
 * more practical implementation would likely put the scheduling loop outside
 * the central CPU's dispatch() path and add some form of priority mechanism.
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

enum {
	FALLBACK_DSQ_ID		= 0,
	MS_TO_NS		= 1000LLU * 1000,
	TIMER_INTERVAL_NS	= 1 * MS_TO_NS,
};

const volatile s32 central_cpu;
const volatile u32 nr_cpu_ids = 1;	/* !0 for veristat, set during init */
const volatile u64 slice_ns = SCX_SLICE_DFL;

bool timer_pinned = true;
u64 nr_total, nr_locals, nr_queued, nr_lost_pids;
u64 nr_timers, nr_dispatches, nr_mismatches, nr_retries;
u64 nr_overflows;

UEI_DEFINE(uei);

struct {
	__uint(type, BPF_MAP_TYPE_QUEUE);
	__uint(max_entries, 4096);
	__type(value, s32);
} central_q SEC(".maps");

/* can't use percpu map due to bad lookups */
bool RESIZABLE_ARRAY(data, cpu_gimme_task);
u64 RESIZABLE_ARRAY(data, cpu_started_at);

struct central_timer {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, u32);
	__type(value, struct central_timer);
} central_timer SEC(".maps");

static bool vtime_before(u64 a, u64 b)
{
	return (s64)(a - b) < 0;
}

s32 BPF_STRUCT_OPS(central_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	/*
	 * Steer wakeups to the central CPU as much as possible to avoid
	 * disturbing other CPUs. It's safe to blindly return the central cpu as
	 * select_cpu() is a hint and if @p can't be on it, the kernel will
	 * automatically pick a fallback CPU.
	 */
	return central_cpu;
}

void BPF_STRUCT_OPS(central_enqueue, struct task_struct *p, u64 enq_flags)
{
	s32 pid = p->pid;

	__sync_fetch_and_add(&nr_total, 1);

	/*
	 * Push per-cpu kthreads at the head of local dsq's and preempt the
	 * corresponding CPU. This ensures that e.g. ksoftirqd isn't blocked
	 * behind other threads which is necessary for forward progress
	 * guarantee as we depend on the BPF timer which may run from ksoftirqd.
	 */
	if ((p->flags & PF_KTHREAD) && p->nr_cpus_allowed == 1) {
		__sync_fetch_and_add(&nr_locals, 1);
		scx_bpf_dispatch(p, SCX_DSQ_LOCAL, SCX_SLICE_INF,
				 enq_flags | SCX_ENQ_PREEMPT);
		return;
	}

	if (bpf_map_push_elem(&central_q, &pid, 0)) {
		__sync_fetch_and_add(&nr_overflows, 1);
		scx_bpf_dispatch(p, FALLBACK_DSQ_ID, SCX_SLICE_INF, enq_flags);
		return;
	}

	__sync_fetch_and_add(&nr_queued, 1);

	if (!scx_bpf_task_running(p))
		scx_bpf_kick_cpu(central_cpu, SCX_KICK_PREEMPT);
}

static bool dispatch_to_cpu(s32 cpu)
{
	struct task_struct *p;
	s32 pid;

	bpf_repeat(BPF_MAX_LOOPS) {
		if (bpf_map_pop_elem(&central_q, &pid))
			break;

		__sync_fetch_and_sub(&nr_queued, 1);

		p = bpf_task_from_pid(pid);
		if (!p) {
			__sync_fetch_and_add(&nr_lost_pids, 1);
			continue;
		}

		/*
		 * If we can't run the task at the top, do the dumb thing and
		 * bounce it to the fallback dsq.
		 */
		if (!bpf_cpumask_test_cpu(cpu, p->cpus_ptr)) {
			__sync_fetch_and_add(&nr_mismatches, 1);
			scx_bpf_dispatch(p, FALLBACK_DSQ_ID, SCX_SLICE_INF, 0);
			bpf_task_release(p);
			/*
			 * We might run out of dispatch buffer slots if we continue dispatching
			 * to the fallback DSQ, without dispatching to the local DSQ of the
			 * target CPU. In such a case, break the loop now as will fail the
			 * next dispatch operation.
			 */
			if (!scx_bpf_dispatch_nr_slots())
				break;
			continue;
		}

		/* dispatch to local and mark that @cpu doesn't need more */
		scx_bpf_dispatch(p, SCX_DSQ_LOCAL_ON | cpu, SCX_SLICE_INF, 0);

		if (cpu != central_cpu)
			scx_bpf_kick_cpu(cpu, SCX_KICK_IDLE);

		bpf_task_release(p);
		return true;
	}

	return false;
}

void BPF_STRUCT_OPS(central_dispatch, s32 cpu, struct task_struct *prev)
{
	if (cpu == central_cpu) {
		/* dispatch for all other CPUs first */
		__sync_fetch_and_add(&nr_dispatches, 1);

		bpf_for(cpu, 0, nr_cpu_ids) {
			bool *gimme;

			if (!scx_bpf_dispatch_nr_slots())
				break;

			/* central's gimme is never set */
			gimme = ARRAY_ELEM_PTR(cpu_gimme_task, cpu, nr_cpu_ids);
			if (gimme && !*gimme)
				continue;

			if (dispatch_to_cpu(cpu))
				*gimme = false;
		}

		/*
		 * Retry if we ran out of dispatch buffer slots as we might have
		 * skipped some CPUs and also need to dispatch for self. The ext
		 * core automatically retries if the local dsq is empty but we
		 * can't rely on that as we're dispatching for other CPUs too.
		 * Kick self explicitly to retry.
		 */
		if (!scx_bpf_dispatch_nr_slots()) {
			__sync_fetch_and_add(&nr_retries, 1);
			scx_bpf_kick_cpu(central_cpu, SCX_KICK_PREEMPT);
			return;
		}

		/* look for a task to run on the central CPU */
		if (scx_bpf_consume(FALLBACK_DSQ_ID))
			return;
		dispatch_to_cpu(central_cpu);
	} else {
		bool *gimme;

		if (scx_bpf_consume(FALLBACK_DSQ_ID))
			return;

		gimme = ARRAY_ELEM_PTR(cpu_gimme_task, cpu, nr_cpu_ids);
		if (gimme)
			*gimme = true;

		/*
		 * Force dispatch on the scheduling CPU so that it finds a task
		 * to run for us.
		 */
		scx_bpf_kick_cpu(central_cpu, SCX_KICK_PREEMPT);
	}
}

void BPF_STRUCT_OPS(central_running, struct task_struct *p)
{
	s32 cpu = scx_bpf_task_cpu(p);
	u64 *started_at = ARRAY_ELEM_PTR(cpu_started_at, cpu, nr_cpu_ids);
	if (started_at)
		*started_at = bpf_ktime_get_ns() ?: 1;	/* 0 indicates idle */
}

void BPF_STRUCT_OPS(central_stopping, struct task_struct *p, bool runnable)
{
	s32 cpu = scx_bpf_task_cpu(p);
	u64 *started_at = ARRAY_ELEM_PTR(cpu_started_at, cpu, nr_cpu_ids);
	if (started_at)
		*started_at = 0;
}

static int central_timerfn(void *map, int *key, struct bpf_timer *timer)
{
	u64 now = bpf_ktime_get_ns();
	u64 nr_to_kick = nr_queued;
	s32 i, curr_cpu;

	curr_cpu = bpf_get_smp_processor_id();
	if (timer_pinned && (curr_cpu != central_cpu)) {
		scx_bpf_error("Central timer ran on CPU %d, not central CPU %d",
			      curr_cpu, central_cpu);
		return 0;
	}

	bpf_for(i, 0, nr_cpu_ids) {
		s32 cpu = (nr_timers + i) % nr_cpu_ids;
		u64 *started_at;

		if (cpu == central_cpu)
			continue;

		/* kick iff the current one exhausted its slice */
		started_at = ARRAY_ELEM_PTR(cpu_started_at, cpu, nr_cpu_ids);
		if (started_at && *started_at &&
		    vtime_before(now, *started_at + slice_ns))
			continue;

		/* and there's something pending */
		if (scx_bpf_dsq_nr_queued(FALLBACK_DSQ_ID) ||
		    scx_bpf_dsq_nr_queued(SCX_DSQ_LOCAL_ON | cpu))
			;
		else if (nr_to_kick)
			nr_to_kick--;
		else
			continue;

		scx_bpf_kick_cpu(cpu, SCX_KICK_PREEMPT);
	}

	bpf_timer_start(timer, TIMER_INTERVAL_NS, BPF_F_TIMER_CPU_PIN);
	__sync_fetch_and_add(&nr_timers, 1);
	return 0;
}

int BPF_STRUCT_OPS_SLEEPABLE(central_init)
{
	u32 key = 0;
	struct bpf_timer *timer;
	int ret;

	ret = scx_bpf_create_dsq(FALLBACK_DSQ_ID, -1);
	if (ret)
		return ret;

	timer = bpf_map_lookup_elem(&central_timer, &key);
	if (!timer)
		return -ESRCH;

	if (bpf_get_smp_processor_id() != central_cpu) {
		scx_bpf_error("init from non-central CPU");
		return -EINVAL;
	}

	bpf_timer_init(timer, &central_timer, CLOCK_MONOTONIC);
	bpf_timer_set_callback(timer, central_timerfn);

	ret = bpf_timer_start(timer, TIMER_INTERVAL_NS, BPF_F_TIMER_CPU_PIN);
	/*
	 * BPF_F_TIMER_CPU_PIN is pretty new (>=6.7). If we're running in a
	 * kernel which doesn't have it, bpf_timer_start() will return -EINVAL.
	 * Retry without the PIN. This would be the perfect use case for
	 * bpf_core_enum_value_exists() but the enum type doesn't have a name
	 * and can't be used with bpf_core_enum_value_exists(). Oh well...
	 */
	if (ret == -EINVAL) {
		timer_pinned = false;
		ret = bpf_timer_start(timer, TIMER_INTERVAL_NS, 0);
	}
	if (ret)
		scx_bpf_error("bpf_timer_start failed (%d)", ret);
	return ret;
}

void BPF_STRUCT_OPS(central_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(central_ops,
	       /*
		* We are offloading all scheduling decisions to the central CPU
		* and thus being the last task on a given CPU doesn't mean
		* anything special. Enqueue the last tasks like any other tasks.
		*/
	       .flags			= SCX_OPS_ENQ_LAST,

	       .select_cpu		= (void *)central_select_cpu,
	       .enqueue			= (void *)central_enqueue,
	       .dispatch		= (void *)central_dispatch,
	       .running			= (void *)central_running,
	       .stopping		= (void *)central_stopping,
	       .init			= (void *)central_init,
	       .exit			= (void *)central_exit,
	       .name			= "central");
