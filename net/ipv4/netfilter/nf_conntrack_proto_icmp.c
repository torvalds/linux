/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- enable working with Layer 3 protocol independent connection tracking.
 *
 * Derived from net/ipv4/netfilter/ip_conntrack_proto_icmp.c
 */

#include <linux/types.h>
#include <linux/sched.h>
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

static unsigned long nf_ct_icmp_timeout __read_mostly = 30*HZ;

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

static int icmp_pkt_to_tuple(const struct sk_buff *skb,
			     unsigned int dataoff,
			     struct nf_conntrack_tuple *tuple)
{
	struct icmphdr _hdr, *hp;

	hp = skb_header_pointer(skb, dataoff, sizeof(_hdr), &_hdr);
	if (hp == NULL)
		return 0;

	tuple->dst.u.icmp.type = hp->type;
	tuple->src.u.icmp.id = hp->un.echo.id;
	tuple->dst.u.icmp.code = hp->code;

	return 1;
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

static int icmp_invert_tuple(struct nf_conntrack_tuple *tuple,
			     const struct nf_conntrack_tuple *orig)
{
	if (orig->dst.u.icmp.type >= sizeof(invmap)
	    || !invmap[orig->dst.u.icmp.type])
		return 0;

	tuple->src.u.icmp.id = orig->src.u.icmp.id;
	tuple->dst.u.icmp.type = invmap[orig->dst.u.icmp.type] - 1;
	tuple->dst.u.icmp.code = orig->dst.u.icmp.code;
	return 1;
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

/* Print out the private part of the conntrack. */
static int icmp_print_conntrack(struct seq_file *s,
				const struct nf_conn *conntrack)
{
	return 0;
}

/* Returns verdict for packet, or -1 for invalid. */
static int icmp_packet(struct nf_conn *ct,
		       const struct sk_buff *skb,
		       unsigned int dataoff,
		       enum ip_conntrack_info ctinfo,
		       int pf,
		       unsigned int hooknum)
{
	/* Try to delete connection immediately after all replies:
           won't actually vanish as we still have skb, and del_timer
           means this will only run once even if count hits zero twice
           (theoretically possible with SMP) */
	if (CTINFO2DIR(ctinfo) == IP_CT_DIR_REPLY) {
		if (atomic_dec_and_test(&ct->proto.icmp.count)
		    && del_timer(&ct->timeout))
			ct->timeout.function((unsigned long)ct);
	} else {
		atomic_inc(&ct->proto.icmp.count);
		nf_conntrack_event_cache(IPCT_PROTOINFO_VOLATILE, skb);
		nf_ct_refresh_acct(ct, ctinfo, skb, nf_ct_icmp_timeout);
	}

	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int icmp_new(struct nf_conn *conntrack,
		    const struct sk_buff *skb, unsigned int dataoff)
{
	static const u_int8_t valid_new[] = {
		[ICMP_ECHO] = 1,
		[ICMP_TIMESTAMP] = 1,
		[ICMP_INFO_REQUEST] = 1,
		[ICMP_ADDRESS] = 1
	};

	if (conntrack->tuplehash[0].tuple.dst.u.icmp.type >= sizeof(valid_new)
	    || !valid_new[conntrack->tuplehash[0].tuple.dst.u.icmp.type]) {
		/* Can't create a new ICMP `conn' with this. */
		DEBUGP("icmp: can't create new conn with type %u\n",
		       conntrack->tuplehash[0].tuple.dst.u.icmp.type);
		NF_CT_DUMP_TUPLE(&conntrack->tuplehash[0].tuple);
		return 0;
	}
	atomic_set(&conntrack->proto.icmp.count, 0);
	return 1;
}

extern struct nf_conntrack_l3proto nf_conntrack_l3proto_ipv4;
/* Returns conntrack if it dealt with ICMP, and filled in skb fields */
static int
icmp_error_message(struct sk_buff *skb,
                 enum ip_conntrack_info *ctinfo,
                 unsigned int hooknum)
{
	struct nf_conntrack_tuple innertuple, origtuple;
	struct {
		struct icmphdr icmp;
		struct iphdr ip;
	} _in, *inside;
	struct nf_conntrack_l4proto *innerproto;
	struct nf_conntrack_tuple_hash *h;
	int dataoff;

	NF_CT_ASSERT(skb->nfct == NULL);

	/* Not enough header? */
	inside = skb_header_pointer(skb, skb->nh.iph->ihl*4, sizeof(_in), &_in);
	if (inside == NULL)
		return -NF_ACCEPT;

	/* Ignore ICMP's containing fragments (shouldn't happen) */
	if (inside->ip.frag_off & htons(IP_OFFSET)) {
		DEBUGP("icmp_error_message: fragment of proto %u\n",
		       inside->ip.protocol);
		return -NF_ACCEPT;
	}

	innerproto = __nf_ct_l4proto_find(PF_INET, inside->ip.protocol);
	dataoff = skb->nh.iph->ihl*4 + sizeof(inside->icmp);
	/* Are they talking about one of our connections? */
	if (!nf_ct_get_tuple(skb, dataoff, dataoff + inside->ip.ihl*4, PF_INET,
			     inside->ip.protocol, &origtuple,
			     &nf_conntrack_l3proto_ipv4, innerproto)) {
		DEBUGP("icmp_error_message: ! get_tuple p=%u",
		       inside->ip.protocol);
		return -NF_ACCEPT;
	}

        /* Ordinarily, we'd expect the inverted tupleproto, but it's
           been preserved inside the ICMP. */
        if (!nf_ct_invert_tuple(&innertuple, &origtuple,
				&nf_conntrack_l3proto_ipv4, innerproto)) {
		DEBUGP("icmp_error_message: no match\n");
		return -NF_ACCEPT;
	}

	*ctinfo = IP_CT_RELATED;

	h = nf_conntrack_find_get(&innertuple, NULL);
	if (!h) {
		/* Locally generated ICMPs will match inverted if they
		   haven't been SNAT'ed yet */
		/* FIXME: NAT code has to handle half-done double NAT --RR */
		if (hooknum == NF_IP_LOCAL_OUT)
			h = nf_conntrack_find_get(&origtuple, NULL);

		if (!h) {
			DEBUGP("icmp_error_message: no match\n");
			return -NF_ACCEPT;
		}

		/* Reverse direction from that found */
		if (NF_CT_DIRECTION(h) == IP_CT_DIR_REPLY)
			*ctinfo += IP_CT_IS_REPLY;
	} else {
		if (NF_CT_DIRECTION(h) == IP_CT_DIR_REPLY)
			*ctinfo += IP_CT_IS_REPLY;
	}

        /* Update skb to refer to this connection */
        skb->nfct = &nf_ct_tuplehash_to_ctrack(h)->ct_general;
        skb->nfctinfo = *ctinfo;
        return -NF_ACCEPT;
}

/* Small and modified version of icmp_rcv */
static int
icmp_error(struct sk_buff *skb, unsigned int dataoff,
	   enum ip_conntrack_info *ctinfo, int pf, unsigned int hooknum)
{
	struct icmphdr _ih, *icmph;

	/* Not enough header? */
	icmph = skb_header_pointer(skb, skb->nh.iph->ihl*4, sizeof(_ih), &_ih);
	if (icmph == NULL) {
		if (LOG_INVALID(IPPROTO_ICMP))
			nf_log_packet(PF_INET, 0, skb, NULL, NULL, NULL,
				      "nf_ct_icmp: short packet ");
		return -NF_ACCEPT;
	}

	/* See ip_conntrack_proto_tcp.c */
	if (nf_conntrack_checksum && hooknum == NF_IP_PRE_ROUTING &&
	    nf_ip_checksum(skb, hooknum, dataoff, 0)) {
		if (LOG_INVALID(IPPROTO_ICMP))
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
		if (LOG_INVALID(IPPROTO_ICMP))
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

	return icmp_error_message(skb, ctinfo, hooknum);
}

#if defined(CONFIG_NF_CT_NETLINK) || \
    defined(CONFIG_NF_CT_NETLINK_MODULE)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

static int icmp_tuple_to_nfattr(struct sk_buff *skb,
				const struct nf_conntrack_tuple *t)
{
	NFA_PUT(skb, CTA_PROTO_ICMP_ID, sizeof(u_int16_t),
		&t->src.u.icmp.id);
	NFA_PUT(skb, CTA_PROTO_ICMP_TYPE, sizeof(u_int8_t),
		&t->dst.u.icmp.type);
	NFA_PUT(skb, CTA_PROTO_ICMP_CODE, sizeof(u_int8_t),
		&t->dst.u.icmp.code);

	return 0;

nfattr_failure:
	return -1;
}

static const size_t cta_min_proto[CTA_PROTO_MAX] = {
	[CTA_PROTO_ICMP_TYPE-1] = sizeof(u_int8_t),
	[CTA_PROTO_ICMP_CODE-1] = sizeof(u_int8_t),
	[CTA_PROTO_ICMP_ID-1]   = sizeof(u_int16_t)
};

static int icmp_nfattr_to_tuple(struct nfattr *tb[],
				struct nf_conntrack_tuple *tuple)
{
	if (!tb[CTA_PROTO_ICMP_TYPE-1]
	    || !tb[CTA_PROTO_ICMP_CODE-1]
	    || !tb[CTA_PROTO_ICMP_ID-1])
		return -EINVAL;

	if (nfattr_bad_size(tb, CTA_PROTO_MAX, cta_min_proto))
		return -EINVAL;

	tuple->dst.u.icmp.type = 
			*(u_int8_t *)NFA_DATA(tb[CTA_PROTO_ICMP_TYPE-1]);
	tuple->dst.u.icmp.code =
			*(u_int8_t *)NFA_DATA(tb[CTA_PROTO_ICMP_CODE-1]);
	tuple->src.u.icmp.id =
			*(u_int16_t *)NFA_DATA(tb[CTA_PROTO_ICMP_ID-1]);

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
		.ctl_name	= NET_NF_CONNTRACK_ICMP_TIMEOUT,
		.procname	= "nf_conntrack_icmp_timeout",
		.data		= &nf_ct_icmp_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
        {
		.ctl_name = 0
	}
};
#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
static struct ctl_table icmp_compat_sysctl_table[] = {
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_ICMP_TIMEOUT,
		.procname	= "ip_conntrack_icmp_timeout",
		.data		= &nf_ct_icmp_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
        {
		.ctl_name = 0
	}
};
#endif /* CONFIG_NF_CONNTRACK_PROC_COMPAT */
#endif /* CONFIG_SYSCTL */

struct nf_conntrack_l4proto nf_conntrack_l4proto_icmp =
{
	.l3proto		= PF_INET,
	.l4proto		= IPPROTO_ICMP,
	.name			= "icmp",
	.pkt_to_tuple		= icmp_pkt_to_tuple,
	.invert_tuple		= icmp_invert_tuple,
	.print_tuple		= icmp_print_tuple,
	.print_conntrack	= icmp_print_conntrack,
	.packet			= icmp_packet,
	.new			= icmp_new,
	.error			= icmp_error,
	.destroy		= NULL,
	.me			= NULL,
#if defined(CONFIG_NF_CT_NETLINK) || \
    defined(CONFIG_NF_CT_NETLINK_MODULE)
	.tuple_to_nfattr	= icmp_tuple_to_nfattr,
	.nfattr_to_tuple	= icmp_nfattr_to_tuple,
#endif
#ifdef CONFIG_SYSCTL
	.ctl_table_header	= &icmp_sysctl_header,
	.ctl_table		= icmp_sysctl_table,
#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
	.ctl_compat_table	= icmp_compat_sysctl_table,
#endif
#endif
};

EXPORT_SYMBOL(nf_conntrack_l4proto_icmp);
