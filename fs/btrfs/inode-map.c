// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/pagemap.h>

#include "ctree.h"
#include "disk-io.h"
#include "free-space-cache.h"
#include "inode-map.h"
#include "transaction.h"

static int caching_kthread(void *data)
{
	struct btrfs_root *root = data;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_free_space_ctl *ctl = root->free_ino_ctl;
	struct btrfs_key key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	u64 last = (u64)-1;
	int slot;
	int ret;

	if (!btrfs_test_opt(fs_info, INODE_MAP_CACHE))
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/* Since the commit root is read-only, we can safely skip locking. */
	path->skip_locking = 1;
	path->search_commit_root = 1;
	path->reada = READA_FORWARD;

	key.objectid = BTRFS_FIRST_FREE_OBJECTID;
	key.offset = 0;
	key.type = BTRFS_INODE_ITEM_KEY;
again:
	/* need to make sure the commit_root doesn't disappear */
	down_read(&fs_info->commit_root_sem);

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	while (1) {
		if (btrfs_fs_closing(fs_info))
			goto out;

		leaf = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			else if (ret > 0)
				break;

			if (need_resched() ||
			    btrfs_transaction_in_commit(fs_info)) {
				leaf = path->nodes[0];

				if (WARN_ON(btrfs_header_nritems(leaf) == 0))
					break;

				/*
				 * Save the key so we can advances forward
				 * in the next search.
				 */
				btrfs_item_key_to_cpu(leaf, &key, 0);
				btrfs_release_path(path);
				root->ino_cache_progress = last;
				up_read(&fs_info->commit_root_sem);
				schedule_timeout(1);
				goto again;
			} else
				continue;
		}

		btrfs_item_key_to_cpu(leaf, &key, slot);

		if (key.type != BTRFS_INODE_ITEM_KEY)
			goto next;

		if (key.objectid >= root->highest_objectid)
			break;

		if (last != (u64)-1 && last + 1 != key.objectid) {
			__btrfs_add_free_space(fs_info, ctl, last + 1,
					       key.objectid - last - 1);
			wake_up(&root->ino_cache_wait);
		}

		last = key.objectid;
next:
		path->slots[0]++;
	}

	if (last < root->highest_objectid - 1) {
		__btrfs_add_free_space(fs_info, ctl, last + 1,
				       root->highest_objectid - last - 1);
	}

	spin_lock(&root->ino_cache_lock);
	root->ino_cache_state = BTRFS_CACHE_FINISHED;
	spin_unlock(&root->ino_cache_lock);

	root->ino_cache_progress = (u64)-1;
	btrfs_unpin_free_ino(root);
out:
	wake_up(&root->ino_cache_wait);
	up_read(&fs_info->commit_root_sem);

	btrfs_free_path(path);

	return ret;
}

static void start_caching(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_free_space_ctl *ctl = root->free_ino_ctl;
	struct task_struct *tsk;
	int ret;
	u64 objectid;

	if (!btrfs_test_opt(fs_info, INODE_MAP_CACHE))
		return;

	spin_lock(&root->ino_cache_lock);
	if (root->ino_cache_state != BTRFS_CACHE_NO) {
		spin_unlock(&root->ino_cache_lock);
		return;
	}

	root->ino_cache_state = BTRFS_CACHE_STARTED;
	spin_unlock(&root->ino_cache_lock);

	ret = load_free_ino_cache(fs_info, root);
	if (ret == 1) {
		spin_lock(&root->ino_cache_lock);
		root->ino_cache_state = BTRFS_CACHE_FINISHED;
		spin_unlock(&root->ino_cache_lock);
		return;
	}

	/*
	 * It can be quite time-consuming to fill the cache by searching
	 * through the extent tree, and this can keep ino allocation path
	 * waiting. Therefore at start we quickly find out the highest
	 * inode number and we know we can use inode numbers which fall in
	 * [highest_ino + 1, BTRFS_LAST_FREE_OBJECTID].
	 */
	ret = btrfs_find_free_objectid(root, &objectid);
	if (!ret && objectid <= BTRFS_LAST_FREE_OBJECTID) {
		__btrfs_add_free_space(fs_info, ctl, objectid,
				       BTRFS_LAST_FREE_OBJECTID - objectid + 1);
	}

	tsk = kthread_run(caching_kthread, root, "btrfs-ino-cache-%llu",
			  root->root_key.objectid);
	if (IS_ERR(tsk)) {
		btrfs_warn(fs_info, "failed to start inode caching task");
		btrfs_clear_pending_and_info(fs_info, INODE_MAP_CACHE,
					     "disabling inode map caching");
	}
}

