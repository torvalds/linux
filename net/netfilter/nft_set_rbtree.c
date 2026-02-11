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
#include <linux/bsearch.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>

struct nft_array_interval {
	struct nft_set_ext	*from;
	struct nft_set_ext	*to;
};

struct nft_array {
	u32			max_intervals;
	u32			num_intervals;
	struct nft_array_interval *intervals;
	struct rcu_head		rcu_head;
};

struct nft_rbtree {
	struct rb_root		root;
	rwlock_t		lock;
	struct nft_array __rcu	*array;
	struct nft_array	*array_next;
	unsigned long		start_rbe_cookie;
	unsigned long		last_gc;
	struct list_head	expired;
	u64			last_tstamp;
};

struct nft_rbtree_elem {
	struct nft_elem_priv	priv;
	union {
		struct rb_node	node;
		struct list_head list;
	};
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

static bool nft_rbtree_interval_null(const struct nft_set *set,
				     const struct nft_rbtree_elem *rbe)
{
	return (!memchr_inv(nft_set_ext_key(&rbe->ext), 0, set->klen) &&
		nft_rbtree_interval_end(rbe));
}

static int nft_rbtree_cmp(const struct nft_set *set,
			  const struct nft_rbtree_elem *e1,
			  const struct nft_rbtree_elem *e2)
{
	return memcmp(nft_set_ext_key(&e1->ext), nft_set_ext_key(&e2->ext),
		      set->klen);
}

struct nft_array_lookup_ctx {
	const u32	*key;
	u32		klen;
};

static int nft_array_lookup_cmp(const void *pkey, const void *entry)
{
	const struct nft_array_interval *interval = entry;
	const struct nft_array_lookup_ctx *ctx = pkey;
	int a, b;

	if (!interval->from)
		return 1;

	a = memcmp(ctx->key, nft_set_ext_key(interval->from), ctx->klen);
	if (!interval->to)
		b = -1;
	else
		b = memcmp(ctx->key, nft_set_ext_key(interval->to), ctx->klen);

	if (a >= 0 && b < 0)
		return 0;

	if (a < 0)
		return -1;

	return 1;
}

INDIRECT_CALLABLE_SCOPE
const struct nft_set_ext *
nft_rbtree_lookup(const struct net *net, const struct nft_set *set,
		  const u32 *key)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_array *array = rcu_dereference(priv->array);
	const struct nft_array_interval *interval;
	struct nft_array_lookup_ctx ctx = {
		.key	= key,
		.klen	= set->klen,
	};

	if (!array)
		return NULL;

	interval = bsearch(&ctx, array->intervals, array->num_intervals,
			   sizeof(struct nft_array_interval),
			   nft_array_lookup_cmp);
	if (!interval || nft_set_elem_expired(interval->from))
		return NULL;

	return interval->from;
}

struct nft_array_get_ctx {
	const u32	*key;
	unsigned int	flags;
	u32		klen;
};

static int nft_array_get_cmp(const void *pkey, const void *entry)
{
	const struct nft_array_interval *interval = entry;
	const struct nft_array_get_ctx *ctx = pkey;
	int a, b;

	if (!interval->from)
		return 1;

	a = memcmp(ctx->key, nft_set_ext_key(interval->from), ctx->klen);
	if (!interval->to)
		b = -1;
	else
		b = memcmp(ctx->key, nft_set_ext_key(interval->to), ctx->klen);

	if (a >= 0) {
		if (ctx->flags & NFT_SET_ELEM_INTERVAL_END && b <= 0)
			return 0;
		else if (b < 0)
			return 0;
	}

	if (a < 0)
		return -1;

	return 1;
}

