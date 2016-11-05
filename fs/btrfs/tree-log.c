/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
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

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/list_sort.h>
#include "tree-log.h"
#include "disk-io.h"
#include "locking.h"
#include "print-tree.h"
#include "backref.h"
#include "hash.h"
#include "compression.h"
#include "qgroup.h"

/* magic values for the inode_only field in btrfs_log_inode:
 *
 * LOG_INODE_ALL means to log everything
 * LOG_INODE_EXISTS means to log just enough to recreate the inode
 * during log replay
 */
#define LOG_INODE_ALL 0
#define LOG_INODE_EXISTS 1

/*
 * directory trouble cases
 *
 * 1) on rename or unlink, if the inode being unlinked isn't in the fsync
 * log, we must force a full commit before doing an fsync of the directory
 * where the unlink was done.
 * ---> record transid of last unlink/rename per directory
 *
 * mkdir foo/some_dir
 * normal commit
 * rename foo/some_dir foo2/some_dir
 * mkdir foo/some_dir
 * fsync foo/some_dir/some_file
 *
 * The fsync above will unlink the original some_dir without recording
 * it in its new location (foo2).  After a crash, some_dir will be gone
 * unless the fsync of some_file forces a full commit
 *
 * 2) we must log any new names for any file or dir that is in the fsync
 * log. ---> check inode while renaming/linking.
 *
 * 2a) we must log any new names for any file or dir during rename
 * when the directory they are being removed from was logged.
 * ---> check inode and old parent dir during rename
 *
 *  2a is actually the more important variant.  With the extra logging
 *  a crash might unlink the old name without recreating the new one
 *
 * 3) after a crash, we must go through any directories with a link count
 * of zero and redo the rm -rf
 *
 * mkdir f1/foo
 * normal commit
 * rm -rf f1/foo
 * fsync(f1)
 *
 * The directory f1 was fully removed from the FS, but fsync was never
 * called on f1, only its parent dir.  After a crash the rm -rf must
 * be replayed.  This must be able to recurse down the entire
 * directory tree.  The inode link count fixup code takes care of the
 * ugly details.
 */

/*
 * stages for the tree walking.  The first
 * stage (0) is to only pin down the blocks we find
 * the second stage (1) is to make sure that all the inodes
 * we find in the log are created in the subvolume.
 *
 * The last stage is to deal with directories and links and extents
 * and all the other fun semantics
 */
#define LOG_WALK_PIN_ONLY 0
#define LOG_WALK_REPLAY_INODES 1
#define LOG_WALK_REPLAY_DIR_INDEX 2
#define LOG_WALK_REPLAY_ALL 3

static int btrfs_log_inode(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, struct inode *inode,
			   int inode_only,
			   const loff_t start,
			   const loff_t end,
			   struct btrfs_log_ctx *ctx);
static int link_to_fixup_dir(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct btrfs_path *path, u64 objectid);
static noinline int replay_dir_deletes(struct btrfs_trans_handle *trans,
				       struct btrfs_root *root,
				       struct btrfs_root *log,
				       struct btrfs_path *path,
				       u64 dirid, int del_all);

/*
 * tree logging is a special write ahead log used to make sure that
 * fsyncs and O_SYNCs can happen without doing full tree commits.
 *
 * Full tree commits are expensive because they require commonly
 * modified blocks to be recowed, creating many dirty pages in the
 * extent tree an 4x-6x higher write load than ext3.
 *
 * Instead of doing a tree commit on every fsync, we use the
 * key ranges and transaction ids to find items for a given file or directory
 * that have changed in this transaction.  Those items are copied into
 * a special tree (one per subvolume root), that tree is written to disk
 * and then the fsync is considered complete.
 *
 * After a crash, items are copied out of the log-tree back into the
 * subvolume tree.  Any file data extents found are recorded in the extent
 * allocation tree, and the log-tree freed.
 *
 * The log tree is read three times, once to pin down all the extents it is
 * using in ram and once, once to create all the inodes logged in the tree
 * and once to do all the other items.
 */

/*
 * start a sub transaction and setup the log tree
 * this increments the log tree writer count to make the people
 * syncing the tree wait for us to finish
 */
static int start_log_trans(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct btrfs_log_ctx *ctx)
{
	int ret = 0;

	mutex_lock(&root->log_mutex);

	if (root->log_root) {
		if (btrfs_need_log_full_commit(root->fs_info, trans)) {
			ret = -EAGAIN;
			goto out;
		}

		if (!root->log_start_pid) {
			clear_bit(BTRFS_ROOT_MULTI_LOG_TASKS, &root->state);
			root->log_start_pid = current->pid;
		} else if (root->log_start_pid != current->pid) {
			set_bit(BTRFS_ROOT_MULTI_LOG_TASKS, &root->state);
		}
	} else {
		mutex_lock(&root->fs_info->tree_log_mutex);
		if (!root->fs_info->log_root_tree)
			ret = btrfs_init_log_root_tree(trans, root->fs_info);
		mutex_unlock(&root->fs_info->tree_log_mutex);
		if (ret)
			goto out;

		ret = btrfs_add_log_tree(trans, root);
		if (ret)
			goto out;

		clear_bit(BTRFS_ROOT_MULTI_LOG_TASKS, &root->state);
		root->log_start_pid = current->pid;
	}

	atomic_inc(&root->log_batch);
	atomic_inc(&root->log_writers);
	if (ctx) {
		int index = root->log_transid % 2;
		list_add_tail(&ctx->list, &root->log_ctxs[index]);
		ctx->log_transid = root->log_transid;
	}

out:
	mutex_unlock(&root->log_mutex);
	return ret;
}

/*
 * returns 0 if there was a log transaction running and we were able
 * to join, or returns -ENOENT if there were not transactions
 * in progress
 */
static int join_running_log_trans(struct btrfs_root *root)
{
	int ret = -ENOENT;

	smp_mb();
	if (!root->log_root)
		return -ENOENT;

	mutex_lock(&root->log_mutex);
	if (root->log_root) {
		ret = 0;
		atomic_inc(&root->log_writers);
	}
	mutex_unlock(&root->log_mutex);
	return ret;
}

/*
 * This either makes the current running log transaction wait
 * until you call btrfs_end_log_trans() or it makes any future
 * log transactions wait until you call btrfs_end_log_trans()
 */
int btrfs_pin_log_trans(struct btrfs_root *root)
{
	int ret = -ENOENT;

	mutex_lock(&root->log_mutex);
	atomic_inc(&root->log_writers);
	mutex_unlock(&root->log_mutex);
	return ret;
}

/*
 * indicate we're done making changes to the log tree
 * and wake up anyone waiting to do a sync
 */
void btrfs_end_log_trans(struct btrfs_root *root)
{
	if (atomic_dec_and_test(&root->log_writers)) {
		/*
		 * Implicit memory barrier after atomic_dec_and_test
		 */
		if (waitqueue_active(&root->log_writer_wait))
			wake_up(&root->log_writer_wait);
	}
}


/*
 * the walk control struct is used to pass state down the chain when
 * processing the log tree.  The stage field tells us which part
 * of the log tree processing we are currently doing.  The others
 * are state fields used for that specific part
 */
struct walk_control {
	/* should we free the extent on disk when done?  This is used
	 * at transaction commit time while freeing a log tree
	 */
	int free;

	/* should we write out the extent buffer?  This is used
	 * while flushing the log tree to disk during a sync
	 */
	int write;

	/* should we wait for the extent buffer io to finish?  Also used
	 * while flushing the log tree to disk for a sync
	 */
	int wait;

	/* pin only walk, we record which extents on disk belong to the
	 * log trees
	 */
	int pin;

	/* what stage of the replay code we're currently in */
	int stage;

	/* the root we are currently replaying */
	struct btrfs_root *replay_dest;

	/* the trans handle for the current replay */
	struct btrfs_trans_handle *trans;

	/* the function that gets used to process blocks we find in the
	 * tree.  Note the extent_buffer might not be up to date when it is
	 * passed in, and it must be checked or read if you need the data
	 * inside it
	 */
	int (*process_func)(struct btrfs_root *log, struct extent_buffer *eb,
			    struct walk_control *wc, u64 gen);
};

/*
 * process_func used to pin down extents, write them or wait on them
 */
static int process_one_buffer(struct btrfs_root *log,
			      struct extent_buffer *eb,
			      struct walk_control *wc, u64 gen)
{
	int ret = 0;

	/*
	 * If this fs is mixed then we need to be able to process the leaves to
	 * pin down any logged extents, so we have to read the block.
	 */
	if (btrfs_fs_incompat(log->fs_info, MIXED_GROUPS)) {
		ret = btrfs_read_buffer(eb, gen);
		if (ret)
			return ret;
	}

	if (wc->pin)
		ret = btrfs_pin_extent_for_log_replay(log->fs_info->extent_root,
						      eb->start, eb->len);

	if (!ret && btrfs_buffer_uptodate(eb, gen, 0)) {
		if (wc->pin && btrfs_header_level(eb) == 0)
			ret = btrfs_exclude_logged_extents(log, eb);
		if (wc->write)
			btrfs_write_tree_block(eb);
		if (wc->wait)
			btrfs_wait_tree_block_writeback(eb);
	}
	return ret;
}

/*
 * Item overwrite used by replay and tree logging.  eb, slot and key all refer
 * to the src data we are copying out.
 *
 * root is the tree we are copying into, and path is a scratch
 * path for use in this function (it should be released on entry and
 * will be released on exit).
 *
 * If the key is already in the destination tree the existing item is
 * overwritten.  If the existing item isn't big enough, it is extended.
 * If it is too large, it is truncated.
 *
 * If the key isn't in the destination yet, a new item is inserted.
 */
static noinline int overwrite_item(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct btrfs_path *path,
				   struct extent_buffer *eb, int slot,
				   struct btrfs_key *key)
{
	int ret;
	u32 item_size;
	u64 saved_i_size = 0;
	int save_old_i_size = 0;
	unsigned long src_ptr;
	unsigned long dst_ptr;
	int overwrite_root = 0;
	bool inode_item = key->type == BTRFS_INODE_ITEM_KEY;

	if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID)
		overwrite_root = 1;

	item_size = btrfs_item_size_nr(eb, slot);
	src_ptr = btrfs_item_ptr_offset(eb, slot);

	/* look for the key in the destination tree */
	ret = btrfs_search_slot(NULL, root, key, path, 0, 0);
	if (ret < 0)
		return ret;

	if (ret == 0) {
		char *src_copy;
		char *dst_copy;
		u32 dst_size = btrfs_item_size_nr(path->nodes[0],
						  path->slots[0]);
		if (dst_size != item_size)
			goto insert;

		if (item_size == 0) {
			btrfs_release_path(path);
			return 0;
		}
		dst_copy = kmalloc(item_size, GFP_NOFS);
		src_copy = kmalloc(item_size, GFP_NOFS);
		if (!dst_copy || !src_copy) {
			btrfs_release_path(path);
			kfree(dst_copy);
			kfree(src_copy);
			return -ENOMEM;
		}

		read_extent_buffer(eb, src_copy, src_ptr, item_size);

		dst_ptr = btrfs_item_ptr_offset(path->nodes[0], path->slots[0]);
		read_extent_buffer(path->nodes[0], dst_copy, dst_ptr,
				   item_size);
		ret = memcmp(dst_copy, src_copy, item_size);

		kfree(dst_copy);
		kfree(src_copy);
		/*
		 * they have the same contents, just return, this saves
		 * us from cowing blocks in the destination tree and doing
		 * extra writes that may not have been done by a previous
		 * sync
		 */
		if (ret == 0) {
			btrfs_release_path(path);
			return 0;
		}

		/*
		 * We need to load the old nbytes into the inode so when we
		 * replay the extents we've logged we get the right nbytes.
		 */
		if (inode_item) {
			struct btrfs_inode_item *item;
			u64 nbytes;
			u32 mode;

			item = btrfs_item_ptr(path->nodes[0], path->slots[0],
					      struct btrfs_inode_item);
			nbytes = btrfs_inode_nbytes(path->nodes[0], item);
			item = btrfs_item_ptr(eb, slot,
					      struct btrfs_inode_item);
			btrfs_set_inode_nbytes(eb, item, nbytes);

			/*
			 * If this is a directory we need to reset the i_size to
			 * 0 so that we can set it up properly when replaying
			 * the rest of the items in this log.
			 */
			mode = btrfs_inode_mode(eb, item);
			if (S_ISDIR(mode))
				btrfs_set_inode_size(eb, item, 0);
		}
	} else if (inode_item) {
		struct btrfs_inode_item *item;
		u32 mode;

		/*
		 * New inode, set nbytes to 0 so that the nbytes comes out
		 * properly when we replay the extents.
		 */
		item = btrfs_item_ptr(eb, slot, struct btrfs_inode_item);
		btrfs_set_inode_nbytes(eb, item, 0);

		/*
		 * If this is a directory we need to reset the i_size to 0 so
		 * that we can set it up properly when replaying the rest of
		 * the items in this log.
		 */
		mode = btrfs_inode_mode(eb, item);
		if (S_ISDIR(mode))
			btrfs_set_inode_size(eb, item, 0);
	}
insert:
	btrfs_release_path(path);
	/* try to insert the key into the destination tree */
	path->skip_release_on_error = 1;
	ret = btrfs_insert_empty_item(trans, root, path,
				      key, item_size);
	path->skip_release_on_error = 0;

	/* make sure any existing item is the correct size */
	if (ret == -EEXIST || ret == -EOVERFLOW) {
		u32 found_size;
		found_size = btrfs_item_size_nr(path->nodes[0],
						path->slots[0]);
		if (found_size > item_size)
			btrfs_truncate_item(root, path, item_size, 1);
		else if (found_size < item_size)
			btrfs_extend_item(root, path,
					  item_size - found_size);
	} else if (ret) {
		return ret;
	}
	dst_ptr = btrfs_item_ptr_offset(path->nodes[0],
					path->slots[0]);

	/* don't overwrite an existing inode if the generation number
	 * was logged as zero.  This is done when the tree logging code
	 * is just logging an inode to make sure it exists after recovery.
	 *
	 * Also, don't overwrite i_size on directories during replay.
	 * log replay inserts and removes directory items based on the
	 * state of the tree found in the subvolume, and i_size is modified
	 * as it goes
	 */
	if (key->type == BTRFS_INODE_ITEM_KEY && ret == -EEXIST) {
		struct btrfs_inode_item *src_item;
		struct btrfs_inode_item *dst_item;

		src_item = (struct btrfs_inode_item *)src_ptr;
		dst_item = (struct btrfs_inode_item *)dst_ptr;

		if (btrfs_inode_generation(eb, src_item) == 0) {
			struct extent_buffer *dst_eb = path->nodes[0];
			const u64 ino_size = btrfs_inode_size(eb, src_item);

			/*
			 * For regular files an ino_size == 0 is used only when
			 * logging that an inode exists, as part of a directory
			 * fsync, and the inode wasn't fsynced before. In this
			 * case don't set the size of the inode in the fs/subvol
			 * tree, otherwise we would be throwing valid data away.
			 */
			if (S_ISREG(btrfs_inode_mode(eb, src_item)) &&
			    S_ISREG(btrfs_inode_mode(dst_eb, dst_item)) &&
			    ino_size != 0) {
				struct btrfs_map_token token;

				btrfs_init_map_token(&token);
				btrfs_set_token_inode_size(dst_eb, dst_item,
							   ino_size, &token);
			}
			goto no_copy;
		}

		if (overwrite_root &&
		    S_ISDIR(btrfs_inode_mode(eb, src_item)) &&
		    S_ISDIR(btrfs_inode_mode(path->nodes[0], dst_item))) {
			save_old_i_size = 1;
			saved_i_size = btrfs_inode_size(path->nodes[0],
							dst_item);
		}
	}

	copy_extent_buffer(path->nodes[0], eb, dst_ptr,
			   src_ptr, item_size);

	if (save_old_i_size) {
		struct btrfs_inode_item *dst_item;
		dst_item = (struct btrfs_inode_item *)dst_ptr;
		btrfs_set_inode_size(path->nodes[0], dst_item, saved_i_size);
	}

	/* make sure the generation is filled in */
	if (key->type == BTRFS_INODE_ITEM_KEY) {
		struct btrfs_inode_item *dst_item;
		dst_item = (struct btrfs_inode_item *)dst_ptr;
		if (btrfs_inode_generation(path->nodes[0], dst_item) == 0) {
			btrfs_set_inode_generation(path->nodes[0], dst_item,
						   trans->transid);
		}
	}
no_copy:
	btrfs_mark_buffer_dirty(path->nodes[0]);
	btrfs_release_path(path);
	return 0;
}

/*
 * simple helper to read an inode off the disk from a given root
 * This can only be called for subvolume roots and not for the log
 */
static noinline struct inode *read_one_inode(struct btrfs_root *root,
					     u64 objectid)
{
	struct btrfs_key key;
	struct inode *inode;

	key.objectid = objectid;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;
	inode = btrfs_iget(root->fs_info->sb, &key, root, NULL);
	if (IS_ERR(inode)) {
		inode = NULL;
	} else if (is_bad_inode(inode)) {
		iput(inode);
		inode = NULL;
	}
	return inode;
}

/* replays a single extent in 'eb' at 'slot' with 'key' into the
 * subvolume 'root'.  path is released on entry and should be released
 * on exit.
 *
 * extents in the log tree have not been allocated out of the extent
 * tree yet.  So, this completes the allocation, taking a reference
 * as required if the extent already exists or creating a new extent
 * if it isn't in the extent allocation tree yet.
 *
 * The extent is inserted into the file, dropping any existing extents
 * from the file that overlap the new one.
 */
static noinline int replay_one_extent(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      struct btrfs_path *path,
				      struct extent_buffer *eb, int slot,
				      struct btrfs_key *key)
{
	int found_type;
	u64 extent_end;
	u64 start = key->offset;
	u64 nbytes = 0;
	struct btrfs_file_extent_item *item;
	struct inode *inode = NULL;
	unsigned long size;
	int ret = 0;

	item = btrfs_item_ptr(eb, slot, struct btrfs_file_extent_item);
	found_type = btrfs_file_extent_type(eb, item);

