// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_core_read.h>
#include "cgroup_iter_memcg.h"

char _license[] SEC("license") = "GPL";

/* The latest values read are stored here. */
struct memcg_query memcg_query SEC(".data.query");

SEC("iter.s/cgroup")
int cgroup_memcg_query(struct bpf_iter__cgroup *ctx)
{
	struct cgroup *cgrp = ctx->cgroup;
	struct cgroup_subsys_state *css;
	struct mem_cgroup *memcg;

	if (!cgrp)
		return 1;

	css = &cgrp->self;
	memcg = bpf_get_mem_cgroup(css);
	if (!memcg)
		return 1;

	bpf_mem_cgroup_flush_stats(memcg);

	memcg_query.nr_anon_mapped = bpf_mem_cgroup_page_state(memcg, NR_ANON_MAPPED);
	memcg_query.nr_shmem = bpf_mem_cgroup_page_state(memcg, NR_SHMEM);
	memcg_query.nr_file_pages = bpf_mem_cgroup_page_state(memcg, NR_FILE_PAGES);
	memcg_query.nr_file_mapped = bpf_mem_cgroup_page_state(memcg, NR_FILE_MAPPED);
	memcg_query.memcg_kmem = bpf_mem_cgroup_page_state(memcg, MEMCG_KMEM);
	memcg_query.pgfault = bpf_mem_cgroup_vm_events(memcg, PGFAULT);

	bpf_put_mem_cgroup(memcg);

	return 0;
}
