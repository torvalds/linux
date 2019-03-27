/*-
 * Copyright (c) 2013-2017, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <linux/module.h>
#include <dev/mlx5/driver.h>
#include "mlx5_core.h"
#include "fs_core.h"
#include <linux/string.h>
#include <linux/compiler.h>

#define INIT_TREE_NODE_ARRAY_SIZE(...)	(sizeof((struct init_tree_node[]){__VA_ARGS__}) /\
					 sizeof(struct init_tree_node))

#define ADD_PRIO(name_val, flags_val, min_level_val, max_ft_val, caps_val, \
		 ...) {.type = FS_TYPE_PRIO,\
	.name = name_val,\
	.min_ft_level = min_level_val,\
	.flags = flags_val,\
	.max_ft = max_ft_val,\
	.caps = caps_val,\
	.children = (struct init_tree_node[]) {__VA_ARGS__},\
	.ar_size = INIT_TREE_NODE_ARRAY_SIZE(__VA_ARGS__) \
}

#define ADD_FT_PRIO(name_val, flags_val, max_ft_val,  ...)\
	ADD_PRIO(name_val, flags_val, 0, max_ft_val, {},\
		 __VA_ARGS__)\

#define ADD_NS(name_val, ...) {.type = FS_TYPE_NAMESPACE,\
	.name = name_val,\
	.children = (struct init_tree_node[]) {__VA_ARGS__},\
	.ar_size = INIT_TREE_NODE_ARRAY_SIZE(__VA_ARGS__) \
}

#define INIT_CAPS_ARRAY_SIZE(...) (sizeof((long[]){__VA_ARGS__}) /\
				   sizeof(long))

#define FS_CAP(cap) (__mlx5_bit_off(flow_table_nic_cap, cap))

#define FS_REQUIRED_CAPS(...) {.arr_sz = INIT_CAPS_ARRAY_SIZE(__VA_ARGS__), \
			       .caps = (long[]) {__VA_ARGS__}}

#define BYPASS_MAX_FT 5
#define BYPASS_PRIO_MAX_FT 1
#define KERNEL_MAX_FT 3
#define LEFTOVER_MAX_FT 1
#define KENREL_MIN_LEVEL 3
#define LEFTOVER_MIN_LEVEL KENREL_MIN_LEVEL + 1
#define BYPASS_MIN_LEVEL MLX5_NUM_BYPASS_FTS + LEFTOVER_MIN_LEVEL
struct node_caps {
	size_t	arr_sz;
	long	*caps;
};

struct init_tree_node {
	enum fs_type	type;
	const char	*name;
	struct init_tree_node *children;
	int ar_size;
	struct node_caps caps;
	u8  flags;
	int min_ft_level;
	int prio;
	int max_ft;
} root_fs = {
	.type = FS_TYPE_NAMESPACE,
	.name = "root",
	.ar_size = 3,
	.children = (struct init_tree_node[]) {
		ADD_PRIO("by_pass_prio", 0, BYPASS_MIN_LEVEL, 0,
			 FS_REQUIRED_CAPS(FS_CAP(flow_table_properties_nic_receive.flow_modify_en),
					  FS_CAP(flow_table_properties_nic_receive.modify_root)),
			 ADD_NS("by_pass_ns",
				ADD_FT_PRIO("prio0", 0,
					    BYPASS_PRIO_MAX_FT),
				ADD_FT_PRIO("prio1", 0,
					    BYPASS_PRIO_MAX_FT),
				ADD_FT_PRIO("prio2", 0,
					    BYPASS_PRIO_MAX_FT),
				ADD_FT_PRIO("prio3", 0,
					    BYPASS_PRIO_MAX_FT),
				ADD_FT_PRIO("prio4", 0,
					    BYPASS_PRIO_MAX_FT),
				ADD_FT_PRIO("prio5", 0,
					    BYPASS_PRIO_MAX_FT),
				ADD_FT_PRIO("prio6", 0,
					    BYPASS_PRIO_MAX_FT),
				ADD_FT_PRIO("prio7", 0,
					    BYPASS_PRIO_MAX_FT),
				ADD_FT_PRIO("prio-mcast", 0,
					    BYPASS_PRIO_MAX_FT))),
		ADD_PRIO("kernel_prio", 0, KENREL_MIN_LEVEL, 0, {},
			 ADD_NS("kernel_ns",
				ADD_FT_PRIO("prio_kernel-0", 0,
					    KERNEL_MAX_FT))),
		ADD_PRIO("leftovers_prio", MLX5_CORE_FS_PRIO_SHARED,
			 LEFTOVER_MIN_LEVEL, 0,
			 FS_REQUIRED_CAPS(FS_CAP(flow_table_properties_nic_receive.flow_modify_en),
					  FS_CAP(flow_table_properties_nic_receive.modify_root)),
			 ADD_NS("leftover_ns",
				ADD_FT_PRIO("leftovers_prio-0",
					MLX5_CORE_FS_PRIO_SHARED,
					LEFTOVER_MAX_FT)))
	}
};

/* Tree creation functions */

static struct mlx5_flow_root_namespace *find_root(struct fs_base *node)
{
	struct fs_base *parent;

	/* Make sure we only read it once while we go up the tree */
	while ((parent = node->parent))
		node = parent;

	if (node->type != FS_TYPE_NAMESPACE) {
		printf("mlx5_core: WARN: ""mlx5: flow steering node %s is not in tree or garbaged\n", node->name);
		return NULL;
	}

	return container_of(container_of(node,
					 struct mlx5_flow_namespace,
					 base),
			    struct mlx5_flow_root_namespace,
			    ns);
}

static inline struct mlx5_core_dev *fs_get_dev(struct fs_base *node)
{
	struct mlx5_flow_root_namespace *root = find_root(node);

	if (root)
		return root->dev;
	return NULL;
}

static void fs_init_node(struct fs_base *node,
			 unsigned int refcount)
{
	kref_init(&node->refcount);
	atomic_set(&node->users_refcount, refcount);
	init_completion(&node->complete);
	INIT_LIST_HEAD(&node->list);
	mutex_init(&node->lock);
}

static void _fs_add_node(struct fs_base *node,
			 const char *name,
			 struct fs_base *parent)
{
	if (parent)
		atomic_inc(&parent->users_refcount);
	node->name = kstrdup_const(name, GFP_KERNEL);
	node->parent = parent;
}

static void fs_add_node(struct fs_base *node,
			struct fs_base *parent, const char *name,
			unsigned int refcount)
{
	fs_init_node(node, refcount);
	_fs_add_node(node, name, parent);
}

static void _fs_put(struct fs_base *node, void (*kref_cb)(struct kref *kref),
		    bool parent_locked);

static void fs_del_dst(struct mlx5_flow_rule *dst);
static void _fs_del_ft(struct mlx5_flow_table *ft);
static void fs_del_fg(struct mlx5_flow_group *fg);
static void fs_del_fte(struct fs_fte *fte);

static void cmd_remove_node(struct fs_base *base)
{
	switch (base->type) {
	case FS_TYPE_FLOW_DEST:
		fs_del_dst(container_of(base, struct mlx5_flow_rule, base));
		break;
	case FS_TYPE_FLOW_TABLE:
		_fs_del_ft(container_of(base, struct mlx5_flow_table, base));
		break;
	case FS_TYPE_FLOW_GROUP:
		fs_del_fg(container_of(base, struct mlx5_flow_group, base));
		break;
	case FS_TYPE_FLOW_ENTRY:
		fs_del_fte(container_of(base, struct fs_fte, base));
		break;
	default:
		break;
	}
}

static void __fs_remove_node(struct kref *kref)
{
	struct fs_base *node = container_of(kref, struct fs_base, refcount);

	if (node->parent)
		mutex_lock(&node->parent->lock);
	mutex_lock(&node->lock);
	cmd_remove_node(node);
	mutex_unlock(&node->lock);
	complete(&node->complete);
	if (node->parent) {
		mutex_unlock(&node->parent->lock);
		_fs_put(node->parent, _fs_remove_node, false);
	}
}

void _fs_remove_node(struct kref *kref)
{
	struct fs_base *node = container_of(kref, struct fs_base, refcount);

	__fs_remove_node(kref);
	kfree_const(node->name);
	kfree(node);
}

static void fs_get(struct fs_base *node)
{
	atomic_inc(&node->users_refcount);
}

static void _fs_put(struct fs_base *node, void (*kref_cb)(struct kref *kref),
		    bool parent_locked)
{
	struct fs_base *parent_node = node->parent;

	if (parent_node && !parent_locked)
		mutex_lock(&parent_node->lock);
	if (atomic_dec_and_test(&node->users_refcount)) {
		if (parent_node) {
			/*remove from parent's list*/
			list_del_init(&node->list);
			mutex_unlock(&parent_node->lock);
		}
		kref_put(&node->refcount, kref_cb);
		if (parent_node && parent_locked)
			mutex_lock(&parent_node->lock);
	} else if (parent_node && !parent_locked) {
		mutex_unlock(&parent_node->lock);
	}
}

static void fs_put(struct fs_base *node)
{
	_fs_put(node, __fs_remove_node, false);
}

static void fs_put_parent_locked(struct fs_base *node)
{
	_fs_put(node, __fs_remove_node, true);
}

static void fs_remove_node(struct fs_base *node)
{
	fs_put(node);
	wait_for_completion(&node->complete);
	kfree_const(node->name);
	kfree(node);
}

static void fs_remove_node_parent_locked(struct fs_base *node)
{
	fs_put_parent_locked(node);
	wait_for_completion(&node->complete);
	kfree_const(node->name);
	kfree(node);
}

static struct fs_fte *fs_alloc_fte(u8 action,
				   u32 flow_tag,
				   u32 *match_value,
				   unsigned int index)
{
	struct fs_fte *fte;


	fte = kzalloc(sizeof(*fte), GFP_KERNEL);
	if (!fte)
		return ERR_PTR(-ENOMEM);

	memcpy(fte->val, match_value, sizeof(fte->val));
	fte->base.type =  FS_TYPE_FLOW_ENTRY;
	fte->dests_size = 0;
	fte->flow_tag = flow_tag;
	fte->index = index;
	INIT_LIST_HEAD(&fte->dests);
	fte->action = action;

	return fte;
}

