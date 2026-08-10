// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

int nr_loops;
long hits;

static int outer_loop(__u32 index, void *data)
{
	int i;

	/*
	 * Empty body: the work being measured is the open-coded numeric iterator itself
	 * (bpf_iter_num_new/next/destroy behind bpf_for()).
	 */
	bpf_for(i, 0, nr_loops)
		;
	__sync_add_and_fetch(&hits, nr_loops);
	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int benchmark(void *ctx)
{
	bpf_loop(1000, outer_loop, NULL, 0);
	return 0;
}
