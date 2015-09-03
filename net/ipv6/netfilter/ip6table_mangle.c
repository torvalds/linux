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
#include <linux/slab.h>
#include <net/ipv6.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("ip6tables mangle table");

#define MANGLE_VALID_HOOKS ((1 << NF_INET_PRE_ROUTING) | \
			    (1 << NF_INET_LOCAL_IN) | \
			    (1 << NF_INET_FORWARD) | \
			    (1 << NF_INET_LOCAL_OUT) | \
			    (1 << NF_INET_POST_ROUTING))

static const struct xt_table packet_mangler = {
	.name		= "mangle",
	.valid_hooks	= MANGLE_VALID_HOOKS,
	.me		= THIS_MODULE,
	.af		= NFPROTO_IPV6,
	.priority	= NF_IP6_PRI_MANGLE,
};

static unsigned int
ip6t_mangle_out(struct sk_buff *skb, const struct nf_hook_state *state)
{
	unsigned int ret;
	struct in6_addr saddr, daddr;
	u_int8_t hop_limit;
	u_int32_t flowlabel, mark;
	int err;
#if 0
	/* root is playing with raw sockets. */
	if (skb->len < sizeof(struct iphdr) ||
	    ip_hdrlen(skb) < sizeof(struct iphdr)) {
		net_warn_ratelimited("ip6t_hook: happy cracking\n");
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

	ret = ip6t_do_table(skb, NF_INET_LOCAL_OUT, state,
			    dev_net(state->out)->ipv6.ip6table_mangle);

	if (ret != NF_DROP && ret != NF_STOLEN &&
	    (!ipv6_addr_equal(&ipv6_hdr(skb)->saddr, &saddr) ||
	     !ipv6_addr_equal(&ipv6_hdr(skb)->daddr, &daddr) ||
	     skb->mark != mark ||
	     ipv6_hdr(skb)->hop_limit != hop_limit ||
	     flowlabel != *((u_int32_t *)ipv6_hdr(skb)))) {
		err = ip6_route_me_harder(skb);
		if (err < 0)
			ret = NF_DROP_ERR(err);
	}

	return ret;
}

/* The work comes in here from netfilter.c. */
static unsigned int
ip6table_mangle_hook(const struct nf_hook_ops *ops, struct sk_buff *skb,
		     const struct nf_hook_state *state)
{
	if (ops->hooknum == NF_INET_LOCAL_OUT)
		return ip6t_mangle_out(skb, state);
	if (ops->hooknum == NF_INET_POST_ROUTING)
		return ip6t_do_table(skb, ops->hooknum, state,
				     dev_net(state->out)->ipv6.ip6table_mangle);
	/* INPUT/FORWARD */
	return ip6t_do_table(skb, ops->hooknum, state,
			     dev_net(state->in)->ipv6.ip6table_mangle);
}

static struct nf_hook_ops *mangle_ops __read_mostly;
static int __net_init ip6table_mangle_net_init(struct net *net)
{
	struct ip6t_replace *repl;

	repl = ip6t_alloc_initial_table(&packet_mangler);
	if (repl == NULL)
		return -ENOMEM;
	net->ipv6.ip6table_mangle =
		ip6t_register_table(net, &packet_mangler, repl);
	kfree(repl);
	return PTR_ERR_OR_ZERO(net->ipv6.ip6table_mangle);
}

static void __net_exit ip6table_mangle_net_exit(struct net *net)
{
	ip6t_unregister_table(net, net->ipv6.ip6table_mangle);
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
	mangle_ops = xt_hook_link(&packet_mangler, ip6table_mangle_hook);
	if (IS_ERR(mangle_ops)) {
		ret = PTR_ERR(mangle_ops);
		goto cleanup_table;
	}

	return ret;

 cleanup_table:
	unregister_pernet_subsys(&ip6table_mangle_net_ops);
	return ret;
}

static void __exit ip6table_mangle_fini(void)
{
	xt_hook_unlink(&packet_mangler, mangle_ops);
	unregister_pernet_subsys(&ip6table_mangle_net_ops);
}

module_init(ip6table_mangle_init);
module_exit(ip6table_mangle_fini);