static struct fs_fte *alloc_star_ft_entry(struct mlx5_flow_table *ft,
					  struct mlx5_flow_group *fg,
					  u32 *match_value,
					  unsigned int index)
{
	int err;
	struct fs_fte *fte;
	struct mlx5_flow_rule *dst;

	if (fg->num_ftes == fg->max_ftes)
		return ERR_PTR(-ENOSPC);

	fte = fs_alloc_fte(MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
			   MLX5_FS_DEFAULT_FLOW_TAG, match_value, index);
	if (IS_ERR(fte))
		return fte;

	/*create dst*/
	dst = kzalloc(sizeof(*dst), GFP_KERNEL);
	if (!dst) {
		err = -ENOMEM;
		goto free_fte;
	}

	fte->base.parent = &fg->base;
	fte->dests_size = 1;
	dst->dest_attr.type = MLX5_FLOW_CONTEXT_DEST_TYPE_FLOW_TABLE;
	dst->base.parent = &fte->base;
	list_add(&dst->base.list, &fte->dests);
	/* assumed that the callee creates the star rules sorted by index */
	list_add_tail(&fte->base.list, &fg->ftes);
	fg->num_ftes++;

	return fte;

free_fte:
	kfree(fte);
	return ERR_PTR(err);
}

/* assume that fte can't be changed */
static void free_star_fte_entry(struct fs_fte *fte)
{
	struct mlx5_flow_group	*fg;
	struct mlx5_flow_rule	*dst, *temp;

	fs_get_parent(fg, fte);

	list_for_each_entry_safe(dst, temp, &fte->dests, base.list) {
		fte->dests_size--;
		list_del(&dst->base.list);
		kfree(dst);
	}

	list_del(&fte->base.list);
	fg->num_ftes--;
	kfree(fte);
}

static struct mlx5_flow_group *fs_alloc_fg(u32 *create_fg_in)
{
	struct mlx5_flow_group *fg;
	void *match_criteria = MLX5_ADDR_OF(create_flow_group_in,
					    create_fg_in, match_criteria);
	u8 match_criteria_enable = MLX5_GET(create_flow_group_in,
					    create_fg_in,
					    match_criteria_enable);
	fg = kzalloc(sizeof(*fg), GFP_KERNEL);
	if (!fg)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&fg->ftes);
	fg->mask.match_criteria_enable = match_criteria_enable;
	memcpy(&fg->mask.match_criteria, match_criteria,
	       sizeof(fg->mask.match_criteria));
	fg->base.type =  FS_TYPE_FLOW_GROUP;
	fg->start_index = MLX5_GET(create_flow_group_in, create_fg_in,
				   start_flow_index);
	fg->max_ftes = MLX5_GET(create_flow_group_in, create_fg_in,
				end_flow_index) - fg->start_index + 1;
	return fg;
}

static struct mlx5_flow_table *find_next_ft(struct fs_prio *prio);
static struct mlx5_flow_table *find_prev_ft(struct mlx5_flow_table *curr,
					    struct fs_prio *prio);

/* assumed src_ft and dst_ft can't be freed */
static int fs_set_star_rule(struct mlx5_core_dev *dev,
			    struct mlx5_flow_table *src_ft,
			    struct mlx5_flow_table *dst_ft)
{
	struct mlx5_flow_rule *src_dst;
	struct fs_fte *src_fte;
	int err = 0;
	u32 *match_value;
	int match_len = MLX5_ST_SZ_BYTES(fte_match_param);

	src_dst = list_first_entry(&src_ft->star_rule.fte->dests,
				   struct mlx5_flow_rule, base.list);
	match_value = mlx5_vzalloc(match_len);
	if (!match_value) {
		mlx5_core_warn(dev, "failed to allocate inbox\n");
		return -ENOMEM;
	}
	/*Create match context*/

	fs_get_parent(src_fte, src_dst);

	src_dst->dest_attr.ft = dst_ft;
	if (dst_ft) {
		err = mlx5_cmd_fs_set_fte(dev,
					  src_ft->vport,
					  &src_fte->status,
					  match_value, src_ft->type,
					  src_ft->id, src_fte->index,
					  src_ft->star_rule.fg->id,
					  src_fte->flow_tag,
					  src_fte->action,
					  src_fte->dests_size,
					  &src_fte->dests);
		if (err)
			goto free;

		fs_get(&dst_ft->base);
	} else {
		mlx5_cmd_fs_delete_fte(dev,
				       src_ft->vport,
				       &src_fte->status,
				       src_ft->type, src_ft->id,
				       src_fte->index);
	}

free:
	kvfree(match_value);
	return err;
}

static int connect_prev_fts(struct fs_prio *locked_prio,
			    struct fs_prio *prev_prio,
			    struct mlx5_flow_table *next_ft)
{
	struct mlx5_flow_table *iter;
	int err = 0;
	struct mlx5_core_dev *dev = fs_get_dev(&prev_prio->base);

	if (!dev)
		return -ENODEV;

	mutex_lock(&prev_prio->base.lock);
	fs_for_each_ft(iter, prev_prio) {
		struct mlx5_flow_rule *src_dst =
			list_first_entry(&iter->star_rule.fte->dests,
					 struct mlx5_flow_rule, base.list);
		struct mlx5_flow_table *prev_ft = src_dst->dest_attr.ft;

		if (prev_ft == next_ft)
			continue;

		err = fs_set_star_rule(dev, iter, next_ft);
		if (err) {
			mlx5_core_warn(dev,
				       "mlx5: flow steering can't connect prev and next\n");
			goto unlock;
		} else {
			/* Assume ft's prio is locked */
			if (prev_ft) {
				struct fs_prio *prio;

				fs_get_parent(prio, prev_ft);
				if (prio == locked_prio)
					fs_put_parent_locked(&prev_ft->base);
				else
					fs_put(&prev_ft->base);
			}
		}
	}

unlock:
	mutex_unlock(&prev_prio->base.lock);
	return 0;
}

static int create_star_rule(struct mlx5_flow_table *ft, struct fs_prio *prio)
{
	struct mlx5_flow_group *fg;
	int err;
	u32 *fg_in;
	u32 *match_value;
	struct mlx5_flow_table *next_ft;
	struct mlx5_flow_table *prev_ft;
	struct mlx5_flow_root_namespace *root = find_root(&prio->base);
	int fg_inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	int match_len = MLX5_ST_SZ_BYTES(fte_match_param);

	fg_in = mlx5_vzalloc(fg_inlen);
	if (!fg_in) {
		mlx5_core_warn(root->dev, "failed to allocate inbox\n");
		return -ENOMEM;
	}

	match_value = mlx5_vzalloc(match_len);
	if (!match_value) {
		mlx5_core_warn(root->dev, "failed to allocate inbox\n");
		kvfree(fg_in);
		return -ENOMEM;
	}

	MLX5_SET(create_flow_group_in, fg_in, start_flow_index, ft->max_fte);
	MLX5_SET(create_flow_group_in, fg_in, end_flow_index, ft->max_fte);
	fg = fs_alloc_fg(fg_in);
	if (IS_ERR(fg)) {
		err = PTR_ERR(fg);
		goto out;
	}
	ft->star_rule.fg = fg;
	err =  mlx5_cmd_fs_create_fg(fs_get_dev(&prio->base),
				     fg_in, ft->vport, ft->type,
				     ft->id,
				     &fg->id);
	if (err)
		goto free_fg;

	ft->star_rule.fte = alloc_star_ft_entry(ft, fg,
						      match_value,
						      ft->max_fte);
	if (IS_ERR(ft->star_rule.fte))
		goto free_star_rule;

	mutex_lock(&root->fs_chain_lock);
	next_ft = find_next_ft(prio);
	err = fs_set_star_rule(root->dev, ft, next_ft);
	if (err) {
		mutex_unlock(&root->fs_chain_lock);
		goto free_star_rule;
	}
	if (next_ft) {
		struct fs_prio *parent;

		fs_get_parent(parent, next_ft);
		fs_put(&next_ft->base);
	}
	prev_ft = find_prev_ft(ft, prio);
	if (prev_ft) {
		struct fs_prio *prev_parent;

		fs_get_parent(prev_parent, prev_ft);

		err = connect_prev_fts(NULL, prev_parent, ft);
		if (err) {
			mutex_unlock(&root->fs_chain_lock);
			goto destroy_chained_star_rule;
		}
		fs_put(&prev_ft->base);
	}
	mutex_unlock(&root->fs_chain_lock);
	kvfree(fg_in);
	kvfree(match_value);

	return 0;

destroy_chained_star_rule:
	fs_set_star_rule(fs_get_dev(&prio->base), ft, NULL);
	if (next_ft)
		fs_put(&next_ft->base);
free_star_rule:
	free_star_fte_entry(ft->star_rule.fte);
	mlx5_cmd_fs_destroy_fg(fs_get_dev(&ft->base), ft->vport,
			       ft->type, ft->id,
			       fg->id);
free_fg:
	kfree(fg);
out:
	kvfree(fg_in);
	kvfree(match_value);
	return err;
}

static void destroy_star_rule(struct mlx5_flow_table *ft, struct fs_prio *prio)
{
	int err;
	struct mlx5_flow_root_namespace *root;
	struct mlx5_core_dev *dev = fs_get_dev(&prio->base);
	struct mlx5_flow_table *prev_ft, *next_ft;
	struct fs_prio *prev_prio;

	WARN_ON(!dev);

	root = find_root(&prio->base);
	if (!root)
		printf("mlx5_core: ERR: ""mlx5: flow steering failed to find root of priority %s", prio->base.name);

	/* In order to ensure atomic deletion, first update
	 * prev ft to point on the next ft.
	 */
	mutex_lock(&root->fs_chain_lock);
	prev_ft = find_prev_ft(ft, prio);
	next_ft = find_next_ft(prio);
	if (prev_ft) {
		fs_get_parent(prev_prio, prev_ft);
		/*Prev is connected to ft, only if ft is the first(last) in the prio*/
		err = connect_prev_fts(prio, prev_prio, next_ft);
		if (err)
			mlx5_core_warn(root->dev,
				       "flow steering can't connect prev and next of flow table\n");
		fs_put(&prev_ft->base);
	}

	err = fs_set_star_rule(root->dev, ft, NULL);
	/*One put is for fs_get in find next ft*/
	if (next_ft) {
		fs_put(&next_ft->base);
		if (!err)
			fs_put(&next_ft->base);
	}

	mutex_unlock(&root->fs_chain_lock);
	err = mlx5_cmd_fs_destroy_fg(dev, ft->vport, ft->type, ft->id,
				     ft->star_rule.fg->id);
	if (err)
		mlx5_core_warn(dev,
			       "flow steering can't destroy star entry group(index:%d) of ft:%s\n", ft->star_rule.fg->start_index,
			       ft->base.name);
	free_star_fte_entry(ft->star_rule.fte);

	kfree(ft->star_rule.fg);
	ft->star_rule.fg = NULL;
}

static struct fs_prio *find_prio(struct mlx5_flow_namespace *ns,
				 unsigned int prio)
{
	struct fs_prio *iter_prio;

	fs_for_each_prio(iter_prio, ns) {
		if (iter_prio->prio == prio)
			return iter_prio;
	}

	return NULL;
}

static unsigned int _alloc_new_level(struct fs_prio *prio,
				     struct mlx5_flow_namespace *match);

