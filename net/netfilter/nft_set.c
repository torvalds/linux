/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>

struct nft_set {
	struct rb_root		root;
	enum nft_registers	sreg:8;
	enum nft_registers	dreg:8;
	u8			klen;
	u8			dlen;
	u16			flags;
};

struct nft_set_elem {
	struct rb_node		node;
	enum nft_set_elem_flags	flags;
	struct nft_data		key;
	struct nft_data		data[];
};

static void nft_set_eval(const struct nft_expr *expr,
			 struct nft_data data[NFT_REG_MAX + 1],
			 const struct nft_pktinfo *pkt)
{
	const struct nft_set *priv = nft_expr_priv(expr);
	const struct rb_node *parent = priv->root.rb_node;
	const struct nft_set_elem *elem, *interval = NULL;
	const struct nft_data *key = &data[priv->sreg];
	int d;

	while (parent != NULL) {
		elem = rb_entry(parent, struct nft_set_elem, node);

		d = nft_data_cmp(&elem->key, key, priv->klen);
		if (d < 0) {
			parent = parent->rb_left;
			interval = elem;
		} else if (d > 0)
			parent = parent->rb_right;
		else {
found:
			if (elem->flags & NFT_SE_INTERVAL_END)
				goto out;
			if (priv->flags & NFT_SET_MAP)
				nft_data_copy(&data[priv->dreg], elem->data);
			return;
		}
	}

	if (priv->flags & NFT_SET_INTERVAL && interval != NULL) {
		elem = interval;
		goto found;
	}
out:
	data[NFT_REG_VERDICT].verdict = NFT_BREAK;
}

static void nft_set_elem_destroy(const struct nft_expr *expr,
				 struct nft_set_elem *elem)
{
	const struct nft_set *priv = nft_expr_priv(expr);

	nft_data_uninit(&elem->key, NFT_DATA_VALUE);
	if (priv->flags & NFT_SET_MAP)
		nft_data_uninit(elem->data, nft_dreg_to_type(priv->dreg));
	kfree(elem);
}

static const struct nla_policy nft_se_policy[NFTA_SE_MAX + 1] = {
	[NFTA_SE_KEY]		= { .type = NLA_NESTED },
	[NFTA_SE_DATA]		= { .type = NLA_NESTED },
	[NFTA_SE_FLAGS]		= { .type = NLA_U32 },
};

static int nft_set_elem_init(const struct nft_ctx *ctx,
			     const struct nft_expr *expr,
			     const struct nlattr *nla,
			     struct nft_set_elem **new)
{
	struct nft_set *priv = nft_expr_priv(expr);
	struct nlattr *tb[NFTA_SE_MAX + 1];
	struct nft_set_elem *elem;
	struct nft_data_desc d1, d2;
	enum nft_set_elem_flags flags = 0;
	unsigned int size;
	int err;

	err = nla_parse_nested(tb, NFTA_SE_MAX, nla, nft_se_policy);
	if (err < 0)
		return err;

	if (tb[NFTA_SE_KEY] == NULL)
		return -EINVAL;

	if (tb[NFTA_SE_FLAGS] != NULL) {
		flags = ntohl(nla_get_be32(tb[NFTA_SE_FLAGS]));
		if (flags & ~NFT_SE_INTERVAL_END)
			return -EINVAL;
	}

	size = sizeof(*elem);
	if (priv->flags & NFT_SET_MAP) {
		if (tb[NFTA_SE_DATA] == NULL && !(flags & NFT_SE_INTERVAL_END))
			return -EINVAL;
		size += sizeof(elem->data[0]);
	} else {
		if (tb[NFTA_SE_DATA] != NULL)
			return -EINVAL;
	}

	elem = kzalloc(size, GFP_KERNEL);
	if (elem == NULL)
		return -ENOMEM;
	elem->flags = flags;

	err = nft_data_init(ctx, &elem->key, &d1, tb[NFTA_SE_KEY]);
	if (err < 0)
		goto err1;
	err = -EINVAL;
	if (d1.type != NFT_DATA_VALUE || d1.len != priv->klen)
		goto err2;

