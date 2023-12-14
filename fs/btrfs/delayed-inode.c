// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 Fujitsu.  All rights reserved.
 * Written by Miao Xie <miaox@cn.fujitsu.com>
 */

#include <linux/slab.h>
#include <linux/iversion.h>
#include "ctree.h"
#include "fs.h"
#include "messages.h"
#include "misc.h"
#include "delayed-inode.h"
#include "disk-io.h"
#include "transaction.h"
#include "qgroup.h"
#include "locking.h"
#include "inode-item.h"
#include "space-info.h"
#include "accessors.h"
#include "file-item.h"

#define BTRFS_DELAYED_WRITEBACK		512
#define BTRFS_DELAYED_BACKGROUND	128
#define BTRFS_DELAYED_BATCH		16

static struct kmem_cache *delayed_node_cache;

int __init btrfs_delayed_inode_init(void)
{
	delayed_node_cache = kmem_cache_create("btrfs_delayed_node",
					sizeof(struct btrfs_delayed_node),
					0,
					SLAB_MEM_SPREAD,
					NULL);
	if (!delayed_node_cache)
		return -ENOMEM;
	return 0;
}

void __cold btrfs_delayed_inode_exit(void)
{
	kmem_cache_destroy(delayed_node_cache);
}

static inline void btrfs_init_delayed_node(
				struct btrfs_delayed_node *delayed_node,
				struct btrfs_root *root, u64 inode_id)
{
	delayed_node->root = root;
	delayed_node->inode_id = inode_id;
	refcount_set(&delayed_node->refs, 0);
	delayed_node->ins_root = RB_ROOT_CACHED;
	delayed_node->del_root = RB_ROOT_CACHED;
	mutex_init(&delayed_node->mutex);
	INIT_LIST_HEAD(&delayed_node->n_list);
	INIT_LIST_HEAD(&delayed_node->p_list);
}

static struct btrfs_delayed_node *btrfs_get_delayed_node(
		struct btrfs_inode *btrfs_inode)
{
	struct btrfs_root *root = btrfs_inode->root;
	u64 ino = btrfs_ino(btrfs_inode);
	struct btrfs_delayed_node *node;

	node = READ_ONCE(btrfs_inode->delayed_node);
	if (node) {
		refcount_inc(&node->refs);
		return node;
	}

	spin_lock(&root->inode_lock);
	node = radix_tree_lookup(&root->delayed_nodes_tree, ino);

	if (node) {
		if (btrfs_inode->delayed_node) {
			refcount_inc(&node->refs);	/* can be accessed */
			BUG_ON(btrfs_inode->delayed_node != node);
			spin_unlock(&root->inode_lock);
			return node;
		}

		/*
		 * It's possible that we're racing into the middle of removing
		 * this node from the radix tree.  In this case, the refcount
		 * was zero and it should never go back to one.  Just return
		 * NULL like it was never in the radix at all; our release
		 * function is in the process of removing it.
		 *
		 * Some implementations of refcount_inc refuse to bump the
		 * refcount once it has hit zero.  If we don't do this dance
		 * here, refcount_inc() may decide to just WARN_ONCE() instead
		 * of actually bumping the refcount.
		 *
		 * If this node is properly in the radix, we want to bump the
		 * refcount twice, once for the inode and once for this get
		 * operation.
		 */
		if (refcount_inc_not_zero(&node->refs)) {
			refcount_inc(&node->refs);
			btrfs_inode->delayed_node = node;
		} else {
			node = NULL;
		}

		spin_unlock(&root->inode_lock);
		return node;
	}
	spin_unlock(&root->inode_lock);

	return NULL;
}

/* Will return either the node or PTR_ERR(-ENOMEM) */
static struct btrfs_delayed_node *btrfs_get_or_create_delayed_node(
		struct btrfs_inode *btrfs_inode)
{
	struct btrfs_delayed_node *node;
	struct btrfs_root *root = btrfs_inode->root;
	u64 ino = btrfs_ino(btrfs_inode);
	int ret;

again:
	node = btrfs_get_delayed_node(btrfs_inode);
	if (node)
		return node;

	node = kmem_cache_zalloc(delayed_node_cache, GFP_NOFS);
	if (!node)
		return ERR_PTR(-ENOMEM);
	btrfs_init_delayed_node(node, root, ino);

	/* cached in the btrfs inode and can be accessed */
	refcount_set(&node->refs, 2);

	ret = radix_tree_preload(GFP_NOFS);
	if (ret) {
		kmem_cache_free(delayed_node_cache, node);
		return ERR_PTR(ret);
	}

	spin_lock(&root->inode_lock);
	ret = radix_tree_insert(&root->delayed_nodes_tree, ino, node);
	if (ret == -EEXIST) {
		spin_unlock(&root->inode_lock);
		kmem_cache_free(delayed_node_cache, node);
		radix_tree_preload_end();
		goto again;
	}
	btrfs_inode->delayed_node = node;
	spin_unlock(&root->inode_lock);
	radix_tree_preload_end();

	return node;
}

/*
 * Call it when holding delayed_node->mutex
 *
 * If mod = 1, add this node into the prepared list.
 */
static void btrfs_queue_delayed_node(struct btrfs_delayed_root *root,
				     struct btrfs_delayed_node *node,
				     int mod)
{
	spin_lock(&root->lock);
	if (test_bit(BTRFS_DELAYED_NODE_IN_LIST, &node->flags)) {
		if (!list_empty(&node->p_list))
			list_move_tail(&node->p_list, &root->prepare_list);
		else if (mod)
			list_add_tail(&node->p_list, &root->prepare_list);
	} else {
		list_add_tail(&node->n_list, &root->node_list);
		list_add_tail(&node->p_list, &root->prepare_list);
		refcount_inc(&node->refs);	/* inserted into list */
		root->nodes++;
		set_bit(BTRFS_DELAYED_NODE_IN_LIST, &node->flags);
	}
	spin_unlock(&root->lock);
}

/* Call it when holding delayed_node->mutex */
static void btrfs_dequeue_delayed_node(struct btrfs_delayed_root *root,
				       struct btrfs_delayed_node *node)
{
	spin_lock(&root->lock);
	if (test_bit(BTRFS_DELAYED_NODE_IN_LIST, &node->flags)) {
		root->nodes--;
		refcount_dec(&node->refs);	/* not in the list */
		list_del_init(&node->n_list);
		if (!list_empty(&node->p_list))
			list_del_init(&node->p_list);
		clear_bit(BTRFS_DELAYED_NODE_IN_LIST, &node->flags);
	}
	spin_unlock(&root->lock);
}

static struct btrfs_delayed_node *btrfs_first_delayed_node(
			struct btrfs_delayed_root *delayed_root)
{
	struct list_head *p;
	struct btrfs_delayed_node *node = NULL;

	spin_lock(&delayed_root->lock);
	if (list_empty(&delayed_root->node_list))
		goto out;

	p = delayed_root->node_list.next;
	node = list_entry(p, struct btrfs_delayed_node, n_list);
	refcount_inc(&node->refs);
out:
	spin_unlock(&delayed_root->lock);

	return node;
}

static struct btrfs_delayed_node *btrfs_next_delayed_node(
						struct btrfs_delayed_node *node)
{
	struct btrfs_delayed_root *delayed_root;
	struct list_head *p;
	struct btrfs_delayed_node *next = NULL;

	delayed_root = node->root->fs_info->delayed_root;
	spin_lock(&delayed_root->lock);
	if (!test_bit(BTRFS_DELAYED_NODE_IN_LIST, &node->flags)) {
		/* not in the list */
		if (list_empty(&delayed_root->node_list))
			goto out;
		p = delayed_root->node_list.next;
	} else if (list_is_last(&node->n_list, &delayed_root->node_list))
		goto out;
	else
		p = node->n_list.next;

	next = list_entry(p, struct btrfs_delayed_node, n_list);
	refcount_inc(&next->refs);
out:
	spin_unlock(&delayed_root->lock);

	return next;
}

static void __btrfs_release_delayed_node(
				struct btrfs_delayed_node *delayed_node,
				int mod)
{
	struct btrfs_delayed_root *delayed_root;

	if (!delayed_node)
		return;

	delayed_root = delayed_node->root->fs_info->delayed_root;

	mutex_lock(&delayed_node->mutex);
	if (delayed_node->count)
		btrfs_queue_delayed_node(delayed_root, delayed_node, mod);
	else
		btrfs_dequeue_delayed_node(delayed_root, delayed_node);
	mutex_unlock(&delayed_node->mutex);

	if (refcount_dec_and_test(&delayed_node->refs)) {
		struct btrfs_root *root = delayed_node->root;

		spin_lock(&root->inode_lock);
		/*
		 * Once our refcount goes to zero, nobody is allowed to bump it
		 * back up.  We can delete it now.
		 */
		ASSERT(refcount_read(&delayed_node->refs) == 0);
		radix_tree_delete(&root->delayed_nodes_tree,
				  delayed_node->inode_id);
		spin_unlock(&root->inode_lock);
		kmem_cache_free(delayed_node_cache, delayed_node);
	}
}

