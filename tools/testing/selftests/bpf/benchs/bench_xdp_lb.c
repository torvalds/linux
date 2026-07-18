// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <argp.h>
#include <string.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include "bench.h"
#include "bench_bpf_timing.h"
#include "xdp_lb_bench.skel.h"
#include "xdp_lb_bench_common.h"
#include "bpf_util.h"

#define IP4(a, b, c, d) (((__u32)(a) << 24) | ((__u32)(b) << 16) | ((__u32)(c) << 8) | (__u32)(d))

#define IP6(a, b, c, d)  { (__u32)(a), (__u32)(b), (__u32)(c), (__u32)(d) }

#define TNL_DST		IP4(192, 168, 1, 2)
#define REAL_INDEX	1
#define REAL_INDEX_V6	2
#define MAX_PKT_SIZE	256
#define IP_MF		0x2000

static const __u32 tnl_dst_v6[4] = { 0xfd000000, 0, 0, 2 };

static const __u8 lb_mac[ETH_ALEN]	= {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
static const __u8 client_mac[ETH_ALEN]	= {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
static const __u8 router_mac[ETH_ALEN]	= {0xde, 0xad, 0xbe, 0xef, 0x00, 0x01};

enum scenario_id {
	S_TCP_V4_LRU_HIT,
	S_TCP_V4_CH,
	S_TCP_V6_LRU_HIT,
	S_TCP_V6_CH,
	S_UDP_V4_LRU_HIT,
	S_UDP_V6_LRU_HIT,
	S_TCP_V4V6_LRU_HIT,
	S_TCP_V4_LRU_DIVERSE,
	S_TCP_V4_CH_DIVERSE,
	S_TCP_V6_LRU_DIVERSE,
	S_TCP_V6_CH_DIVERSE,
	S_UDP_V4_LRU_DIVERSE,
	S_TCP_V4_LRU_MISS,
	S_UDP_V4_LRU_MISS,
	S_TCP_V4_LRU_WARMUP,
	S_TCP_V4_SYN,
	S_TCP_V4_RST_MISS,
	S_PASS_V4_NO_VIP,
	S_PASS_V6_NO_VIP,
	S_PASS_V4_ICMP,
	S_PASS_NON_IP,
	S_DROP_V4_FRAG,
	S_DROP_V4_OPTIONS,
	S_DROP_V6_FRAG,
	NUM_SCENARIOS,
};

enum lru_miss_type {
	LRU_MISS_AUTO = 0,	/* compute from scenario flags (default) */
	LRU_MISS_NONE,		/* 0 misses (all LRU hits) */
	LRU_MISS_ALL,		/* batch_iters+1 misses (every op misses) */
	LRU_MISS_FIRST,		/* 1 miss (first miss, then hits) */
};

#define S_BASE_ENCAP_V4							\
	.expected_retval = XDP_TX, .expect_encap = true,		\
	.tunnel_dst = TNL_DST

#define S_BASE_ENCAP_V6							\
	.expected_retval = XDP_TX, .expect_encap = true,		\
	.is_v6 = true, .encap_v6_outer = true,				\
	.tunnel_dst_v6 = { 0xfd000000, 0, 0, 2 }

#define S_BASE_ENCAP_V4V6						\
	.expected_retval = XDP_TX, .expect_encap = true,		\
	.encap_v6_outer = true,						\
	.tunnel_dst_v6 = { 0xfd000000, 0, 0, 2 }

struct test_scenario {
	const char *name;
	const char *description;
	int         expected_retval;
	bool        expect_encap;
	bool        is_v6;
	__u32       vip_addr;
	__u32       src_addr;
	__u32       tunnel_dst;
	__u32       vip_addr_v6[4];
	__u32       src_addr_v6[4];
	__u32       tunnel_dst_v6[4];
	__u16       dst_port;
	__u16       src_port;
	__u8        ip_proto;
	__u32       vip_flags;
	__u32       vip_num;
	bool        prepopulate_lru;
	bool        set_frag;
	__u16       eth_proto;
	bool        encap_v6_outer;
	__u32       flow_mask;
	bool        cold_lru;
	bool        set_syn;
	bool        set_rst;
	bool        set_ip_options;
	__u32       fixed_batch_iters;	/* 0 = auto-calibrate, >0 = use this value */
	enum lru_miss_type lru_miss;	/* expected LRU miss pattern */
};

static const struct test_scenario scenarios[NUM_SCENARIOS] = {
	/* Single-flow baseline */
	[S_TCP_V4_LRU_HIT] = {
		S_BASE_ENCAP_V4, .ip_proto = IPPROTO_TCP,
		.name        = "tcp-v4-lru-hit",
		.description = "IPv4 TCP, LRU hit, IPIP encap",
		.vip_addr    = IP4(10, 10, 1, 1), .dst_port = 80,
		.src_addr    = IP4(10, 10, 2, 1), .src_port = 12345,
		.prepopulate_lru = true, .lru_miss = LRU_MISS_NONE,
	},
	[S_TCP_V4_CH] = {
		S_BASE_ENCAP_V4, .ip_proto = IPPROTO_TCP,
		.name        = "tcp-v4-ch",
		.description = "IPv4 TCP, CH (LRU bypass), IPIP encap",
		.vip_addr    = IP4(10, 10, 1, 2), .dst_port = 80,
		.src_addr    = IP4(10, 10, 2, 2), .src_port = 54321,
		.vip_flags   = F_LRU_BYPASS, .vip_num = 1,
		.lru_miss    = LRU_MISS_ALL,
	},
	[S_TCP_V6_LRU_HIT] = {
		S_BASE_ENCAP_V6, .ip_proto = IPPROTO_TCP,
		.name        = "tcp-v6-lru-hit",
		.description = "IPv6 TCP, LRU hit, IP6IP6 encap",
		.vip_addr_v6 = IP6(0xfd000100, 0, 0, 1), .dst_port = 80,
		.src_addr_v6 = IP6(0xfd000200, 0, 0, 1), .src_port = 12345,
		.vip_num     = 10,
		.prepopulate_lru = true, .lru_miss = LRU_MISS_NONE,
	},
	[S_TCP_V6_CH] = {
		S_BASE_ENCAP_V6, .ip_proto = IPPROTO_TCP,
		.name        = "tcp-v6-ch",
		.description = "IPv6 TCP, CH (LRU bypass), IP6IP6 encap",
		.vip_addr_v6 = IP6(0xfd000100, 0, 0, 2), .dst_port = 80,
		.src_addr_v6 = IP6(0xfd000200, 0, 0, 2), .src_port = 54321,
		.vip_flags   = F_LRU_BYPASS, .vip_num = 12,
		.lru_miss    = LRU_MISS_ALL,
	},
	[S_UDP_V4_LRU_HIT] = {
		S_BASE_ENCAP_V4, .ip_proto = IPPROTO_UDP,
		.name        = "udp-v4-lru-hit",
		.description = "IPv4 UDP, LRU hit, IPIP encap",
		.vip_addr    = IP4(10, 10, 1, 1), .dst_port = 443,
		.src_addr    = IP4(10, 10, 3, 1), .src_port = 11111,
		.vip_num     = 2,
		.prepopulate_lru = true, .lru_miss = LRU_MISS_NONE,
	},
	[S_UDP_V6_LRU_HIT] = {
		S_BASE_ENCAP_V6, .ip_proto = IPPROTO_UDP,
		.name        = "udp-v6-lru-hit",
		.description = "IPv6 UDP, LRU hit, IP6IP6 encap",
		.vip_addr_v6 = IP6(0xfd000100, 0, 0, 1), .dst_port = 443,
		.src_addr_v6 = IP6(0xfd000200, 0, 0, 3), .src_port = 22222,
		.vip_num     = 14,
		.prepopulate_lru = true, .lru_miss = LRU_MISS_NONE,
	},
	[S_TCP_V4V6_LRU_HIT] = {
		S_BASE_ENCAP_V4V6, .ip_proto = IPPROTO_TCP,
		.name        = "tcp-v4v6-lru-hit",
		.description = "IPv4 TCP, LRU hit, IPv4-in-IPv6 encap",
		.vip_addr    = IP4(10, 10, 1, 4), .dst_port = 80,
		.src_addr    = IP4(10, 10, 2, 4), .src_port = 12347,
		.vip_num     = 13,
		.prepopulate_lru = true, .lru_miss = LRU_MISS_NONE,
	},

	/* Diverse flows (4K src addrs) */
	[S_TCP_V4_LRU_DIVERSE] = {
		S_BASE_ENCAP_V4, .ip_proto = IPPROTO_TCP,
		.name        = "tcp-v4-lru-diverse",
		.description = "IPv4 TCP, diverse flows, warm LRU",
		.vip_addr    = IP4(10, 10, 1, 1), .dst_port = 80,
		.src_addr    = IP4(10, 10, 2, 1), .src_port = 12345,
		.prepopulate_lru = true, .flow_mask = 0xFFF,
		.lru_miss    = LRU_MISS_NONE,
	},
	[S_TCP_V4_CH_DIVERSE] = {
		S_BASE_ENCAP_V4, .ip_proto = IPPROTO_TCP,
		.name        = "tcp-v4-ch-diverse",
		.description = "IPv4 TCP, diverse flows, CH (LRU bypass)",
		.vip_addr    = IP4(10, 10, 1, 2), .dst_port = 80,
		.src_addr    = IP4(10, 10, 2, 2), .src_port = 54321,
		.vip_flags   = F_LRU_BYPASS, .vip_num = 1,
		.flow_mask   = 0xFFF, .lru_miss = LRU_MISS_ALL,
	},
	[S_TCP_V6_LRU_DIVERSE] = {
		S_BASE_ENCAP_V6, .ip_proto = IPPROTO_TCP,
		.name        = "tcp-v6-lru-diverse",
		.description = "IPv6 TCP, diverse flows, warm LRU",
		.vip_addr_v6 = IP6(0xfd000100, 0, 0, 1), .dst_port = 80,
		.src_addr_v6 = IP6(0xfd000200, 0, 0, 1), .src_port = 12345,
		.vip_num     = 10,
		.prepopulate_lru = true, .flow_mask = 0xFFF,
		.lru_miss    = LRU_MISS_NONE,
	},
	[S_TCP_V6_CH_DIVERSE] = {
		S_BASE_ENCAP_V6, .ip_proto = IPPROTO_TCP,
		.name        = "tcp-v6-ch-diverse",
		.description = "IPv6 TCP, diverse flows, CH (LRU bypass)",
		.vip_addr_v6 = IP6(0xfd000100, 0, 0, 2), .dst_port = 80,
		.src_addr_v6 = IP6(0xfd000200, 0, 0, 2), .src_port = 54321,
		.vip_flags   = F_LRU_BYPASS, .vip_num = 12,
		.flow_mask   = 0xFFF, .lru_miss = LRU_MISS_ALL,
	},
	[S_UDP_V4_LRU_DIVERSE] = {
		S_BASE_ENCAP_V4, .ip_proto = IPPROTO_UDP,
		.name        = "udp-v4-lru-diverse",
		.description = "IPv4 UDP, diverse flows, warm LRU",
		.vip_addr    = IP4(10, 10, 1, 1), .dst_port = 443,
		.src_addr    = IP4(10, 10, 3, 1), .src_port = 11111,
		.vip_num     = 2,
		.prepopulate_lru = true, .flow_mask = 0xFFF,
		.lru_miss    = LRU_MISS_NONE,
	},

	/* LRU stress */
	[S_TCP_V4_LRU_MISS] = {
		S_BASE_ENCAP_V4, .ip_proto = IPPROTO_TCP,
		.name        = "tcp-v4-lru-miss",
		.description = "IPv4 TCP, LRU miss (16M flow space), CH lookup",
		.vip_addr    = IP4(10, 10, 1, 1), .dst_port = 80,
		.src_addr    = IP4(10, 10, 2, 1), .src_port = 12345,
		.flow_mask   = 0xFFFFFF, .cold_lru = true,
		.lru_miss    = LRU_MISS_FIRST,
	},
	[S_UDP_V4_LRU_MISS] = {
		S_BASE_ENCAP_V4, .ip_proto = IPPROTO_UDP,
		.name        = "udp-v4-lru-miss",
		.description = "IPv4 UDP, LRU miss (16M flow space), CH lookup",
		.vip_addr    = IP4(10, 10, 1, 1), .dst_port = 443,
		.src_addr    = IP4(10, 10, 3, 1), .src_port = 11111,
		.vip_num     = 2,
		.flow_mask   = 0xFFFFFF, .cold_lru = true,
		.lru_miss    = LRU_MISS_FIRST,
	},
	[S_TCP_V4_LRU_WARMUP] = {
		S_BASE_ENCAP_V4, .ip_proto = IPPROTO_TCP,
		.name        = "tcp-v4-lru-warmup",
		.description = "IPv4 TCP, 4K flows, ~50% LRU miss",
		.vip_addr    = IP4(10, 10, 1, 1), .dst_port = 80,
		.src_addr    = IP4(10, 10, 2, 1), .src_port = 12345,
		.flow_mask   = 0xFFF, .cold_lru = true,
		.fixed_batch_iters = 6500,
		.lru_miss    = LRU_MISS_FIRST,
	},

	/* TCP flags */
	[S_TCP_V4_SYN] = {
		S_BASE_ENCAP_V4, .ip_proto = IPPROTO_TCP,
		.name        = "tcp-v4-syn",
		.description = "IPv4 TCP SYN, skip LRU, CH + LRU insert",
		.vip_addr    = IP4(10, 10, 1, 1), .dst_port = 80,
		.src_addr    = IP4(10, 10, 8, 2), .src_port = 60001,
		.set_syn     = true, .lru_miss = LRU_MISS_ALL,
	},
	[S_TCP_V4_RST_MISS] = {
		S_BASE_ENCAP_V4, .ip_proto = IPPROTO_TCP,
		.name        = "tcp-v4-rst-miss",
		.description = "IPv4 TCP RST, CH lookup, no LRU insert",
		.vip_addr    = IP4(10, 10, 1, 1), .dst_port = 80,
		.src_addr    = IP4(10, 10, 8, 1), .src_port = 60000,
		.flow_mask   = 0xFFFFFF, .cold_lru = true,
		.set_rst     = true, .lru_miss = LRU_MISS_ALL,
	},

	/* Early exits */
	[S_PASS_V4_NO_VIP] = {
		.name            = "pass-v4-no-vip",
		.description     = "IPv4 TCP, unknown VIP, XDP_PASS",
		.expected_retval = XDP_PASS,
		.ip_proto        = IPPROTO_TCP,
		.vip_addr        = IP4(10, 10, 9, 9), .dst_port = 80,
		.src_addr        = IP4(10, 10, 4, 1), .src_port = 33333,
	},
	[S_PASS_V6_NO_VIP] = {
		.name            = "pass-v6-no-vip",
		.description     = "IPv6 TCP, unknown VIP, XDP_PASS",
		.expected_retval = XDP_PASS, .is_v6 = true,
		.ip_proto        = IPPROTO_TCP,
		.vip_addr_v6     = IP6(0xfd009900, 0, 0, 1), .dst_port = 80,
		.src_addr_v6     = IP6(0xfd000400, 0, 0, 1), .src_port = 33333,
	},
	[S_PASS_V4_ICMP] = {
		.name            = "pass-v4-icmp",
		.description     = "IPv4 ICMP, non-TCP/UDP protocol, XDP_PASS",
		.expected_retval = XDP_PASS,
		.ip_proto        = IPPROTO_ICMP,
		.vip_addr        = IP4(10, 10, 1, 1),
		.src_addr        = IP4(10, 10, 6, 1),
	},
	[S_PASS_NON_IP] = {
		.name            = "pass-non-ip",
		.description     = "Non-IP (ARP), earliest XDP_PASS exit",
		.expected_retval = XDP_PASS,
		.eth_proto       = ETH_P_ARP,
	},
	[S_DROP_V4_FRAG] = {
		.name            = "drop-v4-frag",
		.description     = "IPv4 fragmented, XDP_DROP",
		.expected_retval = XDP_DROP, .ip_proto = IPPROTO_TCP,
		.vip_addr        = IP4(10, 10, 1, 1), .dst_port = 80,
		.src_addr        = IP4(10, 10, 5, 1), .src_port = 44444,
		.set_frag        = true,
	},
	[S_DROP_V4_OPTIONS] = {
		.name            = "drop-v4-options",
		.description     = "IPv4 with IP options (ihl>5), XDP_DROP",
		.expected_retval = XDP_DROP, .ip_proto = IPPROTO_TCP,
		.vip_addr        = IP4(10, 10, 1, 1), .dst_port = 80,
		.src_addr        = IP4(10, 10, 7, 1), .src_port = 55555,
		.set_ip_options  = true,
	},
	[S_DROP_V6_FRAG] = {
		.name            = "drop-v6-frag",
		.description     = "IPv6 fragment extension header, XDP_DROP",
		.expected_retval = XDP_DROP, .is_v6 = true,
		.ip_proto        = IPPROTO_TCP,
		.vip_addr_v6     = IP6(0xfd000100, 0, 0, 1), .dst_port = 80,
		.src_addr_v6     = IP6(0xfd000500, 0, 0, 1), .src_port = 44444,
		.set_frag        = true,
	},
};

#define MAX_ENCAP_SIZE	(MAX_PKT_SIZE + sizeof(struct ipv6hdr))

static __u8  pkt_buf[NUM_SCENARIOS][MAX_PKT_SIZE];
static __u32 pkt_len[NUM_SCENARIOS];
static __u8  expected_buf[NUM_SCENARIOS][MAX_ENCAP_SIZE];
static __u32 expected_len[NUM_SCENARIOS];

static int lru_inner_fds[BENCH_NR_CPUS];
static int nr_inner_maps;

static struct ctx {
	struct xdp_lb_bench *skel;
	struct bpf_bench_timing timing;
	int prog_fd;
} ctx;

static struct {
	int   scenario;
	bool  machine_readable;
} args = {
	.scenario = -1,
};

static __u16 ip_checksum(const void *hdr, int len)
{
	const __u16 *p = hdr;
	__u32 csum = 0;
	int i;

	for (i = 0; i < len / 2; i++)
		csum += p[i];

	while (csum >> 16)
		csum = (csum & 0xffff) + (csum >> 16);

	return ~csum;
}

static void htonl_v6(__be32 dst[4], const __u32 src[4])
{
	int i;

	for (i = 0; i < 4; i++)
		dst[i] = htonl(src[i]);
}

static void build_flow_key(struct flow_key *fk, const struct test_scenario *sc)
{
	memset(fk, 0, sizeof(*fk));
	if (sc->is_v6) {
		htonl_v6(fk->srcv6, sc->src_addr_v6);
		htonl_v6(fk->dstv6, sc->vip_addr_v6);
	} else {
		fk->src = htonl(sc->src_addr);
		fk->dst = htonl(sc->vip_addr);
	}
	fk->proto = sc->ip_proto;
	fk->port16[0] = htons(sc->src_port);
	fk->port16[1] = htons(sc->dst_port);
}

static void build_l4(const struct test_scenario *sc, __u8 *p, __u32 *off)
{
	if (sc->ip_proto == IPPROTO_TCP) {
		struct tcphdr tcp = {};

		tcp.source = htons(sc->src_port);
		tcp.dest   = htons(sc->dst_port);
		tcp.doff   = 5;
		tcp.syn    = sc->set_syn ? 1 : 0;
		tcp.rst    = sc->set_rst ? 1 : 0;
		tcp.window = htons(8192);
		memcpy(p + *off, &tcp, sizeof(tcp));
		*off += sizeof(tcp);
	} else if (sc->ip_proto == IPPROTO_UDP) {
		struct udphdr udp = {};

		udp.source = htons(sc->src_port);
		udp.dest   = htons(sc->dst_port);
		udp.len    = htons(sizeof(udp) + 16);
		memcpy(p + *off, &udp, sizeof(udp));
		*off += sizeof(udp);
	}
}

static void build_packet(int idx)
{
	const struct test_scenario *sc = &scenarios[idx];
	__u8 *p = pkt_buf[idx];
	struct ethhdr eth = {};
	__u16 proto;
	__u32 off = 0;

	memcpy(eth.h_dest, lb_mac, ETH_ALEN);
	memcpy(eth.h_source, client_mac, ETH_ALEN);

	if (sc->eth_proto)
		proto = sc->eth_proto;
	else if (sc->is_v6)
		proto = ETH_P_IPV6;
	else
		proto = ETH_P_IP;

	eth.h_proto = htons(proto);
	memcpy(p, &eth, sizeof(eth));
	off += sizeof(eth);

	if (proto != ETH_P_IP && proto != ETH_P_IPV6) {
		memcpy(p + off, "bench___payload!", 16);
		off += 16;
		pkt_len[idx] = off;
		return;
	}

	if (sc->is_v6) {
		struct ipv6hdr ip6h = {};
		__u32 ip6_off = off;

		ip6h.version  = 6;
		ip6h.nexthdr  = sc->set_frag ? 44 : sc->ip_proto;
		ip6h.hop_limit = 64;
		htonl_v6((__be32 *)&ip6h.saddr, sc->src_addr_v6);
		htonl_v6((__be32 *)&ip6h.daddr, sc->vip_addr_v6);
		off += sizeof(ip6h);

		if (sc->set_frag) {
			memset(p + off, 0, 8);
			p[off] = sc->ip_proto;
			off += 8;
		}

		build_l4(sc, p, &off);

		memcpy(p + off, "bench___payload!", 16);
		off += 16;

		ip6h.payload_len = htons(off - ip6_off - sizeof(ip6h));
		memcpy(p + ip6_off, &ip6h, sizeof(ip6h));
	} else {
		struct iphdr iph = {};
		__u32 ip_off = off;

		iph.version  = 4;
		iph.ihl      = sc->set_ip_options ? 6 : 5;
		iph.ttl      = 64;
		iph.protocol = sc->ip_proto;
		iph.saddr    = htonl(sc->src_addr);
		iph.daddr    = htonl(sc->vip_addr);
		iph.frag_off = sc->set_frag ? htons(IP_MF) : 0;
		off += sizeof(iph);

		if (sc->set_ip_options) {
			/* NOP option padding (4 bytes = 1 word) */
			__u32 nop = htonl(0x01010101);

			memcpy(p + off, &nop, sizeof(nop));
			off += sizeof(nop);
		}

		build_l4(sc, p, &off);

		memcpy(p + off, "bench___payload!", 16);
		off += 16;

		iph.tot_len = htons(off - ip_off);
		iph.check   = ip_checksum(&iph, sizeof(iph));
		memcpy(p + ip_off, &iph, sizeof(iph));
	}

	pkt_len[idx] = off;
}

static void populate_vip(struct xdp_lb_bench *skel, const struct test_scenario *sc)
{
	struct vip_definition key = {};
	struct vip_meta val = {};
	int err;

	if (sc->is_v6)
		htonl_v6(key.vipv6, sc->vip_addr_v6);
	else
		key.vip = htonl(sc->vip_addr);
	key.port  = htons(sc->dst_port);
	key.proto = sc->ip_proto;
	val.flags   = sc->vip_flags;
	val.vip_num = sc->vip_num;

	err = bpf_map_update_elem(bpf_map__fd(skel->maps.vip_map), &key, &val, BPF_ANY);
	if (err) {
		fprintf(stderr, "vip_map [%s]: %s\n", sc->name, strerror(errno));
		exit(1);
	}
}

static void create_per_cpu_lru_maps(struct xdp_lb_bench *skel)
{
	int outer_fd = bpf_map__fd(skel->maps.lru_mapping);
	unsigned int nr_cpus = bpf_num_possible_cpus();
	int i, inner_fd, err;
	__u32 cpu;

	if (nr_cpus > BENCH_NR_CPUS)
		nr_cpus = BENCH_NR_CPUS;

	for (i = 0; i < (int)nr_cpus; i++) {
		LIBBPF_OPTS(bpf_map_create_opts, opts);

		inner_fd = bpf_map_create(BPF_MAP_TYPE_LRU_HASH, "lru_inner",
					  sizeof(struct flow_key),
					  sizeof(struct real_pos_lru),
					  DEFAULT_LRU_SIZE, &opts);
		if (inner_fd < 0) {
			fprintf(stderr, "lru_inner[%d]: %s\n", i, strerror(errno));
			exit(1);
		}

		cpu = i;
		err = bpf_map_update_elem(outer_fd, &cpu, &inner_fd, BPF_ANY);
		if (err) {
			fprintf(stderr, "lru_mapping[%d]: %s\n", i, strerror(errno));
			close(inner_fd);
			exit(1);
		}

		lru_inner_fds[i] = inner_fd;
	}

	nr_inner_maps = nr_cpus;
}

static __u64 ktime_get_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (__u64)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void populate_lru(const struct test_scenario *sc, __u32 real_idx)
{
	struct real_pos_lru lru = { .pos = real_idx };
	struct flow_key fk;
	int i, err;

	if (sc->ip_proto == IPPROTO_UDP)
		lru.atime = ktime_get_ns();

	build_flow_key(&fk, sc);

	/* Insert into every per-CPU inner LRU so the entry is found
	 * regardless of which CPU runs the BPF program.
	 */
	for (i = 0; i < nr_inner_maps; i++) {
		err = bpf_map_update_elem(lru_inner_fds[i], &fk, &lru, BPF_ANY);
		if (err) {
			fprintf(stderr, "lru_inner[%d] [%s]: %s\n", i, sc->name,
				strerror(errno));
			exit(1);
		}
	}
}

static void populate_maps(struct xdp_lb_bench *skel)
{
	struct real_definition real_v4 = {};
	struct real_definition real_v6 = {};
	struct ctl_value cval = {};
	__u32 key, real_idx = REAL_INDEX;
	int ch_fd, err, i;

	if (scenarios[args.scenario].expect_encap)
		populate_vip(skel, &scenarios[args.scenario]);

	ch_fd = bpf_map__fd(skel->maps.ch_rings);
	for (i = 0; i < CH_RINGS_SIZE; i++) {
		__u32 k = i;

		err = bpf_map_update_elem(ch_fd, &k, &real_idx, BPF_ANY);
		if (err) {
			fprintf(stderr, "ch_rings[%d]: %s\n", i, strerror(errno));
			exit(1);
		}
	}

	memcpy(cval.mac, router_mac, ETH_ALEN);
	key = 0;
	err = bpf_map_update_elem(bpf_map__fd(skel->maps.ctl_array), &key, &cval, BPF_ANY);
	if (err) {
		fprintf(stderr, "ctl_array: %s\n", strerror(errno));
		exit(1);
	}

	key = REAL_INDEX;
	real_v4.dst = htonl(TNL_DST);
	htonl_v6(real_v4.dstv6, tnl_dst_v6);
	err = bpf_map_update_elem(bpf_map__fd(skel->maps.reals), &key, &real_v4, BPF_ANY);
	if (err) {
		fprintf(stderr, "reals[%d]: %s\n", REAL_INDEX, strerror(errno));
		exit(1);
	}

	key = REAL_INDEX_V6;
	htonl_v6(real_v6.dstv6, tnl_dst_v6);
	real_v6.flags = F_IPV6;
	err = bpf_map_update_elem(bpf_map__fd(skel->maps.reals), &key, &real_v6, BPF_ANY);
	if (err) {
		fprintf(stderr, "reals[%d]: %s\n", REAL_INDEX_V6, strerror(errno));
		exit(1);
	}

	create_per_cpu_lru_maps(skel);

	if (scenarios[args.scenario].prepopulate_lru) {
		const struct test_scenario *sc = &scenarios[args.scenario];
		__u32 ridx = sc->encap_v6_outer ? REAL_INDEX_V6 : REAL_INDEX;

		populate_lru(sc, ridx);
	}

	if (scenarios[args.scenario].expect_encap) {
		const struct test_scenario *sc = &scenarios[args.scenario];
		struct vip_definition miss_vip = {};

		if (sc->is_v6)
			htonl_v6(miss_vip.vipv6, sc->vip_addr_v6);
		else
			miss_vip.vip = htonl(sc->vip_addr);
		miss_vip.port = htons(sc->dst_port);
		miss_vip.proto = sc->ip_proto;

		key = 0;
		err = bpf_map_update_elem(bpf_map__fd(skel->maps.vip_miss_stats),
					  &key, &miss_vip, BPF_ANY);
		if (err) {
			fprintf(stderr, "vip_miss_stats: %s\n", strerror(errno));
			exit(1);
		}
	}
}

static void build_expected_packet(int idx)
{
	const struct test_scenario *sc = &scenarios[idx];
	__u8 *p = expected_buf[idx];
	struct ethhdr eth = {};
	const __u8 *in = pkt_buf[idx];
	__u32 in_len = pkt_len[idx];
	__u32 off = 0;
	__u32 inner_len = in_len - sizeof(struct ethhdr);

	if (sc->expected_retval == XDP_DROP) {
		expected_len[idx] = 0;
		return;
	}

	if (sc->expected_retval == XDP_PASS) {
		memcpy(p, in, in_len);
		expected_len[idx] = in_len;
		return;
	}

	memcpy(eth.h_dest, router_mac, ETH_ALEN);
	memcpy(eth.h_source, lb_mac, ETH_ALEN);
	eth.h_proto = htons(sc->encap_v6_outer ? ETH_P_IPV6 : ETH_P_IP);
	memcpy(p, &eth, sizeof(eth));
	off += sizeof(eth);

	if (sc->encap_v6_outer) {
		struct ipv6hdr ip6h = {};
		__u8 nexthdr = sc->is_v6 ? IPPROTO_IPV6 : IPPROTO_IPIP;

		ip6h.version     = 6;
		ip6h.nexthdr     = nexthdr;
		ip6h.payload_len = htons(inner_len);
		ip6h.hop_limit   = 64;

		create_encap_ipv6_src(htons(sc->src_port),
				      sc->is_v6 ? htonl(sc->src_addr_v6[0])
						: htonl(sc->src_addr),
				      (__be32 *)&ip6h.saddr);
		htonl_v6((__be32 *)&ip6h.daddr, sc->tunnel_dst_v6);

		memcpy(p + off, &ip6h, sizeof(ip6h));
		off += sizeof(ip6h);
	} else {
		struct iphdr iph = {};

		iph.version  = 4;
		iph.ihl      = sizeof(iph) >> 2;
		iph.protocol = IPPROTO_IPIP;
		iph.tot_len  = htons(inner_len + sizeof(iph));
		iph.ttl      = 64;
		iph.saddr    = create_encap_ipv4_src(htons(sc->src_port),
						     htonl(sc->src_addr));
		iph.daddr    = htonl(sc->tunnel_dst);
		iph.check    = ip_checksum(&iph, sizeof(iph));

		memcpy(p + off, &iph, sizeof(iph));
		off += sizeof(iph);
	}

	memcpy(p + off, in + sizeof(struct ethhdr), inner_len);
	off += inner_len;

	expected_len[idx] = off;
}

static void print_hex_diff(const char *name, const __u8 *got, __u32 got_len, const __u8 *exp,
			   __u32 exp_len)
{
	__u32 max_len = got_len > exp_len ? got_len : exp_len;
	__u32 i, ndiffs = 0;

	fprintf(stderr, "  [%s] got %u bytes, expected %u bytes\n",
		name, got_len, exp_len);

	for (i = 0; i < max_len && ndiffs < 8; i++) {
		__u8 g = i < got_len ? got[i] : 0;
		__u8 e = i < exp_len ? exp[i] : 0;

		if (g != e || i >= got_len || i >= exp_len) {
			fprintf(stderr, "    offset 0x%03x: got 0x%02x  expected 0x%02x\n",
				i, g, e);
			ndiffs++;
		}
	}

	if (ndiffs >= 8 && i < max_len)
		fprintf(stderr, "    ... (more differences)\n");
}

static void read_stat(int stats_fd, __u32 key, __u64 *v1_out, __u64 *v2_out)
{
	struct lb_stats values[BENCH_NR_CPUS];
	unsigned int nr_cpus = bpf_num_possible_cpus();
	__u64 v1 = 0, v2 = 0;
	unsigned int i;

	if (nr_cpus > BENCH_NR_CPUS)
		nr_cpus = BENCH_NR_CPUS;

	if (bpf_map_lookup_elem(stats_fd, &key, values) == 0) {
		for (i = 0; i < nr_cpus; i++) {
			v1 += values[i].v1;
			v2 += values[i].v2;
		}
	}

	*v1_out = v1;
	*v2_out = v2;
}

static void reset_stats(int stats_fd)
{
	struct lb_stats zeros[BENCH_NR_CPUS];
	__u32 key;

	memset(zeros, 0, sizeof(zeros));
	for (key = 0; key < STATS_SIZE; key++)
		bpf_map_update_elem(stats_fd, &key, zeros, BPF_ANY);
}

static bool validate_counters(int idx)
{
	const struct test_scenario *sc = &scenarios[idx];
	int stats_fd = bpf_map__fd(ctx.skel->maps.stats);
	__u64 xdp_tx, xdp_pass, xdp_drop, lru_pkts, lru_misses, tcp_misses;
	__u64 expected_misses;
	__u64 dummy;
	/*
	 * BENCH_BPF_LOOP runs batch_iters timed + 1 untimed iteration.
	 * Each iteration calls process_packet -> count_action, so all
	 * counters are incremented (batch_iters + 1) times.
	 */
	__u64 n = ctx.timing.batch_iters + 1;
	bool pass = true;

	read_stat(stats_fd, STATS_XDP_TX, &xdp_tx, &dummy);
	read_stat(stats_fd, STATS_XDP_PASS, &xdp_pass, &dummy);
	read_stat(stats_fd, STATS_XDP_DROP, &xdp_drop, &dummy);
	read_stat(stats_fd, STATS_LRU, &lru_pkts, &lru_misses);
	read_stat(stats_fd, STATS_LRU_MISS, &tcp_misses, &dummy);

	if (sc->expected_retval == XDP_TX && xdp_tx != n) {
		fprintf(stderr, "  [%s] COUNTER FAIL: STATS_XDP_TX=%llu, expected %llu\n", sc->name,
			(unsigned long long)xdp_tx, (unsigned long long)n);
		pass = false;
	}
	if (sc->expected_retval == XDP_PASS && xdp_pass != n) {
		fprintf(stderr, "  [%s] COUNTER FAIL: STATS_XDP_PASS=%llu, expected %llu\n",
			sc->name, (unsigned long long)xdp_pass, (unsigned long long)n);
		pass = false;
	}
	if (sc->expected_retval == XDP_DROP && xdp_drop != n) {
		fprintf(stderr, "  [%s] COUNTER FAIL: STATS_XDP_DROP=%llu, expected %llu\n",
			sc->name, (unsigned long long)xdp_drop, (unsigned long long)n);
		pass = false;
	}

	if (!sc->expect_encap)
		goto out;

	if (lru_pkts != n) {
		fprintf(stderr, "  [%s] COUNTER FAIL: STATS_LRU.v1=%llu, expected %llu\n",
			sc->name, (unsigned long long)lru_pkts, (unsigned long long)n);
		pass = false;
	}

	switch (sc->lru_miss) {
	case LRU_MISS_NONE:
		expected_misses = 0;
		break;
	case LRU_MISS_ALL:
		expected_misses = n;
		break;
	case LRU_MISS_FIRST:
		expected_misses = 1;
		break;
	default:
		/* LRU_MISS_AUTO: compute from scenario flags */
		if (sc->prepopulate_lru && !sc->set_syn)
			expected_misses = 0;
		else if (sc->set_syn || sc->set_rst ||
			 (sc->vip_flags & F_LRU_BYPASS))
			expected_misses = n;
		else if (sc->cold_lru)
			expected_misses = 1;
		else
			expected_misses = n;
		break;
	}

	if (lru_misses != expected_misses) {
		fprintf(stderr, "  [%s] COUNTER FAIL: LRU misses=%llu, expected %llu\n",
			sc->name, (unsigned long long)lru_misses,
			(unsigned long long)expected_misses);
		pass = false;
	}

	if (sc->ip_proto == IPPROTO_TCP && lru_misses > 0) {
		if (tcp_misses != lru_misses) {
			fprintf(stderr, "  [%s] COUNTER FAIL: TCP LRU misses=%llu, expected %llu\n",
				sc->name, (unsigned long long)tcp_misses,
				(unsigned long long)lru_misses);
			pass = false;
		}
	}

out:
	reset_stats(stats_fd);
	return pass;
}

static const char *xdp_action_str(int action)
{
	switch (action) {
	case XDP_DROP:	return "XDP_DROP";
	case XDP_PASS:	return "XDP_PASS";
	case XDP_TX:	return "XDP_TX";
	default:	return "UNKNOWN";
	}
}

static bool validate_scenario(int idx)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	const struct test_scenario *sc = &scenarios[idx];
	__u8 out[MAX_ENCAP_SIZE];
	int err;

	topts.data_in = pkt_buf[idx];
	topts.data_size_in = pkt_len[idx];
	topts.data_out = out;
	topts.data_size_out = sizeof(out);
	topts.repeat = 1;

	err = bpf_prog_test_run_opts(ctx.prog_fd, &topts);
	if (err) {
		fprintf(stderr, "  [%s] FAIL: test_run: %s\n", sc->name, strerror(errno));
		return false;
	}

	if ((int)topts.retval != sc->expected_retval) {
		fprintf(stderr, "  [%s] FAIL: retval %s, expected %s\n", sc->name,
			xdp_action_str(topts.retval), xdp_action_str(sc->expected_retval));
		return false;
	}

	/*
	 * Compare output packet when it's deterministic.
	 * Skip for XDP_DROP (no output) and cold_lru (source IP poisoned).
	 */
	if (sc->expected_retval != XDP_DROP && !sc->cold_lru) {
		if (topts.data_size_out != expected_len[idx] ||
		    memcmp(out, expected_buf[idx], expected_len[idx]) != 0) {
			fprintf(stderr, "  [%s] FAIL: output packet mismatch\n", sc->name);
			print_hex_diff(sc->name, out, topts.data_size_out, expected_buf[idx],
				       expected_len[idx]);
			return false;
		}
	}

	if (!validate_counters(idx))
		return false;
	return true;
}

static int find_scenario(const char *name)
{
	int i;

	for (i = 0; i < NUM_SCENARIOS; i++) {
		if (strcmp(scenarios[i].name, name) == 0)
			return i;
	}
	return -1;
}

static void xdp_lb_validate(void)
{
	if (env.consumer_cnt != 0) {
		fprintf(stderr, "benchmark doesn't support consumers\n");
		exit(1);
	}
	if (bpf_num_possible_cpus() > BENCH_NR_CPUS) {
		fprintf(stderr, "too many CPUs (%d > %d), increase BENCH_NR_CPUS\n",
			bpf_num_possible_cpus(), BENCH_NR_CPUS);
		exit(1);
	}
}

static void xdp_lb_run_once(void *unused __always_unused)
{
	int idx = args.scenario;

	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in      = pkt_buf[idx],
		.data_size_in = pkt_len[idx],
		.repeat       = 1,
	);

	bpf_prog_test_run_opts(ctx.prog_fd, &topts);
}

