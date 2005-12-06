/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- enable working with L3 protocol independent connection tracking.
 *
 * Derived from net/ipv4/netfilter/ip_conntrack_proto_generic.c
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/netfilter.h>
#include <net/netfilter/nf_conntrack_protocol.h>

unsigned long nf_ct_generic_timeout = 600*HZ;

static int generic_pkt_to_tuple(const struct sk_buff *skb,
				unsigned int dataoff,
				struct nf_conntrack_tuple *tuple)
{
	tuple->src.u.all = 0;
	tuple->dst.u.all = 0;

	return 1;
}

static int generic_invert_tuple(struct nf_conntrack_tuple *tuple,
				const struct nf_conntrack_tuple *orig)
{
	tuple->src.u.all = 0;
	tuple->dst.u.all = 0;

	return 1;
}

/* Print out the per-protocol part of the tuple. */
static int generic_print_tuple(struct seq_file *s,
			       const struct nf_conntrack_tuple *tuple)
{
	return 0;
}

/* Print out the private part of the conntrack. */
static int generic_print_conntrack(struct seq_file *s,
				   const struct nf_conn *state)
{
	return 0;
}

/* Returns verdict for packet, or -1 for invalid. */
static int packet(struct nf_conn *conntrack,
		  const struct sk_buff *skb,
		  unsigned int dataoff,
		  enum ip_conntrack_info ctinfo,
		  int pf,
		  unsigned int hooknum)
{
	nf_ct_refresh_acct(conntrack, ctinfo, skb, nf_ct_generic_timeout);
	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int new(struct nf_conn *conntrack, const struct sk_buff *skb,
	       unsigned int dataoff)
{
	return 1;
}

struct nf_conntrack_protocol nf_conntrack_generic_protocol =
{
	.l3proto		= PF_UNSPEC,
	.proto			= 0,
	.name			= "unknown",
	.pkt_to_tuple		= generic_pkt_to_tuple,
	.invert_tuple		= generic_invert_tuple,
	.print_tuple		= generic_print_tuple,
	.print_conntrack	= generic_print_conntrack,
	.packet			= packet,
	.new			= new,
};