static struct nft_elem_priv *
nft_rbtree_get(const struct net *net, const struct nft_set *set,
	       const struct nft_set_elem *elem, unsigned int flags)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_array *array = rcu_dereference(priv->array);
	const struct nft_array_interval *interval;
	struct nft_array_get_ctx ctx = {
		.key	= (const u32 *)&elem->key.val,
		.flags	= flags,
		.klen	= set->klen,
	};
	struct nft_rbtree_elem *rbe;

	if (!array)
		return ERR_PTR(-ENOENT);

	interval = bsearch(&ctx, array->intervals, array->num_intervals,
			   sizeof(struct nft_array_interval), nft_array_get_cmp);
	if (!interval || nft_set_elem_expired(interval->from))
		return ERR_PTR(-ENOENT);

	if (flags & NFT_SET_ELEM_INTERVAL_END)
		rbe = container_of(interval->to, struct nft_rbtree_elem, ext);
	else
		rbe = container_of(interval->from, struct nft_rbtree_elem, ext);

	return &rbe->priv;
}

static void nft_rbtree_gc_elem_move(struct net *net, struct nft_set *set,
				    struct nft_rbtree *priv,
				    struct nft_rbtree_elem *rbe)
{
	lockdep_assert_held_write(&priv->lock);
	nft_setelem_data_deactivate(net, set, &rbe->priv);
	rb_erase(&rbe->node, &priv->root);

	/* collected later on in commit callback */
	list_add(&rbe->list, &priv->expired);
}

static const struct nft_rbtree_elem *
nft_rbtree_gc_elem(const struct nft_set *__set, struct nft_rbtree *priv,
		   struct nft_rbtree_elem *rbe)
{
	struct nft_set *set = (struct nft_set *)__set;
	struct rb_node *prev = rb_prev(&rbe->node);
	struct net *net = read_pnet(&set->net);
	struct nft_rbtree_elem *rbe_prev;

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
		nft_rbtree_gc_elem_move(net, set, priv, rbe_prev);
	}

	nft_rbtree_gc_elem_move(net, set, priv, rbe);

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

/* Only for anonymous sets which do not allow updates, all element are active. */
static struct nft_rbtree_elem *nft_rbtree_prev_active(struct nft_rbtree_elem *rbe)
{
	struct rb_node *node;

	node = rb_prev(&rbe->node);
	if (!node)
		return NULL;

	return rb_entry(node, struct nft_rbtree_elem, node);
}

static struct nft_rbtree_elem *
__nft_rbtree_next_active(struct rb_node *node, u8 genmask)
{
	struct nft_rbtree_elem *next_rbe;

	while (node) {
		next_rbe = rb_entry(node, struct nft_rbtree_elem, node);
		if (!nft_set_elem_active(&next_rbe->ext, genmask)) {
			node = rb_next(node);
			continue;
		}

		return next_rbe;
	}

	return NULL;
}

static struct nft_rbtree_elem *
nft_rbtree_next_active(struct nft_rbtree_elem *rbe, u8 genmask)
{
	return __nft_rbtree_next_active(rb_next(&rbe->node), genmask);
}

static void nft_rbtree_maybe_reset_start_cookie(struct nft_rbtree *priv,
						u64 tstamp)
{
	if (priv->last_tstamp != tstamp) {
		priv->start_rbe_cookie = 0;
		priv->last_tstamp = tstamp;
	}
}

static void nft_rbtree_set_start_cookie(struct nft_rbtree *priv,
					const struct nft_rbtree_elem *rbe)
{
	priv->start_rbe_cookie = (unsigned long)rbe;
}

static void nft_rbtree_set_start_cookie_open(struct nft_rbtree *priv,
					     const struct nft_rbtree_elem *rbe,
					     unsigned long open_interval)
{
	priv->start_rbe_cookie = (unsigned long)rbe | open_interval;
}

#define NFT_RBTREE_OPEN_INTERVAL	1UL

static bool nft_rbtree_cmp_start_cookie(struct nft_rbtree *priv,
					const struct nft_rbtree_elem *rbe)
{
	return (priv->start_rbe_cookie & ~NFT_RBTREE_OPEN_INTERVAL) == (unsigned long)rbe;
}

static bool nft_rbtree_insert_same_interval(const struct net *net,
					    struct nft_rbtree *priv,
					    struct nft_rbtree_elem *rbe)
{
	u8 genmask = nft_genmask_next(net);
	struct nft_rbtree_elem *next_rbe;

	if (!priv->start_rbe_cookie)
		return true;

