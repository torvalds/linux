/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/netfilter_ipv4.h>
#include <net/netfilter/nf_tables.h>
#include <net/ip.h>

static unsigned int nft_ipv4_output(const struct nf_hook_ops *ops,
				    struct sk_buff *skb,
				    const struct net_device *in,
				    const struct net_device *out,
				    int (*okfn)(struct sk_buff *))
{
	if (unlikely(skb->len < sizeof(struct iphdr) ||
		     ip_hdr(skb)->ihl < sizeof(struct iphdr) / 4)) {
		if (net_ratelimit())
			pr_info("nf_tables_ipv4: ignoring short SOCK_RAW "
				"packet\n");
		return NF_ACCEPT;
	}

	return nft_do_chain(ops, skb, in, out, okfn);
}

static struct nft_af_info nft_af_ipv4 __read_mostly = {
	.family		= NFPROTO_IPV4,
	.nhooks		= NF_INET_NUMHOOKS,
	.owner		= THIS_MODULE,
	.hooks		= {
		[NF_INET_LOCAL_OUT]	= nft_ipv4_output,
	},
};

static int __init nf_tables_ipv4_init(void)
{
	return nft_register_afinfo(&nft_af_ipv4);
}

static void __exit nf_tables_ipv4_exit(void)
{
	nft_unregister_afinfo(&nft_af_ipv4);
}

module_init(nf_tables_ipv4_init);
module_exit(nf_tables_ipv4_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_FAMILY(AF_INET);
