// SPDX-License-Identifier: GPL-2.0
/*
 * Validate scx_bpf_cgroup_nr_cpus() from both BPF_PROG_TYPE_SYSCALL and
 * struct_ops contexts.
 *
 * Copyright (c) 2026 NVIDIA Corporation.
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

/* input to cgroup_nr_cpus_read() */
u64 query_cgid;
/* output of cgroup_nr_cpus_read(), -1 if @query_cgid couldn't be resolved */
s64 query_nr_cpus = -1;

/* recorded by ops.cgroup_init() for @init_cgid */
u64 init_cgid;
s64 init_nr_cpus = -1;

SEC("syscall")
int cgroup_nr_cpus_read(void *ctx)
{
	struct cgroup *cgrp;

	query_nr_cpus = -1;

	cgrp = bpf_cgroup_from_id(query_cgid);
	if (!cgrp)
		return -ENOENT;

	query_nr_cpus = scx_bpf_cgroup_nr_cpus(cgrp);
	bpf_cgroup_release(cgrp);

	return 0;
}

s32 BPF_STRUCT_OPS(cgroup_nr_cpus_cgroup_init, struct cgroup *cgrp,
		   struct scx_cgroup_init_args *args)
{
	if (cgrp->kn->id == init_cgid)
		init_nr_cpus = scx_bpf_cgroup_nr_cpus(cgrp);

	return 0;
}

void BPF_STRUCT_OPS(cgroup_nr_cpus_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops cgroup_nr_cpus_ops = {
	.cgroup_init		= (void *)cgroup_nr_cpus_cgroup_init,
	.exit			= (void *)cgroup_nr_cpus_exit,
	.name			= "cgroup_nr_cpus",
};
