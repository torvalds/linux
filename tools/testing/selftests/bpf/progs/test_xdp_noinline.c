// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Facebook
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <linux/pkt_cls.h>
#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

static __always_inline __u32 rol32(__u32 word, unsigned int shift)
{
	return (word << shift) | (word >> ((-shift) & 31));
}

/* copy paste of jhash from kernel sources to make sure llvm
 * can compile it into valid sequence of bpf instructions
 */
#define __jhash_mix(a, b, c)			\
{						\
	a -= c;  a ^= rol32(c, 4);  c += b;	\
	b -= a;  b ^= rol32(a, 6);  a += c;	\
	c -= b;  c ^= rol32(b, 8);  b += a;	\
	a -= c;  a ^= rol32(c, 16); c += b;	\
	b -= a;  b ^= rol32(a, 19); a += c;	\
	c -= b;  c ^= rol32(b, 4);  b += a;	\
}

#define __jhash_final(a, b, c)			\
{						\
	c ^= b; c -= rol32(b, 14);		\
	a ^= c; a -= rol32(c, 11);		\
	b ^= a; b -= rol32(a, 25);		\
	c ^= b; c -= rol32(b, 16);		\
	a ^= c; a -= rol32(c, 4);		\
	b ^= a; b -= rol32(a, 14);		\
	c ^= b; c -= rol32(b, 24);		\
}

#define JHASH_INITVAL		0xdeadbeef

typedef unsigned int u32;

static __noinline
u32 jhash(const void *key, u32 length, u32 initval)
{
	u32 a, b, c;
	const unsigned char *k = key;

	a = b = c = JHASH_INITVAL + length + initval;

	while (length > 12) {
		a += *(u32 *)(k);
		b += *(u32 *)(k + 4);
		c += *(u32 *)(k + 8);
		__jhash_mix(a, b, c);
		length -= 12;
		k += 12;
	}
	switch (length) {
	case 12: c += (u32)k[11]<<24;
	case 11: c += (u32)k[10]<<16;
	case 10: c += (u32)k[9]<<8;
	case 9:  c += k[8];
	case 8:  b += (u32)k[7]<<24;
	case 7:  b += (u32)k[6]<<16;
	case 6:  b += (u32)k[5]<<8;
	case 5:  b += k[4];
	case 4:  a += (u32)k[3]<<24;
	case 3:  a += (u32)k[2]<<16;
	case 2:  a += (u32)k[1]<<8;
	case 1:  a += k[0];
		 __jhash_final(a, b, c);
	case 0: /* Nothing left to add */
		break;
	}

	return c;
}

__noinline
u32 __jhash_nwords(u32 a, u32 b, u32 c, u32 initval)
{
	a += initval;
	b += initval;
	c += initval;
	__jhash_final(a, b, c);
	return c;
}

__noinline
u32 jhash_2words(u32 a, u32 b, u32 initval)
{
	return __jhash_nwords(a, b, 0, initval + JHASH_INITVAL + (2 << 2));
}

struct flow_key {
	union {
		__be32 src;
		__be32 srcv6[4];
	};
	union {
		__be32 dst;
		__be32 dstv6[4];
	};
	union {
		__u32 ports;
		__u16 port16[2];
	};
	__u8 proto;
};

struct packet_description {
	struct flow_key flow;
	__u8 flags;
};

struct ctl_value {
	union {
		__u64 value;
		__u32 ifindex;
		__u8 mac[6];
	};
};

struct vip_definition {
	union {
		__be32 vip;
		__be32 vipv6[4];
	};
	__u16 port;
	__u16 family;
	__u8 proto;
};

struct vip_meta {
	__u32 flags;
	__u32 vip_num;
};

struct real_pos_lru {
	__u32 pos;
	__u64 atime;
};

struct real_definition {
	union {
		__be32 dst;
		__be32 dstv6[4];
	};
	__u8 flags;
};

struct lb_stats {
	__u64 v2;
	__u64 v1;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 512);
	__type(key, struct vip_definition);
	__type(value, struct vip_meta);
} vip_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 300);
	__uint(map_flags, 1U << 1);
	__type(key, struct flow_key);
	__type(value, struct real_pos_lru);
} lru_cache SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 12 * 655);
	__type(key, __u32);
	__type(value, __u32);
} ch_rings SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 40);
	__type(key, __u32);
	__type(value, struct real_definition);
} reals SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 515);
	__type(key, __u32);
	__type(value, struct lb_stats);
} stats SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 16);
	__type(key, __u32);
	__type(value, struct ctl_value);
} ctl_array SEC(".maps");

