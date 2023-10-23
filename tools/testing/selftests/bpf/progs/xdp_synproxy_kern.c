// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <asm/errno.h>

#define TC_ACT_OK 0
#define TC_ACT_SHOT 2

#define NSEC_PER_SEC 1000000000L

#define ETH_ALEN 6
#define ETH_P_IP 0x0800
#define ETH_P_IPV6 0x86DD

#define tcp_flag_word(tp) (((union tcp_word_hdr *)(tp))->words[3])

#define IP_DF 0x4000
#define IP_MF 0x2000
#define IP_OFFSET 0x1fff

#define NEXTHDR_TCP 6

#define TCPOPT_NOP 1
#define TCPOPT_EOL 0
#define TCPOPT_MSS 2
#define TCPOPT_WINDOW 3
#define TCPOPT_SACK_PERM 4
#define TCPOPT_TIMESTAMP 8

#define TCPOLEN_MSS 4
#define TCPOLEN_WINDOW 3
#define TCPOLEN_SACK_PERM 2
#define TCPOLEN_TIMESTAMP 10

#define TCP_TS_HZ 1000
#define TS_OPT_WSCALE_MASK 0xf
#define TS_OPT_SACK (1 << 4)
#define TS_OPT_ECN (1 << 5)
#define TSBITS 6
#define TSMASK (((__u32)1 << TSBITS) - 1)
#define TCP_MAX_WSCALE 14U

#define IPV4_MAXLEN 60
#define TCP_MAXLEN 60

#define DEFAULT_MSS4 1460
#define DEFAULT_MSS6 1440
#define DEFAULT_WSCALE 7
#define DEFAULT_TTL 64
#define MAX_ALLOWED_PORTS 8

#define swap(a, b) \
	do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

#define __get_unaligned_t(type, ptr) ({						\
	const struct { type x; } __attribute__((__packed__)) *__pptr = (typeof(__pptr))(ptr); \
	__pptr->x;								\
})

#define get_unaligned(ptr) __get_unaligned_t(typeof(*(ptr)), (ptr))

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, __u32);
	__type(value, __u64);
	__uint(max_entries, 2);
} values SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, __u32);
	__type(value, __u16);
	__uint(max_entries, MAX_ALLOWED_PORTS);
} allowed_ports SEC(".maps");

/* Some symbols defined in net/netfilter/nf_conntrack_bpf.c are unavailable in
 * vmlinux.h if CONFIG_NF_CONNTRACK=m, so they are redefined locally.
 */

struct bpf_ct_opts___local {
	s32 netns_id;
	s32 error;
	u8 l4proto;
	u8 dir;
	u8 reserved[2];
} __attribute__((preserve_access_index));

#define BPF_F_CURRENT_NETNS (-1)

extern struct nf_conn *bpf_xdp_ct_lookup(struct xdp_md *xdp_ctx,
					 struct bpf_sock_tuple *bpf_tuple,
					 __u32 len_tuple,
					 struct bpf_ct_opts___local *opts,
					 __u32 len_opts) __ksym;

extern struct nf_conn *bpf_skb_ct_lookup(struct __sk_buff *skb_ctx,
					 struct bpf_sock_tuple *bpf_tuple,
					 u32 len_tuple,
					 struct bpf_ct_opts___local *opts,
					 u32 len_opts) __ksym;

extern void bpf_ct_release(struct nf_conn *ct) __ksym;

static __always_inline void swap_eth_addr(__u8 *a, __u8 *b)
{
	__u8 tmp[ETH_ALEN];

	__builtin_memcpy(tmp, a, ETH_ALEN);
	__builtin_memcpy(a, b, ETH_ALEN);
	__builtin_memcpy(b, tmp, ETH_ALEN);
}

static __always_inline __u16 csum_fold(__u32 csum)
{
	csum = (csum & 0xffff) + (csum >> 16);
	csum = (csum & 0xffff) + (csum >> 16);
	return (__u16)~csum;
}

static __always_inline __u16 csum_tcpudp_magic(__be32 saddr, __be32 daddr,
					       __u32 len, __u8 proto,
					       __u32 csum)
{
	__u64 s = csum;

	s += (__u32)saddr;
	s += (__u32)daddr;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	s += proto + len;
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	s += (proto + len) << 8;
#else
#error Unknown endian
#endif
	s = (s & 0xffffffff) + (s >> 32);
	s = (s & 0xffffffff) + (s >> 32);

	return csum_fold((__u32)s);
}

