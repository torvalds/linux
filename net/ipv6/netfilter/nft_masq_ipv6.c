/*
 * Copyright (c) 2014 Arturo Borrero Gonzalez <arturo.borrero.glez@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nft_masq.h>
#include <net/netfilter/ipv6/nf_nat_masquerade.h>

static void nft_masq_ipv6_eval(const struct nft_expr *expr,
			       struct nft_data data[NFT_REG_MAX + 1],
			       const struct nft_pktinfo *pkt)
{
	struct nft_masq *priv = nft_expr_priv(expr);
	struct nf_nat_range range;
	unsigned int verdict;

	range.flags = priv->flags;

	verdict = nf_nat_masquerade_ipv6(pkt->skb, &range, pkt->out);

	data[NFT_REG_VERDICT].verdict = verdict;
}

static struct nft_expr_type nft_masq_ipv6_type;
static const struct nft_expr_ops nft_masq_ipv6_ops = {
	.type		= &nft_masq_ipv6_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_masq)),
	.eval		= nft_masq_ipv6_eval,
	.init		= nft_masq_init,
	.dump		= nft_masq_dump,
	.validate	= nft_masq_validate,
};

static struct nft_expr_type nft_masq_ipv6_type __read_mostly = {
	.family		= NFPROTO_IPV6,
	.name		= "masq",
	.ops		= &nft_masq_ipv6_ops,
	.policy		= nft_masq_policy,
	.maxattr	= NFTA_MASQ_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_masq_ipv6_module_init(void)
{
	int ret;

	ret = nft_register_expr(&nft_masq_ipv6_type);
	if (ret < 0)
		return ret;

	nf_nat_masquerade_ipv6_register_notifier();

	return ret;
}

static void __exit nft_masq_ipv6_module_exit(void)
{
	nft_unregister_expr(&nft_masq_ipv6_type);
	nf_nat_masquerade_ipv6_unregister_notifier();
}

module_init(nft_masq_ipv6_module_init);
module_exit(nft_masq_ipv6_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arturo Borrero Gonzalez <arturo.borrero.glez@gmail.com>");
MODULE_ALIAS_NFT_AF_EXPR(AF_INET6, "masq");
