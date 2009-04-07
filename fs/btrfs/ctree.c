/*
 * Copyright (C) 2007,2008 Oracle.  All rights reserved.
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
#include "disk-io.h"
#include "transaction.h"
#include "print-tree.h"
#include "locking.h"

static int split_node(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, int level);
static int split_leaf(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *ins_key,
		      struct btrfs_path *path, int data_size, int extend);
static int push_node_left(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct extent_buffer *dst,
			  struct extent_buffer *src, int empty);
static int balance_node_right(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct extent_buffer *dst_buf,
			      struct extent_buffer *src_buf);
static int del_ptr(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_path *path, int level, int slot);

struct btrfs_path *btrfs_alloc_path(void)
{
	struct btrfs_path *path;
	path = kmem_cache_zalloc(btrfs_path_cachep, GFP_NOFS);
	if (path)
		path->reada = 1;
	return path;
}

/*
 * set all locked nodes in the path to blocking locks.  This should
 * be done before scheduling
 */
noinline void btrfs_set_path_blocking(struct btrfs_path *p)
{
	int i;
	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		if (p->nodes[i] && p->locks[i])
			btrfs_set_lock_blocking(p->nodes[i]);
	}
}

/*
 * reset all the locked nodes in the patch to spinning locks.
 *
 * held is used to keep lockdep happy, when lockdep is enabled
 * we set held to a blocking lock before we go around and
 * retake all the spinlocks in the path.  You can safely use NULL
 * for held
 */
noinline void btrfs_clear_path_blocking(struct btrfs_path *p,
					struct extent_buffer *held)
{
	int i;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/* lockdep really cares that we take all of these spinlocks
	 * in the right order.  If any of the locks in the path are not
	 * currently blocking, it is going to complain.  So, make really
	 * really sure by forcing the path to blocking before we clear
	 * the path blocking.
	 */
	if (held)
		btrfs_set_lock_blocking(held);
	btrfs_set_path_blocking(p);
#endif

	for (i = BTRFS_MAX_LEVEL - 1; i >= 0; i--) {
		if (p->nodes[i] && p->locks[i])
			btrfs_clear_lock_blocking(p->nodes[i]);
	}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	if (held)
		btrfs_clear_lock_blocking(held);
#endif
}

/* this also releases the path */
void btrfs_free_path(struct btrfs_path *p)
{
	btrfs_release_path(NULL, p);
	kmem_cache_free(btrfs_path_cachep, p);
}

/*
 * path release drops references on the extent buffers in the path
 * and it drops any locks held by this path
 *
 * It is safe to call this on paths that no locks or extent buffers held.
 */
noinline void btrfs_release_path(struct btrfs_root *root, struct btrfs_path *p)
{
	int i;

	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		p->slots[i] = 0;
		if (!p->nodes[i])
			continue;
		if (p->locks[i]) {
			btrfs_tree_unlock(p->nodes[i]);
			p->locks[i] = 0;
		}
		free_extent_buffer(p->nodes[i]);
		p->nodes[i] = NULL;
	}
}

/*
 * safely gets a reference on the root node of a tree.  A lock
 * is not taken, so a concurrent writer may put a different node
 * at the root of the tree.  See btrfs_lock_root_node for the
 * looping required.
 *
 * The extent buffer returned by this has a reference taken, so
 * it won't disappear.  It may stop being the root of the tree
 * at any time because there are no locks held.
 */
struct extent_buffer *btrfs_root_node(struct btrfs_root *root)
{
	struct extent_buffer *eb;
	spin_lock(&root->node_lock);
	eb = root->node;
	extent_buffer_get(eb);
	spin_unlock(&root->node_lock);
	return eb;
}

/* loop around taking references on and locking the root node of the
 * tree until you end up with a lock on the root.  A locked buffer
 * is returned, with a reference held.
 */
struct extent_buffer *btrfs_lock_root_node(struct btrfs_root *root)
{
	struct extent_buffer *eb;

	while (1) {
		eb = btrfs_root_node(root);
		btrfs_tree_lock(eb);

		spin_lock(&root->node_lock);
		if (eb == root->node) {
			spin_unlock(&root->node_lock);
			break;
		}
		spin_unlock(&root->node_lock);

		btrfs_tree_unlock(eb);
		free_extent_buffer(eb);
	}
	return eb;
}

/* cowonly root (everything not a reference counted cow subvolume), just get
 * put onto a simple dirty list.  transaction.c walks this to make sure they
 * get properly updated on disk.
 */
static void add_root_to_dirty_list(struct btrfs_root *root)
{
	if (root->track_dirty && list_empty(&root->dirty_list)) {
		list_add(&root->dirty_list,
			 &root->fs_info->dirty_cowonly_roots);
	}
}

/*
 * used by snapshot creation to make a copy of a root for a tree with
 * a given objectid.  The buffer with the new root node is returned in
 * cow_ret, and this func returns zero on success or a negative error code.
 */
int btrfs_copy_root(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root,
		      struct extent_buffer *buf,
		      struct extent_buffer **cow_ret, u64 new_root_objectid)
{
	struct extent_buffer *cow;
	u32 nritems;
	int ret = 0;
	int level;
	struct btrfs_root *new_root;

	new_root = kmalloc(sizeof(*new_root), GFP_NOFS);
	if (!new_root)
		return -ENOMEM;

	memcpy(new_root, root, sizeof(*new_root));
	new_root->root_key.objectid = new_root_objectid;

	WARN_ON(root->ref_cows && trans->transid !=
		root->fs_info->running_transaction->transid);
	WARN_ON(root->ref_cows && trans->transid != root->last_trans);

	level = btrfs_header_level(buf);
	nritems = btrfs_header_nritems(buf);

	cow = btrfs_alloc_free_block(trans, new_root, buf->len, 0,
				     new_root_objectid, trans->transid,
				     level, buf->start, 0);
	if (IS_ERR(cow)) {
		kfree(new_root);
		return PTR_ERR(cow);
	}

	copy_extent_buffer(cow, buf, 0, 0, cow->len);
	btrfs_set_header_bytenr(cow, cow->start);
	btrfs_set_header_generation(cow, trans->transid);
	btrfs_set_header_owner(cow, new_root_objectid);
	btrfs_clear_header_flag(cow, BTRFS_HEADER_FLAG_WRITTEN);

	write_extent_buffer(cow, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(cow),
			    BTRFS_FSID_SIZE);

	WARN_ON(btrfs_header_generation(buf) > trans->transid);
	ret = btrfs_inc_ref(trans, new_root, buf, cow, NULL);
	kfree(new_root);

	if (ret)
		return ret;

	btrfs_mark_buffer_dirty(cow);
	*cow_ret = cow;
	return 0;
}

/*
 * does the dirty work in cow of a single block.  The parent block (if
 * supplied) is updated to point to the new cow copy.  The new buffer is marked
 * dirty and returned locked.  If you modify the block it needs to be marked
 * dirty again.
 *
 * search_start -- an allocation hint for the new block
 *
 * empty_size -- a hint that you plan on doing more cow.  This is the size in
 * bytes the allocator should try to find free next to the block it returns.
 * This is just a hint and may be ignored by the allocator.
 */
static noinline int __btrfs_cow_block(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct extent_buffer *buf,
			     struct extent_buffer *parent, int parent_slot,
			     struct extent_buffer **cow_ret,
			     u64 search_start, u64 empty_size)
{
	u64 parent_start;
	struct extent_buffer *cow;
	u32 nritems;
	int ret = 0;
	int level;
	int unlock_orig = 0;

	if (*cow_ret == buf)
		unlock_orig = 1;

	btrfs_assert_tree_locked(buf);

	if (parent)
		parent_start = parent->start;
	else
		parent_start = 0;

	WARN_ON(root->ref_cows && trans->transid !=
		root->fs_info->running_transaction->transid);
	WARN_ON(root->ref_cows && trans->transid != root->last_trans);

	level = btrfs_header_level(buf);
	nritems = btrfs_header_nritems(buf);

	cow = btrfs_alloc_free_block(trans, root, buf->len,
				     parent_start, root->root_key.objectid,
				     trans->transid, level,
				     search_start, empty_size);
	if (IS_ERR(cow))
		return PTR_ERR(cow);

	/* cow is set to blocking by btrfs_init_new_buffer */

	copy_extent_buffer(cow, buf, 0, 0, cow->len);
	btrfs_set_header_bytenr(cow, cow->start);
	btrfs_set_header_generation(cow, trans->transid);
	btrfs_set_header_owner(cow, root->root_key.objectid);
	btrfs_clear_header_flag(cow, BTRFS_HEADER_FLAG_WRITTEN);

	write_extent_buffer(cow, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(cow),
			    BTRFS_FSID_SIZE);

	WARN_ON(btrfs_header_generation(buf) > trans->transid);
	if (btrfs_header_generation(buf) != trans->transid) {
		u32 nr_extents;
		ret = btrfs_inc_ref(trans, root, buf, cow, &nr_extents);
		if (ret)
			return ret;

		ret = btrfs_cache_ref(trans, root, buf, nr_extents);
		WARN_ON(ret);
	} else if (btrfs_header_owner(buf) == BTRFS_TREE_RELOC_OBJECTID) {
		/*
		 * There are only two places that can drop reference to
		 * tree blocks owned by living reloc trees, one is here,
		 * the other place is btrfs_drop_subtree. In both places,
		 * we check reference count while tree block is locked.
		 * Furthermore, if reference count is one, it won't get
		 * increased by someone else.
		 */
		u32 refs;
		ret = btrfs_lookup_extent_ref(trans, root, buf->start,
					      buf->len, &refs);
		BUG_ON(ret);
		if (refs == 1) {
			ret = btrfs_update_ref(trans, root, buf, cow,
					       0, nritems);
			clean_tree_block(trans, root, buf);
		} else {
			ret = btrfs_inc_ref(trans, root, buf, cow, NULL);
		}
		BUG_ON(ret);
	} else {
		ret = btrfs_update_ref(trans, root, buf, cow, 0, nritems);
		if (ret)
			return ret;
		clean_tree_block(trans, root, buf);
	}

	if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID) {
		ret = btrfs_reloc_tree_cache_ref(trans, root, cow, buf->start);
		WARN_ON(ret);
	}

	if (buf == root->node) {
		WARN_ON(parent && parent != buf);

		spin_lock(&root->node_lock);
		root->node = cow;
		extent_buffer_get(cow);
		spin_unlock(&root->node_lock);

		if (buf != root->commit_root) {
			btrfs_free_extent(trans, root, buf->start,
					  buf->len, buf->start,
					  root->root_key.objectid,
					  btrfs_header_generation(buf),
					  level, 1);
		}
		free_extent_buffer(buf);
		add_root_to_dirty_list(root);
	} else {
		btrfs_set_node_blockptr(parent, parent_slot,
					cow->start);
		WARN_ON(trans->transid == 0);
		btrfs_set_node_ptr_generation(parent, parent_slot,
					      trans->transid);
		btrfs_mark_buffer_dirty(parent);
		WARN_ON(btrfs_header_generation(parent) != trans->transid);
		btrfs_free_extent(trans, root, buf->start, buf->len,
				  parent_start, btrfs_header_owner(parent),
				  btrfs_header_generation(parent), level, 1);
	}
	if (unlock_orig)
		btrfs_tree_unlock(buf);
	free_extent_buffer(buf);
	btrfs_mark_buffer_dirty(cow);
	*cow_ret = cow;
	return 0;
}

/*
 * cows a single block, see __btrfs_cow_block for the real work.
 * This version of it has extra checks so that a block isn't cow'd more than
 * once per transaction, as long as it hasn't been written yet
 */
noinline int btrfs_cow_block(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, struct extent_buffer *buf,
		    struct extent_buffer *parent, int parent_slot,
		    struct extent_buffer **cow_ret)
{
	u64 search_start;
	int ret;

	if (trans->transaction != root->fs_info->running_transaction) {
		printk(KERN_CRIT "trans %llu running %llu\n",
		       (unsigned long long)trans->transid,
		       (unsigned long long)
		       root->fs_info->running_transaction->transid);
		WARN_ON(1);
	}
	if (trans->transid != root->fs_info->generation) {
		printk(KERN_CRIT "trans %llu running %llu\n",
		       (unsigned long long)trans->transid,
		       (unsigned long long)root->fs_info->generation);
		WARN_ON(1);
	}

	if (btrfs_header_generation(buf) == trans->transid &&
	    btrfs_header_owner(buf) == root->root_key.objectid &&
	    !btrfs_header_flag(buf, BTRFS_HEADER_FLAG_WRITTEN)) {
		*cow_ret = buf;
		return 0;
	}

	search_start = buf->start & ~((u64)(1024 * 1024 * 1024) - 1);

	if (parent)
		btrfs_set_lock_blocking(parent);
	btrfs_set_lock_blocking(buf);

	ret = __btrfs_cow_block(trans, root, buf, parent,
				 parent_slot, cow_ret, search_start, 0);
	return ret;
}

/*
 * helper function for defrag to decide if two blocks pointed to by a
 * node are actually close by
 */
static int close_blocks(u64 blocknr, u64 other, u32 blocksize)
{
	if (blocknr < other && other - (blocknr + blocksize) < 32768)
		return 1;
	if (blocknr > other && blocknr - (other + blocksize) < 32768)
		return 1;
	return 0;
}

/*
 * compare two keys in a memcmp fashion
 */
static int comp_keys(struct btrfs_disk_key *disk, struct btrfs_key *k2)
{
	struct btrfs_key k1;

	btrfs_disk_key_to_cpu(&k1, disk);

	if (k1.objectid > k2->objectid)
		return 1;
	if (k1.objectid < k2->objectid)
		return -1;
	if (k1.type > k2->type)
		return 1;
	if (k1.type < k2->type)
		return -1;
	if (k1.offset > k2->offset)
		return 1;
	if (k1.offset < k2->offset)
		return -1;
	return 0;
}

/*
 * same as comp_keys only with two btrfs_key's
 */
static int comp_cpu_keys(struct btrfs_key *k1, struct btrfs_key *k2)
{
	if (k1->objectid > k2->objectid)
		return 1;
	if (k1->objectid < k2->objectid)
		return -1;
	if (k1->type > k2->type)
		return 1;
	if (k1->type < k2->type)
		return -1;
	if (k1->offset > k2->offset)
		return 1;
	if (k1->offset < k2->offset)
		return -1;
	return 0;
}

/*
 * this is used by the defrag code to go through all the
 * leaves pointed to by a node and reallocate them so that
 * disk order is close to key order
 */
int btrfs_realloc_node(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct extent_buffer *parent,
		       int start_slot, int cache_only, u64 *last_ret,
		       struct btrfs_key *progress)
{
	struct extent_buffer *cur;
	u64 blocknr;
	u64 gen;
	u64 search_start = *last_ret;
	u64 last_block = 0;
	u64 other;
	u32 parent_nritems;
	int end_slot;
	int i;
	int err = 0;
	int parent_level;
	int uptodate;
	u32 blocksize;
	int progress_passed = 0;
	struct btrfs_disk_key disk_key;

	parent_level = btrfs_header_level(parent);
	if (cache_only && parent_level != 1)
		return 0;

	if (trans->transaction != root->fs_info->running_transaction)
		WARN_ON(1);
	if (trans->transid != root->fs_info->generation)
		WARN_ON(1);

	parent_nritems = btrfs_header_nritems(parent);
	blocksize = btrfs_level_size(root, parent_level - 1);
	end_slot = parent_nritems;

	if (parent_nritems == 1)
		return 0;

	btrfs_set_lock_blocking(parent);

