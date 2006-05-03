/*
 * (C) 2003,2004 USAGI/WIDE Project <http://www.linux-ipv6.org>
 *
 * Based largely upon the original ip_conntrack code which
 * had the following copyright information:
 *
 * (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Author:
 *	Yasuyuki Kozakai @USAGI	<yasuyuki.kozakai@toshiba.co.jp>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/ip.h>

#include <linux/netfilter_ipv4.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_protocol.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

DECLARE_PER_CPU(struct nf_conntrack_stat, nf_conntrack_stat);

static int generic_pkt_to_tuple(const struct sk_buff *skb, unsigned int nhoff,
				struct nf_conntrack_tuple *tuple)
{
	memset(&tuple->src.u3, 0, sizeof(tuple->src.u3));
	memset(&tuple->dst.u3, 0, sizeof(tuple->dst.u3));

	return 1;
}

static int generic_invert_tuple(struct nf_conntrack_tuple *tuple,
			   const struct nf_conntrack_tuple *orig)
{
	memset(&tuple->src.u3, 0, sizeof(tuple->src.u3));
	memset(&tuple->dst.u3, 0, sizeof(tuple->dst.u3));

	return 1;
}

static int generic_print_tuple(struct seq_file *s,
			    const struct nf_conntrack_tuple *tuple)
{
	return 0;
}

static int generic_print_conntrack(struct seq_file *s,
				const struct nf_conn *conntrack)
{
	return 0;
}

static int
generic_prepare(struct sk_buff **pskb, unsigned int hooknum,
		unsigned int *dataoff, u_int8_t *protonum)
{
	/* Never track !!! */
	return -NF_ACCEPT;
}


static u_int32_t generic_get_features(const struct nf_conntrack_tuple *tuple)
				
{
	return NF_CT_F_BASIC;
}

struct nf_conntrack_l3proto nf_conntrack_generic_l3proto = {
	.l3proto	 = PF_UNSPEC,
	.name		 = "unknown",
	.pkt_to_tuple	 = generic_pkt_to_tuple,
	.invert_tuple	 = generic_invert_tuple,
	.print_tuple	 = generic_print_tuple,
	.print_conntrack = generic_print_conntrack,
	.prepare	 = generic_prepare,
	.get_features	 = generic_get_features,
};
