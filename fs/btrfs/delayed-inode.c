/*
 * Copyright (C) 2011 Fujitsu.  All rights reserved.
 * Written by Miao Xie <miaox@cn.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/slab.h>
#include "delayed-inode.h"
#include "disk-io.h"
#include "transaction.h"

#define BTRFS_DELAYED_WRITEBACK		400
#define BTRFS_DELAYED_BACKGROUND	100

static struct kmem_cache *delayed_node_cache;

int __init btrfs_delayed_inode_init(void)
{
	delayed_node_cache = kmem_cache_create("delayed_node",
					sizeof(struct btrfs_delayed_node),
					0,
					SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
					NULL);
	if (!delayed_node_cache)
		return -ENOMEM;
	return 0;
}

void btrfs_delayed_inode_exit(void)
{
	if (delayed_node_cache)
		kmem_cache_destroy(delayed_node_cache);
}

static inline void btrfs_init_delayed_node(
				struct btrfs_delayed_node *delayed_node,
				struct btrfs_root *root, u64 inode_id)
{
	delayed_node->root = root;
	delayed_node->inode_id = inode_id;
	atomic_set(&delayed_node->refs, 0);
	delayed_node->count = 0;
	delayed_node->in_list = 0;
	delayed_node->inode_dirty = 0;
	delayed_node->ins_root = RB_ROOT;
	delayed_node->del_root = RB_ROOT;
	mutex_init(&delayed_node->mutex);
	delayed_node->index_cnt = 0;
	INIT_LIST_HEAD(&delayed_node->n_list);
	INIT_LIST_HEAD(&delayed_node->p_list);
	delayed_node->bytes_reserved = 0;
}

static inline int btrfs_is_continuous_delayed_item(
					struct btrfs_delayed_item *item1,
					struct btrfs_delayed_item *item2)
{
	if (item1->key.type == BTRFS_DIR_INDEX_KEY &&
	    item1->key.objectid == item2->key.objectid &&
	    item1->key.type == item2->key.type &&
	    item1->key.offset + 1 == item2->key.offset)
		return 1;
	return 0;
}

static inline struct btrfs_delayed_root *btrfs_get_delayed_root(
							struct btrfs_root *root)
{
	return root->fs_info->delayed_root;
}

static struct btrfs_delayed_node *btrfs_get_or_create_delayed_node(
							struct inode *inode)
{
	struct btrfs_delayed_node *node;
	struct btrfs_inode *btrfs_inode = BTRFS_I(inode);
	struct btrfs_root *root = btrfs_inode->root;
	u64 ino = btrfs_ino(inode);
	int ret;

again:
	node = ACCESS_ONCE(btrfs_inode->delayed_node);
	if (node) {
		atomic_inc(&node->refs);	/* can be accessed */
		return node;
	}

	spin_lock(&root->inode_lock);
	node = radix_tree_lookup(&root->delayed_nodes_tree, ino);
	if (node) {
		if (btrfs_inode->delayed_node) {
			spin_unlock(&root->inode_lock);
			goto again;
		}
		btrfs_inode->delayed_node = node;
		atomic_inc(&node->refs);	/* can be accessed */
		atomic_inc(&node->refs);	/* cached in the inode */
		spin_unlock(&root->inode_lock);
		return node;
	}
	spin_unlock(&root->inode_lock);

	node = kmem_cache_alloc(delayed_node_cache, GFP_NOFS);
	if (!node)
		return ERR_PTR(-ENOMEM);
	btrfs_init_delayed_node(node, root, ino);

	atomic_inc(&node->refs);	/* cached in the btrfs inode */
	atomic_inc(&node->refs);	/* can be accessed */

	ret = radix_tree_preload(GFP_NOFS & ~__GFP_HIGHMEM);
	if (ret) {
		kmem_cache_free(delayed_node_cache, node);
		return ERR_PTR(ret);
	}

	spin_lock(&root->inode_lock);
	ret = radix_tree_insert(&root->delayed_nodes_tree, ino, node);
	if (ret == -EEXIST) {
		kmem_cache_free(delayed_node_cache, node);
		spin_unlock(&root->inode_lock);
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
	if (node->in_list) {
		if (!list_empty(&node->p_list))
			list_move_tail(&node->p_list, &root->prepare_list);
		else if (mod)
			list_add_tail(&node->p_list, &root->prepare_list);
	} else {
		list_add_tail(&node->n_list, &root->node_list);
		list_add_tail(&node->p_list, &root->prepare_list);
		atomic_inc(&node->refs);	/* inserted into list */
		root->nodes++;
		node->in_list = 1;
	}
	spin_unlock(&root->lock);
}

/* Call it when holding delayed_node->mutex */
static void btrfs_dequeue_delayed_node(struct btrfs_delayed_root *root,
				       struct btrfs_delayed_node *node)
{
	spin_lock(&root->lock);
	if (node->in_list) {
		root->nodes--;
		atomic_dec(&node->refs);	/* not in the list */
		list_del_init(&node->n_list);
		if (!list_empty(&node->p_list))
			list_del_init(&node->p_list);
		node->in_list = 0;
	}
	spin_unlock(&root->lock);
}

struct btrfs_delayed_node *btrfs_first_delayed_node(
			struct btrfs_delayed_root *delayed_root)
{
	struct list_head *p;
	struct btrfs_delayed_node *node = NULL;

	spin_lock(&delayed_root->lock);
	if (list_empty(&delayed_root->node_list))
		goto out;

	p = delayed_root->node_list.next;
	node = list_entry(p, struct btrfs_delayed_node, n_list);
	atomic_inc(&node->refs);
out:
	spin_unlock(&delayed_root->lock);

	return node;
}

struct btrfs_delayed_node *btrfs_next_delayed_node(
						struct btrfs_delayed_node *node)
{
	struct btrfs_delayed_root *delayed_root;
	struct list_head *p;
	struct btrfs_delayed_node *next = NULL;

	delayed_root = node->root->fs_info->delayed_root;
	spin_lock(&delayed_root->lock);
	if (!node->in_list) {	/* not in the list */
		if (list_empty(&delayed_root->node_list))
			goto out;
		p = delayed_root->node_list.next;
	} else if (list_is_last(&node->n_list, &delayed_root->node_list))
		goto out;
	else
		p = node->n_list.next;

