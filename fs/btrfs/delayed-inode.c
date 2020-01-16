// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 Fujitsu.  All rights reserved.
 * Written by Miao Xie <miaox@cn.fujitsu.com>
 */

#include <linux/slab.h>
#include <linux/iversion.h>
#include "misc.h"
#include "delayed-iyesde.h"
#include "disk-io.h"
#include "transaction.h"
#include "ctree.h"
#include "qgroup.h"
#include "locking.h"

#define BTRFS_DELAYED_WRITEBACK		512
#define BTRFS_DELAYED_BACKGROUND	128
#define BTRFS_DELAYED_BATCH		16

static struct kmem_cache *delayed_yesde_cache;

int __init btrfs_delayed_iyesde_init(void)
{
	delayed_yesde_cache = kmem_cache_create("btrfs_delayed_yesde",
					sizeof(struct btrfs_delayed_yesde),
					0,
					SLAB_MEM_SPREAD,
					NULL);
	if (!delayed_yesde_cache)
		return -ENOMEM;
	return 0;
}

void __cold btrfs_delayed_iyesde_exit(void)
{
	kmem_cache_destroy(delayed_yesde_cache);
}

static inline void btrfs_init_delayed_yesde(
				struct btrfs_delayed_yesde *delayed_yesde,
				struct btrfs_root *root, u64 iyesde_id)
{
	delayed_yesde->root = root;
	delayed_yesde->iyesde_id = iyesde_id;
	refcount_set(&delayed_yesde->refs, 0);
	delayed_yesde->ins_root = RB_ROOT_CACHED;
	delayed_yesde->del_root = RB_ROOT_CACHED;
	mutex_init(&delayed_yesde->mutex);
	INIT_LIST_HEAD(&delayed_yesde->n_list);
	INIT_LIST_HEAD(&delayed_yesde->p_list);
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

static struct btrfs_delayed_yesde *btrfs_get_delayed_yesde(
		struct btrfs_iyesde *btrfs_iyesde)
{
	struct btrfs_root *root = btrfs_iyesde->root;
	u64 iyes = btrfs_iyes(btrfs_iyesde);
	struct btrfs_delayed_yesde *yesde;

	yesde = READ_ONCE(btrfs_iyesde->delayed_yesde);
	if (yesde) {
		refcount_inc(&yesde->refs);
		return yesde;
	}

	spin_lock(&root->iyesde_lock);
	yesde = radix_tree_lookup(&root->delayed_yesdes_tree, iyes);

	if (yesde) {
		if (btrfs_iyesde->delayed_yesde) {
			refcount_inc(&yesde->refs);	/* can be accessed */
			BUG_ON(btrfs_iyesde->delayed_yesde != yesde);
			spin_unlock(&root->iyesde_lock);
			return yesde;
		}

		/*
		 * It's possible that we're racing into the middle of removing
		 * this yesde from the radix tree.  In this case, the refcount
		 * was zero and it should never go back to one.  Just return
		 * NULL like it was never in the radix at all; our release
		 * function is in the process of removing it.
		 *
		 * Some implementations of refcount_inc refuse to bump the
		 * refcount once it has hit zero.  If we don't do this dance
		 * here, refcount_inc() may decide to just WARN_ONCE() instead
		 * of actually bumping the refcount.
		 *
		 * If this yesde is properly in the radix, we want to bump the
		 * refcount twice, once for the iyesde and once for this get
		 * operation.
		 */
		if (refcount_inc_yest_zero(&yesde->refs)) {
			refcount_inc(&yesde->refs);
			btrfs_iyesde->delayed_yesde = yesde;
		} else {
			yesde = NULL;
		}

		spin_unlock(&root->iyesde_lock);
		return yesde;
	}
	spin_unlock(&root->iyesde_lock);

	return NULL;
}

/* Will return either the yesde or PTR_ERR(-ENOMEM) */
static struct btrfs_delayed_yesde *btrfs_get_or_create_delayed_yesde(
		struct btrfs_iyesde *btrfs_iyesde)
{
	struct btrfs_delayed_yesde *yesde;
	struct btrfs_root *root = btrfs_iyesde->root;
	u64 iyes = btrfs_iyes(btrfs_iyesde);
	int ret;

again:
	yesde = btrfs_get_delayed_yesde(btrfs_iyesde);
	if (yesde)
		return yesde;

	yesde = kmem_cache_zalloc(delayed_yesde_cache, GFP_NOFS);
	if (!yesde)
		return ERR_PTR(-ENOMEM);
	btrfs_init_delayed_yesde(yesde, root, iyes);

	/* cached in the btrfs iyesde and can be accessed */
	refcount_set(&yesde->refs, 2);

	ret = radix_tree_preload(GFP_NOFS);
	if (ret) {
		kmem_cache_free(delayed_yesde_cache, yesde);
		return ERR_PTR(ret);
	}

	spin_lock(&root->iyesde_lock);
	ret = radix_tree_insert(&root->delayed_yesdes_tree, iyes, yesde);
	if (ret == -EEXIST) {
		spin_unlock(&root->iyesde_lock);
		kmem_cache_free(delayed_yesde_cache, yesde);
		radix_tree_preload_end();
		goto again;
	}
	btrfs_iyesde->delayed_yesde = yesde;
	spin_unlock(&root->iyesde_lock);
	radix_tree_preload_end();

	return yesde;
}

/*
 * Call it when holding delayed_yesde->mutex
 *
 * If mod = 1, add this yesde into the prepared list.
 */
static void btrfs_queue_delayed_yesde(struct btrfs_delayed_root *root,
				     struct btrfs_delayed_yesde *yesde,
				     int mod)
{
	spin_lock(&root->lock);
	if (test_bit(BTRFS_DELAYED_NODE_IN_LIST, &yesde->flags)) {
		if (!list_empty(&yesde->p_list))
			list_move_tail(&yesde->p_list, &root->prepare_list);
		else if (mod)
			list_add_tail(&yesde->p_list, &root->prepare_list);
	} else {
		list_add_tail(&yesde->n_list, &root->yesde_list);
		list_add_tail(&yesde->p_list, &root->prepare_list);
		refcount_inc(&yesde->refs);	/* inserted into list */
		root->yesdes++;
		set_bit(BTRFS_DELAYED_NODE_IN_LIST, &yesde->flags);
	}
	spin_unlock(&root->lock);
}

/* Call it when holding delayed_yesde->mutex */
static void btrfs_dequeue_delayed_yesde(struct btrfs_delayed_root *root,
				       struct btrfs_delayed_yesde *yesde)
{
	spin_lock(&root->lock);
	if (test_bit(BTRFS_DELAYED_NODE_IN_LIST, &yesde->flags)) {
		root->yesdes--;
		refcount_dec(&yesde->refs);	/* yest in the list */
		list_del_init(&yesde->n_list);
		if (!list_empty(&yesde->p_list))
			list_del_init(&yesde->p_list);
		clear_bit(BTRFS_DELAYED_NODE_IN_LIST, &yesde->flags);
	}
	spin_unlock(&root->lock);
}

static struct btrfs_delayed_yesde *btrfs_first_delayed_yesde(
			struct btrfs_delayed_root *delayed_root)
{
	struct list_head *p;
	struct btrfs_delayed_yesde *yesde = NULL;

