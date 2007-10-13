/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/ip.h>
#include <linux/tcp.h>

#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_rule.h>
#include <net/netfilter/nf_nat_protocol.h>
#include <net/netfilter/nf_nat_core.h>

static int
tcp_in_range(const struct nf_conntrack_tuple *tuple,
	     enum nf_nat_manip_type maniptype,
	     const union nf_conntrack_man_proto *min,
	     const union nf_conntrack_man_proto *max)
{
	__be16 port;

	if (maniptype == IP_NAT_MANIP_SRC)
		port = tuple->src.u.tcp.port;
	else
		port = tuple->dst.u.tcp.port;

	return ntohs(port) >= ntohs(min->tcp.port) &&
	       ntohs(port) <= ntohs(max->tcp.port);
}

static int
tcp_unique_tuple(struct nf_conntrack_tuple *tuple,
		 const struct nf_nat_range *range,
		 enum nf_nat_manip_type maniptype,
		 const struct nf_conn *ct)
{
	static u_int16_t port;
	__be16 *portptr;
	unsigned int range_size, min, i;

	if (maniptype == IP_NAT_MANIP_SRC)
		portptr = &tuple->src.u.tcp.port;
	else
		portptr = &tuple->dst.u.tcp.port;

	/* If no range specified... */
	if (!(range->flags & IP_NAT_RANGE_PROTO_SPECIFIED)) {
		/* If it's dst rewrite, can't change port */
		if (maniptype == IP_NAT_MANIP_DST)
			return 0;

		/* Map privileged onto privileged. */
		if (ntohs(*portptr) < 1024) {
			/* Loose convention: >> 512 is credential passing */
			if (ntohs(*portptr)<512) {
				min = 1;
				range_size = 511 - min + 1;
			} else {
				min = 600;
				range_size = 1023 - min + 1;
			}
		} else {
			min = 1024;
			range_size = 65535 - 1024 + 1;
		}
	} else {
		min = ntohs(range->min.tcp.port);
		range_size = ntohs(range->max.tcp.port) - min + 1;
	}

	if (range->flags & IP_NAT_RANGE_PROTO_RANDOM)
		port =  net_random();

	for (i = 0; i < range_size; i++, port++) {
		*portptr = htons(min + port % range_size);
		if (!nf_nat_used_tuple(tuple, ct))
			return 1;
	}
	return 0;
}

static int
tcp_manip_pkt(struct sk_buff **pskb,
	      unsigned int iphdroff,
	      const struct nf_conntrack_tuple *tuple,
	      enum nf_nat_manip_type maniptype)
{
	struct iphdr *iph = (struct iphdr *)((*pskb)->data + iphdroff);
	struct tcphdr *hdr;
	unsigned int hdroff = iphdroff + iph->ihl*4;
	__be32 oldip, newip;
	__be16 *portptr, newport, oldport;
	int hdrsize = 8; /* TCP connection tracking guarantees this much */

	/* this could be a inner header returned in icmp packet; in such
	   cases we cannot update the checksum field since it is outside of
	   the 8 bytes of transport layer headers we are guaranteed */
	if ((*pskb)->len >= hdroff + sizeof(struct tcphdr))
		hdrsize = sizeof(struct tcphdr);

	if (!skb_make_writable(pskb, hdroff + hdrsize))
		return 0;

	iph = (struct iphdr *)((*pskb)->data + iphdroff);
	hdr = (struct tcphdr *)((*pskb)->data + hdroff);

	if (maniptype == IP_NAT_MANIP_SRC) {
		/* Get rid of src ip and src pt */
		oldip = iph->saddr;
		newip = tuple->src.u3.ip;
		newport = tuple->src.u.tcp.port;
		portptr = &hdr->source;
	} else {
		/* Get rid of dst ip and dst pt */
		oldip = iph->daddr;
		newip = tuple->dst.u3.ip;
		newport = tuple->dst.u.tcp.port;
		portptr = &hdr->dest;
	}

	oldport = *portptr;
	*portptr = newport;

	if (hdrsize < sizeof(*hdr))
		return 1;

	nf_proto_csum_replace4(&hdr->check, *pskb, oldip, newip, 1);
	nf_proto_csum_replace2(&hdr->check, *pskb, oldport, newport, 0);
	return 1;
}

struct nf_nat_protocol nf_nat_protocol_tcp = {
	.name			= "TCP",
	.protonum		= IPPROTO_TCP,
	.me			= THIS_MODULE,
	.manip_pkt		= tcp_manip_pkt,
	.in_range		= tcp_in_range,
	.unique_tuple		= tcp_unique_tuple,
#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
	.range_to_nlattr	= nf_nat_port_range_to_nlattr,
	.nlattr_to_range	= nf_nat_port_nlattr_to_range,
#endif
};