	for (i = start_slot; i < end_slot; i++) {
		int close = 1;

		if (!parent->map_token) {
			map_extent_buffer(parent,
					btrfs_node_key_ptr_offset(i),
					sizeof(struct btrfs_key_ptr),
					&parent->map_token, &parent->kaddr,
					&parent->map_start, &parent->map_len,
					KM_USER1);
		}
		btrfs_node_key(parent, &disk_key, i);
		if (!progress_passed && comp_keys(&disk_key, progress) < 0)
			continue;

		progress_passed = 1;
		blocknr = btrfs_node_blockptr(parent, i);
		gen = btrfs_node_ptr_generation(parent, i);
		if (last_block == 0)
			last_block = blocknr;

		if (i > 0) {
			other = btrfs_node_blockptr(parent, i - 1);
			close = close_blocks(blocknr, other, blocksize);
		}
		if (!close && i < end_slot - 2) {
			other = btrfs_node_blockptr(parent, i + 1);
			close = close_blocks(blocknr, other, blocksize);
		}
		if (close) {
			last_block = blocknr;
			continue;
		}
		if (parent->map_token) {
			unmap_extent_buffer(parent, parent->map_token,
					    KM_USER1);
			parent->map_token = NULL;
		}

		cur = btrfs_find_tree_block(root, blocknr, blocksize);
		if (cur)
			uptodate = btrfs_buffer_uptodate(cur, gen);
		else
			uptodate = 0;
		if (!cur || !uptodate) {
			if (cache_only) {
				free_extent_buffer(cur);
				continue;
			}
			if (!cur) {
				cur = read_tree_block(root, blocknr,
							 blocksize, gen);
			} else if (!uptodate) {
				btrfs_read_buffer(cur, gen);
			}
		}
		if (search_start == 0)
			search_start = last_block;

		btrfs_tree_lock(cur);
		btrfs_set_lock_blocking(cur);
		err = __btrfs_cow_block(trans, root, cur, parent, i,
					&cur, search_start,
					min(16 * blocksize,
					    (end_slot - i) * blocksize));
		if (err) {
			btrfs_tree_unlock(cur);
			free_extent_buffer(cur);
			break;
		}
		search_start = cur->start;
		last_block = cur->start;
		*last_ret = search_start;
		btrfs_tree_unlock(cur);
		free_extent_buffer(cur);
	}
	if (parent->map_token) {
		unmap_extent_buffer(parent, parent->map_token,
				    KM_USER1);
		parent->map_token = NULL;
	}
	return err;
}

/*
 * The leaf data grows from end-to-front in the node.
 * this returns the address of the start of the last item,
 * which is the stop of the leaf data stack
 */
static inline unsigned int leaf_data_end(struct btrfs_root *root,
					 struct extent_buffer *leaf)
{
	u32 nr = btrfs_header_nritems(leaf);
	if (nr == 0)
		return BTRFS_LEAF_DATA_SIZE(root);
	return btrfs_item_offset_nr(leaf, nr - 1);
}

/*
 * extra debugging checks to make sure all the items in a key are
 * well formed and in the proper order
 */
static int check_node(struct btrfs_root *root, struct btrfs_path *path,
		      int level)
{
	struct extent_buffer *parent = NULL;
	struct extent_buffer *node = path->nodes[level];
	struct btrfs_disk_key parent_key;
	struct btrfs_disk_key node_key;
	int parent_slot;
	int slot;
	struct btrfs_key cpukey;
	u32 nritems = btrfs_header_nritems(node);

	if (path->nodes[level + 1])
		parent = path->nodes[level + 1];

	slot = path->slots[level];
	BUG_ON(nritems == 0);
	if (parent) {
		parent_slot = path->slots[level + 1];
		btrfs_node_key(parent, &parent_key, parent_slot);
		btrfs_node_key(node, &node_key, 0);
		BUG_ON(memcmp(&parent_key, &node_key,
			      sizeof(struct btrfs_disk_key)));
		BUG_ON(btrfs_node_blockptr(parent, parent_slot) !=
		       btrfs_header_bytenr(node));
	}
	BUG_ON(nritems > BTRFS_NODEPTRS_PER_BLOCK(root));
	if (slot != 0) {
		btrfs_node_key_to_cpu(node, &cpukey, slot - 1);
		btrfs_node_key(node, &node_key, slot);
		BUG_ON(comp_keys(&node_key, &cpukey) <= 0);
	}
	if (slot < nritems - 1) {
		btrfs_node_key_to_cpu(node, &cpukey, slot + 1);
		btrfs_node_key(node, &node_key, slot);
		BUG_ON(comp_keys(&node_key, &cpukey) >= 0);
	}
	return 0;
}

/*
 * extra checking to make sure all the items in a leaf are
 * well formed and in the proper order
 */
static int check_leaf(struct btrfs_root *root, struct btrfs_path *path,
		      int level)
{
	struct extent_buffer *leaf = path->nodes[level];
	struct extent_buffer *parent = NULL;
	int parent_slot;
	struct btrfs_key cpukey;
	struct btrfs_disk_key parent_key;
	struct btrfs_disk_key leaf_key;
	int slot = path->slots[0];

	u32 nritems = btrfs_header_nritems(leaf);

	if (path->nodes[level + 1])
		parent = path->nodes[level + 1];

	if (nritems == 0)
		return 0;

	if (parent) {
		parent_slot = path->slots[level + 1];
		btrfs_node_key(parent, &parent_key, parent_slot);
		btrfs_item_key(leaf, &leaf_key, 0);

		BUG_ON(memcmp(&parent_key, &leaf_key,
		       sizeof(struct btrfs_disk_key)));
		BUG_ON(btrfs_node_blockptr(parent, parent_slot) !=
		       btrfs_header_bytenr(leaf));
	}
	if (slot != 0 && slot < nritems - 1) {
		btrfs_item_key(leaf, &leaf_key, slot);
		btrfs_item_key_to_cpu(leaf, &cpukey, slot - 1);
		if (comp_keys(&leaf_key, &cpukey) <= 0) {
			btrfs_print_leaf(root, leaf);
			printk(KERN_CRIT "slot %d offset bad key\n", slot);
			BUG_ON(1);
		}
		if (btrfs_item_offset_nr(leaf, slot - 1) !=
		       btrfs_item_end_nr(leaf, slot)) {
			btrfs_print_leaf(root, leaf);
			printk(KERN_CRIT "slot %d offset bad\n", slot);
			BUG_ON(1);
		}
	}
	if (slot < nritems - 1) {
		btrfs_item_key(leaf, &leaf_key, slot);
		btrfs_item_key_to_cpu(leaf, &cpukey, slot + 1);
		BUG_ON(comp_keys(&leaf_key, &cpukey) >= 0);
		if (btrfs_item_offset_nr(leaf, slot) !=
			btrfs_item_end_nr(leaf, slot + 1)) {
			btrfs_print_leaf(root, leaf);
			printk(KERN_CRIT "slot %d offset bad\n", slot);
			BUG_ON(1);
		}
	}
	BUG_ON(btrfs_item_offset_nr(leaf, 0) +
	       btrfs_item_size_nr(leaf, 0) != BTRFS_LEAF_DATA_SIZE(root));
	return 0;
}

static noinline int check_block(struct btrfs_root *root,
				struct btrfs_path *path, int level)
{
	return 0;
	if (level == 0)
		return check_leaf(root, path, level);
	return check_node(root, path, level);
}

/*
 * search for key in the extent_buffer.  The items start at offset p,
 * and they are item_size apart.  There are 'max' items in p.
 *
 * the slot in the array is returned via slot, and it points to
 * the place where you would insert key if it is not found in
 * the array.
 *
 * slot may point to max if the key is bigger than all of the keys
 */
static noinline int generic_bin_search(struct extent_buffer *eb,
				       unsigned long p,
				       int item_size, struct btrfs_key *key,
				       int max, int *slot)
{
	int low = 0;
	int high = max;
	int mid;
	int ret;
	struct btrfs_disk_key *tmp = NULL;
	struct btrfs_disk_key unaligned;
	unsigned long offset;
	char *map_token = NULL;
	char *kaddr = NULL;
	unsigned long map_start = 0;
	unsigned long map_len = 0;
	int err;

	while (low < high) {
		mid = (low + high) / 2;
		offset = p + mid * item_size;

		if (!map_token || offset < map_start ||
		    (offset + sizeof(struct btrfs_disk_key)) >
		    map_start + map_len) {
			if (map_token) {
				unmap_extent_buffer(eb, map_token, KM_USER0);
				map_token = NULL;
			}

			err = map_private_extent_buffer(eb, offset,
						sizeof(struct btrfs_disk_key),
						&map_token, &kaddr,
						&map_start, &map_len, KM_USER0);

			if (!err) {
				tmp = (struct btrfs_disk_key *)(kaddr + offset -
							map_start);
			} else {
				read_extent_buffer(eb, &unaligned,
						   offset, sizeof(unaligned));
				tmp = &unaligned;
			}

		} else {
			tmp = (struct btrfs_disk_key *)(kaddr + offset -
							map_start);
		}
		ret = comp_keys(tmp, key);

		if (ret < 0)
			low = mid + 1;
		else if (ret > 0)
			high = mid;
		else {
			*slot = mid;
			if (map_token)
				unmap_extent_buffer(eb, map_token, KM_USER0);
			return 0;
		}
	}
	*slot = low;
	if (map_token)
		unmap_extent_buffer(eb, map_token, KM_USER0);
	return 1;
}

/*
 * simple bin_search frontend that does the right thing for
 * leaves vs nodes
 */
static int bin_search(struct extent_buffer *eb, struct btrfs_key *key,
		      int level, int *slot)
{
	if (level == 0) {
		return generic_bin_search(eb,
					  offsetof(struct btrfs_leaf, items),
					  sizeof(struct btrfs_item),
					  key, btrfs_header_nritems(eb),
					  slot);
	} else {
		return generic_bin_search(eb,
					  offsetof(struct btrfs_node, ptrs),
					  sizeof(struct btrfs_key_ptr),
					  key, btrfs_header_nritems(eb),
					  slot);
	}
	return -1;
}

/* given a node and slot number, this reads the blocks it points to.  The
 * extent buffer is returned with a reference taken (but unlocked).
 * NULL is returned on error.
 */
static noinline struct extent_buffer *read_node_slot(struct btrfs_root *root,
				   struct extent_buffer *parent, int slot)
{
	int level = btrfs_header_level(parent);
	if (slot < 0)
		return NULL;
	if (slot >= btrfs_header_nritems(parent))
		return NULL;

	BUG_ON(level == 0);

	return read_tree_block(root, btrfs_node_blockptr(parent, slot),
		       btrfs_level_size(root, level - 1),
		       btrfs_node_ptr_generation(parent, slot));
}

/*
 * node level balancing, used to make sure nodes are in proper order for
 * item deletion.  We balance from the top down, so we have to make sure
 * that a deletion won't leave an node completely empty later on.
 */
static noinline int balance_level(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 struct btrfs_path *path, int level)
{
	struct extent_buffer *right = NULL;
	struct extent_buffer *mid;
	struct extent_buffer *left = NULL;
	struct extent_buffer *parent = NULL;
	int ret = 0;
	int wret;
	int pslot;
	int orig_slot = path->slots[level];
	int err_on_enospc = 0;
	u64 orig_ptr;

	if (level == 0)
		return 0;

	mid = path->nodes[level];

	WARN_ON(!path->locks[level]);
	WARN_ON(btrfs_header_generation(mid) != trans->transid);

	orig_ptr = btrfs_node_blockptr(mid, orig_slot);

	if (level < BTRFS_MAX_LEVEL - 1)
		parent = path->nodes[level + 1];
	pslot = path->slots[level + 1];

	/*
	 * deal with the case where there is only one pointer in the root
	 * by promoting the node below to a root
	 */
	if (!parent) {
		struct extent_buffer *child;

		if (btrfs_header_nritems(mid) != 1)
			return 0;

		/* promote the child to a root */
		child = read_node_slot(root, mid, 0);
		BUG_ON(!child);
		btrfs_tree_lock(child);
		btrfs_set_lock_blocking(child);
		ret = btrfs_cow_block(trans, root, child, mid, 0, &child);
		BUG_ON(ret);

		spin_lock(&root->node_lock);
		root->node = child;
		spin_unlock(&root->node_lock);

		ret = btrfs_update_extent_ref(trans, root, child->start,
					      child->len,
					      mid->start, child->start,
					      root->root_key.objectid,
					      trans->transid, level - 1);
		BUG_ON(ret);

		add_root_to_dirty_list(root);
		btrfs_tree_unlock(child);

		path->locks[level] = 0;
		path->nodes[level] = NULL;
		clean_tree_block(trans, root, mid);
		btrfs_tree_unlock(mid);
		/* once for the path */
		free_extent_buffer(mid);
		ret = btrfs_free_extent(trans, root, mid->start, mid->len,
					mid->start, root->root_key.objectid,
					btrfs_header_generation(mid),
					level, 1);
		/* once for the root ptr */
		free_extent_buffer(mid);
		return ret;
	}
	if (btrfs_header_nritems(mid) >
	    BTRFS_NODEPTRS_PER_BLOCK(root) / 4)
		return 0;

	if (trans->transaction->delayed_refs.flushing &&
	    btrfs_header_nritems(mid) > 2)
		return 0;

	if (btrfs_header_nritems(mid) < 2)
		err_on_enospc = 1;

	left = read_node_slot(root, parent, pslot - 1);
	if (left) {
		btrfs_tree_lock(left);
		btrfs_set_lock_blocking(left);
		wret = btrfs_cow_block(trans, root, left,
				       parent, pslot - 1, &left);
		if (wret) {
			ret = wret;
			goto enospc;
		}
	}
	right = read_node_slot(root, parent, pslot + 1);
	if (right) {
		btrfs_tree_lock(right);
		btrfs_set_lock_blocking(right);
		wret = btrfs_cow_block(trans, root, right,
				       parent, pslot + 1, &right);
		if (wret) {
			ret = wret;
			goto enospc;
		}
	}

	/* first, try to make some room in the middle buffer */
	if (left) {
		orig_slot += btrfs_header_nritems(left);
		wret = push_node_left(trans, root, left, mid, 1);
		if (wret < 0)
			ret = wret;
		if (btrfs_header_nritems(mid) < 2)
			err_on_enospc = 1;
	}

	/*
	 * then try to empty the right most buffer into the middle
	 */
	if (right) {
		wret = push_node_left(trans, root, mid, right, 1);
		if (wret < 0 && wret != -ENOSPC)
			ret = wret;
		if (btrfs_header_nritems(right) == 0) {
			u64 bytenr = right->start;
			u64 generation = btrfs_header_generation(parent);
			u32 blocksize = right->len;

			clean_tree_block(trans, root, right);
			btrfs_tree_unlock(right);
			free_extent_buffer(right);
			right = NULL;
			wret = del_ptr(trans, root, path, level + 1, pslot +
				       1);
			if (wret)
				ret = wret;
			wret = btrfs_free_extent(trans, root, bytenr,
						 blocksize, parent->start,
						 btrfs_header_owner(parent),
						 generation, level, 1);
			if (wret)
				ret = wret;
		} else {
			struct btrfs_disk_key right_key;
			btrfs_node_key(right, &right_key, 0);
			btrfs_set_node_key(parent, &right_key, pslot + 1);
			btrfs_mark_buffer_dirty(parent);
		}
	}
	if (btrfs_header_nritems(mid) == 1) {
		/*
		 * we're not allowed to leave a node with one item in the
		 * tree during a delete.  A deletion from lower in the tree
		 * could try to delete the only pointer in this node.
		 * So, pull some keys from the left.
		 * There has to be a left pointer at this point because
		 * otherwise we would have pulled some pointers from the
		 * right
		 */
		BUG_ON(!left);
		wret = balance_node_right(trans, root, mid, left);
		if (wret < 0) {
			ret = wret;
			goto enospc;
		}
		if (wret == 1) {
			wret = push_node_left(trans, root, left, mid, 1);
			if (wret < 0)
				ret = wret;
		}
		BUG_ON(wret == 1);
	}
	if (btrfs_header_nritems(mid) == 0) {
		/* we've managed to empty the middle node, drop it */
		u64 root_gen = btrfs_header_generation(parent);
		u64 bytenr = mid->start;
		u32 blocksize = mid->len;

		clean_tree_block(trans, root, mid);
		btrfs_tree_unlock(mid);
		free_extent_buffer(mid);
		mid = NULL;
		wret = del_ptr(trans, root, path, level + 1, pslot);
		if (wret)
			ret = wret;
		wret = btrfs_free_extent(trans, root, bytenr, blocksize,
					 parent->start,
					 btrfs_header_owner(parent),
					 root_gen, level, 1);
		if (wret)
			ret = wret;
	} else {
		/* update the parent key to reflect our changes */
		struct btrfs_disk_key mid_key;
		btrfs_node_key(mid, &mid_key, 0);
		btrfs_set_node_key(parent, &mid_key, pslot);
		btrfs_mark_buffer_dirty(parent);
	}

	/* update the path */
	if (left) {
		if (btrfs_header_nritems(left) > orig_slot) {
			extent_buffer_get(left);
			/* left was locked after cow */
			path->nodes[level] = left;
			path->slots[level + 1] -= 1;
			path->slots[level] = orig_slot;
			if (mid) {
				btrfs_tree_unlock(mid);
				free_extent_buffer(mid);
			}
		} else {
			orig_slot -= btrfs_header_nritems(left);
			path->slots[level] = orig_slot;
		}
	}
	/* double check we haven't messed things up */
	check_block(root, path, level);
	if (orig_ptr !=
	    btrfs_node_blockptr(path->nodes[level], path->slots[level]))
		BUG();
enospc:
	if (right) {
		btrfs_tree_unlock(right);
		free_extent_buffer(right);
	}
	if (left) {
		if (path->nodes[level] != left)
			btrfs_tree_unlock(left);
		free_extent_buffer(left);
	}
	return ret;
}