	spin_lock(&delayed_root->lock);
	if (list_empty(&delayed_root->yesde_list))
		goto out;

	p = delayed_root->yesde_list.next;
	yesde = list_entry(p, struct btrfs_delayed_yesde, n_list);
	refcount_inc(&yesde->refs);
out:
	spin_unlock(&delayed_root->lock);

	return yesde;
}

static struct btrfs_delayed_yesde *btrfs_next_delayed_yesde(
						struct btrfs_delayed_yesde *yesde)
{
	struct btrfs_delayed_root *delayed_root;
	struct list_head *p;
	struct btrfs_delayed_yesde *next = NULL;

	delayed_root = yesde->root->fs_info->delayed_root;
	spin_lock(&delayed_root->lock);
	if (!test_bit(BTRFS_DELAYED_NODE_IN_LIST, &yesde->flags)) {
		/* yest in the list */
		if (list_empty(&delayed_root->yesde_list))
			goto out;
		p = delayed_root->yesde_list.next;
	} else if (list_is_last(&yesde->n_list, &delayed_root->yesde_list))
		goto out;
	else
		p = yesde->n_list.next;

	next = list_entry(p, struct btrfs_delayed_yesde, n_list);
	refcount_inc(&next->refs);
out:
	spin_unlock(&delayed_root->lock);

	return next;
}

static void __btrfs_release_delayed_yesde(
				struct btrfs_delayed_yesde *delayed_yesde,
				int mod)
{
	struct btrfs_delayed_root *delayed_root;

	if (!delayed_yesde)
		return;

	delayed_root = delayed_yesde->root->fs_info->delayed_root;

	mutex_lock(&delayed_yesde->mutex);
	if (delayed_yesde->count)
		btrfs_queue_delayed_yesde(delayed_root, delayed_yesde, mod);
	else
		btrfs_dequeue_delayed_yesde(delayed_root, delayed_yesde);
	mutex_unlock(&delayed_yesde->mutex);

	if (refcount_dec_and_test(&delayed_yesde->refs)) {
		struct btrfs_root *root = delayed_yesde->root;

		spin_lock(&root->iyesde_lock);
		/*
		 * Once our refcount goes to zero, yesbody is allowed to bump it
		 * back up.  We can delete it yesw.
		 */
		ASSERT(refcount_read(&delayed_yesde->refs) == 0);
		radix_tree_delete(&root->delayed_yesdes_tree,
				  delayed_yesde->iyesde_id);
		spin_unlock(&root->iyesde_lock);
		kmem_cache_free(delayed_yesde_cache, delayed_yesde);
	}
}

static inline void btrfs_release_delayed_yesde(struct btrfs_delayed_yesde *yesde)
{
	__btrfs_release_delayed_yesde(yesde, 0);
}

static struct btrfs_delayed_yesde *btrfs_first_prepared_delayed_yesde(
					struct btrfs_delayed_root *delayed_root)
{
	struct list_head *p;
	struct btrfs_delayed_yesde *yesde = NULL;

	spin_lock(&delayed_root->lock);
	if (list_empty(&delayed_root->prepare_list))
		goto out;

	p = delayed_root->prepare_list.next;
	list_del_init(p);
	yesde = list_entry(p, struct btrfs_delayed_yesde, p_list);
	refcount_inc(&yesde->refs);
out:
	spin_unlock(&delayed_root->lock);

	return yesde;
}

static inline void btrfs_release_prepared_delayed_yesde(
					struct btrfs_delayed_yesde *yesde)
{
	__btrfs_release_delayed_yesde(yesde, 1);
}

static struct btrfs_delayed_item *btrfs_alloc_delayed_item(u32 data_len)
{
	struct btrfs_delayed_item *item;
	item = kmalloc(sizeof(*item) + data_len, GFP_NOFS);
	if (item) {
		item->data_len = data_len;
		item->ins_or_del = 0;
		item->bytes_reserved = 0;
		item->delayed_yesde = NULL;
		refcount_set(&item->refs, 1);
	}
	return item;
}

/*
 * __btrfs_lookup_delayed_item - look up the delayed item by key
 * @delayed_yesde: pointer to the delayed yesde
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
	struct rb_yesde *yesde, *prev_yesde = NULL;
	struct btrfs_delayed_item *delayed_item = NULL;
	int ret = 0;

	yesde = root->rb_yesde;

	while (yesde) {
		delayed_item = rb_entry(yesde, struct btrfs_delayed_item,
					rb_yesde);
		prev_yesde = yesde;
		ret = btrfs_comp_cpu_keys(&delayed_item->key, key);
		if (ret < 0)
			yesde = yesde->rb_right;
		else if (ret > 0)
			yesde = yesde->rb_left;
		else
			return delayed_item;
	}

	if (prev) {
		if (!prev_yesde)
			*prev = NULL;
		else if (ret < 0)
			*prev = delayed_item;
		else if ((yesde = rb_prev(prev_yesde)) != NULL) {
			*prev = rb_entry(yesde, struct btrfs_delayed_item,
					 rb_yesde);
		} else
			*prev = NULL;
	}

	if (next) {
		if (!prev_yesde)
			*next = NULL;
		else if (ret > 0)
			*next = delayed_item;
		else if ((yesde = rb_next(prev_yesde)) != NULL) {
			*next = rb_entry(yesde, struct btrfs_delayed_item,
					 rb_yesde);
		} else
			*next = NULL;
	}
	return NULL;
}

static struct btrfs_delayed_item *__btrfs_lookup_delayed_insertion_item(
					struct btrfs_delayed_yesde *delayed_yesde,
					struct btrfs_key *key)
{
	return __btrfs_lookup_delayed_item(&delayed_yesde->ins_root.rb_root, key,
					   NULL, NULL);
}

static int __btrfs_add_delayed_item(struct btrfs_delayed_yesde *delayed_yesde,
				    struct btrfs_delayed_item *ins,
				    int action)
{
	struct rb_yesde **p, *yesde;
	struct rb_yesde *parent_yesde = NULL;
	struct rb_root_cached *root;
	struct btrfs_delayed_item *item;
	int cmp;
	bool leftmost = true;

	if (action == BTRFS_DELAYED_INSERTION_ITEM)
		root = &delayed_yesde->ins_root;
	else if (action == BTRFS_DELAYED_DELETION_ITEM)
		root = &delayed_yesde->del_root;
	else
		BUG();
	p = &root->rb_root.rb_yesde;
	yesde = &ins->rb_yesde;

	while (*p) {
		parent_yesde = *p;
		item = rb_entry(parent_yesde, struct btrfs_delayed_item,
				 rb_yesde);

		cmp = btrfs_comp_cpu_keys(&item->key, &ins->key);
		if (cmp < 0) {
			p = &(*p)->rb_right;
			leftmost = false;
		} else if (cmp > 0) {
			p = &(*p)->rb_left;
		} else {
			return -EEXIST;
		}
	}

	rb_link_yesde(yesde, parent_yesde, p);
	rb_insert_color_cached(yesde, root, leftmost);
	ins->delayed_yesde = delayed_yesde;
	ins->ins_or_del = action;

	if (ins->key.type == BTRFS_DIR_INDEX_KEY &&
	    action == BTRFS_DELAYED_INSERTION_ITEM &&
	    ins->key.offset >= delayed_yesde->index_cnt)
			delayed_yesde->index_cnt = ins->key.offset + 1;

	delayed_yesde->count++;
	atomic_inc(&delayed_yesde->root->fs_info->delayed_root->items);
	return 0;
}

static int __btrfs_add_delayed_insertion_item(struct btrfs_delayed_yesde *yesde,
					      struct btrfs_delayed_item *item)
{
	return __btrfs_add_delayed_item(yesde, item,
					BTRFS_DELAYED_INSERTION_ITEM);
}

static int __btrfs_add_delayed_deletion_item(struct btrfs_delayed_yesde *yesde,
					     struct btrfs_delayed_item *item)
{
	return __btrfs_add_delayed_item(yesde, item,
					BTRFS_DELAYED_DELETION_ITEM);
}

static void finish_one_item(struct btrfs_delayed_root *delayed_root)
{
	int seq = atomic_inc_return(&delayed_root->items_seq);

	/* atomic_dec_return implies a barrier */
	if ((atomic_dec_return(&delayed_root->items) <
	    BTRFS_DELAYED_BACKGROUND || seq % BTRFS_DELAYED_BATCH == 0))
		cond_wake_up_yesmb(&delayed_root->wait);
}

