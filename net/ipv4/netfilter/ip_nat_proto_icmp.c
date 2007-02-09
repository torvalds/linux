/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/if.h>

#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_core.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_nat_protocol.h>

static int
icmp_in_range(const struct ip_conntrack_tuple *tuple,
	      enum ip_nat_manip_type maniptype,
	      const union ip_conntrack_manip_proto *min,
	      const union ip_conntrack_manip_proto *max)
{
	return ntohs(tuple->src.u.icmp.id) >= ntohs(min->icmp.id) &&
	       ntohs(tuple->src.u.icmp.id) <= ntohs(max->icmp.id);
}

static int
icmp_unique_tuple(struct ip_conntrack_tuple *tuple,
		  const struct ip_nat_range *range,
		  enum ip_nat_manip_type maniptype,
		  const struct ip_conntrack *conntrack)
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
		if (!ip_nat_used_tuple(tuple, conntrack))
			return 1;
	}
	return 0;
}

static int
icmp_manip_pkt(struct sk_buff **pskb,
	       unsigned int iphdroff,
	       const struct ip_conntrack_tuple *tuple,
	       enum ip_nat_manip_type maniptype)
{
	struct iphdr *iph = (struct iphdr *)((*pskb)->data + iphdroff);
	struct icmphdr *hdr;
	unsigned int hdroff = iphdroff + iph->ihl*4;

	if (!skb_make_writable(pskb, hdroff + sizeof(*hdr)))
		return 0;

	hdr = (struct icmphdr *)((*pskb)->data + hdroff);
	nf_proto_csum_replace2(&hdr->checksum, *pskb,
			       hdr->un.echo.id, tuple->src.u.icmp.id, 0);
	hdr->un.echo.id = tuple->src.u.icmp.id;
	return 1;
}

struct ip_nat_protocol ip_nat_protocol_icmp = {
	.name			= "ICMP",
	.protonum		= IPPROTO_ICMP,
	.me			= THIS_MODULE,
	.manip_pkt		= icmp_manip_pkt,
	.in_range		= icmp_in_range,
	.unique_tuple		= icmp_unique_tuple,
#if defined(CONFIG_IP_NF_CONNTRACK_NETLINK) || \
    defined(CONFIG_IP_NF_CONNTRACK_NETLINK_MODULE)
	.range_to_nfattr	= ip_nat_port_range_to_nfattr,
	.nfattr_to_range	= ip_nat_port_nfattr_to_range,
#endif
};
