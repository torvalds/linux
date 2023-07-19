// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Isovalent */
#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "GPL";

bool seen_tc1;
bool seen_tc2;
bool seen_tc3;
bool seen_tc4;

SEC("tc/ingress")
int tc1(struct __sk_buff *skb)
{
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