int btrfs_find_free_ino(struct btrfs_root *root, u64 *objectid)
{
	if (!btrfs_test_opt(root->fs_info, INODE_MAP_CACHE))
		return btrfs_find_free_objectid(root, objectid);

again:
	*objectid = btrfs_find_ino_for_alloc(root);

	if (*objectid != 0)
		return 0;

	start_caching(root);

	wait_event(root->ino_cache_wait,
		   root->ino_cache_state == BTRFS_CACHE_FINISHED ||
		   root->free_ino_ctl->free_space > 0);

	if (root->ino_cache_state == BTRFS_CACHE_FINISHED &&
	    root->free_ino_ctl->free_space == 0)
		return -ENOSPC;
	else
		goto again;
}

void btrfs_return_ino(struct btrfs_root *root, u64 objectid)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_free_space_ctl *pinned = root->free_ino_pinned;

	if (!btrfs_test_opt(fs_info, INODE_MAP_CACHE))
		return;
again:
	if (root->ino_cache_state == BTRFS_CACHE_FINISHED) {
		__btrfs_add_free_space(fs_info, pinned, objectid, 1);
	} else {
		down_write(&fs_info->commit_root_sem);
		spin_lock(&root->ino_cache_lock);
		if (root->ino_cache_state == BTRFS_CACHE_FINISHED) {
			spin_unlock(&root->ino_cache_lock);
			up_write(&fs_info->commit_root_sem);
			goto again;
		}
		spin_unlock(&root->ino_cache_lock);

		start_caching(root);

		__btrfs_add_free_space(fs_info, pinned, objectid, 1);

		up_write(&fs_info->commit_root_sem);
	}
}

/*
 * When a transaction is committed, we'll move those inode numbers which are
 * smaller than root->ino_cache_progress from pinned tree to free_ino tree, and
 * others will just be dropped, because the commit root we were searching has
 * changed.
 *
 * Must be called with root->fs_info->commit_root_sem held
 */
void btrfs_unpin_free_ino(struct btrfs_root *root)
{
	struct btrfs_free_space_ctl *ctl = root->free_ino_ctl;
	struct rb_root *rbroot = &root->free_ino_pinned->free_space_offset;
	spinlock_t *rbroot_lock = &root->free_ino_pinned->tree_lock;
	struct btrfs_free_space *info;
	struct rb_node *n;
	u64 count;

	if (!btrfs_test_opt(root->fs_info, INODE_MAP_CACHE))
		return;

	while (1) {
		bool add_to_ctl = true;

		spin_lock(rbroot_lock);
		n = rb_first(rbroot);
		if (!n) {
			spin_unlock(rbroot_lock);
			break;
		}

		info = rb_entry(n, struct btrfs_free_space, offset_index);
		BUG_ON(info->bitmap); /* Logic error */

		if (info->offset > root->ino_cache_progress)
			add_to_ctl = false;
		else if (info->offset + info->bytes > root->ino_cache_progress)
			count = root->ino_cache_progress - info->offset + 1;
		else
			count = info->bytes;

		rb_erase(&info->offset_index, rbroot);
		spin_unlock(rbroot_lock);
		if (add_to_ctl)
			__btrfs_add_free_space(root->fs_info, ctl,
					       info->offset, count);
		kmem_cache_free(btrfs_free_space_cachep, info);
	}
}

#define INIT_THRESHOLD	((SZ_32K / 2) / sizeof(struct btrfs_free_space))
#define INODES_PER_BITMAP (PAGE_SIZE * 8)

/*
 * The goal is to keep the memory used by the free_ino tree won't
 * exceed the memory if we use bitmaps only.
 */
static void recalculate_thresholds(struct btrfs_free_space_ctl *ctl)
{
	struct btrfs_free_space *info;
	struct rb_node *n;
	int max_ino;
	int max_bitmaps;

	n = rb_last(&ctl->free_space_offset);
	if (!n) {
		ctl->extents_thresh = INIT_THRESHOLD;
		return;
	}
	info = rb_entry(n, struct btrfs_free_space, offset_index);

	/*
	 * Find the maximum inode number in the filesystem. Note we
	 * ignore the fact that this can be a bitmap, because we are
	 * not doing precise calculation.
	 */
	max_ino = info->bytes - 1;

	max_bitmaps = ALIGN(max_ino, INODES_PER_BITMAP) / INODES_PER_BITMAP;
	if (max_bitmaps <= ctl->total_bitmaps) {
		ctl->extents_thresh = 0;
		return;
	}

	ctl->extents_thresh = (max_bitmaps - ctl->total_bitmaps) *
				PAGE_SIZE / sizeof(*info);
}

