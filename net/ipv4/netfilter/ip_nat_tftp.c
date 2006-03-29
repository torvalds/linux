/* (C) 2001-2002 Magnus Boden <mb@ozaba.mine.nu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Version: 0.0.7
 *
 * Thu 21 Mar 2002 Harald Welte <laforge@gnumonks.org>
 * 	- Port to newnat API
 *
 * This module currently supports DNAT:
 * iptables -t nat -A PREROUTING -d x.x.x.x -j DNAT --to-dest x.x.x.y
 *
 * and SNAT:
 * iptables -t nat -A POSTROUTING { -j MASQUERADE , -j SNAT --to-source x.x.x.x }
 *
 * It has not been tested with
 * -j SNAT --to-source x.x.x.x-x.x.x.y since I only have one external ip
 * If you do test this please let me know if it works or not.
 *
 */

#include <linux/module.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_tftp.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/moduleparam.h>

MODULE_AUTHOR("Magnus Boden <mb@ozaba.mine.nu>");
MODULE_DESCRIPTION("tftp NAT helper");
MODULE_LICENSE("GPL");

static unsigned int help(struct sk_buff **pskb,
			 enum ip_conntrack_info ctinfo,
			 struct ip_conntrack_expect *exp)
{
	struct ip_conntrack *ct = exp->master;

	exp->saved_proto.udp.port
		= ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.udp.port;
	exp->dir = IP_CT_DIR_REPLY;
	exp->expectfn = ip_nat_follow_master;
	if (ip_conntrack_expect_related(exp) != 0)
		return NF_DROP;
	return NF_ACCEPT;
}

static void __exit ip_nat_tftp_fini(void)
{
	ip_nat_tftp_hook = NULL;
	/* Make sure noone calls it, meanwhile. */
	synchronize_net();
}

static int __init ip_nat_tftp_init(void)
{
	BUG_ON(ip_nat_tftp_hook);
	ip_nat_tftp_hook = help;
	return 0;
}

module_init(ip_nat_tftp_init);
module_exit(ip_nat_tftp_fini);
