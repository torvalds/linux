// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

long t_b, t_c, t_ret;

SEC("fexit/bpf_testmod_test_int128_arg")
int test_int128_arg_fexit(unsigned long long *ctx)
{
	t_b = (int)ctx[2];
	t_c = (long)ctx[3];
	t_ret = (long)ctx[4];
	return 0;
}

char _license[] SEC("license") = "GPL";
