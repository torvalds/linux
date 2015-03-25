/*
 * Copyright (c) 2008-2014 Patrick McHardy <kaber@trash.net>
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
#include <linux/log2.h>
#include <linux/jhash.h>
#include <linux/netlink.h>
#include <linux/rhashtable.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>

/* We target a hash table size of 4, element hint is 75% of final size */
#define NFT_HASH_ELEMENT_HINT 3

struct nft_hash {
	struct rhashtable		ht;
};

struct nft_hash_elem {
	struct rhash_head		node;
	struct nft_set_ext		ext;
};

struct nft_hash_cmp_arg {
	const struct nft_set		*set;
	const struct nft_data		*key;
};

static const struct rhashtable_params nft_hash_params;

static inline u32 nft_hash_key(const void *data, u32 len, u32 seed)
{
	const struct nft_hash_cmp_arg *arg = data;

	return jhash(arg->key, len, seed);
}

static inline u32 nft_hash_obj(const void *data, u32 len, u32 seed)
{
	const struct nft_hash_elem *he = data;

	return jhash(nft_set_ext_key(&he->ext), len, seed);
}

static inline int nft_hash_cmp(struct rhashtable_compare_arg *arg,
			       const void *ptr)
{
	const struct nft_hash_cmp_arg *x = arg->key;
	const struct nft_hash_elem *he = ptr;

	if (nft_data_cmp(nft_set_ext_key(&he->ext), x->key, x->set->klen))
		return 1;
	return 0;
}

static bool nft_hash_lookup(const struct nft_set *set,
			    const struct nft_data *key,
			    struct nft_data *data)
{
	struct nft_hash *priv = nft_set_priv(set);
	const struct nft_hash_elem *he;
	struct nft_hash_cmp_arg arg = {
		.set	 = set,
		.key	 = key,
	};

	he = rhashtable_lookup_fast(&priv->ht, &arg, nft_hash_params);
	if (he && set->flags & NFT_SET_MAP)
		nft_data_copy(data, nft_set_ext_data(&he->ext));

	return !!he;
}

static int nft_hash_insert(const struct nft_set *set,
			   const struct nft_set_elem *elem)
{
	struct nft_hash *priv = nft_set_priv(set);
	struct nft_hash_elem *he = elem->priv;
	struct nft_hash_cmp_arg arg = {
		.set	 = set,
		.key	 = &elem->key,
	};

	return rhashtable_lookup_insert_key(&priv->ht, &arg, &he->node,
					    nft_hash_params);
}

static void nft_hash_elem_destroy(const struct nft_set *set,
				  struct nft_hash_elem *he)
{
	nft_data_uninit(nft_set_ext_key(&he->ext), NFT_DATA_VALUE);
	if (set->flags & NFT_SET_MAP)
		nft_data_uninit(nft_set_ext_data(&he->ext), set->dtype);
	kfree(he);
}

static void nft_hash_remove(const struct nft_set *set,
			    const struct nft_set_elem *elem)
{
	struct nft_hash *priv = nft_set_priv(set);

	rhashtable_remove_fast(&priv->ht, elem->cookie, nft_hash_params);
	synchronize_rcu();
	kfree(elem->cookie);
}

static int nft_hash_get(const struct nft_set *set, struct nft_set_elem *elem)
{
	struct nft_hash *priv = nft_set_priv(set);
	struct nft_hash_elem *he;
	struct nft_hash_cmp_arg arg = {
		.set	 = set,
		.key	 = &elem->key,
	};

	he = rhashtable_lookup_fast(&priv->ht, &arg, nft_hash_params);
	if (!he)
		return -ENOENT;

	elem->priv = he;

	return 0;
}

static void nft_hash_walk(const struct nft_ctx *ctx, const struct nft_set *set,
			  struct nft_set_iter *iter)
{
	struct nft_hash *priv = nft_set_priv(set);
	struct nft_hash_elem *he;
	struct rhashtable_iter hti;
	struct nft_set_elem elem;
	int err;

