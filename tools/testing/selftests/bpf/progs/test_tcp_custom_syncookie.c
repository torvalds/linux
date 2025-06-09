// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com Inc. or its affiliates. */

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include "bpf_tracing_net.h"
#include "bpf_kfuncs.h"
#include "test_siphash.h"
#include "test_tcp_custom_syncookie.h"
#include "bpf_misc.h"

#define MAX_PACKET_OFF 0xffff

/* Hash is calculated for each client and split into ISN and TS.
 *
 *       MSB                                   LSB
 * ISN:  | 31 ... 8 | 7 6 |   5 |    4 | 3 2 1 0 |
 *       |   Hash_1 | MSS | ECN | SACK |  WScale |
 *
 * TS:   | 31 ... 8 |          7 ... 0           |
 *       |   Random |           Hash_2           |
 */
#define COOKIE_BITS	8
#define COOKIE_MASK	(((__u32)1 << COOKIE_BITS) - 1)

enum {
	/* 0xf is invalid thus means that SYN did not have WScale. */
	BPF_SYNCOOKIE_WSCALE_MASK	= (1 << 4) - 1,
	BPF_SYNCOOKIE_SACK		= (1 << 4),
	BPF_SYNCOOKIE_ECN		= (1 << 5),
};

#define MSS_LOCAL_IPV4	65495
#define MSS_LOCAL_IPV6	65476

const __u16 msstab4[] = {
	536,
	1300,
	1460,
	MSS_LOCAL_IPV4,
};

const __u16 msstab6[] = {
	1280 - 60, /* IPV6_MIN_MTU - 60 */
	1480 - 60,
	9000 - 60,
	MSS_LOCAL_IPV6,
};

static siphash_key_t test_key_siphash = {
	{ 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL }
};

struct tcp_syncookie {
	struct __sk_buff *skb;
	void *data;
	void *data_end;
	struct ethhdr *eth;
	struct iphdr *ipv4;
	struct ipv6hdr *ipv6;
	struct tcphdr *tcp;
	__be32 *ptr32;
	struct bpf_tcp_req_attrs attrs;
	u32 off;
	u32 cookie;
	u64 first;
};

bool handled_syn, handled_ack;

static int tcp_load_headers(struct tcp_syncookie *ctx)
{
	ctx->data = (void *)(long)ctx->skb->data;
	ctx->data_end = (void *)(long)ctx->skb->data_end;
	ctx->eth = (struct ethhdr *)(long)ctx->skb->data;

	if (ctx->eth + 1 > ctx->data_end)
		goto err;

	switch (bpf_ntohs(ctx->eth->h_proto)) {
	case ETH_P_IP:
		ctx->ipv4 = (struct iphdr *)(ctx->eth + 1);

		if (ctx->ipv4 + 1 > ctx->data_end)
			goto err;

		if (ctx->ipv4->ihl != sizeof(*ctx->ipv4) / 4)
			goto err;

		if (ctx->ipv4->version != 4)
			goto err;

		if (ctx->ipv4->protocol != IPPROTO_TCP)
			goto err;

		ctx->tcp = (struct tcphdr *)(ctx->ipv4 + 1);
		break;
	case ETH_P_IPV6:
		ctx->ipv6 = (struct ipv6hdr *)(ctx->eth + 1);

		if (ctx->ipv6 + 1 > ctx->data_end)
			goto err;

		if (ctx->ipv6->version != 6)
			goto err;

		if (ctx->ipv6->nexthdr != NEXTHDR_TCP)
			goto err;

		ctx->tcp = (struct tcphdr *)(ctx->ipv6 + 1);
		break;
	default:
		goto err;
	}

	if (ctx->tcp + 1 > ctx->data_end)
		goto err;

	return 0;
err:
	return -1;
}

