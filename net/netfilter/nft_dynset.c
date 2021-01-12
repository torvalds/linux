// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 Patrick McHardy <kaber@trash.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>

struct nft_dynset {
	struct nft_set			*set;
	struct nft_set_ext_tmpl		tmpl;
	enum nft_dynset_ops		op:8;
	enum nft_registers		sreg_key:8;
	enum nft_registers		sreg_data:8;
	bool				invert;
	u64				timeout;
	struct nft_expr			*expr;
	struct nft_set_binding		binding;
};

static void *nft_dynset_new(struct nft_set *set, const struct nft_expr *expr,
			    struct nft_regs *regs)
{
	const struct nft_dynset *priv = nft_expr_priv(expr);
	struct nft_set_ext *ext;
	u64 timeout;
	void *elem;

	if (!atomic_add_unless(&set->nelems, 1, set->size))
		return NULL;

	timeout = priv->timeout ? : set->timeout;
	elem = nft_set_elem_init(set, &priv->tmpl,
				 &regs->data[priv->sreg_key], NULL,
				 &regs->data[priv->sreg_data],
				 timeout, 0, GFP_ATOMIC);
	if (elem == NULL)
		goto err1;

	ext = nft_set_elem_ext(set, elem);
	if (priv->expr != NULL &&
	    nft_expr_clone(nft_set_ext_expr(ext), priv->expr) < 0)
		goto err2;

	return elem;

err2:
	nft_set_elem_destroy(set, elem, false);
err1:
	if (set->size)
		atomic_dec(&set->nelems);
	return NULL;
}

void nft_dynset_eval(const struct nft_expr *expr,
		     struct nft_regs *regs, const struct nft_pktinfo *pkt)
{
	const struct nft_dynset *priv = nft_expr_priv(expr);
	struct nft_set *set = priv->set;
	const struct nft_set_ext *ext;
	u64 timeout;

	if (priv->op == NFT_DYNSET_OP_DELETE) {
		set->ops->delete(set, &regs->data[priv->sreg_key]);
		return;
	}

	if (set->ops->update(set, &regs->data[priv->sreg_key], nft_dynset_new,
			     expr, regs, &ext)) {
		if (priv->op == NFT_DYNSET_OP_UPDATE &&
		    nft_set_ext_exists(ext, NFT_SET_EXT_EXPIRATION)) {
			timeout = priv->timeout ? : set->timeout;
			*nft_set_ext_expiration(ext) = get_jiffies_64() + timeout;
		}

		nft_set_elem_update_expr(ext, regs, pkt);

		if (priv->invert)
			regs->verdict.code = NFT_BREAK;
		return;
	}

	if (!priv->invert)
		regs->verdict.code = NFT_BREAK;
}

static const struct nla_policy nft_dynset_policy[NFTA_DYNSET_MAX + 1] = {
	[NFTA_DYNSET_SET_NAME]	= { .type = NLA_STRING,
				    .len = NFT_SET_MAXNAMELEN - 1 },
	[NFTA_DYNSET_SET_ID]	= { .type = NLA_U32 },
	[NFTA_DYNSET_OP]	= { .type = NLA_U32 },
	[NFTA_DYNSET_SREG_KEY]	= { .type = NLA_U32 },
	[NFTA_DYNSET_SREG_DATA]	= { .type = NLA_U32 },
	[NFTA_DYNSET_TIMEOUT]	= { .type = NLA_U64 },
	[NFTA_DYNSET_EXPR]	= { .type = NLA_NESTED },
	[NFTA_DYNSET_FLAGS]	= { .type = NLA_U32 },
};

