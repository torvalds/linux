// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      NetBIOS name service broadcast connection tracking helper
 *
 *      (c) 2005 Patrick McHardy <kaber@trash.net>
 */
/*
 *      This helper tracks locally originating NetBIOS name service
 *      requests by issuing permanent expectations (valid until
 *      timing out) matching all reply connections from the
 *      destination network. The only NetBIOS specific thing is
 *      actually the port number.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/in.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_expect.h>

#define HELPER_NAME	"netbios-ns"
#define NMBD_PORT	137

MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("NetBIOS name service broadcast connection tracking helper");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ip_conntrack_netbios_ns");
MODULE_ALIAS_NFCT_HELPER(HELPER_NAME);

static unsigned int timeout __read_mostly = 3;
module_param(timeout, uint, 0400);
MODULE_PARM_DESC(timeout, "timeout for master connection/replies in seconds");

static struct nf_conntrack_expect_policy exp_policy = {
	.max_expected	= 1,
};

static int netbios_ns_help(struct sk_buff *skb, unsigned int protoff,
			   struct nf_conn *ct,
			   enum ip_conntrack_info ctinfo)
{
	return nf_conntrack_broadcast_help(skb, ct, ctinfo, timeout);
}

static struct nf_conntrack_helper helper __read_mostly;
static struct nf_conntrack_helper *helper_ptr __read_mostly;

static int __init nf_conntrack_netbios_ns_init(void)
{
	NF_CT_HELPER_BUILD_BUG_ON(0);

	exp_policy.timeout = timeout;

	nf_ct_helper_init(&helper, AF_INET, IPPROTO_UDP, HELPER_NAME,
			  NMBD_PORT, NMBD_PORT, NMBD_PORT,
			  &exp_policy, 0, netbios_ns_help, NULL, THIS_MODULE);

	return nf_conntrack_helper_register(&helper, &helper_ptr);
}

static void __exit nf_conntrack_netbios_ns_fini(void)
{
	nf_conntrack_helper_unregister(helper_ptr);
}

module_init(nf_conntrack_netbios_ns_init);
module_exit(nf_conntrack_netbios_ns_fini);
