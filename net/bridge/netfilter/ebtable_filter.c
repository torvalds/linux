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
	.lock		= __RW_LOCK_UNLOCKED(frame_filter.lock),
	.check		= check,
	.me		= THIS_MODULE,
};

static unsigned int
ebt_hook(unsigned int hook, struct sk_buff *skb, const struct net_device *in,
   const struct net_device *out, int (*okfn)(struct sk_buff *))
{
	return ebt_do_table(hook, skb, in, out, &frame_filter);
}

static struct nf_hook_ops ebt_ops_filter[] __read_mostly = {
	{
		.hook		= ebt_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_BRIDGE,
		.hooknum	= NF_BR_LOCAL_IN,
		.priority	= NF_BR_PRI_FILTER_BRIDGED,
	},
	{
		.hook		= ebt_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_BRIDGE,
		.hooknum	= NF_BR_FORWARD,
		.priority	= NF_BR_PRI_FILTER_BRIDGED,
	},
	{
		.hook		= ebt_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_BRIDGE,
		.hooknum	= NF_BR_LOCAL_OUT,
		.priority	= NF_BR_PRI_FILTER_OTHER,
	},
};

static int __init ebtable_filter_init(void)
{
	int i, j, ret;

	ret = ebt_register_table(&frame_filter);
	if (ret < 0)
		return ret;
	for (i = 0; i < ARRAY_SIZE(ebt_ops_filter); i++)
		if ((ret = nf_register_hook(&ebt_ops_filter[i])) < 0)
			goto cleanup;
	return ret;
cleanup:
	for (j = 0; j < i; j++)
		nf_unregister_hook(&ebt_ops_filter[j]);
	ebt_unregister_table(&frame_filter);
	return ret;
}

static void __exit ebtable_filter_fini(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ebt_ops_filter); i++)
		nf_unregister_hook(&ebt_ops_filter[i]);
	ebt_unregister_table(&frame_filter);
}

module_init(ebtable_filter_init);
module_exit(ebtable_filter_fini);
MODULE_LICENSE("GPL");