static void __btrfs_remove_delayed_item(struct btrfs_delayed_item *delayed_item)
{
	struct rb_root_cached *root;
	struct btrfs_delayed_root *delayed_root;

	/* Not associated with any delayed_yesde */
	if (!delayed_item->delayed_yesde)
		return;
	delayed_root = delayed_item->delayed_yesde->root->fs_info->delayed_root;

	BUG_ON(!delayed_root);
	BUG_ON(delayed_item->ins_or_del != BTRFS_DELAYED_DELETION_ITEM &&
	       delayed_item->ins_or_del != BTRFS_DELAYED_INSERTION_ITEM);

	if (delayed_item->ins_or_del == BTRFS_DELAYED_INSERTION_ITEM)
		root = &delayed_item->delayed_yesde->ins_root;
	else
		root = &delayed_item->delayed_yesde->del_root;

	rb_erase_cached(&delayed_item->rb_yesde, root);
	delayed_item->delayed_yesde->count--;

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
					struct btrfs_delayed_yesde *delayed_yesde)
{
	struct rb_yesde *p;
	struct btrfs_delayed_item *item = NULL;

	p = rb_first_cached(&delayed_yesde->ins_root);
	if (p)
		item = rb_entry(p, struct btrfs_delayed_item, rb_yesde);

	return item;
}

static struct btrfs_delayed_item *__btrfs_first_delayed_deletion_item(
					struct btrfs_delayed_yesde *delayed_yesde)
{
	struct rb_yesde *p;
	struct btrfs_delayed_item *item = NULL;

	p = rb_first_cached(&delayed_yesde->del_root);
	if (p)
		item = rb_entry(p, struct btrfs_delayed_item, rb_yesde);

	return item;
}

static struct btrfs_delayed_item *__btrfs_next_delayed_item(
						struct btrfs_delayed_item *item)
{
	struct rb_yesde *p;
	struct btrfs_delayed_item *next = NULL;

	p = rb_next(&item->rb_yesde);
	if (p)
		next = rb_entry(p, struct btrfs_delayed_item, rb_yesde);

	return next;
}

static int btrfs_delayed_item_reserve_metadata(struct btrfs_trans_handle *trans,
					       struct btrfs_root *root,
					       struct btrfs_delayed_item *item)
{
	struct btrfs_block_rsv *src_rsv;
	struct btrfs_block_rsv *dst_rsv;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 num_bytes;
	int ret;

	if (!trans->bytes_reserved)
		return 0;

	src_rsv = trans->block_rsv;
	dst_rsv = &fs_info->delayed_block_rsv;

	num_bytes = btrfs_calc_insert_metadata_size(fs_info, 1);

	/*
	 * Here we migrate space rsv from transaction rsv, since have already
	 * reserved space when starting a transaction.  So yes need to reserve
	 * qgroup space here.
	 */
	ret = btrfs_block_rsv_migrate(src_rsv, dst_rsv, num_bytes, true);
	if (!ret) {
		trace_btrfs_space_reservation(fs_info, "delayed_item",
					      item->key.objectid,
					      num_bytes, 1);
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
				      item->key.objectid, item->bytes_reserved,
				      0);
	btrfs_block_rsv_release(fs_info, rsv,
				item->bytes_reserved);
}

static int btrfs_delayed_iyesde_reserve_metadata(
					struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_iyesde *iyesde,
					struct btrfs_delayed_yesde *yesde)
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
	 * btrfs_dirty_iyesde will update the iyesde under btrfs_join_transaction
	 * which doesn't reserve space for speed.  This is a problem since we
	 * still need to reserve space for this update, so try to reserve the
	 * space.
	 *
	 * Now if src_rsv == delalloc_block_rsv we'll let it just steal since
	 * we always reserve eyesugh to update the iyesde item.
	 */
	if (!src_rsv || (!trans->bytes_reserved &&
			 src_rsv->type != BTRFS_BLOCK_RSV_DELALLOC)) {
		ret = btrfs_qgroup_reserve_meta_prealloc(root,
				fs_info->yesdesize, true);
		if (ret < 0)
			return ret;
		ret = btrfs_block_rsv_add(root, dst_rsv, num_bytes,
					  BTRFS_RESERVE_NO_FLUSH);
		/*
		 * Since we're under a transaction reserve_metadata_bytes could
		 * try to commit the transaction which will make it return
		 * EAGAIN to make us stop the transaction we have, so return
		 * ENOSPC instead so that btrfs_dirty_iyesde kyesws what to do.
		 */
		if (ret == -EAGAIN) {
			ret = -ENOSPC;
			btrfs_qgroup_free_meta_prealloc(root, num_bytes);
		}
		if (!ret) {
			yesde->bytes_reserved = num_bytes;
			trace_btrfs_space_reservation(fs_info,
						      "delayed_iyesde",
						      btrfs_iyes(iyesde),
						      num_bytes, 1);
		} else {
			btrfs_qgroup_free_meta_prealloc(root, fs_info->yesdesize);
		}
		return ret;
	}

	ret = btrfs_block_rsv_migrate(src_rsv, dst_rsv, num_bytes, true);
	if (!ret) {
		trace_btrfs_space_reservation(fs_info, "delayed_iyesde",
					      btrfs_iyes(iyesde), num_bytes, 1);
		yesde->bytes_reserved = num_bytes;
	}

	return ret;
}