	next = list_entry(p, struct btrfs_delayed_node, n_list);
	atomic_inc(&next->refs);
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

	if (atomic_dec_and_test(&delayed_node->refs)) {
		struct btrfs_root *root = delayed_node->root;
		spin_lock(&root->inode_lock);
		if (atomic_read(&delayed_node->refs) == 0) {
			radix_tree_delete(&root->delayed_nodes_tree,
					  delayed_node->inode_id);
			kmem_cache_free(delayed_node_cache, delayed_node);
		}
		spin_unlock(&root->inode_lock);
	}
}

static inline void btrfs_release_delayed_node(struct btrfs_delayed_node *node)
{
	__btrfs_release_delayed_node(node, 0);
}

struct btrfs_delayed_node *btrfs_first_prepared_delayed_node(
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
	atomic_inc(&node->refs);
out:
	spin_unlock(&delayed_root->lock);

	return node;
}

static inline void btrfs_release_prepared_delayed_node(
					struct btrfs_delayed_node *node)
{
	__btrfs_release_delayed_node(node, 1);
}

struct btrfs_delayed_item *btrfs_alloc_delayed_item(u32 data_len)
{
	struct btrfs_delayed_item *item;
	item = kmalloc(sizeof(*item) + data_len, GFP_NOFS);
	if (item) {
		item->data_len = data_len;
		item->ins_or_del = 0;
		item->bytes_reserved = 0;
		item->delayed_node = NULL;
		atomic_set(&item->refs, 1);
	}
	return item;
}

/*
 * __btrfs_lookup_delayed_item - look up the delayed item by key
 * @delayed_node: pointer to the delayed node
 * @key:	  the key to look up
 * @prev:	  used to store the prev item if the right item isn't found
 * @next:	  used to store the next item if the right item isn't found
 *
 * Note: if we don't find the right item, we will return the prev item and
 * the next item.
 */
static struct btrfs_delayed_item *__btrfs_lookup_delayed_item(
				struct rb_root *root,
				struct btrfs_key *key,
				struct btrfs_delayed_item **prev,
				struct btrfs_delayed_item **next)
{
	struct rb_node *node, *prev_node = NULL;
	struct btrfs_delayed_item *delayed_item = NULL;
	int ret = 0;

	node = root->rb_node;

	while (node) {
		delayed_item = rb_entry(node, struct btrfs_delayed_item,
					rb_node);
		prev_node = node;
		ret = btrfs_comp_cpu_keys(&delayed_item->key, key);
		if (ret < 0)
			node = node->rb_right;
		else if (ret > 0)
			node = node->rb_left;
		else
			return delayed_item;
	}

	if (prev) {
		if (!prev_node)
			*prev = NULL;
		else if (ret < 0)
			*prev = delayed_item;
		else if ((node = rb_prev(prev_node)) != NULL) {
			*prev = rb_entry(node, struct btrfs_delayed_item,
					 rb_node);
		} else
			*prev = NULL;
	}

	if (next) {
		if (!prev_node)
			*next = NULL;
		else if (ret > 0)
			*next = delayed_item;
		else if ((node = rb_next(prev_node)) != NULL) {
			*next = rb_entry(node, struct btrfs_delayed_item,
					 rb_node);
		} else
			*next = NULL;
	}
	return NULL;
}

struct btrfs_delayed_item *__btrfs_lookup_delayed_insertion_item(
					struct btrfs_delayed_node *delayed_node,
					struct btrfs_key *key)
{
	struct btrfs_delayed_item *item;

	item = __btrfs_lookup_delayed_item(&delayed_node->ins_root, key,
					   NULL, NULL);
	return item;
}

struct btrfs_delayed_item *__btrfs_lookup_delayed_deletion_item(
					struct btrfs_delayed_node *delayed_node,
					struct btrfs_key *key)
{
	struct btrfs_delayed_item *item;

	item = __btrfs_lookup_delayed_item(&delayed_node->del_root, key,
					   NULL, NULL);
	return item;
}

struct btrfs_delayed_item *__btrfs_search_delayed_insertion_item(
					struct btrfs_delayed_node *delayed_node,
					struct btrfs_key *key)
{
	struct btrfs_delayed_item *item, *next;

	item = __btrfs_lookup_delayed_item(&delayed_node->ins_root, key,
					   NULL, &next);
	if (!item)
		item = next;

	return item;
}

struct btrfs_delayed_item *__btrfs_search_delayed_deletion_item(
					struct btrfs_delayed_node *delayed_node,
					struct btrfs_key *key)
{
	struct btrfs_delayed_item *item, *next;

	item = __btrfs_lookup_delayed_item(&delayed_node->del_root, key,
					   NULL, &next);
	if (!item)
		item = next;

	return item;
}

static int __btrfs_add_delayed_item(struct btrfs_delayed_node *delayed_node,
				    struct btrfs_delayed_item *ins,
				    int action)
{
	struct rb_node **p, *node;
	struct rb_node *parent_node = NULL;
	struct rb_root *root;
	struct btrfs_delayed_item *item;
	int cmp;

	if (action == BTRFS_DELAYED_INSERTION_ITEM)
		root = &delayed_node->ins_root;
	else if (action == BTRFS_DELAYED_DELETION_ITEM)
		root = &delayed_node->del_root;
	else
		BUG();
	p = &root->rb_node;
	node = &ins->rb_node;

	while (*p) {
		parent_node = *p;
		item = rb_entry(parent_node, struct btrfs_delayed_item,
				 rb_node);

		cmp = btrfs_comp_cpu_keys(&item->key, &ins->key);
		if (cmp < 0)
			p = &(*p)->rb_right;
		else if (cmp > 0)
			p = &(*p)->rb_left;
		else
			return -EEXIST;
	}

	rb_link_node(node, parent_node, p);
	rb_insert_color(node, root);
	ins->delayed_node = delayed_node;
	ins->ins_or_del = action;

	if (ins->key.type == BTRFS_DIR_INDEX_KEY &&
	    action == BTRFS_DELAYED_INSERTION_ITEM &&
	    ins->key.offset >= delayed_node->index_cnt)
			delayed_node->index_cnt = ins->key.offset + 1;

	delayed_node->count++;
	atomic_inc(&delayed_node->root->fs_info->delayed_root->items);
	return 0;
}

