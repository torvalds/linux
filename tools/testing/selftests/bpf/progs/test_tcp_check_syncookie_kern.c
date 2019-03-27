// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook
// Copyright (c) 2019 Cloudflare

#include <string.h>

#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <sys/socket.h>
#include <linux/tcp.h>

#include "bpf_helpers.h"
#include "bpf_endian.h"

struct bpf_map_def SEC("maps") results = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(__u32),
	.value_size = sizeof(__u64),
	.max_entries = 1,
};

static __always_inline void check_syncookie(void *ctx, void *data,
					    void *data_end)
{
	struct bpf_sock_tuple tup;
	struct bpf_sock *sk;
	struct ethhdr *ethh;
	struct iphdr *ipv4h;
	struct ipv6hdr *ipv6h;
	struct tcphdr *tcph;
	int ret;
	__u32 key = 0;
	__u64 value = 1;

	ethh = data;
	if (ethh + 1 > data_end)
		return;

	switch (bpf_ntohs(ethh->h_proto)) {
	case ETH_P_IP:
		ipv4h = data + sizeof(struct ethhdr);
		if (ipv4h + 1 > data_end)
			return;

		if (ipv4h->ihl != 5)
			return;

		tcph = data + sizeof(struct ethhdr) + sizeof(struct iphdr);
		if (tcph + 1 > data_end)
			return;

		tup.ipv4.saddr = ipv4h->saddr;
		tup.ipv4.daddr = ipv4h->daddr;
		tup.ipv4.sport = tcph->source;
		tup.ipv4.dport = tcph->dest;

		sk = bpf_skc_lookup_tcp(ctx, &tup, sizeof(tup.ipv4),
					BPF_F_CURRENT_NETNS, 0);
		if (!sk)
			return;

		if (sk->state != BPF_TCP_LISTEN)
			goto release;

		ret = bpf_tcp_check_syncookie(sk, ipv4h, sizeof(*ipv4h),
					      tcph, sizeof(*tcph));
		break;

	case ETH_P_IPV6:
		ipv6h = data + sizeof(struct ethhdr);
		if (ipv6h + 1 > data_end)
			return;

		if (ipv6h->nexthdr != IPPROTO_TCP)
			return;

		tcph = data + sizeof(struct ethhdr) + sizeof(struct ipv6hdr);
		if (tcph + 1 > data_end)
			return;

		memcpy(tup.ipv6.saddr, &ipv6h->saddr, sizeof(tup.ipv6.saddr));
		memcpy(tup.ipv6.daddr, &ipv6h->daddr, sizeof(tup.ipv6.daddr));
		tup.ipv6.sport = tcph->source;
		tup.ipv6.dport = tcph->dest;

		sk = bpf_skc_lookup_tcp(ctx, &tup, sizeof(tup.ipv6),
					BPF_F_CURRENT_NETNS, 0);
		if (!sk)
			return;

		if (sk->state != BPF_TCP_LISTEN)
			goto release;

		ret = bpf_tcp_check_syncookie(sk, ipv6h, sizeof(*ipv6h),
					      tcph, sizeof(*tcph));
		break;

	default:
		return;
	}

	if (ret == 0)
		bpf_map_update_elem(&results, &key, &value, 0);

release:
	bpf_sk_release(sk);
}

SEC("clsact/check_syncookie")
int check_syncookie_clsact(struct __sk_buff *skb)
{
	check_syncookie(skb, (void *)(long)skb->data,
			(void *)(long)skb->data_end);
	return TC_ACT_OK;
}

SEC("xdp/check_syncookie")
int check_syncookie_xdp(struct xdp_md *ctx)
{
	check_syncookie(ctx, (void *)(long)ctx->data,
			(void *)(long)ctx->data_end);
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
