// SPDX-License-Identifier: GPL-2.0-or-later
/* Amanda extension for TCP NAT alteration.
 * (C) 2002 by Brian J. Murrell <netfilter@interlinx.bc.ca>
 * based on a copy of HW's ip_nat_irc.c as well as other modules
 * (C) 2006-2012 Patrick McHardy <kaber@trash.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/udp.h>

#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_nat_helper.h>
#include <linux/netfilter/nf_conntrack_amanda.h>

#define NAT_HELPER_NAME "amanda"

MODULE_AUTHOR("Brian J. Murrell <netfilter@interlinx.bc.ca>");
MODULE_DESCRIPTION("Amanda NAT helper");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NF_NAT_HELPER(NAT_HELPER_NAME);

static struct nf_conntrack_nat_helper nat_helper_amanda =
	NF_CT_NAT_HELPER_INIT(NAT_HELPER_NAME);

static unsigned int help(struct sk_buff *skb,
			 enum ip_conntrack_info ctinfo,
			 unsigned int protoff,
			 unsigned int matchoff,
			 unsigned int matchlen,
			 struct nf_conntrack_expect *exp)
{
	char buffer[sizeof("65535")];
	u_int16_t port;

	/* Connection comes from client. */
	exp->saved_proto.tcp.port = exp->tuple.dst.u.tcp.port;
	exp->dir = IP_CT_DIR_ORIGINAL;

	/* When you see the packet, we need to NAT it the same as the
	 * this one (ie. same IP: it will be TCP and master is UDP). */
	exp->expectfn = nf_nat_follow_master;

	/* Try to get same port: if not, try to change it. */
	port = nf_nat_exp_find_port(exp, ntohs(exp->saved_proto.tcp.port));
	if (port == 0) {
		nf_ct_helper_log(skb, exp->master, "all ports in use");
		return NF_DROP;
	}

	sprintf(buffer, "%u", port);
	if (!nf_nat_mangle_udp_packet(skb, exp->master, ctinfo,
				      protoff, matchoff, matchlen,
				      buffer, strlen(buffer))) {
		nf_ct_helper_log(skb, exp->master, "cannot mangle packet");
		nf_ct_unexpect_related(exp);
		return NF_DROP;
	}
	return NF_ACCEPT;
}

static void __exit nf_nat_amanda_fini(void)
{
	nf_nat_helper_unregister(&nat_helper_amanda);
	RCU_INIT_POINTER(nf_nat_amanda_hook, NULL);
	synchronize_rcu();
}

static int __init nf_nat_amanda_init(void)
{
	BUG_ON(nf_nat_amanda_hook != NULL);
	nf_nat_helper_register(&nat_helper_amanda);
	RCU_INIT_POINTER(nf_nat_amanda_hook, help);
	return 0;
}

module_init(nf_nat_amanda_init);
module_exit(nf_nat_amanda_fini);
