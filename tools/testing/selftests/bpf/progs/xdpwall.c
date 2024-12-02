// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <stdbool.h>
#include <stdint.h>
#include <linux/stddef.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/bpf.h>
#include <linux/types.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

enum pkt_parse_err {
	NO_ERR,
	BAD_IP6_HDR,
	BAD_IP4GUE_HDR,
	BAD_IP6GUE_HDR,
};

enum pkt_flag {
	TUNNEL = 0x1,
	TCP_SYN = 0x2,
	QUIC_INITIAL_FLAG = 0x4,
	TCP_ACK = 0x8,
	TCP_RST = 0x10
};

struct v4_lpm_key {
	__u32 prefixlen;
	__u32 src;
};

struct v4_lpm_val {
	struct v4_lpm_key key;
	__u8 val;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16);
	__type(key, struct in6_addr);
	__type(value, bool);
} v6_addr_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16);
	__type(key, __u32);
	__type(value, bool);
} v4_addr_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LPM_TRIE);
	__uint(max_entries, 16);
	__uint(key_size, sizeof(struct v4_lpm_key));
	__uint(value_size, sizeof(struct v4_lpm_val));
	__uint(map_flags, BPF_F_NO_PREALLOC);
} v4_lpm_val_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 16);
	__type(key, int);
	__type(value, __u8);
} tcp_port_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 16);
	__type(key, int);
	__type(value, __u16);
} udp_port_map SEC(".maps");

enum ip_type { V4 = 1, V6 = 2 };

struct fw_match_info {
	__u8 v4_src_ip_match;
	__u8 v6_src_ip_match;
	__u8 v4_src_prefix_match;
	__u8 v4_dst_prefix_match;
	__u8 tcp_dp_match;
	__u16 udp_sp_match;
	__u16 udp_dp_match;
	bool is_tcp;
	bool is_tcp_syn;
};

struct pkt_info {
	enum ip_type type;
	union {
		struct iphdr *ipv4;
		struct ipv6hdr *ipv6;
	} ip;
	int sport;
	int dport;
	__u16 trans_hdr_offset;
	__u8 proto;
	__u8 flags;
};

static __always_inline struct ethhdr *parse_ethhdr(void *data, void *data_end)
{
	struct ethhdr *eth = data;

	if (eth + 1 > data_end)
		return NULL;

	return eth;
}

static __always_inline __u8 filter_ipv6_addr(const struct in6_addr *ipv6addr)
{
	__u8 *leaf;

	leaf = bpf_map_lookup_elem(&v6_addr_map, ipv6addr);

	return leaf ? *leaf : 0;
}

static __always_inline __u8 filter_ipv4_addr(const __u32 ipaddr)
{
	__u8 *leaf;

	leaf = bpf_map_lookup_elem(&v4_addr_map, &ipaddr);

	return leaf ? *leaf : 0;
}

static __always_inline __u8 filter_ipv4_lpm(const __u32 ipaddr)
{
	struct v4_lpm_key v4_key = {};
	struct v4_lpm_val *lpm_val;

	v4_key.src = ipaddr;
	v4_key.prefixlen = 32;

	lpm_val = bpf_map_lookup_elem(&v4_lpm_val_map, &v4_key);

	return lpm_val ? lpm_val->val : 0;
}


static __always_inline void
filter_src_dst_ip(struct pkt_info* info, struct fw_match_info* match_info)
{
	if (info->type == V6) {
		match_info->v6_src_ip_match =
			filter_ipv6_addr(&info->ip.ipv6->saddr);
	} else if (info->type == V4) {
		match_info->v4_src_ip_match =
			filter_ipv4_addr(info->ip.ipv4->saddr);
		match_info->v4_src_prefix_match =
			filter_ipv4_lpm(info->ip.ipv4->saddr);
		match_info->v4_dst_prefix_match =
			filter_ipv4_lpm(info->ip.ipv4->daddr);
	}
}

static __always_inline void *
get_transport_hdr(__u16 offset, void *data, void *data_end)
{
	if (offset > 255 || data + offset > data_end)
		return NULL;

	return data + offset;
}

static __always_inline bool tcphdr_only_contains_flag(struct tcphdr *tcp,
						      __u32 FLAG)
{
	return (tcp_flag_word(tcp) &
		(TCP_FLAG_ACK | TCP_FLAG_RST | TCP_FLAG_SYN | TCP_FLAG_FIN)) == FLAG;
}

static __always_inline void set_tcp_flags(struct pkt_info *info,
					  struct tcphdr *tcp) {
	if (tcphdr_only_contains_flag(tcp, TCP_FLAG_SYN))
		info->flags |= TCP_SYN;
	else if (tcphdr_only_contains_flag(tcp, TCP_FLAG_ACK))
		info->flags |= TCP_ACK;
	else if (tcphdr_only_contains_flag(tcp, TCP_FLAG_RST))
		info->flags |= TCP_RST;
}

static __always_inline bool
parse_tcp(struct pkt_info *info, void *transport_hdr, void *data_end)
{
	struct tcphdr *tcp = transport_hdr;

	if (tcp + 1 > data_end)
		return false;

	info->sport = bpf_ntohs(tcp->source);
	info->dport = bpf_ntohs(tcp->dest);
	set_tcp_flags(info, tcp);

	return true;
}

