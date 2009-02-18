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

/* EBT_ACCEPT means the frame will be bridged
 * EBT_DROP means the frame will be routed
 */
static struct ebt_entries initial_chain = {
	.name		= "BROUTING",
	.policy		= EBT_ACCEPT,
};

static struct ebt_replace_kernel initial_table =
{
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

static struct ebt_table broute_table =
{
	.name		= "broute",
	.table		= &initial_table,
	.valid_hooks	= 1 << NF_BR_BROUTING,
	.check		= check,
	.me		= THIS_MODULE,
};

static int ebt_broute(struct sk_buff *skb)
{
	int ret;

	ret = ebt_do_table(NF_BR_BROUTING, skb, skb->dev, NULL,
			   dev_net(skb->dev)->xt.broute_table);
	if (ret == NF_DROP)
		return 1; /* route it */
	return 0; /* bridge it */
}

static int __net_init broute_net_init(struct net *net)
{
	net->xt.broute_table = ebt_register_table(net, &broute_table);
	if (IS_ERR(net->xt.broute_table))
		return PTR_ERR(net->xt.broute_table);
	return 0;
}

static void __net_exit broute_net_exit(struct net *net)
{
	ebt_unregister_table(net->xt.broute_table);
}

static struct pernet_operations broute_net_ops = {
	.init = broute_net_init,
	.exit = broute_net_exit,
};

static int __init ebtable_broute_init(void)
{
	int ret;

	ret = register_pernet_subsys(&broute_net_ops);
	if (ret < 0)
		return ret;
	/* see br_input.c */
	rcu_assign_pointer(br_should_route_hook, ebt_broute);
	return 0;
}

static void __exit ebtable_broute_fini(void)
{
	rcu_assign_pointer(br_should_route_hook, NULL);
	synchronize_net();
	unregister_pernet_subsys(&broute_net_ops);
}

module_init(ebtable_broute_init);
module_exit(ebtable_broute_fini);
MODULE_LICENSE("GPL");
