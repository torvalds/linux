// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
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
#include <net/netfilter/nf_tables_core.h>

struct nft_rbtree {
	struct rb_root		root;
	rwlock_t		lock;
	seqcount_rwlock_t	count;
	struct delayed_work	gc_work;
};

struct nft_rbtree_elem {
	struct rb_node		node;
	struct nft_set_ext	ext;
};

static bool nft_rbtree_interval_end(const struct nft_rbtree_elem *rbe)
{
	return nft_set_ext_exists(&rbe->ext, NFT_SET_EXT_FLAGS) &&
	       (*nft_set_ext_flags(&rbe->ext) & NFT_SET_ELEM_INTERVAL_END);
}

static bool nft_rbtree_interval_start(const struct nft_rbtree_elem *rbe)
{
	return !nft_rbtree_interval_end(rbe);
}

static int nft_rbtree_cmp(const struct nft_set *set,
			  const struct nft_rbtree_elem *e1,
			  const struct nft_rbtree_elem *e2)
{
	return memcmp(nft_set_ext_key(&e1->ext), nft_set_ext_key(&e2->ext),
		      set->klen);
}

static bool __nft_rbtree_lookup(const struct net *net, const struct nft_set *set,
				const u32 *key, const struct nft_set_ext **ext,
				unsigned int seq)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	const struct nft_rbtree_elem *rbe, *interval = NULL;
	u8 genmask = nft_genmask_cur(net);
	const struct rb_node *parent;
	int d;

	parent = rcu_dereference_raw(priv->root.rb_node);
	while (parent != NULL) {
		if (read_seqcount_retry(&priv->count, seq))
			return false;

		rbe = rb_entry(parent, struct nft_rbtree_elem, node);

		d = memcmp(nft_set_ext_key(&rbe->ext), key, set->klen);
		if (d < 0) {
			parent = rcu_dereference_raw(parent->rb_left);
			if (interval &&
			    !nft_rbtree_cmp(set, rbe, interval) &&
			    nft_rbtree_interval_end(rbe) &&
			    nft_rbtree_interval_start(interval))
				continue;
			interval = rbe;
		} else if (d > 0)
			parent = rcu_dereference_raw(parent->rb_right);
		else {
			if (!nft_set_elem_active(&rbe->ext, genmask)) {
				parent = rcu_dereference_raw(parent->rb_left);
				continue;
			}

			if (nft_set_elem_expired(&rbe->ext))
				return false;

			if (nft_rbtree_interval_end(rbe)) {
				if (nft_set_is_anonymous(set))
					return false;
				parent = rcu_dereference_raw(parent->rb_left);
				interval = NULL;
				continue;
			}

			*ext = &rbe->ext;
			return true;
		}
	}

	if (set->flags & NFT_SET_INTERVAL && interval != NULL &&
	    nft_set_elem_active(&interval->ext, genmask) &&
	    !nft_set_elem_expired(&interval->ext) &&
	    nft_rbtree_interval_start(interval)) {
		*ext = &interval->ext;
		return true;
	}

	return false;
}

INDIRECT_CALLABLE_SCOPE
bool nft_rbtree_lookup(const struct net *net, const struct nft_set *set,
		       const u32 *key, const struct nft_set_ext **ext)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	unsigned int seq = read_seqcount_begin(&priv->count);
	bool ret;

	ret = __nft_rbtree_lookup(net, set, key, ext, seq);
	if (ret || !read_seqcount_retry(&priv->count, seq))
		return ret;

	read_lock_bh(&priv->lock);
	seq = read_seqcount_begin(&priv->count);
	ret = __nft_rbtree_lookup(net, set, key, ext, seq);
	read_unlock_bh(&priv->lock);

	return ret;
}

