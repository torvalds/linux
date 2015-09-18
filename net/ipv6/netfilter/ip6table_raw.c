/*
 * IPv6 raw table, a port of the IPv4 raw table to IPv6
 *
 * Copyright (C) 2003 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 */
#include <linux/module.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/slab.h>

#define RAW_VALID_HOOKS ((1 << NF_INET_PRE_ROUTING) | (1 << NF_INET_LOCAL_OUT))

static const struct xt_table packet_raw = {
	.name = "raw",
	.valid_hooks = RAW_VALID_HOOKS,
	.me = THIS_MODULE,
	.af = NFPROTO_IPV6,
	.priority = NF_IP6_PRI_RAW,
};

/* The work comes in here from netfilter.c. */
static unsigned int
ip6table_raw_hook(const struct nf_hook_ops *ops, struct sk_buff *skb,
		  const struct nf_hook_state *state)
{
	const struct net *net = dev_net(state->in ? state->in : state->out);

	return ip6t_do_table(skb, ops->hooknum, state, net->ipv6.ip6table_raw);
}

static struct nf_hook_ops *rawtable_ops __read_mostly;

static int __net_init ip6table_raw_net_init(struct net *net)
{
	struct ip6t_replace *repl;

	repl = ip6t_alloc_initial_table(&packet_raw);
	if (repl == NULL)
		return -ENOMEM;
	net->ipv6.ip6table_raw =
		ip6t_register_table(net, &packet_raw, repl);
	kfree(repl);
	return PTR_ERR_OR_ZERO(net->ipv6.ip6table_raw);
}

static void __net_exit ip6table_raw_net_exit(struct net *net)
{
	ip6t_unregister_table(net, net->ipv6.ip6table_raw);
}

static struct pernet_operations ip6table_raw_net_ops = {
	.init = ip6table_raw_net_init,
	.exit = ip6table_raw_net_exit,
};

static int __init ip6table_raw_init(void)
{
	int ret;

	ret = register_pernet_subsys(&ip6table_raw_net_ops);
	if (ret < 0)
		return ret;

	/* Register hooks */
	rawtable_ops = xt_hook_link(&packet_raw, ip6table_raw_hook);
	if (IS_ERR(rawtable_ops)) {
		ret = PTR_ERR(rawtable_ops);
		goto cleanup_table;
	}

	return ret;

 cleanup_table:
	unregister_pernet_subsys(&ip6table_raw_net_ops);
	return ret;
}

static void __exit ip6table_raw_fini(void)
{
	xt_hook_unlink(&packet_raw, rawtable_ops);
	unregister_pernet_subsys(&ip6table_raw_net_ops);
}

module_init(ip6table_raw_init);
module_exit(ip6table_raw_fini);
MODULE_LICENSE("GPL");