static void btrfs_delayed_iyesde_release_metadata(struct btrfs_fs_info *fs_info,
						struct btrfs_delayed_yesde *yesde,
						bool qgroup_free)
{
	struct btrfs_block_rsv *rsv;

	if (!yesde->bytes_reserved)
		return;

	rsv = &fs_info->delayed_block_rsv;
	trace_btrfs_space_reservation(fs_info, "delayed_iyesde",
				      yesde->iyesde_id, yesde->bytes_reserved, 0);
	btrfs_block_rsv_release(fs_info, rsv,
				yesde->bytes_reserved);
	if (qgroup_free)
		btrfs_qgroup_free_meta_prealloc(yesde->root,
				yesde->bytes_reserved);
	else
		btrfs_qgroup_convert_reserved_meta(yesde->root,
				yesde->bytes_reserved);
	yesde->bytes_reserved = 0;
}

/*
 * This helper will insert some continuous items into the same leaf according
 * to the free space of the leaf.
 */
static int btrfs_batch_insert_items(struct btrfs_root *root,
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

	BUG_ON(!path->yesdes[0]);

	leaf = path->yesdes[0];
	free_space = btrfs_leaf_free_space(leaf);
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
	 * to sleep, so we set all locked yesdes in the path to blocking locks
	 * first.
	 */
	btrfs_set_path_blocking(path);

	keys = kmalloc_array(nitems, sizeof(struct btrfs_key), GFP_NOFS);
	if (!keys) {
		ret = -ENOMEM;
		goto out;
	}

	data_size = kmalloc_array(nitems, sizeof(u32), GFP_NOFS);
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

	/* insert the keys of the items */
	setup_items_for_insert(root, path, keys, data_size,
			       total_data_size, total_size, nitems);

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
 * data, such as directory name index insertion, iyesde insertion.
 */
static int btrfs_insert_delayed_item(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct btrfs_path *path,
				     struct btrfs_delayed_item *delayed_item)
{
	struct extent_buffer *leaf;
	char *ptr;
	int ret;

	ret = btrfs_insert_empty_item(trans, root, path, &delayed_item->key,
				      delayed_item->data_len);
	if (ret < 0 && ret != -EEXIST)
		return ret;

	leaf = path->yesdes[0];

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
				      struct btrfs_delayed_yesde *yesde)
{
	struct btrfs_delayed_item *curr, *prev;
	int ret = 0;

do_again:
	mutex_lock(&yesde->mutex);
	curr = __btrfs_first_delayed_insertion_item(yesde);
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
		btrfs_batch_insert_items(root, path, curr);
	}
	btrfs_release_delayed_item(prev);
	btrfs_mark_buffer_dirty(path->yesdes[0]);

	btrfs_release_path(path);
	mutex_unlock(&yesde->mutex);
	goto do_again;

insert_end:
	mutex_unlock(&yesde->mutex);
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

	BUG_ON(!path->yesdes[0]);

	leaf = path->yesdes[0];

	i = path->slots[0];
	last_item = btrfs_header_nritems(leaf) - 1;
	if (i > last_item)
		return -ENOENT;	/* FIXME: Is erryes suitable? */

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
				      struct btrfs_delayed_yesde *yesde)
{
	struct btrfs_delayed_item *curr, *prev;
	int ret = 0;

do_again:
	mutex_lock(&yesde->mutex);
	curr = __btrfs_first_delayed_deletion_item(yesde);
	if (!curr)
		goto delete_fail;

	ret = btrfs_search_slot(trans, root, &curr->key, path, -1, 1);
	if (ret < 0)
		goto delete_fail;
	else if (ret > 0) {
		/*
		 * can't find the item which the yesde points to, so this yesde
		 * is invalid, just drop it.
		 */
		prev = curr;
		curr = __btrfs_next_delayed_item(prev);
		btrfs_release_delayed_item(prev);
		ret = 0;
		btrfs_release_path(path);
		if (curr) {
			mutex_unlock(&yesde->mutex);
			goto do_again;
		} else
			goto delete_fail;
	}

	btrfs_batch_delete_items(trans, root, path, curr);
	btrfs_release_path(path);
	mutex_unlock(&yesde->mutex);
	goto do_again;

delete_fail:
	btrfs_release_path(path);
	mutex_unlock(&yesde->mutex);
	return ret;
}

static void btrfs_release_delayed_iyesde(struct btrfs_delayed_yesde *delayed_yesde)
{
	struct btrfs_delayed_root *delayed_root;

	if (delayed_yesde &&
	    test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_yesde->flags)) {
		BUG_ON(!delayed_yesde->root);
		clear_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_yesde->flags);
		delayed_yesde->count--;

		delayed_root = delayed_yesde->root->fs_info->delayed_root;
		finish_one_item(delayed_root);
	}
}

static void btrfs_release_delayed_iref(struct btrfs_delayed_yesde *delayed_yesde)
{
	struct btrfs_delayed_root *delayed_root;

	ASSERT(delayed_yesde->root);
	clear_bit(BTRFS_DELAYED_NODE_DEL_IREF, &delayed_yesde->flags);
	delayed_yesde->count--;

	delayed_root = delayed_yesde->root->fs_info->delayed_root;
	finish_one_item(delayed_root);
}

static int __btrfs_update_delayed_iyesde(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_path *path,
					struct btrfs_delayed_yesde *yesde)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_key key;
	struct btrfs_iyesde_item *iyesde_item;
	struct extent_buffer *leaf;
	int mod;
	int ret;

	key.objectid = yesde->iyesde_id;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;

	if (test_bit(BTRFS_DELAYED_NODE_DEL_IREF, &yesde->flags))
		mod = -1;
	else
		mod = 1;

	ret = btrfs_lookup_iyesde(trans, root, path, &key, mod);
	if (ret > 0) {
		btrfs_release_path(path);
		return -ENOENT;
	} else if (ret < 0) {
		return ret;
	}

	leaf = path->yesdes[0];
	iyesde_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_iyesde_item);
	write_extent_buffer(leaf, &yesde->iyesde_item, (unsigned long)iyesde_item,
			    sizeof(struct btrfs_iyesde_item));
	btrfs_mark_buffer_dirty(leaf);

	if (!test_bit(BTRFS_DELAYED_NODE_DEL_IREF, &yesde->flags))
		goto yes_iref;

	path->slots[0]++;
	if (path->slots[0] >= btrfs_header_nritems(leaf))
		goto search;
again:
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	if (key.objectid != yesde->iyesde_id)
		goto out;

	if (key.type != BTRFS_INODE_REF_KEY &&
	    key.type != BTRFS_INODE_EXTREF_KEY)
		goto out;

	/*
	 * Delayed iref deletion is for the iyesde who has only one link,
	 * so there is only one iref. The case that several irefs are
	 * in the same item doesn't exist.
	 */
	btrfs_del_item(trans, root, path);
out:
	btrfs_release_delayed_iref(yesde);
yes_iref:
	btrfs_release_path(path);
err_out:
	btrfs_delayed_iyesde_release_metadata(fs_info, yesde, (ret < 0));
	btrfs_release_delayed_iyesde(yesde);

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
	leaf = path->yesdes[0];
	path->slots[0]--;
	goto again;
}

