// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2007-2017 Nicira, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/openvswitch.h>
#include <linux/sctp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/in6.h>
#include <linux/if_arp.h>
#include <linux/if_vlan.h>

#include <net/dst.h>
#include <net/gso.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/ip6_fib.h>
#include <net/checksum.h>
#include <net/dsfield.h>
#include <net/mpls.h>
#include <net/sctp/checksum.h>

#include "datapath.h"
#include "drop.h"
#include "flow.h"
#include "conntrack.h"
#include "vport.h"
#include "flow_netlink.h"
#include "openvswitch_trace.h"

struct deferred_action {
	struct sk_buff *skb;
	const struct nlattr *actions;
	int actions_len;

	/* Store pkt_key clone when creating deferred action. */
	struct sw_flow_key pkt_key;
};

#define MAX_L2_LEN	(VLAN_ETH_HLEN + 3 * MPLS_HLEN)
struct ovs_frag_data {
	unsigned long dst;
	struct vport *vport;
	struct ovs_skb_cb cb;
	__be16 inner_protocol;
	u16 network_offset;	/* valid only for MPLS */
	u16 vlan_tci;
	__be16 vlan_proto;
	unsigned int l2_len;
	u8 mac_proto;
	u8 l2_data[MAX_L2_LEN];
};

static DEFINE_PER_CPU(struct ovs_frag_data, ovs_frag_data_storage);

#define DEFERRED_ACTION_FIFO_SIZE 10
#define OVS_RECURSION_LIMIT 5
#define OVS_DEFERRED_ACTION_THRESHOLD (OVS_RECURSION_LIMIT - 2)
struct action_fifo {
	int head;
	int tail;
	/* Deferred action fifo queue storage. */
	struct deferred_action fifo[DEFERRED_ACTION_FIFO_SIZE];
};

struct action_flow_keys {
	struct sw_flow_key key[OVS_DEFERRED_ACTION_THRESHOLD];
};

static struct action_fifo __percpu *action_fifos;
static struct action_flow_keys __percpu *flow_keys;
static DEFINE_PER_CPU(int, exec_actions_level);

/* Make a clone of the 'key', using the pre-allocated percpu 'flow_keys'
 * space. Return NULL if out of key spaces.
 */
static struct sw_flow_key *clone_key(const struct sw_flow_key *key_)
{
	struct action_flow_keys *keys = this_cpu_ptr(flow_keys);
	int level = this_cpu_read(exec_actions_level);
	struct sw_flow_key *key = NULL;

	if (level <= OVS_DEFERRED_ACTION_THRESHOLD) {
		key = &keys->key[level - 1];
		*key = *key_;
	}

	return key;
}

static void action_fifo_init(struct action_fifo *fifo)
{
	fifo->head = 0;
	fifo->tail = 0;
}

static bool action_fifo_is_empty(const struct action_fifo *fifo)
{
	return (fifo->head == fifo->tail);
}

static struct deferred_action *action_fifo_get(struct action_fifo *fifo)
{
	if (action_fifo_is_empty(fifo))
		return NULL;

	return &fifo->fifo[fifo->tail++];
}

static struct deferred_action *action_fifo_put(struct action_fifo *fifo)
{
	if (fifo->head >= DEFERRED_ACTION_FIFO_SIZE - 1)
		return NULL;

	return &fifo->fifo[fifo->head++];
}

/* Return true if fifo is not full */
static struct deferred_action *add_deferred_actions(struct sk_buff *skb,
				    const struct sw_flow_key *key,
				    const struct nlattr *actions,
				    const int actions_len)
{
	struct action_fifo *fifo;
	struct deferred_action *da;

	fifo = this_cpu_ptr(action_fifos);
	da = action_fifo_put(fifo);
	if (da) {
		da->skb = skb;
		da->actions = actions;
		da->actions_len = actions_len;
		da->pkt_key = *key;
	}

	return da;
}

static void invalidate_flow_key(struct sw_flow_key *key)
{
	key->mac_proto |= SW_FLOW_KEY_INVALID;
}

static bool is_flow_key_valid(const struct sw_flow_key *key)
{
	return !(key->mac_proto & SW_FLOW_KEY_INVALID);
}

static int clone_execute(struct datapath *dp, struct sk_buff *skb,
			 struct sw_flow_key *key,
			 u32 recirc_id,
			 const struct nlattr *actions, int len,
			 bool last, bool clone_flow_key);

static int do_execute_actions(struct datapath *dp, struct sk_buff *skb,
			      struct sw_flow_key *key,
			      const struct nlattr *attr, int len);

static int push_mpls(struct sk_buff *skb, struct sw_flow_key *key,
		     __be32 mpls_lse, __be16 mpls_ethertype, __u16 mac_len)
{
	int err;

	err = skb_mpls_push(skb, mpls_lse, mpls_ethertype, mac_len, !!mac_len);
	if (err)
		return err;

	if (!mac_len)
		key->mac_proto = MAC_PROTO_NONE;

	invalidate_flow_key(key);
	return 0;
}

static int pop_mpls(struct sk_buff *skb, struct sw_flow_key *key,
		    const __be16 ethertype)
{
	int err;

	err = skb_mpls_pop(skb, ethertype, skb->mac_len,
			   ovs_key_mac_proto(key) == MAC_PROTO_ETHERNET);
	if (err)
		return err;

	if (ethertype == htons(ETH_P_TEB))
		key->mac_proto = MAC_PROTO_ETHERNET;

	invalidate_flow_key(key);
	return 0;
}

static int set_mpls(struct sk_buff *skb, struct sw_flow_key *flow_key,
		    const __be32 *mpls_lse, const __be32 *mask)
{
	struct mpls_shim_hdr *stack;
	__be32 lse;
	int err;

	if (!pskb_may_pull(skb, skb_network_offset(skb) + MPLS_HLEN))
		return -ENOMEM;

	stack = mpls_hdr(skb);
	lse = OVS_MASKED(stack->label_stack_entry, *mpls_lse, *mask);
	err = skb_mpls_update_lse(skb, lse);
	if (err)
		return err;

	flow_key->mpls.lse[0] = lse;
	return 0;
}

static int pop_vlan(struct sk_buff *skb, struct sw_flow_key *key)
{
	int err;

	err = skb_vlan_pop(skb);
	if (skb_vlan_tag_present(skb)) {
		invalidate_flow_key(key);
	} else {
		key->eth.vlan.tci = 0;
		key->eth.vlan.tpid = 0;
	}
	return err;
}

static int push_vlan(struct sk_buff *skb, struct sw_flow_key *key,
		     const struct ovs_action_push_vlan *vlan)
{
	if (skb_vlan_tag_present(skb)) {
		invalidate_flow_key(key);
	} else {
		key->eth.vlan.tci = vlan->vlan_tci;
		key->eth.vlan.tpid = vlan->vlan_tpid;
	}
	return skb_vlan_push(skb, vlan->vlan_tpid,
			     ntohs(vlan->vlan_tci) & ~VLAN_CFI_MASK);
}