static int __btrfs_add_delayed_insertion_item(struct btrfs_delayed_node *node,
					      struct btrfs_delayed_item *item)
{
	return __btrfs_add_delayed_item(node, item,
					BTRFS_DELAYED_INSERTION_ITEM);
}

static int __btrfs_add_delayed_deletion_item(struct btrfs_delayed_node *node,
					     struct btrfs_delayed_item *item)
{
	return __btrfs_add_delayed_item(node, item,
					BTRFS_DELAYED_DELETION_ITEM);
}

static void __btrfs_remove_delayed_item(struct btrfs_delayed_item *delayed_item)
{
	struct rb_root *root;
	struct btrfs_delayed_root *delayed_root;

	delayed_root = delayed_item->delayed_node->root->fs_info->delayed_root;

	BUG_ON(!delayed_root);
	BUG_ON(delayed_item->ins_or_del != BTRFS_DELAYED_DELETION_ITEM &&
	       delayed_item->ins_or_del != BTRFS_DELAYED_INSERTION_ITEM);

	if (delayed_item->ins_or_del == BTRFS_DELAYED_INSERTION_ITEM)
		root = &delayed_item->delayed_node->ins_root;
	else
		root = &delayed_item->delayed_node->del_root;

	rb_erase(&delayed_item->rb_node, root);
	delayed_item->delayed_node->count--;
	atomic_dec(&delayed_root->items);
	if (atomic_read(&delayed_root->items) < BTRFS_DELAYED_BACKGROUND &&
	    waitqueue_active(&delayed_root->wait))
		wake_up(&delayed_root->wait);
}

static void btrfs_release_delayed_item(struct btrfs_delayed_item *item)
{
	if (item) {
		__btrfs_remove_delayed_item(item);
		if (atomic_dec_and_test(&item->refs))
			kfree(item);
	}
}

struct btrfs_delayed_item *__btrfs_first_delayed_insertion_item(
					struct btrfs_delayed_node *delayed_node)
{
	struct rb_node *p;
	struct btrfs_delayed_item *item = NULL;

	p = rb_first(&delayed_node->ins_root);
	if (p)
		item = rb_entry(p, struct btrfs_delayed_item, rb_node);

	return item;
}

struct btrfs_delayed_item *__btrfs_first_delayed_deletion_item(
					struct btrfs_delayed_node *delayed_node)
{
	struct rb_node *p;
	struct btrfs_delayed_item *item = NULL;

	p = rb_first(&delayed_node->del_root);
	if (p)
		item = rb_entry(p, struct btrfs_delayed_item, rb_node);

	return item;
}

struct btrfs_delayed_item *__btrfs_next_delayed_item(
						struct btrfs_delayed_item *item)
{
	struct rb_node *p;
	struct btrfs_delayed_item *next = NULL;

	p = rb_next(&item->rb_node);
	if (p)
		next = rb_entry(p, struct btrfs_delayed_item, rb_node);

	return next;
}

static inline struct btrfs_delayed_node *btrfs_get_delayed_node(
							struct inode *inode)
{
	struct btrfs_inode *btrfs_inode = BTRFS_I(inode);
	struct btrfs_delayed_node *delayed_node;

	delayed_node = btrfs_inode->delayed_node;
	if (delayed_node)
		atomic_inc(&delayed_node->refs);

	return delayed_node;
}

static inline struct btrfs_root *btrfs_get_fs_root(struct btrfs_root *root,
						   u64 root_id)
{
	struct btrfs_key root_key;

	if (root->objectid == root_id)
		return root;

	root_key.objectid = root_id;
	root_key.type = BTRFS_ROOT_ITEM_KEY;
	root_key.offset = (u64)-1;
	return btrfs_read_fs_root_no_name(root->fs_info, &root_key);
}

static int btrfs_delayed_item_reserve_metadata(struct btrfs_trans_handle *trans,
					       struct btrfs_root *root,
					       struct btrfs_delayed_item *item)
{
	struct btrfs_block_rsv *src_rsv;
	struct btrfs_block_rsv *dst_rsv;
	u64 num_bytes;
	int ret;

	if (!trans->bytes_reserved)
		return 0;

	src_rsv = trans->block_rsv;
	dst_rsv = &root->fs_info->global_block_rsv;

	num_bytes = btrfs_calc_trans_metadata_size(root, 1);
	ret = btrfs_block_rsv_migrate(src_rsv, dst_rsv, num_bytes);
	if (!ret)
		item->bytes_reserved = num_bytes;

	return ret;
}

static void btrfs_delayed_item_release_metadata(struct btrfs_root *root,
						struct btrfs_delayed_item *item)
{
	struct btrfs_block_rsv *rsv;

	if (!item->bytes_reserved)
		return;

	rsv = &root->fs_info->global_block_rsv;
	btrfs_block_rsv_release(root, rsv,
				item->bytes_reserved);
}

static int btrfs_delayed_inode_reserve_metadata(
					struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_delayed_node *node)
{
	struct btrfs_block_rsv *src_rsv;
	struct btrfs_block_rsv *dst_rsv;
	u64 num_bytes;
	int ret;

	if (!trans->bytes_reserved)
		return 0;

	src_rsv = trans->block_rsv;
	dst_rsv = &root->fs_info->global_block_rsv;

	num_bytes = btrfs_calc_trans_metadata_size(root, 1);
	ret = btrfs_block_rsv_migrate(src_rsv, dst_rsv, num_bytes);
	if (!ret)
		node->bytes_reserved = num_bytes;

	return ret;
}

static void btrfs_delayed_inode_release_metadata(struct btrfs_root *root,
						struct btrfs_delayed_node *node)
{
	struct btrfs_block_rsv *rsv;

	if (!node->bytes_reserved)
		return;

	rsv = &root->fs_info->global_block_rsv;
	btrfs_block_rsv_release(root, rsv,
				node->bytes_reserved);
	node->bytes_reserved = 0;
}

/*
 * This helper will insert some continuous items into the same leaf according
 * to the free space of the leaf.
 */
