// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/rhashtable.h>
#include <linux/list.h>
#include <linux/sort.h>
#include <linux/objagg.h>

#define CREATE_TRACE_POINTS
#include <trace/events/objagg.h>

struct objagg {
	const struct objagg_ops *ops;
	void *priv;
	struct rhashtable obj_ht;
	struct rhashtable_params ht_params;
	struct list_head obj_list;
	unsigned int obj_count;
};

struct objagg_obj {
	struct rhash_head ht_node; /* member of objagg->obj_ht */
	struct list_head list; /* member of objagg->obj_list */
	struct objagg_obj *parent; /* if the object is nested, this
				    * holds pointer to parent, otherwise NULL
				    */
	union {
		void *delta_priv; /* user delta private */
		void *root_priv; /* user root private */
	};
	unsigned int refcount; /* counts number of users of this object
				* including nested objects
				*/
	struct objagg_obj_stats stats;
	unsigned long obj[0];
};

static unsigned int objagg_obj_ref_inc(struct objagg_obj *objagg_obj)
{
	return ++objagg_obj->refcount;
}

static unsigned int objagg_obj_ref_dec(struct objagg_obj *objagg_obj)
{
	return --objagg_obj->refcount;
}

static void objagg_obj_stats_inc(struct objagg_obj *objagg_obj)
{
	objagg_obj->stats.user_count++;
	objagg_obj->stats.delta_user_count++;
	if (objagg_obj->parent)
		objagg_obj->parent->stats.delta_user_count++;
}

static void objagg_obj_stats_dec(struct objagg_obj *objagg_obj)
{
	objagg_obj->stats.user_count--;
	objagg_obj->stats.delta_user_count--;
	if (objagg_obj->parent)
		objagg_obj->parent->stats.delta_user_count--;
}

static bool objagg_obj_is_root(const struct objagg_obj *objagg_obj)
{
	/* Nesting is not supported, so we can use ->parent
	 * to figure out if the object is root.
	 */
	return !objagg_obj->parent;
}

/**
 * objagg_obj_root_priv - obtains root private for an object
 * @objagg_obj:	objagg object instance
 *
 * Note: all locking must be provided by the caller.
 *
 * Either the object is root itself when the private is returned
 * directly, or the parent is root and its private is returned
 * instead.
 *
 * Returns a user private root pointer.
 */
const void *objagg_obj_root_priv(const struct objagg_obj *objagg_obj)
{
	if (objagg_obj_is_root(objagg_obj))
		return objagg_obj->root_priv;
	WARN_ON(!objagg_obj_is_root(objagg_obj->parent));
	return objagg_obj->parent->root_priv;
}
EXPORT_SYMBOL(objagg_obj_root_priv);

/**
 * objagg_obj_delta_priv - obtains delta private for an object
 * @objagg_obj:	objagg object instance
 *
 * Note: all locking must be provided by the caller.
 *
 * Returns user private delta pointer or NULL in case the passed
 * object is root.
 */
const void *objagg_obj_delta_priv(const struct objagg_obj *objagg_obj)
{
	if (objagg_obj_is_root(objagg_obj))
		return NULL;
	return objagg_obj->delta_priv;
}
EXPORT_SYMBOL(objagg_obj_delta_priv);

/**
 * objagg_obj_raw - obtains object user private pointer
 * @objagg_obj:	objagg object instance
 *
 * Note: all locking must be provided by the caller.
 *
 * Returns user private pointer as was passed to objagg_obj_get() by "obj" arg.
 */
const void *objagg_obj_raw(const struct objagg_obj *objagg_obj)
{
	return objagg_obj->obj;
}
EXPORT_SYMBOL(objagg_obj_raw);

static struct objagg_obj *objagg_obj_lookup(struct objagg *objagg, void *obj)
{
	return rhashtable_lookup_fast(&objagg->obj_ht, obj, objagg->ht_params);
}

static int objagg_obj_parent_assign(struct objagg *objagg,
				    struct objagg_obj *objagg_obj,
				    struct objagg_obj *parent)
{
	void *delta_priv;

	delta_priv = objagg->ops->delta_create(objagg->priv, parent->obj,
					       objagg_obj->obj);
	if (IS_ERR(delta_priv))
		return PTR_ERR(delta_priv);