/* 'src' is already properly masked. */
static void ether_addr_copy_masked(u8 *dst_, const u8 *src_, const u8 *mask_)
{
	u16 *dst = (u16 *)dst_;
	const u16 *src = (const u16 *)src_;
	const u16 *mask = (const u16 *)mask_;

	OVS_SET_MASKED(dst[0], src[0], mask[0]);
	OVS_SET_MASKED(dst[1], src[1], mask[1]);
	OVS_SET_MASKED(dst[2], src[2], mask[2]);
}

static int set_eth_addr(struct sk_buff *skb, struct sw_flow_key *flow_key,
			const struct ovs_key_ethernet *key,
			const struct ovs_key_ethernet *mask)
{
	int err;

	err = skb_ensure_writable(skb, ETH_HLEN);
	if (unlikely(err))
		return err;

	skb_postpull_rcsum(skb, eth_hdr(skb), ETH_ALEN * 2);

	ether_addr_copy_masked(eth_hdr(skb)->h_source, key->eth_src,
			       mask->eth_src);
	ether_addr_copy_masked(eth_hdr(skb)->h_dest, key->eth_dst,
			       mask->eth_dst);

	skb_postpush_rcsum(skb, eth_hdr(skb), ETH_ALEN * 2);

	ether_addr_copy(flow_key->eth.src, eth_hdr(skb)->h_source);
	ether_addr_copy(flow_key->eth.dst, eth_hdr(skb)->h_dest);
	return 0;
}

/* pop_eth does not support VLAN packets as this action is never called
 * for them.
 */
static int pop_eth(struct sk_buff *skb, struct sw_flow_key *key)
{
	int err;

	err = skb_eth_pop(skb);
	if (err)
		return err;

	/* safe right before invalidate_flow_key */
	key->mac_proto = MAC_PROTO_NONE;
	invalidate_flow_key(key);
	return 0;
}

static int push_eth(struct sk_buff *skb, struct sw_flow_key *key,
		    const struct ovs_action_push_eth *ethh)
{
	int err;

	err = skb_eth_push(skb, ethh->addresses.eth_dst,
			   ethh->addresses.eth_src);
	if (err)
		return err;

	/* safe right before invalidate_flow_key */
	key->mac_proto = MAC_PROTO_ETHERNET;
	invalidate_flow_key(key);
	return 0;
}

static noinline_for_stack int push_nsh(struct sk_buff *skb,
				       struct sw_flow_key *key,
				       const struct nlattr *a)
{
	u8 buffer[NSH_HDR_MAX_LEN];
	struct nshhdr *nh = (struct nshhdr *)buffer;
	int err;

	err = nsh_hdr_from_nlattr(a, nh, NSH_HDR_MAX_LEN);
	if (err)
		return err;

	err = nsh_push(skb, nh);
	if (err)
		return err;

	/* safe right before invalidate_flow_key */
	key->mac_proto = MAC_PROTO_NONE;
	invalidate_flow_key(key);
	return 0;
}

static int pop_nsh(struct sk_buff *skb, struct sw_flow_key *key)
{
	int err;

	err = nsh_pop(skb);
	if (err)
		return err;

	/* safe right before invalidate_flow_key */
	if (skb->protocol == htons(ETH_P_TEB))
		key->mac_proto = MAC_PROTO_ETHERNET;
	else
		key->mac_proto = MAC_PROTO_NONE;
	invalidate_flow_key(key);
	return 0;
}

static void update_ip_l4_checksum(struct sk_buff *skb, struct iphdr *nh,
				  __be32 addr, __be32 new_addr)
{
	int transport_len = skb->len - skb_transport_offset(skb);

	if (nh->frag_off & htons(IP_OFFSET))
		return;

	if (nh->protocol == IPPROTO_TCP) {
		if (likely(transport_len >= sizeof(struct tcphdr)))
			inet_proto_csum_replace4(&tcp_hdr(skb)->check, skb,
						 addr, new_addr, true);
	} else if (nh->protocol == IPPROTO_UDP) {
		if (likely(transport_len >= sizeof(struct udphdr))) {
			struct udphdr *uh = udp_hdr(skb);

			if (uh->check || skb->ip_summed == CHECKSUM_PARTIAL) {
				inet_proto_csum_replace4(&uh->check, skb,
							 addr, new_addr, true);
				if (!uh->check)
					uh->check = CSUM_MANGLED_0;
			}
		}
	}
}

static void set_ip_addr(struct sk_buff *skb, struct iphdr *nh,
			__be32 *addr, __be32 new_addr)
{
	update_ip_l4_checksum(skb, nh, *addr, new_addr);
	csum_replace4(&nh->check, *addr, new_addr);
	skb_clear_hash(skb);
	ovs_ct_clear(skb, NULL);
	*addr = new_addr;
}

static void update_ipv6_checksum(struct sk_buff *skb, u8 l4_proto,
				 __be32 addr[4], const __be32 new_addr[4])
{
	int transport_len = skb->len - skb_transport_offset(skb);

	if (l4_proto == NEXTHDR_TCP) {
		if (likely(transport_len >= sizeof(struct tcphdr)))
			inet_proto_csum_replace16(&tcp_hdr(skb)->check, skb,
						  addr, new_addr, true);
	} else if (l4_proto == NEXTHDR_UDP) {
		if (likely(transport_len >= sizeof(struct udphdr))) {
			struct udphdr *uh = udp_hdr(skb);

			if (uh->check || skb->ip_summed == CHECKSUM_PARTIAL) {
				inet_proto_csum_replace16(&uh->check, skb,
							  addr, new_addr, true);
				if (!uh->check)
					uh->check = CSUM_MANGLED_0;
			}
		}
	} else if (l4_proto == NEXTHDR_ICMP) {
		if (likely(transport_len >= sizeof(struct icmp6hdr)))
			inet_proto_csum_replace16(&icmp6_hdr(skb)->icmp6_cksum,
						  skb, addr, new_addr, true);
	}
}

static void mask_ipv6_addr(const __be32 old[4], const __be32 addr[4],
			   const __be32 mask[4], __be32 masked[4])
{
	masked[0] = OVS_MASKED(old[0], addr[0], mask[0]);
	masked[1] = OVS_MASKED(old[1], addr[1], mask[1]);
	masked[2] = OVS_MASKED(old[2], addr[2], mask[2]);
	masked[3] = OVS_MASKED(old[3], addr[3], mask[3]);
}

static void set_ipv6_addr(struct sk_buff *skb, u8 l4_proto,
			  __be32 addr[4], const __be32 new_addr[4],
			  bool recalculate_csum)
{
	if (recalculate_csum)
		update_ipv6_checksum(skb, l4_proto, addr, new_addr);

	skb_clear_hash(skb);
	ovs_ct_clear(skb, NULL);
	memcpy(addr, new_addr, sizeof(__be32[4]));
}

