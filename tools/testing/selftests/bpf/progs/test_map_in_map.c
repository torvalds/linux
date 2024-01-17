// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018 Facebook */
#include <stddef.h>
#include <linux/bpf.h>
#include <linux/types.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 1);
	__uint(map_flags, 0);
	__type(key, __u32);
	__type(value, __u32);
} mim_array SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
	__uint(max_entries, 1);
	__uint(map_flags, 0);
	__type(key, int);
	__type(value, __u32);
} mim_hash SEC(".maps");

/* The following three maps are used to test
 * perf_event_array map can be an inner
 * map of hash/array_of_maps.
 */
struct perf_event_array {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__type(key, __u32);
	__type(value, __u32);
} inner_map0 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 1);
	__type(key, __u32);
	__array(values, struct perf_event_array);
} mim_array_pe SEC(".maps") = {
	.values = {&inner_map0}};

struct {
	__uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
	__uint(max_entries, 1);
	__type(key, __u32);
	__array(values, struct perf_event_array);
} mim_hash_pe SEC(".maps") = {
	.values = {&inner_map0}};

SEC("xdp")
int xdp_mimtest0(struct xdp_md *ctx)
{
	int value = 123;
	int *value_p;
	int key = 0;
	void *map;

	map = bpf_map_lookup_elem(&mim_array, &key);
	if (!map)
		return XDP_DROP;

	bpf_map_update_elem(map, &key, &value, 0);
	value_p = bpf_map_lookup_elem(map, &key);
	if (!value_p || *value_p != 123)
		return XDP_DROP;

	map = bpf_map_lookup_elem(&mim_hash, &key);
	if (!map)
		return XDP_DROP;

	bpf_map_update_elem(map, &key, &value, 0);

	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
