/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2007 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/udp.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/ipv6.h>
#include <net/ip6_checksum.h>
#include <net/checksum.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_ecache.h>

static unsigned int nf_ct_udplite_timeout __read_mostly = 30*HZ;
static unsigned int nf_ct_udplite_timeout_stream __read_mostly = 180*HZ;

static int udplite_pkt_to_tuple(const struct sk_buff *skb,
				unsigned int dataoff,
				struct nf_conntrack_tuple *tuple)
{
	struct udphdr _hdr, *hp;

	hp = skb_header_pointer(skb, dataoff, sizeof(_hdr), &_hdr);
	if (hp == NULL)
		return 0;

	tuple->src.u.udp.port = hp->source;
	tuple->dst.u.udp.port = hp->dest;
	return 1;
}

static int udplite_invert_tuple(struct nf_conntrack_tuple *tuple,
				const struct nf_conntrack_tuple *orig)
{
	tuple->src.u.udp.port = orig->dst.u.udp.port;
	tuple->dst.u.udp.port = orig->src.u.udp.port;
	return 1;
}

/* Print out the per-protocol part of the tuple. */
static int udplite_print_tuple(struct seq_file *s,
			       const struct nf_conntrack_tuple *tuple)
{
	return seq_printf(s, "sport=%hu dport=%hu ",
			  ntohs(tuple->src.u.udp.port),
			  ntohs(tuple->dst.u.udp.port));
}

/* Print out the private part of the conntrack. */
static int udplite_print_conntrack(struct seq_file *s,
				   const struct nf_conn *conntrack)
{
	return 0;
}

/* Returns verdict for packet, and may modify conntracktype */
static int udplite_packet(struct nf_conn *conntrack,
			  const struct sk_buff *skb,
			  unsigned int dataoff,
			  enum ip_conntrack_info ctinfo,
			  int pf,
			  unsigned int hooknum)
{
	/* If we've seen traffic both ways, this is some kind of UDP
	   stream.  Extend timeout. */
	if (test_bit(IPS_SEEN_REPLY_BIT, &conntrack->status)) {
		nf_ct_refresh_acct(conntrack, ctinfo, skb,
				   nf_ct_udplite_timeout_stream);
		/* Also, more likely to be important, and not a probe */
		if (!test_and_set_bit(IPS_ASSURED_BIT, &conntrack->status))
			nf_conntrack_event_cache(IPCT_STATUS, skb);
	} else
		nf_ct_refresh_acct(conntrack, ctinfo, skb,
				   nf_ct_udplite_timeout);

	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int udplite_new(struct nf_conn *conntrack, const struct sk_buff *skb,
		       unsigned int dataoff)
{
	return 1;
}

static int udplite_error(struct sk_buff *skb, unsigned int dataoff,
			 enum ip_conntrack_info *ctinfo,
			 int pf,
			 unsigned int hooknum)
{
	unsigned int udplen = skb->len - dataoff;
	struct udphdr _hdr, *hdr;
	unsigned int cscov;

	/* Header is too small? */
	hdr = skb_header_pointer(skb, dataoff, sizeof(_hdr), &_hdr);
	if (hdr == NULL) {
		if (LOG_INVALID(IPPROTO_UDPLITE))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
				      "nf_ct_udplite: short packet ");
		return -NF_ACCEPT;
	}

	cscov = ntohs(hdr->len);
	if (cscov == 0)
		cscov = udplen;
	else if (cscov < sizeof(*hdr) || cscov > udplen) {
		if (LOG_INVALID(IPPROTO_UDPLITE))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
				"nf_ct_udplite: invalid checksum coverage ");
		return -NF_ACCEPT;
	}

	/* UDPLITE mandates checksums */
	if (!hdr->check) {
		if (LOG_INVALID(IPPROTO_UDPLITE))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
				      "nf_ct_udplite: checksum missing ");
		return -NF_ACCEPT;
	}

	/* Checksum invalid? Ignore. */
	if (nf_conntrack_checksum && !skb_csum_unnecessary(skb) &&
	    ((pf == PF_INET && hooknum == NF_IP_PRE_ROUTING) ||
	     (pf == PF_INET6 && hooknum == NF_IP6_PRE_ROUTING))) {
		if (pf == PF_INET) {
			struct iphdr *iph = ip_hdr(skb);

			skb->csum = csum_tcpudp_nofold(iph->saddr, iph->daddr,
						       udplen, IPPROTO_UDPLITE, 0);
		} else {
			struct ipv6hdr *ipv6h = ipv6_hdr(skb);
			__wsum hsum = skb_checksum(skb, 0, dataoff, 0);

			skb->csum = ~csum_unfold(
				csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr,
						udplen, IPPROTO_UDPLITE,
						csum_sub(0, hsum)));
		}

		skb->ip_summed = CHECKSUM_NONE;
		if (__skb_checksum_complete_head(skb, dataoff + cscov)) {
			if (LOG_INVALID(IPPROTO_UDPLITE))
				nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
					      "nf_ct_udplite: bad UDPLite "
					      "checksum ");
			return -NF_ACCEPT;
		}
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	return NF_ACCEPT;
}

