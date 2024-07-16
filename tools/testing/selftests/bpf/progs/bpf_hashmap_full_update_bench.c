// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Bytedance */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

#define MAX_ENTRIES 1000

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, u32);
	__type(value, u64);
	__uint(max_entries, MAX_ENTRIES);
} hash_map_bench SEC(".maps");

u64 __attribute__((__aligned__(256))) percpu_time[256];
u64 nr_loops;

static int loop_update_callback(__u32 index, u32 *key)
{
	u64 init_val = 1;

	bpf_map_update_elem(&hash_map_bench, key, &init_val, BPF_ANY);
	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int benchmark(void *ctx)
{
	u32 cpu = bpf_get_smp_processor_id();
	u32 key = cpu + MAX_ENTRIES;
	u64 start_time = bpf_ktime_get_ns();

	bpf_loop(nr_loops, loop_update_callback, &key, 0);
	percpu_time[cpu & 255] = bpf_ktime_get_ns() - start_time;
	return 0;
}
