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
#include "ctree.h"
#include "transaction.h"
#include "disk-io.h"
#include "locking.h"
#include "print-tree.h"
#include "compat.h"

/* magic values for the inode_only field in btrfs_log_inode:
 *
 * LOG_INODE_ALL means to log everything
 * LOG_INODE_EXISTS means to log just enough to recreate the inode
 * during log replay
 */
#define LOG_INODE_ALL 0
#define LOG_INODE_EXISTS 1

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
#define LOG_WALK_REPLAY_ALL 2

static int __btrfs_log_inode(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, struct inode *inode,
			     int inode_only);

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
 * btrfs_add_log_tree adds a new per-subvolume log tree into the
 * tree of log tree roots.  This must be called with a tree log transaction
 * running (see start_log_trans).
 */
int btrfs_add_log_tree(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root)
{
	struct btrfs_key key;
	struct btrfs_root_item root_item;
	struct btrfs_inode_item *inode_item;
	struct extent_buffer *leaf;
	struct btrfs_root *new_root = root;
	int ret;
	u64 objectid = root->root_key.objectid;

	leaf = btrfs_alloc_free_block(trans, root, root->leafsize, 0,
				      BTRFS_TREE_LOG_OBJECTID,
				      trans->transid, 0, 0, 0);
	if (IS_ERR(leaf)) {
		ret = PTR_ERR(leaf);
		return ret;
	}

	btrfs_set_header_nritems(leaf, 0);
	btrfs_set_header_level(leaf, 0);
	btrfs_set_header_bytenr(leaf, leaf->start);
	btrfs_set_header_generation(leaf, trans->transid);
	btrfs_set_header_owner(leaf, BTRFS_TREE_LOG_OBJECTID);

	write_extent_buffer(leaf, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(leaf),
			    BTRFS_FSID_SIZE);
	btrfs_mark_buffer_dirty(leaf);

	inode_item = &root_item.inode;
	memset(inode_item, 0, sizeof(*inode_item));
	inode_item->generation = cpu_to_le64(1);
	inode_item->size = cpu_to_le64(3);
	inode_item->nlink = cpu_to_le32(1);
	inode_item->nbytes = cpu_to_le64(root->leafsize);
	inode_item->mode = cpu_to_le32(S_IFDIR | 0755);

	btrfs_set_root_bytenr(&root_item, leaf->start);
	btrfs_set_root_level(&root_item, 0);
	btrfs_set_root_refs(&root_item, 0);
	btrfs_set_root_used(&root_item, 0);

	memset(&root_item.drop_progress, 0, sizeof(root_item.drop_progress));
	root_item.drop_level = 0;

	btrfs_tree_unlock(leaf);
	free_extent_buffer(leaf);
	leaf = NULL;

	btrfs_set_root_dirid(&root_item, 0);

	key.objectid = BTRFS_TREE_LOG_OBJECTID;
	key.offset = objectid;
	btrfs_set_key_type(&key, BTRFS_ROOT_ITEM_KEY);
	ret = btrfs_insert_root(trans, root->fs_info->log_root_tree, &key,
				&root_item);
	if (ret)
		goto fail;

	new_root = btrfs_read_fs_root_no_radix(root->fs_info->log_root_tree,
					       &key);
	BUG_ON(!new_root);

	WARN_ON(root->log_root);
	root->log_root = new_root;

	/*
	 * log trees do not get reference counted because they go away
	 * before a real commit is actually done.  They do store pointers
	 * to file data extents, and those reference counts still get
	 * updated (along with back refs to the log tree).
	 */
	new_root->ref_cows = 0;
	new_root->last_trans = trans->transid;
fail:
	return ret;
}

/*
 * start a sub transaction and setup the log tree
 * this increments the log tree writer count to make the people
 * syncing the tree wait for us to finish
 */
static int start_log_trans(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root)
{
	int ret;
	mutex_lock(&root->fs_info->tree_log_mutex);
	if (!root->fs_info->log_root_tree) {
		ret = btrfs_init_log_root_tree(trans, root->fs_info);
		BUG_ON(ret);
	}
	if (!root->log_root) {
		ret = btrfs_add_log_tree(trans, root);
		BUG_ON(ret);
	}
	atomic_inc(&root->fs_info->tree_log_writers);
	root->fs_info->tree_log_batch++;
	mutex_unlock(&root->fs_info->tree_log_mutex);
	return 0;
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

	mutex_lock(&root->fs_info->tree_log_mutex);
	if (root->log_root) {
		ret = 0;
		atomic_inc(&root->fs_info->tree_log_writers);
		root->fs_info->tree_log_batch++;
	}
	mutex_unlock(&root->fs_info->tree_log_mutex);
	return ret;
}

/*
 * indicate we're done making changes to the log tree
 * and wake up anyone waiting to do a sync
 */
static int end_log_trans(struct btrfs_root *root)
{
	atomic_dec(&root->fs_info->tree_log_writers);
	smp_mb();
	if (waitqueue_active(&root->fs_info->tree_log_wait))
		wake_up(&root->fs_info->tree_log_wait);
	return 0;
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
	if (wc->pin) {
		mutex_lock(&log->fs_info->alloc_mutex);
		btrfs_update_pinned_extents(log->fs_info->extent_root,
					    eb->start, eb->len, 1);
		mutex_unlock(&log->fs_info->alloc_mutex);
	}

	if (btrfs_buffer_uptodate(eb, gen)) {
		if (wc->write)
			btrfs_write_tree_block(eb);
		if (wc->wait)
			btrfs_wait_tree_block_writeback(eb);
	}
	return 0;
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

	if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID)
		overwrite_root = 1;

	item_size = btrfs_item_size_nr(eb, slot);
	src_ptr = btrfs_item_ptr_offset(eb, slot);

	/* look for the key in the destination tree */
	ret = btrfs_search_slot(NULL, root, key, path, 0, 0);
	if (ret == 0) {
		char *src_copy;
		char *dst_copy;
		u32 dst_size = btrfs_item_size_nr(path->nodes[0],
						  path->slots[0]);
		if (dst_size != item_size)
			goto insert;

		if (item_size == 0) {
			btrfs_release_path(root, path);
			return 0;
		}
		dst_copy = kmalloc(item_size, GFP_NOFS);
		src_copy = kmalloc(item_size, GFP_NOFS);

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
			btrfs_release_path(root, path);
			return 0;
		}

	}
