// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct bpf_map;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, __u32);
	__type(value, __u32);
	__uint(max_entries, 1000);
} map_random_data SEC(".maps");

struct map_bloom_type {
	__uint(type, BPF_MAP_TYPE_BLOOM_FILTER);
	__type(value, __u32);
	__uint(max_entries, 10000);
	__uint(map_extra, 5);
} map_bloom SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 1);
	__array(values, struct map_bloom_type);
} outer_map SEC(".maps");

struct callback_ctx {
	struct bpf_map *map;
};

int error = 0;

static __u64
check_elem(struct bpf_map *map, __u32 *key, __u32 *val,
	   struct callback_ctx *data)
{
	int err;

	err = bpf_map_peek_elem(data->map, val);
	if (err) {
		error |= 1;
		return 1; /* stop the iteration */
	}

	return 0;
}

SEC("fentry/__x64_sys_getpgid")
int inner_map(void *ctx)
{
	struct bpf_map *inner_map;
	struct callback_ctx data;
	int key = 0;

	inner_map = bpf_map_lookup_elem(&outer_map, &key);
	if (!inner_map) {
		error |= 2;
		return 0;
	}

	data.map = inner_map;
	bpf_for_each_map_elem(&map_random_data, check_elem, &data, 0);

	return 0;
}

SEC("fentry/__x64_sys_getpgid")
int check_bloom(void *ctx)
{
	struct callback_ctx data;

	data.map = (struct bpf_map *)&map_bloom;
	bpf_for_each_map_elem(&map_random_data, check_elem, &data, 0);

	return 0;
}