static int btrfs_batch_insert_items(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_path *path,
				struct btrfs_delayed_item *item)
{
	struct btrfs_delayed_item *curr, *next;
	int free_space;
	int total_data_size = 0, total_size = 0;
	struct extent_buffer *leaf;
	char *data_ptr;
	struct btrfs_key *keys;
	u32 *data_size;
	struct list_head head;
	int slot;
	int nitems;
	int i;
	int ret = 0;

	BUG_ON(!path->nodes[0]);

	leaf = path->nodes[0];
	free_space = btrfs_leaf_free_space(root, leaf);
	INIT_LIST_HEAD(&head);

	next = item;
	nitems = 0;

	/*
	 * count the number of the continuous items that we can insert in batch
	 */
	while (total_size + next->data_len + sizeof(struct btrfs_item) <=
	       free_space) {
		total_data_size += next->data_len;
		total_size += next->data_len + sizeof(struct btrfs_item);
		list_add_tail(&next->tree_list, &head);
		nitems++;

		curr = next;
		next = __btrfs_next_delayed_item(curr);
		if (!next)
			break;

		if (!btrfs_is_continuous_delayed_item(curr, next))
			break;
	}

	if (!nitems) {
		ret = 0;
		goto out;
	}

	/*
	 * we need allocate some memory space, but it might cause the task
	 * to sleep, so we set all locked nodes in the path to blocking locks
	 * first.
	 */
	btrfs_set_path_blocking(path);

	keys = kmalloc(sizeof(struct btrfs_key) * nitems, GFP_NOFS);
	if (!keys) {
		ret = -ENOMEM;
		goto out;
	}

	data_size = kmalloc(sizeof(u32) * nitems, GFP_NOFS);
	if (!data_size) {
		ret = -ENOMEM;
		goto error;
	}

	/* get keys of all the delayed items */
	i = 0;
	list_for_each_entry(next, &head, tree_list) {
		keys[i] = next->key;
		data_size[i] = next->data_len;
		i++;
	}

	/* reset all the locked nodes in the patch to spinning locks. */
	btrfs_clear_path_blocking(path, NULL);

	/* insert the keys of the items */
	ret = setup_items_for_insert(trans, root, path, keys, data_size,
				     total_data_size, total_size, nitems);
	if (ret)
		goto error;

	/* insert the dir index items */
	slot = path->slots[0];
	list_for_each_entry_safe(curr, next, &head, tree_list) {
		data_ptr = btrfs_item_ptr(leaf, slot, char);
		write_extent_buffer(leaf, &curr->data,
				    (unsigned long)data_ptr,
				    curr->data_len);
		slot++;

		btrfs_delayed_item_release_metadata(root, curr);

		list_del(&curr->tree_list);
		btrfs_release_delayed_item(curr);
	}

error:
	kfree(data_size);
	kfree(keys);
out:
	return ret;
}

/*
 * This helper can just do simple insertion that needn't extend item for new
 * data, such as directory name index insertion, inode insertion.
 */
static int btrfs_insert_delayed_item(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct btrfs_path *path,
				     struct btrfs_delayed_item *delayed_item)
{
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	char *ptr;
	int ret;

	ret = btrfs_insert_empty_item(trans, root, path, &delayed_item->key,
				      delayed_item->data_len);
	if (ret < 0 && ret != -EEXIST)
		return ret;

	leaf = path->nodes[0];

	item = btrfs_item_nr(leaf, path->slots[0]);
	ptr = btrfs_item_ptr(leaf, path->slots[0], char);

	write_extent_buffer(leaf, delayed_item->data, (unsigned long)ptr,
			    delayed_item->data_len);
	btrfs_mark_buffer_dirty(leaf);

	btrfs_delayed_item_release_metadata(root, delayed_item);
	return 0;
}

/*
 * we insert an item first, then if there are some continuous items, we try
 * to insert those items into the same leaf.
 */
static int btrfs_insert_delayed_items(struct btrfs_trans_handle *trans,
				      struct btrfs_path *path,
				      struct btrfs_root *root,
				      struct btrfs_delayed_node *node)
{
	struct btrfs_delayed_item *curr, *prev;
	int ret = 0;

do_again:
	mutex_lock(&node->mutex);
	curr = __btrfs_first_delayed_insertion_item(node);
	if (!curr)
		goto insert_end;

	ret = btrfs_insert_delayed_item(trans, root, path, curr);
	if (ret < 0) {
		btrfs_release_path(path);
		goto insert_end;
	}

	prev = curr;
	curr = __btrfs_next_delayed_item(prev);
	if (curr && btrfs_is_continuous_delayed_item(prev, curr)) {
		/* insert the continuous items into the same leaf */
		path->slots[0]++;
		btrfs_batch_insert_items(trans, root, path, curr);
	}
	btrfs_release_delayed_item(prev);
	btrfs_mark_buffer_dirty(path->nodes[0]);

	btrfs_release_path(path);
	mutex_unlock(&node->mutex);
	goto do_again;

insert_end:
	mutex_unlock(&node->mutex);
	return ret;
}

static int btrfs_batch_delete_items(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root,
				    struct btrfs_path *path,
				    struct btrfs_delayed_item *item)
{
	struct btrfs_delayed_item *curr, *next;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct list_head head;
	int nitems, i, last_item;
	int ret = 0;

	BUG_ON(!path->nodes[0]);

	leaf = path->nodes[0];

	i = path->slots[0];
	last_item = btrfs_header_nritems(leaf) - 1;
	if (i > last_item)
		return -ENOENT;	/* FIXME: Is errno suitable? */

	next = item;
	INIT_LIST_HEAD(&head);
	btrfs_item_key_to_cpu(leaf, &key, i);
	nitems = 0;
	/*
	 * count the number of the dir index items that we can delete in batch
	 */
	while (btrfs_comp_cpu_keys(&next->key, &key) == 0) {
		list_add_tail(&next->tree_list, &head);
		nitems++;

		curr = next;
		next = __btrfs_next_delayed_item(curr);
		if (!next)
			break;

		if (!btrfs_is_continuous_delayed_item(curr, next))
			break;

		i++;
		if (i > last_item)
			break;
		btrfs_item_key_to_cpu(leaf, &key, i);
	}

	if (!nitems)
		return 0;

