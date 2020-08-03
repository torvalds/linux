/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Facebook */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct inner_map {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} inner_map1 SEC(".maps"),
  inner_map2 SEC(".maps");

struct outer_arr {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 3);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
	/* it's possible to use anonymous struct as inner map definition here */
	__array(values, struct {
		__uint(type, BPF_MAP_TYPE_ARRAY);
		/* changing max_entries to 2 will fail during load
		 * due to incompatibility with inner_map definition */
		__uint(max_entries, 1);
		__type(key, int);
		__type(value, int);
	});
} outer_arr SEC(".maps") = {
	/* (void *) cast is necessary because we didn't use `struct inner_map`
	 * in __inner(values, ...)
	 * Actually, a conscious effort is required to screw up initialization
	 * of inner map slots, which is a great thing!
	 */
	.values = { (void *)&inner_map1, 0, (void *)&inner_map2 },
};

struct outer_hash {
	__uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
	__uint(max_entries, 5);
	__uint(key_size, sizeof(int));
	/* Here everything works flawlessly due to reuse of struct inner_map
	 * and compiler will complain at the attempt to use non-inner_map
	 * references below. This is great experience.
	 */
	__array(values, struct inner_map);
} outer_hash SEC(".maps") = {
	.values = {
		[0] = &inner_map2,
		[4] = &inner_map1,
	},
};

int input = 0;

SEC("raw_tp/sys_enter")
int handle__sys_enter(void *ctx)
{
	struct inner_map *inner_map;
	int key = 0, val;

	inner_map = bpf_map_lookup_elem(&outer_arr, &key);
	if (!inner_map)
		return 1;
	val = input;
	bpf_map_update_elem(inner_map, &key, &val, 0);

	inner_map = bpf_map_lookup_elem(&outer_hash, &key);
	if (!inner_map)
		return 1;
	val = input + 1;
	bpf_map_update_elem(inner_map, &key, &val, 0);

	return 0;
}

char _license[] SEC("license") = "GPL";
