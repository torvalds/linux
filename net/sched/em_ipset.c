/*
 * net/sched/em_ipset.c	ipset ematch
 *
 * Copyright (c) 2012 Florian Westphal <fw@strlen.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/netfilter/xt_set.h>
#include <linux/ipv6.h>
#include <net/ip.h>
#include <net/pkt_cls.h>

static int em_ipset_change(struct net *net, void *data, int data_len,
			   struct tcf_ematch *em)
{
	struct xt_set_info *set = data;
	ip_set_id_t index;

	if (data_len != sizeof(*set))
		return -EINVAL;

	index = ip_set_nfnl_get_byindex(net, set->index);
	if (index == IPSET_INVALID_ID)
		return -ENOENT;

	em->datalen = sizeof(*set);
	em->data = (unsigned long)kmemdup(data, em->datalen, GFP_KERNEL);
	if (em->data)
		return 0;

	ip_set_nfnl_put(net, index);
	return -ENOMEM;
}

static void em_ipset_destroy(struct tcf_ematch *em)
{
	const struct xt_set_info *set = (const void *) em->data;
	if (set) {
		ip_set_nfnl_put(em->net, set->index);
		kfree((void *) em->data);
	}
}

static int em_ipset_match(struct sk_buff *skb, struct tcf_ematch *em,
			  struct tcf_pkt_info *info)
{
	struct ip_set_adt_opt opt;
	struct xt_action_param acpar;
	const struct xt_set_info *set = (const void *) em->data;
	struct net_device *dev, *indev = NULL;
	struct nf_hook_state state = {
		.net	= em->net,
	};
	int ret, network_offset;

	switch (skb_protocol(skb, true)) {
	case htons(ETH_P_IP):
		state.pf = NFPROTO_IPV4;
		if (!pskb_network_may_pull(skb, sizeof(struct iphdr)))
			return 0;
		acpar.thoff = ip_hdrlen(skb);
		break;
	case htons(ETH_P_IPV6):
		state.pf = NFPROTO_IPV6;
		if (!pskb_network_may_pull(skb, sizeof(struct ipv6hdr)))
			return 0;
		/* doesn't call ipv6_find_hdr() because ipset doesn't use thoff, yet */
		acpar.thoff = sizeof(struct ipv6hdr);
		break;
	default:
		return 0;
	}

	opt.family = state.pf;
	opt.dim = set->dim;
	opt.flags = set->flags;
	opt.cmdflags = 0;
	opt.ext.timeout = ~0u;

	network_offset = skb_network_offset(skb);
	skb_pull(skb, network_offset);

	dev = skb->dev;

	rcu_read_lock();

	if (skb->skb_iif)
		indev = dev_get_by_index_rcu(em->net, skb->skb_iif);

	state.in      = indev ? indev : dev;
	state.out     = dev;
	acpar.state   = &state;

	ret = ip_set_test(set->index, skb, &acpar, &opt);

	rcu_read_unlock();

	skb_push(skb, network_offset);
	return ret;
}

static struct tcf_ematch_ops em_ipset_ops = {
	.kind	  = TCF_EM_IPSET,
	.change	  = em_ipset_change,
	.destroy  = em_ipset_destroy,
	.match	  = em_ipset_match,
	.owner	  = THIS_MODULE,
	.link	  = LIST_HEAD_INIT(em_ipset_ops.link)
};

static int __init init_em_ipset(void)
{
	return tcf_em_register(&em_ipset_ops);
}

static void __exit exit_em_ipset(void)
{
	tcf_em_unregister(&em_ipset_ops);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Westphal <fw@strlen.de>");
MODULE_DESCRIPTION("TC extended match for IP sets");

module_init(init_em_ipset);
module_exit(exit_em_ipset);

MODULE_ALIAS_TCF_EMATCH(TCF_EM_IPSET);