	ret = btrfs_del_items(trans, root, path, path->slots[0], nitems);
	if (ret)
		goto out;

	list_for_each_entry_safe(curr, next, &head, tree_list) {
		btrfs_delayed_item_release_metadata(root, curr);
		list_del(&curr->tree_list);
		btrfs_release_delayed_item(curr);
	}

out:
	return ret;
}

static int btrfs_delete_delayed_items(struct btrfs_trans_handle *trans,
				      struct btrfs_path *path,
				      struct btrfs_root *root,
				      struct btrfs_delayed_node *node)
{
	struct btrfs_delayed_item *curr, *prev;
	int ret = 0;

do_again:
	mutex_lock(&node->mutex);
	curr = __btrfs_first_delayed_deletion_item(node);
	if (!curr)
		goto delete_fail;

	ret = btrfs_search_slot(trans, root, &curr->key, path, -1, 1);
	if (ret < 0)
		goto delete_fail;
	else if (ret > 0) {
		/*
		 * can't find the item which the node points to, so this node
		 * is invalid, just drop it.
		 */
		prev = curr;
		curr = __btrfs_next_delayed_item(prev);
		btrfs_release_delayed_item(prev);
		ret = 0;
		btrfs_release_path(path);
		if (curr)
			goto do_again;
		else
			goto delete_fail;
	}

	btrfs_batch_delete_items(trans, root, path, curr);
	btrfs_release_path(path);
	mutex_unlock(&node->mutex);
	goto do_again;

delete_fail:
	btrfs_release_path(path);
	mutex_unlock(&node->mutex);
	return ret;
}

static void btrfs_release_delayed_inode(struct btrfs_delayed_node *delayed_node)
{
	struct btrfs_delayed_root *delayed_root;

	if (delayed_node && delayed_node->inode_dirty) {
		BUG_ON(!delayed_node->root);
		delayed_node->inode_dirty = 0;
		delayed_node->count--;

		delayed_root = delayed_node->root->fs_info->delayed_root;
		atomic_dec(&delayed_root->items);
		if (atomic_read(&delayed_root->items) <
		    BTRFS_DELAYED_BACKGROUND &&
		    waitqueue_active(&delayed_root->wait))
			wake_up(&delayed_root->wait);
	}
}

static int btrfs_update_delayed_inode(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      struct btrfs_path *path,
				      struct btrfs_delayed_node *node)
{
	struct btrfs_key key;
	struct btrfs_inode_item *inode_item;
	struct extent_buffer *leaf;
	int ret;

	mutex_lock(&node->mutex);
	if (!node->inode_dirty) {
		mutex_unlock(&node->mutex);
		return 0;
	}

	key.objectid = node->inode_id;
	btrfs_set_key_type(&key, BTRFS_INODE_ITEM_KEY);
	key.offset = 0;
	ret = btrfs_lookup_inode(trans, root, path, &key, 1);
	if (ret > 0) {
		btrfs_release_path(path);
		mutex_unlock(&node->mutex);
		return -ENOENT;
	} else if (ret < 0) {
		mutex_unlock(&node->mutex);
		return ret;
	}

	btrfs_unlock_up_safe(path, 1);
	leaf = path->nodes[0];
	inode_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_inode_item);
	write_extent_buffer(leaf, &node->inode_item, (unsigned long)inode_item,
			    sizeof(struct btrfs_inode_item));
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	btrfs_delayed_inode_release_metadata(root, node);
	btrfs_release_delayed_inode(node);
	mutex_unlock(&node->mutex);

	return 0;
}

/* Called when committing the transaction. */
int btrfs_run_delayed_items(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root)
{
	struct btrfs_delayed_root *delayed_root;
	struct btrfs_delayed_node *curr_node, *prev_node;
	struct btrfs_path *path;
	struct btrfs_block_rsv *block_rsv;
	int ret = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->leave_spinning = 1;

	block_rsv = trans->block_rsv;
	trans->block_rsv = &root->fs_info->global_block_rsv;

	delayed_root = btrfs_get_delayed_root(root);

	curr_node = btrfs_first_delayed_node(delayed_root);
	while (curr_node) {
		root = curr_node->root;
		ret = btrfs_insert_delayed_items(trans, path, root,
						 curr_node);
		if (!ret)
			ret = btrfs_delete_delayed_items(trans, path, root,
							 curr_node);
		if (!ret)
			ret = btrfs_update_delayed_inode(trans, root, path,
							 curr_node);
		if (ret) {
			btrfs_release_delayed_node(curr_node);
			break;
		}

		prev_node = curr_node;
		curr_node = btrfs_next_delayed_node(curr_node);
		btrfs_release_delayed_node(prev_node);
	}

	btrfs_free_path(path);
	trans->block_rsv = block_rsv;
	return ret;
}

static int __btrfs_commit_inode_delayed_items(struct btrfs_trans_handle *trans,
					      struct btrfs_delayed_node *node)
{
	struct btrfs_path *path;
	struct btrfs_block_rsv *block_rsv;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->leave_spinning = 1;

	block_rsv = trans->block_rsv;
	trans->block_rsv = &node->root->fs_info->global_block_rsv;

	ret = btrfs_insert_delayed_items(trans, path, node->root, node);
	if (!ret)
		ret = btrfs_delete_delayed_items(trans, path, node->root, node);
	if (!ret)
		ret = btrfs_update_delayed_inode(trans, node->root, path, node);
	btrfs_free_path(path);

	trans->block_rsv = block_rsv;
	return ret;
}

int btrfs_commit_inode_delayed_items(struct btrfs_trans_handle *trans,
				     struct inode *inode)
{
	struct btrfs_delayed_node *delayed_node = btrfs_get_delayed_node(inode);
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

	ret = __btrfs_commit_inode_delayed_items(trans, delayed_node);
	btrfs_release_delayed_node(delayed_node);
	return ret;
}

void btrfs_remove_delayed_node(struct inode *inode)
{
	struct btrfs_delayed_node *delayed_node;

	delayed_node = ACCESS_ONCE(BTRFS_I(inode)->delayed_node);
	if (!delayed_node)
		return;

	BTRFS_I(inode)->delayed_node = NULL;
	btrfs_release_delayed_node(delayed_node);
}