static void set_ipv6_dsfield(struct sk_buff *skb, struct ipv6hdr *nh, u8 ipv6_tclass, u8 mask)
{
	u8 old_ipv6_tclass = ipv6_get_dsfield(nh);

	ipv6_tclass = OVS_MASKED(old_ipv6_tclass, ipv6_tclass, mask);

	if (skb->ip_summed == CHECKSUM_COMPLETE)
		csum_replace(&skb->csum, (__force __wsum)(old_ipv6_tclass << 12),
			     (__force __wsum)(ipv6_tclass << 12));

	ipv6_change_dsfield(nh, ~mask, ipv6_tclass);
}

static void set_ipv6_fl(struct sk_buff *skb, struct ipv6hdr *nh, u32 fl, u32 mask)
{
	u32 ofl;

	ofl = nh->flow_lbl[0] << 16 |  nh->flow_lbl[1] << 8 |  nh->flow_lbl[2];
	fl = OVS_MASKED(ofl, fl, mask);

	/* Bits 21-24 are always unmasked, so this retains their values. */
	nh->flow_lbl[0] = (u8)(fl >> 16);
	nh->flow_lbl[1] = (u8)(fl >> 8);
	nh->flow_lbl[2] = (u8)fl;

	if (skb->ip_summed == CHECKSUM_COMPLETE)
		csum_replace(&skb->csum, (__force __wsum)htonl(ofl), (__force __wsum)htonl(fl));
}

static void set_ipv6_ttl(struct sk_buff *skb, struct ipv6hdr *nh, u8 new_ttl, u8 mask)
{
	new_ttl = OVS_MASKED(nh->hop_limit, new_ttl, mask);

	if (skb->ip_summed == CHECKSUM_COMPLETE)
		csum_replace(&skb->csum, (__force __wsum)(nh->hop_limit << 8),
			     (__force __wsum)(new_ttl << 8));
	nh->hop_limit = new_ttl;
}

static void set_ip_ttl(struct sk_buff *skb, struct iphdr *nh, u8 new_ttl,
		       u8 mask)
{
	new_ttl = OVS_MASKED(nh->ttl, new_ttl, mask);

	csum_replace2(&nh->check, htons(nh->ttl << 8), htons(new_ttl << 8));
	nh->ttl = new_ttl;
}

static int set_ipv4(struct sk_buff *skb, struct sw_flow_key *flow_key,
		    const struct ovs_key_ipv4 *key,
		    const struct ovs_key_ipv4 *mask)
{
	struct iphdr *nh;
	__be32 new_addr;
	int err;

	err = skb_ensure_writable(skb, skb_network_offset(skb) +
				  sizeof(struct iphdr));
	if (unlikely(err))
		return err;

	nh = ip_hdr(skb);

	/* Setting an IP addresses is typically only a side effect of
	 * matching on them in the current userspace implementation, so it
	 * makes sense to check if the value actually changed.
	 */
	if (mask->ipv4_src) {
		new_addr = OVS_MASKED(nh->saddr, key->ipv4_src, mask->ipv4_src);

		if (unlikely(new_addr != nh->saddr)) {
			set_ip_addr(skb, nh, &nh->saddr, new_addr);
			flow_key->ipv4.addr.src = new_addr;
		}
	}
	if (mask->ipv4_dst) {
		new_addr = OVS_MASKED(nh->daddr, key->ipv4_dst, mask->ipv4_dst);

		if (unlikely(new_addr != nh->daddr)) {
			set_ip_addr(skb, nh, &nh->daddr, new_addr);
			flow_key->ipv4.addr.dst = new_addr;
		}
	}
	if (mask->ipv4_tos) {
		ipv4_change_dsfield(nh, ~mask->ipv4_tos, key->ipv4_tos);
		flow_key->ip.tos = nh->tos;
	}
	if (mask->ipv4_ttl) {
		set_ip_ttl(skb, nh, key->ipv4_ttl, mask->ipv4_ttl);
		flow_key->ip.ttl = nh->ttl;
	}

	return 0;
}

static bool is_ipv6_mask_nonzero(const __be32 addr[4])
{
	return !!(addr[0] | addr[1] | addr[2] | addr[3]);
}

static int set_ipv6(struct sk_buff *skb, struct sw_flow_key *flow_key,
		    const struct ovs_key_ipv6 *key,
		    const struct ovs_key_ipv6 *mask)
{
	struct ipv6hdr *nh;
	int err;

	err = skb_ensure_writable(skb, skb_network_offset(skb) +
				  sizeof(struct ipv6hdr));
	if (unlikely(err))
		return err;

	nh = ipv6_hdr(skb);

	/* Setting an IP addresses is typically only a side effect of
	 * matching on them in the current userspace implementation, so it
	 * makes sense to check if the value actually changed.
	 */
	if (is_ipv6_mask_nonzero(mask->ipv6_src)) {
		__be32 *saddr = (__be32 *)&nh->saddr;
		__be32 masked[4];

		mask_ipv6_addr(saddr, key->ipv6_src, mask->ipv6_src, masked);

		if (unlikely(memcmp(saddr, masked, sizeof(masked)))) {
			set_ipv6_addr(skb, flow_key->ip.proto, saddr, masked,
				      true);
			memcpy(&flow_key->ipv6.addr.src, masked,
			       sizeof(flow_key->ipv6.addr.src));
		}
	}
	if (is_ipv6_mask_nonzero(mask->ipv6_dst)) {
		unsigned int offset = 0;
		int flags = IP6_FH_F_SKIP_RH;
		bool recalc_csum = true;
		__be32 *daddr = (__be32 *)&nh->daddr;
		__be32 masked[4];

		mask_ipv6_addr(daddr, key->ipv6_dst, mask->ipv6_dst, masked);

		if (unlikely(memcmp(daddr, masked, sizeof(masked)))) {
			if (ipv6_ext_hdr(nh->nexthdr))
				recalc_csum = (ipv6_find_hdr(skb, &offset,
							     NEXTHDR_ROUTING,
							     NULL, &flags)
					       != NEXTHDR_ROUTING);

			set_ipv6_addr(skb, flow_key->ip.proto, daddr, masked,
				      recalc_csum);
			memcpy(&flow_key->ipv6.addr.dst, masked,
			       sizeof(flow_key->ipv6.addr.dst));
		}
	}
	if (mask->ipv6_tclass) {
		set_ipv6_dsfield(skb, nh, key->ipv6_tclass, mask->ipv6_tclass);
		flow_key->ip.tos = ipv6_get_dsfield(nh);
	}
	if (mask->ipv6_label) {
		set_ipv6_fl(skb, nh, ntohl(key->ipv6_label),
			    ntohl(mask->ipv6_label));
		flow_key->ipv6.label =
		    *(__be32 *)nh & htonl(IPV6_FLOWINFO_FLOWLABEL);
	}
	if (mask->ipv6_hlimit) {
		set_ipv6_ttl(skb, nh, key->ipv6_hlimit, mask->ipv6_hlimit);
		flow_key->ip.ttl = nh->hop_limit;
	}
	return 0;
}