static unsigned int __alloc_new_level(struct mlx5_flow_namespace *ns,
				      struct fs_prio *prio)
{
	unsigned int level = 0;
	struct fs_prio *p;

	if (!ns)
		return 0;

	mutex_lock(&ns->base.lock);
	fs_for_each_prio(p, ns) {
		if (p != prio)
			level += p->max_ft;
		else
			break;
	}
	mutex_unlock(&ns->base.lock);

	fs_get_parent(prio, ns);
	if (prio)
		WARN_ON(prio->base.type != FS_TYPE_PRIO);

	return level + _alloc_new_level(prio, ns);
}

/* Called under lock of priority, hence locking all upper objects */
static unsigned int _alloc_new_level(struct fs_prio *prio,
				     struct mlx5_flow_namespace *match)
{
	struct mlx5_flow_namespace *ns;
	struct fs_base *it;
	unsigned int level = 0;

	if (!prio)
		return 0;

	mutex_lock(&prio->base.lock);
	fs_for_each_ns_or_ft_reverse(it, prio) {
		if (it->type == FS_TYPE_NAMESPACE) {
			struct fs_prio *p;

			fs_get_obj(ns, it);

			if (match != ns) {
				mutex_lock(&ns->base.lock);
				fs_for_each_prio(p, ns)
					level += p->max_ft;
				mutex_unlock(&ns->base.lock);
			} else {
				break;
			}
		} else {
			struct mlx5_flow_table *ft;

			fs_get_obj(ft, it);
			mutex_unlock(&prio->base.lock);
			return level + ft->level + 1;
		}
	}

	fs_get_parent(ns, prio);
	mutex_unlock(&prio->base.lock);
	return __alloc_new_level(ns, prio) + level;
}

static unsigned int alloc_new_level(struct fs_prio *prio)
{
	return _alloc_new_level(prio, NULL);
}

static int update_root_ft_create(struct mlx5_flow_root_namespace *root,
				    struct mlx5_flow_table *ft)
{
	int err = 0;
	int min_level = INT_MAX;

	if (root->root_ft)
		min_level = root->root_ft->level;

	if (ft->level < min_level)
		err = mlx5_cmd_update_root_ft(root->dev, ft->type,
					      ft->id);
	else
		return err;

	if (err)
		mlx5_core_warn(root->dev, "Update root flow table of id=%u failed\n",
			       ft->id);
	else
		root->root_ft = ft;

	return err;
}

static struct mlx5_flow_table *_create_ft_common(struct mlx5_flow_namespace *ns,
						 u16 vport,
						 struct fs_prio *fs_prio,
						 int max_fte,
						 const char *name)
{
	struct mlx5_flow_table *ft;
	int err;
	int log_table_sz;
	int ft_size;
	char gen_name[20];
	struct mlx5_flow_root_namespace *root =
		find_root(&ns->base);

	if (!root) {
		printf("mlx5_core: ERR: ""mlx5: flow steering failed to find root of namespace %s", ns->base.name);
		return ERR_PTR(-ENODEV);
	}

	if (fs_prio->num_ft == fs_prio->max_ft)
		return ERR_PTR(-ENOSPC);

	ft  = kzalloc(sizeof(*ft), GFP_KERNEL);
	if (!ft)
		return ERR_PTR(-ENOMEM);

	fs_init_node(&ft->base, 1);
	INIT_LIST_HEAD(&ft->fgs);

	/* Temporarily WA until we expose the level set in the API */
	if (root->table_type == FS_FT_ESW_EGRESS_ACL ||
		root->table_type == FS_FT_ESW_INGRESS_ACL)
		ft->level = 0;
	else
		ft->level = alloc_new_level(fs_prio);

	ft->base.type = FS_TYPE_FLOW_TABLE;
	ft->vport = vport;
	ft->type = root->table_type;
	/*Two entries are reserved for star rules*/
	ft_size = roundup_pow_of_two(max_fte + 2);
	/*User isn't aware to those rules*/
	ft->max_fte = ft_size - 2;
	log_table_sz = ilog2(ft_size);
	err = mlx5_cmd_fs_create_ft(root->dev, ft->vport, ft->type,
				    ft->level, log_table_sz, &ft->id);
	if (err)
		goto free_ft;

	err = create_star_rule(ft, fs_prio);
	if (err)
		goto del_ft;

	if ((root->table_type == FS_FT_NIC_RX) && MLX5_CAP_FLOWTABLE(root->dev,
			       flow_table_properties_nic_receive.modify_root)) {
		err = update_root_ft_create(root, ft);
		if (err)
			goto destroy_star_rule;
	}

	if (!name || !strlen(name)) {
		snprintf(gen_name, 20, "flow_table_%u", ft->id);
		_fs_add_node(&ft->base, gen_name, &fs_prio->base);
	} else {
		_fs_add_node(&ft->base, name, &fs_prio->base);
	}
	list_add_tail(&ft->base.list, &fs_prio->objs);
	fs_prio->num_ft++;

	return ft;

destroy_star_rule:
	destroy_star_rule(ft, fs_prio);
del_ft:
	mlx5_cmd_fs_destroy_ft(root->dev, ft->vport, ft->type, ft->id);
free_ft:
	kfree(ft);
	return ERR_PTR(err);
}

static struct mlx5_flow_table *create_ft_common(struct mlx5_flow_namespace *ns,
						u16 vport,
						unsigned int prio,
						int max_fte,
						const char *name)
{
	struct fs_prio *fs_prio = NULL;
	fs_prio = find_prio(ns, prio);
	if (!fs_prio)
		return ERR_PTR(-EINVAL);

	return _create_ft_common(ns, vport, fs_prio, max_fte, name);
}


static struct mlx5_flow_table *find_first_ft_in_ns(struct mlx5_flow_namespace *ns,
						   struct list_head *start);

static struct mlx5_flow_table *find_first_ft_in_prio(struct fs_prio *prio,
						     struct list_head *start);

static struct mlx5_flow_table *mlx5_create_autogrouped_shared_flow_table(struct fs_prio *fs_prio)
{
	struct mlx5_flow_table *ft;

	ft = find_first_ft_in_prio(fs_prio, &fs_prio->objs);
	if (ft) {
		ft->shared_refcount++;
		return ft;
	}

	return NULL;
}

struct mlx5_flow_table *mlx5_create_auto_grouped_flow_table(struct mlx5_flow_namespace *ns,
							   int prio,
							   const char *name,
							   int num_flow_table_entries,
							   int max_num_groups)
{
	struct mlx5_flow_table *ft = NULL;
	struct fs_prio *fs_prio;
	bool is_shared_prio;

	fs_prio = find_prio(ns, prio);
	if (!fs_prio)
		return ERR_PTR(-EINVAL);

	is_shared_prio = fs_prio->flags & MLX5_CORE_FS_PRIO_SHARED;
	if (is_shared_prio) {
		mutex_lock(&fs_prio->shared_lock);
		ft = mlx5_create_autogrouped_shared_flow_table(fs_prio);
	}

	if (ft)
		goto return_ft;

	ft = create_ft_common(ns, 0, prio, num_flow_table_entries,
			      name);
	if (IS_ERR(ft))
		goto return_ft;

	ft->autogroup.active = true;
	ft->autogroup.max_types = max_num_groups;
	if (is_shared_prio)
		ft->shared_refcount = 1;

return_ft:
	if (is_shared_prio)
		mutex_unlock(&fs_prio->shared_lock);
	return ft;
}
EXPORT_SYMBOL(mlx5_create_auto_grouped_flow_table);

struct mlx5_flow_table *mlx5_create_vport_flow_table(struct mlx5_flow_namespace *ns,
						     u16 vport,
						     int prio,
						     const char *name,
						     int num_flow_table_entries)
{
	return create_ft_common(ns, vport, prio, num_flow_table_entries, name);
}
EXPORT_SYMBOL(mlx5_create_vport_flow_table);

struct mlx5_flow_table *mlx5_create_flow_table(struct mlx5_flow_namespace *ns,
					       int prio,
					       const char *name,
					       int num_flow_table_entries)
{
	return create_ft_common(ns, 0, prio, num_flow_table_entries, name);
}
EXPORT_SYMBOL(mlx5_create_flow_table);

static void _fs_del_ft(struct mlx5_flow_table *ft)
{
	int err;
	struct mlx5_core_dev *dev = fs_get_dev(&ft->base);
	struct fs_prio *prio;

	err = mlx5_cmd_fs_destroy_ft(dev, ft->vport, ft->type, ft->id);
	if (err)
		mlx5_core_warn(dev, "flow steering can't destroy ft %s\n",
			       ft->base.name);

	fs_get_parent(prio, ft);
	prio->num_ft--;
}

static int update_root_ft_destroy(struct mlx5_flow_root_namespace *root,
				    struct mlx5_flow_table *ft)
{
	int err = 0;
	struct fs_prio *prio;
	struct mlx5_flow_table *next_ft = NULL;
	struct mlx5_flow_table *put_ft = NULL;

	if (root->root_ft != ft)
		return 0;

	fs_get_parent(prio, ft);
	/*Assuming objs containis only flow tables and
	 * flow tables are sorted by level.
	 */
	if (!list_is_last(&ft->base.list, &prio->objs)) {
		next_ft = list_next_entry(ft, base.list);
	} else {
		next_ft = find_next_ft(prio);
		put_ft = next_ft;
	}

	if (next_ft) {
		err = mlx5_cmd_update_root_ft(root->dev, next_ft->type,
					      next_ft->id);
		if (err)
			mlx5_core_warn(root->dev, "Update root flow table of id=%u failed\n",
				       ft->id);
	}
	if (!err)
		root->root_ft = next_ft;

	if (put_ft)
		fs_put(&put_ft->base);

	return err;
}

/*Objects in the same prio are destroyed in the reverse order they were createrd*/
int mlx5_destroy_flow_table(struct mlx5_flow_table *ft)
{
	int err = 0;
	struct fs_prio *prio;
	struct mlx5_flow_root_namespace *root;
	bool is_shared_prio;

	fs_get_parent(prio, ft);
	root = find_root(&prio->base);

	if (!root) {
		printf("mlx5_core: ERR: ""mlx5: flow steering failed to find root of priority %s", prio->base.name);
		return -ENODEV;
	}

	is_shared_prio = prio->flags & MLX5_CORE_FS_PRIO_SHARED;
	if (is_shared_prio) {
		mutex_lock(&prio->shared_lock);
		if (ft->shared_refcount > 1) {
			--ft->shared_refcount;
			fs_put(&ft->base);
			mutex_unlock(&prio->shared_lock);
			return 0;
		}
	}

	mutex_lock(&prio->base.lock);
	mutex_lock(&ft->base.lock);

	err = update_root_ft_destroy(root, ft);
	if (err)
		goto unlock_ft;

	/* delete two last entries */
	destroy_star_rule(ft, prio);

	mutex_unlock(&ft->base.lock);
	fs_remove_node_parent_locked(&ft->base);
	mutex_unlock(&prio->base.lock);
	if (is_shared_prio)
		mutex_unlock(&prio->shared_lock);

	return err;

unlock_ft:
	mutex_unlock(&ft->base.lock);
	mutex_unlock(&prio->base.lock);
	if (is_shared_prio)
		mutex_unlock(&prio->shared_lock);

	return err;
}
EXPORT_SYMBOL(mlx5_destroy_flow_table);

