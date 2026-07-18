/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#ifndef XDP_LB_BENCH_COMMON_H
#define XDP_LB_BENCH_COMMON_H

#define F_IPV6		(1 << 0)
#define F_LRU_BYPASS	(1 << 1)

#define CH_RING_SIZE	65537		/* per-VIP consistent hash ring slots */
#define MAX_VIPS	16
#define CH_RINGS_SIZE	(MAX_VIPS * CH_RING_SIZE)
#define MAX_REALS	512
#define DEFAULT_LRU_SIZE 100000		/* connection tracking cache size */
#define ONE_SEC		1000000000U	/* 1 sec in nanosec */
#define MAX_CONN_RATE	100000000	/* high enough to never trigger in bench */
#define LRU_UDP_TIMEOUT	30000000000ULL	/* 30 sec in nanosec */
#define PCKT_FRAGMENTED	0x3FFF
#define KNUTH_HASH_MULT	2654435761U
#define IPIP_V4_PREFIX	4268		/* 172.16/12 in network order */
#define IPIP_V6_PREFIX1	1		/* 0100::/64 (RFC 6666 discard) */
#define IPIP_V6_PREFIX2	0
#define IPIP_V6_PREFIX3	0

/* Stats indices (0..MAX_VIPS-1 are per-VIP packet/byte counters) */
#define STATS_LRU	(MAX_VIPS + 0)	/* v1: total VIP packets, v2: LRU misses */
#define STATS_XDP_TX	(MAX_VIPS + 1)
#define STATS_XDP_PASS	(MAX_VIPS + 2)
#define STATS_XDP_DROP	(MAX_VIPS + 3)
#define STATS_NEW_CONN	(MAX_VIPS + 4)	/* v1: conn count, v2: last reset ts */
#define STATS_LRU_MISS	(MAX_VIPS + 5)	/* v1: TCP LRU misses */
#define STATS_SIZE	(MAX_VIPS + 6)

#ifdef __BPF__
#define lb_htons(x)	bpf_htons(x)
#define LB_INLINE	static __always_inline
#else
#define lb_htons(x)	htons(x)
#define LB_INLINE	static inline
#endif

LB_INLINE __be32 create_encap_ipv4_src(__u16 port, __be32 src)
{
	__u32 ip_suffix = lb_htons(port);

	ip_suffix <<= 16;
	ip_suffix ^= src;
	return (0xFFFF0000 & ip_suffix) | IPIP_V4_PREFIX;
}

LB_INLINE void create_encap_ipv6_src(__u16 port, __be32 src, __be32 *saddr)
{
	saddr[0] = IPIP_V6_PREFIX1;
	saddr[1] = IPIP_V6_PREFIX2;
	saddr[2] = IPIP_V6_PREFIX3;
	saddr[3] = src ^ port;
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
	__u8 pad[3];
};

struct vip_definition {
	union {
		__be32 vip;
		__be32 vipv6[4];
	};
	__u16 port;
	__u8 proto;
	__u8 pad;
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
	__be32 dst;
	__be32 dstv6[4];
	__u8   flags;
};

struct lb_stats {
	__u64 v1;
	__u64 v2;
};

struct ctl_value {
	__u8 mac[6];
	__u8 pad[2];
};

#endif /* XDP_LB_BENCH_COMMON_H */
