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

#define MANGLE_VALID_HOOKS ((1 << NF_IP6_PRE_ROUTING) | \
			    (1 << NF_IP6_LOCAL_IN) | \
			    (1 << NF_IP6_FORWARD) | \
			    (1 << NF_IP6_LOCAL_OUT) | \
			    (1 << NF_IP6_POST_ROUTING))

#if 0
#define DEBUGP(x, args...)	printk(KERN_DEBUG x, ## args)
#else
#define DEBUGP(x, args...)
#endif

static struct
{
	struct ip6t_replace repl;
	struct ip6t_standard entries[5];
	struct ip6t_error term;
} initial_table __initdata
= { { "mangle", MANGLE_VALID_HOOKS, 6,
      sizeof(struct ip6t_standard) * 5 + sizeof(struct ip6t_error),
      { [NF_IP6_PRE_ROUTING] 	= 0,
	[NF_IP6_LOCAL_IN]	= sizeof(struct ip6t_standard),
	[NF_IP6_FORWARD]	= sizeof(struct ip6t_standard) * 2,
	[NF_IP6_LOCAL_OUT] 	= sizeof(struct ip6t_standard) * 3,
	[NF_IP6_POST_ROUTING]	= sizeof(struct ip6t_standard) * 4},
      { [NF_IP6_PRE_ROUTING] 	= 0,
	[NF_IP6_LOCAL_IN]	= sizeof(struct ip6t_standard),
	[NF_IP6_FORWARD]	= sizeof(struct ip6t_standard) * 2,
	[NF_IP6_LOCAL_OUT] 	= sizeof(struct ip6t_standard) * 3,
	[NF_IP6_POST_ROUTING]	= sizeof(struct ip6t_standard) * 4},
      0, NULL, { } },
    {
	    /* PRE_ROUTING */
	    { { { { { { 0 } } }, { { { 0 } } }, { { { 0 } } }, { { { 0 } } }, "", "", { 0 }, { 0 }, 0, 0, 0 },
		0,
		sizeof(struct ip6t_entry),
		sizeof(struct ip6t_standard),
		0, { 0, 0 }, { } },
	      { { { { IP6T_ALIGN(sizeof(struct ip6t_standard_target)), "" } }, { } },
		-NF_ACCEPT - 1 } },
	    /* LOCAL_IN */
	    { { { { { { 0 } } }, { { { 0 } } }, { { { 0 } } }, { { { 0 } } }, "", "", { 0 }, { 0 }, 0, 0, 0 },
		0,
		sizeof(struct ip6t_entry),
		sizeof(struct ip6t_standard),
		0, { 0, 0 }, { } },
	      { { { { IP6T_ALIGN(sizeof(struct ip6t_standard_target)), "" } }, { } },
		-NF_ACCEPT - 1 } },
	    /* FORWARD */
	    { { { { { { 0 } } }, { { { 0 } } }, { { { 0 } } }, { { { 0 } } }, "", "", { 0 }, { 0 }, 0, 0, 0 },
		0,
		sizeof(struct ip6t_entry),
		sizeof(struct ip6t_standard),
		0, { 0, 0 }, { } },
	      { { { { IP6T_ALIGN(sizeof(struct ip6t_standard_target)), "" } }, { } },
		-NF_ACCEPT - 1 } },
	    /* LOCAL_OUT */
	    { { { { { { 0 } } }, { { { 0 } } }, { { { 0 } } }, { { { 0 } } }, "", "", { 0 }, { 0 }, 0, 0, 0 },
		0,
		sizeof(struct ip6t_entry),
		sizeof(struct ip6t_standard),
		0, { 0, 0 }, { } },
	      { { { { IP6T_ALIGN(sizeof(struct ip6t_standard_target)), "" } }, { } },
		-NF_ACCEPT - 1 } },
	    /* POST_ROUTING */
	    { { { { { { 0 } } }, { { { 0 } } }, { { { 0 } } }, { { { 0 } } }, "", "", { 0 }, { 0 }, 0, 0, 0 },
		0,
		sizeof(struct ip6t_entry),
		sizeof(struct ip6t_standard),
		0, { 0, 0 }, { } },
	      { { { { IP6T_ALIGN(sizeof(struct ip6t_standard_target)), "" } }, { } },
		-NF_ACCEPT - 1 } }
    },
    /* ERROR */
    { { { { { { 0 } } }, { { { 0 } } }, { { { 0 } } }, { { { 0 } } }, "", "", { 0 }, { 0 }, 0, 0, 0 },
	0,
	sizeof(struct ip6t_entry),
	sizeof(struct ip6t_error),
	0, { 0, 0 }, { } },
      { { { { IP6T_ALIGN(sizeof(struct ip6t_error_target)), IP6T_ERROR_TARGET } },
	  { } },
	"ERROR"
      }
    }
};