static int tcp_reload_headers(struct tcp_syncookie *ctx)
{
	/* Without volatile,
	 * R3 32-bit pointer arithmetic prohibited
	 */
	volatile u64 data_len = ctx->skb->data_end - ctx->skb->data;

	if (ctx->tcp->doff < sizeof(*ctx->tcp) / 4)
		goto err;

	/* Needed to calculate csum and parse TCP options. */
	if (bpf_skb_change_tail(ctx->skb, data_len + 60 - ctx->tcp->doff * 4, 0))
		goto err;

	ctx->data = (void *)(long)ctx->skb->data;
	ctx->data_end = (void *)(long)ctx->skb->data_end;
	ctx->eth = (struct ethhdr *)(long)ctx->skb->data;
	if (ctx->ipv4) {
		ctx->ipv4 = (struct iphdr *)(ctx->eth + 1);
		ctx->ipv6 = NULL;
		ctx->tcp = (struct tcphdr *)(ctx->ipv4 + 1);
	} else {
		ctx->ipv4 = NULL;
		ctx->ipv6 = (struct ipv6hdr *)(ctx->eth + 1);
		ctx->tcp = (struct tcphdr *)(ctx->ipv6 + 1);
	}

	if ((void *)ctx->tcp + 60 > ctx->data_end)
		goto err;

	return 0;
err:
	return -1;
}

static __sum16 tcp_v4_csum(struct tcp_syncookie *ctx, __wsum csum)
{
	return csum_tcpudp_magic(ctx->ipv4->saddr, ctx->ipv4->daddr,
				 ctx->tcp->doff * 4, IPPROTO_TCP, csum);
}

static __sum16 tcp_v6_csum(struct tcp_syncookie *ctx, __wsum csum)
{
	return csum_ipv6_magic(&ctx->ipv6->saddr, &ctx->ipv6->daddr,
			       ctx->tcp->doff * 4, IPPROTO_TCP, csum);
}

static int tcp_validate_header(struct tcp_syncookie *ctx)
{
	s64 csum;

	if (tcp_reload_headers(ctx))
		goto err;

	csum = bpf_csum_diff(0, 0, (void *)ctx->tcp, ctx->tcp->doff * 4, 0);
	if (csum < 0)
		goto err;

	if (ctx->ipv4) {
		/* check tcp_v4_csum(csum) is 0 if not on lo. */

		csum = bpf_csum_diff(0, 0, (void *)ctx->ipv4, ctx->ipv4->ihl * 4, 0);
		if (csum < 0)
			goto err;

		if (csum_fold(csum) != 0)
			goto err;
	} else if (ctx->ipv6) {
		/* check tcp_v6_csum(csum) is 0 if not on lo. */
	}

	return 0;
err:
	return -1;
}

static __always_inline void *next(struct tcp_syncookie *ctx, __u32 sz)
{
	__u64 off = ctx->off;
	__u8 *data;

	/* Verifier forbids access to packet when offset exceeds MAX_PACKET_OFF */
	if (off > MAX_PACKET_OFF - sz)
		return NULL;

	data = ctx->data + off;
	barrier_var(data);
	if (data + sz >= ctx->data_end)
		return NULL;

	ctx->off += sz;
	return data;
}

static int tcp_parse_option(__u32 index, struct tcp_syncookie *ctx)
{
	__u8 *opcode, *opsize, *wscale;
	__u32 *tsval, *tsecr;
	__u16 *mss;
	__u32 off;

	off = ctx->off;
	opcode = next(ctx, 1);
	if (!opcode)
		goto stop;

	if (*opcode == TCPOPT_EOL)
		goto stop;

	if (*opcode == TCPOPT_NOP)
		goto next;

	opsize = next(ctx, 1);
	if (!opsize)
		goto stop;

	if (*opsize < 2)
		goto stop;

	switch (*opcode) {
	case TCPOPT_MSS:
		mss = next(ctx, 2);
		if (*opsize == TCPOLEN_MSS && ctx->tcp->syn && mss)
			ctx->attrs.mss = get_unaligned_be16(mss);
		break;
	case TCPOPT_WINDOW:
		wscale = next(ctx, 1);
		if (*opsize == TCPOLEN_WINDOW && ctx->tcp->syn && wscale) {
			ctx->attrs.wscale_ok = 1;
			ctx->attrs.snd_wscale = *wscale;
		}
		break;
	case TCPOPT_TIMESTAMP:
		tsval = next(ctx, 4);
		tsecr = next(ctx, 4);
		if (*opsize == TCPOLEN_TIMESTAMP && tsval && tsecr) {
			ctx->attrs.rcv_tsval = get_unaligned_be32(tsval);
			ctx->attrs.rcv_tsecr = get_unaligned_be32(tsecr);

			if (ctx->tcp->syn && ctx->attrs.rcv_tsecr)
				ctx->attrs.tstamp_ok = 0;
			else
				ctx->attrs.tstamp_ok = 1;
		}
		break;
	case TCPOPT_SACK_PERM:
		if (*opsize == TCPOLEN_SACK_PERM && ctx->tcp->syn)
			ctx->attrs.sack_ok = 1;
		break;
	}

	ctx->off = off + *opsize;
next:
	return 0;
stop:
	return 1;
}