static inline void btrfs_release_delayed_node(struct btrfs_delayed_node *node)
{
	__btrfs_release_delayed_node(node, 0);
}

static struct btrfs_delayed_node *btrfs_first_prepared_delayed_node(
					struct btrfs_delayed_root *delayed_root)
{
	struct list_head *p;
	struct btrfs_delayed_node *node = NULL;

	spin_lock(&delayed_root->lock);
	if (list_empty(&delayed_root->prepare_list))
		goto out;

	p = delayed_root->prepare_list.next;
	list_del_init(p);
	node = list_entry(p, struct btrfs_delayed_node, p_list);
	refcount_inc(&node->refs);
out:
	spin_unlock(&delayed_root->lock);

	return node;
}

static inline void btrfs_release_prepared_delayed_node(
					struct btrfs_delayed_node *node)
{
	__btrfs_release_delayed_node(node, 1);
}

static struct btrfs_delayed_item *btrfs_alloc_delayed_item(u16 data_len,
					   struct btrfs_delayed_node *node,
					   enum btrfs_delayed_item_type type)
{
	struct btrfs_delayed_item *item;

	item = kmalloc(struct_size(item, data, data_len), GFP_NOFS);
	if (item) {
		item->data_len = data_len;
		item->type = type;
		item->bytes_reserved = 0;
		item->delayed_node = node;
		RB_CLEAR_NODE(&item->rb_node);
		INIT_LIST_HEAD(&item->log_list);
		item->logged = false;
		refcount_set(&item->refs, 1);
	}
	return item;
}

/*
 * __btrfs_lookup_delayed_item - look up the delayed item by key
 * @delayed_node: pointer to the delayed node
 * @index:	  the dir index value to lookup (offset of a dir index key)
 *
 * Note: if we don't find the right item, we will return the prev item and
 * the next item.
 */
static struct btrfs_delayed_item *__btrfs_lookup_delayed_item(
				struct rb_root *root,
				u64 index)
{
	struct rb_node *node = root->rb_node;
	struct btrfs_delayed_item *delayed_item = NULL;

	while (node) {
		delayed_item = rb_entry(node, struct btrfs_delayed_item,
					rb_node);
		if (delayed_item->index < index)
			node = node->rb_right;
		else if (delayed_item->index > index)
			node = node->rb_left;
		else
			return delayed_item;
	}

	return NULL;
}

static int __btrfs_add_delayed_item(struct btrfs_delayed_node *delayed_node,
				    struct btrfs_delayed_item *ins)
{
	struct rb_node **p, *node;
	struct rb_node *parent_node = NULL;
	struct rb_root_cached *root;
	struct btrfs_delayed_item *item;
	bool leftmost = true;

	if (ins->type == BTRFS_DELAYED_INSERTION_ITEM)
		root = &delayed_node->ins_root;
	else
		root = &delayed_node->del_root;

	p = &root->rb_root.rb_node;
	node = &ins->rb_node;

	while (*p) {
		parent_node = *p;
		item = rb_entry(parent_node, struct btrfs_delayed_item,
				 rb_node);

		if (item->index < ins->index) {
			p = &(*p)->rb_right;
			leftmost = false;
		} else if (item->index > ins->index) {
			p = &(*p)->rb_left;
		} else {
			return -EEXIST;
		}
	}

	rb_link_node(node, parent_node, p);
	rb_insert_color_cached(node, root, leftmost);

	if (ins->type == BTRFS_DELAYED_INSERTION_ITEM &&
	    ins->index >= delayed_node->index_cnt)
		delayed_node->index_cnt = ins->index + 1;

	delayed_node->count++;
	atomic_inc(&delayed_node->root->fs_info->delayed_root->items);
	return 0;
}

static void finish_one_item(struct btrfs_delayed_root *delayed_root)
{
	int seq = atomic_inc_return(&delayed_root->items_seq);

	/* atomic_dec_return implies a barrier */
	if ((atomic_dec_return(&delayed_root->items) <
	    BTRFS_DELAYED_BACKGROUND || seq % BTRFS_DELAYED_BATCH == 0))
		cond_wake_up_nomb(&delayed_root->wait);
}

static void __btrfs_remove_delayed_item(struct btrfs_delayed_item *delayed_item)
{
	struct btrfs_delayed_node *delayed_node = delayed_item->delayed_node;
	struct rb_root_cached *root;
	struct btrfs_delayed_root *delayed_root;

	/* Not inserted, ignore it. */
	if (RB_EMPTY_NODE(&delayed_item->rb_node))
		return;

	/* If it's in a rbtree, then we need to have delayed node locked. */
	lockdep_assert_held(&delayed_node->mutex);

	delayed_root = delayed_node->root->fs_info->delayed_root;

	BUG_ON(!delayed_root);

	if (delayed_item->type == BTRFS_DELAYED_INSERTION_ITEM)
		root = &delayed_node->ins_root;
	else
		root = &delayed_node->del_root;

	rb_erase_cached(&delayed_item->rb_node, root);
	RB_CLEAR_NODE(&delayed_item->rb_node);
	delayed_node->count--;

	finish_one_item(delayed_root);
}

static void btrfs_release_delayed_item(struct btrfs_delayed_item *item)
{
	if (item) {
		__btrfs_remove_delayed_item(item);
		if (refcount_dec_and_test(&item->refs))
			kfree(item);
	}
}

static struct btrfs_delayed_item *__btrfs_first_delayed_insertion_item(
					struct btrfs_delayed_node *delayed_node)
{
	struct rb_node *p;
	struct btrfs_delayed_item *item = NULL;

	p = rb_first_cached(&delayed_node->ins_root);
	if (p)
		item = rb_entry(p, struct btrfs_delayed_item, rb_node);

	return item;
}

static struct btrfs_delayed_item *__btrfs_first_delayed_deletion_item(
					struct btrfs_delayed_node *delayed_node)
{
	struct rb_node *p;
	struct btrfs_delayed_item *item = NULL;

	p = rb_first_cached(&delayed_node->del_root);
	if (p)
		item = rb_entry(p, struct btrfs_delayed_item, rb_node);

	return item;
}

static struct btrfs_delayed_item *__btrfs_next_delayed_item(
						struct btrfs_delayed_item *item)
{
	struct rb_node *p;
	struct btrfs_delayed_item *next = NULL;

	p = rb_next(&item->rb_node);
	if (p)
		next = rb_entry(p, struct btrfs_delayed_item, rb_node);

	return next;
}

static int btrfs_delayed_item_reserve_metadata(struct btrfs_trans_handle *trans,
					       struct btrfs_delayed_item *item)
{
	struct btrfs_block_rsv *src_rsv;
	struct btrfs_block_rsv *dst_rsv;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	u64 num_bytes;
	int ret;

	if (!trans->bytes_reserved)
		return 0;

	src_rsv = trans->block_rsv;
	dst_rsv = &fs_info->delayed_block_rsv;

	num_bytes = btrfs_calc_insert_metadata_size(fs_info, 1);

	/*
	 * Here we migrate space rsv from transaction rsv, since have already
	 * reserved space when starting a transaction.  So no need to reserve
	 * qgroup space here.
	 */
	ret = btrfs_block_rsv_migrate(src_rsv, dst_rsv, num_bytes, true);
	if (!ret) {
		trace_btrfs_space_reservation(fs_info, "delayed_item",
					      item->delayed_node->inode_id,
					      num_bytes, 1);
		/*
		 * For insertions we track reserved metadata space by accounting
		 * for the number of leaves that will be used, based on the delayed
		 * node's index_items_size field.
		 */
		if (item->type == BTRFS_DELAYED_DELETION_ITEM)
			item->bytes_reserved = num_bytes;
	}

	return ret;
}

static void btrfs_delayed_item_release_metadata(struct btrfs_root *root,
						struct btrfs_delayed_item *item)
{
	struct btrfs_block_rsv *rsv;
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (!item->bytes_reserved)
		return;

	rsv = &fs_info->delayed_block_rsv;
	/*
	 * Check btrfs_delayed_item_reserve_metadata() to see why we don't need
	 * to release/reserve qgroup space.
	 */
	trace_btrfs_space_reservation(fs_info, "delayed_item",
				      item->delayed_node->inode_id,
				      item->bytes_reserved, 0);
	btrfs_block_rsv_release(fs_info, rsv, item->bytes_reserved, NULL);
}

static void btrfs_delayed_item_release_leaves(struct btrfs_delayed_node *node,
					      unsigned int num_leaves)
{
	struct btrfs_fs_info *fs_info = node->root->fs_info;
	const u64 bytes = btrfs_calc_insert_metadata_size(fs_info, num_leaves);

	/* There are no space reservations during log replay, bail out. */
	if (test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags))
		return;

	trace_btrfs_space_reservation(fs_info, "delayed_item", node->inode_id,
				      bytes, 0);
	btrfs_block_rsv_release(fs_info, &fs_info->delayed_block_rsv, bytes, NULL);
}