static __always_inline __u16 csum_ipv6_magic(const struct in6_addr *saddr,
					     const struct in6_addr *daddr,
					     __u32 len, __u8 proto, __u32 csum)
{
	__u64 sum = csum;
	int i;

#pragma unroll
	for (i = 0; i < 4; i++)
		sum += (__u32)saddr->in6_u.u6_addr32[i];

#pragma unroll
	for (i = 0; i < 4; i++)
		sum += (__u32)daddr->in6_u.u6_addr32[i];

	/* Don't combine additions to avoid 32-bit overflow. */
	sum += bpf_htonl(len);
	sum += bpf_htonl(proto);

	sum = (sum & 0xffffffff) + (sum >> 32);
	sum = (sum & 0xffffffff) + (sum >> 32);

	return csum_fold((__u32)sum);
}

static __always_inline __u64 tcp_clock_ns(void)
{
	return bpf_ktime_get_ns();
}

static __always_inline __u32 tcp_ns_to_ts(__u64 ns)
{
	return ns / (NSEC_PER_SEC / TCP_TS_HZ);
}

static __always_inline __u32 tcp_clock_ms(void)
{
	return tcp_ns_to_ts(tcp_clock_ns());
}

struct tcpopt_context {
	__u8 *ptr;
	__u8 *end;
	void *data_end;
	__be32 *tsecr;
	__u8 wscale;
	bool option_timestamp;
	bool option_sack;
};

static int tscookie_tcpopt_parse(struct tcpopt_context *ctx)
{
	__u8 opcode, opsize;

	if (ctx->ptr >= ctx->end)
		return 1;
	if (ctx->ptr >= ctx->data_end)
		return 1;

	opcode = ctx->ptr[0];

	if (opcode == TCPOPT_EOL)
		return 1;
	if (opcode == TCPOPT_NOP) {
		++ctx->ptr;
		return 0;
	}

	if (ctx->ptr + 1 >= ctx->end)
		return 1;
	if (ctx->ptr + 1 >= ctx->data_end)
		return 1;
	opsize = ctx->ptr[1];
	if (opsize < 2)
		return 1;

	if (ctx->ptr + opsize > ctx->end)
		return 1;

	switch (opcode) {
	case TCPOPT_WINDOW:
		if (opsize == TCPOLEN_WINDOW && ctx->ptr + TCPOLEN_WINDOW <= ctx->data_end)
			ctx->wscale = ctx->ptr[2] < TCP_MAX_WSCALE ? ctx->ptr[2] : TCP_MAX_WSCALE;
		break;
	case TCPOPT_TIMESTAMP:
		if (opsize == TCPOLEN_TIMESTAMP && ctx->ptr + TCPOLEN_TIMESTAMP <= ctx->data_end) {
			ctx->option_timestamp = true;
			/* Client's tsval becomes our tsecr. */
			*ctx->tsecr = get_unaligned((__be32 *)(ctx->ptr + 2));
		}
		break;
	case TCPOPT_SACK_PERM:
		if (opsize == TCPOLEN_SACK_PERM)
			ctx->option_sack = true;
		break;
	}

	ctx->ptr += opsize;

	return 0;
}

static int tscookie_tcpopt_parse_batch(__u32 index, void *context)
{
	int i;

	for (i = 0; i < 7; i++)
		if (tscookie_tcpopt_parse(context))
			return 1;
	return 0;
}

static __always_inline bool tscookie_init(struct tcphdr *tcp_header,
					  __u16 tcp_len, __be32 *tsval,
					  __be32 *tsecr, void *data_end)
{
	struct tcpopt_context loop_ctx = {
		.ptr = (__u8 *)(tcp_header + 1),
		.end = (__u8 *)tcp_header + tcp_len,
		.data_end = data_end,
		.tsecr = tsecr,
		.wscale = TS_OPT_WSCALE_MASK,
		.option_timestamp = false,
		.option_sack = false,
	};
	u32 cookie;

	bpf_loop(6, tscookie_tcpopt_parse_batch, &loop_ctx, 0);

	if (!loop_ctx.option_timestamp)
		return false;

	cookie = tcp_clock_ms() & ~TSMASK;
	cookie |= loop_ctx.wscale & TS_OPT_WSCALE_MASK;
	if (loop_ctx.option_sack)
		cookie |= TS_OPT_SACK;
	if (tcp_header->ece && tcp_header->cwr)
		cookie |= TS_OPT_ECN;
	*tsval = bpf_htonl(cookie);

	return true;
}

