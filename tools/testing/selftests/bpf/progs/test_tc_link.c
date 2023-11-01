// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Isovalent */
#include <stdbool.h>

#include <linux/bpf.h>
#include <linux/if_ether.h>

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "GPL";

bool seen_tc1;
bool seen_tc2;
bool seen_tc3;
bool seen_tc4;
bool seen_tc5;
bool seen_tc6;
bool seen_eth;

SEC("tc/ingress")
int tc1(struct __sk_buff *skb)
{
	struct ethhdr eth = {};

	if (skb->protocol != __bpf_constant_htons(ETH_P_IP))
		goto out;
	if (bpf_skb_load_bytes(skb, 0, &eth, sizeof(eth)))
		goto out;
	seen_eth = eth.h_proto == bpf_htons(ETH_P_IP);
out:
	seen_tc1 = true;
	return TCX_NEXT;
}

SEC("tc/egress")
int tc2(struct __sk_buff *skb)
{
	seen_tc2 = true;
	return TCX_NEXT;
}

SEC("tc/egress")
int tc3(struct __sk_buff *skb)
{
	seen_tc3 = true;
	return TCX_NEXT;
}

SEC("tc/egress")
int tc4(struct __sk_buff *skb)
{
	seen_tc4 = true;
	return TCX_NEXT;
}

SEC("tc/egress")
int tc5(struct __sk_buff *skb)
{
	seen_tc5 = true;
	return TCX_PASS;
}

SEC("tc/egress")
int tc6(struct __sk_buff *skb)
{
	seen_tc6 = true;
	return TCX_PASS;
}
