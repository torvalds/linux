/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/route.h>
#include <net/ip.h>

static unsigned int nf_route_table_hook(const struct nf_hook_ops *ops,
					struct sk_buff *skb,
					const struct net_device *in,
					const struct net_device *out,
					int (*okfn)(struct sk_buff *))
{
	unsigned int ret;
	u32 mark;
	__be32 saddr, daddr;
	u_int8_t tos;
	const struct iphdr *iph;

	/* root is playing with raw sockets. */
	if (skb->len < sizeof(struct iphdr) ||
	    ip_hdrlen(skb) < sizeof(struct iphdr))
		return NF_ACCEPT;

	mark = skb->mark;
	iph = ip_hdr(skb);
	saddr = iph->saddr;
	daddr = iph->daddr;
	tos = iph->tos;

	ret = nft_do_chain(ops, skb, in, out, okfn);
	if (ret != NF_DROP && ret != NF_QUEUE) {
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

static struct nft_base_chain nf_chain_route_output __read_mostly = {
	.chain	= {
		.name		= "OUTPUT",
		.rules		= LIST_HEAD_INIT(nf_chain_route_output.chain.rules),
		.flags		= NFT_BASE_CHAIN | NFT_CHAIN_BUILTIN,
	},
	.ops	= {
		.hook		= nf_route_table_hook,
		.owner		= THIS_MODULE,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_MANGLE,
		.priv		= &nf_chain_route_output.chain,
	},
};

static struct nft_table nf_table_route_ipv4 __read_mostly = {
	.name	= "route",
	.chains	= LIST_HEAD_INIT(nf_table_route_ipv4.chains),
};

static int __init nf_table_route_init(void)
{
	list_add_tail(&nf_chain_route_output.chain.list,
		      &nf_table_route_ipv4.chains);
	return nft_register_table(&nf_table_route_ipv4, NFPROTO_IPV4);
}

static void __exit nf_table_route_exit(void)
{
	nft_unregister_table(&nf_table_route_ipv4, NFPROTO_IPV4);
}

module_init(nf_table_route_init);
module_exit(nf_table_route_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_TABLE(AF_INET, "route");
