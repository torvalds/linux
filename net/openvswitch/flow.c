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

#include "flow.h"
#include "datapath.h"
#include <linux/uaccess.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <net/llc_pdu.h>
#include <linux/kernel.h>
#include <linux/jhash.h>
#include <linux/jiffies.h>
#include <linux/llc.h>
#include <linux/module.h>
#include <linux/in.h>
#include <linux/rcupdate.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/sctp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/rculist.h>
#include <net/ip.h>
#include <net/ip_tunnels.h>
#include <net/ipv6.h>
#include <net/ndisc.h>

static struct kmem_cache *flow_cache;

static void ovs_sw_flow_mask_set(struct sw_flow_mask *mask,
		struct sw_flow_key_range *range, u8 val);

static void update_range__(struct sw_flow_match *match,
			  size_t offset, size_t size, bool is_mask)
{
	struct sw_flow_key_range *range = NULL;
	size_t start = offset;
	size_t end = offset + size;

	if (!is_mask)
		range = &match->range;
	else if (match->mask)
		range = &match->mask->range;

	if (!range)
		return;

	if (range->start == range->end) {
		range->start = start;
		range->end = end;
		return;
	}

	if (range->start > start)
		range->start = start;

	if (range->end < end)
		range->end = end;
}

#define SW_FLOW_KEY_PUT(match, field, value, is_mask) \
	do { \
		update_range__(match, offsetof(struct sw_flow_key, field),  \
				     sizeof((match)->key->field), is_mask); \
		if (is_mask) {						    \
			if ((match)->mask)				    \
				(match)->mask->key.field = value;	    \
		} else {                                                    \
			(match)->key->field = value;		            \
		}                                                           \
	} while (0)

#define SW_FLOW_KEY_MEMCPY(match, field, value_p, len, is_mask) \
	do { \
		update_range__(match, offsetof(struct sw_flow_key, field),  \
				len, is_mask);                              \
		if (is_mask) {						    \
			if ((match)->mask)				    \
				memcpy(&(match)->mask->key.field, value_p, len);\
		} else {                                                    \
			memcpy(&(match)->key->field, value_p, len);         \
		}                                                           \
	} while (0)

void ovs_match_init(struct sw_flow_match *match,
		    struct sw_flow_key *key,
		    struct sw_flow_mask *mask)
{
	memset(match, 0, sizeof(*match));
	match->key = key;
	match->mask = mask;

	memset(key, 0, sizeof(*key));

	if (mask) {
		memset(&mask->key, 0, sizeof(mask->key));
		mask->range.start = mask->range.end = 0;
	}
}

static bool ovs_match_validate(const struct sw_flow_match *match,
		u64 key_attrs, u64 mask_attrs)
{
	u64 key_expected = 1 << OVS_KEY_ATTR_ETHERNET;
	u64 mask_allowed = key_attrs;  /* At most allow all key attributes */

	/* The following mask attributes allowed only if they
	 * pass the validation tests. */
	mask_allowed &= ~((1 << OVS_KEY_ATTR_IPV4)
			| (1 << OVS_KEY_ATTR_IPV6)
			| (1 << OVS_KEY_ATTR_TCP)
			| (1 << OVS_KEY_ATTR_UDP)
			| (1 << OVS_KEY_ATTR_SCTP)
			| (1 << OVS_KEY_ATTR_ICMP)
			| (1 << OVS_KEY_ATTR_ICMPV6)
			| (1 << OVS_KEY_ATTR_ARP)
			| (1 << OVS_KEY_ATTR_ND));

	/* Always allowed mask fields. */
	mask_allowed |= ((1 << OVS_KEY_ATTR_TUNNEL)
		       | (1 << OVS_KEY_ATTR_IN_PORT)
		       | (1 << OVS_KEY_ATTR_ETHERTYPE));

	/* Check key attributes. */
	if (match->key->eth.type == htons(ETH_P_ARP)
			|| match->key->eth.type == htons(ETH_P_RARP)) {
		key_expected |= 1 << OVS_KEY_ATTR_ARP;
		if (match->mask && (match->mask->key.eth.type == htons(0xffff)))
			mask_allowed |= 1 << OVS_KEY_ATTR_ARP;
	}

	if (match->key->eth.type == htons(ETH_P_IP)) {
		key_expected |= 1 << OVS_KEY_ATTR_IPV4;
		if (match->mask && (match->mask->key.eth.type == htons(0xffff)))
			mask_allowed |= 1 << OVS_KEY_ATTR_IPV4;

		if (match->key->ip.frag != OVS_FRAG_TYPE_LATER) {
			if (match->key->ip.proto == IPPROTO_UDP) {
				key_expected |= 1 << OVS_KEY_ATTR_UDP;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_UDP;
			}

			if (match->key->ip.proto == IPPROTO_SCTP) {
				key_expected |= 1 << OVS_KEY_ATTR_SCTP;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_SCTP;
			}

			if (match->key->ip.proto == IPPROTO_TCP) {
				key_expected |= 1 << OVS_KEY_ATTR_TCP;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_TCP;
			}

			if (match->key->ip.proto == IPPROTO_ICMP) {
				key_expected |= 1 << OVS_KEY_ATTR_ICMP;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_ICMP;
			}
		}
	}

	if (match->key->eth.type == htons(ETH_P_IPV6)) {
		key_expected |= 1 << OVS_KEY_ATTR_IPV6;
		if (match->mask && (match->mask->key.eth.type == htons(0xffff)))
			mask_allowed |= 1 << OVS_KEY_ATTR_IPV6;

		if (match->key->ip.frag != OVS_FRAG_TYPE_LATER) {
			if (match->key->ip.proto == IPPROTO_UDP) {
				key_expected |= 1 << OVS_KEY_ATTR_UDP;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_UDP;
			}

			if (match->key->ip.proto == IPPROTO_SCTP) {
				key_expected |= 1 << OVS_KEY_ATTR_SCTP;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_SCTP;
			}

			if (match->key->ip.proto == IPPROTO_TCP) {
				key_expected |= 1 << OVS_KEY_ATTR_TCP;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_TCP;
			}

			if (match->key->ip.proto == IPPROTO_ICMPV6) {
				key_expected |= 1 << OVS_KEY_ATTR_ICMPV6;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_ICMPV6;

				if (match->key->ipv6.tp.src ==
						htons(NDISC_NEIGHBOUR_SOLICITATION) ||
				    match->key->ipv6.tp.src == htons(NDISC_NEIGHBOUR_ADVERTISEMENT)) {
					key_expected |= 1 << OVS_KEY_ATTR_ND;
					if (match->mask && (match->mask->key.ipv6.tp.src == htons(0xffff)))
						mask_allowed |= 1 << OVS_KEY_ATTR_ND;
				}
			}
		}
	}

	if ((key_attrs & key_expected) != key_expected) {
		/* Key attributes check failed. */
		OVS_NLERR("Missing expected key attributes (key_attrs=%llx, expected=%llx).\n",
				key_attrs, key_expected);
		return false;
	}

	if ((mask_attrs & mask_allowed) != mask_attrs) {
		/* Mask attributes check failed. */
		OVS_NLERR("Contain more than allowed mask fields (mask_attrs=%llx, mask_allowed=%llx).\n",
				mask_attrs, mask_allowed);
		return false;
	}

	return true;
}

static int check_header(struct sk_buff *skb, int len)
{
	if (unlikely(skb->len < len))
		return -EINVAL;
	if (unlikely(!pskb_may_pull(skb, len)))
		return -ENOMEM;
	return 0;
}

static bool arphdr_ok(struct sk_buff *skb)
{
	return pskb_may_pull(skb, skb_network_offset(skb) +
				  sizeof(struct arp_eth_header));
}

static int check_iphdr(struct sk_buff *skb)
{
	unsigned int nh_ofs = skb_network_offset(skb);
	unsigned int ip_len;
	int err;

	err = check_header(skb, nh_ofs + sizeof(struct iphdr));
	if (unlikely(err))
		return err;

	ip_len = ip_hdrlen(skb);
	if (unlikely(ip_len < sizeof(struct iphdr) ||
		     skb->len < nh_ofs + ip_len))
		return -EINVAL;

	skb_set_transport_header(skb, nh_ofs + ip_len);
	return 0;
}

static bool tcphdr_ok(struct sk_buff *skb)
{
	int th_ofs = skb_transport_offset(skb);
	int tcp_len;

	if (unlikely(!pskb_may_pull(skb, th_ofs + sizeof(struct tcphdr))))
		return false;

	tcp_len = tcp_hdrlen(skb);
	if (unlikely(tcp_len < sizeof(struct tcphdr) ||
		     skb->len < th_ofs + tcp_len))
		return false;

	return true;
}

static bool udphdr_ok(struct sk_buff *skb)
{
	return pskb_may_pull(skb, skb_transport_offset(skb) +
				  sizeof(struct udphdr));
}

static bool sctphdr_ok(struct sk_buff *skb)
{
	return pskb_may_pull(skb, skb_transport_offset(skb) +
				  sizeof(struct sctphdr));
}

static bool icmphdr_ok(struct sk_buff *skb)
{
	return pskb_may_pull(skb, skb_transport_offset(skb) +
				  sizeof(struct icmphdr));
}

u64 ovs_flow_used_time(unsigned long flow_jiffies)
{
	struct timespec cur_ts;
	u64 cur_ms, idle_ms;

	ktime_get_ts(&cur_ts);
	idle_ms = jiffies_to_msecs(jiffies - flow_jiffies);
	cur_ms = (u64)cur_ts.tv_sec * MSEC_PER_SEC +
		 cur_ts.tv_nsec / NSEC_PER_MSEC;

	return cur_ms - idle_ms;
}

