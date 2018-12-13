/* SPDX-License-Identifier: GPL-2.0 */
/* Header for use in defining a given protocol. */
#ifndef _NF_NAT_L4PROTO_H
#define _NF_NAT_L4PROTO_H
#include <net/netfilter/nf_nat.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

struct nf_nat_range;
struct nf_nat_l3proto;

struct nf_nat_l4proto {
	/* Protocol number. */
	u8 l4proto;

	/* Translate a packet to the target according to manip type.
	 * Return true if succeeded.
	 */
	bool (*manip_pkt)(struct sk_buff *skb,
			  const struct nf_nat_l3proto *l3proto,
			  unsigned int iphdroff, unsigned int hdroff,
			  const struct nf_conntrack_tuple *tuple,
			  enum nf_nat_manip_type maniptype);
};

/* Protocol registration. */
int nf_nat_l4proto_register(u8 l3proto, const struct nf_nat_l4proto *l4proto);
void nf_nat_l4proto_unregister(u8 l3proto,
			       const struct nf_nat_l4proto *l4proto);

const struct nf_nat_l4proto *__nf_nat_l4proto_find(u8 l3proto, u8 l4proto);

/* Built-in protocols. */
extern const struct nf_nat_l4proto nf_nat_l4proto_tcp;
extern const struct nf_nat_l4proto nf_nat_l4proto_udp;
extern const struct nf_nat_l4proto nf_nat_l4proto_icmp;
extern const struct nf_nat_l4proto nf_nat_l4proto_icmpv6;
extern const struct nf_nat_l4proto nf_nat_l4proto_unknown;
#ifdef CONFIG_NF_NAT_PROTO_DCCP
extern const struct nf_nat_l4proto nf_nat_l4proto_dccp;
#endif
#ifdef CONFIG_NF_NAT_PROTO_SCTP
extern const struct nf_nat_l4proto nf_nat_l4proto_sctp;
#endif
#ifdef CONFIG_NF_NAT_PROTO_UDPLITE
extern const struct nf_nat_l4proto nf_nat_l4proto_udplite;
#endif

#endif /*_NF_NAT_L4PROTO_H*/
