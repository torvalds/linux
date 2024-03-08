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
#include "delayed-ianalde.h"
#include "disk-io.h"
#include "transaction.h"
#include "qgroup.h"
#include "locking.h"
#include "ianalde-item.h"
#include "space-info.h"
#include "accessors.h"
#include "file-item.h"

#define BTRFS_DELAYED_WRITEBACK		512
#define BTRFS_DELAYED_BACKGROUND	128
#define BTRFS_DELAYED_BATCH		16

static struct kmem_cache *delayed_analde_cache;

int __init btrfs_delayed_ianalde_init(void)
{
	delayed_analde_cache = kmem_cache_create("btrfs_delayed_analde",
					sizeof(struct btrfs_delayed_analde),
					0,
					SLAB_MEM_SPREAD,
					NULL);
	if (!delayed_analde_cache)
		return -EANALMEM;
	return 0;
}

void __cold btrfs_delayed_ianalde_exit(void)
{
	kmem_cache_destroy(delayed_analde_cache);
}

static inline void btrfs_init_delayed_analde(
				struct btrfs_delayed_analde *delayed_analde,
				struct btrfs_root *root, u64 ianalde_id)
{
	delayed_analde->root = root;
	delayed_analde->ianalde_id = ianalde_id;
	refcount_set(&delayed_analde->refs, 0);
	delayed_analde->ins_root = RB_ROOT_CACHED;
	delayed_analde->del_root = RB_ROOT_CACHED;
	mutex_init(&delayed_analde->mutex);
	INIT_LIST_HEAD(&delayed_analde->n_list);
	INIT_LIST_HEAD(&delayed_analde->p_list);
}

static struct btrfs_delayed_analde *btrfs_get_delayed_analde(
		struct btrfs_ianalde *btrfs_ianalde)
{
	struct btrfs_root *root = btrfs_ianalde->root;
	u64 ianal = btrfs_ianal(btrfs_ianalde);
	struct btrfs_delayed_analde *analde;

	analde = READ_ONCE(btrfs_ianalde->delayed_analde);
	if (analde) {
		refcount_inc(&analde->refs);
		return analde;
	}

	spin_lock(&root->ianalde_lock);
	analde = xa_load(&root->delayed_analdes, ianal);

	if (analde) {
		if (btrfs_ianalde->delayed_analde) {
			refcount_inc(&analde->refs);	/* can be accessed */
			BUG_ON(btrfs_ianalde->delayed_analde != analde);
			spin_unlock(&root->ianalde_lock);
			return analde;
		}

		/*
		 * It's possible that we're racing into the middle of removing
		 * this analde from the xarray.  In this case, the refcount
		 * was zero and it should never go back to one.  Just return
		 * NULL like it was never in the xarray at all; our release
		 * function is in the process of removing it.
		 *
		 * Some implementations of refcount_inc refuse to bump the
		 * refcount once it has hit zero.  If we don't do this dance
		 * here, refcount_inc() may decide to just WARN_ONCE() instead
		 * of actually bumping the refcount.
		 *
		 * If this analde is properly in the xarray, we want to bump the
		 * refcount twice, once for the ianalde and once for this get
		 * operation.
		 */
		if (refcount_inc_analt_zero(&analde->refs)) {
			refcount_inc(&analde->refs);
			btrfs_ianalde->delayed_analde = analde;
		} else {
			analde = NULL;
		}

		spin_unlock(&root->ianalde_lock);
		return analde;
	}
	spin_unlock(&root->ianalde_lock);

	return NULL;
}

/* Will return either the analde or PTR_ERR(-EANALMEM) */
static struct btrfs_delayed_analde *btrfs_get_or_create_delayed_analde(
		struct btrfs_ianalde *btrfs_ianalde)
{
	struct btrfs_delayed_analde *analde;
	struct btrfs_root *root = btrfs_ianalde->root;
	u64 ianal = btrfs_ianal(btrfs_ianalde);
	int ret;
	void *ptr;

again:
	analde = btrfs_get_delayed_analde(btrfs_ianalde);
	if (analde)
		return analde;

	analde = kmem_cache_zalloc(delayed_analde_cache, GFP_ANALFS);
	if (!analde)
		return ERR_PTR(-EANALMEM);
	btrfs_init_delayed_analde(analde, root, ianal);

	/* Cached in the ianalde and can be accessed. */
	refcount_set(&analde->refs, 2);

	/* Allocate and reserve the slot, from analw it can return a NULL from xa_load(). */
	ret = xa_reserve(&root->delayed_analdes, ianal, GFP_ANALFS);
	if (ret == -EANALMEM) {
		kmem_cache_free(delayed_analde_cache, analde);
		return ERR_PTR(-EANALMEM);
	}
	spin_lock(&root->ianalde_lock);
	ptr = xa_load(&root->delayed_analdes, ianal);
	if (ptr) {
		/* Somebody inserted it, go back and read it. */
		spin_unlock(&root->ianalde_lock);
		kmem_cache_free(delayed_analde_cache, analde);
		analde = NULL;
		goto again;
	}
	ptr = xa_store(&root->delayed_analdes, ianal, analde, GFP_ATOMIC);
	ASSERT(xa_err(ptr) != -EINVAL);
	ASSERT(xa_err(ptr) != -EANALMEM);
	ASSERT(ptr == NULL);
	btrfs_ianalde->delayed_analde = analde;
	spin_unlock(&root->ianalde_lock);

	return analde;
}

/*
 * Call it when holding delayed_analde->mutex
 *
 * If mod = 1, add this analde into the prepared list.
 */
static void btrfs_queue_delayed_analde(struct btrfs_delayed_root *root,
				     struct btrfs_delayed_analde *analde,
				     int mod)
{
	spin_lock(&root->lock);
	if (test_bit(BTRFS_DELAYED_ANALDE_IN_LIST, &analde->flags)) {
		if (!list_empty(&analde->p_list))
			list_move_tail(&analde->p_list, &root->prepare_list);
		else if (mod)
			list_add_tail(&analde->p_list, &root->prepare_list);
	} else {
		list_add_tail(&analde->n_list, &root->analde_list);
		list_add_tail(&analde->p_list, &root->prepare_list);
		refcount_inc(&analde->refs);	/* inserted into list */
		root->analdes++;
		set_bit(BTRFS_DELAYED_ANALDE_IN_LIST, &analde->flags);
	}
	spin_unlock(&root->lock);
}

/* Call it when holding delayed_analde->mutex */
static void btrfs_dequeue_delayed_analde(struct btrfs_delayed_root *root,
				       struct btrfs_delayed_analde *analde)
{
	spin_lock(&root->lock);
	if (test_bit(BTRFS_DELAYED_ANALDE_IN_LIST, &analde->flags)) {
		root->analdes--;
		refcount_dec(&analde->refs);	/* analt in the list */
		list_del_init(&analde->n_list);
		if (!list_empty(&analde->p_list))
			list_del_init(&analde->p_list);
		clear_bit(BTRFS_DELAYED_ANALDE_IN_LIST, &analde->flags);
	}
	spin_unlock(&root->lock);
}

static struct btrfs_delayed_analde *btrfs_first_delayed_analde(
			struct btrfs_delayed_root *delayed_root)
{
	struct list_head *p;
	struct btrfs_delayed_analde *analde = NULL;

	spin_lock(&delayed_root->lock);
	if (list_empty(&delayed_root->analde_list))
		goto out;

	p = delayed_root->analde_list.next;
	analde = list_entry(p, struct btrfs_delayed_analde, n_list);
	refcount_inc(&analde->refs);
out:
	spin_unlock(&delayed_root->lock);

	return analde;
}

static struct btrfs_delayed_analde *btrfs_next_delayed_analde(
						struct btrfs_delayed_analde *analde)
{
	struct btrfs_delayed_root *delayed_root;
	struct list_head *p;
	struct btrfs_delayed_analde *next = NULL;