	if (found_type == BTRFS_FILE_EXTENT_REG ||
	    found_type == BTRFS_FILE_EXTENT_PREALLOC) {
		nbytes = btrfs_file_extent_num_bytes(eb, item);
		extent_end = start + nbytes;

		/*
		 * We don't add to the inodes nbytes if we are prealloc or a
		 * hole.
		 */
		if (btrfs_file_extent_disk_bytenr(eb, item) == 0)
			nbytes = 0;
	} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
		size = btrfs_file_extent_inline_len(eb, slot, item);
		nbytes = btrfs_file_extent_ram_bytes(eb, item);
		extent_end = ALIGN(start + size, root->sectorsize);
	} else {
		ret = 0;
		goto out;
	}

	inode = read_one_inode(root, key->objectid);
	if (!inode) {
		ret = -EIO;
		goto out;
	}

	/*
	 * first check to see if we already have this extent in the
	 * file.  This must be done before the btrfs_drop_extents run
	 * so we don't try to drop this extent.
	 */
	ret = btrfs_lookup_file_extent(trans, root, path, btrfs_ino(inode),
				       start, 0);

	if (ret == 0 &&
	    (found_type == BTRFS_FILE_EXTENT_REG ||
	     found_type == BTRFS_FILE_EXTENT_PREALLOC)) {
		struct btrfs_file_extent_item cmp1;
		struct btrfs_file_extent_item cmp2;
		struct btrfs_file_extent_item *existing;
		struct extent_buffer *leaf;

		leaf = path->nodes[0];
		existing = btrfs_item_ptr(leaf, path->slots[0],
					  struct btrfs_file_extent_item);

		read_extent_buffer(eb, &cmp1, (unsigned long)item,
				   sizeof(cmp1));
		read_extent_buffer(leaf, &cmp2, (unsigned long)existing,
				   sizeof(cmp2));

		/*
		 * we already have a pointer to this exact extent,
		 * we don't have to do anything
		 */
		if (memcmp(&cmp1, &cmp2, sizeof(cmp1)) == 0) {
			btrfs_release_path(path);
			goto out;
		}
	}
	btrfs_release_path(path);

	/* drop any overlapping extents */
	ret = btrfs_drop_extents(trans, root, inode, start, extent_end, 1);
	if (ret)
		goto out;

	if (found_type == BTRFS_FILE_EXTENT_REG ||
	    found_type == BTRFS_FILE_EXTENT_PREALLOC) {
		u64 offset;
		unsigned long dest_offset;
		struct btrfs_key ins;

		ret = btrfs_insert_empty_item(trans, root, path, key,
					      sizeof(*item));
		if (ret)
			goto out;
		dest_offset = btrfs_item_ptr_offset(path->nodes[0],
						    path->slots[0]);
		copy_extent_buffer(path->nodes[0], eb, dest_offset,
				(unsigned long)item,  sizeof(*item));

		ins.objectid = btrfs_file_extent_disk_bytenr(eb, item);
		ins.offset = btrfs_file_extent_disk_num_bytes(eb, item);
		ins.type = BTRFS_EXTENT_ITEM_KEY;
		offset = key->offset - btrfs_file_extent_offset(eb, item);

		/*
		 * Manually record dirty extent, as here we did a shallow
		 * file extent item copy and skip normal backref update,
		 * but modifying extent tree all by ourselves.
		 * So need to manually record dirty extent for qgroup,
		 * as the owner of the file extent changed from log tree
		 * (doesn't affect qgroup) to fs/file tree(affects qgroup)
		 */
		ret = btrfs_qgroup_insert_dirty_extent(trans, root->fs_info,
				btrfs_file_extent_disk_bytenr(eb, item),
				btrfs_file_extent_disk_num_bytes(eb, item),
				GFP_NOFS);
		if (ret < 0)
			goto out;

		if (ins.objectid > 0) {
			u64 csum_start;
			u64 csum_end;
			LIST_HEAD(ordered_sums);
			/*
			 * is this extent already allocated in the extent
			 * allocation tree?  If so, just add a reference
			 */
			ret = btrfs_lookup_data_extent(root, ins.objectid,
						ins.offset);
			if (ret == 0) {
				ret = btrfs_inc_extent_ref(trans, root,
						ins.objectid, ins.offset,
						0, root->root_key.objectid,
						key->objectid, offset);
				if (ret)
					goto out;
			} else {
				/*
				 * insert the extent pointer in the extent
				 * allocation tree
				 */
				ret = btrfs_alloc_logged_file_extent(trans,
						root, root->root_key.objectid,
						key->objectid, offset, &ins);
				if (ret)
					goto out;
			}
			btrfs_release_path(path);

			if (btrfs_file_extent_compression(eb, item)) {
				csum_start = ins.objectid;
				csum_end = csum_start + ins.offset;
			} else {
				csum_start = ins.objectid +
					btrfs_file_extent_offset(eb, item);
				csum_end = csum_start +
					btrfs_file_extent_num_bytes(eb, item);
			}

			ret = btrfs_lookup_csums_range(root->log_root,
						csum_start, csum_end - 1,
						&ordered_sums, 0);
			if (ret)
				goto out;
			/*
			 * Now delete all existing cums in the csum root that
			 * cover our range. We do this because we can have an
			 * extent that is completely referenced by one file
			 * extent item and partially referenced by another
			 * file extent item (like after using the clone or
			 * extent_same ioctls). In this case if we end up doing
			 * the replay of the one that partially references the
			 * extent first, and we do not do the csum deletion
			 * below, we can get 2 csum items in the csum tree that
			 * overlap each other. For example, imagine our log has
			 * the two following file extent items:
			 *
			 * key (257 EXTENT_DATA 409600)
			 *     extent data disk byte 12845056 nr 102400
			 *     extent data offset 20480 nr 20480 ram 102400
			 *
			 * key (257 EXTENT_DATA 819200)
			 *     extent data disk byte 12845056 nr 102400
			 *     extent data offset 0 nr 102400 ram 102400
			 *
			 * Where the second one fully references the 100K extent
			 * that starts at disk byte 12845056, and the log tree
			 * has a single csum item that covers the entire range
			 * of the extent:
			 *
			 * key (EXTENT_CSUM EXTENT_CSUM 12845056) itemsize 100
			 *
			 * After the first file extent item is replayed, the
			 * csum tree gets the following csum item:
			 *
			 * key (EXTENT_CSUM EXTENT_CSUM 12865536) itemsize 20
			 *
			 * Which covers the 20K sub-range starting at offset 20K
			 * of our extent. Now when we replay the second file
			 * extent item, if we do not delete existing csum items
			 * that cover any of its blocks, we end up getting two
			 * csum items in our csum tree that overlap each other:
			 *
			 * key (EXTENT_CSUM EXTENT_CSUM 12845056) itemsize 100
			 * key (EXTENT_CSUM EXTENT_CSUM 12865536) itemsize 20
			 *
			 * Which is a problem, because after this anyone trying
			 * to lookup up for the checksum of any block of our
			 * extent starting at an offset of 40K or higher, will
			 * end up looking at the second csum item only, which
			 * does not contain the checksum for any block starting
			 * at offset 40K or higher of our extent.
			 */
			while (!list_empty(&ordered_sums)) {
				struct btrfs_ordered_sum *sums;
				sums = list_entry(ordered_sums.next,
						struct btrfs_ordered_sum,
						list);
				if (!ret)
					ret = btrfs_del_csums(trans,
						      root->fs_info->csum_root,
						      sums->bytenr,
						      sums->len);
				if (!ret)
					ret = btrfs_csum_file_blocks(trans,
						root->fs_info->csum_root,
						sums);
				list_del(&sums->list);
				kfree(sums);
			}
			if (ret)
				goto out;
		} else {
			btrfs_release_path(path);
		}
	} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
		/* inline extents are easy, we just overwrite them */
		ret = overwrite_item(trans, root, path, eb, slot, key);
		if (ret)
			goto out;
	}

	inode_add_bytes(inode, nbytes);
	ret = btrfs_update_inode(trans, root, inode);
out:
	if (inode)
		iput(inode);
	return ret;
}

/*
 * when cleaning up conflicts between the directory names in the
 * subvolume, directory names in the log and directory names in the
 * inode back references, we may have to unlink inodes from directories.
 *
 * This is a helper function to do the unlink of a specific directory
 * item
 */
static noinline int drop_one_dir_item(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      struct btrfs_path *path,
				      struct inode *dir,
				      struct btrfs_dir_item *di)
{
	struct inode *inode;
	char *name;
	int name_len;
	struct extent_buffer *leaf;
	struct btrfs_key location;
	int ret;

	leaf = path->nodes[0];

	btrfs_dir_item_key_to_cpu(leaf, di, &location);
	name_len = btrfs_dir_name_len(leaf, di);
	name = kmalloc(name_len, GFP_NOFS);
	if (!name)
		return -ENOMEM;

	read_extent_buffer(leaf, name, (unsigned long)(di + 1), name_len);
	btrfs_release_path(path);

	inode = read_one_inode(root, location.objectid);
	if (!inode) {
		ret = -EIO;
		goto out;
	}

	ret = link_to_fixup_dir(trans, root, path, location.objectid);
	if (ret)
		goto out;

	ret = btrfs_unlink_inode(trans, root, dir, inode, name, name_len);
	if (ret)
		goto out;
	else
		ret = btrfs_run_delayed_items(trans, root);
out:
	kfree(name);
	iput(inode);
	return ret;
}

/*
 * helper function to see if a given name and sequence number found
 * in an inode back reference are already in a directory and correctly
 * point to this inode
 */
static noinline int inode_in_dir(struct btrfs_root *root,
				 struct btrfs_path *path,
				 u64 dirid, u64 objectid, u64 index,
				 const char *name, int name_len)
{
	struct btrfs_dir_item *di;
	struct btrfs_key location;
	int match = 0;

	di = btrfs_lookup_dir_index_item(NULL, root, path, dirid,
					 index, name, name_len, 0);
	if (di && !IS_ERR(di)) {
		btrfs_dir_item_key_to_cpu(path->nodes[0], di, &location);
		if (location.objectid != objectid)
			goto out;
	} else
		goto out;
	btrfs_release_path(path);

	di = btrfs_lookup_dir_item(NULL, root, path, dirid, name, name_len, 0);
	if (di && !IS_ERR(di)) {
		btrfs_dir_item_key_to_cpu(path->nodes[0], di, &location);
		if (location.objectid != objectid)
			goto out;
	} else
		goto out;
	match = 1;
out:
	btrfs_release_path(path);
	return match;
}

/*
 * helper function to check a log tree for a named back reference in
 * an inode.  This is used to decide if a back reference that is
 * found in the subvolume conflicts with what we find in the log.
 *
 * inode backreferences may have multiple refs in a single item,
 * during replay we process one reference at a time, and we don't
 * want to delete valid links to a file from the subvolume if that
 * link is also in the log.
 */
static noinline int backref_in_log(struct btrfs_root *log,
				   struct btrfs_key *key,
				   u64 ref_objectid,
				   const char *name, int namelen)
{
	struct btrfs_path *path;
	struct btrfs_inode_ref *ref;
	unsigned long ptr;
	unsigned long ptr_end;
	unsigned long name_ptr;
	int found_name_len;
	int item_size;
	int ret;
	int match = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(NULL, log, key, path, 0, 0);
	if (ret != 0)
		goto out;

	ptr = btrfs_item_ptr_offset(path->nodes[0], path->slots[0]);

	if (key->type == BTRFS_INODE_EXTREF_KEY) {
		if (btrfs_find_name_in_ext_backref(path, ref_objectid,
						   name, namelen, NULL))
			match = 1;

		goto out;
	}

	item_size = btrfs_item_size_nr(path->nodes[0], path->slots[0]);
	ptr_end = ptr + item_size;
	while (ptr < ptr_end) {
		ref = (struct btrfs_inode_ref *)ptr;
		found_name_len = btrfs_inode_ref_name_len(path->nodes[0], ref);
		if (found_name_len == namelen) {
			name_ptr = (unsigned long)(ref + 1);
			ret = memcmp_extent_buffer(path->nodes[0], name,
						   name_ptr, namelen);
			if (ret == 0) {
				match = 1;
				goto out;
			}
		}
		ptr = (unsigned long)(ref + 1) + found_name_len;
	}
out:
	btrfs_free_path(path);
	return match;
}

static inline int __add_inode_ref(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  struct btrfs_path *path,
				  struct btrfs_root *log_root,
				  struct inode *dir, struct inode *inode,
				  struct extent_buffer *eb,
				  u64 inode_objectid, u64 parent_objectid,
				  u64 ref_index, char *name, int namelen,
				  int *search_done)
{
	int ret;
	char *victim_name;
	int victim_name_len;
	struct extent_buffer *leaf;
	struct btrfs_dir_item *di;
	struct btrfs_key search_key;
	struct btrfs_inode_extref *extref;

again:
	/* Search old style refs */
	search_key.objectid = inode_objectid;
	search_key.type = BTRFS_INODE_REF_KEY;
	search_key.offset = parent_objectid;
	ret = btrfs_search_slot(NULL, root, &search_key, path, 0, 0);
	if (ret == 0) {
		struct btrfs_inode_ref *victim_ref;
		unsigned long ptr;
		unsigned long ptr_end;

		leaf = path->nodes[0];

		/* are we trying to overwrite a back ref for the root directory
		 * if so, just jump out, we're done
		 */
		if (search_key.objectid == search_key.offset)
			return 1;

		/* check all the names in this back reference to see
		 * if they are in the log.  if so, we allow them to stay
		 * otherwise they must be unlinked as a conflict
		 */
		ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
		ptr_end = ptr + btrfs_item_size_nr(leaf, path->slots[0]);
		while (ptr < ptr_end) {
			victim_ref = (struct btrfs_inode_ref *)ptr;
			victim_name_len = btrfs_inode_ref_name_len(leaf,
								   victim_ref);
			victim_name = kmalloc(victim_name_len, GFP_NOFS);
			if (!victim_name)
				return -ENOMEM;

			read_extent_buffer(leaf, victim_name,
					   (unsigned long)(victim_ref + 1),
					   victim_name_len);

			if (!backref_in_log(log_root, &search_key,
					    parent_objectid,
					    victim_name,
					    victim_name_len)) {
				inc_nlink(inode);
				btrfs_release_path(path);

				ret = btrfs_unlink_inode(trans, root, dir,
							 inode, victim_name,
							 victim_name_len);
				kfree(victim_name);
				if (ret)
					return ret;
				ret = btrfs_run_delayed_items(trans, root);
				if (ret)
					return ret;
				*search_done = 1;
				goto again;
			}
			kfree(victim_name);

			ptr = (unsigned long)(victim_ref + 1) + victim_name_len;
		}

		/*
		 * NOTE: we have searched root tree and checked the
		 * corresponding ref, it does not need to check again.
		 */
		*search_done = 1;
	}
	btrfs_release_path(path);

	/* Same search but for extended refs */
	extref = btrfs_lookup_inode_extref(NULL, root, path, name, namelen,
					   inode_objectid, parent_objectid, 0,
					   0);
	if (!IS_ERR_OR_NULL(extref)) {
		u32 item_size;
		u32 cur_offset = 0;
		unsigned long base;
		struct inode *victim_parent;

		leaf = path->nodes[0];

		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
		base = btrfs_item_ptr_offset(leaf, path->slots[0]);

		while (cur_offset < item_size) {
			extref = (struct btrfs_inode_extref *)(base + cur_offset);

			victim_name_len = btrfs_inode_extref_name_len(leaf, extref);

			if (btrfs_inode_extref_parent(leaf, extref) != parent_objectid)
				goto next;

			victim_name = kmalloc(victim_name_len, GFP_NOFS);
			if (!victim_name)
				return -ENOMEM;
			read_extent_buffer(leaf, victim_name, (unsigned long)&extref->name,
					   victim_name_len);

			search_key.objectid = inode_objectid;
			search_key.type = BTRFS_INODE_EXTREF_KEY;
			search_key.offset = btrfs_extref_hash(parent_objectid,
							      victim_name,
							      victim_name_len);
			ret = 0;
			if (!backref_in_log(log_root, &search_key,
					    parent_objectid, victim_name,
					    victim_name_len)) {
				ret = -ENOENT;
				victim_parent = read_one_inode(root,
							       parent_objectid);
				if (victim_parent) {
					inc_nlink(inode);
					btrfs_release_path(path);

					ret = btrfs_unlink_inode(trans, root,
								 victim_parent,
								 inode,
								 victim_name,
								 victim_name_len);
					if (!ret)
						ret = btrfs_run_delayed_items(
								  trans, root);
				}
				iput(victim_parent);
				kfree(victim_name);
				if (ret)
					return ret;
				*search_done = 1;
				goto again;
			}
			kfree(victim_name);
			if (ret)
				return ret;
next:
			cur_offset += victim_name_len + sizeof(*extref);
		}
		*search_done = 1;
	}
	btrfs_release_path(path);

	/* look for a conflicting sequence number */
	di = btrfs_lookup_dir_index_item(trans, root, path, btrfs_ino(dir),
					 ref_index, name, namelen, 0);
	if (di && !IS_ERR(di)) {
		ret = drop_one_dir_item(trans, root, path, dir, di);
		if (ret)
			return ret;
	}
	btrfs_release_path(path);

	/* look for a conflicing name */
	di = btrfs_lookup_dir_item(trans, root, path, btrfs_ino(dir),
				   name, namelen, 0);
	if (di && !IS_ERR(di)) {
		ret = drop_one_dir_item(trans, root, path, dir, di);
		if (ret)
			return ret;
	}
	btrfs_release_path(path);

	return 0;
}

static int extref_get_fields(struct extent_buffer *eb, unsigned long ref_ptr,
			     u32 *namelen, char **name, u64 *index,
			     u64 *parent_objectid)
{
	struct btrfs_inode_extref *extref;

	extref = (struct btrfs_inode_extref *)ref_ptr;

	*namelen = btrfs_inode_extref_name_len(eb, extref);
	*name = kmalloc(*namelen, GFP_NOFS);
	if (*name == NULL)
		return -ENOMEM;

	read_extent_buffer(eb, *name, (unsigned long)&extref->name,
			   *namelen);

	*index = btrfs_inode_extref_index(eb, extref);
	if (parent_objectid)
		*parent_objectid = btrfs_inode_extref_parent(eb, extref);

	return 0;
}

static int ref_get_fields(struct extent_buffer *eb, unsigned long ref_ptr,
			  u32 *namelen, char **name, u64 *index)
{
	struct btrfs_inode_ref *ref;

	ref = (struct btrfs_inode_ref *)ref_ptr;

	*namelen = btrfs_inode_ref_name_len(eb, ref);
	*name = kmalloc(*namelen, GFP_NOFS);
	if (*name == NULL)
		return -ENOMEM;

	read_extent_buffer(eb, *name, (unsigned long)(ref + 1), *namelen);

	*index = btrfs_inode_ref_index(eb, ref);

	return 0;
}

/*
 * replay one inode back reference item found in the log tree.
 * eb, slot and key refer to the buffer and key found in the log tree.
 * root is the destination we are replaying into, and path is for temp
 * use by this function.  (it should be released on return).
 */
static noinline int add_inode_ref(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  struct btrfs_root *log,
				  struct btrfs_path *path,
				  struct extent_buffer *eb, int slot,
				  struct btrfs_key *key)
{
	struct inode *dir = NULL;
	struct inode *inode = NULL;
	unsigned long ref_ptr;
	unsigned long ref_end;
	char *name = NULL;
	int namelen;
	int ret;
	int search_done = 0;
	int log_ref_ver = 0;
	u64 parent_objectid;
	u64 inode_objectid;
	u64 ref_index = 0;
	int ref_struct_size;

	ref_ptr = btrfs_item_ptr_offset(eb, slot);
	ref_end = ref_ptr + btrfs_item_size_nr(eb, slot);

	if (key->type == BTRFS_INODE_EXTREF_KEY) {
		struct btrfs_inode_extref *r;

		ref_struct_size = sizeof(struct btrfs_inode_extref);
		log_ref_ver = 1;
		r = (struct btrfs_inode_extref *)ref_ptr;
		parent_objectid = btrfs_inode_extref_parent(eb, r);
	} else {
		ref_struct_size = sizeof(struct btrfs_inode_ref);
		parent_objectid = key->offset;
	}
	inode_objectid = key->objectid;

	/*
	 * it is possible that we didn't log all the parent directories
	 * for a given inode.  If we don't find the dir, just don't
	 * copy the back ref in.  The link count fixup code will take
	 * care of the rest
	 */
	dir = read_one_inode(root, parent_objectid);
	if (!dir) {
		ret = -ENOENT;
		goto out;
	}

	inode = read_one_inode(root, inode_objectid);
	if (!inode) {
		ret = -EIO;
		goto out;
	}

	while (ref_ptr < ref_end) {
		if (log_ref_ver) {
			ret = extref_get_fields(eb, ref_ptr, &namelen, &name,
						&ref_index, &parent_objectid);
			/*
			 * parent object can change from one array
			 * item to another.
			 */
			if (!dir)
				dir = read_one_inode(root, parent_objectid);
			if (!dir) {
				ret = -ENOENT;
				goto out;
			}
		} else {
			ret = ref_get_fields(eb, ref_ptr, &namelen, &name,
					     &ref_index);
		}
		if (ret)
			goto out;

		/* if we already have a perfect match, we're done */
		if (!inode_in_dir(root, path, btrfs_ino(dir), btrfs_ino(inode),
				  ref_index, name, namelen)) {
			/*
			 * look for a conflicting back reference in the
			 * metadata. if we find one we have to unlink that name
			 * of the file before we add our new link.  Later on, we
			 * overwrite any existing back reference, and we don't
			 * want to create dangling pointers in the directory.
			 */

			if (!search_done) {
				ret = __add_inode_ref(trans, root, path, log,
						      dir, inode, eb,
						      inode_objectid,
						      parent_objectid,
						      ref_index, name, namelen,
						      &search_done);
				if (ret) {
					if (ret == 1)
						ret = 0;
					goto out;
				}
			}

			/* insert our name */
			ret = btrfs_add_link(trans, dir, inode, name, namelen,
					     0, ref_index);
			if (ret)
				goto out;

			btrfs_update_inode(trans, root, inode);
		}

		ref_ptr = (unsigned long)(ref_ptr + ref_struct_size) + namelen;
		kfree(name);
		name = NULL;
		if (log_ref_ver) {
			iput(dir);
			dir = NULL;
		}
	}

	/* finally write the back reference in the inode */
	ret = overwrite_item(trans, root, path, eb, slot, key);
out:
	btrfs_release_path(path);
	kfree(name);
	iput(dir);
	iput(inode);
	return ret;
}

static int insert_orphan_item(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root, u64 ino)
{
	int ret;

	ret = btrfs_insert_orphan_item(trans, root, ino);
	if (ret == -EEXIST)
		ret = 0;

	return ret;
}

static int count_inode_extrefs(struct btrfs_root *root,
			       struct inode *inode, struct btrfs_path *path)
{
	int ret = 0;
	int name_len;
	unsigned int nlink = 0;
	u32 item_size;
	u32 cur_offset = 0;
	u64 inode_objectid = btrfs_ino(inode);
	u64 offset = 0;
	unsigned long ptr;
	struct btrfs_inode_extref *extref;
	struct extent_buffer *leaf;

	while (1) {
		ret = btrfs_find_one_extref(root, inode_objectid, offset, path,
					    &extref, &offset);
		if (ret)
			break;

		leaf = path->nodes[0];
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
		ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
		cur_offset = 0;

		while (cur_offset < item_size) {
			extref = (struct btrfs_inode_extref *) (ptr + cur_offset);
			name_len = btrfs_inode_extref_name_len(leaf, extref);

			nlink++;

			cur_offset += name_len + sizeof(*extref);
		}

		offset++;
		btrfs_release_path(path);
	}
	btrfs_release_path(path);

	if (ret < 0 && ret != -ENOENT)
		return ret;
	return nlink;
}

static int count_inode_refs(struct btrfs_root *root,
			       struct inode *inode, struct btrfs_path *path)
{
	int ret;
	struct btrfs_key key;
	unsigned int nlink = 0;
	unsigned long ptr;
	unsigned long ptr_end;
	int name_len;
	u64 ino = btrfs_ino(inode);

	key.objectid = ino;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = (u64)-1;

	while (1) {
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			break;
		if (ret > 0) {
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		}
process_slot:
		btrfs_item_key_to_cpu(path->nodes[0], &key,
				      path->slots[0]);
		if (key.objectid != ino ||
		    key.type != BTRFS_INODE_REF_KEY)
			break;
		ptr = btrfs_item_ptr_offset(path->nodes[0], path->slots[0]);
		ptr_end = ptr + btrfs_item_size_nr(path->nodes[0],
						   path->slots[0]);
		while (ptr < ptr_end) {
			struct btrfs_inode_ref *ref;

			ref = (struct btrfs_inode_ref *)ptr;
			name_len = btrfs_inode_ref_name_len(path->nodes[0],
							    ref);
			ptr = (unsigned long)(ref + 1) + name_len;
			nlink++;
		}

		if (key.offset == 0)
			break;
		if (path->slots[0] > 0) {
			path->slots[0]--;
			goto process_slot;
		}
		key.offset--;
		btrfs_release_path(path);
	}
	btrfs_release_path(path);

	return nlink;
}

/*
 * There are a few corners where the link count of the file can't
 * be properly maintained during replay.  So, instead of adding
 * lots of complexity to the log code, we just scan the backrefs
 * for any file that has been through replay.
 *
 * The scan will update the link count on the inode to reflect the
 * number of back refs found.  If it goes down to zero, the iput
 * will free the inode.
 */