	/* User returned a delta private, that means that
	 * our object can be aggregated into the parent.
	 */
	objagg_obj->parent = parent;
	objagg_obj->delta_priv = delta_priv;
	objagg_obj_ref_inc(objagg_obj->parent);
	trace_objagg_obj_parent_assign(objagg, objagg_obj,
				       parent,
				       parent->refcount);
	return 0;
}

static int objagg_obj_parent_lookup_assign(struct objagg *objagg,
					   struct objagg_obj *objagg_obj)
{
	struct objagg_obj *objagg_obj_cur;
	int err;

	list_for_each_entry(objagg_obj_cur, &objagg->obj_list, list) {
		/* Nesting is not supported. In case the object
		 * is not root, it cannot be assigned as parent.
		 */
		if (!objagg_obj_is_root(objagg_obj_cur))
			continue;
		err = objagg_obj_parent_assign(objagg, objagg_obj,
					       objagg_obj_cur);
		if (!err)
			return 0;
	}
	return -ENOENT;
}

static void __objagg_obj_put(struct objagg *objagg,
			     struct objagg_obj *objagg_obj);

static void objagg_obj_parent_unassign(struct objagg *objagg,
				       struct objagg_obj *objagg_obj)
{
	trace_objagg_obj_parent_unassign(objagg, objagg_obj,
					 objagg_obj->parent,
					 objagg_obj->parent->refcount);
	objagg->ops->delta_destroy(objagg->priv, objagg_obj->delta_priv);
	__objagg_obj_put(objagg, objagg_obj->parent);
}

static int objagg_obj_root_create(struct objagg *objagg,
				  struct objagg_obj *objagg_obj)
{
	objagg_obj->root_priv = objagg->ops->root_create(objagg->priv,
							 objagg_obj->obj);
	if (IS_ERR(objagg_obj->root_priv))
		return PTR_ERR(objagg_obj->root_priv);

	trace_objagg_obj_root_create(objagg, objagg_obj);
	return 0;
}

static void objagg_obj_root_destroy(struct objagg *objagg,
				    struct objagg_obj *objagg_obj)
{
	trace_objagg_obj_root_destroy(objagg, objagg_obj);
	objagg->ops->root_destroy(objagg->priv, objagg_obj->root_priv);
}

static int objagg_obj_init(struct objagg *objagg,
			   struct objagg_obj *objagg_obj)
{
	int err;

	/* Try to find if the object can be aggregated under an existing one. */
	err = objagg_obj_parent_lookup_assign(objagg, objagg_obj);
	if (!err)
		return 0;
	/* If aggregation is not possible, make the object a root. */
	return objagg_obj_root_create(objagg, objagg_obj);
}

static void objagg_obj_fini(struct objagg *objagg,
			    struct objagg_obj *objagg_obj)
{
	if (!objagg_obj_is_root(objagg_obj))
		objagg_obj_parent_unassign(objagg, objagg_obj);
	else
		objagg_obj_root_destroy(objagg, objagg_obj);
}

static struct objagg_obj *objagg_obj_create(struct objagg *objagg, void *obj)
{
	struct objagg_obj *objagg_obj;
	int err;

	objagg_obj = kzalloc(sizeof(*objagg_obj) + objagg->ops->obj_size,
			     GFP_KERNEL);
	if (!objagg_obj)
		return ERR_PTR(-ENOMEM);
	objagg_obj_ref_inc(objagg_obj);
	memcpy(objagg_obj->obj, obj, objagg->ops->obj_size);

	err = objagg_obj_init(objagg, objagg_obj);
	if (err)
		goto err_obj_init;

	err = rhashtable_insert_fast(&objagg->obj_ht, &objagg_obj->ht_node,
				     objagg->ht_params);
	if (err)
		goto err_ht_insert;
	list_add(&objagg_obj->list, &objagg->obj_list);
	objagg->obj_count++;
	trace_objagg_obj_create(objagg, objagg_obj);

	return objagg_obj;

err_ht_insert:
	objagg_obj_fini(objagg, objagg_obj);
err_obj_init:
	kfree(objagg_obj);
	return ERR_PTR(err);
}

static struct objagg_obj *__objagg_obj_get(struct objagg *objagg, void *obj)
{
	struct objagg_obj *objagg_obj;

