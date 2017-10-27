/*
 * Copyright (C)2003,2004 USAGI/WIDE Project
 *
 * Header for use in defining a given L3 protocol for connection tracking.
 *
 * Author:
 *	Yasuyuki Kozakai @USAGI	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Derived from include/netfilter_ipv4/ip_conntrack_protocol.h
 */

#ifndef _NF_CONNTRACK_L3PROTO_H
#define _NF_CONNTRACK_L3PROTO_H
#include <linux/netlink.h>
#include <net/netlink.h>
#include <linux/seq_file.h>
#include <net/netfilter/nf_conntrack.h>

struct nf_conntrack_l3proto {
	/* L3 Protocol Family number. ex) PF_INET */
	u_int16_t l3proto;

	/* size of tuple nlattr, fills a hole */
	u16 nla_size;

	/*
	 * Try to fill in the third arg: nhoff is offset of l3 proto
         * hdr.  Return true if possible.
	 */
	bool (*pkt_to_tuple)(const struct sk_buff *skb, unsigned int nhoff,
			     struct nf_conntrack_tuple *tuple);

	/*
	 * Invert the per-proto part of the tuple: ie. turn xmit into reply.
	 * Some packets can't be inverted: return 0 in that case.
	 */
	bool (*invert_tuple)(struct nf_conntrack_tuple *inverse,
			     const struct nf_conntrack_tuple *orig);

	/*
	 * Called before tracking. 
	 *	*dataoff: offset of protocol header (TCP, UDP,...) in skb
	 *	*protonum: protocol number
	 */
	int (*get_l4proto)(const struct sk_buff *skb, unsigned int nhoff,
			   unsigned int *dataoff, u_int8_t *protonum);

#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	int (*tuple_to_nlattr)(struct sk_buff *skb,
			       const struct nf_conntrack_tuple *t);
	int (*nlattr_to_tuple)(struct nlattr *tb[],
			       struct nf_conntrack_tuple *t);
	const struct nla_policy *nla_policy;
#endif

	/* Called when netns wants to use connection tracking */
	int (*net_ns_get)(struct net *);
	void (*net_ns_put)(struct net *);

	/* Module (if any) which this is connected to. */
	struct module *me;
};

extern struct nf_conntrack_l3proto __rcu *nf_ct_l3protos[NFPROTO_NUMPROTO];

/* Protocol global registration. */
int nf_ct_l3proto_register(const struct nf_conntrack_l3proto *proto);
void nf_ct_l3proto_unregister(const struct nf_conntrack_l3proto *proto);

const struct nf_conntrack_l3proto *nf_ct_l3proto_find_get(u_int16_t l3proto);

/* Existing built-in protocols */
extern struct nf_conntrack_l3proto nf_conntrack_l3proto_generic;

static inline struct nf_conntrack_l3proto *
__nf_ct_l3proto_find(u_int16_t l3proto)
{
	if (unlikely(l3proto >= NFPROTO_NUMPROTO))
		return &nf_conntrack_l3proto_generic;
	return rcu_dereference(nf_ct_l3protos[l3proto]);
}

#endif /*_NF_CONNTRACK_L3PROTO_H*/