struct eth_hdr {
	unsigned char eth_dest[6];
	unsigned char eth_source[6];
	unsigned short eth_proto;
};

static __noinline __u64 calc_offset(bool is_ipv6, bool is_icmp)
{
	__u64 off = sizeof(struct eth_hdr);
	if (is_ipv6) {
		off += sizeof(struct ipv6hdr);
		if (is_icmp)
			off += sizeof(struct icmp6hdr) + sizeof(struct ipv6hdr);
	} else {
		off += sizeof(struct iphdr);
		if (is_icmp)
			off += sizeof(struct icmphdr) + sizeof(struct iphdr);
	}
	return off;
}

static __attribute__ ((noinline))
bool parse_udp(void *data, void *data_end,
	       bool is_ipv6, struct packet_description *pckt)
{

	bool is_icmp = !((pckt->flags & (1 << 0)) == 0);
	__u64 off = calc_offset(is_ipv6, is_icmp);
	struct udphdr *udp;
	udp = data + off;

	if (udp + 1 > data_end)
		return false;
	if (!is_icmp) {
		pckt->flow.port16[0] = udp->source;
		pckt->flow.port16[1] = udp->dest;
	} else {
		pckt->flow.port16[0] = udp->dest;
		pckt->flow.port16[1] = udp->source;
	}
	return true;
}

static __attribute__ ((noinline))
bool parse_tcp(void *data, void *data_end,
	       bool is_ipv6, struct packet_description *pckt)
{

	bool is_icmp = !((pckt->flags & (1 << 0)) == 0);
	__u64 off = calc_offset(is_ipv6, is_icmp);
	struct tcphdr *tcp;

	tcp = data + off;
	if (tcp + 1 > data_end)
		return false;
	if (tcp->syn)
		pckt->flags |= (1 << 1);
	if (!is_icmp) {
		pckt->flow.port16[0] = tcp->source;
		pckt->flow.port16[1] = tcp->dest;
	} else {
		pckt->flow.port16[0] = tcp->dest;
		pckt->flow.port16[1] = tcp->source;
	}
	return true;
}

static __attribute__ ((noinline))
bool encap_v6(struct xdp_md *xdp, struct ctl_value *cval,
	      struct packet_description *pckt,
	      struct real_definition *dst, __u32 pkt_bytes)
{
	struct eth_hdr *new_eth;
	struct eth_hdr *old_eth;
	struct ipv6hdr *ip6h;
	__u32 ip_suffix;
	void *data_end;
	void *data;

	if (bpf_xdp_adjust_head(xdp, 0 - (int)sizeof(struct ipv6hdr)))
		return false;
	data = (void *)(long)xdp->data;
	data_end = (void *)(long)xdp->data_end;
	new_eth = data;
	ip6h = data + sizeof(struct eth_hdr);
	old_eth = data + sizeof(struct ipv6hdr);
	if (new_eth + 1 > data_end ||
	    old_eth + 1 > data_end || ip6h + 1 > data_end)
		return false;
	memcpy(new_eth->eth_dest, cval->mac, 6);
	memcpy(new_eth->eth_source, old_eth->eth_dest, 6);
	new_eth->eth_proto = 56710;
	ip6h->version = 6;
	ip6h->priority = 0;
	memset(ip6h->flow_lbl, 0, sizeof(ip6h->flow_lbl));

	ip6h->nexthdr = IPPROTO_IPV6;
	ip_suffix = pckt->flow.srcv6[3] ^ pckt->flow.port16[0];
	ip6h->payload_len =
	    bpf_htons(pkt_bytes + sizeof(struct ipv6hdr));
	ip6h->hop_limit = 4;

	ip6h->saddr.in6_u.u6_addr32[0] = 1;
	ip6h->saddr.in6_u.u6_addr32[1] = 2;
	ip6h->saddr.in6_u.u6_addr32[2] = 3;
	ip6h->saddr.in6_u.u6_addr32[3] = ip_suffix;
	memcpy(ip6h->daddr.in6_u.u6_addr32, dst->dstv6, 16);
	return true;
}

