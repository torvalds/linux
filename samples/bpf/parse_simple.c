/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <uapi/linux/bpf.h>
#include <net/ip.h>
#include "bpf_helpers.h"

#define DEFAULT_PKTGEN_UDP_PORT 9

/* copy of 'struct ethhdr' without __packed */
struct eth_hdr {
	unsigned char   h_dest[ETH_ALEN];
	unsigned char   h_source[ETH_ALEN];
	unsigned short  h_proto;
};

SEC("simple")
int handle_ingress(struct __sk_buff *skb)
{
	void *data = (void *)(long)skb->data;
	struct eth_hdr *eth = data;
	struct iphdr *iph = data + sizeof(*eth);
	struct udphdr *udp = data + sizeof(*eth) + sizeof(*iph);
	void *data_end = (void *)(long)skb->data_end;

	/* single length check */
	if (data + sizeof(*eth) + sizeof(*iph) + sizeof(*udp) > data_end)
		return 0;

	if (eth->h_proto != htons(ETH_P_IP))
		return 0;
	if (iph->protocol != IPPROTO_UDP || iph->ihl != 5)
		return 0;
	if (ip_is_fragment(iph))
		return 0;
	if (udp->dest == htons(DEFAULT_PKTGEN_UDP_PORT))
		return TC_ACT_SHOT;
	return 0;
}
char _license[] SEC("license") = "GPL";
