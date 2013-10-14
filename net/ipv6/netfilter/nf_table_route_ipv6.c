/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/route.h>

static unsigned int nf_route_table_hook(const struct nf_hook_ops *ops,
					struct sk_buff *skb,
					const struct net_device *in,
					const struct net_device *out,
					int (*okfn)(struct sk_buff *))
{
	unsigned int ret;
	struct in6_addr saddr, daddr;
	u_int8_t hop_limit;
	u32 mark, flowlabel;

	/* save source/dest address, mark, hoplimit, flowlabel, priority */
	memcpy(&saddr, &ipv6_hdr(skb)->saddr, sizeof(saddr));
	memcpy(&daddr, &ipv6_hdr(skb)->daddr, sizeof(daddr));
	mark = skb->mark;
	hop_limit = ipv6_hdr(skb)->hop_limit;

	/* flowlabel and prio (includes version, which shouldn't change either */
	flowlabel = *((u32 *)ipv6_hdr(skb));

	ret = nft_do_chain(ops, skb, in, out, okfn);
	if (ret != NF_DROP && ret != NF_QUEUE &&
	    (memcmp(&ipv6_hdr(skb)->saddr, &saddr, sizeof(saddr)) ||
	     memcmp(&ipv6_hdr(skb)->daddr, &daddr, sizeof(daddr)) ||
	     skb->mark != mark ||
	     ipv6_hdr(skb)->hop_limit != hop_limit ||
	     flowlabel != *((u_int32_t *)ipv6_hdr(skb))))
		return ip6_route_me_harder(skb) == 0 ? ret : NF_DROP;

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
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_MANGLE,
		.priv		= &nf_chain_route_output.chain,
	},
};

static struct nft_table nf_table_route_ipv6 __read_mostly = {
	.name	= "route",
	.chains	= LIST_HEAD_INIT(nf_table_route_ipv6.chains),
};

static int __init nf_table_route_init(void)
{
	list_add_tail(&nf_chain_route_output.chain.list,
		      &nf_table_route_ipv6.chains);
	return nft_register_table(&nf_table_route_ipv6, NFPROTO_IPV6);
}

static void __exit nf_table_route_exit(void)
{
	nft_unregister_table(&nf_table_route_ipv6, NFPROTO_IPV6);
}

module_init(nf_table_route_init);
module_exit(nf_table_route_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_TABLE(AF_INET6, "route");
