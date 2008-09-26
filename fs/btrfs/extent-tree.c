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
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include "hash.h"
#include "crc32c.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"
#include "transaction.h"
#include "volumes.h"
#include "locking.h"
#include "ref-cache.h"

#define PENDING_EXTENT_INSERT 0
#define PENDING_EXTENT_DELETE 1
#define PENDING_BACKREF_UPDATE 2

struct pending_extent_op {
	int type;
	u64 bytenr;
	u64 num_bytes;
	u64 parent;
	u64 orig_parent;
	u64 generation;
	u64 orig_generation;
	int level;
};

static int finish_current_insert(struct btrfs_trans_handle *trans, struct
				 btrfs_root *extent_root);
static int del_pending_extents(struct btrfs_trans_handle *trans, struct
			       btrfs_root *extent_root);
static struct btrfs_block_group_cache *
__btrfs_find_block_group(struct btrfs_root *root,
			 struct btrfs_block_group_cache *hint,
			 u64 search_start, int data, int owner);

void maybe_lock_mutex(struct btrfs_root *root)
{
	if (root != root->fs_info->extent_root &&
	    root != root->fs_info->chunk_root &&
	    root != root->fs_info->dev_root) {
		mutex_lock(&root->fs_info->alloc_mutex);
	}
}

void maybe_unlock_mutex(struct btrfs_root *root)
{
	if (root != root->fs_info->extent_root &&
	    root != root->fs_info->chunk_root &&
	    root != root->fs_info->dev_root) {
		mutex_unlock(&root->fs_info->alloc_mutex);
	}
}

static int block_group_bits(struct btrfs_block_group_cache *cache, u64 bits)
{
	return (cache->flags & bits) == bits;
}

/*
 * this adds the block group to the fs_info rb tree for the block group
 * cache
 */
int btrfs_add_block_group_cache(struct btrfs_fs_info *info,
				struct btrfs_block_group_cache *block_group)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct btrfs_block_group_cache *cache;

	spin_lock(&info->block_group_cache_lock);
	p = &info->block_group_cache_tree.rb_node;

	while (*p) {
		parent = *p;
		cache = rb_entry(parent, struct btrfs_block_group_cache,
				 cache_node);
		if (block_group->key.objectid < cache->key.objectid) {
			p = &(*p)->rb_left;
		} else if (block_group->key.objectid > cache->key.objectid) {
			p = &(*p)->rb_right;
		} else {
			spin_unlock(&info->block_group_cache_lock);
			return -EEXIST;
		}
	}

	rb_link_node(&block_group->cache_node, parent, p);
	rb_insert_color(&block_group->cache_node,
			&info->block_group_cache_tree);
	spin_unlock(&info->block_group_cache_lock);

	return 0;
}

/*
 * This will return the block group at or after bytenr if contains is 0, else
 * it will return the block group that contains the bytenr
 */
static struct btrfs_block_group_cache *
block_group_cache_tree_search(struct btrfs_fs_info *info, u64 bytenr,
			      int contains)
{
	struct btrfs_block_group_cache *cache, *ret = NULL;
	struct rb_node *n;
	u64 end, start;

	spin_lock(&info->block_group_cache_lock);
	n = info->block_group_cache_tree.rb_node;

	while (n) {
		cache = rb_entry(n, struct btrfs_block_group_cache,
				 cache_node);
		end = cache->key.objectid + cache->key.offset - 1;
		start = cache->key.objectid;

		if (bytenr < start) {
			if (!contains && (!ret || start < ret->key.objectid))
				ret = cache;
			n = n->rb_left;
		} else if (bytenr > start) {
			if (contains && bytenr <= end) {
				ret = cache;
				break;
			}
			n = n->rb_right;
		} else {
			ret = cache;
			break;
		}
	}
	spin_unlock(&info->block_group_cache_lock);

	return ret;
}

/*
 * this is only called by cache_block_group, since we could have freed extents
 * we need to check the pinned_extents for any extents that can't be used yet
 * since their free space will be released as soon as the transaction commits.
 */
static int add_new_free_space(struct btrfs_block_group_cache *block_group,
			      struct btrfs_fs_info *info, u64 start, u64 end)
{
	u64 extent_start, extent_end, size;
	int ret;

	while (start < end) {
		ret = find_first_extent_bit(&info->pinned_extents, start,
					    &extent_start, &extent_end,
					    EXTENT_DIRTY);
		if (ret)
			break;

		if (extent_start == start) {
			start = extent_end + 1;
		} else if (extent_start > start && extent_start < end) {
			size = extent_start - start;
			ret = btrfs_add_free_space(block_group, start, size);
			BUG_ON(ret);
			start = extent_end + 1;
		} else {
			break;
		}
	}

	if (start < end) {
		size = end - start;
		ret = btrfs_add_free_space(block_group, start, size);
		BUG_ON(ret);
	}

	return 0;
}

static int cache_block_group(struct btrfs_root *root,
			     struct btrfs_block_group_cache *block_group)
{
	struct btrfs_path *path;
	int ret = 0;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	int slot;
	u64 last = 0;
	u64 first_free;
	int found = 0;

	if (!block_group)
		return 0;

	root = root->fs_info->extent_root;

	if (block_group->cached)
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 2;
	/*
	 * we get into deadlocks with paths held by callers of this function.
	 * since the alloc_mutex is protecting things right now, just
	 * skip the locking here
	 */
	path->skip_locking = 1;
	first_free = max_t(u64, block_group->key.objectid,
			   BTRFS_SUPER_INFO_OFFSET + BTRFS_SUPER_INFO_SIZE);
	key.objectid = block_group->key.objectid;
	key.offset = 0;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;
	ret = btrfs_previous_item(root, path, 0, BTRFS_EXTENT_ITEM_KEY);
	if (ret < 0)
		goto err;
	if (ret == 0) {
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		if (key.objectid + key.offset > first_free)
			first_free = key.objectid + key.offset;
	}
	while(1) {
		leaf = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto err;
			if (ret == 0)
				continue;
			else
				break;
		}
		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid < block_group->key.objectid)
			goto next;

		if (key.objectid >= block_group->key.objectid +
		    block_group->key.offset)
			break;

		if (btrfs_key_type(&key) == BTRFS_EXTENT_ITEM_KEY) {
			if (!found) {
				last = first_free;
				found = 1;
			}

			add_new_free_space(block_group, root->fs_info, last,
					   key.objectid);

			last = key.objectid + key.offset;
		}
next:
		path->slots[0]++;
	}

	if (!found)
		last = first_free;

	add_new_free_space(block_group, root->fs_info, last,
			   block_group->key.objectid +
			   block_group->key.offset);

	block_group->cached = 1;
	ret = 0;
err:
	btrfs_free_path(path);
	return ret;
}

/*
 * return the block group that starts at or after bytenr
 */
struct btrfs_block_group_cache *btrfs_lookup_first_block_group(struct
						       btrfs_fs_info *info,
							 u64 bytenr)
{
	struct btrfs_block_group_cache *cache;

	cache = block_group_cache_tree_search(info, bytenr, 0);

	return cache;
}

/*
 * return the block group that contains teh given bytenr
 */
struct btrfs_block_group_cache *btrfs_lookup_block_group(struct
							 btrfs_fs_info *info,
							 u64 bytenr)
{
	struct btrfs_block_group_cache *cache;

	cache = block_group_cache_tree_search(info, bytenr, 1);

	return cache;
}

static int noinline find_free_space(struct btrfs_root *root,
				    struct btrfs_block_group_cache **cache_ret,
				    u64 *start_ret, u64 num, int data)
{
	int ret;
	struct btrfs_block_group_cache *cache = *cache_ret;
	struct btrfs_free_space *info = NULL;
	u64 last;
	u64 search_start = *start_ret;

	WARN_ON(!mutex_is_locked(&root->fs_info->alloc_mutex));
	if (!cache)
		goto out;

	last = max(search_start, cache->key.objectid);

again:
	ret = cache_block_group(root, cache);
	if (ret)
		goto out;

	if (cache->ro || !block_group_bits(cache, data))
		goto new_group;

	info = btrfs_find_free_space(cache, last, num);
	if (info) {
		*start_ret = info->offset;
		return 0;
	}

new_group:
	last = cache->key.objectid + cache->key.offset;

	cache = btrfs_lookup_first_block_group(root->fs_info, last);
	if (!cache)
		goto out;

	*cache_ret = cache;
	goto again;

out:
	return -ENOSPC;
}

static u64 div_factor(u64 num, int factor)
{
	if (factor == 10)
		return num;
	num *= factor;
	do_div(num, 10);
	return num;
}

static struct btrfs_space_info *__find_space_info(struct btrfs_fs_info *info,
						  u64 flags)
{
	struct list_head *head = &info->space_info;
	struct list_head *cur;
	struct btrfs_space_info *found;
	list_for_each(cur, head) {
		found = list_entry(cur, struct btrfs_space_info, list);
		if (found->flags == flags)
			return found;
	}
	return NULL;
}

static struct btrfs_block_group_cache *
__btrfs_find_block_group(struct btrfs_root *root,
			 struct btrfs_block_group_cache *hint,
			 u64 search_start, int data, int owner)
{
	struct btrfs_block_group_cache *cache;
	struct btrfs_block_group_cache *found_group = NULL;
	struct btrfs_fs_info *info = root->fs_info;
	u64 used;
	u64 last = 0;
	u64 free_check;
	int full_search = 0;
	int factor = 10;
	int wrapped = 0;

	if (data & BTRFS_BLOCK_GROUP_METADATA)
		factor = 9;

	if (search_start) {
		struct btrfs_block_group_cache *shint;
		shint = btrfs_lookup_first_block_group(info, search_start);
		if (shint && block_group_bits(shint, data) && !shint->ro) {
			spin_lock(&shint->lock);
			used = btrfs_block_group_used(&shint->item);
			if (used + shint->pinned + shint->reserved <
			    div_factor(shint->key.offset, factor)) {
				spin_unlock(&shint->lock);
				return shint;
			}
			spin_unlock(&shint->lock);
		}
	}
	if (hint && !hint->ro && block_group_bits(hint, data)) {
		spin_lock(&hint->lock);
		used = btrfs_block_group_used(&hint->item);
		if (used + hint->pinned + hint->reserved <
		    div_factor(hint->key.offset, factor)) {
			spin_unlock(&hint->lock);
			return hint;
		}
		spin_unlock(&hint->lock);
		last = hint->key.objectid + hint->key.offset;
	} else {
		if (hint)
			last = max(hint->key.objectid, search_start);
		else
			last = search_start;
	}
again:
	while (1) {
		cache = btrfs_lookup_first_block_group(root->fs_info, last);
		if (!cache)
			break;

		spin_lock(&cache->lock);
		last = cache->key.objectid + cache->key.offset;
		used = btrfs_block_group_used(&cache->item);

		if (!cache->ro && block_group_bits(cache, data)) {
			free_check = div_factor(cache->key.offset, factor);
			if (used + cache->pinned + cache->reserved <
			    free_check) {
				found_group = cache;
				spin_unlock(&cache->lock);
				goto found;
			}
		}
		spin_unlock(&cache->lock);
		cond_resched();
	}
	if (!wrapped) {
		last = search_start;
		wrapped = 1;
		goto again;
	}
	if (!full_search && factor < 10) {
		last = search_start;
		full_search = 1;
		factor = 10;
		goto again;
	}
found:
	return found_group;
}

struct btrfs_block_group_cache *btrfs_find_block_group(struct btrfs_root *root,
						 struct btrfs_block_group_cache
						 *hint, u64 search_start,
						 int data, int owner)
{

	struct btrfs_block_group_cache *ret;
	ret = __btrfs_find_block_group(root, hint, search_start, data, owner);
	return ret;
}

/* simple helper to search for an existing extent at a given offset */
int btrfs_lookup_extent(struct btrfs_root *root, u64 start, u64 len)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_path *path;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	maybe_lock_mutex(root);
	key.objectid = start;
	key.offset = len;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	ret = btrfs_search_slot(NULL, root->fs_info->extent_root, &key, path,
				0, 0);
	maybe_unlock_mutex(root);
	btrfs_free_path(path);
	return ret;
}

/*
 * Back reference rules.  Back refs have three main goals:
 *
 * 1) differentiate between all holders of references to an extent so that
 *    when a reference is dropped we can make sure it was a valid reference
 *    before freeing the extent.
 *
 * 2) Provide enough information to quickly find the holders of an extent
 *    if we notice a given block is corrupted or bad.
 *
 * 3) Make it easy to migrate blocks for FS shrinking or storage pool
 *    maintenance.  This is actually the same as #2, but with a slightly
 *    different use case.
 *
 * File extents can be referenced by:
 *
 * - multiple snapshots, subvolumes, or different generations in one subvol
 * - different files inside a single subvolume
 * - different offsets inside a file (bookend extents in file.c)
 *
 * The extent ref structure has fields for:
 *
 * - Objectid of the subvolume root
 * - Generation number of the tree holding the reference
 * - objectid of the file holding the reference
 * - offset in the file corresponding to the key holding the reference
 * - number of references holding by parent node (alway 1 for tree blocks)
 *
 * Btree leaf may hold multiple references to a file extent. In most cases,
 * these references are from same file and the corresponding offsets inside
 * the file are close together. So inode objectid and offset in file are
 * just hints, they provide hints about where in the btree the references
 * can be found and when we can stop searching.
 *
 * When a file extent is allocated the fields are filled in:
 *     (root_key.objectid, trans->transid, inode objectid, offset in file, 1)
 *
 * When a leaf is cow'd new references are added for every file extent found
 * in the leaf.  It looks similar to the create case, but trans->transid will
 * be different when the block is cow'd.
 *
 *     (root_key.objectid, trans->transid, inode objectid, offset in file,
 *      number of references in the leaf)
 *
 * Because inode objectid and offset in file are just hints, they are not
 * used when backrefs are deleted. When a file extent is removed either
 * during snapshot deletion or file truncation, we find the corresponding
 * back back reference and check the following fields.
 *
 *     (btrfs_header_owner(leaf), btrfs_header_generation(leaf))
 *
 * Btree extents can be referenced by:
 *
 * - Different subvolumes
 * - Different generations of the same subvolume
 *
 * When a tree block is created, back references are inserted:
 *
 * (root->root_key.objectid, trans->transid, level, 0, 1)
 *
 * When a tree block is cow'd, new back references are added for all the
 * blocks it points to. If the tree block isn't in reference counted root,
 * the old back references are removed. These new back references are of
 * the form (trans->transid will have increased since creation):
 *
 * (root->root_key.objectid, trans->transid, level, 0, 1)
 *
 * When a backref is in deleting, the following fields are checked:
 *
 * if backref was for a tree root:
 *     (btrfs_header_owner(itself), btrfs_header_generation(itself))
 * else
 *     (btrfs_header_owner(parent), btrfs_header_generation(parent))
 *
 * Back Reference Key composing:
 *
 * The key objectid corresponds to the first byte in the extent, the key
 * type is set to BTRFS_EXTENT_REF_KEY, and the key offset is the first
 * byte of parent extent. If a extent is tree root, the key offset is set
 * to the key objectid.
 */

static int noinline lookup_extent_backref(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path, u64 bytenr,
					  u64 parent, u64 ref_root,
					  u64 ref_generation, int del)
{
	struct btrfs_key key;
	struct btrfs_extent_ref *ref;
	struct extent_buffer *leaf;
	int ret;

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_REF_KEY;
	key.offset = parent;

	ret = btrfs_search_slot(trans, root, &key, path, del ? -1 : 0, 1);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	leaf = path->nodes[0];
	ref = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_ref);
	if (btrfs_ref_root(leaf, ref) != ref_root ||
	    btrfs_ref_generation(leaf, ref) != ref_generation) {
		ret = -EIO;
		WARN_ON(1);
		goto out;
	}
	ret = 0;
out:
	return ret;
}

static int noinline insert_extent_backref(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path,
					  u64 bytenr, u64 parent,
					  u64 ref_root, u64 ref_generation,
					  u64 owner_objectid, u64 owner_offset)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_extent_ref *ref;
	u32 num_refs;
	int ret;

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_REF_KEY;
	key.offset = parent;

	ret = btrfs_insert_empty_item(trans, root, path, &key, sizeof(*ref));
	if (ret == 0) {
		leaf = path->nodes[0];
		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_ref);
		btrfs_set_ref_root(leaf, ref, ref_root);
		btrfs_set_ref_generation(leaf, ref, ref_generation);
		btrfs_set_ref_objectid(leaf, ref, owner_objectid);
		btrfs_set_ref_offset(leaf, ref, owner_offset);
		btrfs_set_ref_num_refs(leaf, ref, 1);
	} else if (ret == -EEXIST) {
		u64 existing_owner;
		BUG_ON(owner_objectid < BTRFS_FIRST_FREE_OBJECTID);
		leaf = path->nodes[0];
		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_ref);
		if (btrfs_ref_root(leaf, ref) != ref_root ||
		    btrfs_ref_generation(leaf, ref) != ref_generation) {
			ret = -EIO;
			WARN_ON(1);
			goto out;
		}

		num_refs = btrfs_ref_num_refs(leaf, ref);
		BUG_ON(num_refs == 0);
		btrfs_set_ref_num_refs(leaf, ref, num_refs + 1);

		existing_owner = btrfs_ref_objectid(leaf, ref);
		if (existing_owner == owner_objectid &&
		    btrfs_ref_offset(leaf, ref) > owner_offset) {
			btrfs_set_ref_offset(leaf, ref, owner_offset);
		} else if (existing_owner != owner_objectid &&
			   existing_owner != BTRFS_MULTIPLE_OBJECTIDS) {
			btrfs_set_ref_objectid(leaf, ref,
					BTRFS_MULTIPLE_OBJECTIDS);
			btrfs_set_ref_offset(leaf, ref, 0);
		}
		ret = 0;
	} else {
		goto out;
	}
	btrfs_mark_buffer_dirty(path->nodes[0]);
out:
	btrfs_release_path(root, path);
	return ret;
}

static int noinline remove_extent_backref(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path)
{
	struct extent_buffer *leaf;
	struct btrfs_extent_ref *ref;
	u32 num_refs;
	int ret = 0;

	leaf = path->nodes[0];
	ref = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_ref);
	num_refs = btrfs_ref_num_refs(leaf, ref);
	BUG_ON(num_refs == 0);
	num_refs -= 1;
	if (num_refs == 0) {
		ret = btrfs_del_item(trans, root, path);
	} else {
		btrfs_set_ref_num_refs(leaf, ref, num_refs);
		btrfs_mark_buffer_dirty(leaf);
	}
	btrfs_release_path(root, path);
	return ret;
}