static struct mlx5_flow_group *fs_create_fg(struct mlx5_core_dev *dev,
					    struct mlx5_flow_table *ft,
					    struct list_head *prev,
					    u32 *fg_in,
					    int refcount)
{
	struct mlx5_flow_group *fg;
	int err;
	unsigned int end_index;
	char name[20];

	fg = fs_alloc_fg(fg_in);
	if (IS_ERR(fg))
		return fg;

	end_index = fg->start_index + fg->max_ftes - 1;
	err =  mlx5_cmd_fs_create_fg(dev, fg_in,
				     ft->vport, ft->type, ft->id,
				     &fg->id);
	if (err)
		goto free_fg;

	mutex_lock(&ft->base.lock);
	if (ft->autogroup.active)
		ft->autogroup.num_types++;

	snprintf(name, sizeof(name), "group_%u", fg->id);
	/*Add node to tree*/
	fs_add_node(&fg->base, &ft->base, name, refcount);
	/*Add node to group list*/
	list_add(&fg->base.list, prev);
	mutex_unlock(&ft->base.lock);

	return fg;

free_fg:
	kfree(fg);
	return ERR_PTR(err);
}

struct mlx5_flow_group *mlx5_create_flow_group(struct mlx5_flow_table *ft,
					       u32 *in)
{
	struct mlx5_flow_group *fg;
	struct mlx5_core_dev *dev = fs_get_dev(&ft->base);

	if (!dev)
		return ERR_PTR(-ENODEV);

	if (ft->autogroup.active)
		return ERR_PTR(-EPERM);

	fg = fs_create_fg(dev, ft, ft->fgs.prev, in, 1);

	return fg;
}
EXPORT_SYMBOL(mlx5_create_flow_group);

/*Group is destoyed when all the rules in the group were removed*/
static void fs_del_fg(struct mlx5_flow_group *fg)
{
	struct mlx5_flow_table *parent_ft;
	struct mlx5_core_dev *dev;

	fs_get_parent(parent_ft, fg);
	dev = fs_get_dev(&parent_ft->base);
	WARN_ON(!dev);

	if (parent_ft->autogroup.active)
		parent_ft->autogroup.num_types--;

	if (mlx5_cmd_fs_destroy_fg(dev, parent_ft->vport,
				   parent_ft->type,
				   parent_ft->id, fg->id))
		mlx5_core_warn(dev, "flow steering can't destroy fg\n");
}

void mlx5_destroy_flow_group(struct mlx5_flow_group *fg)
{
	fs_remove_node(&fg->base);
}
EXPORT_SYMBOL(mlx5_destroy_flow_group);

static bool _fs_match_exact_val(void *mask, void *val1, void *val2, size_t size)
{
	unsigned int i;

	/* TODO: optimize by comparing 64bits when possible */
	for (i = 0; i < size; i++, mask++, val1++, val2++)
		if ((*((u8 *)val1) & (*(u8 *)mask)) !=
		    ((*(u8 *)val2) & (*(u8 *)mask)))
			return false;

	return true;
}

bool fs_match_exact_val(struct mlx5_core_fs_mask *mask,
			       void *val1, void *val2)
{
	if (mask->match_criteria_enable &
	    1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_OUTER_HEADERS) {
		void *fte_match1 = MLX5_ADDR_OF(fte_match_param,
						val1, outer_headers);
		void *fte_match2 = MLX5_ADDR_OF(fte_match_param,
						val2, outer_headers);
		void *fte_mask = MLX5_ADDR_OF(fte_match_param,
					      mask->match_criteria, outer_headers);

		if (!_fs_match_exact_val(fte_mask, fte_match1, fte_match2,
					 MLX5_ST_SZ_BYTES(fte_match_set_lyr_2_4)))
			return false;
	}

	if (mask->match_criteria_enable &
	    1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS) {
		void *fte_match1 = MLX5_ADDR_OF(fte_match_param,
						val1, misc_parameters);
		void *fte_match2 = MLX5_ADDR_OF(fte_match_param,
						val2, misc_parameters);
		void *fte_mask = MLX5_ADDR_OF(fte_match_param,
					  mask->match_criteria, misc_parameters);

		if (!_fs_match_exact_val(fte_mask, fte_match1, fte_match2,
					 MLX5_ST_SZ_BYTES(fte_match_set_misc)))
			return false;
	}
	if (mask->match_criteria_enable &
	    1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_INNER_HEADERS) {
		void *fte_match1 = MLX5_ADDR_OF(fte_match_param,
						val1, inner_headers);
		void *fte_match2 = MLX5_ADDR_OF(fte_match_param,
						val2, inner_headers);
		void *fte_mask = MLX5_ADDR_OF(fte_match_param,
					  mask->match_criteria, inner_headers);

		if (!_fs_match_exact_val(fte_mask, fte_match1, fte_match2,
					 MLX5_ST_SZ_BYTES(fte_match_set_lyr_2_4)))
			return false;
	}
	return true;
}

bool fs_match_exact_mask(u8 match_criteria_enable1,
				u8 match_criteria_enable2,
				void *mask1, void *mask2)
{
	return match_criteria_enable1 == match_criteria_enable2 &&
		!memcmp(mask1, mask2, MLX5_ST_SZ_BYTES(fte_match_param));
}

static struct mlx5_flow_table *find_first_ft_in_ns_reverse(struct mlx5_flow_namespace *ns,
							   struct list_head *start);

static struct mlx5_flow_table *_find_first_ft_in_prio_reverse(struct fs_prio *prio,
							      struct list_head *start)
{
	struct fs_base *it = container_of(start, struct fs_base, list);

	if (!prio)
		return NULL;

	fs_for_each_ns_or_ft_continue_reverse(it, prio) {
		struct mlx5_flow_namespace	*ns;
		struct mlx5_flow_table		*ft;

		if (it->type == FS_TYPE_FLOW_TABLE) {
			fs_get_obj(ft, it);
			fs_get(&ft->base);
			return ft;
		}

		fs_get_obj(ns, it);
		WARN_ON(ns->base.type != FS_TYPE_NAMESPACE);

		ft = find_first_ft_in_ns_reverse(ns, &ns->prios);
		if (ft)
			return ft;
	}

	return NULL;
}

static struct mlx5_flow_table *find_first_ft_in_prio_reverse(struct fs_prio *prio,
							     struct list_head *start)
{
	struct mlx5_flow_table *ft;

	if (!prio)
		return NULL;

	mutex_lock(&prio->base.lock);
	ft = _find_first_ft_in_prio_reverse(prio, start);
	mutex_unlock(&prio->base.lock);

	return ft;
}

static struct mlx5_flow_table *find_first_ft_in_ns_reverse(struct mlx5_flow_namespace *ns,
							   struct list_head *start)
{
	struct fs_prio *prio;

	if (!ns)
		return NULL;

	fs_get_obj(prio, container_of(start, struct fs_base, list));
	mutex_lock(&ns->base.lock);
	fs_for_each_prio_continue_reverse(prio, ns) {
		struct mlx5_flow_table *ft;

		ft = find_first_ft_in_prio_reverse(prio, &prio->objs);
		if (ft) {
			mutex_unlock(&ns->base.lock);
			return ft;
		}
	}
	mutex_unlock(&ns->base.lock);

	return NULL;
}

/* Returned a held ft, assumed curr is protected, assumed curr's parent is
 * locked
 */
static struct mlx5_flow_table *find_prev_ft(struct mlx5_flow_table *curr,
					    struct fs_prio *prio)
{
	struct mlx5_flow_table *ft = NULL;
	struct fs_base *curr_base;

	if (!curr)
		return NULL;

	/* prio has either namespace or flow-tables, but not both */
	if (!list_empty(&prio->objs) &&
	    list_first_entry(&prio->objs, struct mlx5_flow_table, base.list) !=
	    curr)
		return NULL;

	while (!ft && prio) {
		struct mlx5_flow_namespace *ns;

		fs_get_parent(ns, prio);
		ft = find_first_ft_in_ns_reverse(ns, &prio->base.list);
		curr_base = &ns->base;
		fs_get_parent(prio, ns);

		if (prio && !ft)
			ft = find_first_ft_in_prio_reverse(prio,
							   &curr_base->list);
	}
	return ft;
}

static struct mlx5_flow_table *_find_first_ft_in_prio(struct fs_prio *prio,
						      struct list_head *start)
{
	struct fs_base	*it = container_of(start, struct fs_base, list);

	if (!prio)
		return NULL;

	fs_for_each_ns_or_ft_continue(it, prio) {
		struct mlx5_flow_namespace	*ns;
		struct mlx5_flow_table		*ft;

		if (it->type == FS_TYPE_FLOW_TABLE) {
			fs_get_obj(ft, it);
			fs_get(&ft->base);
			return ft;
		}

		fs_get_obj(ns, it);
		WARN_ON(ns->base.type != FS_TYPE_NAMESPACE);

		ft = find_first_ft_in_ns(ns, &ns->prios);
		if (ft)
			return ft;
	}

	return NULL;
}

static struct mlx5_flow_table *find_first_ft_in_prio(struct fs_prio *prio,
						     struct list_head *start)
{
	struct mlx5_flow_table *ft;

	if (!prio)
		return NULL;

	mutex_lock(&prio->base.lock);
	ft = _find_first_ft_in_prio(prio, start);
	mutex_unlock(&prio->base.lock);

	return ft;
}

static struct mlx5_flow_table *find_first_ft_in_ns(struct mlx5_flow_namespace *ns,
						   struct list_head *start)
{
	struct fs_prio *prio;

	if (!ns)
		return NULL;

	fs_get_obj(prio, container_of(start, struct fs_base, list));
	mutex_lock(&ns->base.lock);
	fs_for_each_prio_continue(prio, ns) {
		struct mlx5_flow_table *ft;

		ft = find_first_ft_in_prio(prio, &prio->objs);
		if (ft) {
			mutex_unlock(&ns->base.lock);
			return ft;
		}
	}
	mutex_unlock(&ns->base.lock);

	return NULL;
}

/* returned a held ft, assumed curr is protected, assumed curr's parent is
 * locked
 */