static bool __nft_rbtree_get(const struct net *net, const struct nft_set *set,
			     const u32 *key, struct nft_rbtree_elem **elem,
			     unsigned int seq, unsigned int flags, u8 genmask)
{
	struct nft_rbtree_elem *rbe, *interval = NULL;
	struct nft_rbtree *priv = nft_set_priv(set);
	const struct rb_node *parent;
	const void *this;
	int d;

	parent = rcu_dereference_raw(priv->root.rb_node);
	while (parent != NULL) {
		if (read_seqcount_retry(&priv->count, seq))
			return false;

		rbe = rb_entry(parent, struct nft_rbtree_elem, node);

		this = nft_set_ext_key(&rbe->ext);
		d = memcmp(this, key, set->klen);
		if (d < 0) {
			parent = rcu_dereference_raw(parent->rb_left);
			if (!(flags & NFT_SET_ELEM_INTERVAL_END))
				interval = rbe;
		} else if (d > 0) {
			parent = rcu_dereference_raw(parent->rb_right);
			if (flags & NFT_SET_ELEM_INTERVAL_END)
				interval = rbe;
		} else {
			if (!nft_set_elem_active(&rbe->ext, genmask)) {
				parent = rcu_dereference_raw(parent->rb_left);
				continue;
			}

			if (nft_set_elem_expired(&rbe->ext))
				return false;

			if (!nft_set_ext_exists(&rbe->ext, NFT_SET_EXT_FLAGS) ||
			    (*nft_set_ext_flags(&rbe->ext) & NFT_SET_ELEM_INTERVAL_END) ==
			    (flags & NFT_SET_ELEM_INTERVAL_END)) {
				*elem = rbe;
				return true;
			}

			if (nft_rbtree_interval_end(rbe))
				interval = NULL;

			parent = rcu_dereference_raw(parent->rb_left);
		}
	}

	if (set->flags & NFT_SET_INTERVAL && interval != NULL &&
	    nft_set_elem_active(&interval->ext, genmask) &&
	    !nft_set_elem_expired(&interval->ext) &&
	    ((!nft_rbtree_interval_end(interval) &&
	      !(flags & NFT_SET_ELEM_INTERVAL_END)) ||
	     (nft_rbtree_interval_end(interval) &&
	      (flags & NFT_SET_ELEM_INTERVAL_END)))) {
		*elem = interval;
		return true;
	}

	return false;
}

static void *nft_rbtree_get(const struct net *net, const struct nft_set *set,
			    const struct nft_set_elem *elem, unsigned int flags)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	unsigned int seq = read_seqcount_begin(&priv->count);
	struct nft_rbtree_elem *rbe = ERR_PTR(-ENOENT);
	const u32 *key = (const u32 *)&elem->key.val;
	u8 genmask = nft_genmask_cur(net);
	bool ret;

	ret = __nft_rbtree_get(net, set, key, &rbe, seq, flags, genmask);
	if (ret || !read_seqcount_retry(&priv->count, seq))
		return rbe;

	read_lock_bh(&priv->lock);
	seq = read_seqcount_begin(&priv->count);
	ret = __nft_rbtree_get(net, set, key, &rbe, seq, flags, genmask);
	if (!ret)
		rbe = ERR_PTR(-ENOENT);
	read_unlock_bh(&priv->lock);

	return rbe;
}

static int nft_rbtree_gc_elem(const struct nft_set *__set,
			      struct nft_rbtree *priv,
			      struct nft_rbtree_elem *rbe)
{
	struct nft_set *set = (struct nft_set *)__set;
	struct rb_node *prev = rb_prev(&rbe->node);
	struct nft_rbtree_elem *rbe_prev;
	struct nft_set_gc_batch *gcb;

	gcb = nft_set_gc_batch_check(set, NULL, GFP_ATOMIC);
	if (!gcb)
		return -ENOMEM;

	/* search for expired end interval coming before this element. */
	do {
		rbe_prev = rb_entry(prev, struct nft_rbtree_elem, node);
		if (nft_rbtree_interval_end(rbe_prev))
			break;

		prev = rb_prev(prev);
	} while (prev != NULL);