insert:
	btrfs_release_path(root, path);
	/* try to insert the key into the destination tree */
	ret = btrfs_insert_empty_item(trans, root, path,
				      key, item_size);

	/* make sure any existing item is the correct size */
	if (ret == -EEXIST) {
		u32 found_size;
		found_size = btrfs_item_size_nr(path->nodes[0],
						path->slots[0]);
		if (found_size > item_size) {
			btrfs_truncate_item(trans, root, path, item_size, 1);
		} else if (found_size < item_size) {
			ret = btrfs_del_item(trans, root,
					     path);
			BUG_ON(ret);

			btrfs_release_path(root, path);
			ret = btrfs_insert_empty_item(trans,
				  root, path, key, item_size);
			BUG_ON(ret);
		}
	} else if (ret) {
		BUG();
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

		if (btrfs_inode_generation(eb, src_item) == 0)
			goto no_copy;

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

	if (overwrite_root &&
	    key->type == BTRFS_EXTENT_DATA_KEY) {
		int extent_type;
		struct btrfs_file_extent_item *fi;

		fi = (struct btrfs_file_extent_item *)dst_ptr;
		extent_type = btrfs_file_extent_type(path->nodes[0], fi);
		if (extent_type == BTRFS_FILE_EXTENT_REG) {
			struct btrfs_key ins;
			ins.objectid = btrfs_file_extent_disk_bytenr(
							path->nodes[0], fi);
			ins.offset = btrfs_file_extent_disk_num_bytes(
							path->nodes[0], fi);
			ins.type = BTRFS_EXTENT_ITEM_KEY;

			/*
			 * is this extent already allocated in the extent
			 * allocation tree?  If so, just add a reference
			 */
			ret = btrfs_lookup_extent(root, ins.objectid,
						  ins.offset);
			if (ret == 0) {
				ret = btrfs_inc_extent_ref(trans, root,
						ins.objectid, ins.offset,
						path->nodes[0]->start,
						root->root_key.objectid,
						trans->transid,
						key->objectid, key->offset);
			} else {
				/*
				 * insert the extent pointer in the extent
				 * allocation tree
				 */
				ret = btrfs_alloc_logged_extent(trans, root,
						path->nodes[0]->start,
						root->root_key.objectid,
						trans->transid, key->objectid,
						key->offset, &ins);
				BUG_ON(ret);
			}
		}
	}
no_copy:
	btrfs_mark_buffer_dirty(path->nodes[0]);
	btrfs_release_path(root, path);
	return 0;
}

/*
 * simple helper to read an inode off the disk from a given root
 * This can only be called for subvolume roots and not for the log
 */
static noinline struct inode *read_one_inode(struct btrfs_root *root,
					     u64 objectid)
{
	struct inode *inode;
	inode = btrfs_iget_locked(root->fs_info->sb, objectid, root);
	if (inode->i_state & I_NEW) {
		BTRFS_I(inode)->root = root;
		BTRFS_I(inode)->location.objectid = objectid;
		BTRFS_I(inode)->location.type = BTRFS_INODE_ITEM_KEY;
		BTRFS_I(inode)->location.offset = 0;
		btrfs_read_locked_inode(inode);
		unlock_new_inode(inode);

	}
	if (is_bad_inode(inode)) {
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
	u64 mask = root->sectorsize - 1;
	u64 extent_end;
	u64 alloc_hint;
	u64 start = key->offset;
	struct btrfs_file_extent_item *item;
	struct inode *inode = NULL;
	unsigned long size;
	int ret = 0;

	item = btrfs_item_ptr(eb, slot, struct btrfs_file_extent_item);
	found_type = btrfs_file_extent_type(eb, item);

	if (found_type == BTRFS_FILE_EXTENT_REG)
		extent_end = start + btrfs_file_extent_num_bytes(eb, item);
	else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
		size = btrfs_file_extent_inline_len(eb,
						    btrfs_item_nr(eb, slot));
		extent_end = (start + size + mask) & ~mask;
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
	ret = btrfs_lookup_file_extent(trans, root, path, inode->i_ino,
				       start, 0);

	if (ret == 0 && found_type == BTRFS_FILE_EXTENT_REG) {
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
			btrfs_release_path(root, path);
			goto out;
		}
	}
	btrfs_release_path(root, path);

	/* drop any overlapping extents */
	ret = btrfs_drop_extents(trans, root, inode,
			 start, extent_end, start, &alloc_hint);
	BUG_ON(ret);

	/* insert the extent */
	ret = overwrite_item(trans, root, path, eb, slot, key);
	BUG_ON(ret);

	/* btrfs_drop_extents changes i_bytes & i_blocks, update it here */
	inode_add_bytes(inode, extent_end - start);
	btrfs_update_inode(trans, root, inode);
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
	read_extent_buffer(leaf, name, (unsigned long)(di + 1), name_len);
	btrfs_release_path(root, path);

	inode = read_one_inode(root, location.objectid);
	BUG_ON(!inode);

	btrfs_inc_nlink(inode);
	ret = btrfs_unlink_inode(trans, root, dir, inode, name, name_len);
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
	btrfs_release_path(root, path);

	di = btrfs_lookup_dir_item(NULL, root, path, dirid, name, name_len, 0);
	if (di && !IS_ERR(di)) {
		btrfs_dir_item_key_to_cpu(path->nodes[0], di, &location);
		if (location.objectid != objectid)
			goto out;
	} else
		goto out;
	match = 1;
out:
	btrfs_release_path(root, path);
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
				   char *name, int namelen)
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
	ret = btrfs_search_slot(NULL, log, key, path, 0, 0);
	if (ret != 0)
		goto out;

	item_size = btrfs_item_size_nr(path->nodes[0], path->slots[0]);
	ptr = btrfs_item_ptr_offset(path->nodes[0], path->slots[0]);
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
	struct inode *dir;
	int ret;
	struct btrfs_key location;
	struct btrfs_inode_ref *ref;
	struct btrfs_dir_item *di;
	struct inode *inode;
	char *name;
	int namelen;
	unsigned long ref_ptr;
	unsigned long ref_end;

	location.objectid = key->objectid;
	location.type = BTRFS_INODE_ITEM_KEY;
	location.offset = 0;

	/*
	 * it is possible that we didn't log all the parent directories
	 * for a given inode.  If we don't find the dir, just don't
	 * copy the back ref in.  The link count fixup code will take
	 * care of the rest
	 */
	dir = read_one_inode(root, key->offset);
	if (!dir)
		return -ENOENT;

	inode = read_one_inode(root, key->objectid);
	BUG_ON(!dir);

	ref_ptr = btrfs_item_ptr_offset(eb, slot);
	ref_end = ref_ptr + btrfs_item_size_nr(eb, slot);

again:
	ref = (struct btrfs_inode_ref *)ref_ptr;

	namelen = btrfs_inode_ref_name_len(eb, ref);
	name = kmalloc(namelen, GFP_NOFS);
	BUG_ON(!name);

	read_extent_buffer(eb, name, (unsigned long)(ref + 1), namelen);

	/* if we already have a perfect match, we're done */
	if (inode_in_dir(root, path, dir->i_ino, inode->i_ino,
			 btrfs_inode_ref_index(eb, ref),
			 name, namelen)) {
		goto out;
	}

	/*
	 * look for a conflicting back reference in the metadata.
	 * if we find one we have to unlink that name of the file
	 * before we add our new link.  Later on, we overwrite any
	 * existing back reference, and we don't want to create
	 * dangling pointers in the directory.
	 */
