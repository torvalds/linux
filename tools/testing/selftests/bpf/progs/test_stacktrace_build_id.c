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

typedef struct bpf_stack_build_id stack_trace_t[PERF_MAX_STACK_DEPTH];

struct {
	__u32 type;
	__u32 max_entries;
	__u32 map_flags;
	__u32 key_size;
	__u32 value_size;
} stackmap SEC(".maps") = {
	.type = BPF_MAP_TYPE_STACK_TRACE,
	.max_entries = 128,
	.map_flags = BPF_F_STACK_BUILD_ID,
	.key_size = sizeof(__u32),
	.value_size = sizeof(stack_trace_t),
};

struct {
	__u32 type;
	__u32 max_entries;
	__u32 *key;
	/* there seems to be a bug in kernel not handling typedef properly */
	struct bpf_stack_build_id (*value)[PERF_MAX_STACK_DEPTH];
} stack_amap SEC(".maps") = {
	.type = BPF_MAP_TYPE_ARRAY,
	.max_entries = 128,
};

/* taken from /sys/kernel/debug/tracing/events/random/urandom_read/format */
struct random_urandom_args {
	unsigned long long pad;
	int got_bits;
	int pool_left;
	int input_left;
};

SEC("tracepoint/random/urandom_read")
int oncpu(struct random_urandom_args *args)
{
	__u32 max_len = sizeof(struct bpf_stack_build_id)
			* PERF_MAX_STACK_DEPTH;
	__u32 key = 0, val = 0, *value_p;
	void *stack_p;

	value_p = bpf_map_lookup_elem(&control_map, &key);
	if (value_p && *value_p)
		return 0; /* skip if non-zero *value_p */

	/* The size of stackmap and stackid_hmap should be the same */
	key = bpf_get_stackid(args, &stackmap, BPF_F_USER_STACK);
	if ((int)key >= 0) {
		bpf_map_update_elem(&stackid_hmap, &key, &val, 0);
		stack_p = bpf_map_lookup_elem(&stack_amap, &key);
		if (stack_p)
			bpf_get_stack(args, stack_p, max_len,
				      BPF_F_USER_STACK | BPF_F_USER_BUILD_ID);
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
__u32 _version SEC("version") = 1; /* ignored by tracepoints, required by libbpf.a */