static int parse_ipv6hdr(struct sk_buff *skb, struct sw_flow_key *key)
{
	unsigned int nh_ofs = skb_network_offset(skb);
	unsigned int nh_len;
	int payload_ofs;
	struct ipv6hdr *nh;
	uint8_t nexthdr;
	__be16 frag_off;
	int err;

	err = check_header(skb, nh_ofs + sizeof(*nh));
	if (unlikely(err))
		return err;

	nh = ipv6_hdr(skb);
	nexthdr = nh->nexthdr;
	payload_ofs = (u8 *)(nh + 1) - skb->data;

	key->ip.proto = NEXTHDR_NONE;
	key->ip.tos = ipv6_get_dsfield(nh);
	key->ip.ttl = nh->hop_limit;
	key->ipv6.label = *(__be32 *)nh & htonl(IPV6_FLOWINFO_FLOWLABEL);
	key->ipv6.addr.src = nh->saddr;
	key->ipv6.addr.dst = nh->daddr;

	payload_ofs = ipv6_skip_exthdr(skb, payload_ofs, &nexthdr, &frag_off);
	if (unlikely(payload_ofs < 0))
		return -EINVAL;

	if (frag_off) {
		if (frag_off & htons(~0x7))
			key->ip.frag = OVS_FRAG_TYPE_LATER;
		else
			key->ip.frag = OVS_FRAG_TYPE_FIRST;
	}

	nh_len = payload_ofs - nh_ofs;
	skb_set_transport_header(skb, nh_ofs + nh_len);
	key->ip.proto = nexthdr;
	return nh_len;
}

static bool icmp6hdr_ok(struct sk_buff *skb)
{
	return pskb_may_pull(skb, skb_transport_offset(skb) +
				  sizeof(struct icmp6hdr));
}

void ovs_flow_key_mask(struct sw_flow_key *dst, const struct sw_flow_key *src,
		       const struct sw_flow_mask *mask)
{
	u8 *m = (u8 *)&mask->key + mask->range.start;
	u8 *s = (u8 *)src + mask->range.start;
	u8 *d = (u8 *)dst + mask->range.start;
	int i;

	memset(dst, 0, sizeof(*dst));
	for (i = 0; i < ovs_sw_flow_mask_size_roundup(mask); i++) {
		*d = *s & *m;
		d++, s++, m++;
	}
}

#define TCP_FLAGS_OFFSET 13
#define TCP_FLAG_MASK 0x3f

void ovs_flow_used(struct sw_flow *flow, struct sk_buff *skb)
{
	u8 tcp_flags = 0;

	if ((flow->key.eth.type == htons(ETH_P_IP) ||
	     flow->key.eth.type == htons(ETH_P_IPV6)) &&
	    flow->key.ip.proto == IPPROTO_TCP &&
	    likely(skb->len >= skb_transport_offset(skb) + sizeof(struct tcphdr))) {
		u8 *tcp = (u8 *)tcp_hdr(skb);
		tcp_flags = *(tcp + TCP_FLAGS_OFFSET) & TCP_FLAG_MASK;
	}

	spin_lock(&flow->lock);
	flow->used = jiffies;
	flow->packet_count++;
	flow->byte_count += skb->len;
	flow->tcp_flags |= tcp_flags;
	spin_unlock(&flow->lock);
}

struct sw_flow_actions *ovs_flow_actions_alloc(int size)
{
	struct sw_flow_actions *sfa;

	if (size > MAX_ACTIONS_BUFSIZE)
		return ERR_PTR(-EINVAL);

	sfa = kmalloc(sizeof(*sfa) + size, GFP_KERNEL);
	if (!sfa)
		return ERR_PTR(-ENOMEM);

	sfa->actions_len = 0;
	return sfa;
}

struct sw_flow *ovs_flow_alloc(void)
{
	struct sw_flow *flow;

	flow = kmem_cache_alloc(flow_cache, GFP_KERNEL);
	if (!flow)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&flow->lock);
	flow->sf_acts = NULL;
	flow->mask = NULL;

	return flow;
}

static struct hlist_head *find_bucket(struct flow_table *table, u32 hash)
{
	hash = jhash_1word(hash, table->hash_seed);
	return flex_array_get(table->buckets,
				(hash & (table->n_buckets - 1)));
}

static struct flex_array *alloc_buckets(unsigned int n_buckets)
{
	struct flex_array *buckets;
	int i, err;

	buckets = flex_array_alloc(sizeof(struct hlist_head),
				   n_buckets, GFP_KERNEL);
	if (!buckets)
		return NULL;

	err = flex_array_prealloc(buckets, 0, n_buckets, GFP_KERNEL);
	if (err) {
		flex_array_free(buckets);
		return NULL;
	}

	for (i = 0; i < n_buckets; i++)
		INIT_HLIST_HEAD((struct hlist_head *)
					flex_array_get(buckets, i));

	return buckets;
}

static void free_buckets(struct flex_array *buckets)
{
	flex_array_free(buckets);
}

static struct flow_table *__flow_tbl_alloc(int new_size)
{
	struct flow_table *table = kmalloc(sizeof(*table), GFP_KERNEL);

	if (!table)
		return NULL;

	table->buckets = alloc_buckets(new_size);

	if (!table->buckets) {
		kfree(table);
		return NULL;
	}
	table->n_buckets = new_size;
	table->count = 0;
	table->node_ver = 0;
	table->keep_flows = false;
	get_random_bytes(&table->hash_seed, sizeof(u32));
	table->mask_list = NULL;

	return table;
}

static void __flow_tbl_destroy(struct flow_table *table)
{
	int i;

	if (table->keep_flows)
		goto skip_flows;

	for (i = 0; i < table->n_buckets; i++) {
		struct sw_flow *flow;
		struct hlist_head *head = flex_array_get(table->buckets, i);
		struct hlist_node *n;
		int ver = table->node_ver;

		hlist_for_each_entry_safe(flow, n, head, hash_node[ver]) {
			hlist_del(&flow->hash_node[ver]);
			ovs_flow_free(flow, false);
		}
	}

	BUG_ON(!list_empty(table->mask_list));
	kfree(table->mask_list);

skip_flows:
	free_buckets(table->buckets);
	kfree(table);
}

struct flow_table *ovs_flow_tbl_alloc(int new_size)
{
	struct flow_table *table = __flow_tbl_alloc(new_size);

	if (!table)
		return NULL;

	table->mask_list = kmalloc(sizeof(struct list_head), GFP_KERNEL);
	if (!table->mask_list) {
		table->keep_flows = true;
		__flow_tbl_destroy(table);
		return NULL;
	}
	INIT_LIST_HEAD(table->mask_list);

	return table;
}

static void flow_tbl_destroy_rcu_cb(struct rcu_head *rcu)
{
	struct flow_table *table = container_of(rcu, struct flow_table, rcu);

	__flow_tbl_destroy(table);
}

void ovs_flow_tbl_destroy(struct flow_table *table, bool deferred)
{
	if (!table)
		return;

	if (deferred)
		call_rcu(&table->rcu, flow_tbl_destroy_rcu_cb);
	else
		__flow_tbl_destroy(table);
}

struct sw_flow *ovs_flow_dump_next(struct flow_table *table, u32 *bucket, u32 *last)
{
	struct sw_flow *flow;
	struct hlist_head *head;
	int ver;
	int i;

	ver = table->node_ver;
	while (*bucket < table->n_buckets) {
		i = 0;
		head = flex_array_get(table->buckets, *bucket);
		hlist_for_each_entry_rcu(flow, head, hash_node[ver]) {
			if (i < *last) {
				i++;
				continue;
			}
			*last = i + 1;
			return flow;
		}
		(*bucket)++;
		*last = 0;
	}

	return NULL;
}

static void __tbl_insert(struct flow_table *table, struct sw_flow *flow)
{
	struct hlist_head *head;

	head = find_bucket(table, flow->hash);
	hlist_add_head_rcu(&flow->hash_node[table->node_ver], head);

	table->count++;
}

static void flow_table_copy_flows(struct flow_table *old, struct flow_table *new)
{
	int old_ver;
	int i;

	old_ver = old->node_ver;
	new->node_ver = !old_ver;

	/* Insert in new table. */
	for (i = 0; i < old->n_buckets; i++) {
		struct sw_flow *flow;
		struct hlist_head *head;

		head = flex_array_get(old->buckets, i);

		hlist_for_each_entry(flow, head, hash_node[old_ver])
			__tbl_insert(new, flow);
	}

	new->mask_list = old->mask_list;
	old->keep_flows = true;
}

static struct flow_table *__flow_tbl_rehash(struct flow_table *table, int n_buckets)
{
	struct flow_table *new_table;

	new_table = __flow_tbl_alloc(n_buckets);
	if (!new_table)
		return ERR_PTR(-ENOMEM);

	flow_table_copy_flows(table, new_table);

	return new_table;
}

struct flow_table *ovs_flow_tbl_rehash(struct flow_table *table)
{
	return __flow_tbl_rehash(table, table->n_buckets);
}

struct flow_table *ovs_flow_tbl_expand(struct flow_table *table)
{
	return __flow_tbl_rehash(table, table->n_buckets * 2);
}

static void __flow_free(struct sw_flow *flow)
{
	kfree((struct sf_flow_acts __force *)flow->sf_acts);
	kmem_cache_free(flow_cache, flow);
}