struct btrfs_async_delayed_node {
	struct btrfs_root *root;
	struct btrfs_delayed_node *delayed_node;
	struct btrfs_work work;
};

static void btrfs_async_run_delayed_node_done(struct btrfs_work *work)
{
	struct btrfs_async_delayed_node *async_node;
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path;
	struct btrfs_delayed_node *delayed_node = NULL;
	struct btrfs_root *root;
	struct btrfs_block_rsv *block_rsv;
	unsigned long nr = 0;
	int need_requeue = 0;
	int ret;

	async_node = container_of(work, struct btrfs_async_delayed_node, work);

	path = btrfs_alloc_path();
	if (!path)
		goto out;
	path->leave_spinning = 1;

	delayed_node = async_node->delayed_node;
	root = delayed_node->root;

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans))
		goto free_path;

	block_rsv = trans->block_rsv;
	trans->block_rsv = &root->fs_info->global_block_rsv;

	ret = btrfs_insert_delayed_items(trans, path, root, delayed_node);
	if (!ret)
		ret = btrfs_delete_delayed_items(trans, path, root,
						 delayed_node);

	if (!ret)
		btrfs_update_delayed_inode(trans, root, path, delayed_node);

	/*
	 * Maybe new delayed items have been inserted, so we need requeue
	 * the work. Besides that, we must dequeue the empty delayed nodes
	 * to avoid the race between delayed items balance and the worker.
	 * The race like this:
	 * 	Task1				Worker thread
	 * 					count == 0, needn't requeue
	 * 					  also needn't insert the
	 * 					  delayed node into prepare
	 * 					  list again.
	 * 	add lots of delayed items
	 * 	queue the delayed node
	 * 	  already in the list,
	 * 	  and not in the prepare
	 * 	  list, it means the delayed
	 * 	  node is being dealt with
	 * 	  by the worker.
	 * 	do delayed items balance
	 * 	  the delayed node is being
	 * 	  dealt with by the worker
	 * 	  now, just wait.
	 * 	  				the worker goto idle.
	 * Task1 will sleep until the transaction is commited.
	 */
	mutex_lock(&delayed_node->mutex);
	if (delayed_node->count)
		need_requeue = 1;
	else
		btrfs_dequeue_delayed_node(root->fs_info->delayed_root,
					   delayed_node);
	mutex_unlock(&delayed_node->mutex);

	nr = trans->blocks_used;

	trans->block_rsv = block_rsv;
	btrfs_end_transaction_dmeta(trans, root);
	__btrfs_btree_balance_dirty(root, nr);
free_path:
	btrfs_free_path(path);
out:
	if (need_requeue)
		btrfs_requeue_work(&async_node->work);
	else {
		btrfs_release_prepared_delayed_node(delayed_node);
		kfree(async_node);
	}
}

static int btrfs_wq_run_delayed_node(struct btrfs_delayed_root *delayed_root,
				     struct btrfs_root *root, int all)
{
	struct btrfs_async_delayed_node *async_node;
	struct btrfs_delayed_node *curr;
	int count = 0;

again:
	curr = btrfs_first_prepared_delayed_node(delayed_root);
	if (!curr)
		return 0;

	async_node = kmalloc(sizeof(*async_node), GFP_NOFS);
	if (!async_node) {
		btrfs_release_prepared_delayed_node(curr);
		return -ENOMEM;
	}

	async_node->root = root;
	async_node->delayed_node = curr;

	async_node->work.func = btrfs_async_run_delayed_node_done;
	async_node->work.flags = 0;

	btrfs_queue_worker(&root->fs_info->delayed_workers, &async_node->work);
	count++;

	if (all || count < 4)
		goto again;

	return 0;
}

void btrfs_assert_delayed_root_empty(struct btrfs_root *root)
{
	struct btrfs_delayed_root *delayed_root;
	delayed_root = btrfs_get_delayed_root(root);
	WARN_ON(btrfs_first_delayed_node(delayed_root));
}

void btrfs_balance_delayed_items(struct btrfs_root *root)
{
	struct btrfs_delayed_root *delayed_root;

	delayed_root = btrfs_get_delayed_root(root);

	if (atomic_read(&delayed_root->items) < BTRFS_DELAYED_BACKGROUND)
		return;

	if (atomic_read(&delayed_root->items) >= BTRFS_DELAYED_WRITEBACK) {
		int ret;
		ret = btrfs_wq_run_delayed_node(delayed_root, root, 1);
		if (ret)
			return;

		wait_event_interruptible_timeout(
				delayed_root->wait,
				(atomic_read(&delayed_root->items) <
				 BTRFS_DELAYED_BACKGROUND),
				HZ);
		return;
	}

	btrfs_wq_run_delayed_node(delayed_root, root, 0);
}

int btrfs_insert_delayed_dir_index(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root, const char *name,
				   int name_len, struct inode *dir,
				   struct btrfs_disk_key *disk_key, u8 type,
				   u64 index)
{
	struct btrfs_delayed_node *delayed_node;
	struct btrfs_delayed_item *delayed_item;
	struct btrfs_dir_item *dir_item;
	int ret;

	delayed_node = btrfs_get_or_create_delayed_node(dir);
	if (IS_ERR(delayed_node))
		return PTR_ERR(delayed_node);

	delayed_item = btrfs_alloc_delayed_item(sizeof(*dir_item) + name_len);
	if (!delayed_item) {
		ret = -ENOMEM;
		goto release_node;
	}

	ret = btrfs_delayed_item_reserve_metadata(trans, root, delayed_item);
	/*
	 * we have reserved enough space when we start a new transaction,
	 * so reserving metadata failure is impossible
	 */
	BUG_ON(ret);

	delayed_item->key.objectid = btrfs_ino(dir);
	btrfs_set_key_type(&delayed_item->key, BTRFS_DIR_INDEX_KEY);
	delayed_item->key.offset = index;

	dir_item = (struct btrfs_dir_item *)delayed_item->data;
	dir_item->location = *disk_key;
	dir_item->transid = cpu_to_le64(trans->transid);
	dir_item->data_len = 0;
	dir_item->name_len = cpu_to_le16(name_len);
	dir_item->type = type;
	memcpy((char *)(dir_item + 1), name, name_len);

	mutex_lock(&delayed_node->mutex);
	ret = __btrfs_add_delayed_insertion_item(delayed_node, delayed_item);
	if (unlikely(ret)) {
		printk(KERN_ERR "err add delayed dir index item(name: %s) into "
				"the insertion tree of the delayed node"
				"(root id: %llu, inode id: %llu, errno: %d)\n",
				name,
				(unsigned long long)delayed_node->root->objectid,
				(unsigned long long)delayed_node->inode_id,
				ret);
		BUG();
	}
	mutex_unlock(&delayed_node->mutex);

release_node:
	btrfs_release_delayed_node(delayed_node);
	return ret;
}

