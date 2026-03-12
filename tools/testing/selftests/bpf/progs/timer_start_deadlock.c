// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define CLOCK_MONOTONIC 1

char _license[] SEC("license") = "GPL";

struct elem {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} timer_map SEC(".maps");

volatile int in_timer_start;
volatile int tp_called;

static int timer_cb(void *map, int *key, struct elem *value)
{
	return 0;
}

SEC("tp_btf/hrtimer_cancel")
int BPF_PROG(tp_hrtimer_cancel, struct hrtimer *hrtimer)
{
	struct bpf_timer *timer;
	int key = 0;

	if (!in_timer_start)
		return 0;

	tp_called = 1;
	timer = bpf_map_lookup_elem(&timer_map, &key);

	/*
	 * Call bpf_timer_start() from the tracepoint within hrtimer logic
	 * on the same timer to make sure it doesn't deadlock.
	 */
	bpf_timer_start(timer, 1000000000, 0);
	return 0;
}

SEC("syscall")
int start_timer(void *ctx)
{
	struct bpf_timer *timer;
	int key = 0;

	timer = bpf_map_lookup_elem(&timer_map, &key);
	/* claude may complain here that there is no NULL check. Ignoring it. */
	bpf_timer_init(timer, &timer_map, CLOCK_MONOTONIC);
	bpf_timer_set_callback(timer, timer_cb);

	/*
	 * call hrtimer_start() twice, so that 2nd call does
	 * remove_hrtimer() and trace_hrtimer_cancel() tracepoint.
	 */
	in_timer_start = 1;
	bpf_timer_start(timer, 1000000000, 0);
	bpf_timer_start(timer, 1000000000, 0);
	in_timer_start = 0;
	return 0;
}
