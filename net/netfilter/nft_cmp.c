// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/if_arp.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables_offload.h>
#include <net/netfilter/nf_tables.h>

struct nft_cmp_expr {
	struct nft_data		data;
	enum nft_registers	sreg:8;
	u8			len;
	enum nft_cmp_ops	op:8;
};

void nft_cmp_eval(const struct nft_expr *expr,
		  struct nft_regs *regs,
		  const struct nft_pktinfo *pkt)
{
	const struct nft_cmp_expr *priv = nft_expr_priv(expr);
	int d;

	d = memcmp(&regs->data[priv->sreg], &priv->data, priv->len);
	switch (priv->op) {
	case NFT_CMP_EQ:
		if (d != 0)
			goto mismatch;
		break;
	case NFT_CMP_NEQ:
		if (d == 0)
			goto mismatch;
		break;
	case NFT_CMP_LT:
		if (d == 0)
			goto mismatch;
		fallthrough;
	case NFT_CMP_LTE:
		if (d > 0)
			goto mismatch;
		break;
	case NFT_CMP_GT:
		if (d == 0)
			goto mismatch;
		fallthrough;
	case NFT_CMP_GTE:
		if (d < 0)
			goto mismatch;
		break;
	}
	return;

mismatch:
	regs->verdict.code = NFT_BREAK;
}

static const struct nla_policy nft_cmp_policy[NFTA_CMP_MAX + 1] = {
	[NFTA_CMP_SREG]		= { .type = NLA_U32 },
	[NFTA_CMP_OP]		= { .type = NLA_U32 },
	[NFTA_CMP_DATA]		= { .type = NLA_NESTED },
};

static int nft_cmp_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
			const struct nlattr * const tb[])
{
	struct nft_cmp_expr *priv = nft_expr_priv(expr);
	struct nft_data_desc desc;
	int err;

	err = nft_data_init(NULL, &priv->data, sizeof(priv->data), &desc,
			    tb[NFTA_CMP_DATA]);
	if (err < 0)
		return err;

	if (desc.type != NFT_DATA_VALUE) {
		err = -EINVAL;
		nft_data_release(&priv->data, desc.type);
		return err;
	}

	priv->sreg = nft_parse_register(tb[NFTA_CMP_SREG]);
	err = nft_validate_register_load(priv->sreg, desc.len);
	if (err < 0)
		return err;

	priv->op  = ntohl(nla_get_be32(tb[NFTA_CMP_OP]));
	priv->len = desc.len;
	return 0;
}

static int nft_cmp_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_cmp_expr *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_CMP_SREG, priv->sreg))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_CMP_OP, htonl(priv->op)))
		goto nla_put_failure;

	if (nft_data_dump(skb, NFTA_CMP_DATA, &priv->data,
			  NFT_DATA_VALUE, priv->len) < 0)
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static int __nft_cmp_offload(struct nft_offload_ctx *ctx,
			     struct nft_flow_rule *flow,
			     const struct nft_cmp_expr *priv)
{
	struct nft_offload_reg *reg = &ctx->regs[priv->sreg];
	u8 *mask = (u8 *)&flow->match.mask;
	u8 *key = (u8 *)&flow->match.key;

	if (priv->op != NFT_CMP_EQ || reg->len != priv->len)
		return -EOPNOTSUPP;

	memcpy(key + reg->offset, &priv->data, priv->len);
	memcpy(mask + reg->offset, &reg->mask, priv->len);

	flow->match.dissector.used_keys |= BIT(reg->key);
	flow->match.dissector.offset[reg->key] = reg->base_offset;

	if (reg->key == FLOW_DISSECTOR_KEY_META &&
	    reg->offset == offsetof(struct nft_flow_key, meta.ingress_iftype) &&
	    nft_reg_load16(priv->data.data) != ARPHRD_ETHER)
		return -EOPNOTSUPP;

	nft_offload_update_dependency(ctx, &priv->data, priv->len);

	return 0;
}

static int nft_cmp_offload(struct nft_offload_ctx *ctx,
			   struct nft_flow_rule *flow,
			   const struct nft_expr *expr)
{
	const struct nft_cmp_expr *priv = nft_expr_priv(expr);

	return __nft_cmp_offload(ctx, flow, priv);
}

static const struct nft_expr_ops nft_cmp_ops = {
	.type		= &nft_cmp_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_cmp_expr)),
	.eval		= nft_cmp_eval,
	.init		= nft_cmp_init,
	.dump		= nft_cmp_dump,
	.offload	= nft_cmp_offload,
};