static void rcu_free_flow_callback(struct rcu_head *rcu)
{
	struct sw_flow *flow = container_of(rcu, struct sw_flow, rcu);

	__flow_free(flow);
}

void ovs_flow_free(struct sw_flow *flow, bool deferred)
{
	if (!flow)
		return;

	ovs_sw_flow_mask_del_ref(flow->mask, deferred);

	if (deferred)
		call_rcu(&flow->rcu, rcu_free_flow_callback);
	else
		__flow_free(flow);
}

/* Schedules 'sf_acts' to be freed after the next RCU grace period.
 * The caller must hold rcu_read_lock for this to be sensible. */
void ovs_flow_deferred_free_acts(struct sw_flow_actions *sf_acts)
{
	kfree_rcu(sf_acts, rcu);
}

static int parse_vlan(struct sk_buff *skb, struct sw_flow_key *key)
{
	struct qtag_prefix {
		__be16 eth_type; /* ETH_P_8021Q */
		__be16 tci;
	};
	struct qtag_prefix *qp;

	if (unlikely(skb->len < sizeof(struct qtag_prefix) + sizeof(__be16)))
		return 0;

	if (unlikely(!pskb_may_pull(skb, sizeof(struct qtag_prefix) +
					 sizeof(__be16))))
		return -ENOMEM;

	qp = (struct qtag_prefix *) skb->data;
	key->eth.tci = qp->tci | htons(VLAN_TAG_PRESENT);
	__skb_pull(skb, sizeof(struct qtag_prefix));

	return 0;
}

static __be16 parse_ethertype(struct sk_buff *skb)
{
	struct llc_snap_hdr {
		u8  dsap;  /* Always 0xAA */
		u8  ssap;  /* Always 0xAA */
		u8  ctrl;
		u8  oui[3];
		__be16 ethertype;
	};
	struct llc_snap_hdr *llc;
	__be16 proto;

	proto = *(__be16 *) skb->data;
	__skb_pull(skb, sizeof(__be16));

	if (ntohs(proto) >= ETH_P_802_3_MIN)
		return proto;

	if (skb->len < sizeof(struct llc_snap_hdr))
		return htons(ETH_P_802_2);

	if (unlikely(!pskb_may_pull(skb, sizeof(struct llc_snap_hdr))))
		return htons(0);

	llc = (struct llc_snap_hdr *) skb->data;
	if (llc->dsap != LLC_SAP_SNAP ||
	    llc->ssap != LLC_SAP_SNAP ||
	    (llc->oui[0] | llc->oui[1] | llc->oui[2]) != 0)
		return htons(ETH_P_802_2);

	__skb_pull(skb, sizeof(struct llc_snap_hdr));

	if (ntohs(llc->ethertype) >= ETH_P_802_3_MIN)
		return llc->ethertype;

	return htons(ETH_P_802_2);
}

static int parse_icmpv6(struct sk_buff *skb, struct sw_flow_key *key,
			int nh_len)
{
	struct icmp6hdr *icmp = icmp6_hdr(skb);

	/* The ICMPv6 type and code fields use the 16-bit transport port
	 * fields, so we need to store them in 16-bit network byte order.
	 */
	key->ipv6.tp.src = htons(icmp->icmp6_type);
	key->ipv6.tp.dst = htons(icmp->icmp6_code);

	if (icmp->icmp6_code == 0 &&
	    (icmp->icmp6_type == NDISC_NEIGHBOUR_SOLICITATION ||
	     icmp->icmp6_type == NDISC_NEIGHBOUR_ADVERTISEMENT)) {
		int icmp_len = skb->len - skb_transport_offset(skb);
		struct nd_msg *nd;
		int offset;

		/* In order to process neighbor discovery options, we need the
		 * entire packet.
		 */
		if (unlikely(icmp_len < sizeof(*nd)))
			return 0;

		if (unlikely(skb_linearize(skb)))
			return -ENOMEM;

		nd = (struct nd_msg *)skb_transport_header(skb);
		key->ipv6.nd.target = nd->target;

		icmp_len -= sizeof(*nd);
		offset = 0;
		while (icmp_len >= 8) {
			struct nd_opt_hdr *nd_opt =
				 (struct nd_opt_hdr *)(nd->opt + offset);
			int opt_len = nd_opt->nd_opt_len * 8;

			if (unlikely(!opt_len || opt_len > icmp_len))
				return 0;

			/* Store the link layer address if the appropriate
			 * option is provided.  It is considered an error if
			 * the same link layer option is specified twice.
			 */
			if (nd_opt->nd_opt_type == ND_OPT_SOURCE_LL_ADDR
			    && opt_len == 8) {
				if (unlikely(!is_zero_ether_addr(key->ipv6.nd.sll)))
					goto invalid;
				memcpy(key->ipv6.nd.sll,
				    &nd->opt[offset+sizeof(*nd_opt)], ETH_ALEN);
			} else if (nd_opt->nd_opt_type == ND_OPT_TARGET_LL_ADDR
				   && opt_len == 8) {
				if (unlikely(!is_zero_ether_addr(key->ipv6.nd.tll)))
					goto invalid;
				memcpy(key->ipv6.nd.tll,
				    &nd->opt[offset+sizeof(*nd_opt)], ETH_ALEN);
			}

			icmp_len -= opt_len;
			offset += opt_len;
		}
	}

	return 0;

invalid:
	memset(&key->ipv6.nd.target, 0, sizeof(key->ipv6.nd.target));
	memset(key->ipv6.nd.sll, 0, sizeof(key->ipv6.nd.sll));
	memset(key->ipv6.nd.tll, 0, sizeof(key->ipv6.nd.tll));

	return 0;
}

/**
 * ovs_flow_extract - extracts a flow key from an Ethernet frame.
 * @skb: sk_buff that contains the frame, with skb->data pointing to the
 * Ethernet header
 * @in_port: port number on which @skb was received.
 * @key: output flow key
 *
 * The caller must ensure that skb->len >= ETH_HLEN.
 *
 * Returns 0 if successful, otherwise a negative errno value.
 *
 * Initializes @skb header pointers as follows:
 *
 *    - skb->mac_header: the Ethernet header.
 *
 *    - skb->network_header: just past the Ethernet header, or just past the
 *      VLAN header, to the first byte of the Ethernet payload.
 *
 *    - skb->transport_header: If key->eth.type is ETH_P_IP or ETH_P_IPV6
 *      on output, then just past the IP header, if one is present and
 *      of a correct length, otherwise the same as skb->network_header.
 *      For other key->eth.type values it is left untouched.
 */