static int __btrfs_update_extent_ref(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root, u64 bytenr,
				     u64 orig_parent, u64 parent,
				     u64 orig_root, u64 ref_root,
				     u64 orig_generation, u64 ref_generation,
				     u64 owner_objectid, u64 owner_offset)
{
	int ret;
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	struct btrfs_path *path;

	if (root == root->fs_info->extent_root) {
		struct pending_extent_op *extent_op;
		u64 num_bytes;

		BUG_ON(owner_objectid >= BTRFS_MAX_LEVEL);
		num_bytes = btrfs_level_size(root, (int)owner_objectid);
		if (test_range_bit(&root->fs_info->extent_ins, bytenr,
				bytenr + num_bytes - 1, EXTENT_LOCKED, 0)) {
			u64 priv;
			ret = get_state_private(&root->fs_info->extent_ins,
						bytenr, &priv);
			BUG_ON(ret);
			extent_op = (struct pending_extent_op *)
							(unsigned long)priv;
			BUG_ON(extent_op->parent != orig_parent);
			BUG_ON(extent_op->generation != orig_generation);
			extent_op->parent = parent;
			extent_op->generation = ref_generation;
		} else {
			extent_op = kmalloc(sizeof(*extent_op), GFP_NOFS);
			BUG_ON(!extent_op);

			extent_op->type = PENDING_BACKREF_UPDATE;
			extent_op->bytenr = bytenr;
			extent_op->num_bytes = num_bytes;
			extent_op->parent = parent;
			extent_op->orig_parent = orig_parent;
			extent_op->generation = ref_generation;
			extent_op->orig_generation = orig_generation;
			extent_op->level = (int)owner_objectid;

			set_extent_bits(&root->fs_info->extent_ins,
					bytenr, bytenr + num_bytes - 1,
					EXTENT_LOCKED, GFP_NOFS);
			set_state_private(&root->fs_info->extent_ins,
					  bytenr, (unsigned long)extent_op);
		}
		return 0;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	ret = lookup_extent_backref(trans, extent_root, path,
				    bytenr, orig_parent, orig_root,
				    orig_generation, 1);
	if (ret)
		goto out;
	ret = remove_extent_backref(trans, extent_root, path);
	if (ret)
		goto out;
	ret = insert_extent_backref(trans, extent_root, path, bytenr,
				    parent, ref_root, ref_generation,
				    owner_objectid, owner_offset);
	BUG_ON(ret);
	finish_current_insert(trans, extent_root);
	del_pending_extents(trans, extent_root);
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_update_extent_ref(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root, u64 bytenr,
			    u64 orig_parent, u64 parent,
			    u64 ref_root, u64 ref_generation,
			    u64 owner_objectid, u64 owner_offset)
{
	int ret;
	if (ref_root == BTRFS_TREE_LOG_OBJECTID &&
	    owner_objectid < BTRFS_FIRST_FREE_OBJECTID)
		return 0;
	maybe_lock_mutex(root);
	ret = __btrfs_update_extent_ref(trans, root, bytenr, orig_parent,
					parent, ref_root, ref_root,
					ref_generation, ref_generation,
					owner_objectid, owner_offset);
	maybe_unlock_mutex(root);
	return ret;
}

static int __btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root, u64 bytenr,
				  u64 orig_parent, u64 parent,
				  u64 orig_root, u64 ref_root,
				  u64 orig_generation, u64 ref_generation,
				  u64 owner_objectid, u64 owner_offset)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_extent_item *item;
	u32 refs;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 1;
	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = (u64)-1;

	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key, path,
				0, 1);
	if (ret < 0)
		return ret;
	BUG_ON(ret == 0 || path->slots[0] == 0);

	path->slots[0]--;
	l = path->nodes[0];

	btrfs_item_key_to_cpu(l, &key, path->slots[0]);
	BUG_ON(key.objectid != bytenr);
	BUG_ON(key.type != BTRFS_EXTENT_ITEM_KEY);

	item = btrfs_item_ptr(l, path->slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(l, item);
	btrfs_set_extent_refs(l, item, refs + 1);
	btrfs_mark_buffer_dirty(path->nodes[0]);

	btrfs_release_path(root->fs_info->extent_root, path);

	path->reada = 1;
	ret = insert_extent_backref(trans, root->fs_info->extent_root,
				    path, bytenr, parent,
				    ref_root, ref_generation,
				    owner_objectid, owner_offset);
	BUG_ON(ret);
	finish_current_insert(trans, root->fs_info->extent_root);
	del_pending_extents(trans, root->fs_info->extent_root);

	btrfs_free_path(path);
	return 0;
}

int btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 u64 bytenr, u64 num_bytes, u64 parent,
			 u64 ref_root, u64 ref_generation,
			 u64 owner_objectid, u64 owner_offset)
{
	int ret;
	if (ref_root == BTRFS_TREE_LOG_OBJECTID &&
	    owner_objectid < BTRFS_FIRST_FREE_OBJECTID)
		return 0;
	maybe_lock_mutex(root);
	ret = __btrfs_inc_extent_ref(trans, root, bytenr, 0, parent,
				     0, ref_root, 0, ref_generation,
				     owner_objectid, owner_offset);
	maybe_unlock_mutex(root);
	return ret;
}

int btrfs_extent_post_op(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root)
{
	finish_current_insert(trans, root->fs_info->extent_root);
	del_pending_extents(trans, root->fs_info->extent_root);
	return 0;
}

int btrfs_lookup_extent_ref(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root, u64 bytenr,
			    u64 num_bytes, u32 *refs)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_extent_item *item;

	WARN_ON(num_bytes < root->sectorsize);
	path = btrfs_alloc_path();
	path->reada = 1;
	key.objectid = bytenr;
	key.offset = num_bytes;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key, path,
				0, 0);
	if (ret < 0)
		goto out;
	if (ret != 0) {
		btrfs_print_leaf(root, path->nodes[0]);
		printk("failed to find block number %Lu\n", bytenr);
		BUG();
	}
	l = path->nodes[0];
	item = btrfs_item_ptr(l, path->slots[0], struct btrfs_extent_item);
	*refs = btrfs_extent_refs(l, item);
out:
	btrfs_free_path(path);
	return 0;
}

static int get_reference_status(struct btrfs_root *root, u64 bytenr,
				u64 parent_gen, u64 ref_objectid,
			        u64 *min_generation, u32 *ref_count)
{
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_extent_ref *ref_item;
	struct btrfs_key key;
	struct btrfs_key found_key;
	u64 root_objectid = root->root_key.objectid;
	u64 ref_generation;
	u32 nritems;
	int ret;

	key.objectid = bytenr;
	key.offset = (u64)-1;
	key.type = BTRFS_EXTENT_ITEM_KEY;

	path = btrfs_alloc_path();
	mutex_lock(&root->fs_info->alloc_mutex);
	ret = btrfs_search_slot(NULL, extent_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	BUG_ON(ret == 0);
	if (ret < 0 || path->slots[0] == 0)
		goto out;

	path->slots[0]--;
	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

	if (found_key.objectid != bytenr ||
	    found_key.type != BTRFS_EXTENT_ITEM_KEY) {
		ret = 1;
		goto out;
	}

	*ref_count = 0;
	*min_generation = (u64)-1;

	while (1) {
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(extent_root, path);
			if (ret < 0)
				goto out;
			if (ret == 0)
				continue;
			break;
		}
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid != bytenr)
			break;

		if (found_key.type != BTRFS_EXTENT_REF_KEY) {
			path->slots[0]++;
			continue;
		}

		ref_item = btrfs_item_ptr(leaf, path->slots[0],
					  struct btrfs_extent_ref);
		ref_generation = btrfs_ref_generation(leaf, ref_item);
		/*
		 * For (parent_gen > 0 && parent_gen > ref_generation):
		 *
		 * we reach here through the oldest root, therefore
		 * all other reference from same snapshot should have
		 * a larger generation.
		 */
		if ((root_objectid != btrfs_ref_root(leaf, ref_item)) ||
		    (parent_gen > 0 && parent_gen > ref_generation) ||
		    (ref_objectid >= BTRFS_FIRST_FREE_OBJECTID &&
		     ref_objectid != btrfs_ref_objectid(leaf, ref_item))) {
			*ref_count = 2;
			break;
		}

		*ref_count = 1;
		if (*min_generation > ref_generation)
			*min_generation = ref_generation;

		path->slots[0]++;
	}
	ret = 0;
out:
	mutex_unlock(&root->fs_info->alloc_mutex);
	btrfs_free_path(path);
	return ret;
}

int btrfs_cross_ref_exists(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct btrfs_key *key, u64 bytenr)
{
	struct btrfs_root *old_root;
	struct btrfs_path *path = NULL;
	struct extent_buffer *eb;
	struct btrfs_file_extent_item *item;
	u64 ref_generation;
	u64 min_generation;
	u64 extent_start;
	u32 ref_count;
	int level;
	int ret;

	BUG_ON(trans == NULL);
	BUG_ON(key->type != BTRFS_EXTENT_DATA_KEY);
	ret = get_reference_status(root, bytenr, 0, key->objectid,
				   &min_generation, &ref_count);
	if (ret)
		return ret;

	if (ref_count != 1)
		return 1;

	old_root = root->dirty_root->root;
	ref_generation = old_root->root_key.offset;

	/* all references are created in running transaction */
	if (min_generation > ref_generation) {
		ret = 0;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	path->skip_locking = 1;
	/* if no item found, the extent is referenced by other snapshot */
	ret = btrfs_search_slot(NULL, old_root, key, path, 0, 0);
	if (ret)
		goto out;

	eb = path->nodes[0];
	item = btrfs_item_ptr(eb, path->slots[0],
			      struct btrfs_file_extent_item);
	if (btrfs_file_extent_type(eb, item) != BTRFS_FILE_EXTENT_REG ||
	    btrfs_file_extent_disk_bytenr(eb, item) != bytenr) {
		ret = 1;
		goto out;
	}

	for (level = BTRFS_MAX_LEVEL - 1; level >= -1; level--) {
		if (level >= 0) {
			eb = path->nodes[level];
			if (!eb)
				continue;
			extent_start = eb->start;
		} else
			extent_start = bytenr;

		ret = get_reference_status(root, extent_start, ref_generation,
					   0, &min_generation, &ref_count);
		if (ret)
			goto out;

		if (ref_count != 1) {
			ret = 1;
			goto out;
		}
		if (level >= 0)
			ref_generation = btrfs_header_generation(eb);
	}
	ret = 0;
out:
	if (path)
		btrfs_free_path(path);
	return ret;
}

int btrfs_cache_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		    struct extent_buffer *buf, u32 nr_extents)
{
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	u64 root_gen;
	u32 nritems;
	int i;
	int level;
	int ret = 0;
	int shared = 0;

	if (!root->ref_cows)
		return 0;

	if (root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID) {
		shared = 0;
		root_gen = root->root_key.offset;
	} else {
		shared = 1;
		root_gen = trans->transid - 1;
	}

	level = btrfs_header_level(buf);
	nritems = btrfs_header_nritems(buf);

	if (level == 0) {
		struct btrfs_leaf_ref *ref;
		struct btrfs_extent_info *info;

		ref = btrfs_alloc_leaf_ref(root, nr_extents);
		if (!ref) {
			ret = -ENOMEM;
			goto out;
		}

		ref->root_gen = root_gen;
		ref->bytenr = buf->start;
		ref->owner = btrfs_header_owner(buf);
		ref->generation = btrfs_header_generation(buf);
		ref->nritems = nr_extents;
		info = ref->extents;

		for (i = 0; nr_extents > 0 && i < nritems; i++) {
			u64 disk_bytenr;
			btrfs_item_key_to_cpu(buf, &key, i);
			if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY)
				continue;
			fi = btrfs_item_ptr(buf, i,
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(buf, fi) ==
			    BTRFS_FILE_EXTENT_INLINE)
				continue;
			disk_bytenr = btrfs_file_extent_disk_bytenr(buf, fi);
			if (disk_bytenr == 0)
				continue;

			info->bytenr = disk_bytenr;
			info->num_bytes =
				btrfs_file_extent_disk_num_bytes(buf, fi);
			info->objectid = key.objectid;
			info->offset = key.offset;
			info++;
		}

		ret = btrfs_add_leaf_ref(root, ref, shared);
		WARN_ON(ret);
		btrfs_free_leaf_ref(root, ref);
	}
out:
	return ret;
}

int btrfs_inc_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *orig_buf, struct extent_buffer *buf,
		  u32 *nr_extents)
{
	u64 bytenr;
	u64 ref_root;
	u64 orig_root;
	u64 ref_generation;
	u64 orig_generation;
	u32 nritems;
	u32 nr_file_extents = 0;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	int i;
	int level;
	int ret = 0;
	int faili = 0;
	int (*process_func)(struct btrfs_trans_handle *, struct btrfs_root *,
			    u64, u64, u64, u64, u64, u64, u64, u64, u64);

	ref_root = btrfs_header_owner(buf);
	ref_generation = btrfs_header_generation(buf);
	orig_root = btrfs_header_owner(orig_buf);
	orig_generation = btrfs_header_generation(orig_buf);

	nritems = btrfs_header_nritems(buf);
	level = btrfs_header_level(buf);

	if (root->ref_cows) {
		process_func = __btrfs_inc_extent_ref;
	} else {
		if (level == 0 &&
		    root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID)
			goto out;
		if (level != 0 &&
		    root->root_key.objectid == BTRFS_TREE_LOG_OBJECTID)
			goto out;
		process_func = __btrfs_update_extent_ref;
	}

	for (i = 0; i < nritems; i++) {
		cond_resched();
		if (level == 0) {
			btrfs_item_key_to_cpu(buf, &key, i);
			if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY)
				continue;
			fi = btrfs_item_ptr(buf, i,
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(buf, fi) ==
			    BTRFS_FILE_EXTENT_INLINE)
				continue;
			bytenr = btrfs_file_extent_disk_bytenr(buf, fi);
			if (bytenr == 0)
				continue;

			nr_file_extents++;

			maybe_lock_mutex(root);
			ret = process_func(trans, root, bytenr,
					   orig_buf->start, buf->start,
					   orig_root, ref_root,
					   orig_generation, ref_generation,
					   key.objectid, key.offset);
			maybe_unlock_mutex(root);

			if (ret) {
				faili = i;
				WARN_ON(1);
				goto fail;
			}
		} else {
			bytenr = btrfs_node_blockptr(buf, i);
			maybe_lock_mutex(root);
			ret = process_func(trans, root, bytenr,
					   orig_buf->start, buf->start,
					   orig_root, ref_root,
					   orig_generation, ref_generation,
					   level - 1, 0);
			maybe_unlock_mutex(root);
			if (ret) {
				faili = i;
				WARN_ON(1);
				goto fail;
			}
		}
	}
out:
	if (nr_extents) {
		if (level == 0)
			*nr_extents = nr_file_extents;
		else
			*nr_extents = nritems;
	}
	return 0;
fail:
	WARN_ON(1);
	return ret;
}

int btrfs_update_ref(struct btrfs_trans_handle *trans,
		     struct btrfs_root *root, struct extent_buffer *orig_buf,
		     struct extent_buffer *buf, int start_slot, int nr)

{
	u64 bytenr;
	u64 ref_root;
	u64 orig_root;
	u64 ref_generation;
	u64 orig_generation;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	int i;
	int ret;
	int slot;
	int level;

	BUG_ON(start_slot < 0);
	BUG_ON(start_slot + nr > btrfs_header_nritems(buf));

	ref_root = btrfs_header_owner(buf);
	ref_generation = btrfs_header_generation(buf);
	orig_root = btrfs_header_owner(orig_buf);
	orig_generation = btrfs_header_generation(orig_buf);
	level = btrfs_header_level(buf);

	if (!root->ref_cows) {
		if (level == 0 &&
		    root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID)
			return 0;
		if (level != 0 &&
		    root->root_key.objectid == BTRFS_TREE_LOG_OBJECTID)
			return 0;
	}

	for (i = 0, slot = start_slot; i < nr; i++, slot++) {
		cond_resched();
		if (level == 0) {
			btrfs_item_key_to_cpu(buf, &key, slot);
			if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY)
				continue;
			fi = btrfs_item_ptr(buf, slot,
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(buf, fi) ==
			    BTRFS_FILE_EXTENT_INLINE)
				continue;
			bytenr = btrfs_file_extent_disk_bytenr(buf, fi);
			if (bytenr == 0)
				continue;
			maybe_lock_mutex(root);
			ret = __btrfs_update_extent_ref(trans, root, bytenr,
					    orig_buf->start, buf->start,
					    orig_root, ref_root,
					    orig_generation, ref_generation,
					    key.objectid, key.offset);
			maybe_unlock_mutex(root);
			if (ret)
				goto fail;
		} else {
			bytenr = btrfs_node_blockptr(buf, slot);
			maybe_lock_mutex(root);
			ret = __btrfs_update_extent_ref(trans, root, bytenr,
					    orig_buf->start, buf->start,
					    orig_root, ref_root,
					    orig_generation, ref_generation,
					    level - 1, 0);
			maybe_unlock_mutex(root);
			if (ret)
				goto fail;
		}
	}
	return 0;
fail:
	WARN_ON(1);
	return -1;
}

static int write_one_cache_group(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_block_group_cache *cache)
{
	int ret;
	int pending_ret;
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	unsigned long bi;
	struct extent_buffer *leaf;

	ret = btrfs_search_slot(trans, extent_root, &cache->key, path, 0, 1);
	if (ret < 0)
		goto fail;
	BUG_ON(ret);

	leaf = path->nodes[0];
	bi = btrfs_item_ptr_offset(leaf, path->slots[0]);
	write_extent_buffer(leaf, &cache->item, bi, sizeof(cache->item));
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(extent_root, path);
fail:
	finish_current_insert(trans, extent_root);
	pending_ret = del_pending_extents(trans, extent_root);
	if (ret)
		return ret;
	if (pending_ret)
		return pending_ret;
	return 0;

}

int btrfs_write_dirty_block_groups(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root)
{
	struct btrfs_block_group_cache *cache, *entry;
	struct rb_node *n;
	int err = 0;
	int werr = 0;
	struct btrfs_path *path;
	u64 last = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	mutex_lock(&root->fs_info->alloc_mutex);
	while(1) {
		cache = NULL;
		spin_lock(&root->fs_info->block_group_cache_lock);
		for (n = rb_first(&root->fs_info->block_group_cache_tree);
		     n; n = rb_next(n)) {
			entry = rb_entry(n, struct btrfs_block_group_cache,
					 cache_node);
			if (entry->dirty) {
				cache = entry;
				break;
			}
		}
		spin_unlock(&root->fs_info->block_group_cache_lock);

		if (!cache)
			break;

		cache->dirty = 0;
		last += cache->key.offset;

		err = write_one_cache_group(trans, root,
					    path, cache);
		/*
		 * if we fail to write the cache group, we want
		 * to keep it marked dirty in hopes that a later
		 * write will work
		 */
		if (err) {
			werr = err;
			continue;
		}
	}
	btrfs_free_path(path);
	mutex_unlock(&root->fs_info->alloc_mutex);
	return werr;
}

static int update_space_info(struct btrfs_fs_info *info, u64 flags,
			     u64 total_bytes, u64 bytes_used,
			     struct btrfs_space_info **space_info)
{
	struct btrfs_space_info *found;

	found = __find_space_info(info, flags);
	if (found) {
		found->total_bytes += total_bytes;
		found->bytes_used += bytes_used;
		found->full = 0;
		*space_info = found;
		return 0;
	}
	found = kmalloc(sizeof(*found), GFP_NOFS);
	if (!found)
		return -ENOMEM;

	list_add(&found->list, &info->space_info);
	INIT_LIST_HEAD(&found->block_groups);
	spin_lock_init(&found->lock);
	found->flags = flags;
	found->total_bytes = total_bytes;
	found->bytes_used = bytes_used;
	found->bytes_pinned = 0;
	found->bytes_reserved = 0;
	found->full = 0;
	found->force_alloc = 0;
	*space_info = found;
	return 0;
}

static void set_avail_alloc_bits(struct btrfs_fs_info *fs_info, u64 flags)
{
	u64 extra_flags = flags & (BTRFS_BLOCK_GROUP_RAID0 |
				   BTRFS_BLOCK_GROUP_RAID1 |
				   BTRFS_BLOCK_GROUP_RAID10 |
				   BTRFS_BLOCK_GROUP_DUP);
	if (extra_flags) {
		if (flags & BTRFS_BLOCK_GROUP_DATA)
			fs_info->avail_data_alloc_bits |= extra_flags;
		if (flags & BTRFS_BLOCK_GROUP_METADATA)
			fs_info->avail_metadata_alloc_bits |= extra_flags;
		if (flags & BTRFS_BLOCK_GROUP_SYSTEM)
			fs_info->avail_system_alloc_bits |= extra_flags;
	}
}