static int set_nsh(struct sk_buff *skb, struct sw_flow_key *flow_key,
		   const struct nlattr *a)
{
	struct nshhdr *nh;
	size_t length;
	int err;
	u8 flags;
	u8 ttl;
	int i;

	struct ovs_key_nsh key;
	struct ovs_key_nsh mask;

	err = nsh_key_from_nlattr(a, &key, &mask);
	if (err)
		return err;

	/* Make sure the NSH base header is there */
	if (!pskb_may_pull(skb, skb_network_offset(skb) + NSH_BASE_HDR_LEN))
		return -ENOMEM;

	nh = nsh_hdr(skb);
	length = nsh_hdr_len(nh);

	/* Make sure the whole NSH header is there */
	err = skb_ensure_writable(skb, skb_network_offset(skb) +
				       length);
	if (unlikely(err))
		return err;

	nh = nsh_hdr(skb);
	skb_postpull_rcsum(skb, nh, length);
	flags = nsh_get_flags(nh);
	flags = OVS_MASKED(flags, key.base.flags, mask.base.flags);
	flow_key->nsh.base.flags = flags;
	ttl = nsh_get_ttl(nh);
	ttl = OVS_MASKED(ttl, key.base.ttl, mask.base.ttl);
	flow_key->nsh.base.ttl = ttl;
	nsh_set_flags_and_ttl(nh, flags, ttl);
	nh->path_hdr = OVS_MASKED(nh->path_hdr, key.base.path_hdr,
				  mask.base.path_hdr);
	flow_key->nsh.base.path_hdr = nh->path_hdr;
	switch (nh->mdtype) {
	case NSH_M_TYPE1:
		for (i = 0; i < NSH_MD1_CONTEXT_SIZE; i++) {
			nh->md1.context[i] =
			    OVS_MASKED(nh->md1.context[i], key.context[i],
				       mask.context[i]);
		}
		memcpy(flow_key->nsh.context, nh->md1.context,
		       sizeof(nh->md1.context));
		break;
	case NSH_M_TYPE2:
		memset(flow_key->nsh.context, 0,
		       sizeof(flow_key->nsh.context));
		break;
	default:
		return -EINVAL;
	}
	skb_postpush_rcsum(skb, nh, length);
	return 0;
}

/* Must follow skb_ensure_writable() since that can move the skb data. */
static void set_tp_port(struct sk_buff *skb, __be16 *port,
			__be16 new_port, __sum16 *check)
{
	ovs_ct_clear(skb, NULL);
	inet_proto_csum_replace2(check, skb, *port, new_port, false);
	*port = new_port;
}

static int set_udp(struct sk_buff *skb, struct sw_flow_key *flow_key,
		   const struct ovs_key_udp *key,
		   const struct ovs_key_udp *mask)
{
	struct udphdr *uh;
	__be16 src, dst;
	int err;

	err = skb_ensure_writable(skb, skb_transport_offset(skb) +
				  sizeof(struct udphdr));
	if (unlikely(err))
		return err;

	uh = udp_hdr(skb);
	/* Either of the masks is non-zero, so do not bother checking them. */
	src = OVS_MASKED(uh->source, key->udp_src, mask->udp_src);
	dst = OVS_MASKED(uh->dest, key->udp_dst, mask->udp_dst);

	if (uh->check && skb->ip_summed != CHECKSUM_PARTIAL) {
		if (likely(src != uh->source)) {
			set_tp_port(skb, &uh->source, src, &uh->check);
			flow_key->tp.src = src;
		}
		if (likely(dst != uh->dest)) {
			set_tp_port(skb, &uh->dest, dst, &uh->check);
			flow_key->tp.dst = dst;
		}

		if (unlikely(!uh->check))
			uh->check = CSUM_MANGLED_0;
	} else {
		uh->source = src;
		uh->dest = dst;
		flow_key->tp.src = src;
		flow_key->tp.dst = dst;
		ovs_ct_clear(skb, NULL);
	}

	skb_clear_hash(skb);

	return 0;
}

static int set_tcp(struct sk_buff *skb, struct sw_flow_key *flow_key,
		   const struct ovs_key_tcp *key,
		   const struct ovs_key_tcp *mask)
{
	struct tcphdr *th;
	__be16 src, dst;
	int err;

	err = skb_ensure_writable(skb, skb_transport_offset(skb) +
				  sizeof(struct tcphdr));
	if (unlikely(err))
		return err;

	th = tcp_hdr(skb);
	src = OVS_MASKED(th->source, key->tcp_src, mask->tcp_src);
	if (likely(src != th->source)) {
		set_tp_port(skb, &th->source, src, &th->check);
		flow_key->tp.src = src;
	}
	dst = OVS_MASKED(th->dest, key->tcp_dst, mask->tcp_dst);
	if (likely(dst != th->dest)) {
		set_tp_port(skb, &th->dest, dst, &th->check);
		flow_key->tp.dst = dst;
	}
	skb_clear_hash(skb);

	return 0;
}

static int set_sctp(struct sk_buff *skb, struct sw_flow_key *flow_key,
		    const struct ovs_key_sctp *key,
		    const struct ovs_key_sctp *mask)
{
	unsigned int sctphoff = skb_transport_offset(skb);
	struct sctphdr *sh;
	__le32 old_correct_csum, new_csum, old_csum;
	int err;

	err = skb_ensure_writable(skb, sctphoff + sizeof(struct sctphdr));
	if (unlikely(err))
		return err;

	sh = sctp_hdr(skb);
	old_csum = sh->checksum;
	old_correct_csum = sctp_compute_cksum(skb, sctphoff);

	sh->source = OVS_MASKED(sh->source, key->sctp_src, mask->sctp_src);
	sh->dest = OVS_MASKED(sh->dest, key->sctp_dst, mask->sctp_dst);

	new_csum = sctp_compute_cksum(skb, sctphoff);

	/* Carry any checksum errors through. */
	sh->checksum = old_csum ^ old_correct_csum ^ new_csum;

	skb_clear_hash(skb);
	ovs_ct_clear(skb, NULL);

	flow_key->tp.src = sh->source;
	flow_key->tp.dst = sh->dest;

	return 0;
}

static int ovs_vport_output(struct net *net, struct sock *sk,
			    struct sk_buff *skb)
{
	struct ovs_frag_data *data = this_cpu_ptr(&ovs_frag_data_storage);
	struct vport *vport = data->vport;

	if (skb_cow_head(skb, data->l2_len) < 0) {
		kfree_skb_reason(skb, SKB_DROP_REASON_NOMEM);
		return -ENOMEM;
	}

	__skb_dst_copy(skb, data->dst);
	*OVS_CB(skb) = data->cb;
	skb->inner_protocol = data->inner_protocol;
	if (data->vlan_tci & VLAN_CFI_MASK)
		__vlan_hwaccel_put_tag(skb, data->vlan_proto, data->vlan_tci & ~VLAN_CFI_MASK);
	else
		__vlan_hwaccel_clear_tag(skb);

	/* Reconstruct the MAC header.  */
	skb_push(skb, data->l2_len);
	memcpy(skb->data, &data->l2_data, data->l2_len);
	skb_postpush_rcsum(skb, skb->data, data->l2_len);
	skb_reset_mac_header(skb);