static void tcp_parse_options(struct tcp_syncookie *ctx)
{
	ctx->off = (__u8 *)(ctx->tcp + 1) - (__u8 *)ctx->data,

	bpf_loop(40, tcp_parse_option, ctx, 0);
}

static int tcp_validate_sysctl(struct tcp_syncookie *ctx)
{
	if ((ctx->ipv4 && ctx->attrs.mss != MSS_LOCAL_IPV4) ||
	    (ctx->ipv6 && ctx->attrs.mss != MSS_LOCAL_IPV6))
		goto err;

	if (!ctx->attrs.wscale_ok ||
	    !ctx->attrs.snd_wscale ||
	    ctx->attrs.snd_wscale >= BPF_SYNCOOKIE_WSCALE_MASK)
		goto err;

	if (!ctx->attrs.tstamp_ok)
		goto err;

	if (!ctx->attrs.sack_ok)
		goto err;

	if (!ctx->tcp->ece || !ctx->tcp->cwr)
		goto err;

	return 0;
err:
	return -1;
}

static void tcp_prepare_cookie(struct tcp_syncookie *ctx)
{
	u32 seq = bpf_ntohl(ctx->tcp->seq);
	u64 first = 0, second;
	int mssind = 0;
	u32 hash;

	if (ctx->ipv4) {
		for (mssind = ARRAY_SIZE(msstab4) - 1; mssind; mssind--)
			if (ctx->attrs.mss >= msstab4[mssind])
				break;

		ctx->attrs.mss = msstab4[mssind];

		first = (u64)ctx->ipv4->saddr << 32 | ctx->ipv4->daddr;
	} else if (ctx->ipv6) {
		for (mssind = ARRAY_SIZE(msstab6) - 1; mssind; mssind--)
			if (ctx->attrs.mss >= msstab6[mssind])
				break;

		ctx->attrs.mss = msstab6[mssind];

		first = (u64)ctx->ipv6->saddr.in6_u.u6_addr8[0] << 32 |
			ctx->ipv6->daddr.in6_u.u6_addr32[0];
	}

	second = (u64)seq << 32 | ctx->tcp->source << 16 | ctx->tcp->dest;
	hash = siphash_2u64(first, second, &test_key_siphash);

	if (ctx->attrs.tstamp_ok) {
		ctx->attrs.rcv_tsecr = bpf_get_prandom_u32();
		ctx->attrs.rcv_tsecr &= ~COOKIE_MASK;
		ctx->attrs.rcv_tsecr |= hash & COOKIE_MASK;
	}

	hash &= ~COOKIE_MASK;
	hash |= mssind << 6;

	if (ctx->attrs.wscale_ok)
		hash |= ctx->attrs.snd_wscale & BPF_SYNCOOKIE_WSCALE_MASK;

	if (ctx->attrs.sack_ok)
		hash |= BPF_SYNCOOKIE_SACK;

	if (ctx->attrs.tstamp_ok && ctx->tcp->ece && ctx->tcp->cwr)
		hash |= BPF_SYNCOOKIE_ECN;

	ctx->cookie = hash;
}

