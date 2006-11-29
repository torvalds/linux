/* IRC extension for TCP NAT alteration.
 * (C) 2000-2001 by Harald Welte <laforge@gnumonks.org>
 * (C) 2004 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 * based on a copy of RR's ip_nat_ftp.c
 *
 * ip_nat_irc.c,v 1.16 2001/12/06 07:42:10 laforge Exp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/kernel.h>
#include <net/tcp.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_conntrack_irc.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/moduleparam.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

MODULE_AUTHOR("Harald Welte <laforge@gnumonks.org>");
MODULE_DESCRIPTION("IRC (DCC) NAT helper");
MODULE_LICENSE("GPL");

static unsigned int help(struct sk_buff **pskb,
			 enum ip_conntrack_info ctinfo,
			 unsigned int matchoff,
			 unsigned int matchlen,
			 struct ip_conntrack_expect *exp)
{
	u_int16_t port;
	unsigned int ret;

	/* "4294967296 65635 " */
	char buffer[18];

	DEBUGP("IRC_NAT: info (seq %u + %u) in %u\n",
	       expect->seq, exp_irc_info->len,
	       ntohl(tcph->seq));

	/* Reply comes from server. */
	exp->saved_proto.tcp.port = exp->tuple.dst.u.tcp.port;
	exp->dir = IP_CT_DIR_REPLY;

	/* When you see the packet, we need to NAT it the same as the
	 * this one. */
	exp->expectfn = ip_nat_follow_master;

	/* Try to get same port: if not, try to change it. */
	for (port = ntohs(exp->saved_proto.tcp.port); port != 0; port++) {
		exp->tuple.dst.u.tcp.port = htons(port);
		if (ip_conntrack_expect_related(exp) == 0)
			break;
	}

	if (port == 0)
		return NF_DROP;

	/*      strlen("\1DCC CHAT chat AAAAAAAA P\1\n")=27
	 *      strlen("\1DCC SCHAT chat AAAAAAAA P\1\n")=28
	 *      strlen("\1DCC SEND F AAAAAAAA P S\1\n")=26
	 *      strlen("\1DCC MOVE F AAAAAAAA P S\1\n")=26
	 *      strlen("\1DCC TSEND F AAAAAAAA P S\1\n")=27
	 *              AAAAAAAAA: bound addr (1.0.0.0==16777216, min 8 digits,
	 *                      255.255.255.255==4294967296, 10 digits)
	 *              P:         bound port (min 1 d, max 5d (65635))
	 *              F:         filename   (min 1 d )
	 *              S:         size       (min 1 d )
	 *              0x01, \n:  terminators
	 */

	/* AAA = "us", ie. where server normally talks to. */
	sprintf(buffer, "%u %u",
		ntohl(exp->master->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip),
		port);
	DEBUGP("ip_nat_irc: Inserting '%s' == %u.%u.%u.%u, port %u\n",
	       buffer, NIPQUAD(exp->tuple.src.ip), port);

	ret = ip_nat_mangle_tcp_packet(pskb, exp->master, ctinfo, 
				       matchoff, matchlen, buffer, 
				       strlen(buffer));
	if (ret != NF_ACCEPT)
		ip_conntrack_unexpect_related(exp);
	return ret;
}

static void __exit ip_nat_irc_fini(void)
{
	rcu_assign_pointer(ip_nat_irc_hook, NULL);
	synchronize_rcu();
}

static int __init ip_nat_irc_init(void)
{
	BUG_ON(rcu_dereference(ip_nat_irc_hook));
	rcu_assign_pointer(ip_nat_irc_hook, help);
	return 0;
}

/* Prior to 2.6.11, we had a ports param.  No longer, but don't break users. */
static int warn_set(const char *val, struct kernel_param *kp)
{
	printk(KERN_INFO KBUILD_MODNAME
	       ": kernel >= 2.6.10 only uses 'ports' for conntrack modules\n");
	return 0;
}
module_param_call(ports, warn_set, NULL, NULL, 0);

module_init(ip_nat_irc_init);
module_exit(ip_nat_irc_fini);
