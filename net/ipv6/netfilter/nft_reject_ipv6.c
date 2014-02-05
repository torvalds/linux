/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2013 Eric Leblond <eric@regit.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nft_reject.h>
#include <net/netfilter/ipv6/nf_reject.h>

static void nft_reject_ipv6_eval(const struct nft_expr *expr,
				 struct nft_data data[NFT_REG_MAX + 1],
				 const struct nft_pktinfo *pkt)
{
	struct nft_reject *priv = nft_expr_priv(expr);
	struct net *net = dev_net((pkt->in != NULL) ? pkt->in : pkt->out);

	switch (priv->type) {
	case NFT_REJECT_ICMP_UNREACH:
		nf_send_unreach6(net, pkt->skb, priv->icmp_code,
				 pkt->ops->hooknum);
		break;
	case NFT_REJECT_TCP_RST:
		nf_send_reset6(net, pkt->skb, pkt->ops->hooknum);
		break;
	}

	data[NFT_REG_VERDICT].verdict = NF_DROP;
}

static struct nft_expr_type nft_reject_ipv6_type;
static const struct nft_expr_ops nft_reject_ipv6_ops = {
	.type		= &nft_reject_ipv6_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_reject)),
	.eval		= nft_reject_ipv6_eval,
	.init		= nft_reject_init,
	.dump		= nft_reject_dump,
};

static struct nft_expr_type nft_reject_ipv6_type __read_mostly = {
	.family		= NFPROTO_IPV6,
	.name		= "reject",
	.ops		= &nft_reject_ipv6_ops,
	.policy		= nft_reject_policy,
	.maxattr	= NFTA_REJECT_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_reject_ipv6_module_init(void)
{
	return nft_register_expr(&nft_reject_ipv6_type);
}

static void __exit nft_reject_ipv6_module_exit(void)
{
	nft_unregister_expr(&nft_reject_ipv6_type);
}

module_init(nft_reject_ipv6_module_init);
module_exit(nft_reject_ipv6_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_AF_EXPR(AF_INET6, "reject");
