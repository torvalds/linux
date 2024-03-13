// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Cloudflare
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} map SEC(".maps");

SEC("sockops")
int bpf_sockmap(struct bpf_sock_ops *skops)
{
	__u32 key = 0;

	if (skops->sk)
		bpf_map_update_elem(&map, &key, skops->sk, 0);
	return 0;
}

char _license[] SEC("license") = "GPL";
