// SPDX-License-Identifier: GPL-2.0-only
/*
 * IPv6 packet mangling table, a port of the IPv4 mangle table to IPv6
 *
 * Copyright (C) 2000-2001 by Harald Welte <laforge@gnumonks.org>
 * Copyright (C) 2000-2004 Netfilter Core Team <coreteam@netfilter.org>
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
ip6t_mangle_out(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct in6_addr saddr, daddr;
	unsigned int ret, verdict;
	u32 flowlabel, mark;
	u8 hop_limit;
	int err;

	/* save source/dest address, mark, hoplimit, flowlabel, priority,  */
	memcpy(&saddr, &ipv6_hdr(skb)->saddr, sizeof(saddr));
	memcpy(&daddr, &ipv6_hdr(skb)->daddr, sizeof(daddr));
	mark = skb->mark;
	hop_limit = ipv6_hdr(skb)->hop_limit;

	/* flowlabel and prio (includes version, which shouldn't change either */
	flowlabel = *((u_int32_t *)ipv6_hdr(skb));

	ret = ip6t_do_table(priv, skb, state);
	verdict = ret & NF_VERDICT_MASK;

	if (verdict != NF_DROP && verdict != NF_STOLEN &&
	    (!ipv6_addr_equal(&ipv6_hdr(skb)->saddr, &saddr) ||
	     !ipv6_addr_equal(&ipv6_hdr(skb)->daddr, &daddr) ||
	     skb->mark != mark ||
	     ipv6_hdr(skb)->hop_limit != hop_limit ||
	     flowlabel != *((u_int32_t *)ipv6_hdr(skb)))) {
		err = ip6_route_me_harder(state->net, state->sk, skb);
		if (err < 0)
			ret = NF_DROP_ERR(err);
	}

	return ret;
}

/* The work comes in here from netfilter.c. */
static unsigned int
ip6table_mangle_hook(void *priv, struct sk_buff *skb,
		     const struct nf_hook_state *state)
{
	if (state->hook == NF_INET_LOCAL_OUT)
		return ip6t_mangle_out(priv, skb, state);
	return ip6t_do_table(priv, skb, state);
}

static struct nf_hook_ops *mangle_ops __read_mostly;
static int ip6table_mangle_table_init(struct net *net)
{
	struct ip6t_replace *repl;
	int ret;

	repl = ip6t_alloc_initial_table(&packet_mangler);
	if (repl == NULL)
		return -ENOMEM;
	ret = ip6t_register_table(net, &packet_mangler, repl, mangle_ops);
	kfree(repl);
	return ret;
}

static void __net_exit ip6table_mangle_net_pre_exit(struct net *net)
{
	ip6t_unregister_table_pre_exit(net, "mangle");
}

static void __net_exit ip6table_mangle_net_exit(struct net *net)
{
	ip6t_unregister_table_exit(net, "mangle");
}

static struct pernet_operations ip6table_mangle_net_ops = {
	.pre_exit = ip6table_mangle_net_pre_exit,
	.exit = ip6table_mangle_net_exit,
};

static int __init ip6table_mangle_init(void)
{
	int ret = xt_register_template(&packet_mangler,
				       ip6table_mangle_table_init);

	if (ret < 0)
		return ret;

	mangle_ops = xt_hook_ops_alloc(&packet_mangler, ip6table_mangle_hook);
	if (IS_ERR(mangle_ops)) {
		xt_unregister_template(&packet_mangler);
		return PTR_ERR(mangle_ops);
	}

	ret = register_pernet_subsys(&ip6table_mangle_net_ops);
	if (ret < 0) {
		xt_unregister_template(&packet_mangler);
		kfree(mangle_ops);
		return ret;
	}

	return ret;
}

static void __exit ip6table_mangle_fini(void)
{
	unregister_pernet_subsys(&ip6table_mangle_net_ops);
	xt_unregister_template(&packet_mangler);
	kfree(mangle_ops);
}

module_init(ip6table_mangle_init);
module_exit(ip6table_mangle_fini);