	if (eth_p_mpls(skb->protocol)) {
		skb->inner_network_header = skb->network_header;
		skb_set_network_header(skb, data->network_offset);
		skb_reset_mac_len(skb);
	}

	ovs_vport_send(vport, skb, data->mac_proto);
	return 0;
}

static unsigned int
ovs_dst_get_mtu(const struct dst_entry *dst)
{
	return dst->dev->mtu;
}

static struct dst_ops ovs_dst_ops = {
	.family = AF_UNSPEC,
	.mtu = ovs_dst_get_mtu,
};

/* prepare_frag() is called once per (larger-than-MTU) frame; its inverse is
 * ovs_vport_output(), which is called once per fragmented packet.
 */
static void prepare_frag(struct vport *vport, struct sk_buff *skb,
			 u16 orig_network_offset, u8 mac_proto)
{
	unsigned int hlen = skb_network_offset(skb);
	struct ovs_frag_data *data;

	data = this_cpu_ptr(&ovs_frag_data_storage);
	data->dst = skb->_skb_refdst;
	data->vport = vport;
	data->cb = *OVS_CB(skb);
	data->inner_protocol = skb->inner_protocol;
	data->network_offset = orig_network_offset;
	if (skb_vlan_tag_present(skb))
		data->vlan_tci = skb_vlan_tag_get(skb) | VLAN_CFI_MASK;
	else
		data->vlan_tci = 0;
	data->vlan_proto = skb->vlan_proto;
	data->mac_proto = mac_proto;
	data->l2_len = hlen;
	memcpy(&data->l2_data, skb->data, hlen);

	memset(IPCB(skb), 0, sizeof(struct inet_skb_parm));
	skb_pull(skb, hlen);
}

static void ovs_fragment(struct net *net, struct vport *vport,
			 struct sk_buff *skb, u16 mru,
			 struct sw_flow_key *key)
{
	enum ovs_drop_reason reason;
	u16 orig_network_offset = 0;

	if (eth_p_mpls(skb->protocol)) {
		orig_network_offset = skb_network_offset(skb);
		skb->network_header = skb->inner_network_header;
	}

	if (skb_network_offset(skb) > MAX_L2_LEN) {
		OVS_NLERR(1, "L2 header too long to fragment");
		reason = OVS_DROP_FRAG_L2_TOO_LONG;
		goto err;
	}

	if (key->eth.type == htons(ETH_P_IP)) {
		struct rtable ovs_rt = { 0 };
		unsigned long orig_dst;

		prepare_frag(vport, skb, orig_network_offset,
			     ovs_key_mac_proto(key));
		dst_init(&ovs_rt.dst, &ovs_dst_ops, NULL,
			 DST_OBSOLETE_NONE, DST_NOCOUNT);
		ovs_rt.dst.dev = vport->dev;

		orig_dst = skb->_skb_refdst;
		skb_dst_set_noref(skb, &ovs_rt.dst);
		IPCB(skb)->frag_max_size = mru;

		ip_do_fragment(net, skb->sk, skb, ovs_vport_output);
		refdst_drop(orig_dst);
	} else if (key->eth.type == htons(ETH_P_IPV6)) {
		unsigned long orig_dst;
		struct rt6_info ovs_rt;

		prepare_frag(vport, skb, orig_network_offset,
			     ovs_key_mac_proto(key));
		memset(&ovs_rt, 0, sizeof(ovs_rt));
		dst_init(&ovs_rt.dst, &ovs_dst_ops, NULL,
			 DST_OBSOLETE_NONE, DST_NOCOUNT);
		ovs_rt.dst.dev = vport->dev;

		orig_dst = skb->_skb_refdst;
		skb_dst_set_noref(skb, &ovs_rt.dst);
		IP6CB(skb)->frag_max_size = mru;

		ipv6_stub->ipv6_fragment(net, skb->sk, skb, ovs_vport_output);
		refdst_drop(orig_dst);
	} else {
		WARN_ONCE(1, "Failed fragment ->%s: eth=%04x, MRU=%d, MTU=%d.",
			  ovs_vport_name(vport), ntohs(key->eth.type), mru,
			  vport->dev->mtu);
		reason = OVS_DROP_FRAG_INVALID_PROTO;
		goto err;
	}

	return;
err:
	ovs_kfree_skb_reason(skb, reason);
}

static void do_output(struct datapath *dp, struct sk_buff *skb, int out_port,
		      struct sw_flow_key *key)
{
	struct vport *vport = ovs_vport_rcu(dp, out_port);

	if (likely(vport && netif_carrier_ok(vport->dev))) {
		u16 mru = OVS_CB(skb)->mru;
		u32 cutlen = OVS_CB(skb)->cutlen;

		if (unlikely(cutlen > 0)) {
			if (skb->len - cutlen > ovs_mac_header_len(key))
				pskb_trim(skb, skb->len - cutlen);
			else
				pskb_trim(skb, ovs_mac_header_len(key));
		}

		if (likely(!mru ||
		           (skb->len <= mru + vport->dev->hard_header_len))) {
			ovs_vport_send(vport, skb, ovs_key_mac_proto(key));
		} else if (mru <= vport->dev->mtu) {
			struct net *net = read_pnet(&dp->net);

			ovs_fragment(net, vport, skb, mru, key);
		} else {
			kfree_skb_reason(skb, SKB_DROP_REASON_PKT_TOO_BIG);
		}
	} else {
		kfree_skb_reason(skb, SKB_DROP_REASON_DEV_READY);
	}
}

static int output_userspace(struct datapath *dp, struct sk_buff *skb,
			    struct sw_flow_key *key, const struct nlattr *attr,
			    const struct nlattr *actions, int actions_len,
			    uint32_t cutlen)
{
	struct dp_upcall_info upcall;
	const struct nlattr *a;
	int rem;

	memset(&upcall, 0, sizeof(upcall));
	upcall.cmd = OVS_PACKET_CMD_ACTION;
	upcall.mru = OVS_CB(skb)->mru;

	for (a = nla_data(attr), rem = nla_len(attr); rem > 0;
	     a = nla_next(a, &rem)) {
		switch (nla_type(a)) {
		case OVS_USERSPACE_ATTR_USERDATA:
			upcall.userdata = a;
			break;

		case OVS_USERSPACE_ATTR_PID:
			if (dp->user_features &
			    OVS_DP_F_DISPATCH_UPCALL_PER_CPU)
				upcall.portid =
				  ovs_dp_get_upcall_portid(dp,
							   smp_processor_id());
			else
				upcall.portid = nla_get_u32(a);
			break;

		case OVS_USERSPACE_ATTR_EGRESS_TUN_PORT: {
			/* Get out tunnel info. */
			struct vport *vport;

			vport = ovs_vport_rcu(dp, nla_get_u32(a));
			if (vport) {
				int err;

				err = dev_fill_metadata_dst(vport->dev, skb);
				if (!err)
					upcall.egress_tun_info = skb_tunnel_info(skb);
			}

			break;
		}

		case OVS_USERSPACE_ATTR_ACTIONS: {
			/* Include actions. */
			upcall.actions = actions;
			upcall.actions_len = actions_len;
			break;
		}

		} /* End of switch. */
	}

	return ovs_dp_upcall(dp, skb, key, &upcall, cutlen);
}

