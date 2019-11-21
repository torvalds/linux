/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program shows how to use bpf_xdp_adjust_tail() by
 * generating ICMPv4 "packet to big" (unreachable/ df bit set frag needed
 * to be more preice in case of v4)" where receiving packets bigger then
 * 600 bytes.
 */
#define KBUILD_MODNAME "foo"
#include <uapi/linux/bpf.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include "bpf_helpers.h"

#define DEFAULT_TTL 64
#define MAX_PCKT_SIZE 600
#define ICMP_TOOBIG_SIZE 98
#define ICMP_TOOBIG_PAYLOAD_SIZE 92

/* volatile to prevent compiler optimizations */
static volatile __u32 max_pcktsz = MAX_PCKT_SIZE;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, __u32);
	__type(value, __u64);
	__uint(max_entries, 1);
} icmpcnt SEC(".maps");

static __always_inline void count_icmp(void)
{
	u64 key = 0;
	u64 *icmp_count;

	icmp_count = bpf_map_lookup_elem(&icmpcnt, &key);
	if (icmp_count)
		*icmp_count += 1;
}

static __always_inline void swap_mac(void *data, struct ethhdr *orig_eth)
{
	struct ethhdr *eth;

	eth = data;
	memcpy(eth->h_source, orig_eth->h_dest, ETH_ALEN);
	memcpy(eth->h_dest, orig_eth->h_source, ETH_ALEN);
	eth->h_proto = orig_eth->h_proto;
}

static __always_inline __u16 csum_fold_helper(__u32 csum)
{
	return ~((csum & 0xffff) + (csum >> 16));
}

static __always_inline void ipv4_csum(void *data_start, int data_size,
				      __u32 *csum)
{
	*csum = bpf_csum_diff(0, 0, data_start, data_size, *csum);
	*csum = csum_fold_helper(*csum);
}

static __always_inline int send_icmp4_too_big(struct xdp_md *xdp)
{
	int headroom = (int)sizeof(struct iphdr) + (int)sizeof(struct icmphdr);

	if (bpf_xdp_adjust_head(xdp, 0 - headroom))
		return XDP_DROP;
	void *data = (void *)(long)xdp->data;
	void *data_end = (void *)(long)xdp->data_end;

	if (data + (ICMP_TOOBIG_SIZE + headroom) > data_end)
		return XDP_DROP;

	struct iphdr *iph, *orig_iph;
	struct icmphdr *icmp_hdr;
	struct ethhdr *orig_eth;
	__u32 csum = 0;
	__u64 off = 0;

	orig_eth = data + headroom;
	swap_mac(data, orig_eth);
	off += sizeof(struct ethhdr);
	iph = data + off;
	off += sizeof(struct iphdr);
	icmp_hdr = data + off;
	off += sizeof(struct icmphdr);
	orig_iph = data + off;
	icmp_hdr->type = ICMP_DEST_UNREACH;
	icmp_hdr->code = ICMP_FRAG_NEEDED;
	icmp_hdr->un.frag.mtu = htons(max_pcktsz - sizeof(struct ethhdr));
	icmp_hdr->checksum = 0;
	ipv4_csum(icmp_hdr, ICMP_TOOBIG_PAYLOAD_SIZE, &csum);
	icmp_hdr->checksum = csum;
	iph->ttl = DEFAULT_TTL;
	iph->daddr = orig_iph->saddr;
	iph->saddr = orig_iph->daddr;
	iph->version = 4;
	iph->ihl = 5;
	iph->protocol = IPPROTO_ICMP;
	iph->tos = 0;
	iph->tot_len = htons(
		ICMP_TOOBIG_SIZE + headroom - sizeof(struct ethhdr));
	iph->check = 0;
	csum = 0;
	ipv4_csum(iph, sizeof(struct iphdr), &csum);
	iph->check = csum;
	count_icmp();
	return XDP_TX;
}


static __always_inline int handle_ipv4(struct xdp_md *xdp)
{
	void *data_end = (void *)(long)xdp->data_end;
	void *data = (void *)(long)xdp->data;
	int pckt_size = data_end - data;
	int offset;

	if (pckt_size > max(max_pcktsz, ICMP_TOOBIG_SIZE)) {
		offset = pckt_size - ICMP_TOOBIG_SIZE;
		if (bpf_xdp_adjust_tail(xdp, 0 - offset))
			return XDP_PASS;
		return send_icmp4_too_big(xdp);
	}
	return XDP_PASS;
}

SEC("xdp_icmp")
int _xdp_icmp(struct xdp_md *xdp)
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
	else
		return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