conflict_again:
	ret = btrfs_search_slot(NULL, root, key, path, 0, 0);
	if (ret == 0) {
		char *victim_name;
		int victim_name_len;
		struct btrfs_inode_ref *victim_ref;
		unsigned long ptr;
		unsigned long ptr_end;
		struct extent_buffer *leaf = path->nodes[0];

		/* are we trying to overwrite a back ref for the root directory
		 * if so, just jump out, we're done
		 */
		if (key->objectid == key->offset)
			goto out_nowrite;

		/* check all the names in this back reference to see
		 * if they are in the log.  if so, we allow them to stay
		 * otherwise they must be unlinked as a conflict
		 */
		ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
		ptr_end = ptr + btrfs_item_size_nr(leaf, path->slots[0]);
		while(ptr < ptr_end) {
			victim_ref = (struct btrfs_inode_ref *)ptr;
			victim_name_len = btrfs_inode_ref_name_len(leaf,
								   victim_ref);
			victim_name = kmalloc(victim_name_len, GFP_NOFS);
			BUG_ON(!victim_name);

			read_extent_buffer(leaf, victim_name,
					   (unsigned long)(victim_ref + 1),
					   victim_name_len);

			if (!backref_in_log(log, key, victim_name,
					    victim_name_len)) {
				btrfs_inc_nlink(inode);
				btrfs_release_path(root, path);
				ret = btrfs_unlink_inode(trans, root, dir,
							 inode, victim_name,
							 victim_name_len);
				kfree(victim_name);
				btrfs_release_path(root, path);
				goto conflict_again;
			}
			kfree(victim_name);
			ptr = (unsigned long)(victim_ref + 1) + victim_name_len;
		}
		BUG_ON(ret);
	}
	btrfs_release_path(root, path);

	/* look for a conflicting sequence number */
	di = btrfs_lookup_dir_index_item(trans, root, path, dir->i_ino,
					 btrfs_inode_ref_index(eb, ref),
					 name, namelen, 0);
	if (di && !IS_ERR(di)) {
		ret = drop_one_dir_item(trans, root, path, dir, di);
		BUG_ON(ret);
	}
	btrfs_release_path(root, path);


	/* look for a conflicting name */
	di = btrfs_lookup_dir_item(trans, root, path, dir->i_ino,
				   name, namelen, 0);
	if (di && !IS_ERR(di)) {
		ret = drop_one_dir_item(trans, root, path, dir, di);
		BUG_ON(ret);
	}
	btrfs_release_path(root, path);

	/* insert our name */
	ret = btrfs_add_link(trans, dir, inode, name, namelen, 0,
			     btrfs_inode_ref_index(eb, ref));
	BUG_ON(ret);

	btrfs_update_inode(trans, root, inode);

out:
	ref_ptr = (unsigned long)(ref + 1) + namelen;
	kfree(name);
	if (ref_ptr < ref_end)
		goto again;

	/* finally write the back reference in the inode */
	ret = overwrite_item(trans, root, path, eb, slot, key);
	BUG_ON(ret);

out_nowrite:
	btrfs_release_path(root, path);
	iput(dir);
	iput(inode);
	return 0;
}

/*
 * replay one csum item from the log tree into the subvolume 'root'
 * eb, slot and key all refer to the log tree
 * path is for temp use by this function and should be released on return
 *
 * This copies the checksums out of the log tree and inserts them into
 * the subvolume.  Any existing checksums for this range in the file
 * are overwritten, and new items are added where required.
 *
 * We keep this simple by reusing the btrfs_ordered_sum code from
 * the data=ordered mode.  This basically means making a copy
 * of all the checksums in ram, which we have to do anyway for kmap
 * rules.
 *
 * The copy is then sent down to btrfs_csum_file_blocks, which
 * does all the hard work of finding existing items in the file
 * or adding new ones.
 */
static noinline int replay_one_csum(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      struct btrfs_path *path,
				      struct extent_buffer *eb, int slot,
				      struct btrfs_key *key)
{
	int ret;
	u32 item_size = btrfs_item_size_nr(eb, slot);
	u64 cur_offset;
	unsigned long file_bytes;
	struct btrfs_ordered_sum *sums;
	struct btrfs_sector_sum *sector_sum;
	struct inode *inode;
	unsigned long ptr;

	file_bytes = (item_size / BTRFS_CRC32_SIZE) * root->sectorsize;
	inode = read_one_inode(root, key->objectid);
	if (!inode) {
		return -EIO;
	}

	sums = kzalloc(btrfs_ordered_sum_size(root, file_bytes), GFP_NOFS);
	if (!sums) {
		iput(inode);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&sums->list);
	sums->len = file_bytes;
	sums->file_offset = key->offset;

	/*
	 * copy all the sums into the ordered sum struct
	 */
	sector_sum = sums->sums;
	cur_offset = key->offset;
	ptr = btrfs_item_ptr_offset(eb, slot);
	while(item_size > 0) {
		sector_sum->offset = cur_offset;
		read_extent_buffer(eb, &sector_sum->sum, ptr, BTRFS_CRC32_SIZE);
		sector_sum++;
		item_size -= BTRFS_CRC32_SIZE;
		ptr += BTRFS_CRC32_SIZE;
		cur_offset += root->sectorsize;
	}

	/* let btrfs_csum_file_blocks add them into the file */
	ret = btrfs_csum_file_blocks(trans, root, inode, sums);
	BUG_ON(ret);
	kfree(sums);
	iput(inode);

	return 0;
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
	struct btrfs_key key;
	u64 nlink = 0;
	unsigned long ptr;
	unsigned long ptr_end;
	int name_len;

	key.objectid = inode->i_ino;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = (u64)-1;

	path = btrfs_alloc_path();

	while(1) {
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			break;
		if (ret > 0) {
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		}
		btrfs_item_key_to_cpu(path->nodes[0], &key,
				      path->slots[0]);
		if (key.objectid != inode->i_ino ||
		    key.type != BTRFS_INODE_REF_KEY)
			break;
		ptr = btrfs_item_ptr_offset(path->nodes[0], path->slots[0]);
		ptr_end = ptr + btrfs_item_size_nr(path->nodes[0],
						   path->slots[0]);
		while(ptr < ptr_end) {
			struct btrfs_inode_ref *ref;

			ref = (struct btrfs_inode_ref *)ptr;
			name_len = btrfs_inode_ref_name_len(path->nodes[0],
							    ref);
			ptr = (unsigned long)(ref + 1) + name_len;
			nlink++;
		}

		if (key.offset == 0)
			break;
		key.offset--;
		btrfs_release_path(root, path);
	}
	btrfs_free_path(path);
	if (nlink != inode->i_nlink) {
		inode->i_nlink = nlink;
		btrfs_update_inode(trans, root, inode);
	}
	BTRFS_I(inode)->index_cnt = (u64)-1;

	return 0;
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
	while(1) {
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
		BUG_ON(ret);

		btrfs_release_path(root, path);
		inode = read_one_inode(root, key.offset);
		BUG_ON(!inode);

		ret = fixup_inode_link_count(trans, root, inode);
		BUG_ON(ret);

		iput(inode);

		if (key.offset == 0)
			break;
		key.offset--;
	}
	btrfs_release_path(root, path);
	return 0;
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
	BUG_ON(!inode);

	key.objectid = BTRFS_TREE_LOG_FIXUP_OBJECTID;
	btrfs_set_key_type(&key, BTRFS_ORPHAN_ITEM_KEY);
	key.offset = objectid;

	ret = btrfs_insert_empty_item(trans, root, path, &key, 0);