static int btrfs_delayed_inode_reserve_metadata(
					struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_delayed_node *node)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_rsv *src_rsv;
	struct btrfs_block_rsv *dst_rsv;
	u64 num_bytes;
	int ret;

	src_rsv = trans->block_rsv;
	dst_rsv = &fs_info->delayed_block_rsv;

	num_bytes = btrfs_calc_metadata_size(fs_info, 1);

	/*
	 * btrfs_dirty_inode will update the inode under btrfs_join_transaction
	 * which doesn't reserve space for speed.  This is a problem since we
	 * still need to reserve space for this update, so try to reserve the
	 * space.
	 *
	 * Now if src_rsv == delalloc_block_rsv we'll let it just steal since
	 * we always reserve enough to update the inode item.
	 */
	if (!src_rsv || (!trans->bytes_reserved &&
			 src_rsv->type != BTRFS_BLOCK_RSV_DELALLOC)) {
		ret = btrfs_qgroup_reserve_meta(root, num_bytes,
					  BTRFS_QGROUP_RSV_META_PREALLOC, true);
		if (ret < 0)
			return ret;
		ret = btrfs_block_rsv_add(fs_info, dst_rsv, num_bytes,
					  BTRFS_RESERVE_NO_FLUSH);
		/* NO_FLUSH could only fail with -ENOSPC */
		ASSERT(ret == 0 || ret == -ENOSPC);
		if (ret)
			btrfs_qgroup_free_meta_prealloc(root, num_bytes);
	} else {
		ret = btrfs_block_rsv_migrate(src_rsv, dst_rsv, num_bytes, true);
	}

	if (!ret) {
		trace_btrfs_space_reservation(fs_info, "delayed_inode",
					      node->inode_id, num_bytes, 1);
		node->bytes_reserved = num_bytes;
	}

	return ret;
}

static void btrfs_delayed_inode_release_metadata(struct btrfs_fs_info *fs_info,
						struct btrfs_delayed_node *node,
						bool qgroup_free)
{
	struct btrfs_block_rsv *rsv;

	if (!node->bytes_reserved)
		return;

	rsv = &fs_info->delayed_block_rsv;
	trace_btrfs_space_reservation(fs_info, "delayed_inode",
				      node->inode_id, node->bytes_reserved, 0);
	btrfs_block_rsv_release(fs_info, rsv, node->bytes_reserved, NULL);
	if (qgroup_free)
		btrfs_qgroup_free_meta_prealloc(node->root,
				node->bytes_reserved);
	else
		btrfs_qgroup_convert_reserved_meta(node->root,
				node->bytes_reserved);
	node->bytes_reserved = 0;
}

/*
 * Insert a single delayed item or a batch of delayed items, as many as possible
 * that fit in a leaf. The delayed items (dir index keys) are sorted by their key
 * in the rbtree, and if there's a gap between two consecutive dir index items,
 * then it means at some point we had delayed dir indexes to add but they got
 * removed (by btrfs_delete_delayed_dir_index()) before we attempted to flush them
 * into the subvolume tree. Dir index keys also have their offsets coming from a
 * monotonically increasing counter, so we can't get new keys with an offset that
 * fits within a gap between delayed dir index items.
 */
static int btrfs_insert_delayed_item(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct btrfs_path *path,
				     struct btrfs_delayed_item *first_item)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_delayed_node *node = first_item->delayed_node;
	LIST_HEAD(item_list);
	struct btrfs_delayed_item *curr;
	struct btrfs_delayed_item *next;
	const int max_size = BTRFS_LEAF_DATA_SIZE(fs_info);
	struct btrfs_item_batch batch;
	struct btrfs_key first_key;
	const u32 first_data_size = first_item->data_len;
	int total_size;
	char *ins_data = NULL;
	int ret;
	bool continuous_keys_only = false;

	lockdep_assert_held(&node->mutex);

	/*
	 * During normal operation the delayed index offset is continuously
	 * increasing, so we can batch insert all items as there will not be any
	 * overlapping keys in the tree.
	 *
	 * The exception to this is log replay, where we may have interleaved
	 * offsets in the tree, so our batch needs to be continuous keys only in
	 * order to ensure we do not end up with out of order items in our leaf.
	 */
	if (test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags))
		continuous_keys_only = true;

	/*
	 * For delayed items to insert, we track reserved metadata bytes based
	 * on the number of leaves that we will use.
	 * See btrfs_insert_delayed_dir_index() and
	 * btrfs_delayed_item_reserve_metadata()).
	 */
	ASSERT(first_item->bytes_reserved == 0);

	list_add_tail(&first_item->tree_list, &item_list);
	batch.total_data_size = first_data_size;
	batch.nr = 1;
	total_size = first_data_size + sizeof(struct btrfs_item);
	curr = first_item;

	while (true) {
		int next_size;

		next = __btrfs_next_delayed_item(curr);
		if (!next)
			break;

		/*
		 * We cannot allow gaps in the key space if we're doing log
		 * replay.
		 */
		if (continuous_keys_only && (next->index != curr->index + 1))
			break;

		ASSERT(next->bytes_reserved == 0);

		next_size = next->data_len + sizeof(struct btrfs_item);
		if (total_size + next_size > max_size)
			break;

		list_add_tail(&next->tree_list, &item_list);
		batch.nr++;
		total_size += next_size;
		batch.total_data_size += next->data_len;
		curr = next;
	}

	if (batch.nr == 1) {
		first_key.objectid = node->inode_id;
		first_key.type = BTRFS_DIR_INDEX_KEY;
		first_key.offset = first_item->index;
		batch.keys = &first_key;
		batch.data_sizes = &first_data_size;
	} else {
		struct btrfs_key *ins_keys;
		u32 *ins_sizes;
		int i = 0;

		ins_data = kmalloc(batch.nr * sizeof(u32) +
				   batch.nr * sizeof(struct btrfs_key), GFP_NOFS);
		if (!ins_data) {
			ret = -ENOMEM;
			goto out;
		}
		ins_sizes = (u32 *)ins_data;
		ins_keys = (struct btrfs_key *)(ins_data + batch.nr * sizeof(u32));
		batch.keys = ins_keys;
		batch.data_sizes = ins_sizes;
		list_for_each_entry(curr, &item_list, tree_list) {
			ins_keys[i].objectid = node->inode_id;
			ins_keys[i].type = BTRFS_DIR_INDEX_KEY;
			ins_keys[i].offset = curr->index;
			ins_sizes[i] = curr->data_len;
			i++;
		}
	}

	ret = btrfs_insert_empty_items(trans, root, path, &batch);
	if (ret)
		goto out;

	list_for_each_entry(curr, &item_list, tree_list) {
		char *data_ptr;

		data_ptr = btrfs_item_ptr(path->nodes[0], path->slots[0], char);
		write_extent_buffer(path->nodes[0], &curr->data,
				    (unsigned long)data_ptr, curr->data_len);
		path->slots[0]++;
	}

	/*
	 * Now release our path before releasing the delayed items and their
	 * metadata reservations, so that we don't block other tasks for more
	 * time than needed.
	 */
	btrfs_release_path(path);

	ASSERT(node->index_item_leaves > 0);

	/*
	 * For normal operations we will batch an entire leaf's worth of delayed
	 * items, so if there are more items to process we can decrement
	 * index_item_leaves by 1 as we inserted 1 leaf's worth of items.
	 *
	 * However for log replay we may not have inserted an entire leaf's
	 * worth of items, we may have not had continuous items, so decrementing
	 * here would mess up the index_item_leaves accounting.  For this case
	 * only clean up the accounting when there are no items left.
	 */
	if (next && !continuous_keys_only) {
		/*
		 * We inserted one batch of items into a leaf a there are more
		 * items to flush in a future batch, now release one unit of
		 * metadata space from the delayed block reserve, corresponding
		 * the leaf we just flushed to.
		 */
		btrfs_delayed_item_release_leaves(node, 1);
		node->index_item_leaves--;
	} else if (!next) {
		/*
		 * There are no more items to insert. We can have a number of
		 * reserved leaves > 1 here - this happens when many dir index
		 * items are added and then removed before they are flushed (file
		 * names with a very short life, never span a transaction). So
		 * release all remaining leaves.
		 */
		btrfs_delayed_item_release_leaves(node, node->index_item_leaves);
		node->index_item_leaves = 0;
	}

	list_for_each_entry_safe(curr, next, &item_list, tree_list) {
		list_del(&curr->tree_list);
		btrfs_release_delayed_item(curr);
	}
out:
	kfree(ins_data);
	return ret;
}

static int btrfs_insert_delayed_items(struct btrfs_trans_handle *trans,
				      struct btrfs_path *path,
				      struct btrfs_root *root,
				      struct btrfs_delayed_node *node)
{
	int ret = 0;

