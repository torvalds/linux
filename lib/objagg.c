// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/rhashtable.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/sort.h>
#include <linux/objagg.h>

#define CREATE_TRACE_POINTS
#include <trace/events/objagg.h>

struct objagg_hints {
	struct rhashtable node_ht;
	struct rhashtable_params ht_params;
	struct list_head node_list;
	unsigned int node_count;
	unsigned int root_count;
	unsigned int refcount;
	const struct objagg_ops *ops;
};

struct objagg_hints_node {
	struct rhash_head ht_node; /* member of objagg_hints->node_ht */
	struct list_head list; /* member of objagg_hints->node_list */
	struct objagg_hints_node *parent;
	unsigned int root_id;
	struct objagg_obj_stats_info stats_info;
	unsigned long obj[];
};

static struct objagg_hints_node *
objagg_hints_lookup(struct objagg_hints *objagg_hints, void *obj)
{
	if (!objagg_hints)
		return NULL;
	return rhashtable_lookup_fast(&objagg_hints->node_ht, obj,
				      objagg_hints->ht_params);
}

struct objagg {
	const struct objagg_ops *ops;
	void *priv;
	struct rhashtable obj_ht;
	struct rhashtable_params ht_params;
	struct list_head obj_list;
	unsigned int obj_count;
	struct ida root_ida;
	struct objagg_hints *hints;
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
	unsigned int root_id;
	unsigned int refcount; /* counts number of users of this object
				* including nested objects
				*/
	struct objagg_obj_stats stats;
	unsigned long obj[];
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
				    struct objagg_obj *parent,
				    bool take_parent_ref)
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
	if (take_parent_ref)
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
					       objagg_obj_cur, true);
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

static int objagg_obj_root_id_alloc(struct objagg *objagg,
				    struct objagg_obj *objagg_obj,
				    struct objagg_hints_node *hnode)
{
	unsigned int min, max;
	int root_id;

	/* In case there are no hints available, the root id is invalid. */
	if (!objagg->hints) {
		objagg_obj->root_id = OBJAGG_OBJ_ROOT_ID_INVALID;
		return 0;
	}

	if (hnode) {
		min = hnode->root_id;
		max = hnode->root_id;
	} else {
		/* For objects with no hint, start after the last
		 * hinted root_id.
		 */
		min = objagg->hints->root_count;
		max = ~0;
	}

	root_id = ida_alloc_range(&objagg->root_ida, min, max, GFP_KERNEL);

	if (root_id < 0)
		return root_id;
	objagg_obj->root_id = root_id;
	return 0;
}

static void objagg_obj_root_id_free(struct objagg *objagg,
				    struct objagg_obj *objagg_obj)
{
	if (!objagg->hints)
		return;
	ida_free(&objagg->root_ida, objagg_obj->root_id);
}

static int objagg_obj_root_create(struct objagg *objagg,
				  struct objagg_obj *objagg_obj,
				  struct objagg_hints_node *hnode)
{
	int err;

	err = objagg_obj_root_id_alloc(objagg, objagg_obj, hnode);
	if (err)
		return err;
	objagg_obj->root_priv = objagg->ops->root_create(objagg->priv,
							 objagg_obj->obj,
							 objagg_obj->root_id);
	if (IS_ERR(objagg_obj->root_priv)) {
		err = PTR_ERR(objagg_obj->root_priv);
		goto err_root_create;
	}
	trace_objagg_obj_root_create(objagg, objagg_obj);
	return 0;

err_root_create:
	objagg_obj_root_id_free(objagg, objagg_obj);
	return err;
}

static void objagg_obj_root_destroy(struct objagg *objagg,
				    struct objagg_obj *objagg_obj)
{
	trace_objagg_obj_root_destroy(objagg, objagg_obj);
	objagg->ops->root_destroy(objagg->priv, objagg_obj->root_priv);
	objagg_obj_root_id_free(objagg, objagg_obj);
}

static struct objagg_obj *__objagg_obj_get(struct objagg *objagg, void *obj);

