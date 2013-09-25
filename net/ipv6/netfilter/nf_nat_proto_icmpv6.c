/*
 * Copyright (c) 2011 Patrick Mchardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on Rusty Russell's IPv4 ICMP NAT code. Development of IPv6
 * NAT funded by Astaro.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/icmpv6.h>

#include <linux/netfilter.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_l3proto.h>
#include <net/netfilter/nf_nat_l4proto.h>

static bool
icmpv6_in_range(const struct nf_conntrack_tuple *tuple,
		enum nf_nat_manip_type maniptype,
		const union nf_conntrack_man_proto *min,
		const union nf_conntrack_man_proto *max)
{
	return ntohs(tuple->src.u.icmp.id) >= ntohs(min->icmp.id) &&
	       ntohs(tuple->src.u.icmp.id) <= ntohs(max->icmp.id);
}

static void
icmpv6_unique_tuple(const struct nf_nat_l3proto *l3proto,
		    struct nf_conntrack_tuple *tuple,
		    const struct nf_nat_range *range,
		    enum nf_nat_manip_type maniptype,
		    const struct nf_conn *ct)
{
	static u16 id;
	unsigned int range_size;
	unsigned int i;

	range_size = ntohs(range->max_proto.icmp.id) -
		     ntohs(range->min_proto.icmp.id) + 1;

	if (!(range->flags & NF_NAT_RANGE_PROTO_SPECIFIED))
		range_size = 0xffff;

	for (i = 0; ; ++id) {
		tuple->src.u.icmp.id = htons(ntohs(range->min_proto.icmp.id) +
					     (id % range_size));
		if (++i == range_size || !nf_nat_used_tuple(tuple, ct))
			return;
	}
}

static bool
icmpv6_manip_pkt(struct sk_buff *skb,
		 const struct nf_nat_l3proto *l3proto,
		 unsigned int iphdroff, unsigned int hdroff,
		 const struct nf_conntrack_tuple *tuple,
		 enum nf_nat_manip_type maniptype)
{
	struct icmp6hdr *hdr;

	if (!skb_make_writable(skb, hdroff + sizeof(*hdr)))
		return false;

	hdr = (struct icmp6hdr *)(skb->data + hdroff);
	l3proto->csum_update(skb, iphdroff, &hdr->icmp6_cksum,
			     tuple, maniptype);
	if (hdr->icmp6_type == ICMPV6_ECHO_REQUEST ||
	    hdr->icmp6_type == ICMPV6_ECHO_REPLY) {
		inet_proto_csum_replace2(&hdr->icmp6_cksum, skb,
					 hdr->icmp6_identifier,
					 tuple->src.u.icmp.id, 0);
		hdr->icmp6_identifier = tuple->src.u.icmp.id;
	}
	return true;
}

const struct nf_nat_l4proto nf_nat_l4proto_icmpv6 = {
	.l4proto		= IPPROTO_ICMPV6,
	.manip_pkt		= icmpv6_manip_pkt,
	.in_range		= icmpv6_in_range,
	.unique_tuple		= icmpv6_unique_tuple,
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.nlattr_to_range	= nf_nat_l4proto_nlattr_to_range,
#endif
};