	btrfs_release_path(root, path);
	if (ret == 0) {
		btrfs_inc_nlink(inode);
		btrfs_update_inode(trans, root, inode);
	} else if (ret == -EEXIST) {
		ret = 0;
	} else {
		BUG();
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
				    struct btrfs_path *path,
				    u64 dirid, u64 index,
				    char *name, int name_len, u8 type,
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
	int ret;

	dir = read_one_inode(root, key->objectid);
	BUG_ON(!dir);

	name_len = btrfs_dir_name_len(eb, di);
	name = kmalloc(name_len, GFP_NOFS);
	log_type = btrfs_dir_type(eb, di);
	read_extent_buffer(eb, name, (unsigned long)(di + 1),
		   name_len);

	btrfs_dir_item_key_to_cpu(eb, di, &log_key);
	exists = btrfs_lookup_inode(trans, root, path, &log_key, 0);
	if (exists == 0)
		exists = 1;
	else
		exists = 0;
	btrfs_release_path(root, path);

	if (key->type == BTRFS_DIR_ITEM_KEY) {
		dst_di = btrfs_lookup_dir_item(trans, root, path, key->objectid,
				       name, name_len, 1);
	}
	else if (key->type == BTRFS_DIR_INDEX_KEY) {
		dst_di = btrfs_lookup_dir_index_item(trans, root, path,
						     key->objectid,
						     key->offset, name,
						     name_len, 1);
	} else {
		BUG();
	}
	if (!dst_di || IS_ERR(dst_di)) {
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
		goto out;
	}

	/*
	 * don't drop the conflicting directory entry if the inode
	 * for the new entry doesn't exist
	 */
	if (!exists)
		goto out;

	ret = drop_one_dir_item(trans, root, path, dir, dst_di);
	BUG_ON(ret);

	if (key->type == BTRFS_DIR_INDEX_KEY)
		goto insert;
out:
	btrfs_release_path(root, path);
	kfree(name);
	iput(dir);
	return 0;

insert:
	btrfs_release_path(root, path);
	ret = insert_one_name(trans, root, path, key->objectid, key->offset,
			      name, name_len, log_type, &log_key);

	if (ret && ret != -ENOENT)
		BUG();
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
	int ret;
	u32 item_size = btrfs_item_size_nr(eb, slot);
	struct btrfs_dir_item *di;
	int name_len;
	unsigned long ptr;
	unsigned long ptr_end;

	ptr = btrfs_item_ptr_offset(eb, slot);
	ptr_end = ptr + item_size;
	while(ptr < ptr_end) {
		di = (struct btrfs_dir_item *)ptr;
		name_len = btrfs_dir_name_len(eb, di);
		ret = replay_one_name(trans, root, path, eb, di, key);
		BUG_ON(ret);
		ptr = (unsigned long)(di + 1);
		ptr += name_len;
	}
	return 0;
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
	btrfs_release_path(root, path);
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
	while(ptr < ptr_end) {
		di = (struct btrfs_dir_item *)ptr;
		name_len = btrfs_dir_name_len(eb, di);
		name = kmalloc(name_len, GFP_NOFS);
		if (!name) {
			ret = -ENOMEM;
			goto out;
		}
		read_extent_buffer(eb, name, (unsigned long)(di + 1),
				  name_len);
		log_di = NULL;
		if (dir_key->type == BTRFS_DIR_ITEM_KEY) {
			log_di = btrfs_lookup_dir_item(trans, log, log_path,
						       dir_key->objectid,
						       name, name_len, 0);
		} else if (dir_key->type == BTRFS_DIR_INDEX_KEY) {
			log_di = btrfs_lookup_dir_index_item(trans, log,
						     log_path,
						     dir_key->objectid,
						     dir_key->offset,
						     name, name_len, 0);
		}
		if (!log_di || IS_ERR(log_di)) {
			btrfs_dir_item_key_to_cpu(eb, di, &location);
			btrfs_release_path(root, path);
			btrfs_release_path(log, log_path);
			inode = read_one_inode(root, location.objectid);
			BUG_ON(!inode);

			ret = link_to_fixup_dir(trans, root,
						path, location.objectid);
			BUG_ON(ret);
			btrfs_inc_nlink(inode);
			ret = btrfs_unlink_inode(trans, root, dir, inode,
						 name, name_len);
			BUG_ON(ret);
			kfree(name);
			iput(inode);

			/* there might still be more names under this key
			 * check and repeat if required
			 */
			ret = btrfs_search_slot(NULL, root, dir_key, path,
						0, 0);
			if (ret == 0)
				goto again;
			ret = 0;
			goto out;
		}
		btrfs_release_path(log, log_path);
		kfree(name);

		ptr = (unsigned long)(di + 1);
		ptr += name_len;
	}
	ret = 0;
out:
	btrfs_release_path(root, path);
	btrfs_release_path(log, log_path);
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
				       u64 dirid)
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
	while(1) {
		ret = find_dir_range(log, path, dirid, key_type,
				     &range_start, &range_end);
		if (ret != 0)
			break;

		dir_key.offset = range_start;
		while(1) {
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
						log_path, dir, &found_key);
			BUG_ON(ret);
			if (found_key.offset == (u64)-1)
				break;
			dir_key.offset = found_key.offset + 1;
		}
		btrfs_release_path(root, path);
		if (range_end == (u64)-1)
			break;
		range_start = range_end + 1;
	}

next_type:
	ret = 0;
	if (key_type == BTRFS_DIR_LOG_ITEM_KEY) {
		key_type = BTRFS_DIR_LOG_INDEX_KEY;
		dir_key.type = BTRFS_DIR_INDEX_KEY;
		btrfs_release_path(root, path);
		goto again;
	}
out:
	btrfs_release_path(root, path);
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
	u32 item_size;
	int level;
	int i;
	int ret;

	btrfs_read_buffer(eb, gen);

	level = btrfs_header_level(eb);

	if (level != 0)
		return 0;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	nritems = btrfs_header_nritems(eb);
	for (i = 0; i < nritems; i++) {
		btrfs_item_key_to_cpu(eb, &key, i);
		item_size = btrfs_item_size_nr(eb, i);

		/* inode keys are done during the first stage */
		if (key.type == BTRFS_INODE_ITEM_KEY &&
		    wc->stage == LOG_WALK_REPLAY_INODES) {
			struct inode *inode;
			struct btrfs_inode_item *inode_item;
			u32 mode;

			inode_item = btrfs_item_ptr(eb, i,
					    struct btrfs_inode_item);
			mode = btrfs_inode_mode(eb, inode_item);
			if (S_ISDIR(mode)) {
				ret = replay_dir_deletes(wc->trans,
					 root, log, path, key.objectid);
				BUG_ON(ret);
			}
			ret = overwrite_item(wc->trans, root, path,
					     eb, i, &key);
			BUG_ON(ret);

			/* for regular files, truncate away
			 * extents past the new EOF
			 */
			if (S_ISREG(mode)) {
				inode = read_one_inode(root,
						       key.objectid);
				BUG_ON(!inode);

				ret = btrfs_truncate_inode_items(wc->trans,
					root, inode, inode->i_size,
					BTRFS_EXTENT_DATA_KEY);
				BUG_ON(ret);
				iput(inode);
			}
			ret = link_to_fixup_dir(wc->trans, root,
						path, key.objectid);
			BUG_ON(ret);
		}
		if (wc->stage < LOG_WALK_REPLAY_ALL)
			continue;

		/* these keys are simply copied */
		if (key.type == BTRFS_XATTR_ITEM_KEY) {
			ret = overwrite_item(wc->trans, root, path,
					     eb, i, &key);
			BUG_ON(ret);
		} else if (key.type == BTRFS_INODE_REF_KEY) {
			ret = add_inode_ref(wc->trans, root, log, path,
					    eb, i, &key);
			BUG_ON(ret && ret != -ENOENT);
		} else if (key.type == BTRFS_EXTENT_DATA_KEY) {
			ret = replay_one_extent(wc->trans, root, path,
						eb, i, &key);
			BUG_ON(ret);
		} else if (key.type == BTRFS_CSUM_ITEM_KEY) {
			ret = replay_one_csum(wc->trans, root, path,
					      eb, i, &key);
			BUG_ON(ret);
		} else if (key.type == BTRFS_DIR_ITEM_KEY ||
			   key.type == BTRFS_DIR_INDEX_KEY) {
			ret = replay_one_dir_item(wc->trans, root, path,
						  eb, i, &key);
			BUG_ON(ret);
		}
	}
	btrfs_free_path(path);
	return 0;
}

