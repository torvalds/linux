// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Intel */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include "xsk_xdp_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_XSKMAP);
	__uint(max_entries, 2);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} xsk SEC(".maps");

static unsigned int idx;
int count = 0;

SEC("xdp.frags") int xsk_def_prog(struct xdp_md *xdp)
{
	return bpf_redirect_map(&xsk, 0, XDP_DROP);
}

SEC("xdp.frags") int xsk_xdp_drop(struct xdp_md *xdp)
{
	/* Drop every other packet */
	if (idx++ % 2)
		return XDP_DROP;

	return bpf_redirect_map(&xsk, 0, XDP_DROP);
}

SEC("xdp.frags") int xsk_xdp_populate_metadata(struct xdp_md *xdp)
{
	void *data, *data_meta;
	struct xdp_info *meta;
	int err;

	/* Reserve enough for all custom metadata. */
	err = bpf_xdp_adjust_meta(xdp, -(int)sizeof(struct xdp_info));
	if (err)
		return XDP_DROP;

	data = (void *)(long)xdp->data;
	data_meta = (void *)(long)xdp->data_meta;

	if (data_meta + sizeof(struct xdp_info) > data)
		return XDP_DROP;

	meta = data_meta;
	meta->count = count++;

	return bpf_redirect_map(&xsk, 0, XDP_DROP);
}

SEC("xdp") int xsk_xdp_shared_umem(struct xdp_md *xdp)
{
	void *data = (void *)(long)xdp->data;
	void *data_end = (void *)(long)xdp->data_end;
	struct ethhdr *eth = data;

	if (eth + 1 > data_end)
		return XDP_DROP;

	/* Redirecting packets based on the destination MAC address */
	idx = ((unsigned int)(eth->h_dest[5])) / 2;
	if (idx > MAX_SOCKETS)
		return XDP_DROP;

	return bpf_redirect_map(&xsk, idx, XDP_DROP);
}

char _license[] SEC("license") = "GPL";
