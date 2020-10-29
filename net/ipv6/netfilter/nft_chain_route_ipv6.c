/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2012 Pablo Neira Ayuso <pablo@netfilter.org>
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
#include <net/netfilter/nf_tables_ipv6.h>
#include <net/route.h>

static unsigned int nf_route_table_hook(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	unsigned int ret;
	struct nft_pktinfo pkt;
	struct in6_addr saddr, daddr;
	u_int8_t hop_limit;
	u32 mark, flowlabel;
	int err;

	nft_set_pktinfo(&pkt, skb, state);
	nft_set_pktinfo_ipv6(&pkt, skb);

	/* save source/dest address, mark, hoplimit, flowlabel, priority */
	memcpy(&saddr, &ipv6_hdr(skb)->saddr, sizeof(saddr));
	memcpy(&daddr, &ipv6_hdr(skb)->daddr, sizeof(daddr));
	mark = skb->mark;
	hop_limit = ipv6_hdr(skb)->hop_limit;

	/* flowlabel and prio (includes version, which shouldn't change either */
	flowlabel = *((u32 *)ipv6_hdr(skb));

	ret = nft_do_chain(&pkt, priv);
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    (memcmp(&ipv6_hdr(skb)->saddr, &saddr, sizeof(saddr)) ||
	     memcmp(&ipv6_hdr(skb)->daddr, &daddr, sizeof(daddr)) ||
	     skb->mark != mark ||
	     ipv6_hdr(skb)->hop_limit != hop_limit ||
	     flowlabel != *((u_int32_t *)ipv6_hdr(skb)))) {
		err = ip6_route_me_harder(state->net, state->sk, skb);
		if (err < 0)
			ret = NF_DROP_ERR(err);
	}

	return ret;
}

static const struct nft_chain_type nft_chain_route_ipv6 = {
	.name		= "route",
	.type		= NFT_CHAIN_T_ROUTE,
	.family		= NFPROTO_IPV6,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_INET_LOCAL_OUT),
	.hooks		= {
		[NF_INET_LOCAL_OUT]	= nf_route_table_hook,
	},
};

static int __init nft_chain_route_init(void)
{
	nft_register_chain_type(&nft_chain_route_ipv6);

	return 0;
}

static void __exit nft_chain_route_exit(void)
{
	nft_unregister_chain_type(&nft_chain_route_ipv6);
}

module_init(nft_chain_route_init);
module_exit(nft_chain_route_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_CHAIN(AF_INET6, "route");