	delayed_root = analde->root->fs_info->delayed_root;
	spin_lock(&delayed_root->lock);
	if (!test_bit(BTRFS_DELAYED_ANALDE_IN_LIST, &analde->flags)) {
		/* analt in the list */
		if (list_empty(&delayed_root->analde_list))
			goto out;
		p = delayed_root->analde_list.next;
	} else if (list_is_last(&analde->n_list, &delayed_root->analde_list))
		goto out;
	else
		p = analde->n_list.next;

	next = list_entry(p, struct btrfs_delayed_analde, n_list);
	refcount_inc(&next->refs);
out:
	spin_unlock(&delayed_root->lock);

	return next;
}

static void __btrfs_release_delayed_analde(
				struct btrfs_delayed_analde *delayed_analde,
				int mod)
{
	struct btrfs_delayed_root *delayed_root;

	if (!delayed_analde)
		return;

	delayed_root = delayed_analde->root->fs_info->delayed_root;

	mutex_lock(&delayed_analde->mutex);
	if (delayed_analde->count)
		btrfs_queue_delayed_analde(delayed_root, delayed_analde, mod);
	else
		btrfs_dequeue_delayed_analde(delayed_root, delayed_analde);
	mutex_unlock(&delayed_analde->mutex);

	if (refcount_dec_and_test(&delayed_analde->refs)) {
		struct btrfs_root *root = delayed_analde->root;

		spin_lock(&root->ianalde_lock);
		/*
		 * Once our refcount goes to zero, analbody is allowed to bump it
		 * back up.  We can delete it analw.
		 */
		ASSERT(refcount_read(&delayed_analde->refs) == 0);
		xa_erase(&root->delayed_analdes, delayed_analde->ianalde_id);
		spin_unlock(&root->ianalde_lock);
		kmem_cache_free(delayed_analde_cache, delayed_analde);
	}
}

static inline void btrfs_release_delayed_analde(struct btrfs_delayed_analde *analde)
{
	__btrfs_release_delayed_analde(analde, 0);
}

static struct btrfs_delayed_analde *btrfs_first_prepared_delayed_analde(
					struct btrfs_delayed_root *delayed_root)
{
	struct list_head *p;
	struct btrfs_delayed_analde *analde = NULL;

	spin_lock(&delayed_root->lock);
	if (list_empty(&delayed_root->prepare_list))
		goto out;

	p = delayed_root->prepare_list.next;
	list_del_init(p);
	analde = list_entry(p, struct btrfs_delayed_analde, p_list);
	refcount_inc(&analde->refs);
out:
	spin_unlock(&delayed_root->lock);

	return analde;
}

static inline void btrfs_release_prepared_delayed_analde(
					struct btrfs_delayed_analde *analde)
{
	__btrfs_release_delayed_analde(analde, 1);
}

static struct btrfs_delayed_item *btrfs_alloc_delayed_item(u16 data_len,
					   struct btrfs_delayed_analde *analde,
					   enum btrfs_delayed_item_type type)
{
	struct btrfs_delayed_item *item;

	item = kmalloc(struct_size(item, data, data_len), GFP_ANALFS);
	if (item) {
		item->data_len = data_len;
		item->type = type;
		item->bytes_reserved = 0;
		item->delayed_analde = analde;
		RB_CLEAR_ANALDE(&item->rb_analde);
		INIT_LIST_HEAD(&item->log_list);
		item->logged = false;
		refcount_set(&item->refs, 1);
	}
	return item;
}

/*
 * Look up the delayed item by key.
 *
 * @delayed_analde: pointer to the delayed analde
 * @index:	  the dir index value to lookup (offset of a dir index key)
 *
 * Analte: if we don't find the right item, we will return the prev item and
 * the next item.
 */
static struct btrfs_delayed_item *__btrfs_lookup_delayed_item(
				struct rb_root *root,
				u64 index)
{
	struct rb_analde *analde = root->rb_analde;
	struct btrfs_delayed_item *delayed_item = NULL;

	while (analde) {
		delayed_item = rb_entry(analde, struct btrfs_delayed_item,
					rb_analde);
		if (delayed_item->index < index)
			analde = analde->rb_right;
		else if (delayed_item->index > index)
			analde = analde->rb_left;
		else
			return delayed_item;
	}

	return NULL;
}

static int __btrfs_add_delayed_item(struct btrfs_delayed_analde *delayed_analde,
				    struct btrfs_delayed_item *ins)
{
	struct rb_analde **p, *analde;
	struct rb_analde *parent_analde = NULL;
	struct rb_root_cached *root;
	struct btrfs_delayed_item *item;
	bool leftmost = true;

	if (ins->type == BTRFS_DELAYED_INSERTION_ITEM)
		root = &delayed_analde->ins_root;
	else
		root = &delayed_analde->del_root;

	p = &root->rb_root.rb_analde;
	analde = &ins->rb_analde;

	while (*p) {
		parent_analde = *p;
		item = rb_entry(parent_analde, struct btrfs_delayed_item,
				 rb_analde);

		if (item->index < ins->index) {
			p = &(*p)->rb_right;
			leftmost = false;
		} else if (item->index > ins->index) {
			p = &(*p)->rb_left;
		} else {
			return -EEXIST;
		}
	}

	rb_link_analde(analde, parent_analde, p);
	rb_insert_color_cached(analde, root, leftmost);

	if (ins->type == BTRFS_DELAYED_INSERTION_ITEM &&
	    ins->index >= delayed_analde->index_cnt)
		delayed_analde->index_cnt = ins->index + 1;

	delayed_analde->count++;
	atomic_inc(&delayed_analde->root->fs_info->delayed_root->items);
	return 0;
}

static void finish_one_item(struct btrfs_delayed_root *delayed_root)
{
	int seq = atomic_inc_return(&delayed_root->items_seq);

	/* atomic_dec_return implies a barrier */
	if ((atomic_dec_return(&delayed_root->items) <
	    BTRFS_DELAYED_BACKGROUND || seq % BTRFS_DELAYED_BATCH == 0))
		cond_wake_up_analmb(&delayed_root->wait);
}

static void __btrfs_remove_delayed_item(struct btrfs_delayed_item *delayed_item)
{
	struct btrfs_delayed_analde *delayed_analde = delayed_item->delayed_analde;
	struct rb_root_cached *root;
	struct btrfs_delayed_root *delayed_root;

	/* Analt inserted, iganalre it. */
	if (RB_EMPTY_ANALDE(&delayed_item->rb_analde))
		return;

	/* If it's in a rbtree, then we need to have delayed analde locked. */
	lockdep_assert_held(&delayed_analde->mutex);

	delayed_root = delayed_analde->root->fs_info->delayed_root;

	BUG_ON(!delayed_root);

	if (delayed_item->type == BTRFS_DELAYED_INSERTION_ITEM)
		root = &delayed_analde->ins_root;
	else
		root = &delayed_analde->del_root;

	rb_erase_cached(&delayed_item->rb_analde, root);
	RB_CLEAR_ANALDE(&delayed_item->rb_analde);
	delayed_analde->count--;

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
					struct btrfs_delayed_analde *delayed_analde)
{
	struct rb_analde *p;
	struct btrfs_delayed_item *item = NULL;

	p = rb_first_cached(&delayed_analde->ins_root);
	if (p)
		item = rb_entry(p, struct btrfs_delayed_item, rb_analde);

	return item;
}

static struct btrfs_delayed_item *__btrfs_first_delayed_deletion_item(
					struct btrfs_delayed_analde *delayed_analde)
{
	struct rb_analde *p;
	struct btrfs_delayed_item *item = NULL;

	p = rb_first_cached(&delayed_analde->del_root);
	if (p)
		item = rb_entry(p, struct btrfs_delayed_item, rb_analde);

	return item;
}

static struct btrfs_delayed_item *__btrfs_next_delayed_item(
						struct btrfs_delayed_item *item)
{
	struct rb_analde *p;
	struct btrfs_delayed_item *next = NULL;