static inline int btrfs_update_delayed_iyesde(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     struct btrfs_path *path,
					     struct btrfs_delayed_yesde *yesde)
{
	int ret;

	mutex_lock(&yesde->mutex);
	if (!test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &yesde->flags)) {
		mutex_unlock(&yesde->mutex);
		return 0;
	}

	ret = __btrfs_update_delayed_iyesde(trans, root, path, yesde);
	mutex_unlock(&yesde->mutex);
	return ret;
}

static inline int
__btrfs_commit_iyesde_delayed_items(struct btrfs_trans_handle *trans,
				   struct btrfs_path *path,
				   struct btrfs_delayed_yesde *yesde)
{
	int ret;

	ret = btrfs_insert_delayed_items(trans, path, yesde->root, yesde);
	if (ret)
		return ret;

	ret = btrfs_delete_delayed_items(trans, path, yesde->root, yesde);
	if (ret)
		return ret;

	ret = btrfs_update_delayed_iyesde(trans, yesde->root, path, yesde);
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
	struct btrfs_delayed_yesde *curr_yesde, *prev_yesde;
	struct btrfs_path *path;
	struct btrfs_block_rsv *block_rsv;
	int ret = 0;
	bool count = (nr > 0);

	if (trans->aborted)
		return -EIO;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->leave_spinning = 1;

	block_rsv = trans->block_rsv;
	trans->block_rsv = &fs_info->delayed_block_rsv;

	delayed_root = fs_info->delayed_root;

	curr_yesde = btrfs_first_delayed_yesde(delayed_root);
	while (curr_yesde && (!count || (count && nr--))) {
		ret = __btrfs_commit_iyesde_delayed_items(trans, path,
							 curr_yesde);
		if (ret) {
			btrfs_release_delayed_yesde(curr_yesde);
			curr_yesde = NULL;
			btrfs_abort_transaction(trans, ret);
			break;
		}

		prev_yesde = curr_yesde;
		curr_yesde = btrfs_next_delayed_yesde(curr_yesde);
		btrfs_release_delayed_yesde(prev_yesde);
	}

	if (curr_yesde)
		btrfs_release_delayed_yesde(curr_yesde);
	btrfs_free_path(path);
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

int btrfs_commit_iyesde_delayed_items(struct btrfs_trans_handle *trans,
				     struct btrfs_iyesde *iyesde)
{
	struct btrfs_delayed_yesde *delayed_yesde = btrfs_get_delayed_yesde(iyesde);
	struct btrfs_path *path;
	struct btrfs_block_rsv *block_rsv;
	int ret;

	if (!delayed_yesde)
		return 0;

	mutex_lock(&delayed_yesde->mutex);
	if (!delayed_yesde->count) {
		mutex_unlock(&delayed_yesde->mutex);
		btrfs_release_delayed_yesde(delayed_yesde);
		return 0;
	}
	mutex_unlock(&delayed_yesde->mutex);

	path = btrfs_alloc_path();
	if (!path) {
		btrfs_release_delayed_yesde(delayed_yesde);
		return -ENOMEM;
	}
	path->leave_spinning = 1;

	block_rsv = trans->block_rsv;
	trans->block_rsv = &delayed_yesde->root->fs_info->delayed_block_rsv;

	ret = __btrfs_commit_iyesde_delayed_items(trans, path, delayed_yesde);

	btrfs_release_delayed_yesde(delayed_yesde);
	btrfs_free_path(path);
	trans->block_rsv = block_rsv;

	return ret;
}

int btrfs_commit_iyesde_delayed_iyesde(struct btrfs_iyesde *iyesde)
{
	struct btrfs_fs_info *fs_info = iyesde->root->fs_info;
	struct btrfs_trans_handle *trans;
	struct btrfs_delayed_yesde *delayed_yesde = btrfs_get_delayed_yesde(iyesde);
	struct btrfs_path *path;
	struct btrfs_block_rsv *block_rsv;
	int ret;

	if (!delayed_yesde)
		return 0;

	mutex_lock(&delayed_yesde->mutex);
	if (!test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_yesde->flags)) {
		mutex_unlock(&delayed_yesde->mutex);
		btrfs_release_delayed_yesde(delayed_yesde);
		return 0;
	}
	mutex_unlock(&delayed_yesde->mutex);

	trans = btrfs_join_transaction(delayed_yesde->root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto trans_out;
	}
	path->leave_spinning = 1;

	block_rsv = trans->block_rsv;
	trans->block_rsv = &fs_info->delayed_block_rsv;

	mutex_lock(&delayed_yesde->mutex);
	if (test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_yesde->flags))
		ret = __btrfs_update_delayed_iyesde(trans, delayed_yesde->root,
						   path, delayed_yesde);
	else
		ret = 0;
	mutex_unlock(&delayed_yesde->mutex);

	btrfs_free_path(path);
	trans->block_rsv = block_rsv;
trans_out:
	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(fs_info);
out:
	btrfs_release_delayed_yesde(delayed_yesde);

	return ret;
}

void btrfs_remove_delayed_yesde(struct btrfs_iyesde *iyesde)
{
	struct btrfs_delayed_yesde *delayed_yesde;

	delayed_yesde = READ_ONCE(iyesde->delayed_yesde);
	if (!delayed_yesde)
		return;

	iyesde->delayed_yesde = NULL;
	btrfs_release_delayed_yesde(delayed_yesde);
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
	struct btrfs_delayed_yesde *delayed_yesde = NULL;
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

		delayed_yesde = btrfs_first_prepared_delayed_yesde(delayed_root);
		if (!delayed_yesde)
			break;

		path->leave_spinning = 1;
		root = delayed_yesde->root;

		trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			btrfs_release_path(path);
			btrfs_release_prepared_delayed_yesde(delayed_yesde);
			total_done++;
			continue;
		}

		block_rsv = trans->block_rsv;
		trans->block_rsv = &root->fs_info->delayed_block_rsv;

		__btrfs_commit_iyesde_delayed_items(trans, path, delayed_yesde);

		trans->block_rsv = block_rsv;
		btrfs_end_transaction(trans);
		btrfs_btree_balance_dirty_yesdelay(root->fs_info);

		btrfs_release_path(path);
		btrfs_release_prepared_delayed_yesde(delayed_yesde);
		total_done++;

	} while ((async_work->nr == 0 && total_done < BTRFS_DELAYED_WRITEBACK)
		 || total_done < async_work->nr);

	btrfs_free_path(path);
out:
	wake_up(&delayed_root->wait);
	kfree(async_work);
}


static int btrfs_wq_run_delayed_yesde(struct btrfs_delayed_root *delayed_root,
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
	WARN_ON(btrfs_first_delayed_yesde(fs_info->delayed_root));
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
		btrfs_workqueue_yesrmal_congested(fs_info->delayed_workers))
		return;

	if (atomic_read(&delayed_root->items) >= BTRFS_DELAYED_WRITEBACK) {
		int seq;
		int ret;

		seq = atomic_read(&delayed_root->items_seq);

		ret = btrfs_wq_run_delayed_yesde(delayed_root, fs_info, 0);
		if (ret)
			return;

		wait_event_interruptible(delayed_root->wait,
					 could_end_wait(delayed_root, seq));
		return;
	}

	btrfs_wq_run_delayed_yesde(delayed_root, fs_info, BTRFS_DELAYED_BATCH);
}

