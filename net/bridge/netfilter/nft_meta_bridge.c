// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nft_meta.h>
#include <linux/if_bridge.h>
#include <uapi/linux/netfilter_bridge.h> /* NF_BR_PRE_ROUTING */

#include "../br_private.h"

static const struct net_device *
nft_meta_get_bridge(const struct net_device *dev)
{
	if (dev && netif_is_bridge_port(dev))
		return netdev_master_upper_dev_get_rcu((struct net_device *)dev);

	return NULL;
}

static void nft_meta_bridge_get_eval(const struct nft_expr *expr,
				     struct nft_regs *regs,
				     const struct nft_pktinfo *pkt)
{
	const struct nft_meta *priv = nft_expr_priv(expr);
	const struct net_device *in = nft_in(pkt), *out = nft_out(pkt);
	u32 *dest = &regs->data[priv->dreg];
	const struct net_device *br_dev;

	switch (priv->key) {
	case NFT_META_BRI_IIFNAME:
		br_dev = nft_meta_get_bridge(in);
		break;
	case NFT_META_BRI_OIFNAME:
		br_dev = nft_meta_get_bridge(out);
		break;
	case NFT_META_BRI_IIFPVID: {
		u16 p_pvid;

		br_dev = nft_meta_get_bridge(in);
		if (!br_dev || !br_vlan_enabled(br_dev))
			goto err;

		br_vlan_get_pvid_rcu(in, &p_pvid);
		nft_reg_store16(dest, p_pvid);
		return;
	}
	case NFT_META_BRI_IIFVPROTO: {
		u16 p_proto;

		br_dev = nft_meta_get_bridge(in);
		if (!br_dev || !br_vlan_enabled(br_dev))
			goto err;

		br_vlan_get_proto(br_dev, &p_proto);
		nft_reg_store_be16(dest, htons(p_proto));
		return;
	}
	default:
		return nft_meta_get_eval(expr, regs, pkt);
	}

	strscpy_pad((char *)dest, br_dev ? br_dev->name : "", IFNAMSIZ);
	return;
err:
	regs->verdict.code = NFT_BREAK;
}

static int nft_meta_bridge_get_init(const struct nft_ctx *ctx,
				    const struct nft_expr *expr,
				    const struct nlattr * const tb[])
{
	struct nft_meta *priv = nft_expr_priv(expr);
	unsigned int len;

	priv->key = ntohl(nla_get_be32(tb[NFTA_META_KEY]));
	switch (priv->key) {
	case NFT_META_BRI_IIFNAME:
	case NFT_META_BRI_OIFNAME:
		len = IFNAMSIZ;
		break;
	case NFT_META_BRI_IIFPVID:
	case NFT_META_BRI_IIFVPROTO:
		len = sizeof(u16);
		break;
	default:
		return nft_meta_get_init(ctx, expr, tb);
	}

	priv->len = len;
	return nft_parse_register_store(ctx, tb[NFTA_META_DREG], &priv->dreg,
					NULL, NFT_DATA_VALUE, len);
}

static struct nft_expr_type nft_meta_bridge_type;
static const struct nft_expr_ops nft_meta_bridge_get_ops = {
	.type		= &nft_meta_bridge_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_meta)),
	.eval		= nft_meta_bridge_get_eval,
	.init		= nft_meta_bridge_get_init,
	.dump		= nft_meta_get_dump,
	.reduce		= nft_meta_get_reduce,
};

static void nft_meta_bridge_set_eval(const struct nft_expr *expr,
				     struct nft_regs *regs,
				     const struct nft_pktinfo *pkt)
{
	const struct nft_meta *meta = nft_expr_priv(expr);
	u32 *sreg = &regs->data[meta->sreg];
	struct sk_buff *skb = pkt->skb;
	u8 value8;

	switch (meta->key) {
	case NFT_META_BRI_BROUTE:
		value8 = nft_reg_load8(sreg);
		BR_INPUT_SKB_CB(skb)->br_netfilter_broute = !!value8;
		break;
	default:
		nft_meta_set_eval(expr, regs, pkt);
	}
}

static int nft_meta_bridge_set_init(const struct nft_ctx *ctx,
				    const struct nft_expr *expr,
				    const struct nlattr * const tb[])
{
	struct nft_meta *priv = nft_expr_priv(expr);
	unsigned int len;
	int err;

	priv->key = ntohl(nla_get_be32(tb[NFTA_META_KEY]));
	switch (priv->key) {
	case NFT_META_BRI_BROUTE:
		len = sizeof(u8);
		break;
	default:
		return nft_meta_set_init(ctx, expr, tb);
	}

	priv->len = len;
	err = nft_parse_register_load(ctx, tb[NFTA_META_SREG], &priv->sreg, len);
	if (err < 0)
		return err;

	return 0;
}

static bool nft_meta_bridge_set_reduce(struct nft_regs_track *track,
				       const struct nft_expr *expr)
{
	int i;

	for (i = 0; i < NFT_REG32_NUM; i++) {
		if (!track->regs[i].selector)
			continue;

		if (track->regs[i].selector->ops != &nft_meta_bridge_get_ops)
			continue;

		__nft_reg_track_cancel(track, i);
	}

	return false;
}

static int nft_meta_bridge_set_validate(const struct nft_ctx *ctx,
					const struct nft_expr *expr)
{
	struct nft_meta *priv = nft_expr_priv(expr);
	unsigned int hooks;

	switch (priv->key) {
	case NFT_META_BRI_BROUTE:
		hooks = 1 << NF_BR_PRE_ROUTING;
		break;
	default:
		return nft_meta_set_validate(ctx, expr);
	}

	return nft_chain_validate_hooks(ctx->chain, hooks);
}

static const struct nft_expr_ops nft_meta_bridge_set_ops = {
	.type		= &nft_meta_bridge_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_meta)),
	.eval		= nft_meta_bridge_set_eval,
	.init		= nft_meta_bridge_set_init,
	.destroy	= nft_meta_set_destroy,
	.dump		= nft_meta_set_dump,
	.reduce		= nft_meta_bridge_set_reduce,
	.validate	= nft_meta_bridge_set_validate,
};

static const struct nft_expr_ops *
nft_meta_bridge_select_ops(const struct nft_ctx *ctx,
			   const struct nlattr * const tb[])
{
	if (tb[NFTA_META_KEY] == NULL)
		return ERR_PTR(-EINVAL);

	if (tb[NFTA_META_DREG] && tb[NFTA_META_SREG])
		return ERR_PTR(-EINVAL);

	if (tb[NFTA_META_DREG])
		return &nft_meta_bridge_get_ops;

	if (tb[NFTA_META_SREG])
		return &nft_meta_bridge_set_ops;

	return ERR_PTR(-EINVAL);
}

static struct nft_expr_type nft_meta_bridge_type __read_mostly = {
	.family         = NFPROTO_BRIDGE,
	.name           = "meta",
	.select_ops     = nft_meta_bridge_select_ops,
	.policy         = nft_meta_policy,
	.maxattr        = NFTA_META_MAX,
	.owner          = THIS_MODULE,
};

static int __init nft_meta_bridge_module_init(void)
{
	return nft_register_expr(&nft_meta_bridge_type);
}

static void __exit nft_meta_bridge_module_exit(void)
{
	nft_unregister_expr(&nft_meta_bridge_type);
}

module_init(nft_meta_bridge_module_init);
module_exit(nft_meta_bridge_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("wenxu <wenxu@ucloud.cn>");
MODULE_ALIAS_NFT_AF_EXPR(AF_BRIDGE, "meta");
MODULE_DESCRIPTION("Support for bridge dedicated meta key");
