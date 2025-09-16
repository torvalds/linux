// SPDX-License-Identifier: GPL-2.0
/*
 * A scheduler that validates the behavior of scx_bpf_select_cpu_and() by
 * selecting idle CPUs strictly within a subset of allowed CPUs.
 *
 * Copyright (c) 2025 Andrea Righi <arighi@nvidia.com>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

private(PREF_CPUS) struct bpf_cpumask __kptr * allowed_cpumask;

static void
validate_idle_cpu(const struct task_struct *p, const struct cpumask *allowed, s32 cpu)
{
	if (scx_bpf_test_and_clear_cpu_idle(cpu))
		scx_bpf_error("CPU %d should be marked as busy", cpu);

	if (bpf_cpumask_subset(allowed, p->cpus_ptr) &&
	    !bpf_cpumask_test_cpu(cpu, allowed))
		scx_bpf_error("CPU %d not in the allowed domain for %d (%s)",
			      cpu, p->pid, p->comm);
}

s32 BPF_STRUCT_OPS(allowed_cpus_select_cpu,
		   struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
	const struct cpumask *allowed;
	s32 cpu;

	allowed = cast_mask(allowed_cpumask);
	if (!allowed) {
		scx_bpf_error("allowed domain not initialized");
		return -EINVAL;
	}

	/*
	 * Select an idle CPU strictly within the allowed domain.
	 */
	cpu = scx_bpf_select_cpu_and(p, prev_cpu, wake_flags, allowed, 0);
	if (cpu >= 0) {
		validate_idle_cpu(p, allowed, cpu);
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);

		return cpu;
	}

	return prev_cpu;
}

void BPF_STRUCT_OPS(allowed_cpus_enqueue, struct task_struct *p, u64 enq_flags)
{
	const struct cpumask *allowed;
	s32 prev_cpu = scx_bpf_task_cpu(p), cpu;

	scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, 0);

	allowed = cast_mask(allowed_cpumask);
	if (!allowed) {
		scx_bpf_error("allowed domain not initialized");
		return;
	}

	/*
	 * Use scx_bpf_select_cpu_and() to proactively kick an idle CPU
	 * within @allowed_cpumask, usable by @p.
	 */
	cpu = scx_bpf_select_cpu_and(p, prev_cpu, 0, allowed, 0);
	if (cpu >= 0) {
		validate_idle_cpu(p, allowed, cpu);
		scx_bpf_kick_cpu(cpu, SCX_KICK_IDLE);
	}
}

s32 BPF_STRUCT_OPS_SLEEPABLE(allowed_cpus_init)
{
	struct bpf_cpumask *mask;

	mask = bpf_cpumask_create();
	if (!mask)
		return -ENOMEM;

	mask = bpf_kptr_xchg(&allowed_cpumask, mask);
	if (mask)
		bpf_cpumask_release(mask);

	bpf_rcu_read_lock();

	/*
	 * Assign the first online CPU to the allowed domain.
	 */
	mask = allowed_cpumask;
	if (mask) {
		const struct cpumask *online = scx_bpf_get_online_cpumask();

		bpf_cpumask_set_cpu(bpf_cpumask_first(online), mask);
		scx_bpf_put_cpumask(online);
	}

	bpf_rcu_read_unlock();

	return 0;
}

void BPF_STRUCT_OPS(allowed_cpus_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

struct task_cpu_arg {
	pid_t pid;
};

SEC("syscall")
int select_cpu_from_user(struct task_cpu_arg *input)
{
	struct task_struct *p;
	int cpu;

	p = bpf_task_from_pid(input->pid);
	if (!p)
		return -EINVAL;

	bpf_rcu_read_lock();
	cpu = scx_bpf_select_cpu_and(p, bpf_get_smp_processor_id(), 0, p->cpus_ptr, 0);
	bpf_rcu_read_unlock();

	bpf_task_release(p);

	return cpu;
}

SEC(".struct_ops.link")
struct sched_ext_ops allowed_cpus_ops = {
	.select_cpu		= (void *)allowed_cpus_select_cpu,
	.enqueue		= (void *)allowed_cpus_enqueue,
	.init			= (void *)allowed_cpus_init,
	.exit			= (void *)allowed_cpus_exit,
	.name			= "allowed_cpus",
};