static int noinline walk_down_log_tree(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct btrfs_path *path, int *level,
				   struct walk_control *wc)
{
	u64 root_owner;
	u64 root_gen;
	u64 bytenr;
	u64 ptr_gen;
	struct extent_buffer *next;
	struct extent_buffer *cur;
	struct extent_buffer *parent;
	u32 blocksize;
	int ret = 0;

	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);

	while(*level > 0) {
		WARN_ON(*level < 0);
		WARN_ON(*level >= BTRFS_MAX_LEVEL);
		cur = path->nodes[*level];

		if (btrfs_header_level(cur) != *level)
			WARN_ON(1);

		if (path->slots[*level] >=
		    btrfs_header_nritems(cur))
			break;

		bytenr = btrfs_node_blockptr(cur, path->slots[*level]);
		ptr_gen = btrfs_node_ptr_generation(cur, path->slots[*level]);
		blocksize = btrfs_level_size(root, *level - 1);

		parent = path->nodes[*level];
		root_owner = btrfs_header_owner(parent);
		root_gen = btrfs_header_generation(parent);

		next = btrfs_find_create_tree_block(root, bytenr, blocksize);

		wc->process_func(root, next, wc, ptr_gen);

		if (*level == 1) {
			path->slots[*level]++;
			if (wc->free) {
				btrfs_read_buffer(next, ptr_gen);

				btrfs_tree_lock(next);
				clean_tree_block(trans, root, next);
				btrfs_wait_tree_block_writeback(next);
				btrfs_tree_unlock(next);

				ret = btrfs_drop_leaf_ref(trans, root, next);
				BUG_ON(ret);

				WARN_ON(root_owner !=
					BTRFS_TREE_LOG_OBJECTID);
				ret = btrfs_free_reserved_extent(root,
							 bytenr, blocksize);
				BUG_ON(ret);
			}
			free_extent_buffer(next);
			continue;
		}
		btrfs_read_buffer(next, ptr_gen);

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

	if (path->nodes[*level] == root->node) {
		parent = path->nodes[*level];
	} else {
		parent = path->nodes[*level + 1];
	}
	bytenr = path->nodes[*level]->start;

	blocksize = btrfs_level_size(root, *level);
	root_owner = btrfs_header_owner(parent);
	root_gen = btrfs_header_generation(parent);

	wc->process_func(root, path->nodes[*level], wc,
			 btrfs_header_generation(path->nodes[*level]));

	if (wc->free) {
		next = path->nodes[*level];
		btrfs_tree_lock(next);
		clean_tree_block(trans, root, next);
		btrfs_wait_tree_block_writeback(next);
		btrfs_tree_unlock(next);

		if (*level == 0) {
			ret = btrfs_drop_leaf_ref(trans, root, next);
			BUG_ON(ret);
		}
		WARN_ON(root_owner != BTRFS_TREE_LOG_OBJECTID);
		ret = btrfs_free_reserved_extent(root, bytenr, blocksize);
		BUG_ON(ret);
	}
	free_extent_buffer(path->nodes[*level]);
	path->nodes[*level] = NULL;
	*level += 1;

	cond_resched();
	return 0;
}

static int noinline walk_up_log_tree(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path, int *level,
				 struct walk_control *wc)
{
	u64 root_owner;
	u64 root_gen;
	int i;
	int slot;
	int ret;

