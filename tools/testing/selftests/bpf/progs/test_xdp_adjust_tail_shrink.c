// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <bpf/bpf_helpers.h>

SEC("xdp")
int _xdp_adjust_tail_shrink(struct xdp_md *xdp)
{
	__u8 *data_end = (void *)(long)xdp->data_end;
	__u8 *data = (void *)(long)xdp->data;
	int offset = 0;

	switch (bpf_xdp_get_buff_len(xdp)) {
	case 54:
		/* sizeof(pkt_v4) */
		offset = 256; /* shrink too much */
		break;
	case 9000:
		/* non-linear buff test cases */
		if (data + 1 > data_end)
			return XDP_DROP;

		switch (data[0]) {
		case 0:
			offset = 10;
			break;
		case 1:
			offset = 4100;
			break;
		case 2:
			offset = 8200;
			break;
		default:
			return XDP_DROP;
		}
		break;
	default:
		offset = 20;
		break;
	}
	if (bpf_xdp_adjust_tail(xdp, 0 - offset))
		return XDP_DROP;
	return XDP_TX;
}

char _license[] SEC("license") = "GPL";
