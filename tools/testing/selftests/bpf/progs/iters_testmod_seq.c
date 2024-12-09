// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct bpf_iter_testmod_seq {
	u64 :64;
	u64 :64;
};

extern int bpf_iter_testmod_seq_new(struct bpf_iter_testmod_seq *it, s64 value, int cnt) __ksym;
extern s64 *bpf_iter_testmod_seq_next(struct bpf_iter_testmod_seq *it) __ksym;
extern s64 bpf_iter_testmod_seq_value(int blah, struct bpf_iter_testmod_seq *it) __ksym;
extern void bpf_iter_testmod_seq_destroy(struct bpf_iter_testmod_seq *it) __ksym;

const volatile __s64 exp_empty = 0 + 1;
__s64 res_empty;

SEC("raw_tp/sys_enter")
__success __log_level(2)
__msg("fp-16_w=iter_testmod_seq(ref_id=1,state=active,depth=0)")
__msg("fp-16=iter_testmod_seq(ref_id=1,state=drained,depth=0)")
__msg("call bpf_iter_testmod_seq_destroy")
int testmod_seq_empty(const void *ctx)
{
	__s64 sum = 0, *i;

	bpf_for_each(testmod_seq, i, 1000, 0) sum += *i;
	res_empty = 1 + sum;

	return 0;
}

const volatile __s64 exp_full = 1000000;
__s64 res_full;

SEC("raw_tp/sys_enter")
__success __log_level(2)
__msg("fp-16_w=iter_testmod_seq(ref_id=1,state=active,depth=0)")
__msg("fp-16=iter_testmod_seq(ref_id=1,state=drained,depth=0)")
__msg("call bpf_iter_testmod_seq_destroy")
int testmod_seq_full(const void *ctx)
{
	__s64 sum = 0, *i;

	bpf_for_each(testmod_seq, i, 1000, 1000) sum += *i;
	res_full = sum;

	return 0;
}

const volatile __s64 exp_truncated = 10 * 1000000;
__s64 res_truncated;

static volatile int zero = 0;

SEC("raw_tp/sys_enter")
__success __log_level(2)
__msg("fp-16_w=iter_testmod_seq(ref_id=1,state=active,depth=0)")
__msg("fp-16=iter_testmod_seq(ref_id=1,state=drained,depth=0)")
__msg("call bpf_iter_testmod_seq_destroy")
int testmod_seq_truncated(const void *ctx)
{
	__s64 sum = 0, *i;
	int cnt = zero;

	bpf_for_each(testmod_seq, i, 10, 2000000) {
		sum += *i;
		cnt++;
		if (cnt >= 1000000)
			break;
	}
	res_truncated = sum;

	return 0;
}

SEC("?raw_tp")
__failure
__msg("expected an initialized iter_testmod_seq as arg #1")
int testmod_seq_getter_before_bad(const void *ctx)
{
	struct bpf_iter_testmod_seq it;

	return bpf_iter_testmod_seq_value(0, &it);
}

SEC("?raw_tp")
__failure
__msg("expected an initialized iter_testmod_seq as arg #1")
int testmod_seq_getter_after_bad(const void *ctx)
{
	struct bpf_iter_testmod_seq it;
	s64 sum = 0, *v;

	bpf_iter_testmod_seq_new(&it, 100, 100);

	while ((v = bpf_iter_testmod_seq_next(&it))) {
		sum += *v;
	}

	bpf_iter_testmod_seq_destroy(&it);

	return sum + bpf_iter_testmod_seq_value(0, &it);
}

SEC("?socket")
__success __retval(1000000)
int testmod_seq_getter_good(const void *ctx)
{
	struct bpf_iter_testmod_seq it;
	s64 sum = 0, *v;

	bpf_iter_testmod_seq_new(&it, 100, 100);

	while ((v = bpf_iter_testmod_seq_next(&it))) {
		sum += *v;
	}

	sum *= bpf_iter_testmod_seq_value(0, &it);

	bpf_iter_testmod_seq_destroy(&it);

	return sum;
}

char _license[] SEC("license") = "GPL";
