// SPDX-License-Identifier: GPL-2.0
/*
 * A cid-form scheduler that calls scx_bpf_cid_override() from ops.init_cids()
 * and SCX_KF_ALLOW_UNLOCKED kfuncs from ops.init(). ops.dump() calls
 * scx_bpf_dump().
 *
 * Copyright (c) 2026 Google LLC.
 */

#include <scx/common.bpf.h>

#define SHARED_DSQ	0
#define MAX_CPUS	512

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 1 << 4);
} arena SEC(".maps");

s32 __arena_global cpu_to_cid[MAX_CPUS];
s32 __arena_global shard_start[MAX_CPUS];

s32 BPF_STRUCT_OPS_SLEEPABLE(unlocked_kfunc_cid_init_cids)
{
	u32 nr_cpu_ids = scx_bpf_nr_cpu_ids();

	if (nr_cpu_ids > MAX_CPUS)
		return -E2BIG;

	scx_bpf_cid_override(cpu_to_cid, nr_cpu_ids, shard_start, 1);
	return 0;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(unlocked_kfunc_cid_init)
{
	struct task_struct *p;
	s32 ret;

	ret = scx_bpf_create_dsq(SHARED_DSQ, -1);
	if (ret)
		return ret;

	bpf_rcu_read_lock();
	bpf_for_each(scx_dsq, p, SHARED_DSQ, 0) {
		if (p->pid & 1)
			scx_bpf_dsq_reenq_task(BPF_FOR_EACH_ITER, p);
		else
			scx_bpf_dsq_move(BPF_FOR_EACH_ITER, p, SCX_DSQ_GLOBAL, 0);
	}
	bpf_rcu_read_unlock();

	return 0;
}

void BPF_STRUCT_OPS(unlocked_kfunc_cid_dump, struct scx_dump_ctx *dctx)
{
	scx_bpf_dump("nr_cids=%u\n", scx_bpf_nr_cids());
}

SCX_OPS_CID_DEFINE(unlocked_kfunc_cid_ops,
		   .init_cids		= (void *)unlocked_kfunc_cid_init_cids,
		   .init		= (void *)unlocked_kfunc_cid_init,
		   .dump		= (void *)unlocked_kfunc_cid_dump,
		   .name		= "unlocked_kfunc_cid");
