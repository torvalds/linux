// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Bytedance */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

__u64 percpu_array_elem_sum = 0;
__u64 percpu_hash_elem_sum = 0;
__u64 percpu_lru_hash_elem_sum = 0;
const volatile int nr_cpus;
const volatile int my_pid;

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} percpu_array_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(max_entries, 1);
	__type(key, __u64);
	__type(value, __u64);
} percpu_hash_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_PERCPU_HASH);
	__uint(max_entries, 1);
	__type(key, __u64);
	__type(value, __u64);
} percpu_lru_hash_map SEC(".maps");

struct read_percpu_elem_ctx {
	void *map;
	__u64 sum;
};

static int read_percpu_elem_callback(__u32 index, struct read_percpu_elem_ctx *ctx)
{
	__u64 key = 0;
	__u64 *value;

	value = bpf_map_lookup_percpu_elem(ctx->map, &key, index);
	if (value)
		ctx->sum += *value;
	return 0;
}

SEC("tp/syscalls/sys_enter_getuid")
int sysenter_getuid(const void *ctx)
{
	struct read_percpu_elem_ctx map_ctx;

	if (my_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	map_ctx.map = &percpu_array_map;
	map_ctx.sum = 0;
	bpf_loop(nr_cpus, read_percpu_elem_callback, &map_ctx, 0);
	percpu_array_elem_sum = map_ctx.sum;

	map_ctx.map = &percpu_hash_map;
	map_ctx.sum = 0;
	bpf_loop(nr_cpus, read_percpu_elem_callback, &map_ctx, 0);
	percpu_hash_elem_sum = map_ctx.sum;

	map_ctx.map = &percpu_lru_hash_map;
	map_ctx.sum = 0;
	bpf_loop(nr_cpus, read_percpu_elem_callback, &map_ctx, 0);
	percpu_lru_hash_elem_sum = map_ctx.sum;

	return 0;
}

char _license[] SEC("license") = "GPL";
