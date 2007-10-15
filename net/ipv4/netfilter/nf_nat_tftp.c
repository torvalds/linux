/* (C) 2001-2002 Magnus Boden <mb@ozaba.mine.nu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/udp.h>

#include <net/netfilter/nf_nat_helper.h>
#include <net/netfilter/nf_nat_rule.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <linux/netfilter/nf_conntrack_tftp.h>

MODULE_AUTHOR("Magnus Boden <mb@ozaba.mine.nu>");
MODULE_DESCRIPTION("TFTP NAT helper");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ip_nat_tftp");

static unsigned int help(struct sk_buff *skb,
			 enum ip_conntrack_info ctinfo,
			 struct nf_conntrack_expect *exp)
{
	struct nf_conn *ct = exp->master;

	exp->saved_proto.udp.port
		= ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.udp.port;
	exp->dir = IP_CT_DIR_REPLY;
	exp->expectfn = nf_nat_follow_master;
	if (nf_ct_expect_related(exp) != 0)
		return NF_DROP;
	return NF_ACCEPT;
}

static void __exit nf_nat_tftp_fini(void)
{
	rcu_assign_pointer(nf_nat_tftp_hook, NULL);
	synchronize_rcu();
}

static int __init nf_nat_tftp_init(void)
{
	BUG_ON(rcu_dereference(nf_nat_tftp_hook));
	rcu_assign_pointer(nf_nat_tftp_hook, help);
	return 0;
}

module_init(nf_nat_tftp_init);
module_exit(nf_nat_tftp_fini);
