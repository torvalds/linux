// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 3);
	__type(key, __u32);
	__type(value, __u64);
} arraymap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} percpu_map SEC(".maps");

struct callback_ctx {
	int output;
};

const volatile int bypass_unused = 1;

static __u64
unused_subprog(struct bpf_map *map, __u32 *key, __u64 *val,
	       struct callback_ctx *data)
{
	data->output = 0;
	return 1;
}

static __u64
check_array_elem(struct bpf_map *map, __u32 *key, __u64 *val,
		 struct callback_ctx *data)
{
	data->output += *val;
	if (*key == 1)
		return 1; /* stop the iteration */
	return 0;
}

__u32 cpu = 0;
__u64 percpu_val = 0;

static __u64
check_percpu_elem(struct bpf_map *map, __u32 *key, __u64 *val,
		  struct callback_ctx *data)
{
	cpu = bpf_get_smp_processor_id();
	percpu_val = *val;
	return 0;
}

u32 arraymap_output = 0;

SEC("tc")
int test_pkt_access(struct __sk_buff *skb)
{
	struct callback_ctx data;

	data.output = 0;
	bpf_for_each_map_elem(&arraymap, check_array_elem, &data, 0);
	if (!bypass_unused)
		bpf_for_each_map_elem(&arraymap, unused_subprog, &data, 0);
	arraymap_output = data.output;

	bpf_for_each_map_elem(&percpu_map, check_percpu_elem, (void *)0, 0);
	return 0;
}