/*
 * We don't fall back to bitmap, if we are below the extents threshold
 * or this chunk of inode numbers is a big one.
 */
static bool use_bitmap(struct btrfs_free_space_ctl *ctl,
		       struct btrfs_free_space *info)
{
	if (ctl->free_extents < ctl->extents_thresh ||
	    info->bytes > INODES_PER_BITMAP / 10)
		return false;

	return true;
}

static const struct btrfs_free_space_op free_ino_op = {
	.recalc_thresholds	= recalculate_thresholds,
	.use_bitmap		= use_bitmap,
};

static void pinned_recalc_thresholds(struct btrfs_free_space_ctl *ctl)
{
}

static bool pinned_use_bitmap(struct btrfs_free_space_ctl *ctl,
			      struct btrfs_free_space *info)
{
	/*
	 * We always use extents for two reasons:
	 *
	 * - The pinned tree is only used during the process of caching
	 *   work.
	 * - Make code simpler. See btrfs_unpin_free_ino().
	 */
	return false;
}

static const struct btrfs_free_space_op pinned_free_ino_op = {
	.recalc_thresholds	= pinned_recalc_thresholds,
	.use_bitmap		= pinned_use_bitmap,
};

void btrfs_init_free_ino_ctl(struct btrfs_root *root)
{
	struct btrfs_free_space_ctl *ctl = root->free_ino_ctl;
	struct btrfs_free_space_ctl *pinned = root->free_ino_pinned;

	spin_lock_init(&ctl->tree_lock);
	ctl->unit = 1;
	ctl->start = 0;
	ctl->private = NULL;
	ctl->op = &free_ino_op;
	INIT_LIST_HEAD(&ctl->trimming_ranges);
	mutex_init(&ctl->cache_writeout_mutex);

	/*
	 * Initially we allow to use 16K of ram to cache chunks of
	 * inode numbers before we resort to bitmaps. This is somewhat
	 * arbitrary, but it will be adjusted in runtime.
	 */
	ctl->extents_thresh = INIT_THRESHOLD;

	spin_lock_init(&pinned->tree_lock);
	pinned->unit = 1;
	pinned->start = 0;
	pinned->private = NULL;
	pinned->extents_thresh = 0;
	pinned->op = &pinned_free_ino_op;
}