/* Will return 0 or -ENOMEM */
int btrfs_insert_delayed_dir_index(struct btrfs_trans_handle *trans,
				   const char *name, int name_len,
				   struct btrfs_iyesde *dir,
				   struct btrfs_disk_key *disk_key, u8 type,
				   u64 index)
{
	struct btrfs_delayed_yesde *delayed_yesde;
	struct btrfs_delayed_item *delayed_item;
	struct btrfs_dir_item *dir_item;
	int ret;

	delayed_yesde = btrfs_get_or_create_delayed_yesde(dir);
	if (IS_ERR(delayed_yesde))
		return PTR_ERR(delayed_yesde);

	delayed_item = btrfs_alloc_delayed_item(sizeof(*dir_item) + name_len);
	if (!delayed_item) {
		ret = -ENOMEM;
		goto release_yesde;
	}

	delayed_item->key.objectid = btrfs_iyes(dir);
	delayed_item->key.type = BTRFS_DIR_INDEX_KEY;
	delayed_item->key.offset = index;

	dir_item = (struct btrfs_dir_item *)delayed_item->data;
	dir_item->location = *disk_key;
	btrfs_set_stack_dir_transid(dir_item, trans->transid);
	btrfs_set_stack_dir_data_len(dir_item, 0);
	btrfs_set_stack_dir_name_len(dir_item, name_len);
	btrfs_set_stack_dir_type(dir_item, type);
	memcpy((char *)(dir_item + 1), name, name_len);

	ret = btrfs_delayed_item_reserve_metadata(trans, dir->root, delayed_item);
	/*
	 * we have reserved eyesugh space when we start a new transaction,
	 * so reserving metadata failure is impossible
	 */
	BUG_ON(ret);

	mutex_lock(&delayed_yesde->mutex);
	ret = __btrfs_add_delayed_insertion_item(delayed_yesde, delayed_item);
	if (unlikely(ret)) {
		btrfs_err(trans->fs_info,
			  "err add delayed dir index item(name: %.*s) into the insertion tree of the delayed yesde(root id: %llu, iyesde id: %llu, erryes: %d)",
			  name_len, name, delayed_yesde->root->root_key.objectid,
			  delayed_yesde->iyesde_id, ret);
		BUG();
	}
	mutex_unlock(&delayed_yesde->mutex);

release_yesde:
	btrfs_release_delayed_yesde(delayed_yesde);
	return ret;
}

static int btrfs_delete_delayed_insertion_item(struct btrfs_fs_info *fs_info,
					       struct btrfs_delayed_yesde *yesde,
					       struct btrfs_key *key)
{
	struct btrfs_delayed_item *item;

	mutex_lock(&yesde->mutex);
	item = __btrfs_lookup_delayed_insertion_item(yesde, key);
	if (!item) {
		mutex_unlock(&yesde->mutex);
		return 1;
	}

	btrfs_delayed_item_release_metadata(yesde->root, item);
	btrfs_release_delayed_item(item);
	mutex_unlock(&yesde->mutex);
	return 0;
}

int btrfs_delete_delayed_dir_index(struct btrfs_trans_handle *trans,
				   struct btrfs_iyesde *dir, u64 index)
{
	struct btrfs_delayed_yesde *yesde;
	struct btrfs_delayed_item *item;
	struct btrfs_key item_key;
	int ret;

	yesde = btrfs_get_or_create_delayed_yesde(dir);
	if (IS_ERR(yesde))
		return PTR_ERR(yesde);

	item_key.objectid = btrfs_iyes(dir);
	item_key.type = BTRFS_DIR_INDEX_KEY;
	item_key.offset = index;

	ret = btrfs_delete_delayed_insertion_item(trans->fs_info, yesde,
						  &item_key);
	if (!ret)
		goto end;

	item = btrfs_alloc_delayed_item(0);
	if (!item) {
		ret = -ENOMEM;
		goto end;
	}

	item->key = item_key;

	ret = btrfs_delayed_item_reserve_metadata(trans, dir->root, item);
	/*
	 * we have reserved eyesugh space when we start a new transaction,
	 * so reserving metadata failure is impossible.
	 */
	if (ret < 0) {
		btrfs_err(trans->fs_info,
"metadata reservation failed for delayed dir item deltiona, should have been reserved");
		btrfs_release_delayed_item(item);
		goto end;
	}

	mutex_lock(&yesde->mutex);
	ret = __btrfs_add_delayed_deletion_item(yesde, item);
	if (unlikely(ret)) {
		btrfs_err(trans->fs_info,
			  "err add delayed dir index item(index: %llu) into the deletion tree of the delayed yesde(root id: %llu, iyesde id: %llu, erryes: %d)",
			  index, yesde->root->root_key.objectid,
			  yesde->iyesde_id, ret);
		btrfs_delayed_item_release_metadata(dir->root, item);
		btrfs_release_delayed_item(item);
	}
	mutex_unlock(&yesde->mutex);
end:
	btrfs_release_delayed_yesde(yesde);
	return ret;
}

int btrfs_iyesde_delayed_dir_index_count(struct btrfs_iyesde *iyesde)
{
	struct btrfs_delayed_yesde *delayed_yesde = btrfs_get_delayed_yesde(iyesde);

	if (!delayed_yesde)
		return -ENOENT;

	/*
	 * Since we have held i_mutex of this directory, it is impossible that
	 * a new directory index is added into the delayed yesde and index_cnt
	 * is updated yesw. So we needn't lock the delayed yesde.
	 */
	if (!delayed_yesde->index_cnt) {
		btrfs_release_delayed_yesde(delayed_yesde);
		return -EINVAL;
	}

	iyesde->index_cnt = delayed_yesde->index_cnt;
	btrfs_release_delayed_yesde(delayed_yesde);
	return 0;
}

bool btrfs_readdir_get_delayed_items(struct iyesde *iyesde,
				     struct list_head *ins_list,
				     struct list_head *del_list)
{
	struct btrfs_delayed_yesde *delayed_yesde;
	struct btrfs_delayed_item *item;

	delayed_yesde = btrfs_get_delayed_yesde(BTRFS_I(iyesde));
	if (!delayed_yesde)
		return false;

	/*
	 * We can only do one readdir with delayed items at a time because of
	 * item->readdir_list.
	 */
	iyesde_unlock_shared(iyesde);
	iyesde_lock(iyesde);

	mutex_lock(&delayed_yesde->mutex);
	item = __btrfs_first_delayed_insertion_item(delayed_yesde);
	while (item) {
		refcount_inc(&item->refs);
		list_add_tail(&item->readdir_list, ins_list);
		item = __btrfs_next_delayed_item(item);
	}

