// SPDX-License-Identifier: GPL-2.0
/* Copyright Leon Hwang */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_test_utils.h"

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
	int ret = 0;

	bpf_tail_call_static(ctx, &jmp_table, 0);
	barrier_var(ret);
	return ret;
}

SEC("fentry/dummy")
int BPF_PROG(fentry, struct sk_buff *skb)
{
	int ret1, ret2;

	clobber_regs_stack();

	count++;
	ret1 = subprog_tail(ctx);
	ret2 = subprog_tail(ctx);
	__sink(ret1);
	__sink(ret2);

	return 0;
}


char _license[] SEC("license") = "GPL";