static u64 reduce_alloc_profile(struct btrfs_root *root, u64 flags)
{
	u64 num_devices = root->fs_info->fs_devices->num_devices;

	if (num_devices == 1)
		flags &= ~(BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_RAID0);
	if (num_devices < 4)
		flags &= ~BTRFS_BLOCK_GROUP_RAID10;

	if ((flags & BTRFS_BLOCK_GROUP_DUP) &&
	    (flags & (BTRFS_BLOCK_GROUP_RAID1 |
		      BTRFS_BLOCK_GROUP_RAID10))) {
		flags &= ~BTRFS_BLOCK_GROUP_DUP;
	}

	if ((flags & BTRFS_BLOCK_GROUP_RAID1) &&
	    (flags & BTRFS_BLOCK_GROUP_RAID10)) {
		flags &= ~BTRFS_BLOCK_GROUP_RAID1;
	}

	if ((flags & BTRFS_BLOCK_GROUP_RAID0) &&
	    ((flags & BTRFS_BLOCK_GROUP_RAID1) |
	     (flags & BTRFS_BLOCK_GROUP_RAID10) |
	     (flags & BTRFS_BLOCK_GROUP_DUP)))
		flags &= ~BTRFS_BLOCK_GROUP_RAID0;
	return flags;
}

static int do_chunk_alloc(struct btrfs_trans_handle *trans,
			  struct btrfs_root *extent_root, u64 alloc_bytes,
			  u64 flags, int force)
{
	struct btrfs_space_info *space_info;
	u64 thresh;
	u64 start;
	u64 num_bytes;
	int ret = 0;

	flags = reduce_alloc_profile(extent_root, flags);

	space_info = __find_space_info(extent_root->fs_info, flags);
	if (!space_info) {
		ret = update_space_info(extent_root->fs_info, flags,
					0, 0, &space_info);
		BUG_ON(ret);
	}
	BUG_ON(!space_info);

	if (space_info->force_alloc) {
		force = 1;
		space_info->force_alloc = 0;
	}
	if (space_info->full)
		goto out;

	thresh = div_factor(space_info->total_bytes, 6);
	if (!force &&
	   (space_info->bytes_used + space_info->bytes_pinned +
	    space_info->bytes_reserved + alloc_bytes) < thresh)
		goto out;

	mutex_lock(&extent_root->fs_info->chunk_mutex);
	ret = btrfs_alloc_chunk(trans, extent_root, &start, &num_bytes, flags);
	if (ret == -ENOSPC) {
printk("space info full %Lu\n", flags);
		space_info->full = 1;
		goto out_unlock;
	}
	BUG_ON(ret);

	ret = btrfs_make_block_group(trans, extent_root, 0, flags,
		     BTRFS_FIRST_CHUNK_TREE_OBJECTID, start, num_bytes);
	BUG_ON(ret);

out_unlock:
	mutex_unlock(&extent_root->fs_info->chunk_mutex);
out:
	return ret;
}

static int update_block_group(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      u64 bytenr, u64 num_bytes, int alloc,
			      int mark_free)
{
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *info = root->fs_info;
	u64 total = num_bytes;
	u64 old_val;
	u64 byte_in_group;

	WARN_ON(!mutex_is_locked(&root->fs_info->alloc_mutex));
	while(total) {
		cache = btrfs_lookup_block_group(info, bytenr);
		if (!cache) {
			return -1;
		}
		byte_in_group = bytenr - cache->key.objectid;
		WARN_ON(byte_in_group > cache->key.offset);

		spin_lock(&cache->lock);
		cache->dirty = 1;
		old_val = btrfs_block_group_used(&cache->item);
		num_bytes = min(total, cache->key.offset - byte_in_group);
		if (alloc) {
			old_val += num_bytes;
			cache->space_info->bytes_used += num_bytes;
			btrfs_set_block_group_used(&cache->item, old_val);
			spin_unlock(&cache->lock);
		} else {
			old_val -= num_bytes;
			cache->space_info->bytes_used -= num_bytes;
			btrfs_set_block_group_used(&cache->item, old_val);
			spin_unlock(&cache->lock);
			if (mark_free) {
				int ret;
				ret = btrfs_add_free_space(cache, bytenr,
							   num_bytes);
				if (ret)
					return -1;
			}
		}
		total -= num_bytes;
		bytenr += num_bytes;
	}
	return 0;
}

static u64 first_logical_byte(struct btrfs_root *root, u64 search_start)
{
	struct btrfs_block_group_cache *cache;

	cache = btrfs_lookup_first_block_group(root->fs_info, search_start);
	if (!cache)
		return 0;

	return cache->key.objectid;
}

int btrfs_update_pinned_extents(struct btrfs_root *root,
				u64 bytenr, u64 num, int pin)
{
	u64 len;
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *fs_info = root->fs_info;

	WARN_ON(!mutex_is_locked(&root->fs_info->alloc_mutex));
	if (pin) {
		set_extent_dirty(&fs_info->pinned_extents,
				bytenr, bytenr + num - 1, GFP_NOFS);
	} else {
		clear_extent_dirty(&fs_info->pinned_extents,
				bytenr, bytenr + num - 1, GFP_NOFS);
	}
	while (num > 0) {
		cache = btrfs_lookup_block_group(fs_info, bytenr);
		BUG_ON(!cache);
		len = min(num, cache->key.offset -
			  (bytenr - cache->key.objectid));
		if (pin) {
			spin_lock(&cache->lock);
			cache->pinned += len;
			cache->space_info->bytes_pinned += len;
			spin_unlock(&cache->lock);
			fs_info->total_pinned += len;
		} else {
			spin_lock(&cache->lock);
			cache->pinned -= len;
			cache->space_info->bytes_pinned -= len;
			spin_unlock(&cache->lock);
			fs_info->total_pinned -= len;
		}
		bytenr += len;
		num -= len;
	}
	return 0;
}

static int update_reserved_extents(struct btrfs_root *root,
				   u64 bytenr, u64 num, int reserve)
{
	u64 len;
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *fs_info = root->fs_info;

	WARN_ON(!mutex_is_locked(&root->fs_info->alloc_mutex));
	while (num > 0) {
		cache = btrfs_lookup_block_group(fs_info, bytenr);
		BUG_ON(!cache);
		len = min(num, cache->key.offset -
			  (bytenr - cache->key.objectid));
		if (reserve) {
			spin_lock(&cache->lock);
			cache->reserved += len;
			cache->space_info->bytes_reserved += len;
			spin_unlock(&cache->lock);
		} else {
			spin_lock(&cache->lock);
			cache->reserved -= len;
			cache->space_info->bytes_reserved -= len;
			spin_unlock(&cache->lock);
		}
		bytenr += len;
		num -= len;
	}
	return 0;
}

int btrfs_copy_pinned(struct btrfs_root *root, struct extent_io_tree *copy)
{
	u64 last = 0;
	u64 start;
	u64 end;
	struct extent_io_tree *pinned_extents = &root->fs_info->pinned_extents;
	int ret;

	while(1) {
		ret = find_first_extent_bit(pinned_extents, last,
					    &start, &end, EXTENT_DIRTY);
		if (ret)
			break;
		set_extent_dirty(copy, start, end, GFP_NOFS);
		last = end + 1;
	}
	return 0;
}

int btrfs_finish_extent_commit(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct extent_io_tree *unpin)
{
	u64 start;
	u64 end;
	int ret;
	struct btrfs_block_group_cache *cache;

	mutex_lock(&root->fs_info->alloc_mutex);
	while(1) {
		ret = find_first_extent_bit(unpin, 0, &start, &end,
					    EXTENT_DIRTY);
		if (ret)
			break;
		btrfs_update_pinned_extents(root, start, end + 1 - start, 0);
		clear_extent_dirty(unpin, start, end, GFP_NOFS);
		cache = btrfs_lookup_block_group(root->fs_info, start);
		if (cache->cached)
			btrfs_add_free_space(cache, start, end - start + 1);
		if (need_resched()) {
			mutex_unlock(&root->fs_info->alloc_mutex);
			cond_resched();
			mutex_lock(&root->fs_info->alloc_mutex);
		}
	}
	mutex_unlock(&root->fs_info->alloc_mutex);
	return 0;
}

static int finish_current_insert(struct btrfs_trans_handle *trans,
				 struct btrfs_root *extent_root)
{
	u64 start;
	u64 end;
	u64 priv;
	struct btrfs_fs_info *info = extent_root->fs_info;
	struct btrfs_path *path;
	struct btrfs_extent_ref *ref;
	struct pending_extent_op *extent_op;
	struct btrfs_key key;
	struct btrfs_extent_item extent_item;
	int ret;
	int err = 0;

	WARN_ON(!mutex_is_locked(&extent_root->fs_info->alloc_mutex));
	btrfs_set_stack_extent_refs(&extent_item, 1);
	path = btrfs_alloc_path();

	while(1) {
		ret = find_first_extent_bit(&info->extent_ins, 0, &start,
					    &end, EXTENT_LOCKED);
		if (ret)
			break;

		ret = get_state_private(&info->extent_ins, start, &priv);
		BUG_ON(ret);
		extent_op = (struct pending_extent_op *)(unsigned long)priv;

		if (extent_op->type == PENDING_EXTENT_INSERT) {
			key.objectid = start;
			key.offset = end + 1 - start;
			key.type = BTRFS_EXTENT_ITEM_KEY;
			err = btrfs_insert_item(trans, extent_root, &key,
					&extent_item, sizeof(extent_item));
			BUG_ON(err);

			clear_extent_bits(&info->extent_ins, start, end,
					  EXTENT_LOCKED, GFP_NOFS);

			err = insert_extent_backref(trans, extent_root, path,
						start, extent_op->parent,
						extent_root->root_key.objectid,
						extent_op->generation,
						extent_op->level, 0);
			BUG_ON(err);
		} else if (extent_op->type == PENDING_BACKREF_UPDATE) {
			err = lookup_extent_backref(trans, extent_root, path,
						start, extent_op->orig_parent,
						extent_root->root_key.objectid,
						extent_op->orig_generation, 0);
			BUG_ON(err);

			clear_extent_bits(&info->extent_ins, start, end,
					  EXTENT_LOCKED, GFP_NOFS);

			key.objectid = start;
			key.offset = extent_op->parent;
			key.type = BTRFS_EXTENT_REF_KEY;
			err = btrfs_set_item_key_safe(trans, extent_root, path,
						      &key);
			BUG_ON(err);
			ref = btrfs_item_ptr(path->nodes[0], path->slots[0],
					     struct btrfs_extent_ref);
			btrfs_set_ref_generation(path->nodes[0], ref,
						 extent_op->generation);
			btrfs_mark_buffer_dirty(path->nodes[0]);
			btrfs_release_path(extent_root, path);
		} else {
			BUG_ON(1);
		}
		kfree(extent_op);

		if (need_resched()) {
			mutex_unlock(&extent_root->fs_info->alloc_mutex);
			cond_resched();
			mutex_lock(&extent_root->fs_info->alloc_mutex);
		}
	}
	btrfs_free_path(path);
	return 0;
}

static int pin_down_bytes(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  u64 bytenr, u64 num_bytes, int is_data)
{
	int err = 0;
	struct extent_buffer *buf;

	WARN_ON(!mutex_is_locked(&root->fs_info->alloc_mutex));
	if (is_data)
		goto pinit;

	buf = btrfs_find_tree_block(root, bytenr, num_bytes);
	if (!buf)
		goto pinit;

	/* we can reuse a block if it hasn't been written
	 * and it is from this transaction.  We can't
	 * reuse anything from the tree log root because
	 * it has tiny sub-transactions.
	 */
	if (btrfs_buffer_uptodate(buf, 0) &&
	    btrfs_try_tree_lock(buf)) {
		u64 header_owner = btrfs_header_owner(buf);
		u64 header_transid = btrfs_header_generation(buf);
		if (header_owner != BTRFS_TREE_LOG_OBJECTID &&
		    header_owner != BTRFS_TREE_RELOC_OBJECTID &&
		    header_transid == trans->transid &&
		    !btrfs_header_flag(buf, BTRFS_HEADER_FLAG_WRITTEN)) {
			clean_tree_block(NULL, root, buf);
			btrfs_tree_unlock(buf);
			free_extent_buffer(buf);
			return 1;
		}
		btrfs_tree_unlock(buf);
	}
	free_extent_buffer(buf);
pinit:
	btrfs_update_pinned_extents(root, bytenr, num_bytes, 1);

	BUG_ON(err < 0);
	return 0;
}

/*
 * remove an extent from the root, returns 0 on success
 */
static int __free_extent(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 u64 bytenr, u64 num_bytes, u64 parent,
			 u64 root_objectid, u64 ref_generation,
			 u64 owner_objectid, u64 owner_offset,
			 int pin, int mark_free)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_root *extent_root = info->extent_root;
	struct extent_buffer *leaf;
	int ret;
	int extent_slot = 0;
	int found_extent = 0;
	int num_to_del = 1;
	struct btrfs_extent_item *ei;
	u32 refs;

	WARN_ON(!mutex_is_locked(&root->fs_info->alloc_mutex));
	key.objectid = bytenr;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	key.offset = num_bytes;
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 1;
	ret = lookup_extent_backref(trans, extent_root, path, bytenr, parent,
				    root_objectid, ref_generation, 1);
	if (ret == 0) {
		struct btrfs_key found_key;
		extent_slot = path->slots[0];
		while(extent_slot > 0) {
			extent_slot--;
			btrfs_item_key_to_cpu(path->nodes[0], &found_key,
					      extent_slot);
			if (found_key.objectid != bytenr)
				break;
			if (found_key.type == BTRFS_EXTENT_ITEM_KEY &&
			    found_key.offset == num_bytes) {
				found_extent = 1;
				break;
			}
			if (path->slots[0] - extent_slot > 5)
				break;
		}
		if (!found_extent) {
			ret = remove_extent_backref(trans, extent_root, path);
			BUG_ON(ret);
			btrfs_release_path(extent_root, path);
			ret = btrfs_search_slot(trans, extent_root,
						&key, path, -1, 1);
			BUG_ON(ret);
			extent_slot = path->slots[0];
		}
	} else {
		btrfs_print_leaf(extent_root, path->nodes[0]);
		WARN_ON(1);
		printk("Unable to find ref byte nr %Lu root %Lu "
		       " gen %Lu owner %Lu offset %Lu\n", bytenr,
		       root_objectid, ref_generation, owner_objectid,
		       owner_offset);
	}

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, extent_slot,
			    struct btrfs_extent_item);
	refs = btrfs_extent_refs(leaf, ei);
	BUG_ON(refs == 0);
	refs -= 1;
	btrfs_set_extent_refs(leaf, ei, refs);

	btrfs_mark_buffer_dirty(leaf);

	if (refs == 0 && found_extent && path->slots[0] == extent_slot + 1) {
		struct btrfs_extent_ref *ref;
		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_ref);
		BUG_ON(btrfs_ref_num_refs(leaf, ref) != 1);
		/* if the back ref and the extent are next to each other
		 * they get deleted below in one shot
		 */
		path->slots[0] = extent_slot;
		num_to_del = 2;
	} else if (found_extent) {
		/* otherwise delete the extent back ref */
		ret = remove_extent_backref(trans, extent_root, path);
		BUG_ON(ret);
		/* if refs are 0, we need to setup the path for deletion */
		if (refs == 0) {
			btrfs_release_path(extent_root, path);
			ret = btrfs_search_slot(trans, extent_root, &key, path,
						-1, 1);
			BUG_ON(ret);
		}
	}

	if (refs == 0) {
		u64 super_used;
		u64 root_used;
#ifdef BIO_RW_DISCARD
		u64 map_length = num_bytes;
		struct btrfs_multi_bio *multi = NULL;
#endif

		if (pin) {
			ret = pin_down_bytes(trans, root, bytenr, num_bytes,
				owner_objectid >= BTRFS_FIRST_FREE_OBJECTID);
			if (ret > 0)
				mark_free = 1;
			BUG_ON(ret < 0);
		}

		/* block accounting for super block */
		spin_lock_irq(&info->delalloc_lock);
		super_used = btrfs_super_bytes_used(&info->super_copy);
		btrfs_set_super_bytes_used(&info->super_copy,
					   super_used - num_bytes);
		spin_unlock_irq(&info->delalloc_lock);

		/* block accounting for root item */
		root_used = btrfs_root_used(&root->root_item);
		btrfs_set_root_used(&root->root_item,
					   root_used - num_bytes);
		ret = btrfs_del_items(trans, extent_root, path, path->slots[0],
				      num_to_del);
		BUG_ON(ret);
		ret = update_block_group(trans, root, bytenr, num_bytes, 0,
					 mark_free);
		BUG_ON(ret);

#ifdef BIO_RW_DISCARD
		/* Tell the block device(s) that the sectors can be discarded */
		ret = btrfs_map_block(&root->fs_info->mapping_tree, READ,
				      bytenr, &map_length, &multi, 0);
		if (!ret) {
			struct btrfs_bio_stripe *stripe = multi->stripes;
			int i;

			if (map_length > num_bytes)
				map_length = num_bytes;

			for (i = 0; i < multi->num_stripes; i++, stripe++) {
				blkdev_issue_discard(stripe->dev->bdev,
						     stripe->physical >> 9,
						     map_length >> 9);
			}
			kfree(multi);
		}
#endif
	}
	btrfs_free_path(path);
	finish_current_insert(trans, extent_root);
	return ret;
}

/*
 * find all the blocks marked as pending in the radix tree and remove
 * them from the extent map
 */
static int del_pending_extents(struct btrfs_trans_handle *trans, struct
			       btrfs_root *extent_root)
{
	int ret;
	int err = 0;
	int mark_free = 0;
	u64 start;
	u64 end;
	u64 priv;
	struct extent_io_tree *pending_del;
	struct extent_io_tree *extent_ins;
	struct pending_extent_op *extent_op;

	WARN_ON(!mutex_is_locked(&extent_root->fs_info->alloc_mutex));
	extent_ins = &extent_root->fs_info->extent_ins;
	pending_del = &extent_root->fs_info->pending_del;

	while(1) {
		ret = find_first_extent_bit(pending_del, 0, &start, &end,
					    EXTENT_LOCKED);
		if (ret)
			break;

		ret = get_state_private(pending_del, start, &priv);
		BUG_ON(ret);
		extent_op = (struct pending_extent_op *)(unsigned long)priv;

		clear_extent_bits(pending_del, start, end, EXTENT_LOCKED,
				  GFP_NOFS);

		ret = pin_down_bytes(trans, extent_root, start,
				     end + 1 - start, 0);
		mark_free = ret > 0;
		if (!test_range_bit(extent_ins, start, end,
				    EXTENT_LOCKED, 0)) {
free_extent:
			ret = __free_extent(trans, extent_root,
					    start, end + 1 - start,
					    extent_op->orig_parent,
					    extent_root->root_key.objectid,
					    extent_op->orig_generation,
					    extent_op->level, 0, 0, mark_free);
			kfree(extent_op);
		} else {
			kfree(extent_op);
			ret = get_state_private(extent_ins, start, &priv);
			BUG_ON(ret);
			extent_op = (struct pending_extent_op *)
							(unsigned long)priv;

			clear_extent_bits(extent_ins, start, end,
					  EXTENT_LOCKED, GFP_NOFS);

			if (extent_op->type == PENDING_BACKREF_UPDATE)
				goto free_extent;

			ret = update_block_group(trans, extent_root, start,
						end + 1 - start, 0, mark_free);
			BUG_ON(ret);
			kfree(extent_op);
		}
		if (ret)
			err = ret;

		if (need_resched()) {
			mutex_unlock(&extent_root->fs_info->alloc_mutex);
			cond_resched();
			mutex_lock(&extent_root->fs_info->alloc_mutex);
		}
	}
	return err;
}

