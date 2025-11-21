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
	unsigned long		last_gc;
};

struct nft_rbtree_elem {
	struct nft_elem_priv	priv;
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

static bool nft_rbtree_elem_expired(const struct nft_rbtree_elem *rbe)
{
	return nft_set_elem_expired(&rbe->ext);
}

static const struct nft_set_ext *
__nft_rbtree_lookup(const struct net *net, const struct nft_set *set,
		    const u32 *key, unsigned int seq)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	const struct nft_rbtree_elem *rbe, *interval = NULL;
	u8 genmask = nft_genmask_cur(net);
	const struct rb_node *parent;
	int d;

	parent = rcu_dereference_raw(priv->root.rb_node);
	while (parent != NULL) {
		if (read_seqcount_retry(&priv->count, seq))
			return NULL;

		rbe = rb_entry(parent, struct nft_rbtree_elem, node);

		d = memcmp(nft_set_ext_key(&rbe->ext), key, set->klen);
		if (d < 0) {
			parent = rcu_dereference_raw(parent->rb_left);
			if (interval &&
			    !nft_rbtree_cmp(set, rbe, interval) &&
			    nft_rbtree_interval_end(rbe) &&
			    nft_rbtree_interval_start(interval))
				continue;
			if (nft_set_elem_active(&rbe->ext, genmask) &&
			    !nft_rbtree_elem_expired(rbe))
				interval = rbe;
		} else if (d > 0)
			parent = rcu_dereference_raw(parent->rb_right);
		else {
			if (!nft_set_elem_active(&rbe->ext, genmask)) {
				parent = rcu_dereference_raw(parent->rb_left);
				continue;
			}

			if (nft_rbtree_elem_expired(rbe))
				return NULL;

			if (nft_rbtree_interval_end(rbe)) {
				if (nft_set_is_anonymous(set))
					return NULL;
				parent = rcu_dereference_raw(parent->rb_left);
				interval = NULL;
				continue;
			}

			return &rbe->ext;
		}
	}

	if (set->flags & NFT_SET_INTERVAL && interval != NULL &&
	    nft_rbtree_interval_start(interval))
		return &interval->ext;

	return NULL;
}

INDIRECT_CALLABLE_SCOPE
const struct nft_set_ext *
nft_rbtree_lookup(const struct net *net, const struct nft_set *set,
		  const u32 *key)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	unsigned int seq = read_seqcount_begin(&priv->count);
	const struct nft_set_ext *ext;

	ext = __nft_rbtree_lookup(net, set, key, seq);
	if (ext || !read_seqcount_retry(&priv->count, seq))
		return ext;

	read_lock_bh(&priv->lock);
	seq = read_seqcount_begin(&priv->count);
	ext = __nft_rbtree_lookup(net, set, key, seq);
	read_unlock_bh(&priv->lock);

	return ext;
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

static struct nft_elem_priv *
nft_rbtree_get(const struct net *net, const struct nft_set *set,
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
		return &rbe->priv;

	read_lock_bh(&priv->lock);
	seq = read_seqcount_begin(&priv->count);
	ret = __nft_rbtree_get(net, set, key, &rbe, seq, flags, genmask);
	read_unlock_bh(&priv->lock);

	if (!ret)
		return ERR_PTR(-ENOENT);

	return &rbe->priv;
}

static void nft_rbtree_gc_elem_remove(struct net *net, struct nft_set *set,
				      struct nft_rbtree *priv,
				      struct nft_rbtree_elem *rbe)
{
	lockdep_assert_held_write(&priv->lock);
	nft_setelem_data_deactivate(net, set, &rbe->priv);
	rb_erase(&rbe->node, &priv->root);
}

static const struct nft_rbtree_elem *
nft_rbtree_gc_elem(const struct nft_set *__set, struct nft_rbtree *priv,
		   struct nft_rbtree_elem *rbe)
{
	struct nft_set *set = (struct nft_set *)__set;
	struct rb_node *prev = rb_prev(&rbe->node);
	struct net *net = read_pnet(&set->net);
	struct nft_rbtree_elem *rbe_prev;
	struct nft_trans_gc *gc;

