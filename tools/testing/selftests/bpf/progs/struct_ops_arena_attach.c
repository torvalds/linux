// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

SEC("fentry")
int BPF_PROG(fentry_test_arena, unsigned long long *st_ops_ctx)
{
	return 0;
}

SEC("fexit")
int BPF_PROG(fexit_test_arena, unsigned long long *st_ops_ctx, int ret)
{
	return 0;
}

SEC("freplace")
int freplace_test_arena(unsigned long long *st_ops_ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
