// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define UDP_TEST_PORT 7777

void *bpf_cast_to_kern_ctx(void *) __ksym;

bool had_dst = false;
bool dst_cleared = false;

SEC("tc/egress")
int dst_clear(struct __sk_buff *skb)
{
	struct sk_buff *kskb;
	struct iphdr iph;
	struct udphdr udph;
	int err;

	if (skb->protocol != __bpf_constant_htons(ETH_P_IP))
		return TC_ACT_OK;

	if (bpf_skb_load_bytes(skb, ETH_HLEN, &iph, sizeof(iph)))
		return TC_ACT_OK;

	if (iph.protocol != IPPROTO_UDP)
		return TC_ACT_OK;

	if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(iph), &udph, sizeof(udph)))
		return TC_ACT_OK;

	if (udph.dest != __bpf_constant_htons(UDP_TEST_PORT))
		return TC_ACT_OK;

	kskb = bpf_cast_to_kern_ctx(skb);
	had_dst = (kskb->_skb_refdst != 0);

	/* Same-protocol encap (IPIP): protocol stays IPv4, but the dst
	 * from the original routing is no longer valid for the outer hdr.
	 */
	err = bpf_skb_adjust_room(skb, (s32)sizeof(struct iphdr),
				  BPF_ADJ_ROOM_MAC,
				  BPF_F_ADJ_ROOM_FIXED_GSO |
				  BPF_F_ADJ_ROOM_ENCAP_L3_IPV4);
	if (err)
		return TC_ACT_SHOT;

	dst_cleared = (kskb->_skb_refdst == 0);

	return TC_ACT_SHOT;
}

char __license[] SEC("license") = "GPL";