	while (ret == 0) {
		struct btrfs_delayed_item *curr;

		mutex_lock(&node->mutex);
		curr = __btrfs_first_delayed_insertion_item(node);
		if (!curr) {
			mutex_unlock(&node->mutex);
			break;
		}
		ret = btrfs_insert_delayed_item(trans, root, path, curr);
		mutex_unlock(&node->mutex);
	}

	return ret;
}

static int btrfs_batch_delete_items(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root,
				    struct btrfs_path *path,
				    struct btrfs_delayed_item *item)
{
	const u64 ino = item->delayed_node->inode_id;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_delayed_item *curr, *next;
	struct extent_buffer *leaf = path->nodes[0];
	LIST_HEAD(batch_list);
	int nitems, slot, last_slot;
	int ret;
	u64 total_reserved_size = item->bytes_reserved;

	ASSERT(leaf != NULL);

	slot = path->slots[0];
	last_slot = btrfs_header_nritems(leaf) - 1;
	/*
	 * Our caller always gives us a path pointing to an existing item, so
	 * this can not happen.
	 */
	ASSERT(slot <= last_slot);
	if (WARN_ON(slot > last_slot))
		return -ENOENT;

	nitems = 1;
	curr = item;
	list_add_tail(&curr->tree_list, &batch_list);

	/*
	 * Keep checking if the next delayed item matches the next item in the
	 * leaf - if so, we can add it to the batch of items to delete from the
	 * leaf.
	 */
	while (slot < last_slot) {
		struct btrfs_key key;

		next = __btrfs_next_delayed_item(curr);
		if (!next)
			break;

		slot++;
		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid != ino ||
		    key.type != BTRFS_DIR_INDEX_KEY ||
		    key.offset != next->index)
			break;
		nitems++;
		curr = next;
		list_add_tail(&curr->tree_list, &batch_list);
		total_reserved_size += curr->bytes_reserved;
	}

	ret = btrfs_del_items(trans, root, path, path->slots[0], nitems);
	if (ret)
		return ret;

	/* In case of BTRFS_FS_LOG_RECOVERING items won't have reserved space */
	if (total_reserved_size > 0) {
		/*
		 * Check btrfs_delayed_item_reserve_metadata() to see why we
		 * don't need to release/reserve qgroup space.
		 */
		trace_btrfs_space_reservation(fs_info, "delayed_item", ino,
					      total_reserved_size, 0);
		btrfs_block_rsv_release(fs_info, &fs_info->delayed_block_rsv,
					total_reserved_size, NULL);
	}

	list_for_each_entry_safe(curr, next, &batch_list, tree_list) {
		list_del(&curr->tree_list);
		btrfs_release_delayed_item(curr);
	}

	return 0;
}

static int btrfs_delete_delayed_items(struct btrfs_trans_handle *trans,
				      struct btrfs_path *path,
				      struct btrfs_root *root,
				      struct btrfs_delayed_node *node)
{
	struct btrfs_key key;
	int ret = 0;

	key.objectid = node->inode_id;
	key.type = BTRFS_DIR_INDEX_KEY;

	while (ret == 0) {
		struct btrfs_delayed_item *item;

		mutex_lock(&node->mutex);
		item = __btrfs_first_delayed_deletion_item(node);
		if (!item) {
			mutex_unlock(&node->mutex);
			break;
		}

		key.offset = item->index;
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret > 0) {
			/*
			 * There's no matching item in the leaf. This means we
			 * have already deleted this item in a past run of the
			 * delayed items. We ignore errors when running delayed
			 * items from an async context, through a work queue job
			 * running btrfs_async_run_delayed_root(), and don't
			 * release delayed items that failed to complete. This
			 * is because we will retry later, and at transaction
			 * commit time we always run delayed items and will
			 * then deal with errors if they fail to run again.
			 *
			 * So just release delayed items for which we can't find
			 * an item in the tree, and move to the next item.
			 */
			btrfs_release_path(path);
			btrfs_release_delayed_item(item);
			ret = 0;
		} else if (ret == 0) {
			ret = btrfs_batch_delete_items(trans, root, path, item);
			btrfs_release_path(path);
		}

		/*
		 * We unlock and relock on each iteration, this is to prevent
		 * blocking other tasks for too long while we are being run from
		 * the async context (work queue job). Those tasks are typically
		 * running system calls like creat/mkdir/rename/unlink/etc which
		 * need to add delayed items to this delayed node.
		 */
		mutex_unlock(&node->mutex);
	}

	return ret;
}

static void btrfs_release_delayed_inode(struct btrfs_delayed_node *delayed_node)
{
	struct btrfs_delayed_root *delayed_root;

	if (delayed_node &&
	    test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_node->flags)) {
		BUG_ON(!delayed_node->root);
		clear_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_node->flags);
		delayed_node->count--;

		delayed_root = delayed_node->root->fs_info->delayed_root;
		finish_one_item(delayed_root);
	}
}

static void btrfs_release_delayed_iref(struct btrfs_delayed_node *delayed_node)
{

	if (test_and_clear_bit(BTRFS_DELAYED_NODE_DEL_IREF, &delayed_node->flags)) {
		struct btrfs_delayed_root *delayed_root;

		ASSERT(delayed_node->root);
		delayed_node->count--;

		delayed_root = delayed_node->root->fs_info->delayed_root;
		finish_one_item(delayed_root);
	}
}

static int __btrfs_update_delayed_inode(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_path *path,
					struct btrfs_delayed_node *node)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_key key;
	struct btrfs_inode_item *inode_item;
	struct extent_buffer *leaf;
	int mod;
	int ret;

	key.objectid = node->inode_id;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;

	if (test_bit(BTRFS_DELAYED_NODE_DEL_IREF, &node->flags))
		mod = -1;
	else
		mod = 1;

	ret = btrfs_lookup_inode(trans, root, path, &key, mod);
	if (ret > 0)
		ret = -ENOENT;
	if (ret < 0)
		goto out;

	leaf = path->nodes[0];
	inode_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_inode_item);
	write_extent_buffer(leaf, &node->inode_item, (unsigned long)inode_item,
			    sizeof(struct btrfs_inode_item));
	btrfs_mark_buffer_dirty(trans, leaf);

	if (!test_bit(BTRFS_DELAYED_NODE_DEL_IREF, &node->flags))
		goto out;

	path->slots[0]++;
	if (path->slots[0] >= btrfs_header_nritems(leaf))
		goto search;
again:
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	if (key.objectid != node->inode_id)
		goto out;

	if (key.type != BTRFS_INODE_REF_KEY &&
	    key.type != BTRFS_INODE_EXTREF_KEY)
		goto out;

	/*
	 * Delayed iref deletion is for the inode who has only one link,
	 * so there is only one iref. The case that several irefs are
	 * in the same item doesn't exist.
	 */
	ret = btrfs_del_item(trans, root, path);
out:
	btrfs_release_delayed_iref(node);
	btrfs_release_path(path);
err_out:
	btrfs_delayed_inode_release_metadata(fs_info, node, (ret < 0));
	btrfs_release_delayed_inode(node);

	/*
	 * If we fail to update the delayed inode we need to abort the
	 * transaction, because we could leave the inode with the improper
	 * counts behind.
	 */
	if (ret && ret != -ENOENT)
		btrfs_abort_transaction(trans, ret);

	return ret;

search:
	btrfs_release_path(path);

	key.type = BTRFS_INODE_EXTREF_KEY;
	key.offset = -1;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto err_out;
	ASSERT(ret);

	ret = 0;
	leaf = path->nodes[0];
	path->slots[0]--;
	goto again;
}

static inline int btrfs_update_delayed_inode(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     struct btrfs_path *path,
					     struct btrfs_delayed_node *node)
{
	int ret;

	mutex_lock(&node->mutex);
	if (!test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &node->flags)) {
		mutex_unlock(&node->mutex);
		return 0;
	}

	ret = __btrfs_update_delayed_inode(trans, root, path, node);
	mutex_unlock(&node->mutex);
	return ret;
}

static inline int
__btrfs_commit_inode_delayed_items(struct btrfs_trans_handle *trans,
				   struct btrfs_path *path,
				   struct btrfs_delayed_node *node)
{
	int ret;

	ret = btrfs_insert_delayed_items(trans, path, node->root, node);
	if (ret)
		return ret;

	ret = btrfs_delete_delayed_items(trans, path, node->root, node);
	if (ret)
		return ret;

	ret = btrfs_update_delayed_inode(trans, node->root, path, node);
	return ret;
}

/*
 * Called when committing the transaction.
 * Returns 0 on success.
 * Returns < 0 on error and returns with an aborted transaction with any
 * outstanding delayed items cleaned up.
 */
