// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

int err;

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 4096);
} ringbuf SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} array_map SEC(".maps");

SEC("cgroup_skb/egress")
int program(struct __sk_buff *skb)
{
	err = 0;
	return 0;
}
