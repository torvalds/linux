// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <linux/bpf.h>
#include "bpf_helpers.h"

#ifndef PERF_MAX_STACK_DEPTH
#define PERF_MAX_STACK_DEPTH         127
#endif

struct bpf_map_def SEC("maps") control_map = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(__u32),
	.value_size = sizeof(__u32),
	.max_entries = 1,
};

struct bpf_map_def SEC("maps") stackid_hmap = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(__u32),
	.value_size = sizeof(__u32),
	.max_entries = 16384,
};

struct bpf_map_def SEC("maps") stackmap = {
	.type = BPF_MAP_TYPE_STACK_TRACE,
	.key_size = sizeof(__u32),
	.value_size = sizeof(struct bpf_stack_build_id)
		* PERF_MAX_STACK_DEPTH,
	.max_entries = 128,
	.map_flags = BPF_F_STACK_BUILD_ID,
};

struct bpf_map_def SEC("maps") stack_amap = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(__u32),
	.value_size = sizeof(struct bpf_stack_build_id)
		* PERF_MAX_STACK_DEPTH,
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
