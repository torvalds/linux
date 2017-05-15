/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/sctp.h>
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
	sctp_sctphdr_t *hdr;
	int hdrsize = 8;

	/* This could be an inner header returned in imcp packet; in such
	 * cases we cannot update the checksum field since it is outside
	 * of the 8 bytes of transport layer headers we are guaranteed.
	 */
	if (skb->len >= hdroff + sizeof(*hdr))
		hdrsize = sizeof(*hdr);

	if (!skb_make_writable(skb, hdroff + hdrsize))
		return false;

	hdr = (struct sctphdr *)(skb->data + hdroff);

	if (maniptype == NF_NAT_MANIP_SRC) {
		/* Get rid of src port */
		hdr->source = tuple->src.u.sctp.port;
	} else {
		/* Get rid of dst port */
		hdr->dest = tuple->dst.u.sctp.port;
	}

	if (hdrsize < sizeof(*hdr))
		return true;

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		hdr->checksum = sctp_compute_cksum(skb, hdroff);
		skb->ip_summed = CHECKSUM_NONE;
	}

	return true;
}

const struct nf_nat_l4proto nf_nat_l4proto_sctp = {
	.l4proto		= IPPROTO_SCTP,
	.manip_pkt		= sctp_manip_pkt,
	.in_range		= nf_nat_l4proto_in_range,
	.unique_tuple		= sctp_unique_tuple,
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.nlattr_to_range	= nf_nat_l4proto_nlattr_to_range,
#endif
};