	item = __btrfs_first_delayed_deletion_item(delayed_yesde);
	while (item) {
		refcount_inc(&item->refs);
		list_add_tail(&item->readdir_list, del_list);
		item = __btrfs_next_delayed_item(item);
	}
	mutex_unlock(&delayed_yesde->mutex);
	/*
	 * This delayed yesde is still cached in the btrfs iyesde, so refs
	 * must be > 1 yesw, and we needn't check it is going to be freed
	 * or yest.
	 *
	 * Besides that, this function is used to read dir, we do yest
	 * insert/delete delayed items in this period. So we also needn't
	 * requeue or dequeue this delayed yesde.
	 */
	refcount_dec(&delayed_yesde->refs);

	return true;
}

void btrfs_readdir_put_delayed_items(struct iyesde *iyesde,
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
	downgrade_write(&iyesde->i_rwsem);
}

int btrfs_should_delete_dir_index(struct list_head *del_list,
				  u64 index)
{
	struct btrfs_delayed_item *curr;
	int ret = 0;

	list_for_each_entry(curr, del_list, readdir_list) {
		if (curr->key.offset > index)
			break;
		if (curr->key.offset == index) {
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

	if (list_empty(ins_list))
		return 0;

	/*
	 * Changing the data of the delayed item is impossible. So
	 * we needn't lock them. And we have held i_mutex of the
	 * directory, yesbody can delete any directory indexes yesw.
	 */
	list_for_each_entry_safe(curr, next, ins_list, readdir_list) {
		list_del(&curr->readdir_list);

		if (curr->key.offset < ctx->pos) {
			if (refcount_dec_and_test(&curr->refs))
				kfree(curr);
			continue;
		}

		ctx->pos = curr->key.offset;

		di = (struct btrfs_dir_item *)curr->data;
		name = (char *)(di + 1);
		name_len = btrfs_stack_dir_name_len(di);

		d_type = fs_ftype_to_dtype(di->type);
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

static void fill_stack_iyesde_item(struct btrfs_trans_handle *trans,
				  struct btrfs_iyesde_item *iyesde_item,
				  struct iyesde *iyesde)
{
	btrfs_set_stack_iyesde_uid(iyesde_item, i_uid_read(iyesde));
	btrfs_set_stack_iyesde_gid(iyesde_item, i_gid_read(iyesde));
	btrfs_set_stack_iyesde_size(iyesde_item, BTRFS_I(iyesde)->disk_i_size);
	btrfs_set_stack_iyesde_mode(iyesde_item, iyesde->i_mode);
	btrfs_set_stack_iyesde_nlink(iyesde_item, iyesde->i_nlink);
	btrfs_set_stack_iyesde_nbytes(iyesde_item, iyesde_get_bytes(iyesde));
	btrfs_set_stack_iyesde_generation(iyesde_item,
					 BTRFS_I(iyesde)->generation);
	btrfs_set_stack_iyesde_sequence(iyesde_item,
				       iyesde_peek_iversion(iyesde));
	btrfs_set_stack_iyesde_transid(iyesde_item, trans->transid);
	btrfs_set_stack_iyesde_rdev(iyesde_item, iyesde->i_rdev);
	btrfs_set_stack_iyesde_flags(iyesde_item, BTRFS_I(iyesde)->flags);
	btrfs_set_stack_iyesde_block_group(iyesde_item, 0);

	btrfs_set_stack_timespec_sec(&iyesde_item->atime,
				     iyesde->i_atime.tv_sec);
	btrfs_set_stack_timespec_nsec(&iyesde_item->atime,
				      iyesde->i_atime.tv_nsec);

	btrfs_set_stack_timespec_sec(&iyesde_item->mtime,
				     iyesde->i_mtime.tv_sec);
	btrfs_set_stack_timespec_nsec(&iyesde_item->mtime,
				      iyesde->i_mtime.tv_nsec);

	btrfs_set_stack_timespec_sec(&iyesde_item->ctime,
				     iyesde->i_ctime.tv_sec);
	btrfs_set_stack_timespec_nsec(&iyesde_item->ctime,
				      iyesde->i_ctime.tv_nsec);

	btrfs_set_stack_timespec_sec(&iyesde_item->otime,
				     BTRFS_I(iyesde)->i_otime.tv_sec);
	btrfs_set_stack_timespec_nsec(&iyesde_item->otime,
				     BTRFS_I(iyesde)->i_otime.tv_nsec);
}

int btrfs_fill_iyesde(struct iyesde *iyesde, u32 *rdev)
{
	struct btrfs_delayed_yesde *delayed_yesde;
	struct btrfs_iyesde_item *iyesde_item;

	delayed_yesde = btrfs_get_delayed_yesde(BTRFS_I(iyesde));
	if (!delayed_yesde)
		return -ENOENT;

	mutex_lock(&delayed_yesde->mutex);
	if (!test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_yesde->flags)) {
		mutex_unlock(&delayed_yesde->mutex);
		btrfs_release_delayed_yesde(delayed_yesde);
		return -ENOENT;
	}

	iyesde_item = &delayed_yesde->iyesde_item;

	i_uid_write(iyesde, btrfs_stack_iyesde_uid(iyesde_item));
	i_gid_write(iyesde, btrfs_stack_iyesde_gid(iyesde_item));
	btrfs_i_size_write(BTRFS_I(iyesde), btrfs_stack_iyesde_size(iyesde_item));
	iyesde->i_mode = btrfs_stack_iyesde_mode(iyesde_item);
	set_nlink(iyesde, btrfs_stack_iyesde_nlink(iyesde_item));
	iyesde_set_bytes(iyesde, btrfs_stack_iyesde_nbytes(iyesde_item));
	BTRFS_I(iyesde)->generation = btrfs_stack_iyesde_generation(iyesde_item);
        BTRFS_I(iyesde)->last_trans = btrfs_stack_iyesde_transid(iyesde_item);

	iyesde_set_iversion_queried(iyesde,
				   btrfs_stack_iyesde_sequence(iyesde_item));
	iyesde->i_rdev = 0;
	*rdev = btrfs_stack_iyesde_rdev(iyesde_item);
	BTRFS_I(iyesde)->flags = btrfs_stack_iyesde_flags(iyesde_item);

	iyesde->i_atime.tv_sec = btrfs_stack_timespec_sec(&iyesde_item->atime);
	iyesde->i_atime.tv_nsec = btrfs_stack_timespec_nsec(&iyesde_item->atime);

	iyesde->i_mtime.tv_sec = btrfs_stack_timespec_sec(&iyesde_item->mtime);
	iyesde->i_mtime.tv_nsec = btrfs_stack_timespec_nsec(&iyesde_item->mtime);

	iyesde->i_ctime.tv_sec = btrfs_stack_timespec_sec(&iyesde_item->ctime);
	iyesde->i_ctime.tv_nsec = btrfs_stack_timespec_nsec(&iyesde_item->ctime);

	BTRFS_I(iyesde)->i_otime.tv_sec =
		btrfs_stack_timespec_sec(&iyesde_item->otime);
	BTRFS_I(iyesde)->i_otime.tv_nsec =
		btrfs_stack_timespec_nsec(&iyesde_item->otime);

	iyesde->i_generation = BTRFS_I(iyesde)->generation;
	BTRFS_I(iyesde)->index_cnt = (u64)-1;

	mutex_unlock(&delayed_yesde->mutex);
	btrfs_release_delayed_yesde(delayed_yesde);
	return 0;
}

int btrfs_delayed_update_iyesde(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root, struct iyesde *iyesde)
{
	struct btrfs_delayed_yesde *delayed_yesde;
	int ret = 0;

	delayed_yesde = btrfs_get_or_create_delayed_yesde(BTRFS_I(iyesde));
	if (IS_ERR(delayed_yesde))
		return PTR_ERR(delayed_yesde);

	mutex_lock(&delayed_yesde->mutex);
	if (test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_yesde->flags)) {
		fill_stack_iyesde_item(trans, &delayed_yesde->iyesde_item, iyesde);
		goto release_yesde;
	}

	ret = btrfs_delayed_iyesde_reserve_metadata(trans, root, BTRFS_I(iyesde),
						   delayed_yesde);
	if (ret)
		goto release_yesde;

	fill_stack_iyesde_item(trans, &delayed_yesde->iyesde_item, iyesde);
	set_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_yesde->flags);
	delayed_yesde->count++;
	atomic_inc(&root->fs_info->delayed_root->items);
release_yesde:
	mutex_unlock(&delayed_yesde->mutex);
	btrfs_release_delayed_yesde(delayed_yesde);
	return ret;
}

int btrfs_delayed_delete_iyesde_ref(struct btrfs_iyesde *iyesde)
{
	struct btrfs_fs_info *fs_info = iyesde->root->fs_info;
	struct btrfs_delayed_yesde *delayed_yesde;

	/*
	 * we don't do delayed iyesde updates during log recovery because it
	 * leads to eyesspc problems.  This means we also can't do
	 * delayed iyesde refs
	 */
	if (test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags))
		return -EAGAIN;

	delayed_yesde = btrfs_get_or_create_delayed_yesde(iyesde);
	if (IS_ERR(delayed_yesde))
		return PTR_ERR(delayed_yesde);

	/*
	 * We don't reserve space for iyesde ref deletion is because:
	 * - We ONLY do async iyesde ref deletion for the iyesde who has only
	 *   one link(i_nlink == 1), it means there is only one iyesde ref.
	 *   And in most case, the iyesde ref and the iyesde item are in the
	 *   same leaf, and we will deal with them at the same time.
	 *   Since we are sure we will reserve the space for the iyesde item,
	 *   it is unnecessary to reserve space for iyesde ref deletion.
	 * - If the iyesde ref and the iyesde item are yest in the same leaf,
	 *   We also needn't worry about eyesspc problem, because we reserve
	 *   much more space for the iyesde update than it needs.
	 * - At the worst, we can steal some space from the global reservation.
	 *   It is very rare.
	 */
	mutex_lock(&delayed_yesde->mutex);
	if (test_bit(BTRFS_DELAYED_NODE_DEL_IREF, &delayed_yesde->flags))
		goto release_yesde;

	set_bit(BTRFS_DELAYED_NODE_DEL_IREF, &delayed_yesde->flags);
	delayed_yesde->count++;
	atomic_inc(&fs_info->delayed_root->items);
release_yesde:
	mutex_unlock(&delayed_yesde->mutex);
	btrfs_release_delayed_yesde(delayed_yesde);
	return 0;
}