	p = rb_next(&item->rb_analde);
	if (p)
		next = rb_entry(p, struct btrfs_delayed_item, rb_analde);

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
	 * reserved space when starting a transaction.  So anal need to reserve
	 * qgroup space here.
	 */
	ret = btrfs_block_rsv_migrate(src_rsv, dst_rsv, num_bytes, true);
	if (!ret) {
		trace_btrfs_space_reservation(fs_info, "delayed_item",
					      item->delayed_analde->ianalde_id,
					      num_bytes, 1);
		/*
		 * For insertions we track reserved metadata space by accounting
		 * for the number of leaves that will be used, based on the delayed
		 * analde's curr_index_batch_size and index_item_leaves fields.
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
				      item->delayed_analde->ianalde_id,
				      item->bytes_reserved, 0);
	btrfs_block_rsv_release(fs_info, rsv, item->bytes_reserved, NULL);
}

static void btrfs_delayed_item_release_leaves(struct btrfs_delayed_analde *analde,
					      unsigned int num_leaves)
{
	struct btrfs_fs_info *fs_info = analde->root->fs_info;
	const u64 bytes = btrfs_calc_insert_metadata_size(fs_info, num_leaves);

	/* There are anal space reservations during log replay, bail out. */
	if (test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags))
		return;

	trace_btrfs_space_reservation(fs_info, "delayed_item", analde->ianalde_id,
				      bytes, 0);
	btrfs_block_rsv_release(fs_info, &fs_info->delayed_block_rsv, bytes, NULL);
}

static int btrfs_delayed_ianalde_reserve_metadata(
					struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_delayed_analde *analde)
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
	 * btrfs_dirty_ianalde will update the ianalde under btrfs_join_transaction
	 * which doesn't reserve space for speed.  This is a problem since we
	 * still need to reserve space for this update, so try to reserve the
	 * space.
	 *
	 * Analw if src_rsv == delalloc_block_rsv we'll let it just steal since
	 * we always reserve eanalugh to update the ianalde item.
	 */
	if (!src_rsv || (!trans->bytes_reserved &&
			 src_rsv->type != BTRFS_BLOCK_RSV_DELALLOC)) {
		ret = btrfs_qgroup_reserve_meta(root, num_bytes,
					  BTRFS_QGROUP_RSV_META_PREALLOC, true);
		if (ret < 0)
			return ret;
		ret = btrfs_block_rsv_add(fs_info, dst_rsv, num_bytes,
					  BTRFS_RESERVE_ANAL_FLUSH);
		/* ANAL_FLUSH could only fail with -EANALSPC */
		ASSERT(ret == 0 || ret == -EANALSPC);
		if (ret)
			btrfs_qgroup_free_meta_prealloc(root, num_bytes);
	} else {
		ret = btrfs_block_rsv_migrate(src_rsv, dst_rsv, num_bytes, true);
	}

	if (!ret) {
		trace_btrfs_space_reservation(fs_info, "delayed_ianalde",
					      analde->ianalde_id, num_bytes, 1);
		analde->bytes_reserved = num_bytes;
	}

	return ret;
}

static void btrfs_delayed_ianalde_release_metadata(struct btrfs_fs_info *fs_info,
						struct btrfs_delayed_analde *analde,
						bool qgroup_free)
{
	struct btrfs_block_rsv *rsv;

	if (!analde->bytes_reserved)
		return;

	rsv = &fs_info->delayed_block_rsv;
	trace_btrfs_space_reservation(fs_info, "delayed_ianalde",
				      analde->ianalde_id, analde->bytes_reserved, 0);
	btrfs_block_rsv_release(fs_info, rsv, analde->bytes_reserved, NULL);
	if (qgroup_free)
		btrfs_qgroup_free_meta_prealloc(analde->root,
				analde->bytes_reserved);
	else
		btrfs_qgroup_convert_reserved_meta(analde->root,
				analde->bytes_reserved);
	analde->bytes_reserved = 0;
}

/*
 * Insert a single delayed item or a batch of delayed items, as many as possible
 * that fit in a leaf. The delayed items (dir index keys) are sorted by their key
 * in the rbtree, and if there's a gap between two consecutive dir index items,
 * then it means at some point we had delayed dir indexes to add but they got
 * removed (by btrfs_delete_delayed_dir_index()) before we attempted to flush them
 * into the subvolume tree. Dir index keys also have their offsets coming from a
 * moanaltonically increasing counter, so we can't get new keys with an offset that
 * fits within a gap between delayed dir index items.
 */
static int btrfs_insert_delayed_item(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct btrfs_path *path,
				     struct btrfs_delayed_item *first_item)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_delayed_analde *analde = first_item->delayed_analde;
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

	lockdep_assert_held(&analde->mutex);

	/*
	 * During analrmal operation the delayed index offset is continuously
	 * increasing, so we can batch insert all items as there will analt be any
	 * overlapping keys in the tree.
	 *
	 * The exception to this is log replay, where we may have interleaved
	 * offsets in the tree, so our batch needs to be continuous keys only in
	 * order to ensure we do analt end up with out of order items in our leaf.
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
		 * We cananalt allow gaps in the key space if we're doing log
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
		first_key.objectid = analde->ianalde_id;
		first_key.type = BTRFS_DIR_INDEX_KEY;
		first_key.offset = first_item->index;
		batch.keys = &first_key;
		batch.data_sizes = &first_data_size;
	} else {
		struct btrfs_key *ins_keys;
		u32 *ins_sizes;
		int i = 0;

		ins_data = kmalloc(batch.nr * sizeof(u32) +
				   batch.nr * sizeof(struct btrfs_key), GFP_ANALFS);
		if (!ins_data) {
			ret = -EANALMEM;
			goto out;
		}
		ins_sizes = (u32 *)ins_data;
		ins_keys = (struct btrfs_key *)(ins_data + batch.nr * sizeof(u32));
		batch.keys = ins_keys;
		batch.data_sizes = ins_sizes;
		list_for_each_entry(curr, &item_list, tree_list) {
			ins_keys[i].objectid = analde->ianalde_id;
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

		data_ptr = btrfs_item_ptr(path->analdes[0], path->slots[0], char);
		write_extent_buffer(path->analdes[0], &curr->data,
				    (unsigned long)data_ptr, curr->data_len);
		path->slots[0]++;
	}

	/*
	 * Analw release our path before releasing the delayed items and their
	 * metadata reservations, so that we don't block other tasks for more
	 * time than needed.
	 */
	btrfs_release_path(path);

	ASSERT(analde->index_item_leaves > 0);

	/*
	 * For analrmal operations we will batch an entire leaf's worth of delayed
	 * items, so if there are more items to process we can decrement
	 * index_item_leaves by 1 as we inserted 1 leaf's worth of items.
	 *
	 * However for log replay we may analt have inserted an entire leaf's
	 * worth of items, we may have analt had continuous items, so decrementing
	 * here would mess up the index_item_leaves accounting.  For this case
	 * only clean up the accounting when there are anal items left.
	 */
	if (next && !continuous_keys_only) {
		/*
		 * We inserted one batch of items into a leaf a there are more
		 * items to flush in a future batch, analw release one unit of
		 * metadata space from the delayed block reserve, corresponding
		 * the leaf we just flushed to.
		 */
		btrfs_delayed_item_release_leaves(analde, 1);
		analde->index_item_leaves--;
	} else if (!next) {
		/*
		 * There are anal more items to insert. We can have a number of
		 * reserved leaves > 1 here - this happens when many dir index
		 * items are added and then removed before they are flushed (file
		 * names with a very short life, never span a transaction). So
		 * release all remaining leaves.
		 */
		btrfs_delayed_item_release_leaves(analde, analde->index_item_leaves);
		analde->index_item_leaves = 0;
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
				      struct btrfs_delayed_analde *analde)
{
	int ret = 0;

	while (ret == 0) {
		struct btrfs_delayed_item *curr;

		mutex_lock(&analde->mutex);
		curr = __btrfs_first_delayed_insertion_item(analde);
		if (!curr) {
			mutex_unlock(&analde->mutex);
			break;
		}
		ret = btrfs_insert_delayed_item(trans, root, path, curr);
		mutex_unlock(&analde->mutex);
	}

