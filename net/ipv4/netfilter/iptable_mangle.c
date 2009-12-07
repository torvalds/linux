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
static const struct
{
	struct ipt_replace repl;
	struct ipt_standard entries[5];
	struct ipt_error term;
} initial_table __net_initdata = {
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

static const struct xt_table packet_mangler = {
	.name		= "mangle",
	.valid_hooks	= MANGLE_VALID_HOOKS,
	.me		= THIS_MODULE,
	.af		= NFPROTO_IPV4,
};

/* The work comes in here from netfilter.c. */
static unsigned int
ipt_pre_routing_hook(unsigned int hook,
		     struct sk_buff *skb,
		     const struct net_device *in,
		     const struct net_device *out,
		     int (*okfn)(struct sk_buff *))
{
	return ipt_do_table(skb, hook, in, out,
			    dev_net(in)->ipv4.iptable_mangle);
}

static unsigned int
ipt_post_routing_hook(unsigned int hook,
		      struct sk_buff *skb,
		      const struct net_device *in,
		      const struct net_device *out,
		      int (*okfn)(struct sk_buff *))
{
	return ipt_do_table(skb, hook, in, out,
			    dev_net(out)->ipv4.iptable_mangle);
}

static unsigned int
ipt_local_in_hook(unsigned int hook,
		  struct sk_buff *skb,
		  const struct net_device *in,
		  const struct net_device *out,
		  int (*okfn)(struct sk_buff *))
{
	return ipt_do_table(skb, hook, in, out,
			    dev_net(in)->ipv4.iptable_mangle);
}

static unsigned int
ipt_forward_hook(unsigned int hook,
	 struct sk_buff *skb,
	 const struct net_device *in,
	 const struct net_device *out,
	 int (*okfn)(struct sk_buff *))
{
	return ipt_do_table(skb, hook, in, out,
			    dev_net(in)->ipv4.iptable_mangle);
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
	    || ip_hdrlen(skb) < sizeof(struct iphdr))
		return NF_ACCEPT;

	/* Save things which could affect route */
	mark = skb->mark;
	iph = ip_hdr(skb);
	saddr = iph->saddr;
	daddr = iph->daddr;
	tos = iph->tos;

	ret = ipt_do_table(skb, hook, in, out,
			   dev_net(out)->ipv4.iptable_mangle);
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
		.hook		= ipt_pre_routing_hook,
		.owner		= THIS_MODULE,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_MANGLE,
	},
	{
		.hook		= ipt_local_in_hook,
		.owner		= THIS_MODULE,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_MANGLE,
	},
	{
		.hook		= ipt_forward_hook,
		.owner		= THIS_MODULE,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_FORWARD,
		.priority	= NF_IP_PRI_MANGLE,
	},
	{
		.hook		= ipt_local_hook,
		.owner		= THIS_MODULE,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_MANGLE,
	},
	{
		.hook		= ipt_post_routing_hook,
		.owner		= THIS_MODULE,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_MANGLE,
	},
};

static int __net_init iptable_mangle_net_init(struct net *net)
{
	/* Register table */
	net->ipv4.iptable_mangle =
		ipt_register_table(net, &packet_mangler, &initial_table.repl);
	if (IS_ERR(net->ipv4.iptable_mangle))
		return PTR_ERR(net->ipv4.iptable_mangle);
	return 0;
}

static void __net_exit iptable_mangle_net_exit(struct net *net)
{
	ipt_unregister_table(net->ipv4.iptable_mangle);
}

static struct pernet_operations iptable_mangle_net_ops = {
	.init = iptable_mangle_net_init,
	.exit = iptable_mangle_net_exit,
};

static int __init iptable_mangle_init(void)
{
	int ret;

	ret = register_pernet_subsys(&iptable_mangle_net_ops);
	if (ret < 0)
		return ret;

	/* Register hooks */
	ret = nf_register_hooks(ipt_ops, ARRAY_SIZE(ipt_ops));
	if (ret < 0)
		goto cleanup_table;

	return ret;

 cleanup_table:
	unregister_pernet_subsys(&iptable_mangle_net_ops);
	return ret;
}

static void __exit iptable_mangle_fini(void)
{
	nf_unregister_hooks(ipt_ops, ARRAY_SIZE(ipt_ops));
	unregister_pernet_subsys(&iptable_mangle_net_ops);
}

module_init(iptable_mangle_init);
module_exit(iptable_mangle_fini);
