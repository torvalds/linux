// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

long stack_g, stack_h, stack_ret;

SEC("fentry/callee_stack")
int stack_entry(unsigned long long *ctx)
{
	stack_g = ctx[6];
	stack_h = ctx[7];
	return 0;
}

SEC("fexit/callee_stack")
int stack_exit(unsigned long long *ctx)
{
	stack_ret = ctx[8];
	return 0;
}

char _license[] SEC("license") = "GPL";