/*
 * remove an extent from the root, returns 0 on success
 */
static int __btrfs_free_extent(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       u64 bytenr, u64 num_bytes, u64 parent,
			       u64 root_objectid, u64 ref_generation,
			       u64 owner_objectid, u64 owner_offset, int pin)
{
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	int pending_ret;
	int ret;

	WARN_ON(num_bytes < root->sectorsize);
	if (root == extent_root) {
		struct pending_extent_op *extent_op;

		extent_op = kmalloc(sizeof(*extent_op), GFP_NOFS);
		BUG_ON(!extent_op);

		extent_op->type = PENDING_EXTENT_DELETE;
		extent_op->bytenr = bytenr;
		extent_op->num_bytes = num_bytes;
		extent_op->parent = parent;
		extent_op->orig_parent = parent;
		extent_op->generation = ref_generation;
		extent_op->orig_generation = ref_generation;
		extent_op->level = (int)owner_objectid;

		set_extent_bits(&root->fs_info->pending_del,
				bytenr, bytenr + num_bytes - 1,
				EXTENT_LOCKED, GFP_NOFS);
		set_state_private(&root->fs_info->pending_del,
				  bytenr, (unsigned long)extent_op);
		return 0;
	}
	/* if metadata always pin */
	if (owner_objectid < BTRFS_FIRST_FREE_OBJECTID) {
		if (root->root_key.objectid == BTRFS_TREE_LOG_OBJECTID) {
			struct btrfs_block_group_cache *cache;

			/* btrfs_free_reserved_extent */
			cache = btrfs_lookup_block_group(root->fs_info, bytenr);
			BUG_ON(!cache);
			btrfs_add_free_space(cache, bytenr, num_bytes);
			update_reserved_extents(root, bytenr, num_bytes, 0);
			return 0;
		}
		pin = 1;
	}

	/* if data pin when any transaction has committed this */
	if (ref_generation != trans->transid)
		pin = 1;

	ret = __free_extent(trans, root, bytenr, num_bytes, parent,
			    root_objectid, ref_generation, owner_objectid,
			    owner_offset, pin, pin == 0);

	finish_current_insert(trans, root->fs_info->extent_root);
	pending_ret = del_pending_extents(trans, root->fs_info->extent_root);
	return ret ? ret : pending_ret;
}

int btrfs_free_extent(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root,
		      u64 bytenr, u64 num_bytes, u64 parent,
		      u64 root_objectid, u64 ref_generation,
		      u64 owner_objectid, u64 owner_offset, int pin)
{
	int ret;

	maybe_lock_mutex(root);
	ret = __btrfs_free_extent(trans, root, bytenr, num_bytes, parent,
				  root_objectid, ref_generation,
				  owner_objectid, owner_offset, pin);
	maybe_unlock_mutex(root);
	return ret;
}

static u64 stripe_align(struct btrfs_root *root, u64 val)
{
	u64 mask = ((u64)root->stripesize - 1);
	u64 ret = (val + mask) & ~mask;
	return ret;
}

/*
 * walks the btree of allocated extents and find a hole of a given size.
 * The key ins is changed to record the hole:
 * ins->objectid == block start
 * ins->flags = BTRFS_EXTENT_ITEM_KEY
 * ins->offset == number of blocks
 * Any available blocks before search_start are skipped.
 */
static int noinline find_free_extent(struct btrfs_trans_handle *trans,
				     struct btrfs_root *orig_root,
				     u64 num_bytes, u64 empty_size,
				     u64 search_start, u64 search_end,
				     u64 hint_byte, struct btrfs_key *ins,
				     u64 exclude_start, u64 exclude_nr,
				     int data)
{
	int ret;
	u64 orig_search_start;
	struct btrfs_root * root = orig_root->fs_info->extent_root;
	struct btrfs_fs_info *info = root->fs_info;
	u64 total_needed = num_bytes;
	u64 *last_ptr = NULL;
	struct btrfs_block_group_cache *block_group;
	int chunk_alloc_done = 0;
	int empty_cluster = 2 * 1024 * 1024;
	int allowed_chunk_alloc = 0;

	WARN_ON(num_bytes < root->sectorsize);
	btrfs_set_key_type(ins, BTRFS_EXTENT_ITEM_KEY);

	if (orig_root->ref_cows || empty_size)
		allowed_chunk_alloc = 1;

	if (data & BTRFS_BLOCK_GROUP_METADATA) {
		last_ptr = &root->fs_info->last_alloc;
		empty_cluster = 256 * 1024;
	}

	if ((data & BTRFS_BLOCK_GROUP_DATA) && btrfs_test_opt(root, SSD))
		last_ptr = &root->fs_info->last_data_alloc;

	if (root->root_key.objectid == BTRFS_TREE_LOG_OBJECTID) {
		last_ptr = &root->fs_info->last_log_alloc;
		if (!last_ptr == 0 && root->fs_info->last_alloc) {
			*last_ptr = root->fs_info->last_alloc + empty_cluster;
		}
	}

	if (last_ptr) {
		if (*last_ptr)
			hint_byte = *last_ptr;
		else
			empty_size += empty_cluster;
	}

	search_start = max(search_start, first_logical_byte(root, 0));
	orig_search_start = search_start;

	search_start = max(search_start, hint_byte);
	total_needed += empty_size;

new_group:
	block_group = btrfs_lookup_first_block_group(info, search_start);

	/*
	 * Ok this looks a little tricky, buts its really simple.  First if we
	 * didn't find a block group obviously we want to start over.
	 * Secondly, if the block group we found does not match the type we
	 * need, and we have a last_ptr and its not 0, chances are the last
	 * allocation we made was at the end of the block group, so lets go
	 * ahead and skip the looking through the rest of the block groups and
	 * start at the beginning.  This helps with metadata allocations,
	 * since you are likely to have a bunch of data block groups to search
	 * through first before you realize that you need to start over, so go
	 * ahead and start over and save the time.
	 */
	if (!block_group || (!block_group_bits(block_group, data) &&
			     last_ptr && *last_ptr)) {
		if (search_start != orig_search_start) {
			if (last_ptr && *last_ptr)
				*last_ptr = 0;
			search_start = orig_search_start;
			goto new_group;
		} else if (!chunk_alloc_done && allowed_chunk_alloc) {
			ret = do_chunk_alloc(trans, root,
					     num_bytes + 2 * 1024 * 1024,
					     data, 1);
			if (ret < 0)
				goto error;
			BUG_ON(ret);
			chunk_alloc_done = 1;
			search_start = orig_search_start;
			goto new_group;
		} else {
			ret = -ENOSPC;
			goto error;
		}
	}

	/*
	 * this is going to seach through all of the existing block groups it
	 * can find, so if we don't find something we need to see if we can
	 * allocate what we need.
	 */
	ret = find_free_space(root, &block_group, &search_start,
			      total_needed, data);
	if (ret == -ENOSPC) {
		/*
		 * instead of allocating, start at the original search start
		 * and see if there is something to be found, if not then we
		 * allocate
		 */
		if (search_start != orig_search_start) {
			if (last_ptr && *last_ptr) {
				*last_ptr = 0;
				total_needed += empty_cluster;
			}
			search_start = orig_search_start;
			goto new_group;
		}

		/*
		 * we've already allocated, we're pretty screwed
		 */
		if (chunk_alloc_done) {
			goto error;
		} else if (!allowed_chunk_alloc && block_group &&
			   block_group_bits(block_group, data)) {
			block_group->space_info->force_alloc = 1;
			goto error;
		} else if (!allowed_chunk_alloc) {
			goto error;
		}

		ret = do_chunk_alloc(trans, root, num_bytes + 2 * 1024 * 1024,
				     data, 1);
		if (ret < 0)
			goto error;

		BUG_ON(ret);
		chunk_alloc_done = 1;
		if (block_group)
			search_start = block_group->key.objectid +
				block_group->key.offset;
		else
			search_start = orig_search_start;
		goto new_group;
	}

	if (ret)
		goto error;

	search_start = stripe_align(root, search_start);
	ins->objectid = search_start;
	ins->offset = num_bytes;

	if (ins->objectid + num_bytes >= search_end) {
		search_start = orig_search_start;
		if (chunk_alloc_done) {
			ret = -ENOSPC;
			goto error;
		}
		goto new_group;
	}

	if (ins->objectid + num_bytes >
	    block_group->key.objectid + block_group->key.offset) {
		if (search_start == orig_search_start && chunk_alloc_done) {
			ret = -ENOSPC;
			goto error;
		}
		search_start = block_group->key.objectid +
			block_group->key.offset;
		goto new_group;
	}

	if (exclude_nr > 0 && (ins->objectid + num_bytes > exclude_start &&
	    ins->objectid < exclude_start + exclude_nr)) {
		search_start = exclude_start + exclude_nr;
		goto new_group;
	}

	if (!(data & BTRFS_BLOCK_GROUP_DATA))
		trans->block_group = block_group;

	ins->offset = num_bytes;
	if (last_ptr) {
		*last_ptr = ins->objectid + ins->offset;
		if (*last_ptr ==
		    btrfs_super_total_bytes(&root->fs_info->super_copy))
			*last_ptr = 0;
	}

	ret = 0;
error:
	return ret;
}

static void dump_space_info(struct btrfs_space_info *info, u64 bytes)
{
	struct btrfs_block_group_cache *cache;
	struct list_head *l;

	printk(KERN_INFO "space_info has %Lu free, is %sfull\n",
	       info->total_bytes - info->bytes_used - info->bytes_pinned -
	       info->bytes_reserved, (info->full) ? "" : "not ");

	spin_lock(&info->lock);
	list_for_each(l, &info->block_groups) {
		cache = list_entry(l, struct btrfs_block_group_cache, list);
		spin_lock(&cache->lock);
		printk(KERN_INFO "block group %Lu has %Lu bytes, %Lu used "
		       "%Lu pinned %Lu reserved\n",
		       cache->key.objectid, cache->key.offset,
		       btrfs_block_group_used(&cache->item),
		       cache->pinned, cache->reserved);
		btrfs_dump_free_space(cache, bytes);
		spin_unlock(&cache->lock);
	}
	spin_unlock(&info->lock);
}

static int __btrfs_reserve_extent(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  u64 num_bytes, u64 min_alloc_size,
				  u64 empty_size, u64 hint_byte,
				  u64 search_end, struct btrfs_key *ins,
				  u64 data)
{
	int ret;
	u64 search_start = 0;
	u64 alloc_profile;
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_block_group_cache *cache;

	if (data) {
		alloc_profile = info->avail_data_alloc_bits &
			        info->data_alloc_profile;
		data = BTRFS_BLOCK_GROUP_DATA | alloc_profile;
	} else if (root == root->fs_info->chunk_root) {
		alloc_profile = info->avail_system_alloc_bits &
			        info->system_alloc_profile;
		data = BTRFS_BLOCK_GROUP_SYSTEM | alloc_profile;
	} else {
		alloc_profile = info->avail_metadata_alloc_bits &
			        info->metadata_alloc_profile;
		data = BTRFS_BLOCK_GROUP_METADATA | alloc_profile;
	}
again:
	data = reduce_alloc_profile(root, data);
	/*
	 * the only place that sets empty_size is btrfs_realloc_node, which
	 * is not called recursively on allocations
	 */
	if (empty_size || root->ref_cows) {
		if (!(data & BTRFS_BLOCK_GROUP_METADATA)) {
			ret = do_chunk_alloc(trans, root->fs_info->extent_root,
				     2 * 1024 * 1024,
				     BTRFS_BLOCK_GROUP_METADATA |
				     (info->metadata_alloc_profile &
				      info->avail_metadata_alloc_bits), 0);
		}
		ret = do_chunk_alloc(trans, root->fs_info->extent_root,
				     num_bytes + 2 * 1024 * 1024, data, 0);
	}

	WARN_ON(num_bytes < root->sectorsize);
	ret = find_free_extent(trans, root, num_bytes, empty_size,
			       search_start, search_end, hint_byte, ins,
			       trans->alloc_exclude_start,
			       trans->alloc_exclude_nr, data);

	if (ret == -ENOSPC && num_bytes > min_alloc_size) {
		num_bytes = num_bytes >> 1;
		num_bytes = num_bytes & ~(root->sectorsize - 1);
		num_bytes = max(num_bytes, min_alloc_size);
		do_chunk_alloc(trans, root->fs_info->extent_root,
			       num_bytes, data, 1);
		goto again;
	}
	if (ret) {
		struct btrfs_space_info *sinfo;

		sinfo = __find_space_info(root->fs_info, data);
		printk("allocation failed flags %Lu, wanted %Lu\n",
		       data, num_bytes);
		dump_space_info(sinfo, num_bytes);
		BUG();
	}
	cache = btrfs_lookup_block_group(root->fs_info, ins->objectid);
	if (!cache) {
		printk(KERN_ERR "Unable to find block group for %Lu\n", ins->objectid);
		return -ENOSPC;
	}

	ret = btrfs_remove_free_space(cache, ins->objectid, ins->offset);

	return ret;
}

int btrfs_free_reserved_extent(struct btrfs_root *root, u64 start, u64 len)
{
	struct btrfs_block_group_cache *cache;

	maybe_lock_mutex(root);
	cache = btrfs_lookup_block_group(root->fs_info, start);
	if (!cache) {
		printk(KERN_ERR "Unable to find block group for %Lu\n", start);
		maybe_unlock_mutex(root);
		return -ENOSPC;
	}
	btrfs_add_free_space(cache, start, len);
	update_reserved_extents(root, start, len, 0);
	maybe_unlock_mutex(root);
	return 0;
}

int btrfs_reserve_extent(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  u64 num_bytes, u64 min_alloc_size,
				  u64 empty_size, u64 hint_byte,
				  u64 search_end, struct btrfs_key *ins,
				  u64 data)
{
	int ret;
	maybe_lock_mutex(root);
	ret = __btrfs_reserve_extent(trans, root, num_bytes, min_alloc_size,
				     empty_size, hint_byte, search_end, ins,
				     data);
	update_reserved_extents(root, ins->objectid, ins->offset, 1);
	maybe_unlock_mutex(root);
	return ret;
}

static int __btrfs_alloc_reserved_extent(struct btrfs_trans_handle *trans,
					 struct btrfs_root *root, u64 parent,
					 u64 root_objectid, u64 ref_generation,
					 u64 owner, u64 owner_offset,
					 struct btrfs_key *ins)
{
	int ret;
	int pending_ret;
	u64 super_used;
	u64 root_used;
	u64 num_bytes = ins->offset;
	u32 sizes[2];
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_root *extent_root = info->extent_root;
	struct btrfs_extent_item *extent_item;
	struct btrfs_extent_ref *ref;
	struct btrfs_path *path;
	struct btrfs_key keys[2];

	if (parent == 0)
		parent = ins->objectid;

	/* block accounting for super block */
	spin_lock_irq(&info->delalloc_lock);
	super_used = btrfs_super_bytes_used(&info->super_copy);
	btrfs_set_super_bytes_used(&info->super_copy, super_used + num_bytes);
	spin_unlock_irq(&info->delalloc_lock);

	/* block accounting for root item */
	root_used = btrfs_root_used(&root->root_item);
	btrfs_set_root_used(&root->root_item, root_used + num_bytes);

	if (root == extent_root) {
		struct pending_extent_op *extent_op;

		extent_op = kmalloc(sizeof(*extent_op), GFP_NOFS);
		BUG_ON(!extent_op);

		extent_op->type = PENDING_EXTENT_INSERT;
		extent_op->bytenr = ins->objectid;
		extent_op->num_bytes = ins->offset;
		extent_op->parent = parent;
		extent_op->orig_parent = 0;
		extent_op->generation = ref_generation;
		extent_op->orig_generation = 0;
		extent_op->level = (int)owner;

		set_extent_bits(&root->fs_info->extent_ins, ins->objectid,
				ins->objectid + ins->offset - 1,
				EXTENT_LOCKED, GFP_NOFS);
		set_state_private(&root->fs_info->extent_ins,
				  ins->objectid, (unsigned long)extent_op);
		goto update_block;
	}

	memcpy(&keys[0], ins, sizeof(*ins));
	keys[1].objectid = ins->objectid;
	keys[1].type = BTRFS_EXTENT_REF_KEY;
	keys[1].offset = parent;
	sizes[0] = sizeof(*extent_item);
	sizes[1] = sizeof(*ref);

	path = btrfs_alloc_path();
	BUG_ON(!path);

	ret = btrfs_insert_empty_items(trans, extent_root, path, keys,
				       sizes, 2);
	BUG_ON(ret);

	extent_item = btrfs_item_ptr(path->nodes[0], path->slots[0],
				     struct btrfs_extent_item);
	btrfs_set_extent_refs(path->nodes[0], extent_item, 1);
	ref = btrfs_item_ptr(path->nodes[0], path->slots[0] + 1,
			     struct btrfs_extent_ref);

	btrfs_set_ref_root(path->nodes[0], ref, root_objectid);
	btrfs_set_ref_generation(path->nodes[0], ref, ref_generation);
	btrfs_set_ref_objectid(path->nodes[0], ref, owner);
	btrfs_set_ref_offset(path->nodes[0], ref, owner_offset);
	btrfs_set_ref_num_refs(path->nodes[0], ref, 1);

	btrfs_mark_buffer_dirty(path->nodes[0]);

	trans->alloc_exclude_start = 0;
	trans->alloc_exclude_nr = 0;
	btrfs_free_path(path);
	finish_current_insert(trans, extent_root);
	pending_ret = del_pending_extents(trans, extent_root);

	if (ret)
		goto out;
	if (pending_ret) {
		ret = pending_ret;
		goto out;
	}

update_block:
	ret = update_block_group(trans, root, ins->objectid, ins->offset, 1, 0);
	if (ret) {
		printk("update block group failed for %Lu %Lu\n",
		       ins->objectid, ins->offset);
		BUG();
	}
out:
	return ret;
}

int btrfs_alloc_reserved_extent(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, u64 parent,
				u64 root_objectid, u64 ref_generation,
				u64 owner, u64 owner_offset,
				struct btrfs_key *ins)
{
	int ret;

	if (root_objectid == BTRFS_TREE_LOG_OBJECTID)
		return 0;
	maybe_lock_mutex(root);
	ret = __btrfs_alloc_reserved_extent(trans, root, parent,
					    root_objectid, ref_generation,
					    owner, owner_offset, ins);
	update_reserved_extents(root, ins->objectid, ins->offset, 0);
	maybe_unlock_mutex(root);
	return ret;
}

/*
 * this is used by the tree logging recovery code.  It records that
 * an extent has been allocated and makes sure to clear the free
 * space cache bits as well
 */
int btrfs_alloc_logged_extent(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, u64 parent,
				u64 root_objectid, u64 ref_generation,
				u64 owner, u64 owner_offset,
				struct btrfs_key *ins)
{
	int ret;
	struct btrfs_block_group_cache *block_group;

	maybe_lock_mutex(root);
	block_group = btrfs_lookup_block_group(root->fs_info, ins->objectid);
	cache_block_group(root, block_group);

	ret = btrfs_remove_free_space(block_group, ins->objectid, ins->offset);
	BUG_ON(ret);
	ret = __btrfs_alloc_reserved_extent(trans, root, parent,
					    root_objectid, ref_generation,
					    owner, owner_offset, ins);
	maybe_unlock_mutex(root);
	return ret;
}

/*
 * finds a free extent and does all the dirty work required for allocation
 * returns the key for the extent through ins, and a tree buffer for
 * the first block of the extent through buf.
 *
 * returns 0 if everything worked, non-zero otherwise.
 */