	rb_erase(&rbe_prev->node, &priv->root);
	rb_erase(&rbe->node, &priv->root);
	atomic_sub(2, &set->nelems);

	nft_set_gc_batch_add(gcb, rbe);
	nft_set_gc_batch_complete(gcb);

	return 0;
}

static bool nft_rbtree_update_first(const struct nft_set *set,
				    struct nft_rbtree_elem *rbe,
				    struct rb_node *first)
{
	struct nft_rbtree_elem *first_elem;

	first_elem = rb_entry(first, struct nft_rbtree_elem, node);
	/* this element is closest to where the new element is to be inserted:
	 * update the first element for the node list path.
	 */
	if (nft_rbtree_cmp(set, rbe, first_elem) < 0)
		return true;

	return false;
}

static int __nft_rbtree_insert(const struct net *net, const struct nft_set *set,
			       struct nft_rbtree_elem *new,
			       struct nft_set_ext **ext)
{
	struct nft_rbtree_elem *rbe, *rbe_le = NULL, *rbe_ge = NULL;
	struct rb_node *node, *parent, **p, *first = NULL;
	struct nft_rbtree *priv = nft_set_priv(set);
	u8 genmask = nft_genmask_next(net);
	int d, err;

	/* Descend the tree to search for an existing element greater than the
	 * key value to insert that is greater than the new element. This is the
	 * first element to walk the ordered elements to find possible overlap.
	 */
	parent = NULL;
	p = &priv->root.rb_node;
	while (*p != NULL) {
		parent = *p;
		rbe = rb_entry(parent, struct nft_rbtree_elem, node);
		d = nft_rbtree_cmp(set, rbe, new);

		if (d < 0) {
			p = &parent->rb_left;
		} else if (d > 0) {
			if (!first ||
			    nft_rbtree_update_first(set, rbe, first))
				first = &rbe->node;

			p = &parent->rb_right;
		} else {
			if (nft_rbtree_interval_end(rbe))
				p = &parent->rb_left;
			else
				p = &parent->rb_right;
		}
	}

	if (!first)
		first = rb_first(&priv->root);

	/* Detect overlap by going through the list of valid tree nodes.
	 * Values stored in the tree are in reversed order, starting from
	 * highest to lowest value.
	 */
	for (node = first; node != NULL; node = rb_next(node)) {
		rbe = rb_entry(node, struct nft_rbtree_elem, node);

		if (!nft_set_elem_active(&rbe->ext, genmask))
			continue;

		/* perform garbage collection to avoid bogus overlap reports. */
		if (nft_set_elem_expired(&rbe->ext)) {
			err = nft_rbtree_gc_elem(set, priv, rbe);
			if (err < 0)
				return err;

			continue;
		}

		d = nft_rbtree_cmp(set, rbe, new);
		if (d == 0) {
			/* Matching end element: no need to look for an
			 * overlapping greater or equal element.
			 */
			if (nft_rbtree_interval_end(rbe)) {
				rbe_le = rbe;
				break;
			}

			/* first element that is greater or equal to key value. */
			if (!rbe_ge) {
				rbe_ge = rbe;
				continue;
			}

			/* this is a closer more or equal element, update it. */
			if (nft_rbtree_cmp(set, rbe_ge, new) != 0) {
				rbe_ge = rbe;
				continue;
			}

			/* element is equal to key value, make sure flags are
			 * the same, an existing more or equal start element
			 * must not be replaced by more or equal end element.
			 */
			if ((nft_rbtree_interval_start(new) &&
			     nft_rbtree_interval_start(rbe_ge)) ||
			    (nft_rbtree_interval_end(new) &&
			     nft_rbtree_interval_end(rbe_ge))) {
				rbe_ge = rbe;
				continue;
			}
		} else if (d > 0) {
			/* annotate element greater than the new element. */
			rbe_ge = rbe;
			continue;
		} else if (d < 0) {
			/* annotate element less than the new element. */
			rbe_le = rbe;
			break;
		}
	}

