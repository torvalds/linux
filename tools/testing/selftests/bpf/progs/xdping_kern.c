// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved. */

#define KBUILD_MODNAME "foo"
#include <stddef.h>
#include <string.h>
#include <linux/bpf.h>
#include <linux/icmp.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "xdping.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 256);
	__type(key, __u32);
	__type(value, struct pinginfo);
} ping_map SEC(".maps");

static __always_inline void swap_src_dst_mac(void *data)
{
	unsigned short *p = data;
	unsigned short dst[3];

	dst[0] = p[0];
	dst[1] = p[1];
	dst[2] = p[2];
	p[0] = p[3];
	p[1] = p[4];
	p[2] = p[5];
	p[3] = dst[0];
	p[4] = dst[1];
	p[5] = dst[2];
}

static __always_inline __u16 csum_fold_helper(__wsum sum)
{
	sum = (sum & 0xffff) + (sum >> 16);
	return ~((sum & 0xffff) + (sum >> 16));
}

static __always_inline __u16 ipv4_csum(void *data_start, int data_size)
{
	__wsum sum;

	sum = bpf_csum_diff(0, 0, data_start, data_size, 0);
	return csum_fold_helper(sum);
}

#define ICMP_ECHO_LEN		64

static __always_inline int icmp_check(struct xdp_md *ctx, int type)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	struct icmphdr *icmph;
	struct iphdr *iph;

	if (data + sizeof(*eth) + sizeof(*iph) + ICMP_ECHO_LEN > data_end)
		return XDP_PASS;

	if (eth->h_proto != bpf_htons(ETH_P_IP))
		return XDP_PASS;

	iph = data + sizeof(*eth);

	if (iph->protocol != IPPROTO_ICMP)
		return XDP_PASS;

	if (bpf_ntohs(iph->tot_len) - sizeof(*iph) != ICMP_ECHO_LEN)
		return XDP_PASS;

	icmph = data + sizeof(*eth) + sizeof(*iph);

	if (icmph->type != type)
		return XDP_PASS;

	return XDP_TX;
}

SEC("xdpclient")
int xdping_client(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct pinginfo *pinginfo = NULL;
	struct ethhdr *eth = data;
	struct icmphdr *icmph;
	struct iphdr *iph;
	__u64 recvtime;
	__be32 raddr;
	__be16 seq;
	int ret;
	__u8 i;

	ret = icmp_check(ctx, ICMP_ECHOREPLY);

	if (ret != XDP_TX)
		return ret;

	iph = data + sizeof(*eth);
	icmph = data + sizeof(*eth) + sizeof(*iph);
	raddr = iph->saddr;

	/* Record time reply received. */
	recvtime = bpf_ktime_get_ns();
	pinginfo = bpf_map_lookup_elem(&ping_map, &raddr);
	if (!pinginfo || pinginfo->seq != icmph->un.echo.sequence)
		return XDP_PASS;

	if (pinginfo->start) {
#pragma clang loop unroll(full)
		for (i = 0; i < XDPING_MAX_COUNT; i++) {
			if (pinginfo->times[i] == 0)
				break;
		}
		/* verifier is fussy here... */
		if (i < XDPING_MAX_COUNT) {
			pinginfo->times[i] = recvtime -
					     pinginfo->start;
			pinginfo->start = 0;
			i++;
		}
		/* No more space for values? */
		if (i == pinginfo->count || i == XDPING_MAX_COUNT)
			return XDP_PASS;
	}

	/* Now convert reply back into echo request. */
	swap_src_dst_mac(data);
	iph->saddr = iph->daddr;
	iph->daddr = raddr;
	icmph->type = ICMP_ECHO;
	seq = bpf_htons(bpf_ntohs(icmph->un.echo.sequence) + 1);
	icmph->un.echo.sequence = seq;
	icmph->checksum = 0;
	icmph->checksum = ipv4_csum(icmph, ICMP_ECHO_LEN);

	pinginfo->seq = seq;
	pinginfo->start = bpf_ktime_get_ns();

	return XDP_TX;
}

SEC("xdpserver")
int xdping_server(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	struct icmphdr *icmph;
	struct iphdr *iph;
	__be32 raddr;
	int ret;

	ret = icmp_check(ctx, ICMP_ECHO);

	if (ret != XDP_TX)
		return ret;

	iph = data + sizeof(*eth);
	icmph = data + sizeof(*eth) + sizeof(*iph);
	raddr = iph->saddr;

	/* Now convert request into echo reply. */
	swap_src_dst_mac(data);
	iph->saddr = iph->daddr;
	iph->daddr = raddr;
	icmph->type = ICMP_ECHOREPLY;
	icmph->checksum = 0;
	icmph->checksum = ipv4_csum(icmph, ICMP_ECHO_LEN);

	return XDP_TX;
}

char _license[] SEC("license") = "GPL";
