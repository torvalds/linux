// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 Bytedance

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

int percpu_array_elem_val = 0;
int percpu_hash_elem_val = 0;
int percpu_lru_hash_elem_val = 0;

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} percpu_array_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} percpu_hash_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_PERCPU_HASH);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} percpu_lru_hash_map SEC(".maps");

SEC("tp/syscalls/sys_enter_getuid")
int sysenter_getuid(const void *ctx)
{
	__u32 key = 0;
	__u32 cpu = 0;
	__u32 *value;

	value = bpf_map_lookup_percpu_elem(&percpu_array_map, &key, cpu);
	if (value)
		percpu_array_elem_val = *value;

	value = bpf_map_lookup_percpu_elem(&percpu_hash_map, &key, cpu);
	if (value)
		percpu_hash_elem_val = *value;

	value = bpf_map_lookup_percpu_elem(&percpu_lru_hash_map, &key, cpu);
	if (value)
		percpu_lru_hash_elem_val = *value;

	return 0;
}

char _license[] SEC("license") = "GPL";