int ovs_flow_extract(struct sk_buff *skb, u16 in_port, struct sw_flow_key *key)
{
	int error;
	struct ethhdr *eth;

	memset(key, 0, sizeof(*key));

	key->phy.priority = skb->priority;
	if (OVS_CB(skb)->tun_key)
		memcpy(&key->tun_key, OVS_CB(skb)->tun_key, sizeof(key->tun_key));
	key->phy.in_port = in_port;
	key->phy.skb_mark = skb->mark;

	skb_reset_mac_header(skb);

	/* Link layer.  We are guaranteed to have at least the 14 byte Ethernet
	 * header in the linear data area.
	 */
	eth = eth_hdr(skb);
	memcpy(key->eth.src, eth->h_source, ETH_ALEN);
	memcpy(key->eth.dst, eth->h_dest, ETH_ALEN);

	__skb_pull(skb, 2 * ETH_ALEN);
	/* We are going to push all headers that we pull, so no need to
	 * update skb->csum here.
	 */

	if (vlan_tx_tag_present(skb))
		key->eth.tci = htons(skb->vlan_tci);
	else if (eth->h_proto == htons(ETH_P_8021Q))
		if (unlikely(parse_vlan(skb, key)))
			return -ENOMEM;

	key->eth.type = parse_ethertype(skb);
	if (unlikely(key->eth.type == htons(0)))
		return -ENOMEM;

	skb_reset_network_header(skb);
	__skb_push(skb, skb->data - skb_mac_header(skb));

	/* Network layer. */
	if (key->eth.type == htons(ETH_P_IP)) {
		struct iphdr *nh;
		__be16 offset;

		error = check_iphdr(skb);
		if (unlikely(error)) {
			if (error == -EINVAL) {
				skb->transport_header = skb->network_header;
				error = 0;
			}
			return error;
		}

		nh = ip_hdr(skb);
		key->ipv4.addr.src = nh->saddr;
		key->ipv4.addr.dst = nh->daddr;

		key->ip.proto = nh->protocol;
		key->ip.tos = nh->tos;
		key->ip.ttl = nh->ttl;

		offset = nh->frag_off & htons(IP_OFFSET);
		if (offset) {
			key->ip.frag = OVS_FRAG_TYPE_LATER;
			return 0;
		}
		if (nh->frag_off & htons(IP_MF) ||
			 skb_shinfo(skb)->gso_type & SKB_GSO_UDP)
			key->ip.frag = OVS_FRAG_TYPE_FIRST;

		/* Transport layer. */
		if (key->ip.proto == IPPROTO_TCP) {
			if (tcphdr_ok(skb)) {
				struct tcphdr *tcp = tcp_hdr(skb);
				key->ipv4.tp.src = tcp->source;
				key->ipv4.tp.dst = tcp->dest;
			}
		} else if (key->ip.proto == IPPROTO_UDP) {
			if (udphdr_ok(skb)) {
				struct udphdr *udp = udp_hdr(skb);
				key->ipv4.tp.src = udp->source;
				key->ipv4.tp.dst = udp->dest;
			}
		} else if (key->ip.proto == IPPROTO_SCTP) {
			if (sctphdr_ok(skb)) {
				struct sctphdr *sctp = sctp_hdr(skb);
				key->ipv4.tp.src = sctp->source;
				key->ipv4.tp.dst = sctp->dest;
			}
		} else if (key->ip.proto == IPPROTO_ICMP) {
			if (icmphdr_ok(skb)) {
				struct icmphdr *icmp = icmp_hdr(skb);
				/* The ICMP type and code fields use the 16-bit
				 * transport port fields, so we need to store
				 * them in 16-bit network byte order. */
				key->ipv4.tp.src = htons(icmp->type);
				key->ipv4.tp.dst = htons(icmp->code);
			}
		}

	} else if ((key->eth.type == htons(ETH_P_ARP) ||
		   key->eth.type == htons(ETH_P_RARP)) && arphdr_ok(skb)) {
		struct arp_eth_header *arp;

		arp = (struct arp_eth_header *)skb_network_header(skb);

		if (arp->ar_hrd == htons(ARPHRD_ETHER)
				&& arp->ar_pro == htons(ETH_P_IP)
				&& arp->ar_hln == ETH_ALEN
				&& arp->ar_pln == 4) {

			/* We only match on the lower 8 bits of the opcode. */
			if (ntohs(arp->ar_op) <= 0xff)
				key->ip.proto = ntohs(arp->ar_op);
			memcpy(&key->ipv4.addr.src, arp->ar_sip, sizeof(key->ipv4.addr.src));
			memcpy(&key->ipv4.addr.dst, arp->ar_tip, sizeof(key->ipv4.addr.dst));
			memcpy(key->ipv4.arp.sha, arp->ar_sha, ETH_ALEN);
			memcpy(key->ipv4.arp.tha, arp->ar_tha, ETH_ALEN);
		}
	} else if (key->eth.type == htons(ETH_P_IPV6)) {
		int nh_len;             /* IPv6 Header + Extensions */

		nh_len = parse_ipv6hdr(skb, key);
		if (unlikely(nh_len < 0)) {
			if (nh_len == -EINVAL) {
				skb->transport_header = skb->network_header;
				error = 0;
			} else {
				error = nh_len;
			}
			return error;
		}

		if (key->ip.frag == OVS_FRAG_TYPE_LATER)
			return 0;
		if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP)
			key->ip.frag = OVS_FRAG_TYPE_FIRST;

		/* Transport layer. */
		if (key->ip.proto == NEXTHDR_TCP) {
			if (tcphdr_ok(skb)) {
				struct tcphdr *tcp = tcp_hdr(skb);
				key->ipv6.tp.src = tcp->source;
				key->ipv6.tp.dst = tcp->dest;
			}
		} else if (key->ip.proto == NEXTHDR_UDP) {
			if (udphdr_ok(skb)) {
				struct udphdr *udp = udp_hdr(skb);
				key->ipv6.tp.src = udp->source;
				key->ipv6.tp.dst = udp->dest;
			}
		} else if (key->ip.proto == NEXTHDR_SCTP) {
			if (sctphdr_ok(skb)) {
				struct sctphdr *sctp = sctp_hdr(skb);
				key->ipv6.tp.src = sctp->source;
				key->ipv6.tp.dst = sctp->dest;
			}
		} else if (key->ip.proto == NEXTHDR_ICMP) {
			if (icmp6hdr_ok(skb)) {
				error = parse_icmpv6(skb, key, nh_len);
				if (error)
					return error;
			}
		}
	}

	return 0;
}

static u32 ovs_flow_hash(const struct sw_flow_key *key, int key_start,
			 int key_end)
{
	return jhash2((u32 *)((u8 *)key + key_start),
		      DIV_ROUND_UP(key_end - key_start, sizeof(u32)), 0);
}

static int flow_key_start(const struct sw_flow_key *key)
{
	if (key->tun_key.ipv4_dst)
		return 0;
	else
		return offsetof(struct sw_flow_key, phy);
}

static bool __cmp_key(const struct sw_flow_key *key1,
		const struct sw_flow_key *key2,  int key_start, int key_end)
{
	return !memcmp((u8 *)key1 + key_start,
			(u8 *)key2 + key_start, (key_end - key_start));
}

static bool __flow_cmp_key(const struct sw_flow *flow,
		const struct sw_flow_key *key, int key_start, int key_end)
{
	return __cmp_key(&flow->key, key, key_start, key_end);
}

static bool __flow_cmp_unmasked_key(const struct sw_flow *flow,
		  const struct sw_flow_key *key, int key_start, int key_end)
{
	return __cmp_key(&flow->unmasked_key, key, key_start, key_end);
}

bool ovs_flow_cmp_unmasked_key(const struct sw_flow *flow,
		const struct sw_flow_key *key, int key_end)
{
	int key_start;
	key_start = flow_key_start(key);

	return __flow_cmp_unmasked_key(flow, key, key_start, key_end);

}

struct sw_flow *ovs_flow_lookup_unmasked_key(struct flow_table *table,
				       struct sw_flow_match *match)
{
	struct sw_flow_key *unmasked = match->key;
	int key_end = match->range.end;
	struct sw_flow *flow;

	flow = ovs_flow_lookup(table, unmasked);
	if (flow && (!ovs_flow_cmp_unmasked_key(flow, unmasked, key_end)))
		flow = NULL;

	return flow;
}

static struct sw_flow *ovs_masked_flow_lookup(struct flow_table *table,
				    const struct sw_flow_key *flow_key,
				    struct sw_flow_mask *mask)
{
	struct sw_flow *flow;
	struct hlist_head *head;
	int key_start = mask->range.start;
	int key_end = mask->range.end;
	u32 hash;
	struct sw_flow_key masked_key;

	ovs_flow_key_mask(&masked_key, flow_key, mask);
	hash = ovs_flow_hash(&masked_key, key_start, key_end);
	head = find_bucket(table, hash);
	hlist_for_each_entry_rcu(flow, head, hash_node[table->node_ver]) {
		if (flow->mask == mask &&
		    __flow_cmp_key(flow, &masked_key, key_start, key_end))
			return flow;
	}
	return NULL;
}

struct sw_flow *ovs_flow_lookup(struct flow_table *tbl,
				const struct sw_flow_key *key)
{
	struct sw_flow *flow = NULL;
	struct sw_flow_mask *mask;

	list_for_each_entry_rcu(mask, tbl->mask_list, list) {
		flow = ovs_masked_flow_lookup(tbl, key, mask);
		if (flow)  /* Found */
			break;
	}

	return flow;
}


void ovs_flow_insert(struct flow_table *table, struct sw_flow *flow)
{
	flow->hash = ovs_flow_hash(&flow->key, flow->mask->range.start,
			flow->mask->range.end);
	__tbl_insert(table, flow);
}

void ovs_flow_remove(struct flow_table *table, struct sw_flow *flow)
{
	BUG_ON(table->count == 0);
	hlist_del_rcu(&flow->hash_node[table->node_ver]);
	table->count--;
}

/* The size of the argument for each %OVS_KEY_ATTR_* Netlink attribute.  */
const int ovs_key_lens[OVS_KEY_ATTR_MAX + 1] = {
	[OVS_KEY_ATTR_ENCAP] = -1,
	[OVS_KEY_ATTR_PRIORITY] = sizeof(u32),
	[OVS_KEY_ATTR_IN_PORT] = sizeof(u32),
	[OVS_KEY_ATTR_SKB_MARK] = sizeof(u32),
	[OVS_KEY_ATTR_ETHERNET] = sizeof(struct ovs_key_ethernet),
	[OVS_KEY_ATTR_VLAN] = sizeof(__be16),
	[OVS_KEY_ATTR_ETHERTYPE] = sizeof(__be16),
	[OVS_KEY_ATTR_IPV4] = sizeof(struct ovs_key_ipv4),
	[OVS_KEY_ATTR_IPV6] = sizeof(struct ovs_key_ipv6),
	[OVS_KEY_ATTR_TCP] = sizeof(struct ovs_key_tcp),
	[OVS_KEY_ATTR_UDP] = sizeof(struct ovs_key_udp),
	[OVS_KEY_ATTR_SCTP] = sizeof(struct ovs_key_sctp),
	[OVS_KEY_ATTR_ICMP] = sizeof(struct ovs_key_icmp),
	[OVS_KEY_ATTR_ICMPV6] = sizeof(struct ovs_key_icmpv6),
	[OVS_KEY_ATTR_ARP] = sizeof(struct ovs_key_arp),
	[OVS_KEY_ATTR_ND] = sizeof(struct ovs_key_nd),
	[OVS_KEY_ATTR_TUNNEL] = -1,
};

static bool is_all_zero(const u8 *fp, size_t size)
{
	int i;

	if (!fp)
		return false;

	for (i = 0; i < size; i++)
		if (fp[i])
			return false;

	return true;
}

static int __parse_flow_nlattrs(const struct nlattr *attr,
			      const struct nlattr *a[],
			      u64 *attrsp, bool nz)
{
	const struct nlattr *nla;
	u32 attrs;
	int rem;

	attrs = *attrsp;
	nla_for_each_nested(nla, attr, rem) {
		u16 type = nla_type(nla);
		int expected_len;

		if (type > OVS_KEY_ATTR_MAX) {
			OVS_NLERR("Unknown key attribute (type=%d, max=%d).\n",
				  type, OVS_KEY_ATTR_MAX);
		}

		if (attrs & (1 << type)) {
			OVS_NLERR("Duplicate key attribute (type %d).\n", type);
			return -EINVAL;
		}

		expected_len = ovs_key_lens[type];
		if (nla_len(nla) != expected_len && expected_len != -1) {
			OVS_NLERR("Key attribute has unexpected length (type=%d"
				  ", length=%d, expected=%d).\n", type,
				  nla_len(nla), expected_len);
			return -EINVAL;
		}

		if (!nz || !is_all_zero(nla_data(nla), expected_len)) {
			attrs |= 1 << type;
			a[type] = nla;
		}
	}
	if (rem) {
		OVS_NLERR("Message has %d unknown bytes.\n", rem);
		return -EINVAL;
	}

	*attrsp = attrs;
	return 0;
}

