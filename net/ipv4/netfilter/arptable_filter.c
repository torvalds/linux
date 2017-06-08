/*
 * Filtering ARP tables module.
 *
 * Copyright (C) 2002 David S. Miller (davem@redhat.com)
 *
 */

#include <linux/module.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_arp/arp_tables.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_DESCRIPTION("arptables filter table");

#define FILTER_VALID_HOOKS ((1 << NF_ARP_IN) | (1 << NF_ARP_OUT) | \
			   (1 << NF_ARP_FORWARD))

static int __net_init arptable_filter_table_init(struct net *net);

static const struct xt_table packet_filter = {
	.name		= "filter",
	.valid_hooks	= FILTER_VALID_HOOKS,
	.me		= THIS_MODULE,
	.af		= NFPROTO_ARP,
	.priority	= NF_IP_PRI_FILTER,
	.table_init	= arptable_filter_table_init,
};

/* The work comes in here from netfilter.c */
static unsigned int
arptable_filter_hook(void *priv, struct sk_buff *skb,
		     const struct nf_hook_state *state)
{
	return arpt_do_table(skb, state, state->net->ipv4.arptable_filter);
}

static struct nf_hook_ops *arpfilter_ops __read_mostly;

static int __net_init arptable_filter_table_init(struct net *net)
{
	struct arpt_replace *repl;
	int err;

	if (net->ipv4.arptable_filter)
		return 0;

	repl = arpt_alloc_initial_table(&packet_filter);
	if (repl == NULL)
		return -ENOMEM;
	err = arpt_register_table(net, &packet_filter, repl, arpfilter_ops,
				  &net->ipv4.arptable_filter);
	kfree(repl);
	return err;
}

static void __net_exit arptable_filter_net_exit(struct net *net)
{
	if (!net->ipv4.arptable_filter)
		return;
	arpt_unregister_table(net, net->ipv4.arptable_filter, arpfilter_ops);
	net->ipv4.arptable_filter = NULL;
}

static struct pernet_operations arptable_filter_net_ops = {
	.exit = arptable_filter_net_exit,
};

static int __init arptable_filter_init(void)
{
	int ret;

	arpfilter_ops = xt_hook_ops_alloc(&packet_filter, arptable_filter_hook);
	if (IS_ERR(arpfilter_ops))
		return PTR_ERR(arpfilter_ops);

	ret = register_pernet_subsys(&arptable_filter_net_ops);
	if (ret < 0) {
		kfree(arpfilter_ops);
		return ret;
	}

	ret = arptable_filter_table_init(&init_net);
	if (ret) {
		unregister_pernet_subsys(&arptable_filter_net_ops);
		kfree(arpfilter_ops);
	}

	return ret;
}

static void __exit arptable_filter_fini(void)
{
	unregister_pernet_subsys(&arptable_filter_net_ops);
	kfree(arpfilter_ops);
}

module_init(arptable_filter_init);
module_exit(arptable_filter_fini);