static __attribute__ ((noinline))
bool encap_v4(struct xdp_md *xdp, struct ctl_value *cval,
	      struct packet_description *pckt,
	      struct real_definition *dst, __u32 pkt_bytes)
{

	__u32 ip_suffix = bpf_ntohs(pckt->flow.port16[0]);
	struct eth_hdr *new_eth;
	struct eth_hdr *old_eth;
	__u16 *next_iph_u16;
	struct iphdr *iph;
	__u32 csum = 0;
	void *data_end;
	void *data;

	ip_suffix <<= 15;
	ip_suffix ^= pckt->flow.src;
	if (bpf_xdp_adjust_head(xdp, 0 - (int)sizeof(struct iphdr)))
		return false;
	data = (void *)(long)xdp->data;
	data_end = (void *)(long)xdp->data_end;
	new_eth = data;
	iph = data + sizeof(struct eth_hdr);
	old_eth = data + sizeof(struct iphdr);
	if (new_eth + 1 > data_end ||
	    old_eth + 1 > data_end || iph + 1 > data_end)
		return false;
	memcpy(new_eth->eth_dest, cval->mac, 6);
	memcpy(new_eth->eth_source, old_eth->eth_dest, 6);
	new_eth->eth_proto = 8;
	iph->version = 4;
	iph->ihl = 5;
	iph->frag_off = 0;
	iph->protocol = IPPROTO_IPIP;
	iph->check = 0;
	iph->tos = 1;
	iph->tot_len = bpf_htons(pkt_bytes + sizeof(struct iphdr));
	/* don't update iph->daddr, since it will overwrite old eth_proto
	 * and multiple iterations of bpf_prog_run() will fail
	 */

	iph->saddr = ((0xFFFF0000 & ip_suffix) | 4268) ^ dst->dst;
	iph->ttl = 4;

	next_iph_u16 = (__u16 *) iph;
#pragma clang loop unroll(full)
	for (int i = 0; i < sizeof(struct iphdr) >> 1; i++)
		csum += *next_iph_u16++;
	iph->check = ~((csum & 0xffff) + (csum >> 16));
	if (bpf_xdp_adjust_head(xdp, (int)sizeof(struct iphdr)))
		return false;
	return true;
}

static __attribute__ ((noinline))
int swap_mac_and_send(void *data, void *data_end)
{
	unsigned char tmp_mac[6];
	struct eth_hdr *eth;

	eth = data;
	memcpy(tmp_mac, eth->eth_source, 6);
	memcpy(eth->eth_source, eth->eth_dest, 6);
	memcpy(eth->eth_dest, tmp_mac, 6);
	return XDP_TX;
}

static __attribute__ ((noinline))
int send_icmp_reply(void *data, void *data_end)
{
	struct icmphdr *icmp_hdr;
	__u16 *next_iph_u16;
	__u32 tmp_addr = 0;
	struct iphdr *iph;
	__u32 csum = 0;
	__u64 off = 0;

	if (data + sizeof(struct eth_hdr)
	     + sizeof(struct iphdr) + sizeof(struct icmphdr) > data_end)
		return XDP_DROP;
	off += sizeof(struct eth_hdr);
	iph = data + off;
	off += sizeof(struct iphdr);
	icmp_hdr = data + off;
	icmp_hdr->type = 0;
	icmp_hdr->checksum += 0x0007;
	iph->ttl = 4;
	tmp_addr = iph->daddr;
	iph->daddr = iph->saddr;
	iph->saddr = tmp_addr;
	iph->check = 0;
	next_iph_u16 = (__u16 *) iph;
#pragma clang loop unroll(full)
	for (int i = 0; i < sizeof(struct iphdr) >> 1; i++)
		csum += *next_iph_u16++;
	iph->check = ~((csum & 0xffff) + (csum >> 16));
	return swap_mac_and_send(data, data_end);
}

static __attribute__ ((noinline))
int send_icmp6_reply(void *data, void *data_end)
{
	struct icmp6hdr *icmp_hdr;
	struct ipv6hdr *ip6h;
	__be32 tmp_addr[4];
	__u64 off = 0;

	if (data + sizeof(struct eth_hdr)
	     + sizeof(struct ipv6hdr) + sizeof(struct icmp6hdr) > data_end)
		return XDP_DROP;
	off += sizeof(struct eth_hdr);
	ip6h = data + off;
	off += sizeof(struct ipv6hdr);
	icmp_hdr = data + off;
	icmp_hdr->icmp6_type = 129;
	icmp_hdr->icmp6_cksum -= 0x0001;
	ip6h->hop_limit = 4;
	memcpy(tmp_addr, ip6h->saddr.in6_u.u6_addr32, 16);
	memcpy(ip6h->saddr.in6_u.u6_addr32, ip6h->daddr.in6_u.u6_addr32, 16);
	memcpy(ip6h->daddr.in6_u.u6_addr32, tmp_addr, 16);
	return swap_mac_and_send(data, data_end);
}

