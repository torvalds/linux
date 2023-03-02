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

static unsigned int idx;

SEC("xdp") int xsk_def_prog(struct xdp_md *xdp)
{
	return bpf_redirect_map(&xsk, 0, XDP_DROP);
}

SEC("xdp") int xsk_xdp_drop(struct xdp_md *xdp)
{
	/* Drop every other packet */
	if (idx++ % 2)
		return XDP_DROP;

	return bpf_redirect_map(&xsk, 0, XDP_DROP);
}

char _license[] SEC("license") = "GPL";
