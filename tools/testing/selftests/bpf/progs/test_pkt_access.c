// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */
#include <stddef.h>
#include <string.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/pkt_cls.h>
#include "bpf_helpers.h"
#include "bpf_endian.h"

#define barrier() __asm__ __volatile__("": : :"memory")
int _version SEC("version") = 1;

SEC("test1")
int process(struct __sk_buff *skb)
{
	void *data_end = (void *)(long)skb->data_end;
	void *data = (void *)(long)skb->data;
	struct ethhdr *eth = (struct ethhdr *)(data);
	struct tcphdr *tcp = NULL;
	__u8 proto = 255;
	__u64 ihl_len;

	if (eth + 1 > data_end)
		return TC_ACT_SHOT;

	if (eth->h_proto == bpf_htons(ETH_P_IP)) {
		struct iphdr *iph = (struct iphdr *)(eth + 1);

		if (iph + 1 > data_end)
			return TC_ACT_SHOT;
		ihl_len = iph->ihl * 4;
		proto = iph->protocol;
		tcp = (struct tcphdr *)((void *)(iph) + ihl_len);
	} else if (eth->h_proto == bpf_htons(ETH_P_IPV6)) {
		struct ipv6hdr *ip6h = (struct ipv6hdr *)(eth + 1);

		if (ip6h + 1 > data_end)
			return TC_ACT_SHOT;
		ihl_len = sizeof(*ip6h);
		proto = ip6h->nexthdr;
		tcp = (struct tcphdr *)((void *)(ip6h) + ihl_len);
	}

	if (tcp) {
		if (((void *)(tcp) + 20) > data_end || proto != 6)
			return TC_ACT_SHOT;
		barrier(); /* to force ordering of checks */
		if (((void *)(tcp) + 18) > data_end)
			return TC_ACT_SHOT;
		if (tcp->urg_ptr == 123)
			return TC_ACT_OK;
	}

	return TC_ACT_UNSPEC;
}