static __attribute__ ((noinline))
int parse_icmpv6(void *data, void *data_end, __u64 off,
		 struct packet_description *pckt)
{
	struct icmp6hdr *icmp_hdr;
	struct ipv6hdr *ip6h;

	icmp_hdr = data + off;
	if (icmp_hdr + 1 > data_end)
		return XDP_DROP;
	if (icmp_hdr->icmp6_type == 128)
		return send_icmp6_reply(data, data_end);
	if (icmp_hdr->icmp6_type != 3)
		return XDP_PASS;
	off += sizeof(struct icmp6hdr);
	ip6h = data + off;
	if (ip6h + 1 > data_end)
		return XDP_DROP;
	pckt->flow.proto = ip6h->nexthdr;
	pckt->flags |= (1 << 0);
	memcpy(pckt->flow.srcv6, ip6h->daddr.in6_u.u6_addr32, 16);
	memcpy(pckt->flow.dstv6, ip6h->saddr.in6_u.u6_addr32, 16);
	return -1;
}

static __attribute__ ((noinline))
int parse_icmp(void *data, void *data_end, __u64 off,
	       struct packet_description *pckt)
{
	struct icmphdr *icmp_hdr;
	struct iphdr *iph;

	icmp_hdr = data + off;
	if (icmp_hdr + 1 > data_end)
		return XDP_DROP;
	if (icmp_hdr->type == 8)
		return send_icmp_reply(data, data_end);
	if ((icmp_hdr->type != 3) || (icmp_hdr->code != 4))
		return XDP_PASS;
	off += sizeof(struct icmphdr);
	iph = data + off;
	if (iph + 1 > data_end)
		return XDP_DROP;
	if (iph->ihl != 5)
		return XDP_DROP;
	pckt->flow.proto = iph->protocol;
	pckt->flags |= (1 << 0);
	pckt->flow.src = iph->daddr;
	pckt->flow.dst = iph->saddr;
	return -1;
}

static __attribute__ ((noinline))
__u32 get_packet_hash(struct packet_description *pckt,
		      bool hash_16bytes)
{
	if (hash_16bytes)
		return jhash_2words(jhash(pckt->flow.srcv6, 16, 12),
				    pckt->flow.ports, 24);
	else
		return jhash_2words(pckt->flow.src, pckt->flow.ports,
				    24);
}

__attribute__ ((noinline))
static bool get_packet_dst(struct real_definition **real,
			   struct packet_description *pckt,
			   struct vip_meta *vip_info,
			   bool is_ipv6, void *lru_map)
{
	struct real_pos_lru new_dst_lru = { };
	bool hash_16bytes = is_ipv6;
	__u32 *real_pos, hash, key;
	__u64 cur_time;

	if (vip_info->flags & (1 << 2))
		hash_16bytes = 1;
	if (vip_info->flags & (1 << 3)) {
		pckt->flow.port16[0] = pckt->flow.port16[1];
		memset(pckt->flow.srcv6, 0, 16);
	}
	hash = get_packet_hash(pckt, hash_16bytes);
	if (hash != 0x358459b7 /* jhash of ipv4 packet */  &&
	    hash != 0x2f4bc6bb /* jhash of ipv6 packet */)
		return false;
	key = 2 * vip_info->vip_num + hash % 2;
	real_pos = bpf_map_lookup_elem(&ch_rings, &key);
	if (!real_pos)
		return false;
	key = *real_pos;
	*real = bpf_map_lookup_elem(&reals, &key);
	if (!(*real))
		return false;
	if (!(vip_info->flags & (1 << 1))) {
		__u32 conn_rate_key = 512 + 2;
		struct lb_stats *conn_rate_stats =
		    bpf_map_lookup_elem(&stats, &conn_rate_key);

		if (!conn_rate_stats)
			return true;
		cur_time = bpf_ktime_get_ns();
		if ((cur_time - conn_rate_stats->v2) >> 32 > 0xffFFFF) {
			conn_rate_stats->v1 = 1;
			conn_rate_stats->v2 = cur_time;
		} else {
			conn_rate_stats->v1 += 1;
			if (conn_rate_stats->v1 >= 1)
				return true;
		}
		if (pckt->flow.proto == IPPROTO_UDP)
			new_dst_lru.atime = cur_time;
		new_dst_lru.pos = key;
		bpf_map_update_elem(lru_map, &pckt->flow, &new_dst_lru, 0);
	}
	return true;
}