static void __btrfs_kill_delayed_yesde(struct btrfs_delayed_yesde *delayed_yesde)
{
	struct btrfs_root *root = delayed_yesde->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_delayed_item *curr_item, *prev_item;

	mutex_lock(&delayed_yesde->mutex);
	curr_item = __btrfs_first_delayed_insertion_item(delayed_yesde);
	while (curr_item) {
		btrfs_delayed_item_release_metadata(root, curr_item);
		prev_item = curr_item;
		curr_item = __btrfs_next_delayed_item(prev_item);
		btrfs_release_delayed_item(prev_item);
	}

	curr_item = __btrfs_first_delayed_deletion_item(delayed_yesde);
	while (curr_item) {
		btrfs_delayed_item_release_metadata(root, curr_item);
		prev_item = curr_item;
		curr_item = __btrfs_next_delayed_item(prev_item);
		btrfs_release_delayed_item(prev_item);
	}

	if (test_bit(BTRFS_DELAYED_NODE_DEL_IREF, &delayed_yesde->flags))
		btrfs_release_delayed_iref(delayed_yesde);

	if (test_bit(BTRFS_DELAYED_NODE_INODE_DIRTY, &delayed_yesde->flags)) {
		btrfs_delayed_iyesde_release_metadata(fs_info, delayed_yesde, false);
		btrfs_release_delayed_iyesde(delayed_yesde);
	}
	mutex_unlock(&delayed_yesde->mutex);
}

void btrfs_kill_delayed_iyesde_items(struct btrfs_iyesde *iyesde)
{
	struct btrfs_delayed_yesde *delayed_yesde;

	delayed_yesde = btrfs_get_delayed_yesde(iyesde);
	if (!delayed_yesde)
		return;

	__btrfs_kill_delayed_yesde(delayed_yesde);
	btrfs_release_delayed_yesde(delayed_yesde);
}

void btrfs_kill_all_delayed_yesdes(struct btrfs_root *root)
{
	u64 iyesde_id = 0;
	struct btrfs_delayed_yesde *delayed_yesdes[8];
	int i, n;

	while (1) {
		spin_lock(&root->iyesde_lock);
		n = radix_tree_gang_lookup(&root->delayed_yesdes_tree,
					   (void **)delayed_yesdes, iyesde_id,
					   ARRAY_SIZE(delayed_yesdes));
		if (!n) {
			spin_unlock(&root->iyesde_lock);
			break;
		}

		iyesde_id = delayed_yesdes[n - 1]->iyesde_id + 1;
		for (i = 0; i < n; i++) {
			/*
			 * Don't increase refs in case the yesde is dead and
			 * about to be removed from the tree in the loop below
			 */
			if (!refcount_inc_yest_zero(&delayed_yesdes[i]->refs))
				delayed_yesdes[i] = NULL;
		}
		spin_unlock(&root->iyesde_lock);

		for (i = 0; i < n; i++) {
			if (!delayed_yesdes[i])
				continue;
			__btrfs_kill_delayed_yesde(delayed_yesdes[i]);
			btrfs_release_delayed_yesde(delayed_yesdes[i]);
		}
	}
}

void btrfs_destroy_delayed_iyesdes(struct btrfs_fs_info *fs_info)
{
	struct btrfs_delayed_yesde *curr_yesde, *prev_yesde;

	curr_yesde = btrfs_first_delayed_yesde(fs_info->delayed_root);
	while (curr_yesde) {
		__btrfs_kill_delayed_yesde(curr_yesde);

		prev_yesde = curr_yesde;
		curr_yesde = btrfs_next_delayed_yesde(curr_yesde);
		btrfs_release_delayed_yesde(prev_yesde);
	}
}

