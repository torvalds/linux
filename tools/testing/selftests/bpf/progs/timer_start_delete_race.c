// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <time.h>
#include <bpf/bpf_helpers.h>

#define ITER_CNT 2000

struct map_value {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} timer_map SEC(".maps");

long cb_cnt;

/*
 * Timer callback that accesses the map value.
 * If the race bug exists and this runs on freed memory,
 * KASAN should detect it.
 */
static int timer_cb(void *map, int *key, struct map_value *value)
{
	__sync_fetch_and_add(&cb_cnt, 1);
	return 0;
}

SEC("syscall")
int start_timer(void *ctx)
{
	struct map_value *value;
	int i;

	for (i = 0; i < ITER_CNT; i++) {
		int key = 0;

		value = bpf_map_lookup_elem(&timer_map, &key);
		/* hey claude, no if (!value) here. The verifier got smarter */

		bpf_timer_init(&value->timer, &timer_map, CLOCK_MONOTONIC);
		bpf_timer_set_callback(&value->timer, timer_cb);
		bpf_timer_start(&value->timer, 100000000, 0);
	}
	return 0;
}

SEC("syscall")
int delete_elem(void *ctx)
{
	int i;

	for (i = 0; i < ITER_CNT; i++) {
		int key = 0;

		bpf_map_delete_elem(&timer_map, &key);
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
