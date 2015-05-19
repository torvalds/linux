/* Copyright (c) 2015 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"
#include <uapi/linux/in.h>
#include <uapi/linux/if.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/ipv6.h>
#include <uapi/linux/if_tunnel.h>
#include <uapi/linux/mpls.h>
#define IP_MF		0x2000
#define IP_OFFSET	0x1FFF

#define PROG(F) SEC("socket/"__stringify(F)) int bpf_func_##F

struct bpf_map_def SEC("maps") jmp_table = {
	.type = BPF_MAP_TYPE_PROG_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(u32),
	.max_entries = 8,
};

#define PARSE_VLAN 1
#define PARSE_MPLS 2
#define PARSE_IP 3
#define PARSE_IPV6 4

/* protocol dispatch routine.
 * It tail-calls next BPF program depending on eth proto
 * Note, we could have used:
 * bpf_tail_call(skb, &jmp_table, proto);
 * but it would need large prog_array
 */
static inline void parse_eth_proto(struct __sk_buff *skb, u32 proto)
{
	switch (proto) {
	case ETH_P_8021Q:
	case ETH_P_8021AD:
		bpf_tail_call(skb, &jmp_table, PARSE_VLAN);
		break;
	case ETH_P_MPLS_UC:
	case ETH_P_MPLS_MC:
		bpf_tail_call(skb, &jmp_table, PARSE_MPLS);
		break;
	case ETH_P_IP:
		bpf_tail_call(skb, &jmp_table, PARSE_IP);
		break;
	case ETH_P_IPV6:
		bpf_tail_call(skb, &jmp_table, PARSE_IPV6);
		break;
	}
}

struct vlan_hdr {
	__be16 h_vlan_TCI;
	__be16 h_vlan_encapsulated_proto;
};

struct flow_keys {
	__be32 src;
	__be32 dst;
	union {
		__be32 ports;
		__be16 port16[2];
	};
	__u32 ip_proto;
};

static inline int ip_is_fragment(struct __sk_buff *ctx, __u64 nhoff)
{
	return load_half(ctx, nhoff + offsetof(struct iphdr, frag_off))
		& (IP_MF | IP_OFFSET);
}

static inline __u32 ipv6_addr_hash(struct __sk_buff *ctx, __u64 off)
{
	__u64 w0 = load_word(ctx, off);
	__u64 w1 = load_word(ctx, off + 4);
	__u64 w2 = load_word(ctx, off + 8);
	__u64 w3 = load_word(ctx, off + 12);

	return (__u32)(w0 ^ w1 ^ w2 ^ w3);
}

struct globals {
	struct flow_keys flow;
	__u32 nhoff;
};

struct bpf_map_def SEC("maps") percpu_map = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(__u32),
	.value_size = sizeof(struct globals),
	.max_entries = 32,
};

/* user poor man's per_cpu until native support is ready */
static struct globals *this_cpu_globals(void)
{
	u32 key = bpf_get_smp_processor_id();

	return bpf_map_lookup_elem(&percpu_map, &key);
}

/* some simple stats for user space consumption */
struct pair {
	__u64 packets;
	__u64 bytes;
};

struct bpf_map_def SEC("maps") hash_map = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(struct flow_keys),
	.value_size = sizeof(struct pair),
	.max_entries = 1024,
};

static void update_stats(struct __sk_buff *skb, struct globals *g)
{
	struct flow_keys key = g->flow;
	struct pair *value;

	value = bpf_map_lookup_elem(&hash_map, &key);
	if (value) {
		__sync_fetch_and_add(&value->packets, 1);
		__sync_fetch_and_add(&value->bytes, skb->len);
	} else {
		struct pair val = {1, skb->len};

		bpf_map_update_elem(&hash_map, &key, &val, BPF_ANY);
	}
}

static __always_inline void parse_ip_proto(struct __sk_buff *skb,
					   struct globals *g, __u32 ip_proto)
{
	__u32 nhoff = g->nhoff;
	int poff;

