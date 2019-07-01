// SPDX-License-Identifier: GPL-2.0-only
/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 */

#include <linux/types.h>
#include <linux/ipv6.h>
#include <linux/in6.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/ipv6_frag.h>

#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_bridge.h>
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/ipv6/nf_conntrack_ipv6.h>
#endif
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/ipv6/nf_defrag_ipv6.h>

static DEFINE_MUTEX(defrag6_mutex);

static enum ip6_defrag_users nf_ct6_defrag_user(unsigned int hooknum,
						struct sk_buff *skb)
{
	u16 zone_id = NF_CT_DEFAULT_ZONE_ID;
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	if (skb_nfct(skb)) {
		enum ip_conntrack_info ctinfo;
		const struct nf_conn *ct = nf_ct_get(skb, &ctinfo);

		zone_id = nf_ct_zone_id(nf_ct_zone(ct), CTINFO2DIR(ctinfo));
	}
#endif
	if (nf_bridge_in_prerouting(skb))
		return IP6_DEFRAG_CONNTRACK_BRIDGE_IN + zone_id;

	if (hooknum == NF_INET_PRE_ROUTING)
		return IP6_DEFRAG_CONNTRACK_IN + zone_id;
	else
		return IP6_DEFRAG_CONNTRACK_OUT + zone_id;
}

static unsigned int ipv6_defrag(void *priv,
				struct sk_buff *skb,
				const struct nf_hook_state *state)
{
	int err;

#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	/* Previously seen (loopback)?	*/
	if (skb_nfct(skb) && !nf_ct_is_template((struct nf_conn *)skb_nfct(skb)))
		return NF_ACCEPT;

	if (skb->_nfct == IP_CT_UNTRACKED)
		return NF_ACCEPT;
#endif

	err = nf_ct_frag6_gather(state->net, skb,
				 nf_ct6_defrag_user(state->hook, skb));
	/* queued */
	if (err == -EINPROGRESS)
		return NF_STOLEN;

	return err == 0 ? NF_ACCEPT : NF_DROP;
}

static const struct nf_hook_ops ipv6_defrag_ops[] = {
	{
		.hook		= ipv6_defrag,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP6_PRI_CONNTRACK_DEFRAG,
	},
	{
		.hook		= ipv6_defrag,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_CONNTRACK_DEFRAG,
	},
};

static void __net_exit defrag6_net_exit(struct net *net)
{
	if (net->nf.defrag_ipv6) {
		nf_unregister_net_hooks(net, ipv6_defrag_ops,
					ARRAY_SIZE(ipv6_defrag_ops));
		net->nf.defrag_ipv6 = false;
	}
}

static struct pernet_operations defrag6_net_ops = {
	.exit = defrag6_net_exit,
};

static int __init nf_defrag_init(void)
{
	int ret = 0;

	ret = nf_ct_frag6_init();
	if (ret < 0) {
		pr_err("nf_defrag_ipv6: can't initialize frag6.\n");
		return ret;
	}
	ret = register_pernet_subsys(&defrag6_net_ops);
	if (ret < 0) {
		pr_err("nf_defrag_ipv6: can't register pernet ops\n");
		goto cleanup_frag6;
	}
	return ret;

cleanup_frag6:
	nf_ct_frag6_cleanup();
	return ret;

}

static void __exit nf_defrag_fini(void)
{
	unregister_pernet_subsys(&defrag6_net_ops);
	nf_ct_frag6_cleanup();
}

int nf_defrag_ipv6_enable(struct net *net)
{
	int err = 0;

	might_sleep();

	if (net->nf.defrag_ipv6)
		return 0;

	mutex_lock(&defrag6_mutex);
	if (net->nf.defrag_ipv6)
		goto out_unlock;

	err = nf_register_net_hooks(net, ipv6_defrag_ops,
				    ARRAY_SIZE(ipv6_defrag_ops));
	if (err == 0)
		net->nf.defrag_ipv6 = true;

 out_unlock:
	mutex_unlock(&defrag6_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(nf_defrag_ipv6_enable);

module_init(nf_defrag_init);
module_exit(nf_defrag_fini);

MODULE_LICENSE("GPL");