	gc = nft_trans_gc_alloc(set, 0, GFP_ATOMIC);
	if (!gc)
		return ERR_PTR(-ENOMEM);

	/* search for end interval coming before this element.
	 * end intervals don't carry a timeout extension, they
	 * are coupled with the interval start element.
	 */
	while (prev) {
		rbe_prev = rb_entry(prev, struct nft_rbtree_elem, node);
		if (nft_rbtree_interval_end(rbe_prev) &&
		    nft_set_elem_active(&rbe_prev->ext, NFT_GENMASK_ANY))
			break;

		prev = rb_prev(prev);
	}

	rbe_prev = NULL;
	if (prev) {
		rbe_prev = rb_entry(prev, struct nft_rbtree_elem, node);
		nft_rbtree_gc_elem_remove(net, set, priv, rbe_prev);

		/* There is always room in this trans gc for this element,
		 * memory allocation never actually happens, hence, the warning
		 * splat in such case. No need to set NFT_SET_ELEM_DEAD_BIT,
		 * this is synchronous gc which never fails.
		 */
		gc = nft_trans_gc_queue_sync(gc, GFP_ATOMIC);
		if (WARN_ON_ONCE(!gc))
			return ERR_PTR(-ENOMEM);

		nft_trans_gc_elem_add(gc, rbe_prev);
	}

	nft_rbtree_gc_elem_remove(net, set, priv, rbe);
	gc = nft_trans_gc_queue_sync(gc, GFP_ATOMIC);
	if (WARN_ON_ONCE(!gc))
		return ERR_PTR(-ENOMEM);

	nft_trans_gc_elem_add(gc, rbe);

	nft_trans_gc_queue_sync_done(gc);

