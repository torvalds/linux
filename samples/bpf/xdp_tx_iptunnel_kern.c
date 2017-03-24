/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program shows how to use bpf_xdp_adjust_head() by
 * encapsulating the incoming packet in an IPv4/v6 header
 * and then XDP_TX it out.
 */
#define KBUILD_MODNAME "foo"
#include <uapi/linux/bpf.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include "bpf_helpers.h"
#include "xdp_tx_iptunnel_common.h"

struct bpf_map_def SEC("maps") rxcnt = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size = sizeof(__u32),
	.value_size = sizeof(__u64),
	.max_entries = 256,
};

struct bpf_map_def SEC("maps") vip2tnl = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(struct vip),
	.value_size = sizeof(struct iptnl_info),
	.max_entries = MAX_IPTNL_ENTRIES,
};

static __always_inline void count_tx(u32 protocol)
{
	u64 *rxcnt_count;

	rxcnt_count = bpf_map_lookup_elem(&rxcnt, &protocol);
	if (rxcnt_count)
		*rxcnt_count += 1;
}

static __always_inline int get_dport(void *trans_data, void *data_end,
				     u8 protocol)
{
	struct tcphdr *th;
	struct udphdr *uh;

	switch (protocol) {
	case IPPROTO_TCP:
		th = (struct tcphdr *)trans_data;
		if (th + 1 > data_end)
			return -1;
		return th->dest;
	case IPPROTO_UDP:
		uh = (struct udphdr *)trans_data;
		if (uh + 1 > data_end)
			return -1;
		return uh->dest;
	default:
		return 0;
	}
}

static __always_inline void set_ethhdr(struct ethhdr *new_eth,
				       const struct ethhdr *old_eth,
				       const struct iptnl_info *tnl,
				       __be16 h_proto)
{
	memcpy(new_eth->h_source, old_eth->h_dest, sizeof(new_eth->h_source));
	memcpy(new_eth->h_dest, tnl->dmac, sizeof(new_eth->h_dest));
	new_eth->h_proto = h_proto;
}

static __always_inline int handle_ipv4(struct xdp_md *xdp)
{
	void *data_end = (void *)(long)xdp->data_end;
	void *data = (void *)(long)xdp->data;
	struct iptnl_info *tnl;
	struct ethhdr *new_eth;
	struct ethhdr *old_eth;
	struct iphdr *iph = data + sizeof(struct ethhdr);
	u16 *next_iph_u16;
	u16 payload_len;
	struct vip vip = {};
	int dport;
	u32 csum = 0;
	int i;

	if (iph + 1 > data_end)
		return XDP_DROP;

	dport = get_dport(iph + 1, data_end, iph->protocol);
	if (dport == -1)
		return XDP_DROP;

	vip.protocol = iph->protocol;
	vip.family = AF_INET;
	vip.daddr.v4 = iph->daddr;
	vip.dport = dport;
	payload_len = ntohs(iph->tot_len);

	tnl = bpf_map_lookup_elem(&vip2tnl, &vip);
	/* It only does v4-in-v4 */
	if (!tnl || tnl->family != AF_INET)
		return XDP_PASS;

	/* The vip key is found.  Add an IP header and send it out */

	if (bpf_xdp_adjust_head(xdp, 0 - (int)sizeof(struct iphdr)))
		return XDP_DROP;

	data = (void *)(long)xdp->data;
	data_end = (void *)(long)xdp->data_end;

	new_eth = data;
	iph = data + sizeof(*new_eth);
	old_eth = data + sizeof(*iph);

	if (new_eth + 1 > data_end ||
	    old_eth + 1 > data_end ||
	    iph + 1 > data_end)
		return XDP_DROP;

	set_ethhdr(new_eth, old_eth, tnl, htons(ETH_P_IP));

	iph->version = 4;
	iph->ihl = sizeof(*iph) >> 2;
	iph->frag_off =	0;
	iph->protocol = IPPROTO_IPIP;
	iph->check = 0;
	iph->tos = 0;
	iph->tot_len = htons(payload_len + sizeof(*iph));
	iph->daddr = tnl->daddr.v4;
	iph->saddr = tnl->saddr.v4;
	iph->ttl = 8;

	next_iph_u16 = (u16 *)iph;
#pragma clang loop unroll(full)
	for (i = 0; i < sizeof(*iph) >> 1; i++)
		csum += *next_iph_u16++;

	iph->check = ~((csum & 0xffff) + (csum >> 16));

	count_tx(vip.protocol);

	return XDP_TX;
}

static __always_inline int handle_ipv6(struct xdp_md *xdp)
{
	void *data_end = (void *)(long)xdp->data_end;
	void *data = (void *)(long)xdp->data;
	struct iptnl_info *tnl;
	struct ethhdr *new_eth;
	struct ethhdr *old_eth;
	struct ipv6hdr *ip6h = data + sizeof(struct ethhdr);
	__u16 payload_len;
	struct vip vip = {};
	int dport;

	if (ip6h + 1 > data_end)
		return XDP_DROP;

	dport = get_dport(ip6h + 1, data_end, ip6h->nexthdr);
	if (dport == -1)
		return XDP_DROP;

	vip.protocol = ip6h->nexthdr;
	vip.family = AF_INET6;
	memcpy(vip.daddr.v6, ip6h->daddr.s6_addr32, sizeof(vip.daddr));
	vip.dport = dport;
	payload_len = ip6h->payload_len;

	tnl = bpf_map_lookup_elem(&vip2tnl, &vip);
	/* It only does v6-in-v6 */
	if (!tnl || tnl->family != AF_INET6)
		return XDP_PASS;

	/* The vip key is found.  Add an IP header and send it out */

	if (bpf_xdp_adjust_head(xdp, 0 - (int)sizeof(struct ipv6hdr)))
		return XDP_DROP;

	data = (void *)(long)xdp->data;
	data_end = (void *)(long)xdp->data_end;

	new_eth = data;
	ip6h = data + sizeof(*new_eth);
	old_eth = data + sizeof(*ip6h);

	if (new_eth + 1 > data_end ||
	    old_eth + 1 > data_end ||
	    ip6h + 1 > data_end)
		return XDP_DROP;

	set_ethhdr(new_eth, old_eth, tnl, htons(ETH_P_IPV6));

	ip6h->version = 6;
	ip6h->priority = 0;
	memset(ip6h->flow_lbl, 0, sizeof(ip6h->flow_lbl));
	ip6h->payload_len = htons(ntohs(payload_len) + sizeof(*ip6h));
	ip6h->nexthdr = IPPROTO_IPV6;
	ip6h->hop_limit = 8;
	memcpy(ip6h->saddr.s6_addr32, tnl->saddr.v6, sizeof(tnl->saddr.v6));
	memcpy(ip6h->daddr.s6_addr32, tnl->daddr.v6, sizeof(tnl->daddr.v6));

	count_tx(vip.protocol);

	return XDP_TX;
}

SEC("xdp_tx_iptunnel")
int _xdp_tx_iptunnel(struct xdp_md *xdp)
{
	void *data_end = (void *)(long)xdp->data_end;
	void *data = (void *)(long)xdp->data;
	struct ethhdr *eth = data;
	__u16 h_proto;

	if (eth + 1 > data_end)
		return XDP_DROP;

	h_proto = eth->h_proto;

	if (h_proto == htons(ETH_P_IP))
		return handle_ipv4(xdp);
	else if (h_proto == htons(ETH_P_IPV6))

		return handle_ipv6(xdp);
	else
		return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
