/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2012 Pablo Neira Ayuso <pablo@netfilter.org>
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
#include <net/netfilter/nf_tables_ipv4.h>
#include <net/route.h>
#include <net/ip.h>

static unsigned int nf_route_table_hook(const struct nf_hook_ops *ops,
					struct sk_buff *skb,
					const struct net_device *in,
					const struct net_device *out,
					int (*okfn)(struct sk_buff *))
{
	unsigned int ret;
	struct nft_pktinfo pkt;
	u32 mark;
	__be32 saddr, daddr;
	u_int8_t tos;
	const struct iphdr *iph;

	/* root is playing with raw sockets. */
	if (skb->len < sizeof(struct iphdr) ||
	    ip_hdrlen(skb) < sizeof(struct iphdr))
		return NF_ACCEPT;

	nft_set_pktinfo_ipv4(&pkt, ops, skb, in, out);

	mark = skb->mark;
	iph = ip_hdr(skb);
	saddr = iph->saddr;
	daddr = iph->daddr;
	tos = iph->tos;

	ret = nft_do_chain_pktinfo(&pkt, ops);
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

static struct nf_chain_type nft_chain_route_ipv4 = {
	.family		= NFPROTO_IPV4,
	.name		= "route",
	.type		= NFT_CHAIN_T_ROUTE,
	.hook_mask	= (1 << NF_INET_LOCAL_OUT),
	.fn		= {
		[NF_INET_LOCAL_OUT]	= nf_route_table_hook,
	},
	.me		= THIS_MODULE,
};

static int __init nft_chain_route_init(void)
{
	return nft_register_chain_type(&nft_chain_route_ipv4);
}

static void __exit nft_chain_route_exit(void)
{
	nft_unregister_chain_type(&nft_chain_route_ipv4);
}

module_init(nft_chain_route_init);
module_exit(nft_chain_route_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_CHAIN(AF_INET, "route");