	next_rbe = nft_rbtree_next_active(rbe, genmask);
	if (next_rbe) {
		/* Closest start element differs from last element added. */
		if (nft_rbtree_interval_start(next_rbe) &&
		    nft_rbtree_cmp_start_cookie(priv, next_rbe)) {
			priv->start_rbe_cookie = 0;
			return true;
		}
	}

	priv->start_rbe_cookie = 0;

	return false;
}

static int __nft_rbtree_insert(const struct net *net, const struct nft_set *set,
			       struct nft_rbtree_elem *new,
			       struct nft_elem_priv **elem_priv, u64 tstamp, bool last)
{
	struct nft_rbtree_elem *rbe, *rbe_le = NULL, *rbe_ge = NULL, *rbe_prev;
	struct rb_node *node, *next, *parent, **p, *first = NULL;
	struct nft_rbtree *priv = nft_set_priv(set);
	u8 cur_genmask = nft_genmask_cur(net);
	u8 genmask = nft_genmask_next(net);
	unsigned long open_interval = 0;
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

	if (nft_rbtree_interval_null(set, new)) {
		priv->start_rbe_cookie = 0;
	} else if (nft_rbtree_interval_start(new) && priv->start_rbe_cookie) {
		if (nft_set_is_anonymous(set)) {
			priv->start_rbe_cookie = 0;
		} else if (priv->start_rbe_cookie & NFT_RBTREE_OPEN_INTERVAL) {
			/* Previous element is an open interval that partially
			 * overlaps with an existing non-open interval.
			 */
			return -ENOTEMPTY;
		}
	}

	/* - new start element matching existing start element: full overlap
	 *   reported as -EEXIST, cleared by caller if NLM_F_EXCL is not given.
	 */
	if (rbe_ge && !nft_rbtree_cmp(set, new, rbe_ge) &&
	    nft_rbtree_interval_start(rbe_ge) == nft_rbtree_interval_start(new)) {
		*elem_priv = &rbe_ge->priv;

		/* - Corner case: new start element of open interval (which
		 *   comes as last element in the batch) overlaps the start of
		 *   an existing interval with an end element: partial overlap.
		 */
		node = rb_first(&priv->root);
		rbe = __nft_rbtree_next_active(node, genmask);
		if (rbe && nft_rbtree_interval_end(rbe)) {
			rbe = nft_rbtree_next_active(rbe, genmask);
			if (rbe &&
			    nft_rbtree_interval_start(rbe) &&
			    !nft_rbtree_cmp(set, new, rbe)) {
				if (last)
					return -ENOTEMPTY;

				/* Maybe open interval? */
				open_interval = NFT_RBTREE_OPEN_INTERVAL;
			}
		}
		nft_rbtree_set_start_cookie_open(priv, rbe_ge, open_interval);

		return -EEXIST;
	}

	/* - new end element matching existing end element: full overlap
	 *   reported as -EEXIST, cleared by caller if NLM_F_EXCL is not given.
	 */
	if (rbe_le && !nft_rbtree_cmp(set, new, rbe_le) &&
	    nft_rbtree_interval_end(rbe_le) == nft_rbtree_interval_end(new)) {
		/* - ignore null interval, otherwise NLM_F_CREATE bogusly
		 *   reports EEXIST.
		 */
		if (nft_rbtree_interval_null(set, new))
			return -ECANCELED;

		*elem_priv = &rbe_le->priv;

		/* - start and end element belong to the same interval. */
		if (!nft_rbtree_insert_same_interval(net, priv, rbe_le))
			return -ENOTEMPTY;

		return -EEXIST;
	}

	/* - new start element with existing closest, less or equal key value
	 *   being a start element: partial overlap, reported as -ENOTEMPTY.
	 *   Anonymous sets allow for two consecutive start element since they
	 *   are constant, but validate that this new start element does not
	 *   sit in between an existing start and end elements: partial overlap,
	 *   reported as -ENOTEMPTY.
	 */
	if (rbe_le &&
	    nft_rbtree_interval_start(rbe_le) && nft_rbtree_interval_start(new)) {
		if (!nft_set_is_anonymous(set))
			return -ENOTEMPTY;

		rbe_prev = nft_rbtree_prev_active(rbe_le);
		if (rbe_prev && nft_rbtree_interval_end(rbe_prev))
			return -ENOTEMPTY;
	}

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