static int objagg_obj_init_with_hints(struct objagg *objagg,
				      struct objagg_obj *objagg_obj,
				      bool *hint_found)
{
	struct objagg_hints_node *hnode;
	struct objagg_obj *parent;
	int err;

	hnode = objagg_hints_lookup(objagg->hints, objagg_obj->obj);
	if (!hnode) {
		*hint_found = false;
		return 0;
	}
	*hint_found = true;

	if (!hnode->parent)
		return objagg_obj_root_create(objagg, objagg_obj, hnode);

	parent = __objagg_obj_get(objagg, hnode->parent->obj);
	if (IS_ERR(parent))
		return PTR_ERR(parent);

	err = objagg_obj_parent_assign(objagg, objagg_obj, parent, false);
	if (err) {
		*hint_found = false;
		err = 0;
		goto err_parent_assign;
	}

	return 0;

err_parent_assign:
	objagg_obj_put(objagg, parent);
	return err;
}

static int objagg_obj_init(struct objagg *objagg,
			   struct objagg_obj *objagg_obj)
{
	bool hint_found;
	int err;

	/* First, try to use hints if they are available and
	 * if they provide result.
	 */
	err = objagg_obj_init_with_hints(objagg, objagg_obj, &hint_found);
	if (err)
		return err;

	if (hint_found)
		return 0;

	/* Try to find if the object can be aggregated under an existing one. */
	err = objagg_obj_parent_lookup_assign(objagg, objagg_obj);
	if (!err)
		return 0;
	/* If aggregation is not possible, make the object a root. */
	return objagg_obj_root_create(objagg, objagg_obj, NULL);
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
 * @ops:		user-specific callbacks
 * @objagg_hints:	hints, can be NULL
 * @priv:		pointer to a private data passed to the ops
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
struct objagg *objagg_create(const struct objagg_ops *ops,
			     struct objagg_hints *objagg_hints, void *priv)
{
	struct objagg *objagg;
	int err;

	if (WARN_ON(!ops || !ops->root_create || !ops->root_destroy ||
		    !ops->delta_check || !ops->delta_create ||
		    !ops->delta_destroy))
		return ERR_PTR(-EINVAL);

	objagg = kzalloc(sizeof(*objagg), GFP_KERNEL);
	if (!objagg)
		return ERR_PTR(-ENOMEM);
	objagg->ops = ops;
	if (objagg_hints) {
		objagg->hints = objagg_hints;
		objagg_hints->refcount++;
	}
	objagg->priv = priv;
	INIT_LIST_HEAD(&objagg->obj_list);

	objagg->ht_params.key_len = ops->obj_size;
	objagg->ht_params.key_offset = offsetof(struct objagg_obj, obj);
	objagg->ht_params.head_offset = offsetof(struct objagg_obj, ht_node);

	err = rhashtable_init(&objagg->obj_ht, &objagg->ht_params);
	if (err)
		goto err_rhashtable_init;

	ida_init(&objagg->root_ida);

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
	ida_destroy(&objagg->root_ida);
	WARN_ON(!list_empty(&objagg->obj_list));
	rhashtable_destroy(&objagg->obj_ht);
	if (objagg->hints)
		objagg_hints_put(objagg->hints);
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
	int i;

	objagg_stats = kzalloc(struct_size(objagg_stats, stats_info,
					   objagg->obj_count), GFP_KERNEL);
	if (!objagg_stats)
		return ERR_PTR(-ENOMEM);

