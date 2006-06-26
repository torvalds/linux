/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/netfilter.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/seq_file.h>
#include <net/checksum.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>

unsigned int ip_ct_udp_timeout = 30*HZ;
unsigned int ip_ct_udp_timeout_stream = 180*HZ;

static int udp_pkt_to_tuple(const struct sk_buff *skb,
			     unsigned int dataoff,
			     struct ip_conntrack_tuple *tuple)
{
	struct udphdr _hdr, *hp;

	/* Actually only need first 8 bytes. */
	hp = skb_header_pointer(skb, dataoff, sizeof(_hdr), &_hdr);
	if (hp == NULL)
		return 0;

	tuple->src.u.udp.port = hp->source;
	tuple->dst.u.udp.port = hp->dest;

	return 1;
}

static int udp_invert_tuple(struct ip_conntrack_tuple *tuple,
			    const struct ip_conntrack_tuple *orig)
{
	tuple->src.u.udp.port = orig->dst.u.udp.port;
	tuple->dst.u.udp.port = orig->src.u.udp.port;
	return 1;
}

/* Print out the per-protocol part of the tuple. */
static int udp_print_tuple(struct seq_file *s,
			   const struct ip_conntrack_tuple *tuple)
{
	return seq_printf(s, "sport=%hu dport=%hu ",
			  ntohs(tuple->src.u.udp.port),
			  ntohs(tuple->dst.u.udp.port));
}

/* Print out the private part of the conntrack. */
static int udp_print_conntrack(struct seq_file *s,
			       const struct ip_conntrack *conntrack)
{
	return 0;
}

/* Returns verdict for packet, and may modify conntracktype */
static int udp_packet(struct ip_conntrack *conntrack,
		      const struct sk_buff *skb,
		      enum ip_conntrack_info ctinfo)
{
	/* If we've seen traffic both ways, this is some kind of UDP
	   stream.  Extend timeout. */
	if (test_bit(IPS_SEEN_REPLY_BIT, &conntrack->status)) {
		ip_ct_refresh_acct(conntrack, ctinfo, skb, 
				   ip_ct_udp_timeout_stream);
		/* Also, more likely to be important, and not a probe */
		if (!test_and_set_bit(IPS_ASSURED_BIT, &conntrack->status))
			ip_conntrack_event_cache(IPCT_STATUS, skb);
	} else
		ip_ct_refresh_acct(conntrack, ctinfo, skb, ip_ct_udp_timeout);

	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int udp_new(struct ip_conntrack *conntrack, const struct sk_buff *skb)
{
	return 1;
}

static int udp_error(struct sk_buff *skb, enum ip_conntrack_info *ctinfo,
		     unsigned int hooknum)
{
	struct iphdr *iph = skb->nh.iph;
	unsigned int udplen = skb->len - iph->ihl * 4;
	struct udphdr _hdr, *hdr;

	/* Header is too small? */
	hdr = skb_header_pointer(skb, iph->ihl*4, sizeof(_hdr), &_hdr);
	if (hdr == NULL) {
		if (LOG_INVALID(IPPROTO_UDP))
			nf_log_packet(PF_INET, 0, skb, NULL, NULL, NULL,
				  "ip_ct_udp: short packet ");
		return -NF_ACCEPT;
	}
	
	/* Truncated/malformed packets */
	if (ntohs(hdr->len) > udplen || ntohs(hdr->len) < sizeof(*hdr)) {
		if (LOG_INVALID(IPPROTO_UDP))
			nf_log_packet(PF_INET, 0, skb, NULL, NULL, NULL,
				  "ip_ct_udp: truncated/malformed packet ");
		return -NF_ACCEPT;
	}
	
	/* Packet with no checksum */
	if (!hdr->check)
		return NF_ACCEPT;

	/* Checksum invalid? Ignore.
	 * We skip checking packets on the outgoing path
	 * because the semantic of CHECKSUM_HW is different there 
	 * and moreover root might send raw packets.
	 * FIXME: Source route IP option packets --RR */
	if (ip_conntrack_checksum && hooknum == NF_IP_PRE_ROUTING &&
	    nf_ip_checksum(skb, hooknum, iph->ihl * 4, IPPROTO_UDP)) {
		if (LOG_INVALID(IPPROTO_UDP))
			nf_log_packet(PF_INET, 0, skb, NULL, NULL, NULL,
				  "ip_ct_udp: bad UDP checksum ");
		return -NF_ACCEPT;
	}
	
	return NF_ACCEPT;
}

struct ip_conntrack_protocol ip_conntrack_protocol_udp =
{
	.proto 			= IPPROTO_UDP,
	.name			= "udp",
	.pkt_to_tuple		= udp_pkt_to_tuple,
	.invert_tuple		= udp_invert_tuple,
	.print_tuple		= udp_print_tuple,
	.print_conntrack	= udp_print_conntrack,
	.packet			= udp_packet,
	.new			= udp_new,
	.error			= udp_error,
#if defined(CONFIG_IP_NF_CONNTRACK_NETLINK) || \
    defined(CONFIG_IP_NF_CONNTRACK_NETLINK_MODULE)
	.tuple_to_nfattr	= ip_ct_port_tuple_to_nfattr,
	.nfattr_to_tuple	= ip_ct_port_nfattr_to_tuple,
#endif
};
