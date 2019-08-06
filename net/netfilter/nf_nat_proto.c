/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>

#include <linux/dccp.h>
#include <linux/sctp.h>
#include <net/sctp/checksum.h>

#include <linux/netfilter.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_l3proto.h>
#include <net/netfilter/nf_nat_l4proto.h>

static void
__udp_manip_pkt(struct sk_buff *skb,
	        const struct nf_nat_l3proto *l3proto,
	        unsigned int iphdroff, struct udphdr *hdr,
	        const struct nf_conntrack_tuple *tuple,
	        enum nf_nat_manip_type maniptype, bool do_csum)
{
	__be16 *portptr, newport;

	if (maniptype == NF_NAT_MANIP_SRC) {
		/* Get rid of src port */
		newport = tuple->src.u.udp.port;
		portptr = &hdr->source;
	} else {
		/* Get rid of dst port */
		newport = tuple->dst.u.udp.port;
		portptr = &hdr->dest;
	}
	if (do_csum) {
		l3proto->csum_update(skb, iphdroff, &hdr->check,
				     tuple, maniptype);
		inet_proto_csum_replace2(&hdr->check, skb, *portptr, newport,
					 false);
		if (!hdr->check)
			hdr->check = CSUM_MANGLED_0;
	}
	*portptr = newport;
}

static bool udp_manip_pkt(struct sk_buff *skb,
			  const struct nf_nat_l3proto *l3proto,
			  unsigned int iphdroff, unsigned int hdroff,
			  const struct nf_conntrack_tuple *tuple,
			  enum nf_nat_manip_type maniptype)
{
	struct udphdr *hdr;
	bool do_csum;

	if (!skb_make_writable(skb, hdroff + sizeof(*hdr)))
		return false;

	hdr = (struct udphdr *)(skb->data + hdroff);
	do_csum = hdr->check || skb->ip_summed == CHECKSUM_PARTIAL;

	__udp_manip_pkt(skb, l3proto, iphdroff, hdr, tuple, maniptype, do_csum);
	return true;
}

static bool udplite_manip_pkt(struct sk_buff *skb,
			      const struct nf_nat_l3proto *l3proto,
			      unsigned int iphdroff, unsigned int hdroff,
			      const struct nf_conntrack_tuple *tuple,
			      enum nf_nat_manip_type maniptype)
{
#ifdef CONFIG_NF_CT_PROTO_UDPLITE
	struct udphdr *hdr;

	if (!skb_make_writable(skb, hdroff + sizeof(*hdr)))
		return false;

	hdr = (struct udphdr *)(skb->data + hdroff);
	__udp_manip_pkt(skb, l3proto, iphdroff, hdr, tuple, maniptype, true);
#endif
	return true;
}

static bool
sctp_manip_pkt(struct sk_buff *skb,
	       const struct nf_nat_l3proto *l3proto,
	       unsigned int iphdroff, unsigned int hdroff,
	       const struct nf_conntrack_tuple *tuple,
	       enum nf_nat_manip_type maniptype)
{
#ifdef CONFIG_NF_CT_PROTO_SCTP
	struct sctphdr *hdr;
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

#endif
	return true;
}

static bool
tcp_manip_pkt(struct sk_buff *skb,
	      const struct nf_nat_l3proto *l3proto,
	      unsigned int iphdroff, unsigned int hdroff,
	      const struct nf_conntrack_tuple *tuple,
	      enum nf_nat_manip_type maniptype)
{
	struct tcphdr *hdr;
	__be16 *portptr, newport, oldport;
	int hdrsize = 8; /* TCP connection tracking guarantees this much */

	/* this could be a inner header returned in icmp packet; in such
	   cases we cannot update the checksum field since it is outside of
	   the 8 bytes of transport layer headers we are guaranteed */
	if (skb->len >= hdroff + sizeof(struct tcphdr))
		hdrsize = sizeof(struct tcphdr);

	if (!skb_make_writable(skb, hdroff + hdrsize))
		return false;

	hdr = (struct tcphdr *)(skb->data + hdroff);

	if (maniptype == NF_NAT_MANIP_SRC) {
		/* Get rid of src port */
		newport = tuple->src.u.tcp.port;
		portptr = &hdr->source;
	} else {
		/* Get rid of dst port */
		newport = tuple->dst.u.tcp.port;
		portptr = &hdr->dest;
	}

	oldport = *portptr;
	*portptr = newport;

	if (hdrsize < sizeof(*hdr))
		return true;

	l3proto->csum_update(skb, iphdroff, &hdr->check, tuple, maniptype);
	inet_proto_csum_replace2(&hdr->check, skb, oldport, newport, false);
	return true;
}

static bool
dccp_manip_pkt(struct sk_buff *skb,
	       const struct nf_nat_l3proto *l3proto,
	       unsigned int iphdroff, unsigned int hdroff,
	       const struct nf_conntrack_tuple *tuple,
	       enum nf_nat_manip_type maniptype)
{
#ifdef CONFIG_NF_CT_PROTO_DCCP
	struct dccp_hdr *hdr;
	__be16 *portptr, oldport, newport;
	int hdrsize = 8; /* DCCP connection tracking guarantees this much */