static struct mlx5_flow_table *find_next_ft(struct fs_prio *prio)
{
	struct mlx5_flow_table *ft = NULL;
	struct fs_base *curr_base;

	while (!ft && prio) {
		struct mlx5_flow_namespace *ns;

		fs_get_parent(ns, prio);
		ft = find_first_ft_in_ns(ns, &prio->base.list);
		curr_base = &ns->base;
		fs_get_parent(prio, ns);

		if (!ft && prio)
			ft = _find_first_ft_in_prio(prio, &curr_base->list);
	}
	return ft;
}


/* called under ft mutex lock */
static struct mlx5_flow_group *create_autogroup(struct mlx5_flow_table *ft,
						u8 match_criteria_enable,
						u32 *match_criteria)
{
	unsigned int group_size;
	unsigned int candidate_index = 0;
	unsigned int candidate_group_num = 0;
	struct mlx5_flow_group *g;
	struct mlx5_flow_group *ret;
	struct list_head *prev = &ft->fgs;
	struct mlx5_core_dev *dev;
	u32 *in;
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	void *match_criteria_addr;

	if (!ft->autogroup.active)
		return ERR_PTR(-ENOENT);

	dev = fs_get_dev(&ft->base);
	if (!dev)
		return ERR_PTR(-ENODEV);

	in = mlx5_vzalloc(inlen);
	if (!in) {
		mlx5_core_warn(dev, "failed to allocate inbox\n");
		return ERR_PTR(-ENOMEM);
	}


	if (ft->autogroup.num_types < ft->autogroup.max_types)
		group_size = ft->max_fte / (ft->autogroup.max_types + 1);
	else
		group_size = 1;

	if (group_size == 0) {
		mlx5_core_warn(dev,
			       "flow steering can't create group size of 0\n");
		ret = ERR_PTR(-EINVAL);
		goto out;
	}

	/* sorted by start_index */
	fs_for_each_fg(g, ft) {
		candidate_group_num++;
		if (candidate_index + group_size > g->start_index)
			candidate_index = g->start_index + g->max_ftes;
		else
			break;
		prev = &g->base.list;
	}

	if (candidate_index + group_size > ft->max_fte) {
		ret = ERR_PTR(-ENOSPC);
		goto out;
	}

	MLX5_SET(create_flow_group_in, in, match_criteria_enable,
		 match_criteria_enable);
	MLX5_SET(create_flow_group_in, in, start_flow_index, candidate_index);
	MLX5_SET(create_flow_group_in, in, end_flow_index,   candidate_index +
		 group_size - 1);
	match_criteria_addr = MLX5_ADDR_OF(create_flow_group_in,
					   in, match_criteria);
	memcpy(match_criteria_addr, match_criteria,
	       MLX5_ST_SZ_BYTES(fte_match_param));

	ret = fs_create_fg(dev, ft, prev, in, 0);
out:
	kvfree(in);
	return ret;
}

static struct mlx5_flow_namespace *get_ns_with_notifiers(struct fs_base *node)
{
	struct mlx5_flow_namespace *ns = NULL;

	while (node  && (node->type != FS_TYPE_NAMESPACE ||
			      list_empty(&container_of(node, struct
						       mlx5_flow_namespace,
						       base)->list_notifiers)))
		node = node->parent;

	if (node)
		fs_get_obj(ns, node);

	return ns;
}


/*Assumption- fte is locked*/
static void call_to_add_rule_notifiers(struct mlx5_flow_rule *dst,
				      struct fs_fte *fte)
{
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_handler *iter_handler;
	struct fs_client_priv_data *iter_client;
	void *data;
	bool is_new_rule = list_first_entry(&fte->dests,
					    struct mlx5_flow_rule,
					    base.list) == dst;
	int err;

	ns = get_ns_with_notifiers(&fte->base);
	if (!ns)
		return;

	down_read(&ns->notifiers_rw_sem);
	list_for_each_entry(iter_handler, &ns->list_notifiers,
			    list) {
		if (iter_handler->add_dst_cb) {
			data = NULL;
			mutex_lock(&dst->clients_lock);
			list_for_each_entry(
				iter_client, &dst->clients_data, list) {
				if (iter_client->fs_handler == iter_handler) {
					data = iter_client->client_dst_data;
					break;
				}
			}
			mutex_unlock(&dst->clients_lock);
			err  = iter_handler->add_dst_cb(dst,
							is_new_rule,
							NULL,
							iter_handler->client_context);
			if (err)
				break;
		}
	}
	up_read(&ns->notifiers_rw_sem);
}

static void call_to_del_rule_notifiers(struct mlx5_flow_rule *dst,
				      struct fs_fte *fte)
{
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_handler *iter_handler;
	struct fs_client_priv_data *iter_client;
	void *data;
	bool ctx_changed = (fte->dests_size == 0);

	ns = get_ns_with_notifiers(&fte->base);
	if (!ns)
		return;
	down_read(&ns->notifiers_rw_sem);
	list_for_each_entry(iter_handler, &ns->list_notifiers,
			    list) {
		data = NULL;
		mutex_lock(&dst->clients_lock);
		list_for_each_entry(iter_client, &dst->clients_data, list) {
			if (iter_client->fs_handler == iter_handler) {
				data = iter_client->client_dst_data;
				break;
			}
		}
		mutex_unlock(&dst->clients_lock);
		if (iter_handler->del_dst_cb) {
			iter_handler->del_dst_cb(dst, ctx_changed, data,
						 iter_handler->client_context);
		}
	}
	up_read(&ns->notifiers_rw_sem);
}

/* fte should not be deleted while calling this function */
static struct mlx5_flow_rule *_fs_add_dst_fte(struct fs_fte *fte,
					      struct mlx5_flow_group *fg,
					      struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_table *ft;
	struct mlx5_flow_rule *dst;
	int err;

	dst = kzalloc(sizeof(*dst), GFP_KERNEL);
	if (!dst)
		return ERR_PTR(-ENOMEM);

	memcpy(&dst->dest_attr, dest, sizeof(*dest));
	dst->base.type = FS_TYPE_FLOW_DEST;
	INIT_LIST_HEAD(&dst->clients_data);
	mutex_init(&dst->clients_lock);
	fs_get_parent(ft, fg);
	/*Add dest to dests list- added as first element after the head*/
	list_add_tail(&dst->base.list, &fte->dests);
	fte->dests_size++;
	err = mlx5_cmd_fs_set_fte(fs_get_dev(&ft->base),
				  ft->vport,
				  &fte->status,
				  fte->val, ft->type,
				  ft->id, fte->index, fg->id, fte->flow_tag,
				  fte->action, fte->dests_size, &fte->dests);
	if (err)
		goto free_dst;

	list_del(&dst->base.list);

	return dst;

free_dst:
	list_del(&dst->base.list);
	kfree(dst);
	fte->dests_size--;
	return ERR_PTR(err);
}

static char *get_dest_name(struct mlx5_flow_destination *dest)
{
	char *name = kzalloc(sizeof(char) * 20, GFP_KERNEL);

	switch (dest->type) {
	case MLX5_FLOW_CONTEXT_DEST_TYPE_FLOW_TABLE:
		snprintf(name, 20, "dest_%s_%u", "flow_table",
			 dest->ft->id);
		return name;
	case MLX5_FLOW_CONTEXT_DEST_TYPE_VPORT:
		snprintf(name, 20, "dest_%s_%u", "vport",
			 dest->vport_num);
		return name;
	case MLX5_FLOW_CONTEXT_DEST_TYPE_TIR:
		snprintf(name, 20, "dest_%s_%u", "tir", dest->tir_num);
		return name;
	default:
		kfree(name);
		return NULL;
	}
}

/* assumed fg is locked */
static unsigned int fs_get_free_fg_index(struct mlx5_flow_group *fg,
					 struct list_head **prev)
{
	struct fs_fte *fte;
	unsigned int start = fg->start_index;

	if (prev)
		*prev = &fg->ftes;

	/* assumed list is sorted by index */
	fs_for_each_fte(fte, fg) {
		if (fte->index != start)
			return start;
		start++;
		if (prev)
			*prev = &fte->base.list;
	}

	return start;
}


static struct fs_fte *fs_create_fte(struct mlx5_flow_group *fg,
			     u32 *match_value,
			     u8 action,
			     u32 flow_tag,
			     struct list_head **prev)
{
	struct fs_fte *fte;
	int index = 0;

	index = fs_get_free_fg_index(fg, prev);
	fte = fs_alloc_fte(action, flow_tag, match_value, index);
	if (IS_ERR(fte))
		return fte;

	return fte;
}

static void add_rule_to_tree(struct mlx5_flow_rule *rule,
			     struct fs_fte *fte)
{
	char *dest_name;

	dest_name = get_dest_name(&rule->dest_attr);
	fs_add_node(&rule->base, &fte->base, dest_name, 1);
	/* re-add to list, since fs_add_node reset our list */
	list_add_tail(&rule->base.list, &fte->dests);
	kfree(dest_name);
	call_to_add_rule_notifiers(rule, fte);
}

static void fs_del_dst(struct mlx5_flow_rule *dst)
{
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct fs_fte *fte;
	u32	*match_value;
	struct mlx5_core_dev *dev = fs_get_dev(&dst->base);
	int match_len = MLX5_ST_SZ_BYTES(fte_match_param);
	int err;

	WARN_ON(!dev);

	match_value = mlx5_vzalloc(match_len);
	if (!match_value) {
		mlx5_core_warn(dev, "failed to allocate inbox\n");
		return;
	}

	fs_get_parent(fte, dst);
	fs_get_parent(fg, fte);
	mutex_lock(&fg->base.lock);
	memcpy(match_value, fte->val, sizeof(fte->val));
	/* ft can't be changed as fg is locked */
	fs_get_parent(ft, fg);
	list_del(&dst->base.list);
	fte->dests_size--;
	if (fte->dests_size) {
		err = mlx5_cmd_fs_set_fte(dev, ft->vport,
					  &fte->status, match_value, ft->type,
					  ft->id, fte->index, fg->id,
					  fte->flow_tag, fte->action,
					  fte->dests_size, &fte->dests);
		if (err) {
			mlx5_core_warn(dev, "%s can't delete dst %s\n",
				       __func__, dst->base.name);
			goto err;
		}
	}
	call_to_del_rule_notifiers(dst, fte);
err:
	mutex_unlock(&fg->base.lock);
	kvfree(match_value);
}

static void fs_del_fte(struct fs_fte *fte)
{
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	int err;
	struct mlx5_core_dev *dev;

	fs_get_parent(fg, fte);
	fs_get_parent(ft, fg);

	dev = fs_get_dev(&ft->base);
	WARN_ON(!dev);

	err = mlx5_cmd_fs_delete_fte(dev, ft->vport, &fte->status,
				     ft->type, ft->id, fte->index);
	if (err)
		mlx5_core_warn(dev, "flow steering can't delete fte %s\n",
			       fte->base.name);

	fg->num_ftes--;
}