static void xdp_lb_setup(void)
{
	struct xdp_lb_bench *skel;
	int err;

	if (args.scenario < 0) {
		fprintf(stderr, "--scenario is required. Use --list-scenarios to see options.\n");
		exit(1);
	}

	setup_libbpf();

	skel = xdp_lb_bench__open();
	if (!skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	err = xdp_lb_bench__load(skel);
	if (err) {
		fprintf(stderr, "failed to load skeleton: %s\n", strerror(-err));
		xdp_lb_bench__destroy(skel);
		exit(1);
	}

	ctx.skel    = skel;
	ctx.prog_fd = bpf_program__fd(skel->progs.xdp_lb_bench);

	build_packet(args.scenario);
	build_expected_packet(args.scenario);

	populate_maps(skel);

	BENCH_TIMING_INIT(&ctx.timing, skel, 0);
	ctx.timing.machine_readable = args.machine_readable;

	if (scenarios[args.scenario].fixed_batch_iters) {
		ctx.timing.batch_iters = scenarios[args.scenario].fixed_batch_iters;
		skel->bss->batch_iters = ctx.timing.batch_iters;
	} else {
		bpf_bench_calibrate(&ctx.timing, xdp_lb_run_once, NULL);
	}

	env.duration_sec = 600;

	/*
	 * Enable cold_lru before validation so LRU miss counters are
	 * correct.  Seed the LRU with one run so the original flow is
	 * present; validation then sees exactly 1 miss (the poisoned
	 * flow) regardless of whether calibration ran.
	 */
	if (scenarios[args.scenario].cold_lru) {
		skel->bss->cold_lru = 1;
		xdp_lb_run_once(NULL);
	}

	reset_stats(bpf_map__fd(skel->maps.stats));

	if (!validate_scenario(args.scenario)) {
		fprintf(stderr, "Validation FAILED - aborting benchmark\n");
		exit(1);
	}

	if (scenarios[args.scenario].flow_mask)
		skel->bss->flow_mask = scenarios[args.scenario].flow_mask;
}

static void *xdp_lb_producer(void *input)
{
	while (true)
		xdp_lb_run_once(NULL);

	return NULL;
}

static void xdp_lb_measure(struct bench_res *res)
{
	bpf_bench_timing_measure(&ctx.timing, res);
}

static void xdp_lb_report_final(struct bench_res res[], int res_cnt)
{
	bpf_bench_timing_report(&ctx.timing, scenarios[args.scenario].name,
				scenarios[args.scenario].description);
}

enum {
	ARG_SCENARIO         = 9001,
	ARG_LIST_SCENARIOS   = 9002,
	ARG_MACHINE_READABLE = 9003,
};

static const struct argp_option opts[] = {
	{ "scenario", ARG_SCENARIO, "NAME", 0,
	  "Scenario to benchmark (required)" },
	{ "list-scenarios", ARG_LIST_SCENARIOS, NULL, 0,
	  "List available scenarios and exit" },
	{ "machine-readable", ARG_MACHINE_READABLE, NULL, 0,
	  "Print only a machine-readable RESULT line" },
	{},
};

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	int i;

	switch (key) {
	case ARG_SCENARIO:
		args.scenario = find_scenario(arg);
		if (args.scenario < 0) {
			fprintf(stderr, "unknown scenario: '%s'\n", arg);
			fprintf(stderr, "use --list-scenarios to see options\n");
			argp_usage(state);
		}
		break;
	case ARG_LIST_SCENARIOS:
		printf("Available scenarios:\n");
		for (i = 0; i < NUM_SCENARIOS; i++)
			printf("  %-20s  %s\n", scenarios[i].name, scenarios[i].description);
		exit(0);
	case ARG_MACHINE_READABLE:
		args.machine_readable = true;
		env.quiet = true;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

const struct argp bench_xdp_lb_argp = {
	.options = opts,
	.parser  = parse_arg,
};

const struct bench bench_xdp_lb = {
	.name            = "xdp-lb",
	.argp            = &bench_xdp_lb_argp,
	.validate        = xdp_lb_validate,
	.setup           = xdp_lb_setup,
	.producer_thread = xdp_lb_producer,
	.measure         = xdp_lb_measure,
	.report_final    = xdp_lb_report_final,
};
