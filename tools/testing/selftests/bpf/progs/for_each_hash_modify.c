// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Intel Corporation */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 128);
	__type(key, __u64);
	__type(value, __u64);
} hashmap SEC(".maps");

static int cb(struct bpf_map *map, __u64 *key, __u64 *val, void *arg)
{
	bpf_map_delete_elem(map, key);
	bpf_map_update_elem(map, key, val, 0);
	return 0;
}

SEC("tc")
int test_pkt_access(struct __sk_buff *skb)
{
	(void)skb;

	bpf_for_each_map_elem(&hashmap, cb, NULL, 0);

	return 0;
}
