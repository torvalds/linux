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
#include <net/netfilter/nf_log.h>

enum udplite_conntrack {
	UDPLITE_CT_UNREPLIED,
	UDPLITE_CT_REPLIED,
	UDPLITE_CT_MAX
};

static unsigned int udplite_timeouts[UDPLITE_CT_MAX] = {
	[UDPLITE_CT_UNREPLIED]	= 30*HZ,
	[UDPLITE_CT_REPLIED]	= 180*HZ,
};

static bool udplite_pkt_to_tuple(const struct sk_buff *skb,
				 unsigned int dataoff,
				 struct nf_conntrack_tuple *tuple)
{
	const struct udphdr *hp;
	struct udphdr _hdr;

	hp = skb_header_pointer(skb, dataoff, sizeof(_hdr), &_hdr);
	if (hp == NULL)
		return false;

	tuple->src.u.udp.port = hp->source;
	tuple->dst.u.udp.port = hp->dest;
	return true;
}

static bool udplite_invert_tuple(struct nf_conntrack_tuple *tuple,
				 const struct nf_conntrack_tuple *orig)
{
	tuple->src.u.udp.port = orig->dst.u.udp.port;
	tuple->dst.u.udp.port = orig->src.u.udp.port;
	return true;
}

/* Print out the per-protocol part of the tuple. */
static int udplite_print_tuple(struct seq_file *s,
			       const struct nf_conntrack_tuple *tuple)
{
	return seq_printf(s, "sport=%hu dport=%hu ",
			  ntohs(tuple->src.u.udp.port),
			  ntohs(tuple->dst.u.udp.port));
}

static unsigned int *udplite_get_timeouts(struct net *net)
{
	return udplite_timeouts;
}

/* Returns verdict for packet, and may modify conntracktype */
static int udplite_packet(struct nf_conn *ct,
			  const struct sk_buff *skb,
			  unsigned int dataoff,
			  enum ip_conntrack_info ctinfo,
			  u_int8_t pf,
			  unsigned int hooknum,
			  unsigned int *timeouts)
{
	/* If we've seen traffic both ways, this is some kind of UDP
	   stream.  Extend timeout. */
	if (test_bit(IPS_SEEN_REPLY_BIT, &ct->status)) {
		nf_ct_refresh_acct(ct, ctinfo, skb,
				   timeouts[UDPLITE_CT_REPLIED]);
		/* Also, more likely to be important, and not a probe */
		if (!test_and_set_bit(IPS_ASSURED_BIT, &ct->status))
			nf_conntrack_event_cache(IPCT_ASSURED, ct);
	} else {
		nf_ct_refresh_acct(ct, ctinfo, skb,
				   timeouts[UDPLITE_CT_UNREPLIED]);
	}
	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static bool udplite_new(struct nf_conn *ct, const struct sk_buff *skb,
			unsigned int dataoff, unsigned int *timeouts)
{
	return true;
}

static int udplite_error(struct net *net, struct nf_conn *tmpl,
			 struct sk_buff *skb,
			 unsigned int dataoff,
			 enum ip_conntrack_info *ctinfo,
			 u_int8_t pf,
			 unsigned int hooknum)
{
	unsigned int udplen = skb->len - dataoff;
	const struct udphdr *hdr;
	struct udphdr _hdr;
	unsigned int cscov;

	/* Header is too small? */
	hdr = skb_header_pointer(skb, dataoff, sizeof(_hdr), &_hdr);
	if (hdr == NULL) {
		if (LOG_INVALID(net, IPPROTO_UDPLITE))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
				      "nf_ct_udplite: short packet ");
		return -NF_ACCEPT;
	}

	cscov = ntohs(hdr->len);
	if (cscov == 0)
		cscov = udplen;
	else if (cscov < sizeof(*hdr) || cscov > udplen) {
		if (LOG_INVALID(net, IPPROTO_UDPLITE))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
				"nf_ct_udplite: invalid checksum coverage ");
		return -NF_ACCEPT;
	}

	/* UDPLITE mandates checksums */
	if (!hdr->check) {
		if (LOG_INVALID(net, IPPROTO_UDPLITE))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
				      "nf_ct_udplite: checksum missing ");
		return -NF_ACCEPT;
	}

	/* Checksum invalid? Ignore. */
	if (net->ct.sysctl_checksum && hooknum == NF_INET_PRE_ROUTING &&
	    nf_checksum_partial(skb, hooknum, dataoff, cscov, IPPROTO_UDP,
	    			pf)) {
		if (LOG_INVALID(net, IPPROTO_UDPLITE))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
				      "nf_ct_udplite: bad UDPLite checksum ");
		return -NF_ACCEPT;
	}

	return NF_ACCEPT;
}

#if IS_ENABLED(CONFIG_NF_CT_NETLINK_TIMEOUT)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_cttimeout.h>

