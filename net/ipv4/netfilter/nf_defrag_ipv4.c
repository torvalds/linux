/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/route.h>
#include <net/ip.h>

#include <linux/netfilter_bridge.h>
#include <linux/netfilter_ipv4.h>
#include <net/netfilter/ipv4/nf_defrag_ipv4.h>

/* Returns new sk_buff, or NULL */
static int nf_ct_ipv4_gather_frags(struct sk_buff *skb, u_int32_t user)
{
	int err;

	skb_orphan(skb);

	local_bh_disable();
	err = ip_defrag(skb, user);
	local_bh_enable();

	if (!err)
		ip_send_check(ip_hdr(skb));

	return err;
}

static enum ip_defrag_users nf_ct_defrag_user(unsigned int hooknum,
					      struct sk_buff *skb)
{
#ifdef CONFIG_BRIDGE_NETFILTER
	if (skb->nf_bridge &&
	    skb->nf_bridge->mask & BRNF_NF_BRIDGE_PREROUTING)
		return IP_DEFRAG_CONNTRACK_BRIDGE_IN;
#endif
	if (hooknum == NF_INET_PRE_ROUTING)
		return IP_DEFRAG_CONNTRACK_IN;
	else
		return IP_DEFRAG_CONNTRACK_OUT;
}

static unsigned int ipv4_conntrack_defrag(unsigned int hooknum,
					  struct sk_buff *skb,
					  const struct net_device *in,
					  const struct net_device *out,
					  int (*okfn)(struct sk_buff *))
{
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
#if !defined(CONFIG_NF_NAT) && !defined(CONFIG_NF_NAT_MODULE)
	/* Previously seen (loopback)?  Ignore.  Do this before
	   fragment check. */
	if (skb->nfct)
		return NF_ACCEPT;
#endif
#endif
	/* Gather fragments. */
	if (ip_hdr(skb)->frag_off & htons(IP_MF | IP_OFFSET)) {
		enum ip_defrag_users user = nf_ct_defrag_user(hooknum, skb);
		if (nf_ct_ipv4_gather_frags(skb, user))
			return NF_STOLEN;
	}
	return NF_ACCEPT;
}

static struct nf_hook_ops ipv4_defrag_ops[] = {
	{
		.hook		= ipv4_conntrack_defrag,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_DEFRAG,
	},
	{
		.hook           = ipv4_conntrack_defrag,
		.owner          = THIS_MODULE,
		.pf             = PF_INET,
		.hooknum        = NF_INET_LOCAL_OUT,
		.priority       = NF_IP_PRI_CONNTRACK_DEFRAG,
	},
};

static int __init nf_defrag_init(void)
{
	return nf_register_hooks(ipv4_defrag_ops, ARRAY_SIZE(ipv4_defrag_ops));
}

static void __exit nf_defrag_fini(void)
{
	nf_unregister_hooks(ipv4_defrag_ops, ARRAY_SIZE(ipv4_defrag_ops));
}

void nf_defrag_ipv4_enable(void)
{
}
EXPORT_SYMBOL_GPL(nf_defrag_ipv4_enable);

module_init(nf_defrag_init);
module_exit(nf_defrag_fini);

MODULE_LICENSE("GPL");