	for(i = *level; i < BTRFS_MAX_LEVEL - 1 && path->nodes[i]; i++) {
		slot = path->slots[i];
		if (slot < btrfs_header_nritems(path->nodes[i]) - 1) {
			struct extent_buffer *node;
			node = path->nodes[i];
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
			root_gen = btrfs_header_generation(parent);
			wc->process_func(root, path->nodes[*level], wc,
				 btrfs_header_generation(path->nodes[*level]));
			if (wc->free) {
				struct extent_buffer *next;

				next = path->nodes[*level];

				btrfs_tree_lock(next);
				clean_tree_block(trans, root, next);
				btrfs_wait_tree_block_writeback(next);
				btrfs_tree_unlock(next);

				if (*level == 0) {
					ret = btrfs_drop_leaf_ref(trans, root,
								  next);
					BUG_ON(ret);
				}

				WARN_ON(root_owner != BTRFS_TREE_LOG_OBJECTID);
				ret = btrfs_free_reserved_extent(root,
						path->nodes[*level]->start,
						path->nodes[*level]->len);
				BUG_ON(ret);
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
	int i;
	int orig_level;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	level = btrfs_header_level(log->node);
	orig_level = level;
	path->nodes[level] = log->node;
	extent_buffer_get(log->node);
	path->slots[level] = 0;

	while(1) {
		wret = walk_down_log_tree(trans, log, path, &level, wc);
		if (wret > 0)
			break;
		if (wret < 0)
			ret = wret;

		wret = walk_up_log_tree(trans, log, path, &level, wc);
		if (wret > 0)
			break;
		if (wret < 0)
			ret = wret;
	}

	/* was the root node processed? if not, catch it here */
	if (path->nodes[orig_level]) {
		wc->process_func(log, path->nodes[orig_level], wc,
			 btrfs_header_generation(path->nodes[orig_level]));
		if (wc->free) {
			struct extent_buffer *next;

			next = path->nodes[orig_level];

			btrfs_tree_lock(next);
			clean_tree_block(trans, log, next);
			btrfs_wait_tree_block_writeback(next);
			btrfs_tree_unlock(next);

			if (orig_level == 0) {
				ret = btrfs_drop_leaf_ref(trans, log,
							  next);
				BUG_ON(ret);
			}
			WARN_ON(log->root_key.objectid !=
				BTRFS_TREE_LOG_OBJECTID);
			ret = btrfs_free_reserved_extent(log, next->start,
							 next->len);
			BUG_ON(ret);
		}
	}

	for (i = 0; i <= orig_level; i++) {
		if (path->nodes[i]) {
			free_extent_buffer(path->nodes[i]);
			path->nodes[i] = NULL;
		}
	}
	btrfs_free_path(path);
	if (wc->free)
		free_extent_buffer(log->node);
	return ret;
}

int wait_log_commit(struct btrfs_root *log)
{
	DEFINE_WAIT(wait);
	u64 transid = log->fs_info->tree_log_transid;

	do {
		prepare_to_wait(&log->fs_info->tree_log_wait, &wait,
				TASK_UNINTERRUPTIBLE);
		mutex_unlock(&log->fs_info->tree_log_mutex);
		if (atomic_read(&log->fs_info->tree_log_commit))
			schedule();
		finish_wait(&log->fs_info->tree_log_wait, &wait);
		mutex_lock(&log->fs_info->tree_log_mutex);
	} while(transid == log->fs_info->tree_log_transid &&
		atomic_read(&log->fs_info->tree_log_commit));
	return 0;
}

/*
 * btrfs_sync_log does sends a given tree log down to the disk and
 * updates the super blocks to record it.  When this call is done,
 * you know that any inodes previously logged are safely on disk
 */
int btrfs_sync_log(struct btrfs_trans_handle *trans,
		   struct btrfs_root *root)
{
	int ret;
	unsigned long batch;
	struct btrfs_root *log = root->log_root;

	mutex_lock(&log->fs_info->tree_log_mutex);
	if (atomic_read(&log->fs_info->tree_log_commit)) {
		wait_log_commit(log);
		goto out;
	}
	atomic_set(&log->fs_info->tree_log_commit, 1);

	while(1) {
		batch = log->fs_info->tree_log_batch;
		mutex_unlock(&log->fs_info->tree_log_mutex);
		schedule_timeout_uninterruptible(1);
		mutex_lock(&log->fs_info->tree_log_mutex);

		while(atomic_read(&log->fs_info->tree_log_writers)) {
			DEFINE_WAIT(wait);
			prepare_to_wait(&log->fs_info->tree_log_wait, &wait,
					TASK_UNINTERRUPTIBLE);
			mutex_unlock(&log->fs_info->tree_log_mutex);
			if (atomic_read(&log->fs_info->tree_log_writers))
				schedule();
			mutex_lock(&log->fs_info->tree_log_mutex);
			finish_wait(&log->fs_info->tree_log_wait, &wait);
		}
		if (batch == log->fs_info->tree_log_batch)
			break;
	}

	ret = btrfs_write_and_wait_marked_extents(log, &log->dirty_log_pages);
	BUG_ON(ret);
	ret = btrfs_write_and_wait_marked_extents(root->fs_info->log_root_tree,
			       &root->fs_info->log_root_tree->dirty_log_pages);
	BUG_ON(ret);

	btrfs_set_super_log_root(&root->fs_info->super_for_commit,
				 log->fs_info->log_root_tree->node->start);
	btrfs_set_super_log_root_level(&root->fs_info->super_for_commit,
		       btrfs_header_level(log->fs_info->log_root_tree->node));

	write_ctree_super(trans, log->fs_info->tree_root);
	log->fs_info->tree_log_transid++;
	log->fs_info->tree_log_batch = 0;
	atomic_set(&log->fs_info->tree_log_commit, 0);
	smp_mb();
	if (waitqueue_active(&log->fs_info->tree_log_wait))
		wake_up(&log->fs_info->tree_log_wait);
out:
	mutex_unlock(&log->fs_info->tree_log_mutex);
	return 0;

}

/* * free all the extents used by the tree log.  This should be called
 * at commit time of the full transaction
 */
int btrfs_free_log(struct btrfs_trans_handle *trans, struct btrfs_root *root)
{
	int ret;
	struct btrfs_root *log;
	struct key;
	u64 start;
	u64 end;
	struct walk_control wc = {
		.free = 1,
		.process_func = process_one_buffer
	};

	if (!root->log_root)
		return 0;

	log = root->log_root;
	ret = walk_log_tree(trans, log, &wc);
	BUG_ON(ret);

	while(1) {
		ret = find_first_extent_bit(&log->dirty_log_pages,
				    0, &start, &end, EXTENT_DIRTY);
		if (ret)
			break;

		clear_extent_dirty(&log->dirty_log_pages,
				   start, end, GFP_NOFS);
	}

	log = root->log_root;
	ret = btrfs_del_root(trans, root->fs_info->log_root_tree,
			     &log->root_key);
	BUG_ON(ret);
	root->log_root = NULL;
	kfree(root->log_root);
	return 0;
}

/*
 * helper function to update the item for a given subvolumes log root
 * in the tree of log roots
 */
static int update_log_root(struct btrfs_trans_handle *trans,
			   struct btrfs_root *log)
{
	u64 bytenr = btrfs_root_bytenr(&log->root_item);
	int ret;

	if (log->node->start == bytenr)
		return 0;

	btrfs_set_root_bytenr(&log->root_item, log->node->start);
	btrfs_set_root_level(&log->root_item, btrfs_header_level(log->node));
	ret = btrfs_update_root(trans, log->fs_info->log_root_tree,
				&log->root_key, &log->root_item);
	BUG_ON(ret);
	return ret;
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
	int bytes_del = 0;

	if (BTRFS_I(dir)->logged_trans < trans->transid)
		return 0;

	ret = join_running_log_trans(root);
	if (ret)
		return 0;

	mutex_lock(&BTRFS_I(dir)->log_mutex);

	log = root->log_root;
	path = btrfs_alloc_path();
	di = btrfs_lookup_dir_item(trans, log, path, dir->i_ino,
				   name, name_len, -1);
	if (di && !IS_ERR(di)) {
		ret = btrfs_delete_one_dir_name(trans, log, path, di);
		bytes_del += name_len;
		BUG_ON(ret);
	}
	btrfs_release_path(log, path);
	di = btrfs_lookup_dir_index_item(trans, log, path, dir->i_ino,
					 index, name, name_len, -1);
	if (di && !IS_ERR(di)) {
		ret = btrfs_delete_one_dir_name(trans, log, path, di);
		bytes_del += name_len;
		BUG_ON(ret);
	}

	/* update the directory size in the log to reflect the names
	 * we have removed
	 */
	if (bytes_del) {
		struct btrfs_key key;

		key.objectid = dir->i_ino;
		key.offset = 0;
		key.type = BTRFS_INODE_ITEM_KEY;
		btrfs_release_path(log, path);

		ret = btrfs_search_slot(trans, log, &key, path, 0, 1);
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
		btrfs_release_path(log, path);
	}

	btrfs_free_path(path);
	mutex_unlock(&BTRFS_I(dir)->log_mutex);
	end_log_trans(root);

	return 0;
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

	ret = btrfs_del_inode_ref(trans, log, name, name_len, inode->i_ino,
				  dirid, &index);
	mutex_unlock(&BTRFS_I(inode)->log_mutex);
	end_log_trans(root);

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
	BUG_ON(ret);

	item = btrfs_item_ptr(path->nodes[0], path->slots[0],
			      struct btrfs_dir_log_item);
	btrfs_set_dir_log_end(path->nodes[0], item, last_offset);
	btrfs_mark_buffer_dirty(path->nodes[0]);
	btrfs_release_path(log, path);
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
			  u64 min_offset, u64 *last_offset_ret)
{
	struct btrfs_key min_key;
	struct btrfs_key max_key;
	struct btrfs_root *log = root->log_root;
	struct extent_buffer *src;
	int ret;
	int i;
	int nritems;
	u64 first_offset = min_offset;
	u64 last_offset = (u64)-1;

	log = root->log_root;
	max_key.objectid = inode->i_ino;
	max_key.offset = (u64)-1;
	max_key.type = key_type;

	min_key.objectid = inode->i_ino;
	min_key.type = key_type;
	min_key.offset = min_offset;

	path->keep_locks = 1;

	ret = btrfs_search_forward(root, &min_key, &max_key,
				   path, 0, trans->transid);

	/*
	 * we didn't find anything from this transaction, see if there
	 * is anything at all
	 */
	if (ret != 0 || min_key.objectid != inode->i_ino ||
	    min_key.type != key_type) {
		min_key.objectid = inode->i_ino;
		min_key.type = key_type;
		min_key.offset = (u64)-1;
		btrfs_release_path(root, path);
		ret = btrfs_search_slot(NULL, root, &min_key, path, 0, 0);
		if (ret < 0) {
			btrfs_release_path(root, path);
			return ret;
		}
		ret = btrfs_previous_item(root, path, inode->i_ino, key_type);

		/* if ret == 0 there are items for this type,
		 * create a range to tell us the last key of this type.
		 * otherwise, there are no items in this directory after
		 * *min_offset, and we create a range to indicate that.
		 */
		if (ret == 0) {
			struct btrfs_key tmp;
			btrfs_item_key_to_cpu(path->nodes[0], &tmp,
					      path->slots[0]);
			if (key_type == tmp.type) {
				first_offset = max(min_offset, tmp.offset) + 1;
			}
		}
		goto done;
	}

	/* go backward to find any previous key */
	ret = btrfs_previous_item(root, path, inode->i_ino, key_type);
	if (ret == 0) {
		struct btrfs_key tmp;
		btrfs_item_key_to_cpu(path->nodes[0], &tmp, path->slots[0]);
		if (key_type == tmp.type) {
			first_offset = tmp.offset;
			ret = overwrite_item(trans, log, dst_path,
					     path->nodes[0], path->slots[0],
					     &tmp);
		}
	}
	btrfs_release_path(root, path);

	/* find the first key from this transaction again */
	ret = btrfs_search_slot(NULL, root, &min_key, path, 0, 0);
	if (ret != 0) {
		WARN_ON(1);
		goto done;
	}

	/*
	 * we have a block from this transaction, log every item in it
	 * from our directory
	 */
	while(1) {
		struct btrfs_key tmp;
		src = path->nodes[0];
		nritems = btrfs_header_nritems(src);
		for (i = path->slots[0]; i < nritems; i++) {
			btrfs_item_key_to_cpu(src, &min_key, i);

			if (min_key.objectid != inode->i_ino ||
			    min_key.type != key_type)
				goto done;
			ret = overwrite_item(trans, log, dst_path, src, i,
					     &min_key);
			BUG_ON(ret);
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
		if (tmp.objectid != inode->i_ino || tmp.type != key_type) {
			last_offset = (u64)-1;
			goto done;
		}
		if (btrfs_header_generation(path->nodes[0]) != trans->transid) {
			ret = overwrite_item(trans, log, dst_path,
					     path->nodes[0], path->slots[0],
					     &tmp);

			BUG_ON(ret);
			last_offset = tmp.offset;
			goto done;
		}
	}
done:
	*last_offset_ret = last_offset;
	btrfs_release_path(root, path);
	btrfs_release_path(log, dst_path);

	/* insert the log range keys to indicate where the log is valid */
	ret = insert_dir_log_key(trans, log, path, key_type, inode->i_ino,
				 first_offset, last_offset);
	BUG_ON(ret);
	return 0;
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
			  struct btrfs_path *dst_path)
{
	u64 min_key;
	u64 max_key;
	int ret;
	int key_type = BTRFS_DIR_ITEM_KEY;

again:
	min_key = 0;
	max_key = 0;
	while(1) {
		ret = log_dir_items(trans, root, inode, path,
				    dst_path, key_type, min_key,
				    &max_key);
		BUG_ON(ret);
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

	key.objectid = objectid;
	key.type = max_key_type;
	key.offset = (u64)-1;

	while(1) {
		ret = btrfs_search_slot(trans, log, &key, path, -1, 1);

		if (ret != 1)
			break;

		if (path->slots[0] == 0)
			break;

		path->slots[0]--;
		btrfs_item_key_to_cpu(path->nodes[0], &found_key,
				      path->slots[0]);

		if (found_key.objectid != objectid)
			break;

		ret = btrfs_del_item(trans, log, path);
		BUG_ON(ret);
		btrfs_release_path(log, path);
	}
	btrfs_release_path(log, path);
	return 0;
}

static noinline int copy_items(struct btrfs_trans_handle *trans,
			       struct btrfs_root *log,
			       struct btrfs_path *dst_path,
			       struct extent_buffer *src,
			       int start_slot, int nr, int inode_only)
{
	unsigned long src_offset;
	unsigned long dst_offset;
	struct btrfs_file_extent_item *extent;
	struct btrfs_inode_item *inode_item;
	int ret;
	struct btrfs_key *ins_keys;
	u32 *ins_sizes;
	char *ins_data;
	int i;

	ins_data = kmalloc(nr * sizeof(struct btrfs_key) +
			   nr * sizeof(u32), GFP_NOFS);
	ins_sizes = (u32 *)ins_data;
	ins_keys = (struct btrfs_key *)(ins_data + nr * sizeof(u32));

	for (i = 0; i < nr; i++) {
		ins_sizes[i] = btrfs_item_size_nr(src, i + start_slot);
		btrfs_item_key_to_cpu(src, ins_keys + i, i + start_slot);
	}
	ret = btrfs_insert_empty_items(trans, log, dst_path,
				       ins_keys, ins_sizes, nr);
	BUG_ON(ret);

	for (i = 0; i < nr; i++) {
		dst_offset = btrfs_item_ptr_offset(dst_path->nodes[0],
						   dst_path->slots[0]);

		src_offset = btrfs_item_ptr_offset(src, start_slot + i);

		copy_extent_buffer(dst_path->nodes[0], src, dst_offset,
				   src_offset, ins_sizes[i]);

		if (inode_only == LOG_INODE_EXISTS &&
		    ins_keys[i].type == BTRFS_INODE_ITEM_KEY) {
			inode_item = btrfs_item_ptr(dst_path->nodes[0],
						    dst_path->slots[0],
						    struct btrfs_inode_item);
			btrfs_set_inode_size(dst_path->nodes[0], inode_item, 0);

			/* set the generation to zero so the recover code
			 * can tell the difference between an logging
			 * just to say 'this inode exists' and a logging
			 * to say 'update this inode with these values'
			 */
			btrfs_set_inode_generation(dst_path->nodes[0],
						   inode_item, 0);
		}
		/* take a reference on file data extents so that truncates
		 * or deletes of this inode don't have to relog the inode
		 * again
		 */
		if (btrfs_key_type(ins_keys + i) == BTRFS_EXTENT_DATA_KEY) {
			int found_type;
			extent = btrfs_item_ptr(src, start_slot + i,
						struct btrfs_file_extent_item);

			found_type = btrfs_file_extent_type(src, extent);
			if (found_type == BTRFS_FILE_EXTENT_REG) {
				u64 ds = btrfs_file_extent_disk_bytenr(src,
								   extent);
				u64 dl = btrfs_file_extent_disk_num_bytes(src,
								      extent);
				/* ds == 0 is a hole */
				if (ds != 0) {
					ret = btrfs_inc_extent_ref(trans, log,
						   ds, dl,
						   dst_path->nodes[0]->start,
						   BTRFS_TREE_LOG_OBJECTID,
						   trans->transid,
						   ins_keys[i].objectid,
						   ins_keys[i].offset);
					BUG_ON(ret);
				}
			}
		}
		dst_path->slots[0]++;
	}

	btrfs_mark_buffer_dirty(dst_path->nodes[0]);
	btrfs_release_path(log, dst_path);
	kfree(ins_data);
	return 0;
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
static int __btrfs_log_inode(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, struct inode *inode,
			     int inode_only)
{
	struct btrfs_path *path;
	struct btrfs_path *dst_path;
	struct btrfs_key min_key;
	struct btrfs_key max_key;
	struct btrfs_root *log = root->log_root;
	struct extent_buffer *src = NULL;
	u32 size;
	int ret;
	int nritems;
	int ins_start_slot = 0;
	int ins_nr;

	log = root->log_root;

	path = btrfs_alloc_path();
	dst_path = btrfs_alloc_path();

	min_key.objectid = inode->i_ino;
	min_key.type = BTRFS_INODE_ITEM_KEY;
	min_key.offset = 0;

	max_key.objectid = inode->i_ino;
	if (inode_only == LOG_INODE_EXISTS || S_ISDIR(inode->i_mode))
		max_key.type = BTRFS_XATTR_ITEM_KEY;
	else
		max_key.type = (u8)-1;
	max_key.offset = (u64)-1;

	/*
	 * if this inode has already been logged and we're in inode_only
	 * mode, we don't want to delete the things that have already
	 * been written to the log.
	 *
	 * But, if the inode has been through an inode_only log,
	 * the logged_trans field is not set.  This allows us to catch
	 * any new names for this inode in the backrefs by logging it
	 * again
	 */
	if (inode_only == LOG_INODE_EXISTS &&
	    BTRFS_I(inode)->logged_trans == trans->transid) {
		btrfs_free_path(path);
		btrfs_free_path(dst_path);
		goto out;
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
		ret = drop_objectid_items(trans, log, path,
					  inode->i_ino, max_key_type);
	} else {
		ret = btrfs_truncate_inode_items(trans, log, inode, 0, 0);
	}
	BUG_ON(ret);
	path->keep_locks = 1;

	while(1) {
		ins_nr = 0;
		ret = btrfs_search_forward(root, &min_key, &max_key,
					   path, 0, trans->transid);
		if (ret != 0)
			break;
again:
		/* note, ins_nr might be > 0 here, cleanup outside the loop */
		if (min_key.objectid != inode->i_ino)
			break;
		if (min_key.type > max_key.type)
			break;

		src = path->nodes[0];
		size = btrfs_item_size_nr(src, path->slots[0]);
		if (ins_nr && ins_start_slot + ins_nr == path->slots[0]) {
			ins_nr++;
			goto next_slot;
		} else if (!ins_nr) {
			ins_start_slot = path->slots[0];
			ins_nr = 1;
			goto next_slot;
		}

		ret = copy_items(trans, log, dst_path, src, ins_start_slot,
				 ins_nr, inode_only);
		BUG_ON(ret);
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
			ret = copy_items(trans, log, dst_path, src,
					 ins_start_slot,
					 ins_nr, inode_only);
			BUG_ON(ret);
			ins_nr = 0;
		}
		btrfs_release_path(root, path);

		if (min_key.offset < (u64)-1)
			min_key.offset++;
		else if (min_key.type < (u8)-1)
			min_key.type++;
		else if (min_key.objectid < (u64)-1)
			min_key.objectid++;
		else
			break;
	}
	if (ins_nr) {
		ret = copy_items(trans, log, dst_path, src,
				 ins_start_slot,
				 ins_nr, inode_only);
		BUG_ON(ret);
		ins_nr = 0;
	}
	WARN_ON(ins_nr);
	if (inode_only == LOG_INODE_ALL && S_ISDIR(inode->i_mode)) {
		btrfs_release_path(root, path);
		btrfs_release_path(log, dst_path);
		BTRFS_I(inode)->log_dirty_trans = 0;
		ret = log_directory_changes(trans, root, inode, path, dst_path);
		BUG_ON(ret);
	}
	BTRFS_I(inode)->logged_trans = trans->transid;
	mutex_unlock(&BTRFS_I(inode)->log_mutex);

	btrfs_free_path(path);
	btrfs_free_path(dst_path);

	mutex_lock(&root->fs_info->tree_log_mutex);
	ret = update_log_root(trans, log);
	BUG_ON(ret);
	mutex_unlock(&root->fs_info->tree_log_mutex);
out:
	return 0;
}

int btrfs_log_inode(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, struct inode *inode,
		    int inode_only)
{
	int ret;

	start_log_trans(trans, root);
	ret = __btrfs_log_inode(trans, root, inode, inode_only);
	end_log_trans(root);
	return ret;
}

/*
 * helper function around btrfs_log_inode to make sure newly created
 * parent directories also end up in the log.  A minimal inode and backref
 * only logging is done of any parent directories that are older than
 * the last committed transaction
 */
int btrfs_log_dentry(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, struct dentry *dentry)
{
	int inode_only = LOG_INODE_ALL;
	struct super_block *sb;
	int ret;

	start_log_trans(trans, root);
	sb = dentry->d_inode->i_sb;
	while(1) {
		ret = __btrfs_log_inode(trans, root, dentry->d_inode,
					inode_only);
		BUG_ON(ret);
		inode_only = LOG_INODE_EXISTS;

		dentry = dentry->d_parent;
		if (!dentry || !dentry->d_inode || sb != dentry->d_inode->i_sb)
			break;

		if (BTRFS_I(dentry->d_inode)->generation <=
		    root->fs_info->last_trans_committed)
			break;
	}
	end_log_trans(root);
	return 0;
}

/*
 * it is not safe to log dentry if the chunk root has added new
 * chunks.  This returns 0 if the dentry was logged, and 1 otherwise.
 * If this returns 1, you must commit the transaction to safely get your
 * data on disk.
 */
int btrfs_log_dentry_safe(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct dentry *dentry)
{
	u64 gen;
	gen = root->fs_info->last_trans_new_blockgroup;
	if (gen > root->fs_info->last_trans_committed)
		return 1;
	else
		return btrfs_log_dentry(trans, root, dentry);
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
	u64 highest_inode;
	struct walk_control wc = {
		.process_func = process_one_buffer,
		.stage = 0,
	};

	fs_info->log_root_recovering = 1;
	path = btrfs_alloc_path();
	BUG_ON(!path);

	trans = btrfs_start_transaction(fs_info->tree_root, 1);

	wc.trans = trans;
	wc.pin = 1;

	walk_log_tree(trans, log_root_tree, &wc);

again:
	key.objectid = BTRFS_TREE_LOG_OBJECTID;
	key.offset = (u64)-1;
	btrfs_set_key_type(&key, BTRFS_ROOT_ITEM_KEY);

	while(1) {
		ret = btrfs_search_slot(NULL, log_root_tree, &key, path, 0, 0);
		if (ret < 0)
			break;
		if (ret > 0) {
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		}
		btrfs_item_key_to_cpu(path->nodes[0], &found_key,
				      path->slots[0]);
		btrfs_release_path(log_root_tree, path);
		if (found_key.objectid != BTRFS_TREE_LOG_OBJECTID)
			break;

		log = btrfs_read_fs_root_no_radix(log_root_tree,
						  &found_key);
		BUG_ON(!log);


		tmp_key.objectid = found_key.offset;
		tmp_key.type = BTRFS_ROOT_ITEM_KEY;
		tmp_key.offset = (u64)-1;

		wc.replay_dest = btrfs_read_fs_root_no_name(fs_info, &tmp_key);

		BUG_ON(!wc.replay_dest);

		btrfs_record_root_in_trans(wc.replay_dest);
		ret = walk_log_tree(trans, log, &wc);
		BUG_ON(ret);

		if (wc.stage == LOG_WALK_REPLAY_ALL) {
			ret = fixup_inode_link_counts(trans, wc.replay_dest,
						      path);
			BUG_ON(ret);
		}
		ret = btrfs_find_highest_inode(wc.replay_dest, &highest_inode);
		if (ret == 0) {
			wc.replay_dest->highest_inode = highest_inode;
			wc.replay_dest->last_inode_alloc = highest_inode;
		}

		key.offset = found_key.offset - 1;
		free_extent_buffer(log->node);
		kfree(log);

		if (found_key.offset == 0)
			break;
	}
	btrfs_release_path(log_root_tree, path);

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

	free_extent_buffer(log_root_tree->node);
	log_root_tree->log_root = NULL;
	fs_info->log_root_recovering = 0;

	/* step 4: commit the transaction, which also unpins the blocks */
	btrfs_commit_transaction(trans, fs_info->tree_root);

	kfree(log_root_tree);
	return 0;
}