static int btrfs_delete_delayed_insertion_item(struct btrfs_root *root,
					       struct btrfs_delayed_node *node,
					       struct btrfs_key *key)
{
	struct btrfs_delayed_item *item;

	mutex_lock(&node->mutex);
	item = __btrfs_lookup_delayed_insertion_item(node, key);
	if (!item) {
		mutex_unlock(&node->mutex);
		return 1;
	}

	btrfs_delayed_item_release_metadata(root, item);
	btrfs_release_delayed_item(item);
	mutex_unlock(&node->mutex);
	return 0;
}

int btrfs_delete_delayed_dir_index(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root, struct inode *dir,
				   u64 index)
{
	struct btrfs_delayed_node *node;
	struct btrfs_delayed_item *item;
	struct btrfs_key item_key;
	int ret;

	node = btrfs_get_or_create_delayed_node(dir);
	if (IS_ERR(node))
		return PTR_ERR(node);

	item_key.objectid = btrfs_ino(dir);
	btrfs_set_key_type(&item_key, BTRFS_DIR_INDEX_KEY);
	item_key.offset = index;

	ret = btrfs_delete_delayed_insertion_item(root, node, &item_key);
	if (!ret)
		goto end;

	item = btrfs_alloc_delayed_item(0);
	if (!item) {
		ret = -ENOMEM;
		goto end;
	}

	item->key = item_key;

	ret = btrfs_delayed_item_reserve_metadata(trans, root, item);
	/*
	 * we have reserved enough space when we start a new transaction,
	 * so reserving metadata failure is impossible.
	 */
	BUG_ON(ret);

	mutex_lock(&node->mutex);
	ret = __btrfs_add_delayed_deletion_item(node, item);
	if (unlikely(ret)) {
		printk(KERN_ERR "err add delayed dir index item(index: %llu) "
				"into the deletion tree of the delayed node"
				"(root id: %llu, inode id: %llu, errno: %d)\n",
				(unsigned long long)index,
				(unsigned long long)node->root->objectid,
				(unsigned long long)node->inode_id,
				ret);
		BUG();
	}
	mutex_unlock(&node->mutex);
end:
	btrfs_release_delayed_node(node);
	return ret;
}

int btrfs_inode_delayed_dir_index_count(struct inode *inode)
{
	struct btrfs_delayed_node *delayed_node = BTRFS_I(inode)->delayed_node;
	int ret = 0;

	if (!delayed_node)
		return -ENOENT;

	/*
	 * Since we have held i_mutex of this directory, it is impossible that
	 * a new directory index is added into the delayed node and index_cnt
	 * is updated now. So we needn't lock the delayed node.
	 */
	if (!delayed_node->index_cnt)
		return -EINVAL;

	BTRFS_I(inode)->index_cnt = delayed_node->index_cnt;
	return ret;
}

void btrfs_get_delayed_items(struct inode *inode, struct list_head *ins_list,
			     struct list_head *del_list)
{
	struct btrfs_delayed_node *delayed_node;
	struct btrfs_delayed_item *item;

	delayed_node = btrfs_get_delayed_node(inode);
	if (!delayed_node)
		return;

	mutex_lock(&delayed_node->mutex);
	item = __btrfs_first_delayed_insertion_item(delayed_node);
	while (item) {
		atomic_inc(&item->refs);
		list_add_tail(&item->readdir_list, ins_list);
		item = __btrfs_next_delayed_item(item);
	}

	item = __btrfs_first_delayed_deletion_item(delayed_node);
	while (item) {
		atomic_inc(&item->refs);
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
	atomic_dec(&delayed_node->refs);
}

void btrfs_put_delayed_items(struct list_head *ins_list,
			     struct list_head *del_list)
{
	struct btrfs_delayed_item *curr, *next;

	list_for_each_entry_safe(curr, next, ins_list, readdir_list) {
		list_del(&curr->readdir_list);
		if (atomic_dec_and_test(&curr->refs))
			kfree(curr);
	}

	list_for_each_entry_safe(curr, next, del_list, readdir_list) {
		list_del(&curr->readdir_list);
		if (atomic_dec_and_test(&curr->refs))
			kfree(curr);
	}
}

int btrfs_should_delete_dir_index(struct list_head *del_list,
				  u64 index)
{
	struct btrfs_delayed_item *curr, *next;
	int ret;

	if (list_empty(del_list))
		return 0;

	list_for_each_entry_safe(curr, next, del_list, readdir_list) {
		if (curr->key.offset > index)
			break;

		list_del(&curr->readdir_list);
		ret = (curr->key.offset == index);

		if (atomic_dec_and_test(&curr->refs))
			kfree(curr);

		if (ret)
			return 1;
		else
			continue;
	}
	return 0;
}

/*
 * btrfs_readdir_delayed_dir_index - read dir info stored in the delayed tree
 *
 */
int btrfs_readdir_delayed_dir_index(struct file *filp, void *dirent,
				    filldir_t filldir,
				    struct list_head *ins_list)
{
	struct btrfs_dir_item *di;
	struct btrfs_delayed_item *curr, *next;
	struct btrfs_key location;
	char *name;
	int name_len;
	int over = 0;
	unsigned char d_type;

	if (list_empty(ins_list))
		return 0;

