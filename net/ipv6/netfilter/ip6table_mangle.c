/*
 * IPv6 packet mangling table, a port of the IPv4 mangle table to IPv6
 *
 * Copyright (C) 2000-2001 by Harald Welte <laforge@gnumonks.org>
 * Copyright (C) 2000-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("ip6tables mangle table");

#define MANGLE_VALID_HOOKS ((1 << NF_INET_PRE_ROUTING) | \
			    (1 << NF_INET_LOCAL_IN) | \
			    (1 << NF_INET_FORWARD) | \
			    (1 << NF_INET_LOCAL_OUT) | \
			    (1 << NF_INET_POST_ROUTING))

static struct
{
	struct ip6t_replace repl;
	struct ip6t_standard entries[5];
	struct ip6t_error term;
} initial_table __net_initdata = {
	.repl = {
		.name = "mangle",
		.valid_hooks = MANGLE_VALID_HOOKS,
		.num_entries = 6,
		.size = sizeof(struct ip6t_standard) * 5 + sizeof(struct ip6t_error),
		.hook_entry = {
			[NF_INET_PRE_ROUTING] 	= 0,
			[NF_INET_LOCAL_IN]	= sizeof(struct ip6t_standard),
			[NF_INET_FORWARD]	= sizeof(struct ip6t_standard) * 2,
			[NF_INET_LOCAL_OUT] 	= sizeof(struct ip6t_standard) * 3,
			[NF_INET_POST_ROUTING]	= sizeof(struct ip6t_standard) * 4,
		},
		.underflow = {
			[NF_INET_PRE_ROUTING] 	= 0,
			[NF_INET_LOCAL_IN]	= sizeof(struct ip6t_standard),
			[NF_INET_FORWARD]	= sizeof(struct ip6t_standard) * 2,
			[NF_INET_LOCAL_OUT] 	= sizeof(struct ip6t_standard) * 3,
			[NF_INET_POST_ROUTING]	= sizeof(struct ip6t_standard) * 4,
		},
	},
	.entries = {
		IP6T_STANDARD_INIT(NF_ACCEPT),	/* PRE_ROUTING */
		IP6T_STANDARD_INIT(NF_ACCEPT),	/* LOCAL_IN */
		IP6T_STANDARD_INIT(NF_ACCEPT),	/* FORWARD */
		IP6T_STANDARD_INIT(NF_ACCEPT),	/* LOCAL_OUT */
		IP6T_STANDARD_INIT(NF_ACCEPT),	/* POST_ROUTING */
	},
	.term = IP6T_ERROR_INIT,		/* ERROR */
};

static struct xt_table packet_mangler = {
	.name		= "mangle",
	.valid_hooks	= MANGLE_VALID_HOOKS,
	.me		= THIS_MODULE,
	.af		= AF_INET6,
};

/* The work comes in here from netfilter.c. */
static unsigned int
ip6t_in_hook(unsigned int hook,
	 struct sk_buff *skb,
	 const struct net_device *in,
	 const struct net_device *out,
	 int (*okfn)(struct sk_buff *))
{
	return ip6t_do_table(skb, hook, in, out,
			     dev_net(in)->ipv6.ip6table_mangle);
}

static unsigned int
ip6t_post_routing_hook(unsigned int hook,
		struct sk_buff *skb,
		const struct net_device *in,
		const struct net_device *out,
		int (*okfn)(struct sk_buff *))
{
	return ip6t_do_table(skb, hook, in, out,
			     dev_net(out)->ipv6.ip6table_mangle);
}

static unsigned int
ip6t_local_out_hook(unsigned int hook,
		   struct sk_buff *skb,
		   const struct net_device *in,
		   const struct net_device *out,
		   int (*okfn)(struct sk_buff *))
{

	unsigned int ret;
	struct in6_addr saddr, daddr;
	u_int8_t hop_limit;
	u_int32_t flowlabel, mark;

#if 0
	/* root is playing with raw sockets. */
	if (skb->len < sizeof(struct iphdr)
	    || ip_hdrlen(skb) < sizeof(struct iphdr)) {
		if (net_ratelimit())
			printk("ip6t_hook: happy cracking.\n");
		return NF_ACCEPT;
	}
#endif

	/* save source/dest address, mark, hoplimit, flowlabel, priority,  */
	memcpy(&saddr, &ipv6_hdr(skb)->saddr, sizeof(saddr));
	memcpy(&daddr, &ipv6_hdr(skb)->daddr, sizeof(daddr));
	mark = skb->mark;
	hop_limit = ipv6_hdr(skb)->hop_limit;

	/* flowlabel and prio (includes version, which shouldn't change either */
	flowlabel = *((u_int32_t *)ipv6_hdr(skb));

	ret = ip6t_do_table(skb, hook, in, out,
			    dev_net(out)->ipv6.ip6table_mangle);

	if (ret != NF_DROP && ret != NF_STOLEN
		&& (memcmp(&ipv6_hdr(skb)->saddr, &saddr, sizeof(saddr))
		    || memcmp(&ipv6_hdr(skb)->daddr, &daddr, sizeof(daddr))
		    || skb->mark != mark
		    || ipv6_hdr(skb)->hop_limit != hop_limit))
		return ip6_route_me_harder(skb) == 0 ? ret : NF_DROP;

	return ret;
}

static struct nf_hook_ops ip6t_ops[] __read_mostly = {
	{
		.hook		= ip6t_in_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP6_PRI_MANGLE,
	},
	{
		.hook		= ip6t_in_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_MANGLE,
	},
	{
		.hook		= ip6t_in_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_INET_FORWARD,
		.priority	= NF_IP6_PRI_MANGLE,
	},
	{
		.hook		= ip6t_local_out_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_MANGLE,
	},
	{
		.hook		= ip6t_post_routing_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP6_PRI_MANGLE,
	},
};

static int __net_init ip6table_mangle_net_init(struct net *net)
{
	/* Register table */
	net->ipv6.ip6table_mangle =
		ip6t_register_table(net, &packet_mangler, &initial_table.repl);
	if (IS_ERR(net->ipv6.ip6table_mangle))
		return PTR_ERR(net->ipv6.ip6table_mangle);
	return 0;
}

static void __net_exit ip6table_mangle_net_exit(struct net *net)
{
	ip6t_unregister_table(net->ipv6.ip6table_mangle);
}

static struct pernet_operations ip6table_mangle_net_ops = {
	.init = ip6table_mangle_net_init,
	.exit = ip6table_mangle_net_exit,
};

static int __init ip6table_mangle_init(void)
{
	int ret;

	ret = register_pernet_subsys(&ip6table_mangle_net_ops);
	if (ret < 0)
		return ret;

	/* Register hooks */
	ret = nf_register_hooks(ip6t_ops, ARRAY_SIZE(ip6t_ops));
	if (ret < 0)
		goto cleanup_table;

	return ret;

 cleanup_table:
	unregister_pernet_subsys(&ip6table_mangle_net_ops);
	return ret;
}

static void __exit ip6table_mangle_fini(void)
{
	nf_unregister_hooks(ip6t_ops, ARRAY_SIZE(ip6t_ops));
	unregister_pernet_subsys(&ip6table_mangle_net_ops);
}

module_init(ip6table_mangle_init);
module_exit(ip6table_mangle_fini);
