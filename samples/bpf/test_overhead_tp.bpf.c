/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* from /sys/kernel/tracing/events/task/task_rename/format */
struct task_rename {
	__u64 pad;
	__u32 pid;
	char oldcomm[TASK_COMM_LEN];
	char newcomm[TASK_COMM_LEN];
	__u16 oom_score_adj;
};
SEC("tracepoint/task/task_rename")
int prog(struct task_rename *ctx)
{
	return 0;
}

/* from /sys/kernel/tracing/events/fib/fib_table_lookup/format */
struct fib_table_lookup {
	__u64 pad;
	__u32 tb_id;
	int err;
	int oif;
	int iif;
	__u8 proto;
	__u8 tos;
	__u8 scope;
	__u8 flags;
	__u8 src[4];
	__u8 dst[4];
	__u8 gw4[4];
	__u8 gw6[16];
	__u16 sport;
	__u16 dport;
	char name[16];
};
SEC("tracepoint/fib/fib_table_lookup")
int prog2(struct fib_table_lookup *ctx)
{
	return 0;
}
char _license[] SEC("license") = "GPL";