	if (tb[NFTA_SE_DATA] != NULL) {
		err = nft_data_init(ctx, elem->data, &d2, tb[NFTA_SE_DATA]);
		if (err < 0)
			goto err2;
		err = -EINVAL;
		if (priv->dreg != NFT_REG_VERDICT && d2.len != priv->dlen)
			goto err2;
		err = nft_validate_data_load(ctx, priv->dreg, elem->data, d2.type);
		if (err < 0)
			goto err3;
	}

	*new = elem;
	return 0;

err3:
	nft_data_uninit(elem->data, d2.type);
err2:
	nft_data_uninit(&elem->key, d1.type);
err1:
	kfree(elem);
	return err;
}

static int nft_set_elem_dump(struct sk_buff *skb, const struct nft_expr *expr,
			     const struct nft_set_elem *elem)

{
	const struct nft_set *priv = nft_expr_priv(expr);
	struct nlattr *nest;

	nest = nla_nest_start(skb, NFTA_LIST_ELEM);
	if (nest == NULL)
		goto nla_put_failure;

	if (nft_data_dump(skb, NFTA_SE_KEY, &elem->key,
			  NFT_DATA_VALUE, priv->klen) < 0)
		goto nla_put_failure;

	if (priv->flags & NFT_SET_MAP && !(elem->flags & NFT_SE_INTERVAL_END)) {
		if (nft_data_dump(skb, NFTA_SE_DATA, elem->data,
				  nft_dreg_to_type(priv->dreg), priv->dlen) < 0)
			goto nla_put_failure;
	}

	if (elem->flags){
		if (nla_put_be32(skb, NFTA_SE_FLAGS, htonl(elem->flags)))
			goto nla_put_failure;
	}

	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	return -1;
}

static void nft_set_destroy(const struct nft_expr *expr)
{
	struct nft_set *priv = nft_expr_priv(expr);
	struct nft_set_elem *elem;
	struct rb_node *node;

	while ((node = priv->root.rb_node) != NULL) {
		rb_erase(node, &priv->root);
		elem = rb_entry(node, struct nft_set_elem, node);
		nft_set_elem_destroy(expr, elem);
	}
}

static const struct nla_policy nft_set_policy[NFTA_SET_MAX + 1] = {
	[NFTA_SET_FLAGS]	= { .type = NLA_U32 },
	[NFTA_SET_SREG]		= { .type = NLA_U32 },
	[NFTA_SET_DREG]		= { .type = NLA_U32 },
	[NFTA_SET_KLEN]		= { .type = NLA_U32 },
	[NFTA_SET_DLEN]		= { .type = NLA_U32 },
	[NFTA_SET_ELEMENTS]	= { .type = NLA_NESTED },
};