static int parse_flow_mask_nlattrs(const struct nlattr *attr,
			      const struct nlattr *a[], u64 *attrsp)
{
	return __parse_flow_nlattrs(attr, a, attrsp, true);
}

static int parse_flow_nlattrs(const struct nlattr *attr,
			      const struct nlattr *a[], u64 *attrsp)
{
	return __parse_flow_nlattrs(attr, a, attrsp, false);
}

int ovs_ipv4_tun_from_nlattr(const struct nlattr *attr,
			     struct sw_flow_match *match, bool is_mask)
{
	struct nlattr *a;
	int rem;
	bool ttl = false;
	__be16 tun_flags = 0;

	nla_for_each_nested(a, attr, rem) {
		int type = nla_type(a);
		static const u32 ovs_tunnel_key_lens[OVS_TUNNEL_KEY_ATTR_MAX + 1] = {
			[OVS_TUNNEL_KEY_ATTR_ID] = sizeof(u64),
			[OVS_TUNNEL_KEY_ATTR_IPV4_SRC] = sizeof(u32),
			[OVS_TUNNEL_KEY_ATTR_IPV4_DST] = sizeof(u32),
			[OVS_TUNNEL_KEY_ATTR_TOS] = 1,
			[OVS_TUNNEL_KEY_ATTR_TTL] = 1,
			[OVS_TUNNEL_KEY_ATTR_DONT_FRAGMENT] = 0,
			[OVS_TUNNEL_KEY_ATTR_CSUM] = 0,
		};

		if (type > OVS_TUNNEL_KEY_ATTR_MAX) {
			OVS_NLERR("Unknown IPv4 tunnel attribute (type=%d, max=%d).\n",
			type, OVS_TUNNEL_KEY_ATTR_MAX);
			return -EINVAL;
		}

		if (ovs_tunnel_key_lens[type] != nla_len(a)) {
			OVS_NLERR("IPv4 tunnel attribute type has unexpected "
				  " length (type=%d, length=%d, expected=%d).\n",
				  type, nla_len(a), ovs_tunnel_key_lens[type]);
			return -EINVAL;
		}

		switch (type) {
		case OVS_TUNNEL_KEY_ATTR_ID:
			SW_FLOW_KEY_PUT(match, tun_key.tun_id,
					nla_get_be64(a), is_mask);
			tun_flags |= TUNNEL_KEY;
			break;
		case OVS_TUNNEL_KEY_ATTR_IPV4_SRC:
			SW_FLOW_KEY_PUT(match, tun_key.ipv4_src,
					nla_get_be32(a), is_mask);
			break;
		case OVS_TUNNEL_KEY_ATTR_IPV4_DST:
			SW_FLOW_KEY_PUT(match, tun_key.ipv4_dst,
					nla_get_be32(a), is_mask);
			break;
		case OVS_TUNNEL_KEY_ATTR_TOS:
			SW_FLOW_KEY_PUT(match, tun_key.ipv4_tos,
					nla_get_u8(a), is_mask);
			break;
		case OVS_TUNNEL_KEY_ATTR_TTL:
			SW_FLOW_KEY_PUT(match, tun_key.ipv4_ttl,
					nla_get_u8(a), is_mask);
			ttl = true;
			break;
		case OVS_TUNNEL_KEY_ATTR_DONT_FRAGMENT:
			tun_flags |= TUNNEL_DONT_FRAGMENT;
			break;
		case OVS_TUNNEL_KEY_ATTR_CSUM:
			tun_flags |= TUNNEL_CSUM;
			break;
		default:
			return -EINVAL;
		}
	}

	SW_FLOW_KEY_PUT(match, tun_key.tun_flags, tun_flags, is_mask);

	if (rem > 0) {
		OVS_NLERR("IPv4 tunnel attribute has %d unknown bytes.\n", rem);
		return -EINVAL;
	}

	if (!is_mask) {
		if (!match->key->tun_key.ipv4_dst) {
			OVS_NLERR("IPv4 tunnel destination address is zero.\n");
			return -EINVAL;
		}

		if (!ttl) {
			OVS_NLERR("IPv4 tunnel TTL not specified.\n");
			return -EINVAL;
		}
	}

	return 0;
}

int ovs_ipv4_tun_to_nlattr(struct sk_buff *skb,
			   const struct ovs_key_ipv4_tunnel *tun_key,
			   const struct ovs_key_ipv4_tunnel *output)
{
	struct nlattr *nla;

	nla = nla_nest_start(skb, OVS_KEY_ATTR_TUNNEL);
	if (!nla)
		return -EMSGSIZE;

	if (output->tun_flags & TUNNEL_KEY &&
	    nla_put_be64(skb, OVS_TUNNEL_KEY_ATTR_ID, output->tun_id))
		return -EMSGSIZE;
	if (output->ipv4_src &&
		nla_put_be32(skb, OVS_TUNNEL_KEY_ATTR_IPV4_SRC, output->ipv4_src))
		return -EMSGSIZE;
	if (output->ipv4_dst &&
		nla_put_be32(skb, OVS_TUNNEL_KEY_ATTR_IPV4_DST, output->ipv4_dst))
		return -EMSGSIZE;
	if (output->ipv4_tos &&
		nla_put_u8(skb, OVS_TUNNEL_KEY_ATTR_TOS, output->ipv4_tos))
		return -EMSGSIZE;
	if (nla_put_u8(skb, OVS_TUNNEL_KEY_ATTR_TTL, output->ipv4_ttl))
		return -EMSGSIZE;
	if ((output->tun_flags & TUNNEL_DONT_FRAGMENT) &&
		nla_put_flag(skb, OVS_TUNNEL_KEY_ATTR_DONT_FRAGMENT))
		return -EMSGSIZE;
	if ((output->tun_flags & TUNNEL_CSUM) &&
		nla_put_flag(skb, OVS_TUNNEL_KEY_ATTR_CSUM))
		return -EMSGSIZE;

	nla_nest_end(skb, nla);
	return 0;
}

static int metadata_from_nlattrs(struct sw_flow_match *match,  u64 *attrs,
		const struct nlattr **a, bool is_mask)
{
	if (*attrs & (1 << OVS_KEY_ATTR_PRIORITY)) {
		SW_FLOW_KEY_PUT(match, phy.priority,
			  nla_get_u32(a[OVS_KEY_ATTR_PRIORITY]), is_mask);
		*attrs &= ~(1 << OVS_KEY_ATTR_PRIORITY);
	}

	if (*attrs & (1 << OVS_KEY_ATTR_IN_PORT)) {
		u32 in_port = nla_get_u32(a[OVS_KEY_ATTR_IN_PORT]);

		if (is_mask)
			in_port = 0xffffffff; /* Always exact match in_port. */
		else if (in_port >= DP_MAX_PORTS)
			return -EINVAL;

		SW_FLOW_KEY_PUT(match, phy.in_port, in_port, is_mask);
		*attrs &= ~(1 << OVS_KEY_ATTR_IN_PORT);
	} else if (!is_mask) {
		SW_FLOW_KEY_PUT(match, phy.in_port, DP_MAX_PORTS, is_mask);
	}

	if (*attrs & (1 << OVS_KEY_ATTR_SKB_MARK)) {
		uint32_t mark = nla_get_u32(a[OVS_KEY_ATTR_SKB_MARK]);

		SW_FLOW_KEY_PUT(match, phy.skb_mark, mark, is_mask);
		*attrs &= ~(1 << OVS_KEY_ATTR_SKB_MARK);
	}
	if (*attrs & (1 << OVS_KEY_ATTR_TUNNEL)) {
		if (ovs_ipv4_tun_from_nlattr(a[OVS_KEY_ATTR_TUNNEL], match,
					is_mask))
			return -EINVAL;
		*attrs &= ~(1 << OVS_KEY_ATTR_TUNNEL);
	}
	return 0;
}