static int __btrfs_run_delayed_items(struct btrfs_trans_handle *trans, int nr)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_delayed_root *delayed_root;
	struct btrfs_delayed_node *curr_node, *prev_node;
	struct btrfs_path *path;
	struct btrfs_block_rsv *block_rsv;
	int ret = 0;
	bool count = (nr > 0);

	if (TRANS_ABORTED(trans))
		return -EIO;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	block_rsv = trans->block_rsv;
	trans->block_rsv = &fs_info->delayed_block_rsv;

	delayed_root = fs_info->delayed_root;

	curr_node = btrfs_first_delayed_node(delayed_root);
	while (curr_node && (!count || nr--)) {
		ret = __btrfs_commit_inode_delayed_items(trans, path,
							 curr_node);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			break;
		}

		prev_node = curr_node;
		curr_node = btrfs_next_delayed_node(curr_node);
		/*
		 * See the comment below about releasing path before releasing
		 * node. If the commit of delayed items was successful the path
		 * should always be released, but in case of an error, it may
		 * point to locked extent buffers (a leaf at the very least).
		 */
		ASSERT(path->nodes[0] == NULL);
		btrfs_release_delayed_node(prev_node);
	}

	/*
	 * Release the path to avoid a potential deadlock and lockdep splat when
	 * releasing the delayed node, as that requires taking the delayed node's
	 * mutex. If another task starts running delayed items before we take
	 * the mutex, it will first lock the mutex and then it may try to lock
	 * the same btree path (leaf).
	 */
	btrfs_free_path(path);

	if (curr_node)
		btrfs_release_delayed_node(curr_node);
	trans->block_rsv = block_rsv;

	return ret;
}

int btrfs_run_delayed_items(struct btrfs_trans_handle *trans)
{
	return __btrfs_run_delayed_items(trans, -1);
}

int btrfs_run_delayed_items_nr(struct btrfs_trans_handle *trans, int nr)
{
	return __btrfs_run_delayed_items(trans, nr);
}

int btrfs_commit_inode_delayed_items(struct btrfs_trans_handle *trans,
				     struct btrfs_inode *inode)
{
	struct btrfs_delayed_node *delayed_node = btrfs_get_delayed_node(inode);
	struct btrfs_path *path;
	struct btrfs_block_rsv *block_rsv;
	int ret;

	if (!delayed_node)
		return 0;

	mutex_lock(&delayed_node->mutex);
	if (!delayed_node->count) {
		mutex_unlock(&delayed_node->mutex);
		btrfs_release_delayed_node(delayed_node);
		return 0;
	}
	mutex_unlock(&delayed_node->mutex);

	path = btrfs_alloc_path();
	if (!path) {
		btrfs_release_delayed_node(delayed_node);
		return -ENOMEM;
	}

	block_rsv = trans->block_rsv;
	trans->block_rsv = &delayed_node->root->fs_info->delayed_block_rsv;

	ret = __btrfs_commit_inode_delayed_items(trans, path, delayed_node);

	btrfs_release_delayed_node(delayed_node);
	btrfs_free_path(path);
	trans->block_rsv = block_rsv;

	return ret;
}

int btrfs_commit_inode_delayed_inode(struct btrfs_inode *inode)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_trans_handle *trans;
	struct btrfs_delayed_node *delayed_node = btrfs_get_delayed_node(inode);
	struct btrfs_path *path;
	struct btrfs_block_rsv *block_rsv;
	int ret;

	if (!delayed_node)
		return 0;

	mutex_lock(&delayed_node->mutex);
	if (!test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_node->flags)) {
		mutex_unlock(&delayed_node->mutex);
		btrfs_release_delayed_node(delayed_node);
		return 0;
	}
	mutex_unlock(&delayed_node->mutex);

	trans = btrfs_join_transaction(delayed_node->root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto trans_out;
	}

	block_rsv = trans->block_rsv;
	trans->block_rsv = &fs_info->delayed_block_rsv;

	mutex_lock(&delayed_node->mutex);
	if (test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_node->flags))
		ret = __btrfs_update_delayed_inode(trans, delayed_node->root,
						   path, delayed_node);
	else
		ret = 0;
	mutex_unlock(&delayed_node->mutex);

	btrfs_free_path(path);
	trans->block_rsv = block_rsv;
trans_out:
	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(fs_info);
out:
	btrfs_release_delayed_node(delayed_node);

	return ret;
}

void btrfs_remove_delayed_node(struct btrfs_inode *inode)
{
	struct btrfs_delayed_node *delayed_node;

	delayed_node = READ_ONCE(inode->delayed_node);
	if (!delayed_node)
		return;

	inode->delayed_node = NULL;
	btrfs_release_delayed_node(delayed_node);
}

struct btrfs_async_delayed_work {
	struct btrfs_delayed_root *delayed_root;
	int nr;
	struct btrfs_work work;
};

static void btrfs_async_run_delayed_root(struct btrfs_work *work)
{
	struct btrfs_async_delayed_work *async_work;
	struct btrfs_delayed_root *delayed_root;
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path;
	struct btrfs_delayed_node *delayed_node = NULL;
	struct btrfs_root *root;
	struct btrfs_block_rsv *block_rsv;
	int total_done = 0;

	async_work = container_of(work, struct btrfs_async_delayed_work, work);
	delayed_root = async_work->delayed_root;

	path = btrfs_alloc_path();
	if (!path)
		goto out;

	do {
		if (atomic_read(&delayed_root->items) <
		    BTRFS_DELAYED_BACKGROUND / 2)
			break;

		delayed_node = btrfs_first_prepared_delayed_node(delayed_root);
		if (!delayed_node)
			break;

		root = delayed_node->root;

		trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			btrfs_release_path(path);
			btrfs_release_prepared_delayed_node(delayed_node);
			total_done++;
			continue;
		}

		block_rsv = trans->block_rsv;
		trans->block_rsv = &root->fs_info->delayed_block_rsv;

		__btrfs_commit_inode_delayed_items(trans, path, delayed_node);

		trans->block_rsv = block_rsv;
		btrfs_end_transaction(trans);
		btrfs_btree_balance_dirty_nodelay(root->fs_info);

		btrfs_release_path(path);
		btrfs_release_prepared_delayed_node(delayed_node);
		total_done++;

	} while ((async_work->nr == 0 && total_done < BTRFS_DELAYED_WRITEBACK)
		 || total_done < async_work->nr);

	btrfs_free_path(path);
out:
	wake_up(&delayed_root->wait);
	kfree(async_work);
}


static int btrfs_wq_run_delayed_node(struct btrfs_delayed_root *delayed_root,
				     struct btrfs_fs_info *fs_info, int nr)
{
	struct btrfs_async_delayed_work *async_work;

	async_work = kmalloc(sizeof(*async_work), GFP_NOFS);
	if (!async_work)
		return -ENOMEM;

	async_work->delayed_root = delayed_root;
	btrfs_init_work(&async_work->work, btrfs_async_run_delayed_root, NULL,
			NULL);
	async_work->nr = nr;

	btrfs_queue_work(fs_info->delayed_workers, &async_work->work);
	return 0;
}

void btrfs_assert_delayed_root_empty(struct btrfs_fs_info *fs_info)
{
	WARN_ON(btrfs_first_delayed_node(fs_info->delayed_root));
}

static int could_end_wait(struct btrfs_delayed_root *delayed_root, int seq)
{
	int val = atomic_read(&delayed_root->items_seq);

	if (val < seq || val >= seq + BTRFS_DELAYED_BATCH)
		return 1;

	if (atomic_read(&delayed_root->items) < BTRFS_DELAYED_BACKGROUND)
		return 1;

	return 0;
}

void btrfs_balance_delayed_items(struct btrfs_fs_info *fs_info)
{
	struct btrfs_delayed_root *delayed_root = fs_info->delayed_root;

	if ((atomic_read(&delayed_root->items) < BTRFS_DELAYED_BACKGROUND) ||
		btrfs_workqueue_normal_congested(fs_info->delayed_workers))
		return;

	if (atomic_read(&delayed_root->items) >= BTRFS_DELAYED_WRITEBACK) {
		int seq;
		int ret;

		seq = atomic_read(&delayed_root->items_seq);

		ret = btrfs_wq_run_delayed_node(delayed_root, fs_info, 0);
		if (ret)
			return;

		wait_event_interruptible(delayed_root->wait,
					 could_end_wait(delayed_root, seq));
		return;
	}

	btrfs_wq_run_delayed_node(delayed_root, fs_info, BTRFS_DELAYED_BATCH);
}

static void btrfs_release_dir_index_item_space(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	const u64 bytes = btrfs_calc_insert_metadata_size(fs_info, 1);

	if (test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags))
		return;

	/*
	 * Adding the new dir index item does not require touching another
	 * leaf, so we can release 1 unit of metadata that was previously
	 * reserved when starting the transaction. This applies only to
	 * the case where we had a transaction start and excludes the
	 * transaction join case (when replaying log trees).
	 */
	trace_btrfs_space_reservation(fs_info, "transaction",
				      trans->transid, bytes, 0);
	btrfs_block_rsv_release(fs_info, trans->block_rsv, bytes, NULL);
	ASSERT(trans->bytes_reserved >= bytes);
	trans->bytes_reserved -= bytes;
}

