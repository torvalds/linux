/*
 * Copyright (C)2003,2004 USAGI/WIDE Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Author:
 *	Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- ICMPv6 tracking support. Derived from the original ip_conntrack code
 *	  net/ipv4/netfilter/ip_conntrack_proto_icmp.c which had the following
 *	  copyright information:
 *		(C) 1999-2001 Paul `Rusty' Russell
 *		(C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/in6.h>
#include <linux/icmpv6.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <net/ip6_checksum.h>
#include <linux/seq_file.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/ipv6/nf_conntrack_icmpv6.h>

unsigned long nf_ct_icmpv6_timeout __read_mostly = 30*HZ;

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

static int icmpv6_pkt_to_tuple(const struct sk_buff *skb,
			       unsigned int dataoff,
			       struct nf_conntrack_tuple *tuple)
{
	struct icmp6hdr _hdr, *hp;

	hp = skb_header_pointer(skb, dataoff, sizeof(_hdr), &_hdr);
	if (hp == NULL)
		return 0;
	tuple->dst.u.icmp.type = hp->icmp6_type;
	tuple->src.u.icmp.id = hp->icmp6_identifier;
	tuple->dst.u.icmp.code = hp->icmp6_code;

	return 1;
}

/* Add 1; spaces filled with 0. */
static u_int8_t invmap[] = {
	[ICMPV6_ECHO_REQUEST - 128]	= ICMPV6_ECHO_REPLY + 1,
	[ICMPV6_ECHO_REPLY - 128]	= ICMPV6_ECHO_REQUEST + 1,
	[ICMPV6_NI_QUERY - 128]		= ICMPV6_NI_QUERY + 1,
	[ICMPV6_NI_REPLY - 128]		= ICMPV6_NI_REPLY +1
};

static int icmpv6_invert_tuple(struct nf_conntrack_tuple *tuple,
			       const struct nf_conntrack_tuple *orig)
{
	int type = orig->dst.u.icmp.type - 128;
	if (type < 0 || type >= sizeof(invmap) || !invmap[type])
		return 0;

	tuple->src.u.icmp.id   = orig->src.u.icmp.id;
	tuple->dst.u.icmp.type = invmap[type] - 1;
	tuple->dst.u.icmp.code = orig->dst.u.icmp.code;
	return 1;
}

/* Print out the per-protocol part of the tuple. */
static int icmpv6_print_tuple(struct seq_file *s,
			      const struct nf_conntrack_tuple *tuple)
{
	return seq_printf(s, "type=%u code=%u id=%u ",
			  tuple->dst.u.icmp.type,
			  tuple->dst.u.icmp.code,
			  ntohs(tuple->src.u.icmp.id));
}

/* Print out the private part of the conntrack. */
static int icmpv6_print_conntrack(struct seq_file *s,
				  const struct nf_conn *conntrack)
{
	return 0;
}

/* Returns verdict for packet, or -1 for invalid. */
static int icmpv6_packet(struct nf_conn *ct,
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
		nf_ct_refresh_acct(ct, ctinfo, skb, nf_ct_icmpv6_timeout);
	}

	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int icmpv6_new(struct nf_conn *conntrack,
		      const struct sk_buff *skb,
		      unsigned int dataoff)
{
	static u_int8_t valid_new[] = {
		[ICMPV6_ECHO_REQUEST - 128] = 1,
		[ICMPV6_NI_QUERY - 128] = 1
	};
	int type = conntrack->tuplehash[0].tuple.dst.u.icmp.type - 128;

	if (type < 0 || type >= sizeof(valid_new) || !valid_new[type]) {
		/* Can't create a new ICMPv6 `conn' with this. */
		DEBUGP("icmpv6: can't create new conn with type %u\n",
		       type + 128);
		NF_CT_DUMP_TUPLE(&conntrack->tuplehash[0].tuple);
		return 0;
	}
	atomic_set(&conntrack->proto.icmp.count, 0);
	return 1;
}