int btrfs_save_ino_cache(struct btrfs_root *root,
			 struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_free_space_ctl *ctl = root->free_ino_ctl;
	struct btrfs_path *path;
	struct inode *inode;
	struct btrfs_block_rsv *rsv;
	struct extent_changeset *data_reserved = NULL;
	u64 num_bytes;
	u64 alloc_hint = 0;
	int ret;
	int prealloc;
	bool retry = false;

	/* only fs tree and subvol/snap needs ino cache */
	if (root->root_key.objectid != BTRFS_FS_TREE_OBJECTID &&
	    (root->root_key.objectid < BTRFS_FIRST_FREE_OBJECTID ||
	     root->root_key.objectid > BTRFS_LAST_FREE_OBJECTID))
		return 0;

	/* Don't save inode cache if we are deleting this root */
	if (btrfs_root_refs(&root->root_item) == 0)
		return 0;

	if (!btrfs_test_opt(fs_info, INODE_MAP_CACHE))
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	rsv = trans->block_rsv;
	trans->block_rsv = &fs_info->trans_block_rsv;

	num_bytes = trans->bytes_reserved;
	/*
	 * 1 item for inode item insertion if need
	 * 4 items for inode item update (in the worst case)
	 * 1 items for slack space if we need do truncation
	 * 1 item for free space object
	 * 3 items for pre-allocation
	 */
	trans->bytes_reserved = btrfs_calc_trans_metadata_size(fs_info, 10);
	ret = btrfs_block_rsv_add(root, trans->block_rsv,
				  trans->bytes_reserved,
				  BTRFS_RESERVE_NO_FLUSH);
	if (ret)
		goto out;
	trace_btrfs_space_reservation(fs_info, "ino_cache", trans->transid,
				      trans->bytes_reserved, 1);
again:
	inode = lookup_free_ino_inode(root, path);
	if (IS_ERR(inode) && (PTR_ERR(inode) != -ENOENT || retry)) {
		ret = PTR_ERR(inode);
		goto out_release;
	}

	if (IS_ERR(inode)) {
		BUG_ON(retry); /* Logic error */
		retry = true;

		ret = create_free_ino_inode(root, trans, path);
		if (ret)
			goto out_release;
		goto again;
	}

	BTRFS_I(inode)->generation = 0;
	ret = btrfs_update_inode(trans, root, inode);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_put;
	}

	if (i_size_read(inode) > 0) {
		ret = btrfs_truncate_free_space_cache(trans, NULL, inode);
		if (ret) {
			if (ret != -ENOSPC)
				btrfs_abort_transaction(trans, ret);
			goto out_put;
		}
	}

	spin_lock(&root->ino_cache_lock);
	if (root->ino_cache_state != BTRFS_CACHE_FINISHED) {
		ret = -1;
		spin_unlock(&root->ino_cache_lock);
		goto out_put;
	}
	spin_unlock(&root->ino_cache_lock);

	spin_lock(&ctl->tree_lock);
	prealloc = sizeof(struct btrfs_free_space) * ctl->free_extents;
	prealloc = ALIGN(prealloc, PAGE_SIZE);
	prealloc += ctl->total_bitmaps * PAGE_SIZE;
	spin_unlock(&ctl->tree_lock);

	/* Just to make sure we have enough space */
	prealloc += 8 * PAGE_SIZE;

	ret = btrfs_delalloc_reserve_space(inode, &data_reserved, 0, prealloc);
	if (ret)
		goto out_put;

	ret = btrfs_prealloc_file_range_trans(inode, trans, 0, 0, prealloc,
					      prealloc, prealloc, &alloc_hint);
	if (ret) {
		btrfs_delalloc_release_extents(BTRFS_I(inode), prealloc, true);
		goto out_put;
	}

	ret = btrfs_write_out_ino_cache(root, trans, path, inode);
	btrfs_delalloc_release_extents(BTRFS_I(inode), prealloc, false);
out_put:
	iput(inode);
out_release:
	trace_btrfs_space_reservation(fs_info, "ino_cache", trans->transid,
				      trans->bytes_reserved, 0);
	btrfs_block_rsv_release(fs_info, trans->block_rsv,
				trans->bytes_reserved);
out:
	trans->block_rsv = rsv;
	trans->bytes_reserved = num_bytes;

	btrfs_free_path(path);
	extent_changeset_free(data_reserved);
	return ret;
}

int btrfs_find_highest_objectid(struct btrfs_root *root, u64 *objectid)
{
	struct btrfs_path *path;
	int ret;
	struct extent_buffer *l;
	struct btrfs_key search_key;
	struct btrfs_key found_key;
	int slot;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	search_key.objectid = BTRFS_LAST_FREE_OBJECTID;
	search_key.type = -1;
	search_key.offset = (u64)-1;
	ret = btrfs_search_slot(NULL, root, &search_key, path, 0, 0);
	if (ret < 0)
		goto error;
	BUG_ON(ret == 0); /* Corruption */
	if (path->slots[0] > 0) {
		slot = path->slots[0] - 1;
		l = path->nodes[0];
		btrfs_item_key_to_cpu(l, &found_key, slot);
		*objectid = max_t(u64, found_key.objectid,
				  BTRFS_FIRST_FREE_OBJECTID - 1);
	} else {
		*objectid = BTRFS_FIRST_FREE_OBJECTID - 1;
	}
	ret = 0;
error:
	btrfs_free_path(path);
	return ret;
}

int btrfs_find_free_objectid(struct btrfs_root *root, u64 *objectid)
{
	int ret;
	mutex_lock(&root->objectid_mutex);

	if (unlikely(root->highest_objectid >= BTRFS_LAST_FREE_OBJECTID)) {
		btrfs_warn(root->fs_info,
			   "the objectid of root %llu reaches its highest value",
			   root->root_key.objectid);
		ret = -ENOSPC;
		goto out;
	}

	*objectid = ++root->highest_objectid;
	ret = 0;
out:
	mutex_unlock(&root->objectid_mutex);
	return ret;
}