	if (skb->len >= hdroff + sizeof(struct dccp_hdr))
		hdrsize = sizeof(struct dccp_hdr);

	if (!skb_make_writable(skb, hdroff + hdrsize))
		return false;

	hdr = (struct dccp_hdr *)(skb->data + hdroff);

	if (maniptype == NF_NAT_MANIP_SRC) {
		newport = tuple->src.u.dccp.port;
		portptr = &hdr->dccph_sport;
	} else {
		newport = tuple->dst.u.dccp.port;
		portptr = &hdr->dccph_dport;
	}

	oldport = *portptr;
	*portptr = newport;

	if (hdrsize < sizeof(*hdr))
		return true;

	l3proto->csum_update(skb, iphdroff, &hdr->dccph_checksum,
			     tuple, maniptype);
	inet_proto_csum_replace2(&hdr->dccph_checksum, skb, oldport, newport,
				 false);
#endif
	return true;
}

static bool
icmp_manip_pkt(struct sk_buff *skb,
	       const struct nf_nat_l3proto *l3proto,
	       unsigned int iphdroff, unsigned int hdroff,
	       const struct nf_conntrack_tuple *tuple,
	       enum nf_nat_manip_type maniptype)
{
	struct icmphdr *hdr;

	if (!skb_make_writable(skb, hdroff + sizeof(*hdr)))
		return false;

	hdr = (struct icmphdr *)(skb->data + hdroff);
	inet_proto_csum_replace2(&hdr->checksum, skb,
				 hdr->un.echo.id, tuple->src.u.icmp.id, false);
	hdr->un.echo.id = tuple->src.u.icmp.id;
	return true;
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
					 tuple->src.u.icmp.id, false);
		hdr->icmp6_identifier = tuple->src.u.icmp.id;
	}
	return true;
}

/* manipulate a GRE packet according to maniptype */
static bool
gre_manip_pkt(struct sk_buff *skb,
	      const struct nf_nat_l3proto *l3proto,
	      unsigned int iphdroff, unsigned int hdroff,
	      const struct nf_conntrack_tuple *tuple,
	      enum nf_nat_manip_type maniptype)
{
#if IS_ENABLED(CONFIG_NF_CT_PROTO_GRE)
	const struct gre_base_hdr *greh;
	struct pptp_gre_header *pgreh;

	/* pgreh includes two optional 32bit fields which are not required
	 * to be there.  That's where the magic '8' comes from */
	if (!skb_make_writable(skb, hdroff + sizeof(*pgreh) - 8))
		return false;

	greh = (void *)skb->data + hdroff;
	pgreh = (struct pptp_gre_header *)greh;

	/* we only have destination manip of a packet, since 'source key'
	 * is not present in the packet itself */
	if (maniptype != NF_NAT_MANIP_DST)
		return true;

	switch (greh->flags & GRE_VERSION) {
	case GRE_VERSION_0:
		/* We do not currently NAT any GREv0 packets.
		 * Try to behave like "nf_nat_proto_unknown" */
		break;
	case GRE_VERSION_1:
		pr_debug("call_id -> 0x%04x\n", ntohs(tuple->dst.u.gre.key));
		pgreh->call_id = tuple->dst.u.gre.key;
		break;
	default:
		pr_debug("can't nat unknown GRE version\n");
		return false;
	}
#endif
	return true;
}

bool nf_nat_l4proto_manip_pkt(struct sk_buff *skb,
			      const struct nf_nat_l3proto *l3proto,
			      unsigned int iphdroff, unsigned int hdroff,
			      const struct nf_conntrack_tuple *tuple,
			      enum nf_nat_manip_type maniptype)
{
	switch (tuple->dst.protonum) {
	case IPPROTO_TCP:
		return tcp_manip_pkt(skb, l3proto, iphdroff, hdroff,
				     tuple, maniptype);
	case IPPROTO_UDP:
		return udp_manip_pkt(skb, l3proto, iphdroff, hdroff,
				     tuple, maniptype);
	case IPPROTO_UDPLITE:
		return udplite_manip_pkt(skb, l3proto, iphdroff, hdroff,
					 tuple, maniptype);
	case IPPROTO_SCTP:
		return sctp_manip_pkt(skb, l3proto, iphdroff, hdroff,
				      tuple, maniptype);
	case IPPROTO_ICMP:
		return icmp_manip_pkt(skb, l3proto, iphdroff, hdroff,
				      tuple, maniptype);
	case IPPROTO_ICMPV6:
		return icmpv6_manip_pkt(skb, l3proto, iphdroff, hdroff,
					tuple, maniptype);
	case IPPROTO_DCCP:
		return dccp_manip_pkt(skb, l3proto, iphdroff, hdroff,
				      tuple, maniptype);
	case IPPROTO_GRE:
		return gre_manip_pkt(skb, l3proto, iphdroff, hdroff,
				     tuple, maniptype);
	}

	/* If we don't know protocol -- no error, pass it unmodified. */
	return true;
}
EXPORT_SYMBOL_GPL(nf_nat_l4proto_manip_pkt);
