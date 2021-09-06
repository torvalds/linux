// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 3);
	__type(key, __u32);
	__type(value, __u64);
} hashmap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} percpu_map SEC(".maps");

struct callback_ctx {
	struct __sk_buff *ctx;
	int input;
	int output;
};

static __u64
check_hash_elem(struct bpf_map *map, __u32 *key, __u64 *val,
		struct callback_ctx *data)
{
	struct __sk_buff *skb = data->ctx;
	__u32 k;
	__u64 v;

	if (skb) {
		k = *key;
		v = *val;
		if (skb->len == 10000 && k == 10 && v == 10)
			data->output = 3; /* impossible path */
		else
			data->output = 4;
	} else {
		data->output = data->input;
		bpf_map_delete_elem(map, key);
	}

	return 0;
}

__u32 cpu = 0;
__u32 percpu_called = 0;
__u32 percpu_key = 0;
__u64 percpu_val = 0;
int percpu_output = 0;

static __u64
check_percpu_elem(struct bpf_map *map, __u32 *key, __u64 *val,
		  struct callback_ctx *unused)
{
	struct callback_ctx data;

	percpu_called++;
	cpu = bpf_get_smp_processor_id();
	percpu_key = *key;
	percpu_val = *val;

	data.ctx = 0;
	data.input = 100;
	data.output = 0;
	bpf_for_each_map_elem(&hashmap, check_hash_elem, &data, 0);
	percpu_output = data.output;

	return 0;
}

int hashmap_output = 0;
int hashmap_elems = 0;
int percpu_map_elems = 0;

SEC("classifier")
int test_pkt_access(struct __sk_buff *skb)
{
	struct callback_ctx data;

	data.ctx = skb;
	data.input = 10;
	data.output = 0;
	hashmap_elems = bpf_for_each_map_elem(&hashmap, check_hash_elem, &data, 0);
	hashmap_output = data.output;

	percpu_map_elems = bpf_for_each_map_elem(&percpu_map, check_percpu_elem,
						 (void *)0, 0);
	return 0;
}