/* Will return 0, -ENOMEM or -EEXIST (index number collision, unexpected). */
int btrfs_insert_delayed_dir_index(struct btrfs_trans_handle *trans,
				   const char *name, int name_len,
				   struct btrfs_inode *dir,
				   struct btrfs_disk_key *disk_key, u8 flags,
				   u64 index)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	const unsigned int leaf_data_size = BTRFS_LEAF_DATA_SIZE(fs_info);
	struct btrfs_delayed_node *delayed_node;
	struct btrfs_delayed_item *delayed_item;
	struct btrfs_dir_item *dir_item;
	bool reserve_leaf_space;
	u32 data_len;
	int ret;

	delayed_node = btrfs_get_or_create_delayed_node(dir);
	if (IS_ERR(delayed_node))
		return PTR_ERR(delayed_node);

	delayed_item = btrfs_alloc_delayed_item(sizeof(*dir_item) + name_len,
						delayed_node,
						BTRFS_DELAYED_INSERTION_ITEM);
	if (!delayed_item) {
		ret = -ENOMEM;
		goto release_node;
	}

	delayed_item->index = index;

	dir_item = (struct btrfs_dir_item *)delayed_item->data;
	dir_item->location = *disk_key;
	btrfs_set_stack_dir_transid(dir_item, trans->transid);
	btrfs_set_stack_dir_data_len(dir_item, 0);
	btrfs_set_stack_dir_name_len(dir_item, name_len);
	btrfs_set_stack_dir_flags(dir_item, flags);
	memcpy((char *)(dir_item + 1), name, name_len);

	data_len = delayed_item->data_len + sizeof(struct btrfs_item);

	mutex_lock(&delayed_node->mutex);

	/*
	 * First attempt to insert the delayed item. This is to make the error
	 * handling path simpler in case we fail (-EEXIST). There's no risk of
	 * any other task coming in and running the delayed item before we do
	 * the metadata space reservation below, because we are holding the
	 * delayed node's mutex and that mutex must also be locked before the
	 * node's delayed items can be run.
	 */
	ret = __btrfs_add_delayed_item(delayed_node, delayed_item);
	if (unlikely(ret)) {
		btrfs_err(trans->fs_info,
"error adding delayed dir index item, name: %.*s, index: %llu, root: %llu, dir: %llu, dir->index_cnt: %llu, delayed_node->index_cnt: %llu, error: %d",
			  name_len, name, index, btrfs_root_id(delayed_node->root),
			  delayed_node->inode_id, dir->index_cnt,
			  delayed_node->index_cnt, ret);
		btrfs_release_delayed_item(delayed_item);
		btrfs_release_dir_index_item_space(trans);
		mutex_unlock(&delayed_node->mutex);
		goto release_node;
	}

	if (delayed_node->index_item_leaves == 0 ||
	    delayed_node->curr_index_batch_size + data_len > leaf_data_size) {
		delayed_node->curr_index_batch_size = data_len;
		reserve_leaf_space = true;
	} else {
		delayed_node->curr_index_batch_size += data_len;
		reserve_leaf_space = false;
	}

	if (reserve_leaf_space) {
		ret = btrfs_delayed_item_reserve_metadata(trans, delayed_item);
		/*
		 * Space was reserved for a dir index item insertion when we
		 * started the transaction, so getting a failure here should be
		 * impossible.
		 */
		if (WARN_ON(ret)) {
			btrfs_release_delayed_item(delayed_item);
			mutex_unlock(&delayed_node->mutex);
			goto release_node;
		}

		delayed_node->index_item_leaves++;
	} else {
		btrfs_release_dir_index_item_space(trans);
	}
	mutex_unlock(&delayed_node->mutex);

release_node:
	btrfs_release_delayed_node(delayed_node);
	return ret;
}

static int btrfs_delete_delayed_insertion_item(struct btrfs_fs_info *fs_info,
					       struct btrfs_delayed_node *node,
					       u64 index)
{
	struct btrfs_delayed_item *item;

	mutex_lock(&node->mutex);
	item = __btrfs_lookup_delayed_item(&node->ins_root.rb_root, index);
	if (!item) {
		mutex_unlock(&node->mutex);
		return 1;
	}

	/*
	 * For delayed items to insert, we track reserved metadata bytes based
	 * on the number of leaves that we will use.
	 * See btrfs_insert_delayed_dir_index() and
	 * btrfs_delayed_item_reserve_metadata()).
	 */
	ASSERT(item->bytes_reserved == 0);
	ASSERT(node->index_item_leaves > 0);

	/*
	 * If there's only one leaf reserved, we can decrement this item from the
	 * current batch, otherwise we can not because we don't know which leaf
	 * it belongs to. With the current limit on delayed items, we rarely
	 * accumulate enough dir index items to fill more than one leaf (even
	 * when using a leaf size of 4K).
	 */
	if (node->index_item_leaves == 1) {
		const u32 data_len = item->data_len + sizeof(struct btrfs_item);

		ASSERT(node->curr_index_batch_size >= data_len);
		node->curr_index_batch_size -= data_len;
	}

	btrfs_release_delayed_item(item);

	/* If we now have no more dir index items, we can release all leaves. */
	if (RB_EMPTY_ROOT(&node->ins_root.rb_root)) {
		btrfs_delayed_item_release_leaves(node, node->index_item_leaves);
		node->index_item_leaves = 0;
	}

	mutex_unlock(&node->mutex);
	return 0;
}

int btrfs_delete_delayed_dir_index(struct btrfs_trans_handle *trans,
				   struct btrfs_inode *dir, u64 index)
{
	struct btrfs_delayed_node *node;
	struct btrfs_delayed_item *item;
	int ret;

	node = btrfs_get_or_create_delayed_node(dir);
	if (IS_ERR(node))
		return PTR_ERR(node);

	ret = btrfs_delete_delayed_insertion_item(trans->fs_info, node, index);
	if (!ret)
		goto end;

	item = btrfs_alloc_delayed_item(0, node, BTRFS_DELAYED_DELETION_ITEM);
	if (!item) {
		ret = -ENOMEM;
		goto end;
	}

	item->index = index;

	ret = btrfs_delayed_item_reserve_metadata(trans, item);
	/*
	 * we have reserved enough space when we start a new transaction,
	 * so reserving metadata failure is impossible.
	 */
	if (ret < 0) {
		btrfs_err(trans->fs_info,
"metadata reservation failed for delayed dir item deltiona, should have been reserved");
		btrfs_release_delayed_item(item);
		goto end;
	}

	mutex_lock(&node->mutex);
	ret = __btrfs_add_delayed_item(node, item);
	if (unlikely(ret)) {
		btrfs_err(trans->fs_info,
			  "err add delayed dir index item(index: %llu) into the deletion tree of the delayed node(root id: %llu, inode id: %llu, errno: %d)",
			  index, node->root->root_key.objectid,
			  node->inode_id, ret);
		btrfs_delayed_item_release_metadata(dir->root, item);
		btrfs_release_delayed_item(item);
	}
	mutex_unlock(&node->mutex);
end:
	btrfs_release_delayed_node(node);
	return ret;
}

int btrfs_inode_delayed_dir_index_count(struct btrfs_inode *inode)
{
	struct btrfs_delayed_node *delayed_node = btrfs_get_delayed_node(inode);

	if (!delayed_node)
		return -ENOENT;

	/*
	 * Since we have held i_mutex of this directory, it is impossible that
	 * a new directory index is added into the delayed node and index_cnt
	 * is updated now. So we needn't lock the delayed node.
	 */
	if (!delayed_node->index_cnt) {
		btrfs_release_delayed_node(delayed_node);
		return -EINVAL;
	}

	inode->index_cnt = delayed_node->index_cnt;
	btrfs_release_delayed_node(delayed_node);
	return 0;
}

bool btrfs_readdir_get_delayed_items(struct inode *inode,
				     u64 last_index,
				     struct list_head *ins_list,
				     struct list_head *del_list)
{
	struct btrfs_delayed_node *delayed_node;
	struct btrfs_delayed_item *item;

	delayed_node = btrfs_get_delayed_node(BTRFS_I(inode));
	if (!delayed_node)
		return false;

	/*
	 * We can only do one readdir with delayed items at a time because of
	 * item->readdir_list.
	 */
	btrfs_inode_unlock(BTRFS_I(inode), BTRFS_ILOCK_SHARED);
	btrfs_inode_lock(BTRFS_I(inode), 0);

	mutex_lock(&delayed_node->mutex);
	item = __btrfs_first_delayed_insertion_item(delayed_node);
	while (item && item->index <= last_index) {
		refcount_inc(&item->refs);
		list_add_tail(&item->readdir_list, ins_list);
		item = __btrfs_next_delayed_item(item);
	}

