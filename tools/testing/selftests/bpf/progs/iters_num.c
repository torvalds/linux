// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <limits.h>
#include <linux/errno.h>
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

const volatile __s64 exp_empty_zero = 0 + 1;
__s64 res_empty_zero;

SEC("raw_tp/sys_enter")
int num_empty_zero(const void *ctx)
{
	__s64 sum = 0, i;

	bpf_for(i, 0, 0) sum += i;
	res_empty_zero = 1 + sum;

	return 0;
}

const volatile __s64 exp_empty_int_min = 0 + 2;
__s64 res_empty_int_min;

SEC("raw_tp/sys_enter")
int num_empty_int_min(const void *ctx)
{
	__s64 sum = 0, i;

	bpf_for(i, INT_MIN, INT_MIN) sum += i;
	res_empty_int_min = 2 + sum;

	return 0;
}

const volatile __s64 exp_empty_int_max = 0 + 3;
__s64 res_empty_int_max;

SEC("raw_tp/sys_enter")
int num_empty_int_max(const void *ctx)
{
	__s64 sum = 0, i;

	bpf_for(i, INT_MAX, INT_MAX) sum += i;
	res_empty_int_max = 3 + sum;

	return 0;
}

const volatile __s64 exp_empty_minus_one = 0 + 4;
__s64 res_empty_minus_one;

SEC("raw_tp/sys_enter")
int num_empty_minus_one(const void *ctx)
{
	__s64 sum = 0, i;

	bpf_for(i, -1, -1) sum += i;
	res_empty_minus_one = 4 + sum;

	return 0;
}

const volatile __s64 exp_simple_sum = 9 * 10 / 2;
__s64 res_simple_sum;

SEC("raw_tp/sys_enter")
int num_simple_sum(const void *ctx)
{
	__s64 sum = 0, i;

	bpf_for(i, 0, 10) sum += i;
	res_simple_sum = sum;

	return 0;
}

const volatile __s64 exp_neg_sum = -11 * 10 / 2;
__s64 res_neg_sum;

SEC("raw_tp/sys_enter")
int num_neg_sum(const void *ctx)
{
	__s64 sum = 0, i;

	bpf_for(i, -10, 0) sum += i;
	res_neg_sum = sum;

	return 0;
}

const volatile __s64 exp_very_neg_sum = INT_MIN + (__s64)(INT_MIN + 1);
__s64 res_very_neg_sum;

SEC("raw_tp/sys_enter")
int num_very_neg_sum(const void *ctx)
{
	__s64 sum = 0, i;

	bpf_for(i, INT_MIN, INT_MIN + 2) sum += i;
	res_very_neg_sum = sum;

	return 0;
}

const volatile __s64 exp_very_big_sum = (__s64)(INT_MAX - 1) + (__s64)(INT_MAX - 2);
__s64 res_very_big_sum;

SEC("raw_tp/sys_enter")
int num_very_big_sum(const void *ctx)
{
	__s64 sum = 0, i;

	bpf_for(i, INT_MAX - 2, INT_MAX) sum += i;
	res_very_big_sum = sum;

	return 0;
}

const volatile __s64 exp_neg_pos_sum = -3;
__s64 res_neg_pos_sum;

SEC("raw_tp/sys_enter")
int num_neg_pos_sum(const void *ctx)
{
	__s64 sum = 0, i;

	bpf_for(i, -3, 3) sum += i;
	res_neg_pos_sum = sum;

	return 0;
}

const volatile __s64 exp_invalid_range = -EINVAL;
__s64 res_invalid_range;

SEC("raw_tp/sys_enter")
int num_invalid_range(const void *ctx)
{
	struct bpf_iter_num it;

	res_invalid_range = bpf_iter_num_new(&it, 1, 0);
	bpf_iter_num_destroy(&it);

	return 0;
}

const volatile __s64 exp_max_range = 0 + 10;
__s64 res_max_range;

SEC("raw_tp/sys_enter")
int num_max_range(const void *ctx)
{
	struct bpf_iter_num it;

	res_max_range = 10 + bpf_iter_num_new(&it, 0, BPF_MAX_LOOPS);
	bpf_iter_num_destroy(&it);

	return 0;
}

const volatile __s64 exp_e2big_range = -E2BIG;
__s64 res_e2big_range;

SEC("raw_tp/sys_enter")
int num_e2big_range(const void *ctx)
{
	struct bpf_iter_num it;

	res_e2big_range = bpf_iter_num_new(&it, -1, BPF_MAX_LOOPS);
	bpf_iter_num_destroy(&it);

	return 0;
}

const volatile __s64 exp_succ_elem_cnt = 10;
__s64 res_succ_elem_cnt;

SEC("raw_tp/sys_enter")
int num_succ_elem_cnt(const void *ctx)
{
	struct bpf_iter_num it;
	int cnt = 0, *v;

	bpf_iter_num_new(&it, 0, 10);
	while ((v = bpf_iter_num_next(&it))) {
		cnt++;
	}
	bpf_iter_num_destroy(&it);

	res_succ_elem_cnt = cnt;

	return 0;
}

const volatile __s64 exp_overfetched_elem_cnt = 5;
__s64 res_overfetched_elem_cnt;

SEC("raw_tp/sys_enter")
int num_overfetched_elem_cnt(const void *ctx)
{
	struct bpf_iter_num it;
	int cnt = 0, *v, i;

	bpf_iter_num_new(&it, 0, 5);
	for (i = 0; i < 10; i++) {
		v = bpf_iter_num_next(&it);
		if (v)
			cnt++;
	}
	bpf_iter_num_destroy(&it);

	res_overfetched_elem_cnt = cnt;

	return 0;
}

const volatile __s64 exp_fail_elem_cnt = 20 + 0;
__s64 res_fail_elem_cnt;

SEC("raw_tp/sys_enter")
int num_fail_elem_cnt(const void *ctx)
{
	struct bpf_iter_num it;
	int cnt = 0, *v, i;

	bpf_iter_num_new(&it, 100, 10);
	for (i = 0; i < 10; i++) {
		v = bpf_iter_num_next(&it);
		if (v)
			cnt++;
	}
	bpf_iter_num_destroy(&it);

	res_fail_elem_cnt = 20 + cnt;

	return 0;
}

char _license[] SEC("license") = "GPL";