static __always_inline bool
parse_udp(struct pkt_info *info, void *transport_hdr, void *data_end)
{
	struct udphdr *udp = transport_hdr;

	if (udp + 1 > data_end)
		return false;

	info->sport = bpf_ntohs(udp->source);
	info->dport = bpf_ntohs(udp->dest);

	return true;
}

static __always_inline __u8 filter_tcp_port(int port)
{
	__u8 *leaf = bpf_map_lookup_elem(&tcp_port_map, &port);

	return leaf ? *leaf : 0;
}

static __always_inline __u16 filter_udp_port(int port)
{
	__u16 *leaf = bpf_map_lookup_elem(&udp_port_map, &port);

	return leaf ? *leaf : 0;
}

static __always_inline bool
filter_transport_hdr(void *transport_hdr, void *data_end,
		     struct pkt_info *info, struct fw_match_info *match_info)
{
	if (info->proto == IPPROTO_TCP) {
		if (!parse_tcp(info, transport_hdr, data_end))
			return false;

		match_info->is_tcp = true;
		match_info->is_tcp_syn = (info->flags & TCP_SYN) > 0;

		match_info->tcp_dp_match = filter_tcp_port(info->dport);
	} else if (info->proto == IPPROTO_UDP) {
		if (!parse_udp(info, transport_hdr, data_end))
			return false;

		match_info->udp_dp_match = filter_udp_port(info->dport);
		match_info->udp_sp_match = filter_udp_port(info->sport);
	}

	return true;
}

static __always_inline __u8
parse_gue_v6(struct pkt_info *info, struct ipv6hdr *ip6h, void *data_end)
{
	struct udphdr *udp = (struct udphdr *)(ip6h + 1);
	void *encap_data = udp + 1;

	if (udp + 1 > data_end)
		return BAD_IP6_HDR;

	if (udp->dest != bpf_htons(6666))
		return NO_ERR;

	info->flags |= TUNNEL;

	if (encap_data + 1 > data_end)
		return BAD_IP6GUE_HDR;

	if (*(__u8 *)encap_data & 0x30) {
		struct ipv6hdr *inner_ip6h = encap_data;

		if (inner_ip6h + 1 > data_end)
			return BAD_IP6GUE_HDR;

		info->type = V6;
		info->proto = inner_ip6h->nexthdr;
		info->ip.ipv6 = inner_ip6h;
		info->trans_hdr_offset += sizeof(struct ipv6hdr) + sizeof(struct udphdr);
	} else {
		struct iphdr *inner_ip4h = encap_data;

		if (inner_ip4h + 1 > data_end)
			return BAD_IP6GUE_HDR;

		info->type = V4;
		info->proto = inner_ip4h->protocol;
		info->ip.ipv4 = inner_ip4h;
		info->trans_hdr_offset += sizeof(struct iphdr) + sizeof(struct udphdr);
	}

	return NO_ERR;
}

static __always_inline __u8 parse_ipv6_gue(struct pkt_info *info,
					   void *data, void *data_end)
{
	struct ipv6hdr *ip6h = data + sizeof(struct ethhdr);

	if (ip6h + 1 > data_end)
		return BAD_IP6_HDR;

	info->proto = ip6h->nexthdr;
	info->ip.ipv6 = ip6h;
	info->type = V6;
	info->trans_hdr_offset = sizeof(struct ethhdr) + sizeof(struct ipv6hdr);

	if (info->proto == IPPROTO_UDP)
		return parse_gue_v6(info, ip6h, data_end);

	return NO_ERR;
}

SEC("xdp")
int edgewall(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)(ctx->data_end);
	void *data = (void *)(long)(ctx->data);
	struct fw_match_info match_info = {};
	struct pkt_info info = {};
	__u8 parse_err = NO_ERR;
	void *transport_hdr;
	struct ethhdr *eth;
	bool filter_res;
	__u32 proto;

	eth = parse_ethhdr(data, data_end);
	if (!eth)
		return XDP_DROP;

	proto = eth->h_proto;
	if (proto != bpf_htons(ETH_P_IPV6))
		return XDP_DROP;

	if (parse_ipv6_gue(&info, data, data_end))
		return XDP_DROP;

	if (info.proto == IPPROTO_ICMPV6)
		return XDP_PASS;

	if (info.proto != IPPROTO_TCP && info.proto != IPPROTO_UDP)
		return XDP_DROP;

	filter_src_dst_ip(&info, &match_info);

	transport_hdr = get_transport_hdr(info.trans_hdr_offset, data,
					  data_end);
	if (!transport_hdr)
		return XDP_DROP;

	filter_res = filter_transport_hdr(transport_hdr, data_end,
					  &info, &match_info);
	if (!filter_res)
		return XDP_DROP;

	if (match_info.is_tcp && !match_info.is_tcp_syn)
		return XDP_PASS;

	return XDP_DROP;
}

char LICENSE[] SEC("license") = "GPL";