	item = __btrfs_first_delayed_deletion_item(delayed_node);
	while (item && item->index <= last_index) {
		refcount_inc(&item->refs);
		list_add_tail(&item->readdir_list, del_list);
		item = __btrfs_next_delayed_item(item);
	}
	mutex_unlock(&delayed_node->mutex);
	/*
	 * This delayed node is still cached in the btrfs inode, so refs
	 * must be > 1 now, and we needn't check it is going to be freed
	 * or not.
	 *
	 * Besides that, this function is used to read dir, we do not
	 * insert/delete delayed items in this period. So we also needn't
	 * requeue or dequeue this delayed node.
	 */
	refcount_dec(&delayed_node->refs);

	return true;
}

void btrfs_readdir_put_delayed_items(struct inode *inode,
				     struct list_head *ins_list,
				     struct list_head *del_list)
{
	struct btrfs_delayed_item *curr, *next;

	list_for_each_entry_safe(curr, next, ins_list, readdir_list) {
		list_del(&curr->readdir_list);
		if (refcount_dec_and_test(&curr->refs))
			kfree(curr);
	}

	list_for_each_entry_safe(curr, next, del_list, readdir_list) {
		list_del(&curr->readdir_list);
		if (refcount_dec_and_test(&curr->refs))
			kfree(curr);
	}

	/*
	 * The VFS is going to do up_read(), so we need to downgrade back to a
	 * read lock.
	 */
	downgrade_write(&inode->i_rwsem);
}

int btrfs_should_delete_dir_index(struct list_head *del_list,
				  u64 index)
{
	struct btrfs_delayed_item *curr;
	int ret = 0;

	list_for_each_entry(curr, del_list, readdir_list) {
		if (curr->index > index)
			break;
		if (curr->index == index) {
			ret = 1;
			break;
		}
	}
	return ret;
}

/*
 * btrfs_readdir_delayed_dir_index - read dir info stored in the delayed tree
 *
 */
int btrfs_readdir_delayed_dir_index(struct dir_context *ctx,
				    struct list_head *ins_list)
{
	struct btrfs_dir_item *di;
	struct btrfs_delayed_item *curr, *next;
	struct btrfs_key location;
	char *name;
	int name_len;
	int over = 0;
	unsigned char d_type;

	/*
	 * Changing the data of the delayed item is impossible. So
	 * we needn't lock them. And we have held i_mutex of the
	 * directory, nobody can delete any directory indexes now.
	 */
	list_for_each_entry_safe(curr, next, ins_list, readdir_list) {
		list_del(&curr->readdir_list);

		if (curr->index < ctx->pos) {
			if (refcount_dec_and_test(&curr->refs))
				kfree(curr);
			continue;
		}

		ctx->pos = curr->index;

		di = (struct btrfs_dir_item *)curr->data;
		name = (char *)(di + 1);
		name_len = btrfs_stack_dir_name_len(di);

		d_type = fs_ftype_to_dtype(btrfs_dir_flags_to_ftype(di->type));
		btrfs_disk_key_to_cpu(&location, &di->location);

		over = !dir_emit(ctx, name, name_len,
			       location.objectid, d_type);

		if (refcount_dec_and_test(&curr->refs))
			kfree(curr);

		if (over)
			return 1;
		ctx->pos++;
	}
	return 0;
}

static void fill_stack_inode_item(struct btrfs_trans_handle *trans,
				  struct btrfs_inode_item *inode_item,
				  struct inode *inode)
{
	u64 flags;

	btrfs_set_stack_inode_uid(inode_item, i_uid_read(inode));
	btrfs_set_stack_inode_gid(inode_item, i_gid_read(inode));
	btrfs_set_stack_inode_size(inode_item, BTRFS_I(inode)->disk_i_size);
	btrfs_set_stack_inode_mode(inode_item, inode->i_mode);
	btrfs_set_stack_inode_nlink(inode_item, inode->i_nlink);
	btrfs_set_stack_inode_nbytes(inode_item, inode_get_bytes(inode));
	btrfs_set_stack_inode_generation(inode_item,
					 BTRFS_I(inode)->generation);
	btrfs_set_stack_inode_sequence(inode_item,
				       inode_peek_iversion(inode));
	btrfs_set_stack_inode_transid(inode_item, trans->transid);
	btrfs_set_stack_inode_rdev(inode_item, inode->i_rdev);
	flags = btrfs_inode_combine_flags(BTRFS_I(inode)->flags,
					  BTRFS_I(inode)->ro_flags);
	btrfs_set_stack_inode_flags(inode_item, flags);
	btrfs_set_stack_inode_block_group(inode_item, 0);

	btrfs_set_stack_timespec_sec(&inode_item->atime,
				     inode->i_atime.tv_sec);
	btrfs_set_stack_timespec_nsec(&inode_item->atime,
				      inode->i_atime.tv_nsec);

	btrfs_set_stack_timespec_sec(&inode_item->mtime,
				     inode->i_mtime.tv_sec);
	btrfs_set_stack_timespec_nsec(&inode_item->mtime,
				      inode->i_mtime.tv_nsec);

	btrfs_set_stack_timespec_sec(&inode_item->ctime,
				     inode_get_ctime(inode).tv_sec);
	btrfs_set_stack_timespec_nsec(&inode_item->ctime,
				      inode_get_ctime(inode).tv_nsec);

	btrfs_set_stack_timespec_sec(&inode_item->otime,
				     BTRFS_I(inode)->i_otime.tv_sec);
	btrfs_set_stack_timespec_nsec(&inode_item->otime,
				     BTRFS_I(inode)->i_otime.tv_nsec);
}

int btrfs_fill_inode(struct inode *inode, u32 *rdev)
{
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	struct btrfs_delayed_node *delayed_node;
	struct btrfs_inode_item *inode_item;

	delayed_node = btrfs_get_delayed_node(BTRFS_I(inode));
	if (!delayed_node)
		return -ENOENT;

	mutex_lock(&delayed_node->mutex);
	if (!test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_node->flags)) {
		mutex_unlock(&delayed_node->mutex);
		btrfs_release_delayed_node(delayed_node);
		return -ENOENT;
	}

	inode_item = &delayed_node->inode_item;

	i_uid_write(inode, btrfs_stack_inode_uid(inode_item));
	i_gid_write(inode, btrfs_stack_inode_gid(inode_item));
	btrfs_i_size_write(BTRFS_I(inode), btrfs_stack_inode_size(inode_item));
	btrfs_inode_set_file_extent_range(BTRFS_I(inode), 0,
			round_up(i_size_read(inode), fs_info->sectorsize));
	inode->i_mode = btrfs_stack_inode_mode(inode_item);
	set_nlink(inode, btrfs_stack_inode_nlink(inode_item));
	inode_set_bytes(inode, btrfs_stack_inode_nbytes(inode_item));
	BTRFS_I(inode)->generation = btrfs_stack_inode_generation(inode_item);
        BTRFS_I(inode)->last_trans = btrfs_stack_inode_transid(inode_item);

	inode_set_iversion_queried(inode,
				   btrfs_stack_inode_sequence(inode_item));
	inode->i_rdev = 0;
	*rdev = btrfs_stack_inode_rdev(inode_item);
	btrfs_inode_split_flags(btrfs_stack_inode_flags(inode_item),
				&BTRFS_I(inode)->flags, &BTRFS_I(inode)->ro_flags);

	inode->i_atime.tv_sec = btrfs_stack_timespec_sec(&inode_item->atime);
	inode->i_atime.tv_nsec = btrfs_stack_timespec_nsec(&inode_item->atime);

	inode->i_mtime.tv_sec = btrfs_stack_timespec_sec(&inode_item->mtime);
	inode->i_mtime.tv_nsec = btrfs_stack_timespec_nsec(&inode_item->mtime);

	inode_set_ctime(inode, btrfs_stack_timespec_sec(&inode_item->ctime),
			btrfs_stack_timespec_nsec(&inode_item->ctime));

	BTRFS_I(inode)->i_otime.tv_sec =
		btrfs_stack_timespec_sec(&inode_item->otime);
	BTRFS_I(inode)->i_otime.tv_nsec =
		btrfs_stack_timespec_nsec(&inode_item->otime);

	inode->i_generation = BTRFS_I(inode)->generation;
	BTRFS_I(inode)->index_cnt = (u64)-1;

	mutex_unlock(&delayed_node->mutex);
	btrfs_release_delayed_node(delayed_node);
	return 0;
}

int btrfs_delayed_update_inode(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_inode *inode)
{
	struct btrfs_delayed_node *delayed_node;
	int ret = 0;

	delayed_node = btrfs_get_or_create_delayed_node(inode);
	if (IS_ERR(delayed_node))
		return PTR_ERR(delayed_node);

	mutex_lock(&delayed_node->mutex);
	if (test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_node->flags)) {
		fill_stack_inode_item(trans, &delayed_node->inode_item,
				      &inode->vfs_inode);
		goto release_node;
	}

	ret = btrfs_delayed_inode_reserve_metadata(trans, root, delayed_node);
	if (ret)
		goto release_node;

	fill_stack_inode_item(trans, &delayed_node->inode_item, &inode->vfs_inode);
	set_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_node->flags);
	delayed_node->count++;
	atomic_inc(&root->fs_info->delayed_root->items);
