// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Patrick McHardy <kaber@trash.net>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nft_reject.h>
#include <net/netfilter/ipv4/nf_reject.h>
#include <net/netfilter/ipv6/nf_reject.h>

static void nft_reject_inet_eval(const struct nft_expr *expr,
				 struct nft_regs *regs,
				 const struct nft_pktinfo *pkt)
{
	struct nft_reject *priv = nft_expr_priv(expr);

	switch (nft_pf(pkt)) {
	case NFPROTO_IPV4:
		switch (priv->type) {
		case NFT_REJECT_ICMP_UNREACH:
			nf_send_unreach(pkt->skb, priv->icmp_code,
					nft_hook(pkt));
			break;
		case NFT_REJECT_TCP_RST:
			nf_send_reset(nft_net(pkt), pkt->xt.state->sk,
				      pkt->skb, nft_hook(pkt));
			break;
		case NFT_REJECT_ICMPX_UNREACH:
			nf_send_unreach(pkt->skb,
					nft_reject_icmp_code(priv->icmp_code),
					nft_hook(pkt));
			break;
		}
		break;
	case NFPROTO_IPV6:
		switch (priv->type) {
		case NFT_REJECT_ICMP_UNREACH:
			nf_send_unreach6(nft_net(pkt), pkt->skb,
					 priv->icmp_code, nft_hook(pkt));
			break;
		case NFT_REJECT_TCP_RST:
			nf_send_reset6(nft_net(pkt), pkt->xt.state->sk,
				       pkt->skb, nft_hook(pkt));
			break;
		case NFT_REJECT_ICMPX_UNREACH:
			nf_send_unreach6(nft_net(pkt), pkt->skb,
					 nft_reject_icmpv6_code(priv->icmp_code),
					 nft_hook(pkt));
			break;
		}
		break;
	}

	regs->verdict.code = NF_DROP;
}

static int nft_reject_inet_validate(const struct nft_ctx *ctx,
				    const struct nft_expr *expr,
				    const struct nft_data **data)
{
	return nft_chain_validate_hooks(ctx->chain,
					(1 << NF_INET_LOCAL_IN) |
					(1 << NF_INET_FORWARD) |
					(1 << NF_INET_LOCAL_OUT) |
					(1 << NF_INET_PRE_ROUTING) |
					(1 << NF_INET_INGRESS));
}

static struct nft_expr_type nft_reject_inet_type;
static const struct nft_expr_ops nft_reject_inet_ops = {
	.type		= &nft_reject_inet_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_reject)),
	.eval		= nft_reject_inet_eval,
	.init		= nft_reject_init,
	.dump		= nft_reject_dump,
	.validate	= nft_reject_inet_validate,
};

static struct nft_expr_type nft_reject_inet_type __read_mostly = {
	.family		= NFPROTO_INET,
	.name		= "reject",
	.ops		= &nft_reject_inet_ops,
	.policy		= nft_reject_policy,
	.maxattr	= NFTA_REJECT_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_reject_inet_module_init(void)
{
	return nft_register_expr(&nft_reject_inet_type);
}

static void __exit nft_reject_inet_module_exit(void)
{
	nft_unregister_expr(&nft_reject_inet_type);
}

module_init(nft_reject_inet_module_init);
module_exit(nft_reject_inet_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_AF_EXPR(1, "reject");
MODULE_DESCRIPTION("Netfilter nftables reject inet support");