static int dec_ttl_exception_handler(struct datapath *dp, struct sk_buff *skb,
				     struct sw_flow_key *key,
				     const struct nlattr *attr)
{
	/* The first attribute is always 'OVS_DEC_TTL_ATTR_ACTION'. */
	struct nlattr *actions = nla_data(attr);

	if (nla_len(actions))
		return clone_execute(dp, skb, key, 0, nla_data(actions),
				     nla_len(actions), true, false);

	ovs_kfree_skb_reason(skb, OVS_DROP_IP_TTL);
	return 0;
}

/* When 'last' is true, sample() should always consume the 'skb'.
 * Otherwise, sample() should keep 'skb' intact regardless what
 * actions are executed within sample().
 */
static int sample(struct datapath *dp, struct sk_buff *skb,
		  struct sw_flow_key *key, const struct nlattr *attr,
		  bool last)
{
	struct nlattr *actions;
	struct nlattr *sample_arg;
	int rem = nla_len(attr);
	const struct sample_arg *arg;
	bool clone_flow_key;

	/* The first action is always 'OVS_SAMPLE_ATTR_ARG'. */
	sample_arg = nla_data(attr);
	arg = nla_data(sample_arg);
	actions = nla_next(sample_arg, &rem);

	if ((arg->probability != U32_MAX) &&
	    (!arg->probability || get_random_u32() > arg->probability)) {
		if (last)
			ovs_kfree_skb_reason(skb, OVS_DROP_LAST_ACTION);
		return 0;
	}

	clone_flow_key = !arg->exec;
	return clone_execute(dp, skb, key, 0, actions, rem, last,
			     clone_flow_key);
}

/* When 'last' is true, clone() should always consume the 'skb'.
 * Otherwise, clone() should keep 'skb' intact regardless what
 * actions are executed within clone().
 */
static int clone(struct datapath *dp, struct sk_buff *skb,
		 struct sw_flow_key *key, const struct nlattr *attr,
		 bool last)
{
	struct nlattr *actions;
	struct nlattr *clone_arg;
	int rem = nla_len(attr);
	bool dont_clone_flow_key;

	/* The first action is always 'OVS_CLONE_ATTR_EXEC'. */
	clone_arg = nla_data(attr);
	dont_clone_flow_key = nla_get_u32(clone_arg);
	actions = nla_next(clone_arg, &rem);

	return clone_execute(dp, skb, key, 0, actions, rem, last,
			     !dont_clone_flow_key);
}

static void execute_hash(struct sk_buff *skb, struct sw_flow_key *key,
			 const struct nlattr *attr)
{
	struct ovs_action_hash *hash_act = nla_data(attr);
	u32 hash = 0;

	if (hash_act->hash_alg == OVS_HASH_ALG_L4) {
		/* OVS_HASH_ALG_L4 hasing type. */
		hash = skb_get_hash(skb);
	} else if (hash_act->hash_alg == OVS_HASH_ALG_SYM_L4) {
		/* OVS_HASH_ALG_SYM_L4 hashing type.  NOTE: this doesn't
		 * extend past an encapsulated header.
		 */
		hash = __skb_get_hash_symmetric(skb);
	}

	hash = jhash_1word(hash, hash_act->hash_basis);
	if (!hash)
		hash = 0x1;

	key->ovs_flow_hash = hash;
}

static int execute_set_action(struct sk_buff *skb,
			      struct sw_flow_key *flow_key,
			      const struct nlattr *a)
{
	/* Only tunnel set execution is supported without a mask. */
	if (nla_type(a) == OVS_KEY_ATTR_TUNNEL_INFO) {
		struct ovs_tunnel_info *tun = nla_data(a);

		skb_dst_drop(skb);
		dst_hold((struct dst_entry *)tun->tun_dst);
		skb_dst_set(skb, (struct dst_entry *)tun->tun_dst);
		return 0;
	}

	return -EINVAL;
}

/* Mask is at the midpoint of the data. */
#define get_mask(a, type) ((const type)nla_data(a) + 1)

static int execute_masked_set_action(struct sk_buff *skb,
				     struct sw_flow_key *flow_key,
				     const struct nlattr *a)
{
	int err = 0;

	switch (nla_type(a)) {
	case OVS_KEY_ATTR_PRIORITY:
		OVS_SET_MASKED(skb->priority, nla_get_u32(a),
			       *get_mask(a, u32 *));
		flow_key->phy.priority = skb->priority;
		break;

	case OVS_KEY_ATTR_SKB_MARK:
		OVS_SET_MASKED(skb->mark, nla_get_u32(a), *get_mask(a, u32 *));
		flow_key->phy.skb_mark = skb->mark;
		break;

	case OVS_KEY_ATTR_TUNNEL_INFO:
		/* Masked data not supported for tunnel. */
		err = -EINVAL;
		break;

	case OVS_KEY_ATTR_ETHERNET:
		err = set_eth_addr(skb, flow_key, nla_data(a),
				   get_mask(a, struct ovs_key_ethernet *));
		break;

	case OVS_KEY_ATTR_NSH:
		err = set_nsh(skb, flow_key, a);
		break;

	case OVS_KEY_ATTR_IPV4:
		err = set_ipv4(skb, flow_key, nla_data(a),
			       get_mask(a, struct ovs_key_ipv4 *));
		break;

	case OVS_KEY_ATTR_IPV6:
		err = set_ipv6(skb, flow_key, nla_data(a),
			       get_mask(a, struct ovs_key_ipv6 *));
		break;

	case OVS_KEY_ATTR_TCP:
		err = set_tcp(skb, flow_key, nla_data(a),
			      get_mask(a, struct ovs_key_tcp *));
		break;

	case OVS_KEY_ATTR_UDP:
		err = set_udp(skb, flow_key, nla_data(a),
			      get_mask(a, struct ovs_key_udp *));
		break;

	case OVS_KEY_ATTR_SCTP:
		err = set_sctp(skb, flow_key, nla_data(a),
			       get_mask(a, struct ovs_key_sctp *));
		break;

	case OVS_KEY_ATTR_MPLS:
		err = set_mpls(skb, flow_key, nla_data(a), get_mask(a,
								    __be32 *));
		break;

	case OVS_KEY_ATTR_CT_STATE:
	case OVS_KEY_ATTR_CT_ZONE:
	case OVS_KEY_ATTR_CT_MARK:
	case OVS_KEY_ATTR_CT_LABELS:
	case OVS_KEY_ATTR_CT_ORIG_TUPLE_IPV4:
	case OVS_KEY_ATTR_CT_ORIG_TUPLE_IPV6:
		err = -EINVAL;
		break;
	}

	return err;
}