static int
icmpv6_error_message(struct sk_buff *skb,
		     unsigned int icmp6off,
		     enum ip_conntrack_info *ctinfo,
		     unsigned int hooknum)
{
	struct nf_conntrack_tuple intuple, origtuple;
	struct nf_conntrack_tuple_hash *h;
	struct icmp6hdr _hdr, *hp;
	unsigned int inip6off;
	struct nf_conntrack_l4proto *inproto;
	u_int8_t inprotonum;
	unsigned int inprotoff;

	NF_CT_ASSERT(skb->nfct == NULL);

	hp = skb_header_pointer(skb, icmp6off, sizeof(_hdr), &_hdr);
	if (hp == NULL) {
		DEBUGP("icmpv6_error: Can't get ICMPv6 hdr.\n");
		return -NF_ACCEPT;
	}

	inip6off = icmp6off + sizeof(_hdr);
	if (skb_copy_bits(skb, inip6off+offsetof(struct ipv6hdr, nexthdr),
			  &inprotonum, sizeof(inprotonum)) != 0) {
		DEBUGP("icmpv6_error: Can't get nexthdr in inner IPv6 header.\n");
		return -NF_ACCEPT;
	}
	inprotoff = nf_ct_ipv6_skip_exthdr(skb,
					   inip6off + sizeof(struct ipv6hdr),
					   &inprotonum,
					   skb->len - inip6off
						    - sizeof(struct ipv6hdr));

	if ((inprotoff < 0) || (inprotoff > skb->len) ||
	    (inprotonum == NEXTHDR_FRAGMENT)) {
		DEBUGP("icmpv6_error: Can't get protocol header in ICMPv6 payload.\n");
		return -NF_ACCEPT;
	}

	inproto = __nf_ct_l4proto_find(PF_INET6, inprotonum);

	/* Are they talking about one of our connections? */
	if (!nf_ct_get_tuple(skb, inip6off, inprotoff, PF_INET6, inprotonum,
			     &origtuple, &nf_conntrack_l3proto_ipv6, inproto)) {
		DEBUGP("icmpv6_error: Can't get tuple\n");
		return -NF_ACCEPT;
	}

	/* Ordinarily, we'd expect the inverted tupleproto, but it's
	   been preserved inside the ICMP. */
	if (!nf_ct_invert_tuple(&intuple, &origtuple,
				&nf_conntrack_l3proto_ipv6, inproto)) {
		DEBUGP("icmpv6_error: Can't invert tuple\n");
		return -NF_ACCEPT;
	}

	*ctinfo = IP_CT_RELATED;

	h = nf_conntrack_find_get(&intuple, NULL);
	if (!h) {
		DEBUGP("icmpv6_error: no match\n");
		return -NF_ACCEPT;
	} else {
		if (NF_CT_DIRECTION(h) == IP_CT_DIR_REPLY)
			*ctinfo += IP_CT_IS_REPLY;
	}

	/* Update skb to refer to this connection */
	skb->nfct = &nf_ct_tuplehash_to_ctrack(h)->ct_general;
	skb->nfctinfo = *ctinfo;
	return -NF_ACCEPT;
}

static int
icmpv6_error(struct sk_buff *skb, unsigned int dataoff,
	     enum ip_conntrack_info *ctinfo, int pf, unsigned int hooknum)
{
	struct icmp6hdr _ih, *icmp6h;

	icmp6h = skb_header_pointer(skb, dataoff, sizeof(_ih), &_ih);
	if (icmp6h == NULL) {
		if (LOG_INVALID(IPPROTO_ICMPV6))
		nf_log_packet(PF_INET6, 0, skb, NULL, NULL, NULL,
			      "nf_ct_icmpv6: short packet ");
		return -NF_ACCEPT;
	}

