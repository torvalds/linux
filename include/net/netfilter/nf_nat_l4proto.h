/* SPDX-License-Identifier: GPL-2.0 */
/* Header for use in defining a given protocol. */
#ifndef _NF_NAT_L4PROTO_H
#define _NF_NAT_L4PROTO_H
#include <net/netfilter/nf_nat.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

struct nf_nat_l3proto;

/* Translate a packet to the target according to manip type.  Return on success. */
bool nf_nat_l4proto_manip_pkt(struct sk_buff *skb,
			      const struct nf_nat_l3proto *l3proto,
			      unsigned int iphdroff, unsigned int hdroff,
			      const struct nf_conntrack_tuple *tuple,
			      enum nf_nat_manip_type maniptype);
#endif /*_NF_NAT_L4PROTO_H*/
