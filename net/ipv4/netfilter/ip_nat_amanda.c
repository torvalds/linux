/* Amanda extension for TCP NAT alteration.
 * (C) 2002 by Brian J. Murrell <netfilter@interlinx.bc.ca>
 * based on a copy of HW's ip_nat_irc.c as well as other modules
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	Module load syntax:
 * 	insmod ip_nat_amanda.o
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <net/tcp.h>
#include <net/udp.h>

#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_amanda.h>


MODULE_AUTHOR("Brian J. Murrell <netfilter@interlinx.bc.ca>");
MODULE_DESCRIPTION("Amanda NAT helper");
MODULE_LICENSE("GPL");

static unsigned int help(struct sk_buff **pskb,
			 enum ip_conntrack_info ctinfo,
			 unsigned int matchoff,
			 unsigned int matchlen,
			 struct ip_conntrack_expect *exp)
{
	char buffer[sizeof("65535")];
	u_int16_t port;
	unsigned int ret;

	/* Connection comes from client. */
	exp->saved_proto.tcp.port = exp->tuple.dst.u.tcp.port;
	exp->dir = IP_CT_DIR_ORIGINAL;

	/* When you see the packet, we need to NAT it the same as the
	 * this one (ie. same IP: it will be TCP and master is UDP). */
	exp->expectfn = ip_nat_follow_master;

	/* Try to get same port: if not, try to change it. */
	for (port = ntohs(exp->saved_proto.tcp.port); port != 0; port++) {
		exp->tuple.dst.u.tcp.port = htons(port);
		if (ip_conntrack_expect_related(exp) == 0)
			break;
	}

	if (port == 0)
		return NF_DROP;

	sprintf(buffer, "%u", port);
	ret = ip_nat_mangle_udp_packet(pskb, exp->master, ctinfo,
				       matchoff, matchlen,
				       buffer, strlen(buffer));
	if (ret != NF_ACCEPT)
		ip_conntrack_unexpect_related(exp);
	return ret;
}

static void __exit ip_nat_amanda_fini(void)
{
	ip_nat_amanda_hook = NULL;
	/* Make sure noone calls it, meanwhile. */
	synchronize_net();
}

static int __init ip_nat_amanda_init(void)
{
	BUG_ON(ip_nat_amanda_hook);
	ip_nat_amanda_hook = help;
	return 0;
}

module_init(ip_nat_amanda_init);
module_exit(ip_nat_amanda_fini);
