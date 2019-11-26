/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#define KBUILD_MODNAME "foo"
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"
#include "bpf_legacy.h"

#define DEFAULT_PKTGEN_UDP_PORT	9
#define IP_MF			0x2000
#define IP_OFFSET		0x1FFF

static inline int ip_is_fragment(struct __sk_buff *ctx, __u64 nhoff)
{
	return load_half(ctx, nhoff + offsetof(struct iphdr, frag_off))
		& (IP_MF | IP_OFFSET);
}

SEC("ldabs")
int handle_ingress(struct __sk_buff *skb)
{
	__u64 troff = ETH_HLEN + sizeof(struct iphdr);

	if (load_half(skb, offsetof(struct ethhdr, h_proto)) != ETH_P_IP)
		return 0;
	if (load_byte(skb, ETH_HLEN + offsetof(struct iphdr, protocol)) != IPPROTO_UDP ||
	    load_byte(skb, ETH_HLEN) != 0x45)
		return 0;
	if (ip_is_fragment(skb, ETH_HLEN))
		return 0;
	if (load_half(skb, troff + offsetof(struct udphdr, dest)) == DEFAULT_PKTGEN_UDP_PORT)
		return TC_ACT_SHOT;
	return 0;
}
char _license[] SEC("license") = "GPL";
