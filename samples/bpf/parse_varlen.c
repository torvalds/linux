/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#define KBUILD_MODNAME "foo"
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <uapi/linux/bpf.h>
#include <net/ip.h>
#include <bpf/bpf_helpers.h>

#define DEFAULT_PKTGEN_UDP_PORT 9
#define DEBUG 0

static int tcp(void *data, uint64_t tp_off, void *data_end)
{
	struct tcphdr *tcp = data + tp_off;

	if (tcp + 1 > data_end)
		return 0;
	if (tcp->dest == htons(80) || tcp->source == htons(80))
		return TC_ACT_SHOT;
	return 0;
}

static int udp(void *data, uint64_t tp_off, void *data_end)
{
	struct udphdr *udp = data + tp_off;

	if (udp + 1 > data_end)
		return 0;
	if (udp->dest == htons(DEFAULT_PKTGEN_UDP_PORT) ||
	    udp->source == htons(DEFAULT_PKTGEN_UDP_PORT)) {
		if (DEBUG) {
			char fmt[] = "udp port 9 indeed\n";

			bpf_trace_printk(fmt, sizeof(fmt));
		}
		return TC_ACT_SHOT;
	}
	return 0;
}

static int parse_ipv4(void *data, uint64_t nh_off, void *data_end)
{
	struct iphdr *iph;
	uint64_t ihl_len;

	iph = data + nh_off;
	if (iph + 1 > data_end)
		return 0;

	if (ip_is_fragment(iph))
		return 0;
	ihl_len = iph->ihl * 4;

	if (iph->protocol == IPPROTO_IPIP) {
		iph = data + nh_off + ihl_len;
		if (iph + 1 > data_end)
			return 0;
		ihl_len += iph->ihl * 4;
	}

	if (iph->protocol == IPPROTO_TCP)
		return tcp(data, nh_off + ihl_len, data_end);
	else if (iph->protocol == IPPROTO_UDP)
		return udp(data, nh_off + ihl_len, data_end);
	return 0;
}

static int parse_ipv6(void *data, uint64_t nh_off, void *data_end)
{
	struct ipv6hdr *ip6h;
	struct iphdr *iph;
	uint64_t ihl_len = sizeof(struct ipv6hdr);
	uint64_t nexthdr;

	ip6h = data + nh_off;
	if (ip6h + 1 > data_end)
		return 0;

	nexthdr = ip6h->nexthdr;

	if (nexthdr == IPPROTO_IPIP) {
		iph = data + nh_off + ihl_len;
		if (iph + 1 > data_end)
			return 0;
		ihl_len += iph->ihl * 4;
		nexthdr = iph->protocol;
	} else if (nexthdr == IPPROTO_IPV6) {
		ip6h = data + nh_off + ihl_len;
		if (ip6h + 1 > data_end)
			return 0;
		ihl_len += sizeof(struct ipv6hdr);
		nexthdr = ip6h->nexthdr;
	}

	if (nexthdr == IPPROTO_TCP)
		return tcp(data, nh_off + ihl_len, data_end);
	else if (nexthdr == IPPROTO_UDP)
		return udp(data, nh_off + ihl_len, data_end);
	return 0;
}

SEC("varlen")
int handle_ingress(struct __sk_buff *skb)
{
	void *data = (void *)(long)skb->data;
	struct ethhdr *eth = data;
	void *data_end = (void *)(long)skb->data_end;
	uint64_t h_proto, nh_off;

	nh_off = sizeof(*eth);
	if (data + nh_off > data_end)
		return 0;

	h_proto = eth->h_proto;

	if (h_proto == ETH_P_8021Q || h_proto == ETH_P_8021AD) {
		struct vlan_hdr *vhdr;

		vhdr = data + nh_off;
		nh_off += sizeof(struct vlan_hdr);
		if (data + nh_off > data_end)
			return 0;
		h_proto = vhdr->h_vlan_encapsulated_proto;
	}
	if (h_proto == ETH_P_8021Q || h_proto == ETH_P_8021AD) {
		struct vlan_hdr *vhdr;

		vhdr = data + nh_off;
		nh_off += sizeof(struct vlan_hdr);
		if (data + nh_off > data_end)
			return 0;
		h_proto = vhdr->h_vlan_encapsulated_proto;
	}
	if (h_proto == htons(ETH_P_IP))
		return parse_ipv4(data, nh_off, data_end);
	else if (h_proto == htons(ETH_P_IPV6))
		return parse_ipv6(data, nh_off, data_end);
	return 0;
}
char _license[] SEC("license") = "GPL";
