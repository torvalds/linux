// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Intel */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_XSKMAP);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} xsk SEC(".maps");

SEC("xdp") int xsk_def_prog(struct xdp_md *xdp)
{
	return bpf_redirect_map(&xsk, 0, XDP_DROP);
}

char _license[] SEC("license") = "GPL";
