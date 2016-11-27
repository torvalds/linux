/*
 * Copyright (c) 2012-2016 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>

#define nft_objref_priv(expr)	*((struct nft_object **)nft_expr_priv(expr))

static void nft_objref_eval(const struct nft_expr *expr,
			    struct nft_regs *regs,
			    const struct nft_pktinfo *pkt)
{
	struct nft_object *obj = nft_objref_priv(expr);

	obj->type->eval(obj, regs, pkt);
}

static int nft_objref_init(const struct nft_ctx *ctx,
			   const struct nft_expr *expr,
			   const struct nlattr * const tb[])
{
	struct nft_object *obj = nft_objref_priv(expr);
	u8 genmask = nft_genmask_next(ctx->net);
	u32 objtype;

	if (!tb[NFTA_OBJREF_IMM_NAME] ||
	    !tb[NFTA_OBJREF_IMM_TYPE])
		return -EINVAL;

	objtype = ntohl(nla_get_be32(tb[NFTA_OBJREF_IMM_TYPE]));
	obj = nf_tables_obj_lookup(ctx->table, tb[NFTA_OBJREF_IMM_NAME], objtype,
				   genmask);
	if (IS_ERR(obj))
		return -ENOENT;

	nft_objref_priv(expr) = obj;
	obj->use++;

	return 0;
}

static int nft_objref_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_object *obj = nft_objref_priv(expr);

	if (nla_put_string(skb, NFTA_OBJREF_IMM_NAME, obj->name) ||
	    nla_put_be32(skb, NFTA_OBJREF_IMM_TYPE, htonl(obj->type->type)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static void nft_objref_destroy(const struct nft_ctx *ctx,
			       const struct nft_expr *expr)
{
	struct nft_object *obj = nft_objref_priv(expr);

	obj->use--;
}

static struct nft_expr_type nft_objref_type;
static const struct nft_expr_ops nft_objref_ops = {
	.type		= &nft_objref_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_object *)),
	.eval		= nft_objref_eval,
	.init		= nft_objref_init,
	.destroy	= nft_objref_destroy,
	.dump		= nft_objref_dump,
};

static const struct nla_policy nft_objref_policy[NFTA_OBJREF_MAX + 1] = {
	[NFTA_OBJREF_IMM_NAME]	= { .type = NLA_STRING },
	[NFTA_OBJREF_IMM_TYPE]	= { .type = NLA_U32 },
};

static struct nft_expr_type nft_objref_type __read_mostly = {
	.name		= "objref",
	.ops		= &nft_objref_ops,
	.policy		= nft_objref_policy,
	.maxattr	= NFTA_OBJREF_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_objref_module_init(void)
{
	return nft_register_expr(&nft_objref_type);
}

static void __exit nft_objref_module_exit(void)
{
	nft_unregister_expr(&nft_objref_type);
}

module_init(nft_objref_module_init);
module_exit(nft_objref_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NFT_EXPR("objref");