static __always_inline void values_get_tcpipopts(__u16 *mss, __u8 *wscale,
						 __u8 *ttl, bool ipv6)
{
	__u32 key = 0;
	__u64 *value;

	value = bpf_map_lookup_elem(&values, &key);
	if (value && *value != 0) {
		if (ipv6)
			*mss = (*value >> 32) & 0xffff;
		else
			*mss = *value & 0xffff;
		*wscale = (*value >> 16) & 0xf;
		*ttl = (*value >> 24) & 0xff;
		return;
	}

	*mss = ipv6 ? DEFAULT_MSS6 : DEFAULT_MSS4;
	*wscale = DEFAULT_WSCALE;
	*ttl = DEFAULT_TTL;
}

static __always_inline void values_inc_synacks(void)
{
	__u32 key = 1;
	__u64 *value;

	value = bpf_map_lookup_elem(&values, &key);
	if (value)
		__sync_fetch_and_add(value, 1);
}

static __always_inline bool check_port_allowed(__u16 port)
{
	__u32 i;

	for (i = 0; i < MAX_ALLOWED_PORTS; i++) {
		__u32 key = i;
		__u16 *value;

		value = bpf_map_lookup_elem(&allowed_ports, &key);

		if (!value)
			break;
		/* 0 is a terminator value. Check it first to avoid matching on
		 * a forbidden port == 0 and returning true.
		 */
		if (*value == 0)
			break;

		if (*value == port)
			return true;
	}

	return false;
}

struct header_pointers {
	struct ethhdr *eth;
	struct iphdr *ipv4;
	struct ipv6hdr *ipv6;
	struct tcphdr *tcp;
	__u16 tcp_len;
};

static __always_inline int tcp_dissect(void *data, void *data_end,
				       struct header_pointers *hdr)
{
	hdr->eth = data;
	if (hdr->eth + 1 > data_end)
		return XDP_DROP;

	switch (bpf_ntohs(hdr->eth->h_proto)) {
	case ETH_P_IP:
		hdr->ipv6 = NULL;

		hdr->ipv4 = (void *)hdr->eth + sizeof(*hdr->eth);
		if (hdr->ipv4 + 1 > data_end)
			return XDP_DROP;
		if (hdr->ipv4->ihl * 4 < sizeof(*hdr->ipv4))
			return XDP_DROP;
		if (hdr->ipv4->version != 4)
			return XDP_DROP;

		if (hdr->ipv4->protocol != IPPROTO_TCP)
			return XDP_PASS;

		hdr->tcp = (void *)hdr->ipv4 + hdr->ipv4->ihl * 4;
		break;
	case ETH_P_IPV6:
		hdr->ipv4 = NULL;

		hdr->ipv6 = (void *)hdr->eth + sizeof(*hdr->eth);
		if (hdr->ipv6 + 1 > data_end)
			return XDP_DROP;
		if (hdr->ipv6->version != 6)
			return XDP_DROP;

		/* XXX: Extension headers are not supported and could circumvent
		 * XDP SYN flood protection.
		 */
		if (hdr->ipv6->nexthdr != NEXTHDR_TCP)
			return XDP_PASS;

		hdr->tcp = (void *)hdr->ipv6 + sizeof(*hdr->ipv6);
		break;
	default:
		/* XXX: VLANs will circumvent XDP SYN flood protection. */
		return XDP_PASS;
	}

	if (hdr->tcp + 1 > data_end)
		return XDP_DROP;
	hdr->tcp_len = hdr->tcp->doff * 4;
	if (hdr->tcp_len < sizeof(*hdr->tcp))
		return XDP_DROP;

	return XDP_TX;
}