static noinline int fixup_inode_link_count(struct btrfs_trans_handle *trans,
					   struct btrfs_root *root,
					   struct inode *inode)
{
	struct btrfs_path *path;
	int ret;
	u64 nlink = 0;
	u64 ino = btrfs_ino(inode);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = count_inode_refs(root, inode, path);
	if (ret < 0)
		goto out;

	nlink = ret;

	ret = count_inode_extrefs(root, inode, path);
	if (ret < 0)
		goto out;

	nlink += ret;

	ret = 0;

	if (nlink != inode->i_nlink) {
		set_nlink(inode, nlink);
		btrfs_update_inode(trans, root, inode);
	}
	BTRFS_I(inode)->index_cnt = (u64)-1;

	if (inode->i_nlink == 0) {
		if (S_ISDIR(inode->i_mode)) {
			ret = replay_dir_deletes(trans, root, NULL, path,
						 ino, 1);
			if (ret)
				goto out;
		}
		ret = insert_orphan_item(trans, root, ino);
	}

out:
	btrfs_free_path(path);
	return ret;
}

static noinline int fixup_inode_link_counts(struct btrfs_trans_handle *trans,
					    struct btrfs_root *root,
					    struct btrfs_path *path)
{
	int ret;
	struct btrfs_key key;
	struct inode *inode;

	key.objectid = BTRFS_TREE_LOG_FIXUP_OBJECTID;
	key.type = BTRFS_ORPHAN_ITEM_KEY;
	key.offset = (u64)-1;
	while (1) {
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0)
			break;

		if (ret == 1) {
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		}

		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (key.objectid != BTRFS_TREE_LOG_FIXUP_OBJECTID ||
		    key.type != BTRFS_ORPHAN_ITEM_KEY)
			break;

		ret = btrfs_del_item(trans, root, path);
		if (ret)
			goto out;

		btrfs_release_path(path);
		inode = read_one_inode(root, key.offset);
		if (!inode)
			return -EIO;

		ret = fixup_inode_link_count(trans, root, inode);
		iput(inode);
		if (ret)
			goto out;

		/*
		 * fixup on a directory may create new entries,
		 * make sure we always look for the highset possible
		 * offset
		 */
		key.offset = (u64)-1;
	}
	ret = 0;
out:
	btrfs_release_path(path);
	return ret;
}


/*
 * record a given inode in the fixup dir so we can check its link
 * count when replay is done.  The link count is incremented here
 * so the inode won't go away until we check it
 */
static noinline int link_to_fixup_dir(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      struct btrfs_path *path,
				      u64 objectid)
{
	struct btrfs_key key;
	int ret = 0;
	struct inode *inode;

	inode = read_one_inode(root, objectid);
	if (!inode)
		return -EIO;

	key.objectid = BTRFS_TREE_LOG_FIXUP_OBJECTID;
	key.type = BTRFS_ORPHAN_ITEM_KEY;
	key.offset = objectid;

	ret = btrfs_insert_empty_item(trans, root, path, &key, 0);

	btrfs_release_path(path);
	if (ret == 0) {
		if (!inode->i_nlink)
			set_nlink(inode, 1);
		else
			inc_nlink(inode);
		ret = btrfs_update_inode(trans, root, inode);
	} else if (ret == -EEXIST) {
		ret = 0;
	} else {
		BUG(); /* Logic Error */
	}
	iput(inode);

	return ret;
}

/*
 * when replaying the log for a directory, we only insert names
 * for inodes that actually exist.  This means an fsync on a directory
 * does not implicitly fsync all the new files in it
 */
static noinline int insert_one_name(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root,
				    u64 dirid, u64 index,
				    char *name, int name_len,
				    struct btrfs_key *location)
{
	struct inode *inode;
	struct inode *dir;
	int ret;

	inode = read_one_inode(root, location->objectid);
	if (!inode)
		return -ENOENT;

	dir = read_one_inode(root, dirid);
	if (!dir) {
		iput(inode);
		return -EIO;
	}

	ret = btrfs_add_link(trans, dir, inode, name, name_len, 1, index);

	/* FIXME, put inode into FIXUP list */

	iput(inode);
	iput(dir);
	return ret;
}

/*
 * Return true if an inode reference exists in the log for the given name,
 * inode and parent inode.
 */
static bool name_in_log_ref(struct btrfs_root *log_root,
			    const char *name, const int name_len,
			    const u64 dirid, const u64 ino)
{
	struct btrfs_key search_key;

	search_key.objectid = ino;
	search_key.type = BTRFS_INODE_REF_KEY;
	search_key.offset = dirid;
	if (backref_in_log(log_root, &search_key, dirid, name, name_len))
		return true;

	search_key.type = BTRFS_INODE_EXTREF_KEY;
	search_key.offset = btrfs_extref_hash(dirid, name, name_len);
	if (backref_in_log(log_root, &search_key, dirid, name, name_len))
		return true;

	return false;
}

/*
 * take a single entry in a log directory item and replay it into
 * the subvolume.
 *
 * if a conflicting item exists in the subdirectory already,
 * the inode it points to is unlinked and put into the link count
 * fix up tree.
 *
 * If a name from the log points to a file or directory that does
 * not exist in the FS, it is skipped.  fsyncs on directories
 * do not force down inodes inside that directory, just changes to the
 * names or unlinks in a directory.
 *
 * Returns < 0 on error, 0 if the name wasn't replayed (dentry points to a
 * non-existing inode) and 1 if the name was replayed.
 */
static noinline int replay_one_name(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root,
				    struct btrfs_path *path,
				    struct extent_buffer *eb,
				    struct btrfs_dir_item *di,
				    struct btrfs_key *key)
{
	char *name;
	int name_len;
	struct btrfs_dir_item *dst_di;
	struct btrfs_key found_key;
	struct btrfs_key log_key;
	struct inode *dir;
	u8 log_type;
	int exists;
	int ret = 0;
	bool update_size = (key->type == BTRFS_DIR_INDEX_KEY);
	bool name_added = false;

	dir = read_one_inode(root, key->objectid);
	if (!dir)
		return -EIO;

	name_len = btrfs_dir_name_len(eb, di);
	name = kmalloc(name_len, GFP_NOFS);
	if (!name) {
		ret = -ENOMEM;
		goto out;
	}

	log_type = btrfs_dir_type(eb, di);
	read_extent_buffer(eb, name, (unsigned long)(di + 1),
		   name_len);

	btrfs_dir_item_key_to_cpu(eb, di, &log_key);
	exists = btrfs_lookup_inode(trans, root, path, &log_key, 0);
	if (exists == 0)
		exists = 1;
	else
		exists = 0;
	btrfs_release_path(path);

	if (key->type == BTRFS_DIR_ITEM_KEY) {
		dst_di = btrfs_lookup_dir_item(trans, root, path, key->objectid,
				       name, name_len, 1);
	} else if (key->type == BTRFS_DIR_INDEX_KEY) {
		dst_di = btrfs_lookup_dir_index_item(trans, root, path,
						     key->objectid,
						     key->offset, name,
						     name_len, 1);
	} else {
		/* Corruption */
		ret = -EINVAL;
		goto out;
	}
	if (IS_ERR_OR_NULL(dst_di)) {
		/* we need a sequence number to insert, so we only
		 * do inserts for the BTRFS_DIR_INDEX_KEY types
		 */
		if (key->type != BTRFS_DIR_INDEX_KEY)
			goto out;
		goto insert;
	}

	btrfs_dir_item_key_to_cpu(path->nodes[0], dst_di, &found_key);
	/* the existing item matches the logged item */
	if (found_key.objectid == log_key.objectid &&
	    found_key.type == log_key.type &&
	    found_key.offset == log_key.offset &&
	    btrfs_dir_type(path->nodes[0], dst_di) == log_type) {
		update_size = false;
		goto out;
	}

	/*
	 * don't drop the conflicting directory entry if the inode
	 * for the new entry doesn't exist
	 */
	if (!exists)
		goto out;

	ret = drop_one_dir_item(trans, root, path, dir, dst_di);
	if (ret)
		goto out;

	if (key->type == BTRFS_DIR_INDEX_KEY)
		goto insert;
out:
	btrfs_release_path(path);
	if (!ret && update_size) {
		btrfs_i_size_write(dir, dir->i_size + name_len * 2);
		ret = btrfs_update_inode(trans, root, dir);
	}
	kfree(name);
	iput(dir);
	if (!ret && name_added)
		ret = 1;
	return ret;

insert:
	if (name_in_log_ref(root->log_root, name, name_len,
			    key->objectid, log_key.objectid)) {
		/* The dentry will be added later. */
		ret = 0;
		update_size = false;
		goto out;
	}
	btrfs_release_path(path);
	ret = insert_one_name(trans, root, key->objectid, key->offset,
			      name, name_len, &log_key);
	if (ret && ret != -ENOENT && ret != -EEXIST)
		goto out;
	if (!ret)
		name_added = true;
	update_size = false;
	ret = 0;
	goto out;
}

/*
 * find all the names in a directory item and reconcile them into
 * the subvolume.  Only BTRFS_DIR_ITEM_KEY types will have more than
 * one name in a directory item, but the same code gets used for
 * both directory index types
 */
static noinline int replay_one_dir_item(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_path *path,
					struct extent_buffer *eb, int slot,
					struct btrfs_key *key)
{
	int ret = 0;
	u32 item_size = btrfs_item_size_nr(eb, slot);
	struct btrfs_dir_item *di;
	int name_len;
	unsigned long ptr;
	unsigned long ptr_end;
	struct btrfs_path *fixup_path = NULL;

	ptr = btrfs_item_ptr_offset(eb, slot);
	ptr_end = ptr + item_size;
	while (ptr < ptr_end) {
		di = (struct btrfs_dir_item *)ptr;
		if (verify_dir_item(root, eb, di))
			return -EIO;
		name_len = btrfs_dir_name_len(eb, di);
		ret = replay_one_name(trans, root, path, eb, di, key);
		if (ret < 0)
			break;
		ptr = (unsigned long)(di + 1);
		ptr += name_len;

		/*
		 * If this entry refers to a non-directory (directories can not
		 * have a link count > 1) and it was added in the transaction
		 * that was not committed, make sure we fixup the link count of
		 * the inode it the entry points to. Otherwise something like
		 * the following would result in a directory pointing to an
		 * inode with a wrong link that does not account for this dir
		 * entry:
		 *
		 * mkdir testdir
		 * touch testdir/foo
		 * touch testdir/bar
		 * sync
		 *
		 * ln testdir/bar testdir/bar_link
		 * ln testdir/foo testdir/foo_link
		 * xfs_io -c "fsync" testdir/bar
		 *
		 * <power failure>
		 *
		 * mount fs, log replay happens
		 *
		 * File foo would remain with a link count of 1 when it has two
		 * entries pointing to it in the directory testdir. This would
		 * make it impossible to ever delete the parent directory has
		 * it would result in stale dentries that can never be deleted.
		 */
		if (ret == 1 && btrfs_dir_type(eb, di) != BTRFS_FT_DIR) {
			struct btrfs_key di_key;

			if (!fixup_path) {
				fixup_path = btrfs_alloc_path();
				if (!fixup_path) {
					ret = -ENOMEM;
					break;
				}
			}

			btrfs_dir_item_key_to_cpu(eb, di, &di_key);
			ret = link_to_fixup_dir(trans, root, fixup_path,
						di_key.objectid);
			if (ret)
				break;
		}
		ret = 0;
	}
	btrfs_free_path(fixup_path);
	return ret;
}

/*
 * directory replay has two parts.  There are the standard directory
 * items in the log copied from the subvolume, and range items
 * created in the log while the subvolume was logged.
 *
 * The range items tell us which parts of the key space the log
 * is authoritative for.  During replay, if a key in the subvolume
 * directory is in a logged range item, but not actually in the log
 * that means it was deleted from the directory before the fsync
 * and should be removed.
 */
static noinline int find_dir_range(struct btrfs_root *root,
				   struct btrfs_path *path,
				   u64 dirid, int key_type,
				   u64 *start_ret, u64 *end_ret)
{
	struct btrfs_key key;
	u64 found_end;
	struct btrfs_dir_log_item *item;
	int ret;
	int nritems;

	if (*start_ret == (u64)-1)
		return 1;

	key.objectid = dirid;
	key.type = key_type;
	key.offset = *start_ret;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		if (path->slots[0] == 0)
			goto out;
		path->slots[0]--;
	}
	if (ret != 0)
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

	if (key.type != key_type || key.objectid != dirid) {
		ret = 1;
		goto next;
	}
	item = btrfs_item_ptr(path->nodes[0], path->slots[0],
			      struct btrfs_dir_log_item);
	found_end = btrfs_dir_log_end(path->nodes[0], item);

	if (*start_ret >= key.offset && *start_ret <= found_end) {
		ret = 0;
		*start_ret = key.offset;
		*end_ret = found_end;
		goto out;
	}
	ret = 1;
next:
	/* check the next slot in the tree to see if it is a valid item */
	nritems = btrfs_header_nritems(path->nodes[0]);
	if (path->slots[0] >= nritems) {
		ret = btrfs_next_leaf(root, path);
		if (ret)
			goto out;
	} else {
		path->slots[0]++;
	}

	btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

	if (key.type != key_type || key.objectid != dirid) {
		ret = 1;
		goto out;
	}
	item = btrfs_item_ptr(path->nodes[0], path->slots[0],
			      struct btrfs_dir_log_item);
	found_end = btrfs_dir_log_end(path->nodes[0], item);
	*start_ret = key.offset;
	*end_ret = found_end;
	ret = 0;
out:
	btrfs_release_path(path);
	return ret;
}

/*
 * this looks for a given directory item in the log.  If the directory
 * item is not in the log, the item is removed and the inode it points
 * to is unlinked
 */
static noinline int check_item_in_log(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      struct btrfs_root *log,
				      struct btrfs_path *path,
				      struct btrfs_path *log_path,
				      struct inode *dir,
				      struct btrfs_key *dir_key)
{
	int ret;
	struct extent_buffer *eb;
	int slot;
	u32 item_size;
	struct btrfs_dir_item *di;
	struct btrfs_dir_item *log_di;
	int name_len;
	unsigned long ptr;
	unsigned long ptr_end;
	char *name;
	struct inode *inode;
	struct btrfs_key location;

again:
	eb = path->nodes[0];
	slot = path->slots[0];
	item_size = btrfs_item_size_nr(eb, slot);
	ptr = btrfs_item_ptr_offset(eb, slot);
	ptr_end = ptr + item_size;
	while (ptr < ptr_end) {
		di = (struct btrfs_dir_item *)ptr;
		if (verify_dir_item(root, eb, di)) {
			ret = -EIO;
			goto out;
		}

		name_len = btrfs_dir_name_len(eb, di);
		name = kmalloc(name_len, GFP_NOFS);
		if (!name) {
			ret = -ENOMEM;
			goto out;
		}
		read_extent_buffer(eb, name, (unsigned long)(di + 1),
				  name_len);
		log_di = NULL;
		if (log && dir_key->type == BTRFS_DIR_ITEM_KEY) {
			log_di = btrfs_lookup_dir_item(trans, log, log_path,
						       dir_key->objectid,
						       name, name_len, 0);
		} else if (log && dir_key->type == BTRFS_DIR_INDEX_KEY) {
			log_di = btrfs_lookup_dir_index_item(trans, log,
						     log_path,
						     dir_key->objectid,
						     dir_key->offset,
						     name, name_len, 0);
		}
		if (!log_di || (IS_ERR(log_di) && PTR_ERR(log_di) == -ENOENT)) {
			btrfs_dir_item_key_to_cpu(eb, di, &location);
			btrfs_release_path(path);
			btrfs_release_path(log_path);
			inode = read_one_inode(root, location.objectid);
			if (!inode) {
				kfree(name);
				return -EIO;
			}

			ret = link_to_fixup_dir(trans, root,
						path, location.objectid);
			if (ret) {
				kfree(name);
				iput(inode);
				goto out;
			}

			inc_nlink(inode);
			ret = btrfs_unlink_inode(trans, root, dir, inode,
						 name, name_len);
			if (!ret)
				ret = btrfs_run_delayed_items(trans, root);
			kfree(name);
			iput(inode);
			if (ret)
				goto out;

			/* there might still be more names under this key
			 * check and repeat if required
			 */
			ret = btrfs_search_slot(NULL, root, dir_key, path,
						0, 0);
			if (ret == 0)
				goto again;
			ret = 0;
			goto out;
		} else if (IS_ERR(log_di)) {
			kfree(name);
			return PTR_ERR(log_di);
		}
		btrfs_release_path(log_path);
		kfree(name);

		ptr = (unsigned long)(di + 1);
		ptr += name_len;
	}
	ret = 0;
out:
	btrfs_release_path(path);
	btrfs_release_path(log_path);
	return ret;
}

static int replay_xattr_deletes(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct btrfs_root *log,
			      struct btrfs_path *path,
			      const u64 ino)
{
	struct btrfs_key search_key;
	struct btrfs_path *log_path;
	int i;
	int nritems;
	int ret;

	log_path = btrfs_alloc_path();
	if (!log_path)
		return -ENOMEM;

	search_key.objectid = ino;
	search_key.type = BTRFS_XATTR_ITEM_KEY;
	search_key.offset = 0;
again:
	ret = btrfs_search_slot(NULL, root, &search_key, path, 0, 0);
	if (ret < 0)
		goto out;
process_leaf:
	nritems = btrfs_header_nritems(path->nodes[0]);
	for (i = path->slots[0]; i < nritems; i++) {
		struct btrfs_key key;
		struct btrfs_dir_item *di;
		struct btrfs_dir_item *log_di;
		u32 total_size;
		u32 cur;

		btrfs_item_key_to_cpu(path->nodes[0], &key, i);
		if (key.objectid != ino || key.type != BTRFS_XATTR_ITEM_KEY) {
			ret = 0;
			goto out;
		}

		di = btrfs_item_ptr(path->nodes[0], i, struct btrfs_dir_item);
		total_size = btrfs_item_size_nr(path->nodes[0], i);
		cur = 0;
		while (cur < total_size) {
			u16 name_len = btrfs_dir_name_len(path->nodes[0], di);
			u16 data_len = btrfs_dir_data_len(path->nodes[0], di);
			u32 this_len = sizeof(*di) + name_len + data_len;
			char *name;

			name = kmalloc(name_len, GFP_NOFS);
			if (!name) {
				ret = -ENOMEM;
				goto out;
			}
			read_extent_buffer(path->nodes[0], name,
					   (unsigned long)(di + 1), name_len);

			log_di = btrfs_lookup_xattr(NULL, log, log_path, ino,
						    name, name_len, 0);
			btrfs_release_path(log_path);
			if (!log_di) {
				/* Doesn't exist in log tree, so delete it. */
				btrfs_release_path(path);
				di = btrfs_lookup_xattr(trans, root, path, ino,
							name, name_len, -1);
				kfree(name);
				if (IS_ERR(di)) {
					ret = PTR_ERR(di);
					goto out;
				}
				ASSERT(di);
				ret = btrfs_delete_one_dir_name(trans, root,
								path, di);
				if (ret)
					goto out;
				btrfs_release_path(path);
				search_key = key;
				goto again;
			}
			kfree(name);
			if (IS_ERR(log_di)) {
				ret = PTR_ERR(log_di);
				goto out;
			}
			cur += this_len;
			di = (struct btrfs_dir_item *)((char *)di + this_len);
		}
	}
	ret = btrfs_next_leaf(root, path);
	if (ret > 0)
		ret = 0;
	else if (ret == 0)
		goto process_leaf;
out:
	btrfs_free_path(log_path);
	btrfs_release_path(path);
	return ret;
}


/*
 * deletion replay happens before we copy any new directory items
 * out of the log or out of backreferences from inodes.  It
 * scans the log to find ranges of keys that log is authoritative for,
 * and then scans the directory to find items in those ranges that are
 * not present in the log.
 *
 * Anything we don't find in the log is unlinked and removed from the
 * directory.
 */
static noinline int replay_dir_deletes(struct btrfs_trans_handle *trans,
				       struct btrfs_root *root,
				       struct btrfs_root *log,
				       struct btrfs_path *path,
				       u64 dirid, int del_all)
{
	u64 range_start;
	u64 range_end;
	int key_type = BTRFS_DIR_LOG_ITEM_KEY;
	int ret = 0;
	struct btrfs_key dir_key;
	struct btrfs_key found_key;
	struct btrfs_path *log_path;
	struct inode *dir;

	dir_key.objectid = dirid;
	dir_key.type = BTRFS_DIR_ITEM_KEY;
	log_path = btrfs_alloc_path();
	if (!log_path)
		return -ENOMEM;

	dir = read_one_inode(root, dirid);
	/* it isn't an error if the inode isn't there, that can happen
	 * because we replay the deletes before we copy in the inode item
	 * from the log
	 */
	if (!dir) {
		btrfs_free_path(log_path);
		return 0;
	}
again:
	range_start = 0;
	range_end = 0;
	while (1) {
		if (del_all)
			range_end = (u64)-1;
		else {
			ret = find_dir_range(log, path, dirid, key_type,
					     &range_start, &range_end);
			if (ret != 0)
				break;
		}

		dir_key.offset = range_start;
		while (1) {
			int nritems;
			ret = btrfs_search_slot(NULL, root, &dir_key, path,
						0, 0);
			if (ret < 0)
				goto out;

			nritems = btrfs_header_nritems(path->nodes[0]);
			if (path->slots[0] >= nritems) {
				ret = btrfs_next_leaf(root, path);
				if (ret)
					break;
			}
			btrfs_item_key_to_cpu(path->nodes[0], &found_key,
					      path->slots[0]);
			if (found_key.objectid != dirid ||
			    found_key.type != dir_key.type)
				goto next_type;

			if (found_key.offset > range_end)
				break;

			ret = check_item_in_log(trans, root, log, path,
						log_path, dir,
						&found_key);
			if (ret)
				goto out;
			if (found_key.offset == (u64)-1)
				break;
			dir_key.offset = found_key.offset + 1;
		}
		btrfs_release_path(path);
		if (range_end == (u64)-1)
			break;
		range_start = range_end + 1;
	}

next_type:
	ret = 0;
	if (key_type == BTRFS_DIR_LOG_ITEM_KEY) {
		key_type = BTRFS_DIR_LOG_INDEX_KEY;
		dir_key.type = BTRFS_DIR_INDEX_KEY;
		btrfs_release_path(path);
		goto again;
	}
out:
	btrfs_release_path(path);
	btrfs_free_path(log_path);
	iput(dir);
	return ret;
}

