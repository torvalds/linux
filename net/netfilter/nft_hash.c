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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>

struct nft_hash {
	struct hlist_head	*hash;
	unsigned int		hsize;
	enum nft_registers	sreg:8;
	enum nft_registers	dreg:8;
	u8			klen;
	u8			dlen;
	u16			flags;
};

struct nft_hash_elem {
	struct hlist_node	hnode;
	struct nft_data		key;
	struct nft_data		data[];
};

static u32 nft_hash_rnd __read_mostly;
static bool nft_hash_rnd_initted __read_mostly;

static unsigned int nft_hash_data(const struct nft_data *data,
				  unsigned int hsize, unsigned int len)
{
	unsigned int h;

	// FIXME: can we reasonably guarantee the upper bits are fixed?
	h = jhash2(data->data, len >> 2, nft_hash_rnd);
	return ((u64)h * hsize) >> 32;
}

static void nft_hash_eval(const struct nft_expr *expr,
			  struct nft_data data[NFT_REG_MAX + 1],
			  const struct nft_pktinfo *pkt)
{
	const struct nft_hash *priv = nft_expr_priv(expr);
	const struct nft_hash_elem *elem;
	const struct nft_data *key = &data[priv->sreg];
	unsigned int h;

	h = nft_hash_data(key, priv->hsize, priv->klen);
	hlist_for_each_entry(elem, &priv->hash[h], hnode) {
		if (nft_data_cmp(&elem->key, key, priv->klen))
			continue;
		if (priv->flags & NFT_HASH_MAP)
			nft_data_copy(&data[priv->dreg], elem->data);
		return;
	}
	data[NFT_REG_VERDICT].verdict = NFT_BREAK;
}

static void nft_hash_elem_destroy(const struct nft_expr *expr,
				  struct nft_hash_elem *elem)
{
	const struct nft_hash *priv = nft_expr_priv(expr);

	nft_data_uninit(&elem->key, NFT_DATA_VALUE);
	if (priv->flags & NFT_HASH_MAP)
		nft_data_uninit(elem->data, nft_dreg_to_type(priv->dreg));
	kfree(elem);
}

static const struct nla_policy nft_he_policy[NFTA_HE_MAX + 1] = {
	[NFTA_HE_KEY]		= { .type = NLA_NESTED },
	[NFTA_HE_DATA]		= { .type = NLA_NESTED },
};

static int nft_hash_elem_init(const struct nft_ctx *ctx,
			      const struct nft_expr *expr,
			      const struct nlattr *nla,
			      struct nft_hash_elem **new)
{
	struct nft_hash *priv = nft_expr_priv(expr);
	struct nlattr *tb[NFTA_HE_MAX + 1];
	struct nft_hash_elem *elem;
	struct nft_data_desc d1, d2;
	unsigned int size;
	int err;

	err = nla_parse_nested(tb, NFTA_HE_MAX, nla, nft_he_policy);
	if (err < 0)
		return err;

	if (tb[NFTA_HE_KEY] == NULL)
		return -EINVAL;
	size = sizeof(*elem);

	if (priv->flags & NFT_HASH_MAP) {
		if (tb[NFTA_HE_DATA] == NULL)
			return -EINVAL;
		size += sizeof(elem->data[0]);
	} else {
		if (tb[NFTA_HE_DATA] != NULL)
			return -EINVAL;
	}

	elem = kzalloc(size, GFP_KERNEL);
	if (elem == NULL)
		return -ENOMEM;

	err = nft_data_init(ctx, &elem->key, &d1, tb[NFTA_HE_KEY]);
	if (err < 0)
		goto err1;
	err = -EINVAL;
	if (d1.type != NFT_DATA_VALUE || d1.len != priv->klen)
		goto err2;

