// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_experimental.h"

char _license[] SEC("license") = "GPL";

#define CLOCK_MONOTONIC		1

int preempt_count;
int in_interrupt;
int in_interrupt_cb;

struct elem {
	struct bpf_timer t;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} array SEC(".maps");

static int timer_in_interrupt(void *map, int *key, struct bpf_timer *timer)
{
	preempt_count = get_preempt_count();
	in_interrupt_cb = bpf_in_interrupt();
	return 0;
}

SEC("fentry/bpf_fentry_test1")
int BPF_PROG(test_timer_interrupt)
{
	struct bpf_timer *timer;
	int key = 0;

	timer = bpf_map_lookup_elem(&array, &key);
	if (!timer)
		return 0;

	in_interrupt = bpf_in_interrupt();
	bpf_timer_init(timer, &array, CLOCK_MONOTONIC);
	bpf_timer_set_callback(timer, timer_in_interrupt);
	bpf_timer_start(timer, 0, 0);
	return 0;
}