/*
 * the process_func used to replay items from the log tree.  This
 * gets called in two different stages.  The first stage just looks
 * for inodes and makes sure they are all copied into the subvolume.
 *
 * The second stage copies all the other item types from the log into
 * the subvolume.  The two stage approach is slower, but gets rid of
 * lots of complexity around inodes referencing other inodes that exist
 * only in the log (references come from either directory items or inode
 * back refs).
 */
static int replay_one_buffer(struct btrfs_root *log, struct extent_buffer *eb,
			     struct walk_control *wc, u64 gen)
{
	int nritems;
	struct btrfs_path *path;
	struct btrfs_root *root = wc->replay_dest;
	struct btrfs_key key;
	int level;
	int i;
	int ret;

	ret = btrfs_read_buffer(eb, gen);
	if (ret)
		return ret;

	level = btrfs_header_level(eb);

	if (level != 0)
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	nritems = btrfs_header_nritems(eb);
	for (i = 0; i < nritems; i++) {
		btrfs_item_key_to_cpu(eb, &key, i);

		/* inode keys are done during the first stage */
		if (key.type == BTRFS_INODE_ITEM_KEY &&
		    wc->stage == LOG_WALK_REPLAY_INODES) {
			struct btrfs_inode_item *inode_item;
			u32 mode;

			inode_item = btrfs_item_ptr(eb, i,
					    struct btrfs_inode_item);
			ret = replay_xattr_deletes(wc->trans, root, log,
						   path, key.objectid);
			if (ret)
				break;
			mode = btrfs_inode_mode(eb, inode_item);
			if (S_ISDIR(mode)) {
				ret = replay_dir_deletes(wc->trans,
					 root, log, path, key.objectid, 0);
				if (ret)
					break;
			}
			ret = overwrite_item(wc->trans, root, path,
					     eb, i, &key);
			if (ret)
				break;

			/* for regular files, make sure corresponding
			 * orphan item exist. extents past the new EOF
			 * will be truncated later by orphan cleanup.
			 */
			if (S_ISREG(mode)) {
				ret = insert_orphan_item(wc->trans, root,
							 key.objectid);
				if (ret)
					break;
			}

			ret = link_to_fixup_dir(wc->trans, root,
						path, key.objectid);
			if (ret)
				break;
		}

		if (key.type == BTRFS_DIR_INDEX_KEY &&
		    wc->stage == LOG_WALK_REPLAY_DIR_INDEX) {
			ret = replay_one_dir_item(wc->trans, root, path,
						  eb, i, &key);
			if (ret)
				break;
		}

		if (wc->stage < LOG_WALK_REPLAY_ALL)
			continue;

		/* these keys are simply copied */
		if (key.type == BTRFS_XATTR_ITEM_KEY) {
			ret = overwrite_item(wc->trans, root, path,
					     eb, i, &key);
			if (ret)
				break;
		} else if (key.type == BTRFS_INODE_REF_KEY ||
			   key.type == BTRFS_INODE_EXTREF_KEY) {
			ret = add_inode_ref(wc->trans, root, log, path,
					    eb, i, &key);
			if (ret && ret != -ENOENT)
				break;
			ret = 0;
		} else if (key.type == BTRFS_EXTENT_DATA_KEY) {
			ret = replay_one_extent(wc->trans, root, path,
						eb, i, &key);
			if (ret)
				break;
		} else if (key.type == BTRFS_DIR_ITEM_KEY) {
			ret = replay_one_dir_item(wc->trans, root, path,
						  eb, i, &key);
			if (ret)
				break;
		}
	}
	btrfs_free_path(path);
	return ret;
}

static noinline int walk_down_log_tree(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct btrfs_path *path, int *level,
				   struct walk_control *wc)
{
	u64 root_owner;
	u64 bytenr;
	u64 ptr_gen;
	struct extent_buffer *next;
	struct extent_buffer *cur;
	struct extent_buffer *parent;
	u32 blocksize;
	int ret = 0;

	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);

	while (*level > 0) {
		WARN_ON(*level < 0);
		WARN_ON(*level >= BTRFS_MAX_LEVEL);
		cur = path->nodes[*level];

		WARN_ON(btrfs_header_level(cur) != *level);

		if (path->slots[*level] >=
		    btrfs_header_nritems(cur))
			break;

		bytenr = btrfs_node_blockptr(cur, path->slots[*level]);
		ptr_gen = btrfs_node_ptr_generation(cur, path->slots[*level]);
		blocksize = root->nodesize;

		parent = path->nodes[*level];
		root_owner = btrfs_header_owner(parent);

		next = btrfs_find_create_tree_block(root, bytenr);
		if (IS_ERR(next))
			return PTR_ERR(next);

		if (*level == 1) {
			ret = wc->process_func(root, next, wc, ptr_gen);
			if (ret) {
				free_extent_buffer(next);
				return ret;
			}

			path->slots[*level]++;
			if (wc->free) {
				ret = btrfs_read_buffer(next, ptr_gen);
				if (ret) {
					free_extent_buffer(next);
					return ret;
				}

				if (trans) {
					btrfs_tree_lock(next);
					btrfs_set_lock_blocking(next);
					clean_tree_block(trans, root->fs_info,
							next);
					btrfs_wait_tree_block_writeback(next);
					btrfs_tree_unlock(next);
				}

				WARN_ON(root_owner !=
					BTRFS_TREE_LOG_OBJECTID);
				ret = btrfs_free_and_pin_reserved_extent(root,
							 bytenr, blocksize);
				if (ret) {
					free_extent_buffer(next);
					return ret;
				}
			}
			free_extent_buffer(next);
			continue;
		}
		ret = btrfs_read_buffer(next, ptr_gen);
		if (ret) {
			free_extent_buffer(next);
			return ret;
		}

		WARN_ON(*level <= 0);
		if (path->nodes[*level-1])
			free_extent_buffer(path->nodes[*level-1]);
		path->nodes[*level-1] = next;
		*level = btrfs_header_level(next);
		path->slots[*level] = 0;
		cond_resched();
	}
	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);

	path->slots[*level] = btrfs_header_nritems(path->nodes[*level]);

	cond_resched();
	return 0;
}

static noinline int walk_up_log_tree(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path, int *level,
				 struct walk_control *wc)
{
	u64 root_owner;
	int i;
	int slot;
	int ret;

	for (i = *level; i < BTRFS_MAX_LEVEL - 1 && path->nodes[i]; i++) {
		slot = path->slots[i];
		if (slot + 1 < btrfs_header_nritems(path->nodes[i])) {
			path->slots[i]++;
			*level = i;
			WARN_ON(*level == 0);
			return 0;
		} else {
			struct extent_buffer *parent;
			if (path->nodes[*level] == root->node)
				parent = path->nodes[*level];
			else
				parent = path->nodes[*level + 1];

			root_owner = btrfs_header_owner(parent);
			ret = wc->process_func(root, path->nodes[*level], wc,
				 btrfs_header_generation(path->nodes[*level]));
			if (ret)
				return ret;

			if (wc->free) {
				struct extent_buffer *next;

				next = path->nodes[*level];

				if (trans) {
					btrfs_tree_lock(next);
					btrfs_set_lock_blocking(next);
					clean_tree_block(trans, root->fs_info,
							next);
					btrfs_wait_tree_block_writeback(next);
					btrfs_tree_unlock(next);
				}

				WARN_ON(root_owner != BTRFS_TREE_LOG_OBJECTID);
				ret = btrfs_free_and_pin_reserved_extent(root,
						path->nodes[*level]->start,
						path->nodes[*level]->len);
				if (ret)
					return ret;
			}
			free_extent_buffer(path->nodes[*level]);
			path->nodes[*level] = NULL;
			*level = i + 1;
		}
	}
	return 1;
}

/*
 * drop the reference count on the tree rooted at 'snap'.  This traverses
 * the tree freeing any blocks that have a ref count of zero after being
 * decremented.
 */
static int walk_log_tree(struct btrfs_trans_handle *trans,
			 struct btrfs_root *log, struct walk_control *wc)
{
	int ret = 0;
	int wret;
	int level;
	struct btrfs_path *path;
	int orig_level;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	level = btrfs_header_level(log->node);
	orig_level = level;
	path->nodes[level] = log->node;
	extent_buffer_get(log->node);
	path->slots[level] = 0;

	while (1) {
		wret = walk_down_log_tree(trans, log, path, &level, wc);
		if (wret > 0)
			break;
		if (wret < 0) {
			ret = wret;
			goto out;
		}

		wret = walk_up_log_tree(trans, log, path, &level, wc);
		if (wret > 0)
			break;
		if (wret < 0) {
			ret = wret;
			goto out;
		}
	}

	/* was the root node processed? if not, catch it here */
	if (path->nodes[orig_level]) {
		ret = wc->process_func(log, path->nodes[orig_level], wc,
			 btrfs_header_generation(path->nodes[orig_level]));
		if (ret)
			goto out;
		if (wc->free) {
			struct extent_buffer *next;

			next = path->nodes[orig_level];

			if (trans) {
				btrfs_tree_lock(next);
				btrfs_set_lock_blocking(next);
				clean_tree_block(trans, log->fs_info, next);
				btrfs_wait_tree_block_writeback(next);
				btrfs_tree_unlock(next);
			}

			WARN_ON(log->root_key.objectid !=
				BTRFS_TREE_LOG_OBJECTID);
			ret = btrfs_free_and_pin_reserved_extent(log, next->start,
							 next->len);
			if (ret)
				goto out;
		}
	}

out:
	btrfs_free_path(path);
	return ret;
}

/*
 * helper function to update the item for a given subvolumes log root
 * in the tree of log roots
 */
static int update_log_root(struct btrfs_trans_handle *trans,
			   struct btrfs_root *log)
{
	int ret;

	if (log->log_transid == 1) {
		/* insert root item on the first sync */
		ret = btrfs_insert_root(trans, log->fs_info->log_root_tree,
				&log->root_key, &log->root_item);
	} else {
		ret = btrfs_update_root(trans, log->fs_info->log_root_tree,
				&log->root_key, &log->root_item);
	}
	return ret;
}

static void wait_log_commit(struct btrfs_root *root, int transid)
{
	DEFINE_WAIT(wait);
	int index = transid % 2;

	/*
	 * we only allow two pending log transactions at a time,
	 * so we know that if ours is more than 2 older than the
	 * current transaction, we're done
	 */
	do {
		prepare_to_wait(&root->log_commit_wait[index],
				&wait, TASK_UNINTERRUPTIBLE);
		mutex_unlock(&root->log_mutex);

		if (root->log_transid_committed < transid &&
		    atomic_read(&root->log_commit[index]))
			schedule();

		finish_wait(&root->log_commit_wait[index], &wait);
		mutex_lock(&root->log_mutex);
	} while (root->log_transid_committed < transid &&
		 atomic_read(&root->log_commit[index]));
}

static void wait_for_writer(struct btrfs_root *root)
{
	DEFINE_WAIT(wait);

	while (atomic_read(&root->log_writers)) {
		prepare_to_wait(&root->log_writer_wait,
				&wait, TASK_UNINTERRUPTIBLE);
		mutex_unlock(&root->log_mutex);
		if (atomic_read(&root->log_writers))
			schedule();
		finish_wait(&root->log_writer_wait, &wait);
		mutex_lock(&root->log_mutex);
	}
}

static inline void btrfs_remove_log_ctx(struct btrfs_root *root,
					struct btrfs_log_ctx *ctx)
{
	if (!ctx)
		return;

	mutex_lock(&root->log_mutex);
	list_del_init(&ctx->list);
	mutex_unlock(&root->log_mutex);
}

/* 
 * Invoked in log mutex context, or be sure there is no other task which
 * can access the list.
 */
static inline void btrfs_remove_all_log_ctxs(struct btrfs_root *root,
					     int index, int error)
{
	struct btrfs_log_ctx *ctx;
	struct btrfs_log_ctx *safe;

	list_for_each_entry_safe(ctx, safe, &root->log_ctxs[index], list) {
		list_del_init(&ctx->list);
		ctx->log_ret = error;
	}

	INIT_LIST_HEAD(&root->log_ctxs[index]);
}

/*
 * btrfs_sync_log does sends a given tree log down to the disk and
 * updates the super blocks to record it.  When this call is done,
 * you know that any inodes previously logged are safely on disk only
 * if it returns 0.
 *
 * Any other return value means you need to call btrfs_commit_transaction.
 * Some of the edge cases for fsyncing directories that have had unlinks
 * or renames done in the past mean that sometimes the only safe
 * fsync is to commit the whole FS.  When btrfs_sync_log returns -EAGAIN,
 * that has happened.
 */
int btrfs_sync_log(struct btrfs_trans_handle *trans,
		   struct btrfs_root *root, struct btrfs_log_ctx *ctx)
{
	int index1;
	int index2;
	int mark;
	int ret;
	struct btrfs_root *log = root->log_root;
	struct btrfs_root *log_root_tree = root->fs_info->log_root_tree;
	int log_transid = 0;
	struct btrfs_log_ctx root_log_ctx;
	struct blk_plug plug;

	mutex_lock(&root->log_mutex);
	log_transid = ctx->log_transid;
	if (root->log_transid_committed >= log_transid) {
		mutex_unlock(&root->log_mutex);
		return ctx->log_ret;
	}

	index1 = log_transid % 2;
	if (atomic_read(&root->log_commit[index1])) {
		wait_log_commit(root, log_transid);
		mutex_unlock(&root->log_mutex);
		return ctx->log_ret;
	}
	ASSERT(log_transid == root->log_transid);
	atomic_set(&root->log_commit[index1], 1);

	/* wait for previous tree log sync to complete */
	if (atomic_read(&root->log_commit[(index1 + 1) % 2]))
		wait_log_commit(root, log_transid - 1);

	while (1) {
		int batch = atomic_read(&root->log_batch);
		/* when we're on an ssd, just kick the log commit out */
		if (!btrfs_test_opt(root->fs_info, SSD) &&
		    test_bit(BTRFS_ROOT_MULTI_LOG_TASKS, &root->state)) {
			mutex_unlock(&root->log_mutex);
			schedule_timeout_uninterruptible(1);
			mutex_lock(&root->log_mutex);
		}
		wait_for_writer(root);
		if (batch == atomic_read(&root->log_batch))
			break;
	}

	/* bail out if we need to do a full commit */
	if (btrfs_need_log_full_commit(root->fs_info, trans)) {
		ret = -EAGAIN;
		btrfs_free_logged_extents(log, log_transid);
		mutex_unlock(&root->log_mutex);
		goto out;
	}

	if (log_transid % 2 == 0)
		mark = EXTENT_DIRTY;
	else
		mark = EXTENT_NEW;

	/* we start IO on  all the marked extents here, but we don't actually
	 * wait for them until later.
	 */
	blk_start_plug(&plug);
	ret = btrfs_write_marked_extents(log, &log->dirty_log_pages, mark);
	if (ret) {
		blk_finish_plug(&plug);
		btrfs_abort_transaction(trans, ret);
		btrfs_free_logged_extents(log, log_transid);
		btrfs_set_log_full_commit(root->fs_info, trans);
		mutex_unlock(&root->log_mutex);
		goto out;
	}

	btrfs_set_root_node(&log->root_item, log->node);

	root->log_transid++;
	log->log_transid = root->log_transid;
	root->log_start_pid = 0;
	/*
	 * IO has been started, blocks of the log tree have WRITTEN flag set
	 * in their headers. new modifications of the log will be written to
	 * new positions. so it's safe to allow log writers to go in.
	 */
	mutex_unlock(&root->log_mutex);

	btrfs_init_log_ctx(&root_log_ctx, NULL);

	mutex_lock(&log_root_tree->log_mutex);
	atomic_inc(&log_root_tree->log_batch);
	atomic_inc(&log_root_tree->log_writers);

	index2 = log_root_tree->log_transid % 2;
	list_add_tail(&root_log_ctx.list, &log_root_tree->log_ctxs[index2]);
	root_log_ctx.log_transid = log_root_tree->log_transid;

	mutex_unlock(&log_root_tree->log_mutex);

	ret = update_log_root(trans, log);

	mutex_lock(&log_root_tree->log_mutex);
	if (atomic_dec_and_test(&log_root_tree->log_writers)) {
		/*
		 * Implicit memory barrier after atomic_dec_and_test
		 */
		if (waitqueue_active(&log_root_tree->log_writer_wait))
			wake_up(&log_root_tree->log_writer_wait);
	}

	if (ret) {
		if (!list_empty(&root_log_ctx.list))
			list_del_init(&root_log_ctx.list);

		blk_finish_plug(&plug);
		btrfs_set_log_full_commit(root->fs_info, trans);

		if (ret != -ENOSPC) {
			btrfs_abort_transaction(trans, ret);
			mutex_unlock(&log_root_tree->log_mutex);
			goto out;
		}
		btrfs_wait_marked_extents(log, &log->dirty_log_pages, mark);
		btrfs_free_logged_extents(log, log_transid);
		mutex_unlock(&log_root_tree->log_mutex);
		ret = -EAGAIN;
		goto out;
	}

	if (log_root_tree->log_transid_committed >= root_log_ctx.log_transid) {
		blk_finish_plug(&plug);
		list_del_init(&root_log_ctx.list);
		mutex_unlock(&log_root_tree->log_mutex);
		ret = root_log_ctx.log_ret;
		goto out;
	}

	index2 = root_log_ctx.log_transid % 2;
	if (atomic_read(&log_root_tree->log_commit[index2])) {
		blk_finish_plug(&plug);
		ret = btrfs_wait_marked_extents(log, &log->dirty_log_pages,
						mark);
		btrfs_wait_logged_extents(trans, log, log_transid);
		wait_log_commit(log_root_tree,
				root_log_ctx.log_transid);
		mutex_unlock(&log_root_tree->log_mutex);
		if (!ret)
			ret = root_log_ctx.log_ret;
		goto out;
	}
	ASSERT(root_log_ctx.log_transid == log_root_tree->log_transid);
	atomic_set(&log_root_tree->log_commit[index2], 1);

	if (atomic_read(&log_root_tree->log_commit[(index2 + 1) % 2])) {
		wait_log_commit(log_root_tree,
				root_log_ctx.log_transid - 1);
	}

	wait_for_writer(log_root_tree);

	/*
	 * now that we've moved on to the tree of log tree roots,
	 * check the full commit flag again
	 */
	if (btrfs_need_log_full_commit(root->fs_info, trans)) {
		blk_finish_plug(&plug);
		btrfs_wait_marked_extents(log, &log->dirty_log_pages, mark);
		btrfs_free_logged_extents(log, log_transid);
		mutex_unlock(&log_root_tree->log_mutex);
		ret = -EAGAIN;
		goto out_wake_log_root;
	}

	ret = btrfs_write_marked_extents(log_root_tree,
					 &log_root_tree->dirty_log_pages,
					 EXTENT_DIRTY | EXTENT_NEW);
	blk_finish_plug(&plug);
	if (ret) {
		btrfs_set_log_full_commit(root->fs_info, trans);
		btrfs_abort_transaction(trans, ret);
		btrfs_free_logged_extents(log, log_transid);
		mutex_unlock(&log_root_tree->log_mutex);
		goto out_wake_log_root;
	}
	ret = btrfs_wait_marked_extents(log, &log->dirty_log_pages, mark);
	if (!ret)
		ret = btrfs_wait_marked_extents(log_root_tree,
						&log_root_tree->dirty_log_pages,
						EXTENT_NEW | EXTENT_DIRTY);
	if (ret) {
		btrfs_set_log_full_commit(root->fs_info, trans);
		btrfs_free_logged_extents(log, log_transid);
		mutex_unlock(&log_root_tree->log_mutex);
		goto out_wake_log_root;
	}
	btrfs_wait_logged_extents(trans, log, log_transid);

	btrfs_set_super_log_root(root->fs_info->super_for_commit,
				log_root_tree->node->start);
	btrfs_set_super_log_root_level(root->fs_info->super_for_commit,
				btrfs_header_level(log_root_tree->node));

	log_root_tree->log_transid++;
	mutex_unlock(&log_root_tree->log_mutex);

	/*
	 * nobody else is going to jump in and write the the ctree
	 * super here because the log_commit atomic below is protecting
	 * us.  We must be called with a transaction handle pinning
	 * the running transaction open, so a full commit can't hop
	 * in and cause problems either.
	 */
	ret = write_ctree_super(trans, root->fs_info->tree_root, 1);
	if (ret) {
		btrfs_set_log_full_commit(root->fs_info, trans);
		btrfs_abort_transaction(trans, ret);
		goto out_wake_log_root;
	}

	mutex_lock(&root->log_mutex);
	if (root->last_log_commit < log_transid)
		root->last_log_commit = log_transid;
	mutex_unlock(&root->log_mutex);

out_wake_log_root:
	mutex_lock(&log_root_tree->log_mutex);
	btrfs_remove_all_log_ctxs(log_root_tree, index2, ret);

	log_root_tree->log_transid_committed++;
	atomic_set(&log_root_tree->log_commit[index2], 0);
	mutex_unlock(&log_root_tree->log_mutex);

	/*
	 * The barrier before waitqueue_active is implied by mutex_unlock
	 */
	if (waitqueue_active(&log_root_tree->log_commit_wait[index2]))
		wake_up(&log_root_tree->log_commit_wait[index2]);
out:
	mutex_lock(&root->log_mutex);
	btrfs_remove_all_log_ctxs(root, index1, ret);
	root->log_transid_committed++;
	atomic_set(&root->log_commit[index1], 0);
	mutex_unlock(&root->log_mutex);