	/* - new start element matching existing start element: full overlap
	 *   reported as -EEXIST, cleared by caller if NLM_F_EXCL is not given.
	 */
	if (rbe_ge && !nft_rbtree_cmp(set, new, rbe_ge) &&
	    nft_rbtree_interval_start(rbe_ge) == nft_rbtree_interval_start(new)) {
		*ext = &rbe_ge->ext;
		return -EEXIST;
	}

	/* - new end element matching existing end element: full overlap
	 *   reported as -EEXIST, cleared by caller if NLM_F_EXCL is not given.
	 */
	if (rbe_le && !nft_rbtree_cmp(set, new, rbe_le) &&
	    nft_rbtree_interval_end(rbe_le) == nft_rbtree_interval_end(new)) {
		*ext = &rbe_le->ext;
		return -EEXIST;
	}

	/* - new start element with existing closest, less or equal key value
	 *   being a start element: partial overlap, reported as -ENOTEMPTY.
	 *   Anonymous sets allow for two consecutive start element since they
	 *   are constant, skip them to avoid bogus overlap reports.
	 */
	if (!nft_set_is_anonymous(set) && rbe_le &&
	    nft_rbtree_interval_start(rbe_le) && nft_rbtree_interval_start(new))
		return -ENOTEMPTY;

	/* - new end element with existing closest, less or equal key value
	 *   being a end element: partial overlap, reported as -ENOTEMPTY.
	 */
	if (rbe_le &&
	    nft_rbtree_interval_end(rbe_le) && nft_rbtree_interval_end(new))
		return -ENOTEMPTY;

	/* - new end element with existing closest, greater or equal key value
	 *   being an end element: partial overlap, reported as -ENOTEMPTY
	 */
	if (rbe_ge &&
	    nft_rbtree_interval_end(rbe_ge) && nft_rbtree_interval_end(new))
		return -ENOTEMPTY;

	/* Accepted element: pick insertion point depending on key value */
	parent = NULL;
	p = &priv->root.rb_node;
	while (*p != NULL) {
		parent = *p;
		rbe = rb_entry(parent, struct nft_rbtree_elem, node);
		d = nft_rbtree_cmp(set, rbe, new);

		if (d < 0)
			p = &parent->rb_left;
		else if (d > 0)
			p = &parent->rb_right;
		else if (nft_rbtree_interval_end(rbe))
			p = &parent->rb_left;
		else
			p = &parent->rb_right;
	}

	rb_link_node_rcu(&new->node, parent, p);
	rb_insert_color(&new->node, &priv->root);
	return 0;
}

static int nft_rbtree_insert(const struct net *net, const struct nft_set *set,
			     const struct nft_set_elem *elem,
			     struct nft_set_ext **ext)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe = elem->priv;
	int err;

	write_lock_bh(&priv->lock);
	write_seqcount_begin(&priv->count);
	err = __nft_rbtree_insert(net, set, rbe, ext);
	write_seqcount_end(&priv->count);
	write_unlock_bh(&priv->lock);

	return err;
}

static void nft_rbtree_remove(const struct net *net,
			      const struct nft_set *set,
			      const struct nft_set_elem *elem)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe = elem->priv;

	write_lock_bh(&priv->lock);
	write_seqcount_begin(&priv->count);
	rb_erase(&rbe->node, &priv->root);
	write_seqcount_end(&priv->count);
	write_unlock_bh(&priv->lock);
}

static void nft_rbtree_activate(const struct net *net,
				const struct nft_set *set,
				const struct nft_set_elem *elem)
{
	struct nft_rbtree_elem *rbe = elem->priv;

	nft_set_elem_change_active(net, set, &rbe->ext);
	nft_set_elem_clear_busy(&rbe->ext);
}

