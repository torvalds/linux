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

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, __u32);
	__type(value, __u32);
	__uint(max_entries, 3);
} results SEC(".maps");

static __always_inline __s64 gen_syncookie(void *data_end, struct bpf_sock *sk,
					   void *iph, __u32 ip_size,
					   struct tcphdr *tcph)
{
	__u32 thlen = tcph->doff * 4;

	if (tcph->syn && !tcph->ack) {
		// packet should only have an MSS option
		if (thlen != 24)
			return 0;

		if ((void *)tcph + thlen > data_end)
			return 0;

		return bpf_tcp_gen_syncookie(sk, iph, ip_size, tcph, thlen);
	}
	return 0;
}

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
	__u32 key_mss = 2;
	__u32 key_gen = 1;
	__u32 key = 0;
	__s64 seq_mss;

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

		seq_mss = gen_syncookie(data_end, sk, ipv4h, sizeof(*ipv4h),
					tcph);

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

		seq_mss = gen_syncookie(data_end, sk, ipv6h, sizeof(*ipv6h),
					tcph);

		ret = bpf_tcp_check_syncookie(sk, ipv6h, sizeof(*ipv6h),
					      tcph, sizeof(*tcph));
		break;

	default:
		return;
	}

	if (seq_mss > 0) {
		__u32 cookie = (__u32)seq_mss;
		__u32 mss = seq_mss >> 32;

		bpf_map_update_elem(&results, &key_gen, &cookie, 0);
		bpf_map_update_elem(&results, &key_mss, &mss, 0);
	}

	if (ret == 0) {
		__u32 cookie = bpf_ntohl(tcph->ack_seq) - 1;

		bpf_map_update_elem(&results, &key, &cookie, 0);
	}

release:
	bpf_sk_release(sk);
}

SEC("tc")
int check_syncookie_clsact(struct __sk_buff *skb)
{
	check_syncookie(skb, (void *)(long)skb->data,
			(void *)(long)skb->data_end);
	return TC_ACT_OK;
}

SEC("xdp")
int check_syncookie_xdp(struct xdp_md *ctx)
{
	check_syncookie(ctx, (void *)(long)ctx->data,
			(void *)(long)ctx->data_end);
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