static __always_inline int tcp_lookup(void *ctx, struct header_pointers *hdr, bool xdp)
{
	struct bpf_ct_opts___local ct_lookup_opts = {
		.netns_id = BPF_F_CURRENT_NETNS,
		.l4proto = IPPROTO_TCP,
	};
	struct bpf_sock_tuple tup = {};
	struct nf_conn *ct;
	__u32 tup_size;

	if (hdr->ipv4) {
		/* TCP doesn't normally use fragments, and XDP can't reassemble
		 * them.
		 */
		if ((hdr->ipv4->frag_off & bpf_htons(IP_DF | IP_MF | IP_OFFSET)) != bpf_htons(IP_DF))
			return XDP_DROP;

		tup.ipv4.saddr = hdr->ipv4->saddr;
		tup.ipv4.daddr = hdr->ipv4->daddr;
		tup.ipv4.sport = hdr->tcp->source;
		tup.ipv4.dport = hdr->tcp->dest;
		tup_size = sizeof(tup.ipv4);
	} else if (hdr->ipv6) {
		__builtin_memcpy(tup.ipv6.saddr, &hdr->ipv6->saddr, sizeof(tup.ipv6.saddr));
		__builtin_memcpy(tup.ipv6.daddr, &hdr->ipv6->daddr, sizeof(tup.ipv6.daddr));
		tup.ipv6.sport = hdr->tcp->source;
		tup.ipv6.dport = hdr->tcp->dest;
		tup_size = sizeof(tup.ipv6);
	} else {
		/* The verifier can't track that either ipv4 or ipv6 is not
		 * NULL.
		 */
		return XDP_ABORTED;
	}
	if (xdp)
		ct = bpf_xdp_ct_lookup(ctx, &tup, tup_size, &ct_lookup_opts, sizeof(ct_lookup_opts));
	else
		ct = bpf_skb_ct_lookup(ctx, &tup, tup_size, &ct_lookup_opts, sizeof(ct_lookup_opts));
	if (ct) {
		unsigned long status = ct->status;

		bpf_ct_release(ct);
		if (status & IPS_CONFIRMED_BIT)
			return XDP_PASS;
	} else if (ct_lookup_opts.error != -ENOENT) {
		return XDP_ABORTED;
	}

	/* error == -ENOENT || !(status & IPS_CONFIRMED_BIT) */
	return XDP_TX;
}

static __always_inline __u8 tcp_mkoptions(__be32 *buf, __be32 *tsopt, __u16 mss,
					  __u8 wscale)
{
	__be32 *start = buf;

	*buf++ = bpf_htonl((TCPOPT_MSS << 24) | (TCPOLEN_MSS << 16) | mss);

	if (!tsopt)
		return buf - start;

	if (tsopt[0] & bpf_htonl(1 << 4))
		*buf++ = bpf_htonl((TCPOPT_SACK_PERM << 24) |
				   (TCPOLEN_SACK_PERM << 16) |
				   (TCPOPT_TIMESTAMP << 8) |
				   TCPOLEN_TIMESTAMP);
	else
		*buf++ = bpf_htonl((TCPOPT_NOP << 24) |
				   (TCPOPT_NOP << 16) |
				   (TCPOPT_TIMESTAMP << 8) |
				   TCPOLEN_TIMESTAMP);
	*buf++ = tsopt[0];
	*buf++ = tsopt[1];

	if ((tsopt[0] & bpf_htonl(0xf)) != bpf_htonl(0xf))
		*buf++ = bpf_htonl((TCPOPT_NOP << 24) |
				   (TCPOPT_WINDOW << 16) |
				   (TCPOLEN_WINDOW << 8) |
				   wscale);

	return buf - start;
}

static __always_inline void tcp_gen_synack(struct tcphdr *tcp_header,
					   __u32 cookie, __be32 *tsopt,
					   __u16 mss, __u8 wscale)
{
	void *tcp_options;

	tcp_flag_word(tcp_header) = TCP_FLAG_SYN | TCP_FLAG_ACK;
	if (tsopt && (tsopt[0] & bpf_htonl(1 << 5)))
		tcp_flag_word(tcp_header) |= TCP_FLAG_ECE;
	tcp_header->doff = 5; /* doff is part of tcp_flag_word. */
	swap(tcp_header->source, tcp_header->dest);
	tcp_header->ack_seq = bpf_htonl(bpf_ntohl(tcp_header->seq) + 1);
	tcp_header->seq = bpf_htonl(cookie);
	tcp_header->window = 0;
	tcp_header->urg_ptr = 0;
	tcp_header->check = 0; /* Calculate checksum later. */

	tcp_options = (void *)(tcp_header + 1);
	tcp_header->doff += tcp_mkoptions(tcp_options, tsopt, mss, wscale);
}

