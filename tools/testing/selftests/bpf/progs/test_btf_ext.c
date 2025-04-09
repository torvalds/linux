// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Meta Platforms Inc. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

__noinline static void f0(void)
{
	__u64 a = 1;

	__sink(a);
}

SEC("xdp")
__u64 global_func(struct xdp_md *xdp)
{
	f0();
	return XDP_DROP;
}