static int ovs_key_from_nlattrs(struct sw_flow_match *match,  u64 attrs,
		const struct nlattr **a, bool is_mask)
{
	int err;
	u64 orig_attrs = attrs;

	err = metadata_from_nlattrs(match, &attrs, a, is_mask);
	if (err)
		return err;

	if (attrs & (1 << OVS_KEY_ATTR_ETHERNET)) {
		const struct ovs_key_ethernet *eth_key;

		eth_key = nla_data(a[OVS_KEY_ATTR_ETHERNET]);
		SW_FLOW_KEY_MEMCPY(match, eth.src,
				eth_key->eth_src, ETH_ALEN, is_mask);
		SW_FLOW_KEY_MEMCPY(match, eth.dst,
				eth_key->eth_dst, ETH_ALEN, is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_ETHERNET);
	}

	if (attrs & (1 << OVS_KEY_ATTR_VLAN)) {
		__be16 tci;

		tci = nla_get_be16(a[OVS_KEY_ATTR_VLAN]);
		if (!(tci & htons(VLAN_TAG_PRESENT))) {
			if (is_mask)
				OVS_NLERR("VLAN TCI mask does not have exact match for VLAN_TAG_PRESENT bit.\n");
			else
				OVS_NLERR("VLAN TCI does not have VLAN_TAG_PRESENT bit set.\n");

			return -EINVAL;
		}

		SW_FLOW_KEY_PUT(match, eth.tci, tci, is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_VLAN);
	} else if (!is_mask)
		SW_FLOW_KEY_PUT(match, eth.tci, htons(0xffff), true);

	if (attrs & (1 << OVS_KEY_ATTR_ETHERTYPE)) {
		__be16 eth_type;

		eth_type = nla_get_be16(a[OVS_KEY_ATTR_ETHERTYPE]);
		if (is_mask) {
			/* Always exact match EtherType. */
			eth_type = htons(0xffff);
		} else if (ntohs(eth_type) < ETH_P_802_3_MIN) {
			OVS_NLERR("EtherType is less than minimum (type=%x, min=%x).\n",
					ntohs(eth_type), ETH_P_802_3_MIN);
			return -EINVAL;
		}

		SW_FLOW_KEY_PUT(match, eth.type, eth_type, is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_ETHERTYPE);
	} else if (!is_mask) {
		SW_FLOW_KEY_PUT(match, eth.type, htons(ETH_P_802_2), is_mask);
	}

	if (attrs & (1 << OVS_KEY_ATTR_IPV4)) {
		const struct ovs_key_ipv4 *ipv4_key;

		ipv4_key = nla_data(a[OVS_KEY_ATTR_IPV4]);
		if (!is_mask && ipv4_key->ipv4_frag > OVS_FRAG_TYPE_MAX) {
			OVS_NLERR("Unknown IPv4 fragment type (value=%d, max=%d).\n",
				ipv4_key->ipv4_frag, OVS_FRAG_TYPE_MAX);
			return -EINVAL;
		}
		SW_FLOW_KEY_PUT(match, ip.proto,
				ipv4_key->ipv4_proto, is_mask);
		SW_FLOW_KEY_PUT(match, ip.tos,
				ipv4_key->ipv4_tos, is_mask);
		SW_FLOW_KEY_PUT(match, ip.ttl,
				ipv4_key->ipv4_ttl, is_mask);
		SW_FLOW_KEY_PUT(match, ip.frag,
				ipv4_key->ipv4_frag, is_mask);
		SW_FLOW_KEY_PUT(match, ipv4.addr.src,
				ipv4_key->ipv4_src, is_mask);
		SW_FLOW_KEY_PUT(match, ipv4.addr.dst,
				ipv4_key->ipv4_dst, is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_IPV4);
	}

	if (attrs & (1 << OVS_KEY_ATTR_IPV6)) {
		const struct ovs_key_ipv6 *ipv6_key;

		ipv6_key = nla_data(a[OVS_KEY_ATTR_IPV6]);
		if (!is_mask && ipv6_key->ipv6_frag > OVS_FRAG_TYPE_MAX) {
			OVS_NLERR("Unknown IPv6 fragment type (value=%d, max=%d).\n",
				ipv6_key->ipv6_frag, OVS_FRAG_TYPE_MAX);
			return -EINVAL;
		}
		SW_FLOW_KEY_PUT(match, ipv6.label,
				ipv6_key->ipv6_label, is_mask);
		SW_FLOW_KEY_PUT(match, ip.proto,
				ipv6_key->ipv6_proto, is_mask);
		SW_FLOW_KEY_PUT(match, ip.tos,
				ipv6_key->ipv6_tclass, is_mask);
		SW_FLOW_KEY_PUT(match, ip.ttl,
				ipv6_key->ipv6_hlimit, is_mask);
		SW_FLOW_KEY_PUT(match, ip.frag,
				ipv6_key->ipv6_frag, is_mask);
		SW_FLOW_KEY_MEMCPY(match, ipv6.addr.src,
				ipv6_key->ipv6_src,
				sizeof(match->key->ipv6.addr.src),
				is_mask);
		SW_FLOW_KEY_MEMCPY(match, ipv6.addr.dst,
				ipv6_key->ipv6_dst,
				sizeof(match->key->ipv6.addr.dst),
				is_mask);

		attrs &= ~(1 << OVS_KEY_ATTR_IPV6);
	}

	if (attrs & (1 << OVS_KEY_ATTR_ARP)) {
		const struct ovs_key_arp *arp_key;

		arp_key = nla_data(a[OVS_KEY_ATTR_ARP]);
		if (!is_mask && (arp_key->arp_op & htons(0xff00))) {
			OVS_NLERR("Unknown ARP opcode (opcode=%d).\n",
				  arp_key->arp_op);
			return -EINVAL;
		}

		SW_FLOW_KEY_PUT(match, ipv4.addr.src,
				arp_key->arp_sip, is_mask);
		SW_FLOW_KEY_PUT(match, ipv4.addr.dst,
			arp_key->arp_tip, is_mask);
		SW_FLOW_KEY_PUT(match, ip.proto,
				ntohs(arp_key->arp_op), is_mask);
		SW_FLOW_KEY_MEMCPY(match, ipv4.arp.sha,
				arp_key->arp_sha, ETH_ALEN, is_mask);
		SW_FLOW_KEY_MEMCPY(match, ipv4.arp.tha,
				arp_key->arp_tha, ETH_ALEN, is_mask);

		attrs &= ~(1 << OVS_KEY_ATTR_ARP);
	}

	if (attrs & (1 << OVS_KEY_ATTR_TCP)) {
		const struct ovs_key_tcp *tcp_key;

		tcp_key = nla_data(a[OVS_KEY_ATTR_TCP]);
		if (orig_attrs & (1 << OVS_KEY_ATTR_IPV4)) {
			SW_FLOW_KEY_PUT(match, ipv4.tp.src,
					tcp_key->tcp_src, is_mask);
			SW_FLOW_KEY_PUT(match, ipv4.tp.dst,
					tcp_key->tcp_dst, is_mask);
		} else {
			SW_FLOW_KEY_PUT(match, ipv6.tp.src,
					tcp_key->tcp_src, is_mask);
			SW_FLOW_KEY_PUT(match, ipv6.tp.dst,
					tcp_key->tcp_dst, is_mask);
		}
		attrs &= ~(1 << OVS_KEY_ATTR_TCP);
	}

	if (attrs & (1 << OVS_KEY_ATTR_UDP)) {
		const struct ovs_key_udp *udp_key;

		udp_key = nla_data(a[OVS_KEY_ATTR_UDP]);
		if (orig_attrs & (1 << OVS_KEY_ATTR_IPV4)) {
			SW_FLOW_KEY_PUT(match, ipv4.tp.src,
					udp_key->udp_src, is_mask);
			SW_FLOW_KEY_PUT(match, ipv4.tp.dst,
					udp_key->udp_dst, is_mask);
		} else {
			SW_FLOW_KEY_PUT(match, ipv6.tp.src,
					udp_key->udp_src, is_mask);
			SW_FLOW_KEY_PUT(match, ipv6.tp.dst,
					udp_key->udp_dst, is_mask);
		}
		attrs &= ~(1 << OVS_KEY_ATTR_UDP);
	}

	if (attrs & (1 << OVS_KEY_ATTR_SCTP)) {
		const struct ovs_key_sctp *sctp_key;

		sctp_key = nla_data(a[OVS_KEY_ATTR_SCTP]);
		if (orig_attrs & (1 << OVS_KEY_ATTR_IPV4)) {
			SW_FLOW_KEY_PUT(match, ipv4.tp.src,
					sctp_key->sctp_src, is_mask);
			SW_FLOW_KEY_PUT(match, ipv4.tp.dst,
					sctp_key->sctp_dst, is_mask);
		} else {
			SW_FLOW_KEY_PUT(match, ipv6.tp.src,
					sctp_key->sctp_src, is_mask);
			SW_FLOW_KEY_PUT(match, ipv6.tp.dst,
					sctp_key->sctp_dst, is_mask);
		}
		attrs &= ~(1 << OVS_KEY_ATTR_SCTP);
	}

	if (attrs & (1 << OVS_KEY_ATTR_ICMP)) {
		const struct ovs_key_icmp *icmp_key;

		icmp_key = nla_data(a[OVS_KEY_ATTR_ICMP]);
		SW_FLOW_KEY_PUT(match, ipv4.tp.src,
				htons(icmp_key->icmp_type), is_mask);
		SW_FLOW_KEY_PUT(match, ipv4.tp.dst,
				htons(icmp_key->icmp_code), is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_ICMP);
	}

	if (attrs & (1 << OVS_KEY_ATTR_ICMPV6)) {
		const struct ovs_key_icmpv6 *icmpv6_key;

		icmpv6_key = nla_data(a[OVS_KEY_ATTR_ICMPV6]);
		SW_FLOW_KEY_PUT(match, ipv6.tp.src,
				htons(icmpv6_key->icmpv6_type), is_mask);
		SW_FLOW_KEY_PUT(match, ipv6.tp.dst,
				htons(icmpv6_key->icmpv6_code), is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_ICMPV6);
	}

	if (attrs & (1 << OVS_KEY_ATTR_ND)) {
		const struct ovs_key_nd *nd_key;

		nd_key = nla_data(a[OVS_KEY_ATTR_ND]);
		SW_FLOW_KEY_MEMCPY(match, ipv6.nd.target,
			nd_key->nd_target,
			sizeof(match->key->ipv6.nd.target),
			is_mask);
		SW_FLOW_KEY_MEMCPY(match, ipv6.nd.sll,
			nd_key->nd_sll, ETH_ALEN, is_mask);
		SW_FLOW_KEY_MEMCPY(match, ipv6.nd.tll,
				nd_key->nd_tll, ETH_ALEN, is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_ND);
	}

	if (attrs != 0)
		return -EINVAL;

	return 0;
}

