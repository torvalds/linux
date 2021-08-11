// SPDX-License-Identifier: GPL-2.0-only
/*
 *  ebtable_broute
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  April, 2002
 *
 *  This table lets you choose between routing and bridging for frames
 *  entering on a bridge enslaved nic. This table is traversed before any
 *  other ebtables table. See net/bridge/br_input.c.
 */

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/module.h>
#include <linux/if_bridge.h>

#include "../br_private.h"

/* EBT_ACCEPT means the frame will be bridged
 * EBT_DROP means the frame will be routed
 */
static struct ebt_entries initial_chain = {
	.name		= "BROUTING",
	.policy		= EBT_ACCEPT,
};

static struct ebt_replace_kernel initial_table = {
	.name		= "broute",
	.valid_hooks	= 1 << NF_BR_BROUTING,
	.entries_size	= sizeof(struct ebt_entries),
	.hook_entry	= {
		[NF_BR_BROUTING]	= &initial_chain,
	},
	.entries	= (char *)&initial_chain,
};

static int check(const struct ebt_table_info *info, unsigned int valid_hooks)
{
	if (valid_hooks & ~(1 << NF_BR_BROUTING))
		return -EINVAL;
	return 0;
}

static const struct ebt_table broute_table = {
	.name		= "broute",
	.table		= &initial_table,
	.valid_hooks	= 1 << NF_BR_BROUTING,
	.check		= check,
	.me		= THIS_MODULE,
};

static unsigned int ebt_broute(void *priv, struct sk_buff *skb,
			       const struct nf_hook_state *s)
{
	struct net_bridge_port *p = br_port_get_rcu(skb->dev);
	struct nf_hook_state state;
	unsigned char *dest;
	int ret;

	if (!p || p->state != BR_STATE_FORWARDING)
		return NF_ACCEPT;

	nf_hook_state_init(&state, NF_BR_BROUTING,
			   NFPROTO_BRIDGE, s->in, NULL, NULL,
			   s->net, NULL);

	ret = ebt_do_table(skb, &state, priv);
	if (ret != NF_DROP)
		return ret;

	/* DROP in ebtables -t broute means that the
	 * skb should be routed, not bridged.
	 * This is awkward, but can't be changed for compatibility
	 * reasons.
	 *
	 * We map DROP to ACCEPT and set the ->br_netfilter_broute flag.
	 */
	BR_INPUT_SKB_CB(skb)->br_netfilter_broute = 1;

	/* undo PACKET_HOST mangling done in br_input in case the dst
	 * address matches the logical bridge but not the port.
	 */
	dest = eth_hdr(skb)->h_dest;
	if (skb->pkt_type == PACKET_HOST &&
	    !ether_addr_equal(skb->dev->dev_addr, dest) &&
	     ether_addr_equal(p->br->dev->dev_addr, dest))
		skb->pkt_type = PACKET_OTHERHOST;

	return NF_ACCEPT;
}

static const struct nf_hook_ops ebt_ops_broute = {
	.hook		= ebt_broute,
	.pf		= NFPROTO_BRIDGE,
	.hooknum	= NF_BR_PRE_ROUTING,
	.priority	= NF_BR_PRI_FIRST,
};

static int broute_table_init(struct net *net)
{
	return ebt_register_table(net, &broute_table, &ebt_ops_broute);
}

static void __net_exit broute_net_pre_exit(struct net *net)
{
	ebt_unregister_table_pre_exit(net, "broute");
}

static void __net_exit broute_net_exit(struct net *net)
{
	ebt_unregister_table(net, "broute");
}

static struct pernet_operations broute_net_ops = {
	.exit = broute_net_exit,
	.pre_exit = broute_net_pre_exit,
};

static int __init ebtable_broute_init(void)
{
	int ret = ebt_register_template(&broute_table, broute_table_init);

	if (ret)
		return ret;

	ret = register_pernet_subsys(&broute_net_ops);
	if (ret) {
		ebt_unregister_template(&broute_table);
		return ret;
	}

	return 0;
}

static void __exit ebtable_broute_fini(void)
{
	unregister_pernet_subsys(&broute_net_ops);
	ebt_unregister_template(&broute_table);
}

module_init(ebtable_broute_init);
module_exit(ebtable_broute_fini);
MODULE_LICENSE("GPL");
