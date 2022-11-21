// SPDX-License-Identifier: GPL-2.0-only
/*
 *  ebtable_filter
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

#define FILTER_VALID_HOOKS ((1 << NF_BR_LOCAL_IN) | (1 << NF_BR_FORWARD) | \
			    (1 << NF_BR_LOCAL_OUT))

static struct ebt_entries initial_chains[] = {
	{
		.name	= "INPUT",
		.policy	= EBT_ACCEPT,
	},
	{
		.name	= "FORWARD",
		.policy	= EBT_ACCEPT,
	},
	{
		.name	= "OUTPUT",
		.policy	= EBT_ACCEPT,
	},
};

static struct ebt_replace_kernel initial_table = {
	.name		= "filter",
	.valid_hooks	= FILTER_VALID_HOOKS,
	.entries_size	= 3 * sizeof(struct ebt_entries),
	.hook_entry	= {
		[NF_BR_LOCAL_IN]	= &initial_chains[0],
		[NF_BR_FORWARD]		= &initial_chains[1],
		[NF_BR_LOCAL_OUT]	= &initial_chains[2],
	},
	.entries	= (char *)initial_chains,
};

static int check(const struct ebt_table_info *info, unsigned int valid_hooks)
{
	if (valid_hooks & ~FILTER_VALID_HOOKS)
		return -EINVAL;
	return 0;
}

static const struct ebt_table frame_filter = {
	.name		= "filter",
	.table		= &initial_table,
	.valid_hooks	= FILTER_VALID_HOOKS,
	.check		= check,
	.me		= THIS_MODULE,
};

static const struct nf_hook_ops ebt_ops_filter[] = {
	{
		.hook		= ebt_do_table,
		.pf		= NFPROTO_BRIDGE,
		.hooknum	= NF_BR_LOCAL_IN,
		.priority	= NF_BR_PRI_FILTER_BRIDGED,
	},
	{
		.hook		= ebt_do_table,
		.pf		= NFPROTO_BRIDGE,
		.hooknum	= NF_BR_FORWARD,
		.priority	= NF_BR_PRI_FILTER_BRIDGED,
	},
	{
		.hook		= ebt_do_table,
		.pf		= NFPROTO_BRIDGE,
		.hooknum	= NF_BR_LOCAL_OUT,
		.priority	= NF_BR_PRI_FILTER_OTHER,
	},
};

static int frame_filter_table_init(struct net *net)
{
	return ebt_register_table(net, &frame_filter, ebt_ops_filter);
}

static void __net_exit frame_filter_net_pre_exit(struct net *net)
{
	ebt_unregister_table_pre_exit(net, "filter");
}

static void __net_exit frame_filter_net_exit(struct net *net)
{
	ebt_unregister_table(net, "filter");
}

static struct pernet_operations frame_filter_net_ops = {
	.exit = frame_filter_net_exit,
	.pre_exit = frame_filter_net_pre_exit,
};

static int __init ebtable_filter_init(void)
{
	int ret = ebt_register_template(&frame_filter, frame_filter_table_init);

	if (ret)
		return ret;

	ret = register_pernet_subsys(&frame_filter_net_ops);
	if (ret) {
		ebt_unregister_template(&frame_filter);
		return ret;
	}

	return 0;
}

static void __exit ebtable_filter_fini(void)
{
	unregister_pernet_subsys(&frame_filter_net_ops);
	ebt_unregister_template(&frame_filter);
}

module_init(ebtable_filter_init);
module_exit(ebtable_filter_fini);
MODULE_LICENSE("GPL");