/**
 * ovs_match_from_nlattrs - parses Netlink attributes into a flow key and
 * mask. In case the 'mask' is NULL, the flow is treated as exact match
 * flow. Otherwise, it is treated as a wildcarded flow, except the mask
 * does not include any don't care bit.
 * @match: receives the extracted flow match information.
 * @key: Netlink attribute holding nested %OVS_KEY_ATTR_* Netlink attribute
 * sequence. The fields should of the packet that triggered the creation
 * of this flow.
 * @mask: Optional. Netlink attribute holding nested %OVS_KEY_ATTR_* Netlink
 * attribute specifies the mask field of the wildcarded flow.
 */
int ovs_match_from_nlattrs(struct sw_flow_match *match,
			   const struct nlattr *key,
			   const struct nlattr *mask)
{
	const struct nlattr *a[OVS_KEY_ATTR_MAX + 1];
	const struct nlattr *encap;
	u64 key_attrs = 0;
	u64 mask_attrs = 0;
	bool encap_valid = false;
	int err;

	err = parse_flow_nlattrs(key, a, &key_attrs);
	if (err)
		return err;

	if ((key_attrs & (1 << OVS_KEY_ATTR_ETHERNET)) &&
	    (key_attrs & (1 << OVS_KEY_ATTR_ETHERTYPE)) &&
	    (nla_get_be16(a[OVS_KEY_ATTR_ETHERTYPE]) == htons(ETH_P_8021Q))) {
		__be16 tci;

		if (!((key_attrs & (1 << OVS_KEY_ATTR_VLAN)) &&
		      (key_attrs & (1 << OVS_KEY_ATTR_ENCAP)))) {
			OVS_NLERR("Invalid Vlan frame.\n");
			return -EINVAL;
		}

		key_attrs &= ~(1 << OVS_KEY_ATTR_ETHERTYPE);
		tci = nla_get_be16(a[OVS_KEY_ATTR_VLAN]);
		encap = a[OVS_KEY_ATTR_ENCAP];
		key_attrs &= ~(1 << OVS_KEY_ATTR_ENCAP);
		encap_valid = true;

		if (tci & htons(VLAN_TAG_PRESENT)) {
			err = parse_flow_nlattrs(encap, a, &key_attrs);
			if (err)
				return err;
		} else if (!tci) {
			/* Corner case for truncated 802.1Q header. */
			if (nla_len(encap)) {
				OVS_NLERR("Truncated 802.1Q header has non-zero encap attribute.\n");
				return -EINVAL;
			}
		} else {
			OVS_NLERR("Encap attribute is set for a non-VLAN frame.\n");
			return  -EINVAL;
		}
	}

	err = ovs_key_from_nlattrs(match, key_attrs, a, false);
	if (err)
		return err;

	if (mask) {
		err = parse_flow_mask_nlattrs(mask, a, &mask_attrs);
		if (err)
			return err;

		if (mask_attrs & 1ULL << OVS_KEY_ATTR_ENCAP)  {
			__be16 eth_type = 0;
			__be16 tci = 0;

			if (!encap_valid) {
				OVS_NLERR("Encap mask attribute is set for non-VLAN frame.\n");
				return  -EINVAL;
			}

			mask_attrs &= ~(1 << OVS_KEY_ATTR_ENCAP);
			if (a[OVS_KEY_ATTR_ETHERTYPE])
				eth_type = nla_get_be16(a[OVS_KEY_ATTR_ETHERTYPE]);

			if (eth_type == htons(0xffff)) {
				mask_attrs &= ~(1 << OVS_KEY_ATTR_ETHERTYPE);
				encap = a[OVS_KEY_ATTR_ENCAP];
				err = parse_flow_mask_nlattrs(encap, a, &mask_attrs);
			} else {
				OVS_NLERR("VLAN frames must have an exact match on the TPID (mask=%x).\n",
						ntohs(eth_type));
				return -EINVAL;
			}

			if (a[OVS_KEY_ATTR_VLAN])
				tci = nla_get_be16(a[OVS_KEY_ATTR_VLAN]);

			if (!(tci & htons(VLAN_TAG_PRESENT))) {
				OVS_NLERR("VLAN tag present bit must have an exact match (tci_mask=%x).\n", ntohs(tci));
				return -EINVAL;
			}
		}

		err = ovs_key_from_nlattrs(match, mask_attrs, a, true);
		if (err)
			return err;
	} else {
		/* Populate exact match flow's key mask. */
		if (match->mask)
			ovs_sw_flow_mask_set(match->mask, &match->range, 0xff);
	}

	if (!ovs_match_validate(match, key_attrs, mask_attrs))
		return -EINVAL;

	return 0;
}

/**
 * ovs_flow_metadata_from_nlattrs - parses Netlink attributes into a flow key.
 * @flow: Receives extracted in_port, priority, tun_key and skb_mark.
 * @attr: Netlink attribute holding nested %OVS_KEY_ATTR_* Netlink attribute
 * sequence.
 *
 * This parses a series of Netlink attributes that form a flow key, which must
 * take the same form accepted by flow_from_nlattrs(), but only enough of it to
 * get the metadata, that is, the parts of the flow key that cannot be
 * extracted from the packet itself.
 */

int ovs_flow_metadata_from_nlattrs(struct sw_flow *flow,
		const struct nlattr *attr)
{
	struct ovs_key_ipv4_tunnel *tun_key = &flow->key.tun_key;
	const struct nlattr *a[OVS_KEY_ATTR_MAX + 1];
	u64 attrs = 0;
	int err;
	struct sw_flow_match match;

	flow->key.phy.in_port = DP_MAX_PORTS;
	flow->key.phy.priority = 0;
	flow->key.phy.skb_mark = 0;
	memset(tun_key, 0, sizeof(flow->key.tun_key));

	err = parse_flow_nlattrs(attr, a, &attrs);
	if (err)
		return -EINVAL;

	memset(&match, 0, sizeof(match));
	match.key = &flow->key;

	err = metadata_from_nlattrs(&match, &attrs, a, false);
	if (err)
		return err;

	return 0;
}

