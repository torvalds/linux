// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define UDP_TEST_PORT 7777

void *bpf_cast_to_kern_ctx(void *) __ksym;
bool init_csum_partial = false;
bool final_csum_none = false;
bool broken_csum_start = false;

static unsigned int skb_headlen(const struct sk_buff *skb)
{
	return skb->len - skb->data_len;
}

static unsigned int skb_headroom(const struct sk_buff *skb)
{
	return skb->data - skb->head;
}

static int skb_checksum_start_offset(const struct sk_buff *skb)
{
	return skb->csum_start - skb_headroom(skb);
}

SEC("tc")
int decap_sanity(struct __sk_buff *skb)
{
	struct sk_buff *kskb;
	struct ipv6hdr ip6h;
	struct udphdr udph;
	int err;

	if (skb->protocol != __bpf_constant_htons(ETH_P_IPV6))
		return TC_ACT_SHOT;

	if (bpf_skb_load_bytes(skb, ETH_HLEN, &ip6h, sizeof(ip6h)))
		return TC_ACT_SHOT;

	if (ip6h.nexthdr != IPPROTO_UDP)
		return TC_ACT_SHOT;

	if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(ip6h), &udph, sizeof(udph)))
		return TC_ACT_SHOT;

	if (udph.dest != __bpf_constant_htons(UDP_TEST_PORT))
		return TC_ACT_SHOT;

	kskb = bpf_cast_to_kern_ctx(skb);
	init_csum_partial = (kskb->ip_summed == CHECKSUM_PARTIAL);
	err = bpf_skb_adjust_room(skb, -(s32)(ETH_HLEN + sizeof(ip6h) + sizeof(udph)),
				  1, BPF_F_ADJ_ROOM_FIXED_GSO);
	if (err)
		return TC_ACT_SHOT;
	final_csum_none = (kskb->ip_summed == CHECKSUM_NONE);
	if (kskb->ip_summed == CHECKSUM_PARTIAL &&
	    (unsigned int)skb_checksum_start_offset(kskb) >= skb_headlen(kskb))
		broken_csum_start = true;

	return TC_ACT_SHOT;
}

char __license[] SEC("license") = "GPL";