static __always_inline void tcpv4_gen_synack(struct header_pointers *hdr,
					     __u32 cookie, __be32 *tsopt)
{
	__u8 wscale;
	__u16 mss;
	__u8 ttl;

	values_get_tcpipopts(&mss, &wscale, &ttl, false);

	swap_eth_addr(hdr->eth->h_source, hdr->eth->h_dest);

	swap(hdr->ipv4->saddr, hdr->ipv4->daddr);
	hdr->ipv4->check = 0; /* Calculate checksum later. */
	hdr->ipv4->tos = 0;
	hdr->ipv4->id = 0;
	hdr->ipv4->ttl = ttl;

	tcp_gen_synack(hdr->tcp, cookie, tsopt, mss, wscale);

	hdr->tcp_len = hdr->tcp->doff * 4;
	hdr->ipv4->tot_len = bpf_htons(sizeof(*hdr->ipv4) + hdr->tcp_len);
}

static __always_inline void tcpv6_gen_synack(struct header_pointers *hdr,
					     __u32 cookie, __be32 *tsopt)
{
	__u8 wscale;
	__u16 mss;
	__u8 ttl;

	values_get_tcpipopts(&mss, &wscale, &ttl, true);

	swap_eth_addr(hdr->eth->h_source, hdr->eth->h_dest);

	swap(hdr->ipv6->saddr, hdr->ipv6->daddr);
	*(__be32 *)hdr->ipv6 = bpf_htonl(0x60000000);
	hdr->ipv6->hop_limit = ttl;

	tcp_gen_synack(hdr->tcp, cookie, tsopt, mss, wscale);

	hdr->tcp_len = hdr->tcp->doff * 4;
	hdr->ipv6->payload_len = bpf_htons(hdr->tcp_len);
}

