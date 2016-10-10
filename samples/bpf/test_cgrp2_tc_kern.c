/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <uapi/linux/if_ether.h>
#include <uapi/linux/in6.h>
#include <uapi/linux/ipv6.h>
#include <uapi/linux/pkt_cls.h>
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

/* copy of 'struct ethhdr' without __packed */
struct eth_hdr {
	unsigned char   h_dest[ETH_ALEN];
	unsigned char   h_source[ETH_ALEN];
	unsigned short  h_proto;
};

#define PIN_GLOBAL_NS		2
struct bpf_elf_map {
	__u32 type;
	__u32 size_key;
	__u32 size_value;
	__u32 max_elem;
	__u32 flags;
	__u32 id;
	__u32 pinning;
};

struct bpf_elf_map SEC("maps") test_cgrp2_array_pin = {
	.type		= BPF_MAP_TYPE_CGROUP_ARRAY,
	.size_key	= sizeof(uint32_t),
	.size_value	= sizeof(uint32_t),
	.pinning	= PIN_GLOBAL_NS,
	.max_elem	= 1,
};

SEC("filter")
int handle_egress(struct __sk_buff *skb)
{
	void *data = (void *)(long)skb->data;
	struct eth_hdr *eth = data;
	struct ipv6hdr *ip6h = data + sizeof(*eth);
	void *data_end = (void *)(long)skb->data_end;
	char dont_care_msg[] = "dont care %04x %d\n";
	char pass_msg[] = "pass\n";
	char reject_msg[] = "reject\n";

	/* single length check */
	if (data + sizeof(*eth) + sizeof(*ip6h) > data_end)
		return TC_ACT_OK;

	if (eth->h_proto != htons(ETH_P_IPV6) ||
	    ip6h->nexthdr != IPPROTO_ICMPV6) {
		bpf_trace_printk(dont_care_msg, sizeof(dont_care_msg),
				 eth->h_proto, ip6h->nexthdr);
		return TC_ACT_OK;
	} else if (bpf_skb_under_cgroup(skb, &test_cgrp2_array_pin, 0) != 1) {
		bpf_trace_printk(pass_msg, sizeof(pass_msg));
		return TC_ACT_OK;
	} else {
		bpf_trace_printk(reject_msg, sizeof(reject_msg));
		return TC_ACT_SHOT;
	}
}

char _license[] SEC("license") = "GPL";