#ifdef CONFIG_SYSCTL
static unsigned int udplite_sysctl_table_users;
static struct ctl_table_header *udplite_sysctl_header;
static struct ctl_table udplite_sysctl_table[] = {
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "nf_conntrack_udplite_timeout",
		.data		= &nf_ct_udplite_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "nf_conntrack_udplite_timeout_stream",
		.data		= &nf_ct_udplite_timeout_stream,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= 0
	}
};
#endif /* CONFIG_SYSCTL */

static struct nf_conntrack_l4proto nf_conntrack_l4proto_udplite4 __read_mostly =
{
	.l3proto		= PF_INET,
	.l4proto		= IPPROTO_UDPLITE,
	.name			= "udplite",
	.pkt_to_tuple		= udplite_pkt_to_tuple,
	.invert_tuple		= udplite_invert_tuple,
	.print_tuple		= udplite_print_tuple,
	.print_conntrack	= udplite_print_conntrack,
	.packet			= udplite_packet,
	.new			= udplite_new,
	.error			= udplite_error,
#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
	.tuple_to_nfattr	= nf_ct_port_tuple_to_nfattr,
	.nfattr_to_tuple	= nf_ct_port_nfattr_to_tuple,
#endif
#ifdef CONFIG_SYSCTL
	.ctl_table_users	= &udplite_sysctl_table_users,
	.ctl_table_header	= &udplite_sysctl_header,
	.ctl_table		= udplite_sysctl_table,
#endif
};

static struct nf_conntrack_l4proto nf_conntrack_l4proto_udplite6 __read_mostly =
{
	.l3proto		= PF_INET6,
	.l4proto		= IPPROTO_UDPLITE,
	.name			= "udplite",
	.pkt_to_tuple		= udplite_pkt_to_tuple,
	.invert_tuple		= udplite_invert_tuple,
	.print_tuple		= udplite_print_tuple,
	.print_conntrack	= udplite_print_conntrack,
	.packet			= udplite_packet,
	.new			= udplite_new,
	.error			= udplite_error,
#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
	.tuple_to_nfattr	= nf_ct_port_tuple_to_nfattr,
	.nfattr_to_tuple	= nf_ct_port_nfattr_to_tuple,
#endif
#ifdef CONFIG_SYSCTL
	.ctl_table_users	= &udplite_sysctl_table_users,
	.ctl_table_header	= &udplite_sysctl_header,
	.ctl_table		= udplite_sysctl_table,
#endif
};

static int __init nf_conntrack_proto_udplite_init(void)
{
	int err;

	err = nf_conntrack_l4proto_register(&nf_conntrack_l4proto_udplite4);
	if (err < 0)
		goto err1;
	err = nf_conntrack_l4proto_register(&nf_conntrack_l4proto_udplite6);
	if (err < 0)
		goto err2;
	return 0;
err2:
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_udplite4);
err1:
	return err;
}

static void __exit nf_conntrack_proto_udplite_exit(void)
{
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_udplite6);
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_udplite4);
}

module_init(nf_conntrack_proto_udplite_init);
module_exit(nf_conntrack_proto_udplite_exit);

MODULE_LICENSE("GPL");
