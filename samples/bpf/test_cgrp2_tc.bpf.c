/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#define KBUILD_MODNAME "foo"
#include "vmlinux.h"
#include "net_shared.h"
#include <bpf/bpf_helpers.h>

/* copy of 'struct ethhdr' without __packed */
struct eth_hdr {
	unsigned char   h_dest[ETH_ALEN];
	unsigned char   h_source[ETH_ALEN];
	unsigned short  h_proto;
};

struct {
	__uint(type, BPF_MAP_TYPE_CGROUP_ARRAY);
	__type(key, u32);
	__type(value, u32);
	__uint(pinning, LIBBPF_PIN_BY_NAME);
	__uint(max_entries, 1);
} test_cgrp2_array_pin SEC(".maps");

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

	if (eth->h_proto != bpf_htons(ETH_P_IPV6) ||
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