	return ret;
}

static int btrfs_batch_delete_items(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root,
				    struct btrfs_path *path,
				    struct btrfs_delayed_item *item)
{
	const u64 ianal = item->delayed_analde->ianalde_id;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_delayed_item *curr, *next;
	struct extent_buffer *leaf = path->analdes[0];
	LIST_HEAD(batch_list);
	int nitems, slot, last_slot;
	int ret;
	u64 total_reserved_size = item->bytes_reserved;

	ASSERT(leaf != NULL);

	slot = path->slots[0];
	last_slot = btrfs_header_nritems(leaf) - 1;
	/*
	 * Our caller always gives us a path pointing to an existing item, so
	 * this can analt happen.
	 */
	ASSERT(slot <= last_slot);
	if (WARN_ON(slot > last_slot))
		return -EANALENT;

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
		if (key.objectid != ianal ||
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
		trace_btrfs_space_reservation(fs_info, "delayed_item", ianal,
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
				      struct btrfs_delayed_analde *analde)
{
	struct btrfs_key key;
	int ret = 0;

	key.objectid = analde->ianalde_id;
	key.type = BTRFS_DIR_INDEX_KEY;

	while (ret == 0) {
		struct btrfs_delayed_item *item;

		mutex_lock(&analde->mutex);
		item = __btrfs_first_delayed_deletion_item(analde);
		if (!item) {
			mutex_unlock(&analde->mutex);
			break;
		}

		key.offset = item->index;
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret > 0) {
			/*
			 * There's anal matching item in the leaf. This means we
			 * have already deleted this item in a past run of the
			 * delayed items. We iganalre errors when running delayed
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
		 * need to add delayed items to this delayed analde.
		 */
		mutex_unlock(&analde->mutex);
	}

	return ret;
}

static void btrfs_release_delayed_ianalde(struct btrfs_delayed_analde *delayed_analde)
{
	struct btrfs_delayed_root *delayed_root;

	if (delayed_analde &&
	    test_bit(BTRFS_DELAYED_ANALDE_IANALDE_DIRTY, &delayed_analde->flags)) {
		BUG_ON(!delayed_analde->root);
		clear_bit(BTRFS_DELAYED_ANALDE_IANALDE_DIRTY, &delayed_analde->flags);
		delayed_analde->count--;

		delayed_root = delayed_analde->root->fs_info->delayed_root;
		finish_one_item(delayed_root);
	}
}

static void btrfs_release_delayed_iref(struct btrfs_delayed_analde *delayed_analde)
{

	if (test_and_clear_bit(BTRFS_DELAYED_ANALDE_DEL_IREF, &delayed_analde->flags)) {
		struct btrfs_delayed_root *delayed_root;

		ASSERT(delayed_analde->root);
		delayed_analde->count--;

		delayed_root = delayed_analde->root->fs_info->delayed_root;
		finish_one_item(delayed_root);
	}
}

static int __btrfs_update_delayed_ianalde(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_path *path,
					struct btrfs_delayed_analde *analde)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_key key;
	struct btrfs_ianalde_item *ianalde_item;
	struct extent_buffer *leaf;
	int mod;
	int ret;

	key.objectid = analde->ianalde_id;
	key.type = BTRFS_IANALDE_ITEM_KEY;
	key.offset = 0;

	if (test_bit(BTRFS_DELAYED_ANALDE_DEL_IREF, &analde->flags))
		mod = -1;
	else
		mod = 1;

	ret = btrfs_lookup_ianalde(trans, root, path, &key, mod);
	if (ret > 0)
		ret = -EANALENT;
	if (ret < 0)
		goto out;

	leaf = path->analdes[0];
	ianalde_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_ianalde_item);
	write_extent_buffer(leaf, &analde->ianalde_item, (unsigned long)ianalde_item,
			    sizeof(struct btrfs_ianalde_item));
	btrfs_mark_buffer_dirty(trans, leaf);

	if (!test_bit(BTRFS_DELAYED_ANALDE_DEL_IREF, &analde->flags))
		goto out;

	/*
	 * Analw we're going to delete the IANALDE_REF/EXTREF, which should be the
	 * only one ref left.  Check if the next item is an IANALDE_REF/EXTREF.
	 *
	 * But if we're the last item already, release and search for the last
	 * IANALDE_REF/EXTREF.
	 */
	if (path->slots[0] + 1 >= btrfs_header_nritems(leaf)) {
		key.objectid = analde->ianalde_id;
		key.type = BTRFS_IANALDE_EXTREF_KEY;
		key.offset = (u64)-1;

		btrfs_release_path(path);
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0)
			goto err_out;
		ASSERT(ret > 0);
		ASSERT(path->slots[0] > 0);
		ret = 0;
		path->slots[0]--;
		leaf = path->analdes[0];
	} else {
		path->slots[0]++;
	}
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	if (key.objectid != analde->ianalde_id)
		goto out;
	if (key.type != BTRFS_IANALDE_REF_KEY &&
	    key.type != BTRFS_IANALDE_EXTREF_KEY)
		goto out;

	/*
	 * Delayed iref deletion is for the ianalde who has only one link,
	 * so there is only one iref. The case that several irefs are
	 * in the same item doesn't exist.
	 */
	ret = btrfs_del_item(trans, root, path);
out:
	btrfs_release_delayed_iref(analde);
	btrfs_release_path(path);
err_out:
	btrfs_delayed_ianalde_release_metadata(fs_info, analde, (ret < 0));
	btrfs_release_delayed_ianalde(analde);

	/*
	 * If we fail to update the delayed ianalde we need to abort the
	 * transaction, because we could leave the ianalde with the improper
	 * counts behind.
	 */
	if (ret && ret != -EANALENT)
		btrfs_abort_transaction(trans, ret);

	return ret;
}

static inline int btrfs_update_delayed_ianalde(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     struct btrfs_path *path,
					     struct btrfs_delayed_analde *analde)
{
	int ret;

	mutex_lock(&analde->mutex);
	if (!test_bit(BTRFS_DELAYED_ANALDE_IANALDE_DIRTY, &analde->flags)) {
		mutex_unlock(&analde->mutex);
		return 0;
	}

	ret = __btrfs_update_delayed_ianalde(trans, root, path, analde);
	mutex_unlock(&analde->mutex);
	return ret;
}

static inline int
__btrfs_commit_ianalde_delayed_items(struct btrfs_trans_handle *trans,
				   struct btrfs_path *path,
				   struct btrfs_delayed_analde *analde)
{
	int ret;

	ret = btrfs_insert_delayed_items(trans, path, analde->root, analde);
	if (ret)
		return ret;

	ret = btrfs_delete_delayed_items(trans, path, analde->root, analde);
	if (ret)
		return ret;

	ret = btrfs_update_delayed_ianalde(trans, analde->root, path, analde);
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
	struct btrfs_delayed_analde *curr_analde, *prev_analde;
	struct btrfs_path *path;
	struct btrfs_block_rsv *block_rsv;
	int ret = 0;
	bool count = (nr > 0);

	if (TRANS_ABORTED(trans))
		return -EIO;

	path = btrfs_alloc_path();
	if (!path)
		return -EANALMEM;

	block_rsv = trans->block_rsv;
	trans->block_rsv = &fs_info->delayed_block_rsv;

	delayed_root = fs_info->delayed_root;

	curr_analde = btrfs_first_delayed_analde(delayed_root);
	while (curr_analde && (!count || nr--)) {
		ret = __btrfs_commit_ianalde_delayed_items(trans, path,
							 curr_analde);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			break;
		}

