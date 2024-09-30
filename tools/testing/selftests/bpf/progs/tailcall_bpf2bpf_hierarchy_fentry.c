// SPDX-License-Identifier: GPL-2.0
/* Copyright Leon Hwang */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

int count = 0;

static __noinline
int subprog_tail(void *ctx)
{
	bpf_tail_call_static(ctx, &jmp_table, 0);
	return 0;
}

SEC("fentry/dummy")
int BPF_PROG(fentry, struct sk_buff *skb)
{
	count++;
	subprog_tail(ctx);
	subprog_tail(ctx);

	return 0;
}


char _license[] SEC("license") = "GPL";
