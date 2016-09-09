/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

/* from /sys/kernel/debug/tracing/events/task/task_rename/format */
struct task_rename {
	__u64 pad;
	__u32 pid;
	char oldcomm[16];
	char newcomm[16];
	__u16 oom_score_adj;
};
SEC("tracepoint/task/task_rename")
int prog(struct task_rename *ctx)
{
	return 0;
}

/* from /sys/kernel/debug/tracing/events/random/urandom_read/format */
struct urandom_read {
	__u64 pad;
	int got_bits;
	int pool_left;
	int input_left;
};
SEC("tracepoint/random/urandom_read")
int prog2(struct urandom_read *ctx)
{
	return 0;
}
char _license[] SEC("license") = "GPL";
