/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Stress concurrent SCX_KICK_WAIT calls to reproduce wait-cycle deadlock.
 *
 * Three CPUs are designated from userspace. Every enqueue from one of the
 * three CPUs kicks the next CPU in the ring with SCX_KICK_WAIT, creating a
 * persistent A -> B -> C -> A wait cycle pressure.
 */
#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

const volatile s32 test_cpu_a;
const volatile s32 test_cpu_b;
const volatile s32 test_cpu_c;

u64 nr_enqueues;
u64 nr_wait_kicks;

UEI_DEFINE(uei);

static s32 target_cpu(s32 cpu)
{
	if (cpu == test_cpu_a)
		return test_cpu_b;
	if (cpu == test_cpu_b)
		return test_cpu_c;
	if (cpu == test_cpu_c)
		return test_cpu_a;
	return -1;
}

void BPF_STRUCT_OPS(cyclic_kick_wait_enqueue, struct task_struct *p,
		    u64 enq_flags)
{
	s32 this_cpu = bpf_get_smp_processor_id();
	s32 tgt;

	__sync_fetch_and_add(&nr_enqueues, 1);

	if (p->flags & PF_KTHREAD) {
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_INF,
				   enq_flags | SCX_ENQ_PREEMPT);
		return;
	}

	scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);

	tgt = target_cpu(this_cpu);
	if (tgt < 0 || tgt == this_cpu)
		return;

	__sync_fetch_and_add(&nr_wait_kicks, 1);
	scx_bpf_kick_cpu(tgt, SCX_KICK_WAIT);
}

void BPF_STRUCT_OPS(cyclic_kick_wait_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops cyclic_kick_wait_ops = {
	.enqueue		= cyclic_kick_wait_enqueue,
	.exit			= cyclic_kick_wait_exit,
	.name			= "cyclic_kick_wait",
	.timeout_ms		= 1000U,
};
