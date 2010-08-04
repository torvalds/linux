/*
 * DCCP NAT protocol helper
 *
 * Copyright (c) 2005, 2006. 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/dccp.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_protocol.h>

static u_int16_t dccp_port_rover;

static void
dccp_unique_tuple(struct nf_conntrack_tuple *tuple,
		  const struct nf_nat_range *range,
		  enum nf_nat_manip_type maniptype,
		  const struct nf_conn *ct)
{
	nf_nat_proto_unique_tuple(tuple, range, maniptype, ct,
				  &dccp_port_rover);
}

static bool
dccp_manip_pkt(struct sk_buff *skb,
	       unsigned int iphdroff,
	       const struct nf_conntrack_tuple *tuple,
	       enum nf_nat_manip_type maniptype)
{
	const struct iphdr *iph = (const void *)(skb->data + iphdroff);
	struct dccp_hdr *hdr;
	unsigned int hdroff = iphdroff + iph->ihl * 4;
	__be32 oldip, newip;
	__be16 *portptr, oldport, newport;
	int hdrsize = 8; /* DCCP connection tracking guarantees this much */

	if (skb->len >= hdroff + sizeof(struct dccp_hdr))
		hdrsize = sizeof(struct dccp_hdr);

	if (!skb_make_writable(skb, hdroff + hdrsize))
		return false;

	iph = (struct iphdr *)(skb->data + iphdroff);
	hdr = (struct dccp_hdr *)(skb->data + hdroff);

	if (maniptype == IP_NAT_MANIP_SRC) {
		oldip = iph->saddr;
		newip = tuple->src.u3.ip;
		newport = tuple->src.u.dccp.port;
		portptr = &hdr->dccph_sport;
	} else {
		oldip = iph->daddr;
		newip = tuple->dst.u3.ip;
		newport = tuple->dst.u.dccp.port;
		portptr = &hdr->dccph_dport;
	}

	oldport = *portptr;
	*portptr = newport;

	if (hdrsize < sizeof(*hdr))
		return true;

	inet_proto_csum_replace4(&hdr->dccph_checksum, skb, oldip, newip, 1);
	inet_proto_csum_replace2(&hdr->dccph_checksum, skb, oldport, newport,
				 0);
	return true;
}

static const struct nf_nat_protocol nf_nat_protocol_dccp = {
	.protonum		= IPPROTO_DCCP,
	.me			= THIS_MODULE,
	.manip_pkt		= dccp_manip_pkt,
	.in_range		= nf_nat_proto_in_range,
	.unique_tuple		= dccp_unique_tuple,
#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
	.range_to_nlattr	= nf_nat_proto_range_to_nlattr,
	.nlattr_to_range	= nf_nat_proto_nlattr_to_range,
#endif
};

static int __init nf_nat_proto_dccp_init(void)
{
	return nf_nat_protocol_register(&nf_nat_protocol_dccp);
}

static void __exit nf_nat_proto_dccp_fini(void)
{
	nf_nat_protocol_unregister(&nf_nat_protocol_dccp);
}

module_init(nf_nat_proto_dccp_init);
module_exit(nf_nat_proto_dccp_fini);

MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("DCCP NAT protocol helper");
MODULE_LICENSE("GPL");