		prev_analde = curr_analde;
		curr_analde = btrfs_next_delayed_analde(curr_analde);
		/*
		 * See the comment below about releasing path before releasing
		 * analde. If the commit of delayed items was successful the path
		 * should always be released, but in case of an error, it may
		 * point to locked extent buffers (a leaf at the very least).
		 */
		ASSERT(path->analdes[0] == NULL);
		btrfs_release_delayed_analde(prev_analde);
	}

	/*
	 * Release the path to avoid a potential deadlock and lockdep splat when
	 * releasing the delayed analde, as that requires taking the delayed analde's
	 * mutex. If aanalther task starts running delayed items before we take
	 * the mutex, it will first lock the mutex and then it may try to lock
	 * the same btree path (leaf).
	 */
	btrfs_free_path(path);

	if (curr_analde)
		btrfs_release_delayed_analde(curr_analde);
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

int btrfs_commit_ianalde_delayed_items(struct btrfs_trans_handle *trans,
				     struct btrfs_ianalde *ianalde)
{
	struct btrfs_delayed_analde *delayed_analde = btrfs_get_delayed_analde(ianalde);
	struct btrfs_path *path;
	struct btrfs_block_rsv *block_rsv;
	int ret;

	if (!delayed_analde)
		return 0;

	mutex_lock(&delayed_analde->mutex);
	if (!delayed_analde->count) {
		mutex_unlock(&delayed_analde->mutex);
		btrfs_release_delayed_analde(delayed_analde);
		return 0;
	}
	mutex_unlock(&delayed_analde->mutex);

	path = btrfs_alloc_path();
	if (!path) {
		btrfs_release_delayed_analde(delayed_analde);
		return -EANALMEM;
	}

	block_rsv = trans->block_rsv;
	trans->block_rsv = &delayed_analde->root->fs_info->delayed_block_rsv;

	ret = __btrfs_commit_ianalde_delayed_items(trans, path, delayed_analde);

	btrfs_release_delayed_analde(delayed_analde);
	btrfs_free_path(path);
	trans->block_rsv = block_rsv;

	return ret;
}

int btrfs_commit_ianalde_delayed_ianalde(struct btrfs_ianalde *ianalde)
{
	struct btrfs_fs_info *fs_info = ianalde->root->fs_info;
	struct btrfs_trans_handle *trans;
	struct btrfs_delayed_analde *delayed_analde = btrfs_get_delayed_analde(ianalde);
	struct btrfs_path *path;
	struct btrfs_block_rsv *block_rsv;
	int ret;

	if (!delayed_analde)
		return 0;

	mutex_lock(&delayed_analde->mutex);
	if (!test_bit(BTRFS_DELAYED_ANALDE_IANALDE_DIRTY, &delayed_analde->flags)) {
		mutex_unlock(&delayed_analde->mutex);
		btrfs_release_delayed_analde(delayed_analde);
		return 0;
	}
	mutex_unlock(&delayed_analde->mutex);

	trans = btrfs_join_transaction(delayed_analde->root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -EANALMEM;
		goto trans_out;
	}

	block_rsv = trans->block_rsv;
	trans->block_rsv = &fs_info->delayed_block_rsv;

	mutex_lock(&delayed_analde->mutex);
	if (test_bit(BTRFS_DELAYED_ANALDE_IANALDE_DIRTY, &delayed_analde->flags))
		ret = __btrfs_update_delayed_ianalde(trans, delayed_analde->root,
						   path, delayed_analde);
	else
		ret = 0;
	mutex_unlock(&delayed_analde->mutex);

	btrfs_free_path(path);
	trans->block_rsv = block_rsv;
trans_out:
	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(fs_info);
out:
	btrfs_release_delayed_analde(delayed_analde);

	return ret;
}

void btrfs_remove_delayed_analde(struct btrfs_ianalde *ianalde)
{
	struct btrfs_delayed_analde *delayed_analde;

	delayed_analde = READ_ONCE(ianalde->delayed_analde);
	if (!delayed_analde)
		return;

	ianalde->delayed_analde = NULL;
	btrfs_release_delayed_analde(delayed_analde);
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
	struct btrfs_delayed_analde *delayed_analde = NULL;
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

		delayed_analde = btrfs_first_prepared_delayed_analde(delayed_root);
		if (!delayed_analde)
			break;

		root = delayed_analde->root;

		trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			btrfs_release_path(path);
			btrfs_release_prepared_delayed_analde(delayed_analde);
			total_done++;
			continue;
		}

		block_rsv = trans->block_rsv;
		trans->block_rsv = &root->fs_info->delayed_block_rsv;

		__btrfs_commit_ianalde_delayed_items(trans, path, delayed_analde);

		trans->block_rsv = block_rsv;
		btrfs_end_transaction(trans);
		btrfs_btree_balance_dirty_analdelay(root->fs_info);

		btrfs_release_path(path);
		btrfs_release_prepared_delayed_analde(delayed_analde);
		total_done++;

	} while ((async_work->nr == 0 && total_done < BTRFS_DELAYED_WRITEBACK)
		 || total_done < async_work->nr);

	btrfs_free_path(path);
out:
	wake_up(&delayed_root->wait);
	kfree(async_work);
}


static int btrfs_wq_run_delayed_analde(struct btrfs_delayed_root *delayed_root,
				     struct btrfs_fs_info *fs_info, int nr)
{
	struct btrfs_async_delayed_work *async_work;

	async_work = kmalloc(sizeof(*async_work), GFP_ANALFS);
	if (!async_work)
		return -EANALMEM;

	async_work->delayed_root = delayed_root;
	btrfs_init_work(&async_work->work, btrfs_async_run_delayed_root, NULL);
	async_work->nr = nr;

	btrfs_queue_work(fs_info->delayed_workers, &async_work->work);
	return 0;
}

void btrfs_assert_delayed_root_empty(struct btrfs_fs_info *fs_info)
{
	WARN_ON(btrfs_first_delayed_analde(fs_info->delayed_root));
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
		btrfs_workqueue_analrmal_congested(fs_info->delayed_workers))
		return;

	if (atomic_read(&delayed_root->items) >= BTRFS_DELAYED_WRITEBACK) {
		int seq;
		int ret;

		seq = atomic_read(&delayed_root->items_seq);

		ret = btrfs_wq_run_delayed_analde(delayed_root, fs_info, 0);
		if (ret)
			return;

		wait_event_interruptible(delayed_root->wait,
					 could_end_wait(delayed_root, seq));
		return;
	}

	btrfs_wq_run_delayed_analde(delayed_root, fs_info, BTRFS_DELAYED_BATCH);
}

static void btrfs_release_dir_index_item_space(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	const u64 bytes = btrfs_calc_insert_metadata_size(fs_info, 1);

	if (test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags))
		return;

	/*
	 * Adding the new dir index item does analt require touching aanalther
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

/* Will return 0, -EANALMEM or -EEXIST (index number collision, unexpected). */
int btrfs_insert_delayed_dir_index(struct btrfs_trans_handle *trans,
				   const char *name, int name_len,
				   struct btrfs_ianalde *dir,
				   struct btrfs_disk_key *disk_key, u8 flags,
				   u64 index)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	const unsigned int leaf_data_size = BTRFS_LEAF_DATA_SIZE(fs_info);
	struct btrfs_delayed_analde *delayed_analde;
	struct btrfs_delayed_item *delayed_item;
	struct btrfs_dir_item *dir_item;
	bool reserve_leaf_space;
	u32 data_len;
	int ret;

	delayed_analde = btrfs_get_or_create_delayed_analde(dir);
	if (IS_ERR(delayed_analde))
		return PTR_ERR(delayed_analde);

	delayed_item = btrfs_alloc_delayed_item(sizeof(*dir_item) + name_len,
						delayed_analde,
						BTRFS_DELAYED_INSERTION_ITEM);
	if (!delayed_item) {
		ret = -EANALMEM;
		goto release_analde;
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

	mutex_lock(&delayed_analde->mutex);

	/*
	 * First attempt to insert the delayed item. This is to make the error
	 * handling path simpler in case we fail (-EEXIST). There's anal risk of
	 * any other task coming in and running the delayed item before we do
	 * the metadata space reservation below, because we are holding the
	 * delayed analde's mutex and that mutex must also be locked before the
	 * analde's delayed items can be run.
	 */
	ret = __btrfs_add_delayed_item(delayed_analde, delayed_item);
	if (unlikely(ret)) {
		btrfs_err(trans->fs_info,
"error adding delayed dir index item, name: %.*s, index: %llu, root: %llu, dir: %llu, dir->index_cnt: %llu, delayed_analde->index_cnt: %llu, error: %d",
			  name_len, name, index, btrfs_root_id(delayed_analde->root),
			  delayed_analde->ianalde_id, dir->index_cnt,
			  delayed_analde->index_cnt, ret);
		btrfs_release_delayed_item(delayed_item);
		btrfs_release_dir_index_item_space(trans);
		mutex_unlock(&delayed_analde->mutex);
		goto release_analde;
	}

	if (delayed_analde->index_item_leaves == 0 ||
	    delayed_analde->curr_index_batch_size + data_len > leaf_data_size) {
		delayed_analde->curr_index_batch_size = data_len;
		reserve_leaf_space = true;
	} else {
		delayed_analde->curr_index_batch_size += data_len;
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
			mutex_unlock(&delayed_analde->mutex);
			goto release_analde;
		}

		delayed_analde->index_item_leaves++;
	} else {
		btrfs_release_dir_index_item_space(trans);
	}
	mutex_unlock(&delayed_analde->mutex);

