// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct inner_map {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 5);
	__type(key, int);
	__type(value, int);
} inner_map1 SEC(".maps");

struct outer_map {
	__uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
	__uint(max_entries, 3);
	__type(key, int);
	__array(values, struct inner_map);
} outer_map1 SEC(".maps") = {
	.values = {
		[2] = &inner_map1,
	},
};

SEC("raw_tp/sys_enter")
int handle__sys_enter(void *ctx)
{
	int outer_key = 2, inner_key = 3;
	int *val;
	void *map;

	map = bpf_map_lookup_elem(&outer_map1, &outer_key);
	if (!map)
		return 1;

	val = bpf_map_lookup_elem(map, &inner_key);
	if (!val)
		return 1;

	if (*val == 1)
		*val = 2;

	return 0;
}

char _license[] SEC("license") = "GPL";
