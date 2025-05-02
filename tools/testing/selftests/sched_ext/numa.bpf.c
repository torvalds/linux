// SPDX-License-Identifier: GPL-2.0
/*
 * A scheduler that validates the behavior of the NUMA-aware
 * functionalities.
 *
 * The scheduler creates a separate DSQ for each NUMA node, ensuring tasks
 * are exclusively processed by CPUs within their respective nodes. Idle
 * CPUs are selected only within the same node, so task migration can only
 * occurs between CPUs belonging to the same node.
 *
 * Copyright (c) 2025 Andrea Righi <arighi@nvidia.com>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

const volatile unsigned int __COMPAT_SCX_PICK_IDLE_IN_NODE;

static bool is_cpu_idle(s32 cpu, int node)
{
	const struct cpumask *idle_cpumask;
	bool idle;

	idle_cpumask = __COMPAT_scx_bpf_get_idle_cpumask_node(node);
	idle = bpf_cpumask_test_cpu(cpu, idle_cpumask);
	scx_bpf_put_cpumask(idle_cpumask);

	return idle;
}

s32 BPF_STRUCT_OPS(numa_select_cpu,
		   struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
	int node = __COMPAT_scx_bpf_cpu_node(scx_bpf_task_cpu(p));
	s32 cpu;

	/*
	 * We could just use __COMPAT_scx_bpf_pick_any_cpu_node() here,
	 * since it already tries to pick an idle CPU within the node
	 * first, but let's use both functions for better testing coverage.
	 */
	cpu = __COMPAT_scx_bpf_pick_idle_cpu_node(p->cpus_ptr, node,
					__COMPAT_SCX_PICK_IDLE_IN_NODE);
	if (cpu < 0)
		cpu = __COMPAT_scx_bpf_pick_any_cpu_node(p->cpus_ptr, node,
						__COMPAT_SCX_PICK_IDLE_IN_NODE);

	if (is_cpu_idle(cpu, node))
		scx_bpf_error("CPU %d should be marked as busy", cpu);

	if (__COMPAT_scx_bpf_cpu_node(cpu) != node)
		scx_bpf_error("CPU %d should be in node %d", cpu, node);

	return cpu;
}

void BPF_STRUCT_OPS(numa_enqueue, struct task_struct *p, u64 enq_flags)
{
	int node = __COMPAT_scx_bpf_cpu_node(scx_bpf_task_cpu(p));

	scx_bpf_dsq_insert(p, node, SCX_SLICE_DFL, enq_flags);
}

void BPF_STRUCT_OPS(numa_dispatch, s32 cpu, struct task_struct *prev)
{
	int node = __COMPAT_scx_bpf_cpu_node(cpu);

	scx_bpf_dsq_move_to_local(node);
}

s32 BPF_STRUCT_OPS_SLEEPABLE(numa_init)
{
	int node, err;

	bpf_for(node, 0, __COMPAT_scx_bpf_nr_node_ids()) {
		err = scx_bpf_create_dsq(node, node);
		if (err)
			return err;
	}

	return 0;
}

void BPF_STRUCT_OPS(numa_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops numa_ops = {
	.select_cpu		= (void *)numa_select_cpu,
	.enqueue		= (void *)numa_enqueue,
	.dispatch		= (void *)numa_dispatch,
	.init			= (void *)numa_init,
	.exit			= (void *)numa_exit,
	.name			= "numa",
};
