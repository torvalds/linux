/*
 * Filtering ARP tables module.
 *
 * Copyright (C) 2002 David S. Miller (davem@redhat.com)
 *
 */

#include <linux/module.h>
#include <linux/netfilter_arp/arp_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_DESCRIPTION("arptables filter table");

#define FILTER_VALID_HOOKS ((1 << NF_ARP_IN) | (1 << NF_ARP_OUT) | \
			   (1 << NF_ARP_FORWARD))

static struct
{
	struct arpt_replace repl;
	struct arpt_standard entries[3];
	struct arpt_error term;
} initial_table __initdata = {
	.repl = {
		.name = "filter",
		.valid_hooks = FILTER_VALID_HOOKS,
		.num_entries = 4,
		.size = sizeof(struct arpt_standard) * 3 + sizeof(struct arpt_error),
		.hook_entry = {
			[NF_ARP_IN] = 0,
			[NF_ARP_OUT] = sizeof(struct arpt_standard),
			[NF_ARP_FORWARD] = 2 * sizeof(struct arpt_standard),
		},
		.underflow = {
			[NF_ARP_IN] = 0,
			[NF_ARP_OUT] = sizeof(struct arpt_standard),
			[NF_ARP_FORWARD] = 2 * sizeof(struct arpt_standard),
		},
	},
	.entries = {
		ARPT_STANDARD_INIT(NF_ACCEPT),	/* ARP_IN */
		ARPT_STANDARD_INIT(NF_ACCEPT),	/* ARP_OUT */
		ARPT_STANDARD_INIT(NF_ACCEPT),	/* ARP_FORWARD */
	},
	.term = ARPT_ERROR_INIT,
};

static struct arpt_table packet_filter = {
	.name		= "filter",
	.valid_hooks	= FILTER_VALID_HOOKS,
	.lock		= RW_LOCK_UNLOCKED,
	.private	= NULL,
	.me		= THIS_MODULE,
	.af		= NF_ARP,
};

/* The work comes in here from netfilter.c */
static unsigned int arpt_hook(unsigned int hook,
			      struct sk_buff *skb,
			      const struct net_device *in,
			      const struct net_device *out,
			      int (*okfn)(struct sk_buff *))
{
	return arpt_do_table(skb, hook, in, out, &packet_filter);
}

static struct nf_hook_ops arpt_ops[] __read_mostly = {
	{
		.hook		= arpt_hook,
		.owner		= THIS_MODULE,
		.pf		= NF_ARP,
		.hooknum	= NF_ARP_IN,
	},
	{
		.hook		= arpt_hook,
		.owner		= THIS_MODULE,
		.pf		= NF_ARP,
		.hooknum	= NF_ARP_OUT,
	},
	{
		.hook		= arpt_hook,
		.owner		= THIS_MODULE,
		.pf		= NF_ARP,
		.hooknum	= NF_ARP_FORWARD,
	},
};

static int __init arptable_filter_init(void)
{
	int ret;

	/* Register table */
	ret = arpt_register_table(&packet_filter, &initial_table.repl);
	if (ret < 0)
		return ret;

	ret = nf_register_hooks(arpt_ops, ARRAY_SIZE(arpt_ops));
	if (ret < 0)
		goto cleanup_table;
	return ret;

cleanup_table:
	arpt_unregister_table(&packet_filter);
	return ret;
}

static void __exit arptable_filter_fini(void)
{
	nf_unregister_hooks(arpt_ops, ARRAY_SIZE(arpt_ops));
	arpt_unregister_table(&packet_filter);
}

module_init(arptable_filter_init);
module_exit(arptable_filter_fini);
