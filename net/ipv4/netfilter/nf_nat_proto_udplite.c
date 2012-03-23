/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include <linux/netfilter.h>
#include <linux/module.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_protocol.h>

static u_int16_t udplite_port_rover;

static void
udplite_unique_tuple(struct nf_conntrack_tuple *tuple,
		     const struct nf_nat_ipv4_range *range,
		     enum nf_nat_manip_type maniptype,
		     const struct nf_conn *ct)
{
	nf_nat_proto_unique_tuple(tuple, range, maniptype, ct,
				  &udplite_port_rover);
}

static bool
udplite_manip_pkt(struct sk_buff *skb,
		  unsigned int iphdroff,
		  const struct nf_conntrack_tuple *tuple,
		  enum nf_nat_manip_type maniptype)
{
	const struct iphdr *iph = (struct iphdr *)(skb->data + iphdroff);
	struct udphdr *hdr;
	unsigned int hdroff = iphdroff + iph->ihl*4;
	__be32 oldip, newip;
	__be16 *portptr, newport;

	if (!skb_make_writable(skb, hdroff + sizeof(*hdr)))
		return false;

	iph = (struct iphdr *)(skb->data + iphdroff);
	hdr = (struct udphdr *)(skb->data + hdroff);

	if (maniptype == NF_NAT_MANIP_SRC) {
		/* Get rid of src ip and src pt */
		oldip = iph->saddr;
		newip = tuple->src.u3.ip;
		newport = tuple->src.u.udp.port;
		portptr = &hdr->source;
	} else {
		/* Get rid of dst ip and dst pt */
		oldip = iph->daddr;
		newip = tuple->dst.u3.ip;
		newport = tuple->dst.u.udp.port;
		portptr = &hdr->dest;
	}

	inet_proto_csum_replace4(&hdr->check, skb, oldip, newip, 1);
	inet_proto_csum_replace2(&hdr->check, skb, *portptr, newport, 0);
	if (!hdr->check)
		hdr->check = CSUM_MANGLED_0;

	*portptr = newport;
	return true;
}

static const struct nf_nat_protocol nf_nat_protocol_udplite = {
	.protonum		= IPPROTO_UDPLITE,
	.manip_pkt		= udplite_manip_pkt,
	.in_range		= nf_nat_proto_in_range,
	.unique_tuple		= udplite_unique_tuple,
#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
	.nlattr_to_range	= nf_nat_proto_nlattr_to_range,
#endif
};

static int __init nf_nat_proto_udplite_init(void)
{
	return nf_nat_protocol_register(&nf_nat_protocol_udplite);
}

static void __exit nf_nat_proto_udplite_fini(void)
{
	nf_nat_protocol_unregister(&nf_nat_protocol_udplite);
}

module_init(nf_nat_proto_udplite_init);
module_exit(nf_nat_proto_udplite_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("UDP-Lite NAT protocol helper");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
