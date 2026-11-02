// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct int128_arg {
	__int128 v;
};

long odd_a, odd_lo, odd_hi, odd_b, odd_ret;

SEC("fentry/callee_odd")
int BPF_PROG2(odd_entry, __u64, a, struct int128_arg, v, __u64, b)
{
	odd_a = a;
	odd_lo = (__u64)v.v;
	odd_hi = (unsigned __int128)v.v >> 64;
	odd_b = b;
	return 0;
}

SEC("fexit/callee_odd")
int odd_exit(unsigned long long *ctx)
{
	odd_ret = ctx[4];
	return 0;
}

long stack_g, stack_lo, stack_hi, stack_i, stack_ret;

SEC("fentry/callee_stack")
int BPF_PROG2(stack_entry, __u64, a, __u64, b, __u64, c, __u64, d, __u64, e,
	      __u64, f, __u64, g, struct int128_arg, h, __u64, i)
{
	stack_g = g;
	stack_lo = (__u64)h.v;
	stack_hi = (unsigned __int128)h.v >> 64;
	stack_i = i;
	return 0;
}

SEC("fexit/callee_stack")
int stack_exit(unsigned long long *ctx)
{
	stack_ret = ctx[10];
	return 0;
}

long split_e, split_lo, split_hi, split_i, split_ret;

SEC("fentry/callee_split")
int BPF_PROG2(split_entry, __u64, a, __u64, b, __u64, c, __u64, d, __u64, e,
	      struct int128_arg, h, __u64, i)
{
	split_e = e;
	split_lo = (__u64)h.v;
	split_hi = (unsigned __int128)h.v >> 64;
	split_i = i;
	return 0;
}

SEC("fexit/callee_split")
int split_exit(unsigned long long *ctx)
{
	split_ret = ctx[8];
	return 0;
}

/* Refused: the trampoline would lose the stack argument. */
SEC("?fentry/callee_unreliable")
int unreliable_entry(unsigned long long *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