	i = 0;
	list_for_each_entry(objagg_obj, &objagg->obj_list, list) {
		memcpy(&objagg_stats->stats_info[i].stats, &objagg_obj->stats,
		       sizeof(objagg_stats->stats_info[0].stats));
		objagg_stats->stats_info[i].objagg_obj = objagg_obj;
		objagg_stats->stats_info[i].is_root =
					objagg_obj_is_root(objagg_obj);
		if (objagg_stats->stats_info[i].is_root)
			objagg_stats->root_count++;
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
 * objagg_stats_put - puts stats of the objagg instance
 * @objagg_stats:	objagg instance stats
 *
 * Note: all locking must be provided by the caller.
 */
void objagg_stats_put(const struct objagg_stats *objagg_stats)
{
	kfree(objagg_stats);
}
EXPORT_SYMBOL(objagg_stats_put);

static struct objagg_hints_node *
objagg_hints_node_create(struct objagg_hints *objagg_hints,
			 struct objagg_obj *objagg_obj, size_t obj_size,
			 struct objagg_hints_node *parent_hnode)
{
	unsigned int user_count = objagg_obj->stats.user_count;
	struct objagg_hints_node *hnode;
	int err;

	hnode = kzalloc(sizeof(*hnode) + obj_size, GFP_KERNEL);
	if (!hnode)
		return ERR_PTR(-ENOMEM);
	memcpy(hnode->obj, &objagg_obj->obj, obj_size);
	hnode->stats_info.stats.user_count = user_count;
	hnode->stats_info.stats.delta_user_count = user_count;
	if (parent_hnode) {
		parent_hnode->stats_info.stats.delta_user_count += user_count;
	} else {
		hnode->root_id = objagg_hints->root_count++;
		hnode->stats_info.is_root = true;
	}
	hnode->stats_info.objagg_obj = objagg_obj;

	err = rhashtable_insert_fast(&objagg_hints->node_ht, &hnode->ht_node,
				     objagg_hints->ht_params);
	if (err)
		goto err_ht_insert;

	list_add(&hnode->list, &objagg_hints->node_list);
	hnode->parent = parent_hnode;
	objagg_hints->node_count++;

	return hnode;

err_ht_insert:
	kfree(hnode);
	return ERR_PTR(err);
}

static void objagg_hints_flush(struct objagg_hints *objagg_hints)
{
	struct objagg_hints_node *hnode, *tmp;

	list_for_each_entry_safe(hnode, tmp, &objagg_hints->node_list, list) {
		list_del(&hnode->list);
		rhashtable_remove_fast(&objagg_hints->node_ht, &hnode->ht_node,
				       objagg_hints->ht_params);
		kfree(hnode);
	}
}

struct objagg_tmp_node {
	struct objagg_obj *objagg_obj;
	bool crossed_out;
};

struct objagg_tmp_graph {
	struct objagg_tmp_node *nodes;
	unsigned long nodes_count;
	unsigned long *edges;
};

static int objagg_tmp_graph_edge_index(struct objagg_tmp_graph *graph,
				       int parent_index, int index)
{
	return index * graph->nodes_count + parent_index;
}

static void objagg_tmp_graph_edge_set(struct objagg_tmp_graph *graph,
				      int parent_index, int index)
{
	int edge_index = objagg_tmp_graph_edge_index(graph, index,
						     parent_index);

	__set_bit(edge_index, graph->edges);
}

static bool objagg_tmp_graph_is_edge(struct objagg_tmp_graph *graph,
				     int parent_index, int index)
{
	int edge_index = objagg_tmp_graph_edge_index(graph, index,
						     parent_index);

	return test_bit(edge_index, graph->edges);
}

static unsigned int objagg_tmp_graph_node_weight(struct objagg_tmp_graph *graph,
						 unsigned int index)
{
	struct objagg_tmp_node *node = &graph->nodes[index];
	unsigned int weight = node->objagg_obj->stats.user_count;
	int j;

	/* Node weight is sum of node users and all other nodes users
	 * that this node can represent with delta.
	 */

	for (j = 0; j < graph->nodes_count; j++) {
		if (!objagg_tmp_graph_is_edge(graph, index, j))
			continue;
		node = &graph->nodes[j];
		if (node->crossed_out)
			continue;
		weight += node->objagg_obj->stats.user_count;
	}
	return weight;
}

static int objagg_tmp_graph_node_max_weight(struct objagg_tmp_graph *graph)
{
	struct objagg_tmp_node *node;
	unsigned int max_weight = 0;
	unsigned int weight;
	int max_index = -1;
	int i;

	for (i = 0; i < graph->nodes_count; i++) {
		node = &graph->nodes[i];
		if (node->crossed_out)
			continue;
		weight = objagg_tmp_graph_node_weight(graph, i);
		if (weight >= max_weight) {
			max_weight = weight;
			max_index = i;
		}
	}
	return max_index;
}

static struct objagg_tmp_graph *objagg_tmp_graph_create(struct objagg *objagg)
{
	unsigned int nodes_count = objagg->obj_count;
	struct objagg_tmp_graph *graph;
	struct objagg_tmp_node *node;
	struct objagg_tmp_node *pnode;
	struct objagg_obj *objagg_obj;
	int i, j;

	graph = kzalloc(sizeof(*graph), GFP_KERNEL);
	if (!graph)
		return NULL;

	graph->nodes = kcalloc(nodes_count, sizeof(*graph->nodes), GFP_KERNEL);
	if (!graph->nodes)
		goto err_nodes_alloc;
	graph->nodes_count = nodes_count;

	graph->edges = bitmap_zalloc(nodes_count * nodes_count, GFP_KERNEL);
	if (!graph->edges)
		goto err_edges_alloc;

	i = 0;
	list_for_each_entry(objagg_obj, &objagg->obj_list, list) {
		node = &graph->nodes[i++];
		node->objagg_obj = objagg_obj;
	}

	/* Assemble a temporary graph. Insert edge X->Y in case Y can be
	 * in delta of X.
	 */
	for (i = 0; i < nodes_count; i++) {
		for (j = 0; j < nodes_count; j++) {
			if (i == j)
				continue;
			pnode = &graph->nodes[i];
			node = &graph->nodes[j];
			if (objagg->ops->delta_check(objagg->priv,
						     pnode->objagg_obj->obj,
						     node->objagg_obj->obj)) {
				objagg_tmp_graph_edge_set(graph, i, j);

			}
		}
	}
	return graph;

err_edges_alloc:
	kfree(graph->nodes);
err_nodes_alloc:
	kfree(graph);
	return NULL;
}

static void objagg_tmp_graph_destroy(struct objagg_tmp_graph *graph)
{
	bitmap_free(graph->edges);
	kfree(graph->nodes);
	kfree(graph);
}

static int
objagg_opt_simple_greedy_fillup_hints(struct objagg_hints *objagg_hints,
				      struct objagg *objagg)
{
	struct objagg_hints_node *hnode, *parent_hnode;
	struct objagg_tmp_graph *graph;
	struct objagg_tmp_node *node;
	int index;
	int j;
	int err;

	graph = objagg_tmp_graph_create(objagg);
	if (!graph)
		return -ENOMEM;

	/* Find the nodes from the ones that can accommodate most users
	 * and cross them out of the graph. Save them to the hint list.
	 */
	while ((index = objagg_tmp_graph_node_max_weight(graph)) != -1) {
		node = &graph->nodes[index];
		node->crossed_out = true;
		hnode = objagg_hints_node_create(objagg_hints,
						 node->objagg_obj,
						 objagg->ops->obj_size,
						 NULL);
		if (IS_ERR(hnode)) {
			err = PTR_ERR(hnode);
			goto out;
		}
		parent_hnode = hnode;
		for (j = 0; j < graph->nodes_count; j++) {
			if (!objagg_tmp_graph_is_edge(graph, index, j))
				continue;
			node = &graph->nodes[j];
			if (node->crossed_out)
				continue;
			node->crossed_out = true;
			hnode = objagg_hints_node_create(objagg_hints,
							 node->objagg_obj,
							 objagg->ops->obj_size,
							 parent_hnode);
			if (IS_ERR(hnode)) {
				err = PTR_ERR(hnode);
				goto out;
			}
		}
	}

	err = 0;
out:
	objagg_tmp_graph_destroy(graph);
	return err;
}

struct objagg_opt_algo {
	int (*fillup_hints)(struct objagg_hints *objagg_hints,
			    struct objagg *objagg);
};

static const struct objagg_opt_algo objagg_opt_simple_greedy = {
	.fillup_hints = objagg_opt_simple_greedy_fillup_hints,
};


static const struct objagg_opt_algo *objagg_opt_algos[] = {
	[OBJAGG_OPT_ALGO_SIMPLE_GREEDY] = &objagg_opt_simple_greedy,
};

static int objagg_hints_obj_cmp(struct rhashtable_compare_arg *arg,
				const void *obj)
{
	struct rhashtable *ht = arg->ht;
	struct objagg_hints *objagg_hints =
			container_of(ht, struct objagg_hints, node_ht);
	const struct objagg_ops *ops = objagg_hints->ops;
	const char *ptr = obj;

	ptr += ht->p.key_offset;
	return ops->hints_obj_cmp ? ops->hints_obj_cmp(ptr, arg->key) :
				    memcmp(ptr, arg->key, ht->p.key_len);
}

/**
 * objagg_hints_get - obtains hints instance
 * @objagg:		objagg instance
 * @opt_algo_type:	type of hints finding algorithm
 *
 * Note: all locking must be provided by the caller.
 *
 * According to the algo type, the existing objects of objagg instance
 * are going to be went-through to assemble an optimal tree. We call this
 * tree hints. These hints can be later on used for creation of
 * a new objagg instance. There, the future object creations are going
 * to be consulted with these hints in order to find out, where exactly
 * the new object should be put as a root or delta.
 *
 * Returns a pointer to hints instance in case of success,
 * otherwise it returns pointer error using ERR_PTR macro.
 */
struct objagg_hints *objagg_hints_get(struct objagg *objagg,
				      enum objagg_opt_algo_type opt_algo_type)
{
	const struct objagg_opt_algo *algo = objagg_opt_algos[opt_algo_type];
	struct objagg_hints *objagg_hints;
	int err;

	objagg_hints = kzalloc(sizeof(*objagg_hints), GFP_KERNEL);
	if (!objagg_hints)
		return ERR_PTR(-ENOMEM);

	objagg_hints->ops = objagg->ops;
	objagg_hints->refcount = 1;

	INIT_LIST_HEAD(&objagg_hints->node_list);

	objagg_hints->ht_params.key_len = objagg->ops->obj_size;
	objagg_hints->ht_params.key_offset =
				offsetof(struct objagg_hints_node, obj);
	objagg_hints->ht_params.head_offset =
				offsetof(struct objagg_hints_node, ht_node);
	objagg_hints->ht_params.obj_cmpfn = objagg_hints_obj_cmp;

	err = rhashtable_init(&objagg_hints->node_ht, &objagg_hints->ht_params);
	if (err)
		goto err_rhashtable_init;

	err = algo->fillup_hints(objagg_hints, objagg);
	if (err)
		goto err_fillup_hints;

	if (WARN_ON(objagg_hints->node_count != objagg->obj_count)) {
		err = -EINVAL;
		goto err_node_count_check;
	}

	return objagg_hints;

err_node_count_check:
err_fillup_hints:
	objagg_hints_flush(objagg_hints);
	rhashtable_destroy(&objagg_hints->node_ht);
err_rhashtable_init:
	kfree(objagg_hints);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(objagg_hints_get);

/**
 * objagg_hints_put - puts hints instance
 * @objagg_hints:	objagg hints instance
 *
 * Note: all locking must be provided by the caller.
 */
void objagg_hints_put(struct objagg_hints *objagg_hints)
{
	if (--objagg_hints->refcount)
		return;
	objagg_hints_flush(objagg_hints);
	rhashtable_destroy(&objagg_hints->node_ht);
	kfree(objagg_hints);
}
EXPORT_SYMBOL(objagg_hints_put);

/**
 * objagg_hints_stats_get - obtains stats of the hints instance
 * @objagg_hints:	hints instance
 *
 * Note: all locking must be provided by the caller.
 *
 * The returned structure contains statistics of all objects
 * currently in use, ordered by following rules:
 * 1) Root objects are always on lower indexes than the rest.
 * 2) Objects with higher delta user count are always on lower
 *    indexes.
 * 3) In case multiple objects have the same delta user count,
 *    the objects are ordered by user count.
 *
 * Returns a pointer to stats instance in case of success,
 * otherwise it returns pointer error using ERR_PTR macro.
 */
const struct objagg_stats *
objagg_hints_stats_get(struct objagg_hints *objagg_hints)
{
	struct objagg_stats *objagg_stats;
	struct objagg_hints_node *hnode;
	int i;

	objagg_stats = kzalloc(struct_size(objagg_stats, stats_info,
					   objagg_hints->node_count),
			       GFP_KERNEL);
	if (!objagg_stats)
		return ERR_PTR(-ENOMEM);

	i = 0;
	list_for_each_entry(hnode, &objagg_hints->node_list, list) {
		memcpy(&objagg_stats->stats_info[i], &hnode->stats_info,
		       sizeof(objagg_stats->stats_info[0]));
		if (objagg_stats->stats_info[i].is_root)
			objagg_stats->root_count++;
		i++;
	}
	objagg_stats->stats_info_count = i;

	sort(objagg_stats->stats_info, objagg_stats->stats_info_count,
	     sizeof(struct objagg_obj_stats_info),
	     objagg_stats_info_sort_cmp_func, NULL);

	return objagg_stats;
}
EXPORT_SYMBOL(objagg_hints_stats_get);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Object aggregation manager");
