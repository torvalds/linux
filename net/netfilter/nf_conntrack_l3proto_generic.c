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
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>

static int generic_get_l4proto(const struct sk_buff *skb, unsigned int nhoff,
			       unsigned int *dataoff, u_int8_t *protonum)
{
	/* Never track !!! */
	return -NF_ACCEPT;
}


struct nf_conntrack_l3proto nf_conntrack_l3proto_generic __read_mostly = {
	.l3proto	 = PF_UNSPEC,
	.get_l4proto	 = generic_get_l4proto,
};
EXPORT_SYMBOL_GPL(nf_conntrack_l3proto_generic);
