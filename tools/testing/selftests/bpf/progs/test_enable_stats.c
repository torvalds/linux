// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <linux/bpf.h>
#include <stdint.h>
#include <linux/types.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

__u64 count = 0;

SEC("raw_tracepoint/sys_enter")
int test_enable_stats(void *ctx)
{
	__sync_fetch_and_add(&count, 1);
	return 0;
}
