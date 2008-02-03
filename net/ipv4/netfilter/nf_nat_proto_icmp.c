/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/icmp.h>

#include <linux/netfilter.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_rule.h>
#include <net/netfilter/nf_nat_protocol.h>

static int
icmp_in_range(const struct nf_conntrack_tuple *tuple,
	      enum nf_nat_manip_type maniptype,
	      const union nf_conntrack_man_proto *min,
	      const union nf_conntrack_man_proto *max)
{
	return ntohs(tuple->src.u.icmp.id) >= ntohs(min->icmp.id) &&
	       ntohs(tuple->src.u.icmp.id) <= ntohs(max->icmp.id);
}

static int
icmp_unique_tuple(struct nf_conntrack_tuple *tuple,
		  const struct nf_nat_range *range,
		  enum nf_nat_manip_type maniptype,
		  const struct nf_conn *ct)
{
	static u_int16_t id;
	unsigned int range_size;
	unsigned int i;

	range_size = ntohs(range->max.icmp.id) - ntohs(range->min.icmp.id) + 1;
	/* If no range specified... */
	if (!(range->flags & IP_NAT_RANGE_PROTO_SPECIFIED))
		range_size = 0xFFFF;

	for (i = 0; i < range_size; i++, id++) {
		tuple->src.u.icmp.id = htons(ntohs(range->min.icmp.id) +
					     (id % range_size));
		if (!nf_nat_used_tuple(tuple, ct))
			return 1;
	}
	return 0;
}

static int
icmp_manip_pkt(struct sk_buff *skb,
	       unsigned int iphdroff,
	       const struct nf_conntrack_tuple *tuple,
	       enum nf_nat_manip_type maniptype)
{
	const struct iphdr *iph = (struct iphdr *)(skb->data + iphdroff);
	struct icmphdr *hdr;
	unsigned int hdroff = iphdroff + iph->ihl*4;

	if (!skb_make_writable(skb, hdroff + sizeof(*hdr)))
		return 0;

	hdr = (struct icmphdr *)(skb->data + hdroff);
	inet_proto_csum_replace2(&hdr->checksum, skb,
				 hdr->un.echo.id, tuple->src.u.icmp.id, 0);
	hdr->un.echo.id = tuple->src.u.icmp.id;
	return 1;
}

const struct nf_nat_protocol nf_nat_protocol_icmp = {
	.name			= "ICMP",
	.protonum		= IPPROTO_ICMP,
	.me			= THIS_MODULE,
	.manip_pkt		= icmp_manip_pkt,
	.in_range		= icmp_in_range,
	.unique_tuple		= icmp_unique_tuple,
#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
	.range_to_nlattr	= nf_nat_port_range_to_nlattr,
	.nlattr_to_range	= nf_nat_port_nlattr_to_range,
#endif
};
