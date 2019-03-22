// SPDX-License-Identifier: GPL-2.0

/* In-place tunneling */

#include <linux/stddef.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/pkt_cls.h>
#include <linux/types.h>

#include "bpf_endian.h"
#include "bpf_helpers.h"

static const int cfg_port = 8000;

static __always_inline void set_ipv4_csum(struct iphdr *iph)
{
	__u16 *iph16 = (__u16 *)iph;
	__u32 csum;
	int i;

	iph->check = 0;

#pragma clang loop unroll(full)
	for (i = 0, csum = 0; i < sizeof(*iph) >> 1; i++)
		csum += *iph16++;

	iph->check = ~((csum & 0xffff) + (csum >> 16));
}

static int encap_ipv4(struct __sk_buff *skb)
{
	struct iphdr iph_outer, iph_inner;
	struct tcphdr tcph;

	if (bpf_skb_load_bytes(skb, ETH_HLEN, &iph_inner,
			       sizeof(iph_inner)) < 0)
		return TC_ACT_OK;

	/* filter only packets we want */
	if (iph_inner.ihl != 5 || iph_inner.protocol != IPPROTO_TCP)
		return TC_ACT_OK;

	if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(iph_inner),
			       &tcph, sizeof(tcph)) < 0)
		return TC_ACT_OK;

	if (tcph.dest != __bpf_constant_htons(cfg_port))
		return TC_ACT_OK;

	/* add room between mac and network header */
	if (bpf_skb_adjust_room(skb, sizeof(iph_outer), BPF_ADJ_ROOM_NET, 0))
		return TC_ACT_SHOT;

	/* prepare new outer network header */
	iph_outer = iph_inner;
	iph_outer.protocol = IPPROTO_IPIP;
	iph_outer.tot_len = bpf_htons(sizeof(iph_outer) +
				      bpf_htons(iph_outer.tot_len));
	set_ipv4_csum(&iph_outer);

	/* store new outer network header */
	if (bpf_skb_store_bytes(skb, ETH_HLEN, &iph_outer, sizeof(iph_outer),
				BPF_F_INVALIDATE_HASH) < 0)
		return TC_ACT_SHOT;

	/* bpf_skb_adjust_room has moved header to start of room: restore */
	if (bpf_skb_store_bytes(skb, ETH_HLEN + sizeof(iph_outer),
				&iph_inner, sizeof(iph_inner),
				BPF_F_INVALIDATE_HASH) < 0)
		return TC_ACT_SHOT;

	return TC_ACT_OK;
}

static int encap_ipv6(struct __sk_buff *skb)
{
	struct ipv6hdr iph_outer, iph_inner;
	struct tcphdr tcph;

	if (bpf_skb_load_bytes(skb, ETH_HLEN, &iph_inner,
			       sizeof(iph_inner)) < 0)
		return TC_ACT_OK;

	/* filter only packets we want */
	if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(iph_inner),
			       &tcph, sizeof(tcph)) < 0)
		return TC_ACT_OK;

	if (tcph.dest != __bpf_constant_htons(cfg_port))
		return TC_ACT_OK;

	/* add room between mac and network header */
	if (bpf_skb_adjust_room(skb, sizeof(iph_outer), BPF_ADJ_ROOM_NET, 0))
		return TC_ACT_SHOT;

	/* prepare new outer network header */
	iph_outer = iph_inner;
	iph_outer.nexthdr = IPPROTO_IPV6;
	iph_outer.payload_len = bpf_htons(sizeof(iph_outer) +
					  bpf_ntohs(iph_outer.payload_len));

	/* store new outer network header */
	if (bpf_skb_store_bytes(skb, ETH_HLEN, &iph_outer, sizeof(iph_outer),
				BPF_F_INVALIDATE_HASH) < 0)
		return TC_ACT_SHOT;

	/* bpf_skb_adjust_room has moved header to start of room: restore */
	if (bpf_skb_store_bytes(skb, ETH_HLEN + sizeof(iph_outer),
				&iph_inner, sizeof(iph_inner),
				BPF_F_INVALIDATE_HASH) < 0)
		return TC_ACT_SHOT;

	return TC_ACT_OK;
}

SEC("encap")
int encap_f(struct __sk_buff *skb)
{
	switch (skb->protocol) {
	case __bpf_constant_htons(ETH_P_IP):
		return encap_ipv4(skb);
	case __bpf_constant_htons(ETH_P_IPV6):
		return encap_ipv6(skb);
	default:
		/* does not match, ignore */
		return TC_ACT_OK;
	}
}

static int decap_internal(struct __sk_buff *skb, int off, int len)
{
	char buf[sizeof(struct ipv6hdr)];

	if (bpf_skb_load_bytes(skb, off + len, &buf, len) < 0)
		return TC_ACT_OK;

	if (bpf_skb_adjust_room(skb, -len, BPF_ADJ_ROOM_NET, 0))
		return TC_ACT_SHOT;

	/* bpf_skb_adjust_room has moved outer over inner header: restore */
	if (bpf_skb_store_bytes(skb, off, buf, len, BPF_F_INVALIDATE_HASH) < 0)
		return TC_ACT_SHOT;

	return TC_ACT_OK;
}

static int decap_ipv4(struct __sk_buff *skb)
{
	struct iphdr iph_outer;

	if (bpf_skb_load_bytes(skb, ETH_HLEN, &iph_outer,
			       sizeof(iph_outer)) < 0)
		return TC_ACT_OK;

	if (iph_outer.ihl != 5 || iph_outer.protocol != IPPROTO_IPIP)
		return TC_ACT_OK;

	return decap_internal(skb, ETH_HLEN, sizeof(iph_outer));
}

static int decap_ipv6(struct __sk_buff *skb)
{
	struct ipv6hdr iph_outer;

	if (bpf_skb_load_bytes(skb, ETH_HLEN, &iph_outer,
			       sizeof(iph_outer)) < 0)
		return TC_ACT_OK;

	if (iph_outer.nexthdr != IPPROTO_IPV6)
		return TC_ACT_OK;

	return decap_internal(skb, ETH_HLEN, sizeof(iph_outer));
}

SEC("decap")
int decap_f(struct __sk_buff *skb)
{
	switch (skb->protocol) {
	case __bpf_constant_htons(ETH_P_IP):
		return decap_ipv4(skb);
	case __bpf_constant_htons(ETH_P_IPV6):
		return decap_ipv6(skb);
	default:
		/* does not match, ignore */
		return TC_ACT_OK;
	}
}

char __license[] SEC("license") = "GPL";
