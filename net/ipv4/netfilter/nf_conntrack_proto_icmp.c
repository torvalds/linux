/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/timer.h>
#include <linux/netfilter.h>
#include <linux/in.h>
#include <linux/icmp.h>
#include <linux/seq_file.h>
#include <net/ip.h>
#include <net/checksum.h>
#include <linux/netfilter_ipv4.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_log.h>

static unsigned int nf_ct_icmp_timeout __read_mostly = 30*HZ;

static bool icmp_pkt_to_tuple(const struct sk_buff *skb, unsigned int dataoff,
			      struct nf_conntrack_tuple *tuple)
{
	const struct icmphdr *hp;
	struct icmphdr _hdr;

	hp = skb_header_pointer(skb, dataoff, sizeof(_hdr), &_hdr);
	if (hp == NULL)
		return false;

	tuple->dst.u.icmp.type = hp->type;
	tuple->src.u.icmp.id = hp->un.echo.id;
	tuple->dst.u.icmp.code = hp->code;

	return true;
}

/* Add 1; spaces filled with 0. */
static const u_int8_t invmap[] = {
	[ICMP_ECHO] = ICMP_ECHOREPLY + 1,
	[ICMP_ECHOREPLY] = ICMP_ECHO + 1,
	[ICMP_TIMESTAMP] = ICMP_TIMESTAMPREPLY + 1,
	[ICMP_TIMESTAMPREPLY] = ICMP_TIMESTAMP + 1,
	[ICMP_INFO_REQUEST] = ICMP_INFO_REPLY + 1,
	[ICMP_INFO_REPLY] = ICMP_INFO_REQUEST + 1,
	[ICMP_ADDRESS] = ICMP_ADDRESSREPLY + 1,
	[ICMP_ADDRESSREPLY] = ICMP_ADDRESS + 1
};

static bool icmp_invert_tuple(struct nf_conntrack_tuple *tuple,
			      const struct nf_conntrack_tuple *orig)
{
	if (orig->dst.u.icmp.type >= sizeof(invmap)
	    || !invmap[orig->dst.u.icmp.type])
		return false;

	tuple->src.u.icmp.id = orig->src.u.icmp.id;
	tuple->dst.u.icmp.type = invmap[orig->dst.u.icmp.type] - 1;
	tuple->dst.u.icmp.code = orig->dst.u.icmp.code;
	return true;
}

/* Print out the per-protocol part of the tuple. */
static int icmp_print_tuple(struct seq_file *s,
			    const struct nf_conntrack_tuple *tuple)
{
	return seq_printf(s, "type=%u code=%u id=%u ",
			  tuple->dst.u.icmp.type,
			  tuple->dst.u.icmp.code,
			  ntohs(tuple->src.u.icmp.id));
}