int btrfs_alloc_extent(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root,
		       u64 num_bytes, u64 parent, u64 min_alloc_size,
		       u64 root_objectid, u64 ref_generation,
		       u64 owner_objectid, u64 owner_offset,
		       u64 empty_size, u64 hint_byte,
		       u64 search_end, struct btrfs_key *ins, u64 data)
{
	int ret;

	maybe_lock_mutex(root);

	ret = __btrfs_reserve_extent(trans, root, num_bytes,
				     min_alloc_size, empty_size, hint_byte,
				     search_end, ins, data);
	BUG_ON(ret);
	if (root_objectid != BTRFS_TREE_LOG_OBJECTID) {
		ret = __btrfs_alloc_reserved_extent(trans, root, parent,
					root_objectid, ref_generation,
					owner_objectid, owner_offset, ins);
		BUG_ON(ret);

	} else {
		update_reserved_extents(root, ins->objectid, ins->offset, 1);
	}
	maybe_unlock_mutex(root);
	return ret;
}

struct extent_buffer *btrfs_init_new_buffer(struct btrfs_trans_handle *trans,
					    struct btrfs_root *root,
					    u64 bytenr, u32 blocksize)
{
	struct extent_buffer *buf;

	buf = btrfs_find_create_tree_block(root, bytenr, blocksize);
	if (!buf)
		return ERR_PTR(-ENOMEM);
	btrfs_set_header_generation(buf, trans->transid);
	btrfs_tree_lock(buf);
	clean_tree_block(trans, root, buf);
	btrfs_set_buffer_uptodate(buf);
	if (root->root_key.objectid == BTRFS_TREE_LOG_OBJECTID) {
		set_extent_dirty(&root->dirty_log_pages, buf->start,
			 buf->start + buf->len - 1, GFP_NOFS);
	} else {
		set_extent_dirty(&trans->transaction->dirty_pages, buf->start,
			 buf->start + buf->len - 1, GFP_NOFS);
	}
	trans->blocks_used++;
	return buf;
}

/*
 * helper function to allocate a block for a given tree
 * returns the tree buffer or NULL.
 */
struct extent_buffer *btrfs_alloc_free_block(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     u32 blocksize, u64 parent,
					     u64 root_objectid,
					     u64 ref_generation,
					     int level,
					     u64 hint,
					     u64 empty_size)
{
	struct btrfs_key ins;
	int ret;
	struct extent_buffer *buf;

	ret = btrfs_alloc_extent(trans, root, blocksize, parent, blocksize,
				 root_objectid, ref_generation, level, 0,
				 empty_size, hint, (u64)-1, &ins, 0);
	if (ret) {
		BUG_ON(ret > 0);
		return ERR_PTR(ret);
	}

	buf = btrfs_init_new_buffer(trans, root, ins.objectid, blocksize);
	return buf;
}

int btrfs_drop_leaf_ref(struct btrfs_trans_handle *trans,
			struct btrfs_root *root, struct extent_buffer *leaf)
{
	u64 leaf_owner;
	u64 leaf_generation;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	int i;
	int nritems;
	int ret;

	BUG_ON(!btrfs_is_leaf(leaf));
	nritems = btrfs_header_nritems(leaf);
	leaf_owner = btrfs_header_owner(leaf);
	leaf_generation = btrfs_header_generation(leaf);

	for (i = 0; i < nritems; i++) {
		u64 disk_bytenr;
		cond_resched();

		btrfs_item_key_to_cpu(leaf, &key, i);
		if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY)
			continue;
		fi = btrfs_item_ptr(leaf, i, struct btrfs_file_extent_item);
		if (btrfs_file_extent_type(leaf, fi) ==
		    BTRFS_FILE_EXTENT_INLINE)
			continue;
		/*
		 * FIXME make sure to insert a trans record that
		 * repeats the snapshot del on crash
		 */
		disk_bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
		if (disk_bytenr == 0)
			continue;

		mutex_lock(&root->fs_info->alloc_mutex);
		ret = __btrfs_free_extent(trans, root, disk_bytenr,
				btrfs_file_extent_disk_num_bytes(leaf, fi),
				leaf->start, leaf_owner, leaf_generation,
				key.objectid, key.offset, 0);
		mutex_unlock(&root->fs_info->alloc_mutex);
		BUG_ON(ret);

		atomic_inc(&root->fs_info->throttle_gen);
		wake_up(&root->fs_info->transaction_throttle);
		cond_resched();
	}
	return 0;
}

static int noinline cache_drop_leaf_ref(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_leaf_ref *ref)
{
	int i;
	int ret;
	struct btrfs_extent_info *info = ref->extents;

	for (i = 0; i < ref->nritems; i++) {
		mutex_lock(&root->fs_info->alloc_mutex);
		ret = __btrfs_free_extent(trans, root, info->bytenr,
					  info->num_bytes, ref->bytenr,
					  ref->owner, ref->generation,
					  info->objectid, info->offset, 0);
		mutex_unlock(&root->fs_info->alloc_mutex);

		atomic_inc(&root->fs_info->throttle_gen);
		wake_up(&root->fs_info->transaction_throttle);
		cond_resched();

		BUG_ON(ret);
		info++;
	}

	return 0;
}

int drop_snap_lookup_refcount(struct btrfs_root *root, u64 start, u64 len,
			      u32 *refs)
{
	int ret;

	ret = btrfs_lookup_extent_ref(NULL, root, start, len, refs);
	BUG_ON(ret);

#if 0 // some debugging code in case we see problems here
	/* if the refs count is one, it won't get increased again.  But
	 * if the ref count is > 1, someone may be decreasing it at
	 * the same time we are.
	 */
	if (*refs != 1) {
		struct extent_buffer *eb = NULL;
		eb = btrfs_find_create_tree_block(root, start, len);
		if (eb)
			btrfs_tree_lock(eb);

		mutex_lock(&root->fs_info->alloc_mutex);
		ret = lookup_extent_ref(NULL, root, start, len, refs);
		BUG_ON(ret);
		mutex_unlock(&root->fs_info->alloc_mutex);

		if (eb) {
			btrfs_tree_unlock(eb);
			free_extent_buffer(eb);
		}
		if (*refs == 1) {
			printk("block %llu went down to one during drop_snap\n",
			       (unsigned long long)start);
		}

	}
#endif

	cond_resched();
	return ret;
}

/*
 * helper function for drop_snapshot, this walks down the tree dropping ref
 * counts as it goes.
 */
static int noinline walk_down_tree(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct btrfs_path *path, int *level)
{
	u64 root_owner;
	u64 root_gen;
	u64 bytenr;
	u64 ptr_gen;
	struct extent_buffer *next;
	struct extent_buffer *cur;
	struct extent_buffer *parent;
	struct btrfs_leaf_ref *ref;
	u32 blocksize;
	int ret;
	u32 refs;

	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);
	ret = drop_snap_lookup_refcount(root, path->nodes[*level]->start,
				path->nodes[*level]->len, &refs);
	BUG_ON(ret);
	if (refs > 1)
		goto out;

	/*
	 * walk down to the last node level and free all the leaves
	 */
	while(*level >= 0) {
		WARN_ON(*level < 0);
		WARN_ON(*level >= BTRFS_MAX_LEVEL);
		cur = path->nodes[*level];

		if (btrfs_header_level(cur) != *level)
			WARN_ON(1);

		if (path->slots[*level] >=
		    btrfs_header_nritems(cur))
			break;
		if (*level == 0) {
			ret = btrfs_drop_leaf_ref(trans, root, cur);
			BUG_ON(ret);
			break;
		}
		bytenr = btrfs_node_blockptr(cur, path->slots[*level]);
		ptr_gen = btrfs_node_ptr_generation(cur, path->slots[*level]);
		blocksize = btrfs_level_size(root, *level - 1);

		ret = drop_snap_lookup_refcount(root, bytenr, blocksize, &refs);
		BUG_ON(ret);
		if (refs != 1) {
			parent = path->nodes[*level];
			root_owner = btrfs_header_owner(parent);
			root_gen = btrfs_header_generation(parent);
			path->slots[*level]++;

			mutex_lock(&root->fs_info->alloc_mutex);
			ret = __btrfs_free_extent(trans, root, bytenr,
						blocksize, parent->start,
						root_owner, root_gen, 0, 0, 1);
			BUG_ON(ret);
			mutex_unlock(&root->fs_info->alloc_mutex);

			atomic_inc(&root->fs_info->throttle_gen);
			wake_up(&root->fs_info->transaction_throttle);
			cond_resched();

			continue;
		}
		/*
		 * at this point, we have a single ref, and since the
		 * only place referencing this extent is a dead root
		 * the reference count should never go higher.
		 * So, we don't need to check it again
		 */
		if (*level == 1) {
			ref = btrfs_lookup_leaf_ref(root, bytenr);
			if (ref && ref->generation != ptr_gen) {
				btrfs_free_leaf_ref(root, ref);
				ref = NULL;
			}
			if (ref) {
				ret = cache_drop_leaf_ref(trans, root, ref);
				BUG_ON(ret);
				btrfs_remove_leaf_ref(root, ref);
				btrfs_free_leaf_ref(root, ref);
				*level = 0;
				break;
			}
			if (printk_ratelimit())
				printk("leaf ref miss for bytenr %llu\n",
				       (unsigned long long)bytenr);
		}
		next = btrfs_find_tree_block(root, bytenr, blocksize);
		if (!next || !btrfs_buffer_uptodate(next, ptr_gen)) {
			free_extent_buffer(next);

			next = read_tree_block(root, bytenr, blocksize,
					       ptr_gen);
			cond_resched();
#if 0
			/*
			 * this is a debugging check and can go away
			 * the ref should never go all the way down to 1
			 * at this point
			 */
			ret = lookup_extent_ref(NULL, root, bytenr, blocksize,
						&refs);
			BUG_ON(ret);
			WARN_ON(refs != 1);
#endif
		}
		WARN_ON(*level <= 0);
		if (path->nodes[*level-1])
			free_extent_buffer(path->nodes[*level-1]);
		path->nodes[*level-1] = next;
		*level = btrfs_header_level(next);
		path->slots[*level] = 0;
		cond_resched();
	}
out:
	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);

	if (path->nodes[*level] == root->node) {
		parent = path->nodes[*level];
		bytenr = path->nodes[*level]->start;
	} else {
		parent = path->nodes[*level + 1];
		bytenr = btrfs_node_blockptr(parent, path->slots[*level + 1]);
	}

	blocksize = btrfs_level_size(root, *level);
	root_owner = btrfs_header_owner(parent);
	root_gen = btrfs_header_generation(parent);

	mutex_lock(&root->fs_info->alloc_mutex);
	ret = __btrfs_free_extent(trans, root, bytenr, blocksize,
				  parent->start, root_owner, root_gen,
				  0, 0, 1);
	mutex_unlock(&root->fs_info->alloc_mutex);
	free_extent_buffer(path->nodes[*level]);
	path->nodes[*level] = NULL;
	*level += 1;
	BUG_ON(ret);

	cond_resched();
	return 0;
}

/*
 * helper for dropping snapshots.  This walks back up the tree in the path
 * to find the first node higher up where we haven't yet gone through
 * all the slots
 */
static int noinline walk_up_tree(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path, int *level)
{
	u64 root_owner;
	u64 root_gen;
	struct btrfs_root_item *root_item = &root->root_item;
	int i;
	int slot;
	int ret;

	for(i = *level; i < BTRFS_MAX_LEVEL - 1 && path->nodes[i]; i++) {
		slot = path->slots[i];
		if (slot < btrfs_header_nritems(path->nodes[i]) - 1) {
			struct extent_buffer *node;
			struct btrfs_disk_key disk_key;
			node = path->nodes[i];
			path->slots[i]++;
			*level = i;
			WARN_ON(*level == 0);
			btrfs_node_key(node, &disk_key, path->slots[i]);
			memcpy(&root_item->drop_progress,
			       &disk_key, sizeof(disk_key));
			root_item->drop_level = i;
			return 0;
		} else {
			struct extent_buffer *parent;
			if (path->nodes[*level] == root->node)
				parent = path->nodes[*level];
			else
				parent = path->nodes[*level + 1];

			root_owner = btrfs_header_owner(parent);
			root_gen = btrfs_header_generation(parent);
			ret = btrfs_free_extent(trans, root,
						path->nodes[*level]->start,
						path->nodes[*level]->len,
						parent->start,
						root_owner, root_gen, 0, 0, 1);
			BUG_ON(ret);
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
int btrfs_drop_snapshot(struct btrfs_trans_handle *trans, struct btrfs_root
			*root)
{
	int ret = 0;
	int wret;
	int level;
	struct btrfs_path *path;
	int i;
	int orig_level;
	struct btrfs_root_item *root_item = &root->root_item;

	WARN_ON(!mutex_is_locked(&root->fs_info->drop_mutex));
	path = btrfs_alloc_path();
	BUG_ON(!path);

	level = btrfs_header_level(root->node);
	orig_level = level;
	if (btrfs_disk_key_objectid(&root_item->drop_progress) == 0) {
		path->nodes[level] = root->node;
		extent_buffer_get(root->node);
		path->slots[level] = 0;
	} else {
		struct btrfs_key key;
		struct btrfs_disk_key found_key;
		struct extent_buffer *node;

		btrfs_disk_key_to_cpu(&key, &root_item->drop_progress);
		level = root_item->drop_level;
		path->lowest_level = level;
		wret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (wret < 0) {
			ret = wret;
			goto out;
		}
		node = path->nodes[level];
		btrfs_node_key(node, &found_key, path->slots[level]);
		WARN_ON(memcmp(&found_key, &root_item->drop_progress,
			       sizeof(found_key)));
		/*
		 * unlock our path, this is safe because only this
		 * function is allowed to delete this snapshot
		 */
		for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
			if (path->nodes[i] && path->locks[i]) {
				path->locks[i] = 0;
				btrfs_tree_unlock(path->nodes[i]);
			}
		}
	}
	while(1) {
		wret = walk_down_tree(trans, root, path, &level);
		if (wret > 0)
			break;
		if (wret < 0)
			ret = wret;

		wret = walk_up_tree(trans, root, path, &level);
		if (wret > 0)
			break;
		if (wret < 0)
			ret = wret;
		if (trans->transaction->in_commit) {
			ret = -EAGAIN;
			break;
		}
		atomic_inc(&root->fs_info->throttle_gen);
		wake_up(&root->fs_info->transaction_throttle);
	}
	for (i = 0; i <= orig_level; i++) {
		if (path->nodes[i]) {
			free_extent_buffer(path->nodes[i]);
			path->nodes[i] = NULL;
		}
	}
out:
	btrfs_free_path(path);
	return ret;
}

static unsigned long calc_ra(unsigned long start, unsigned long last,
			     unsigned long nr)
{
	return min(last, start + nr - 1);
}

static int noinline relocate_inode_pages(struct inode *inode, u64 start,
					 u64 len)
{
	u64 page_start;
	u64 page_end;
	unsigned long first_index;
	unsigned long last_index;
	unsigned long i;
	struct page *page;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct file_ra_state *ra;
	struct btrfs_ordered_extent *ordered;
	unsigned int total_read = 0;
	unsigned int total_dirty = 0;
	int ret = 0;

	ra = kzalloc(sizeof(*ra), GFP_NOFS);

	mutex_lock(&inode->i_mutex);
	first_index = start >> PAGE_CACHE_SHIFT;
	last_index = (start + len - 1) >> PAGE_CACHE_SHIFT;

	/* make sure the dirty trick played by the caller work */
	ret = invalidate_inode_pages2_range(inode->i_mapping,
					    first_index, last_index);
	if (ret)
		goto out_unlock;

	file_ra_state_init(ra, inode->i_mapping);

	for (i = first_index ; i <= last_index; i++) {
		if (total_read % ra->ra_pages == 0) {
			btrfs_force_ra(inode->i_mapping, ra, NULL, i,
				       calc_ra(i, last_index, ra->ra_pages));
		}
		total_read++;
again:
		if (((u64)i << PAGE_CACHE_SHIFT) > i_size_read(inode))
			BUG_ON(1);
		page = grab_cache_page(inode->i_mapping, i);
		if (!page) {
			ret = -ENOMEM;
			goto out_unlock;
		}
		if (!PageUptodate(page)) {
			btrfs_readpage(NULL, page);
			lock_page(page);
			if (!PageUptodate(page)) {
				unlock_page(page);
				page_cache_release(page);
				ret = -EIO;
				goto out_unlock;
			}
		}
		wait_on_page_writeback(page);

		page_start = (u64)page->index << PAGE_CACHE_SHIFT;
		page_end = page_start + PAGE_CACHE_SIZE - 1;
		lock_extent(io_tree, page_start, page_end, GFP_NOFS);

		ordered = btrfs_lookup_ordered_extent(inode, page_start);
		if (ordered) {
			unlock_extent(io_tree, page_start, page_end, GFP_NOFS);
			unlock_page(page);
			page_cache_release(page);
			btrfs_start_ordered_extent(inode, ordered, 1);
			btrfs_put_ordered_extent(ordered);
			goto again;
		}
		set_page_extent_mapped(page);

		btrfs_set_extent_delalloc(inode, page_start, page_end);
		if (i == first_index)
			set_extent_bits(io_tree, page_start, page_end,
					EXTENT_BOUNDARY, GFP_NOFS);

		set_page_dirty(page);
		total_dirty++;

		unlock_extent(io_tree, page_start, page_end, GFP_NOFS);
		unlock_page(page);
		page_cache_release(page);
	}

out_unlock:
	kfree(ra);
	mutex_unlock(&inode->i_mutex);
	balance_dirty_pages_ratelimited_nr(inode->i_mapping, total_dirty);
	return ret;
}

static int noinline relocate_data_extent(struct inode *reloc_inode,
					 struct btrfs_key *extent_key,
					 u64 offset)
{
	struct btrfs_root *root = BTRFS_I(reloc_inode)->root;
	struct extent_map_tree *em_tree = &BTRFS_I(reloc_inode)->extent_tree;
	struct extent_map *em;

	em = alloc_extent_map(GFP_NOFS);
	BUG_ON(!em || IS_ERR(em));

	em->start = extent_key->objectid - offset;
	em->len = extent_key->offset;
	em->block_start = extent_key->objectid;
	em->bdev = root->fs_info->fs_devices->latest_bdev;
	set_bit(EXTENT_FLAG_PINNED, &em->flags);

	/* setup extent map to cheat btrfs_readpage */
	mutex_lock(&BTRFS_I(reloc_inode)->extent_mutex);
	while (1) {
		int ret;
		spin_lock(&em_tree->lock);
		ret = add_extent_mapping(em_tree, em);
		spin_unlock(&em_tree->lock);
		if (ret != -EEXIST) {
			free_extent_map(em);
			break;
		}
		btrfs_drop_extent_cache(reloc_inode, em->start,
					em->start + em->len - 1, 0);
	}
	mutex_unlock(&BTRFS_I(reloc_inode)->extent_mutex);

	return relocate_inode_pages(reloc_inode, extent_key->objectid - offset,
				    extent_key->offset);
}

struct btrfs_ref_path {
	u64 extent_start;
	u64 nodes[BTRFS_MAX_LEVEL];
	u64 root_objectid;
	u64 root_generation;
	u64 owner_objectid;
	u64 owner_offset;
	u32 num_refs;
	int lowest_level;
	int current_level;
};

struct disk_extent {
	u64 disk_bytenr;
	u64 disk_num_bytes;
	u64 offset;
	u64 num_bytes;
};

static int is_cowonly_root(u64 root_objectid)
{
	if (root_objectid == BTRFS_ROOT_TREE_OBJECTID ||
	    root_objectid == BTRFS_EXTENT_TREE_OBJECTID ||
	    root_objectid == BTRFS_CHUNK_TREE_OBJECTID ||
	    root_objectid == BTRFS_DEV_TREE_OBJECTID ||
	    root_objectid == BTRFS_TREE_LOG_OBJECTID)
		return 1;
	return 0;
}

static int noinline __next_ref_path(struct btrfs_trans_handle *trans,
				    struct btrfs_root *extent_root,
				    struct btrfs_ref_path *ref_path,
				    int first_time)
{
	struct extent_buffer *leaf;
	struct btrfs_path *path;
	struct btrfs_extent_ref *ref;
	struct btrfs_key key;
	struct btrfs_key found_key;
	u64 bytenr;
	u32 nritems;
	int level;
	int ret = 1;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	mutex_lock(&extent_root->fs_info->alloc_mutex);

	if (first_time) {
		ref_path->lowest_level = -1;
		ref_path->current_level = -1;
		goto walk_up;
	}
walk_down:
	level = ref_path->current_level - 1;
	while (level >= -1) {
		u64 parent;
		if (level < ref_path->lowest_level)
			break;

		if (level >= 0) {
			bytenr = ref_path->nodes[level];
		} else {
			bytenr = ref_path->extent_start;
		}
		BUG_ON(bytenr == 0);

		parent = ref_path->nodes[level + 1];
		ref_path->nodes[level + 1] = 0;
		ref_path->current_level = level;
		BUG_ON(parent == 0);

		key.objectid = bytenr;
		key.offset = parent + 1;
		key.type = BTRFS_EXTENT_REF_KEY;

		ret = btrfs_search_slot(trans, extent_root, &key, path, 0, 0);
		if (ret < 0)
			goto out;
		BUG_ON(ret == 0);

		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(extent_root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				goto next;
			leaf = path->nodes[0];
		}

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid == bytenr &&
				found_key.type == BTRFS_EXTENT_REF_KEY)
			goto found;
next:
		level--;
		btrfs_release_path(extent_root, path);
		if (need_resched()) {
			mutex_unlock(&extent_root->fs_info->alloc_mutex);
			cond_resched();
			mutex_lock(&extent_root->fs_info->alloc_mutex);
		}
	}
	/* reached lowest level */
	ret = 1;
	goto out;
walk_up:
	level = ref_path->current_level;
	while (level < BTRFS_MAX_LEVEL - 1) {
		u64 ref_objectid;
		if (level >= 0) {
			bytenr = ref_path->nodes[level];
		} else {
			bytenr = ref_path->extent_start;
		}
		BUG_ON(bytenr == 0);

		key.objectid = bytenr;
		key.offset = 0;
		key.type = BTRFS_EXTENT_REF_KEY;

		ret = btrfs_search_slot(trans, extent_root, &key, path, 0, 0);
		if (ret < 0)
			goto out;

		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(extent_root, path);
			if (ret < 0)
				goto out;
			if (ret > 0) {
				/* the extent was freed by someone */
				if (ref_path->lowest_level == level)
					goto out;
				btrfs_release_path(extent_root, path);
				goto walk_down;
			}
			leaf = path->nodes[0];
		}

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid != bytenr ||
				found_key.type != BTRFS_EXTENT_REF_KEY) {
			/* the extent was freed by someone */
			if (ref_path->lowest_level == level) {
				ret = 1;
				goto out;
			}
			btrfs_release_path(extent_root, path);
			goto walk_down;
		}
found:
		ref = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_extent_ref);
		ref_objectid = btrfs_ref_objectid(leaf, ref);
		if (ref_objectid < BTRFS_FIRST_FREE_OBJECTID) {
			if (first_time) {
				level = (int)ref_objectid;
				BUG_ON(level >= BTRFS_MAX_LEVEL);
				ref_path->lowest_level = level;
				ref_path->current_level = level;
				ref_path->nodes[level] = bytenr;
			} else {
				WARN_ON(ref_objectid != level);
			}
		} else {
			WARN_ON(level != -1);
		}
		first_time = 0;

		if (ref_path->lowest_level == level) {
			ref_path->owner_objectid = ref_objectid;
			ref_path->owner_offset = btrfs_ref_offset(leaf, ref);
			ref_path->num_refs = btrfs_ref_num_refs(leaf, ref);
		}

		/*
		 * the block is tree root or the block isn't in reference
		 * counted tree.
		 */
		if (found_key.objectid == found_key.offset ||
		    is_cowonly_root(btrfs_ref_root(leaf, ref))) {
			ref_path->root_objectid = btrfs_ref_root(leaf, ref);
			ref_path->root_generation =
				btrfs_ref_generation(leaf, ref);
			if (level < 0) {
				/* special reference from the tree log */
				ref_path->nodes[0] = found_key.offset;
				ref_path->current_level = 0;
			}
			ret = 0;
			goto out;
		}

		level++;
		BUG_ON(ref_path->nodes[level] != 0);
		ref_path->nodes[level] = found_key.offset;
		ref_path->current_level = level;

		/*
		 * the reference was created in the running transaction,
		 * no need to continue walking up.
		 */
		if (btrfs_ref_generation(leaf, ref) == trans->transid) {
			ref_path->root_objectid = btrfs_ref_root(leaf, ref);
			ref_path->root_generation =
				btrfs_ref_generation(leaf, ref);
			ret = 0;
			goto out;
		}

		btrfs_release_path(extent_root, path);
		if (need_resched()) {
			mutex_unlock(&extent_root->fs_info->alloc_mutex);
			cond_resched();
			mutex_lock(&extent_root->fs_info->alloc_mutex);
		}
	}
	/* reached max tree level, but no tree root found. */
	BUG();
