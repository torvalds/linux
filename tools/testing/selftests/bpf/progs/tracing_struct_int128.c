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

/*
 * The arguments come packed in the BPF convention whatever holes the
 * kernel convention left between them: an eightbyte each, and two for the
 * struct holding an __int128.
 */
long s_g, s_h_lo, s_h_hi, s_i, s_ret;

SEC("fentry/bpf_testmod_test_int128_stack")
int test_int128_stack_fentry(unsigned long long *ctx)
{
	s_g = ctx[6];
	s_h_lo = ctx[7];
	s_h_hi = ctx[8];
	s_i = ctx[9];
	return 0;
}

SEC("fexit/bpf_testmod_test_int128_stack")
int test_int128_stack_fexit(unsigned long long *ctx)
{
	s_ret = ctx[10];
	return 0;
}

long b_f_a, b_f_b, b_g, b_h, b_i_lo, b_i_hi, b_ret;

SEC("fentry/bpf_testmod_test_int128_backfill")
int test_int128_backfill_fentry(unsigned long long *ctx)
{
	b_f_a = ctx[5];
	b_f_b = ctx[6];
	b_g = ctx[7];
	b_h = ctx[8];
	b_i_lo = ctx[9];
	b_i_hi = ctx[10];
	return 0;
}

SEC("fexit/bpf_testmod_test_int128_backfill")
int test_int128_backfill_fexit(unsigned long long *ctx)
{
	b_ret = ctx[11];
	return 0;
}

char _license[] SEC("license") = "GPL";