release_node:
	mutex_unlock(&delayed_node->mutex);
	btrfs_release_delayed_node(delayed_node);
	return ret;
}

int btrfs_delayed_delete_inode_ref(struct btrfs_inode *inode)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_delayed_node *delayed_node;

	/*
	 * we don't do delayed inode updates during log recovery because it
	 * leads to enospc problems.  This means we also can't do
	 * delayed inode refs
	 */
	if (test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags))
		return -EAGAIN;

	delayed_node = btrfs_get_or_create_delayed_node(inode);
	if (IS_ERR(delayed_node))
		return PTR_ERR(delayed_node);

	/*
	 * We don't reserve space for inode ref deletion is because:
	 * - We ONLY do async inode ref deletion for the inode who has only
	 *   one link(i_nlink == 1), it means there is only one inode ref.
	 *   And in most case, the inode ref and the inode item are in the
	 *   same leaf, and we will deal with them at the same time.
	 *   Since we are sure we will reserve the space for the inode item,
	 *   it is unnecessary to reserve space for inode ref deletion.
	 * - If the inode ref and the inode item are not in the same leaf,
	 *   We also needn't worry about enospc problem, because we reserve
	 *   much more space for the inode update than it needs.
	 * - At the worst, we can steal some space from the global reservation.
	 *   It is very rare.
	 */
	mutex_lock(&delayed_node->mutex);
	if (test_bit(BTRFS_DELAYED_NODE_DEL_IREF, &delayed_node->flags))
		goto release_node;

	set_bit(BTRFS_DELAYED_NODE_DEL_IREF, &delayed_node->flags);
	delayed_node->count++;
	atomic_inc(&fs_info->delayed_root->items);
release_node:
	mutex_unlock(&delayed_node->mutex);
	btrfs_release_delayed_node(delayed_node);
	return 0;
}

static void __btrfs_kill_delayed_node(struct btrfs_delayed_node *delayed_node)
{
	struct btrfs_root *root = delayed_node->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_delayed_item *curr_item, *prev_item;

	mutex_lock(&delayed_node->mutex);
	curr_item = __btrfs_first_delayed_insertion_item(delayed_node);
	while (curr_item) {
		prev_item = curr_item;
		curr_item = __btrfs_next_delayed_item(prev_item);
		btrfs_release_delayed_item(prev_item);
	}

	if (delayed_node->index_item_leaves > 0) {
		btrfs_delayed_item_release_leaves(delayed_node,
					  delayed_node->index_item_leaves);
		delayed_node->index_item_leaves = 0;
	}

	curr_item = __btrfs_first_delayed_deletion_item(delayed_node);
	while (curr_item) {
		btrfs_delayed_item_release_metadata(root, curr_item);
		prev_item = curr_item;
		curr_item = __btrfs_next_delayed_item(prev_item);
		btrfs_release_delayed_item(prev_item);
	}

	btrfs_release_delayed_iref(delayed_node);

	if (test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_node->flags)) {
		btrfs_delayed_inode_release_metadata(fs_info, delayed_node, false);
		btrfs_release_delayed_inode(delayed_node);
	}
	mutex_unlock(&delayed_node->mutex);
}

void btrfs_kill_delayed_inode_items(struct btrfs_inode *inode)
{
	struct btrfs_delayed_node *delayed_node;

	delayed_node = btrfs_get_delayed_node(inode);
	if (!delayed_node)
		return;

	__btrfs_kill_delayed_node(delayed_node);
	btrfs_release_delayed_node(delayed_node);
}

void btrfs_kill_all_delayed_nodes(struct btrfs_root *root)
{
	u64 inode_id = 0;
	struct btrfs_delayed_node *delayed_nodes[8];
	int i, n;

	while (1) {
		spin_lock(&root->inode_lock);
		n = radix_tree_gang_lookup(&root->delayed_nodes_tree,
					   (void **)delayed_nodes, inode_id,
					   ARRAY_SIZE(delayed_nodes));
		if (!n) {
			spin_unlock(&root->inode_lock);
			break;
		}

		inode_id = delayed_nodes[n - 1]->inode_id + 1;
		for (i = 0; i < n; i++) {
			/*
			 * Don't increase refs in case the node is dead and
			 * about to be removed from the tree in the loop below
			 */
			if (!refcount_inc_not_zero(&delayed_nodes[i]->refs))
				delayed_nodes[i] = NULL;
		}
		spin_unlock(&root->inode_lock);

		for (i = 0; i < n; i++) {
			if (!delayed_nodes[i])
				continue;
			__btrfs_kill_delayed_node(delayed_nodes[i]);
			btrfs_release_delayed_node(delayed_nodes[i]);
		}
	}
}

void btrfs_destroy_delayed_inodes(struct btrfs_fs_info *fs_info)
{
	struct btrfs_delayed_node *curr_node, *prev_node;

	curr_node = btrfs_first_delayed_node(fs_info->delayed_root);
	while (curr_node) {
		__btrfs_kill_delayed_node(curr_node);

		prev_node = curr_node;
		curr_node = btrfs_next_delayed_node(curr_node);
		btrfs_release_delayed_node(prev_node);
	}
}

void btrfs_log_get_delayed_items(struct btrfs_inode *inode,
				 struct list_head *ins_list,
				 struct list_head *del_list)
{
	struct btrfs_delayed_node *node;
	struct btrfs_delayed_item *item;

	node = btrfs_get_delayed_node(inode);
	if (!node)
		return;

	mutex_lock(&node->mutex);
	item = __btrfs_first_delayed_insertion_item(node);
	while (item) {
		/*
		 * It's possible that the item is already in a log list. This
		 * can happen in case two tasks are trying to log the same
		 * directory. For example if we have tasks A and task B:
		 *
		 * Task A collected the delayed items into a log list while
		 * under the inode's log_mutex (at btrfs_log_inode()), but it
		 * only releases the items after logging the inodes they point
		 * to (if they are new inodes), which happens after unlocking
		 * the log mutex;
		 *
		 * Task B enters btrfs_log_inode() and acquires the log_mutex
		 * of the same directory inode, before task B releases the
		 * delayed items. This can happen for example when logging some
		 * inode we need to trigger logging of its parent directory, so
		 * logging two files that have the same parent directory can
		 * lead to this.
		 *
		 * If this happens, just ignore delayed items already in a log
		 * list. All the tasks logging the directory are under a log
		 * transaction and whichever finishes first can not sync the log
		 * before the other completes and leaves the log transaction.
		 */
		if (!item->logged && list_empty(&item->log_list)) {
			refcount_inc(&item->refs);
			list_add_tail(&item->log_list, ins_list);
		}
		item = __btrfs_next_delayed_item(item);
	}

	item = __btrfs_first_delayed_deletion_item(node);
	while (item) {
		/* It may be non-empty, for the same reason mentioned above. */
		if (!item->logged && list_empty(&item->log_list)) {
			refcount_inc(&item->refs);
			list_add_tail(&item->log_list, del_list);
		}
		item = __btrfs_next_delayed_item(item);
	}
	mutex_unlock(&node->mutex);

	/*
	 * We are called during inode logging, which means the inode is in use
	 * and can not be evicted before we finish logging the inode. So we never
	 * have the last reference on the delayed inode.
	 * Also, we don't use btrfs_release_delayed_node() because that would
	 * requeue the delayed inode (change its order in the list of prepared
	 * nodes) and we don't want to do such change because we don't create or
	 * delete delayed items.
	 */
	ASSERT(refcount_read(&node->refs) > 1);
	refcount_dec(&node->refs);
}

void btrfs_log_put_delayed_items(struct btrfs_inode *inode,
				 struct list_head *ins_list,
				 struct list_head *del_list)
{
	struct btrfs_delayed_node *node;
	struct btrfs_delayed_item *item;
	struct btrfs_delayed_item *next;

	node = btrfs_get_delayed_node(inode);
	if (!node)
		return;

	mutex_lock(&node->mutex);

	list_for_each_entry_safe(item, next, ins_list, log_list) {
		item->logged = true;
		list_del_init(&item->log_list);
		if (refcount_dec_and_test(&item->refs))
			kfree(item);
	}

	list_for_each_entry_safe(item, next, del_list, log_list) {
		item->logged = true;
		list_del_init(&item->log_list);
		if (refcount_dec_and_test(&item->refs))
			kfree(item);
	}

	mutex_unlock(&node->mutex);

	/*
	 * We are called during inode logging, which means the inode is in use
	 * and can not be evicted before we finish logging the inode. So we never
	 * have the last reference on the delayed inode.
	 * Also, we don't use btrfs_release_delayed_node() because that would
	 * requeue the delayed inode (change its order in the list of prepared
	 * nodes) and we don't want to do such change because we don't create or
	 * delete delayed items.
	 */
	ASSERT(refcount_read(&node->refs) > 1);
	refcount_dec(&node->refs);
}
