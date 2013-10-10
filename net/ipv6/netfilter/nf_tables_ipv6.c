/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2012-2013 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ipv6.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_tables.h>

static unsigned int nft_ipv6_output(const struct nf_hook_ops *ops,
				    struct sk_buff *skb,
				    const struct net_device *in,
				    const struct net_device *out,
				    int (*okfn)(struct sk_buff *))
{
	if (unlikely(skb->len < sizeof(struct ipv6hdr))) {
		if (net_ratelimit())
			pr_info("nf_tables_ipv6: ignoring short SOCK_RAW "
				"packet\n");
		return NF_ACCEPT;
	}

	return nft_do_chain(ops, skb, in, out, okfn);
}

static struct nft_af_info nft_af_ipv6 __read_mostly = {
	.family		= NFPROTO_IPV6,
	.nhooks		= NF_INET_NUMHOOKS,
	.owner		= THIS_MODULE,
	.hooks		= {
		[NF_INET_LOCAL_OUT]	= nft_ipv6_output,
	},
};

static struct nf_chain_type filter_ipv6 = {
	.family		= NFPROTO_IPV6,
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.hook_mask	= (1 << NF_INET_LOCAL_IN) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_FORWARD) |
			  (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING),
	.fn		= {
		[NF_INET_LOCAL_IN]	= nft_do_chain,
		[NF_INET_LOCAL_OUT]	= nft_do_chain,
		[NF_INET_FORWARD]	= nft_do_chain,
		[NF_INET_PRE_ROUTING]	= nft_do_chain,
		[NF_INET_POST_ROUTING]	= nft_do_chain,
	},
};

static int __init nf_tables_ipv6_init(void)
{
	nft_register_chain_type(&filter_ipv6);
	return nft_register_afinfo(&nft_af_ipv6);
}
static void __exit nf_tables_ipv6_exit(void)
{
	nft_unregister_afinfo(&nft_af_ipv6);
	nft_unregister_chain_type(&filter_ipv6);
}

module_init(nf_tables_ipv6_init);
module_exit(nf_tables_ipv6_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_FAMILY(AF_INET6);