	err = rhashtable_walk_init(&priv->ht, &hti);
	iter->err = err;
	if (err)
		return;

	err = rhashtable_walk_start(&hti);
	if (err && err != -EAGAIN) {
		iter->err = err;
		goto out;
	}

	while ((he = rhashtable_walk_next(&hti))) {
		if (IS_ERR(he)) {
			err = PTR_ERR(he);
			if (err != -EAGAIN) {
				iter->err = err;
				goto out;
			}

			continue;
		}

		if (iter->count < iter->skip)
			goto cont;

		elem.priv = he;

		iter->err = iter->fn(ctx, set, iter, &elem);
		if (iter->err < 0)
			goto out;

cont:
		iter->count++;
	}

out:
	rhashtable_walk_stop(&hti);
	rhashtable_walk_exit(&hti);
}

static unsigned int nft_hash_privsize(const struct nlattr * const nla[])
{
	return sizeof(struct nft_hash);
}

static const struct rhashtable_params nft_hash_params = {
	.head_offset		= offsetof(struct nft_hash_elem, node),
	.hashfn			= nft_hash_key,
	.obj_hashfn		= nft_hash_obj,
	.obj_cmpfn		= nft_hash_cmp,
	.automatic_shrinking	= true,
};

static int nft_hash_init(const struct nft_set *set,
			 const struct nft_set_desc *desc,
			 const struct nlattr * const tb[])
{
	struct nft_hash *priv = nft_set_priv(set);
	struct rhashtable_params params = nft_hash_params;

	params.nelem_hint = desc->size ?: NFT_HASH_ELEMENT_HINT;
	params.key_len	  = set->klen;

	return rhashtable_init(&priv->ht, &params);
}

static void nft_free_element(void *ptr, void *arg)
{
	nft_hash_elem_destroy((const struct nft_set *)arg, ptr);
}

static void nft_hash_destroy(const struct nft_set *set)
{
	struct nft_hash *priv = nft_set_priv(set);

	rhashtable_free_and_destroy(&priv->ht, nft_free_element, (void *)set);
}

static bool nft_hash_estimate(const struct nft_set_desc *desc, u32 features,
			      struct nft_set_estimate *est)
{
	unsigned int esize;

	esize = sizeof(struct nft_hash_elem);
	if (desc->size) {
		est->size = sizeof(struct nft_hash) +
			    roundup_pow_of_two(desc->size * 4 / 3) *
			    sizeof(struct nft_hash_elem *) +
			    desc->size * esize;
	} else {
		/* Resizing happens when the load drops below 30% or goes
		 * above 75%. The average of 52.5% load (approximated by 50%)
		 * is used for the size estimation of the hash buckets,
		 * meaning we calculate two buckets per element.
		 */
		est->size = esize + 2 * sizeof(struct nft_hash_elem *);
	}

	est->class = NFT_SET_CLASS_O_1;

	return true;
}

static struct nft_set_ops nft_hash_ops __read_mostly = {
	.privsize       = nft_hash_privsize,
	.elemsize	= offsetof(struct nft_hash_elem, ext),
	.estimate	= nft_hash_estimate,
	.init		= nft_hash_init,
	.destroy	= nft_hash_destroy,
	.get		= nft_hash_get,
	.insert		= nft_hash_insert,
	.remove		= nft_hash_remove,
	.lookup		= nft_hash_lookup,
	.walk		= nft_hash_walk,
	.features	= NFT_SET_MAP,
	.owner		= THIS_MODULE,
};

static int __init nft_hash_module_init(void)
{
	return nft_register_set(&nft_hash_ops);
}

static void __exit nft_hash_module_exit(void)
{
	nft_unregister_set(&nft_hash_ops);
}

module_init(nft_hash_module_init);
module_exit(nft_hash_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_SET();
