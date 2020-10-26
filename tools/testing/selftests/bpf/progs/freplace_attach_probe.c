// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <linux/ptrace.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define VAR_NUM 2

struct hmap_elem {
	struct bpf_spin_lock lock;
	int var[VAR_NUM];
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct hmap_elem);
} hash_map SEC(".maps");

SEC("freplace/handle_kprobe")
int new_handle_kprobe(struct pt_regs *ctx)
{
	struct hmap_elem zero = {}, *val;
	int key = 0;

	val = bpf_map_lookup_elem(&hash_map, &key);
	if (!val)
		return 1;
	/* spin_lock in hash map */
	bpf_spin_lock(&val->lock);
	val->var[0] = 99;
	bpf_spin_unlock(&val->lock);

	return 0;
}

char _license[] SEC("license") = "GPL";