__attribute__ ((noinline))
static void connection_table_lookup(struct real_definition **real,
				    struct packet_description *pckt,
				    void *lru_map)
{

	struct real_pos_lru *dst_lru;
	__u64 cur_time;
	__u32 key;

	dst_lru = bpf_map_lookup_elem(lru_map, &pckt->flow);
	if (!dst_lru)
		return;
	if (pckt->flow.proto == IPPROTO_UDP) {
		cur_time = bpf_ktime_get_ns();
		if (cur_time - dst_lru->atime > 300000)
			return;
		dst_lru->atime = cur_time;
	}
	key = dst_lru->pos;
	*real = bpf_map_lookup_elem(&reals, &key);
}

/* don't believe your eyes!
 * below function has 6 arguments whereas bpf and llvm allow maximum of 5
 * but since it's _static_ llvm can optimize one argument away
 */
__attribute__ ((noinline))
static int process_l3_headers_v6(struct packet_description *pckt,
				 __u8 *protocol, __u64 off,
				 __u16 *pkt_bytes, void *data,
				 void *data_end)
{
	struct ipv6hdr *ip6h;
	__u64 iph_len;
	int action;

	ip6h = data + off;
	if (ip6h + 1 > data_end)
		return XDP_DROP;
	iph_len = sizeof(struct ipv6hdr);
	*protocol = ip6h->nexthdr;
	pckt->flow.proto = *protocol;
	*pkt_bytes = bpf_ntohs(ip6h->payload_len);
	off += iph_len;
	if (*protocol == 45) {
		return XDP_DROP;
	} else if (*protocol == 59) {
		action = parse_icmpv6(data, data_end, off, pckt);
		if (action >= 0)
			return action;
	} else {
		memcpy(pckt->flow.srcv6, ip6h->saddr.in6_u.u6_addr32, 16);
		memcpy(pckt->flow.dstv6, ip6h->daddr.in6_u.u6_addr32, 16);
	}
	return -1;
}

__attribute__ ((noinline))
static int process_l3_headers_v4(struct packet_description *pckt,
				 __u8 *protocol, __u64 off,
				 __u16 *pkt_bytes, void *data,
				 void *data_end)
{
	struct iphdr *iph;
	int action;

	iph = data + off;
	if (iph + 1 > data_end)
		return XDP_DROP;
	if (iph->ihl != 5)
		return XDP_DROP;
	*protocol = iph->protocol;
	pckt->flow.proto = *protocol;
	*pkt_bytes = bpf_ntohs(iph->tot_len);
	off += 20;
	if (iph->frag_off & 65343)
		return XDP_DROP;
	if (*protocol == IPPROTO_ICMP) {
		action = parse_icmp(data, data_end, off, pckt);
		if (action >= 0)
			return action;
	} else {
		pckt->flow.src = iph->saddr;
		pckt->flow.dst = iph->daddr;
	}
	return -1;
}

