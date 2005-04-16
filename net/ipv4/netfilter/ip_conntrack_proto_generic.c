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
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>

unsigned long ip_ct_generic_timeout = 600*HZ;

static int generic_pkt_to_tuple(const struct sk_buff *skb,
				unsigned int dataoff,
				struct ip_conntrack_tuple *tuple)
{
	tuple->src.u.all = 0;
	tuple->dst.u.all = 0;

	return 1;
}

static int generic_invert_tuple(struct ip_conntrack_tuple *tuple,
				const struct ip_conntrack_tuple *orig)
{
	tuple->src.u.all = 0;
	tuple->dst.u.all = 0;

	return 1;
}

/* Print out the per-protocol part of the tuple. */
static int generic_print_tuple(struct seq_file *s,
			       const struct ip_conntrack_tuple *tuple)
{
	return 0;
}

/* Print out the private part of the conntrack. */
static int generic_print_conntrack(struct seq_file *s,
				   const struct ip_conntrack *state)
{
	return 0;
}

/* Returns verdict for packet, or -1 for invalid. */
static int packet(struct ip_conntrack *conntrack,
		  const struct sk_buff *skb,
		  enum ip_conntrack_info ctinfo)
{
	ip_ct_refresh_acct(conntrack, ctinfo, skb, ip_ct_generic_timeout);
	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int new(struct ip_conntrack *conntrack, const struct sk_buff *skb)
{
	return 1;
}

struct ip_conntrack_protocol ip_conntrack_generic_protocol =
{
	.proto			= 0,
	.name			= "unknown",
	.pkt_to_tuple		= generic_pkt_to_tuple,
	.invert_tuple		= generic_invert_tuple,
	.print_tuple		= generic_print_tuple,
	.print_conntrack	= generic_print_conntrack,
	.packet			= packet,
	.new			= new,
};