out:
	mutex_unlock(&extent_root->fs_info->alloc_mutex);
	btrfs_free_path(path);
	return ret;
}

static int btrfs_first_ref_path(struct btrfs_trans_handle *trans,
				struct btrfs_root *extent_root,
				struct btrfs_ref_path *ref_path,
				u64 extent_start)
{
	memset(ref_path, 0, sizeof(*ref_path));
	ref_path->extent_start = extent_start;

	return __next_ref_path(trans, extent_root, ref_path, 1);
}

static int btrfs_next_ref_path(struct btrfs_trans_handle *trans,
			       struct btrfs_root *extent_root,
			       struct btrfs_ref_path *ref_path)
{
	return __next_ref_path(trans, extent_root, ref_path, 0);
}

static int noinline get_new_locations(struct inode *reloc_inode,
				      struct btrfs_key *extent_key,
				      u64 offset, int no_fragment,
				      struct disk_extent **extents,
				      int *nr_extents)
{
	struct btrfs_root *root = BTRFS_I(reloc_inode)->root;
	struct btrfs_path *path;
	struct btrfs_file_extent_item *fi;
	struct extent_buffer *leaf;
	struct disk_extent *exts = *extents;
	struct btrfs_key found_key;
	u64 cur_pos;
	u64 last_byte;
	u32 nritems;
	int nr = 0;
	int max = *nr_extents;
	int ret;

	WARN_ON(!no_fragment && *extents);
	if (!exts) {
		max = 1;
		exts = kmalloc(sizeof(*exts) * max, GFP_NOFS);
		if (!exts)
			return -ENOMEM;
	}

	path = btrfs_alloc_path();
	BUG_ON(!path);

	cur_pos = extent_key->objectid - offset;
	last_byte = extent_key->objectid + extent_key->offset;
	ret = btrfs_lookup_file_extent(NULL, root, path, reloc_inode->i_ino,
				       cur_pos, 0);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	while (1) {
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				break;
			leaf = path->nodes[0];
		}

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.offset != cur_pos ||
		    found_key.type != BTRFS_EXTENT_DATA_KEY ||
		    found_key.objectid != reloc_inode->i_ino)
			break;

		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		if (btrfs_file_extent_type(leaf, fi) !=
		    BTRFS_FILE_EXTENT_REG ||
		    btrfs_file_extent_disk_bytenr(leaf, fi) == 0)
			break;

		if (nr == max) {
			struct disk_extent *old = exts;
			max *= 2;
			exts = kzalloc(sizeof(*exts) * max, GFP_NOFS);
			memcpy(exts, old, sizeof(*exts) * nr);
			if (old != *extents)
				kfree(old);
		}

		exts[nr].disk_bytenr =
			btrfs_file_extent_disk_bytenr(leaf, fi);
		exts[nr].disk_num_bytes =
			btrfs_file_extent_disk_num_bytes(leaf, fi);
		exts[nr].offset = btrfs_file_extent_offset(leaf, fi);
		exts[nr].num_bytes = btrfs_file_extent_num_bytes(leaf, fi);
		WARN_ON(exts[nr].offset > 0);
		WARN_ON(exts[nr].num_bytes != exts[nr].disk_num_bytes);

		cur_pos += exts[nr].num_bytes;
		nr++;

		if (cur_pos + offset >= last_byte)
			break;

		if (no_fragment) {
			ret = 1;
			goto out;
		}
		path->slots[0]++;
	}

	WARN_ON(cur_pos + offset > last_byte);
	if (cur_pos + offset < last_byte) {
		ret = -ENOENT;
		goto out;
	}
	ret = 0;
out:
	btrfs_free_path(path);
	if (ret) {
		if (exts != *extents)
			kfree(exts);
	} else {
		*extents = exts;
		*nr_extents = nr;
	}
	return ret;
}

static int noinline replace_one_extent(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_path *path,
					struct btrfs_key *extent_key,
					struct btrfs_key *leaf_key,
					struct btrfs_ref_path *ref_path,
					struct disk_extent *new_extents,
					int nr_extents)
{
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *fi;
	struct inode *inode = NULL;
	struct btrfs_key key;
	u64 lock_start = 0;
	u64 lock_end = 0;
	u64 num_bytes;
	u64 ext_offset;
	u64 first_pos;
	u32 nritems;
	int extent_locked = 0;
	int ret;

	first_pos = ref_path->owner_offset;
	if (ref_path->owner_objectid != BTRFS_MULTIPLE_OBJECTIDS) {
		key.objectid = ref_path->owner_objectid;
		key.offset = ref_path->owner_offset;
		key.type = BTRFS_EXTENT_DATA_KEY;
	} else {
		memcpy(&key, leaf_key, sizeof(key));
	}

	while (1) {
		ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
		if (ret < 0)
			goto out;

		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
next:
		if (extent_locked && ret > 0) {
			/*
			 * the file extent item was modified by someone
			 * before the extent got locked.
			 */
			mutex_unlock(&BTRFS_I(inode)->extent_mutex);
			unlock_extent(&BTRFS_I(inode)->io_tree, lock_start,
				      lock_end, GFP_NOFS);
			extent_locked = 0;
		}

		if (path->slots[0] >= nritems) {
			if (ref_path->owner_objectid ==
			    BTRFS_MULTIPLE_OBJECTIDS)
				break;

			BUG_ON(extent_locked);
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				break;
			leaf = path->nodes[0];
			nritems = btrfs_header_nritems(leaf);
		}

		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

		if (ref_path->owner_objectid != BTRFS_MULTIPLE_OBJECTIDS) {
			if ((key.objectid > ref_path->owner_objectid) ||
			    (key.objectid == ref_path->owner_objectid &&
			     key.type > BTRFS_EXTENT_DATA_KEY) ||
			    (key.offset >= first_pos + extent_key->offset))
				break;
		}

		if (inode && key.objectid != inode->i_ino) {
			BUG_ON(extent_locked);
			btrfs_release_path(root, path);
			mutex_unlock(&inode->i_mutex);
			iput(inode);
			inode = NULL;
			continue;
		}

		if (key.type != BTRFS_EXTENT_DATA_KEY) {
			path->slots[0]++;
			ret = 1;
			goto next;
		}
		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		if ((btrfs_file_extent_type(leaf, fi) !=
		     BTRFS_FILE_EXTENT_REG) ||
		    (btrfs_file_extent_disk_bytenr(leaf, fi) !=
		     extent_key->objectid)) {
			path->slots[0]++;
			ret = 1;
			goto next;
		}

		num_bytes = btrfs_file_extent_num_bytes(leaf, fi);
		ext_offset = btrfs_file_extent_offset(leaf, fi);

		if (first_pos > key.offset - ext_offset)
			first_pos = key.offset - ext_offset;

		if (!extent_locked) {
			lock_start = key.offset;
			lock_end = lock_start + num_bytes - 1;
		} else {
			BUG_ON(lock_start != key.offset);
			BUG_ON(lock_end - lock_start + 1 < num_bytes);
		}

		if (!inode) {
			btrfs_release_path(root, path);

			inode = btrfs_iget_locked(root->fs_info->sb,
						  key.objectid, root);
			if (inode->i_state & I_NEW) {
				BTRFS_I(inode)->root = root;
				BTRFS_I(inode)->location.objectid =
					key.objectid;
				BTRFS_I(inode)->location.type =
					BTRFS_INODE_ITEM_KEY;
				BTRFS_I(inode)->location.offset = 0;
				btrfs_read_locked_inode(inode);
				unlock_new_inode(inode);
			}
			/*
			 * some code call btrfs_commit_transaction while
			 * holding the i_mutex, so we can't use mutex_lock
			 * here.
			 */
			if (is_bad_inode(inode) ||
			    !mutex_trylock(&inode->i_mutex)) {
				iput(inode);
				inode = NULL;
				key.offset = (u64)-1;
				goto skip;
			}
		}

		if (!extent_locked) {
			struct btrfs_ordered_extent *ordered;

			btrfs_release_path(root, path);

			lock_extent(&BTRFS_I(inode)->io_tree, lock_start,
				    lock_end, GFP_NOFS);
			ordered = btrfs_lookup_first_ordered_extent(inode,
								    lock_end);
			if (ordered &&
			    ordered->file_offset <= lock_end &&
			    ordered->file_offset + ordered->len > lock_start) {
				unlock_extent(&BTRFS_I(inode)->io_tree,
					      lock_start, lock_end, GFP_NOFS);
				btrfs_start_ordered_extent(inode, ordered, 1);
				btrfs_put_ordered_extent(ordered);
				key.offset += num_bytes;
				goto skip;
			}
			if (ordered)
				btrfs_put_ordered_extent(ordered);

			mutex_lock(&BTRFS_I(inode)->extent_mutex);
			extent_locked = 1;
			continue;
		}

		if (nr_extents == 1) {
			/* update extent pointer in place */
			btrfs_set_file_extent_generation(leaf, fi,
						trans->transid);
			btrfs_set_file_extent_disk_bytenr(leaf, fi,
						new_extents[0].disk_bytenr);
			btrfs_set_file_extent_disk_num_bytes(leaf, fi,
						new_extents[0].disk_num_bytes);
			ext_offset += new_extents[0].offset;
			btrfs_set_file_extent_offset(leaf, fi, ext_offset);
			btrfs_mark_buffer_dirty(leaf);

			btrfs_drop_extent_cache(inode, key.offset,
						key.offset + num_bytes - 1, 0);

			ret = btrfs_inc_extent_ref(trans, root,
						new_extents[0].disk_bytenr,
						new_extents[0].disk_num_bytes,
						leaf->start,
						root->root_key.objectid,
						trans->transid,
						key.objectid, key.offset);
			BUG_ON(ret);

			ret = btrfs_free_extent(trans, root,
						extent_key->objectid,
						extent_key->offset,
						leaf->start,
						btrfs_header_owner(leaf),
						btrfs_header_generation(leaf),
						key.objectid, key.offset, 0);
			BUG_ON(ret);

			btrfs_release_path(root, path);
			key.offset += num_bytes;
		} else {
			u64 alloc_hint;
			u64 extent_len;
			int i;
			/*
			 * drop old extent pointer at first, then insert the
			 * new pointers one bye one
			 */
			btrfs_release_path(root, path);
			ret = btrfs_drop_extents(trans, root, inode, key.offset,
						 key.offset + num_bytes,
						 key.offset, &alloc_hint);
			BUG_ON(ret);

			for (i = 0; i < nr_extents; i++) {
				if (ext_offset >= new_extents[i].num_bytes) {
					ext_offset -= new_extents[i].num_bytes;
					continue;
				}
				extent_len = min(new_extents[i].num_bytes -
						 ext_offset, num_bytes);

				ret = btrfs_insert_empty_item(trans, root,
							      path, &key,
							      sizeof(*fi));
				BUG_ON(ret);

				leaf = path->nodes[0];
				fi = btrfs_item_ptr(leaf, path->slots[0],
						struct btrfs_file_extent_item);
				btrfs_set_file_extent_generation(leaf, fi,
							trans->transid);
				btrfs_set_file_extent_type(leaf, fi,
							BTRFS_FILE_EXTENT_REG);
				btrfs_set_file_extent_disk_bytenr(leaf, fi,
						new_extents[i].disk_bytenr);
				btrfs_set_file_extent_disk_num_bytes(leaf, fi,
						new_extents[i].disk_num_bytes);
				btrfs_set_file_extent_num_bytes(leaf, fi,
							extent_len);
				ext_offset += new_extents[i].offset;
				btrfs_set_file_extent_offset(leaf, fi,
							ext_offset);
				btrfs_mark_buffer_dirty(leaf);

				btrfs_drop_extent_cache(inode, key.offset,
						key.offset + extent_len - 1, 0);

				ret = btrfs_inc_extent_ref(trans, root,
						new_extents[i].disk_bytenr,
						new_extents[i].disk_num_bytes,
						leaf->start,
						root->root_key.objectid,
						trans->transid,
						key.objectid, key.offset);
				BUG_ON(ret);
				btrfs_release_path(root, path);

				inode->i_blocks += extent_len >> 9;

				ext_offset = 0;
				num_bytes -= extent_len;
				key.offset += extent_len;

				if (num_bytes == 0)
					break;
			}
			BUG_ON(i >= nr_extents);
		}

		if (extent_locked) {
			mutex_unlock(&BTRFS_I(inode)->extent_mutex);
			unlock_extent(&BTRFS_I(inode)->io_tree, lock_start,
				      lock_end, GFP_NOFS);
			extent_locked = 0;
		}
skip:
		if (ref_path->owner_objectid != BTRFS_MULTIPLE_OBJECTIDS &&
		    key.offset >= first_pos + extent_key->offset)
			break;

		cond_resched();
	}
	ret = 0;
out:
	btrfs_release_path(root, path);
	if (inode) {
		mutex_unlock(&inode->i_mutex);
		if (extent_locked) {
			mutex_unlock(&BTRFS_I(inode)->extent_mutex);
			unlock_extent(&BTRFS_I(inode)->io_tree, lock_start,
				      lock_end, GFP_NOFS);
		}
		iput(inode);
	}
	return ret;
}

int btrfs_add_reloc_mapping(struct btrfs_root *root, u64 orig_bytenr,
			    u64 num_bytes, u64 new_bytenr)
{
	set_extent_bits(&root->fs_info->reloc_mapping_tree,
			orig_bytenr, orig_bytenr + num_bytes - 1,
			EXTENT_LOCKED, GFP_NOFS);
	set_state_private(&root->fs_info->reloc_mapping_tree,
			  orig_bytenr, new_bytenr);
	return 0;
}

int btrfs_get_reloc_mapping(struct btrfs_root *root, u64 orig_bytenr,
			    u64 num_bytes, u64 *new_bytenr)
{
	u64 bytenr;
	u64 cur_bytenr = orig_bytenr;
	u64 prev_bytenr = orig_bytenr;
	int ret;

	while (1) {
		ret = get_state_private(&root->fs_info->reloc_mapping_tree,
					cur_bytenr, &bytenr);
		if (ret)
			break;
		prev_bytenr = cur_bytenr;
		cur_bytenr = bytenr;
	}

	if (orig_bytenr == cur_bytenr)
		return -ENOENT;

	if (prev_bytenr != orig_bytenr) {
		set_state_private(&root->fs_info->reloc_mapping_tree,
				  orig_bytenr, cur_bytenr);
	}
	*new_bytenr = cur_bytenr;
	return 0;
}