	/* First, try to find the object exactly as user passed it,
	 * perhaps it is already in use.
	 */
	objagg_obj = objagg_obj_lookup(objagg, obj);
	if (objagg_obj) {
		objagg_obj_ref_inc(objagg_obj);
		return objagg_obj;
	}

	return objagg_obj_create(objagg, obj);
}

/**
 * objagg_obj_get - gets an object within objagg instance
 * @objagg:	objagg instance
 * @obj:	user-specific private object pointer
 *
 * Note: all locking must be provided by the caller.
 *
 * Size of the "obj" memory is specified in "objagg->ops".
 *
 * There are 3 main options this function wraps:
 * 1) The object according to "obj" already exist. In that case
 *    the reference counter is incrementes and the object is returned.
 * 2) The object does not exist, but it can be aggregated within
 *    another object. In that case, user ops->delta_create() is called
 *    to obtain delta data and a new object is created with returned
 *    user-delta private pointer.
 * 3) The object does not exist and cannot be aggregated into
 *    any of the existing objects. In that case, user ops->root_create()
 *    is called to create the root and a new object is created with
 *    returned user-root private pointer.
 *
 * Returns a pointer to objagg object instance in case of success,
 * otherwise it returns pointer error using ERR_PTR macro.
 */
struct objagg_obj *objagg_obj_get(struct objagg *objagg, void *obj)
{
	struct objagg_obj *objagg_obj;

	objagg_obj = __objagg_obj_get(objagg, obj);
	if (IS_ERR(objagg_obj))
		return objagg_obj;
	objagg_obj_stats_inc(objagg_obj);
	trace_objagg_obj_get(objagg, objagg_obj, objagg_obj->refcount);
	return objagg_obj;
}
EXPORT_SYMBOL(objagg_obj_get);

static void objagg_obj_destroy(struct objagg *objagg,
			       struct objagg_obj *objagg_obj)
{
	trace_objagg_obj_destroy(objagg, objagg_obj);
	--objagg->obj_count;
	list_del(&objagg_obj->list);
	rhashtable_remove_fast(&objagg->obj_ht, &objagg_obj->ht_node,
			       objagg->ht_params);
	objagg_obj_fini(objagg, objagg_obj);
	kfree(objagg_obj);
}

static void __objagg_obj_put(struct objagg *objagg,
			     struct objagg_obj *objagg_obj)
{
	if (!objagg_obj_ref_dec(objagg_obj))
		objagg_obj_destroy(objagg, objagg_obj);
}

/**
 * objagg_obj_put - puts an object within objagg instance
 * @objagg:	objagg instance
 * @objagg_obj:	objagg object instance
 *
 * Note: all locking must be provided by the caller.
 *
 * Symmetric to objagg_obj_get().
 */
void objagg_obj_put(struct objagg *objagg, struct objagg_obj *objagg_obj)
{
	trace_objagg_obj_put(objagg, objagg_obj, objagg_obj->refcount);
	objagg_obj_stats_dec(objagg_obj);
	__objagg_obj_put(objagg, objagg_obj);
}
EXPORT_SYMBOL(objagg_obj_put);

/**
 * objagg_create - creates a new objagg instance
 * @ops:	user-specific callbacks
 * @priv:	pointer to a private data passed to the ops
 *
 * Note: all locking must be provided by the caller.
 *
 * The purpose of the library is to provide an infrastructure to
 * aggregate user-specified objects. Library does not care about the type
 * of the object. User fills-up ops which take care of the specific
 * user object manipulation.
 *
 * As a very stupid example, consider integer numbers. For example
 * number 8 as a root object. That can aggregate number 9 with delta 1,
 * number 10 with delta 2, etc. This example is implemented as
 * a part of a testing module in test_objagg.c file.
 *
 * Each objagg instance contains multiple trees. Each tree node is
 * represented by "an object". In the current implementation there can be
 * only roots and leafs nodes. Leaf nodes are called deltas.
 * But in general, this can be easily extended for intermediate nodes.
 * In that extension, a delta would be associated with all non-root
 * nodes.
 *
 * Returns a pointer to newly created objagg instance in case of success,
 * otherwise it returns pointer error using ERR_PTR macro.
 */
struct objagg *objagg_create(const struct objagg_ops *ops, void *priv)
{
	struct objagg *objagg;
	int err;

