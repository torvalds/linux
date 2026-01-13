// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Google LLC.
 */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

SEC("syscall")
__success __retval(0)
int root_mem_cgroup_default_trusted(void *ctx)
{
	unsigned long usage;
	struct mem_cgroup *root_mem_cgroup;

	root_mem_cgroup = bpf_get_root_mem_cgroup();
	if (!root_mem_cgroup)
		return 1;

	/*
	 * BPF kfunc bpf_get_root_mem_cgroup() returns a PTR_TO_BTF_ID |
	 * PTR_TRUSTED | PTR_MAYBE_NULL, therefore it should be accepted when
	 * passed to a BPF kfunc only accepting KF_TRUSTED_ARGS.
	 */
	usage = bpf_mem_cgroup_usage(root_mem_cgroup);
	__sink(usage);
	return 0;
}

char _license[] SEC("license") = "GPL";