static bool nft_rbtree_flush(const struct net *net,
			     const struct nft_set *set, void *priv)
{
	struct nft_rbtree_elem *rbe = priv;

	if (!nft_set_elem_mark_busy(&rbe->ext) ||
	    !nft_is_active(net, &rbe->ext)) {
		nft_set_elem_change_active(net, set, &rbe->ext);
		return true;
	}
	return false;
}

static void *nft_rbtree_deactivate(const struct net *net,
				   const struct nft_set *set,
				   const struct nft_set_elem *elem)
{
	const struct nft_rbtree *priv = nft_set_priv(set);
	const struct rb_node *parent = priv->root.rb_node;
	struct nft_rbtree_elem *rbe, *this = elem->priv;
	u8 genmask = nft_genmask_next(net);
	int d;

	while (parent != NULL) {
		rbe = rb_entry(parent, struct nft_rbtree_elem, node);

		d = memcmp(nft_set_ext_key(&rbe->ext), &elem->key.val,
					   set->klen);
		if (d < 0)
			parent = parent->rb_left;
		else if (d > 0)
			parent = parent->rb_right;
		else {
			if (nft_rbtree_interval_end(rbe) &&
			    nft_rbtree_interval_start(this)) {
				parent = parent->rb_left;
				continue;
			} else if (nft_rbtree_interval_start(rbe) &&
				   nft_rbtree_interval_end(this)) {
				parent = parent->rb_right;
				continue;
			} else if (!nft_set_elem_active(&rbe->ext, genmask)) {
				parent = parent->rb_left;
				continue;
			}
			nft_rbtree_flush(net, set, rbe);
			return rbe;
		}
	}
	return NULL;
}

static void nft_rbtree_walk(const struct nft_ctx *ctx,
			    struct nft_set *set,
			    struct nft_set_iter *iter)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe;
	struct nft_set_elem elem;
	struct rb_node *node;

	read_lock_bh(&priv->lock);
	for (node = rb_first(&priv->root); node != NULL; node = rb_next(node)) {
		rbe = rb_entry(node, struct nft_rbtree_elem, node);

		if (iter->count < iter->skip)
			goto cont;
		if (nft_set_elem_expired(&rbe->ext))
			goto cont;
		if (!nft_set_elem_active(&rbe->ext, iter->genmask))
			goto cont;

		elem.priv = rbe;

		iter->err = iter->fn(ctx, set, iter, &elem);
		if (iter->err < 0) {
			read_unlock_bh(&priv->lock);
			return;
		}
cont:
		iter->count++;
	}
	read_unlock_bh(&priv->lock);
}

static void nft_rbtree_gc(struct work_struct *work)
{
	struct nft_rbtree_elem *rbe, *rbe_end = NULL, *rbe_prev = NULL;
	struct nft_set_gc_batch *gcb = NULL;
	struct nft_rbtree *priv;
	struct rb_node *node;
	struct nft_set *set;
	struct net *net;
	u8 genmask;

	priv = container_of(work, struct nft_rbtree, gc_work.work);
	set  = nft_set_container_of(priv);
	net  = read_pnet(&set->net);
	genmask = nft_genmask_cur(net);

	write_lock_bh(&priv->lock);
	write_seqcount_begin(&priv->count);
	for (node = rb_first(&priv->root); node != NULL; node = rb_next(node)) {
		rbe = rb_entry(node, struct nft_rbtree_elem, node);

		if (!nft_set_elem_active(&rbe->ext, genmask))
			continue;

		/* elements are reversed in the rbtree for historical reasons,
		 * from highest to lowest value, that is why end element is
		 * always visited before the start element.
		 */
		if (nft_rbtree_interval_end(rbe)) {
			rbe_end = rbe;
			continue;
		}
		if (!nft_set_elem_expired(&rbe->ext))
			continue;

		if (nft_set_elem_mark_busy(&rbe->ext)) {
			rbe_end = NULL;
			continue;
		}

		if (rbe_prev) {
			rb_erase(&rbe_prev->node, &priv->root);
			rbe_prev = NULL;
		}
		gcb = nft_set_gc_batch_check(set, gcb, GFP_ATOMIC);
		if (!gcb)
			break;

		atomic_dec(&set->nelems);
		nft_set_gc_batch_add(gcb, rbe);
		rbe_prev = rbe;

		if (rbe_end) {
			atomic_dec(&set->nelems);
			nft_set_gc_batch_add(gcb, rbe_end);
			rb_erase(&rbe_end->node, &priv->root);
			rbe_end = NULL;
		}
		node = rb_next(node);
		if (!node)
			break;
	}
	if (rbe_prev)
		rb_erase(&rbe_prev->node, &priv->root);
	write_seqcount_end(&priv->count);
	write_unlock_bh(&priv->lock);

	rbe = nft_set_catchall_gc(set);
	if (rbe) {
		gcb = nft_set_gc_batch_check(set, gcb, GFP_ATOMIC);
		if (gcb)
			nft_set_gc_batch_add(gcb, rbe);
	}
	nft_set_gc_batch_complete(gcb);

	queue_delayed_work(system_power_efficient_wq, &priv->gc_work,
			   nft_set_gc_interval(set));
}