	switch (ip_proto) {
	case IPPROTO_GRE: {
		struct gre_hdr {
			__be16 flags;
			__be16 proto;
		};

		__u32 gre_flags = load_half(skb,
					    nhoff + offsetof(struct gre_hdr, flags));
		__u32 gre_proto = load_half(skb,
					    nhoff + offsetof(struct gre_hdr, proto));

		if (gre_flags & (GRE_VERSION|GRE_ROUTING))
			break;

		nhoff += 4;
		if (gre_flags & GRE_CSUM)
			nhoff += 4;
		if (gre_flags & GRE_KEY)
			nhoff += 4;
		if (gre_flags & GRE_SEQ)
			nhoff += 4;

		g->nhoff = nhoff;
		parse_eth_proto(skb, gre_proto);
		break;
	}
	case IPPROTO_IPIP:
		parse_eth_proto(skb, ETH_P_IP);
		break;
	case IPPROTO_IPV6:
		parse_eth_proto(skb, ETH_P_IPV6);
		break;
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		g->flow.ports = load_word(skb, nhoff);
	case IPPROTO_ICMP:
		g->flow.ip_proto = ip_proto;
		update_stats(skb, g);
		break;
	default:
		break;
	}
}

PROG(PARSE_IP)(struct __sk_buff *skb)
{
	struct globals *g = this_cpu_globals();
	__u32 nhoff, verlen, ip_proto;

	if (!g)
		return 0;

	nhoff = g->nhoff;

	if (unlikely(ip_is_fragment(skb, nhoff)))
		return 0;

	ip_proto = load_byte(skb, nhoff + offsetof(struct iphdr, protocol));

	if (ip_proto != IPPROTO_GRE) {
		g->flow.src = load_word(skb, nhoff + offsetof(struct iphdr, saddr));
		g->flow.dst = load_word(skb, nhoff + offsetof(struct iphdr, daddr));
	}

	verlen = load_byte(skb, nhoff + 0/*offsetof(struct iphdr, ihl)*/);
	nhoff += (verlen & 0xF) << 2;

	g->nhoff = nhoff;
	parse_ip_proto(skb, g, ip_proto);
	return 0;
}

PROG(PARSE_IPV6)(struct __sk_buff *skb)
{
	struct globals *g = this_cpu_globals();
	__u32 nhoff, ip_proto;

	if (!g)
		return 0;

	nhoff = g->nhoff;

	ip_proto = load_byte(skb,
			     nhoff + offsetof(struct ipv6hdr, nexthdr));
	g->flow.src = ipv6_addr_hash(skb,
				     nhoff + offsetof(struct ipv6hdr, saddr));
	g->flow.dst = ipv6_addr_hash(skb,
				     nhoff + offsetof(struct ipv6hdr, daddr));
	nhoff += sizeof(struct ipv6hdr);

	g->nhoff = nhoff;
	parse_ip_proto(skb, g, ip_proto);
	return 0;
}

PROG(PARSE_VLAN)(struct __sk_buff *skb)
{
	struct globals *g = this_cpu_globals();
	__u32 nhoff, proto;

	if (!g)
		return 0;

	nhoff = g->nhoff;

	proto = load_half(skb, nhoff + offsetof(struct vlan_hdr,
						h_vlan_encapsulated_proto));
	nhoff += sizeof(struct vlan_hdr);
	g->nhoff = nhoff;

	parse_eth_proto(skb, proto);

	return 0;
}

PROG(PARSE_MPLS)(struct __sk_buff *skb)
{
	struct globals *g = this_cpu_globals();
	__u32 nhoff, label;

	if (!g)
		return 0;

	nhoff = g->nhoff;

	label = load_word(skb, nhoff);
	nhoff += sizeof(struct mpls_label);
	g->nhoff = nhoff;

	if (label & MPLS_LS_S_MASK) {
		__u8 verlen = load_byte(skb, nhoff);
		if ((verlen & 0xF0) == 4)
			parse_eth_proto(skb, ETH_P_IP);
		else
			parse_eth_proto(skb, ETH_P_IPV6);
	} else {
		parse_eth_proto(skb, ETH_P_MPLS_UC);
	}

	return 0;
}

SEC("socket/0")
int main_prog(struct __sk_buff *skb)
{
	struct globals *g = this_cpu_globals();
	__u32 nhoff = ETH_HLEN;
	__u32 proto = load_half(skb, 12);

	if (!g)
		return 0;

	g->nhoff = nhoff;
	parse_eth_proto(skb, proto);
	return 0;
}

char _license[] SEC("license") = "GPL";