	/*
	 * The barrier before waitqueue_active is implied by mutex_unlock
	 */
	if (waitqueue_active(&root->log_commit_wait[index1]))
		wake_up(&root->log_commit_wait[index1]);
	return ret;
}

static void free_log_tree(struct btrfs_trans_handle *trans,
			  struct btrfs_root *log)
{
	int ret;
	u64 start;
	u64 end;
	struct walk_control wc = {
		.free = 1,
		.process_func = process_one_buffer
	};

	ret = walk_log_tree(trans, log, &wc);
	/* I don't think this can happen but just in case */
	if (ret)
		btrfs_abort_transaction(trans, ret);

	while (1) {
		ret = find_first_extent_bit(&log->dirty_log_pages,
				0, &start, &end, EXTENT_DIRTY | EXTENT_NEW,
				NULL);
		if (ret)
			break;

		clear_extent_bits(&log->dirty_log_pages, start, end,
				  EXTENT_DIRTY | EXTENT_NEW);
	}

	/*
	 * We may have short-circuited the log tree with the full commit logic
	 * and left ordered extents on our list, so clear these out to keep us
	 * from leaking inodes and memory.
	 */
	btrfs_free_logged_extents(log, 0);
	btrfs_free_logged_extents(log, 1);

	free_extent_buffer(log->node);
	kfree(log);
}

/*
 * free all the extents used by the tree log.  This should be called
 * at commit time of the full transaction
 */
int btrfs_free_log(struct btrfs_trans_handle *trans, struct btrfs_root *root)
{
	if (root->log_root) {
		free_log_tree(trans, root->log_root);
		root->log_root = NULL;
	}
	return 0;
}

int btrfs_free_log_root_tree(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info)
{
	if (fs_info->log_root_tree) {
		free_log_tree(trans, fs_info->log_root_tree);
		fs_info->log_root_tree = NULL;
	}
	return 0;
}

/*
 * If both a file and directory are logged, and unlinks or renames are
 * mixed in, we have a few interesting corners:
 *
 * create file X in dir Y
 * link file X to X.link in dir Y
 * fsync file X
 * unlink file X but leave X.link
 * fsync dir Y
 *
 * After a crash we would expect only X.link to exist.  But file X
 * didn't get fsync'd again so the log has back refs for X and X.link.
 *
 * We solve this by removing directory entries and inode backrefs from the
 * log when a file that was logged in the current transaction is
 * unlinked.  Any later fsync will include the updated log entries, and
 * we'll be able to reconstruct the proper directory items from backrefs.
 *
 * This optimizations allows us to avoid relogging the entire inode
 * or the entire directory.
 */
int btrfs_del_dir_entries_in_log(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 const char *name, int name_len,
				 struct inode *dir, u64 index)
{
	struct btrfs_root *log;
	struct btrfs_dir_item *di;
	struct btrfs_path *path;
	int ret;
	int err = 0;
	int bytes_del = 0;
	u64 dir_ino = btrfs_ino(dir);

	if (BTRFS_I(dir)->logged_trans < trans->transid)
		return 0;

	ret = join_running_log_trans(root);
	if (ret)
		return 0;

	mutex_lock(&BTRFS_I(dir)->log_mutex);

	log = root->log_root;
	path = btrfs_alloc_path();
	if (!path) {
		err = -ENOMEM;
		goto out_unlock;
	}

	di = btrfs_lookup_dir_item(trans, log, path, dir_ino,
				   name, name_len, -1);
	if (IS_ERR(di)) {
		err = PTR_ERR(di);
		goto fail;
	}
	if (di) {
		ret = btrfs_delete_one_dir_name(trans, log, path, di);
		bytes_del += name_len;
		if (ret) {
			err = ret;
			goto fail;
		}
	}
	btrfs_release_path(path);
	di = btrfs_lookup_dir_index_item(trans, log, path, dir_ino,
					 index, name, name_len, -1);
	if (IS_ERR(di)) {
		err = PTR_ERR(di);
		goto fail;
	}
	if (di) {
		ret = btrfs_delete_one_dir_name(trans, log, path, di);
		bytes_del += name_len;
		if (ret) {
			err = ret;
			goto fail;
		}
	}

	/* update the directory size in the log to reflect the names
	 * we have removed
	 */
	if (bytes_del) {
		struct btrfs_key key;

		key.objectid = dir_ino;
		key.offset = 0;
		key.type = BTRFS_INODE_ITEM_KEY;
		btrfs_release_path(path);

		ret = btrfs_search_slot(trans, log, &key, path, 0, 1);
		if (ret < 0) {
			err = ret;
			goto fail;
		}
		if (ret == 0) {
			struct btrfs_inode_item *item;
			u64 i_size;

			item = btrfs_item_ptr(path->nodes[0], path->slots[0],
					      struct btrfs_inode_item);
			i_size = btrfs_inode_size(path->nodes[0], item);
			if (i_size > bytes_del)
				i_size -= bytes_del;
			else
				i_size = 0;
			btrfs_set_inode_size(path->nodes[0], item, i_size);
			btrfs_mark_buffer_dirty(path->nodes[0]);
		} else
			ret = 0;
		btrfs_release_path(path);
	}
fail:
	btrfs_free_path(path);
out_unlock:
	mutex_unlock(&BTRFS_I(dir)->log_mutex);
	if (ret == -ENOSPC) {
		btrfs_set_log_full_commit(root->fs_info, trans);
		ret = 0;
	} else if (ret < 0)
		btrfs_abort_transaction(trans, ret);

	btrfs_end_log_trans(root);

	return err;
}

/* see comments for btrfs_del_dir_entries_in_log */
int btrfs_del_inode_ref_in_log(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       const char *name, int name_len,
			       struct inode *inode, u64 dirid)
{
	struct btrfs_root *log;
	u64 index;
	int ret;

	if (BTRFS_I(inode)->logged_trans < trans->transid)
		return 0;

	ret = join_running_log_trans(root);
	if (ret)
		return 0;
	log = root->log_root;
	mutex_lock(&BTRFS_I(inode)->log_mutex);

	ret = btrfs_del_inode_ref(trans, log, name, name_len, btrfs_ino(inode),
				  dirid, &index);
	mutex_unlock(&BTRFS_I(inode)->log_mutex);
	if (ret == -ENOSPC) {
		btrfs_set_log_full_commit(root->fs_info, trans);
		ret = 0;
	} else if (ret < 0 && ret != -ENOENT)
		btrfs_abort_transaction(trans, ret);
	btrfs_end_log_trans(root);

	return ret;
}

/*
 * creates a range item in the log for 'dirid'.  first_offset and
 * last_offset tell us which parts of the key space the log should
 * be considered authoritative for.
 */
static noinline int insert_dir_log_key(struct btrfs_trans_handle *trans,
				       struct btrfs_root *log,
				       struct btrfs_path *path,
				       int key_type, u64 dirid,
				       u64 first_offset, u64 last_offset)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_dir_log_item *item;

	key.objectid = dirid;
	key.offset = first_offset;
	if (key_type == BTRFS_DIR_ITEM_KEY)
		key.type = BTRFS_DIR_LOG_ITEM_KEY;
	else
		key.type = BTRFS_DIR_LOG_INDEX_KEY;
	ret = btrfs_insert_empty_item(trans, log, path, &key, sizeof(*item));
	if (ret)
		return ret;

	item = btrfs_item_ptr(path->nodes[0], path->slots[0],
			      struct btrfs_dir_log_item);
	btrfs_set_dir_log_end(path->nodes[0], item, last_offset);
	btrfs_mark_buffer_dirty(path->nodes[0]);
	btrfs_release_path(path);
	return 0;
}

/*
 * log all the items included in the current transaction for a given
 * directory.  This also creates the range items in the log tree required
 * to replay anything deleted before the fsync
 */
static noinline int log_dir_items(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct inode *inode,
			  struct btrfs_path *path,
			  struct btrfs_path *dst_path, int key_type,
			  struct btrfs_log_ctx *ctx,
			  u64 min_offset, u64 *last_offset_ret)
{
	struct btrfs_key min_key;
	struct btrfs_root *log = root->log_root;
	struct extent_buffer *src;
	int err = 0;
	int ret;
	int i;
	int nritems;
	u64 first_offset = min_offset;
	u64 last_offset = (u64)-1;
	u64 ino = btrfs_ino(inode);

	log = root->log_root;

	min_key.objectid = ino;
	min_key.type = key_type;
	min_key.offset = min_offset;

	ret = btrfs_search_forward(root, &min_key, path, trans->transid);

	/*
	 * we didn't find anything from this transaction, see if there
	 * is anything at all
	 */
	if (ret != 0 || min_key.objectid != ino || min_key.type != key_type) {
		min_key.objectid = ino;
		min_key.type = key_type;
		min_key.offset = (u64)-1;
		btrfs_release_path(path);
		ret = btrfs_search_slot(NULL, root, &min_key, path, 0, 0);
		if (ret < 0) {
			btrfs_release_path(path);
			return ret;
		}
		ret = btrfs_previous_item(root, path, ino, key_type);

		/* if ret == 0 there are items for this type,
		 * create a range to tell us the last key of this type.
		 * otherwise, there are no items in this directory after
		 * *min_offset, and we create a range to indicate that.
		 */
		if (ret == 0) {
			struct btrfs_key tmp;
			btrfs_item_key_to_cpu(path->nodes[0], &tmp,
					      path->slots[0]);
			if (key_type == tmp.type)
				first_offset = max(min_offset, tmp.offset) + 1;
		}
		goto done;
	}

	/* go backward to find any previous key */
	ret = btrfs_previous_item(root, path, ino, key_type);
	if (ret == 0) {
		struct btrfs_key tmp;
		btrfs_item_key_to_cpu(path->nodes[0], &tmp, path->slots[0]);
		if (key_type == tmp.type) {
			first_offset = tmp.offset;
			ret = overwrite_item(trans, log, dst_path,
					     path->nodes[0], path->slots[0],
					     &tmp);
			if (ret) {
				err = ret;
				goto done;
			}
		}
	}
	btrfs_release_path(path);

	/* find the first key from this transaction again */
	ret = btrfs_search_slot(NULL, root, &min_key, path, 0, 0);
	if (WARN_ON(ret != 0))
		goto done;

	/*
	 * we have a block from this transaction, log every item in it
	 * from our directory
	 */
	while (1) {
		struct btrfs_key tmp;
		src = path->nodes[0];
		nritems = btrfs_header_nritems(src);
		for (i = path->slots[0]; i < nritems; i++) {
			struct btrfs_dir_item *di;

			btrfs_item_key_to_cpu(src, &min_key, i);

			if (min_key.objectid != ino || min_key.type != key_type)
				goto done;
			ret = overwrite_item(trans, log, dst_path, src, i,
					     &min_key);
			if (ret) {
				err = ret;
				goto done;
			}

			/*
			 * We must make sure that when we log a directory entry,
			 * the corresponding inode, after log replay, has a
			 * matching link count. For example:
			 *
			 * touch foo
			 * mkdir mydir
			 * sync
			 * ln foo mydir/bar
			 * xfs_io -c "fsync" mydir
			 * <crash>
			 * <mount fs and log replay>
			 *
			 * Would result in a fsync log that when replayed, our
			 * file inode would have a link count of 1, but we get
			 * two directory entries pointing to the same inode.
			 * After removing one of the names, it would not be
			 * possible to remove the other name, which resulted
			 * always in stale file handle errors, and would not
			 * be possible to rmdir the parent directory, since
			 * its i_size could never decrement to the value
			 * BTRFS_EMPTY_DIR_SIZE, resulting in -ENOTEMPTY errors.
			 */
			di = btrfs_item_ptr(src, i, struct btrfs_dir_item);
			btrfs_dir_item_key_to_cpu(src, di, &tmp);
			if (ctx &&
			    (btrfs_dir_transid(src, di) == trans->transid ||
			     btrfs_dir_type(src, di) == BTRFS_FT_DIR) &&
			    tmp.type != BTRFS_ROOT_ITEM_KEY)
				ctx->log_new_dentries = true;
		}
		path->slots[0] = nritems;

		/*
		 * look ahead to the next item and see if it is also
		 * from this directory and from this transaction
		 */
		ret = btrfs_next_leaf(root, path);
		if (ret == 1) {
			last_offset = (u64)-1;
			goto done;
		}
		btrfs_item_key_to_cpu(path->nodes[0], &tmp, path->slots[0]);
		if (tmp.objectid != ino || tmp.type != key_type) {
			last_offset = (u64)-1;
			goto done;
		}
		if (btrfs_header_generation(path->nodes[0]) != trans->transid) {
			ret = overwrite_item(trans, log, dst_path,
					     path->nodes[0], path->slots[0],
					     &tmp);
			if (ret)
				err = ret;
			else
				last_offset = tmp.offset;
			goto done;
		}
	}
done:
	btrfs_release_path(path);
	btrfs_release_path(dst_path);

	if (err == 0) {
		*last_offset_ret = last_offset;
		/*
		 * insert the log range keys to indicate where the log
		 * is valid
		 */
		ret = insert_dir_log_key(trans, log, path, key_type,
					 ino, first_offset, last_offset);
		if (ret)
			err = ret;
	}
	return err;
}

/*
 * logging directories is very similar to logging inodes, We find all the items
 * from the current transaction and write them to the log.
 *
 * The recovery code scans the directory in the subvolume, and if it finds a
 * key in the range logged that is not present in the log tree, then it means
 * that dir entry was unlinked during the transaction.
 *
 * In order for that scan to work, we must include one key smaller than
 * the smallest logged by this transaction and one key larger than the largest
 * key logged by this transaction.
 */
static noinline int log_directory_changes(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct inode *inode,
			  struct btrfs_path *path,
			  struct btrfs_path *dst_path,
			  struct btrfs_log_ctx *ctx)
{
	u64 min_key;
	u64 max_key;
	int ret;
	int key_type = BTRFS_DIR_ITEM_KEY;

again:
	min_key = 0;
	max_key = 0;
	while (1) {
		ret = log_dir_items(trans, root, inode, path,
				    dst_path, key_type, ctx, min_key,
				    &max_key);
		if (ret)
			return ret;
		if (max_key == (u64)-1)
			break;
		min_key = max_key + 1;
	}

	if (key_type == BTRFS_DIR_ITEM_KEY) {
		key_type = BTRFS_DIR_INDEX_KEY;
		goto again;
	}
	return 0;
}

/*
 * a helper function to drop items from the log before we relog an
 * inode.  max_key_type indicates the highest item type to remove.
 * This cannot be run for file data extents because it does not
 * free the extents they point to.
 */
static int drop_objectid_items(struct btrfs_trans_handle *trans,
				  struct btrfs_root *log,
				  struct btrfs_path *path,
				  u64 objectid, int max_key_type)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_key found_key;
	int start_slot;

	key.objectid = objectid;
	key.type = max_key_type;
	key.offset = (u64)-1;

	while (1) {
		ret = btrfs_search_slot(trans, log, &key, path, -1, 1);
		BUG_ON(ret == 0); /* Logic error */
		if (ret < 0)
			break;

		if (path->slots[0] == 0)
			break;

		path->slots[0]--;
		btrfs_item_key_to_cpu(path->nodes[0], &found_key,
				      path->slots[0]);

		if (found_key.objectid != objectid)
			break;

		found_key.offset = 0;
		found_key.type = 0;
		ret = btrfs_bin_search(path->nodes[0], &found_key, 0,
				       &start_slot);

		ret = btrfs_del_items(trans, log, path, start_slot,
				      path->slots[0] - start_slot + 1);
		/*
		 * If start slot isn't 0 then we don't need to re-search, we've
		 * found the last guy with the objectid in this tree.
		 */
		if (ret || start_slot != 0)
			break;
		btrfs_release_path(path);
	}
	btrfs_release_path(path);
	if (ret > 0)
		ret = 0;
	return ret;
}

static void fill_inode_item(struct btrfs_trans_handle *trans,
			    struct extent_buffer *leaf,
			    struct btrfs_inode_item *item,
			    struct inode *inode, int log_inode_only,
			    u64 logged_isize)
{
	struct btrfs_map_token token;

	btrfs_init_map_token(&token);

	if (log_inode_only) {
		/* set the generation to zero so the recover code
		 * can tell the difference between an logging
		 * just to say 'this inode exists' and a logging
		 * to say 'update this inode with these values'
		 */
		btrfs_set_token_inode_generation(leaf, item, 0, &token);
		btrfs_set_token_inode_size(leaf, item, logged_isize, &token);
	} else {
		btrfs_set_token_inode_generation(leaf, item,
						 BTRFS_I(inode)->generation,
						 &token);
		btrfs_set_token_inode_size(leaf, item, inode->i_size, &token);
	}

	btrfs_set_token_inode_uid(leaf, item, i_uid_read(inode), &token);
	btrfs_set_token_inode_gid(leaf, item, i_gid_read(inode), &token);
	btrfs_set_token_inode_mode(leaf, item, inode->i_mode, &token);
	btrfs_set_token_inode_nlink(leaf, item, inode->i_nlink, &token);

	btrfs_set_token_timespec_sec(leaf, &item->atime,
				     inode->i_atime.tv_sec, &token);
	btrfs_set_token_timespec_nsec(leaf, &item->atime,
				      inode->i_atime.tv_nsec, &token);

	btrfs_set_token_timespec_sec(leaf, &item->mtime,
				     inode->i_mtime.tv_sec, &token);
	btrfs_set_token_timespec_nsec(leaf, &item->mtime,
				      inode->i_mtime.tv_nsec, &token);

	btrfs_set_token_timespec_sec(leaf, &item->ctime,
				     inode->i_ctime.tv_sec, &token);
	btrfs_set_token_timespec_nsec(leaf, &item->ctime,
				      inode->i_ctime.tv_nsec, &token);

	btrfs_set_token_inode_nbytes(leaf, item, inode_get_bytes(inode),
				     &token);

	btrfs_set_token_inode_sequence(leaf, item, inode->i_version, &token);
	btrfs_set_token_inode_transid(leaf, item, trans->transid, &token);
	btrfs_set_token_inode_rdev(leaf, item, inode->i_rdev, &token);
	btrfs_set_token_inode_flags(leaf, item, BTRFS_I(inode)->flags, &token);
	btrfs_set_token_inode_block_group(leaf, item, 0, &token);
}

static int log_inode_item(struct btrfs_trans_handle *trans,
			  struct btrfs_root *log, struct btrfs_path *path,
			  struct inode *inode)
{
	struct btrfs_inode_item *inode_item;
	int ret;

	ret = btrfs_insert_empty_item(trans, log, path,
				      &BTRFS_I(inode)->location,
				      sizeof(*inode_item));
	if (ret && ret != -EEXIST)
		return ret;
	inode_item = btrfs_item_ptr(path->nodes[0], path->slots[0],
				    struct btrfs_inode_item);
	fill_inode_item(trans, path->nodes[0], inode_item, inode, 0, 0);
	btrfs_release_path(path);
	return 0;
}