/* assuming parent fg is locked */
/* Add dst algorithm */
static struct mlx5_flow_rule *fs_add_dst_fg(struct mlx5_flow_group *fg,
						   u32 *match_value,
						   u8 action,
						   u32 flow_tag,
						   struct mlx5_flow_destination *dest)
{
	struct fs_fte *fte;
	struct mlx5_flow_rule *dst;
	struct mlx5_flow_table *ft;
	struct list_head *prev;
	char fte_name[20];

	mutex_lock(&fg->base.lock);
	fs_for_each_fte(fte, fg) {
		/* TODO: Check of size against PRM max size */
		mutex_lock(&fte->base.lock);
		if (fs_match_exact_val(&fg->mask, match_value, &fte->val) &&
		    action == fte->action && flow_tag == fte->flow_tag) {
			dst = _fs_add_dst_fte(fte, fg, dest);
			mutex_unlock(&fte->base.lock);
			if (IS_ERR(dst))
				goto unlock_fg;
			goto add_rule;
		}
		mutex_unlock(&fte->base.lock);
	}

	fs_get_parent(ft, fg);
	if (fg->num_ftes == fg->max_ftes) {
		dst = ERR_PTR(-ENOSPC);
		goto unlock_fg;
	}

	fte = fs_create_fte(fg, match_value, action, flow_tag, &prev);
	if (IS_ERR(fte)) {
		dst = (void *)fte;
		goto unlock_fg;
	}
	dst = _fs_add_dst_fte(fte, fg, dest);
	if (IS_ERR(dst)) {
		kfree(fte);
		goto unlock_fg;
	}

	fg->num_ftes++;

	snprintf(fte_name, sizeof(fte_name), "fte%u", fte->index);
	/* Add node to tree */
	fs_add_node(&fte->base, &fg->base, fte_name, 0);
	list_add(&fte->base.list, prev);
add_rule:
	add_rule_to_tree(dst, fte);
unlock_fg:
	mutex_unlock(&fg->base.lock);
	return dst;
}

static struct mlx5_flow_rule *fs_add_dst_ft(struct mlx5_flow_table *ft,
					    u8 match_criteria_enable,
					    u32 *match_criteria,
					    u32 *match_value,
					    u8 action, u32 flow_tag,
					    struct mlx5_flow_destination *dest)
{
	/*? where dst_entry is allocated*/
	struct mlx5_flow_group *g;
	struct mlx5_flow_rule *dst;

	fs_get(&ft->base);
	mutex_lock(&ft->base.lock);
	fs_for_each_fg(g, ft)
		if (fs_match_exact_mask(g->mask.match_criteria_enable,
					match_criteria_enable,
					g->mask.match_criteria,
					match_criteria)) {
			mutex_unlock(&ft->base.lock);

			dst = fs_add_dst_fg(g, match_value,
					    action, flow_tag, dest);
			if (PTR_ERR(dst) && PTR_ERR(dst) != -ENOSPC)
				goto unlock;
		}
	mutex_unlock(&ft->base.lock);

	g = create_autogroup(ft, match_criteria_enable, match_criteria);
	if (IS_ERR(g)) {
		dst = (void *)g;
		goto unlock;
	}

	dst = fs_add_dst_fg(g, match_value,
			    action, flow_tag, dest);
	if (IS_ERR(dst)) {
		/* Remove assumes refcount > 0 and autogroup creates a group
		 * with a refcount = 0.
		 */
		fs_get(&g->base);
		fs_remove_node(&g->base);
		goto unlock;
	}

unlock:
	fs_put(&ft->base);
	return dst;
}

struct mlx5_flow_rule *
mlx5_add_flow_rule(struct mlx5_flow_table *ft,
		   u8 match_criteria_enable,
		   u32 *match_criteria,
		   u32 *match_value,
		   u32 action,
		   u32 flow_tag,
		   struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_rule *dst;
	struct mlx5_flow_namespace *ns;

	ns = get_ns_with_notifiers(&ft->base);
	if (ns)
		down_read(&ns->dests_rw_sem);
	dst =  fs_add_dst_ft(ft, match_criteria_enable, match_criteria,
			     match_value, action, flow_tag, dest);
	if (ns)
		up_read(&ns->dests_rw_sem);

	return dst;


}
EXPORT_SYMBOL(mlx5_add_flow_rule);

void mlx5_del_flow_rule(struct mlx5_flow_rule *dst)
{
	struct mlx5_flow_namespace *ns;

	ns = get_ns_with_notifiers(&dst->base);
	if (ns)
		down_read(&ns->dests_rw_sem);
	fs_remove_node(&dst->base);
	if (ns)
		up_read(&ns->dests_rw_sem);
}
EXPORT_SYMBOL(mlx5_del_flow_rule);

#define MLX5_CORE_FS_ROOT_NS_NAME "root"
#define MLX5_CORE_FS_ESW_EGRESS_ACL "esw_egress_root"
#define MLX5_CORE_FS_ESW_INGRESS_ACL "esw_ingress_root"
#define MLX5_CORE_FS_FDB_ROOT_NS_NAME "fdb_root"
#define MLX5_CORE_FS_SNIFFER_RX_ROOT_NS_NAME "sniffer_rx_root"
#define MLX5_CORE_FS_SNIFFER_TX_ROOT_NS_NAME "sniffer_tx_root"
#define MLX5_CORE_FS_PRIO_MAX_FT 4
#define MLX5_CORE_FS_PRIO_MAX_NS 1

static struct fs_prio *fs_create_prio(struct mlx5_flow_namespace *ns,
				      unsigned prio, int max_ft,
				      const char *name, u8 flags)
{
	struct fs_prio *fs_prio;

	fs_prio = kzalloc(sizeof(*fs_prio), GFP_KERNEL);
	if (!fs_prio)
		return ERR_PTR(-ENOMEM);

	fs_prio->base.type = FS_TYPE_PRIO;
	fs_add_node(&fs_prio->base, &ns->base, name, 1);
	fs_prio->max_ft = max_ft;
	fs_prio->max_ns = MLX5_CORE_FS_PRIO_MAX_NS;
	fs_prio->prio = prio;
	fs_prio->flags = flags;
	list_add_tail(&fs_prio->base.list, &ns->prios);
	INIT_LIST_HEAD(&fs_prio->objs);
	mutex_init(&fs_prio->shared_lock);

	return fs_prio;
}

static void cleanup_root_ns(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_root_namespace *root_ns = dev->root_ns;
	struct fs_prio *iter_prio;

	if (!root_ns)
		return;

	/* stage 1 */
	fs_for_each_prio(iter_prio, &root_ns->ns) {
		struct mlx5_flow_namespace *iter_ns;

		fs_for_each_ns(iter_ns, iter_prio) {
			while (!list_empty(&iter_ns->prios)) {
				struct fs_base *iter_prio2 =
					list_first_entry(&iter_ns->prios,
							 struct fs_base,
							 list);

				fs_remove_node(iter_prio2);
			}
		}
	}

	/* stage 2 */
	fs_for_each_prio(iter_prio, &root_ns->ns) {
		while (!list_empty(&iter_prio->objs)) {
			struct fs_base *iter_ns =
				list_first_entry(&iter_prio->objs,
						 struct fs_base,
						 list);

				fs_remove_node(iter_ns);
		}
	}
	/* stage 3 */
	while (!list_empty(&root_ns->ns.prios)) {
		struct fs_base *iter_prio =
			list_first_entry(&root_ns->ns.prios,
					 struct fs_base,
					 list);

		fs_remove_node(iter_prio);
	}

	fs_remove_node(&root_ns->ns.base);
	dev->root_ns = NULL;
}

static void cleanup_single_prio_root_ns(struct mlx5_core_dev *dev,
					struct mlx5_flow_root_namespace *root_ns)
{
	struct fs_base *prio;

	if (!root_ns)
		return;

	if (!list_empty(&root_ns->ns.prios)) {
		prio = list_first_entry(&root_ns->ns.prios,
					struct fs_base,
				 list);
		fs_remove_node(prio);
	}
	fs_remove_node(&root_ns->ns.base);
	root_ns = NULL;
}

void mlx5_cleanup_fs(struct mlx5_core_dev *dev)
{
	cleanup_root_ns(dev);
	cleanup_single_prio_root_ns(dev, dev->sniffer_rx_root_ns);
	cleanup_single_prio_root_ns(dev, dev->sniffer_tx_root_ns);
	cleanup_single_prio_root_ns(dev, dev->fdb_root_ns);
	cleanup_single_prio_root_ns(dev, dev->esw_egress_root_ns);
	cleanup_single_prio_root_ns(dev, dev->esw_ingress_root_ns);
}

static struct mlx5_flow_namespace *fs_init_namespace(struct mlx5_flow_namespace
						 *ns)
{
	ns->base.type = FS_TYPE_NAMESPACE;
	init_rwsem(&ns->dests_rw_sem);
	init_rwsem(&ns->notifiers_rw_sem);
	INIT_LIST_HEAD(&ns->prios);
	INIT_LIST_HEAD(&ns->list_notifiers);

	return ns;
}

static struct mlx5_flow_root_namespace *create_root_ns(struct mlx5_core_dev *dev,
							  enum fs_ft_type
							  table_type,
							  char *name)
{
	struct mlx5_flow_root_namespace *root_ns;
	struct mlx5_flow_namespace *ns;

	/* create the root namespace */
	root_ns = mlx5_vzalloc(sizeof(*root_ns));
	if (!root_ns)
		goto err;

	root_ns->dev = dev;
	root_ns->table_type = table_type;
	mutex_init(&root_ns->fs_chain_lock);

	ns = &root_ns->ns;
	fs_init_namespace(ns);
	fs_add_node(&ns->base, NULL, name, 1);

	return root_ns;
err:
	return NULL;
}

static int init_fdb_root_ns(struct mlx5_core_dev *dev)
{
	struct fs_prio *prio;

	dev->fdb_root_ns = create_root_ns(dev, FS_FT_FDB,
					  MLX5_CORE_FS_FDB_ROOT_NS_NAME);
	if (!dev->fdb_root_ns)
		return -ENOMEM;

	/* create 1 prio*/
	prio = fs_create_prio(&dev->fdb_root_ns->ns, 0, 1, "fdb_prio", 0);
	if (IS_ERR(prio))
		return PTR_ERR(prio);
	else
		return 0;
}

#define MAX_VPORTS 128

