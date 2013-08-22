/*
 * Copyright (c) 2007-2013 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#ifndef FLOW_H
#define FLOW_H 1

#include <linux/kernel.h>
#include <linux/netlink.h>
#include <linux/openvswitch.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/if_ether.h>
#include <linux/in6.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/flex_array.h>
#include <net/inet_ecn.h>

struct sk_buff;
struct sw_flow_mask;
struct flow_table;

struct sw_flow_actions {
	struct rcu_head rcu;
	u32 actions_len;
	struct nlattr actions[];
};

/* Used to memset ovs_key_ipv4_tunnel padding. */
#define OVS_TUNNEL_KEY_SIZE					\
	(offsetof(struct ovs_key_ipv4_tunnel, ipv4_ttl) +	\
	FIELD_SIZEOF(struct ovs_key_ipv4_tunnel, ipv4_ttl))

struct ovs_key_ipv4_tunnel {
	__be64 tun_id;
	__be32 ipv4_src;
	__be32 ipv4_dst;
	__be16 tun_flags;
	u8   ipv4_tos;
	u8   ipv4_ttl;
};

static inline void ovs_flow_tun_key_init(struct ovs_key_ipv4_tunnel *tun_key,
					 const struct iphdr *iph, __be64 tun_id,
					 __be16 tun_flags)
{
	tun_key->tun_id = tun_id;
	tun_key->ipv4_src = iph->saddr;
	tun_key->ipv4_dst = iph->daddr;
	tun_key->ipv4_tos = iph->tos;
	tun_key->ipv4_ttl = iph->ttl;
	tun_key->tun_flags = tun_flags;

	/* clear struct padding. */
	memset((unsigned char *) tun_key + OVS_TUNNEL_KEY_SIZE, 0,
	       sizeof(*tun_key) - OVS_TUNNEL_KEY_SIZE);
}

struct sw_flow_key {
	struct ovs_key_ipv4_tunnel tun_key;  /* Encapsulating tunnel key. */
	struct {
		u32	priority;	/* Packet QoS priority. */
		u32	skb_mark;	/* SKB mark. */
		u16	in_port;	/* Input switch port (or DP_MAX_PORTS). */
	} phy;
	struct {
		u8     src[ETH_ALEN];	/* Ethernet source address. */
		u8     dst[ETH_ALEN];	/* Ethernet destination address. */
		__be16 tci;		/* 0 if no VLAN, VLAN_TAG_PRESENT set otherwise. */
		__be16 type;		/* Ethernet frame type. */
	} eth;
	struct {
		u8     proto;		/* IP protocol or lower 8 bits of ARP opcode. */
		u8     tos;		/* IP ToS. */
		u8     ttl;		/* IP TTL/hop limit. */
		u8     frag;		/* One of OVS_FRAG_TYPE_*. */
	} ip;
	union {
		struct {
			struct {
				__be32 src;	/* IP source address. */
				__be32 dst;	/* IP destination address. */
			} addr;
			union {
				struct {
					__be16 src;		/* TCP/UDP/SCTP source port. */
					__be16 dst;		/* TCP/UDP/SCTP destination port. */
				} tp;
				struct {
					u8 sha[ETH_ALEN];	/* ARP source hardware address. */
					u8 tha[ETH_ALEN];	/* ARP target hardware address. */
				} arp;
			};
		} ipv4;
		struct {
			struct {
				struct in6_addr src;	/* IPv6 source address. */
				struct in6_addr dst;	/* IPv6 destination address. */
			} addr;
			__be32 label;			/* IPv6 flow label. */
			struct {
				__be16 src;		/* TCP/UDP/SCTP source port. */
				__be16 dst;		/* TCP/UDP/SCTP destination port. */
			} tp;
			struct {
				struct in6_addr target;	/* ND target address. */
				u8 sll[ETH_ALEN];	/* ND source link layer address. */
				u8 tll[ETH_ALEN];	/* ND target link layer address. */
			} nd;
		} ipv6;
	};
};

struct sw_flow {
	struct rcu_head rcu;
	struct hlist_node hash_node[2];
	u32 hash;

	struct sw_flow_key key;
	struct sw_flow_key unmasked_key;
	struct sw_flow_mask *mask;
	struct sw_flow_actions __rcu *sf_acts;

	spinlock_t lock;	/* Lock for values below. */
	unsigned long used;	/* Last used time (in jiffies). */
	u64 packet_count;	/* Number of packets matched. */
	u64 byte_count;		/* Number of bytes matched. */
	u8 tcp_flags;		/* Union of seen TCP flags. */
};

struct sw_flow_key_range {
	size_t start;
	size_t end;
};

static inline u16 ovs_sw_flow_key_range_actual_size(const struct sw_flow_key_range *range)
{
	return range->end - range->start;
}

struct sw_flow_match {
	struct sw_flow_key *key;
	struct sw_flow_key_range range;
	struct sw_flow_mask *mask;
};

