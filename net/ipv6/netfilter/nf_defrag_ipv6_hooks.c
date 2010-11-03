/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/ipv6.h>
#include <linux/in6.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/ipv6.h>
#include <net/inet_frag.h>

#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_bridge.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/ipv6/nf_conntrack_ipv6.h>
#include <net/netfilter/ipv6/nf_defrag_ipv6.h>

static enum ip6_defrag_users nf_ct6_defrag_user(unsigned int hooknum,
						struct sk_buff *skb)
{
	u16 zone = NF_CT_DEFAULT_ZONE;

	if (skb->nfct)
		zone = nf_ct_zone((struct nf_conn *)skb->nfct);

#ifdef CONFIG_BRIDGE_NETFILTER
	if (skb->nf_bridge &&
	    skb->nf_bridge->mask & BRNF_NF_BRIDGE_PREROUTING)
		return IP6_DEFRAG_CONNTRACK_BRIDGE_IN + zone;
#endif
	if (hooknum == NF_INET_PRE_ROUTING)
		return IP6_DEFRAG_CONNTRACK_IN + zone;
	else
		return IP6_DEFRAG_CONNTRACK_OUT + zone;

}

static unsigned int ipv6_defrag(unsigned int hooknum,
				struct sk_buff *skb,
				const struct net_device *in,
				const struct net_device *out,
				int (*okfn)(struct sk_buff *))
{
	struct sk_buff *reasm;

	/* Previously seen (loopback)?	*/
	if (skb->nfct && !nf_ct_is_template((struct nf_conn *)skb->nfct))
		return NF_ACCEPT;

	reasm = nf_ct_frag6_gather(skb, nf_ct6_defrag_user(hooknum, skb));
	/* queued */
	if (reasm == NULL)
		return NF_STOLEN;

	/* error occured or not fragmented */
	if (reasm == skb)
		return NF_ACCEPT;

	nf_ct_frag6_output(hooknum, reasm, (struct net_device *)in,
			   (struct net_device *)out, okfn);

	return NF_STOLEN;
}

static struct nf_hook_ops ipv6_defrag_ops[] = {
	{
		.hook		= ipv6_defrag,
		.owner		= THIS_MODULE,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP6_PRI_CONNTRACK_DEFRAG,
	},
	{
		.hook		= ipv6_defrag,
		.owner		= THIS_MODULE,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_CONNTRACK_DEFRAG,
	},
};

static int __init nf_defrag_init(void)
{
	int ret = 0;

	ret = nf_ct_frag6_init();
	if (ret < 0) {
		pr_err("nf_defrag_ipv6: can't initialize frag6.\n");
		return ret;
	}
	ret = nf_register_hooks(ipv6_defrag_ops, ARRAY_SIZE(ipv6_defrag_ops));
	if (ret < 0) {
		pr_err("nf_defrag_ipv6: can't register hooks\n");
		goto cleanup_frag6;
	}
	return ret;

cleanup_frag6:
	nf_ct_frag6_cleanup();
	return ret;

}

static void __exit nf_defrag_fini(void)
{
	nf_unregister_hooks(ipv6_defrag_ops, ARRAY_SIZE(ipv6_defrag_ops));
	nf_ct_frag6_cleanup();
}

void nf_defrag_ipv6_enable(void)
{
}
EXPORT_SYMBOL_GPL(nf_defrag_ipv6_enable);

module_init(nf_defrag_init);
module_exit(nf_defrag_fini);

MODULE_LICENSE("GPL");
