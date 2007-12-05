/*
 * This is the 1999 rewrite of IP Firewalling, aiming for kernel 2.3.x.
 *
 * Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 * Copyright (C) 2000-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/route.h>
#include <linux/ip.h>
#include <net/ip.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("iptables mangle table");

#define MANGLE_VALID_HOOKS ((1 << NF_INET_PRE_ROUTING) | \
			    (1 << NF_INET_LOCAL_IN) | \
			    (1 << NF_INET_FORWARD) | \
			    (1 << NF_INET_LOCAL_OUT) | \
			    (1 << NF_INET_POST_ROUTING))

/* Ouch - five different hooks? Maybe this should be a config option..... -- BC */
static struct
{
	struct ipt_replace repl;
	struct ipt_standard entries[5];
	struct ipt_error term;
} initial_table __initdata = {
	.repl = {
		.name = "mangle",
		.valid_hooks = MANGLE_VALID_HOOKS,
		.num_entries = 6,
		.size = sizeof(struct ipt_standard) * 5 + sizeof(struct ipt_error),
		.hook_entry = {
			[NF_INET_PRE_ROUTING] 	= 0,
			[NF_INET_LOCAL_IN] 	= sizeof(struct ipt_standard),
			[NF_INET_FORWARD] 	= sizeof(struct ipt_standard) * 2,
			[NF_INET_LOCAL_OUT] 	= sizeof(struct ipt_standard) * 3,
			[NF_INET_POST_ROUTING] 	= sizeof(struct ipt_standard) * 4,
		},
		.underflow = {
			[NF_INET_PRE_ROUTING] 	= 0,
			[NF_INET_LOCAL_IN] 	= sizeof(struct ipt_standard),
			[NF_INET_FORWARD] 	= sizeof(struct ipt_standard) * 2,
			[NF_INET_LOCAL_OUT] 	= sizeof(struct ipt_standard) * 3,
			[NF_INET_POST_ROUTING]	= sizeof(struct ipt_standard) * 4,
		},
	},
	.entries = {
		IPT_STANDARD_INIT(NF_ACCEPT),	/* PRE_ROUTING */
		IPT_STANDARD_INIT(NF_ACCEPT),	/* LOCAL_IN */
		IPT_STANDARD_INIT(NF_ACCEPT),	/* FORWARD */
		IPT_STANDARD_INIT(NF_ACCEPT),	/* LOCAL_OUT */
		IPT_STANDARD_INIT(NF_ACCEPT),	/* POST_ROUTING */
	},
	.term = IPT_ERROR_INIT,			/* ERROR */
};

static struct xt_table packet_mangler = {
	.name		= "mangle",
	.valid_hooks	= MANGLE_VALID_HOOKS,
	.lock		= RW_LOCK_UNLOCKED,
	.me		= THIS_MODULE,
	.af		= AF_INET,
};

/* The work comes in here from netfilter.c. */
static unsigned int
ipt_route_hook(unsigned int hook,
	 struct sk_buff *skb,
	 const struct net_device *in,
	 const struct net_device *out,
	 int (*okfn)(struct sk_buff *))
{
	return ipt_do_table(skb, hook, in, out, &packet_mangler);
}

static unsigned int
ipt_local_hook(unsigned int hook,
		   struct sk_buff *skb,
		   const struct net_device *in,
		   const struct net_device *out,
		   int (*okfn)(struct sk_buff *))
{
	unsigned int ret;
	const struct iphdr *iph;
	u_int8_t tos;
	__be32 saddr, daddr;
	u_int32_t mark;

	/* root is playing with raw sockets. */
	if (skb->len < sizeof(struct iphdr)
	    || ip_hdrlen(skb) < sizeof(struct iphdr)) {
		if (net_ratelimit())
			printk("iptable_mangle: ignoring short SOCK_RAW "
			       "packet.\n");
		return NF_ACCEPT;
	}

	/* Save things which could affect route */
	mark = skb->mark;
	iph = ip_hdr(skb);
	saddr = iph->saddr;
	daddr = iph->daddr;
	tos = iph->tos;

	ret = ipt_do_table(skb, hook, in, out, &packet_mangler);
	/* Reroute for ANY change. */
	if (ret != NF_DROP && ret != NF_STOLEN && ret != NF_QUEUE) {
		iph = ip_hdr(skb);

		if (iph->saddr != saddr ||
		    iph->daddr != daddr ||
		    skb->mark != mark ||
		    iph->tos != tos)
			if (ip_route_me_harder(skb, RTN_UNSPEC))
				ret = NF_DROP;
	}

	return ret;
}

static struct nf_hook_ops ipt_ops[] __read_mostly = {
	{
		.hook		= ipt_route_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_MANGLE,
	},
	{
		.hook		= ipt_route_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_MANGLE,
	},
	{
		.hook		= ipt_route_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_FORWARD,
		.priority	= NF_IP_PRI_MANGLE,
	},
	{
		.hook		= ipt_local_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_MANGLE,
	},
	{
		.hook		= ipt_route_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_MANGLE,
	},
};

static int __init iptable_mangle_init(void)
{
	int ret;

	/* Register table */
	ret = ipt_register_table(&packet_mangler, &initial_table.repl);
	if (ret < 0)
		return ret;

	/* Register hooks */
	ret = nf_register_hooks(ipt_ops, ARRAY_SIZE(ipt_ops));
	if (ret < 0)
		goto cleanup_table;

	return ret;

 cleanup_table:
	ipt_unregister_table(&packet_mangler);
	return ret;
}

static void __exit iptable_mangle_fini(void)
{
	nf_unregister_hooks(ipt_ops, ARRAY_SIZE(ipt_ops));
	ipt_unregister_table(&packet_mangler);
}

module_init(iptable_mangle_init);
module_exit(iptable_mangle_fini);
