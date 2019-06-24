// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <linux/bpf.h>
#include "bpf_helpers.h"

#ifndef PERF_MAX_STACK_DEPTH
#define PERF_MAX_STACK_DEPTH         127
#endif

struct {
	__u32 type;
	__u32 max_entries;
	__u32 *key;
	__u32 *value;
} control_map SEC(".maps") = {
	.type = BPF_MAP_TYPE_ARRAY,
	.max_entries = 1,
};

struct {
	__u32 type;
	__u32 max_entries;
	__u32 *key;
	__u32 *value;
} stackid_hmap SEC(".maps") = {
	.type = BPF_MAP_TYPE_HASH,
	.max_entries = 16384,
};

typedef __u64 stack_trace_t[PERF_MAX_STACK_DEPTH];

struct {
	__u32 type;
	__u32 max_entries;
	__u32 key_size;
	__u32 value_size;
} stackmap SEC(".maps") = {
	.type = BPF_MAP_TYPE_STACK_TRACE,
	.max_entries = 16384,
	.key_size = sizeof(__u32),
	.value_size = sizeof(stack_trace_t),
};

struct {
	__u32 type;
	__u32 max_entries;
	__u32 *key;
	__u64 (*value)[PERF_MAX_STACK_DEPTH];
} stack_amap SEC(".maps") = {
	.type = BPF_MAP_TYPE_ARRAY,
	.max_entries = 16384,
};

/* taken from /sys/kernel/debug/tracing/events/sched/sched_switch/format */
struct sched_switch_args {
	unsigned long long pad;
	char prev_comm[16];
	int prev_pid;
	int prev_prio;
	long long prev_state;
	char next_comm[16];
	int next_pid;
	int next_prio;
};

SEC("tracepoint/sched/sched_switch")
int oncpu(struct sched_switch_args *ctx)
{
	__u32 max_len = PERF_MAX_STACK_DEPTH * sizeof(__u64);
	__u32 key = 0, val = 0, *value_p;
	void *stack_p;

	value_p = bpf_map_lookup_elem(&control_map, &key);
	if (value_p && *value_p)
		return 0; /* skip if non-zero *value_p */

	/* The size of stackmap and stackid_hmap should be the same */
	key = bpf_get_stackid(ctx, &stackmap, 0);
	if ((int)key >= 0) {
		bpf_map_update_elem(&stackid_hmap, &key, &val, 0);
		stack_p = bpf_map_lookup_elem(&stack_amap, &key);
		if (stack_p)
			bpf_get_stack(ctx, stack_p, max_len, 0);
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
__u32 _version SEC("version") = 1; /* ignored by tracepoints, required by libbpf.a */
