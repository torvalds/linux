// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <time.h>
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_tcp_helpers.h"

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

static int timer_cb_ret1(void *map, int *key, struct bpf_timer *timer)
{
	if (bpf_get_smp_processor_id() % 2)
		return 1;
	else
		return 0;
}

SEC("fentry/bpf_fentry_test1")
__failure __msg("should have been in (0x0; 0x0)")
int BPF_PROG2(test_ret_1, int, a)
{
	int key = 0;
	struct bpf_timer *timer;

	timer = bpf_map_lookup_elem(&timer_map, &key);
	if (timer) {
		bpf_timer_init(timer, &timer_map, CLOCK_BOOTTIME);
		bpf_timer_set_callback(timer, timer_cb_ret1);
		bpf_timer_start(timer, 1000, 0);
	}

	return 0;
}
