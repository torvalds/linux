// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <time.h>
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct elem {
	struct bpf_timer t;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} timer_map SEC(".maps");

__naked __noinline __used
static unsigned long timer_cb_ret_bad()
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"if r0 s> 1000 goto 1f;"
		"r0 = 0;"
	"1:"
		"goto +0;" /* checkpoint */
		/* async callback is expected to return 0, so branch above
		 * skipping r0 = 0; should lead to a failure, but if exit
		 * instruction doesn't enforce r0's precision, this callback
		 * will be successfully verified
		 */
		"exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_common
	);
}

SEC("fentry/bpf_fentry_test1")
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__failure
/* check that fallthrough code path marks r0 as precise */
__msg("mark_precise: frame0: regs=r0 stack= before")
__msg(": (85) call bpf_get_prandom_u32#7") /* anchor message */
/* check that branch code path marks r0 as precise */
__msg("mark_precise: frame0: regs=r0 stack= before ") __msg(": (85) call bpf_get_prandom_u32#7")
__msg("should have been in [0, 0]")
long BPF_PROG2(test_bad_ret, int, a)
{
	int key = 0;
	struct bpf_timer *timer;

	timer = bpf_map_lookup_elem(&timer_map, &key);
	if (timer) {
		bpf_timer_init(timer, &timer_map, CLOCK_BOOTTIME);
		bpf_timer_set_callback(timer, timer_cb_ret_bad);
		bpf_timer_start(timer, 1000, 0);
	}

	return 0;
}