static noinline int copy_items(struct btrfs_trans_handle *trans,
			       struct inode *inode,
			       struct btrfs_path *dst_path,
			       struct btrfs_path *src_path, u64 *last_extent,
			       int start_slot, int nr, int inode_only,
			       u64 logged_isize)
{
	unsigned long src_offset;
	unsigned long dst_offset;
	struct btrfs_root *log = BTRFS_I(inode)->root->log_root;
	struct btrfs_file_extent_item *extent;
	struct btrfs_inode_item *inode_item;
	struct extent_buffer *src = src_path->nodes[0];
	struct btrfs_key first_key, last_key, key;
	int ret;
	struct btrfs_key *ins_keys;
	u32 *ins_sizes;
	char *ins_data;
	int i;
	struct list_head ordered_sums;
	int skip_csum = BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM;
	bool has_extents = false;
	bool need_find_last_extent = true;
	bool done = false;

	INIT_LIST_HEAD(&ordered_sums);

	ins_data = kmalloc(nr * sizeof(struct btrfs_key) +
			   nr * sizeof(u32), GFP_NOFS);
	if (!ins_data)
		return -ENOMEM;

	first_key.objectid = (u64)-1;

	ins_sizes = (u32 *)ins_data;
	ins_keys = (struct btrfs_key *)(ins_data + nr * sizeof(u32));

	for (i = 0; i < nr; i++) {
		ins_sizes[i] = btrfs_item_size_nr(src, i + start_slot);
		btrfs_item_key_to_cpu(src, ins_keys + i, i + start_slot);
	}
	ret = btrfs_insert_empty_items(trans, log, dst_path,
				       ins_keys, ins_sizes, nr);
	if (ret) {
		kfree(ins_data);
		return ret;
	}

	for (i = 0; i < nr; i++, dst_path->slots[0]++) {
		dst_offset = btrfs_item_ptr_offset(dst_path->nodes[0],
						   dst_path->slots[0]);

		src_offset = btrfs_item_ptr_offset(src, start_slot + i);

		if ((i == (nr - 1)))
			last_key = ins_keys[i];

		if (ins_keys[i].type == BTRFS_INODE_ITEM_KEY) {
			inode_item = btrfs_item_ptr(dst_path->nodes[0],
						    dst_path->slots[0],
						    struct btrfs_inode_item);
			fill_inode_item(trans, dst_path->nodes[0], inode_item,
					inode, inode_only == LOG_INODE_EXISTS,
					logged_isize);
		} else {
			copy_extent_buffer(dst_path->nodes[0], src, dst_offset,
					   src_offset, ins_sizes[i]);
		}

		/*
		 * We set need_find_last_extent here in case we know we were
		 * processing other items and then walk into the first extent in
		 * the inode.  If we don't hit an extent then nothing changes,
		 * we'll do the last search the next time around.
		 */
		if (ins_keys[i].type == BTRFS_EXTENT_DATA_KEY) {
			has_extents = true;
			if (first_key.objectid == (u64)-1)
				first_key = ins_keys[i];
		} else {
			need_find_last_extent = false;
		}

		/* take a reference on file data extents so that truncates
		 * or deletes of this inode don't have to relog the inode
		 * again
		 */
		if (ins_keys[i].type == BTRFS_EXTENT_DATA_KEY &&
		    !skip_csum) {
			int found_type;
			extent = btrfs_item_ptr(src, start_slot + i,
						struct btrfs_file_extent_item);

			if (btrfs_file_extent_generation(src, extent) < trans->transid)
				continue;

			found_type = btrfs_file_extent_type(src, extent);
			if (found_type == BTRFS_FILE_EXTENT_REG) {
				u64 ds, dl, cs, cl;
				ds = btrfs_file_extent_disk_bytenr(src,
								extent);
				/* ds == 0 is a hole */
				if (ds == 0)
					continue;

				dl = btrfs_file_extent_disk_num_bytes(src,
								extent);
				cs = btrfs_file_extent_offset(src, extent);
				cl = btrfs_file_extent_num_bytes(src,
								extent);
				if (btrfs_file_extent_compression(src,
								  extent)) {
					cs = 0;
					cl = dl;
				}

				ret = btrfs_lookup_csums_range(
						log->fs_info->csum_root,
						ds + cs, ds + cs + cl - 1,
						&ordered_sums, 0);
				if (ret) {
					btrfs_release_path(dst_path);
					kfree(ins_data);
					return ret;
				}
			}
		}
	}

	btrfs_mark_buffer_dirty(dst_path->nodes[0]);
	btrfs_release_path(dst_path);
	kfree(ins_data);

	/*
	 * we have to do this after the loop above to avoid changing the
	 * log tree while trying to change the log tree.
	 */
	ret = 0;
	while (!list_empty(&ordered_sums)) {
		struct btrfs_ordered_sum *sums = list_entry(ordered_sums.next,
						   struct btrfs_ordered_sum,
						   list);
		if (!ret)
			ret = btrfs_csum_file_blocks(trans, log, sums);
		list_del(&sums->list);
		kfree(sums);
	}

	if (!has_extents)
		return ret;

	if (need_find_last_extent && *last_extent == first_key.offset) {
		/*
		 * We don't have any leafs between our current one and the one
		 * we processed before that can have file extent items for our
		 * inode (and have a generation number smaller than our current
		 * transaction id).
		 */
		need_find_last_extent = false;
	}

	/*
	 * Because we use btrfs_search_forward we could skip leaves that were
	 * not modified and then assume *last_extent is valid when it really
	 * isn't.  So back up to the previous leaf and read the end of the last
	 * extent before we go and fill in holes.
	 */
	if (need_find_last_extent) {
		u64 len;

		ret = btrfs_prev_leaf(BTRFS_I(inode)->root, src_path);
		if (ret < 0)
			return ret;
		if (ret)
			goto fill_holes;
		if (src_path->slots[0])
			src_path->slots[0]--;
		src = src_path->nodes[0];
		btrfs_item_key_to_cpu(src, &key, src_path->slots[0]);
		if (key.objectid != btrfs_ino(inode) ||
		    key.type != BTRFS_EXTENT_DATA_KEY)
			goto fill_holes;
		extent = btrfs_item_ptr(src, src_path->slots[0],
					struct btrfs_file_extent_item);
		if (btrfs_file_extent_type(src, extent) ==
		    BTRFS_FILE_EXTENT_INLINE) {
			len = btrfs_file_extent_inline_len(src,
							   src_path->slots[0],
							   extent);
			*last_extent = ALIGN(key.offset + len,
					     log->sectorsize);
		} else {
			len = btrfs_file_extent_num_bytes(src, extent);
			*last_extent = key.offset + len;
		}
	}
fill_holes:
	/* So we did prev_leaf, now we need to move to the next leaf, but a few
	 * things could have happened
	 *
	 * 1) A merge could have happened, so we could currently be on a leaf
	 * that holds what we were copying in the first place.
	 * 2) A split could have happened, and now not all of the items we want
	 * are on the same leaf.
	 *
	 * So we need to adjust how we search for holes, we need to drop the
	 * path and re-search for the first extent key we found, and then walk
	 * forward until we hit the last one we copied.
	 */
	if (need_find_last_extent) {
		/* btrfs_prev_leaf could return 1 without releasing the path */
		btrfs_release_path(src_path);
		ret = btrfs_search_slot(NULL, BTRFS_I(inode)->root, &first_key,
					src_path, 0, 0);
		if (ret < 0)
			return ret;
		ASSERT(ret == 0);
		src = src_path->nodes[0];
		i = src_path->slots[0];
	} else {
		i = start_slot;
	}

	/*
	 * Ok so here we need to go through and fill in any holes we may have
	 * to make sure that holes are punched for those areas in case they had
	 * extents previously.
	 */
	while (!done) {
		u64 offset, len;
		u64 extent_end;

		if (i >= btrfs_header_nritems(src_path->nodes[0])) {
			ret = btrfs_next_leaf(BTRFS_I(inode)->root, src_path);
			if (ret < 0)
				return ret;
			ASSERT(ret == 0);
			src = src_path->nodes[0];
			i = 0;
		}

		btrfs_item_key_to_cpu(src, &key, i);
		if (!btrfs_comp_cpu_keys(&key, &last_key))
			done = true;
		if (key.objectid != btrfs_ino(inode) ||
		    key.type != BTRFS_EXTENT_DATA_KEY) {
			i++;
			continue;
		}
		extent = btrfs_item_ptr(src, i, struct btrfs_file_extent_item);
		if (btrfs_file_extent_type(src, extent) ==
		    BTRFS_FILE_EXTENT_INLINE) {
			len = btrfs_file_extent_inline_len(src, i, extent);
			extent_end = ALIGN(key.offset + len, log->sectorsize);
		} else {
			len = btrfs_file_extent_num_bytes(src, extent);
			extent_end = key.offset + len;
		}
		i++;

		if (*last_extent == key.offset) {
			*last_extent = extent_end;
			continue;
		}
		offset = *last_extent;
		len = key.offset - *last_extent;
		ret = btrfs_insert_file_extent(trans, log, btrfs_ino(inode),
					       offset, 0, 0, len, 0, len, 0,
					       0, 0);
		if (ret)
			break;
		*last_extent = extent_end;
	}
	/*
	 * Need to let the callers know we dropped the path so they should
	 * re-search.
	 */
	if (!ret && need_find_last_extent)
		ret = 1;
	return ret;
}

static int extent_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct extent_map *em1, *em2;

	em1 = list_entry(a, struct extent_map, list);
	em2 = list_entry(b, struct extent_map, list);

	if (em1->start < em2->start)
		return -1;
	else if (em1->start > em2->start)
		return 1;
	return 0;
}

static int wait_ordered_extents(struct btrfs_trans_handle *trans,
				struct inode *inode,
				struct btrfs_root *root,
				const struct extent_map *em,
				const struct list_head *logged_list,
				bool *ordered_io_error)
{
	struct btrfs_ordered_extent *ordered;
	struct btrfs_root *log = root->log_root;
	u64 mod_start = em->mod_start;
	u64 mod_len = em->mod_len;
	const bool skip_csum = BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM;
	u64 csum_offset;
	u64 csum_len;
	LIST_HEAD(ordered_sums);
	int ret = 0;

	*ordered_io_error = false;

	if (test_bit(EXTENT_FLAG_PREALLOC, &em->flags) ||
	    em->block_start == EXTENT_MAP_HOLE)
		return 0;

	/*
	 * Wait far any ordered extent that covers our extent map. If it
	 * finishes without an error, first check and see if our csums are on
	 * our outstanding ordered extents.
	 */
	list_for_each_entry(ordered, logged_list, log_list) {
		struct btrfs_ordered_sum *sum;

		if (!mod_len)
			break;

		if (ordered->file_offset + ordered->len <= mod_start ||
		    mod_start + mod_len <= ordered->file_offset)
			continue;

		if (!test_bit(BTRFS_ORDERED_IO_DONE, &ordered->flags) &&
		    !test_bit(BTRFS_ORDERED_IOERR, &ordered->flags) &&
		    !test_bit(BTRFS_ORDERED_DIRECT, &ordered->flags)) {
			const u64 start = ordered->file_offset;
			const u64 end = ordered->file_offset + ordered->len - 1;

			WARN_ON(ordered->inode != inode);
			filemap_fdatawrite_range(inode->i_mapping, start, end);
		}

		wait_event(ordered->wait,
			   (test_bit(BTRFS_ORDERED_IO_DONE, &ordered->flags) ||
			    test_bit(BTRFS_ORDERED_IOERR, &ordered->flags)));

		if (test_bit(BTRFS_ORDERED_IOERR, &ordered->flags)) {
			/*
			 * Clear the AS_EIO/AS_ENOSPC flags from the inode's
			 * i_mapping flags, so that the next fsync won't get
			 * an outdated io error too.
			 */
			filemap_check_errors(inode->i_mapping);
			*ordered_io_error = true;
			break;
		}
		/*
		 * We are going to copy all the csums on this ordered extent, so
		 * go ahead and adjust mod_start and mod_len in case this
		 * ordered extent has already been logged.
		 */
		if (ordered->file_offset > mod_start) {
			if (ordered->file_offset + ordered->len >=
			    mod_start + mod_len)
				mod_len = ordered->file_offset - mod_start;
			/*
			 * If we have this case
			 *
			 * |--------- logged extent ---------|
			 *       |----- ordered extent ----|
			 *
			 * Just don't mess with mod_start and mod_len, we'll
			 * just end up logging more csums than we need and it
			 * will be ok.
			 */
		} else {
			if (ordered->file_offset + ordered->len <
			    mod_start + mod_len) {
				mod_len = (mod_start + mod_len) -
					(ordered->file_offset + ordered->len);
				mod_start = ordered->file_offset +
					ordered->len;
			} else {
				mod_len = 0;
			}
		}

		if (skip_csum)
			continue;

		/*
		 * To keep us from looping for the above case of an ordered
		 * extent that falls inside of the logged extent.
		 */
		if (test_and_set_bit(BTRFS_ORDERED_LOGGED_CSUM,
				     &ordered->flags))
			continue;

		list_for_each_entry(sum, &ordered->list, list) {
			ret = btrfs_csum_file_blocks(trans, log, sum);
			if (ret)
				break;
		}
	}

	if (*ordered_io_error || !mod_len || ret || skip_csum)
		return ret;

	if (em->compress_type) {
		csum_offset = 0;
		csum_len = max(em->block_len, em->orig_block_len);
	} else {
		csum_offset = mod_start - em->start;
		csum_len = mod_len;
	}

	/* block start is already adjusted for the file extent offset. */
	ret = btrfs_lookup_csums_range(log->fs_info->csum_root,
				       em->block_start + csum_offset,
				       em->block_start + csum_offset +
				       csum_len - 1, &ordered_sums, 0);
	if (ret)
		return ret;

	while (!list_empty(&ordered_sums)) {
		struct btrfs_ordered_sum *sums = list_entry(ordered_sums.next,
						   struct btrfs_ordered_sum,
						   list);
		if (!ret)
			ret = btrfs_csum_file_blocks(trans, log, sums);
		list_del(&sums->list);
		kfree(sums);
	}

	return ret;
}

static int log_one_extent(struct btrfs_trans_handle *trans,
			  struct inode *inode, struct btrfs_root *root,
			  const struct extent_map *em,
			  struct btrfs_path *path,
			  const struct list_head *logged_list,
			  struct btrfs_log_ctx *ctx)
{
	struct btrfs_root *log = root->log_root;
	struct btrfs_file_extent_item *fi;
	struct extent_buffer *leaf;
	struct btrfs_map_token token;
	struct btrfs_key key;
	u64 extent_offset = em->start - em->orig_start;
	u64 block_len;
	int ret;
	int extent_inserted = 0;
	bool ordered_io_err = false;

	ret = wait_ordered_extents(trans, inode, root, em, logged_list,
				   &ordered_io_err);
	if (ret)
		return ret;

	if (ordered_io_err) {
		ctx->io_err = -EIO;
		return 0;
	}

	btrfs_init_map_token(&token);

	ret = __btrfs_drop_extents(trans, log, inode, path, em->start,
				   em->start + em->len, NULL, 0, 1,
				   sizeof(*fi), &extent_inserted);
	if (ret)
		return ret;

	if (!extent_inserted) {
		key.objectid = btrfs_ino(inode);
		key.type = BTRFS_EXTENT_DATA_KEY;
		key.offset = em->start;

		ret = btrfs_insert_empty_item(trans, log, path, &key,
					      sizeof(*fi));
		if (ret)
			return ret;
	}
	leaf = path->nodes[0];
	fi = btrfs_item_ptr(leaf, path->slots[0],
			    struct btrfs_file_extent_item);

	btrfs_set_token_file_extent_generation(leaf, fi, trans->transid,
					       &token);
	if (test_bit(EXTENT_FLAG_PREALLOC, &em->flags))
		btrfs_set_token_file_extent_type(leaf, fi,
						 BTRFS_FILE_EXTENT_PREALLOC,
						 &token);
	else
		btrfs_set_token_file_extent_type(leaf, fi,
						 BTRFS_FILE_EXTENT_REG,
						 &token);

	block_len = max(em->block_len, em->orig_block_len);
	if (em->compress_type != BTRFS_COMPRESS_NONE) {
		btrfs_set_token_file_extent_disk_bytenr(leaf, fi,
							em->block_start,
							&token);
		btrfs_set_token_file_extent_disk_num_bytes(leaf, fi, block_len,
							   &token);
	} else if (em->block_start < EXTENT_MAP_LAST_BYTE) {
		btrfs_set_token_file_extent_disk_bytenr(leaf, fi,
							em->block_start -
							extent_offset, &token);
		btrfs_set_token_file_extent_disk_num_bytes(leaf, fi, block_len,
							   &token);
	} else {
		btrfs_set_token_file_extent_disk_bytenr(leaf, fi, 0, &token);
		btrfs_set_token_file_extent_disk_num_bytes(leaf, fi, 0,
							   &token);
	}

	btrfs_set_token_file_extent_offset(leaf, fi, extent_offset, &token);
	btrfs_set_token_file_extent_num_bytes(leaf, fi, em->len, &token);
	btrfs_set_token_file_extent_ram_bytes(leaf, fi, em->ram_bytes, &token);
	btrfs_set_token_file_extent_compression(leaf, fi, em->compress_type,
						&token);
	btrfs_set_token_file_extent_encryption(leaf, fi, 0, &token);
	btrfs_set_token_file_extent_other_encoding(leaf, fi, 0, &token);
	btrfs_mark_buffer_dirty(leaf);

	btrfs_release_path(path);

	return ret;
}

static int btrfs_log_changed_extents(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct inode *inode,
				     struct btrfs_path *path,
				     struct list_head *logged_list,
				     struct btrfs_log_ctx *ctx,
				     const u64 start,
				     const u64 end)
{
	struct extent_map *em, *n;
	struct list_head extents;
	struct extent_map_tree *tree = &BTRFS_I(inode)->extent_tree;
	u64 test_gen;
	int ret = 0;
	int num = 0;

	INIT_LIST_HEAD(&extents);

	down_write(&BTRFS_I(inode)->dio_sem);
	write_lock(&tree->lock);
	test_gen = root->fs_info->last_trans_committed;

	list_for_each_entry_safe(em, n, &tree->modified_extents, list) {
		list_del_init(&em->list);

		/*
		 * Just an arbitrary number, this can be really CPU intensive
		 * once we start getting a lot of extents, and really once we
		 * have a bunch of extents we just want to commit since it will
		 * be faster.
		 */
		if (++num > 32768) {
			list_del_init(&tree->modified_extents);
			ret = -EFBIG;
			goto process;
		}

		if (em->generation <= test_gen)
			continue;
		/* Need a ref to keep it from getting evicted from cache */
		atomic_inc(&em->refs);
		set_bit(EXTENT_FLAG_LOGGING, &em->flags);
		list_add_tail(&em->list, &extents);
		num++;
	}

	list_sort(NULL, &extents, extent_cmp);
	btrfs_get_logged_extents(inode, logged_list, start, end);
	/*
	 * Some ordered extents started by fsync might have completed
	 * before we could collect them into the list logged_list, which
	 * means they're gone, not in our logged_list nor in the inode's
	 * ordered tree. We want the application/user space to know an
	 * error happened while attempting to persist file data so that
	 * it can take proper action. If such error happened, we leave
	 * without writing to the log tree and the fsync must report the
	 * file data write error and not commit the current transaction.
	 */
	ret = filemap_check_errors(inode->i_mapping);
	if (ret)
		ctx->io_err = ret;
process:
	while (!list_empty(&extents)) {
		em = list_entry(extents.next, struct extent_map, list);

		list_del_init(&em->list);

		/*
		 * If we had an error we just need to delete everybody from our
		 * private list.
		 */
		if (ret) {
			clear_em_logging(tree, em);
			free_extent_map(em);
			continue;
		}

		write_unlock(&tree->lock);

		ret = log_one_extent(trans, inode, root, em, path, logged_list,
				     ctx);
		write_lock(&tree->lock);
		clear_em_logging(tree, em);
		free_extent_map(em);
	}
	WARN_ON(!list_empty(&extents));
	write_unlock(&tree->lock);
	up_write(&BTRFS_I(inode)->dio_sem);

	btrfs_release_path(path);
	return ret;
}

static int logged_inode_size(struct btrfs_root *log, struct inode *inode,
			     struct btrfs_path *path, u64 *size_ret)
{
	struct btrfs_key key;
	int ret;

	key.objectid = btrfs_ino(inode);
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, log, &key, path, 0, 0);
	if (ret < 0) {
		return ret;
	} else if (ret > 0) {
		*size_ret = 0;
	} else {
		struct btrfs_inode_item *item;

		item = btrfs_item_ptr(path->nodes[0], path->slots[0],
				      struct btrfs_inode_item);
		*size_ret = btrfs_inode_size(path->nodes[0], item);
	}

	btrfs_release_path(path);
	return 0;
}

/*
 * At the moment we always log all xattrs. This is to figure out at log replay
 * time which xattrs must have their deletion replayed. If a xattr is missing
 * in the log tree and exists in the fs/subvol tree, we delete it. This is
 * because if a xattr is deleted, the inode is fsynced and a power failure
 * happens, causing the log to be replayed the next time the fs is mounted,
 * we want the xattr to not exist anymore (same behaviour as other filesystems
 * with a journal, ext3/4, xfs, f2fs, etc).
 */
static int btrfs_log_all_xattrs(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct inode *inode,
				struct btrfs_path *path,
				struct btrfs_path *dst_path)
{
	int ret;
	struct btrfs_key key;
	const u64 ino = btrfs_ino(inode);
	int ins_nr = 0;
	int start_slot = 0;

	key.objectid = ino;
	key.type = BTRFS_XATTR_ITEM_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		return ret;

	while (true) {
		int slot = path->slots[0];
		struct extent_buffer *leaf = path->nodes[0];
		int nritems = btrfs_header_nritems(leaf);

		if (slot >= nritems) {
			if (ins_nr > 0) {
				u64 last_extent = 0;

				ret = copy_items(trans, inode, dst_path, path,
						 &last_extent, start_slot,
						 ins_nr, 1, 0);
				/* can't be 1, extent items aren't processed */
				ASSERT(ret <= 0);
				if (ret < 0)
					return ret;
				ins_nr = 0;
			}
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				return ret;
			else if (ret > 0)
				break;
			continue;
		}

		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid != ino || key.type != BTRFS_XATTR_ITEM_KEY)
			break;

		if (ins_nr == 0)
			start_slot = slot;
		ins_nr++;
		path->slots[0]++;
		cond_resched();
	}
	if (ins_nr > 0) {
		u64 last_extent = 0;

		ret = copy_items(trans, inode, dst_path, path,
				 &last_extent, start_slot,
				 ins_nr, 1, 0);
		/* can't be 1, extent items aren't processed */
		ASSERT(ret <= 0);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * If the no holes feature is enabled we need to make sure any hole between the
 * last extent and the i_size of our inode is explicitly marked in the log. This
 * is to make sure that doing something like:
 *
 *      1) create file with 128Kb of data
 *      2) truncate file to 64Kb
 *      3) truncate file to 256Kb
 *      4) fsync file
 *      5) <crash/power failure>
 *      6) mount fs and trigger log replay
 *
 * Will give us a file with a size of 256Kb, the first 64Kb of data match what
 * the file had in its first 64Kb of data at step 1 and the last 192Kb of the
 * file correspond to a hole. The presence of explicit holes in a log tree is
 * what guarantees that log replay will remove/adjust file extent items in the
 * fs/subvol tree.
 *
 * Here we do not need to care about holes between extents, that is already done
 * by copy_items(). We also only need to do this in the full sync path, where we
 * lookup for extents from the fs/subvol tree only. In the fast path case, we
 * lookup the list of modified extent maps and if any represents a hole, we
 * insert a corresponding extent representing a hole in the log tree.
 */
static int btrfs_log_trailing_hole(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct inode *inode,
				   struct btrfs_path *path)
{
	int ret;
	struct btrfs_key key;
	u64 hole_start;
	u64 hole_size;
	struct extent_buffer *leaf;
	struct btrfs_root *log = root->log_root;
	const u64 ino = btrfs_ino(inode);
	const u64 i_size = i_size_read(inode);

	if (!btrfs_fs_incompat(root->fs_info, NO_HOLES))
		return 0;

	key.objectid = ino;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = (u64)-1;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	ASSERT(ret != 0);
	if (ret < 0)
		return ret;

	ASSERT(path->slots[0] > 0);
	path->slots[0]--;
	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

	if (key.objectid != ino || key.type != BTRFS_EXTENT_DATA_KEY) {
		/* inode does not have any extents */
		hole_start = 0;
		hole_size = i_size;
	} else {
		struct btrfs_file_extent_item *extent;
		u64 len;

		/*
		 * If there's an extent beyond i_size, an explicit hole was
		 * already inserted by copy_items().
		 */
		if (key.offset >= i_size)
			return 0;

		extent = btrfs_item_ptr(leaf, path->slots[0],
					struct btrfs_file_extent_item);

		if (btrfs_file_extent_type(leaf, extent) ==
		    BTRFS_FILE_EXTENT_INLINE) {
			len = btrfs_file_extent_inline_len(leaf,
							   path->slots[0],
							   extent);
			ASSERT(len == i_size);
			return 0;
		}

		len = btrfs_file_extent_num_bytes(leaf, extent);
		/* Last extent goes beyond i_size, no need to log a hole. */
		if (key.offset + len > i_size)
			return 0;
		hole_start = key.offset + len;
		hole_size = i_size - hole_start;
	}
	btrfs_release_path(path);