/* Node balancing for insertion.  Here we only split or push nodes around
 * when they are completely full.  This is also done top down, so we
 * have to be pessimistic.
 */
static noinline int push_nodes_for_insert(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path, int level)
{
	struct extent_buffer *right = NULL;
	struct extent_buffer *mid;
	struct extent_buffer *left = NULL;
	struct extent_buffer *parent = NULL;
	int ret = 0;
	int wret;
	int pslot;
	int orig_slot = path->slots[level];
	u64 orig_ptr;

	if (level == 0)
		return 1;

	mid = path->nodes[level];
	WARN_ON(btrfs_header_generation(mid) != trans->transid);
	orig_ptr = btrfs_node_blockptr(mid, orig_slot);

	if (level < BTRFS_MAX_LEVEL - 1)
		parent = path->nodes[level + 1];
	pslot = path->slots[level + 1];

	if (!parent)
		return 1;

	left = read_node_slot(root, parent, pslot - 1);

	/* first, try to make some room in the middle buffer */
	if (left) {
		u32 left_nr;

		btrfs_tree_lock(left);
		btrfs_set_lock_blocking(left);

		left_nr = btrfs_header_nritems(left);
		if (left_nr >= BTRFS_NODEPTRS_PER_BLOCK(root) - 1) {
			wret = 1;
		} else {
			ret = btrfs_cow_block(trans, root, left, parent,
					      pslot - 1, &left);
			if (ret)
				wret = 1;
			else {
				wret = push_node_left(trans, root,
						      left, mid, 0);
			}
		}
		if (wret < 0)
			ret = wret;
		if (wret == 0) {
			struct btrfs_disk_key disk_key;
			orig_slot += left_nr;
			btrfs_node_key(mid, &disk_key, 0);
			btrfs_set_node_key(parent, &disk_key, pslot);
			btrfs_mark_buffer_dirty(parent);
			if (btrfs_header_nritems(left) > orig_slot) {
				path->nodes[level] = left;
				path->slots[level + 1] -= 1;
				path->slots[level] = orig_slot;
				btrfs_tree_unlock(mid);
				free_extent_buffer(mid);
			} else {
				orig_slot -=
					btrfs_header_nritems(left);
				path->slots[level] = orig_slot;
				btrfs_tree_unlock(left);
				free_extent_buffer(left);
			}
			return 0;
		}
		btrfs_tree_unlock(left);
		free_extent_buffer(left);
	}
	right = read_node_slot(root, parent, pslot + 1);

	/*
	 * then try to empty the right most buffer into the middle
	 */
	if (right) {
		u32 right_nr;

		btrfs_tree_lock(right);
		btrfs_set_lock_blocking(right);

		right_nr = btrfs_header_nritems(right);
		if (right_nr >= BTRFS_NODEPTRS_PER_BLOCK(root) - 1) {
			wret = 1;
		} else {
			ret = btrfs_cow_block(trans, root, right,
					      parent, pslot + 1,
					      &right);
			if (ret)
				wret = 1;
			else {
				wret = balance_node_right(trans, root,
							  right, mid);
			}
		}
		if (wret < 0)
			ret = wret;
		if (wret == 0) {
			struct btrfs_disk_key disk_key;

			btrfs_node_key(right, &disk_key, 0);
			btrfs_set_node_key(parent, &disk_key, pslot + 1);
			btrfs_mark_buffer_dirty(parent);

			if (btrfs_header_nritems(mid) <= orig_slot) {
				path->nodes[level] = right;
				path->slots[level + 1] += 1;
				path->slots[level] = orig_slot -
					btrfs_header_nritems(mid);
				btrfs_tree_unlock(mid);
				free_extent_buffer(mid);
			} else {
				btrfs_tree_unlock(right);
				free_extent_buffer(right);
			}
			return 0;
		}
		btrfs_tree_unlock(right);
		free_extent_buffer(right);
	}
	return 1;
}

/*
 * readahead one full node of leaves, finding things that are close
 * to the block in 'slot', and triggering ra on them.
 */
static void reada_for_search(struct btrfs_root *root,
			     struct btrfs_path *path,
			     int level, int slot, u64 objectid)
{
	struct extent_buffer *node;
	struct btrfs_disk_key disk_key;
	u32 nritems;
	u64 search;
	u64 target;
	u64 nread = 0;
	int direction = path->reada;
	struct extent_buffer *eb;
	u32 nr;
	u32 blocksize;
	u32 nscan = 0;

	if (level != 1)
		return;

	if (!path->nodes[level])
		return;

	node = path->nodes[level];

	search = btrfs_node_blockptr(node, slot);
	blocksize = btrfs_level_size(root, level - 1);
	eb = btrfs_find_tree_block(root, search, blocksize);
	if (eb) {
		free_extent_buffer(eb);
		return;
	}

	target = search;

	nritems = btrfs_header_nritems(node);
	nr = slot;
	while (1) {
		if (direction < 0) {
			if (nr == 0)
				break;
			nr--;
		} else if (direction > 0) {
			nr++;
			if (nr >= nritems)
				break;
		}
		if (path->reada < 0 && objectid) {
			btrfs_node_key(node, &disk_key, nr);
			if (btrfs_disk_key_objectid(&disk_key) != objectid)
				break;
		}
		search = btrfs_node_blockptr(node, nr);
		if ((search <= target && target - search <= 65536) ||
		    (search > target && search - target <= 65536)) {
			readahead_tree_block(root, search, blocksize,
				     btrfs_node_ptr_generation(node, nr));
			nread += blocksize;
		}
		nscan++;
		if ((nread > 65536 || nscan > 32))
			break;
	}
}

/*
 * returns -EAGAIN if it had to drop the path, or zero if everything was in
 * cache
 */
static noinline int reada_for_balance(struct btrfs_root *root,
				      struct btrfs_path *path, int level)
{
	int slot;
	int nritems;
	struct extent_buffer *parent;
	struct extent_buffer *eb;
	u64 gen;
	u64 block1 = 0;
	u64 block2 = 0;
	int ret = 0;
	int blocksize;

	parent = path->nodes[level - 1];
	if (!parent)
		return 0;

	nritems = btrfs_header_nritems(parent);
	slot = path->slots[level];
	blocksize = btrfs_level_size(root, level);

	if (slot > 0) {
		block1 = btrfs_node_blockptr(parent, slot - 1);
		gen = btrfs_node_ptr_generation(parent, slot - 1);
		eb = btrfs_find_tree_block(root, block1, blocksize);
		if (eb && btrfs_buffer_uptodate(eb, gen))
			block1 = 0;
		free_extent_buffer(eb);
	}
	if (slot < nritems) {
		block2 = btrfs_node_blockptr(parent, slot + 1);
		gen = btrfs_node_ptr_generation(parent, slot + 1);
		eb = btrfs_find_tree_block(root, block2, blocksize);
		if (eb && btrfs_buffer_uptodate(eb, gen))
			block2 = 0;
		free_extent_buffer(eb);
	}
	if (block1 || block2) {
		ret = -EAGAIN;
		btrfs_release_path(root, path);
		if (block1)
			readahead_tree_block(root, block1, blocksize, 0);
		if (block2)
			readahead_tree_block(root, block2, blocksize, 0);

		if (block1) {
			eb = read_tree_block(root, block1, blocksize, 0);
			free_extent_buffer(eb);
		}
		if (block1) {
			eb = read_tree_block(root, block2, blocksize, 0);
			free_extent_buffer(eb);
		}
	}
	return ret;
}


/*
 * when we walk down the tree, it is usually safe to unlock the higher layers
 * in the tree.  The exceptions are when our path goes through slot 0, because
 * operations on the tree might require changing key pointers higher up in the
 * tree.
 *
 * callers might also have set path->keep_locks, which tells this code to keep
 * the lock if the path points to the last slot in the block.  This is part of
 * walking through the tree, and selecting the next slot in the higher block.
 *
 * lowest_unlock sets the lowest level in the tree we're allowed to unlock.  so
 * if lowest_unlock is 1, level 0 won't be unlocked
 */
static noinline void unlock_up(struct btrfs_path *path, int level,
			       int lowest_unlock)
{
	int i;
	int skip_level = level;
	int no_skips = 0;
	struct extent_buffer *t;

	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		if (!path->nodes[i])
			break;
		if (!path->locks[i])
			break;
		if (!no_skips && path->slots[i] == 0) {
			skip_level = i + 1;
			continue;
		}
		if (!no_skips && path->keep_locks) {
			u32 nritems;
			t = path->nodes[i];
			nritems = btrfs_header_nritems(t);
			if (nritems < 1 || path->slots[i] >= nritems - 1) {
				skip_level = i + 1;
				continue;
			}
		}
		if (skip_level < i && i >= lowest_unlock)
			no_skips = 1;

		t = path->nodes[i];
		if (i >= lowest_unlock && i > skip_level && path->locks[i]) {
			btrfs_tree_unlock(t);
			path->locks[i] = 0;
		}
	}
}

/*
 * This releases any locks held in the path starting at level and
 * going all the way up to the root.
 *
 * btrfs_search_slot will keep the lock held on higher nodes in a few
 * corner cases, such as COW of the block at slot zero in the node.  This
 * ignores those rules, and it should only be called when there are no
 * more updates to be done higher up in the tree.
 */
noinline void btrfs_unlock_up_safe(struct btrfs_path *path, int level)
{
	int i;

	if (path->keep_locks || path->lowest_level)
		return;

	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		if (!path->nodes[i])
			continue;
		if (!path->locks[i])
			continue;
		btrfs_tree_unlock(path->nodes[i]);
		path->locks[i] = 0;
	}
}

/*
 * helper function for btrfs_search_slot.  The goal is to find a block
 * in cache without setting the path to blocking.  If we find the block
 * we return zero and the path is unchanged.
 *
 * If we can't find the block, we set the path blocking and do some
 * reada.  -EAGAIN is returned and the search must be repeated.
 */
static int
read_block_for_search(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct btrfs_path *p,
		       struct extent_buffer **eb_ret, int level, int slot,
		       struct btrfs_key *key)
{
	u64 blocknr;
	u64 gen;
	u32 blocksize;
	struct extent_buffer *b = *eb_ret;
	struct extent_buffer *tmp;

	blocknr = btrfs_node_blockptr(b, slot);
	gen = btrfs_node_ptr_generation(b, slot);
	blocksize = btrfs_level_size(root, level - 1);

	tmp = btrfs_find_tree_block(root, blocknr, blocksize);
	if (tmp && btrfs_buffer_uptodate(tmp, gen)) {
		*eb_ret = tmp;
		return 0;
	}

	/*
	 * reduce lock contention at high levels
	 * of the btree by dropping locks before
	 * we read.
	 */
	btrfs_release_path(NULL, p);
	if (tmp)
		free_extent_buffer(tmp);
	if (p->reada)
		reada_for_search(root, p, level, slot, key->objectid);

	tmp = read_tree_block(root, blocknr, blocksize, gen);
	if (tmp)
		free_extent_buffer(tmp);
	return -EAGAIN;
}

/*
 * helper function for btrfs_search_slot.  This does all of the checks
 * for node-level blocks and does any balancing required based on
 * the ins_len.
 *
 * If no extra work was required, zero is returned.  If we had to
 * drop the path, -EAGAIN is returned and btrfs_search_slot must
 * start over
 */
static int
setup_nodes_for_search(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct btrfs_path *p,
		       struct extent_buffer *b, int level, int ins_len)
{
	int ret;
	if ((p->search_for_split || ins_len > 0) && btrfs_header_nritems(b) >=
	    BTRFS_NODEPTRS_PER_BLOCK(root) - 3) {
		int sret;

		sret = reada_for_balance(root, p, level);
		if (sret)
			goto again;

		btrfs_set_path_blocking(p);
		sret = split_node(trans, root, p, level);
		btrfs_clear_path_blocking(p, NULL);

		BUG_ON(sret > 0);
		if (sret) {
			ret = sret;
			goto done;
		}
		b = p->nodes[level];
	} else if (ins_len < 0 && btrfs_header_nritems(b) <
		   BTRFS_NODEPTRS_PER_BLOCK(root) / 4) {
		int sret;

		sret = reada_for_balance(root, p, level);
		if (sret)
			goto again;

		btrfs_set_path_blocking(p);
		sret = balance_level(trans, root, p, level);
		btrfs_clear_path_blocking(p, NULL);

		if (sret) {
			ret = sret;
			goto done;
		}
		b = p->nodes[level];
		if (!b) {
			btrfs_release_path(NULL, p);
			goto again;
		}
		BUG_ON(btrfs_header_nritems(b) == 1);
	}
	return 0;

again:
	ret = -EAGAIN;
done:
	return ret;
}

/*
 * look for key in the tree.  path is filled in with nodes along the way
 * if key is found, we return zero and you can find the item in the leaf
 * level of the path (level 0)
 *
 * If the key isn't found, the path points to the slot where it should
 * be inserted, and 1 is returned.  If there are other errors during the
 * search a negative error number is returned.
 *
 * if ins_len > 0, nodes and leaves will be split as we walk down the
 * tree.  if ins_len < 0, nodes will be merged as we walk down the tree (if
 * possible)
 */