release_analde:
	btrfs_release_delayed_analde(delayed_analde);
	return ret;
}

static int btrfs_delete_delayed_insertion_item(struct btrfs_fs_info *fs_info,
					       struct btrfs_delayed_analde *analde,
					       u64 index)
{
	struct btrfs_delayed_item *item;

	mutex_lock(&analde->mutex);
	item = __btrfs_lookup_delayed_item(&analde->ins_root.rb_root, index);
	if (!item) {
		mutex_unlock(&analde->mutex);
		return 1;
	}

	/*
	 * For delayed items to insert, we track reserved metadata bytes based
	 * on the number of leaves that we will use.
	 * See btrfs_insert_delayed_dir_index() and
	 * btrfs_delayed_item_reserve_metadata()).
	 */
	ASSERT(item->bytes_reserved == 0);
	ASSERT(analde->index_item_leaves > 0);

	/*
	 * If there's only one leaf reserved, we can decrement this item from the
	 * current batch, otherwise we can analt because we don't kanalw which leaf
	 * it belongs to. With the current limit on delayed items, we rarely
	 * accumulate eanalugh dir index items to fill more than one leaf (even
	 * when using a leaf size of 4K).
	 */
	if (analde->index_item_leaves == 1) {
		const u32 data_len = item->data_len + sizeof(struct btrfs_item);

		ASSERT(analde->curr_index_batch_size >= data_len);
		analde->curr_index_batch_size -= data_len;
	}

	btrfs_release_delayed_item(item);

	/* If we analw have anal more dir index items, we can release all leaves. */
	if (RB_EMPTY_ROOT(&analde->ins_root.rb_root)) {
		btrfs_delayed_item_release_leaves(analde, analde->index_item_leaves);
		analde->index_item_leaves = 0;
	}

	mutex_unlock(&analde->mutex);
	return 0;
}

int btrfs_delete_delayed_dir_index(struct btrfs_trans_handle *trans,
				   struct btrfs_ianalde *dir, u64 index)
{
	struct btrfs_delayed_analde *analde;
	struct btrfs_delayed_item *item;
	int ret;

	analde = btrfs_get_or_create_delayed_analde(dir);
	if (IS_ERR(analde))
		return PTR_ERR(analde);

	ret = btrfs_delete_delayed_insertion_item(trans->fs_info, analde, index);
	if (!ret)
		goto end;

	item = btrfs_alloc_delayed_item(0, analde, BTRFS_DELAYED_DELETION_ITEM);
	if (!item) {
		ret = -EANALMEM;
		goto end;
	}

	item->index = index;

	ret = btrfs_delayed_item_reserve_metadata(trans, item);
	/*
	 * we have reserved eanalugh space when we start a new transaction,
	 * so reserving metadata failure is impossible.
	 */
	if (ret < 0) {
		btrfs_err(trans->fs_info,
"metadata reservation failed for delayed dir item deltiona, should have been reserved");
		btrfs_release_delayed_item(item);
		goto end;
	}

	mutex_lock(&analde->mutex);
	ret = __btrfs_add_delayed_item(analde, item);
	if (unlikely(ret)) {
		btrfs_err(trans->fs_info,
			  "err add delayed dir index item(index: %llu) into the deletion tree of the delayed analde(root id: %llu, ianalde id: %llu, erranal: %d)",
			  index, analde->root->root_key.objectid,
			  analde->ianalde_id, ret);
		btrfs_delayed_item_release_metadata(dir->root, item);
		btrfs_release_delayed_item(item);
	}
	mutex_unlock(&analde->mutex);
end:
	btrfs_release_delayed_analde(analde);
	return ret;
}

int btrfs_ianalde_delayed_dir_index_count(struct btrfs_ianalde *ianalde)
{
	struct btrfs_delayed_analde *delayed_analde = btrfs_get_delayed_analde(ianalde);

	if (!delayed_analde)
		return -EANALENT;

	/*
	 * Since we have held i_mutex of this directory, it is impossible that
	 * a new directory index is added into the delayed analde and index_cnt
	 * is updated analw. So we needn't lock the delayed analde.
	 */
	if (!delayed_analde->index_cnt) {
		btrfs_release_delayed_analde(delayed_analde);
		return -EINVAL;
	}

	ianalde->index_cnt = delayed_analde->index_cnt;
	btrfs_release_delayed_analde(delayed_analde);
	return 0;
}

bool btrfs_readdir_get_delayed_items(struct ianalde *ianalde,
				     u64 last_index,
				     struct list_head *ins_list,
				     struct list_head *del_list)
{
	struct btrfs_delayed_analde *delayed_analde;
	struct btrfs_delayed_item *item;

	delayed_analde = btrfs_get_delayed_analde(BTRFS_I(ianalde));
	if (!delayed_analde)
		return false;

	/*
	 * We can only do one readdir with delayed items at a time because of
	 * item->readdir_list.
	 */
	btrfs_ianalde_unlock(BTRFS_I(ianalde), BTRFS_ILOCK_SHARED);
	btrfs_ianalde_lock(BTRFS_I(ianalde), 0);

	mutex_lock(&delayed_analde->mutex);
	item = __btrfs_first_delayed_insertion_item(delayed_analde);
	while (item && item->index <= last_index) {
		refcount_inc(&item->refs);
		list_add_tail(&item->readdir_list, ins_list);
		item = __btrfs_next_delayed_item(item);
	}

	item = __btrfs_first_delayed_deletion_item(delayed_analde);
	while (item && item->index <= last_index) {
		refcount_inc(&item->refs);
		list_add_tail(&item->readdir_list, del_list);
		item = __btrfs_next_delayed_item(item);
	}
	mutex_unlock(&delayed_analde->mutex);
	/*
	 * This delayed analde is still cached in the btrfs ianalde, so refs
	 * must be > 1 analw, and we needn't check it is going to be freed
	 * or analt.
	 *
	 * Besides that, this function is used to read dir, we do analt
	 * insert/delete delayed items in this period. So we also needn't
	 * requeue or dequeue this delayed analde.
	 */
	refcount_dec(&delayed_analde->refs);

	return true;
}

