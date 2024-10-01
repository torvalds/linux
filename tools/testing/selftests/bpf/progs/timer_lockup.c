// SPDX-License-Identifier: GPL-2.0

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
} timer1_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} timer2_map SEC(".maps");

int timer1_err;
int timer2_err;

static int timer_cb1(void *map, int *k, struct elem *v)
{
	struct bpf_timer *timer;
	int key = 0;

	timer = bpf_map_lookup_elem(&timer2_map, &key);
	if (timer)
		timer2_err = bpf_timer_cancel(timer);

	return 0;
}

static int timer_cb2(void *map, int *k, struct elem *v)
{
	struct bpf_timer *timer;
	int key = 0;

	timer = bpf_map_lookup_elem(&timer1_map, &key);
	if (timer)
		timer1_err = bpf_timer_cancel(timer);

	return 0;
}

SEC("tc")
int timer1_prog(void *ctx)
{
	struct bpf_timer *timer;
	int key = 0;

	timer = bpf_map_lookup_elem(&timer1_map, &key);
	if (timer) {
		bpf_timer_init(timer, &timer1_map, CLOCK_BOOTTIME);
		bpf_timer_set_callback(timer, timer_cb1);
		bpf_timer_start(timer, 1, BPF_F_TIMER_CPU_PIN);
	}

	return 0;
}

SEC("tc")
int timer2_prog(void *ctx)
{
	struct bpf_timer *timer;
	int key = 0;

	timer = bpf_map_lookup_elem(&timer2_map, &key);
	if (timer) {
		bpf_timer_init(timer, &timer2_map, CLOCK_BOOTTIME);
		bpf_timer_set_callback(timer, timer_cb2);
		bpf_timer_start(timer, 1, BPF_F_TIMER_CPU_PIN);
	}

	return 0;
}