int btrfs_search_slot(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_path *p, int
		      ins_len, int cow)
{
	struct extent_buffer *b;
	int slot;
	int ret;
	int level;
	int lowest_unlock = 1;
	u8 lowest_level = 0;

	lowest_level = p->lowest_level;
	WARN_ON(lowest_level && ins_len > 0);
	WARN_ON(p->nodes[0] != NULL);

	if (ins_len < 0)
		lowest_unlock = 2;

again:
	if (p->skip_locking)
		b = btrfs_root_node(root);
	else
		b = btrfs_lock_root_node(root);

	while (b) {
		level = btrfs_header_level(b);

		/*
		 * setup the path here so we can release it under lock
		 * contention with the cow code
		 */
		p->nodes[level] = b;
		if (!p->skip_locking)
			p->locks[level] = 1;

		if (cow) {
			int wret;

			/*
			 * if we don't really need to cow this block
			 * then we don't want to set the path blocking,
			 * so we test it here
			 */
			if (btrfs_header_generation(b) == trans->transid &&
			    btrfs_header_owner(b) == root->root_key.objectid &&
			    !btrfs_header_flag(b, BTRFS_HEADER_FLAG_WRITTEN)) {
				goto cow_done;
			}
			btrfs_set_path_blocking(p);

			wret = btrfs_cow_block(trans, root, b,
					       p->nodes[level + 1],
					       p->slots[level + 1], &b);
			if (wret) {
				free_extent_buffer(b);
				ret = wret;
				goto done;
			}
		}
cow_done:
		BUG_ON(!cow && ins_len);
		if (level != btrfs_header_level(b))
			WARN_ON(1);
		level = btrfs_header_level(b);

		p->nodes[level] = b;
		if (!p->skip_locking)
			p->locks[level] = 1;

		btrfs_clear_path_blocking(p, NULL);

		/*
		 * we have a lock on b and as long as we aren't changing
		 * the tree, there is no way to for the items in b to change.
		 * It is safe to drop the lock on our parent before we
		 * go through the expensive btree search on b.
		 *
		 * If cow is true, then we might be changing slot zero,
		 * which may require changing the parent.  So, we can't
		 * drop the lock until after we know which slot we're
		 * operating on.
		 */
		if (!cow)
			btrfs_unlock_up_safe(p, level + 1);

		ret = check_block(root, p, level);
		if (ret) {
			ret = -1;
			goto done;
		}

		ret = bin_search(b, key, level, &slot);

		if (level != 0) {
			if (ret && slot > 0)
				slot -= 1;
			p->slots[level] = slot;
			ret = setup_nodes_for_search(trans, root, p, b, level,
						     ins_len);
			if (ret == -EAGAIN)
				goto again;
			else if (ret)
				goto done;
			b = p->nodes[level];
			slot = p->slots[level];

			unlock_up(p, level, lowest_unlock);

			/* this is only true while dropping a snapshot */
			if (level == lowest_level) {
				ret = 0;
				goto done;
			}

			ret = read_block_for_search(trans, root, p,
						    &b, level, slot, key);
			if (ret == -EAGAIN)
				goto again;

			if (!p->skip_locking) {
				int lret;

				btrfs_clear_path_blocking(p, NULL);
				lret = btrfs_try_spin_lock(b);

				if (!lret) {
					btrfs_set_path_blocking(p);
					btrfs_tree_lock(b);
					btrfs_clear_path_blocking(p, b);
				}
			}
		} else {
			p->slots[level] = slot;
			if (ins_len > 0 &&
			    btrfs_leaf_free_space(root, b) < ins_len) {
				int sret;

				btrfs_set_path_blocking(p);
				sret = split_leaf(trans, root, key,
						      p, ins_len, ret == 0);
				btrfs_clear_path_blocking(p, NULL);

				BUG_ON(sret > 0);
				if (sret) {
					ret = sret;
					goto done;
				}
			}
			if (!p->search_for_split)
				unlock_up(p, level, lowest_unlock);
			goto done;
		}
	}
	ret = 1;
done:
	/*
	 * we don't really know what they plan on doing with the path
	 * from here on, so for now just mark it as blocking
	 */
	if (!p->leave_spinning)
		btrfs_set_path_blocking(p);
	return ret;
}

int btrfs_merge_path(struct btrfs_trans_handle *trans,
		     struct btrfs_root *root,
		     struct btrfs_key *node_keys,
		     u64 *nodes, int lowest_level)
{
	struct extent_buffer *eb;
	struct extent_buffer *parent;
	struct btrfs_key key;
	u64 bytenr;
	u64 generation;
	u32 blocksize;
	int level;
	int slot;
	int key_match;
	int ret;

	eb = btrfs_lock_root_node(root);
	ret = btrfs_cow_block(trans, root, eb, NULL, 0, &eb);
	BUG_ON(ret);

	btrfs_set_lock_blocking(eb);

	parent = eb;
	while (1) {
		level = btrfs_header_level(parent);
		if (level == 0 || level <= lowest_level)
			break;

		ret = bin_search(parent, &node_keys[lowest_level], level,
				 &slot);
		if (ret && slot > 0)
			slot--;

		bytenr = btrfs_node_blockptr(parent, slot);
		if (nodes[level - 1] == bytenr)
			break;

		blocksize = btrfs_level_size(root, level - 1);
		generation = btrfs_node_ptr_generation(parent, slot);
		btrfs_node_key_to_cpu(eb, &key, slot);
		key_match = !memcmp(&key, &node_keys[level - 1], sizeof(key));

		if (generation == trans->transid) {
			eb = read_tree_block(root, bytenr, blocksize,
					     generation);
			btrfs_tree_lock(eb);
			btrfs_set_lock_blocking(eb);
		}

		/*
		 * if node keys match and node pointer hasn't been modified
		 * in the running transaction, we can merge the path. for
		 * blocks owened by reloc trees, the node pointer check is
		 * skipped, this is because these blocks are fully controlled
		 * by the space balance code, no one else can modify them.
		 */
		if (!nodes[level - 1] || !key_match ||
		    (generation == trans->transid &&
		     btrfs_header_owner(eb) != BTRFS_TREE_RELOC_OBJECTID)) {
			if (level == 1 || level == lowest_level + 1) {
				if (generation == trans->transid) {
					btrfs_tree_unlock(eb);
					free_extent_buffer(eb);
				}
				break;
			}

			if (generation != trans->transid) {
				eb = read_tree_block(root, bytenr, blocksize,
						generation);
				btrfs_tree_lock(eb);
				btrfs_set_lock_blocking(eb);
			}

			ret = btrfs_cow_block(trans, root, eb, parent, slot,
					      &eb);
			BUG_ON(ret);

			if (root->root_key.objectid ==
			    BTRFS_TREE_RELOC_OBJECTID) {
				if (!nodes[level - 1]) {
					nodes[level - 1] = eb->start;
					memcpy(&node_keys[level - 1], &key,
					       sizeof(node_keys[0]));
				} else {
					WARN_ON(1);
				}
			}

			btrfs_tree_unlock(parent);
			free_extent_buffer(parent);
			parent = eb;
			continue;
		}

		btrfs_set_node_blockptr(parent, slot, nodes[level - 1]);
		btrfs_set_node_ptr_generation(parent, slot, trans->transid);
		btrfs_mark_buffer_dirty(parent);

		ret = btrfs_inc_extent_ref(trans, root,
					nodes[level - 1],
					blocksize, parent->start,
					btrfs_header_owner(parent),
					btrfs_header_generation(parent),
					level - 1);
		BUG_ON(ret);

		/*
		 * If the block was created in the running transaction,
		 * it's possible this is the last reference to it, so we
		 * should drop the subtree.
		 */
		if (generation == trans->transid) {
			ret = btrfs_drop_subtree(trans, root, eb, parent);
			BUG_ON(ret);
			btrfs_tree_unlock(eb);
			free_extent_buffer(eb);
		} else {
			ret = btrfs_free_extent(trans, root, bytenr,
					blocksize, parent->start,
					btrfs_header_owner(parent),
					btrfs_header_generation(parent),
					level - 1, 1);
			BUG_ON(ret);
		}
		break;
	}
	btrfs_tree_unlock(parent);
	free_extent_buffer(parent);
	return 0;
}

/*
 * adjust the pointers going up the tree, starting at level
 * making sure the right key of each node is points to 'key'.
 * This is used after shifting pointers to the left, so it stops
 * fixing up pointers when a given leaf/node is not in slot 0 of the
 * higher levels
 *
 * If this fails to write a tree block, it returns -1, but continues
 * fixing up the blocks in ram so the tree is consistent.
 */
static int fixup_low_keys(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct btrfs_path *path,
			  struct btrfs_disk_key *key, int level)
{
	int i;
	int ret = 0;
	struct extent_buffer *t;

	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		int tslot = path->slots[i];
		if (!path->nodes[i])
			break;
		t = path->nodes[i];
		btrfs_set_node_key(t, key, tslot);
		btrfs_mark_buffer_dirty(path->nodes[i]);
		if (tslot != 0)
			break;
	}
	return ret;
}

/*
 * update item key.
 *
 * This function isn't completely safe. It's the caller's responsibility
 * that the new key won't break the order
 */
int btrfs_set_item_key_safe(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root, struct btrfs_path *path,
			    struct btrfs_key *new_key)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *eb;
	int slot;

	eb = path->nodes[0];
	slot = path->slots[0];
	if (slot > 0) {
		btrfs_item_key(eb, &disk_key, slot - 1);
		if (comp_keys(&disk_key, new_key) >= 0)
			return -1;
	}
	if (slot < btrfs_header_nritems(eb) - 1) {
		btrfs_item_key(eb, &disk_key, slot + 1);
		if (comp_keys(&disk_key, new_key) <= 0)
			return -1;
	}

	btrfs_cpu_key_to_disk(&disk_key, new_key);
	btrfs_set_item_key(eb, &disk_key, slot);
	btrfs_mark_buffer_dirty(eb);
	if (slot == 0)
		fixup_low_keys(trans, root, path, &disk_key, 1);
	return 0;
}

/*
 * try to push data from one node into the next node left in the
 * tree.
 *
 * returns 0 if some ptrs were pushed left, < 0 if there was some horrible
 * error, and > 0 if there was no room in the left hand block.
 */
static int push_node_left(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct extent_buffer *dst,
			  struct extent_buffer *src, int empty)
{
	int push_items = 0;
	int src_nritems;
	int dst_nritems;
	int ret = 0;

	src_nritems = btrfs_header_nritems(src);
	dst_nritems = btrfs_header_nritems(dst);
	push_items = BTRFS_NODEPTRS_PER_BLOCK(root) - dst_nritems;
	WARN_ON(btrfs_header_generation(src) != trans->transid);
	WARN_ON(btrfs_header_generation(dst) != trans->transid);

	if (!empty && src_nritems <= 8)
		return 1;

	if (push_items <= 0)
		return 1;

	if (empty) {
		push_items = min(src_nritems, push_items);
		if (push_items < src_nritems) {
			/* leave at least 8 pointers in the node if
			 * we aren't going to empty it
			 */
			if (src_nritems - push_items < 8) {
				if (push_items <= 8)
					return 1;
				push_items -= 8;
			}
		}
	} else
		push_items = min(src_nritems - 8, push_items);

	copy_extent_buffer(dst, src,
			   btrfs_node_key_ptr_offset(dst_nritems),
			   btrfs_node_key_ptr_offset(0),
			   push_items * sizeof(struct btrfs_key_ptr));

	if (push_items < src_nritems) {
		memmove_extent_buffer(src, btrfs_node_key_ptr_offset(0),
				      btrfs_node_key_ptr_offset(push_items),
				      (src_nritems - push_items) *
				      sizeof(struct btrfs_key_ptr));
	}
	btrfs_set_header_nritems(src, src_nritems - push_items);
	btrfs_set_header_nritems(dst, dst_nritems + push_items);
	btrfs_mark_buffer_dirty(src);
	btrfs_mark_buffer_dirty(dst);

	ret = btrfs_update_ref(trans, root, src, dst, dst_nritems, push_items);
	BUG_ON(ret);

	return ret;
}

/*
 * try to push data from one node into the next node right in the
 * tree.
 *
 * returns 0 if some ptrs were pushed, < 0 if there was some horrible
 * error, and > 0 if there was no room in the right hand block.
 *
 * this will  only push up to 1/2 the contents of the left node over
 */
static int balance_node_right(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct extent_buffer *dst,
			      struct extent_buffer *src)
{
	int push_items = 0;
	int max_push;
	int src_nritems;
	int dst_nritems;
	int ret = 0;

	WARN_ON(btrfs_header_generation(src) != trans->transid);
	WARN_ON(btrfs_header_generation(dst) != trans->transid);

	src_nritems = btrfs_header_nritems(src);
	dst_nritems = btrfs_header_nritems(dst);
	push_items = BTRFS_NODEPTRS_PER_BLOCK(root) - dst_nritems;
	if (push_items <= 0)
		return 1;

	if (src_nritems < 4)
		return 1;

	max_push = src_nritems / 2 + 1;
	/* don't try to empty the node */
	if (max_push >= src_nritems)
		return 1;

	if (max_push < push_items)
		push_items = max_push;

	memmove_extent_buffer(dst, btrfs_node_key_ptr_offset(push_items),
				      btrfs_node_key_ptr_offset(0),
				      (dst_nritems) *
				      sizeof(struct btrfs_key_ptr));

	copy_extent_buffer(dst, src,
			   btrfs_node_key_ptr_offset(0),
			   btrfs_node_key_ptr_offset(src_nritems - push_items),
			   push_items * sizeof(struct btrfs_key_ptr));

	btrfs_set_header_nritems(src, src_nritems - push_items);
	btrfs_set_header_nritems(dst, dst_nritems + push_items);

	btrfs_mark_buffer_dirty(src);
	btrfs_mark_buffer_dirty(dst);

	ret = btrfs_update_ref(trans, root, src, dst, 0, push_items);
	BUG_ON(ret);

	return ret;
}

/*
 * helper function to insert a new root level in the tree.
 * A new node is allocated, and a single item is inserted to
 * point to the existing root
 *
 * returns zero on success or < 0 on failure.
 */
static noinline int insert_new_root(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct btrfs_path *path, int level)
{
	u64 lower_gen;
	struct extent_buffer *lower;
	struct extent_buffer *c;
	struct extent_buffer *old;
	struct btrfs_disk_key lower_key;
	int ret;

	BUG_ON(path->nodes[level]);
	BUG_ON(path->nodes[level-1] != root->node);

	lower = path->nodes[level-1];
	if (level == 1)
		btrfs_item_key(lower, &lower_key, 0);
	else
		btrfs_node_key(lower, &lower_key, 0);

	c = btrfs_alloc_free_block(trans, root, root->nodesize, 0,
				   root->root_key.objectid, trans->transid,
				   level, root->node->start, 0);
	if (IS_ERR(c))
		return PTR_ERR(c);

	memset_extent_buffer(c, 0, 0, root->nodesize);
	btrfs_set_header_nritems(c, 1);
	btrfs_set_header_level(c, level);
	btrfs_set_header_bytenr(c, c->start);
	btrfs_set_header_generation(c, trans->transid);
	btrfs_set_header_owner(c, root->root_key.objectid);

	write_extent_buffer(c, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(c),
			    BTRFS_FSID_SIZE);

	write_extent_buffer(c, root->fs_info->chunk_tree_uuid,
			    (unsigned long)btrfs_header_chunk_tree_uuid(c),
			    BTRFS_UUID_SIZE);

	btrfs_set_node_key(c, &lower_key, 0);
	btrfs_set_node_blockptr(c, 0, lower->start);
	lower_gen = btrfs_header_generation(lower);
	WARN_ON(lower_gen != trans->transid);

	btrfs_set_node_ptr_generation(c, 0, lower_gen);

	btrfs_mark_buffer_dirty(c);

	spin_lock(&root->node_lock);
	old = root->node;
	root->node = c;
	spin_unlock(&root->node_lock);

	ret = btrfs_update_extent_ref(trans, root, lower->start,
				      lower->len, lower->start, c->start,
				      root->root_key.objectid,
				      trans->transid, level - 1);
	BUG_ON(ret);

	/* the super has an extra ref to root->node */
	free_extent_buffer(old);

	add_root_to_dirty_list(root);
	extent_buffer_get(c);
	path->nodes[level] = c;
	path->locks[level] = 1;
	path->slots[level] = 0;
	return 0;
}

/*
 * worker function to insert a single pointer in a node.
 * the node should have enough room for the pointer already
 *
 * slot and level indicate where you want the key to go, and
 * blocknr is the block the key points to.
 *
 * returns zero on success and < 0 on any error
 */