static int nft_cmp_fast_init(const struct nft_ctx *ctx,
			     const struct nft_expr *expr,
			     const struct nlattr * const tb[])
{
	struct nft_cmp_fast_expr *priv = nft_expr_priv(expr);
	struct nft_data_desc desc;
	struct nft_data data;
	int err;

	err = nft_data_init(NULL, &data, sizeof(data), &desc,
			    tb[NFTA_CMP_DATA]);
	if (err < 0)
		return err;

	priv->sreg = nft_parse_register(tb[NFTA_CMP_SREG]);
	err = nft_validate_register_load(priv->sreg, desc.len);
	if (err < 0)
		return err;

	desc.len *= BITS_PER_BYTE;

	priv->mask = nft_cmp_fast_mask(desc.len);
	priv->data = data.data[0] & priv->mask;
	priv->len  = desc.len;
	priv->inv  = ntohl(nla_get_be32(tb[NFTA_CMP_OP])) != NFT_CMP_EQ;
	return 0;
}

static int nft_cmp_fast_offload(struct nft_offload_ctx *ctx,
				struct nft_flow_rule *flow,
				const struct nft_expr *expr)
{
	const struct nft_cmp_fast_expr *priv = nft_expr_priv(expr);
	struct nft_cmp_expr cmp = {
		.data	= {
			.data	= {
				[0] = priv->data,
			},
		},
		.sreg	= priv->sreg,
		.len	= priv->len / BITS_PER_BYTE,
		.op	= priv->inv ? NFT_CMP_NEQ : NFT_CMP_EQ,
	};

	return __nft_cmp_offload(ctx, flow, &cmp);
}

static int nft_cmp_fast_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_cmp_fast_expr *priv = nft_expr_priv(expr);
	enum nft_cmp_ops op = priv->inv ? NFT_CMP_NEQ : NFT_CMP_EQ;
	struct nft_data data;

	if (nft_dump_register(skb, NFTA_CMP_SREG, priv->sreg))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_CMP_OP, htonl(op)))
		goto nla_put_failure;

	data.data[0] = priv->data;
	if (nft_data_dump(skb, NFTA_CMP_DATA, &data,
			  NFT_DATA_VALUE, priv->len / BITS_PER_BYTE) < 0)
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

const struct nft_expr_ops nft_cmp_fast_ops = {
	.type		= &nft_cmp_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_cmp_fast_expr)),
	.eval		= NULL,	/* inlined */
	.init		= nft_cmp_fast_init,
	.dump		= nft_cmp_fast_dump,
	.offload	= nft_cmp_fast_offload,
};

static const struct nft_expr_ops *
nft_cmp_select_ops(const struct nft_ctx *ctx, const struct nlattr * const tb[])
{
	struct nft_data_desc desc;
	struct nft_data data;
	enum nft_cmp_ops op;
	int err;

	if (tb[NFTA_CMP_SREG] == NULL ||
	    tb[NFTA_CMP_OP] == NULL ||
	    tb[NFTA_CMP_DATA] == NULL)
		return ERR_PTR(-EINVAL);

	op = ntohl(nla_get_be32(tb[NFTA_CMP_OP]));
	switch (op) {
	case NFT_CMP_EQ:
	case NFT_CMP_NEQ:
	case NFT_CMP_LT:
	case NFT_CMP_LTE:
	case NFT_CMP_GT:
	case NFT_CMP_GTE:
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	err = nft_data_init(NULL, &data, sizeof(data), &desc,
			    tb[NFTA_CMP_DATA]);
	if (err < 0)
		return ERR_PTR(err);

	if (desc.type != NFT_DATA_VALUE) {
		err = -EINVAL;
		goto err1;
	}

	if (desc.len <= sizeof(u32) && (op == NFT_CMP_EQ || op == NFT_CMP_NEQ))
		return &nft_cmp_fast_ops;

	return &nft_cmp_ops;
err1:
	nft_data_release(&data, desc.type);
	return ERR_PTR(-EINVAL);
}

struct nft_expr_type nft_cmp_type __read_mostly = {
	.name		= "cmp",
	.select_ops	= nft_cmp_select_ops,
	.policy		= nft_cmp_policy,
	.maxattr	= NFTA_CMP_MAX,
	.owner		= THIS_MODULE,
};