static int udplite_timeout_nlattr_to_obj(struct nlattr *tb[], void *data)
{
	unsigned int *timeouts = data;

	/* set default timeouts for UDPlite. */
	timeouts[UDPLITE_CT_UNREPLIED] = udplite_timeouts[UDPLITE_CT_UNREPLIED];
	timeouts[UDPLITE_CT_REPLIED] = udplite_timeouts[UDPLITE_CT_REPLIED];

	if (tb[CTA_TIMEOUT_UDPLITE_UNREPLIED]) {
		timeouts[UDPLITE_CT_UNREPLIED] =
		  ntohl(nla_get_be32(tb[CTA_TIMEOUT_UDPLITE_UNREPLIED])) * HZ;
	}
	if (tb[CTA_TIMEOUT_UDPLITE_REPLIED]) {
		timeouts[UDPLITE_CT_REPLIED] =
		  ntohl(nla_get_be32(tb[CTA_TIMEOUT_UDPLITE_REPLIED])) * HZ;
	}
	return 0;
}

static int
udplite_timeout_obj_to_nlattr(struct sk_buff *skb, const void *data)
{
	const unsigned int *timeouts = data;

	NLA_PUT_BE32(skb, CTA_TIMEOUT_UDPLITE_UNREPLIED,
			htonl(timeouts[UDPLITE_CT_UNREPLIED] / HZ));
	NLA_PUT_BE32(skb, CTA_TIMEOUT_UDPLITE_REPLIED,
			htonl(timeouts[UDPLITE_CT_REPLIED] / HZ));
	return 0;

nla_put_failure:
	return -ENOSPC;
}

static const struct nla_policy
udplite_timeout_nla_policy[CTA_TIMEOUT_UDPLITE_MAX+1] = {
	[CTA_TIMEOUT_UDPLITE_UNREPLIED]	= { .type = NLA_U32 },
	[CTA_TIMEOUT_UDPLITE_REPLIED]	= { .type = NLA_U32 },
};
#endif /* CONFIG_NF_CT_NETLINK_TIMEOUT */

#ifdef CONFIG_SYSCTL
static unsigned int udplite_sysctl_table_users;
static struct ctl_table_header *udplite_sysctl_header;
static struct ctl_table udplite_sysctl_table[] = {
	{
		.procname	= "nf_conntrack_udplite_timeout",
		.data		= &udplite_timeouts[UDPLITE_CT_UNREPLIED],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "nf_conntrack_udplite_timeout_stream",
		.data		= &udplite_timeouts[UDPLITE_CT_REPLIED],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{ }
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
	.packet			= udplite_packet,
	.get_timeouts		= udplite_get_timeouts,
	.new			= udplite_new,
	.error			= udplite_error,
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.tuple_to_nlattr	= nf_ct_port_tuple_to_nlattr,
	.nlattr_tuple_size	= nf_ct_port_nlattr_tuple_size,
	.nlattr_to_tuple	= nf_ct_port_nlattr_to_tuple,
	.nla_policy		= nf_ct_port_nla_policy,
#endif
#if IS_ENABLED(CONFIG_NF_CT_NETLINK_TIMEOUT)
	.ctnl_timeout		= {
		.nlattr_to_obj	= udplite_timeout_nlattr_to_obj,
		.obj_to_nlattr	= udplite_timeout_obj_to_nlattr,
		.nlattr_max	= CTA_TIMEOUT_UDPLITE_MAX,
		.obj_size	= sizeof(unsigned int) *
					CTA_TIMEOUT_UDPLITE_MAX,
		.nla_policy	= udplite_timeout_nla_policy,
	},
#endif /* CONFIG_NF_CT_NETLINK_TIMEOUT */
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
	.packet			= udplite_packet,
	.get_timeouts		= udplite_get_timeouts,
	.new			= udplite_new,
	.error			= udplite_error,
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.tuple_to_nlattr	= nf_ct_port_tuple_to_nlattr,
	.nlattr_tuple_size	= nf_ct_port_nlattr_tuple_size,
	.nlattr_to_tuple	= nf_ct_port_nlattr_to_tuple,
	.nla_policy		= nf_ct_port_nla_policy,
#endif
#if IS_ENABLED(CONFIG_NF_CT_NETLINK_TIMEOUT)
	.ctnl_timeout		= {
		.nlattr_to_obj	= udplite_timeout_nlattr_to_obj,
		.obj_to_nlattr	= udplite_timeout_obj_to_nlattr,
		.nlattr_max	= CTA_TIMEOUT_UDPLITE_MAX,
		.obj_size	= sizeof(unsigned int) *
					CTA_TIMEOUT_UDPLITE_MAX,
		.nla_policy	= udplite_timeout_nla_policy,
	},
#endif /* CONFIG_NF_CT_NETLINK_TIMEOUT */
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