static int insert_ptr(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, struct btrfs_disk_key
		      *key, u64 bytenr, int slot, int level)
{
	struct extent_buffer *lower;
	int nritems;

	BUG_ON(!path->nodes[level]);
	lower = path->nodes[level];
	nritems = btrfs_header_nritems(lower);
	BUG_ON(slot > nritems);
	if (nritems == BTRFS_NODEPTRS_PER_BLOCK(root))
		BUG();
	if (slot != nritems) {
		memmove_extent_buffer(lower,
			      btrfs_node_key_ptr_offset(slot + 1),
			      btrfs_node_key_ptr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_key_ptr));
	}
	btrfs_set_node_key(lower, key, slot);
	btrfs_set_node_blockptr(lower, slot, bytenr);
	WARN_ON(trans->transid == 0);
	btrfs_set_node_ptr_generation(lower, slot, trans->transid);
	btrfs_set_header_nritems(lower, nritems + 1);
	btrfs_mark_buffer_dirty(lower);
	return 0;
}

/*
 * split the node at the specified level in path in two.
 * The path is corrected to point to the appropriate node after the split
 *
 * Before splitting this tries to make some room in the node by pushing
 * left and right, if either one works, it returns right away.
 *
 * returns 0 on success and < 0 on failure
 */
static noinline int split_node(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_path *path, int level)
{
	struct extent_buffer *c;
	struct extent_buffer *split;
	struct btrfs_disk_key disk_key;
	int mid;
	int ret;
	int wret;
	u32 c_nritems;

	c = path->nodes[level];
	WARN_ON(btrfs_header_generation(c) != trans->transid);
	if (c == root->node) {
		/* trying to split the root, lets make a new one */
		ret = insert_new_root(trans, root, path, level + 1);
		if (ret)
			return ret;
	} else if (!trans->transaction->delayed_refs.flushing) {
		ret = push_nodes_for_insert(trans, root, path, level);
		c = path->nodes[level];
		if (!ret && btrfs_header_nritems(c) <
		    BTRFS_NODEPTRS_PER_BLOCK(root) - 3)
			return 0;
		if (ret < 0)
			return ret;
	}

	c_nritems = btrfs_header_nritems(c);

	split = btrfs_alloc_free_block(trans, root, root->nodesize,
					path->nodes[level + 1]->start,
					root->root_key.objectid,
					trans->transid, level, c->start, 0);
	if (IS_ERR(split))
		return PTR_ERR(split);

	btrfs_set_header_flags(split, btrfs_header_flags(c));
	btrfs_set_header_level(split, btrfs_header_level(c));
	btrfs_set_header_bytenr(split, split->start);
	btrfs_set_header_generation(split, trans->transid);
	btrfs_set_header_owner(split, root->root_key.objectid);
	btrfs_set_header_flags(split, 0);
	write_extent_buffer(split, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(split),
			    BTRFS_FSID_SIZE);
	write_extent_buffer(split, root->fs_info->chunk_tree_uuid,
			    (unsigned long)btrfs_header_chunk_tree_uuid(split),
			    BTRFS_UUID_SIZE);

	mid = (c_nritems + 1) / 2;

	copy_extent_buffer(split, c,
			   btrfs_node_key_ptr_offset(0),
			   btrfs_node_key_ptr_offset(mid),
			   (c_nritems - mid) * sizeof(struct btrfs_key_ptr));
	btrfs_set_header_nritems(split, c_nritems - mid);
	btrfs_set_header_nritems(c, mid);
	ret = 0;

	btrfs_mark_buffer_dirty(c);
	btrfs_mark_buffer_dirty(split);

	btrfs_node_key(split, &disk_key, 0);
	wret = insert_ptr(trans, root, path, &disk_key, split->start,
			  path->slots[level + 1] + 1,
			  level + 1);
	if (wret)
		ret = wret;

	ret = btrfs_update_ref(trans, root, c, split, 0, c_nritems - mid);
	BUG_ON(ret);

	if (path->slots[level] >= mid) {
		path->slots[level] -= mid;
		btrfs_tree_unlock(c);
		free_extent_buffer(c);
		path->nodes[level] = split;
		path->slots[level + 1] += 1;
	} else {
		btrfs_tree_unlock(split);
		free_extent_buffer(split);
	}
	return ret;
}

/*
 * how many bytes are required to store the items in a leaf.  start
 * and nr indicate which items in the leaf to check.  This totals up the
 * space used both by the item structs and the item data
 */
static int leaf_space_used(struct extent_buffer *l, int start, int nr)
{
	int data_len;
	int nritems = btrfs_header_nritems(l);
	int end = min(nritems, start + nr) - 1;

	if (!nr)
		return 0;
	data_len = btrfs_item_end_nr(l, start);
	data_len = data_len - btrfs_item_offset_nr(l, end);
	data_len += sizeof(struct btrfs_item) * nr;
	WARN_ON(data_len < 0);
	return data_len;
}

/*
 * The space between the end of the leaf items and
 * the start of the leaf data.  IOW, how much room
 * the leaf has left for both items and data
 */
noinline int btrfs_leaf_free_space(struct btrfs_root *root,
				   struct extent_buffer *leaf)
{
	int nritems = btrfs_header_nritems(leaf);
	int ret;
	ret = BTRFS_LEAF_DATA_SIZE(root) - leaf_space_used(leaf, 0, nritems);
	if (ret < 0) {
		printk(KERN_CRIT "leaf free space ret %d, leaf data size %lu, "
		       "used %d nritems %d\n",
		       ret, (unsigned long) BTRFS_LEAF_DATA_SIZE(root),
		       leaf_space_used(leaf, 0, nritems), nritems);
	}
	return ret;
}

static noinline int __push_leaf_right(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      struct btrfs_path *path,
				      int data_size, int empty,
				      struct extent_buffer *right,
				      int free_space, u32 left_nritems)
{
	struct extent_buffer *left = path->nodes[0];
	struct extent_buffer *upper = path->nodes[1];
	struct btrfs_disk_key disk_key;
	int slot;
	u32 i;
	int push_space = 0;
	int push_items = 0;
	struct btrfs_item *item;
	u32 nr;
	u32 right_nritems;
	u32 data_end;
	u32 this_item_size;
	int ret;

	if (empty)
		nr = 0;
	else
		nr = 1;

	if (path->slots[0] >= left_nritems)
		push_space += data_size;

	slot = path->slots[1];
	i = left_nritems - 1;
	while (i >= nr) {
		item = btrfs_item_nr(left, i);

		if (!empty && push_items > 0) {
			if (path->slots[0] > i)
				break;
			if (path->slots[0] == i) {
				int space = btrfs_leaf_free_space(root, left);
				if (space + push_space * 2 > free_space)
					break;
			}
		}

		if (path->slots[0] == i)
			push_space += data_size;

		if (!left->map_token) {
			map_extent_buffer(left, (unsigned long)item,
					sizeof(struct btrfs_item),
					&left->map_token, &left->kaddr,
					&left->map_start, &left->map_len,
					KM_USER1);
		}

		this_item_size = btrfs_item_size(left, item);
		if (this_item_size + sizeof(*item) + push_space > free_space)
			break;

		push_items++;
		push_space += this_item_size + sizeof(*item);
		if (i == 0)
			break;
		i--;
	}
	if (left->map_token) {
		unmap_extent_buffer(left, left->map_token, KM_USER1);
		left->map_token = NULL;
	}

	if (push_items == 0)
		goto out_unlock;

	if (!empty && push_items == left_nritems)
		WARN_ON(1);

	/* push left to right */
	right_nritems = btrfs_header_nritems(right);

	push_space = btrfs_item_end_nr(left, left_nritems - push_items);
	push_space -= leaf_data_end(root, left);

	/* make room in the right data area */
	data_end = leaf_data_end(root, right);
	memmove_extent_buffer(right,
			      btrfs_leaf_data(right) + data_end - push_space,
			      btrfs_leaf_data(right) + data_end,
			      BTRFS_LEAF_DATA_SIZE(root) - data_end);

	/* copy from the left data area */
	copy_extent_buffer(right, left, btrfs_leaf_data(right) +
		     BTRFS_LEAF_DATA_SIZE(root) - push_space,
		     btrfs_leaf_data(left) + leaf_data_end(root, left),
		     push_space);

	memmove_extent_buffer(right, btrfs_item_nr_offset(push_items),
			      btrfs_item_nr_offset(0),
			      right_nritems * sizeof(struct btrfs_item));

	/* copy the items from left to right */
	copy_extent_buffer(right, left, btrfs_item_nr_offset(0),
		   btrfs_item_nr_offset(left_nritems - push_items),
		   push_items * sizeof(struct btrfs_item));

	/* update the item pointers */
	right_nritems += push_items;
	btrfs_set_header_nritems(right, right_nritems);
	push_space = BTRFS_LEAF_DATA_SIZE(root);
	for (i = 0; i < right_nritems; i++) {
		item = btrfs_item_nr(right, i);
		if (!right->map_token) {
			map_extent_buffer(right, (unsigned long)item,
					sizeof(struct btrfs_item),
					&right->map_token, &right->kaddr,
					&right->map_start, &right->map_len,
					KM_USER1);
		}
		push_space -= btrfs_item_size(right, item);
		btrfs_set_item_offset(right, item, push_space);
	}

	if (right->map_token) {
		unmap_extent_buffer(right, right->map_token, KM_USER1);
		right->map_token = NULL;
	}
	left_nritems -= push_items;
	btrfs_set_header_nritems(left, left_nritems);

	if (left_nritems)
		btrfs_mark_buffer_dirty(left);
	btrfs_mark_buffer_dirty(right);

	ret = btrfs_update_ref(trans, root, left, right, 0, push_items);
	BUG_ON(ret);

	btrfs_item_key(right, &disk_key, 0);
	btrfs_set_node_key(upper, &disk_key, slot + 1);
	btrfs_mark_buffer_dirty(upper);

	/* then fixup the leaf pointer in the path */
	if (path->slots[0] >= left_nritems) {
		path->slots[0] -= left_nritems;
		if (btrfs_header_nritems(path->nodes[0]) == 0)
			clean_tree_block(trans, root, path->nodes[0]);
		btrfs_tree_unlock(path->nodes[0]);
		free_extent_buffer(path->nodes[0]);
		path->nodes[0] = right;
		path->slots[1] += 1;
	} else {
		btrfs_tree_unlock(right);
		free_extent_buffer(right);
	}
	return 0;

out_unlock:
	btrfs_tree_unlock(right);
	free_extent_buffer(right);
	return 1;
}

/*
 * push some data in the path leaf to the right, trying to free up at
 * least data_size bytes.  returns zero if the push worked, nonzero otherwise
 *
 * returns 1 if the push failed because the other node didn't have enough
 * room, 0 if everything worked out and < 0 if there were major errors.
 */
static int push_leaf_right(struct btrfs_trans_handle *trans, struct btrfs_root
			   *root, struct btrfs_path *path, int data_size,
			   int empty)
{
	struct extent_buffer *left = path->nodes[0];
	struct extent_buffer *right;
	struct extent_buffer *upper;
	int slot;
	int free_space;
	u32 left_nritems;
	int ret;

	if (!path->nodes[1])
		return 1;

	slot = path->slots[1];
	upper = path->nodes[1];
	if (slot >= btrfs_header_nritems(upper) - 1)
		return 1;

	btrfs_assert_tree_locked(path->nodes[1]);

	right = read_node_slot(root, upper, slot + 1);
	btrfs_tree_lock(right);
	btrfs_set_lock_blocking(right);

	free_space = btrfs_leaf_free_space(root, right);
	if (free_space < data_size)
		goto out_unlock;

	/* cow and double check */
	ret = btrfs_cow_block(trans, root, right, upper,
			      slot + 1, &right);
	if (ret)
		goto out_unlock;

	free_space = btrfs_leaf_free_space(root, right);
	if (free_space < data_size)
		goto out_unlock;

	left_nritems = btrfs_header_nritems(left);
	if (left_nritems == 0)
		goto out_unlock;

	return __push_leaf_right(trans, root, path, data_size, empty,
				right, free_space, left_nritems);
out_unlock:
	btrfs_tree_unlock(right);
	free_extent_buffer(right);
	return 1;
}

/*
 * push some data in the path leaf to the left, trying to free up at
 * least data_size bytes.  returns zero if the push worked, nonzero otherwise
 */
static noinline int __push_leaf_left(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct btrfs_path *path, int data_size,
				     int empty, struct extent_buffer *left,
				     int free_space, int right_nritems)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *right = path->nodes[0];
	int slot;
	int i;
	int push_space = 0;
	int push_items = 0;
	struct btrfs_item *item;
	u32 old_left_nritems;
	u32 nr;
	int ret = 0;
	int wret;
	u32 this_item_size;
	u32 old_left_item_size;

	slot = path->slots[1];

	if (empty)
		nr = right_nritems;
	else
		nr = right_nritems - 1;

	for (i = 0; i < nr; i++) {
		item = btrfs_item_nr(right, i);
		if (!right->map_token) {
			map_extent_buffer(right, (unsigned long)item,
					sizeof(struct btrfs_item),
					&right->map_token, &right->kaddr,
					&right->map_start, &right->map_len,
					KM_USER1);
		}

		if (!empty && push_items > 0) {
			if (path->slots[0] < i)
				break;
			if (path->slots[0] == i) {
				int space = btrfs_leaf_free_space(root, right);
				if (space + push_space * 2 > free_space)
					break;
			}
		}

		if (path->slots[0] == i)
			push_space += data_size;

		this_item_size = btrfs_item_size(right, item);
		if (this_item_size + sizeof(*item) + push_space > free_space)
			break;

		push_items++;
		push_space += this_item_size + sizeof(*item);
	}

	if (right->map_token) {
		unmap_extent_buffer(right, right->map_token, KM_USER1);
		right->map_token = NULL;
	}

	if (push_items == 0) {
		ret = 1;
		goto out;
	}
	if (!empty && push_items == btrfs_header_nritems(right))
		WARN_ON(1);

	/* push data from right to left */
	copy_extent_buffer(left, right,
			   btrfs_item_nr_offset(btrfs_header_nritems(left)),
			   btrfs_item_nr_offset(0),
			   push_items * sizeof(struct btrfs_item));

	push_space = BTRFS_LEAF_DATA_SIZE(root) -
		     btrfs_item_offset_nr(right, push_items - 1);

	copy_extent_buffer(left, right, btrfs_leaf_data(left) +
		     leaf_data_end(root, left) - push_space,
		     btrfs_leaf_data(right) +
		     btrfs_item_offset_nr(right, push_items - 1),
		     push_space);
	old_left_nritems = btrfs_header_nritems(left);
	BUG_ON(old_left_nritems <= 0);

	old_left_item_size = btrfs_item_offset_nr(left, old_left_nritems - 1);
	for (i = old_left_nritems; i < old_left_nritems + push_items; i++) {
		u32 ioff;

		item = btrfs_item_nr(left, i);
		if (!left->map_token) {
			map_extent_buffer(left, (unsigned long)item,
					sizeof(struct btrfs_item),
					&left->map_token, &left->kaddr,
					&left->map_start, &left->map_len,
					KM_USER1);
		}

		ioff = btrfs_item_offset(left, item);
		btrfs_set_item_offset(left, item,
		      ioff - (BTRFS_LEAF_DATA_SIZE(root) - old_left_item_size));
	}
	btrfs_set_header_nritems(left, old_left_nritems + push_items);
	if (left->map_token) {
		unmap_extent_buffer(left, left->map_token, KM_USER1);
		left->map_token = NULL;
	}

	/* fixup right node */
	if (push_items > right_nritems) {
		printk(KERN_CRIT "push items %d nr %u\n", push_items,
		       right_nritems);
		WARN_ON(1);
	}

	if (push_items < right_nritems) {
		push_space = btrfs_item_offset_nr(right, push_items - 1) -
						  leaf_data_end(root, right);
		memmove_extent_buffer(right, btrfs_leaf_data(right) +
				      BTRFS_LEAF_DATA_SIZE(root) - push_space,
				      btrfs_leaf_data(right) +
				      leaf_data_end(root, right), push_space);

		memmove_extent_buffer(right, btrfs_item_nr_offset(0),
			      btrfs_item_nr_offset(push_items),
			     (btrfs_header_nritems(right) - push_items) *
			     sizeof(struct btrfs_item));
	}
	right_nritems -= push_items;
	btrfs_set_header_nritems(right, right_nritems);
	push_space = BTRFS_LEAF_DATA_SIZE(root);
	for (i = 0; i < right_nritems; i++) {
		item = btrfs_item_nr(right, i);

		if (!right->map_token) {
			map_extent_buffer(right, (unsigned long)item,
					sizeof(struct btrfs_item),
					&right->map_token, &right->kaddr,
					&right->map_start, &right->map_len,
					KM_USER1);
		}

		push_space = push_space - btrfs_item_size(right, item);
		btrfs_set_item_offset(right, item, push_space);
	}
	if (right->map_token) {
		unmap_extent_buffer(right, right->map_token, KM_USER1);
		right->map_token = NULL;
	}

	btrfs_mark_buffer_dirty(left);
	if (right_nritems)
		btrfs_mark_buffer_dirty(right);

	ret = btrfs_update_ref(trans, root, right, left,
			       old_left_nritems, push_items);
	BUG_ON(ret);

	btrfs_item_key(right, &disk_key, 0);
	wret = fixup_low_keys(trans, root, path, &disk_key, 1);
	if (wret)
		ret = wret;

	/* then fixup the leaf pointer in the path */
	if (path->slots[0] < push_items) {
		path->slots[0] += old_left_nritems;
		if (btrfs_header_nritems(path->nodes[0]) == 0)
			clean_tree_block(trans, root, path->nodes[0]);
		btrfs_tree_unlock(path->nodes[0]);
		free_extent_buffer(path->nodes[0]);
		path->nodes[0] = left;
		path->slots[1] -= 1;
	} else {
		btrfs_tree_unlock(left);
		free_extent_buffer(left);
		path->slots[0] -= push_items;
	}
	BUG_ON(path->slots[0] < 0);
	return ret;