static int execute_recirc(struct datapath *dp, struct sk_buff *skb,
			  struct sw_flow_key *key,
			  const struct nlattr *a, bool last)
{
	u32 recirc_id;

	if (!is_flow_key_valid(key)) {
		int err;

		err = ovs_flow_key_update(skb, key);
		if (err)
			return err;
	}
	BUG_ON(!is_flow_key_valid(key));

	recirc_id = nla_get_u32(a);
	return clone_execute(dp, skb, key, recirc_id, NULL, 0, last, true);
}

static int execute_check_pkt_len(struct datapath *dp, struct sk_buff *skb,
				 struct sw_flow_key *key,
				 const struct nlattr *attr, bool last)
{
	struct ovs_skb_cb *ovs_cb = OVS_CB(skb);
	const struct nlattr *actions, *cpl_arg;
	int len, max_len, rem = nla_len(attr);
	const struct check_pkt_len_arg *arg;
	bool clone_flow_key;

	/* The first netlink attribute in 'attr' is always
	 * 'OVS_CHECK_PKT_LEN_ATTR_ARG'.
	 */
	cpl_arg = nla_data(attr);
	arg = nla_data(cpl_arg);

	len = ovs_cb->mru ? ovs_cb->mru + skb->mac_len : skb->len;
	max_len = arg->pkt_len;

	if ((skb_is_gso(skb) && skb_gso_validate_mac_len(skb, max_len)) ||
	    len <= max_len) {
		/* Second netlink attribute in 'attr' is always
		 * 'OVS_CHECK_PKT_LEN_ATTR_ACTIONS_IF_LESS_EQUAL'.
		 */
		actions = nla_next(cpl_arg, &rem);
		clone_flow_key = !arg->exec_for_lesser_equal;
	} else {
		/* Third netlink attribute in 'attr' is always
		 * 'OVS_CHECK_PKT_LEN_ATTR_ACTIONS_IF_GREATER'.
		 */
		actions = nla_next(cpl_arg, &rem);
		actions = nla_next(actions, &rem);
		clone_flow_key = !arg->exec_for_greater;
	}

	return clone_execute(dp, skb, key, 0, nla_data(actions),
			     nla_len(actions), last, clone_flow_key);
}

static int execute_dec_ttl(struct sk_buff *skb, struct sw_flow_key *key)
{
	int err;

	if (skb->protocol == htons(ETH_P_IPV6)) {
		struct ipv6hdr *nh;

		err = skb_ensure_writable(skb, skb_network_offset(skb) +
					  sizeof(*nh));
		if (unlikely(err))
			return err;

		nh = ipv6_hdr(skb);

		if (nh->hop_limit <= 1)
			return -EHOSTUNREACH;

		key->ip.ttl = --nh->hop_limit;
	} else if (skb->protocol == htons(ETH_P_IP)) {
		struct iphdr *nh;
		u8 old_ttl;

		err = skb_ensure_writable(skb, skb_network_offset(skb) +
					  sizeof(*nh));
		if (unlikely(err))
			return err;

		nh = ip_hdr(skb);
		if (nh->ttl <= 1)
			return -EHOSTUNREACH;

		old_ttl = nh->ttl--;
		csum_replace2(&nh->check, htons(old_ttl << 8),
			      htons(nh->ttl << 8));
		key->ip.ttl = nh->ttl;
	}
	return 0;
}

/* Execute a list of actions against 'skb'. */
static int do_execute_actions(struct datapath *dp, struct sk_buff *skb,
			      struct sw_flow_key *key,
			      const struct nlattr *attr, int len)
{
	const struct nlattr *a;
	int rem;

	for (a = attr, rem = len; rem > 0;
	     a = nla_next(a, &rem)) {
		int err = 0;

		if (trace_ovs_do_execute_action_enabled())
			trace_ovs_do_execute_action(dp, skb, key, a, rem);

		/* Actions that rightfully have to consume the skb should do it
		 * and return directly.
		 */
		switch (nla_type(a)) {
		case OVS_ACTION_ATTR_OUTPUT: {
			int port = nla_get_u32(a);
			struct sk_buff *clone;

			/* Every output action needs a separate clone
			 * of 'skb', In case the output action is the
			 * last action, cloning can be avoided.
			 */
			if (nla_is_last(a, rem)) {
				do_output(dp, skb, port, key);
				/* 'skb' has been used for output.
				 */
				return 0;
			}

			clone = skb_clone(skb, GFP_ATOMIC);
			if (clone)
				do_output(dp, clone, port, key);
			OVS_CB(skb)->cutlen = 0;
			break;
		}

		case OVS_ACTION_ATTR_TRUNC: {
			struct ovs_action_trunc *trunc = nla_data(a);

			if (skb->len > trunc->max_len)
				OVS_CB(skb)->cutlen = skb->len - trunc->max_len;
			break;
		}

		case OVS_ACTION_ATTR_USERSPACE:
			output_userspace(dp, skb, key, a, attr,
						     len, OVS_CB(skb)->cutlen);
			OVS_CB(skb)->cutlen = 0;
			if (nla_is_last(a, rem)) {
				consume_skb(skb);
				return 0;
			}
			break;

		case OVS_ACTION_ATTR_HASH:
			execute_hash(skb, key, a);
			break;

		case OVS_ACTION_ATTR_PUSH_MPLS: {
			struct ovs_action_push_mpls *mpls = nla_data(a);

			err = push_mpls(skb, key, mpls->mpls_lse,
					mpls->mpls_ethertype, skb->mac_len);
			break;
		}
		case OVS_ACTION_ATTR_ADD_MPLS: {
			struct ovs_action_add_mpls *mpls = nla_data(a);
			__u16 mac_len = 0;

			if (mpls->tun_flags & OVS_MPLS_L3_TUNNEL_FLAG_MASK)
				mac_len = skb->mac_len;

			err = push_mpls(skb, key, mpls->mpls_lse,
					mpls->mpls_ethertype, mac_len);
			break;
		}
		case OVS_ACTION_ATTR_POP_MPLS:
			err = pop_mpls(skb, key, nla_get_be16(a));
			break;

		case OVS_ACTION_ATTR_PUSH_VLAN:
			err = push_vlan(skb, key, nla_data(a));
			break;

		case OVS_ACTION_ATTR_POP_VLAN:
			err = pop_vlan(skb, key);
			break;

		case OVS_ACTION_ATTR_RECIRC: {
			bool last = nla_is_last(a, rem);

			err = execute_recirc(dp, skb, key, a, last);
			if (last) {
				/* If this is the last action, the skb has
				 * been consumed or freed.
				 * Return immediately.
				 */
				return err;
			}
			break;
		}

		case OVS_ACTION_ATTR_SET:
			err = execute_set_action(skb, key, nla_data(a));
			break;

		case OVS_ACTION_ATTR_SET_MASKED:
		case OVS_ACTION_ATTR_SET_TO_MASKED:
			err = execute_masked_set_action(skb, key, nla_data(a));
			break;

		case OVS_ACTION_ATTR_SAMPLE: {
			bool last = nla_is_last(a, rem);

			err = sample(dp, skb, key, a, last);
			if (last)
				return err;

			break;
		}

		case OVS_ACTION_ATTR_CT:
			if (!is_flow_key_valid(key)) {
				err = ovs_flow_key_update(skb, key);
				if (err)
					return err;
			}

			err = ovs_ct_execute(ovs_dp_get_net(dp), skb, key,
					     nla_data(a));

			/* Hide stolen IP fragments from user space. */
			if (err)
				return err == -EINPROGRESS ? 0 : err;
			break;

		case OVS_ACTION_ATTR_CT_CLEAR:
			err = ovs_ct_clear(skb, key);
			break;

		case OVS_ACTION_ATTR_PUSH_ETH:
			err = push_eth(skb, key, nla_data(a));
			break;

		case OVS_ACTION_ATTR_POP_ETH:
			err = pop_eth(skb, key);
			break;

		case OVS_ACTION_ATTR_PUSH_NSH:
			err = push_nsh(skb, key, nla_data(a));
			break;

		case OVS_ACTION_ATTR_POP_NSH:
			err = pop_nsh(skb, key);
			break;

		case OVS_ACTION_ATTR_METER:
			if (ovs_meter_execute(dp, skb, key, nla_get_u32(a))) {
				ovs_kfree_skb_reason(skb, OVS_DROP_METER);
				return 0;
			}
			break;

		case OVS_ACTION_ATTR_CLONE: {
			bool last = nla_is_last(a, rem);

			err = clone(dp, skb, key, a, last);
			if (last)
				return err;

			break;
		}

		case OVS_ACTION_ATTR_CHECK_PKT_LEN: {
			bool last = nla_is_last(a, rem);

			err = execute_check_pkt_len(dp, skb, key, a, last);
			if (last)
				return err;

			break;
		}

		case OVS_ACTION_ATTR_DEC_TTL:
			err = execute_dec_ttl(skb, key);
			if (err == -EHOSTUNREACH)
				return dec_ttl_exception_handler(dp, skb,
								 key, a);
			break;

		case OVS_ACTION_ATTR_DROP: {
			enum ovs_drop_reason reason = nla_get_u32(a)
				? OVS_DROP_EXPLICIT_WITH_ERROR
				: OVS_DROP_EXPLICIT;

			ovs_kfree_skb_reason(skb, reason);
			return 0;
		}
		}

		if (unlikely(err)) {
			ovs_kfree_skb_reason(skb, OVS_DROP_ACTION_ERROR);
			return err;
		}
	}

	ovs_kfree_skb_reason(skb, OVS_DROP_LAST_ACTION);
	return 0;
}

