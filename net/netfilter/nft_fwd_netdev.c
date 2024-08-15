// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 Pablo Neira Ayuso <pablo@netfilter.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_offload.h>
#include <net/netfilter/nf_dup_netdev.h>
#include <net/neighbour.h>
#include <net/ip.h>

struct nft_fwd_netdev {
	u8	sreg_dev;
};

static void nft_fwd_netdev_eval(const struct nft_expr *expr,
				struct nft_regs *regs,
				const struct nft_pktinfo *pkt)
{
	struct nft_fwd_netdev *priv = nft_expr_priv(expr);
	int oif = regs->data[priv->sreg_dev];
	struct sk_buff *skb = pkt->skb;

	/* This is used by ifb only. */
	skb->skb_iif = skb->dev->ifindex;
	skb_set_redirected(skb, nft_hook(pkt) == NF_NETDEV_INGRESS);

	nf_fwd_netdev_egress(pkt, oif);
	regs->verdict.code = NF_STOLEN;
}

static const struct nla_policy nft_fwd_netdev_policy[NFTA_FWD_MAX + 1] = {
	[NFTA_FWD_SREG_DEV]	= { .type = NLA_U32 },
	[NFTA_FWD_SREG_ADDR]	= { .type = NLA_U32 },
	[NFTA_FWD_NFPROTO]	= { .type = NLA_U32 },
};

static int nft_fwd_netdev_init(const struct nft_ctx *ctx,
			       const struct nft_expr *expr,
			       const struct nlattr * const tb[])
{
	struct nft_fwd_netdev *priv = nft_expr_priv(expr);

	if (tb[NFTA_FWD_SREG_DEV] == NULL)
		return -EINVAL;

	return nft_parse_register_load(tb[NFTA_FWD_SREG_DEV], &priv->sreg_dev,
				       sizeof(int));
}

static int nft_fwd_netdev_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	struct nft_fwd_netdev *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_FWD_SREG_DEV, priv->sreg_dev))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static int nft_fwd_netdev_offload(struct nft_offload_ctx *ctx,
				  struct nft_flow_rule *flow,
				  const struct nft_expr *expr)
{
	const struct nft_fwd_netdev *priv = nft_expr_priv(expr);
	int oif = ctx->regs[priv->sreg_dev].data.data[0];

	return nft_fwd_dup_netdev_offload(ctx, flow, FLOW_ACTION_REDIRECT, oif);
}

static bool nft_fwd_netdev_offload_action(const struct nft_expr *expr)
{
	return true;
}

struct nft_fwd_neigh {
	u8			sreg_dev;
	u8			sreg_addr;
	u8			nfproto;
};

static void nft_fwd_neigh_eval(const struct nft_expr *expr,
			      struct nft_regs *regs,
			      const struct nft_pktinfo *pkt)
{
	struct nft_fwd_neigh *priv = nft_expr_priv(expr);
	void *addr = &regs->data[priv->sreg_addr];
	int oif = regs->data[priv->sreg_dev];
	unsigned int verdict = NF_STOLEN;
	struct sk_buff *skb = pkt->skb;
	struct net_device *dev;
	int neigh_table;

	switch (priv->nfproto) {
	case NFPROTO_IPV4: {
		struct iphdr *iph;

		if (skb->protocol != htons(ETH_P_IP)) {
			verdict = NFT_BREAK;
			goto out;
		}
		if (skb_try_make_writable(skb, sizeof(*iph))) {
			verdict = NF_DROP;
			goto out;
		}
		iph = ip_hdr(skb);
		ip_decrease_ttl(iph);
		neigh_table = NEIGH_ARP_TABLE;
		break;
		}
	case NFPROTO_IPV6: {
		struct ipv6hdr *ip6h;

		if (skb->protocol != htons(ETH_P_IPV6)) {
			verdict = NFT_BREAK;
			goto out;
		}
		if (skb_try_make_writable(skb, sizeof(*ip6h))) {
			verdict = NF_DROP;
			goto out;
		}
		ip6h = ipv6_hdr(skb);
		ip6h->hop_limit--;
		neigh_table = NEIGH_ND_TABLE;
		break;
		}
	default:
		verdict = NFT_BREAK;
		goto out;
	}

	dev = dev_get_by_index_rcu(nft_net(pkt), oif);
	if (dev == NULL)
		return;

	skb->dev = dev;
	skb_clear_tstamp(skb);
	neigh_xmit(neigh_table, dev, addr, skb);
out:
	regs->verdict.code = verdict;
}

