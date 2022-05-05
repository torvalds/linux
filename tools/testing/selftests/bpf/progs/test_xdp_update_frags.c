// SPDX-License-Identifier: GPL-2.0
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <bpf/bpf_helpers.h>

int _version SEC("version") = 1;

SEC("xdp.frags")
int xdp_adjust_frags(struct xdp_md *xdp)
{
	__u8 *data_end = (void *)(long)xdp->data_end;
	__u8 *data = (void *)(long)xdp->data;
	__u8 val[16] = {};
	__u32 offset;
	int err;

	if (data + sizeof(__u32) > data_end)
		return XDP_DROP;

	offset = *(__u32 *)data;
	err = bpf_xdp_load_bytes(xdp, offset, val, sizeof(val));
	if (err < 0)
		return XDP_DROP;

	if (val[0] != 0xaa || val[15] != 0xaa) /* marker */
		return XDP_DROP;

	val[0] = 0xbb; /* update the marker */
	val[15] = 0xbb;
	err = bpf_xdp_store_bytes(xdp, offset, val, sizeof(val));
	if (err < 0)
		return XDP_DROP;

	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