__attribute__ ((noinline))
static int process_packet(void *data, __u64 off, void *data_end,
			  bool is_ipv6, struct xdp_md *xdp)
{

	struct real_definition *dst = NULL;
	struct packet_description pckt = { };
	struct vip_definition vip = { };
	struct lb_stats *data_stats;
	void *lru_map = &lru_cache;
	struct vip_meta *vip_info;
	__u32 lru_stats_key = 513;
	__u32 mac_addr_pos = 0;
	__u32 stats_key = 512;
	struct ctl_value *cval;
	__u16 pkt_bytes;
	__u8 protocol;
	__u32 vip_num;
	int action;

	if (is_ipv6)
		action = process_l3_headers_v6(&pckt, &protocol, off,
					       &pkt_bytes, data, data_end);
	else
		action = process_l3_headers_v4(&pckt, &protocol, off,
					       &pkt_bytes, data, data_end);
	if (action >= 0)
		return action;
	protocol = pckt.flow.proto;
	if (protocol == IPPROTO_TCP) {
		if (!parse_tcp(data, data_end, is_ipv6, &pckt))
			return XDP_DROP;
	} else if (protocol == IPPROTO_UDP) {
		if (!parse_udp(data, data_end, is_ipv6, &pckt))
			return XDP_DROP;
	} else {
		return XDP_TX;
	}

	if (is_ipv6)
		memcpy(vip.vipv6, pckt.flow.dstv6, 16);
	else
		vip.vip = pckt.flow.dst;
	vip.port = pckt.flow.port16[1];
	vip.proto = pckt.flow.proto;
	vip_info = bpf_map_lookup_elem(&vip_map, &vip);
	if (!vip_info) {
		vip.port = 0;
		vip_info = bpf_map_lookup_elem(&vip_map, &vip);
		if (!vip_info)
			return XDP_PASS;
		if (!(vip_info->flags & (1 << 4)))
			pckt.flow.port16[1] = 0;
	}
	if (data_end - data > 1400)
		return XDP_DROP;
	data_stats = bpf_map_lookup_elem(&stats, &stats_key);
	if (!data_stats)
		return XDP_DROP;
	data_stats->v1 += 1;
	if (!dst) {
		if (vip_info->flags & (1 << 0))
			pckt.flow.port16[0] = 0;
		if (!(pckt.flags & (1 << 1)) && !(vip_info->flags & (1 << 1)))
			connection_table_lookup(&dst, &pckt, lru_map);
		if (dst)
			goto out;
		if (pckt.flow.proto == IPPROTO_TCP) {
			struct lb_stats *lru_stats =
			    bpf_map_lookup_elem(&stats, &lru_stats_key);

			if (!lru_stats)
				return XDP_DROP;
			if (pckt.flags & (1 << 1))
				lru_stats->v1 += 1;
			else
				lru_stats->v2 += 1;
		}
		if (!get_packet_dst(&dst, &pckt, vip_info, is_ipv6, lru_map))
			return XDP_DROP;
		data_stats->v2 += 1;
	}
out:
	cval = bpf_map_lookup_elem(&ctl_array, &mac_addr_pos);
	if (!cval)
		return XDP_DROP;
	if (dst->flags & (1 << 0)) {
		if (!encap_v6(xdp, cval, &pckt, dst, pkt_bytes))
			return XDP_DROP;
	} else {
		if (!encap_v4(xdp, cval, &pckt, dst, pkt_bytes))
			return XDP_DROP;
	}
	vip_num = vip_info->vip_num;
	data_stats = bpf_map_lookup_elem(&stats, &vip_num);
	if (!data_stats)
		return XDP_DROP;
	data_stats->v1 += 1;
	data_stats->v2 += pkt_bytes;

	data = (void *)(long)xdp->data;
	data_end = (void *)(long)xdp->data_end;
	if (data + 4 > data_end)
		return XDP_DROP;
	*(u32 *)data = dst->dst;
	return XDP_DROP;
}

SEC("xdp")
int balancer_ingress_v4(struct xdp_md *ctx)
{
	void *data = (void *)(long)ctx->data;
	void *data_end = (void *)(long)ctx->data_end;
	struct eth_hdr *eth = data;
	__u32 eth_proto;
	__u32 nh_off;

	nh_off = sizeof(struct eth_hdr);
	if (data + nh_off > data_end)
		return XDP_DROP;
	eth_proto = bpf_ntohs(eth->eth_proto);
	if (eth_proto == ETH_P_IP)
		return process_packet(data, nh_off, data_end, 0, ctx);
	else
		return XDP_DROP;
}

SEC("xdp")
int balancer_ingress_v6(struct xdp_md *ctx)
{
	void *data = (void *)(long)ctx->data;
	void *data_end = (void *)(long)ctx->data_end;
	struct eth_hdr *eth = data;
	__u32 eth_proto;
	__u32 nh_off;

	nh_off = sizeof(struct eth_hdr);
	if (data + nh_off > data_end)
		return XDP_DROP;
	eth_proto = bpf_ntohs(eth->eth_proto);
	if (eth_proto == ETH_P_IPV6)
		return process_packet(data, nh_off, data_end, 1, ctx);
	else
		return XDP_DROP;
}

char _license[] SEC("license") = "GPL";