out:
	btrfs_tree_unlock(left);
	free_extent_buffer(left);
	return ret;
}

/*
 * push some data in the path leaf to the left, trying to free up at
 * least data_size bytes.  returns zero if the push worked, nonzero otherwise
 */
static int push_leaf_left(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, struct btrfs_path *path, int data_size,
			  int empty)
{
	struct extent_buffer *right = path->nodes[0];
	struct extent_buffer *left;
	int slot;
	int free_space;
	u32 right_nritems;
	int ret = 0;

	slot = path->slots[1];
	if (slot == 0)
		return 1;
	if (!path->nodes[1])
		return 1;

	right_nritems = btrfs_header_nritems(right);
	if (right_nritems == 0)
		return 1;

	btrfs_assert_tree_locked(path->nodes[1]);

	left = read_node_slot(root, path->nodes[1], slot - 1);
	btrfs_tree_lock(left);
	btrfs_set_lock_blocking(left);

	free_space = btrfs_leaf_free_space(root, left);
	if (free_space < data_size) {
		ret = 1;
		goto out;
	}

	/* cow and double check */
	ret = btrfs_cow_block(trans, root, left,
			      path->nodes[1], slot - 1, &left);
	if (ret) {
		/* we hit -ENOSPC, but it isn't fatal here */
		ret = 1;
		goto out;
	}

	free_space = btrfs_leaf_free_space(root, left);
	if (free_space < data_size) {
		ret = 1;
		goto out;
	}

	return __push_leaf_left(trans, root, path, data_size,
			       empty, left, free_space, right_nritems);
out:
	btrfs_tree_unlock(left);
	free_extent_buffer(left);
	return ret;
}

/*
 * split the path's leaf in two, making sure there is at least data_size
 * available for the resulting leaf level of the path.
 *
 * returns 0 if all went well and < 0 on failure.
 */
static noinline int copy_for_split(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_path *path,
			       struct extent_buffer *l,
			       struct extent_buffer *right,
			       int slot, int mid, int nritems)
{
	int data_copy_size;
	int rt_data_off;
	int i;
	int ret = 0;
	int wret;
	struct btrfs_disk_key disk_key;

	nritems = nritems - mid;
	btrfs_set_header_nritems(right, nritems);
	data_copy_size = btrfs_item_end_nr(l, mid) - leaf_data_end(root, l);

	copy_extent_buffer(right, l, btrfs_item_nr_offset(0),
			   btrfs_item_nr_offset(mid),
			   nritems * sizeof(struct btrfs_item));

	copy_extent_buffer(right, l,
		     btrfs_leaf_data(right) + BTRFS_LEAF_DATA_SIZE(root) -
		     data_copy_size, btrfs_leaf_data(l) +
		     leaf_data_end(root, l), data_copy_size);

	rt_data_off = BTRFS_LEAF_DATA_SIZE(root) -
		      btrfs_item_end_nr(l, mid);

	for (i = 0; i < nritems; i++) {
		struct btrfs_item *item = btrfs_item_nr(right, i);
		u32 ioff;

		if (!right->map_token) {
			map_extent_buffer(right, (unsigned long)item,
					sizeof(struct btrfs_item),
					&right->map_token, &right->kaddr,
					&right->map_start, &right->map_len,
					KM_USER1);
		}

		ioff = btrfs_item_offset(right, item);
		btrfs_set_item_offset(right, item, ioff + rt_data_off);
	}

	if (right->map_token) {
		unmap_extent_buffer(right, right->map_token, KM_USER1);
		right->map_token = NULL;
	}

	btrfs_set_header_nritems(l, mid);
	ret = 0;
	btrfs_item_key(right, &disk_key, 0);
	wret = insert_ptr(trans, root, path, &disk_key, right->start,
			  path->slots[1] + 1, 1);
	if (wret)
		ret = wret;

	btrfs_mark_buffer_dirty(right);
	btrfs_mark_buffer_dirty(l);
	BUG_ON(path->slots[0] != slot);

	ret = btrfs_update_ref(trans, root, l, right, 0, nritems);
	BUG_ON(ret);

	if (mid <= slot) {
		btrfs_tree_unlock(path->nodes[0]);
		free_extent_buffer(path->nodes[0]);
		path->nodes[0] = right;
		path->slots[0] -= mid;
		path->slots[1] += 1;
	} else {
		btrfs_tree_unlock(right);
		free_extent_buffer(right);
	}

	BUG_ON(path->slots[0] < 0);

	return ret;
}

/*
 * split the path's leaf in two, making sure there is at least data_size
 * available for the resulting leaf level of the path.
 *
 * returns 0 if all went well and < 0 on failure.
 */
static noinline int split_leaf(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_key *ins_key,
			       struct btrfs_path *path, int data_size,
			       int extend)
{
	struct extent_buffer *l;
	u32 nritems;
	int mid;
	int slot;
	struct extent_buffer *right;
	int ret = 0;
	int wret;
	int double_split;
	int num_doubles = 0;

	/* first try to make some room by pushing left and right */
	if (data_size && ins_key->type != BTRFS_DIR_ITEM_KEY &&
	    !trans->transaction->delayed_refs.flushing) {
		wret = push_leaf_right(trans, root, path, data_size, 0);
		if (wret < 0)
			return wret;
		if (wret) {
			wret = push_leaf_left(trans, root, path, data_size, 0);
			if (wret < 0)
				return wret;
		}
		l = path->nodes[0];

		/* did the pushes work? */
		if (btrfs_leaf_free_space(root, l) >= data_size)
			return 0;
	}

	if (!path->nodes[1]) {
		ret = insert_new_root(trans, root, path, 1);
		if (ret)
			return ret;
	}
again:
	double_split = 0;
	l = path->nodes[0];
	slot = path->slots[0];
	nritems = btrfs_header_nritems(l);
	mid = (nritems + 1) / 2;

	right = btrfs_alloc_free_block(trans, root, root->leafsize,
					path->nodes[1]->start,
					root->root_key.objectid,
					trans->transid, 0, l->start, 0);
	if (IS_ERR(right)) {
		BUG_ON(1);
		return PTR_ERR(right);
	}

	memset_extent_buffer(right, 0, 0, sizeof(struct btrfs_header));
	btrfs_set_header_bytenr(right, right->start);
	btrfs_set_header_generation(right, trans->transid);
	btrfs_set_header_owner(right, root->root_key.objectid);
	btrfs_set_header_level(right, 0);
	write_extent_buffer(right, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(right),
			    BTRFS_FSID_SIZE);

	write_extent_buffer(right, root->fs_info->chunk_tree_uuid,
			    (unsigned long)btrfs_header_chunk_tree_uuid(right),
			    BTRFS_UUID_SIZE);

	if (mid <= slot) {
		if (nritems == 1 ||
		    leaf_space_used(l, mid, nritems - mid) + data_size >
			BTRFS_LEAF_DATA_SIZE(root)) {
			if (slot >= nritems) {
				struct btrfs_disk_key disk_key;

				btrfs_cpu_key_to_disk(&disk_key, ins_key);
				btrfs_set_header_nritems(right, 0);
				wret = insert_ptr(trans, root, path,
						  &disk_key, right->start,
						  path->slots[1] + 1, 1);
				if (wret)
					ret = wret;

				btrfs_tree_unlock(path->nodes[0]);
				free_extent_buffer(path->nodes[0]);
				path->nodes[0] = right;
				path->slots[0] = 0;
				path->slots[1] += 1;
				btrfs_mark_buffer_dirty(right);
				return ret;
			}
			mid = slot;
			if (mid != nritems &&
			    leaf_space_used(l, mid, nritems - mid) +
			    data_size > BTRFS_LEAF_DATA_SIZE(root)) {
				double_split = 1;
			}
		}
	} else {
		if (leaf_space_used(l, 0, mid) + data_size >
			BTRFS_LEAF_DATA_SIZE(root)) {
			if (!extend && data_size && slot == 0) {
				struct btrfs_disk_key disk_key;

				btrfs_cpu_key_to_disk(&disk_key, ins_key);
				btrfs_set_header_nritems(right, 0);
				wret = insert_ptr(trans, root, path,
						  &disk_key,
						  right->start,
						  path->slots[1], 1);
				if (wret)
					ret = wret;
				btrfs_tree_unlock(path->nodes[0]);
				free_extent_buffer(path->nodes[0]);
				path->nodes[0] = right;
				path->slots[0] = 0;
				if (path->slots[1] == 0) {
					wret = fixup_low_keys(trans, root,
						      path, &disk_key, 1);
					if (wret)
						ret = wret;
				}
				btrfs_mark_buffer_dirty(right);
				return ret;
			} else if ((extend || !data_size) && slot == 0) {
				mid = 1;
			} else {
				mid = slot;
				if (mid != nritems &&
				    leaf_space_used(l, mid, nritems - mid) +
				    data_size > BTRFS_LEAF_DATA_SIZE(root)) {
					double_split = 1;
				}
			}
		}
	}

	ret = copy_for_split(trans, root, path, l, right, slot, mid, nritems);
	BUG_ON(ret);

	if (double_split) {
		BUG_ON(num_doubles != 0);
		num_doubles++;
		goto again;
	}

	return ret;
}

/*
 * This function splits a single item into two items,
 * giving 'new_key' to the new item and splitting the
 * old one at split_offset (from the start of the item).
 *
 * The path may be released by this operation.  After
 * the split, the path is pointing to the old item.  The
 * new item is going to be in the same node as the old one.
 *
 * Note, the item being split must be smaller enough to live alone on
 * a tree block with room for one extra struct btrfs_item
 *
 * This allows us to split the item in place, keeping a lock on the
 * leaf the entire time.
 */
int btrfs_split_item(struct btrfs_trans_handle *trans,
		     struct btrfs_root *root,
		     struct btrfs_path *path,
		     struct btrfs_key *new_key,
		     unsigned long split_offset)
{
	u32 item_size;
	struct extent_buffer *leaf;
	struct btrfs_key orig_key;
	struct btrfs_item *item;
	struct btrfs_item *new_item;
	int ret = 0;
	int slot;
	u32 nritems;
	u32 orig_offset;
	struct btrfs_disk_key disk_key;
	char *buf;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &orig_key, path->slots[0]);
	if (btrfs_leaf_free_space(root, leaf) >= sizeof(struct btrfs_item))
		goto split;

	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	btrfs_release_path(root, path);

	path->search_for_split = 1;
	path->keep_locks = 1;

	ret = btrfs_search_slot(trans, root, &orig_key, path, 0, 1);
	path->search_for_split = 0;

	/* if our item isn't there or got smaller, return now */
	if (ret != 0 || item_size != btrfs_item_size_nr(path->nodes[0],
							path->slots[0])) {
		path->keep_locks = 0;
		return -EAGAIN;
	}

	btrfs_set_path_blocking(path);
	ret = split_leaf(trans, root, &orig_key, path,
			 sizeof(struct btrfs_item), 1);
	path->keep_locks = 0;
	BUG_ON(ret);

	btrfs_unlock_up_safe(path, 1);
	leaf = path->nodes[0];
	BUG_ON(btrfs_leaf_free_space(root, leaf) < sizeof(struct btrfs_item));

split:
	/*
	 * make sure any changes to the path from split_leaf leave it
	 * in a blocking state
	 */
	btrfs_set_path_blocking(path);

	item = btrfs_item_nr(leaf, path->slots[0]);
	orig_offset = btrfs_item_offset(leaf, item);
	item_size = btrfs_item_size(leaf, item);

	buf = kmalloc(item_size, GFP_NOFS);
	read_extent_buffer(leaf, buf, btrfs_item_ptr_offset(leaf,
			    path->slots[0]), item_size);
	slot = path->slots[0] + 1;
	leaf = path->nodes[0];

	nritems = btrfs_header_nritems(leaf);

	if (slot != nritems) {
		/* shift the items */
		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot + 1),
			      btrfs_item_nr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_item));

	}

	btrfs_cpu_key_to_disk(&disk_key, new_key);
	btrfs_set_item_key(leaf, &disk_key, slot);

	new_item = btrfs_item_nr(leaf, slot);

	btrfs_set_item_offset(leaf, new_item, orig_offset);
	btrfs_set_item_size(leaf, new_item, item_size - split_offset);

	btrfs_set_item_offset(leaf, item,
			      orig_offset + item_size - split_offset);
	btrfs_set_item_size(leaf, item, split_offset);

	btrfs_set_header_nritems(leaf, nritems + 1);

	/* write the data for the start of the original item */
	write_extent_buffer(leaf, buf,
			    btrfs_item_ptr_offset(leaf, path->slots[0]),
			    split_offset);

	/* write the data for the new item */
	write_extent_buffer(leaf, buf + split_offset,
			    btrfs_item_ptr_offset(leaf, slot),
			    item_size - split_offset);
	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
	if (btrfs_leaf_free_space(root, leaf) < 0) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
	kfree(buf);
	return ret;
}

/*
 * make the item pointed to by the path smaller.  new_size indicates
 * how small to make it, and from_end tells us if we just chop bytes
 * off the end of the item or if we shift the item to chop bytes off
 * the front.
 */