static void tcp_write_options(struct tcp_syncookie *ctx)
{
	ctx->ptr32 = (__be32 *)(ctx->tcp + 1);

	*ctx->ptr32++ = bpf_htonl(TCPOPT_MSS << 24 | TCPOLEN_MSS << 16 |
				  ctx->attrs.mss);

	if (ctx->attrs.wscale_ok)
		*ctx->ptr32++ = bpf_htonl(TCPOPT_NOP << 24 |
					  TCPOPT_WINDOW << 16 |
					  TCPOLEN_WINDOW << 8 |
					  ctx->attrs.snd_wscale);

	if (ctx->attrs.tstamp_ok) {
		if (ctx->attrs.sack_ok)
			*ctx->ptr32++ = bpf_htonl(TCPOPT_SACK_PERM << 24 |
						  TCPOLEN_SACK_PERM << 16 |
						  TCPOPT_TIMESTAMP << 8 |
						  TCPOLEN_TIMESTAMP);
		else
			*ctx->ptr32++ = bpf_htonl(TCPOPT_NOP << 24 |
						  TCPOPT_NOP << 16 |
						  TCPOPT_TIMESTAMP << 8 |
						  TCPOLEN_TIMESTAMP);

		*ctx->ptr32++ = bpf_htonl(ctx->attrs.rcv_tsecr);
		*ctx->ptr32++ = bpf_htonl(ctx->attrs.rcv_tsval);
	} else if (ctx->attrs.sack_ok) {
		*ctx->ptr32++ = bpf_htonl(TCPOPT_NOP << 24 |
					  TCPOPT_NOP << 16 |
					  TCPOPT_SACK_PERM << 8 |
					  TCPOLEN_SACK_PERM);
	}
}

static int tcp_handle_syn(struct tcp_syncookie *ctx)
{
	s64 csum;

	if (tcp_validate_header(ctx))
		goto err;

	tcp_parse_options(ctx);

	if (tcp_validate_sysctl(ctx))
		goto err;

	tcp_prepare_cookie(ctx);
	tcp_write_options(ctx);

	swap(ctx->tcp->source, ctx->tcp->dest);
	ctx->tcp->check = 0;
	ctx->tcp->ack_seq = bpf_htonl(bpf_ntohl(ctx->tcp->seq) + 1);
	ctx->tcp->seq = bpf_htonl(ctx->cookie);
	ctx->tcp->doff = ((long)ctx->ptr32 - (long)ctx->tcp) >> 2;
	ctx->tcp->ack = 1;
	if (!ctx->attrs.tstamp_ok || !ctx->tcp->ece || !ctx->tcp->cwr)
		ctx->tcp->ece = 0;
	ctx->tcp->cwr = 0;

	csum = bpf_csum_diff(0, 0, (void *)ctx->tcp, ctx->tcp->doff * 4, 0);
	if (csum < 0)
		goto err;

	if (ctx->ipv4) {
		swap(ctx->ipv4->saddr, ctx->ipv4->daddr);
		ctx->tcp->check = tcp_v4_csum(ctx, csum);

		ctx->ipv4->check = 0;
		ctx->ipv4->tos = 0;
		ctx->ipv4->tot_len = bpf_htons((long)ctx->ptr32 - (long)ctx->ipv4);
		ctx->ipv4->id = 0;
		ctx->ipv4->ttl = 64;

		csum = bpf_csum_diff(0, 0, (void *)ctx->ipv4, sizeof(*ctx->ipv4), 0);
		if (csum < 0)
			goto err;

		ctx->ipv4->check = csum_fold(csum);
	} else if (ctx->ipv6) {
		swap(ctx->ipv6->saddr, ctx->ipv6->daddr);
		ctx->tcp->check = tcp_v6_csum(ctx, csum);

		*(__be32 *)ctx->ipv6 = bpf_htonl(0x60000000);
		ctx->ipv6->payload_len = bpf_htons((long)ctx->ptr32 - (long)ctx->tcp);
		ctx->ipv6->hop_limit = 64;
	}

	swap_array(ctx->eth->h_source, ctx->eth->h_dest);

	if (bpf_skb_change_tail(ctx->skb, (long)ctx->ptr32 - (long)ctx->eth, 0))
		goto err;

	return bpf_redirect(ctx->skb->ifindex, 0);
err:
	return TC_ACT_SHOT;
}