/* Returns verdict for packet, or -1 for invalid. */
static int icmp_packet(struct nf_conn *ct,
		       const struct sk_buff *skb,
		       unsigned int dataoff,
		       enum ip_conntrack_info ctinfo,
		       u_int8_t pf,
		       unsigned int hooknum)
{
	/* Try to delete connection immediately after all replies:
	   won't actually vanish as we still have skb, and del_timer
	   means this will only run once even if count hits zero twice
	   (theoretically possible with SMP) */
	if (CTINFO2DIR(ctinfo) == IP_CT_DIR_REPLY) {
		if (atomic_dec_and_test(&ct->proto.icmp.count))
			nf_ct_kill_acct(ct, ctinfo, skb);
	} else {
		atomic_inc(&ct->proto.icmp.count);
		nf_conntrack_event_cache(IPCT_PROTOINFO_VOLATILE, ct);
		nf_ct_refresh_acct(ct, ctinfo, skb, nf_ct_icmp_timeout);
	}

	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static bool icmp_new(struct nf_conn *ct, const struct sk_buff *skb,
		     unsigned int dataoff)
{
	static const u_int8_t valid_new[] = {
		[ICMP_ECHO] = 1,
		[ICMP_TIMESTAMP] = 1,
		[ICMP_INFO_REQUEST] = 1,
		[ICMP_ADDRESS] = 1
	};

	if (ct->tuplehash[0].tuple.dst.u.icmp.type >= sizeof(valid_new)
	    || !valid_new[ct->tuplehash[0].tuple.dst.u.icmp.type]) {
		/* Can't create a new ICMP `conn' with this. */
		pr_debug("icmp: can't create new conn with type %u\n",
			 ct->tuplehash[0].tuple.dst.u.icmp.type);
		nf_ct_dump_tuple_ip(&ct->tuplehash[0].tuple);
		return false;
	}
	atomic_set(&ct->proto.icmp.count, 0);
	return true;
}

/* Returns conntrack if it dealt with ICMP, and filled in skb fields */
static int
icmp_error_message(struct net *net, struct sk_buff *skb,
		 enum ip_conntrack_info *ctinfo,
		 unsigned int hooknum)
{
	struct nf_conntrack_tuple innertuple, origtuple;
	const struct nf_conntrack_l4proto *innerproto;
	const struct nf_conntrack_tuple_hash *h;

	NF_CT_ASSERT(skb->nfct == NULL);

	/* Are they talking about one of our connections? */
	if (!nf_ct_get_tuplepr(skb,
			       skb_network_offset(skb) + ip_hdrlen(skb)
						       + sizeof(struct icmphdr),
			       PF_INET, &origtuple)) {
		pr_debug("icmp_error_message: failed to get tuple\n");
		return -NF_ACCEPT;
	}

	/* rcu_read_lock()ed by nf_hook_slow */
	innerproto = __nf_ct_l4proto_find(PF_INET, origtuple.dst.protonum);

	/* Ordinarily, we'd expect the inverted tupleproto, but it's
	   been preserved inside the ICMP. */
	if (!nf_ct_invert_tuple(&innertuple, &origtuple,
				&nf_conntrack_l3proto_ipv4, innerproto)) {
		pr_debug("icmp_error_message: no match\n");
		return -NF_ACCEPT;
	}

	*ctinfo = IP_CT_RELATED;

	h = nf_conntrack_find_get(net, &innertuple);
	if (!h) {
		pr_debug("icmp_error_message: no match\n");
		return -NF_ACCEPT;
	}

	if (NF_CT_DIRECTION(h) == IP_CT_DIR_REPLY)
		*ctinfo += IP_CT_IS_REPLY;

	/* Update skb to refer to this connection */
	skb->nfct = &nf_ct_tuplehash_to_ctrack(h)->ct_general;
	skb->nfctinfo = *ctinfo;
	return -NF_ACCEPT;
}

/* Small and modified version of icmp_rcv */
static int
icmp_error(struct net *net, struct sk_buff *skb, unsigned int dataoff,
	   enum ip_conntrack_info *ctinfo, u_int8_t pf, unsigned int hooknum)
{
	const struct icmphdr *icmph;
	struct icmphdr _ih;

	/* Not enough header? */
	icmph = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(_ih), &_ih);
	if (icmph == NULL) {
		if (LOG_INVALID(net, IPPROTO_ICMP))
			nf_log_packet(PF_INET, 0, skb, NULL, NULL, NULL,
				      "nf_ct_icmp: short packet ");
		return -NF_ACCEPT;
	}

	/* See ip_conntrack_proto_tcp.c */
	if (net->ct.sysctl_checksum && hooknum == NF_INET_PRE_ROUTING &&
	    nf_ip_checksum(skb, hooknum, dataoff, 0)) {
		if (LOG_INVALID(net, IPPROTO_ICMP))
			nf_log_packet(PF_INET, 0, skb, NULL, NULL, NULL,
				      "nf_ct_icmp: bad HW ICMP checksum ");
		return -NF_ACCEPT;
	}

	/*
	 *	18 is the highest 'known' ICMP type. Anything else is a mystery
	 *
	 *	RFC 1122: 3.2.2  Unknown ICMP messages types MUST be silently
	 *		  discarded.
	 */
	if (icmph->type > NR_ICMP_TYPES) {
		if (LOG_INVALID(net, IPPROTO_ICMP))
			nf_log_packet(PF_INET, 0, skb, NULL, NULL, NULL,
				      "nf_ct_icmp: invalid ICMP type ");
		return -NF_ACCEPT;
	}

	/* Need to track icmp error message? */
	if (icmph->type != ICMP_DEST_UNREACH
	    && icmph->type != ICMP_SOURCE_QUENCH
	    && icmph->type != ICMP_TIME_EXCEEDED
	    && icmph->type != ICMP_PARAMETERPROB
	    && icmph->type != ICMP_REDIRECT)
		return NF_ACCEPT;

	return icmp_error_message(net, skb, ctinfo, hooknum);
}

