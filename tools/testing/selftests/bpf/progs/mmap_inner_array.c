// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct inner_array_type {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(map_flags, BPF_F_MMAPABLE);
	__type(key, __u32);
	__type(value, __u64);
	__uint(max_entries, 1);
} inner_array SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
	__uint(key_size, 4);
	__uint(value_size, 4);
	__uint(max_entries, 1);
	__array(values, struct inner_array_type);
} outer_map SEC(".maps");

int pid = 0;
__u64 match_value = 0x13572468;
bool done = false;
bool pid_match = false;
bool outer_map_match = false;

SEC("fentry/" SYS_PREFIX "sys_nanosleep")
int add_to_list_in_inner_array(void *ctx)
{
	__u32 curr_pid, zero = 0;
	struct bpf_map *map;
	__u64 *value;

	curr_pid = (u32)bpf_get_current_pid_tgid();
	if (done || curr_pid != pid)
		return 0;

	pid_match = true;
	map = bpf_map_lookup_elem(&outer_map, &curr_pid);
	if (!map)
		return 0;

	outer_map_match = true;
	value = bpf_map_lookup_elem(map, &zero);
	if (!value)
		return 0;

	*value = match_value;
	done = true;
	return 0;
}