/* Execute the actions on the clone of the packet. The effect of the
 * execution does not affect the original 'skb' nor the original 'key'.
 *
 * The execution may be deferred in case the actions can not be executed
 * immediately.
 */
static int clone_execute(struct datapath *dp, struct sk_buff *skb,
			 struct sw_flow_key *key, u32 recirc_id,
			 const struct nlattr *actions, int len,
			 bool last, bool clone_flow_key)
{
	struct deferred_action *da;
	struct sw_flow_key *clone;

	skb = last ? skb : skb_clone(skb, GFP_ATOMIC);
	if (!skb) {
		/* Out of memory, skip this action.
		 */
		return 0;
	}

	/* When clone_flow_key is false, the 'key' will not be change
	 * by the actions, then the 'key' can be used directly.
	 * Otherwise, try to clone key from the next recursion level of
	 * 'flow_keys'. If clone is successful, execute the actions
	 * without deferring.
	 */
	clone = clone_flow_key ? clone_key(key) : key;
	if (clone) {
		int err = 0;

		if (actions) { /* Sample action */
			if (clone_flow_key)
				__this_cpu_inc(exec_actions_level);

			err = do_execute_actions(dp, skb, clone,
						 actions, len);

			if (clone_flow_key)
				__this_cpu_dec(exec_actions_level);
		} else { /* Recirc action */
			clone->recirc_id = recirc_id;
			ovs_dp_process_packet(skb, clone);
		}
		return err;
	}

	/* Out of 'flow_keys' space. Defer actions */
	da = add_deferred_actions(skb, key, actions, len);
	if (da) {
		if (!actions) { /* Recirc action */
			key = &da->pkt_key;
			key->recirc_id = recirc_id;
		}
	} else {
		/* Out of per CPU action FIFO space. Drop the 'skb' and
		 * log an error.
		 */
		ovs_kfree_skb_reason(skb, OVS_DROP_DEFERRED_LIMIT);

		if (net_ratelimit()) {
			if (actions) { /* Sample action */
				pr_warn("%s: deferred action limit reached, drop sample action\n",
					ovs_dp_name(dp));
			} else {  /* Recirc action */
				pr_warn("%s: deferred action limit reached, drop recirc action (recirc_id=%#x)\n",
					ovs_dp_name(dp), recirc_id);
			}
		}
	}
	return 0;
}

static void process_deferred_actions(struct datapath *dp)
{
	struct action_fifo *fifo = this_cpu_ptr(action_fifos);

	/* Do not touch the FIFO in case there is no deferred actions. */
	if (action_fifo_is_empty(fifo))
		return;

	/* Finishing executing all deferred actions. */
	do {
		struct deferred_action *da = action_fifo_get(fifo);
		struct sk_buff *skb = da->skb;
		struct sw_flow_key *key = &da->pkt_key;
		const struct nlattr *actions = da->actions;
		int actions_len = da->actions_len;

		if (actions)
			do_execute_actions(dp, skb, key, actions, actions_len);
		else
			ovs_dp_process_packet(skb, key);
	} while (!action_fifo_is_empty(fifo));

	/* Reset FIFO for the next packet.  */
	action_fifo_init(fifo);
}

/* Execute a list of actions against 'skb'. */
int ovs_execute_actions(struct datapath *dp, struct sk_buff *skb,
			const struct sw_flow_actions *acts,
			struct sw_flow_key *key)
{
	int err, level;

	level = __this_cpu_inc_return(exec_actions_level);
	if (unlikely(level > OVS_RECURSION_LIMIT)) {
		net_crit_ratelimited("ovs: recursion limit reached on datapath %s, probable configuration error\n",
				     ovs_dp_name(dp));
		ovs_kfree_skb_reason(skb, OVS_DROP_RECURSION_LIMIT);
		err = -ENETDOWN;
		goto out;
	}

	OVS_CB(skb)->acts_origlen = acts->orig_len;
	err = do_execute_actions(dp, skb, key,
				 acts->actions, acts->actions_len);

	if (level == 1)
		process_deferred_actions(dp);

out:
	__this_cpu_dec(exec_actions_level);
	return err;
}

int action_fifos_init(void)
{
	action_fifos = alloc_percpu(struct action_fifo);
	if (!action_fifos)
		return -ENOMEM;

	flow_keys = alloc_percpu(struct action_flow_keys);
	if (!flow_keys) {
		free_percpu(action_fifos);
		return -ENOMEM;
	}

	return 0;
}

void action_fifos_exit(void)
{
	free_percpu(action_fifos);
	free_percpu(flow_keys);
}