static __always_inline int syncookie_handle_syn(struct header_pointers *hdr,
						void *ctx,
						void *data, void *data_end,
						bool xdp)
{
	__u32 old_pkt_size, new_pkt_size;
	/* Unlike clang 10, clang 11 and 12 generate code that doesn't pass the
	 * BPF verifier if tsopt is not volatile. Volatile forces it to store
	 * the pointer value and use it directly, otherwise tcp_mkoptions is
	 * (mis)compiled like this:
	 *   if (!tsopt)
	 *       return buf - start;
	 *   reg = stored_return_value_of_tscookie_init;
	 *   if (reg)
	 *       tsopt = tsopt_buf;
	 *   else
	 *       tsopt = NULL;
	 *   ...
	 *   *buf++ = tsopt[1];
	 * It creates a dead branch where tsopt is assigned NULL, but the
	 * verifier can't prove it's dead and blocks the program.
	 */
	__be32 * volatile tsopt = NULL;
	__be32 tsopt_buf[2] = {};
	__u16 ip_len;
	__u32 cookie;
	__s64 value;

	/* Checksum is not yet verified, but both checksum failure and TCP
	 * header checks return XDP_DROP, so the order doesn't matter.
	 */
	if (hdr->tcp->fin || hdr->tcp->rst)
		return XDP_DROP;

	/* Issue SYN cookies on allowed ports, drop SYN packets on blocked
	 * ports.
	 */
	if (!check_port_allowed(bpf_ntohs(hdr->tcp->dest)))
		return XDP_DROP;

	if (hdr->ipv4) {
		/* Check the IPv4 and TCP checksums before creating a SYNACK. */
		value = bpf_csum_diff(0, 0, (void *)hdr->ipv4, hdr->ipv4->ihl * 4, 0);
		if (value < 0)
			return XDP_ABORTED;
		if (csum_fold(value) != 0)
			return XDP_DROP; /* Bad IPv4 checksum. */

		value = bpf_csum_diff(0, 0, (void *)hdr->tcp, hdr->tcp_len, 0);
		if (value < 0)
			return XDP_ABORTED;
		if (csum_tcpudp_magic(hdr->ipv4->saddr, hdr->ipv4->daddr,
				      hdr->tcp_len, IPPROTO_TCP, value) != 0)
			return XDP_DROP; /* Bad TCP checksum. */

		ip_len = sizeof(*hdr->ipv4);

		value = bpf_tcp_raw_gen_syncookie_ipv4(hdr->ipv4, hdr->tcp,
						       hdr->tcp_len);
	} else if (hdr->ipv6) {
		/* Check the TCP checksum before creating a SYNACK. */
		value = bpf_csum_diff(0, 0, (void *)hdr->tcp, hdr->tcp_len, 0);
		if (value < 0)
			return XDP_ABORTED;
		if (csum_ipv6_magic(&hdr->ipv6->saddr, &hdr->ipv6->daddr,
				    hdr->tcp_len, IPPROTO_TCP, value) != 0)
			return XDP_DROP; /* Bad TCP checksum. */

		ip_len = sizeof(*hdr->ipv6);

		value = bpf_tcp_raw_gen_syncookie_ipv6(hdr->ipv6, hdr->tcp,
						       hdr->tcp_len);
	} else {
		return XDP_ABORTED;
	}

	if (value < 0)
		return XDP_ABORTED;
	cookie = (__u32)value;

	if (tscookie_init((void *)hdr->tcp, hdr->tcp_len,
			  &tsopt_buf[0], &tsopt_buf[1], data_end))
		tsopt = tsopt_buf;

	/* Check that there is enough space for a SYNACK. It also covers
	 * the check that the destination of the __builtin_memmove below
	 * doesn't overflow.
	 */
	if (data + sizeof(*hdr->eth) + ip_len + TCP_MAXLEN > data_end)
		return XDP_ABORTED;

	if (hdr->ipv4) {
		if (hdr->ipv4->ihl * 4 > sizeof(*hdr->ipv4)) {
			struct tcphdr *new_tcp_header;

			new_tcp_header = data + sizeof(*hdr->eth) + sizeof(*hdr->ipv4);
			__builtin_memmove(new_tcp_header, hdr->tcp, sizeof(*hdr->tcp));
			hdr->tcp = new_tcp_header;

			hdr->ipv4->ihl = sizeof(*hdr->ipv4) / 4;
		}

		tcpv4_gen_synack(hdr, cookie, tsopt);
	} else if (hdr->ipv6) {
		tcpv6_gen_synack(hdr, cookie, tsopt);
	} else {
		return XDP_ABORTED;
	}

	/* Recalculate checksums. */
	hdr->tcp->check = 0;
	value = bpf_csum_diff(0, 0, (void *)hdr->tcp, hdr->tcp_len, 0);
	if (value < 0)
		return XDP_ABORTED;
	if (hdr->ipv4) {
		hdr->tcp->check = csum_tcpudp_magic(hdr->ipv4->saddr,
						    hdr->ipv4->daddr,
						    hdr->tcp_len,
						    IPPROTO_TCP,
						    value);

		hdr->ipv4->check = 0;
		value = bpf_csum_diff(0, 0, (void *)hdr->ipv4, sizeof(*hdr->ipv4), 0);
		if (value < 0)
			return XDP_ABORTED;
		hdr->ipv4->check = csum_fold(value);
	} else if (hdr->ipv6) {
		hdr->tcp->check = csum_ipv6_magic(&hdr->ipv6->saddr,
						  &hdr->ipv6->daddr,
						  hdr->tcp_len,
						  IPPROTO_TCP,
						  value);
	} else {
		return XDP_ABORTED;
	}

	/* Set the new packet size. */
	old_pkt_size = data_end - data;
	new_pkt_size = sizeof(*hdr->eth) + ip_len + hdr->tcp->doff * 4;
	if (xdp) {
		if (bpf_xdp_adjust_tail(ctx, new_pkt_size - old_pkt_size))
			return XDP_ABORTED;
	} else {
		if (bpf_skb_change_tail(ctx, new_pkt_size, 0))
			return XDP_ABORTED;
	}

	values_inc_synacks();

	return XDP_TX;
}

static __always_inline int syncookie_handle_ack(struct header_pointers *hdr)
{
	int err;

	if (hdr->tcp->rst)
		return XDP_DROP;

	if (hdr->ipv4)
		err = bpf_tcp_raw_check_syncookie_ipv4(hdr->ipv4, hdr->tcp);
	else if (hdr->ipv6)
		err = bpf_tcp_raw_check_syncookie_ipv6(hdr->ipv6, hdr->tcp);
	else
		return XDP_ABORTED;
	if (err)
		return XDP_DROP;

	return XDP_PASS;
}