static int tcp_validate_cookie(struct tcp_syncookie *ctx)
{
	u32 cookie = bpf_ntohl(ctx->tcp->ack_seq) - 1;
	u32 seq = bpf_ntohl(ctx->tcp->seq) - 1;
	u64 first = 0, second;
	int mssind;
	u32 hash;

	if (ctx->ipv4)
		first = (u64)ctx->ipv4->saddr << 32 | ctx->ipv4->daddr;
	else if (ctx->ipv6)
		first = (u64)ctx->ipv6->saddr.in6_u.u6_addr8[0] << 32 |
			ctx->ipv6->daddr.in6_u.u6_addr32[0];

	second = (u64)seq << 32 | ctx->tcp->source << 16 | ctx->tcp->dest;
	hash = siphash_2u64(first, second, &test_key_siphash);

	if (ctx->attrs.tstamp_ok)
		hash -= ctx->attrs.rcv_tsecr & COOKIE_MASK;
	else
		hash &= ~COOKIE_MASK;

	hash -= cookie & ~COOKIE_MASK;
	if (hash)
		goto err;

	mssind = (cookie & (3 << 6)) >> 6;
	if (ctx->ipv4)
		ctx->attrs.mss = msstab4[mssind];
	else
		ctx->attrs.mss = msstab6[mssind];

	ctx->attrs.snd_wscale = cookie & BPF_SYNCOOKIE_WSCALE_MASK;
	ctx->attrs.rcv_wscale = ctx->attrs.snd_wscale;
	ctx->attrs.wscale_ok = ctx->attrs.snd_wscale == BPF_SYNCOOKIE_WSCALE_MASK;
	ctx->attrs.sack_ok = cookie & BPF_SYNCOOKIE_SACK;
	ctx->attrs.ecn_ok = cookie & BPF_SYNCOOKIE_ECN;

	return 0;
err:
	return -1;
}

static int tcp_handle_ack(struct tcp_syncookie *ctx)
{
	struct bpf_sock_tuple tuple;
	struct bpf_sock *skc;
	int ret = TC_ACT_OK;
	struct sock *sk;
	u32 tuple_size;

	if (ctx->ipv4) {
		tuple.ipv4.saddr = ctx->ipv4->saddr;
		tuple.ipv4.daddr = ctx->ipv4->daddr;
		tuple.ipv4.sport = ctx->tcp->source;
		tuple.ipv4.dport = ctx->tcp->dest;
		tuple_size = sizeof(tuple.ipv4);
	} else if (ctx->ipv6) {
		__builtin_memcpy(tuple.ipv6.saddr, &ctx->ipv6->saddr, sizeof(tuple.ipv6.saddr));
		__builtin_memcpy(tuple.ipv6.daddr, &ctx->ipv6->daddr, sizeof(tuple.ipv6.daddr));
		tuple.ipv6.sport = ctx->tcp->source;
		tuple.ipv6.dport = ctx->tcp->dest;
		tuple_size = sizeof(tuple.ipv6);
	} else {
		goto out;
	}

	skc = bpf_skc_lookup_tcp(ctx->skb, &tuple, tuple_size, -1, 0);
	if (!skc)
		goto out;

	if (skc->state != TCP_LISTEN)
		goto release;

	sk = (struct sock *)bpf_skc_to_tcp_sock(skc);
	if (!sk)
		goto err;

	if (tcp_validate_header(ctx))
		goto err;

	tcp_parse_options(ctx);

	if (tcp_validate_cookie(ctx))
		goto err;

	ret = bpf_sk_assign_tcp_reqsk(ctx->skb, sk, &ctx->attrs, sizeof(ctx->attrs));
	if (ret < 0)
		goto err;

release:
	bpf_sk_release(skc);
out:
	return ret;

err:
	ret = TC_ACT_SHOT;
	goto release;
}

SEC("tc")
int tcp_custom_syncookie(struct __sk_buff *skb)
{
	struct tcp_syncookie ctx = {
		.skb = skb,
	};

	if (tcp_load_headers(&ctx))
		return TC_ACT_OK;

	if (ctx.tcp->rst)
		return TC_ACT_OK;

	if (ctx.tcp->syn) {
		if (ctx.tcp->ack)
			return TC_ACT_OK;

		handled_syn = true;

		return tcp_handle_syn(&ctx);
	}

	handled_ack = true;

	return tcp_handle_ack(&ctx);
}

char _license[] SEC("license") = "GPL";