void ovs_match_init(struct sw_flow_match *match,
		struct sw_flow_key *key, struct sw_flow_mask *mask);

struct arp_eth_header {
	__be16      ar_hrd;	/* format of hardware address   */
	__be16      ar_pro;	/* format of protocol address   */
	unsigned char   ar_hln;	/* length of hardware address   */
	unsigned char   ar_pln;	/* length of protocol address   */
	__be16      ar_op;	/* ARP opcode (command)     */

	/* Ethernet+IPv4 specific members. */
	unsigned char       ar_sha[ETH_ALEN];	/* sender hardware address  */
	unsigned char       ar_sip[4];		/* sender IP address        */
	unsigned char       ar_tha[ETH_ALEN];	/* target hardware address  */
	unsigned char       ar_tip[4];		/* target IP address        */
} __packed;

int ovs_flow_init(void);
void ovs_flow_exit(void);

struct sw_flow *ovs_flow_alloc(void);
void ovs_flow_deferred_free(struct sw_flow *);
void ovs_flow_free(struct sw_flow *, bool deferred);

struct sw_flow_actions *ovs_flow_actions_alloc(int actions_len);
void ovs_flow_deferred_free_acts(struct sw_flow_actions *);

int ovs_flow_extract(struct sk_buff *, u16 in_port, struct sw_flow_key *);
void ovs_flow_used(struct sw_flow *, struct sk_buff *);
u64 ovs_flow_used_time(unsigned long flow_jiffies);
int ovs_flow_to_nlattrs(const struct sw_flow_key *,
		const struct sw_flow_key *, struct sk_buff *);
int ovs_match_from_nlattrs(struct sw_flow_match *match,
		      const struct nlattr *,
		      const struct nlattr *);
int ovs_flow_metadata_from_nlattrs(struct sw_flow *flow,
		const struct nlattr *attr);

#define MAX_ACTIONS_BUFSIZE    (32 * 1024)
#define TBL_MIN_BUCKETS		1024

struct flow_table {
	struct flex_array *buckets;
	unsigned int count, n_buckets;
	struct rcu_head rcu;
	struct list_head *mask_list;
	int node_ver;
	u32 hash_seed;
	bool keep_flows;
};

static inline int ovs_flow_tbl_count(struct flow_table *table)
{
	return table->count;
}

static inline int ovs_flow_tbl_need_to_expand(struct flow_table *table)
{
	return (table->count > table->n_buckets);
}

struct sw_flow *ovs_flow_lookup(struct flow_table *,
				const struct sw_flow_key *);
struct sw_flow *ovs_flow_lookup_unmasked_key(struct flow_table *table,
				    struct sw_flow_match *match);

void ovs_flow_tbl_destroy(struct flow_table *table, bool deferred);
struct flow_table *ovs_flow_tbl_alloc(int new_size);
struct flow_table *ovs_flow_tbl_expand(struct flow_table *table);
struct flow_table *ovs_flow_tbl_rehash(struct flow_table *table);

void ovs_flow_insert(struct flow_table *table, struct sw_flow *flow);
void ovs_flow_remove(struct flow_table *table, struct sw_flow *flow);

struct sw_flow *ovs_flow_dump_next(struct flow_table *table, u32 *bucket, u32 *idx);
extern const int ovs_key_lens[OVS_KEY_ATTR_MAX + 1];
int ovs_ipv4_tun_from_nlattr(const struct nlattr *attr,
			     struct sw_flow_match *match, bool is_mask);
int ovs_ipv4_tun_to_nlattr(struct sk_buff *skb,
			   const struct ovs_key_ipv4_tunnel *tun_key,
			   const struct ovs_key_ipv4_tunnel *output);

bool ovs_flow_cmp_unmasked_key(const struct sw_flow *flow,
		const struct sw_flow_key *key, int key_len);

struct sw_flow_mask {
	int ref_count;
	struct rcu_head rcu;
	struct list_head list;
	struct sw_flow_key_range range;
	struct sw_flow_key key;
};

static inline u16
ovs_sw_flow_mask_actual_size(const struct sw_flow_mask *mask)
{
	return ovs_sw_flow_key_range_actual_size(&mask->range);
}

static inline u16
ovs_sw_flow_mask_size_roundup(const struct sw_flow_mask *mask)
{
	return roundup(ovs_sw_flow_mask_actual_size(mask), sizeof(u32));
}

struct sw_flow_mask *ovs_sw_flow_mask_alloc(void);
void ovs_sw_flow_mask_add_ref(struct sw_flow_mask *);
void ovs_sw_flow_mask_del_ref(struct sw_flow_mask *, bool deferred);
void ovs_sw_flow_mask_insert(struct flow_table *, struct sw_flow_mask *);
struct sw_flow_mask *ovs_sw_flow_mask_find(const struct flow_table *,
		const struct sw_flow_mask *);
void ovs_flow_key_mask(struct sw_flow_key *dst, const struct sw_flow_key *src,
		       const struct sw_flow_mask *mask);
#endif /* flow.h */