static u64 nft_rbtree_privsize(const struct nlattr * const nla[],
			       const struct nft_set_desc *desc)
{
	return sizeof(struct nft_rbtree);
}

static int nft_rbtree_init(const struct nft_set *set,
			   const struct nft_set_desc *desc,
			   const struct nlattr * const nla[])
{
	struct nft_rbtree *priv = nft_set_priv(set);

	rwlock_init(&priv->lock);
	seqcount_rwlock_init(&priv->count, &priv->lock);
	priv->root = RB_ROOT;

	INIT_DEFERRABLE_WORK(&priv->gc_work, nft_rbtree_gc);
	if (set->flags & NFT_SET_TIMEOUT)
		queue_delayed_work(system_power_efficient_wq, &priv->gc_work,
				   nft_set_gc_interval(set));

	return 0;
}

static void nft_rbtree_destroy(const struct nft_set *set)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe;
	struct rb_node *node;

	cancel_delayed_work_sync(&priv->gc_work);
	rcu_barrier();
	while ((node = priv->root.rb_node) != NULL) {
		rb_erase(node, &priv->root);
		rbe = rb_entry(node, struct nft_rbtree_elem, node);
		nft_set_elem_destroy(set, rbe, true);
	}
}

static bool nft_rbtree_estimate(const struct nft_set_desc *desc, u32 features,
				struct nft_set_estimate *est)
{
	if (desc->field_count > 1)
		return false;

	if (desc->size)
		est->size = sizeof(struct nft_rbtree) +
			    desc->size * sizeof(struct nft_rbtree_elem);
	else
		est->size = ~0;

	est->lookup = NFT_SET_CLASS_O_LOG_N;
	est->space  = NFT_SET_CLASS_O_N;

	return true;
}

const struct nft_set_type nft_set_rbtree_type = {
	.features	= NFT_SET_INTERVAL | NFT_SET_MAP | NFT_SET_OBJECT | NFT_SET_TIMEOUT,
	.ops		= {
		.privsize	= nft_rbtree_privsize,
		.elemsize	= offsetof(struct nft_rbtree_elem, ext),
		.estimate	= nft_rbtree_estimate,
		.init		= nft_rbtree_init,
		.destroy	= nft_rbtree_destroy,
		.insert		= nft_rbtree_insert,
		.remove		= nft_rbtree_remove,
		.deactivate	= nft_rbtree_deactivate,
		.flush		= nft_rbtree_flush,
		.activate	= nft_rbtree_activate,
		.lookup		= nft_rbtree_lookup,
		.walk		= nft_rbtree_walk,
		.get		= nft_rbtree_get,
	},
};
