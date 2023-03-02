// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018 Facebook */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

SEC("raw_tracepoint/task_rename")
int prog(struct bpf_raw_tracepoint_args *ctx)
{
	return 0;
}

SEC("raw_tracepoint/fib_table_lookup")
int prog2(struct bpf_raw_tracepoint_args *ctx)
{
	return 0;
}
char _license[] SEC("license") = "GPL";