	return rbe_prev;
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
			       struct nft_elem_priv **elem_priv)
{
	struct nft_rbtree_elem *rbe, *rbe_le = NULL, *rbe_ge = NULL;
	struct rb_node *node, *next, *parent, **p, *first = NULL;
	struct nft_rbtree *priv = nft_set_priv(set);
	u8 cur_genmask = nft_genmask_cur(net);
	u8 genmask = nft_genmask_next(net);
	u64 tstamp = nft_net_tstamp(net);
	int d;

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
	for (node = first; node != NULL; node = next) {
		next = rb_next(node);

		rbe = rb_entry(node, struct nft_rbtree_elem, node);

		if (!nft_set_elem_active(&rbe->ext, genmask))
			continue;

		/* perform garbage collection to avoid bogus overlap reports
		 * but skip new elements in this transaction.
		 */
		if (__nft_set_elem_expired(&rbe->ext, tstamp) &&
		    nft_set_elem_active(&rbe->ext, cur_genmask)) {
			const struct nft_rbtree_elem *removed_end;

			removed_end = nft_rbtree_gc_elem(set, priv, rbe);
			if (IS_ERR(removed_end))
				return PTR_ERR(removed_end);

			if (removed_end == rbe_le || removed_end == rbe_ge)
				return -EAGAIN;

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
		*elem_priv = &rbe_ge->priv;
		return -EEXIST;
	}

	/* - new end element matching existing end element: full overlap
	 *   reported as -EEXIST, cleared by caller if NLM_F_EXCL is not given.
	 */
	if (rbe_le && !nft_rbtree_cmp(set, new, rbe_le) &&
	    nft_rbtree_interval_end(rbe_le) == nft_rbtree_interval_end(new)) {
		*elem_priv = &rbe_le->priv;
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
			     struct nft_elem_priv **elem_priv)
{
	struct nft_rbtree_elem *rbe = nft_elem_priv_cast(elem->priv);
	struct nft_rbtree *priv = nft_set_priv(set);
	int err;

	do {
		if (fatal_signal_pending(current))
			return -EINTR;

		cond_resched();

		write_lock_bh(&priv->lock);
		write_seqcount_begin(&priv->count);
		err = __nft_rbtree_insert(net, set, rbe, elem_priv);
		write_seqcount_end(&priv->count);
		write_unlock_bh(&priv->lock);
	} while (err == -EAGAIN);

	return err;
}

static void nft_rbtree_erase(struct nft_rbtree *priv, struct nft_rbtree_elem *rbe)
{
	write_lock_bh(&priv->lock);
	write_seqcount_begin(&priv->count);
	rb_erase(&rbe->node, &priv->root);
	write_seqcount_end(&priv->count);
	write_unlock_bh(&priv->lock);
}

static void nft_rbtree_remove(const struct net *net,
			      const struct nft_set *set,
			      struct nft_elem_priv *elem_priv)
{
	struct nft_rbtree_elem *rbe = nft_elem_priv_cast(elem_priv);
	struct nft_rbtree *priv = nft_set_priv(set);

	nft_rbtree_erase(priv, rbe);
}

static void nft_rbtree_activate(const struct net *net,
				const struct nft_set *set,
				struct nft_elem_priv *elem_priv)
{
	struct nft_rbtree_elem *rbe = nft_elem_priv_cast(elem_priv);

	nft_clear(net, &rbe->ext);
}

static void nft_rbtree_flush(const struct net *net,
			     const struct nft_set *set,
			     struct nft_elem_priv *elem_priv)
{
	struct nft_rbtree_elem *rbe = nft_elem_priv_cast(elem_priv);

	nft_set_elem_change_active(net, set, &rbe->ext);
}

static struct nft_elem_priv *
nft_rbtree_deactivate(const struct net *net, const struct nft_set *set,
		      const struct nft_set_elem *elem)
{
	struct nft_rbtree_elem *rbe, *this = nft_elem_priv_cast(elem->priv);
	const struct nft_rbtree *priv = nft_set_priv(set);
	const struct rb_node *parent = priv->root.rb_node;
	u8 genmask = nft_genmask_next(net);
	u64 tstamp = nft_net_tstamp(net);
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
			} else if (__nft_set_elem_expired(&rbe->ext, tstamp)) {
				break;
			} else if (!nft_set_elem_active(&rbe->ext, genmask)) {
				parent = parent->rb_left;
				continue;
			}
			nft_rbtree_flush(net, set, &rbe->priv);
			return &rbe->priv;
		}
	}
	return NULL;
}

static void nft_rbtree_do_walk(const struct nft_ctx *ctx,
			       struct nft_set *set,
			       struct nft_set_iter *iter)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe;
	struct rb_node *node;

	for (node = rb_first(&priv->root); node != NULL; node = rb_next(node)) {
		rbe = rb_entry(node, struct nft_rbtree_elem, node);

		if (iter->count < iter->skip)
			goto cont;

		iter->err = iter->fn(ctx, set, iter, &rbe->priv);
		if (iter->err < 0)
			return;
cont:
		iter->count++;
	}
}

static void nft_rbtree_walk(const struct nft_ctx *ctx,
			    struct nft_set *set,
			    struct nft_set_iter *iter)
{
	struct nft_rbtree *priv = nft_set_priv(set);

	switch (iter->type) {
	case NFT_ITER_UPDATE:
		lockdep_assert_held(&nft_pernet(ctx->net)->commit_mutex);
		nft_rbtree_do_walk(ctx, set, iter);
		break;
	case NFT_ITER_READ:
		read_lock_bh(&priv->lock);
		nft_rbtree_do_walk(ctx, set, iter);
		read_unlock_bh(&priv->lock);
		break;
	default:
		iter->err = -EINVAL;
		WARN_ON_ONCE(1);
		break;
	}
}

static void nft_rbtree_gc_remove(struct net *net, struct nft_set *set,
				 struct nft_rbtree *priv,
				 struct nft_rbtree_elem *rbe)
{
	nft_setelem_data_deactivate(net, set, &rbe->priv);
	nft_rbtree_erase(priv, rbe);
}