	if (tb[NFTA_HE_DATA] != NULL) {
		err = nft_data_init(ctx, elem->data, &d2, tb[NFTA_HE_DATA]);
		if (err < 0)
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

static int nft_hash_elem_dump(struct sk_buff *skb, const struct nft_expr *expr,
			      const struct nft_hash_elem *elem)

{
	const struct nft_hash *priv = nft_expr_priv(expr);
	struct nlattr *nest;

	nest = nla_nest_start(skb, NFTA_LIST_ELEM);
	if (nest == NULL)
		goto nla_put_failure;

	if (nft_data_dump(skb, NFTA_HE_KEY, &elem->key,
			  NFT_DATA_VALUE, priv->klen) < 0)
		goto nla_put_failure;

	if (priv->flags & NFT_HASH_MAP) {
		if (nft_data_dump(skb, NFTA_HE_DATA, elem->data,
				  NFT_DATA_VALUE, priv->dlen) < 0)
			goto nla_put_failure;
	}

	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	return -1;
}

static void nft_hash_destroy(const struct nft_ctx *ctx,
			     const struct nft_expr *expr)
{
	const struct nft_hash *priv = nft_expr_priv(expr);
	const struct hlist_node *next;
	struct nft_hash_elem *elem;
	unsigned int i;

	for (i = 0; i < priv->hsize; i++) {
		hlist_for_each_entry_safe(elem, next, &priv->hash[i], hnode) {
			hlist_del(&elem->hnode);
			nft_hash_elem_destroy(expr, elem);
		}
	}
	kfree(priv->hash);
}

static const struct nla_policy nft_hash_policy[NFTA_HASH_MAX + 1] = {
	[NFTA_HASH_FLAGS]	= { .type = NLA_U32 },
	[NFTA_HASH_SREG]	= { .type = NLA_U32 },
	[NFTA_HASH_DREG]	= { .type = NLA_U32 },
	[NFTA_HASH_KLEN]	= { .type = NLA_U32 },
	[NFTA_HASH_ELEMENTS]	= { .type = NLA_NESTED },
};

static int nft_hash_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
			 const struct nlattr * const tb[])
{
	struct nft_hash *priv = nft_expr_priv(expr);
	struct nft_hash_elem *elem, *uninitialized_var(new);
	const struct nlattr *nla;
	unsigned int cnt, i;
	unsigned int h;
	int err, rem;

	if (unlikely(!nft_hash_rnd_initted)) {
		get_random_bytes(&nft_hash_rnd, 4);
		nft_hash_rnd_initted = true;
	}

	if (tb[NFTA_HASH_SREG] == NULL ||
	    tb[NFTA_HASH_KLEN] == NULL ||
	    tb[NFTA_HASH_ELEMENTS] == NULL)
		return -EINVAL;

	if (tb[NFTA_HASH_FLAGS] != NULL) {
		priv->flags = ntohl(nla_get_be32(tb[NFTA_HASH_FLAGS]));
		if (priv->flags & ~NFT_HASH_MAP)
			return -EINVAL;
	}

	priv->sreg = ntohl(nla_get_be32(tb[NFTA_HASH_SREG]));
	err = nft_validate_input_register(priv->sreg);
	if (err < 0)
		return err;

	if (tb[NFTA_HASH_DREG] != NULL) {
		if (!(priv->flags & NFT_HASH_MAP))
			return -EINVAL;
		priv->dreg = ntohl(nla_get_be32(tb[NFTA_HASH_DREG]));
		err = nft_validate_output_register(priv->dreg);
		if (err < 0)
			return err;
	}

	priv->klen = ntohl(nla_get_be32(tb[NFTA_HASH_KLEN]));
	if (priv->klen == 0)
		return -EINVAL;

	cnt = 0;
	nla_for_each_nested(nla, tb[NFTA_HASH_ELEMENTS], rem) {
		if (nla_type(nla) != NFTA_LIST_ELEM)
			return -EINVAL;
		cnt++;
	}

	/* Aim for a load factor of 0.75 */
	cnt = cnt * 4 / 3;

	priv->hash = kcalloc(cnt, sizeof(struct hlist_head), GFP_KERNEL);
	if (priv->hash == NULL)
		return -ENOMEM;
	priv->hsize = cnt;

	for (i = 0; i < cnt; i++)
		INIT_HLIST_HEAD(&priv->hash[i]);

	err = -ENOMEM;
	nla_for_each_nested(nla, tb[NFTA_HASH_ELEMENTS], rem) {
		err = nft_hash_elem_init(ctx, expr, nla, &new);
		if (err < 0)
			goto err1;

		h = nft_hash_data(&new->key, priv->hsize, priv->klen);
		hlist_for_each_entry(elem, &priv->hash[h], hnode) {
			if (nft_data_cmp(&elem->key, &new->key, priv->klen))
				continue;
			nft_hash_elem_destroy(expr, new);
			err = -EEXIST;
			goto err1;
		}
		hlist_add_head(&new->hnode, &priv->hash[h]);
	}
	return 0;

err1:
	nft_hash_destroy(ctx, expr);
	return err;
}

static int nft_hash_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_hash *priv = nft_expr_priv(expr);
	const struct nft_hash_elem *elem;
	struct nlattr *list;
	unsigned int i;

	if (priv->flags)
		if (nla_put_be32(skb, NFTA_HASH_FLAGS, htonl(priv->flags)))
			goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_HASH_SREG, htonl(priv->sreg)))
		goto nla_put_failure;
	if (priv->flags & NFT_HASH_MAP)
		if (nla_put_be32(skb, NFTA_HASH_DREG, htonl(priv->dreg)))
			goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_HASH_KLEN, htonl(priv->klen)))
		goto nla_put_failure;

	list = nla_nest_start(skb, NFTA_HASH_ELEMENTS);
	if (list == NULL)
		goto nla_put_failure;

	for (i = 0; i < priv->hsize; i++) {
		hlist_for_each_entry(elem, &priv->hash[i], hnode) {
			if (nft_hash_elem_dump(skb, expr, elem) < 0)
				goto nla_put_failure;
		}
	}

	nla_nest_end(skb, list);
	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_ops nft_hash_ops __read_mostly = {
	.name		= "hash",
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_hash)),
	.owner		= THIS_MODULE,
	.eval		= nft_hash_eval,
	.init		= nft_hash_init,
	.destroy	= nft_hash_destroy,
	.dump		= nft_hash_dump,
	.policy		= nft_hash_policy,
	.maxattr	= NFTA_HASH_MAX,
};

static int __init nft_hash_module_init(void)
{
	return nft_register_expr(&nft_hash_ops);
}

static void __exit nft_hash_module_exit(void)
{
	nft_unregister_expr(&nft_hash_ops);
}

module_init(nft_hash_module_init);
module_exit(nft_hash_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_EXPR("hash");
