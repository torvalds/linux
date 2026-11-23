// SPDX-License-Identifier: GPL-2.0
/*
 * A cpu-form scheduler that calls SCX_KF_ALLOW_UNLOCKED kfuncs from ops.init()
 * and ops.exit(). ops.dump() calls scx_bpf_dump().
 *
 * Copyright (c) 2026 Google LLC.
 */

#include <scx/common.bpf.h>

#define SHARED_DSQ	0

char _license[] SEC("license") = "GPL";

static void drain_dsq(void)
{
	struct task_struct *p;

	bpf_rcu_read_lock();
	bpf_for_each(scx_dsq, p, SHARED_DSQ, 0) {
		if (p->pid & 1)
			scx_bpf_dsq_reenq_task(BPF_FOR_EACH_ITER, p);
		else
			scx_bpf_dsq_move(BPF_FOR_EACH_ITER, p, SCX_DSQ_GLOBAL, 0);
	}
	bpf_rcu_read_unlock();
}

static void select_cpu_for_current(void)
{
	struct task_struct *p = bpf_get_current_task_btf();
	bool is_idle;

	bpf_rcu_read_lock();
	scx_bpf_select_cpu_dfl(p, bpf_get_smp_processor_id(), 0, &is_idle);
	bpf_rcu_read_unlock();
}

s32 BPF_STRUCT_OPS_SLEEPABLE(unlocked_kfunc_init)
{
	s32 ret;

	ret = scx_bpf_create_dsq(SHARED_DSQ, -1);
	if (ret)
		return ret;

	drain_dsq();
	select_cpu_for_current();
	return 0;
}

void BPF_STRUCT_OPS(unlocked_kfunc_exit, struct scx_exit_info *ei)
{
	drain_dsq();
	select_cpu_for_current();
}

void BPF_STRUCT_OPS(unlocked_kfunc_dump, struct scx_dump_ctx *dctx)
{
	scx_bpf_dump("nr_cpu_ids=%u\n", scx_bpf_nr_cpu_ids());
}

SEC(".struct_ops.link")
struct sched_ext_ops unlocked_kfunc_ops = {
	.init			= (void *)unlocked_kfunc_init,
	.exit			= (void *)unlocked_kfunc_exit,
	.dump			= (void *)unlocked_kfunc_dump,
	.name			= "unlocked_kfunc",
};