	if (nf_conntrack_checksum && hooknum == NF_IP6_PRE_ROUTING &&
	    nf_ip6_checksum(skb, hooknum, dataoff, IPPROTO_ICMPV6)) {
		nf_log_packet(PF_INET6, 0, skb, NULL, NULL, NULL,
			      "nf_ct_icmpv6: ICMPv6 checksum failed\n");
		return -NF_ACCEPT;
	}

	/* is not error message ? */
	if (icmp6h->icmp6_type >= 128)
		return NF_ACCEPT;

	return icmpv6_error_message(skb, dataoff, ctinfo, hooknum);
}

#if defined(CONFIG_NF_CT_NETLINK) || \
    defined(CONFIG_NF_CT_NETLINK_MODULE)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
static int icmpv6_tuple_to_nfattr(struct sk_buff *skb,
				  const struct nf_conntrack_tuple *t)
{
	NFA_PUT(skb, CTA_PROTO_ICMPV6_ID, sizeof(u_int16_t),
		&t->src.u.icmp.id);
	NFA_PUT(skb, CTA_PROTO_ICMPV6_TYPE, sizeof(u_int8_t),
		&t->dst.u.icmp.type);
	NFA_PUT(skb, CTA_PROTO_ICMPV6_CODE, sizeof(u_int8_t),
		&t->dst.u.icmp.code);

	return 0;

nfattr_failure:
	return -1;
}

static const size_t cta_min_proto[CTA_PROTO_MAX] = {
	[CTA_PROTO_ICMPV6_TYPE-1] = sizeof(u_int8_t),
	[CTA_PROTO_ICMPV6_CODE-1] = sizeof(u_int8_t),
	[CTA_PROTO_ICMPV6_ID-1]   = sizeof(u_int16_t)
};

static int icmpv6_nfattr_to_tuple(struct nfattr *tb[],
				struct nf_conntrack_tuple *tuple)
{
	if (!tb[CTA_PROTO_ICMPV6_TYPE-1]
	    || !tb[CTA_PROTO_ICMPV6_CODE-1]
	    || !tb[CTA_PROTO_ICMPV6_ID-1])
		return -EINVAL;

	if (nfattr_bad_size(tb, CTA_PROTO_MAX, cta_min_proto))
		return -EINVAL;

	tuple->dst.u.icmp.type =
			*(u_int8_t *)NFA_DATA(tb[CTA_PROTO_ICMPV6_TYPE-1]);
	tuple->dst.u.icmp.code =
			*(u_int8_t *)NFA_DATA(tb[CTA_PROTO_ICMPV6_CODE-1]);
	tuple->src.u.icmp.id =
			*(u_int16_t *)NFA_DATA(tb[CTA_PROTO_ICMPV6_ID-1]);

	if (tuple->dst.u.icmp.type < 128
	    || tuple->dst.u.icmp.type - 128 >= sizeof(invmap)
	    || !invmap[tuple->dst.u.icmp.type - 128])
		return -EINVAL;

	return 0;
}
#endif

struct nf_conntrack_l4proto nf_conntrack_l4proto_icmpv6 =
{
	.l3proto		= PF_INET6,
	.l4proto		= IPPROTO_ICMPV6,
	.name			= "icmpv6",
	.pkt_to_tuple		= icmpv6_pkt_to_tuple,
	.invert_tuple		= icmpv6_invert_tuple,
	.print_tuple		= icmpv6_print_tuple,
	.print_conntrack	= icmpv6_print_conntrack,
	.packet			= icmpv6_packet,
	.new			= icmpv6_new,
	.error			= icmpv6_error,
#if defined(CONFIG_NF_CT_NETLINK) || \
    defined(CONFIG_NF_CT_NETLINK_MODULE)
	.tuple_to_nfattr	= icmpv6_tuple_to_nfattr,
	.nfattr_to_tuple	= icmpv6_nfattr_to_tuple,
#endif
};

EXPORT_SYMBOL(nf_conntrack_l4proto_icmpv6);
