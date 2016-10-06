/*
 * 'raw' table, which is the very first hooked in at PRE_ROUTING and LOCAL_OUT .
 *
 * Copyright (C) 2003 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 */
#include <linux/module.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/slab.h>
#include <net/ip.h>

#define RAW_VALID_HOOKS ((1 << NF_INET_PRE_ROUTING) | (1 << NF_INET_LOCAL_OUT))

static int __net_init iptable_raw_table_init(struct net *net);

static const struct xt_table packet_raw = {
	.name = "raw",
	.valid_hooks =  RAW_VALID_HOOKS,
	.me = THIS_MODULE,
	.af = NFPROTO_IPV4,
	.priority = NF_IP_PRI_RAW,
	.table_init = iptable_raw_table_init,
};

/* The work comes in here from netfilter.c. */
static unsigned int
iptable_raw_hook(void *priv, struct sk_buff *skb,
		 const struct nf_hook_state *state)
{
	if (state->hook == NF_INET_LOCAL_OUT &&
	    (skb->len < sizeof(struct iphdr) ||
	     ip_hdrlen(skb) < sizeof(struct iphdr)))
		/* root is playing with raw sockets. */
		return NF_ACCEPT;

	return ipt_do_table(skb, state, state->net->ipv4.iptable_raw);
}

static struct nf_hook_ops *rawtable_ops __read_mostly;

static int __net_init iptable_raw_table_init(struct net *net)
{
	struct ipt_replace *repl;
	int ret;

	if (net->ipv4.iptable_raw)
		return 0;

	repl = ipt_alloc_initial_table(&packet_raw);
	if (repl == NULL)
		return -ENOMEM;
	ret = ipt_register_table(net, &packet_raw, repl, rawtable_ops,
				 &net->ipv4.iptable_raw);
	kfree(repl);
	return ret;
}

static void __net_exit iptable_raw_net_exit(struct net *net)
{
	if (!net->ipv4.iptable_raw)
		return;
	ipt_unregister_table(net, net->ipv4.iptable_raw, rawtable_ops);
	net->ipv4.iptable_raw = NULL;
}

static struct pernet_operations iptable_raw_net_ops = {
	.exit = iptable_raw_net_exit,
};

static int __init iptable_raw_init(void)
{
	int ret;

	rawtable_ops = xt_hook_ops_alloc(&packet_raw, iptable_raw_hook);
	if (IS_ERR(rawtable_ops))
		return PTR_ERR(rawtable_ops);

	ret = register_pernet_subsys(&iptable_raw_net_ops);
	if (ret < 0) {
		kfree(rawtable_ops);
		return ret;
	}

	ret = iptable_raw_table_init(&init_net);
	if (ret) {
		unregister_pernet_subsys(&iptable_raw_net_ops);
		kfree(rawtable_ops);
	}

	return ret;
}

static void __exit iptable_raw_fini(void)
{
	unregister_pernet_subsys(&iptable_raw_net_ops);
	kfree(rawtable_ops);
}

module_init(iptable_raw_init);
module_exit(iptable_raw_fini);
MODULE_LICENSE("GPL");