#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

static int icmp_tuple_to_nlattr(struct sk_buff *skb,
				const struct nf_conntrack_tuple *t)
{
	NLA_PUT_BE16(skb, CTA_PROTO_ICMP_ID, t->src.u.icmp.id);
	NLA_PUT_U8(skb, CTA_PROTO_ICMP_TYPE, t->dst.u.icmp.type);
	NLA_PUT_U8(skb, CTA_PROTO_ICMP_CODE, t->dst.u.icmp.code);

	return 0;

nla_put_failure:
	return -1;
}

static const struct nla_policy icmp_nla_policy[CTA_PROTO_MAX+1] = {
	[CTA_PROTO_ICMP_TYPE]	= { .type = NLA_U8 },
	[CTA_PROTO_ICMP_CODE]	= { .type = NLA_U8 },
	[CTA_PROTO_ICMP_ID]	= { .type = NLA_U16 },
};

static int icmp_nlattr_to_tuple(struct nlattr *tb[],
				struct nf_conntrack_tuple *tuple)
{
	if (!tb[CTA_PROTO_ICMP_TYPE]
	    || !tb[CTA_PROTO_ICMP_CODE]
	    || !tb[CTA_PROTO_ICMP_ID])
		return -EINVAL;

	tuple->dst.u.icmp.type = nla_get_u8(tb[CTA_PROTO_ICMP_TYPE]);
	tuple->dst.u.icmp.code = nla_get_u8(tb[CTA_PROTO_ICMP_CODE]);
	tuple->src.u.icmp.id = nla_get_be16(tb[CTA_PROTO_ICMP_ID]);

	if (tuple->dst.u.icmp.type >= sizeof(invmap)
	    || !invmap[tuple->dst.u.icmp.type])
		return -EINVAL;

	return 0;
}
#endif

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *icmp_sysctl_header;
static struct ctl_table icmp_sysctl_table[] = {
	{
		.procname	= "nf_conntrack_icmp_timeout",
		.data		= &nf_ct_icmp_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.ctl_name = 0
	}
};
#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
static struct ctl_table icmp_compat_sysctl_table[] = {
	{
		.procname	= "ip_conntrack_icmp_timeout",
		.data		= &nf_ct_icmp_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.ctl_name = 0
	}
};
#endif /* CONFIG_NF_CONNTRACK_PROC_COMPAT */
#endif /* CONFIG_SYSCTL */

struct nf_conntrack_l4proto nf_conntrack_l4proto_icmp __read_mostly =
{
	.l3proto		= PF_INET,
	.l4proto		= IPPROTO_ICMP,
	.name			= "icmp",
	.pkt_to_tuple		= icmp_pkt_to_tuple,
	.invert_tuple		= icmp_invert_tuple,
	.print_tuple		= icmp_print_tuple,
	.packet			= icmp_packet,
	.new			= icmp_new,
	.error			= icmp_error,
	.destroy		= NULL,
	.me			= NULL,
#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
	.tuple_to_nlattr	= icmp_tuple_to_nlattr,
	.nlattr_to_tuple	= icmp_nlattr_to_tuple,
	.nla_policy		= icmp_nla_policy,
#endif
#ifdef CONFIG_SYSCTL
	.ctl_table_header	= &icmp_sysctl_header,
	.ctl_table		= icmp_sysctl_table,
#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
	.ctl_compat_table	= icmp_compat_sysctl_table,
#endif
#endif
};
