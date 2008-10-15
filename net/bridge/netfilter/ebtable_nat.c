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
#include <linux/module.h>

#define NAT_VALID_HOOKS ((1 << NF_BR_PRE_ROUTING) | (1 << NF_BR_LOCAL_OUT) | \
   (1 << NF_BR_POST_ROUTING))

static struct ebt_entries initial_chains[] =
{
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

static struct ebt_replace_kernel initial_table =
{
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

static int check(const struct ebt_table_info *info, unsigned int valid_hooks)
{
	if (valid_hooks & ~NAT_VALID_HOOKS)
		return -EINVAL;
	return 0;
}

static struct ebt_table frame_nat =
{
	.name		= "nat",
	.table		= &initial_table,
	.valid_hooks	= NAT_VALID_HOOKS,
	.lock		= __RW_LOCK_UNLOCKED(frame_nat.lock),
	.check		= check,
	.me		= THIS_MODULE,
};

static unsigned int
ebt_nat_dst(unsigned int hook, struct sk_buff *skb, const struct net_device *in
   , const struct net_device *out, int (*okfn)(struct sk_buff *))
{
	return ebt_do_table(hook, skb, in, out, &frame_nat);
}

static unsigned int
ebt_nat_src(unsigned int hook, struct sk_buff *skb, const struct net_device *in
   , const struct net_device *out, int (*okfn)(struct sk_buff *))
{
	return ebt_do_table(hook, skb, in, out, &frame_nat);
}

static struct nf_hook_ops ebt_ops_nat[] __read_mostly = {
	{
		.hook		= ebt_nat_dst,
		.owner		= THIS_MODULE,
		.pf		= PF_BRIDGE,
		.hooknum	= NF_BR_LOCAL_OUT,
		.priority	= NF_BR_PRI_NAT_DST_OTHER,
	},
	{
		.hook		= ebt_nat_src,
		.owner		= THIS_MODULE,
		.pf		= PF_BRIDGE,
		.hooknum	= NF_BR_POST_ROUTING,
		.priority	= NF_BR_PRI_NAT_SRC,
	},
	{
		.hook		= ebt_nat_dst,
		.owner		= THIS_MODULE,
		.pf		= PF_BRIDGE,
		.hooknum	= NF_BR_PRE_ROUTING,
		.priority	= NF_BR_PRI_NAT_DST_BRIDGED,
	},
};

static int __init ebtable_nat_init(void)
{
	int ret;

	ret = ebt_register_table(&frame_nat);
	if (ret < 0)
		return ret;
	ret = nf_register_hooks(ebt_ops_nat, ARRAY_SIZE(ebt_ops_nat));
	if (ret < 0)
		ebt_unregister_table(&frame_nat);
	return ret;
}

static void __exit ebtable_nat_fini(void)
{
	nf_unregister_hooks(ebt_ops_nat, ARRAY_SIZE(ebt_ops_nat));
	ebt_unregister_table(&frame_nat);
}

module_init(ebtable_nat_init);
module_exit(ebtable_nat_fini);
MODULE_LICENSE("GPL");
