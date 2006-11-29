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
#include <linux/seq_file.h>
#include <net/netfilter/nf_conntrack.h>

struct nfattr;

struct nf_conntrack_l3proto
{
	/* L3 Protocol Family number. ex) PF_INET */
	u_int16_t l3proto;

	/* Protocol name */
	const char *name;

	/*
	 * Try to fill in the third arg: nhoff is offset of l3 proto
         * hdr.  Return true if possible.
	 */
	int (*pkt_to_tuple)(const struct sk_buff *skb, unsigned int nhoff,
			    struct nf_conntrack_tuple *tuple);

	/*
	 * Invert the per-proto part of the tuple: ie. turn xmit into reply.
	 * Some packets can't be inverted: return 0 in that case.
	 */
	int (*invert_tuple)(struct nf_conntrack_tuple *inverse,
			    const struct nf_conntrack_tuple *orig);

	/* Print out the per-protocol part of the tuple. */
	int (*print_tuple)(struct seq_file *s,
			   const struct nf_conntrack_tuple *);

	/* Print out the private part of the conntrack. */
	int (*print_conntrack)(struct seq_file *s, const struct nf_conn *);

	/* Returns verdict for packet, or -1 for invalid. */
	int (*packet)(struct nf_conn *conntrack,
		      const struct sk_buff *skb,
		      enum ip_conntrack_info ctinfo);

	/*
	 * Called when a new connection for this protocol found;
	 * returns TRUE if it's OK.  If so, packet() called next.
	 */
	int (*new)(struct nf_conn *conntrack, const struct sk_buff *skb);

	/* Called when a conntrack entry is destroyed */
	void (*destroy)(struct nf_conn *conntrack);

	/*
	 * Called before tracking. 
	 *	*dataoff: offset of protocol header (TCP, UDP,...) in *pskb
	 *	*protonum: protocol number
	 */
	int (*prepare)(struct sk_buff **pskb, unsigned int hooknum,
		       unsigned int *dataoff, u_int8_t *protonum);

	u_int32_t (*get_features)(const struct nf_conntrack_tuple *tuple);

	int (*tuple_to_nfattr)(struct sk_buff *skb,
			       const struct nf_conntrack_tuple *t);

	int (*nfattr_to_tuple)(struct nfattr *tb[],
			       struct nf_conntrack_tuple *t);

	/* Module (if any) which this is connected to. */
	struct module *me;
};

extern struct nf_conntrack_l3proto *nf_ct_l3protos[AF_MAX];

/* Protocol registration. */
extern int nf_conntrack_l3proto_register(struct nf_conntrack_l3proto *proto);
extern int nf_conntrack_l3proto_unregister(struct nf_conntrack_l3proto *proto);

extern struct nf_conntrack_l3proto *
nf_ct_l3proto_find_get(u_int16_t l3proto);

extern void nf_ct_l3proto_put(struct nf_conntrack_l3proto *p);

/* Existing built-in protocols */
extern struct nf_conntrack_l3proto nf_conntrack_l3proto_ipv4;
extern struct nf_conntrack_l3proto nf_conntrack_l3proto_ipv6;
extern struct nf_conntrack_l3proto nf_conntrack_l3proto_generic;

static inline struct nf_conntrack_l3proto *
__nf_ct_l3proto_find(u_int16_t l3proto)
{
	if (unlikely(l3proto >= AF_MAX))
		return &nf_conntrack_l3proto_generic;
	return nf_ct_l3protos[l3proto];
}

#endif /*_NF_CONNTRACK_L3PROTO_H*/
