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
	return (tuple->src.u.icmp.id >= min->icmp.id
		&& tuple->src.u.icmp.id <= max->icmp.id);
}

static int
icmp_unique_tuple(struct ip_conntrack_tuple *tuple,
		  const struct ip_nat_range *range,
		  enum ip_nat_manip_type maniptype,
		  const struct ip_conntrack *conntrack)
{
	static u_int16_t id;
	unsigned int range_size
		= (unsigned int)range->max.icmp.id - range->min.icmp.id + 1;
	unsigned int i;

	/* If no range specified... */
	if (!(range->flags & IP_NAT_RANGE_PROTO_SPECIFIED))
		range_size = 0xFFFF;

	for (i = 0; i < range_size; i++, id++) {
		tuple->src.u.icmp.id = range->min.icmp.id + (id % range_size);
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

	if (!skb_ip_make_writable(pskb, hdroff + sizeof(*hdr)))
		return 0;

	hdr = (struct icmphdr *)((*pskb)->data + hdroff);

	hdr->checksum = ip_nat_cheat_check(hdr->un.echo.id ^ 0xFFFF,
					    tuple->src.u.icmp.id,
					    hdr->checksum);
	hdr->un.echo.id = tuple->src.u.icmp.id;
	return 1;
}

static unsigned int
icmp_print(char *buffer,
	   const struct ip_conntrack_tuple *match,
	   const struct ip_conntrack_tuple *mask)
{
	unsigned int len = 0;

	if (mask->src.u.icmp.id)
		len += sprintf(buffer + len, "id=%u ",
			       ntohs(match->src.u.icmp.id));

	if (mask->dst.u.icmp.type)
		len += sprintf(buffer + len, "type=%u ",
			       ntohs(match->dst.u.icmp.type));

	if (mask->dst.u.icmp.code)
		len += sprintf(buffer + len, "code=%u ",
			       ntohs(match->dst.u.icmp.code));

	return len;
}

static unsigned int
icmp_print_range(char *buffer, const struct ip_nat_range *range)
{
	if (range->min.icmp.id != 0 || range->max.icmp.id != 0xFFFF)
		return sprintf(buffer, "id %u-%u ",
			       ntohs(range->min.icmp.id),
			       ntohs(range->max.icmp.id));
	else return 0;
}

struct ip_nat_protocol ip_nat_protocol_icmp
= { "ICMP", IPPROTO_ICMP,
    icmp_manip_pkt,
    icmp_in_range,
    icmp_unique_tuple,
    icmp_print,
    icmp_print_range
};