	/* Last extent ends at i_size. */
	if (hole_size == 0)
		return 0;

	hole_size = ALIGN(hole_size, root->sectorsize);
	ret = btrfs_insert_file_extent(trans, log, ino, hole_start, 0, 0,
				       hole_size, 0, hole_size, 0, 0, 0);
	return ret;
}

/*
 * When we are logging a new inode X, check if it doesn't have a reference that
 * matches the reference from some other inode Y created in a past transaction
 * and that was renamed in the current transaction. If we don't do this, then at
 * log replay time we can lose inode Y (and all its files if it's a directory):
 *
 * mkdir /mnt/x
 * echo "hello world" > /mnt/x/foobar
 * sync
 * mv /mnt/x /mnt/y
 * mkdir /mnt/x                 # or touch /mnt/x
 * xfs_io -c fsync /mnt/x
 * <power fail>
 * mount fs, trigger log replay
 *
 * After the log replay procedure, we would lose the first directory and all its
 * files (file foobar).
 * For the case where inode Y is not a directory we simply end up losing it:
 *
 * echo "123" > /mnt/foo
 * sync
 * mv /mnt/foo /mnt/bar
 * echo "abc" > /mnt/foo
 * xfs_io -c fsync /mnt/foo
 * <power fail>
 *
 * We also need this for cases where a snapshot entry is replaced by some other
 * entry (file or directory) otherwise we end up with an unreplayable log due to
 * attempts to delete the snapshot entry (entry of type BTRFS_ROOT_ITEM_KEY) as
 * if it were a regular entry:
 *
 * mkdir /mnt/x
 * btrfs subvolume snapshot /mnt /mnt/x/snap
 * btrfs subvolume delete /mnt/x/snap
 * rmdir /mnt/x
 * mkdir /mnt/x
 * fsync /mnt/x or fsync some new file inside it
 * <power fail>
 *
 * The snapshot delete, rmdir of x, mkdir of a new x and the fsync all happen in
 * the same transaction.
 */
static int btrfs_check_ref_name_override(struct extent_buffer *eb,
					 const int slot,
					 const struct btrfs_key *key,
					 struct inode *inode,
					 u64 *other_ino)
{
	int ret;
	struct btrfs_path *search_path;
	char *name = NULL;
	u32 name_len = 0;
	u32 item_size = btrfs_item_size_nr(eb, slot);
	u32 cur_offset = 0;
	unsigned long ptr = btrfs_item_ptr_offset(eb, slot);

	search_path = btrfs_alloc_path();
	if (!search_path)
		return -ENOMEM;
	search_path->search_commit_root = 1;
	search_path->skip_locking = 1;

	while (cur_offset < item_size) {
		u64 parent;
		u32 this_name_len;
		u32 this_len;
		unsigned long name_ptr;
		struct btrfs_dir_item *di;

		if (key->type == BTRFS_INODE_REF_KEY) {
			struct btrfs_inode_ref *iref;

			iref = (struct btrfs_inode_ref *)(ptr + cur_offset);
			parent = key->offset;
			this_name_len = btrfs_inode_ref_name_len(eb, iref);
			name_ptr = (unsigned long)(iref + 1);
			this_len = sizeof(*iref) + this_name_len;
		} else {
			struct btrfs_inode_extref *extref;

			extref = (struct btrfs_inode_extref *)(ptr +
							       cur_offset);
			parent = btrfs_inode_extref_parent(eb, extref);
			this_name_len = btrfs_inode_extref_name_len(eb, extref);
			name_ptr = (unsigned long)&extref->name;
			this_len = sizeof(*extref) + this_name_len;
		}

		if (this_name_len > name_len) {
			char *new_name;

			new_name = krealloc(name, this_name_len, GFP_NOFS);
			if (!new_name) {
				ret = -ENOMEM;
				goto out;
			}
			name_len = this_name_len;
			name = new_name;
		}

		read_extent_buffer(eb, name, name_ptr, this_name_len);
		di = btrfs_lookup_dir_item(NULL, BTRFS_I(inode)->root,
					   search_path, parent,
					   name, this_name_len, 0);
		if (di && !IS_ERR(di)) {
			struct btrfs_key di_key;

			btrfs_dir_item_key_to_cpu(search_path->nodes[0],
						  di, &di_key);
			if (di_key.type == BTRFS_INODE_ITEM_KEY) {
				ret = 1;
				*other_ino = di_key.objectid;
			} else {
				ret = -EAGAIN;
			}
			goto out;
		} else if (IS_ERR(di)) {
			ret = PTR_ERR(di);
			goto out;
		}
		btrfs_release_path(search_path);

		cur_offset += this_len;
	}
	ret = 0;
out:
	btrfs_free_path(search_path);
	kfree(name);
	return ret;
}

/* log a single inode in the tree log.
 * At least one parent directory for this inode must exist in the tree
 * or be logged already.
 *
 * Any items from this inode changed by the current transaction are copied
 * to the log tree.  An extra reference is taken on any extents in this
 * file, allowing us to avoid a whole pile of corner cases around logging
 * blocks that have been removed from the tree.
 *
 * See LOG_INODE_ALL and related defines for a description of what inode_only
 * does.
 *
 * This handles both files and directories.
 */
static int btrfs_log_inode(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, struct inode *inode,
			   int inode_only,
			   const loff_t start,
			   const loff_t end,
			   struct btrfs_log_ctx *ctx)
{
	struct btrfs_path *path;
	struct btrfs_path *dst_path;
	struct btrfs_key min_key;
	struct btrfs_key max_key;
	struct btrfs_root *log = root->log_root;
	struct extent_buffer *src = NULL;
	LIST_HEAD(logged_list);
	u64 last_extent = 0;
	int err = 0;
	int ret;
	int nritems;
	int ins_start_slot = 0;
	int ins_nr;
	bool fast_search = false;
	u64 ino = btrfs_ino(inode);
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	u64 logged_isize = 0;
	bool need_log_inode_item = true;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	dst_path = btrfs_alloc_path();
	if (!dst_path) {
		btrfs_free_path(path);
		return -ENOMEM;
	}

	min_key.objectid = ino;
	min_key.type = BTRFS_INODE_ITEM_KEY;
	min_key.offset = 0;

	max_key.objectid = ino;


	/* today the code can only do partial logging of directories */
	if (S_ISDIR(inode->i_mode) ||
	    (!test_bit(BTRFS_INODE_NEEDS_FULL_SYNC,
		       &BTRFS_I(inode)->runtime_flags) &&
	     inode_only == LOG_INODE_EXISTS))
		max_key.type = BTRFS_XATTR_ITEM_KEY;
	else
		max_key.type = (u8)-1;
	max_key.offset = (u64)-1;

	/*
	 * Only run delayed items if we are a dir or a new file.
	 * Otherwise commit the delayed inode only, which is needed in
	 * order for the log replay code to mark inodes for link count
	 * fixup (create temporary BTRFS_TREE_LOG_FIXUP_OBJECTID items).
	 */
	if (S_ISDIR(inode->i_mode) ||
	    BTRFS_I(inode)->generation > root->fs_info->last_trans_committed)
		ret = btrfs_commit_inode_delayed_items(trans, inode);
	else
		ret = btrfs_commit_inode_delayed_inode(inode);

	if (ret) {
		btrfs_free_path(path);
		btrfs_free_path(dst_path);
		return ret;
	}

	mutex_lock(&BTRFS_I(inode)->log_mutex);

	/*
	 * a brute force approach to making sure we get the most uptodate
	 * copies of everything.
	 */
	if (S_ISDIR(inode->i_mode)) {
		int max_key_type = BTRFS_DIR_LOG_INDEX_KEY;

		if (inode_only == LOG_INODE_EXISTS)
			max_key_type = BTRFS_XATTR_ITEM_KEY;
		ret = drop_objectid_items(trans, log, path, ino, max_key_type);
	} else {
		if (inode_only == LOG_INODE_EXISTS) {
			/*
			 * Make sure the new inode item we write to the log has
			 * the same isize as the current one (if it exists).
			 * This is necessary to prevent data loss after log
			 * replay, and also to prevent doing a wrong expanding
			 * truncate - for e.g. create file, write 4K into offset
			 * 0, fsync, write 4K into offset 4096, add hard link,
			 * fsync some other file (to sync log), power fail - if
			 * we use the inode's current i_size, after log replay
			 * we get a 8Kb file, with the last 4Kb extent as a hole
			 * (zeroes), as if an expanding truncate happened,
			 * instead of getting a file of 4Kb only.
			 */
			err = logged_inode_size(log, inode, path,
						&logged_isize);
			if (err)
				goto out_unlock;
		}
		if (test_bit(BTRFS_INODE_NEEDS_FULL_SYNC,
			     &BTRFS_I(inode)->runtime_flags)) {
			if (inode_only == LOG_INODE_EXISTS) {
				max_key.type = BTRFS_XATTR_ITEM_KEY;
				ret = drop_objectid_items(trans, log, path, ino,
							  max_key.type);
			} else {
				clear_bit(BTRFS_INODE_NEEDS_FULL_SYNC,
					  &BTRFS_I(inode)->runtime_flags);
				clear_bit(BTRFS_INODE_COPY_EVERYTHING,
					  &BTRFS_I(inode)->runtime_flags);
				while(1) {
					ret = btrfs_truncate_inode_items(trans,
							 log, inode, 0, 0);
					if (ret != -EAGAIN)
						break;
				}
			}
		} else if (test_and_clear_bit(BTRFS_INODE_COPY_EVERYTHING,
					      &BTRFS_I(inode)->runtime_flags) ||
			   inode_only == LOG_INODE_EXISTS) {
			if (inode_only == LOG_INODE_ALL)
				fast_search = true;
			max_key.type = BTRFS_XATTR_ITEM_KEY;
			ret = drop_objectid_items(trans, log, path, ino,
						  max_key.type);
		} else {
			if (inode_only == LOG_INODE_ALL)
				fast_search = true;
			goto log_extents;
		}

	}
	if (ret) {
		err = ret;
		goto out_unlock;
	}

	while (1) {
		ins_nr = 0;
		ret = btrfs_search_forward(root, &min_key,
					   path, trans->transid);
		if (ret < 0) {
			err = ret;
			goto out_unlock;
		}
		if (ret != 0)
			break;
again:
		/* note, ins_nr might be > 0 here, cleanup outside the loop */
		if (min_key.objectid != ino)
			break;
		if (min_key.type > max_key.type)
			break;

		if (min_key.type == BTRFS_INODE_ITEM_KEY)
			need_log_inode_item = false;

		if ((min_key.type == BTRFS_INODE_REF_KEY ||
		     min_key.type == BTRFS_INODE_EXTREF_KEY) &&
		    BTRFS_I(inode)->generation == trans->transid) {
			u64 other_ino = 0;

			ret = btrfs_check_ref_name_override(path->nodes[0],
							    path->slots[0],
							    &min_key, inode,
							    &other_ino);
			if (ret < 0) {
				err = ret;
				goto out_unlock;
			} else if (ret > 0 && ctx &&
				   other_ino != btrfs_ino(ctx->inode)) {
				struct btrfs_key inode_key;
				struct inode *other_inode;

				if (ins_nr > 0) {
					ins_nr++;
				} else {
					ins_nr = 1;
					ins_start_slot = path->slots[0];
				}
				ret = copy_items(trans, inode, dst_path, path,
						 &last_extent, ins_start_slot,
						 ins_nr, inode_only,
						 logged_isize);
				if (ret < 0) {
					err = ret;
					goto out_unlock;
				}
				ins_nr = 0;
				btrfs_release_path(path);
				inode_key.objectid = other_ino;
				inode_key.type = BTRFS_INODE_ITEM_KEY;
				inode_key.offset = 0;
				other_inode = btrfs_iget(root->fs_info->sb,
							 &inode_key, root,
							 NULL);
				/*
				 * If the other inode that had a conflicting dir
				 * entry was deleted in the current transaction,
				 * we don't need to do more work nor fallback to
				 * a transaction commit.
				 */
				if (IS_ERR(other_inode) &&
				    PTR_ERR(other_inode) == -ENOENT) {
					goto next_key;
				} else if (IS_ERR(other_inode)) {
					err = PTR_ERR(other_inode);
					goto out_unlock;
				}
				/*
				 * We are safe logging the other inode without
				 * acquiring its i_mutex as long as we log with
				 * the LOG_INODE_EXISTS mode. We're safe against
				 * concurrent renames of the other inode as well
				 * because during a rename we pin the log and
				 * update the log with the new name before we
				 * unpin it.
				 */
				err = btrfs_log_inode(trans, root, other_inode,
						      LOG_INODE_EXISTS,
						      0, LLONG_MAX, ctx);
				iput(other_inode);
				if (err)
					goto out_unlock;
				else
					goto next_key;
			}
		}

		/* Skip xattrs, we log them later with btrfs_log_all_xattrs() */
		if (min_key.type == BTRFS_XATTR_ITEM_KEY) {
			if (ins_nr == 0)
				goto next_slot;
			ret = copy_items(trans, inode, dst_path, path,
					 &last_extent, ins_start_slot,
					 ins_nr, inode_only, logged_isize);
			if (ret < 0) {
				err = ret;
				goto out_unlock;
			}
			ins_nr = 0;
			if (ret) {
				btrfs_release_path(path);
				continue;
			}
			goto next_slot;
		}

		src = path->nodes[0];
		if (ins_nr && ins_start_slot + ins_nr == path->slots[0]) {
			ins_nr++;
			goto next_slot;
		} else if (!ins_nr) {
			ins_start_slot = path->slots[0];
			ins_nr = 1;
			goto next_slot;
		}

		ret = copy_items(trans, inode, dst_path, path, &last_extent,
				 ins_start_slot, ins_nr, inode_only,
				 logged_isize);
		if (ret < 0) {
			err = ret;
			goto out_unlock;
		}
		if (ret) {
			ins_nr = 0;
			btrfs_release_path(path);
			continue;
		}
		ins_nr = 1;
		ins_start_slot = path->slots[0];
next_slot:

		nritems = btrfs_header_nritems(path->nodes[0]);
		path->slots[0]++;
		if (path->slots[0] < nritems) {
			btrfs_item_key_to_cpu(path->nodes[0], &min_key,
					      path->slots[0]);
			goto again;
		}
		if (ins_nr) {
			ret = copy_items(trans, inode, dst_path, path,
					 &last_extent, ins_start_slot,
					 ins_nr, inode_only, logged_isize);
			if (ret < 0) {
				err = ret;
				goto out_unlock;
			}
			ret = 0;
			ins_nr = 0;
		}
		btrfs_release_path(path);
next_key:
		if (min_key.offset < (u64)-1) {
			min_key.offset++;
		} else if (min_key.type < max_key.type) {
			min_key.type++;
			min_key.offset = 0;
		} else {
			break;
		}
	}
	if (ins_nr) {
		ret = copy_items(trans, inode, dst_path, path, &last_extent,
				 ins_start_slot, ins_nr, inode_only,
				 logged_isize);
		if (ret < 0) {
			err = ret;
			goto out_unlock;
		}
		ret = 0;
		ins_nr = 0;
	}

	btrfs_release_path(path);
	btrfs_release_path(dst_path);
	err = btrfs_log_all_xattrs(trans, root, inode, path, dst_path);
	if (err)
		goto out_unlock;
	if (max_key.type >= BTRFS_EXTENT_DATA_KEY && !fast_search) {
		btrfs_release_path(path);
		btrfs_release_path(dst_path);
		err = btrfs_log_trailing_hole(trans, root, inode, path);
		if (err)
			goto out_unlock;
	}
log_extents:
	btrfs_release_path(path);
	btrfs_release_path(dst_path);
	if (need_log_inode_item) {
		err = log_inode_item(trans, log, dst_path, inode);
		if (err)
			goto out_unlock;
	}
	if (fast_search) {
		ret = btrfs_log_changed_extents(trans, root, inode, dst_path,
						&logged_list, ctx, start, end);
		if (ret) {
			err = ret;
			goto out_unlock;
		}
	} else if (inode_only == LOG_INODE_ALL) {
		struct extent_map *em, *n;

		write_lock(&em_tree->lock);
		/*
		 * We can't just remove every em if we're called for a ranged
		 * fsync - that is, one that doesn't cover the whole possible
		 * file range (0 to LLONG_MAX). This is because we can have
		 * em's that fall outside the range we're logging and therefore
		 * their ordered operations haven't completed yet
		 * (btrfs_finish_ordered_io() not invoked yet). This means we
		 * didn't get their respective file extent item in the fs/subvol
		 * tree yet, and need to let the next fast fsync (one which
		 * consults the list of modified extent maps) find the em so
		 * that it logs a matching file extent item and waits for the
		 * respective ordered operation to complete (if it's still
		 * running).
		 *
		 * Removing every em outside the range we're logging would make
		 * the next fast fsync not log their matching file extent items,
		 * therefore making us lose data after a log replay.
		 */
		list_for_each_entry_safe(em, n, &em_tree->modified_extents,
					 list) {
			const u64 mod_end = em->mod_start + em->mod_len - 1;

			if (em->mod_start >= start && mod_end <= end)
				list_del_init(&em->list);
		}
		write_unlock(&em_tree->lock);
	}

	if (inode_only == LOG_INODE_ALL && S_ISDIR(inode->i_mode)) {
		ret = log_directory_changes(trans, root, inode, path, dst_path,
					    ctx);
		if (ret) {
			err = ret;
			goto out_unlock;
		}
	}

	spin_lock(&BTRFS_I(inode)->lock);
	BTRFS_I(inode)->logged_trans = trans->transid;
	BTRFS_I(inode)->last_log_commit = BTRFS_I(inode)->last_sub_trans;
	spin_unlock(&BTRFS_I(inode)->lock);
out_unlock:
	if (unlikely(err))
		btrfs_put_logged_extents(&logged_list);
	else
		btrfs_submit_logged_extents(&logged_list, log);
	mutex_unlock(&BTRFS_I(inode)->log_mutex);

	btrfs_free_path(path);
	btrfs_free_path(dst_path);
	return err;
}

/*
 * Check if we must fallback to a transaction commit when logging an inode.
 * This must be called after logging the inode and is used only in the context
 * when fsyncing an inode requires the need to log some other inode - in which
 * case we can't lock the i_mutex of each other inode we need to log as that
 * can lead to deadlocks with concurrent fsync against other inodes (as we can
 * log inodes up or down in the hierarchy) or rename operations for example. So
 * we take the log_mutex of the inode after we have logged it and then check for
 * its last_unlink_trans value - this is safe because any task setting
 * last_unlink_trans must take the log_mutex and it must do this before it does
 * the actual unlink operation, so if we do this check before a concurrent task
 * sets last_unlink_trans it means we've logged a consistent version/state of
 * all the inode items, otherwise we are not sure and must do a transaction
 * commit (the concurrent task might have only updated last_unlink_trans before
 * we logged the inode or it might have also done the unlink).
 */
static bool btrfs_must_commit_transaction(struct btrfs_trans_handle *trans,
					  struct inode *inode)
{
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	bool ret = false;

	mutex_lock(&BTRFS_I(inode)->log_mutex);
	if (BTRFS_I(inode)->last_unlink_trans > fs_info->last_trans_committed) {
		/*
		 * Make sure any commits to the log are forced to be full
		 * commits.
		 */
		btrfs_set_log_full_commit(fs_info, trans);
		ret = true;
	}
	mutex_unlock(&BTRFS_I(inode)->log_mutex);

	return ret;
}

/*
 * follow the dentry parent pointers up the chain and see if any
 * of the directories in it require a full commit before they can
 * be logged.  Returns zero if nothing special needs to be done or 1 if
 * a full commit is required.
 */
static noinline int check_parent_dirs_for_sync(struct btrfs_trans_handle *trans,
					       struct inode *inode,
					       struct dentry *parent,
					       struct super_block *sb,
					       u64 last_committed)
{
	int ret = 0;
	struct dentry *old_parent = NULL;
	struct inode *orig_inode = inode;

	/*
	 * for regular files, if its inode is already on disk, we don't
	 * have to worry about the parents at all.  This is because
	 * we can use the last_unlink_trans field to record renames
	 * and other fun in this file.
	 */
	if (S_ISREG(inode->i_mode) &&
	    BTRFS_I(inode)->generation <= last_committed &&
	    BTRFS_I(inode)->last_unlink_trans <= last_committed)
			goto out;

	if (!S_ISDIR(inode->i_mode)) {
		if (!parent || d_really_is_negative(parent) || sb != parent->d_sb)
			goto out;
		inode = d_inode(parent);
	}

	while (1) {
		/*
		 * If we are logging a directory then we start with our inode,
		 * not our parent's inode, so we need to skip setting the
		 * logged_trans so that further down in the log code we don't
		 * think this inode has already been logged.
		 */
		if (inode != orig_inode)
			BTRFS_I(inode)->logged_trans = trans->transid;
		smp_mb();

		if (btrfs_must_commit_transaction(trans, inode)) {
			ret = 1;
			break;
		}

		if (!parent || d_really_is_negative(parent) || sb != parent->d_sb)
			break;

		if (IS_ROOT(parent)) {
			inode = d_inode(parent);
			if (btrfs_must_commit_transaction(trans, inode))
				ret = 1;
			break;
		}

		parent = dget_parent(parent);
		dput(old_parent);
		old_parent = parent;
		inode = d_inode(parent);

	}
	dput(old_parent);
out:
	return ret;
}

struct btrfs_dir_list {
	u64 ino;
	struct list_head list;
};