static struct xt_table packet_mangler = {
	.name		= "mangle",
	.valid_hooks	= MANGLE_VALID_HOOKS,
	.lock		= RW_LOCK_UNLOCKED,
	.me		= THIS_MODULE,
	.af		= AF_INET6,
};

/* The work comes in here from netfilter.c. */
static unsigned int
ip6t_route_hook(unsigned int hook,
	 struct sk_buff **pskb,
	 const struct net_device *in,
	 const struct net_device *out,
	 int (*okfn)(struct sk_buff *))
{
	return ip6t_do_table(pskb, hook, in, out, &packet_mangler);
}

static unsigned int
ip6t_local_hook(unsigned int hook,
		   struct sk_buff **pskb,
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
	if ((*pskb)->len < sizeof(struct iphdr)
	    || ip_hdrlen(*pskb) < sizeof(struct iphdr)) {
		if (net_ratelimit())
			printk("ip6t_hook: happy cracking.\n");
		return NF_ACCEPT;
	}
#endif

	/* save source/dest address, mark, hoplimit, flowlabel, priority,  */
	memcpy(&saddr, &ipv6_hdr(*pskb)->saddr, sizeof(saddr));
	memcpy(&daddr, &ipv6_hdr(*pskb)->daddr, sizeof(daddr));
	mark = (*pskb)->mark;
	hop_limit = ipv6_hdr(*pskb)->hop_limit;

	/* flowlabel and prio (includes version, which shouldn't change either */
	flowlabel = *((u_int32_t *)ipv6_hdr(*pskb));

	ret = ip6t_do_table(pskb, hook, in, out, &packet_mangler);

	if (ret != NF_DROP && ret != NF_STOLEN
		&& (memcmp(&ipv6_hdr(*pskb)->saddr, &saddr, sizeof(saddr))
		    || memcmp(&ipv6_hdr(*pskb)->daddr, &daddr, sizeof(daddr))
		    || (*pskb)->mark != mark
		    || ipv6_hdr(*pskb)->hop_limit != hop_limit))
		return ip6_route_me_harder(*pskb) == 0 ? ret : NF_DROP;

	return ret;
}

static struct nf_hook_ops ip6t_ops[] = {
	{
		.hook		= ip6t_route_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_IP6_PRE_ROUTING,
		.priority	= NF_IP6_PRI_MANGLE,
	},
	{
		.hook		= ip6t_local_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_IP6_LOCAL_IN,
		.priority	= NF_IP6_PRI_MANGLE,
	},
	{
		.hook		= ip6t_route_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_IP6_FORWARD,
		.priority	= NF_IP6_PRI_MANGLE,
	},
	{
		.hook		= ip6t_local_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_IP6_LOCAL_OUT,
		.priority	= NF_IP6_PRI_MANGLE,
	},
	{
		.hook		= ip6t_route_hook,
		.owner		= THIS_MODULE,
		.pf		= PF_INET6,
		.hooknum	= NF_IP6_POST_ROUTING,
		.priority	= NF_IP6_PRI_MANGLE,
	},
};

static int __init ip6table_mangle_init(void)
{
	int ret;

	/* Register table */
	ret = ip6t_register_table(&packet_mangler, &initial_table.repl);
	if (ret < 0)
		return ret;

	/* Register hooks */
	ret = nf_register_hooks(ip6t_ops, ARRAY_SIZE(ip6t_ops));
	if (ret < 0)
		goto cleanup_table;

	return ret;

 cleanup_table:
	ip6t_unregister_table(&packet_mangler);
	return ret;
}

static void __exit ip6table_mangle_fini(void)
{
	nf_unregister_hooks(ip6t_ops, ARRAY_SIZE(ip6t_ops));
	ip6t_unregister_table(&packet_mangler);
}

module_init(ip6table_mangle_init);
module_exit(ip6table_mangle_fini);