static int nft_dynset_init(const struct nft_ctx *ctx,
			   const struct nft_expr *expr,
			   const struct nlattr * const tb[])
{
	struct nft_dynset *priv = nft_expr_priv(expr);
	u8 genmask = nft_genmask_next(ctx->net);
	struct nft_set *set;
	u64 timeout;
	int err;

	lockdep_assert_held(&ctx->net->nft.commit_mutex);

	if (tb[NFTA_DYNSET_SET_NAME] == NULL ||
	    tb[NFTA_DYNSET_OP] == NULL ||
	    tb[NFTA_DYNSET_SREG_KEY] == NULL)
		return -EINVAL;

	if (tb[NFTA_DYNSET_FLAGS]) {
		u32 flags = ntohl(nla_get_be32(tb[NFTA_DYNSET_FLAGS]));

		if (flags & ~NFT_DYNSET_F_INV)
			return -EOPNOTSUPP;
		if (flags & NFT_DYNSET_F_INV)
			priv->invert = true;
	}

	set = nft_set_lookup_global(ctx->net, ctx->table,
				    tb[NFTA_DYNSET_SET_NAME],
				    tb[NFTA_DYNSET_SET_ID], genmask);
	if (IS_ERR(set))
		return PTR_ERR(set);

	if (set->ops->update == NULL)
		return -EOPNOTSUPP;

	if (set->flags & NFT_SET_CONSTANT)
		return -EBUSY;

	priv->op = ntohl(nla_get_be32(tb[NFTA_DYNSET_OP]));
	switch (priv->op) {
	case NFT_DYNSET_OP_ADD:
	case NFT_DYNSET_OP_DELETE:
		break;
	case NFT_DYNSET_OP_UPDATE:
		if (!(set->flags & NFT_SET_TIMEOUT))
			return -EOPNOTSUPP;
		break;
	default:
		return -EOPNOTSUPP;
	}

	timeout = 0;
	if (tb[NFTA_DYNSET_TIMEOUT] != NULL) {
		if (!(set->flags & NFT_SET_TIMEOUT))
			return -EOPNOTSUPP;

		err = nf_msecs_to_jiffies64(tb[NFTA_DYNSET_TIMEOUT], &timeout);
		if (err)
			return err;
	}

	priv->sreg_key = nft_parse_register(tb[NFTA_DYNSET_SREG_KEY]);
	err = nft_validate_register_load(priv->sreg_key, set->klen);
	if (err < 0)
		return err;

	if (tb[NFTA_DYNSET_SREG_DATA] != NULL) {
		if (!(set->flags & NFT_SET_MAP))
			return -EOPNOTSUPP;
		if (set->dtype == NFT_DATA_VERDICT)
			return -EOPNOTSUPP;

		priv->sreg_data = nft_parse_register(tb[NFTA_DYNSET_SREG_DATA]);
		err = nft_validate_register_load(priv->sreg_data, set->dlen);
		if (err < 0)
			return err;
	} else if (set->flags & NFT_SET_MAP)
		return -EINVAL;

	if (tb[NFTA_DYNSET_EXPR] != NULL) {
		if (!(set->flags & NFT_SET_EVAL))
			return -EINVAL;

		priv->expr = nft_set_elem_expr_alloc(ctx, set,
						     tb[NFTA_DYNSET_EXPR]);
		if (IS_ERR(priv->expr))
			return PTR_ERR(priv->expr);

		if (set->expr && set->expr->ops != priv->expr->ops) {
			err = -EOPNOTSUPP;
			goto err_expr_free;
		}
	}

	nft_set_ext_prepare(&priv->tmpl);
	nft_set_ext_add_length(&priv->tmpl, NFT_SET_EXT_KEY, set->klen);
	if (set->flags & NFT_SET_MAP)
		nft_set_ext_add_length(&priv->tmpl, NFT_SET_EXT_DATA, set->dlen);
	if (priv->expr != NULL)
		nft_set_ext_add_length(&priv->tmpl, NFT_SET_EXT_EXPR,
				       priv->expr->ops->size);
	if (set->flags & NFT_SET_TIMEOUT) {
		if (timeout || set->timeout)
			nft_set_ext_add(&priv->tmpl, NFT_SET_EXT_EXPIRATION);
	}

	priv->timeout = timeout;

	err = nf_tables_bind_set(ctx, set, &priv->binding);
	if (err < 0)
		goto err_expr_free;

	if (set->size == 0)
		set->size = 0xffff;

	priv->set = set;
	return 0;

err_expr_free:
	if (priv->expr != NULL)
		nft_expr_destroy(ctx, priv->expr);
	return err;
}

static void nft_dynset_deactivate(const struct nft_ctx *ctx,
				  const struct nft_expr *expr,
				  enum nft_trans_phase phase)
{
	struct nft_dynset *priv = nft_expr_priv(expr);

	nf_tables_deactivate_set(ctx, priv->set, &priv->binding, phase);
}

static void nft_dynset_activate(const struct nft_ctx *ctx,
				const struct nft_expr *expr)
{
	struct nft_dynset *priv = nft_expr_priv(expr);

	priv->set->use++;
}

static void nft_dynset_destroy(const struct nft_ctx *ctx,
			       const struct nft_expr *expr)
{
	struct nft_dynset *priv = nft_expr_priv(expr);

	if (priv->expr != NULL)
		nft_expr_destroy(ctx, priv->expr);

	nf_tables_destroy_set(ctx, priv->set);
}

static int nft_dynset_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_dynset *priv = nft_expr_priv(expr);
	u32 flags = priv->invert ? NFT_DYNSET_F_INV : 0;

	if (nft_dump_register(skb, NFTA_DYNSET_SREG_KEY, priv->sreg_key))
		goto nla_put_failure;
	if (priv->set->flags & NFT_SET_MAP &&
	    nft_dump_register(skb, NFTA_DYNSET_SREG_DATA, priv->sreg_data))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_DYNSET_OP, htonl(priv->op)))
		goto nla_put_failure;
	if (nla_put_string(skb, NFTA_DYNSET_SET_NAME, priv->set->name))
		goto nla_put_failure;
	if (nla_put_be64(skb, NFTA_DYNSET_TIMEOUT,
			 nf_jiffies64_to_msecs(priv->timeout),
			 NFTA_DYNSET_PAD))
		goto nla_put_failure;
	if (priv->expr && nft_expr_dump(skb, NFTA_DYNSET_EXPR, priv->expr))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_DYNSET_FLAGS, htonl(flags)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static const struct nft_expr_ops nft_dynset_ops = {
	.type		= &nft_dynset_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_dynset)),
	.eval		= nft_dynset_eval,
	.init		= nft_dynset_init,
	.destroy	= nft_dynset_destroy,
	.activate	= nft_dynset_activate,
	.deactivate	= nft_dynset_deactivate,
	.dump		= nft_dynset_dump,
};

struct nft_expr_type nft_dynset_type __read_mostly = {
	.name		= "dynset",
	.ops		= &nft_dynset_ops,
	.policy		= nft_dynset_policy,
	.maxattr	= NFTA_DYNSET_MAX,
	.owner		= THIS_MODULE,
};