	/* - start element overlaps an open interval but end element is new:
	 *   partial overlap, reported as -ENOEMPTY.
	 */
	if (!rbe_ge && priv->start_rbe_cookie && nft_rbtree_interval_end(new))
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

static int nft_array_intervals_alloc(struct nft_array *array, u32 max_intervals)
{
	struct nft_array_interval *intervals;

	intervals = kvcalloc(max_intervals, sizeof(struct nft_array_interval),
			     GFP_KERNEL_ACCOUNT);
	if (!intervals)
		return -ENOMEM;

	if (array->intervals)
		kvfree(array->intervals);

	array->intervals = intervals;
	array->max_intervals = max_intervals;

	return 0;
}

static struct nft_array *nft_array_alloc(u32 max_intervals)
{
	struct nft_array *array;

	array = kzalloc(sizeof(*array), GFP_KERNEL_ACCOUNT);
	if (!array)
		return NULL;

	if (nft_array_intervals_alloc(array, max_intervals) < 0) {
		kfree(array);
		return NULL;
	}

	return array;
}

#define NFT_ARRAY_EXTRA_SIZE	10240

/* Similar to nft_rbtree_{u,k}size to hide details to userspace, but consider
 * packed representation coming from userspace for anonymous sets too.
 */
static u32 nft_array_elems(const struct nft_set *set)
{
	u32 nelems = atomic_read(&set->nelems);

	/* Adjacent intervals are represented with a single start element in
	 * anonymous sets, use the current element counter as is.
	 */
	if (nft_set_is_anonymous(set))
		return nelems;

	/* Add extra room for never matching interval at the beginning and open
	 * interval at the end which only use a single element to represent it.
	 * The conversion to array will compact intervals, this allows reduce
	 * memory consumption.
	 */
	return (nelems / 2) + 2;
}

static int nft_array_may_resize(const struct nft_set *set)
{
	u32 nelems = nft_array_elems(set), new_max_intervals;
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_array *array;

	if (!priv->array_next) {
		array = nft_array_alloc(nelems + NFT_ARRAY_EXTRA_SIZE);
		if (!array)
			return -ENOMEM;

		priv->array_next = array;
	}

	if (nelems < priv->array_next->max_intervals)
		return 0;

	new_max_intervals = priv->array_next->max_intervals + NFT_ARRAY_EXTRA_SIZE;
	if (nft_array_intervals_alloc(priv->array_next, new_max_intervals) < 0)
		return -ENOMEM;

	return 0;
}

static int nft_rbtree_insert(const struct net *net, const struct nft_set *set,
			     const struct nft_set_elem *elem,
			     struct nft_elem_priv **elem_priv)
{
	struct nft_rbtree_elem *rbe = nft_elem_priv_cast(elem->priv);
	bool last = !!(elem->flags & NFT_SET_ELEM_INTERNAL_LAST);
	struct nft_rbtree *priv = nft_set_priv(set);
	u64 tstamp = nft_net_tstamp(net);
	int err;

	nft_rbtree_maybe_reset_start_cookie(priv, tstamp);

	if (nft_array_may_resize(set) < 0)
		return -ENOMEM;

	do {
		if (fatal_signal_pending(current))
			return -EINTR;

		cond_resched();

		write_lock_bh(&priv->lock);
		err = __nft_rbtree_insert(net, set, rbe, elem_priv, tstamp, last);
		write_unlock_bh(&priv->lock);

		if (nft_rbtree_interval_end(rbe))
			priv->start_rbe_cookie = 0;

	} while (err == -EAGAIN);

