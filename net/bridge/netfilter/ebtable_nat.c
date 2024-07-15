// SPDX-License-Identifier: GPL-2.0-only
/*
 *  ebtable_nat
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  April, 2002
 *
 */

#include <linux/netfilter_bridge/ebtables.h>
#include <uapi/linux/netfilter_bridge.h>
#include <linux/module.h>

#define NAT_VALID_HOOKS ((1 << NF_BR_PRE_ROUTING) | (1 << NF_BR_LOCAL_OUT) | \
			 (1 << NF_BR_POST_ROUTING))

static struct ebt_entries initial_chains[] = {
	{
		.name	= "PREROUTING",
		.policy	= EBT_ACCEPT,
	},
	{
		.name	= "OUTPUT",
		.policy	= EBT_ACCEPT,
	},
	{
		.name	= "POSTROUTING",
		.policy	= EBT_ACCEPT,
	}
};

static struct ebt_replace_kernel initial_table = {
	.name		= "nat",
	.valid_hooks	= NAT_VALID_HOOKS,
	.entries_size	= 3 * sizeof(struct ebt_entries),
	.hook_entry	= {
		[NF_BR_PRE_ROUTING]	= &initial_chains[0],
		[NF_BR_LOCAL_OUT]	= &initial_chains[1],
		[NF_BR_POST_ROUTING]	= &initial_chains[2],
	},
	.entries	= (char *)initial_chains,
};

static const struct ebt_table frame_nat = {
	.name		= "nat",
	.table		= &initial_table,
	.valid_hooks	= NAT_VALID_HOOKS,
	.me		= THIS_MODULE,
};

static const struct nf_hook_ops ebt_ops_nat[] = {
	{
		.hook		= ebt_do_table,
		.pf		= NFPROTO_BRIDGE,
		.hooknum	= NF_BR_LOCAL_OUT,
		.priority	= NF_BR_PRI_NAT_DST_OTHER,
	},
	{
		.hook		= ebt_do_table,
		.pf		= NFPROTO_BRIDGE,
		.hooknum	= NF_BR_POST_ROUTING,
		.priority	= NF_BR_PRI_NAT_SRC,
	},
	{
		.hook		= ebt_do_table,
		.pf		= NFPROTO_BRIDGE,
		.hooknum	= NF_BR_PRE_ROUTING,
		.priority	= NF_BR_PRI_NAT_DST_BRIDGED,
	},
};

static int frame_nat_table_init(struct net *net)
{
	return ebt_register_table(net, &frame_nat, ebt_ops_nat);
}

static void __net_exit frame_nat_net_pre_exit(struct net *net)
{
	ebt_unregister_table_pre_exit(net, "nat");
}

static void __net_exit frame_nat_net_exit(struct net *net)
{
	ebt_unregister_table(net, "nat");
}

static struct pernet_operations frame_nat_net_ops = {
	.exit = frame_nat_net_exit,
	.pre_exit = frame_nat_net_pre_exit,
};

static int __init ebtable_nat_init(void)
{
	int ret = ebt_register_template(&frame_nat, frame_nat_table_init);

	if (ret)
		return ret;

	ret = register_pernet_subsys(&frame_nat_net_ops);
	if (ret) {
		ebt_unregister_template(&frame_nat);
		return ret;
	}

	return ret;
}

static void __exit ebtable_nat_fini(void)
{
	unregister_pernet_subsys(&frame_nat_net_ops);
	ebt_unregister_template(&frame_nat);
}

module_init(ebtable_nat_init);
module_exit(ebtable_nat_fini);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ebtables legacy stateless nat table");