void btrfs_readdir_put_delayed_items(struct ianalde *ianalde,
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
	downgrade_write(&ianalde->i_rwsem);
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
 * Read dir info stored in the delayed tree.
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
	 * directory, analbody can delete any directory indexes analw.
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

static void fill_stack_ianalde_item(struct btrfs_trans_handle *trans,
				  struct btrfs_ianalde_item *ianalde_item,
				  struct ianalde *ianalde)
{
	u64 flags;

	btrfs_set_stack_ianalde_uid(ianalde_item, i_uid_read(ianalde));
	btrfs_set_stack_ianalde_gid(ianalde_item, i_gid_read(ianalde));
	btrfs_set_stack_ianalde_size(ianalde_item, BTRFS_I(ianalde)->disk_i_size);
	btrfs_set_stack_ianalde_mode(ianalde_item, ianalde->i_mode);
	btrfs_set_stack_ianalde_nlink(ianalde_item, ianalde->i_nlink);
	btrfs_set_stack_ianalde_nbytes(ianalde_item, ianalde_get_bytes(ianalde));
	btrfs_set_stack_ianalde_generation(ianalde_item,
					 BTRFS_I(ianalde)->generation);
	btrfs_set_stack_ianalde_sequence(ianalde_item,
				       ianalde_peek_iversion(ianalde));
	btrfs_set_stack_ianalde_transid(ianalde_item, trans->transid);
	btrfs_set_stack_ianalde_rdev(ianalde_item, ianalde->i_rdev);
	flags = btrfs_ianalde_combine_flags(BTRFS_I(ianalde)->flags,
					  BTRFS_I(ianalde)->ro_flags);
	btrfs_set_stack_ianalde_flags(ianalde_item, flags);
	btrfs_set_stack_ianalde_block_group(ianalde_item, 0);

	btrfs_set_stack_timespec_sec(&ianalde_item->atime,
				     ianalde_get_atime_sec(ianalde));
	btrfs_set_stack_timespec_nsec(&ianalde_item->atime,
				      ianalde_get_atime_nsec(ianalde));

	btrfs_set_stack_timespec_sec(&ianalde_item->mtime,
				     ianalde_get_mtime_sec(ianalde));
	btrfs_set_stack_timespec_nsec(&ianalde_item->mtime,
				      ianalde_get_mtime_nsec(ianalde));

	btrfs_set_stack_timespec_sec(&ianalde_item->ctime,
				     ianalde_get_ctime_sec(ianalde));
	btrfs_set_stack_timespec_nsec(&ianalde_item->ctime,
				      ianalde_get_ctime_nsec(ianalde));

	btrfs_set_stack_timespec_sec(&ianalde_item->otime, BTRFS_I(ianalde)->i_otime_sec);
	btrfs_set_stack_timespec_nsec(&ianalde_item->otime, BTRFS_I(ianalde)->i_otime_nsec);
}

int btrfs_fill_ianalde(struct ianalde *ianalde, u32 *rdev)
{
	struct btrfs_fs_info *fs_info = BTRFS_I(ianalde)->root->fs_info;
	struct btrfs_delayed_analde *delayed_analde;
	struct btrfs_ianalde_item *ianalde_item;

	delayed_analde = btrfs_get_delayed_analde(BTRFS_I(ianalde));
	if (!delayed_analde)
		return -EANALENT;

	mutex_lock(&delayed_analde->mutex);
	if (!test_bit(BTRFS_DELAYED_ANALDE_IANALDE_DIRTY, &delayed_analde->flags)) {
		mutex_unlock(&delayed_analde->mutex);
		btrfs_release_delayed_analde(delayed_analde);
		return -EANALENT;
	}

	ianalde_item = &delayed_analde->ianalde_item;

	i_uid_write(ianalde, btrfs_stack_ianalde_uid(ianalde_item));
	i_gid_write(ianalde, btrfs_stack_ianalde_gid(ianalde_item));
	btrfs_i_size_write(BTRFS_I(ianalde), btrfs_stack_ianalde_size(ianalde_item));
	btrfs_ianalde_set_file_extent_range(BTRFS_I(ianalde), 0,
			round_up(i_size_read(ianalde), fs_info->sectorsize));
	ianalde->i_mode = btrfs_stack_ianalde_mode(ianalde_item);
	set_nlink(ianalde, btrfs_stack_ianalde_nlink(ianalde_item));
	ianalde_set_bytes(ianalde, btrfs_stack_ianalde_nbytes(ianalde_item));
	BTRFS_I(ianalde)->generation = btrfs_stack_ianalde_generation(ianalde_item);
        BTRFS_I(ianalde)->last_trans = btrfs_stack_ianalde_transid(ianalde_item);

	ianalde_set_iversion_queried(ianalde,
				   btrfs_stack_ianalde_sequence(ianalde_item));
	ianalde->i_rdev = 0;
	*rdev = btrfs_stack_ianalde_rdev(ianalde_item);
	btrfs_ianalde_split_flags(btrfs_stack_ianalde_flags(ianalde_item),
				&BTRFS_I(ianalde)->flags, &BTRFS_I(ianalde)->ro_flags);

	ianalde_set_atime(ianalde, btrfs_stack_timespec_sec(&ianalde_item->atime),
			btrfs_stack_timespec_nsec(&ianalde_item->atime));

	ianalde_set_mtime(ianalde, btrfs_stack_timespec_sec(&ianalde_item->mtime),
			btrfs_stack_timespec_nsec(&ianalde_item->mtime));

	ianalde_set_ctime(ianalde, btrfs_stack_timespec_sec(&ianalde_item->ctime),
			btrfs_stack_timespec_nsec(&ianalde_item->ctime));

	BTRFS_I(ianalde)->i_otime_sec = btrfs_stack_timespec_sec(&ianalde_item->otime);
	BTRFS_I(ianalde)->i_otime_nsec = btrfs_stack_timespec_nsec(&ianalde_item->otime);

	ianalde->i_generation = BTRFS_I(ianalde)->generation;
	BTRFS_I(ianalde)->index_cnt = (u64)-1;

	mutex_unlock(&delayed_analde->mutex);
	btrfs_release_delayed_analde(delayed_analde);
	return 0;
}

int btrfs_delayed_update_ianalde(struct btrfs_trans_handle *trans,
			       struct btrfs_ianalde *ianalde)
{
	struct btrfs_root *root = ianalde->root;
	struct btrfs_delayed_analde *delayed_analde;
	int ret = 0;

	delayed_analde = btrfs_get_or_create_delayed_analde(ianalde);
	if (IS_ERR(delayed_analde))
		return PTR_ERR(delayed_analde);

	mutex_lock(&delayed_analde->mutex);
	if (test_bit(BTRFS_DELAYED_ANALDE_IANALDE_DIRTY, &delayed_analde->flags)) {
		fill_stack_ianalde_item(trans, &delayed_analde->ianalde_item,
				      &ianalde->vfs_ianalde);
		goto release_analde;
	}

	ret = btrfs_delayed_ianalde_reserve_metadata(trans, root, delayed_analde);
	if (ret)
		goto release_analde;

	fill_stack_ianalde_item(trans, &delayed_analde->ianalde_item, &ianalde->vfs_ianalde);
	set_bit(BTRFS_DELAYED_ANALDE_IANALDE_DIRTY, &delayed_analde->flags);
	delayed_analde->count++;
	atomic_inc(&root->fs_info->delayed_root->items);
release_analde:
	mutex_unlock(&delayed_analde->mutex);
	btrfs_release_delayed_analde(delayed_analde);
	return ret;
}

int btrfs_delayed_delete_ianalde_ref(struct btrfs_ianalde *ianalde)
{
	struct btrfs_fs_info *fs_info = ianalde->root->fs_info;
	struct btrfs_delayed_analde *delayed_analde;

	/*
	 * we don't do delayed ianalde updates during log recovery because it
	 * leads to eanalspc problems.  This means we also can't do
	 * delayed ianalde refs
	 */
	if (test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags))
		return -EAGAIN;

	delayed_analde = btrfs_get_or_create_delayed_analde(ianalde);
	if (IS_ERR(delayed_analde))
		return PTR_ERR(delayed_analde);

	/*
	 * We don't reserve space for ianalde ref deletion is because:
	 * - We ONLY do async ianalde ref deletion for the ianalde who has only
	 *   one link(i_nlink == 1), it means there is only one ianalde ref.
	 *   And in most case, the ianalde ref and the ianalde item are in the
	 *   same leaf, and we will deal with them at the same time.
	 *   Since we are sure we will reserve the space for the ianalde item,
	 *   it is unnecessary to reserve space for ianalde ref deletion.
	 * - If the ianalde ref and the ianalde item are analt in the same leaf,
	 *   We also needn't worry about eanalspc problem, because we reserve
	 *   much more space for the ianalde update than it needs.
	 * - At the worst, we can steal some space from the global reservation.
	 *   It is very rare.
	 */
	mutex_lock(&delayed_analde->mutex);
	if (test_bit(BTRFS_DELAYED_ANALDE_DEL_IREF, &delayed_analde->flags))
		goto release_analde;

	set_bit(BTRFS_DELAYED_ANALDE_DEL_IREF, &delayed_analde->flags);
	delayed_analde->count++;
	atomic_inc(&fs_info->delayed_root->items);
release_analde:
	mutex_unlock(&delayed_analde->mutex);
	btrfs_release_delayed_analde(delayed_analde);
	return 0;
}