int btrfs_truncate_item(struct btrfs_trans_handle *trans,
			struct btrfs_root *root,
			struct btrfs_path *path,
			u32 new_size, int from_end)
{
	int ret = 0;
	int slot;
	int slot_orig;
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	u32 nritems;
	unsigned int data_end;
	unsigned int old_data_start;
	unsigned int old_size;
	unsigned int size_diff;
	int i;

	slot_orig = path->slots[0];
	leaf = path->nodes[0];
	slot = path->slots[0];

	old_size = btrfs_item_size_nr(leaf, slot);
	if (old_size == new_size)
		return 0;

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	old_data_start = btrfs_item_offset_nr(leaf, slot);

	size_diff = old_size - new_size;

	BUG_ON(slot < 0);
	BUG_ON(slot >= nritems);

	/*
	 * item0..itemN ... dataN.offset..dataN.size .. data0.size
	 */
	/* first correct the data pointers */
	for (i = slot; i < nritems; i++) {
		u32 ioff;
		item = btrfs_item_nr(leaf, i);

		if (!leaf->map_token) {
			map_extent_buffer(leaf, (unsigned long)item,
					sizeof(struct btrfs_item),
					&leaf->map_token, &leaf->kaddr,
					&leaf->map_start, &leaf->map_len,
					KM_USER1);
		}

		ioff = btrfs_item_offset(leaf, item);
		btrfs_set_item_offset(leaf, item, ioff + size_diff);
	}

	if (leaf->map_token) {
		unmap_extent_buffer(leaf, leaf->map_token, KM_USER1);
		leaf->map_token = NULL;
	}

	/* shift the data */
	if (from_end) {
		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end + size_diff, btrfs_leaf_data(leaf) +
			      data_end, old_data_start + new_size - data_end);
	} else {
		struct btrfs_disk_key disk_key;
		u64 offset;

		btrfs_item_key(leaf, &disk_key, slot);

		if (btrfs_disk_key_type(&disk_key) == BTRFS_EXTENT_DATA_KEY) {
			unsigned long ptr;
			struct btrfs_file_extent_item *fi;

			fi = btrfs_item_ptr(leaf, slot,
					    struct btrfs_file_extent_item);
			fi = (struct btrfs_file_extent_item *)(
			     (unsigned long)fi - size_diff);

			if (btrfs_file_extent_type(leaf, fi) ==
			    BTRFS_FILE_EXTENT_INLINE) {
				ptr = btrfs_item_ptr_offset(leaf, slot);
				memmove_extent_buffer(leaf, ptr,
				      (unsigned long)fi,
				      offsetof(struct btrfs_file_extent_item,
						 disk_bytenr));
			}
		}

		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end + size_diff, btrfs_leaf_data(leaf) +
			      data_end, old_data_start - data_end);

		offset = btrfs_disk_key_offset(&disk_key);
		btrfs_set_disk_key_offset(&disk_key, offset + size_diff);
		btrfs_set_item_key(leaf, &disk_key, slot);
		if (slot == 0)
			fixup_low_keys(trans, root, path, &disk_key, 1);
	}

	item = btrfs_item_nr(leaf, slot);
	btrfs_set_item_size(leaf, item, new_size);
	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
	if (btrfs_leaf_free_space(root, leaf) < 0) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
	return ret;
}

/*
 * make the item pointed to by the path bigger, data_size is the new size.
 */
int btrfs_extend_item(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root, struct btrfs_path *path,
		      u32 data_size)
{
	int ret = 0;
	int slot;
	int slot_orig;
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	u32 nritems;
	unsigned int data_end;
	unsigned int old_data;
	unsigned int old_size;
	int i;

	slot_orig = path->slots[0];
	leaf = path->nodes[0];

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	if (btrfs_leaf_free_space(root, leaf) < data_size) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
	slot = path->slots[0];
	old_data = btrfs_item_end_nr(leaf, slot);

	BUG_ON(slot < 0);
	if (slot >= nritems) {
		btrfs_print_leaf(root, leaf);
		printk(KERN_CRIT "slot %d too large, nritems %d\n",
		       slot, nritems);
		BUG_ON(1);
	}

	/*
	 * item0..itemN ... dataN.offset..dataN.size .. data0.size
	 */
	/* first correct the data pointers */
	for (i = slot; i < nritems; i++) {
		u32 ioff;
		item = btrfs_item_nr(leaf, i);

		if (!leaf->map_token) {
			map_extent_buffer(leaf, (unsigned long)item,
					sizeof(struct btrfs_item),
					&leaf->map_token, &leaf->kaddr,
					&leaf->map_start, &leaf->map_len,
					KM_USER1);
		}
		ioff = btrfs_item_offset(leaf, item);
		btrfs_set_item_offset(leaf, item, ioff - data_size);
	}

	if (leaf->map_token) {
		unmap_extent_buffer(leaf, leaf->map_token, KM_USER1);
		leaf->map_token = NULL;
	}

	/* shift the data */
	memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
		      data_end - data_size, btrfs_leaf_data(leaf) +
		      data_end, old_data - data_end);

	data_end = old_data;
	old_size = btrfs_item_size_nr(leaf, slot);
	item = btrfs_item_nr(leaf, slot);
	btrfs_set_item_size(leaf, item, old_size + data_size);
	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
	if (btrfs_leaf_free_space(root, leaf) < 0) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
	return ret;
}

/*
 * Given a key and some data, insert items into the tree.
 * This does all the path init required, making room in the tree if needed.
 * Returns the number of keys that were inserted.
 */
int btrfs_insert_some_items(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct btrfs_path *path,
			    struct btrfs_key *cpu_key, u32 *data_size,
			    int nr)
{
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	int ret = 0;
	int slot;
	int i;
	u32 nritems;
	u32 total_data = 0;
	u32 total_size = 0;
	unsigned int data_end;
	struct btrfs_disk_key disk_key;
	struct btrfs_key found_key;

	for (i = 0; i < nr; i++) {
		if (total_size + data_size[i] + sizeof(struct btrfs_item) >
		    BTRFS_LEAF_DATA_SIZE(root)) {
			break;
			nr = i;
		}
		total_data += data_size[i];
		total_size += data_size[i] + sizeof(struct btrfs_item);
	}
	BUG_ON(nr == 0);

	ret = btrfs_search_slot(trans, root, cpu_key, path, total_size, 1);
	if (ret == 0)
		return -EEXIST;
	if (ret < 0)
		goto out;

	leaf = path->nodes[0];

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	if (btrfs_leaf_free_space(root, leaf) < total_size) {
		for (i = nr; i >= 0; i--) {
			total_data -= data_size[i];
			total_size -= data_size[i] + sizeof(struct btrfs_item);
			if (total_size < btrfs_leaf_free_space(root, leaf))
				break;
		}
		nr = i;
	}

	slot = path->slots[0];
	BUG_ON(slot < 0);

	if (slot != nritems) {
		unsigned int old_data = btrfs_item_end_nr(leaf, slot);

		item = btrfs_item_nr(leaf, slot);
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		/* figure out how many keys we can insert in here */
		total_data = data_size[0];
		for (i = 1; i < nr; i++) {
			if (comp_cpu_keys(&found_key, cpu_key + i) <= 0)
				break;
			total_data += data_size[i];
		}
		nr = i;

		if (old_data < data_end) {
			btrfs_print_leaf(root, leaf);
			printk(KERN_CRIT "slot %d old_data %d data_end %d\n",
			       slot, old_data, data_end);
			BUG_ON(1);
		}
		/*
		 * item0..itemN ... dataN.offset..dataN.size .. data0.size
		 */
		/* first correct the data pointers */
		WARN_ON(leaf->map_token);
		for (i = slot; i < nritems; i++) {
			u32 ioff;

			item = btrfs_item_nr(leaf, i);
			if (!leaf->map_token) {
				map_extent_buffer(leaf, (unsigned long)item,
					sizeof(struct btrfs_item),
					&leaf->map_token, &leaf->kaddr,
					&leaf->map_start, &leaf->map_len,
					KM_USER1);
			}

			ioff = btrfs_item_offset(leaf, item);
			btrfs_set_item_offset(leaf, item, ioff - total_data);
		}
		if (leaf->map_token) {
			unmap_extent_buffer(leaf, leaf->map_token, KM_USER1);
			leaf->map_token = NULL;
		}

		/* shift the items */
		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot + nr),
			      btrfs_item_nr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_item));

		/* shift the data */
		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end - total_data, btrfs_leaf_data(leaf) +
			      data_end, old_data - data_end);
		data_end = old_data;
	} else {
		/*
		 * this sucks but it has to be done, if we are inserting at
		 * the end of the leaf only insert 1 of the items, since we
		 * have no way of knowing whats on the next leaf and we'd have
		 * to drop our current locks to figure it out
		 */
		nr = 1;
	}

	/* setup the item for the new data */
	for (i = 0; i < nr; i++) {
		btrfs_cpu_key_to_disk(&disk_key, cpu_key + i);
		btrfs_set_item_key(leaf, &disk_key, slot + i);
		item = btrfs_item_nr(leaf, slot + i);
		btrfs_set_item_offset(leaf, item, data_end - data_size[i]);
		data_end -= data_size[i];
		btrfs_set_item_size(leaf, item, data_size[i]);
	}
	btrfs_set_header_nritems(leaf, nritems + nr);
	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
	if (slot == 0) {
		btrfs_cpu_key_to_disk(&disk_key, cpu_key);
		ret = fixup_low_keys(trans, root, path, &disk_key, 1);
	}

	if (btrfs_leaf_free_space(root, leaf) < 0) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
out:
	if (!ret)
		ret = nr;
	return ret;
}

/*
 * this is a helper for btrfs_insert_empty_items, the main goal here is
 * to save stack depth by doing the bulk of the work in a function
 * that doesn't call btrfs_search_slot
 */
static noinline_for_stack int
setup_items_for_insert(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root, struct btrfs_path *path,
		      struct btrfs_key *cpu_key, u32 *data_size,
		      u32 total_data, u32 total_size, int nr)
{
	struct btrfs_item *item;
	int i;
	u32 nritems;
	unsigned int data_end;
	struct btrfs_disk_key disk_key;
	int ret;
	struct extent_buffer *leaf;
	int slot;

	leaf = path->nodes[0];
	slot = path->slots[0];

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	if (btrfs_leaf_free_space(root, leaf) < total_size) {
		btrfs_print_leaf(root, leaf);
		printk(KERN_CRIT "not enough freespace need %u have %d\n",
		       total_size, btrfs_leaf_free_space(root, leaf));
		BUG();
	}

	if (slot != nritems) {
		unsigned int old_data = btrfs_item_end_nr(leaf, slot);

		if (old_data < data_end) {
			btrfs_print_leaf(root, leaf);
			printk(KERN_CRIT "slot %d old_data %d data_end %d\n",
			       slot, old_data, data_end);
			BUG_ON(1);
		}
		/*
		 * item0..itemN ... dataN.offset..dataN.size .. data0.size
		 */
		/* first correct the data pointers */
		WARN_ON(leaf->map_token);
		for (i = slot; i < nritems; i++) {
			u32 ioff;

			item = btrfs_item_nr(leaf, i);
			if (!leaf->map_token) {
				map_extent_buffer(leaf, (unsigned long)item,
					sizeof(struct btrfs_item),
					&leaf->map_token, &leaf->kaddr,
					&leaf->map_start, &leaf->map_len,
					KM_USER1);
			}

			ioff = btrfs_item_offset(leaf, item);
			btrfs_set_item_offset(leaf, item, ioff - total_data);
		}
		if (leaf->map_token) {
			unmap_extent_buffer(leaf, leaf->map_token, KM_USER1);
			leaf->map_token = NULL;
		}

		/* shift the items */
		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot + nr),
			      btrfs_item_nr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_item));

		/* shift the data */
		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end - total_data, btrfs_leaf_data(leaf) +
			      data_end, old_data - data_end);
		data_end = old_data;
	}

	/* setup the item for the new data */
	for (i = 0; i < nr; i++) {
		btrfs_cpu_key_to_disk(&disk_key, cpu_key + i);
		btrfs_set_item_key(leaf, &disk_key, slot + i);
		item = btrfs_item_nr(leaf, slot + i);
		btrfs_set_item_offset(leaf, item, data_end - data_size[i]);
		data_end -= data_size[i];
		btrfs_set_item_size(leaf, item, data_size[i]);
	}

	btrfs_set_header_nritems(leaf, nritems + nr);

	ret = 0;
	if (slot == 0) {
		struct btrfs_disk_key disk_key;
		btrfs_cpu_key_to_disk(&disk_key, cpu_key);
		ret = fixup_low_keys(trans, root, path, &disk_key, 1);
	}
	btrfs_unlock_up_safe(path, 1);
	btrfs_mark_buffer_dirty(leaf);

	if (btrfs_leaf_free_space(root, leaf) < 0) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
	return ret;
}

/*
 * Given a key and some data, insert items into the tree.
 * This does all the path init required, making room in the tree if needed.
 */
int btrfs_insert_empty_items(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct btrfs_path *path,
			    struct btrfs_key *cpu_key, u32 *data_size,
			    int nr)
{
	struct extent_buffer *leaf;
	int ret = 0;
	int slot;
	int i;
	u32 total_size = 0;
	u32 total_data = 0;

	for (i = 0; i < nr; i++)
		total_data += data_size[i];

	total_size = total_data + (nr * sizeof(struct btrfs_item));
	ret = btrfs_search_slot(trans, root, cpu_key, path, total_size, 1);
	if (ret == 0)
		return -EEXIST;
	if (ret < 0)
		goto out;

	leaf = path->nodes[0];
	slot = path->slots[0];
	BUG_ON(slot < 0);

	ret = setup_items_for_insert(trans, root, path, cpu_key, data_size,
			       total_data, total_size, nr);

out:
	return ret;
}

/*
 * Given a key and some data, insert an item into the tree.
 * This does all the path init required, making room in the tree if needed.
 */
int btrfs_insert_item(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *cpu_key, void *data, u32
		      data_size)
{
	int ret = 0;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	unsigned long ptr;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_insert_empty_item(trans, root, path, cpu_key, data_size);
	if (!ret) {
		leaf = path->nodes[0];
		ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
		write_extent_buffer(leaf, data, ptr, data_size);
		btrfs_mark_buffer_dirty(leaf);
	}
	btrfs_free_path(path);
	return ret;
}

/*
 * delete the pointer from a given node.
 *
 * the tree should have been previously balanced so the deletion does not
 * empty a node.
 */
static int del_ptr(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_path *path, int level, int slot)
{
	struct extent_buffer *parent = path->nodes[level];
	u32 nritems;
	int ret = 0;
	int wret;

	nritems = btrfs_header_nritems(parent);
	if (slot != nritems - 1) {
		memmove_extent_buffer(parent,
			      btrfs_node_key_ptr_offset(slot),
			      btrfs_node_key_ptr_offset(slot + 1),
			      sizeof(struct btrfs_key_ptr) *
			      (nritems - slot - 1));
	}
	nritems--;
	btrfs_set_header_nritems(parent, nritems);
	if (nritems == 0 && parent == root->node) {
		BUG_ON(btrfs_header_level(root->node) != 1);
		/* just turn the root into a leaf and break */
		btrfs_set_header_level(root->node, 0);
	} else if (slot == 0) {
		struct btrfs_disk_key disk_key;

		btrfs_node_key(parent, &disk_key, 0);
		wret = fixup_low_keys(trans, root, path, &disk_key, level + 1);
		if (wret)
			ret = wret;
	}
	btrfs_mark_buffer_dirty(parent);
	return ret;
}

/*
 * a helper function to delete the leaf pointed to by path->slots[1] and
 * path->nodes[1].  bytenr is the node block pointer, but since the callers
 * already know it, it is faster to have them pass it down than to
 * read it out of the node again.
 *
 * This deletes the pointer in path->nodes[1] and frees the leaf
 * block extent.  zero is returned if it all worked out, < 0 otherwise.
 *
 * The path must have already been setup for deleting the leaf, including
 * all the proper balancing.  path->nodes[1] must be locked.
 */
