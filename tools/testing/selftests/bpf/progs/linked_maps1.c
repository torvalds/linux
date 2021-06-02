// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct my_key { long x; };
struct my_value { long x; };

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, struct my_key);
	__type(value, struct my_value);
	__uint(max_entries, 16);
} map1 SEC(".maps");

 /* Matches map2 definition in linked_maps2.c. Order of the attributes doesn't
  * matter.
  */
typedef struct {
	__uint(max_entries, 8);
	__type(key, int);
	__type(value, int);
	__uint(type, BPF_MAP_TYPE_ARRAY);
} map2_t;

extern map2_t map2 SEC(".maps");

/* This should be the winning map definition, but we have no way of verifying,
 * so we just make sure that it links and works without errors
 */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 16);
} map_weak __weak SEC(".maps");

int output_first1;
int output_second1;
int output_weak1;

SEC("raw_tp/sys_enter")
int BPF_PROG(handler_enter1)
{
	/* update values with key = 1 */
	int key = 1, val = 1;
	struct my_key key_struct = { .x = 1 };
	struct my_value val_struct = { .x = 1000 };

	bpf_map_update_elem(&map1, &key_struct, &val_struct, 0);
	bpf_map_update_elem(&map2, &key, &val, 0);
	bpf_map_update_elem(&map_weak, &key, &val, 0);

	return 0;
}

SEC("raw_tp/sys_exit")
int BPF_PROG(handler_exit1)
{
	/* lookup values with key = 2, set in another file */
	int key = 2, *val;
	struct my_key key_struct = { .x = 2 };
	struct my_value *value_struct;

	value_struct = bpf_map_lookup_elem(&map1, &key_struct);
	if (value_struct)
		output_first1 = value_struct->x;

	val = bpf_map_lookup_elem(&map2, &key);
	if (val)
		output_second1 = *val;

	val = bpf_map_lookup_elem(&map_weak, &key);
	if (val)
		output_weak1 = *val;
	
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