	/*
	 * Changing the data of the delayed item is impossible. So
	 * we needn't lock them. And we have held i_mutex of the
	 * directory, nobody can delete any directory indexes now.
	 */
	list_for_each_entry_safe(curr, next, ins_list, readdir_list) {
		list_del(&curr->readdir_list);

		if (curr->key.offset < filp->f_pos) {
			if (atomic_dec_and_test(&curr->refs))
				kfree(curr);
			continue;
		}

		filp->f_pos = curr->key.offset;

		di = (struct btrfs_dir_item *)curr->data;
		name = (char *)(di + 1);
		name_len = le16_to_cpu(di->name_len);

		d_type = btrfs_filetype_table[di->type];
		btrfs_disk_key_to_cpu(&location, &di->location);

		over = filldir(dirent, name, name_len, curr->key.offset,
			       location.objectid, d_type);

		if (atomic_dec_and_test(&curr->refs))
			kfree(curr);

		if (over)
			return 1;
	}
	return 0;
}

BTRFS_SETGET_STACK_FUNCS(stack_inode_generation, struct btrfs_inode_item,
			 generation, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_sequence, struct btrfs_inode_item,
			 sequence, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_transid, struct btrfs_inode_item,
			 transid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_size, struct btrfs_inode_item, size, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_nbytes, struct btrfs_inode_item,
			 nbytes, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_block_group, struct btrfs_inode_item,
			 block_group, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_nlink, struct btrfs_inode_item, nlink, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_uid, struct btrfs_inode_item, uid, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_gid, struct btrfs_inode_item, gid, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_mode, struct btrfs_inode_item, mode, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_rdev, struct btrfs_inode_item, rdev, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_flags, struct btrfs_inode_item, flags, 64);

BTRFS_SETGET_STACK_FUNCS(stack_timespec_sec, struct btrfs_timespec, sec, 64);
BTRFS_SETGET_STACK_FUNCS(stack_timespec_nsec, struct btrfs_timespec, nsec, 32);

static void fill_stack_inode_item(struct btrfs_trans_handle *trans,
				  struct btrfs_inode_item *inode_item,
				  struct inode *inode)
{
	btrfs_set_stack_inode_uid(inode_item, inode->i_uid);
	btrfs_set_stack_inode_gid(inode_item, inode->i_gid);
	btrfs_set_stack_inode_size(inode_item, BTRFS_I(inode)->disk_i_size);
	btrfs_set_stack_inode_mode(inode_item, inode->i_mode);
	btrfs_set_stack_inode_nlink(inode_item, inode->i_nlink);
	btrfs_set_stack_inode_nbytes(inode_item, inode_get_bytes(inode));
	btrfs_set_stack_inode_generation(inode_item,
					 BTRFS_I(inode)->generation);
	btrfs_set_stack_inode_sequence(inode_item, BTRFS_I(inode)->sequence);
	btrfs_set_stack_inode_transid(inode_item, trans->transid);
	btrfs_set_stack_inode_rdev(inode_item, inode->i_rdev);
	btrfs_set_stack_inode_flags(inode_item, BTRFS_I(inode)->flags);
	btrfs_set_stack_inode_block_group(inode_item, 0);

	btrfs_set_stack_timespec_sec(btrfs_inode_atime(inode_item),
				     inode->i_atime.tv_sec);
	btrfs_set_stack_timespec_nsec(btrfs_inode_atime(inode_item),
				      inode->i_atime.tv_nsec);

	btrfs_set_stack_timespec_sec(btrfs_inode_mtime(inode_item),
				     inode->i_mtime.tv_sec);
	btrfs_set_stack_timespec_nsec(btrfs_inode_mtime(inode_item),
				      inode->i_mtime.tv_nsec);

	btrfs_set_stack_timespec_sec(btrfs_inode_ctime(inode_item),
				     inode->i_ctime.tv_sec);
	btrfs_set_stack_timespec_nsec(btrfs_inode_ctime(inode_item),
				      inode->i_ctime.tv_nsec);
}

int btrfs_delayed_update_inode(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root, struct inode *inode)
{
	struct btrfs_delayed_node *delayed_node;
	int ret = 0;

	delayed_node = btrfs_get_or_create_delayed_node(inode);
	if (IS_ERR(delayed_node))
		return PTR_ERR(delayed_node);

	mutex_lock(&delayed_node->mutex);
	if (delayed_node->inode_dirty) {
		fill_stack_inode_item(trans, &delayed_node->inode_item, inode);
		goto release_node;
	}

	ret = btrfs_delayed_inode_reserve_metadata(trans, root, delayed_node);
	/*
	 * we must reserve enough space when we start a new transaction,
	 * so reserving metadata failure is impossible
	 */
	BUG_ON(ret);

	fill_stack_inode_item(trans, &delayed_node->inode_item, inode);
	delayed_node->inode_dirty = 1;
	delayed_node->count++;
	atomic_inc(&root->fs_info->delayed_root->items);
release_node:
	mutex_unlock(&delayed_node->mutex);
	btrfs_release_delayed_node(delayed_node);
	return ret;
}

static void __btrfs_kill_delayed_node(struct btrfs_delayed_node *delayed_node)
{
	struct btrfs_root *root = delayed_node->root;
	struct btrfs_delayed_item *curr_item, *prev_item;

	mutex_lock(&delayed_node->mutex);
	curr_item = __btrfs_first_delayed_insertion_item(delayed_node);
	while (curr_item) {
		btrfs_delayed_item_release_metadata(root, curr_item);
		prev_item = curr_item;
		curr_item = __btrfs_next_delayed_item(prev_item);
		btrfs_release_delayed_item(prev_item);
	}

	curr_item = __btrfs_first_delayed_deletion_item(delayed_node);
	while (curr_item) {
		btrfs_delayed_item_release_metadata(root, curr_item);
		prev_item = curr_item;
		curr_item = __btrfs_next_delayed_item(prev_item);
		btrfs_release_delayed_item(prev_item);
	}

	if (delayed_node->inode_dirty) {
		btrfs_delayed_inode_release_metadata(root, delayed_node);
		btrfs_release_delayed_inode(delayed_node);
	}
	mutex_unlock(&delayed_node->mutex);
}

void btrfs_kill_delayed_inode_items(struct inode *inode)
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

		for (i = 0; i < n; i++)
			atomic_inc(&delayed_nodes[i]->refs);
		spin_unlock(&root->inode_lock);

		for (i = 0; i < n; i++) {
			__btrfs_kill_delayed_node(delayed_nodes[i]);
			btrfs_release_delayed_node(delayed_nodes[i]);
		}
	}
}
