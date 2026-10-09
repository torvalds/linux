// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct reclaim_args {
	__u64 cgroup_id;
	__u64 size;
};

/* Signed, because bpf_test_run_opts.retval is a __u32. */
__s64 reclaimed;

SEC("syscall")
int memcg_proactive_reclaim(struct reclaim_args *ctx)
{
	struct mem_cgroup *memcg;
	struct cgroup *cgrp;

	cgrp = bpf_cgroup_from_id(ctx->cgroup_id);
	if (!cgrp)
		return 0;

	memcg = bpf_get_mem_cgroup(&cgrp->self);
	if (memcg) {
		reclaimed = bpf_proactive_reclaim(memcg, ctx->size, -1);
		bpf_put_mem_cgroup(memcg);
	}
	bpf_cgroup_release(cgrp);

	return 0;
}

char _license[] SEC("license") = "GPL";
