/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
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
#include <linux/rbtree.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>

struct nft_rbtree {
	struct rb_root		root;
};

struct nft_rbtree_elem {
	struct rb_node		node;
	u16			flags;
	struct nft_data		key;
	struct nft_data		data[];
};

static bool nft_rbtree_lookup(const struct nft_set *set,
			      const struct nft_data *key,
			      struct nft_data *data)
{
	const struct nft_rbtree *priv = nft_set_priv(set);
	const struct nft_rbtree_elem *rbe, *interval = NULL;
	const struct rb_node *parent = priv->root.rb_node;
	int d;

	while (parent != NULL) {
		rbe = rb_entry(parent, struct nft_rbtree_elem, node);

		d = nft_data_cmp(&rbe->key, key, set->klen);
		if (d < 0) {
			parent = parent->rb_left;
			interval = rbe;
		} else if (d > 0)
			parent = parent->rb_right;
		else {
found:
			if (rbe->flags & NFT_SET_ELEM_INTERVAL_END)
				goto out;
			if (set->flags & NFT_SET_MAP)
				nft_data_copy(data, rbe->data);
			return true;
		}
	}

	if (set->flags & NFT_SET_INTERVAL && interval != NULL) {
		rbe = interval;
		goto found;
	}
out:
	return false;
}

static void nft_rbtree_elem_destroy(const struct nft_set *set,
				    struct nft_rbtree_elem *rbe)
{
	nft_data_uninit(&rbe->key, NFT_DATA_VALUE);
	if (set->flags & NFT_SET_MAP &&
	    !(rbe->flags & NFT_SET_ELEM_INTERVAL_END))
		nft_data_uninit(rbe->data, set->dtype);

	kfree(rbe);
}

static int __nft_rbtree_insert(const struct nft_set *set,
			       struct nft_rbtree_elem *new)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe;
	struct rb_node *parent, **p;
	int d;

	parent = NULL;
	p = &priv->root.rb_node;
	while (*p != NULL) {
		parent = *p;
		rbe = rb_entry(parent, struct nft_rbtree_elem, node);
		d = nft_data_cmp(&rbe->key, &new->key, set->klen);
		if (d < 0)
			p = &parent->rb_left;
		else if (d > 0)
			p = &parent->rb_right;
		else
			return -EEXIST;
	}
	rb_link_node(&new->node, parent, p);
	rb_insert_color(&new->node, &priv->root);
	return 0;
}

static int nft_rbtree_insert(const struct nft_set *set,
			     const struct nft_set_elem *elem)
{
	struct nft_rbtree_elem *rbe;
	unsigned int size;
	int err;

	size = sizeof(*rbe);
	if (set->flags & NFT_SET_MAP &&
	    !(elem->flags & NFT_SET_ELEM_INTERVAL_END))
		size += sizeof(rbe->data[0]);

	rbe = kzalloc(size, GFP_KERNEL);
	if (rbe == NULL)
		return -ENOMEM;

	rbe->flags = elem->flags;
	nft_data_copy(&rbe->key, &elem->key);
	if (set->flags & NFT_SET_MAP &&
	    !(rbe->flags & NFT_SET_ELEM_INTERVAL_END))
		nft_data_copy(rbe->data, &elem->data);

	err = __nft_rbtree_insert(set, rbe);
	if (err < 0)
		kfree(rbe);
	return err;
}

static void nft_rbtree_remove(const struct nft_set *set,
			      const struct nft_set_elem *elem)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe = elem->cookie;

	rb_erase(&rbe->node, &priv->root);
	kfree(rbe);
}

static int nft_rbtree_get(const struct nft_set *set, struct nft_set_elem *elem)
{
	const struct nft_rbtree *priv = nft_set_priv(set);
	const struct rb_node *parent = priv->root.rb_node;
	struct nft_rbtree_elem *rbe;
	int d;

	while (parent != NULL) {
		rbe = rb_entry(parent, struct nft_rbtree_elem, node);

		d = nft_data_cmp(&rbe->key, &elem->key, set->klen);
		if (d < 0)
			parent = parent->rb_left;
		else if (d > 0)
			parent = parent->rb_right;
		else {
			elem->cookie = rbe;
			if (set->flags & NFT_SET_MAP &&
			    !(rbe->flags & NFT_SET_ELEM_INTERVAL_END))
				nft_data_copy(&elem->data, rbe->data);
			elem->flags = rbe->flags;
			return 0;
		}
	}
	return -ENOENT;
}

static void nft_rbtree_walk(const struct nft_ctx *ctx,
			    const struct nft_set *set,
			    struct nft_set_iter *iter)
{
	const struct nft_rbtree *priv = nft_set_priv(set);
	const struct nft_rbtree_elem *rbe;
	struct nft_set_elem elem;
	struct rb_node *node;

	for (node = rb_first(&priv->root); node != NULL; node = rb_next(node)) {
		if (iter->count < iter->skip)
			goto cont;

		rbe = rb_entry(node, struct nft_rbtree_elem, node);
		nft_data_copy(&elem.key, &rbe->key);
		if (set->flags & NFT_SET_MAP &&
		    !(rbe->flags & NFT_SET_ELEM_INTERVAL_END))
			nft_data_copy(&elem.data, rbe->data);
		elem.flags = rbe->flags;

		iter->err = iter->fn(ctx, set, iter, &elem);
		if (iter->err < 0)
			return;
cont:
		iter->count++;
	}
}

static unsigned int nft_rbtree_privsize(const struct nlattr * const nla[])
{
	return sizeof(struct nft_rbtree);
}

static int nft_rbtree_init(const struct nft_set *set,
			   const struct nlattr * const nla[])
{
	struct nft_rbtree *priv = nft_set_priv(set);

	priv->root = RB_ROOT;
	return 0;
}

static void nft_rbtree_destroy(const struct nft_set *set)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe;
	struct rb_node *node;

	while ((node = priv->root.rb_node) != NULL) {
		rb_erase(node, &priv->root);
		rbe = rb_entry(node, struct nft_rbtree_elem, node);
		nft_rbtree_elem_destroy(set, rbe);
	}
}

static struct nft_set_ops nft_rbtree_ops __read_mostly = {
	.privsize	= nft_rbtree_privsize,
	.init		= nft_rbtree_init,
	.destroy	= nft_rbtree_destroy,
	.insert		= nft_rbtree_insert,
	.remove		= nft_rbtree_remove,
	.get		= nft_rbtree_get,
	.lookup		= nft_rbtree_lookup,
	.walk		= nft_rbtree_walk,
	.features	= NFT_SET_INTERVAL | NFT_SET_MAP,
	.owner		= THIS_MODULE,
};

static int __init nft_rbtree_module_init(void)
{
	return nft_register_set(&nft_rbtree_ops);
}

static void __exit nft_rbtree_module_exit(void)
{
	nft_unregister_set(&nft_rbtree_ops);
}

module_init(nft_rbtree_module_init);
module_exit(nft_rbtree_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_SET();