static int init_egress_acl_root_ns(struct mlx5_core_dev *dev)
{
	struct fs_prio *prio;

	dev->esw_egress_root_ns = create_root_ns(dev, FS_FT_ESW_EGRESS_ACL,
						 MLX5_CORE_FS_ESW_EGRESS_ACL);
	if (!dev->esw_egress_root_ns)
		return -ENOMEM;

	/* create 1 prio*/
	prio = fs_create_prio(&dev->esw_egress_root_ns->ns, 0, MAX_VPORTS,
			      "esw_egress_prio", 0);
	if (IS_ERR(prio))
		return PTR_ERR(prio);
	else
		return 0;
}

static int init_ingress_acl_root_ns(struct mlx5_core_dev *dev)
{
	struct fs_prio *prio;

	dev->esw_ingress_root_ns = create_root_ns(dev, FS_FT_ESW_INGRESS_ACL,
						  MLX5_CORE_FS_ESW_INGRESS_ACL);
	if (!dev->esw_ingress_root_ns)
		return -ENOMEM;

	/* create 1 prio*/
	prio = fs_create_prio(&dev->esw_ingress_root_ns->ns, 0, MAX_VPORTS,
			      "esw_ingress_prio", 0);
	if (IS_ERR(prio))
		return PTR_ERR(prio);
	else
		return 0;
}

static int init_sniffer_rx_root_ns(struct mlx5_core_dev *dev)
{
	struct fs_prio *prio;

	dev->sniffer_rx_root_ns = create_root_ns(dev, FS_FT_SNIFFER_RX,
				     MLX5_CORE_FS_SNIFFER_RX_ROOT_NS_NAME);
	if (!dev->sniffer_rx_root_ns)
		return  -ENOMEM;

	/* create 1 prio*/
	prio = fs_create_prio(&dev->sniffer_rx_root_ns->ns, 0, 1,
			      "sniffer_prio", 0);
	if (IS_ERR(prio))
		return PTR_ERR(prio);
	else
		return 0;
}


static int init_sniffer_tx_root_ns(struct mlx5_core_dev *dev)
{
	struct fs_prio *prio;

	dev->sniffer_tx_root_ns = create_root_ns(dev, FS_FT_SNIFFER_TX,
						 MLX5_CORE_FS_SNIFFER_TX_ROOT_NS_NAME);
	if (!dev->sniffer_tx_root_ns)
		return  -ENOMEM;

	/* create 1 prio*/
	prio = fs_create_prio(&dev->sniffer_tx_root_ns->ns, 0, 1,
			      "sniffer_prio", 0);
	if (IS_ERR(prio))
		return PTR_ERR(prio);
	else
		return 0;
}

static struct mlx5_flow_namespace *fs_create_namespace(struct fs_prio *prio,
						       const char *name)
{
	struct mlx5_flow_namespace	*ns;

	ns = kzalloc(sizeof(*ns), GFP_KERNEL);
	if (!ns)
		return ERR_PTR(-ENOMEM);

	fs_init_namespace(ns);
	fs_add_node(&ns->base, &prio->base, name, 1);
	list_add_tail(&ns->base.list, &prio->objs);

	return ns;
}

#define FLOW_TABLE_BIT_SZ 1
#define GET_FLOW_TABLE_CAP(dev, offset) \
	((be32_to_cpu(*((__be32 *)(dev->hca_caps_cur[MLX5_CAP_FLOW_TABLE]) +	\
			offset / 32)) >>					\
	  (32 - FLOW_TABLE_BIT_SZ - (offset & 0x1f))) & FLOW_TABLE_BIT_SZ)

static bool has_required_caps(struct mlx5_core_dev *dev, struct node_caps *caps)
{
	int i;

	for (i = 0; i < caps->arr_sz; i++) {
		if (!GET_FLOW_TABLE_CAP(dev, caps->caps[i]))
			return false;
	}
	return true;
}

static int _init_root_tree(struct mlx5_core_dev *dev, int max_ft_level,
		    struct init_tree_node *node, struct fs_base *base_parent,
		    struct init_tree_node *tree_parent)
{
	struct mlx5_flow_namespace *fs_ns;
	struct fs_prio *fs_prio;
	int priority;
	struct fs_base *base;
	int i;
	int err = 0;

	if (node->type == FS_TYPE_PRIO) {
		if ((node->min_ft_level > max_ft_level) ||
		    !has_required_caps(dev, &node->caps))
			goto out;

		fs_get_obj(fs_ns, base_parent);
		priority = node - tree_parent->children;
		fs_prio = fs_create_prio(fs_ns, priority,
					 node->max_ft,
					 node->name, node->flags);
		if (IS_ERR(fs_prio)) {
			err = PTR_ERR(fs_prio);
			goto out;
		}
		base = &fs_prio->base;
	} else if (node->type == FS_TYPE_NAMESPACE) {
		fs_get_obj(fs_prio, base_parent);
		fs_ns = fs_create_namespace(fs_prio, node->name);
		if (IS_ERR(fs_ns)) {
			err = PTR_ERR(fs_ns);
			goto out;
		}
		base = &fs_ns->base;
	} else {
		return -EINVAL;
	}
	for (i = 0; i < node->ar_size; i++) {
		err = _init_root_tree(dev, max_ft_level, &node->children[i], base,
				      node);
		if (err)
			break;
	}
out:
	return err;
}

static int init_root_tree(struct mlx5_core_dev *dev, int max_ft_level,
		   struct init_tree_node *node, struct fs_base *parent)
{
	int i;
	struct mlx5_flow_namespace *fs_ns;
	int err = 0;

	fs_get_obj(fs_ns, parent);
	for (i = 0; i < node->ar_size; i++) {
		err = _init_root_tree(dev, max_ft_level,
				      &node->children[i], &fs_ns->base, node);
		if (err)
			break;
	}
	return err;
}

static int sum_max_ft_in_prio(struct fs_prio *prio);
static int sum_max_ft_in_ns(struct mlx5_flow_namespace *ns)
{
	struct fs_prio *prio;
	int sum = 0;

	fs_for_each_prio(prio, ns) {
		sum += sum_max_ft_in_prio(prio);
	}
	return  sum;
}

static int sum_max_ft_in_prio(struct fs_prio *prio)
{
	int sum = 0;
	struct fs_base *it;
	struct mlx5_flow_namespace	*ns;

	if (prio->max_ft)
		return prio->max_ft;

	fs_for_each_ns_or_ft(it, prio) {
		if (it->type == FS_TYPE_FLOW_TABLE)
			continue;

		fs_get_obj(ns, it);
		sum += sum_max_ft_in_ns(ns);
	}
	prio->max_ft = sum;
	return  sum;
}

static void set_max_ft(struct mlx5_flow_namespace *ns)
{
	struct fs_prio *prio;

	if (!ns)
		return;

	fs_for_each_prio(prio, ns)
		sum_max_ft_in_prio(prio);
}

static int init_root_ns(struct mlx5_core_dev *dev)
{
	int max_ft_level = MLX5_CAP_FLOWTABLE(dev,
					      flow_table_properties_nic_receive.
					      max_ft_level);

	dev->root_ns = create_root_ns(dev, FS_FT_NIC_RX,
				      MLX5_CORE_FS_ROOT_NS_NAME);
	if (IS_ERR_OR_NULL(dev->root_ns))
		goto err;


	if (init_root_tree(dev, max_ft_level, &root_fs, &dev->root_ns->ns.base))
		goto err;

	set_max_ft(&dev->root_ns->ns);

	return 0;
err:
	return -ENOMEM;
}

u8 mlx5_get_match_criteria_enable(struct mlx5_flow_rule *rule)
{
	struct fs_base *pbase;
	struct mlx5_flow_group *fg;

	pbase = rule->base.parent;
	WARN_ON(!pbase);
	pbase = pbase->parent;
	WARN_ON(!pbase);

	fs_get_obj(fg, pbase);
	return fg->mask.match_criteria_enable;
}

void mlx5_get_match_value(u32 *match_value,
			  struct mlx5_flow_rule *rule)
{
	struct fs_base *pbase;
	struct fs_fte *fte;

	pbase = rule->base.parent;
	WARN_ON(!pbase);
	fs_get_obj(fte, pbase);

	memcpy(match_value, fte->val, sizeof(fte->val));
}

void mlx5_get_match_criteria(u32 *match_criteria,
			     struct mlx5_flow_rule *rule)
{
	struct fs_base *pbase;
	struct mlx5_flow_group *fg;

	pbase = rule->base.parent;
	WARN_ON(!pbase);
	pbase = pbase->parent;
	WARN_ON(!pbase);

	fs_get_obj(fg, pbase);
	memcpy(match_criteria, &fg->mask.match_criteria,
	       sizeof(fg->mask.match_criteria));
}

int mlx5_init_fs(struct mlx5_core_dev *dev)
{
	int err;

	if (MLX5_CAP_GEN(dev, nic_flow_table)) {
		err = init_root_ns(dev);
		if (err)
			goto err;
	}

	err = init_fdb_root_ns(dev);
	if (err)
		goto err;

	err = init_egress_acl_root_ns(dev);
	if (err)
		goto err;

	err = init_ingress_acl_root_ns(dev);
	if (err)
		goto err;

	err = init_sniffer_tx_root_ns(dev);
	if (err)
		goto err;

	err = init_sniffer_rx_root_ns(dev);
	if (err)
		goto err;

	return 0;
err:
	mlx5_cleanup_fs(dev);
	return err;
}

struct mlx5_flow_namespace *mlx5_get_flow_namespace(struct mlx5_core_dev *dev,
						  enum mlx5_flow_namespace_type type)
{
	struct mlx5_flow_root_namespace *root_ns = dev->root_ns;
	int prio;
	static struct fs_prio *fs_prio;
	struct mlx5_flow_namespace *ns;

	switch (type) {
	case MLX5_FLOW_NAMESPACE_BYPASS:
		prio = 0;
		break;
	case MLX5_FLOW_NAMESPACE_KERNEL:
		prio = 1;
		break;
	case MLX5_FLOW_NAMESPACE_LEFTOVERS:
		prio = 2;
		break;
	case MLX5_FLOW_NAMESPACE_FDB:
		if (dev->fdb_root_ns)
			return &dev->fdb_root_ns->ns;
		else
			return NULL;
	case MLX5_FLOW_NAMESPACE_ESW_EGRESS:
		if (dev->esw_egress_root_ns)
			return &dev->esw_egress_root_ns->ns;
		else
			return NULL;
	case MLX5_FLOW_NAMESPACE_ESW_INGRESS:
		if (dev->esw_ingress_root_ns)
			return &dev->esw_ingress_root_ns->ns;
		else
			return NULL;
	case MLX5_FLOW_NAMESPACE_SNIFFER_RX:
		if (dev->sniffer_rx_root_ns)
			return &dev->sniffer_rx_root_ns->ns;
		else
			return NULL;
	case MLX5_FLOW_NAMESPACE_SNIFFER_TX:
		if (dev->sniffer_tx_root_ns)
			return &dev->sniffer_tx_root_ns->ns;
		else
			return NULL;
	default:
		return NULL;
	}

