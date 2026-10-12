// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

SEC("freplace/agg_ret_target_func")
__u64 new_agg_ret_target_func(void)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