/*
 * Log the inodes of the new dentries of a directory. See log_dir_items() for
 * details about the why it is needed.
 * This is a recursive operation - if an existing dentry corresponds to a
 * directory, that directory's new entries are logged too (same behaviour as
 * ext3/4, xfs, f2fs, reiserfs, nilfs2). Note that when logging the inodes
 * the dentries point to we do not lock their i_mutex, otherwise lockdep
 * complains about the following circular lock dependency / possible deadlock:
 *
 *        CPU0                                        CPU1
 *        ----                                        ----
 * lock(&type->i_mutex_dir_key#3/2);
 *                                            lock(sb_internal#2);
 *                                            lock(&type->i_mutex_dir_key#3/2);
 * lock(&sb->s_type->i_mutex_key#14);
 *
 * Where sb_internal is the lock (a counter that works as a lock) acquired by
 * sb_start_intwrite() in btrfs_start_transaction().
 * Not locking i_mutex of the inodes is still safe because:
 *
 * 1) For regular files we log with a mode of LOG_INODE_EXISTS. It's possible
 *    that while logging the inode new references (names) are added or removed
 *    from the inode, leaving the logged inode item with a link count that does
 *    not match the number of logged inode reference items. This is fine because
 *    at log replay time we compute the real number of links and correct the
 *    link count in the inode item (see replay_one_buffer() and
 *    link_to_fixup_dir());
 *
 * 2) For directories we log with a mode of LOG_INODE_ALL. It's possible that
 *    while logging the inode's items new items with keys BTRFS_DIR_ITEM_KEY and
 *    BTRFS_DIR_INDEX_KEY are added to fs/subvol tree and the logged inode item
 *    has a size that doesn't match the sum of the lengths of all the logged
 *    names. This does not result in a problem because if a dir_item key is
 *    logged but its matching dir_index key is not logged, at log replay time we
 *    don't use it to replay the respective name (see replay_one_name()). On the
 *    other hand if only the dir_index key ends up being logged, the respective
 *    name is added to the fs/subvol tree with both the dir_item and dir_index
 *    keys created (see replay_one_name()).
 *    The directory's inode item with a wrong i_size is not a problem as well,
 *    since we don't use it at log replay time to set the i_size in the inode
 *    item of the fs/subvol tree (see overwrite_item()).
 */
static int log_new_dir_dentries(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct inode *start_inode,
				struct btrfs_log_ctx *ctx)
{
	struct btrfs_root *log = root->log_root;
	struct btrfs_path *path;
	LIST_HEAD(dir_list);
	struct btrfs_dir_list *dir_elem;
	int ret = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	dir_elem = kmalloc(sizeof(*dir_elem), GFP_NOFS);
	if (!dir_elem) {
		btrfs_free_path(path);
		return -ENOMEM;
	}
	dir_elem->ino = btrfs_ino(start_inode);
	list_add_tail(&dir_elem->list, &dir_list);

	while (!list_empty(&dir_list)) {
		struct extent_buffer *leaf;
		struct btrfs_key min_key;
		int nritems;
		int i;

		dir_elem = list_first_entry(&dir_list, struct btrfs_dir_list,
					    list);
		if (ret)
			goto next_dir_inode;

		min_key.objectid = dir_elem->ino;
		min_key.type = BTRFS_DIR_ITEM_KEY;
		min_key.offset = 0;
again:
		btrfs_release_path(path);
		ret = btrfs_search_forward(log, &min_key, path, trans->transid);
		if (ret < 0) {
			goto next_dir_inode;
		} else if (ret > 0) {
			ret = 0;
			goto next_dir_inode;
		}

process_leaf:
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		for (i = path->slots[0]; i < nritems; i++) {
			struct btrfs_dir_item *di;
			struct btrfs_key di_key;
			struct inode *di_inode;
			struct btrfs_dir_list *new_dir_elem;
			int log_mode = LOG_INODE_EXISTS;
			int type;

			btrfs_item_key_to_cpu(leaf, &min_key, i);
			if (min_key.objectid != dir_elem->ino ||
			    min_key.type != BTRFS_DIR_ITEM_KEY)
				goto next_dir_inode;

			di = btrfs_item_ptr(leaf, i, struct btrfs_dir_item);
			type = btrfs_dir_type(leaf, di);
			if (btrfs_dir_transid(leaf, di) < trans->transid &&
			    type != BTRFS_FT_DIR)
				continue;
			btrfs_dir_item_key_to_cpu(leaf, di, &di_key);
			if (di_key.type == BTRFS_ROOT_ITEM_KEY)
				continue;

			di_inode = btrfs_iget(root->fs_info->sb, &di_key,
					      root, NULL);
			if (IS_ERR(di_inode)) {
				ret = PTR_ERR(di_inode);
				goto next_dir_inode;
			}

			if (btrfs_inode_in_log(di_inode, trans->transid)) {
				iput(di_inode);
				continue;
			}

			ctx->log_new_dentries = false;
			if (type == BTRFS_FT_DIR || type == BTRFS_FT_SYMLINK)
				log_mode = LOG_INODE_ALL;
			btrfs_release_path(path);
			ret = btrfs_log_inode(trans, root, di_inode,
					      log_mode, 0, LLONG_MAX, ctx);
			if (!ret &&
			    btrfs_must_commit_transaction(trans, di_inode))
				ret = 1;
			iput(di_inode);
			if (ret)
				goto next_dir_inode;
			if (ctx->log_new_dentries) {
				new_dir_elem = kmalloc(sizeof(*new_dir_elem),
						       GFP_NOFS);
				if (!new_dir_elem) {
					ret = -ENOMEM;
					goto next_dir_inode;
				}
				new_dir_elem->ino = di_key.objectid;
				list_add_tail(&new_dir_elem->list, &dir_list);
			}
			break;
		}
		if (i == nritems) {
			ret = btrfs_next_leaf(log, path);
			if (ret < 0) {
				goto next_dir_inode;
			} else if (ret > 0) {
				ret = 0;
				goto next_dir_inode;
			}
			goto process_leaf;
		}
		if (min_key.offset < (u64)-1) {
			min_key.offset++;
			goto again;
		}
next_dir_inode:
		list_del(&dir_elem->list);
		kfree(dir_elem);
	}

	btrfs_free_path(path);
	return ret;
}

static int btrfs_log_all_parents(struct btrfs_trans_handle *trans,
				 struct inode *inode,
				 struct btrfs_log_ctx *ctx)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	const u64 ino = btrfs_ino(inode);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->skip_locking = 1;
	path->search_commit_root = 1;

	key.objectid = ino;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	while (true) {
		struct extent_buffer *leaf = path->nodes[0];
		int slot = path->slots[0];
		u32 cur_offset = 0;
		u32 item_size;
		unsigned long ptr;

		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			else if (ret > 0)
				break;
			continue;
		}

		btrfs_item_key_to_cpu(leaf, &key, slot);
		/* BTRFS_INODE_EXTREF_KEY is BTRFS_INODE_REF_KEY + 1 */
		if (key.objectid != ino || key.type > BTRFS_INODE_EXTREF_KEY)
			break;

		item_size = btrfs_item_size_nr(leaf, slot);
		ptr = btrfs_item_ptr_offset(leaf, slot);
		while (cur_offset < item_size) {
			struct btrfs_key inode_key;
			struct inode *dir_inode;

			inode_key.type = BTRFS_INODE_ITEM_KEY;
			inode_key.offset = 0;

			if (key.type == BTRFS_INODE_EXTREF_KEY) {
				struct btrfs_inode_extref *extref;

				extref = (struct btrfs_inode_extref *)
					(ptr + cur_offset);
				inode_key.objectid = btrfs_inode_extref_parent(
					leaf, extref);
				cur_offset += sizeof(*extref);
				cur_offset += btrfs_inode_extref_name_len(leaf,
					extref);
			} else {
				inode_key.objectid = key.offset;
				cur_offset = item_size;
			}

			dir_inode = btrfs_iget(root->fs_info->sb, &inode_key,
					       root, NULL);
			/* If parent inode was deleted, skip it. */
			if (IS_ERR(dir_inode))
				continue;

			if (ctx)
				ctx->log_new_dentries = false;
			ret = btrfs_log_inode(trans, root, dir_inode,
					      LOG_INODE_ALL, 0, LLONG_MAX, ctx);
			if (!ret &&
			    btrfs_must_commit_transaction(trans, dir_inode))
				ret = 1;
			if (!ret && ctx && ctx->log_new_dentries)
				ret = log_new_dir_dentries(trans, root,
							   dir_inode, ctx);
			iput(dir_inode);
			if (ret)
				goto out;
		}
		path->slots[0]++;
	}
	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * helper function around btrfs_log_inode to make sure newly created
 * parent directories also end up in the log.  A minimal inode and backref
 * only logging is done of any parent directories that are older than
 * the last committed transaction
 */
static int btrfs_log_inode_parent(struct btrfs_trans_handle *trans,
			    	  struct btrfs_root *root, struct inode *inode,
				  struct dentry *parent,
				  const loff_t start,
				  const loff_t end,
				  int exists_only,
				  struct btrfs_log_ctx *ctx)
{
	int inode_only = exists_only ? LOG_INODE_EXISTS : LOG_INODE_ALL;
	struct super_block *sb;
	struct dentry *old_parent = NULL;
	int ret = 0;
	u64 last_committed = root->fs_info->last_trans_committed;
	bool log_dentries = false;
	struct inode *orig_inode = inode;

	sb = inode->i_sb;

	if (btrfs_test_opt(root->fs_info, NOTREELOG)) {
		ret = 1;
		goto end_no_trans;
	}

	/*
	 * The prev transaction commit doesn't complete, we need do
	 * full commit by ourselves.
	 */
	if (root->fs_info->last_trans_log_full_commit >
	    root->fs_info->last_trans_committed) {
		ret = 1;
		goto end_no_trans;
	}

	if (root != BTRFS_I(inode)->root ||
	    btrfs_root_refs(&root->root_item) == 0) {
		ret = 1;
		goto end_no_trans;
	}

	ret = check_parent_dirs_for_sync(trans, inode, parent,
					 sb, last_committed);
	if (ret)
		goto end_no_trans;

	if (btrfs_inode_in_log(inode, trans->transid)) {
		ret = BTRFS_NO_LOG_SYNC;
		goto end_no_trans;
	}

	ret = start_log_trans(trans, root, ctx);
	if (ret)
		goto end_no_trans;

	ret = btrfs_log_inode(trans, root, inode, inode_only, start, end, ctx);
	if (ret)
		goto end_trans;

	/*
	 * for regular files, if its inode is already on disk, we don't
	 * have to worry about the parents at all.  This is because
	 * we can use the last_unlink_trans field to record renames
	 * and other fun in this file.
	 */
	if (S_ISREG(inode->i_mode) &&
	    BTRFS_I(inode)->generation <= last_committed &&
	    BTRFS_I(inode)->last_unlink_trans <= last_committed) {
		ret = 0;
		goto end_trans;
	}

	if (S_ISDIR(inode->i_mode) && ctx && ctx->log_new_dentries)
		log_dentries = true;

	/*
	 * On unlink we must make sure all our current and old parent directory
	 * inodes are fully logged. This is to prevent leaving dangling
	 * directory index entries in directories that were our parents but are
	 * not anymore. Not doing this results in old parent directory being
	 * impossible to delete after log replay (rmdir will always fail with
	 * error -ENOTEMPTY).
	 *
	 * Example 1:
	 *
	 * mkdir testdir
	 * touch testdir/foo
	 * ln testdir/foo testdir/bar
	 * sync
	 * unlink testdir/bar
	 * xfs_io -c fsync testdir/foo
	 * <power failure>
	 * mount fs, triggers log replay
	 *
	 * If we don't log the parent directory (testdir), after log replay the
	 * directory still has an entry pointing to the file inode using the bar
	 * name, but a matching BTRFS_INODE_[REF|EXTREF]_KEY does not exist and
	 * the file inode has a link count of 1.
	 *
	 * Example 2:
	 *
	 * mkdir testdir
	 * touch foo
	 * ln foo testdir/foo2
	 * ln foo testdir/foo3
	 * sync
	 * unlink testdir/foo3
	 * xfs_io -c fsync foo
	 * <power failure>
	 * mount fs, triggers log replay
	 *
	 * Similar as the first example, after log replay the parent directory
	 * testdir still has an entry pointing to the inode file with name foo3
	 * but the file inode does not have a matching BTRFS_INODE_REF_KEY item
	 * and has a link count of 2.
	 */
	if (BTRFS_I(inode)->last_unlink_trans > last_committed) {
		ret = btrfs_log_all_parents(trans, orig_inode, ctx);
		if (ret)
			goto end_trans;
	}

	while (1) {
		if (!parent || d_really_is_negative(parent) || sb != parent->d_sb)
			break;

		inode = d_inode(parent);
		if (root != BTRFS_I(inode)->root)
			break;

		if (BTRFS_I(inode)->generation > last_committed) {
			ret = btrfs_log_inode(trans, root, inode,
					      LOG_INODE_EXISTS,
					      0, LLONG_MAX, ctx);
			if (ret)
				goto end_trans;
		}
		if (IS_ROOT(parent))
			break;

		parent = dget_parent(parent);
		dput(old_parent);
		old_parent = parent;
	}
	if (log_dentries)
		ret = log_new_dir_dentries(trans, root, orig_inode, ctx);
	else
		ret = 0;
end_trans:
	dput(old_parent);
	if (ret < 0) {
		btrfs_set_log_full_commit(root->fs_info, trans);
		ret = 1;
	}

	if (ret)
		btrfs_remove_log_ctx(root, ctx);
	btrfs_end_log_trans(root);
end_no_trans:
	return ret;
}

/*
 * it is not safe to log dentry if the chunk root has added new
 * chunks.  This returns 0 if the dentry was logged, and 1 otherwise.
 * If this returns 1, you must commit the transaction to safely get your
 * data on disk.
 */
int btrfs_log_dentry_safe(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct dentry *dentry,
			  const loff_t start,
			  const loff_t end,
			  struct btrfs_log_ctx *ctx)
{
	struct dentry *parent = dget_parent(dentry);
	int ret;

	ret = btrfs_log_inode_parent(trans, root, d_inode(dentry), parent,
				     start, end, 0, ctx);
	dput(parent);

	return ret;
}

/*
 * should be called during mount to recover any replay any log trees
 * from the FS
 */
int btrfs_recover_log_trees(struct btrfs_root *log_root_tree)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_trans_handle *trans;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_key tmp_key;
	struct btrfs_root *log;
	struct btrfs_fs_info *fs_info = log_root_tree->fs_info;
	struct walk_control wc = {
		.process_func = process_one_buffer,
		.stage = 0,
	};

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	set_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags);

	trans = btrfs_start_transaction(fs_info->tree_root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto error;
	}

	wc.trans = trans;
	wc.pin = 1;

	ret = walk_log_tree(trans, log_root_tree, &wc);
	if (ret) {
		btrfs_handle_fs_error(fs_info, ret,
			"Failed to pin buffers while recovering log root tree.");
		goto error;
	}

again:
	key.objectid = BTRFS_TREE_LOG_OBJECTID;
	key.offset = (u64)-1;
	key.type = BTRFS_ROOT_ITEM_KEY;

	while (1) {
		ret = btrfs_search_slot(NULL, log_root_tree, &key, path, 0, 0);

		if (ret < 0) {
			btrfs_handle_fs_error(fs_info, ret,
				    "Couldn't find tree log root.");
			goto error;
		}
		if (ret > 0) {
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		}
		btrfs_item_key_to_cpu(path->nodes[0], &found_key,
				      path->slots[0]);
		btrfs_release_path(path);
		if (found_key.objectid != BTRFS_TREE_LOG_OBJECTID)
			break;

		log = btrfs_read_fs_root(log_root_tree, &found_key);
		if (IS_ERR(log)) {
			ret = PTR_ERR(log);
			btrfs_handle_fs_error(fs_info, ret,
				    "Couldn't read tree log root.");
			goto error;
		}

		tmp_key.objectid = found_key.offset;
		tmp_key.type = BTRFS_ROOT_ITEM_KEY;
		tmp_key.offset = (u64)-1;

		wc.replay_dest = btrfs_read_fs_root_no_name(fs_info, &tmp_key);
		if (IS_ERR(wc.replay_dest)) {
			ret = PTR_ERR(wc.replay_dest);
			free_extent_buffer(log->node);
			free_extent_buffer(log->commit_root);
			kfree(log);
			btrfs_handle_fs_error(fs_info, ret,
				"Couldn't read target root for tree log recovery.");
			goto error;
		}

		wc.replay_dest->log_root = log;
		btrfs_record_root_in_trans(trans, wc.replay_dest);
		ret = walk_log_tree(trans, log, &wc);

		if (!ret && wc.stage == LOG_WALK_REPLAY_ALL) {
			ret = fixup_inode_link_counts(trans, wc.replay_dest,
						      path);
		}

		key.offset = found_key.offset - 1;
		wc.replay_dest->log_root = NULL;
		free_extent_buffer(log->node);
		free_extent_buffer(log->commit_root);
		kfree(log);

		if (ret)
			goto error;

		if (found_key.offset == 0)
			break;
	}
	btrfs_release_path(path);

	/* step one is to pin it all, step two is to replay just inodes */
	if (wc.pin) {
		wc.pin = 0;
		wc.process_func = replay_one_buffer;
		wc.stage = LOG_WALK_REPLAY_INODES;
		goto again;
	}
	/* step three is to replay everything */
	if (wc.stage < LOG_WALK_REPLAY_ALL) {
		wc.stage++;
		goto again;
	}

	btrfs_free_path(path);

	/* step 4: commit the transaction, which also unpins the blocks */
	ret = btrfs_commit_transaction(trans, fs_info->tree_root);
	if (ret)
		return ret;

	free_extent_buffer(log_root_tree->node);
	log_root_tree->log_root = NULL;
	clear_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags);
	kfree(log_root_tree);

	return 0;
error:
	if (wc.trans)
		btrfs_end_transaction(wc.trans, fs_info->tree_root);
	btrfs_free_path(path);
	return ret;
}

/*
 * there are some corner cases where we want to force a full
 * commit instead of allowing a directory to be logged.
 *
 * They revolve around files there were unlinked from the directory, and
 * this function updates the parent directory so that a full commit is
 * properly done if it is fsync'd later after the unlinks are done.
 *
 * Must be called before the unlink operations (updates to the subvolume tree,
 * inodes, etc) are done.
 */
void btrfs_record_unlink_dir(struct btrfs_trans_handle *trans,
			     struct inode *dir, struct inode *inode,
			     int for_rename)
{
	/*
	 * when we're logging a file, if it hasn't been renamed
	 * or unlinked, and its inode is fully committed on disk,
	 * we don't have to worry about walking up the directory chain
	 * to log its parents.
	 *
	 * So, we use the last_unlink_trans field to put this transid
	 * into the file.  When the file is logged we check it and
	 * don't log the parents if the file is fully on disk.
	 */
	mutex_lock(&BTRFS_I(inode)->log_mutex);
	BTRFS_I(inode)->last_unlink_trans = trans->transid;
	mutex_unlock(&BTRFS_I(inode)->log_mutex);

	/*
	 * if this directory was already logged any new
	 * names for this file/dir will get recorded
	 */
	smp_mb();
	if (BTRFS_I(dir)->logged_trans == trans->transid)
		return;

	/*
	 * if the inode we're about to unlink was logged,
	 * the log will be properly updated for any new names
	 */
	if (BTRFS_I(inode)->logged_trans == trans->transid)
		return;

	/*
	 * when renaming files across directories, if the directory
	 * there we're unlinking from gets fsync'd later on, there's
	 * no way to find the destination directory later and fsync it
	 * properly.  So, we have to be conservative and force commits
	 * so the new name gets discovered.
	 */
	if (for_rename)
		goto record;

	/* we can safely do the unlink without any special recording */
	return;

record:
	mutex_lock(&BTRFS_I(dir)->log_mutex);
	BTRFS_I(dir)->last_unlink_trans = trans->transid;
	mutex_unlock(&BTRFS_I(dir)->log_mutex);
}

/*
 * Make sure that if someone attempts to fsync the parent directory of a deleted
 * snapshot, it ends up triggering a transaction commit. This is to guarantee
 * that after replaying the log tree of the parent directory's root we will not
 * see the snapshot anymore and at log replay time we will not see any log tree
 * corresponding to the deleted snapshot's root, which could lead to replaying
 * it after replaying the log tree of the parent directory (which would replay
 * the snapshot delete operation).
 *
 * Must be called before the actual snapshot destroy operation (updates to the
 * parent root and tree of tree roots trees, etc) are done.
 */
void btrfs_record_snapshot_destroy(struct btrfs_trans_handle *trans,
				   struct inode *dir)
{
	mutex_lock(&BTRFS_I(dir)->log_mutex);
	BTRFS_I(dir)->last_unlink_trans = trans->transid;
	mutex_unlock(&BTRFS_I(dir)->log_mutex);
}

/*
 * Call this after adding a new name for a file and it will properly
 * update the log to reflect the new name.
 *
 * It will return zero if all goes well, and it will return 1 if a
 * full transaction commit is required.
 */
int btrfs_log_new_name(struct btrfs_trans_handle *trans,
			struct inode *inode, struct inode *old_dir,
			struct dentry *parent)
{
	struct btrfs_root * root = BTRFS_I(inode)->root;

	/*
	 * this will force the logging code to walk the dentry chain
	 * up for the file
	 */
	if (S_ISREG(inode->i_mode))
		BTRFS_I(inode)->last_unlink_trans = trans->transid;

	/*
	 * if this inode hasn't been logged and directory we're renaming it
	 * from hasn't been logged, we don't need to log it
	 */
	if (BTRFS_I(inode)->logged_trans <=
	    root->fs_info->last_trans_committed &&
	    (!old_dir || BTRFS_I(old_dir)->logged_trans <=
		    root->fs_info->last_trans_committed))
		return 0;

	return btrfs_log_inode_parent(trans, root, inode, parent, 0,
				      LLONG_MAX, 1, NULL);
}