void btrfs_free_reloc_mappings(struct btrfs_root *root)
{
	clear_extent_bits(&root->fs_info->reloc_mapping_tree,
			  0, (u64)-1, -1, GFP_NOFS);
}

int btrfs_reloc_tree_cache_ref(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct extent_buffer *buf, u64 orig_start)
{
	int level;
	int ret;

	BUG_ON(btrfs_header_generation(buf) != trans->transid);
	BUG_ON(root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID);

	level = btrfs_header_level(buf);
	if (level == 0) {
		struct btrfs_leaf_ref *ref;
		struct btrfs_leaf_ref *orig_ref;

		orig_ref = btrfs_lookup_leaf_ref(root, orig_start);
		if (!orig_ref)
			return -ENOENT;

		ref = btrfs_alloc_leaf_ref(root, orig_ref->nritems);
		if (!ref) {
			btrfs_free_leaf_ref(root, orig_ref);
			return -ENOMEM;
		}

		ref->nritems = orig_ref->nritems;
		memcpy(ref->extents, orig_ref->extents,
			sizeof(ref->extents[0]) * ref->nritems);

		btrfs_free_leaf_ref(root, orig_ref);

		ref->root_gen = trans->transid;
		ref->bytenr = buf->start;
		ref->owner = btrfs_header_owner(buf);
		ref->generation = btrfs_header_generation(buf);
		ret = btrfs_add_leaf_ref(root, ref, 0);
		WARN_ON(ret);
		btrfs_free_leaf_ref(root, ref);
	}
	return 0;
}

static int noinline invalidate_extent_cache(struct btrfs_root *root,
					struct extent_buffer *leaf,
					struct btrfs_block_group_cache *group,
					struct btrfs_root *target_root)
{
	struct btrfs_key key;
	struct inode *inode = NULL;
	struct btrfs_file_extent_item *fi;
	u64 num_bytes;
	u64 skip_objectid = 0;
	u32 nritems;
	u32 i;

	nritems = btrfs_header_nritems(leaf);
	for (i = 0; i < nritems; i++) {
		btrfs_item_key_to_cpu(leaf, &key, i);
		if (key.objectid == skip_objectid ||
		    key.type != BTRFS_EXTENT_DATA_KEY)
			continue;
		fi = btrfs_item_ptr(leaf, i, struct btrfs_file_extent_item);
		if (btrfs_file_extent_type(leaf, fi) ==
		    BTRFS_FILE_EXTENT_INLINE)
			continue;
		if (btrfs_file_extent_disk_bytenr(leaf, fi) == 0)
			continue;
		if (!inode || inode->i_ino != key.objectid) {
			iput(inode);
			inode = btrfs_ilookup(target_root->fs_info->sb,
					      key.objectid, target_root, 1);
		}
		if (!inode) {
			skip_objectid = key.objectid;
			continue;
		}
		num_bytes = btrfs_file_extent_num_bytes(leaf, fi);

		lock_extent(&BTRFS_I(inode)->io_tree, key.offset,
			    key.offset + num_bytes - 1, GFP_NOFS);
		mutex_lock(&BTRFS_I(inode)->extent_mutex);
		btrfs_drop_extent_cache(inode, key.offset,
					key.offset + num_bytes - 1, 1);
		mutex_unlock(&BTRFS_I(inode)->extent_mutex);
		unlock_extent(&BTRFS_I(inode)->io_tree, key.offset,
			      key.offset + num_bytes - 1, GFP_NOFS);
		cond_resched();
	}
	iput(inode);
	return 0;
}

static int noinline replace_extents_in_leaf(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct extent_buffer *leaf,
					struct btrfs_block_group_cache *group,
					struct inode *reloc_inode)
{
	struct btrfs_key key;
	struct btrfs_key extent_key;
	struct btrfs_file_extent_item *fi;
	struct btrfs_leaf_ref *ref;
	struct disk_extent *new_extent;
	u64 bytenr;
	u64 num_bytes;
	u32 nritems;
	u32 i;
	int ext_index;
	int nr_extent;
	int ret;

	new_extent = kmalloc(sizeof(*new_extent), GFP_NOFS);
	BUG_ON(!new_extent);

	ref = btrfs_lookup_leaf_ref(root, leaf->start);
	BUG_ON(!ref);

	ext_index = -1;
	nritems = btrfs_header_nritems(leaf);
	for (i = 0; i < nritems; i++) {
		btrfs_item_key_to_cpu(leaf, &key, i);
		if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY)
			continue;
		fi = btrfs_item_ptr(leaf, i, struct btrfs_file_extent_item);
		if (btrfs_file_extent_type(leaf, fi) ==
		    BTRFS_FILE_EXTENT_INLINE)
			continue;
		bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
		num_bytes = btrfs_file_extent_disk_num_bytes(leaf, fi);
		if (bytenr == 0)
			continue;

		ext_index++;
		if (bytenr >= group->key.objectid + group->key.offset ||
		    bytenr + num_bytes <= group->key.objectid)
			continue;

		extent_key.objectid = bytenr;
		extent_key.offset = num_bytes;
		extent_key.type = BTRFS_EXTENT_ITEM_KEY;
		nr_extent = 1;
		ret = get_new_locations(reloc_inode, &extent_key,
					group->key.objectid, 1,
					&new_extent, &nr_extent);
		if (ret > 0)
			continue;
		BUG_ON(ret < 0);

		BUG_ON(ref->extents[ext_index].bytenr != bytenr);
		BUG_ON(ref->extents[ext_index].num_bytes != num_bytes);
		ref->extents[ext_index].bytenr = new_extent->disk_bytenr;
		ref->extents[ext_index].num_bytes = new_extent->disk_num_bytes;

		btrfs_set_file_extent_generation(leaf, fi, trans->transid);
		btrfs_set_file_extent_disk_bytenr(leaf, fi,
						new_extent->disk_bytenr);
		btrfs_set_file_extent_disk_num_bytes(leaf, fi,
						new_extent->disk_num_bytes);
		new_extent->offset += btrfs_file_extent_offset(leaf, fi);
		btrfs_set_file_extent_offset(leaf, fi, new_extent->offset);
		btrfs_mark_buffer_dirty(leaf);

		ret = btrfs_inc_extent_ref(trans, root,
					new_extent->disk_bytenr,
					new_extent->disk_num_bytes,
					leaf->start,
					root->root_key.objectid,
					trans->transid,
					key.objectid, key.offset);
		BUG_ON(ret);
		ret = btrfs_free_extent(trans, root,
					bytenr, num_bytes, leaf->start,
					btrfs_header_owner(leaf),
					btrfs_header_generation(leaf),
					key.objectid, key.offset, 0);
		BUG_ON(ret);
		cond_resched();
	}
	kfree(new_extent);
	BUG_ON(ext_index + 1 != ref->nritems);
	btrfs_free_leaf_ref(root, ref);
	return 0;
}

int btrfs_free_reloc_root(struct btrfs_root *root)
{
	struct btrfs_root *reloc_root;

	if (root->reloc_root) {
		reloc_root = root->reloc_root;
		root->reloc_root = NULL;
		list_add(&reloc_root->dead_list,
			 &root->fs_info->dead_reloc_roots);
	}
	return 0;
}

int btrfs_drop_dead_reloc_roots(struct btrfs_root *root)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *reloc_root;
	struct btrfs_root *prev_root = NULL;
	struct list_head dead_roots;
	int ret;
	unsigned long nr;

	INIT_LIST_HEAD(&dead_roots);
	list_splice_init(&root->fs_info->dead_reloc_roots, &dead_roots);

	while (!list_empty(&dead_roots)) {
		reloc_root = list_entry(dead_roots.prev,
					struct btrfs_root, dead_list);
		list_del_init(&reloc_root->dead_list);

		BUG_ON(reloc_root->commit_root != NULL);
		while (1) {
			trans = btrfs_join_transaction(root, 1);
			BUG_ON(!trans);

			mutex_lock(&root->fs_info->drop_mutex);
			ret = btrfs_drop_snapshot(trans, reloc_root);
			if (ret != -EAGAIN)
				break;
			mutex_unlock(&root->fs_info->drop_mutex);

			nr = trans->blocks_used;
			ret = btrfs_end_transaction(trans, root);
			BUG_ON(ret);
			btrfs_btree_balance_dirty(root, nr);
		}

		free_extent_buffer(reloc_root->node);

		ret = btrfs_del_root(trans, root->fs_info->tree_root,
				     &reloc_root->root_key);
		BUG_ON(ret);
		mutex_unlock(&root->fs_info->drop_mutex);

		nr = trans->blocks_used;
		ret = btrfs_end_transaction(trans, root);
		BUG_ON(ret);
		btrfs_btree_balance_dirty(root, nr);

		kfree(prev_root);
		prev_root = reloc_root;
	}
	if (prev_root) {
		btrfs_remove_leaf_refs(prev_root, (u64)-1, 0);
		kfree(prev_root);
	}
	return 0;
}

int btrfs_add_dead_reloc_root(struct btrfs_root *root)
{
	list_add(&root->dead_list, &root->fs_info->dead_reloc_roots);
	return 0;
}

int btrfs_cleanup_reloc_trees(struct btrfs_root *root)
{
	struct btrfs_root *reloc_root;
	struct btrfs_trans_handle *trans;
	struct btrfs_key location;
	int found;
	int ret;

	mutex_lock(&root->fs_info->tree_reloc_mutex);
	ret = btrfs_find_dead_roots(root, BTRFS_TREE_RELOC_OBJECTID, NULL);
	BUG_ON(ret);
	found = !list_empty(&root->fs_info->dead_reloc_roots);
	mutex_unlock(&root->fs_info->tree_reloc_mutex);

	if (found) {
		trans = btrfs_start_transaction(root, 1);
		BUG_ON(!trans);
		ret = btrfs_commit_transaction(trans, root);
		BUG_ON(ret);
	}

	location.objectid = BTRFS_DATA_RELOC_TREE_OBJECTID;
	location.offset = (u64)-1;
	location.type = BTRFS_ROOT_ITEM_KEY;

	reloc_root = btrfs_read_fs_root_no_name(root->fs_info, &location);
	BUG_ON(!reloc_root);
	btrfs_orphan_cleanup(reloc_root);
	return 0;
}

static int noinline init_reloc_tree(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root)
{
	struct btrfs_root *reloc_root;
	struct extent_buffer *eb;
	struct btrfs_root_item *root_item;
	struct btrfs_key root_key;
	int ret;

	BUG_ON(!root->ref_cows);
	if (root->reloc_root)
		return 0;

	root_item = kmalloc(sizeof(*root_item), GFP_NOFS);
	BUG_ON(!root_item);

	ret = btrfs_copy_root(trans, root, root->commit_root,
			      &eb, BTRFS_TREE_RELOC_OBJECTID);
	BUG_ON(ret);

	root_key.objectid = BTRFS_TREE_RELOC_OBJECTID;
	root_key.offset = root->root_key.objectid;
	root_key.type = BTRFS_ROOT_ITEM_KEY;

	memcpy(root_item, &root->root_item, sizeof(root_item));
	btrfs_set_root_refs(root_item, 0);
	btrfs_set_root_bytenr(root_item, eb->start);
	btrfs_set_root_level(root_item, btrfs_header_level(eb));
	memset(&root_item->drop_progress, 0, sizeof(root_item->drop_progress));
	root_item->drop_level = 0;

	btrfs_tree_unlock(eb);
	free_extent_buffer(eb);

	ret = btrfs_insert_root(trans, root->fs_info->tree_root,
				&root_key, root_item);
	BUG_ON(ret);
	kfree(root_item);

	reloc_root = btrfs_read_fs_root_no_radix(root->fs_info->tree_root,
						 &root_key);
	BUG_ON(!reloc_root);
	reloc_root->last_trans = trans->transid;
	reloc_root->commit_root = NULL;
	reloc_root->ref_tree = &root->fs_info->reloc_ref_tree;

	root->reloc_root = reloc_root;
	return 0;
}

/*
 * Core function of space balance.
 *
 * The idea is using reloc trees to relocate tree blocks in reference
 * counted roots. There is one reloc tree for each subvol, all reloc
 * trees share same key objectid. Reloc trees are snapshots of the
 * latest committed roots (subvol root->commit_root). To relocate a tree
 * block referenced by a subvol, the code COW the block through the reloc
 * tree, then update pointer in the subvol to point to the new block.
 * Since all reloc trees share same key objectid, we can easily do special
 * handing to share tree blocks between reloc trees. Once a tree block has
 * been COWed in one reloc tree, we can use the result when the same block
 * is COWed again through other reloc trees.
 */
static int noinline relocate_one_path(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      struct btrfs_path *path,
				      struct btrfs_key *first_key,
				      struct btrfs_ref_path *ref_path,
				      struct btrfs_block_group_cache *group,
				      struct inode *reloc_inode)
{
	struct btrfs_root *reloc_root;
	struct extent_buffer *eb = NULL;
	struct btrfs_key *keys;
	u64 *nodes;
	int level;
	int lowest_merge;
	int lowest_level = 0;
	int update_refs;
	int ret;

	if (ref_path->owner_objectid < BTRFS_FIRST_FREE_OBJECTID)
		lowest_level = ref_path->owner_objectid;

	if (is_cowonly_root(ref_path->root_objectid)) {
		path->lowest_level = lowest_level;
		ret = btrfs_search_slot(trans, root, first_key, path, 0, 1);
		BUG_ON(ret < 0);
		path->lowest_level = 0;
		btrfs_release_path(root, path);
		return 0;
	}

	keys = kzalloc(sizeof(*keys) * BTRFS_MAX_LEVEL, GFP_NOFS);
	BUG_ON(!keys);
	nodes = kzalloc(sizeof(*nodes) * BTRFS_MAX_LEVEL, GFP_NOFS);
	BUG_ON(!nodes);

	mutex_lock(&root->fs_info->tree_reloc_mutex);
	ret = init_reloc_tree(trans, root);
	BUG_ON(ret);
	reloc_root = root->reloc_root;

	path->lowest_level = lowest_level;
	ret = btrfs_search_slot(trans, reloc_root, first_key, path, 0, 0);
	BUG_ON(ret);
	/*
	 * get relocation mapping for tree blocks in the path
	 */
	lowest_merge = BTRFS_MAX_LEVEL;
	for (level = BTRFS_MAX_LEVEL - 1; level >= lowest_level; level--) {
		u64 new_bytenr;
		eb = path->nodes[level];
		if (!eb || eb == reloc_root->node)
			continue;
		ret = btrfs_get_reloc_mapping(reloc_root, eb->start, eb->len,
					      &new_bytenr);
		if (ret)
			continue;
		if (level == 0)
			btrfs_item_key_to_cpu(eb, &keys[level], 0);
		else
			btrfs_node_key_to_cpu(eb, &keys[level], 0);
		nodes[level] = new_bytenr;
		lowest_merge = level;
	}

	update_refs = 0;
	if (ref_path->owner_objectid >= BTRFS_FIRST_FREE_OBJECTID) {
		eb = path->nodes[0];
		if (btrfs_header_generation(eb) < trans->transid)
			update_refs = 1;
	}

	btrfs_release_path(reloc_root, path);
	/*
	 * merge tree blocks that already relocated in other reloc trees
	 */
	if (lowest_merge != BTRFS_MAX_LEVEL) {
		ret = btrfs_merge_path(trans, reloc_root, keys, nodes,
				       lowest_merge);
		BUG_ON(ret < 0);
	}
	/*
	 * cow any tree blocks that still haven't been relocated
	 */
	ret = btrfs_search_slot(trans, reloc_root, first_key, path, 0, 1);
	BUG_ON(ret);
	/*
	 * if we are relocating data block group, update extent pointers
	 * in the newly created tree leaf.
	 */
	eb = path->nodes[0];
	if (update_refs && nodes[0] != eb->start) {
		ret = replace_extents_in_leaf(trans, reloc_root, eb, group,
					      reloc_inode);
		BUG_ON(ret);
	}

	memset(keys, 0, sizeof(*keys) * BTRFS_MAX_LEVEL);
	memset(nodes, 0, sizeof(*nodes) * BTRFS_MAX_LEVEL);
	for (level = BTRFS_MAX_LEVEL - 1; level >= lowest_level; level--) {
		eb = path->nodes[level];
		if (!eb || eb == reloc_root->node)
			continue;
		BUG_ON(btrfs_header_owner(eb) != BTRFS_TREE_RELOC_OBJECTID);
		nodes[level] = eb->start;
		if (level == 0)
			btrfs_item_key_to_cpu(eb, &keys[level], 0);
		else
			btrfs_node_key_to_cpu(eb, &keys[level], 0);
	}

	if (ref_path->owner_objectid >= BTRFS_FIRST_FREE_OBJECTID) {
		eb = path->nodes[0];
		extent_buffer_get(eb);
	}
	btrfs_release_path(reloc_root, path);
	/*
	 * replace tree blocks in the fs tree with tree blocks in
	 * the reloc tree.
	 */
	ret = btrfs_merge_path(trans, root, keys, nodes, lowest_level);
	BUG_ON(ret < 0);

	if (ref_path->owner_objectid >= BTRFS_FIRST_FREE_OBJECTID) {
		ret = invalidate_extent_cache(reloc_root, eb, group, root);
		BUG_ON(ret);
		free_extent_buffer(eb);
	}
	mutex_unlock(&root->fs_info->tree_reloc_mutex);

	path->lowest_level = 0;
	kfree(nodes);
	kfree(keys);
	return 0;
}

static int noinline relocate_tree_block(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_path *path,
					struct btrfs_key *first_key,
					struct btrfs_ref_path *ref_path)
{
	int ret;
	int needs_lock = 0;

	if (root == root->fs_info->extent_root ||
	    root == root->fs_info->chunk_root ||
	    root == root->fs_info->dev_root) {
		needs_lock = 1;
		mutex_lock(&root->fs_info->alloc_mutex);
	}

	ret = relocate_one_path(trans, root, path, first_key,
				ref_path, NULL, NULL);
	BUG_ON(ret);

	if (root == root->fs_info->extent_root)
		btrfs_extent_post_op(trans, root);
	if (needs_lock)
		mutex_unlock(&root->fs_info->alloc_mutex);

	return 0;
}

static int noinline del_extent_zero(struct btrfs_trans_handle *trans,
				    struct btrfs_root *extent_root,
				    struct btrfs_path *path,
				    struct btrfs_key *extent_key)
{
	int ret;

	mutex_lock(&extent_root->fs_info->alloc_mutex);
	ret = btrfs_search_slot(trans, extent_root, extent_key, path, -1, 1);
	if (ret)
		goto out;
	ret = btrfs_del_item(trans, extent_root, path);
out:
	btrfs_release_path(extent_root, path);
	mutex_unlock(&extent_root->fs_info->alloc_mutex);
	return ret;
}

static struct btrfs_root noinline *read_ref_root(struct btrfs_fs_info *fs_info,
						struct btrfs_ref_path *ref_path)
{
	struct btrfs_key root_key;

	root_key.objectid = ref_path->root_objectid;
	root_key.type = BTRFS_ROOT_ITEM_KEY;
	if (is_cowonly_root(ref_path->root_objectid))
		root_key.offset = 0;
	else
		root_key.offset = (u64)-1;

	return btrfs_read_fs_root_no_name(fs_info, &root_key);
}