	if (WARN_ON(!ops || !ops->root_create || !ops->root_destroy ||
		    !ops->delta_create || !ops->delta_destroy))
		return ERR_PTR(-EINVAL);
	objagg = kzalloc(sizeof(*objagg), GFP_KERNEL);
	if (!objagg)
		return ERR_PTR(-ENOMEM);
	objagg->ops = ops;
	objagg->priv = priv;
	INIT_LIST_HEAD(&objagg->obj_list);

	objagg->ht_params.key_len = ops->obj_size;
	objagg->ht_params.key_offset = offsetof(struct objagg_obj, obj);
	objagg->ht_params.head_offset = offsetof(struct objagg_obj, ht_node);

	err = rhashtable_init(&objagg->obj_ht, &objagg->ht_params);
	if (err)
		goto err_rhashtable_init;

	trace_objagg_create(objagg);
	return objagg;

err_rhashtable_init:
	kfree(objagg);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(objagg_create);

/**
 * objagg_destroy - destroys a new objagg instance
 * @objagg:	objagg instance
 *
 * Note: all locking must be provided by the caller.
 */
void objagg_destroy(struct objagg *objagg)
{
	trace_objagg_destroy(objagg);
	WARN_ON(!list_empty(&objagg->obj_list));
	rhashtable_destroy(&objagg->obj_ht);
	kfree(objagg);
}
EXPORT_SYMBOL(objagg_destroy);

static int objagg_stats_info_sort_cmp_func(const void *a, const void *b)
{
	const struct objagg_obj_stats_info *stats_info1 = a;
	const struct objagg_obj_stats_info *stats_info2 = b;

	if (stats_info1->is_root != stats_info2->is_root)
		return stats_info2->is_root - stats_info1->is_root;
	if (stats_info1->stats.delta_user_count !=
	    stats_info2->stats.delta_user_count)
		return stats_info2->stats.delta_user_count -
		       stats_info1->stats.delta_user_count;
	return stats_info2->stats.user_count - stats_info1->stats.user_count;
}

/**
 * objagg_stats_get - obtains stats of the objagg instance
 * @objagg:	objagg instance
 *
 * Note: all locking must be provided by the caller.
 *
 * The returned structure contains statistics of all object
 * currently in use, ordered by following rules:
 * 1) Root objects are always on lower indexes than the rest.
 * 2) Objects with higher delta user count are always on lower
 *    indexes.
 * 3) In case more objects have the same delta user count,
 *    the objects are ordered by user count.
 *
 * Returns a pointer to stats instance in case of success,
 * otherwise it returns pointer error using ERR_PTR macro.
 */
const struct objagg_stats *objagg_stats_get(struct objagg *objagg)
{
	struct objagg_stats *objagg_stats;
	struct objagg_obj *objagg_obj;
	size_t alloc_size;
	int i;

	alloc_size = sizeof(*objagg_stats) +
		     sizeof(objagg_stats->stats_info[0]) * objagg->obj_count;
	objagg_stats = kzalloc(alloc_size, GFP_KERNEL);
	if (!objagg_stats)
		return ERR_PTR(-ENOMEM);

	i = 0;
	list_for_each_entry(objagg_obj, &objagg->obj_list, list) {
		memcpy(&objagg_stats->stats_info[i].stats, &objagg_obj->stats,
		       sizeof(objagg_stats->stats_info[0].stats));
		objagg_stats->stats_info[i].objagg_obj = objagg_obj;
		objagg_stats->stats_info[i].is_root =
					objagg_obj_is_root(objagg_obj);
		i++;
	}
	objagg_stats->stats_info_count = i;

	sort(objagg_stats->stats_info, objagg_stats->stats_info_count,
	     sizeof(struct objagg_obj_stats_info),
	     objagg_stats_info_sort_cmp_func, NULL);

	return objagg_stats;
}
EXPORT_SYMBOL(objagg_stats_get);

/**
 * objagg_stats_puts - puts stats of the objagg instance
 * @objagg_stats:	objagg instance stats
 *
 * Note: all locking must be provided by the caller.
 */
void objagg_stats_put(const struct objagg_stats *objagg_stats)
{
	kfree(objagg_stats);
}
EXPORT_SYMBOL(objagg_stats_put);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Object aggregation manager");
