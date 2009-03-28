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
#include <linux/module.h>

#define FILTER_VALID_HOOKS ((1 << NF_BR_LOCAL_IN) | (1 << NF_BR_FORWARD) | \
   (1 << NF_BR_LOCAL_OUT))

static struct ebt_entries initial_chains[] =
{
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

static struct ebt_replace_kernel initial_table =
{
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

static struct ebt_table frame_filter =
{
	.name		= "filter",
	.table		= &initial_table,
	.valid_hooks	= FILTER_VALID_HOOKS,
	.check		= check,
	.me		= THIS_MODULE,
};

static unsigned int
ebt_in_hook(unsigned int hook, struct sk_buff *skb, const struct net_device *in,
   const struct net_device *out, int (*okfn)(struct sk_buff *))
{
	return ebt_do_table(hook, skb, in, out, dev_net(in)->xt.frame_filter);
}

static unsigned int
ebt_out_hook(unsigned int hook, struct sk_buff *skb, const struct net_device *in,
   const struct net_device *out, int (*okfn)(struct sk_buff *))
{
	return ebt_do_table(hook, skb, in, out, dev_net(out)->xt.frame_filter);
}

static struct nf_hook_ops ebt_ops_filter[] __read_mostly = {
	{
		.hook		= ebt_in_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_BRIDGE,
		.hooknum	= NF_BR_LOCAL_IN,
		.priority	= NF_BR_PRI_FILTER_BRIDGED,
	},
	{
		.hook		= ebt_in_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_BRIDGE,
		.hooknum	= NF_BR_FORWARD,
		.priority	= NF_BR_PRI_FILTER_BRIDGED,
	},
	{
		.hook		= ebt_out_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_BRIDGE,
		.hooknum	= NF_BR_LOCAL_OUT,
		.priority	= NF_BR_PRI_FILTER_OTHER,
	},
};

static int __net_init frame_filter_net_init(struct net *net)
{
	net->xt.frame_filter = ebt_register_table(net, &frame_filter);
	if (IS_ERR(net->xt.frame_filter))
		return PTR_ERR(net->xt.frame_filter);
	return 0;
}

static void __net_exit frame_filter_net_exit(struct net *net)
{
	ebt_unregister_table(net->xt.frame_filter);
}

static struct pernet_operations frame_filter_net_ops = {
	.init = frame_filter_net_init,
	.exit = frame_filter_net_exit,
};

static int __init ebtable_filter_init(void)
{
	int ret;

	ret = register_pernet_subsys(&frame_filter_net_ops);
	if (ret < 0)
		return ret;
	ret = nf_register_hooks(ebt_ops_filter, ARRAY_SIZE(ebt_ops_filter));
	if (ret < 0)
		unregister_pernet_subsys(&frame_filter_net_ops);
	return ret;
}

static void __exit ebtable_filter_fini(void)
{
	nf_unregister_hooks(ebt_ops_filter, ARRAY_SIZE(ebt_ops_filter));
	unregister_pernet_subsys(&frame_filter_net_ops);
}

module_init(ebtable_filter_init);
module_exit(ebtable_filter_fini);
MODULE_LICENSE("GPL");