static int noinline relocate_one_extent(struct btrfs_root *extent_root,
					struct btrfs_path *path,
					struct btrfs_key *extent_key,
					struct btrfs_block_group_cache *group,
					struct inode *reloc_inode, int pass)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *found_root;
	struct btrfs_ref_path *ref_path = NULL;
	struct disk_extent *new_extents = NULL;
	int nr_extents = 0;
	int loops;
	int ret;
	int level;
	struct btrfs_key first_key;
	u64 prev_block = 0;

	mutex_unlock(&extent_root->fs_info->alloc_mutex);

	trans = btrfs_start_transaction(extent_root, 1);
	BUG_ON(!trans);

	if (extent_key->objectid == 0) {
		ret = del_extent_zero(trans, extent_root, path, extent_key);
		goto out;
	}

	ref_path = kmalloc(sizeof(*ref_path), GFP_NOFS);
	if (!ref_path) {
	       ret = -ENOMEM;
	       goto out;
	}

	for (loops = 0; ; loops++) {
		if (loops == 0) {
			ret = btrfs_first_ref_path(trans, extent_root, ref_path,
						   extent_key->objectid);
		} else {
			ret = btrfs_next_ref_path(trans, extent_root, ref_path);
		}
		if (ret < 0)
			goto out;
		if (ret > 0)
			break;

		if (ref_path->root_objectid == BTRFS_TREE_LOG_OBJECTID ||
		    ref_path->root_objectid == BTRFS_TREE_RELOC_OBJECTID)
			continue;

		found_root = read_ref_root(extent_root->fs_info, ref_path);
		BUG_ON(!found_root);
		/*
		 * for reference counted tree, only process reference paths
		 * rooted at the latest committed root.
		 */
		if (found_root->ref_cows &&
		    ref_path->root_generation != found_root->root_key.offset)
			continue;

		if (ref_path->owner_objectid >= BTRFS_FIRST_FREE_OBJECTID) {
			if (pass == 0) {
				/*
				 * copy data extents to new locations
				 */
				u64 group_start = group->key.objectid;
				ret = relocate_data_extent(reloc_inode,
							   extent_key,
							   group_start);
				if (ret < 0)
					goto out;
				break;
			}
			level = 0;
		} else {
			level = ref_path->owner_objectid;
		}

		if (prev_block != ref_path->nodes[level]) {
			struct extent_buffer *eb;
			u64 block_start = ref_path->nodes[level];
			u64 block_size = btrfs_level_size(found_root, level);

			eb = read_tree_block(found_root, block_start,
					     block_size, 0);
			btrfs_tree_lock(eb);
			BUG_ON(level != btrfs_header_level(eb));

			if (level == 0)
				btrfs_item_key_to_cpu(eb, &first_key, 0);
			else
				btrfs_node_key_to_cpu(eb, &first_key, 0);

			btrfs_tree_unlock(eb);
			free_extent_buffer(eb);
			prev_block = block_start;
		}

		if (ref_path->owner_objectid >= BTRFS_FIRST_FREE_OBJECTID &&
		    pass >= 2) {
			/*
			 * use fallback method to process the remaining
			 * references.
			 */
			if (!new_extents) {
				u64 group_start = group->key.objectid;
				ret = get_new_locations(reloc_inode,
							extent_key,
							group_start, 0,
							&new_extents,
							&nr_extents);
				if (ret < 0)
					goto out;
			}
			btrfs_record_root_in_trans(found_root);
			ret = replace_one_extent(trans, found_root,
						path, extent_key,
						&first_key, ref_path,
						new_extents, nr_extents);
			if (ret < 0)
				goto out;
			continue;
		}

		btrfs_record_root_in_trans(found_root);
		if (ref_path->owner_objectid < BTRFS_FIRST_FREE_OBJECTID) {
			ret = relocate_tree_block(trans, found_root, path,
						  &first_key, ref_path);
		} else {
			/*
			 * try to update data extent references while
			 * keeping metadata shared between snapshots.
			 */
			ret = relocate_one_path(trans, found_root, path,
						&first_key, ref_path,
						group, reloc_inode);
		}
		if (ret < 0)
			goto out;
	}
	ret = 0;
out:
	btrfs_end_transaction(trans, extent_root);
	kfree(new_extents);
	kfree(ref_path);
	mutex_lock(&extent_root->fs_info->alloc_mutex);
	return ret;
}

static u64 update_block_group_flags(struct btrfs_root *root, u64 flags)
{
	u64 num_devices;
	u64 stripped = BTRFS_BLOCK_GROUP_RAID0 |
		BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_RAID10;

	num_devices = root->fs_info->fs_devices->num_devices;
	if (num_devices == 1) {
		stripped |= BTRFS_BLOCK_GROUP_DUP;
		stripped = flags & ~stripped;

		/* turn raid0 into single device chunks */
		if (flags & BTRFS_BLOCK_GROUP_RAID0)
			return stripped;

		/* turn mirroring into duplication */
		if (flags & (BTRFS_BLOCK_GROUP_RAID1 |
			     BTRFS_BLOCK_GROUP_RAID10))
			return stripped | BTRFS_BLOCK_GROUP_DUP;
		return flags;
	} else {
		/* they already had raid on here, just return */
		if (flags & stripped)
			return flags;

		stripped |= BTRFS_BLOCK_GROUP_DUP;
		stripped = flags & ~stripped;

		/* switch duplicated blocks with raid1 */
		if (flags & BTRFS_BLOCK_GROUP_DUP)
			return stripped | BTRFS_BLOCK_GROUP_RAID1;

		/* turn single device chunks into raid0 */
		return stripped | BTRFS_BLOCK_GROUP_RAID0;
	}
	return flags;
}

int __alloc_chunk_for_shrink(struct btrfs_root *root,
		     struct btrfs_block_group_cache *shrink_block_group,
		     int force)
{
	struct btrfs_trans_handle *trans;
	u64 new_alloc_flags;
	u64 calc;

	spin_lock(&shrink_block_group->lock);
	if (btrfs_block_group_used(&shrink_block_group->item) > 0) {
		spin_unlock(&shrink_block_group->lock);
		mutex_unlock(&root->fs_info->alloc_mutex);

		trans = btrfs_start_transaction(root, 1);
		mutex_lock(&root->fs_info->alloc_mutex);
		spin_lock(&shrink_block_group->lock);

		new_alloc_flags = update_block_group_flags(root,
						   shrink_block_group->flags);
		if (new_alloc_flags != shrink_block_group->flags) {
			calc =
			     btrfs_block_group_used(&shrink_block_group->item);
		} else {
			calc = shrink_block_group->key.offset;
		}
		spin_unlock(&shrink_block_group->lock);

		do_chunk_alloc(trans, root->fs_info->extent_root,
			       calc + 2 * 1024 * 1024, new_alloc_flags, force);

		mutex_unlock(&root->fs_info->alloc_mutex);
		btrfs_end_transaction(trans, root);
		mutex_lock(&root->fs_info->alloc_mutex);
	} else
		spin_unlock(&shrink_block_group->lock);
	return 0;
}

static int __insert_orphan_inode(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 u64 objectid, u64 size)
{
	struct btrfs_path *path;
	struct btrfs_inode_item *item;
	struct extent_buffer *leaf;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_insert_empty_inode(trans, root, path, objectid);
	if (ret)
		goto out;

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_inode_item);
	memset_extent_buffer(leaf, 0, (unsigned long)item, sizeof(*item));
	btrfs_set_inode_generation(leaf, item, 1);
	btrfs_set_inode_size(leaf, item, size);
	btrfs_set_inode_mode(leaf, item, S_IFREG | 0600);
	btrfs_set_inode_flags(leaf, item, BTRFS_INODE_NODATASUM);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(root, path);
out:
	btrfs_free_path(path);
	return ret;
}

static struct inode noinline *create_reloc_inode(struct btrfs_fs_info *fs_info,
					struct btrfs_block_group_cache *group)
{
	struct inode *inode = NULL;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root;
	struct btrfs_key root_key;
	u64 objectid = BTRFS_FIRST_FREE_OBJECTID;
	int err = 0;

	root_key.objectid = BTRFS_DATA_RELOC_TREE_OBJECTID;
	root_key.type = BTRFS_ROOT_ITEM_KEY;
	root_key.offset = (u64)-1;
	root = btrfs_read_fs_root_no_name(fs_info, &root_key);
	if (IS_ERR(root))
		return ERR_CAST(root);

	trans = btrfs_start_transaction(root, 1);
	BUG_ON(!trans);

	err = btrfs_find_free_objectid(trans, root, objectid, &objectid);
	if (err)
		goto out;

	err = __insert_orphan_inode(trans, root, objectid, group->key.offset);
	BUG_ON(err);

	err = btrfs_insert_file_extent(trans, root, objectid, 0, 0, 0,
				       group->key.offset, 0);
	BUG_ON(err);

	inode = btrfs_iget_locked(root->fs_info->sb, objectid, root);
	if (inode->i_state & I_NEW) {
		BTRFS_I(inode)->root = root;
		BTRFS_I(inode)->location.objectid = objectid;
		BTRFS_I(inode)->location.type = BTRFS_INODE_ITEM_KEY;
		BTRFS_I(inode)->location.offset = 0;
		btrfs_read_locked_inode(inode);
		unlock_new_inode(inode);
		BUG_ON(is_bad_inode(inode));
	} else {
		BUG_ON(1);
	}

	err = btrfs_orphan_add(trans, inode);
out:
	btrfs_end_transaction(trans, root);
	if (err) {
		if (inode)
			iput(inode);
		inode = ERR_PTR(err);
	}
	return inode;
}

int btrfs_relocate_block_group(struct btrfs_root *root, u64 group_start)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path;
	struct btrfs_fs_info *info = root->fs_info;
	struct extent_buffer *leaf;
	struct inode *reloc_inode;
	struct btrfs_block_group_cache *block_group;
	struct btrfs_key key;
	u64 cur_byte;
	u64 total_found;
	u32 nritems;
	int ret;
	int progress;
	int pass = 0;

	root = root->fs_info->extent_root;

	block_group = btrfs_lookup_block_group(info, group_start);
	BUG_ON(!block_group);

	printk("btrfs relocating block group %llu flags %llu\n",
	       (unsigned long long)block_group->key.objectid,
	       (unsigned long long)block_group->flags);

	path = btrfs_alloc_path();
	BUG_ON(!path);

	reloc_inode = create_reloc_inode(info, block_group);
	BUG_ON(IS_ERR(reloc_inode));

	mutex_lock(&root->fs_info->alloc_mutex);

	__alloc_chunk_for_shrink(root, block_group, 1);
	block_group->ro = 1;
	block_group->space_info->total_bytes -= block_group->key.offset;

	mutex_unlock(&root->fs_info->alloc_mutex);

	btrfs_start_delalloc_inodes(info->tree_root);
	btrfs_wait_ordered_extents(info->tree_root, 0);
again:
	total_found = 0;
	progress = 0;
	key.objectid = block_group->key.objectid;
	key.offset = 0;
	key.type = 0;
	cur_byte = key.objectid;

	trans = btrfs_start_transaction(info->tree_root, 1);
	btrfs_commit_transaction(trans, info->tree_root);

	mutex_lock(&root->fs_info->cleaner_mutex);
	btrfs_clean_old_snapshots(info->tree_root);
	btrfs_remove_leaf_refs(info->tree_root, (u64)-1, 1);
	mutex_unlock(&root->fs_info->cleaner_mutex);

	mutex_lock(&root->fs_info->alloc_mutex);

	while(1) {
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;
next:
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			if (ret == 1) {
				ret = 0;
				break;
			}
			leaf = path->nodes[0];
			nritems = btrfs_header_nritems(leaf);
		}

		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

		if (key.objectid >= block_group->key.objectid +
		    block_group->key.offset)
			break;

		if (progress && need_resched()) {
			btrfs_release_path(root, path);
			mutex_unlock(&root->fs_info->alloc_mutex);
			cond_resched();
			mutex_lock(&root->fs_info->alloc_mutex);
			progress = 0;
			continue;
		}
		progress = 1;

		if (btrfs_key_type(&key) != BTRFS_EXTENT_ITEM_KEY ||
		    key.objectid + key.offset <= cur_byte) {
			path->slots[0]++;
			goto next;
		}

		total_found++;
		cur_byte = key.objectid + key.offset;
		btrfs_release_path(root, path);

		__alloc_chunk_for_shrink(root, block_group, 0);
		ret = relocate_one_extent(root, path, &key, block_group,
					  reloc_inode, pass);
		BUG_ON(ret < 0);

		key.objectid = cur_byte;
		key.type = 0;
		key.offset = 0;
	}

	btrfs_release_path(root, path);
	mutex_unlock(&root->fs_info->alloc_mutex);

	if (pass == 0) {
		btrfs_wait_ordered_range(reloc_inode, 0, (u64)-1);
		invalidate_mapping_pages(reloc_inode->i_mapping, 0, -1);
		WARN_ON(reloc_inode->i_mapping->nrpages);
	}

	if (total_found > 0) {
		printk("btrfs found %llu extents in pass %d\n",
		       (unsigned long long)total_found, pass);
		pass++;
		goto again;
	}

	/* delete reloc_inode */
	iput(reloc_inode);

	/* unpin extents in this range */
	trans = btrfs_start_transaction(info->tree_root, 1);
	btrfs_commit_transaction(trans, info->tree_root);

	mutex_lock(&root->fs_info->alloc_mutex);

	spin_lock(&block_group->lock);
	WARN_ON(block_group->pinned > 0);
	WARN_ON(block_group->reserved > 0);
	WARN_ON(btrfs_block_group_used(&block_group->item) > 0);
	spin_unlock(&block_group->lock);
	ret = 0;
out:
	mutex_unlock(&root->fs_info->alloc_mutex);
	btrfs_free_path(path);
	return ret;
}

int find_first_block_group(struct btrfs_root *root, struct btrfs_path *path,
			   struct btrfs_key *key)
{
	int ret = 0;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	int slot;

	ret = btrfs_search_slot(NULL, root, key, path, 0, 0);
	if (ret < 0)
		goto out;

	while(1) {
		slot = path->slots[0];
		leaf = path->nodes[0];
		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto out;
			break;
		}
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.objectid >= key->objectid &&
		    found_key.type == BTRFS_BLOCK_GROUP_ITEM_KEY) {
			ret = 0;
			goto out;
		}
		path->slots[0]++;
	}
	ret = -ENOENT;
out:
	return ret;
}

int btrfs_free_block_groups(struct btrfs_fs_info *info)
{
	struct btrfs_block_group_cache *block_group;
	struct rb_node *n;

	mutex_lock(&info->alloc_mutex);
	spin_lock(&info->block_group_cache_lock);
	while ((n = rb_last(&info->block_group_cache_tree)) != NULL) {
		block_group = rb_entry(n, struct btrfs_block_group_cache,
				       cache_node);

		spin_unlock(&info->block_group_cache_lock);
		btrfs_remove_free_space_cache(block_group);
		spin_lock(&info->block_group_cache_lock);

		rb_erase(&block_group->cache_node,
			 &info->block_group_cache_tree);
		spin_lock(&block_group->space_info->lock);
		list_del(&block_group->list);
		spin_unlock(&block_group->space_info->lock);
		kfree(block_group);
	}
	spin_unlock(&info->block_group_cache_lock);
	mutex_unlock(&info->alloc_mutex);
	return 0;
}

int btrfs_read_block_groups(struct btrfs_root *root)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_space_info *space_info;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;

	root = info->extent_root;
	key.objectid = 0;
	key.offset = 0;
	btrfs_set_key_type(&key, BTRFS_BLOCK_GROUP_ITEM_KEY);
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	mutex_lock(&root->fs_info->alloc_mutex);
	while(1) {
		ret = find_first_block_group(root, path, &key);
		if (ret > 0) {
			ret = 0;
			goto error;
		}
		if (ret != 0)
			goto error;

		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		cache = kzalloc(sizeof(*cache), GFP_NOFS);
		if (!cache) {
			ret = -ENOMEM;
			break;
		}

		spin_lock_init(&cache->lock);
		INIT_LIST_HEAD(&cache->list);
		read_extent_buffer(leaf, &cache->item,
				   btrfs_item_ptr_offset(leaf, path->slots[0]),
				   sizeof(cache->item));
		memcpy(&cache->key, &found_key, sizeof(found_key));

		key.objectid = found_key.objectid + found_key.offset;
		btrfs_release_path(root, path);
		cache->flags = btrfs_block_group_flags(&cache->item);

		ret = update_space_info(info, cache->flags, found_key.offset,
					btrfs_block_group_used(&cache->item),
					&space_info);
		BUG_ON(ret);
		cache->space_info = space_info;
		spin_lock(&space_info->lock);
		list_add(&cache->list, &space_info->block_groups);
		spin_unlock(&space_info->lock);

		ret = btrfs_add_block_group_cache(root->fs_info, cache);
		BUG_ON(ret);
	}
	ret = 0;
error:
	btrfs_free_path(path);
	mutex_unlock(&root->fs_info->alloc_mutex);
	return ret;
}

int btrfs_make_block_group(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, u64 bytes_used,
			   u64 type, u64 chunk_objectid, u64 chunk_offset,
			   u64 size)
{
	int ret;
	struct btrfs_root *extent_root;
	struct btrfs_block_group_cache *cache;

	WARN_ON(!mutex_is_locked(&root->fs_info->alloc_mutex));
	extent_root = root->fs_info->extent_root;

	root->fs_info->last_trans_new_blockgroup = trans->transid;

	cache = kzalloc(sizeof(*cache), GFP_NOFS);
	if (!cache)
		return -ENOMEM;

	cache->key.objectid = chunk_offset;
	cache->key.offset = size;
	spin_lock_init(&cache->lock);
	INIT_LIST_HEAD(&cache->list);
	btrfs_set_key_type(&cache->key, BTRFS_BLOCK_GROUP_ITEM_KEY);

	btrfs_set_block_group_used(&cache->item, bytes_used);
	btrfs_set_block_group_chunk_objectid(&cache->item, chunk_objectid);
	cache->flags = type;
	btrfs_set_block_group_flags(&cache->item, type);

	ret = update_space_info(root->fs_info, cache->flags, size, bytes_used,
				&cache->space_info);
	BUG_ON(ret);
	spin_lock(&cache->space_info->lock);
	list_add(&cache->list, &cache->space_info->block_groups);
	spin_unlock(&cache->space_info->lock);

	ret = btrfs_add_block_group_cache(root->fs_info, cache);
	BUG_ON(ret);

	ret = btrfs_insert_item(trans, extent_root, &cache->key, &cache->item,
				sizeof(cache->item));
	BUG_ON(ret);

	finish_current_insert(trans, extent_root);
	ret = del_pending_extents(trans, extent_root);
	BUG_ON(ret);
	set_avail_alloc_bits(extent_root->fs_info, type);

	return 0;
}

int btrfs_remove_block_group(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, u64 group_start)
{
	struct btrfs_path *path;
	struct btrfs_block_group_cache *block_group;
	struct btrfs_key key;
	int ret;

	BUG_ON(!mutex_is_locked(&root->fs_info->alloc_mutex));
	root = root->fs_info->extent_root;

	block_group = btrfs_lookup_block_group(root->fs_info, group_start);
	BUG_ON(!block_group);

	memcpy(&key, &block_group->key, sizeof(key));

	path = btrfs_alloc_path();
	BUG_ON(!path);

	btrfs_remove_free_space_cache(block_group);
	rb_erase(&block_group->cache_node,
		 &root->fs_info->block_group_cache_tree);
	spin_lock(&block_group->space_info->lock);
	list_del(&block_group->list);
	spin_unlock(&block_group->space_info->lock);

	/*
	memset(shrink_block_group, 0, sizeof(*shrink_block_group));
	kfree(shrink_block_group);
	*/

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret > 0)
		ret = -EIO;
	if (ret < 0)
		goto out;

	ret = btrfs_del_item(trans, root, path);
out:
	btrfs_free_path(path);
	return ret;
}
