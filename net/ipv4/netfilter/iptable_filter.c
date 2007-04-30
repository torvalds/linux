/*
 * This is the 1999 rewrite of IP Firewalling, aiming for kernel 2.3.x.
 *
 * Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 * Copyright (C) 2000-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <net/ip.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("iptables filter table");

#define FILTER_VALID_HOOKS ((1 << NF_IP_LOCAL_IN) | (1 << NF_IP_FORWARD) | (1 << NF_IP_LOCAL_OUT))

static struct
{
	struct ipt_replace repl;
	struct ipt_standard entries[3];
	struct ipt_error term;
} initial_table __initdata
= { { "filter", FILTER_VALID_HOOKS, 4,
      sizeof(struct ipt_standard) * 3 + sizeof(struct ipt_error),
      { [NF_IP_LOCAL_IN] = 0,
	[NF_IP_FORWARD] = sizeof(struct ipt_standard),
	[NF_IP_LOCAL_OUT] = sizeof(struct ipt_standard) * 2 },
      { [NF_IP_LOCAL_IN] = 0,
	[NF_IP_FORWARD] = sizeof(struct ipt_standard),
	[NF_IP_LOCAL_OUT] = sizeof(struct ipt_standard) * 2 },
      0, NULL, { } },
    {
	    /* LOCAL_IN */
	    { { { { 0 }, { 0 }, { 0 }, { 0 }, "", "", { 0 }, { 0 }, 0, 0, 0 },
		0,
		sizeof(struct ipt_entry),
		sizeof(struct ipt_standard),
		0, { 0, 0 }, { } },
	      { { { { IPT_ALIGN(sizeof(struct ipt_standard_target)), "" } }, { } },
		-NF_ACCEPT - 1 } },
	    /* FORWARD */
	    { { { { 0 }, { 0 }, { 0 }, { 0 }, "", "", { 0 }, { 0 }, 0, 0, 0 },
		0,
		sizeof(struct ipt_entry),
		sizeof(struct ipt_standard),
		0, { 0, 0 }, { } },
	      { { { { IPT_ALIGN(sizeof(struct ipt_standard_target)), "" } }, { } },
		-NF_ACCEPT - 1 } },
	    /* LOCAL_OUT */
	    { { { { 0 }, { 0 }, { 0 }, { 0 }, "", "", { 0 }, { 0 }, 0, 0, 0 },
		0,
		sizeof(struct ipt_entry),
		sizeof(struct ipt_standard),
		0, { 0, 0 }, { } },
	      { { { { IPT_ALIGN(sizeof(struct ipt_standard_target)), "" } }, { } },
		-NF_ACCEPT - 1 } }
    },
    /* ERROR */
    { { { { 0 }, { 0 }, { 0 }, { 0 }, "", "", { 0 }, { 0 }, 0, 0, 0 },
	0,
	sizeof(struct ipt_entry),
	sizeof(struct ipt_error),
	0, { 0, 0 }, { } },
      { { { { IPT_ALIGN(sizeof(struct ipt_error_target)), IPT_ERROR_TARGET } },
	  { } },
	"ERROR"
      }
    }
};

static struct xt_table packet_filter = {
	.name		= "filter",
	.valid_hooks	= FILTER_VALID_HOOKS,
	.lock		= RW_LOCK_UNLOCKED,
	.me		= THIS_MODULE,
	.af		= AF_INET,
};

/* The work comes in here from netfilter.c. */
static unsigned int
ipt_hook(unsigned int hook,
	 struct sk_buff **pskb,
	 const struct net_device *in,
	 const struct net_device *out,
	 int (*okfn)(struct sk_buff *))
{
	return ipt_do_table(pskb, hook, in, out, &packet_filter);
}

static unsigned int
ipt_local_out_hook(unsigned int hook,
		   struct sk_buff **pskb,
		   const struct net_device *in,
		   const struct net_device *out,
		   int (*okfn)(struct sk_buff *))
{
	/* root is playing with raw sockets. */
	if ((*pskb)->len < sizeof(struct iphdr)
	    || ip_hdrlen(*pskb) < sizeof(struct iphdr)) {
		if (net_ratelimit())
			printk("ipt_hook: happy cracking.\n");
		return NF_ACCEPT;
	}

	return ipt_do_table(pskb, hook, in, out, &packet_filter);
}

static struct nf_hook_ops ipt_ops[] = {
	{
		.hook		= ipt_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_IN,
		.priority	= NF_IP_PRI_FILTER,
	},
	{
		.hook		= ipt_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_FORWARD,
		.priority	= NF_IP_PRI_FILTER,
	},
	{
		.hook		= ipt_local_out_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_OUT,
		.priority	= NF_IP_PRI_FILTER,
	},
};

/* Default to forward because I got too much mail already. */
static int forward = NF_ACCEPT;
module_param(forward, bool, 0000);

static int __init iptable_filter_init(void)
{
	int ret;

	if (forward < 0 || forward > NF_MAX_VERDICT) {
		printk("iptables forward must be 0 or 1\n");
		return -EINVAL;
	}

	/* Entry 1 is the FORWARD hook */
	initial_table.entries[1].target.verdict = -forward - 1;

	/* Register table */
	ret = ipt_register_table(&packet_filter, &initial_table.repl);
	if (ret < 0)
		return ret;

	/* Register hooks */
	ret = nf_register_hooks(ipt_ops, ARRAY_SIZE(ipt_ops));
	if (ret < 0)
		goto cleanup_table;

	return ret;

 cleanup_table:
	ipt_unregister_table(&packet_filter);
	return ret;
}

static void __exit iptable_filter_fini(void)
{
	nf_unregister_hooks(ipt_ops, ARRAY_SIZE(ipt_ops));
	ipt_unregister_table(&packet_filter);
}

module_init(iptable_filter_init);
module_exit(iptable_filter_fini);
