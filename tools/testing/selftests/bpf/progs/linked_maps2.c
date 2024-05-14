// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* modifiers and typedefs are ignored when comparing key/value types */
typedef struct my_key { long x; } key_type;
typedef struct my_value { long x; } value_type;

extern struct {
	__uint(max_entries, 16);
	__type(key, key_type);
	__type(value, value_type);
	__uint(type, BPF_MAP_TYPE_HASH);
} map1 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 8);
} map2 SEC(".maps");

/* this definition will lose, but it has to exactly match the winner */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 16);
} map_weak __weak SEC(".maps");

int output_first2;
int output_second2;
int output_weak2;

SEC("raw_tp/sys_enter")
int BPF_PROG(handler_enter2)
{
	/* update values with key = 2 */
	int key = 2, val = 2;
	key_type key_struct = { .x = 2 };
	value_type val_struct = { .x = 2000 };

	bpf_map_update_elem(&map1, &key_struct, &val_struct, 0);
	bpf_map_update_elem(&map2, &key, &val, 0);
	bpf_map_update_elem(&map_weak, &key, &val, 0);

	return 0;
}

SEC("raw_tp/sys_exit")
int BPF_PROG(handler_exit2)
{
	/* lookup values with key = 1, set in another file */
	int key = 1, *val;
	key_type key_struct = { .x = 1 };
	value_type *value_struct;

	value_struct = bpf_map_lookup_elem(&map1, &key_struct);
	if (value_struct)
		output_first2 = value_struct->x;

	val = bpf_map_lookup_elem(&map2, &key);
	if (val)
		output_second2 = *val;

	val = bpf_map_lookup_elem(&map_weak, &key);
	if (val)
		output_weak2 = *val;

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
