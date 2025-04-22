// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Intel */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/errno.h>
#include "xsk_xdp_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_XSKMAP);
	__uint(max_entries, 2);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} xsk SEC(".maps");

static unsigned int idx;
int adjust_value = 0;
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

SEC("xdp.frags") int xsk_xdp_adjust_tail(struct xdp_md *xdp)
{
	__u32 buff_len, curr_buff_len;
	int ret;

	buff_len = bpf_xdp_get_buff_len(xdp);
	if (buff_len == 0)
		return XDP_DROP;

	ret = bpf_xdp_adjust_tail(xdp, adjust_value);
	if (ret < 0) {
		/* Handle unsupported cases */
		if (ret == -EOPNOTSUPP) {
			/* Set adjust_value to -EOPNOTSUPP to indicate to userspace that this case
			 * is unsupported
			 */
			adjust_value = -EOPNOTSUPP;
			return bpf_redirect_map(&xsk, 0, XDP_DROP);
		}

		return XDP_DROP;
	}

	curr_buff_len = bpf_xdp_get_buff_len(xdp);
	if (curr_buff_len != buff_len + adjust_value)
		return XDP_DROP;

	if (curr_buff_len > buff_len) {
		__u32 *pkt_data = (void *)(long)xdp->data;
		__u32 len, words_to_end, seq_num;

		len = curr_buff_len - PKT_HDR_ALIGN;
		words_to_end = len / sizeof(*pkt_data) - 1;
		seq_num = words_to_end;

		/* Convert sequence number to network byte order. Store this in the last 4 bytes of
		 * the packet. Use 'adjust_value' to determine the position at the end of the
		 * packet for storing the sequence number.
		 */
		seq_num = __constant_htonl(words_to_end);
		bpf_xdp_store_bytes(xdp, curr_buff_len - sizeof(seq_num), &seq_num,
				    sizeof(seq_num));
	}

	return bpf_redirect_map(&xsk, 0, XDP_DROP);
}

char _license[] SEC("license") = "GPL";
