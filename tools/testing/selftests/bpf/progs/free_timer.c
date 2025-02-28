// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025. Huawei Technologies Co., Ltd */
#include <linux/bpf.h>
#include <time.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

#define MAX_ENTRIES 8

struct map_value {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, MAX_ENTRIES);
} map SEC(".maps");

static int timer_cb(void *map, void *key, struct map_value *value)
{
	volatile int sum = 0;
	int i;

	bpf_for(i, 0, 1024 * 1024) sum += i;

	return 0;
}

static int start_cb(int key)
{
	struct map_value *value;

	value = bpf_map_lookup_elem(&map, (void *)&key);
	if (!value)
		return 0;

	bpf_timer_init(&value->timer, &map, CLOCK_MONOTONIC);
	bpf_timer_set_callback(&value->timer, timer_cb);
	/* Hope 100us will be enough to wake-up and run the overwrite thread */
	bpf_timer_start(&value->timer, 100000, BPF_F_TIMER_CPU_PIN);

	return 0;
}

static int overwrite_cb(int key)
{
	struct map_value zero = {};

	/* Free the timer which may run on other CPU */
	bpf_map_update_elem(&map, (void *)&key, &zero, BPF_ANY);

	return 0;
}

SEC("syscall")
int BPF_PROG(start_timer)
{
	bpf_loop(MAX_ENTRIES, start_cb, NULL, 0);
	return 0;
}

SEC("syscall")
int BPF_PROG(overwrite_timer)
{
	bpf_loop(MAX_ENTRIES, overwrite_cb, NULL, 0);
	return 0;
}

char _license[] SEC("license") = "GPL";
