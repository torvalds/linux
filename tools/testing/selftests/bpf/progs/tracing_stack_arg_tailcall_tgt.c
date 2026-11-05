// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

__u64 base;
__u64 ret_stack;

#if defined(__clang__) && defined(__BPF_FEATURE_STACK_ARGUMENT)

const volatile bool has_stack_arg = true;

/* g and h are on the stack, for the x86-64 kernel convention too. */
static __noinline __u64 callee_stack(__u64 a, __u64 b, __u64 c, __u64 d,
				     __u64 e, __u64 f, __u64 g, __u64 h)
{
	return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8;
}

/* No stack args of its own, so it can share a program with a tail call. */
static __noinline __u64 call_stack(__u64 x)
{
	return callee_stack(x + 1, x + 2, x + 3, x + 4, x + 5, x + 6, x + 7,
			    x + 8);
}

#else

const volatile bool has_stack_arg = false;

#endif

/* Makes the program tail_call_reachable, though callee_stack() is not. */
static __noinline int subprog_tail(void *ctx)
{
	int ret = 0;

	bpf_tail_call_static(ctx, &jmp_table, 0);
	barrier_var(ret);
	return ret;
}

SEC("syscall")
int run(void *ctx)
{
#if defined(__clang__) && defined(__BPF_FEATURE_STACK_ARGUMENT)
	ret_stack = call_stack(base);
#endif
	return subprog_tail(ctx);
}

char _license[] SEC("license") = "GPL";