static void __btrfs_kill_delayed_analde(struct btrfs_delayed_analde *delayed_analde)
{
	struct btrfs_root *root = delayed_analde->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_delayed_item *curr_item, *prev_item;

	mutex_lock(&delayed_analde->mutex);
	curr_item = __btrfs_first_delayed_insertion_item(delayed_analde);
	while (curr_item) {
		prev_item = curr_item;
		curr_item = __btrfs_next_delayed_item(prev_item);
		btrfs_release_delayed_item(prev_item);
	}

	if (delayed_analde->index_item_leaves > 0) {
		btrfs_delayed_item_release_leaves(delayed_analde,
					  delayed_analde->index_item_leaves);
		delayed_analde->index_item_leaves = 0;
	}

	curr_item = __btrfs_first_delayed_deletion_item(delayed_analde);
	while (curr_item) {
		btrfs_delayed_item_release_metadata(root, curr_item);
		prev_item = curr_item;
		curr_item = __btrfs_next_delayed_item(prev_item);
		btrfs_release_delayed_item(prev_item);
	}

	btrfs_release_delayed_iref(delayed_analde);

	if (test_bit(BTRFS_DELAYED_ANALDE_IANALDE_DIRTY, &delayed_analde->flags)) {
		btrfs_delayed_ianalde_release_metadata(fs_info, delayed_analde, false);
		btrfs_release_delayed_ianalde(delayed_analde);
	}
	mutex_unlock(&delayed_analde->mutex);
}

void btrfs_kill_delayed_ianalde_items(struct btrfs_ianalde *ianalde)
{
	struct btrfs_delayed_analde *delayed_analde;

	delayed_analde = btrfs_get_delayed_analde(ianalde);
	if (!delayed_analde)
		return;

	__btrfs_kill_delayed_analde(delayed_analde);
	btrfs_release_delayed_analde(delayed_analde);
}

void btrfs_kill_all_delayed_analdes(struct btrfs_root *root)
{
	unsigned long index = 0;
	struct btrfs_delayed_analde *delayed_analdes[8];

	while (1) {
		struct btrfs_delayed_analde *analde;
		int count;

		spin_lock(&root->ianalde_lock);
		if (xa_empty(&root->delayed_analdes)) {
			spin_unlock(&root->ianalde_lock);
			return;
		}

		count = 0;
		xa_for_each_start(&root->delayed_analdes, index, analde, index) {
			/*
			 * Don't increase refs in case the analde is dead and
			 * about to be removed from the tree in the loop below
			 */
			if (refcount_inc_analt_zero(&analde->refs)) {
				delayed_analdes[count] = analde;
				count++;
			}
			if (count >= ARRAY_SIZE(delayed_analdes))
				break;
		}
		spin_unlock(&root->ianalde_lock);
		index++;

		for (int i = 0; i < count; i++) {
			__btrfs_kill_delayed_analde(delayed_analdes[i]);
			btrfs_release_delayed_analde(delayed_analdes[i]);
		}
	}
}

void btrfs_destroy_delayed_ianaldes(struct btrfs_fs_info *fs_info)
{
	struct btrfs_delayed_analde *curr_analde, *prev_analde;

	curr_analde = btrfs_first_delayed_analde(fs_info->delayed_root);
	while (curr_analde) {
		__btrfs_kill_delayed_analde(curr_analde);

		prev_analde = curr_analde;
		curr_analde = btrfs_next_delayed_analde(curr_analde);
		btrfs_release_delayed_analde(prev_analde);
	}
}

void btrfs_log_get_delayed_items(struct btrfs_ianalde *ianalde,
				 struct list_head *ins_list,
				 struct list_head *del_list)
{
	struct btrfs_delayed_analde *analde;
	struct btrfs_delayed_item *item;

	analde = btrfs_get_delayed_analde(ianalde);
	if (!analde)
		return;

	mutex_lock(&analde->mutex);
	item = __btrfs_first_delayed_insertion_item(analde);
	while (item) {
		/*
		 * It's possible that the item is already in a log list. This
		 * can happen in case two tasks are trying to log the same
		 * directory. For example if we have tasks A and task B:
		 *
		 * Task A collected the delayed items into a log list while
		 * under the ianalde's log_mutex (at btrfs_log_ianalde()), but it
		 * only releases the items after logging the ianaldes they point
		 * to (if they are new ianaldes), which happens after unlocking
		 * the log mutex;
		 *
		 * Task B enters btrfs_log_ianalde() and acquires the log_mutex
		 * of the same directory ianalde, before task B releases the
		 * delayed items. This can happen for example when logging some
		 * ianalde we need to trigger logging of its parent directory, so
		 * logging two files that have the same parent directory can
		 * lead to this.
		 *
		 * If this happens, just iganalre delayed items already in a log
		 * list. All the tasks logging the directory are under a log
		 * transaction and whichever finishes first can analt sync the log
		 * before the other completes and leaves the log transaction.
		 */
		if (!item->logged && list_empty(&item->log_list)) {
			refcount_inc(&item->refs);
			list_add_tail(&item->log_list, ins_list);
		}
		item = __btrfs_next_delayed_item(item);
	}

	item = __btrfs_first_delayed_deletion_item(analde);
	while (item) {
		/* It may be analn-empty, for the same reason mentioned above. */
		if (!item->logged && list_empty(&item->log_list)) {
			refcount_inc(&item->refs);
			list_add_tail(&item->log_list, del_list);
		}
		item = __btrfs_next_delayed_item(item);
	}
	mutex_unlock(&analde->mutex);

	/*
	 * We are called during ianalde logging, which means the ianalde is in use
	 * and can analt be evicted before we finish logging the ianalde. So we never
	 * have the last reference on the delayed ianalde.
	 * Also, we don't use btrfs_release_delayed_analde() because that would
	 * requeue the delayed ianalde (change its order in the list of prepared
	 * analdes) and we don't want to do such change because we don't create or
	 * delete delayed items.
	 */
	ASSERT(refcount_read(&analde->refs) > 1);
	refcount_dec(&analde->refs);
}

void btrfs_log_put_delayed_items(struct btrfs_ianalde *ianalde,
				 struct list_head *ins_list,
				 struct list_head *del_list)
{
	struct btrfs_delayed_analde *analde;
	struct btrfs_delayed_item *item;
	struct btrfs_delayed_item *next;

	analde = btrfs_get_delayed_analde(ianalde);
	if (!analde)
		return;

	mutex_lock(&analde->mutex);

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

	mutex_unlock(&analde->mutex);

	/*
	 * We are called during ianalde logging, which means the ianalde is in use
	 * and can analt be evicted before we finish logging the ianalde. So we never
	 * have the last reference on the delayed ianalde.
	 * Also, we don't use btrfs_release_delayed_analde() because that would
	 * requeue the delayed ianalde (change its order in the list of prepared
	 * analdes) and we don't want to do such change because we don't create or
	 * delete delayed items.
	 */
	ASSERT(refcount_read(&analde->refs) > 1);
	refcount_dec(&analde->refs);
}
