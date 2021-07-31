// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Isovalent, Inc. */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct inner {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, __u32);
	__type(value, int);
	__uint(max_entries, 4);
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 0); /* This will make map creation to fail */
	__uint(key_size, sizeof(__u32));
	__array(values, struct inner);
} mim SEC(".maps");

SEC("xdp")
int xdp_noop0(struct xdp_md *ctx)
{
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