static __always_inline int syncookie_part1(void *ctx, void *data, void *data_end,
					   struct header_pointers *hdr, bool xdp)
{
	int ret;

	ret = tcp_dissect(data, data_end, hdr);
	if (ret != XDP_TX)
		return ret;

	ret = tcp_lookup(ctx, hdr, xdp);
	if (ret != XDP_TX)
		return ret;

	/* Packet is TCP and doesn't belong to an established connection. */

	if ((hdr->tcp->syn ^ hdr->tcp->ack) != 1)
		return XDP_DROP;

	/* Grow the TCP header to TCP_MAXLEN to be able to pass any hdr->tcp_len
	 * to bpf_tcp_raw_gen_syncookie_ipv{4,6} and pass the verifier.
	 */
	if (xdp) {
		if (bpf_xdp_adjust_tail(ctx, TCP_MAXLEN - hdr->tcp_len))
			return XDP_ABORTED;
	} else {
		/* Without volatile the verifier throws this error:
		 * R9 32-bit pointer arithmetic prohibited
		 */
		volatile u64 old_len = data_end - data;

		if (bpf_skb_change_tail(ctx, old_len + TCP_MAXLEN - hdr->tcp_len, 0))
			return XDP_ABORTED;
	}

	return XDP_TX;
}

static __always_inline int syncookie_part2(void *ctx, void *data, void *data_end,
					   struct header_pointers *hdr, bool xdp)
{
	if (hdr->ipv4) {
		hdr->eth = data;
		hdr->ipv4 = (void *)hdr->eth + sizeof(*hdr->eth);
		/* IPV4_MAXLEN is needed when calculating checksum.
		 * At least sizeof(struct iphdr) is needed here to access ihl.
		 */
		if ((void *)hdr->ipv4 + IPV4_MAXLEN > data_end)
			return XDP_ABORTED;
		hdr->tcp = (void *)hdr->ipv4 + hdr->ipv4->ihl * 4;
	} else if (hdr->ipv6) {
		hdr->eth = data;
		hdr->ipv6 = (void *)hdr->eth + sizeof(*hdr->eth);
		hdr->tcp = (void *)hdr->ipv6 + sizeof(*hdr->ipv6);
	} else {
		return XDP_ABORTED;
	}

	if ((void *)hdr->tcp + TCP_MAXLEN > data_end)
		return XDP_ABORTED;

	/* We run out of registers, tcp_len gets spilled to the stack, and the
	 * verifier forgets its min and max values checked above in tcp_dissect.
	 */
	hdr->tcp_len = hdr->tcp->doff * 4;
	if (hdr->tcp_len < sizeof(*hdr->tcp))
		return XDP_ABORTED;

	return hdr->tcp->syn ? syncookie_handle_syn(hdr, ctx, data, data_end, xdp) :
			       syncookie_handle_ack(hdr);
}

SEC("xdp")
int syncookie_xdp(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct header_pointers hdr;
	int ret;

	ret = syncookie_part1(ctx, data, data_end, &hdr, true);
	if (ret != XDP_TX)
		return ret;

	data_end = (void *)(long)ctx->data_end;
	data = (void *)(long)ctx->data;

	return syncookie_part2(ctx, data, data_end, &hdr, true);
}

SEC("tc")
int syncookie_tc(struct __sk_buff *skb)
{
	void *data_end = (void *)(long)skb->data_end;
	void *data = (void *)(long)skb->data;
	struct header_pointers hdr;
	int ret;

	ret = syncookie_part1(skb, data, data_end, &hdr, false);
	if (ret != XDP_TX)
		return ret == XDP_PASS ? TC_ACT_OK : TC_ACT_SHOT;

	data_end = (void *)(long)skb->data_end;
	data = (void *)(long)skb->data;

	ret = syncookie_part2(skb, data, data_end, &hdr, false);
	switch (ret) {
	case XDP_PASS:
		return TC_ACT_OK;
	case XDP_TX:
		return bpf_redirect(skb->ifindex, 0);
	default:
		return TC_ACT_SHOT;
	}
}

char _license[] SEC("license") = "GPL";