static void nft_rbtree_gc(struct nft_set *set)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe, *rbe_end = NULL;
	struct net *net = read_pnet(&set->net);
	u64 tstamp = nft_net_tstamp(net);
	struct rb_node *node, *next;
	struct nft_trans_gc *gc;

	set  = nft_set_container_of(priv);
	net  = read_pnet(&set->net);

	gc = nft_trans_gc_alloc(set, 0, GFP_KERNEL);
	if (!gc)
		return;

	for (node = rb_first(&priv->root); node ; node = next) {
		next = rb_next(node);

		rbe = rb_entry(node, struct nft_rbtree_elem, node);

		/* elements are reversed in the rbtree for historical reasons,
		 * from highest to lowest value, that is why end element is
		 * always visited before the start element.
		 */
		if (nft_rbtree_interval_end(rbe)) {
			rbe_end = rbe;
			continue;
		}
		if (!__nft_set_elem_expired(&rbe->ext, tstamp))
			continue;

		gc = nft_trans_gc_queue_sync(gc, GFP_KERNEL);
		if (!gc)
			goto try_later;

		/* end element needs to be removed first, it has
		 * no timeout extension.
		 */
		if (rbe_end) {
			nft_rbtree_gc_remove(net, set, priv, rbe_end);
			nft_trans_gc_elem_add(gc, rbe_end);
			rbe_end = NULL;
		}

		gc = nft_trans_gc_queue_sync(gc, GFP_KERNEL);
		if (!gc)
			goto try_later;

		nft_rbtree_gc_remove(net, set, priv, rbe);
		nft_trans_gc_elem_add(gc, rbe);
	}

try_later:

	if (gc) {
		gc = nft_trans_gc_catchall_sync(gc);
		nft_trans_gc_queue_sync_done(gc);
		priv->last_gc = jiffies;
	}
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

	BUILD_BUG_ON(offsetof(struct nft_rbtree_elem, priv) != 0);

	rwlock_init(&priv->lock);
	seqcount_rwlock_init(&priv->count, &priv->lock);
	priv->root = RB_ROOT;

	return 0;
}

static void nft_rbtree_destroy(const struct nft_ctx *ctx,
			       const struct nft_set *set)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe;
	struct rb_node *node;

	while ((node = priv->root.rb_node) != NULL) {
		rb_erase(node, &priv->root);
		rbe = rb_entry(node, struct nft_rbtree_elem, node);
		nf_tables_set_elem_destroy(ctx, set, &rbe->priv);
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

static void nft_rbtree_commit(struct nft_set *set)
{
	struct nft_rbtree *priv = nft_set_priv(set);

	if (time_after_eq(jiffies, priv->last_gc + nft_set_gc_interval(set)))
		nft_rbtree_gc(set);
}

static void nft_rbtree_gc_init(const struct nft_set *set)
{
	struct nft_rbtree *priv = nft_set_priv(set);

	priv->last_gc = jiffies;
}

/* rbtree stores ranges as singleton elements, each range is composed of two
 * elements ...
 */
static u32 nft_rbtree_ksize(u32 size)
{
	return size * 2;
}

/* ... hide this detail to userspace. */
static u32 nft_rbtree_usize(u32 size)
{
	if (!size)
		return 0;

	return size / 2;
}

static u32 nft_rbtree_adjust_maxsize(const struct nft_set *set)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe;
	struct rb_node *node;
	const void *key;

	node = rb_last(&priv->root);
	if (!node)
		return 0;

	rbe = rb_entry(node, struct nft_rbtree_elem, node);
	if (!nft_rbtree_interval_end(rbe))
		return 0;

	key = nft_set_ext_key(&rbe->ext);
	if (memchr(key, 1, set->klen))
		return 0;

	/* this is the all-zero no-match element. */
	return 1;
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
		.commit		= nft_rbtree_commit,
		.gc_init	= nft_rbtree_gc_init,
		.lookup		= nft_rbtree_lookup,
		.walk		= nft_rbtree_walk,
		.get		= nft_rbtree_get,
		.ksize		= nft_rbtree_ksize,
		.usize		= nft_rbtree_usize,
		.adjust_maxsize = nft_rbtree_adjust_maxsize,
	},
};
