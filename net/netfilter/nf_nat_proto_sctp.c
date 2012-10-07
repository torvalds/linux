/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/sctp.h>
#include <linux/module.h>
#include <net/sctp/checksum.h>

#include <net/netfilter/nf_nat_l4proto.h>

static u_int16_t nf_sctp_port_rover;

static void
sctp_unique_tuple(const struct nf_nat_l3proto *l3proto,
		  struct nf_conntrack_tuple *tuple,
		  const struct nf_nat_range *range,
		  enum nf_nat_manip_type maniptype,
		  const struct nf_conn *ct)
{
	nf_nat_l4proto_unique_tuple(l3proto, tuple, range, maniptype, ct,
				    &nf_sctp_port_rover);
}

static bool
sctp_manip_pkt(struct sk_buff *skb,
	       const struct nf_nat_l3proto *l3proto,
	       unsigned int iphdroff, unsigned int hdroff,
	       const struct nf_conntrack_tuple *tuple,
	       enum nf_nat_manip_type maniptype)
{
	struct sk_buff *frag;
	sctp_sctphdr_t *hdr;
	__be32 crc32;

	if (!skb_make_writable(skb, hdroff + sizeof(*hdr)))
		return false;

	hdr = (struct sctphdr *)(skb->data + hdroff);

	if (maniptype == NF_NAT_MANIP_SRC) {
		/* Get rid of src port */
		hdr->source = tuple->src.u.sctp.port;
	} else {
		/* Get rid of dst port */
		hdr->dest = tuple->dst.u.sctp.port;
	}

	crc32 = sctp_start_cksum((u8 *)hdr, skb_headlen(skb) - hdroff);
	skb_walk_frags(skb, frag)
		crc32 = sctp_update_cksum((u8 *)frag->data, skb_headlen(frag),
					  crc32);
	crc32 = sctp_end_cksum(crc32);
	hdr->checksum = crc32;

	return true;
}

static const struct nf_nat_l4proto nf_nat_l4proto_sctp = {
	.l4proto		= IPPROTO_SCTP,
	.manip_pkt		= sctp_manip_pkt,
	.in_range		= nf_nat_l4proto_in_range,
	.unique_tuple		= sctp_unique_tuple,
#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
	.nlattr_to_range	= nf_nat_l4proto_nlattr_to_range,
#endif
};

static int __init nf_nat_proto_sctp_init(void)
{
	int err;

	err = nf_nat_l4proto_register(NFPROTO_IPV4, &nf_nat_l4proto_sctp);
	if (err < 0)
		goto err1;
	err = nf_nat_l4proto_register(NFPROTO_IPV6, &nf_nat_l4proto_sctp);
	if (err < 0)
		goto err2;
	return 0;

err2:
	nf_nat_l4proto_unregister(NFPROTO_IPV4, &nf_nat_l4proto_sctp);
err1:
	return err;
}

static void __exit nf_nat_proto_sctp_exit(void)
{
	nf_nat_l4proto_unregister(NFPROTO_IPV6, &nf_nat_l4proto_sctp);
	nf_nat_l4proto_unregister(NFPROTO_IPV4, &nf_nat_l4proto_sctp);
}

module_init(nf_nat_proto_sctp_init);
module_exit(nf_nat_proto_sctp_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SCTP NAT protocol helper");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