	return err;
}

static void nft_rbtree_erase(struct nft_rbtree *priv, struct nft_rbtree_elem *rbe)
{
	write_lock_bh(&priv->lock);
	rb_erase(&rbe->node, &priv->root);
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

static struct nft_rbtree_elem *
nft_rbtree_next_inactive(struct nft_rbtree_elem *rbe, u8 genmask)
{
	struct nft_rbtree_elem *next_rbe;
	struct rb_node *node;

	node = rb_next(&rbe->node);
	if (node) {
		next_rbe = rb_entry(node, struct nft_rbtree_elem, node);
		if (nft_rbtree_interval_start(next_rbe) &&
		    !nft_set_elem_active(&next_rbe->ext, genmask))
			return next_rbe;
	}

	return NULL;
}

static bool nft_rbtree_deactivate_same_interval(const struct net *net,
						struct nft_rbtree *priv,
						struct nft_rbtree_elem *rbe)
{
	u8 genmask = nft_genmask_next(net);
	struct nft_rbtree_elem *next_rbe;

	if (!priv->start_rbe_cookie)
		return true;

	next_rbe = nft_rbtree_next_inactive(rbe, genmask);
	if (next_rbe) {
		/* Closest start element differs from last element added. */
		if (nft_rbtree_interval_start(next_rbe) &&
		    nft_rbtree_cmp_start_cookie(priv, next_rbe)) {
			priv->start_rbe_cookie = 0;
			return true;
		}
	}

	priv->start_rbe_cookie = 0;

	return false;
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
	bool last = !!(elem->flags & NFT_SET_ELEM_INTERNAL_LAST);
	struct nft_rbtree *priv = nft_set_priv(set);
	const struct rb_node *parent = priv->root.rb_node;
	u8 genmask = nft_genmask_next(net);
	u64 tstamp = nft_net_tstamp(net);
	int d;

	nft_rbtree_maybe_reset_start_cookie(priv, tstamp);

	if (nft_rbtree_interval_start(this) ||
	    nft_rbtree_interval_null(set, this))
		priv->start_rbe_cookie = 0;

	if (nft_array_may_resize(set) < 0)
		return NULL;

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

			if (nft_rbtree_interval_start(rbe)) {
				if (!last)
					nft_rbtree_set_start_cookie(priv, rbe);
			} else if (!nft_rbtree_deactivate_same_interval(net, priv, rbe))
				return NULL;

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

		if (nft_array_may_resize(set) < 0) {
			iter->err = -ENOMEM;
			break;
		}
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

static void nft_rbtree_gc_scan(struct nft_set *set)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe, *rbe_end = NULL;
	struct net *net = read_pnet(&set->net);
	u64 tstamp = nft_net_tstamp(net);
	struct rb_node *node, *next;

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

		/* end element needs to be removed first, it has
		 * no timeout extension.
		 */
		write_lock_bh(&priv->lock);
		if (rbe_end) {
			nft_rbtree_gc_elem_move(net, set, priv, rbe_end);
			rbe_end = NULL;
		}

		nft_rbtree_gc_elem_move(net, set, priv, rbe);
		write_unlock_bh(&priv->lock);
	}

	priv->last_gc = jiffies;
}

static void nft_rbtree_gc_queue(struct nft_set *set)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe, *rbe_end;
	struct nft_trans_gc *gc;

	if (list_empty(&priv->expired))
		return;

	gc = nft_trans_gc_alloc(set, 0, GFP_KERNEL);
	if (!gc)
		return;

	list_for_each_entry_safe(rbe, rbe_end, &priv->expired, list) {
		list_del(&rbe->list);
		nft_trans_gc_elem_add(gc, rbe);

		gc = nft_trans_gc_queue_sync(gc, GFP_KERNEL);
		if (!gc)
			return;
	}

	gc = nft_trans_gc_catchall_sync(gc);
	nft_trans_gc_queue_sync_done(gc);
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
	priv->root = RB_ROOT;
	INIT_LIST_HEAD(&priv->expired);

	priv->array = NULL;
	priv->array_next = NULL;

	return 0;
}

static void __nft_array_free(struct nft_array *array)
{
	kvfree(array->intervals);
	kfree(array);
}