noinline int btrfs_del_leaf(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct btrfs_path *path, u64 bytenr)
{
	int ret;
	u64 root_gen = btrfs_header_generation(path->nodes[1]);
	u64 parent_start = path->nodes[1]->start;
	u64 parent_owner = btrfs_header_owner(path->nodes[1]);

	ret = del_ptr(trans, root, path, 1, path->slots[1]);
	if (ret)
		return ret;

	/*
	 * btrfs_free_extent is expensive, we want to make sure we
	 * aren't holding any locks when we call it
	 */
	btrfs_unlock_up_safe(path, 0);

	ret = btrfs_free_extent(trans, root, bytenr,
				btrfs_level_size(root, 0),
				parent_start, parent_owner,
				root_gen, 0, 1);
	return ret;
}
/*
 * delete the item at the leaf level in path.  If that empties
 * the leaf, remove it from the tree
 */
int btrfs_del_items(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		    struct btrfs_path *path, int slot, int nr)
{
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	int last_off;
	int dsize = 0;
	int ret = 0;
	int wret;
	int i;
	u32 nritems;

	leaf = path->nodes[0];
	last_off = btrfs_item_offset_nr(leaf, slot + nr - 1);

	for (i = 0; i < nr; i++)
		dsize += btrfs_item_size_nr(leaf, slot + i);

	nritems = btrfs_header_nritems(leaf);

	if (slot + nr != nritems) {
		int data_end = leaf_data_end(root, leaf);

		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end + dsize,
			      btrfs_leaf_data(leaf) + data_end,
			      last_off - data_end);

		for (i = slot + nr; i < nritems; i++) {
			u32 ioff;

			item = btrfs_item_nr(leaf, i);
			if (!leaf->map_token) {
				map_extent_buffer(leaf, (unsigned long)item,
					sizeof(struct btrfs_item),
					&leaf->map_token, &leaf->kaddr,
					&leaf->map_start, &leaf->map_len,
					KM_USER1);
			}
			ioff = btrfs_item_offset(leaf, item);
			btrfs_set_item_offset(leaf, item, ioff + dsize);
		}

		if (leaf->map_token) {
			unmap_extent_buffer(leaf, leaf->map_token, KM_USER1);
			leaf->map_token = NULL;
		}

		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot),
			      btrfs_item_nr_offset(slot + nr),
			      sizeof(struct btrfs_item) *
			      (nritems - slot - nr));
	}
	btrfs_set_header_nritems(leaf, nritems - nr);
	nritems -= nr;

	/* delete the leaf if we've emptied it */
	if (nritems == 0) {
		if (leaf == root->node) {
			btrfs_set_header_level(leaf, 0);
		} else {
			ret = btrfs_del_leaf(trans, root, path, leaf->start);
			BUG_ON(ret);
		}
	} else {
		int used = leaf_space_used(leaf, 0, nritems);
		if (slot == 0) {
			struct btrfs_disk_key disk_key;

			btrfs_item_key(leaf, &disk_key, 0);
			wret = fixup_low_keys(trans, root, path,
					      &disk_key, 1);
			if (wret)
				ret = wret;
		}

		/* delete the leaf if it is mostly empty */
		if (used < BTRFS_LEAF_DATA_SIZE(root) / 4 &&
		    !trans->transaction->delayed_refs.flushing) {
			/* push_leaf_left fixes the path.
			 * make sure the path still points to our leaf
			 * for possible call to del_ptr below
			 */
			slot = path->slots[1];
			extent_buffer_get(leaf);

			btrfs_set_path_blocking(path);
			wret = push_leaf_left(trans, root, path, 1, 1);
			if (wret < 0 && wret != -ENOSPC)
				ret = wret;

			if (path->nodes[0] == leaf &&
			    btrfs_header_nritems(leaf)) {
				wret = push_leaf_right(trans, root, path, 1, 1);
				if (wret < 0 && wret != -ENOSPC)
					ret = wret;
			}

			if (btrfs_header_nritems(leaf) == 0) {
				path->slots[1] = slot;
				ret = btrfs_del_leaf(trans, root, path,
						     leaf->start);
				BUG_ON(ret);
				free_extent_buffer(leaf);
			} else {
				/* if we're still in the path, make sure
				 * we're dirty.  Otherwise, one of the
				 * push_leaf functions must have already
				 * dirtied this buffer
				 */
				if (path->nodes[0] == leaf)
					btrfs_mark_buffer_dirty(leaf);
				free_extent_buffer(leaf);
			}
		} else {
			btrfs_mark_buffer_dirty(leaf);
		}
	}
	return ret;
}

/*
 * search the tree again to find a leaf with lesser keys
 * returns 0 if it found something or 1 if there are no lesser leaves.
 * returns < 0 on io errors.
 *
 * This may release the path, and so you may lose any locks held at the
 * time you call it.
 */
int btrfs_prev_leaf(struct btrfs_root *root, struct btrfs_path *path)
{
	struct btrfs_key key;
	struct btrfs_disk_key found_key;
	int ret;

	btrfs_item_key_to_cpu(path->nodes[0], &key, 0);

	if (key.offset > 0)
		key.offset--;
	else if (key.type > 0)
		key.type--;
	else if (key.objectid > 0)
		key.objectid--;
	else
		return 1;

	btrfs_release_path(root, path);
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		return ret;
	btrfs_item_key(path->nodes[0], &found_key, 0);
	ret = comp_keys(&found_key, &key);
	if (ret < 0)
		return 0;
	return 1;
}

/*
 * A helper function to walk down the tree starting at min_key, and looking
 * for nodes or leaves that are either in cache or have a minimum
 * transaction id.  This is used by the btree defrag code, and tree logging
 *
 * This does not cow, but it does stuff the starting key it finds back
 * into min_key, so you can call btrfs_search_slot with cow=1 on the
 * key and get a writable path.
 *
 * This does lock as it descends, and path->keep_locks should be set
 * to 1 by the caller.
 *
 * This honors path->lowest_level to prevent descent past a given level
 * of the tree.
 *
 * min_trans indicates the oldest transaction that you are interested
 * in walking through.  Any nodes or leaves older than min_trans are
 * skipped over (without reading them).
 *
 * returns zero if something useful was found, < 0 on error and 1 if there
 * was nothing in the tree that matched the search criteria.
 */
int btrfs_search_forward(struct btrfs_root *root, struct btrfs_key *min_key,
			 struct btrfs_key *max_key,
			 struct btrfs_path *path, int cache_only,
			 u64 min_trans)
{
	struct extent_buffer *cur;
	struct btrfs_key found_key;
	int slot;
	int sret;
	u32 nritems;
	int level;
	int ret = 1;

	WARN_ON(!path->keep_locks);
again:
	cur = btrfs_lock_root_node(root);
	level = btrfs_header_level(cur);
	WARN_ON(path->nodes[level]);
	path->nodes[level] = cur;
	path->locks[level] = 1;

	if (btrfs_header_generation(cur) < min_trans) {
		ret = 1;
		goto out;
	}
	while (1) {
		nritems = btrfs_header_nritems(cur);
		level = btrfs_header_level(cur);
		sret = bin_search(cur, min_key, level, &slot);

		/* at the lowest level, we're done, setup the path and exit */
		if (level == path->lowest_level) {
			if (slot >= nritems)
				goto find_next_key;
			ret = 0;
			path->slots[level] = slot;
			btrfs_item_key_to_cpu(cur, &found_key, slot);
			goto out;
		}
		if (sret && slot > 0)
			slot--;
		/*
		 * check this node pointer against the cache_only and
		 * min_trans parameters.  If it isn't in cache or is too
		 * old, skip to the next one.
		 */
		while (slot < nritems) {
			u64 blockptr;
			u64 gen;
			struct extent_buffer *tmp;
			struct btrfs_disk_key disk_key;

			blockptr = btrfs_node_blockptr(cur, slot);
			gen = btrfs_node_ptr_generation(cur, slot);
			if (gen < min_trans) {
				slot++;
				continue;
			}
			if (!cache_only)
				break;

			if (max_key) {
				btrfs_node_key(cur, &disk_key, slot);
				if (comp_keys(&disk_key, max_key) >= 0) {
					ret = 1;
					goto out;
				}
			}

			tmp = btrfs_find_tree_block(root, blockptr,
					    btrfs_level_size(root, level - 1));

			if (tmp && btrfs_buffer_uptodate(tmp, gen)) {
				free_extent_buffer(tmp);
				break;
			}
			if (tmp)
				free_extent_buffer(tmp);
			slot++;
		}
find_next_key:
		/*
		 * we didn't find a candidate key in this node, walk forward
		 * and find another one
		 */
		if (slot >= nritems) {
			path->slots[level] = slot;
			btrfs_set_path_blocking(path);
			sret = btrfs_find_next_key(root, path, min_key, level,
						  cache_only, min_trans);
			if (sret == 0) {
				btrfs_release_path(root, path);
				goto again;
			} else {
				goto out;
			}
		}
		/* save our key for returning back */
		btrfs_node_key_to_cpu(cur, &found_key, slot);
		path->slots[level] = slot;
		if (level == path->lowest_level) {
			ret = 0;
			unlock_up(path, level, 1);
			goto out;
		}
		btrfs_set_path_blocking(path);
		cur = read_node_slot(root, cur, slot);

		btrfs_tree_lock(cur);

		path->locks[level - 1] = 1;
		path->nodes[level - 1] = cur;
		unlock_up(path, level, 1);
		btrfs_clear_path_blocking(path, NULL);
	}
out:
	if (ret == 0)
		memcpy(min_key, &found_key, sizeof(found_key));
	btrfs_set_path_blocking(path);
	return ret;
}

/*
 * this is similar to btrfs_next_leaf, but does not try to preserve
 * and fixup the path.  It looks for and returns the next key in the
 * tree based on the current path and the cache_only and min_trans
 * parameters.
 *
 * 0 is returned if another key is found, < 0 if there are any errors
 * and 1 is returned if there are no higher keys in the tree
 *
 * path->keep_locks should be set to 1 on the search made before
 * calling this function.
 */
int btrfs_find_next_key(struct btrfs_root *root, struct btrfs_path *path,
			struct btrfs_key *key, int lowest_level,
			int cache_only, u64 min_trans)
{
	int level = lowest_level;
	int slot;
	struct extent_buffer *c;

	WARN_ON(!path->keep_locks);
	while (level < BTRFS_MAX_LEVEL) {
		if (!path->nodes[level])
			return 1;

		slot = path->slots[level] + 1;
		c = path->nodes[level];
next:
		if (slot >= btrfs_header_nritems(c)) {
			level++;
			if (level == BTRFS_MAX_LEVEL)
				return 1;
			continue;
		}
		if (level == 0)
			btrfs_item_key_to_cpu(c, key, slot);
		else {
			u64 blockptr = btrfs_node_blockptr(c, slot);
			u64 gen = btrfs_node_ptr_generation(c, slot);

			if (cache_only) {
				struct extent_buffer *cur;
				cur = btrfs_find_tree_block(root, blockptr,
					    btrfs_level_size(root, level - 1));
				if (!cur || !btrfs_buffer_uptodate(cur, gen)) {
					slot++;
					if (cur)
						free_extent_buffer(cur);
					goto next;
				}
				free_extent_buffer(cur);
			}
			if (gen < min_trans) {
				slot++;
				goto next;
			}
			btrfs_node_key_to_cpu(c, key, slot);
		}
		return 0;
	}
	return 1;
}

/*
 * search the tree again to find a leaf with greater keys
 * returns 0 if it found something or 1 if there are no greater leaves.
 * returns < 0 on io errors.
 */
int btrfs_next_leaf(struct btrfs_root *root, struct btrfs_path *path)
{
	int slot;
	int level;
	struct extent_buffer *c;
	struct extent_buffer *next;
	struct btrfs_key key;
	u32 nritems;
	int ret;
	int old_spinning = path->leave_spinning;
	int force_blocking = 0;

	nritems = btrfs_header_nritems(path->nodes[0]);
	if (nritems == 0)
		return 1;

	/*
	 * we take the blocks in an order that upsets lockdep.  Using
	 * blocking mode is the only way around it.
	 */
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	force_blocking = 1;
#endif

	btrfs_item_key_to_cpu(path->nodes[0], &key, nritems - 1);
again:
	level = 1;
	next = NULL;
	btrfs_release_path(root, path);

	path->keep_locks = 1;

	if (!force_blocking)
		path->leave_spinning = 1;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	path->keep_locks = 0;

	if (ret < 0)
		return ret;

	nritems = btrfs_header_nritems(path->nodes[0]);
	/*
	 * by releasing the path above we dropped all our locks.  A balance
	 * could have added more items next to the key that used to be
	 * at the very end of the block.  So, check again here and
	 * advance the path if there are now more items available.
	 */
	if (nritems > 0 && path->slots[0] < nritems - 1) {
		path->slots[0]++;
		ret = 0;
		goto done;
	}

	while (level < BTRFS_MAX_LEVEL) {
		if (!path->nodes[level]) {
			ret = 1;
			goto done;
		}

		slot = path->slots[level] + 1;
		c = path->nodes[level];
		if (slot >= btrfs_header_nritems(c)) {
			level++;
			if (level == BTRFS_MAX_LEVEL) {
				ret = 1;
				goto done;
			}
			continue;
		}

		if (next) {
			btrfs_tree_unlock(next);
			free_extent_buffer(next);
		}

		next = c;
		ret = read_block_for_search(NULL, root, path, &next, level,
					    slot, &key);
		if (ret == -EAGAIN)
			goto again;

		if (!path->skip_locking) {
			ret = btrfs_try_spin_lock(next);
			if (!ret) {
				btrfs_set_path_blocking(path);
				btrfs_tree_lock(next);
				if (!force_blocking)
					btrfs_clear_path_blocking(path, next);
			}
			if (force_blocking)
				btrfs_set_lock_blocking(next);
		}
		break;
	}
	path->slots[level] = slot;
	while (1) {
		level--;
		c = path->nodes[level];
		if (path->locks[level])
			btrfs_tree_unlock(c);

		free_extent_buffer(c);
		path->nodes[level] = next;
		path->slots[level] = 0;
		if (!path->skip_locking)
			path->locks[level] = 1;

		if (!level)
			break;

		ret = read_block_for_search(NULL, root, path, &next, level,
					    0, &key);
		if (ret == -EAGAIN)
			goto again;

		if (!path->skip_locking) {
			btrfs_assert_tree_locked(path->nodes[level]);
			ret = btrfs_try_spin_lock(next);
			if (!ret) {
				btrfs_set_path_blocking(path);
				btrfs_tree_lock(next);
				if (!force_blocking)
					btrfs_clear_path_blocking(path, next);
			}
			if (force_blocking)
				btrfs_set_lock_blocking(next);
		}
	}
	ret = 0;
done:
	unlock_up(path, 0, 1);
	path->leave_spinning = old_spinning;
	if (!old_spinning)
		btrfs_set_path_blocking(path);

	return ret;
}

/*
 * this uses btrfs_prev_leaf to walk backwards in the tree, and keeps
 * searching until it gets past min_objectid or finds an item of 'type'
 *
 * returns 0 if something is found, 1 if nothing was found and < 0 on error
 */
int btrfs_previous_item(struct btrfs_root *root,
			struct btrfs_path *path, u64 min_objectid,
			int type)
{
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	u32 nritems;
	int ret;

	while (1) {
		if (path->slots[0] == 0) {
			btrfs_set_path_blocking(path);
			ret = btrfs_prev_leaf(root, path);
			if (ret != 0)
				return ret;
		} else {
			path->slots[0]--;
		}
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (nritems == 0)
			return 1;
		if (path->slots[0] == nritems)
			path->slots[0]--;

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.type == type)
			return 0;
		if (found_key.objectid < min_objectid)
			break;
		if (found_key.objectid == min_objectid &&
		    found_key.type < type)
			break;
	}
	return 1;
}