int ovs_flow_to_nlattrs(const struct sw_flow_key *swkey,
		const struct sw_flow_key *output, struct sk_buff *skb)
{
	struct ovs_key_ethernet *eth_key;
	struct nlattr *nla, *encap;
	bool is_mask = (swkey != output);

	if (nla_put_u32(skb, OVS_KEY_ATTR_PRIORITY, output->phy.priority))
		goto nla_put_failure;

	if ((swkey->tun_key.ipv4_dst || is_mask) &&
	    ovs_ipv4_tun_to_nlattr(skb, &swkey->tun_key, &output->tun_key))
		goto nla_put_failure;

	if (swkey->phy.in_port == DP_MAX_PORTS) {
		if (is_mask && (output->phy.in_port == 0xffff))
			if (nla_put_u32(skb, OVS_KEY_ATTR_IN_PORT, 0xffffffff))
				goto nla_put_failure;
	} else {
		u16 upper_u16;
		upper_u16 = !is_mask ? 0 : 0xffff;

		if (nla_put_u32(skb, OVS_KEY_ATTR_IN_PORT,
				(upper_u16 << 16) | output->phy.in_port))
			goto nla_put_failure;
	}

	if (nla_put_u32(skb, OVS_KEY_ATTR_SKB_MARK, output->phy.skb_mark))
		goto nla_put_failure;

	nla = nla_reserve(skb, OVS_KEY_ATTR_ETHERNET, sizeof(*eth_key));
	if (!nla)
		goto nla_put_failure;

	eth_key = nla_data(nla);
	memcpy(eth_key->eth_src, output->eth.src, ETH_ALEN);
	memcpy(eth_key->eth_dst, output->eth.dst, ETH_ALEN);

	if (swkey->eth.tci || swkey->eth.type == htons(ETH_P_8021Q)) {
		__be16 eth_type;
		eth_type = !is_mask ? htons(ETH_P_8021Q) : htons(0xffff);
		if (nla_put_be16(skb, OVS_KEY_ATTR_ETHERTYPE, eth_type) ||
		    nla_put_be16(skb, OVS_KEY_ATTR_VLAN, output->eth.tci))
			goto nla_put_failure;
		encap = nla_nest_start(skb, OVS_KEY_ATTR_ENCAP);
		if (!swkey->eth.tci)
			goto unencap;
	} else
		encap = NULL;

	if (swkey->eth.type == htons(ETH_P_802_2)) {
		/*
		 * Ethertype 802.2 is represented in the netlink with omitted
		 * OVS_KEY_ATTR_ETHERTYPE in the flow key attribute, and
		 * 0xffff in the mask attribute.  Ethertype can also
		 * be wildcarded.
		 */
		if (is_mask && output->eth.type)
			if (nla_put_be16(skb, OVS_KEY_ATTR_ETHERTYPE,
						output->eth.type))
				goto nla_put_failure;
		goto unencap;
	}

	if (nla_put_be16(skb, OVS_KEY_ATTR_ETHERTYPE, output->eth.type))
		goto nla_put_failure;

	if (swkey->eth.type == htons(ETH_P_IP)) {
		struct ovs_key_ipv4 *ipv4_key;

		nla = nla_reserve(skb, OVS_KEY_ATTR_IPV4, sizeof(*ipv4_key));
		if (!nla)
			goto nla_put_failure;
		ipv4_key = nla_data(nla);
		ipv4_key->ipv4_src = output->ipv4.addr.src;
		ipv4_key->ipv4_dst = output->ipv4.addr.dst;
		ipv4_key->ipv4_proto = output->ip.proto;
		ipv4_key->ipv4_tos = output->ip.tos;
		ipv4_key->ipv4_ttl = output->ip.ttl;
		ipv4_key->ipv4_frag = output->ip.frag;
	} else if (swkey->eth.type == htons(ETH_P_IPV6)) {
		struct ovs_key_ipv6 *ipv6_key;

		nla = nla_reserve(skb, OVS_KEY_ATTR_IPV6, sizeof(*ipv6_key));
		if (!nla)
			goto nla_put_failure;
		ipv6_key = nla_data(nla);
		memcpy(ipv6_key->ipv6_src, &output->ipv6.addr.src,
				sizeof(ipv6_key->ipv6_src));
		memcpy(ipv6_key->ipv6_dst, &output->ipv6.addr.dst,
				sizeof(ipv6_key->ipv6_dst));
		ipv6_key->ipv6_label = output->ipv6.label;
		ipv6_key->ipv6_proto = output->ip.proto;
		ipv6_key->ipv6_tclass = output->ip.tos;
		ipv6_key->ipv6_hlimit = output->ip.ttl;
		ipv6_key->ipv6_frag = output->ip.frag;
	} else if (swkey->eth.type == htons(ETH_P_ARP) ||
		   swkey->eth.type == htons(ETH_P_RARP)) {
		struct ovs_key_arp *arp_key;

		nla = nla_reserve(skb, OVS_KEY_ATTR_ARP, sizeof(*arp_key));
		if (!nla)
			goto nla_put_failure;
		arp_key = nla_data(nla);
		memset(arp_key, 0, sizeof(struct ovs_key_arp));
		arp_key->arp_sip = output->ipv4.addr.src;
		arp_key->arp_tip = output->ipv4.addr.dst;
		arp_key->arp_op = htons(output->ip.proto);
		memcpy(arp_key->arp_sha, output->ipv4.arp.sha, ETH_ALEN);
		memcpy(arp_key->arp_tha, output->ipv4.arp.tha, ETH_ALEN);
	}

	if ((swkey->eth.type == htons(ETH_P_IP) ||
	     swkey->eth.type == htons(ETH_P_IPV6)) &&
	     swkey->ip.frag != OVS_FRAG_TYPE_LATER) {

		if (swkey->ip.proto == IPPROTO_TCP) {
			struct ovs_key_tcp *tcp_key;

			nla = nla_reserve(skb, OVS_KEY_ATTR_TCP, sizeof(*tcp_key));
			if (!nla)
				goto nla_put_failure;
			tcp_key = nla_data(nla);
			if (swkey->eth.type == htons(ETH_P_IP)) {
				tcp_key->tcp_src = output->ipv4.tp.src;
				tcp_key->tcp_dst = output->ipv4.tp.dst;
			} else if (swkey->eth.type == htons(ETH_P_IPV6)) {
				tcp_key->tcp_src = output->ipv6.tp.src;
				tcp_key->tcp_dst = output->ipv6.tp.dst;
			}
		} else if (swkey->ip.proto == IPPROTO_UDP) {
			struct ovs_key_udp *udp_key;

			nla = nla_reserve(skb, OVS_KEY_ATTR_UDP, sizeof(*udp_key));
			if (!nla)
				goto nla_put_failure;
			udp_key = nla_data(nla);
			if (swkey->eth.type == htons(ETH_P_IP)) {
				udp_key->udp_src = output->ipv4.tp.src;
				udp_key->udp_dst = output->ipv4.tp.dst;
			} else if (swkey->eth.type == htons(ETH_P_IPV6)) {
				udp_key->udp_src = output->ipv6.tp.src;
				udp_key->udp_dst = output->ipv6.tp.dst;
			}
		} else if (swkey->ip.proto == IPPROTO_SCTP) {
			struct ovs_key_sctp *sctp_key;

			nla = nla_reserve(skb, OVS_KEY_ATTR_SCTP, sizeof(*sctp_key));
			if (!nla)
				goto nla_put_failure;
			sctp_key = nla_data(nla);
			if (swkey->eth.type == htons(ETH_P_IP)) {
				sctp_key->sctp_src = swkey->ipv4.tp.src;
				sctp_key->sctp_dst = swkey->ipv4.tp.dst;
			} else if (swkey->eth.type == htons(ETH_P_IPV6)) {
				sctp_key->sctp_src = swkey->ipv6.tp.src;
				sctp_key->sctp_dst = swkey->ipv6.tp.dst;
			}
		} else if (swkey->eth.type == htons(ETH_P_IP) &&
			   swkey->ip.proto == IPPROTO_ICMP) {
			struct ovs_key_icmp *icmp_key;

			nla = nla_reserve(skb, OVS_KEY_ATTR_ICMP, sizeof(*icmp_key));
			if (!nla)
				goto nla_put_failure;
			icmp_key = nla_data(nla);
			icmp_key->icmp_type = ntohs(output->ipv4.tp.src);
			icmp_key->icmp_code = ntohs(output->ipv4.tp.dst);
		} else if (swkey->eth.type == htons(ETH_P_IPV6) &&
			   swkey->ip.proto == IPPROTO_ICMPV6) {
			struct ovs_key_icmpv6 *icmpv6_key;

			nla = nla_reserve(skb, OVS_KEY_ATTR_ICMPV6,
						sizeof(*icmpv6_key));
			if (!nla)
				goto nla_put_failure;
			icmpv6_key = nla_data(nla);
			icmpv6_key->icmpv6_type = ntohs(output->ipv6.tp.src);
			icmpv6_key->icmpv6_code = ntohs(output->ipv6.tp.dst);

			if (icmpv6_key->icmpv6_type == NDISC_NEIGHBOUR_SOLICITATION ||
			    icmpv6_key->icmpv6_type == NDISC_NEIGHBOUR_ADVERTISEMENT) {
				struct ovs_key_nd *nd_key;

				nla = nla_reserve(skb, OVS_KEY_ATTR_ND, sizeof(*nd_key));
				if (!nla)
					goto nla_put_failure;
				nd_key = nla_data(nla);
				memcpy(nd_key->nd_target, &output->ipv6.nd.target,
							sizeof(nd_key->nd_target));
				memcpy(nd_key->nd_sll, output->ipv6.nd.sll, ETH_ALEN);
				memcpy(nd_key->nd_tll, output->ipv6.nd.tll, ETH_ALEN);
			}
		}
	}

unencap:
	if (encap)
		nla_nest_end(skb, encap);

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

/* Initializes the flow module.
 * Returns zero if successful or a negative error code. */
int ovs_flow_init(void)
{
	flow_cache = kmem_cache_create("sw_flow", sizeof(struct sw_flow), 0,
					0, NULL);
	if (flow_cache == NULL)
		return -ENOMEM;

	return 0;
}

/* Uninitializes the flow module. */
void ovs_flow_exit(void)
{
	kmem_cache_destroy(flow_cache);
}

struct sw_flow_mask *ovs_sw_flow_mask_alloc(void)
{
	struct sw_flow_mask *mask;

	mask = kmalloc(sizeof(*mask), GFP_KERNEL);
	if (mask)
		mask->ref_count = 0;

	return mask;
}

void ovs_sw_flow_mask_add_ref(struct sw_flow_mask *mask)
{
	mask->ref_count++;
}

void ovs_sw_flow_mask_del_ref(struct sw_flow_mask *mask, bool deferred)
{
	if (!mask)
		return;

	BUG_ON(!mask->ref_count);
	mask->ref_count--;

	if (!mask->ref_count) {
		list_del_rcu(&mask->list);
		if (deferred)
			kfree_rcu(mask, rcu);
		else
			kfree(mask);
	}
}

static bool ovs_sw_flow_mask_equal(const struct sw_flow_mask *a,
		const struct sw_flow_mask *b)
{
	u8 *a_ = (u8 *)&a->key + a->range.start;
	u8 *b_ = (u8 *)&b->key + b->range.start;

	return  (a->range.end == b->range.end)
		&& (a->range.start == b->range.start)
		&& (memcmp(a_, b_, ovs_sw_flow_mask_actual_size(a)) == 0);
}

struct sw_flow_mask *ovs_sw_flow_mask_find(const struct flow_table *tbl,
                                           const struct sw_flow_mask *mask)
{
	struct list_head *ml;

	list_for_each(ml, tbl->mask_list) {
		struct sw_flow_mask *m;
		m = container_of(ml, struct sw_flow_mask, list);
		if (ovs_sw_flow_mask_equal(mask, m))
			return m;
	}

	return NULL;
}

/**
 * add a new mask into the mask list.
 * The caller needs to make sure that 'mask' is not the same
 * as any masks that are already on the list.
 */
void ovs_sw_flow_mask_insert(struct flow_table *tbl, struct sw_flow_mask *mask)
{
	list_add_rcu(&mask->list, tbl->mask_list);
}

/**
 * Set 'range' fields in the mask to the value of 'val'.
 */
static void ovs_sw_flow_mask_set(struct sw_flow_mask *mask,
		struct sw_flow_key_range *range, u8 val)
{
	u8 *m = (u8 *)&mask->key + range->start;

	mask->range = *range;
	memset(m, val, ovs_sw_flow_mask_size_roundup(mask));
}
