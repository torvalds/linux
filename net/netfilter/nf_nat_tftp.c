// SPDX-License-Identifier: GPL-2.0-only
/* (C) 2001-2002 Magnus Boden <mb@ozaba.mine.nu>
 */

#include <linux/module.h>
#include <linux/udp.h>

#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_nat_helper.h>
#include <linux/netfilter/nf_conntrack_tftp.h>

#define NAT_HELPER_NAME "tftp"

MODULE_AUTHOR("Magnus Boden <mb@ozaba.mine.nu>");
MODULE_DESCRIPTION("TFTP NAT helper");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NF_NAT_HELPER(NAT_HELPER_NAME);

static struct nf_conntrack_nat_helper nat_helper_tftp =
	NF_CT_NAT_HELPER_INIT(NAT_HELPER_NAME);

static unsigned int help(struct sk_buff *skb,
			 enum ip_conntrack_info ctinfo,
			 struct nf_conntrack_expect *exp)
{
	const struct nf_conn *ct = exp->master;

	exp->saved_proto.udp.port
		= ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.udp.port;
	exp->dir = IP_CT_DIR_REPLY;
	exp->expectfn = nf_nat_follow_master;
	if (nf_ct_expect_related(exp) != 0) {
		nf_ct_helper_log(skb, exp->master, "cannot add expectation");
		return NF_DROP;
	}
	return NF_ACCEPT;
}

static void __exit nf_nat_tftp_fini(void)
{
	nf_nat_helper_unregister(&nat_helper_tftp);
	RCU_INIT_POINTER(nf_nat_tftp_hook, NULL);
	synchronize_rcu();
}

static int __init nf_nat_tftp_init(void)
{
	BUG_ON(nf_nat_tftp_hook != NULL);
	nf_nat_helper_register(&nat_helper_tftp);
	RCU_INIT_POINTER(nf_nat_tftp_hook, help);
	return 0;
}

module_init(nf_nat_tftp_init);
module_exit(nf_nat_tftp_fini);
