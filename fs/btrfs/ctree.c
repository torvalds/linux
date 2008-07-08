/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
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

inline void btrfs_init_path(struct btrfs_path *p)
{
	memset(p, 0, sizeof(*p));
}

struct btrfs_path *btrfs_alloc_path(void)
{
	struct btrfs_path *path;
	path = kmem_cache_alloc(btrfs_path_cachep, GFP_NOFS);
	if (path) {
		btrfs_init_path(path);
		path->reada = 1;
	}
	return path;
}

void btrfs_free_path(struct btrfs_path *p)
{
	btrfs_release_path(NULL, p);
	kmem_cache_free(btrfs_path_cachep, p);
}

void btrfs_release_path(struct btrfs_root *root, struct btrfs_path *p)
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

struct extent_buffer *btrfs_root_node(struct btrfs_root *root)
{
	struct extent_buffer *eb;
	spin_lock(&root->node_lock);
	eb = root->node;
	extent_buffer_get(eb);
	spin_unlock(&root->node_lock);
	return eb;
}

struct extent_buffer *btrfs_lock_root_node(struct btrfs_root *root)
{
	struct extent_buffer *eb;

	while(1) {
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

static void add_root_to_dirty_list(struct btrfs_root *root)
{
	if (root->track_dirty && list_empty(&root->dirty_list)) {
		list_add(&root->dirty_list,
			 &root->fs_info->dirty_cowonly_roots);
	}
}

int btrfs_copy_root(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root,
		      struct extent_buffer *buf,
		      struct extent_buffer **cow_ret, u64 new_root_objectid)
{
	struct extent_buffer *cow;
	u32 nritems;
	int ret = 0;
	int level;
	struct btrfs_key first_key;
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
	if (nritems) {
		if (level == 0)
			btrfs_item_key_to_cpu(buf, &first_key, 0);
		else
			btrfs_node_key_to_cpu(buf, &first_key, 0);
	} else {
		first_key.objectid = 0;
	}
	cow = btrfs_alloc_free_block(trans, new_root, buf->len,
				       new_root_objectid,
				       trans->transid, first_key.objectid,
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

	WARN_ON(btrfs_header_generation(buf) > trans->transid);
	ret = btrfs_inc_ref(trans, new_root, buf);
	kfree(new_root);

	if (ret)
		return ret;

	btrfs_mark_buffer_dirty(cow);
	*cow_ret = cow;
	return 0;
}

int __btrfs_cow_block(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct extent_buffer *buf,
			     struct extent_buffer *parent, int parent_slot,
			     struct extent_buffer **cow_ret,
			     u64 search_start, u64 empty_size)
{
	u64 root_gen;
	struct extent_buffer *cow;
	u32 nritems;
	int ret = 0;
	int different_trans = 0;
	int level;
	int unlock_orig = 0;
	struct btrfs_key first_key;

	if (*cow_ret == buf)
		unlock_orig = 1;

	WARN_ON(!btrfs_tree_locked(buf));

	if (root->ref_cows) {
		root_gen = trans->transid;
	} else {
		root_gen = 0;
	}
	WARN_ON(root->ref_cows && trans->transid !=
		root->fs_info->running_transaction->transid);
	WARN_ON(root->ref_cows && trans->transid != root->last_trans);

	level = btrfs_header_level(buf);
	nritems = btrfs_header_nritems(buf);
	if (nritems) {
		if (level == 0)
			btrfs_item_key_to_cpu(buf, &first_key, 0);
		else
			btrfs_node_key_to_cpu(buf, &first_key, 0);
	} else {
		first_key.objectid = 0;
	}
	cow = btrfs_alloc_free_block(trans, root, buf->len,
				     root->root_key.objectid,
				     root_gen, first_key.objectid, level,
				     search_start, empty_size);
	if (IS_ERR(cow))
		return PTR_ERR(cow);

	copy_extent_buffer(cow, buf, 0, 0, cow->len);
	btrfs_set_header_bytenr(cow, cow->start);
	btrfs_set_header_generation(cow, trans->transid);
	btrfs_set_header_owner(cow, root->root_key.objectid);
	btrfs_clear_header_flag(cow, BTRFS_HEADER_FLAG_WRITTEN);

	WARN_ON(btrfs_header_generation(buf) > trans->transid);
	if (btrfs_header_generation(buf) != trans->transid) {
		different_trans = 1;
		ret = btrfs_inc_ref(trans, root, buf);
		if (ret)
			return ret;
	} else {
		clean_tree_block(trans, root, buf);
	}

	if (buf == root->node) {
		WARN_ON(parent && parent != buf);
		root_gen = btrfs_header_generation(buf);

		spin_lock(&root->node_lock);
		root->node = cow;
		extent_buffer_get(cow);
		spin_unlock(&root->node_lock);

		if (buf != root->commit_root) {
			btrfs_free_extent(trans, root, buf->start,
					  buf->len, root->root_key.objectid,
					  root_gen, 0, 0, 1);
		}
		free_extent_buffer(buf);
		add_root_to_dirty_list(root);
	} else {
		root_gen = btrfs_header_generation(parent);
		btrfs_set_node_blockptr(parent, parent_slot,
					cow->start);
		WARN_ON(trans->transid == 0);
		btrfs_set_node_ptr_generation(parent, parent_slot,
					      trans->transid);
		btrfs_mark_buffer_dirty(parent);
		WARN_ON(btrfs_header_generation(parent) != trans->transid);
		btrfs_free_extent(trans, root, buf->start, buf->len,
				  btrfs_header_owner(parent), root_gen,
				  0, 0, 1);
	}
	if (unlock_orig)
		btrfs_tree_unlock(buf);
	free_extent_buffer(buf);
	btrfs_mark_buffer_dirty(cow);
	*cow_ret = cow;
	return 0;
}

int btrfs_cow_block(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, struct extent_buffer *buf,
		    struct extent_buffer *parent, int parent_slot,
		    struct extent_buffer **cow_ret)
{
	u64 search_start;
	u64 header_trans;
	int ret;

	if (trans->transaction != root->fs_info->running_transaction) {
		printk(KERN_CRIT "trans %Lu running %Lu\n", trans->transid,
		       root->fs_info->running_transaction->transid);
		WARN_ON(1);
	}
	if (trans->transid != root->fs_info->generation) {
		printk(KERN_CRIT "trans %Lu running %Lu\n", trans->transid,
		       root->fs_info->generation);
		WARN_ON(1);
	}

	header_trans = btrfs_header_generation(buf);
	spin_lock(&root->fs_info->hash_lock);
	if (header_trans == trans->transid &&
	    !btrfs_header_flag(buf, BTRFS_HEADER_FLAG_WRITTEN)) {
		*cow_ret = buf;
		spin_unlock(&root->fs_info->hash_lock);
		return 0;
	}
	spin_unlock(&root->fs_info->hash_lock);
	search_start = buf->start & ~((u64)(1024 * 1024 * 1024) - 1);
	ret = __btrfs_cow_block(trans, root, buf, parent,
				 parent_slot, cow_ret, search_start, 0);
	return ret;
}

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

	if (trans->transaction != root->fs_info->running_transaction) {
		printk(KERN_CRIT "trans %Lu running %Lu\n", trans->transid,
		       root->fs_info->running_transaction->transid);
		WARN_ON(1);
	}
	if (trans->transid != root->fs_info->generation) {
		printk(KERN_CRIT "trans %Lu running %Lu\n", trans->transid,
		       root->fs_info->generation);
		WARN_ON(1);
	}

	parent_nritems = btrfs_header_nritems(parent);
	blocksize = btrfs_level_size(root, parent_level - 1);
	end_slot = parent_nritems;

	if (parent_nritems == 1)
		return 0;

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
#if 0
	for (i = 0; nritems > 1 && i < nritems - 2; i++) {
		btrfs_item_key_to_cpu(leaf, &cpukey, i + 1);
		btrfs_item_key(leaf, &leaf_key, i);
		if (comp_keys(&leaf_key, &cpukey) >= 0) {
			btrfs_print_leaf(root, leaf);
			printk("slot %d offset bad key\n", i);
			BUG_ON(1);
		}
		if (btrfs_item_offset_nr(leaf, i) !=
			btrfs_item_end_nr(leaf, i + 1)) {
			btrfs_print_leaf(root, leaf);
			printk("slot %d offset bad\n", i);
			BUG_ON(1);
		}
		if (i == 0) {
			if (btrfs_item_offset_nr(leaf, i) +
			       btrfs_item_size_nr(leaf, i) !=
			       BTRFS_LEAF_DATA_SIZE(root)) {
				btrfs_print_leaf(root, leaf);
				printk("slot %d first offset bad\n", i);
				BUG_ON(1);
			}
		}
	}
	if (nritems > 0) {
		if (btrfs_item_size_nr(leaf, nritems - 1) > 4096) {
				btrfs_print_leaf(root, leaf);
				printk("slot %d bad size \n", nritems - 1);
				BUG_ON(1);
		}
	}
#endif
	if (slot != 0 && slot < nritems - 1) {
		btrfs_item_key(leaf, &leaf_key, slot);
		btrfs_item_key_to_cpu(leaf, &cpukey, slot - 1);
		if (comp_keys(&leaf_key, &cpukey) <= 0) {
			btrfs_print_leaf(root, leaf);
			printk("slot %d offset bad key\n", slot);
			BUG_ON(1);
		}
		if (btrfs_item_offset_nr(leaf, slot - 1) !=
		       btrfs_item_end_nr(leaf, slot)) {
			btrfs_print_leaf(root, leaf);
			printk("slot %d offset bad\n", slot);
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
			printk("slot %d offset bad\n", slot);
			BUG_ON(1);
		}
	}
	BUG_ON(btrfs_item_offset_nr(leaf, 0) +
	       btrfs_item_size_nr(leaf, 0) != BTRFS_LEAF_DATA_SIZE(root));
	return 0;
}

static int noinline check_block(struct btrfs_root *root,
				struct btrfs_path *path, int level)
{
	u64 found_start;
	return 0;
	if (btrfs_header_level(path->nodes[level]) != level)
	    printk("warning: bad level %Lu wanted %d found %d\n",
		   path->nodes[level]->start, level,
		   btrfs_header_level(path->nodes[level]));
	found_start = btrfs_header_bytenr(path->nodes[level]);
	if (found_start != path->nodes[level]->start) {
	    printk("warning: bad bytentr %Lu found %Lu\n",
		   path->nodes[level]->start, found_start);
	}
#if 0
	struct extent_buffer *buf = path->nodes[level];

	if (memcmp_extent_buffer(buf, root->fs_info->fsid,
				 (unsigned long)btrfs_header_fsid(buf),
				 BTRFS_FSID_SIZE)) {
		printk("warning bad block %Lu\n", buf->start);
		return 1;
	}
#endif
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
static int generic_bin_search(struct extent_buffer *eb, unsigned long p,
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

	while(low < high) {
		mid = (low + high) / 2;
		offset = p + mid * item_size;

		if (!map_token || offset < map_start ||
		    (offset + sizeof(struct btrfs_disk_key)) >
		    map_start + map_len) {
			if (map_token) {
				unmap_extent_buffer(eb, map_token, KM_USER0);
				map_token = NULL;
			}
			err = map_extent_buffer(eb, offset,
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

static struct extent_buffer *read_node_slot(struct btrfs_root *root,
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

static int balance_level(struct btrfs_trans_handle *trans,
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
		btrfs_tree_lock(child);
		BUG_ON(!child);
		ret = btrfs_cow_block(trans, root, child, mid, 0, &child);
		BUG_ON(ret);

		spin_lock(&root->node_lock);
		root->node = child;
		spin_unlock(&root->node_lock);

		add_root_to_dirty_list(root);
		btrfs_tree_unlock(child);
		path->locks[level] = 0;
		path->nodes[level] = NULL;
		clean_tree_block(trans, root, mid);
		btrfs_tree_unlock(mid);
		/* once for the path */
		free_extent_buffer(mid);
		ret = btrfs_free_extent(trans, root, mid->start, mid->len,
					root->root_key.objectid,
					btrfs_header_generation(mid), 0, 0, 1);
		/* once for the root ptr */
		free_extent_buffer(mid);
		return ret;
	}
	if (btrfs_header_nritems(mid) >
	    BTRFS_NODEPTRS_PER_BLOCK(root) / 4)
		return 0;

	if (btrfs_header_nritems(mid) < 2)
		err_on_enospc = 1;

	left = read_node_slot(root, parent, pslot - 1);
	if (left) {
		btrfs_tree_lock(left);
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
						 blocksize,
						 btrfs_header_owner(parent),
						 generation, 0, 0, 1);
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
					 btrfs_header_owner(parent),
					 root_gen, 0, 0, 1);
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

/* returns zero if the push worked, non-zero otherwise */
static int noinline push_nodes_for_insert(struct btrfs_trans_handle *trans,
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
 * readahead one full node of leaves
 */
static void reada_for_search(struct btrfs_root *root, struct btrfs_path *path,
			     int level, int slot, u64 objectid)
{
	struct extent_buffer *node;
	struct btrfs_disk_key disk_key;
	u32 nritems;
	u64 search;
	u64 lowest_read;
	u64 highest_read;
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

	highest_read = search;
	lowest_read = search;

	nritems = btrfs_header_nritems(node);
	nr = slot;
	while(1) {
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
		if ((search >= lowest_read && search <= highest_read) ||
		    (search < lowest_read && lowest_read - search <= 32768) ||
		    (search > highest_read && search - highest_read <= 32768)) {
			readahead_tree_block(root, search, blocksize,
				     btrfs_node_ptr_generation(node, nr));
			nread += blocksize;
		}
		nscan++;
		if (path->reada < 2 && (nread > (256 * 1024) || nscan > 32))
			break;
		if(nread > (1024 * 1024) || nscan > 128)
			break;

		if (search < lowest_read)
			lowest_read = search;
		if (search > highest_read)
			highest_read = search;
	}
}

static void unlock_up(struct btrfs_path *path, int level, int lowest_unlock)
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
	struct extent_buffer *tmp;
	int slot;
	int ret;
	int level;
	int should_reada = p->reada;
	int lowest_unlock = 1;
	int blocksize;
	u8 lowest_level = 0;
	u64 blocknr;
	u64 gen;

	lowest_level = p->lowest_level;
	WARN_ON(lowest_level && ins_len);
	WARN_ON(p->nodes[0] != NULL);
	WARN_ON(cow && root == root->fs_info->extent_root &&
		!mutex_is_locked(&root->fs_info->alloc_mutex));
	if (ins_len < 0)
		lowest_unlock = 2;
again:
	if (p->skip_locking)
		b = btrfs_root_node(root);
	else
		b = btrfs_lock_root_node(root);

	while (b) {
		level = btrfs_header_level(b);
		if (cow) {
			int wret;
			wret = btrfs_cow_block(trans, root, b,
					       p->nodes[level + 1],
					       p->slots[level + 1],
					       &b);
			if (wret) {
				free_extent_buffer(b);
				return wret;
			}
		}
		BUG_ON(!cow && ins_len);
		if (level != btrfs_header_level(b))
			WARN_ON(1);
		level = btrfs_header_level(b);
		p->nodes[level] = b;
		if (!p->skip_locking)
			p->locks[level] = 1;
		ret = check_block(root, p, level);
		if (ret)
			return -1;

		ret = bin_search(b, key, level, &slot);
		if (level != 0) {
			if (ret && slot > 0)
				slot -= 1;
			p->slots[level] = slot;
			if (ins_len > 0 && btrfs_header_nritems(b) >=
			    BTRFS_NODEPTRS_PER_BLOCK(root) - 3) {
				int sret = split_node(trans, root, p, level);
				BUG_ON(sret > 0);
				if (sret)
					return sret;
				b = p->nodes[level];
				slot = p->slots[level];
			} else if (ins_len < 0) {
				int sret = balance_level(trans, root, p,
							 level);
				if (sret)
					return sret;
				b = p->nodes[level];
				if (!b) {
					btrfs_release_path(NULL, p);
					goto again;
				}
				slot = p->slots[level];
				BUG_ON(btrfs_header_nritems(b) == 1);
			}
			unlock_up(p, level, lowest_unlock);

			/* this is only true while dropping a snapshot */
			if (level == lowest_level) {
				break;
			}

			blocknr = btrfs_node_blockptr(b, slot);
			gen = btrfs_node_ptr_generation(b, slot);
			blocksize = btrfs_level_size(root, level - 1);

			tmp = btrfs_find_tree_block(root, blocknr, blocksize);
			if (tmp && btrfs_buffer_uptodate(tmp, gen)) {
				b = tmp;
			} else {
				/*
				 * reduce lock contention at high levels
				 * of the btree by dropping locks before
				 * we read.
				 */
				if (level > 1) {
					btrfs_release_path(NULL, p);
					if (tmp)
						free_extent_buffer(tmp);
					if (should_reada)
						reada_for_search(root, p,
								 level, slot,
								 key->objectid);

					tmp = read_tree_block(root, blocknr,
							 blocksize, gen);
					if (tmp)
						free_extent_buffer(tmp);
					goto again;
				} else {
					if (tmp)
						free_extent_buffer(tmp);
					if (should_reada)
						reada_for_search(root, p,
								 level, slot,
								 key->objectid);
					b = read_node_slot(root, b, slot);
				}
			}
			if (!p->skip_locking)
				btrfs_tree_lock(b);
		} else {
			p->slots[level] = slot;
			if (ins_len > 0 && btrfs_leaf_free_space(root, b) <
			    sizeof(struct btrfs_item) + ins_len) {
				int sret = split_leaf(trans, root, key,
						      p, ins_len, ret == 0);
				BUG_ON(sret > 0);
				if (sret)
					return sret;
			}
			unlock_up(p, level, lowest_unlock);
			return ret;
		}
	}
	return 1;
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

	if (push_items <= 0) {
		return 1;
	}

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
	if (push_items <= 0) {
		return 1;
	}

	if (src_nritems < 4) {
		return 1;
	}

	max_push = src_nritems / 2 + 1;
	/* don't try to empty the node */
	if (max_push >= src_nritems) {
		return 1;
	}

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
	return ret;
}

/*
 * helper function to insert a new root level in the tree.
 * A new node is allocated, and a single item is inserted to
 * point to the existing root
 *
 * returns zero on success or < 0 on failure.
 */
static int noinline insert_new_root(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct btrfs_path *path, int level)
{
	u64 root_gen;
	u64 lower_gen;
	struct extent_buffer *lower;
	struct extent_buffer *c;
	struct extent_buffer *old;
	struct btrfs_disk_key lower_key;

	BUG_ON(path->nodes[level]);
	BUG_ON(path->nodes[level-1] != root->node);

	if (root->ref_cows)
		root_gen = trans->transid;
	else
		root_gen = 0;

	lower = path->nodes[level-1];
	if (level == 1)
		btrfs_item_key(lower, &lower_key, 0);
	else
		btrfs_node_key(lower, &lower_key, 0);

	c = btrfs_alloc_free_block(trans, root, root->nodesize,
				   root->root_key.objectid,
				   root_gen, lower_key.objectid, level,
				   root->node->start, 0);
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
	WARN_ON(lower_gen == 0);

	btrfs_set_node_ptr_generation(c, 0, lower_gen);

	btrfs_mark_buffer_dirty(c);

	spin_lock(&root->node_lock);
	old = root->node;
	root->node = c;
	spin_unlock(&root->node_lock);

	/* the super has an extra ref to root->node */
	free_extent_buffer(old);

	add_root_to_dirty_list(root);
	extent_buffer_get(c);
	path->nodes[level] = c;
	path->locks[level] = 1;
	path->slots[level] = 0;

	if (root->ref_cows && lower_gen != trans->transid) {
		struct btrfs_path *back_path = btrfs_alloc_path();
		int ret;
		mutex_lock(&root->fs_info->alloc_mutex);
		ret = btrfs_insert_extent_backref(trans,
						  root->fs_info->extent_root,
						  path, lower->start,
						  root->root_key.objectid,
						  trans->transid, 0, 0);
		BUG_ON(ret);
		mutex_unlock(&root->fs_info->alloc_mutex);
		btrfs_free_path(back_path);
	}
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
	if (slot > nritems)
		BUG();
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
static int split_node(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, int level)
{
	u64 root_gen;
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
	} else {
		ret = push_nodes_for_insert(trans, root, path, level);
		c = path->nodes[level];
		if (!ret && btrfs_header_nritems(c) <
		    BTRFS_NODEPTRS_PER_BLOCK(root) - 3)
			return 0;
		if (ret < 0)
			return ret;
	}

	c_nritems = btrfs_header_nritems(c);
	if (root->ref_cows)
		root_gen = trans->transid;
	else
		root_gen = 0;

	btrfs_node_key(c, &disk_key, 0);
	split = btrfs_alloc_free_block(trans, root, root->nodesize,
					 root->root_key.objectid,
					 root_gen,
					 btrfs_disk_key_objectid(&disk_key),
					 level, c->start, 0);
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
int btrfs_leaf_free_space(struct btrfs_root *root, struct extent_buffer *leaf)
{
	int nritems = btrfs_header_nritems(leaf);
	int ret;
	ret = BTRFS_LEAF_DATA_SIZE(root) - leaf_space_used(leaf, 0, nritems);
	if (ret < 0) {
		printk("leaf free space ret %d, leaf data size %lu, used %d nritems %d\n",
		       ret, (unsigned long) BTRFS_LEAF_DATA_SIZE(root),
		       leaf_space_used(leaf, 0, nritems), nritems);
	}
	return ret;
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
	struct btrfs_disk_key disk_key;
	int slot;
	u32 i;
	int free_space;
	int push_space = 0;
	int push_items = 0;
	struct btrfs_item *item;
	u32 left_nritems;
	u32 nr;
	u32 right_nritems;
	u32 data_end;
	u32 this_item_size;
	int ret;

	slot = path->slots[1];
	if (!path->nodes[1]) {
		return 1;
	}
	upper = path->nodes[1];
	if (slot >= btrfs_header_nritems(upper) - 1)
		return 1;

	WARN_ON(!btrfs_tree_locked(path->nodes[1]));

	right = read_node_slot(root, upper, slot + 1);
	btrfs_tree_lock(right);
	free_space = btrfs_leaf_free_space(root, right);
	if (free_space < data_size + sizeof(struct btrfs_item))
		goto out_unlock;

	/* cow and double check */
	ret = btrfs_cow_block(trans, root, right, upper,
			      slot + 1, &right);
	if (ret)
		goto out_unlock;

	free_space = btrfs_leaf_free_space(root, right);
	if (free_space < data_size + sizeof(struct btrfs_item))
		goto out_unlock;

	left_nritems = btrfs_header_nritems(left);
	if (left_nritems == 0)
		goto out_unlock;

	if (empty)
		nr = 0;
	else
		nr = 1;

	i = left_nritems - 1;
	while (i >= nr) {
		item = btrfs_item_nr(left, i);

		if (path->slots[0] == i)
			push_space += data_size + sizeof(*item);

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
 * push some data in the path leaf to the left, trying to free up at
 * least data_size bytes.  returns zero if the push worked, nonzero otherwise
 */
static int push_leaf_left(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, struct btrfs_path *path, int data_size,
			  int empty)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *right = path->nodes[0];
	struct extent_buffer *left;
	int slot;
	int i;
	int free_space;
	int push_space = 0;
	int push_items = 0;
	struct btrfs_item *item;
	u32 old_left_nritems;
	u32 right_nritems;
	u32 nr;
	int ret = 0;
	int wret;
	u32 this_item_size;
	u32 old_left_item_size;

	slot = path->slots[1];
	if (slot == 0)
		return 1;
	if (!path->nodes[1])
		return 1;

	right_nritems = btrfs_header_nritems(right);
	if (right_nritems == 0) {
		return 1;
	}

	WARN_ON(!btrfs_tree_locked(path->nodes[1]));

	left = read_node_slot(root, path->nodes[1], slot - 1);
	btrfs_tree_lock(left);
	free_space = btrfs_leaf_free_space(root, left);
	if (free_space < data_size + sizeof(struct btrfs_item)) {
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
	if (free_space < data_size + sizeof(struct btrfs_item)) {
		ret = 1;
		goto out;
	}

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

		if (path->slots[0] == i)
			push_space += data_size + sizeof(*item);

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
		     btrfs_item_offset_nr(right, push_items -1);

	copy_extent_buffer(left, right, btrfs_leaf_data(left) +
		     leaf_data_end(root, left) - push_space,
		     btrfs_leaf_data(right) +
		     btrfs_item_offset_nr(right, push_items - 1),
		     push_space);
	old_left_nritems = btrfs_header_nritems(left);
	BUG_ON(old_left_nritems < 0);

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
		printk("push items %d nr %u\n", push_items, right_nritems);
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
 * split the path's leaf in two, making sure there is at least data_size
 * available for the resulting leaf level of the path.
 *
 * returns 0 if all went well and < 0 on failure.
 */
static int split_leaf(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *ins_key,
		      struct btrfs_path *path, int data_size, int extend)
{
	u64 root_gen;
	struct extent_buffer *l;
	u32 nritems;
	int mid;
	int slot;
	struct extent_buffer *right;
	int space_needed = data_size + sizeof(struct btrfs_item);
	int data_copy_size;
	int rt_data_off;
	int i;
	int ret = 0;
	int wret;
	int double_split;
	int num_doubles = 0;
	struct btrfs_disk_key disk_key;

	if (extend)
		space_needed = data_size;

	if (root->ref_cows)
		root_gen = trans->transid;
	else
		root_gen = 0;

	/* first try to make some room by pushing left and right */
	if (ins_key->type != BTRFS_DIR_ITEM_KEY) {
		wret = push_leaf_right(trans, root, path, data_size, 0);
		if (wret < 0) {
			return wret;
		}
		if (wret) {
			wret = push_leaf_left(trans, root, path, data_size, 0);
			if (wret < 0)
				return wret;
		}
		l = path->nodes[0];

		/* did the pushes work? */
		if (btrfs_leaf_free_space(root, l) >= space_needed)
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
	mid = (nritems + 1)/ 2;

	btrfs_item_key(l, &disk_key, 0);

	right = btrfs_alloc_free_block(trans, root, root->leafsize,
					 root->root_key.objectid,
					 root_gen, disk_key.objectid, 0,
					 l->start, 0);
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
		    leaf_space_used(l, mid, nritems - mid) + space_needed >
			BTRFS_LEAF_DATA_SIZE(root)) {
			if (slot >= nritems) {
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
			    space_needed > BTRFS_LEAF_DATA_SIZE(root)) {
				double_split = 1;
			}
		}
	} else {
		if (leaf_space_used(l, 0, mid + 1) + space_needed >
			BTRFS_LEAF_DATA_SIZE(root)) {
			if (!extend && slot == 0) {
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
			} else if (extend && slot == 0) {
				mid = 1;
			} else {
				mid = slot;
				if (mid != nritems &&
				    leaf_space_used(l, mid, nritems - mid) +
				    space_needed > BTRFS_LEAF_DATA_SIZE(root)) {
					double_split = 1;
				}
			}
		}
	}
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

	if (double_split) {
		BUG_ON(num_doubles != 0);
		num_doubles++;
		goto again;
	}
	return ret;
}

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
		printk("slot %d too large, nritems %d\n", slot, nritems);
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
 * Given a key and some data, insert an item into the tree.
 * This does all the path init required, making room in the tree if needed.
 */
int btrfs_insert_empty_items(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct btrfs_path *path,
			    struct btrfs_key *cpu_key, u32 *data_size,
			    int nr)
{
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	int ret = 0;
	int slot;
	int slot_orig;
	int i;
	u32 nritems;
	u32 total_size = 0;
	u32 total_data = 0;
	unsigned int data_end;
	struct btrfs_disk_key disk_key;

	for (i = 0; i < nr; i++) {
		total_data += data_size[i];
	}

	total_size = total_data + (nr - 1) * sizeof(struct btrfs_item);
	ret = btrfs_search_slot(trans, root, cpu_key, path, total_size, 1);
	if (ret == 0) {
		return -EEXIST;
	}
	if (ret < 0)
		goto out;

	slot_orig = path->slots[0];
	leaf = path->nodes[0];

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	if (btrfs_leaf_free_space(root, leaf) <
	    sizeof(struct btrfs_item) + total_size) {
		btrfs_print_leaf(root, leaf);
		printk("not enough freespace need %u have %d\n",
		       total_size, btrfs_leaf_free_space(root, leaf));
		BUG();
	}

	slot = path->slots[0];
	BUG_ON(slot < 0);

	if (slot != nritems) {
		int i;
		unsigned int old_data = btrfs_item_end_nr(leaf, slot);

		if (old_data < data_end) {
			btrfs_print_leaf(root, leaf);
			printk("slot %d old_data %d data_end %d\n",
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
 * If the delete empties a node, the node is removed from the tree,
 * continuing all the way the root if required.  The root is converted into
 * a leaf if all the nodes are emptied.
 */
static int del_ptr(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_path *path, int level, int slot)
{
	struct extent_buffer *parent = path->nodes[level];
	u32 nritems;
	int ret = 0;
	int wret;

	nritems = btrfs_header_nritems(parent);
	if (slot != nritems -1) {
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
		int i;
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
			u64 root_gen = btrfs_header_generation(path->nodes[1]);
			wret = del_ptr(trans, root, path, 1, path->slots[1]);
			if (wret)
				ret = wret;
			wret = btrfs_free_extent(trans, root,
					 leaf->start, leaf->len,
					 btrfs_header_owner(path->nodes[1]),
					 root_gen, 0, 0, 1);
			if (wret)
				ret = wret;
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
		if (used < BTRFS_LEAF_DATA_SIZE(root) / 4) {
			/* push_leaf_left fixes the path.
			 * make sure the path still points to our leaf
			 * for possible call to del_ptr below
			 */
			slot = path->slots[1];
			extent_buffer_get(leaf);

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
				u64 root_gen;
				u64 bytenr = leaf->start;
				u32 blocksize = leaf->len;

				root_gen = btrfs_header_generation(
							   path->nodes[1]);

				wret = del_ptr(trans, root, path, 1, slot);
				if (wret)
					ret = wret;

				free_extent_buffer(leaf);
				wret = btrfs_free_extent(trans, root, bytenr,
					     blocksize,
					     btrfs_header_owner(path->nodes[1]),
					     root_gen, 0, 0, 1);
				if (wret)
					ret = wret;
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
 * transaction id.  This is used by the btree defrag code, but could
 * also be used to search for blocks that have changed since a given
 * transaction id.
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
 * returns zero if something useful was found, < 0 on error and 1 if there
 * was nothing in the tree that matched the search criteria.
 */
int btrfs_search_forward(struct btrfs_root *root, struct btrfs_key *min_key,
			 struct btrfs_path *path, int cache_only,
			 u64 min_trans)
{
	struct extent_buffer *cur;
	struct btrfs_key found_key;
	int slot;
	u32 nritems;
	int level;
	int ret = 1;

again:
	cur = btrfs_lock_root_node(root);
	level = btrfs_header_level(cur);
	path->nodes[level] = cur;
	path->locks[level] = 1;

	if (btrfs_header_generation(cur) < min_trans) {
		ret = 1;
		goto out;
	}
	while(1) {
		nritems = btrfs_header_nritems(cur);
		level = btrfs_header_level(cur);
		bin_search(cur, min_key, level, &slot);

		/* at level = 0, we're done, setup the path and exit */
		if (level == 0) {
			ret = 0;
			path->slots[level] = slot;
			btrfs_item_key_to_cpu(cur, &found_key, slot);
			goto out;
		}
		/*
		 * check this node pointer against the cache_only and
		 * min_trans parameters.  If it isn't in cache or is too
		 * old, skip to the next one.
		 */
		while(slot < nritems) {
			u64 blockptr;
			u64 gen;
			struct extent_buffer *tmp;
			blockptr = btrfs_node_blockptr(cur, slot);
			gen = btrfs_node_ptr_generation(cur, slot);
			if (gen < min_trans) {
				slot++;
				continue;
			}
			if (!cache_only)
				break;

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
		/*
		 * we didn't find a candidate key in this node, walk forward
		 * and find another one
		 */
		if (slot >= nritems) {
			ret = btrfs_find_next_key(root, path, min_key, level,
						  cache_only, min_trans);
			if (ret == 0) {
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
		cur = read_node_slot(root, cur, slot);

		btrfs_tree_lock(cur);
		path->locks[level - 1] = 1;
		path->nodes[level - 1] = cur;
		unlock_up(path, level, 1);
	}
out:
	if (ret == 0)
		memcpy(min_key, &found_key, sizeof(found_key));
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

	while(level < BTRFS_MAX_LEVEL) {
		if (!path->nodes[level])
			return 1;

		slot = path->slots[level] + 1;
		c = path->nodes[level];
next:
		if (slot >= btrfs_header_nritems(c)) {
			level++;
			if (level == BTRFS_MAX_LEVEL) {
				return 1;
			}
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
	int level = 1;
	struct extent_buffer *c;
	struct extent_buffer *next = NULL;
	struct btrfs_key key;
	u32 nritems;
	int ret;

	nritems = btrfs_header_nritems(path->nodes[0]);
	if (nritems == 0) {
		return 1;
	}

	btrfs_item_key_to_cpu(path->nodes[0], &key, nritems - 1);

	btrfs_release_path(root, path);
	path->keep_locks = 1;
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
		goto done;
	}

	while(level < BTRFS_MAX_LEVEL) {
		if (!path->nodes[level])
			return 1;

		slot = path->slots[level] + 1;
		c = path->nodes[level];
		if (slot >= btrfs_header_nritems(c)) {
			level++;
			if (level == BTRFS_MAX_LEVEL) {
				return 1;
			}
			continue;
		}

		if (next) {
			btrfs_tree_unlock(next);
			free_extent_buffer(next);
		}

		if (level == 1 && path->locks[1] && path->reada)
			reada_for_search(root, path, level, slot, 0);

		next = read_node_slot(root, c, slot);
		if (!path->skip_locking) {
			WARN_ON(!btrfs_tree_locked(c));
			btrfs_tree_lock(next);
		}
		break;
	}
	path->slots[level] = slot;
	while(1) {
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
		if (level == 1 && path->locks[1] && path->reada)
			reada_for_search(root, path, level, slot, 0);
		next = read_node_slot(root, next, 0);
		if (!path->skip_locking) {
			WARN_ON(!btrfs_tree_locked(path->nodes[level]));
			btrfs_tree_lock(next);
		}
	}
done:
	unlock_up(path, 0, 1);
	return 0;
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
	int ret;

	while(1) {
		if (path->slots[0] == 0) {
			ret = btrfs_prev_leaf(root, path);
			if (ret != 0)
				return ret;
		} else {
			path->slots[0]--;
		}
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.type == type)
			return 0;
	}
	return 1;
}