static void nft_rbtree_destroy(const struct nft_ctx *ctx,
			       const struct nft_set *set)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe, *next;
	struct nft_array *array;
	struct rb_node *node;

	list_for_each_entry_safe(rbe, next, &priv->expired, list) {
		list_del(&rbe->list);
		nf_tables_set_elem_destroy(ctx, set, &rbe->priv);
	}

	while ((node = priv->root.rb_node) != NULL) {
		rb_erase(node, &priv->root);
		rbe = rb_entry(node, struct nft_rbtree_elem, node);
		nf_tables_set_elem_destroy(ctx, set, &rbe->priv);
	}

	array = rcu_dereference_protected(priv->array, true);
	if (array)
		__nft_array_free(array);
	if (priv->array_next)
		__nft_array_free(priv->array_next);
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

static void nft_array_free_rcu(struct rcu_head *rcu_head)
{
	struct nft_array *array = container_of(rcu_head, struct nft_array, rcu_head);

	__nft_array_free(array);
}

static void nft_rbtree_commit(struct nft_set *set)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_rbtree_elem *rbe, *prev_rbe;
	struct nft_array *old;
	u32 num_intervals = 0;
	struct rb_node *node;

	/* No changes, skip, eg. elements updates only. */
	if (!priv->array_next)
		return;

	/* GC can be performed if the binary search blob is going
	 * to be rebuilt.  It has to be done in two phases: first
	 * scan tree and move all expired elements to the expired
	 * list.
	 *
	 * Then, after blob has been re-built and published to other
	 * CPUs, queue collected entries for freeing.
	 */
	if (time_after_eq(jiffies, priv->last_gc + nft_set_gc_interval(set)))
		nft_rbtree_gc_scan(set);

	/* Reverse walk to create an array from smaller to largest interval. */
	node = rb_last(&priv->root);
	if (node)
		prev_rbe = rb_entry(node, struct nft_rbtree_elem, node);
	else
		prev_rbe = NULL;

	while (prev_rbe) {
		rbe = prev_rbe;

		if (nft_rbtree_interval_start(rbe))
			priv->array_next->intervals[num_intervals].from = &rbe->ext;
		else if (nft_rbtree_interval_end(rbe))
			priv->array_next->intervals[num_intervals++].to = &rbe->ext;

		if (num_intervals >= priv->array_next->max_intervals) {
			pr_warn_once("malformed interval set from userspace?");
			goto err_out;
		}

		node = rb_prev(node);
		if (!node)
			break;

		prev_rbe = rb_entry(node, struct nft_rbtree_elem, node);

		/* For anonymous sets, when adjacent ranges are found,
		 * the end element is not added to the set to pack the set
		 * representation. Use next start element to complete this
		 * interval.
		 */
		if (nft_rbtree_interval_start(rbe) &&
		    nft_rbtree_interval_start(prev_rbe) &&
		    priv->array_next->intervals[num_intervals].from)
			priv->array_next->intervals[num_intervals++].to = &prev_rbe->ext;

		if (num_intervals >= priv->array_next->max_intervals) {
			pr_warn_once("malformed interval set from userspace?");
			goto err_out;
		}
	}

	if (priv->array_next->intervals[num_intervals].from)
		num_intervals++;
err_out:
	priv->array_next->num_intervals = num_intervals;
	old = rcu_replace_pointer(priv->array, priv->array_next,
				  lockdep_is_held(&nft_pernet(read_pnet(&set->net))->commit_mutex));
	priv->array_next = NULL;
	if (old)
		call_rcu(&old->rcu_head, nft_array_free_rcu);

	/* New blob is public, queue collected entries for freeing.
	 * call_rcu ensures elements stay around until readers are done.
	 */
	nft_rbtree_gc_queue(set);
}

static void nft_rbtree_abort(const struct nft_set *set)
{
	struct nft_rbtree *priv = nft_set_priv(set);
	struct nft_array *array_next;

	if (!priv->array_next)
		return;

	array_next = priv->array_next;
	priv->array_next = NULL;
	__nft_array_free(array_next);
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
		.abort		= nft_rbtree_abort,
		.gc_init	= nft_rbtree_gc_init,
		.lookup		= nft_rbtree_lookup,
		.walk		= nft_rbtree_walk,
		.get		= nft_rbtree_get,
		.ksize		= nft_rbtree_ksize,
		.usize		= nft_rbtree_usize,
		.adjust_maxsize = nft_rbtree_adjust_maxsize,
	},
};
