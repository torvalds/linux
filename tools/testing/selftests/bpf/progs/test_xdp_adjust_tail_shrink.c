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

int _version SEC("version") = 1;

SEC("xdp_adjust_tail_shrink")
int _xdp_adjust_tail_shrink(struct xdp_md *xdp)
{
	void *data_end = (void *)(long)xdp->data_end;
	void *data = (void *)(long)xdp->data;
	int offset = 0;

	if (data_end - data == 54) /* sizeof(pkt_v4) */
		offset = 256; /* shrink too much */
	else
		offset = 20;
	if (bpf_xdp_adjust_tail(xdp, 0 - offset))
		return XDP_DROP;
	return XDP_TX;
}

char _license[] SEC("license") = "GPL";
