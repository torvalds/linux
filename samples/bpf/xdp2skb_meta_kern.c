/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018 Jesper Dangaard Brouer, Red Hat Inc.
 *
 * Example howto transfer info from XDP to SKB, e.g. skb->mark
 * -----------------------------------------------------------
 * This uses the XDP data_meta infrastructure, and is a cooperation
 * between two bpf-programs (1) XDP and (2) clsact at TC-ingress hook.
 *
 * Notice: This example does not use the BPF C-loader (bpf_load.c),
 * but instead rely on the iproute2 TC tool for loading BPF-objects.
 */
#include <uapi/linux/bpf.h>
#include <uapi/linux/pkt_cls.h>

#include "bpf_helpers.h"

/*
 * This struct is stored in the XDP 'data_meta' area, which is located
 * just in-front-of the raw packet payload data.  The meaning is
 * specific to these two BPF programs that use it as a communication
 * channel.  XDP adjust/increase the area via a bpf-helper, and TC use
 * boundary checks to see if data have been provided.
 *
 * The struct must be 4 byte aligned, which here is enforced by the
 * struct __attribute__((aligned(4))).
 */
struct meta_info {
	__u32 mark;
} __attribute__((aligned(4)));

SEC("xdp_mark")
int _xdp_mark(struct xdp_md *ctx)
{
	struct meta_info *meta;
	void *data, *data_end;
	int ret;

	/* Reserve space in-front of data pointer for our meta info.
	 * (Notice drivers not supporting data_meta will fail here!)
	 */
	ret = bpf_xdp_adjust_meta(ctx, -(int)sizeof(*meta));
	if (ret < 0)
		return XDP_ABORTED;

	/* Notice: Kernel-side verifier requires that loading of
	 * ctx->data MUST happen _after_ helper bpf_xdp_adjust_meta(),
	 * as pkt-data pointers are invalidated.  Helpers that require
	 * this are determined/marked by bpf_helper_changes_pkt_data()
	 */
	data = (void *)(unsigned long)ctx->data;

	/* Check data_meta have room for meta_info struct */
	meta = (void *)(unsigned long)ctx->data_meta;
	if (meta + 1 > data)
		return XDP_ABORTED;

	meta->mark = 42;

	return XDP_PASS;
}

SEC("tc_mark")
int _tc_mark(struct __sk_buff *ctx)
{
	void *data      = (void *)(unsigned long)ctx->data;
	void *data_end  = (void *)(unsigned long)ctx->data_end;
	void *data_meta = (void *)(unsigned long)ctx->data_meta;
	struct meta_info *meta = data_meta;

	/* Check XDP gave us some data_meta */
	if (meta + 1 > data) {
		ctx->mark = 41;
		 /* Skip "accept" if no data_meta is avail */
		return TC_ACT_OK;
	}

	/* Hint: See func tc_cls_act_is_valid_access() for BPF_WRITE access */
	ctx->mark = meta->mark; /* Transfer XDP-mark to SKB-mark */

	return TC_ACT_OK;
}

/* Manually attaching these programs:
export DEV=ixgbe2
export FILE=xdp2skb_meta_kern.o

# via TC command
tc qdisc del dev $DEV clsact 2> /dev/null
tc qdisc add dev $DEV clsact
tc filter  add dev $DEV ingress prio 1 handle 1 bpf da obj $FILE sec tc_mark
tc filter show dev $DEV ingress

# XDP via IP command:
ip link set dev $DEV xdp off
ip link set dev $DEV xdp obj $FILE sec xdp_mark

# Use iptable to "see" if SKBs are marked
iptables -I INPUT -p icmp -m mark --mark 41  # == 0x29
iptables -I INPUT -p icmp -m mark --mark 42  # == 0x2a

# Hint: catch XDP_ABORTED errors via
perf record -e xdp:*
perf script

*/
