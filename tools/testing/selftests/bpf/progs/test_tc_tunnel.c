// SPDX-License-Identifier: GPL-2.0

/* In-place tunneling */

#include <stdbool.h>
#include <string.h>

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

struct grev4hdr {
	struct iphdr ip;
	__be16 flags;
	__be16 protocol;
} __attribute__((packed));

struct grev6hdr {
	struct ipv6hdr ip;
	__be16 flags;
	__be16 protocol;
} __attribute__((packed));

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

static __always_inline int encap_ipv4(struct __sk_buff *skb, bool with_gre)
{
	struct grev4hdr h_outer;
	struct iphdr iph_inner;
	struct tcphdr tcph;
	__u64 flags;
	int olen;

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

	flags = BPF_F_ADJ_ROOM_FIXED_GSO;
	olen = with_gre ? sizeof(h_outer) : sizeof(h_outer.ip);

	/* add room between mac and network header */
	if (bpf_skb_adjust_room(skb, olen, BPF_ADJ_ROOM_MAC, flags))
		return TC_ACT_SHOT;

	/* prepare new outer network header */
	h_outer.ip = iph_inner;
	h_outer.ip.tot_len = bpf_htons(olen +
				      bpf_htons(h_outer.ip.tot_len));
	if (with_gre) {
		h_outer.ip.protocol = IPPROTO_GRE;
		h_outer.protocol = bpf_htons(ETH_P_IP);
		h_outer.flags = 0;
	} else {
		h_outer.ip.protocol = IPPROTO_IPIP;
	}

	set_ipv4_csum((void *)&h_outer.ip);

	/* store new outer network header */
	if (bpf_skb_store_bytes(skb, ETH_HLEN, &h_outer, olen,
				BPF_F_INVALIDATE_HASH) < 0)
		return TC_ACT_SHOT;

	return TC_ACT_OK;
}

static __always_inline int encap_ipv6(struct __sk_buff *skb, bool with_gre)
{
	struct ipv6hdr iph_inner;
	struct grev6hdr h_outer;
	struct tcphdr tcph;
	__u64 flags;
	int olen;

	if (bpf_skb_load_bytes(skb, ETH_HLEN, &iph_inner,
			       sizeof(iph_inner)) < 0)
		return TC_ACT_OK;

	/* filter only packets we want */
	if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(iph_inner),
			       &tcph, sizeof(tcph)) < 0)
		return TC_ACT_OK;

	if (tcph.dest != __bpf_constant_htons(cfg_port))
		return TC_ACT_OK;

	flags = BPF_F_ADJ_ROOM_FIXED_GSO;
	olen = with_gre ? sizeof(h_outer) : sizeof(h_outer.ip);

	/* add room between mac and network header */
	if (bpf_skb_adjust_room(skb, olen, BPF_ADJ_ROOM_MAC, flags))
		return TC_ACT_SHOT;

	/* prepare new outer network header */
	h_outer.ip = iph_inner;
	h_outer.ip.payload_len = bpf_htons(olen +
					   bpf_ntohs(h_outer.ip.payload_len));
	if (with_gre) {
		h_outer.ip.nexthdr = IPPROTO_GRE;
		h_outer.protocol = bpf_htons(ETH_P_IPV6);
		h_outer.flags = 0;
	} else {
		h_outer.ip.nexthdr = IPPROTO_IPV6;
	}

	/* store new outer network header */
	if (bpf_skb_store_bytes(skb, ETH_HLEN, &h_outer, olen,
				BPF_F_INVALIDATE_HASH) < 0)
		return TC_ACT_SHOT;

	return TC_ACT_OK;
}

SEC("encap_ipip")
int __encap_ipip(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IP))
		return encap_ipv4(skb, false);
	else
		return TC_ACT_OK;
}

SEC("encap_gre")
int __encap_gre(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IP))
		return encap_ipv4(skb, true);
	else
		return TC_ACT_OK;
}

SEC("encap_ip6tnl")
int __encap_ip6tnl(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IPV6))
		return encap_ipv6(skb, false);
	else
		return TC_ACT_OK;
}

SEC("encap_ip6gre")
int __encap_ip6gre(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IPV6))
		return encap_ipv6(skb, true);
	else
		return TC_ACT_OK;
}

static int decap_internal(struct __sk_buff *skb, int off, int len, char proto)
{
	char buf[sizeof(struct grev6hdr)];
	int olen;

	switch (proto) {
	case IPPROTO_IPIP:
	case IPPROTO_IPV6:
		olen = len;
		break;
	case IPPROTO_GRE:
		olen = len + 4 /* gre hdr */;
		break;
	default:
		return TC_ACT_OK;
	}

	if (bpf_skb_adjust_room(skb, -olen, BPF_ADJ_ROOM_MAC,
				BPF_F_ADJ_ROOM_FIXED_GSO))
		return TC_ACT_SHOT;

	return TC_ACT_OK;
}

static int decap_ipv4(struct __sk_buff *skb)
{
	struct iphdr iph_outer;

	if (bpf_skb_load_bytes(skb, ETH_HLEN, &iph_outer,
			       sizeof(iph_outer)) < 0)
		return TC_ACT_OK;

	if (iph_outer.ihl != 5)
		return TC_ACT_OK;

	return decap_internal(skb, ETH_HLEN, sizeof(iph_outer),
			      iph_outer.protocol);
}

static int decap_ipv6(struct __sk_buff *skb)
{
	struct ipv6hdr iph_outer;

	if (bpf_skb_load_bytes(skb, ETH_HLEN, &iph_outer,
			       sizeof(iph_outer)) < 0)
		return TC_ACT_OK;

	return decap_internal(skb, ETH_HLEN, sizeof(iph_outer),
			      iph_outer.nexthdr);
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