static int nft_set_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
			const struct nlattr * const tb[])
{
	struct nft_set *priv = nft_expr_priv(expr);
	struct nft_set_elem *elem, *uninitialized_var(new);
	struct rb_node *parent, **p;
	const struct nlattr *nla;
	int err, rem, d;

	if (tb[NFTA_SET_SREG] == NULL ||
	    tb[NFTA_SET_KLEN] == NULL ||
	    tb[NFTA_SET_ELEMENTS] == NULL)
		return -EINVAL;

	priv->root = RB_ROOT;

	if (tb[NFTA_SET_FLAGS] != NULL) {
		priv->flags = ntohl(nla_get_be32(tb[NFTA_SET_FLAGS]));
		if (priv->flags & ~(NFT_SET_INTERVAL | NFT_SET_MAP))
			return -EINVAL;
	}

	priv->sreg = ntohl(nla_get_be32(tb[NFTA_SET_SREG]));
	err = nft_validate_input_register(priv->sreg);
	if (err < 0)
		return err;

	if (tb[NFTA_SET_DREG] != NULL) {
		if (!(priv->flags & NFT_SET_MAP))
			return -EINVAL;
		if (tb[NFTA_SET_DLEN] == NULL)
			return -EINVAL;

		priv->dreg = ntohl(nla_get_be32(tb[NFTA_SET_DREG]));
		err = nft_validate_output_register(priv->dreg);
		if (err < 0)
			return err;

		if (priv->dreg == NFT_REG_VERDICT)
			priv->dlen = FIELD_SIZEOF(struct nft_data, data);
		else {
			priv->dlen = ntohl(nla_get_be32(tb[NFTA_SET_DLEN]));
			if (priv->dlen == 0 ||
			    priv->dlen > FIELD_SIZEOF(struct nft_data, data))
				return -EINVAL;
		}
	} else {
		if (priv->flags & NFT_SET_MAP)
			return -EINVAL;
		if (tb[NFTA_SET_DLEN] != NULL)
			return -EINVAL;
	}

	priv->klen = ntohl(nla_get_be32(tb[NFTA_SET_KLEN]));
	if (priv->klen == 0 ||
	    priv->klen > FIELD_SIZEOF(struct nft_data, data))
		return -EINVAL;

	nla_for_each_nested(nla, tb[NFTA_SET_ELEMENTS], rem) {
		err = -EINVAL;
		if (nla_type(nla) != NFTA_LIST_ELEM)
			goto err1;

		err = nft_set_elem_init(ctx, expr, nla, &new);
		if (err < 0)
			goto err1;

		parent = NULL;
		p = &priv->root.rb_node;
		while (*p != NULL) {
			parent = *p;
			elem = rb_entry(parent, struct nft_set_elem, node);
			d = nft_data_cmp(&elem->key, &new->key, priv->klen);
			if (d < 0)
				p = &parent->rb_left;
			else if (d > 0)
				p = &parent->rb_right;
			else {
				err = -EEXIST;
				goto err2;
			}
		}
		rb_link_node(&new->node, parent, p);
		rb_insert_color(&new->node, &priv->root);
	}

	return 0;

err2:
	nft_set_elem_destroy(expr, new);
err1:
	nft_set_destroy(expr);
	return err;
}

static int nft_set_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	struct nft_set *priv = nft_expr_priv(expr);
	const struct nft_set_elem *elem;
	struct rb_node *node;
	struct nlattr *list;

	if (priv->flags) {
		if (nla_put_be32(skb, NFTA_SET_FLAGS, htonl(priv->flags)))
			goto nla_put_failure;
	}

	if (nla_put_be32(skb, NFTA_SET_SREG, htonl(priv->sreg)))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_SET_KLEN, htonl(priv->klen)))
		goto nla_put_failure;

	if (priv->flags & NFT_SET_MAP) {
		if (nla_put_be32(skb, NFTA_SET_DREG, htonl(priv->dreg)))
			goto nla_put_failure;
		if (nla_put_be32(skb, NFTA_SET_DLEN, htonl(priv->dlen)))
			goto nla_put_failure;
	}

	list = nla_nest_start(skb, NFTA_SET_ELEMENTS);
	if (list == NULL)
		goto nla_put_failure;

	for (node = rb_first(&priv->root); node; node = rb_next(node)) {
		elem = rb_entry(node, struct nft_set_elem, node);
		if (nft_set_elem_dump(skb, expr, elem) < 0)
			goto nla_put_failure;
	}

	nla_nest_end(skb, list);
	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_ops nft_set_ops __read_mostly = {
	.name		= "set",
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_set)),
	.owner		= THIS_MODULE,
	.eval		= nft_set_eval,
	.init		= nft_set_init,
	.destroy	= nft_set_destroy,
	.dump		= nft_set_dump,
	.policy		= nft_set_policy,
	.maxattr	= NFTA_SET_MAX,
};

static int __init nft_set_module_init(void)
{
	return nft_register_expr(&nft_set_ops);
}

static void __exit nft_set_module_exit(void)
{
	nft_unregister_expr(&nft_set_ops);
}

module_init(nft_set_module_init);
module_exit(nft_set_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_EXPR("set");