	if (!root_ns)
		return NULL;

	fs_prio = find_prio(&root_ns->ns, prio);
	if (!fs_prio)
		return NULL;

	ns = list_first_entry(&fs_prio->objs,
			      typeof(*ns),
			      base.list);

	return ns;
}
EXPORT_SYMBOL(mlx5_get_flow_namespace);


int mlx5_set_rule_private_data(struct mlx5_flow_rule *rule,
				  struct mlx5_flow_handler *fs_handler,
				  void  *client_data)
{
	struct fs_client_priv_data *priv_data;

	mutex_lock(&rule->clients_lock);
	/*Check that hanlder isn't exists in the list already*/
	list_for_each_entry(priv_data, &rule->clients_data, list) {
		if (priv_data->fs_handler == fs_handler) {
			priv_data->client_dst_data = client_data;
			goto unlock;
		}
	}
	priv_data = kzalloc(sizeof(*priv_data), GFP_KERNEL);
	if (!priv_data) {
		mutex_unlock(&rule->clients_lock);
		return -ENOMEM;
	}

	priv_data->client_dst_data = client_data;
	priv_data->fs_handler = fs_handler;
	list_add(&priv_data->list, &rule->clients_data);

unlock:
	mutex_unlock(&rule->clients_lock);

	return 0;
}

static int remove_from_clients(struct mlx5_flow_rule *rule,
			bool ctx_changed,
			void *client_data,
			void *context)
{
	struct fs_client_priv_data *iter_client;
	struct fs_client_priv_data *temp_client;
	struct mlx5_flow_handler *handler = (struct
						mlx5_flow_handler*)context;

	mutex_lock(&rule->clients_lock);
	list_for_each_entry_safe(iter_client, temp_client,
				 &rule->clients_data, list) {
		if (iter_client->fs_handler == handler) {
			list_del(&iter_client->list);
			kfree(iter_client);
			break;
		}
	}
	mutex_unlock(&rule->clients_lock);

	return 0;
}

struct mlx5_flow_handler *mlx5_register_rule_notifier(struct mlx5_core_dev *dev,
								enum mlx5_flow_namespace_type ns_type,
								rule_event_fn add_cb,
								rule_event_fn del_cb,
								void *context)
{
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_handler *handler;

	ns = mlx5_get_flow_namespace(dev, ns_type);
	if (!ns)
		return ERR_PTR(-EINVAL);

	handler = kzalloc(sizeof(*handler), GFP_KERNEL);
	if (!handler)
		return ERR_PTR(-ENOMEM);

	handler->add_dst_cb = add_cb;
	handler->del_dst_cb = del_cb;
	handler->client_context = context;
	handler->ns = ns;
	down_write(&ns->notifiers_rw_sem);
	list_add_tail(&handler->list, &ns->list_notifiers);
	up_write(&ns->notifiers_rw_sem);

	return handler;
}

static void iterate_rules_in_ns(struct mlx5_flow_namespace *ns,
				rule_event_fn add_rule_cb,
				void *context);

void mlx5_unregister_rule_notifier(struct mlx5_flow_handler *handler)
{
	struct mlx5_flow_namespace *ns = handler->ns;

	/*Remove from dst's clients*/
	down_write(&ns->dests_rw_sem);
	down_write(&ns->notifiers_rw_sem);
	iterate_rules_in_ns(ns, remove_from_clients, handler);
	list_del(&handler->list);
	up_write(&ns->notifiers_rw_sem);
	up_write(&ns->dests_rw_sem);
	kfree(handler);
}

static void iterate_rules_in_ft(struct mlx5_flow_table *ft,
				rule_event_fn add_rule_cb,
				void *context)
{
	struct mlx5_flow_group *iter_fg;
	struct fs_fte *iter_fte;
	struct mlx5_flow_rule *iter_rule;
	int err = 0;
	bool is_new_rule;

	mutex_lock(&ft->base.lock);
	fs_for_each_fg(iter_fg, ft) {
		mutex_lock(&iter_fg->base.lock);
		fs_for_each_fte(iter_fte, iter_fg) {
			mutex_lock(&iter_fte->base.lock);
			is_new_rule = true;
			fs_for_each_dst(iter_rule, iter_fte) {
				fs_get(&iter_rule->base);
				err = add_rule_cb(iter_rule,
						 is_new_rule,
						 NULL,
						 context);
				fs_put_parent_locked(&iter_rule->base);
				if (err)
					break;
				is_new_rule = false;
			}
			mutex_unlock(&iter_fte->base.lock);
			if (err)
				break;
		}
		mutex_unlock(&iter_fg->base.lock);
		if (err)
			break;
	}
	mutex_unlock(&ft->base.lock);
}

static void iterate_rules_in_prio(struct fs_prio *prio,
				  rule_event_fn add_rule_cb,
				  void *context)
{
	struct fs_base *it;

	mutex_lock(&prio->base.lock);
	fs_for_each_ns_or_ft(it, prio) {
		if (it->type == FS_TYPE_FLOW_TABLE) {
			struct mlx5_flow_table	      *ft;

			fs_get_obj(ft, it);
			iterate_rules_in_ft(ft, add_rule_cb, context);
		} else {
			struct mlx5_flow_namespace *ns;

			fs_get_obj(ns, it);
			iterate_rules_in_ns(ns, add_rule_cb, context);
		}
	}
	mutex_unlock(&prio->base.lock);
}

static void iterate_rules_in_ns(struct mlx5_flow_namespace *ns,
				rule_event_fn add_rule_cb,
				void *context)
{
	struct fs_prio *iter_prio;

	mutex_lock(&ns->base.lock);
	fs_for_each_prio(iter_prio, ns) {
		iterate_rules_in_prio(iter_prio, add_rule_cb, context);
	}
	mutex_unlock(&ns->base.lock);
}

void mlx5_flow_iterate_existing_rules(struct mlx5_flow_namespace *ns,
					 rule_event_fn add_rule_cb,
					 void *context)
{
	down_write(&ns->dests_rw_sem);
	down_read(&ns->notifiers_rw_sem);
	iterate_rules_in_ns(ns, add_rule_cb, context);
	up_read(&ns->notifiers_rw_sem);
	up_write(&ns->dests_rw_sem);
}


void mlx5_del_flow_rules_list(struct mlx5_flow_rules_list *rules_list)
{
	struct mlx5_flow_rule_node *iter_node;
	struct mlx5_flow_rule_node *temp_node;

	list_for_each_entry_safe(iter_node, temp_node, &rules_list->head, list) {
		list_del(&iter_node->list);
		kfree(iter_node);
	}

	kfree(rules_list);
}

#define ROCEV1_ETHERTYPE 0x8915
static int set_rocev1_rules(struct list_head *rules_list)
{
	struct mlx5_flow_rule_node *rocev1_rule;

	rocev1_rule = kzalloc(sizeof(*rocev1_rule), GFP_KERNEL);
	if (!rocev1_rule)
		return -ENOMEM;

	rocev1_rule->match_criteria_enable =
		1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_OUTER_HEADERS;
	MLX5_SET(fte_match_set_lyr_2_4, rocev1_rule->match_criteria, ethertype,
		 0xffff);
	MLX5_SET(fte_match_set_lyr_2_4, rocev1_rule->match_value, ethertype,
		 ROCEV1_ETHERTYPE);

	list_add_tail(&rocev1_rule->list, rules_list);

	return 0;
}

#define ROCEV2_UDP_PORT 4791
static int set_rocev2_rules(struct list_head *rules_list)
{
	struct mlx5_flow_rule_node *ipv4_rule;
	struct mlx5_flow_rule_node *ipv6_rule;

	ipv4_rule = kzalloc(sizeof(*ipv4_rule), GFP_KERNEL);
	if (!ipv4_rule)
		return -ENOMEM;

	ipv6_rule = kzalloc(sizeof(*ipv6_rule), GFP_KERNEL);
	if (!ipv6_rule) {
		kfree(ipv4_rule);
		return -ENOMEM;
	}

	ipv4_rule->match_criteria_enable =
		1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_OUTER_HEADERS;
	MLX5_SET(fte_match_set_lyr_2_4, ipv4_rule->match_criteria, ethertype,
		 0xffff);
	MLX5_SET(fte_match_set_lyr_2_4, ipv4_rule->match_value, ethertype,
		 0x0800);
	MLX5_SET(fte_match_set_lyr_2_4, ipv4_rule->match_criteria, ip_protocol,
		 0xff);
	MLX5_SET(fte_match_set_lyr_2_4, ipv4_rule->match_value, ip_protocol,
		 IPPROTO_UDP);
	MLX5_SET(fte_match_set_lyr_2_4, ipv4_rule->match_criteria, udp_dport,
		 0xffff);
	MLX5_SET(fte_match_set_lyr_2_4, ipv4_rule->match_value, udp_dport,
		 ROCEV2_UDP_PORT);

	ipv6_rule->match_criteria_enable =
		1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_OUTER_HEADERS;
	MLX5_SET(fte_match_set_lyr_2_4, ipv6_rule->match_criteria, ethertype,
		 0xffff);
	MLX5_SET(fte_match_set_lyr_2_4, ipv6_rule->match_value, ethertype,
		 0x86dd);
	MLX5_SET(fte_match_set_lyr_2_4, ipv6_rule->match_criteria, ip_protocol,
		 0xff);
	MLX5_SET(fte_match_set_lyr_2_4, ipv6_rule->match_value, ip_protocol,
		 IPPROTO_UDP);
	MLX5_SET(fte_match_set_lyr_2_4, ipv6_rule->match_criteria, udp_dport,
		 0xffff);
	MLX5_SET(fte_match_set_lyr_2_4, ipv6_rule->match_value, udp_dport,
		 ROCEV2_UDP_PORT);

	list_add_tail(&ipv4_rule->list, rules_list);
	list_add_tail(&ipv6_rule->list, rules_list);

	return 0;
}


struct mlx5_flow_rules_list *get_roce_flow_rules(u8 roce_mode)
{
	int err = 0;
	struct mlx5_flow_rules_list *rules_list =
		kzalloc(sizeof(*rules_list), GFP_KERNEL);

	if (!rules_list)
		return NULL;

	INIT_LIST_HEAD(&rules_list->head);

	if (roce_mode & MLX5_ROCE_VERSION_1_CAP) {
		err = set_rocev1_rules(&rules_list->head);
		if (err)
			goto free_list;
	}
	if (roce_mode & MLX5_ROCE_VERSION_2_CAP)
		err = set_rocev2_rules(&rules_list->head);
	if (err)
		goto free_list;

	return rules_list;

free_list:
	mlx5_del_flow_rules_list(rules_list);
	return NULL;
}
