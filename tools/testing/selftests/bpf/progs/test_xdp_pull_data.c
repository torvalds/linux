// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

int xdpf_sz;
int sinfo_sz;
int data_len;
int pull_len;

#define XDP_PACKET_HEADROOM 256

SEC("xdp.frags")
int xdp_find_sizes(struct xdp_md *ctx)
{
	xdpf_sz = sizeof(struct xdp_frame);
	sinfo_sz = __PAGE_SIZE - XDP_PACKET_HEADROOM -
		   (ctx->data_end - ctx->data);

	return XDP_PASS;
}

SEC("xdp.frags")
int xdp_pull_data_prog(struct xdp_md *ctx)
{
	__u8 *data_end = (void *)(long)ctx->data_end;
	__u8 *data = (void *)(long)ctx->data;
	__u8 *val_p;
	int err;

	if (data_len != data_end - data)
		return XDP_DROP;

	err = bpf_xdp_pull_data(ctx, pull_len);
	if (err)
		return XDP_DROP;

	val_p = (void *)(long)ctx->data + 1024;
	if (val_p + 1 > (void *)(long)ctx->data_end)
		return XDP_DROP;

	if (*val_p != 0xbb)
		return XDP_DROP;

	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