static int nft_fwd_neigh_init(const struct nft_ctx *ctx,
			      const struct nft_expr *expr,
			      const struct nlattr * const tb[])
{
	struct nft_fwd_neigh *priv = nft_expr_priv(expr);
	unsigned int addr_len;
	int err;

	if (!tb[NFTA_FWD_SREG_DEV] ||
	    !tb[NFTA_FWD_SREG_ADDR] ||
	    !tb[NFTA_FWD_NFPROTO])
		return -EINVAL;

	priv->nfproto = ntohl(nla_get_be32(tb[NFTA_FWD_NFPROTO]));

	switch (priv->nfproto) {
	case NFPROTO_IPV4:
		addr_len = sizeof(struct in_addr);
		break;
	case NFPROTO_IPV6:
		addr_len = sizeof(struct in6_addr);
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = nft_parse_register_load(tb[NFTA_FWD_SREG_DEV], &priv->sreg_dev,
				      sizeof(int));
	if (err < 0)
		return err;

	return nft_parse_register_load(tb[NFTA_FWD_SREG_ADDR], &priv->sreg_addr,
				       addr_len);
}

static int nft_fwd_neigh_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	struct nft_fwd_neigh *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_FWD_SREG_DEV, priv->sreg_dev) ||
	    nft_dump_register(skb, NFTA_FWD_SREG_ADDR, priv->sreg_addr) ||
	    nla_put_be32(skb, NFTA_FWD_NFPROTO, htonl(priv->nfproto)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static int nft_fwd_validate(const struct nft_ctx *ctx,
			    const struct nft_expr *expr,
			    const struct nft_data **data)
{
	return nft_chain_validate_hooks(ctx->chain, (1 << NF_NETDEV_INGRESS) |
						    (1 << NF_NETDEV_EGRESS));
}

static struct nft_expr_type nft_fwd_netdev_type;
static const struct nft_expr_ops nft_fwd_neigh_netdev_ops = {
	.type		= &nft_fwd_netdev_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_fwd_neigh)),
	.eval		= nft_fwd_neigh_eval,
	.init		= nft_fwd_neigh_init,
	.dump		= nft_fwd_neigh_dump,
	.validate	= nft_fwd_validate,
	.reduce		= NFT_REDUCE_READONLY,
};

static const struct nft_expr_ops nft_fwd_netdev_ops = {
	.type		= &nft_fwd_netdev_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_fwd_netdev)),
	.eval		= nft_fwd_netdev_eval,
	.init		= nft_fwd_netdev_init,
	.dump		= nft_fwd_netdev_dump,
	.validate	= nft_fwd_validate,
	.reduce		= NFT_REDUCE_READONLY,
	.offload	= nft_fwd_netdev_offload,
	.offload_action	= nft_fwd_netdev_offload_action,
};

static const struct nft_expr_ops *
nft_fwd_select_ops(const struct nft_ctx *ctx,
		   const struct nlattr * const tb[])
{
	if (tb[NFTA_FWD_SREG_ADDR])
		return &nft_fwd_neigh_netdev_ops;
	if (tb[NFTA_FWD_SREG_DEV])
		return &nft_fwd_netdev_ops;

        return ERR_PTR(-EOPNOTSUPP);
}

static struct nft_expr_type nft_fwd_netdev_type __read_mostly = {
	.family		= NFPROTO_NETDEV,
	.name		= "fwd",
	.select_ops	= nft_fwd_select_ops,
	.policy		= nft_fwd_netdev_policy,
	.maxattr	= NFTA_FWD_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_fwd_netdev_module_init(void)
{
	return nft_register_expr(&nft_fwd_netdev_type);
}

static void __exit nft_fwd_netdev_module_exit(void)
{
	nft_unregister_expr(&nft_fwd_netdev_type);
}

module_init(nft_fwd_netdev_module_init);
module_exit(nft_fwd_netdev_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NFT_AF_EXPR(5, "fwd");
