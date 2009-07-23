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
#include <linux/sort.h>
#include <linux/rcupdate.h>
#include "compat.h"
#include "hash.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"
#include "transaction.h"
#include "volumes.h"
#include "locking.h"
#include "free-space-cache.h"

static int update_reserved_extents(struct btrfs_root *root,
				   u64 bytenr, u64 num, int reserve);
static int update_block_group(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      u64 bytenr, u64 num_bytes, int alloc,
			      int mark_free);
static int __btrfs_free_extent(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				u64 bytenr, u64 num_bytes, u64 parent,
				u64 root_objectid, u64 owner_objectid,
				u64 owner_offset, int refs_to_drop,
				struct btrfs_delayed_extent_op *extra_op);
static void __run_delayed_extent_op(struct btrfs_delayed_extent_op *extent_op,
				    struct extent_buffer *leaf,
				    struct btrfs_extent_item *ei);
static int alloc_reserved_file_extent(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      u64 parent, u64 root_objectid,
				      u64 flags, u64 owner, u64 offset,
				      struct btrfs_key *ins, int ref_mod);
static int alloc_reserved_tree_block(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     u64 parent, u64 root_objectid,
				     u64 flags, struct btrfs_disk_key *key,
				     int level, struct btrfs_key *ins);

static int do_chunk_alloc(struct btrfs_trans_handle *trans,
			  struct btrfs_root *extent_root, u64 alloc_bytes,
			  u64 flags, int force);

static int block_group_bits(struct btrfs_block_group_cache *cache, u64 bits)
{
	return (cache->flags & bits) == bits;
}

/*
 * this adds the block group to the fs_info rb tree for the block group
 * cache
 */
static int btrfs_add_block_group_cache(struct btrfs_fs_info *info,
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
	if (ret)
		atomic_inc(&ret->count);
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
			ret = btrfs_add_free_space(block_group, start,
						   size);
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

static int remove_sb_from_cache(struct btrfs_root *root,
				struct btrfs_block_group_cache *cache)
{
	u64 bytenr;
	u64 *logical;
	int stripe_len;
	int i, nr, ret;

	for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
		bytenr = btrfs_sb_offset(i);
		ret = btrfs_rmap_block(&root->fs_info->mapping_tree,
				       cache->key.objectid, bytenr, 0,
				       &logical, &nr, &stripe_len);
		BUG_ON(ret);
		while (nr--) {
			btrfs_remove_free_space(cache, logical[nr],
						stripe_len);
		}
		kfree(logical);
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
	u64 last;

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
	last = max_t(u64, block_group->key.objectid, BTRFS_SUPER_INFO_OFFSET);
	key.objectid = last;
	key.offset = 0;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;

	while (1) {
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
			add_new_free_space(block_group, root->fs_info, last,
					   key.objectid);

			last = key.objectid + key.offset;
		}
next:
		path->slots[0]++;
	}

	add_new_free_space(block_group, root->fs_info, last,
			   block_group->key.objectid +
			   block_group->key.offset);

	block_group->cached = 1;
	remove_sb_from_cache(root, block_group);
	ret = 0;
err:
	btrfs_free_path(path);
	return ret;
}

/*
 * return the block group that starts at or after bytenr
 */
static struct btrfs_block_group_cache *
btrfs_lookup_first_block_group(struct btrfs_fs_info *info, u64 bytenr)
{
	struct btrfs_block_group_cache *cache;

	cache = block_group_cache_tree_search(info, bytenr, 0);

	return cache;
}

/*
 * return the block group that contains the given bytenr
 */
struct btrfs_block_group_cache *btrfs_lookup_block_group(
						 struct btrfs_fs_info *info,
						 u64 bytenr)
{
	struct btrfs_block_group_cache *cache;

	cache = block_group_cache_tree_search(info, bytenr, 1);

	return cache;
}

void btrfs_put_block_group(struct btrfs_block_group_cache *cache)
{
	if (atomic_dec_and_test(&cache->count))
		kfree(cache);
}

static struct btrfs_space_info *__find_space_info(struct btrfs_fs_info *info,
						  u64 flags)
{
	struct list_head *head = &info->space_info;
	struct btrfs_space_info *found;

	rcu_read_lock();
	list_for_each_entry_rcu(found, head, list) {
		if (found->flags == flags) {
			rcu_read_unlock();
			return found;
		}
	}
	rcu_read_unlock();
	return NULL;
}

/*
 * after adding space to the filesystem, we need to clear the full flags
 * on all the space infos.
 */
void btrfs_clear_space_info_full(struct btrfs_fs_info *info)
{
	struct list_head *head = &info->space_info;
	struct btrfs_space_info *found;

	rcu_read_lock();
	list_for_each_entry_rcu(found, head, list)
		found->full = 0;
	rcu_read_unlock();
}

static u64 div_factor(u64 num, int factor)
{
	if (factor == 10)
		return num;
	num *= factor;
	do_div(num, 10);
	return num;
}

u64 btrfs_find_block_group(struct btrfs_root *root,
			   u64 search_start, u64 search_hint, int owner)
{
	struct btrfs_block_group_cache *cache;
	u64 used;
	u64 last = max(search_hint, search_start);
	u64 group_start = 0;
	int full_search = 0;
	int factor = 9;
	int wrapped = 0;
again:
	while (1) {
		cache = btrfs_lookup_first_block_group(root->fs_info, last);
		if (!cache)
			break;

		spin_lock(&cache->lock);
		last = cache->key.objectid + cache->key.offset;
		used = btrfs_block_group_used(&cache->item);

		if ((full_search || !cache->ro) &&
		    block_group_bits(cache, BTRFS_BLOCK_GROUP_METADATA)) {
			if (used + cache->pinned + cache->reserved <
			    div_factor(cache->key.offset, factor)) {
				group_start = cache->key.objectid;
				spin_unlock(&cache->lock);
				btrfs_put_block_group(cache);
				goto found;
			}
		}
		spin_unlock(&cache->lock);
		btrfs_put_block_group(cache);
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
	return group_start;
}

/* simple helper to search for an existing extent at a given offset */
int btrfs_lookup_extent(struct btrfs_root *root, u64 start, u64 len)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_path *path;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	key.objectid = start;
	key.offset = len;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	ret = btrfs_search_slot(NULL, root->fs_info->extent_root, &key, path,
				0, 0);
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
 * There are two kinds of back refs. The implicit back refs is optimized
 * for pointers in non-shared tree blocks. For a given pointer in a block,
 * back refs of this kind provide information about the block's owner tree
 * and the pointer's key. These information allow us to find the block by
 * b-tree searching. The full back refs is for pointers in tree blocks not
 * referenced by their owner trees. The location of tree block is recorded
 * in the back refs. Actually the full back refs is generic, and can be
 * used in all cases the implicit back refs is used. The major shortcoming
 * of the full back refs is its overhead. Every time a tree block gets
 * COWed, we have to update back refs entry for all pointers in it.
 *
 * For a newly allocated tree block, we use implicit back refs for
 * pointers in it. This means most tree related operations only involve
 * implicit back refs. For a tree block created in old transaction, the
 * only way to drop a reference to it is COW it. So we can detect the
 * event that tree block loses its owner tree's reference and do the
 * back refs conversion.
 *
 * When a tree block is COW'd through a tree, there are four cases:
 *
 * The reference count of the block is one and the tree is the block's
 * owner tree. Nothing to do in this case.
 *
 * The reference count of the block is one and the tree is not the
 * block's owner tree. In this case, full back refs is used for pointers
 * in the block. Remove these full back refs, add implicit back refs for
 * every pointers in the new block.
 *
 * The reference count of the block is greater than one and the tree is
 * the block's owner tree. In this case, implicit back refs is used for
 * pointers in the block. Add full back refs for every pointers in the
 * block, increase lower level extents' reference counts. The original
 * implicit back refs are entailed to the new block.
 *
 * The reference count of the block is greater than one and the tree is
 * not the block's owner tree. Add implicit back refs for every pointer in
 * the new block, increase lower level extents' reference count.
 *
 * Back Reference Key composing:
 *
 * The key objectid corresponds to the first byte in the extent,
 * The key type is used to differentiate between types of back refs.
 * There are different meanings of the key offset for different types
 * of back refs.
 *
 * File extents can be referenced by:
 *
 * - multiple snapshots, subvolumes, or different generations in one subvol
 * - different files inside a single subvolume
 * - different offsets inside a file (bookend extents in file.c)
 *
 * The extent ref structure for the implicit back refs has fields for:
 *
 * - Objectid of the subvolume root
 * - objectid of the file holding the reference
 * - original offset in the file
 * - how many bookend extents
 *
 * The key offset for the implicit back refs is hash of the first
 * three fields.
 *
 * The extent ref structure for the full back refs has field for:
 *
 * - number of pointers in the tree leaf
 *
 * The key offset for the implicit back refs is the first byte of
 * the tree leaf
 *
 * When a file extent is allocated, The implicit back refs is used.
 * the fields are filled in:
 *
 *     (root_key.objectid, inode objectid, offset in file, 1)
 *
 * When a file extent is removed file truncation, we find the
 * corresponding implicit back refs and check the following fields:
 *
 *     (btrfs_header_owner(leaf), inode objectid, offset in file)
 *
 * Btree extents can be referenced by:
 *
 * - Different subvolumes
 *
 * Both the implicit back refs and the full back refs for tree blocks
 * only consist of key. The key offset for the implicit back refs is
 * objectid of block's owner tree. The key offset for the full back refs
 * is the first byte of parent block.
 *
 * When implicit back refs is used, information about the lowest key and
 * level of the tree block are required. These information are stored in
 * tree block info structure.
 */

#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
static int convert_extent_item_v0(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  struct btrfs_path *path,
				  u64 owner, u32 extra_size)
{
	struct btrfs_extent_item *item;
	struct btrfs_extent_item_v0 *ei0;
	struct btrfs_extent_ref_v0 *ref0;
	struct btrfs_tree_block_info *bi;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct btrfs_key found_key;
	u32 new_size = sizeof(*item);
	u64 refs;
	int ret;

	leaf = path->nodes[0];
	BUG_ON(btrfs_item_size_nr(leaf, path->slots[0]) != sizeof(*ei0));

	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	ei0 = btrfs_item_ptr(leaf, path->slots[0],
			     struct btrfs_extent_item_v0);
	refs = btrfs_extent_refs_v0(leaf, ei0);

	if (owner == (u64)-1) {
		while (1) {
			if (path->slots[0] >= btrfs_header_nritems(leaf)) {
				ret = btrfs_next_leaf(root, path);
				if (ret < 0)
					return ret;
				BUG_ON(ret > 0);
				leaf = path->nodes[0];
			}
			btrfs_item_key_to_cpu(leaf, &found_key,
					      path->slots[0]);
			BUG_ON(key.objectid != found_key.objectid);
			if (found_key.type != BTRFS_EXTENT_REF_V0_KEY) {
				path->slots[0]++;
				continue;
			}
			ref0 = btrfs_item_ptr(leaf, path->slots[0],
					      struct btrfs_extent_ref_v0);
			owner = btrfs_ref_objectid_v0(leaf, ref0);
			break;
		}
	}
	btrfs_release_path(root, path);

	if (owner < BTRFS_FIRST_FREE_OBJECTID)
		new_size += sizeof(*bi);

	new_size -= sizeof(*ei0);
	ret = btrfs_search_slot(trans, root, &key, path,
				new_size + extra_size, 1);
	if (ret < 0)
		return ret;
	BUG_ON(ret);

	ret = btrfs_extend_item(trans, root, path, new_size);
	BUG_ON(ret);

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	btrfs_set_extent_refs(leaf, item, refs);
	/* FIXME: get real generation */
	btrfs_set_extent_generation(leaf, item, 0);
	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		btrfs_set_extent_flags(leaf, item,
				       BTRFS_EXTENT_FLAG_TREE_BLOCK |
				       BTRFS_BLOCK_FLAG_FULL_BACKREF);
		bi = (struct btrfs_tree_block_info *)(item + 1);
		/* FIXME: get first key of the block */
		memset_extent_buffer(leaf, 0, (unsigned long)bi, sizeof(*bi));
		btrfs_set_tree_block_level(leaf, bi, (int)owner);
	} else {
		btrfs_set_extent_flags(leaf, item, BTRFS_EXTENT_FLAG_DATA);
	}
	btrfs_mark_buffer_dirty(leaf);
	return 0;
}
#endif

static u64 hash_extent_data_ref(u64 root_objectid, u64 owner, u64 offset)
{
	u32 high_crc = ~(u32)0;
	u32 low_crc = ~(u32)0;
	__le64 lenum;

	lenum = cpu_to_le64(root_objectid);
	high_crc = crc32c(high_crc, &lenum, sizeof(lenum));
	lenum = cpu_to_le64(owner);
	low_crc = crc32c(low_crc, &lenum, sizeof(lenum));
	lenum = cpu_to_le64(offset);
	low_crc = crc32c(low_crc, &lenum, sizeof(lenum));

	return ((u64)high_crc << 31) ^ (u64)low_crc;
}

static u64 hash_extent_data_ref_item(struct extent_buffer *leaf,
				     struct btrfs_extent_data_ref *ref)
{
	return hash_extent_data_ref(btrfs_extent_data_ref_root(leaf, ref),
				    btrfs_extent_data_ref_objectid(leaf, ref),
				    btrfs_extent_data_ref_offset(leaf, ref));
}

static int match_extent_data_ref(struct extent_buffer *leaf,
				 struct btrfs_extent_data_ref *ref,
				 u64 root_objectid, u64 owner, u64 offset)
{
	if (btrfs_extent_data_ref_root(leaf, ref) != root_objectid ||
	    btrfs_extent_data_ref_objectid(leaf, ref) != owner ||
	    btrfs_extent_data_ref_offset(leaf, ref) != offset)
		return 0;
	return 1;
}

static noinline int lookup_extent_data_ref(struct btrfs_trans_handle *trans,
					   struct btrfs_root *root,
					   struct btrfs_path *path,
					   u64 bytenr, u64 parent,
					   u64 root_objectid,
					   u64 owner, u64 offset)
{
	struct btrfs_key key;
	struct btrfs_extent_data_ref *ref;
	struct extent_buffer *leaf;
	u32 nritems;
	int ret;
	int recow;
	int err = -ENOENT;

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_DATA_REF_KEY;
		key.offset = parent;
	} else {
		key.type = BTRFS_EXTENT_DATA_REF_KEY;
		key.offset = hash_extent_data_ref(root_objectid,
						  owner, offset);
	}
again:
	recow = 0;
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0) {
		err = ret;
		goto fail;
	}

	if (parent) {
		if (!ret)
			return 0;
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
		key.type = BTRFS_EXTENT_REF_V0_KEY;
		btrfs_release_path(root, path);
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0) {
			err = ret;
			goto fail;
		}
		if (!ret)
			return 0;
#endif
		goto fail;
	}

	leaf = path->nodes[0];
	nritems = btrfs_header_nritems(leaf);
	while (1) {
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				err = ret;
			if (ret)
				goto fail;

			leaf = path->nodes[0];
			nritems = btrfs_header_nritems(leaf);
			recow = 1;
		}

		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		if (key.objectid != bytenr ||
		    key.type != BTRFS_EXTENT_DATA_REF_KEY)
			goto fail;

		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_data_ref);

		if (match_extent_data_ref(leaf, ref, root_objectid,
					  owner, offset)) {
			if (recow) {
				btrfs_release_path(root, path);
				goto again;
			}
			err = 0;
			break;
		}
		path->slots[0]++;
	}
fail:
	return err;
}

static noinline int insert_extent_data_ref(struct btrfs_trans_handle *trans,
					   struct btrfs_root *root,
					   struct btrfs_path *path,
					   u64 bytenr, u64 parent,
					   u64 root_objectid, u64 owner,
					   u64 offset, int refs_to_add)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	u32 size;
	u32 num_refs;
	int ret;

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_DATA_REF_KEY;
		key.offset = parent;
		size = sizeof(struct btrfs_shared_data_ref);
	} else {
		key.type = BTRFS_EXTENT_DATA_REF_KEY;
		key.offset = hash_extent_data_ref(root_objectid,
						  owner, offset);
		size = sizeof(struct btrfs_extent_data_ref);
	}

	ret = btrfs_insert_empty_item(trans, root, path, &key, size);
	if (ret && ret != -EEXIST)
		goto fail;

	leaf = path->nodes[0];
	if (parent) {
		struct btrfs_shared_data_ref *ref;
		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_shared_data_ref);
		if (ret == 0) {
			btrfs_set_shared_data_ref_count(leaf, ref, refs_to_add);
		} else {
			num_refs = btrfs_shared_data_ref_count(leaf, ref);
			num_refs += refs_to_add;
			btrfs_set_shared_data_ref_count(leaf, ref, num_refs);
		}
	} else {
		struct btrfs_extent_data_ref *ref;
		while (ret == -EEXIST) {
			ref = btrfs_item_ptr(leaf, path->slots[0],
					     struct btrfs_extent_data_ref);
			if (match_extent_data_ref(leaf, ref, root_objectid,
						  owner, offset))
				break;
			btrfs_release_path(root, path);
			key.offset++;
			ret = btrfs_insert_empty_item(trans, root, path, &key,
						      size);
			if (ret && ret != -EEXIST)
				goto fail;

			leaf = path->nodes[0];
		}
		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_data_ref);
		if (ret == 0) {
			btrfs_set_extent_data_ref_root(leaf, ref,
						       root_objectid);
			btrfs_set_extent_data_ref_objectid(leaf, ref, owner);
			btrfs_set_extent_data_ref_offset(leaf, ref, offset);
			btrfs_set_extent_data_ref_count(leaf, ref, refs_to_add);
		} else {
			num_refs = btrfs_extent_data_ref_count(leaf, ref);
			num_refs += refs_to_add;
			btrfs_set_extent_data_ref_count(leaf, ref, num_refs);
		}
	}
	btrfs_mark_buffer_dirty(leaf);
	ret = 0;
fail:
	btrfs_release_path(root, path);
	return ret;
}

static noinline int remove_extent_data_ref(struct btrfs_trans_handle *trans,
					   struct btrfs_root *root,
					   struct btrfs_path *path,
					   int refs_to_drop)
{
	struct btrfs_key key;
	struct btrfs_extent_data_ref *ref1 = NULL;
	struct btrfs_shared_data_ref *ref2 = NULL;
	struct extent_buffer *leaf;
	u32 num_refs = 0;
	int ret = 0;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

	if (key.type == BTRFS_EXTENT_DATA_REF_KEY) {
		ref1 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_extent_data_ref);
		num_refs = btrfs_extent_data_ref_count(leaf, ref1);
	} else if (key.type == BTRFS_SHARED_DATA_REF_KEY) {
		ref2 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_shared_data_ref);
		num_refs = btrfs_shared_data_ref_count(leaf, ref2);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	} else if (key.type == BTRFS_EXTENT_REF_V0_KEY) {
		struct btrfs_extent_ref_v0 *ref0;
		ref0 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_extent_ref_v0);
		num_refs = btrfs_ref_count_v0(leaf, ref0);
#endif
	} else {
		BUG();
	}

	BUG_ON(num_refs < refs_to_drop);
	num_refs -= refs_to_drop;

	if (num_refs == 0) {
		ret = btrfs_del_item(trans, root, path);
	} else {
		if (key.type == BTRFS_EXTENT_DATA_REF_KEY)
			btrfs_set_extent_data_ref_count(leaf, ref1, num_refs);
		else if (key.type == BTRFS_SHARED_DATA_REF_KEY)
			btrfs_set_shared_data_ref_count(leaf, ref2, num_refs);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
		else {
			struct btrfs_extent_ref_v0 *ref0;
			ref0 = btrfs_item_ptr(leaf, path->slots[0],
					struct btrfs_extent_ref_v0);
			btrfs_set_ref_count_v0(leaf, ref0, num_refs);
		}
#endif
		btrfs_mark_buffer_dirty(leaf);
	}
	return ret;
}

static noinline u32 extent_data_ref_count(struct btrfs_root *root,
					  struct btrfs_path *path,
					  struct btrfs_extent_inline_ref *iref)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_extent_data_ref *ref1;
	struct btrfs_shared_data_ref *ref2;
	u32 num_refs = 0;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	if (iref) {
		if (btrfs_extent_inline_ref_type(leaf, iref) ==
		    BTRFS_EXTENT_DATA_REF_KEY) {
			ref1 = (struct btrfs_extent_data_ref *)(&iref->offset);
			num_refs = btrfs_extent_data_ref_count(leaf, ref1);
		} else {
			ref2 = (struct btrfs_shared_data_ref *)(iref + 1);
			num_refs = btrfs_shared_data_ref_count(leaf, ref2);
		}
	} else if (key.type == BTRFS_EXTENT_DATA_REF_KEY) {
		ref1 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_extent_data_ref);
		num_refs = btrfs_extent_data_ref_count(leaf, ref1);
	} else if (key.type == BTRFS_SHARED_DATA_REF_KEY) {
		ref2 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_shared_data_ref);
		num_refs = btrfs_shared_data_ref_count(leaf, ref2);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	} else if (key.type == BTRFS_EXTENT_REF_V0_KEY) {
		struct btrfs_extent_ref_v0 *ref0;
		ref0 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_extent_ref_v0);
		num_refs = btrfs_ref_count_v0(leaf, ref0);
#endif
	} else {
		WARN_ON(1);
	}
	return num_refs;
}

static noinline int lookup_tree_block_ref(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path,
					  u64 bytenr, u64 parent,
					  u64 root_objectid)
{
	struct btrfs_key key;
	int ret;

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_BLOCK_REF_KEY;
		key.offset = parent;
	} else {
		key.type = BTRFS_TREE_BLOCK_REF_KEY;
		key.offset = root_objectid;
	}

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret > 0)
		ret = -ENOENT;
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (ret == -ENOENT && parent) {
		btrfs_release_path(root, path);
		key.type = BTRFS_EXTENT_REF_V0_KEY;
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret > 0)
			ret = -ENOENT;
	}
#endif
	return ret;
}

static noinline int insert_tree_block_ref(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path,
					  u64 bytenr, u64 parent,
					  u64 root_objectid)
{
	struct btrfs_key key;
	int ret;

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_BLOCK_REF_KEY;
		key.offset = parent;
	} else {
		key.type = BTRFS_TREE_BLOCK_REF_KEY;
		key.offset = root_objectid;
	}

	ret = btrfs_insert_empty_item(trans, root, path, &key, 0);
	btrfs_release_path(root, path);
	return ret;
}

static inline int extent_ref_type(u64 parent, u64 owner)
{
	int type;
	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		if (parent > 0)
			type = BTRFS_SHARED_BLOCK_REF_KEY;
		else
			type = BTRFS_TREE_BLOCK_REF_KEY;
	} else {
		if (parent > 0)
			type = BTRFS_SHARED_DATA_REF_KEY;
		else
			type = BTRFS_EXTENT_DATA_REF_KEY;
	}
	return type;
}

static int find_next_key(struct btrfs_path *path, int level,
			 struct btrfs_key *key)

{
	for (; level < BTRFS_MAX_LEVEL; level++) {
		if (!path->nodes[level])
			break;
		if (path->slots[level] + 1 >=
		    btrfs_header_nritems(path->nodes[level]))
			continue;
		if (level == 0)
			btrfs_item_key_to_cpu(path->nodes[level], key,
					      path->slots[level] + 1);
		else
			btrfs_node_key_to_cpu(path->nodes[level], key,
					      path->slots[level] + 1);
		return 0;
	}
	return 1;
}

/*
 * look for inline back ref. if back ref is found, *ref_ret is set
 * to the address of inline back ref, and 0 is returned.
 *
 * if back ref isn't found, *ref_ret is set to the address where it
 * should be inserted, and -ENOENT is returned.
 *
 * if insert is true and there are too many inline back refs, the path
 * points to the extent item, and -EAGAIN is returned.
 *
 * NOTE: inline back refs are ordered in the same way that back ref
 *	 items in the tree are ordered.
 */
static noinline_for_stack
int lookup_inline_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref **ref_ret,
				 u64 bytenr, u64 num_bytes,
				 u64 parent, u64 root_objectid,
				 u64 owner, u64 offset, int insert)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_extent_item *ei;
	struct btrfs_extent_inline_ref *iref;
	u64 flags;
	u64 item_size;
	unsigned long ptr;
	unsigned long end;
	int extra_size;
	int type;
	int want;
	int ret;
	int err = 0;

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;

	want = extent_ref_type(parent, owner);
	if (insert) {
		extra_size = btrfs_extent_inline_ref_size(want);
		path->keep_locks = 1;
	} else
		extra_size = -1;
	ret = btrfs_search_slot(trans, root, &key, path, extra_size, 1);
	if (ret < 0) {
		err = ret;
		goto out;
	}
	BUG_ON(ret);

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		if (!insert) {
			err = -ENOENT;
			goto out;
		}
		ret = convert_extent_item_v0(trans, root, path, owner,
					     extra_size);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		leaf = path->nodes[0];
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	}
#endif
	BUG_ON(item_size < sizeof(*ei));

	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	flags = btrfs_extent_flags(leaf, ei);

	ptr = (unsigned long)(ei + 1);
	end = (unsigned long)ei + item_size;

	if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		ptr += sizeof(struct btrfs_tree_block_info);
		BUG_ON(ptr > end);
	} else {
		BUG_ON(!(flags & BTRFS_EXTENT_FLAG_DATA));
	}

	err = -ENOENT;
	while (1) {
		if (ptr >= end) {
			WARN_ON(ptr > end);
			break;
		}
		iref = (struct btrfs_extent_inline_ref *)ptr;
		type = btrfs_extent_inline_ref_type(leaf, iref);
		if (want < type)
			break;
		if (want > type) {
			ptr += btrfs_extent_inline_ref_size(type);
			continue;
		}

		if (type == BTRFS_EXTENT_DATA_REF_KEY) {
			struct btrfs_extent_data_ref *dref;
			dref = (struct btrfs_extent_data_ref *)(&iref->offset);
			if (match_extent_data_ref(leaf, dref, root_objectid,
						  owner, offset)) {
				err = 0;
				break;
			}
			if (hash_extent_data_ref_item(leaf, dref) <
			    hash_extent_data_ref(root_objectid, owner, offset))
				break;
		} else {
			u64 ref_offset;
			ref_offset = btrfs_extent_inline_ref_offset(leaf, iref);
			if (parent > 0) {
				if (parent == ref_offset) {
					err = 0;
					break;
				}
				if (ref_offset < parent)
					break;
			} else {
				if (root_objectid == ref_offset) {
					err = 0;
					break;
				}
				if (ref_offset < root_objectid)
					break;
			}
		}
		ptr += btrfs_extent_inline_ref_size(type);
	}
	if (err == -ENOENT && insert) {
		if (item_size + extra_size >=
		    BTRFS_MAX_EXTENT_ITEM_SIZE(root)) {
			err = -EAGAIN;
			goto out;
		}
		/*
		 * To add new inline back ref, we have to make sure
		 * there is no corresponding back ref item.
		 * For simplicity, we just do not add new inline back
		 * ref if there is any kind of item for this block
		 */
		if (find_next_key(path, 0, &key) == 0 &&
		    key.objectid == bytenr &&
		    key.type < BTRFS_BLOCK_GROUP_ITEM_KEY) {
			err = -EAGAIN;
			goto out;
		}
	}
	*ref_ret = (struct btrfs_extent_inline_ref *)ptr;
out:
	if (insert) {
		path->keep_locks = 0;
		btrfs_unlock_up_safe(path, 1);
	}
	return err;
}

/*
 * helper to add new inline back ref
 */
static noinline_for_stack
int setup_inline_extent_backref(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_path *path,
				struct btrfs_extent_inline_ref *iref,
				u64 parent, u64 root_objectid,
				u64 owner, u64 offset, int refs_to_add,
				struct btrfs_delayed_extent_op *extent_op)
{
	struct extent_buffer *leaf;
	struct btrfs_extent_item *ei;
	unsigned long ptr;
	unsigned long end;
	unsigned long item_offset;
	u64 refs;
	int size;
	int type;
	int ret;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	item_offset = (unsigned long)iref - (unsigned long)ei;

	type = extent_ref_type(parent, owner);
	size = btrfs_extent_inline_ref_size(type);

	ret = btrfs_extend_item(trans, root, path, size);
	BUG_ON(ret);

	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(leaf, ei);
	refs += refs_to_add;
	btrfs_set_extent_refs(leaf, ei, refs);
	if (extent_op)
		__run_delayed_extent_op(extent_op, leaf, ei);

	ptr = (unsigned long)ei + item_offset;
	end = (unsigned long)ei + btrfs_item_size_nr(leaf, path->slots[0]);
	if (ptr < end - size)
		memmove_extent_buffer(leaf, ptr + size, ptr,
				      end - size - ptr);

	iref = (struct btrfs_extent_inline_ref *)ptr;
	btrfs_set_extent_inline_ref_type(leaf, iref, type);
	if (type == BTRFS_EXTENT_DATA_REF_KEY) {
		struct btrfs_extent_data_ref *dref;
		dref = (struct btrfs_extent_data_ref *)(&iref->offset);
		btrfs_set_extent_data_ref_root(leaf, dref, root_objectid);
		btrfs_set_extent_data_ref_objectid(leaf, dref, owner);
		btrfs_set_extent_data_ref_offset(leaf, dref, offset);
		btrfs_set_extent_data_ref_count(leaf, dref, refs_to_add);
	} else if (type == BTRFS_SHARED_DATA_REF_KEY) {
		struct btrfs_shared_data_ref *sref;
		sref = (struct btrfs_shared_data_ref *)(iref + 1);
		btrfs_set_shared_data_ref_count(leaf, sref, refs_to_add);
		btrfs_set_extent_inline_ref_offset(leaf, iref, parent);
	} else if (type == BTRFS_SHARED_BLOCK_REF_KEY) {
		btrfs_set_extent_inline_ref_offset(leaf, iref, parent);
	} else {
		btrfs_set_extent_inline_ref_offset(leaf, iref, root_objectid);
	}
	btrfs_mark_buffer_dirty(leaf);
	return 0;
}

static int lookup_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref **ref_ret,
				 u64 bytenr, u64 num_bytes, u64 parent,
				 u64 root_objectid, u64 owner, u64 offset)
{
	int ret;

	ret = lookup_inline_extent_backref(trans, root, path, ref_ret,
					   bytenr, num_bytes, parent,
					   root_objectid, owner, offset, 0);
	if (ret != -ENOENT)
		return ret;

	btrfs_release_path(root, path);
	*ref_ret = NULL;

	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		ret = lookup_tree_block_ref(trans, root, path, bytenr, parent,
					    root_objectid);
	} else {
		ret = lookup_extent_data_ref(trans, root, path, bytenr, parent,
					     root_objectid, owner, offset);
	}
	return ret;
}

/*
 * helper to update/remove inline back ref
 */
static noinline_for_stack
int update_inline_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref *iref,
				 int refs_to_mod,
				 struct btrfs_delayed_extent_op *extent_op)
{
	struct extent_buffer *leaf;
	struct btrfs_extent_item *ei;
	struct btrfs_extent_data_ref *dref = NULL;
	struct btrfs_shared_data_ref *sref = NULL;
	unsigned long ptr;
	unsigned long end;
	u32 item_size;
	int size;
	int type;
	int ret;
	u64 refs;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(leaf, ei);
	WARN_ON(refs_to_mod < 0 && refs + refs_to_mod <= 0);
	refs += refs_to_mod;
	btrfs_set_extent_refs(leaf, ei, refs);
	if (extent_op)
		__run_delayed_extent_op(extent_op, leaf, ei);

	type = btrfs_extent_inline_ref_type(leaf, iref);

	if (type == BTRFS_EXTENT_DATA_REF_KEY) {
		dref = (struct btrfs_extent_data_ref *)(&iref->offset);
		refs = btrfs_extent_data_ref_count(leaf, dref);
	} else if (type == BTRFS_SHARED_DATA_REF_KEY) {
		sref = (struct btrfs_shared_data_ref *)(iref + 1);
		refs = btrfs_shared_data_ref_count(leaf, sref);
	} else {
		refs = 1;
		BUG_ON(refs_to_mod != -1);
	}

	BUG_ON(refs_to_mod < 0 && refs < -refs_to_mod);
	refs += refs_to_mod;

	if (refs > 0) {
		if (type == BTRFS_EXTENT_DATA_REF_KEY)
			btrfs_set_extent_data_ref_count(leaf, dref, refs);
		else
			btrfs_set_shared_data_ref_count(leaf, sref, refs);
	} else {
		size =  btrfs_extent_inline_ref_size(type);
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
		ptr = (unsigned long)iref;
		end = (unsigned long)ei + item_size;
		if (ptr + size < end)
			memmove_extent_buffer(leaf, ptr, ptr + size,
					      end - ptr - size);
		item_size -= size;
		ret = btrfs_truncate_item(trans, root, path, item_size, 1);
		BUG_ON(ret);
	}
	btrfs_mark_buffer_dirty(leaf);
	return 0;
}

static noinline_for_stack
int insert_inline_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 u64 bytenr, u64 num_bytes, u64 parent,
				 u64 root_objectid, u64 owner,
				 u64 offset, int refs_to_add,
				 struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_extent_inline_ref *iref;
	int ret;

	ret = lookup_inline_extent_backref(trans, root, path, &iref,
					   bytenr, num_bytes, parent,
					   root_objectid, owner, offset, 1);
	if (ret == 0) {
		BUG_ON(owner < BTRFS_FIRST_FREE_OBJECTID);
		ret = update_inline_extent_backref(trans, root, path, iref,
						   refs_to_add, extent_op);
	} else if (ret == -ENOENT) {
		ret = setup_inline_extent_backref(trans, root, path, iref,
						  parent, root_objectid,
						  owner, offset, refs_to_add,
						  extent_op);
	}
	return ret;
}

static int insert_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 u64 bytenr, u64 parent, u64 root_objectid,
				 u64 owner, u64 offset, int refs_to_add)
{
	int ret;
	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		BUG_ON(refs_to_add != 1);
		ret = insert_tree_block_ref(trans, root, path, bytenr,
					    parent, root_objectid);
	} else {
		ret = insert_extent_data_ref(trans, root, path, bytenr,
					     parent, root_objectid,
					     owner, offset, refs_to_add);
	}
	return ret;
}

static int remove_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref *iref,
				 int refs_to_drop, int is_data)
{
	int ret;

	BUG_ON(!is_data && refs_to_drop != 1);
	if (iref) {
		ret = update_inline_extent_backref(trans, root, path, iref,
						   -refs_to_drop, NULL);
	} else if (is_data) {
		ret = remove_extent_data_ref(trans, root, path, refs_to_drop);
	} else {
		ret = btrfs_del_item(trans, root, path);
	}
	return ret;
}

#ifdef BIO_RW_DISCARD
static void btrfs_issue_discard(struct block_device *bdev,
				u64 start, u64 len)
{
	blkdev_issue_discard(bdev, start >> 9, len >> 9, GFP_KERNEL);
}
#endif

static int btrfs_discard_extent(struct btrfs_root *root, u64 bytenr,
				u64 num_bytes)
{
#ifdef BIO_RW_DISCARD
	int ret;
	u64 map_length = num_bytes;
	struct btrfs_multi_bio *multi = NULL;

	/* Tell the block device(s) that the sectors can be discarded */
	ret = btrfs_map_block(&root->fs_info->mapping_tree, READ,
			      bytenr, &map_length, &multi, 0);
	if (!ret) {
		struct btrfs_bio_stripe *stripe = multi->stripes;
		int i;

		if (map_length > num_bytes)
			map_length = num_bytes;

		for (i = 0; i < multi->num_stripes; i++, stripe++) {
			btrfs_issue_discard(stripe->dev->bdev,
					    stripe->physical,
					    map_length);
		}
		kfree(multi);
	}

	return ret;
#else
	return 0;
#endif
}

int btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 u64 bytenr, u64 num_bytes, u64 parent,
			 u64 root_objectid, u64 owner, u64 offset)
{
	int ret;
	BUG_ON(owner < BTRFS_FIRST_FREE_OBJECTID &&
	       root_objectid == BTRFS_TREE_LOG_OBJECTID);

	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_add_delayed_tree_ref(trans, bytenr, num_bytes,
					parent, root_objectid, (int)owner,
					BTRFS_ADD_DELAYED_REF, NULL);
	} else {
		ret = btrfs_add_delayed_data_ref(trans, bytenr, num_bytes,
					parent, root_objectid, owner, offset,
					BTRFS_ADD_DELAYED_REF, NULL);
	}
	return ret;
}

static int __btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  u64 bytenr, u64 num_bytes,
				  u64 parent, u64 root_objectid,
				  u64 owner, u64 offset, int refs_to_add,
				  struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_extent_item *item;
	u64 refs;
	int ret;
	int err = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 1;
	path->leave_spinning = 1;
	/* this will setup the path even if it fails to insert the back ref */
	ret = insert_inline_extent_backref(trans, root->fs_info->extent_root,
					   path, bytenr, num_bytes, parent,
					   root_objectid, owner, offset,
					   refs_to_add, extent_op);
	if (ret == 0)
		goto out;

	if (ret != -EAGAIN) {
		err = ret;
		goto out;
	}

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(leaf, item);
	btrfs_set_extent_refs(leaf, item, refs + refs_to_add);
	if (extent_op)
		__run_delayed_extent_op(extent_op, leaf, item);

	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(root->fs_info->extent_root, path);

	path->reada = 1;
	path->leave_spinning = 1;

	/* now insert the actual backref */
	ret = insert_extent_backref(trans, root->fs_info->extent_root,
				    path, bytenr, parent, root_objectid,
				    owner, offset, refs_to_add);
	BUG_ON(ret);
out:
	btrfs_free_path(path);
	return err;
}

static int run_delayed_data_ref(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_delayed_ref_node *node,
				struct btrfs_delayed_extent_op *extent_op,
				int insert_reserved)
{
	int ret = 0;
	struct btrfs_delayed_data_ref *ref;
	struct btrfs_key ins;
	u64 parent = 0;
	u64 ref_root = 0;
	u64 flags = 0;

	ins.objectid = node->bytenr;
	ins.offset = node->num_bytes;
	ins.type = BTRFS_EXTENT_ITEM_KEY;

	ref = btrfs_delayed_node_to_data_ref(node);
	if (node->type == BTRFS_SHARED_DATA_REF_KEY)
		parent = ref->parent;
	else
		ref_root = ref->root;

	if (node->action == BTRFS_ADD_DELAYED_REF && insert_reserved) {
		if (extent_op) {
			BUG_ON(extent_op->update_key);
			flags |= extent_op->flags_to_set;
		}
		ret = alloc_reserved_file_extent(trans, root,
						 parent, ref_root, flags,
						 ref->objectid, ref->offset,
						 &ins, node->ref_mod);
		update_reserved_extents(root, ins.objectid, ins.offset, 0);
	} else if (node->action == BTRFS_ADD_DELAYED_REF) {
		ret = __btrfs_inc_extent_ref(trans, root, node->bytenr,
					     node->num_bytes, parent,
					     ref_root, ref->objectid,
					     ref->offset, node->ref_mod,
					     extent_op);
	} else if (node->action == BTRFS_DROP_DELAYED_REF) {
		ret = __btrfs_free_extent(trans, root, node->bytenr,
					  node->num_bytes, parent,
					  ref_root, ref->objectid,
					  ref->offset, node->ref_mod,
					  extent_op);
	} else {
		BUG();
	}
	return ret;
}

static void __run_delayed_extent_op(struct btrfs_delayed_extent_op *extent_op,
				    struct extent_buffer *leaf,
				    struct btrfs_extent_item *ei)
{
	u64 flags = btrfs_extent_flags(leaf, ei);
	if (extent_op->update_flags) {
		flags |= extent_op->flags_to_set;
		btrfs_set_extent_flags(leaf, ei, flags);
	}

	if (extent_op->update_key) {
		struct btrfs_tree_block_info *bi;
		BUG_ON(!(flags & BTRFS_EXTENT_FLAG_TREE_BLOCK));
		bi = (struct btrfs_tree_block_info *)(ei + 1);
		btrfs_set_tree_block_key(leaf, bi, &extent_op->key);
	}
}

static int run_delayed_extent_op(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_delayed_ref_node *node,
				 struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_key key;
	struct btrfs_path *path;
	struct btrfs_extent_item *ei;
	struct extent_buffer *leaf;
	u32 item_size;
	int ret;
	int err = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = node->bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = node->num_bytes;

	path->reada = 1;
	path->leave_spinning = 1;
	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key,
				path, 0, 1);
	if (ret < 0) {
		err = ret;
		goto out;
	}
	if (ret > 0) {
		err = -EIO;
		goto out;
	}

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		ret = convert_extent_item_v0(trans, root->fs_info->extent_root,
					     path, (u64)-1, 0);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		leaf = path->nodes[0];
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	}
#endif
	BUG_ON(item_size < sizeof(*ei));
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	__run_delayed_extent_op(extent_op, leaf, ei);

	btrfs_mark_buffer_dirty(leaf);
out:
	btrfs_free_path(path);
	return err;
}

static int run_delayed_tree_ref(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_delayed_ref_node *node,
				struct btrfs_delayed_extent_op *extent_op,
				int insert_reserved)
{
	int ret = 0;
	struct btrfs_delayed_tree_ref *ref;
	struct btrfs_key ins;
	u64 parent = 0;
	u64 ref_root = 0;

	ins.objectid = node->bytenr;
	ins.offset = node->num_bytes;
	ins.type = BTRFS_EXTENT_ITEM_KEY;

	ref = btrfs_delayed_node_to_tree_ref(node);
	if (node->type == BTRFS_SHARED_BLOCK_REF_KEY)
		parent = ref->parent;
	else
		ref_root = ref->root;

	BUG_ON(node->ref_mod != 1);
	if (node->action == BTRFS_ADD_DELAYED_REF && insert_reserved) {
		BUG_ON(!extent_op || !extent_op->update_flags ||
		       !extent_op->update_key);
		ret = alloc_reserved_tree_block(trans, root,
						parent, ref_root,
						extent_op->flags_to_set,
						&extent_op->key,
						ref->level, &ins);
		update_reserved_extents(root, ins.objectid, ins.offset, 0);
	} else if (node->action == BTRFS_ADD_DELAYED_REF) {
		ret = __btrfs_inc_extent_ref(trans, root, node->bytenr,
					     node->num_bytes, parent, ref_root,
					     ref->level, 0, 1, extent_op);
	} else if (node->action == BTRFS_DROP_DELAYED_REF) {
		ret = __btrfs_free_extent(trans, root, node->bytenr,
					  node->num_bytes, parent, ref_root,
					  ref->level, 0, 1, extent_op);
	} else {
		BUG();
	}
	return ret;
}


/* helper function to actually process a single delayed ref entry */
static int run_one_delayed_ref(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_delayed_ref_node *node,
			       struct btrfs_delayed_extent_op *extent_op,
			       int insert_reserved)
{
	int ret;
	if (btrfs_delayed_ref_is_head(node)) {
		struct btrfs_delayed_ref_head *head;
		/*
		 * we've hit the end of the chain and we were supposed
		 * to insert this extent into the tree.  But, it got
		 * deleted before we ever needed to insert it, so all
		 * we have to do is clean up the accounting
		 */
		BUG_ON(extent_op);
		head = btrfs_delayed_node_to_head(node);
		if (insert_reserved) {
			if (head->is_data) {
				ret = btrfs_del_csums(trans, root,
						      node->bytenr,
						      node->num_bytes);
				BUG_ON(ret);
			}
			btrfs_update_pinned_extents(root, node->bytenr,
						    node->num_bytes, 1);
			update_reserved_extents(root, node->bytenr,
						node->num_bytes, 0);
		}
		mutex_unlock(&head->mutex);
		return 0;
	}

	if (node->type == BTRFS_TREE_BLOCK_REF_KEY ||
	    node->type == BTRFS_SHARED_BLOCK_REF_KEY)
		ret = run_delayed_tree_ref(trans, root, node, extent_op,
					   insert_reserved);
	else if (node->type == BTRFS_EXTENT_DATA_REF_KEY ||
		 node->type == BTRFS_SHARED_DATA_REF_KEY)
		ret = run_delayed_data_ref(trans, root, node, extent_op,
					   insert_reserved);
	else
		BUG();
	return ret;
}

static noinline struct btrfs_delayed_ref_node *
select_delayed_ref(struct btrfs_delayed_ref_head *head)
{
	struct rb_node *node;
	struct btrfs_delayed_ref_node *ref;
	int action = BTRFS_ADD_DELAYED_REF;
again:
	/*
	 * select delayed ref of type BTRFS_ADD_DELAYED_REF first.
	 * this prevents ref count from going down to zero when
	 * there still are pending delayed ref.
	 */
	node = rb_prev(&head->node.rb_node);
	while (1) {
		if (!node)
			break;
		ref = rb_entry(node, struct btrfs_delayed_ref_node,
				rb_node);
		if (ref->bytenr != head->node.bytenr)
			break;
		if (ref->action == action)
			return ref;
		node = rb_prev(node);
	}
	if (action == BTRFS_ADD_DELAYED_REF) {
		action = BTRFS_DROP_DELAYED_REF;
		goto again;
	}
	return NULL;
}

static noinline int run_clustered_refs(struct btrfs_trans_handle *trans,
				       struct btrfs_root *root,
				       struct list_head *cluster)
{
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_node *ref;
	struct btrfs_delayed_ref_head *locked_ref = NULL;
	struct btrfs_delayed_extent_op *extent_op;
	int ret;
	int count = 0;
	int must_insert_reserved = 0;

	delayed_refs = &trans->transaction->delayed_refs;
	while (1) {
		if (!locked_ref) {
			/* pick a new head ref from the cluster list */
			if (list_empty(cluster))
				break;

			locked_ref = list_entry(cluster->next,
				     struct btrfs_delayed_ref_head, cluster);

			/* grab the lock that says we are going to process
			 * all the refs for this head */
			ret = btrfs_delayed_ref_lock(trans, locked_ref);

			/*
			 * we may have dropped the spin lock to get the head
			 * mutex lock, and that might have given someone else
			 * time to free the head.  If that's true, it has been
			 * removed from our list and we can move on.
			 */
			if (ret == -EAGAIN) {
				locked_ref = NULL;
				count++;
				continue;
			}
		}

		/*
		 * record the must insert reserved flag before we
		 * drop the spin lock.
		 */
		must_insert_reserved = locked_ref->must_insert_reserved;
		locked_ref->must_insert_reserved = 0;

		extent_op = locked_ref->extent_op;
		locked_ref->extent_op = NULL;

		/*
		 * locked_ref is the head node, so we have to go one
		 * node back for any delayed ref updates
		 */
		ref = select_delayed_ref(locked_ref);
		if (!ref) {
			/* All delayed refs have been processed, Go ahead
			 * and send the head node to run_one_delayed_ref,
			 * so that any accounting fixes can happen
			 */
			ref = &locked_ref->node;

			if (extent_op && must_insert_reserved) {
				kfree(extent_op);
				extent_op = NULL;
			}

			if (extent_op) {
				spin_unlock(&delayed_refs->lock);

				ret = run_delayed_extent_op(trans, root,
							    ref, extent_op);
				BUG_ON(ret);
				kfree(extent_op);

				cond_resched();
				spin_lock(&delayed_refs->lock);
				continue;
			}

			list_del_init(&locked_ref->cluster);
			locked_ref = NULL;
		}

		ref->in_tree = 0;
		rb_erase(&ref->rb_node, &delayed_refs->root);
		delayed_refs->num_entries--;

		spin_unlock(&delayed_refs->lock);

		ret = run_one_delayed_ref(trans, root, ref, extent_op,
					  must_insert_reserved);
		BUG_ON(ret);

		btrfs_put_delayed_ref(ref);
		kfree(extent_op);
		count++;

		cond_resched();
		spin_lock(&delayed_refs->lock);
	}
	return count;
}

/*
 * this starts processing the delayed reference count updates and
 * extent insertions we have queued up so far.  count can be
 * 0, which means to process everything in the tree at the start
 * of the run (but not newly added entries), or it can be some target
 * number you'd like to process.
 */
int btrfs_run_delayed_refs(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, unsigned long count)
{
	struct rb_node *node;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_node *ref;
	struct list_head cluster;
	int ret;
	int run_all = count == (unsigned long)-1;
	int run_most = 0;

	if (root == root->fs_info->extent_root)
		root = root->fs_info->tree_root;

	delayed_refs = &trans->transaction->delayed_refs;
	INIT_LIST_HEAD(&cluster);
again:
	spin_lock(&delayed_refs->lock);
	if (count == 0) {
		count = delayed_refs->num_entries * 2;
		run_most = 1;
	}
	while (1) {
		if (!(run_all || run_most) &&
		    delayed_refs->num_heads_ready < 64)
			break;

		/*
		 * go find something we can process in the rbtree.  We start at
		 * the beginning of the tree, and then build a cluster
		 * of refs to process starting at the first one we are able to
		 * lock
		 */
		ret = btrfs_find_ref_cluster(trans, &cluster,
					     delayed_refs->run_delayed_start);
		if (ret)
			break;

		ret = run_clustered_refs(trans, root, &cluster);
		BUG_ON(ret < 0);

		count -= min_t(unsigned long, ret, count);

		if (count == 0)
			break;
	}

	if (run_all) {
		node = rb_first(&delayed_refs->root);
		if (!node)
			goto out;
		count = (unsigned long)-1;

		while (node) {
			ref = rb_entry(node, struct btrfs_delayed_ref_node,
				       rb_node);
			if (btrfs_delayed_ref_is_head(ref)) {
				struct btrfs_delayed_ref_head *head;

				head = btrfs_delayed_node_to_head(ref);
				atomic_inc(&ref->refs);

				spin_unlock(&delayed_refs->lock);
				mutex_lock(&head->mutex);
				mutex_unlock(&head->mutex);

				btrfs_put_delayed_ref(ref);
				cond_resched();
				goto again;
			}
			node = rb_next(node);
		}
		spin_unlock(&delayed_refs->lock);
		schedule_timeout(1);
		goto again;
	}
out:
	spin_unlock(&delayed_refs->lock);
	return 0;
}

int btrfs_set_disk_extent_flags(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				u64 bytenr, u64 num_bytes, u64 flags,
				int is_data)
{
	struct btrfs_delayed_extent_op *extent_op;
	int ret;

	extent_op = kmalloc(sizeof(*extent_op), GFP_NOFS);
	if (!extent_op)
		return -ENOMEM;

	extent_op->flags_to_set = flags;
	extent_op->update_flags = 1;
	extent_op->update_key = 0;
	extent_op->is_data = is_data ? 1 : 0;

	ret = btrfs_add_delayed_extent_op(trans, bytenr, num_bytes, extent_op);
	if (ret)
		kfree(extent_op);
	return ret;
}

static noinline int check_delayed_ref(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      struct btrfs_path *path,
				      u64 objectid, u64 offset, u64 bytenr)
{
	struct btrfs_delayed_ref_head *head;
	struct btrfs_delayed_ref_node *ref;
	struct btrfs_delayed_data_ref *data_ref;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct rb_node *node;
	int ret = 0;

	ret = -ENOENT;
	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);
	head = btrfs_find_delayed_ref_head(trans, bytenr);
	if (!head)
		goto out;

	if (!mutex_trylock(&head->mutex)) {
		atomic_inc(&head->node.refs);
		spin_unlock(&delayed_refs->lock);

		btrfs_release_path(root->fs_info->extent_root, path);

		mutex_lock(&head->mutex);
		mutex_unlock(&head->mutex);
		btrfs_put_delayed_ref(&head->node);
		return -EAGAIN;
	}

	node = rb_prev(&head->node.rb_node);
	if (!node)
		goto out_unlock;

	ref = rb_entry(node, struct btrfs_delayed_ref_node, rb_node);

	if (ref->bytenr != bytenr)
		goto out_unlock;

	ret = 1;
	if (ref->type != BTRFS_EXTENT_DATA_REF_KEY)
		goto out_unlock;

	data_ref = btrfs_delayed_node_to_data_ref(ref);

	node = rb_prev(node);
	if (node) {
		ref = rb_entry(node, struct btrfs_delayed_ref_node, rb_node);
		if (ref->bytenr == bytenr)
			goto out_unlock;
	}

	if (data_ref->root != root->root_key.objectid ||
	    data_ref->objectid != objectid || data_ref->offset != offset)
		goto out_unlock;

	ret = 0;
out_unlock:
	mutex_unlock(&head->mutex);
out:
	spin_unlock(&delayed_refs->lock);
	return ret;
}

static noinline int check_committed_ref(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_path *path,
					u64 objectid, u64 offset, u64 bytenr)
{
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	struct extent_buffer *leaf;
	struct btrfs_extent_data_ref *ref;
	struct btrfs_extent_inline_ref *iref;
	struct btrfs_extent_item *ei;
	struct btrfs_key key;
	u32 item_size;
	int ret;

	key.objectid = bytenr;
	key.offset = (u64)-1;
	key.type = BTRFS_EXTENT_ITEM_KEY;

	ret = btrfs_search_slot(NULL, extent_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	BUG_ON(ret == 0);

	ret = -ENOENT;
	if (path->slots[0] == 0)
		goto out;

	path->slots[0]--;
	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

	if (key.objectid != bytenr || key.type != BTRFS_EXTENT_ITEM_KEY)
		goto out;

	ret = 1;
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		WARN_ON(item_size != sizeof(struct btrfs_extent_item_v0));
		goto out;
	}
#endif
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);

	if (item_size != sizeof(*ei) +
	    btrfs_extent_inline_ref_size(BTRFS_EXTENT_DATA_REF_KEY))
		goto out;

	if (btrfs_extent_generation(leaf, ei) <=
	    btrfs_root_last_snapshot(&root->root_item))
		goto out;

	iref = (struct btrfs_extent_inline_ref *)(ei + 1);
	if (btrfs_extent_inline_ref_type(leaf, iref) !=
	    BTRFS_EXTENT_DATA_REF_KEY)
		goto out;

	ref = (struct btrfs_extent_data_ref *)(&iref->offset);
	if (btrfs_extent_refs(leaf, ei) !=
	    btrfs_extent_data_ref_count(leaf, ref) ||
	    btrfs_extent_data_ref_root(leaf, ref) !=
	    root->root_key.objectid ||
	    btrfs_extent_data_ref_objectid(leaf, ref) != objectid ||
	    btrfs_extent_data_ref_offset(leaf, ref) != offset)
		goto out;

	ret = 0;
out:
	return ret;
}

int btrfs_cross_ref_exist(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  u64 objectid, u64 offset, u64 bytenr)
{
	struct btrfs_path *path;
	int ret;
	int ret2;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOENT;

	do {
		ret = check_committed_ref(trans, root, path, objectid,
					  offset, bytenr);
		if (ret && ret != -ENOENT)
			goto out;

		ret2 = check_delayed_ref(trans, root, path, objectid,
					 offset, bytenr);
	} while (ret2 == -EAGAIN);

	if (ret2 && ret2 != -ENOENT) {
		ret = ret2;
		goto out;
	}

	if (ret != -ENOENT || ret2 != -ENOENT)
		ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

#if 0
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
		if (ret == -EEXIST && shared) {
			struct btrfs_leaf_ref *old;
			old = btrfs_lookup_leaf_ref(root, ref->bytenr);
			BUG_ON(!old);
			btrfs_remove_leaf_ref(root, old);
			btrfs_free_leaf_ref(root, old);
			ret = btrfs_add_leaf_ref(root, ref, shared);
		}
		WARN_ON(ret);
		btrfs_free_leaf_ref(root, ref);
	}
out:
	return ret;
}

/* when a block goes through cow, we update the reference counts of
 * everything that block points to.  The internal pointers of the block
 * can be in just about any order, and it is likely to have clusters of
 * things that are close together and clusters of things that are not.
 *
 * To help reduce the seeks that come with updating all of these reference
 * counts, sort them by byte number before actual updates are done.
 *
 * struct refsort is used to match byte number to slot in the btree block.
 * we sort based on the byte number and then use the slot to actually
 * find the item.
 *
 * struct refsort is smaller than strcut btrfs_item and smaller than
 * struct btrfs_key_ptr.  Since we're currently limited to the page size
 * for a btree block, there's no way for a kmalloc of refsorts for a
 * single node to be bigger than a page.
 */
struct refsort {
	u64 bytenr;
	u32 slot;
};

/*
 * for passing into sort()
 */
static int refsort_cmp(const void *a_void, const void *b_void)
{
	const struct refsort *a = a_void;
	const struct refsort *b = b_void;

	if (a->bytenr < b->bytenr)
		return -1;
	if (a->bytenr > b->bytenr)
		return 1;
	return 0;
}
#endif

static int __btrfs_mod_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct extent_buffer *buf,
			   int full_backref, int inc)
{
	u64 bytenr;
	u64 num_bytes;
	u64 parent;
	u64 ref_root;
	u32 nritems;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	int i;
	int level;
	int ret = 0;
	int (*process_func)(struct btrfs_trans_handle *, struct btrfs_root *,
			    u64, u64, u64, u64, u64, u64);

	ref_root = btrfs_header_owner(buf);
	nritems = btrfs_header_nritems(buf);
	level = btrfs_header_level(buf);

	if (!root->ref_cows && level == 0)
		return 0;

	if (inc)
		process_func = btrfs_inc_extent_ref;
	else
		process_func = btrfs_free_extent;

	if (full_backref)
		parent = buf->start;
	else
		parent = 0;

	for (i = 0; i < nritems; i++) {
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

			num_bytes = btrfs_file_extent_disk_num_bytes(buf, fi);
			key.offset -= btrfs_file_extent_offset(buf, fi);
			ret = process_func(trans, root, bytenr, num_bytes,
					   parent, ref_root, key.objectid,
					   key.offset);
			if (ret)
				goto fail;
		} else {
			bytenr = btrfs_node_blockptr(buf, i);
			num_bytes = btrfs_level_size(root, level - 1);
			ret = process_func(trans, root, bytenr, num_bytes,
					   parent, ref_root, level - 1, 0);
			if (ret)
				goto fail;
		}
	}
	return 0;
fail:
	BUG();
	return ret;
}

int btrfs_inc_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf, int full_backref)
{
	return __btrfs_mod_ref(trans, root, buf, full_backref, 1);
}

int btrfs_dec_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf, int full_backref)
{
	return __btrfs_mod_ref(trans, root, buf, full_backref, 0);
}

static int write_one_cache_group(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_block_group_cache *cache)
{
	int ret;
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
	if (ret)
		return ret;
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

	while (1) {
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
	return werr;
}

int btrfs_extent_readonly(struct btrfs_root *root, u64 bytenr)
{
	struct btrfs_block_group_cache *block_group;
	int readonly = 0;

	block_group = btrfs_lookup_block_group(root->fs_info, bytenr);
	if (!block_group || block_group->ro)
		readonly = 1;
	if (block_group)
		btrfs_put_block_group(block_group);
	return readonly;
}

static int update_space_info(struct btrfs_fs_info *info, u64 flags,
			     u64 total_bytes, u64 bytes_used,
			     struct btrfs_space_info **space_info)
{
	struct btrfs_space_info *found;

	found = __find_space_info(info, flags);
	if (found) {
		spin_lock(&found->lock);
		found->total_bytes += total_bytes;
		found->bytes_used += bytes_used;
		found->full = 0;
		spin_unlock(&found->lock);
		*space_info = found;
		return 0;
	}
	found = kzalloc(sizeof(*found), GFP_NOFS);
	if (!found)
		return -ENOMEM;

	INIT_LIST_HEAD(&found->block_groups);
	init_rwsem(&found->groups_sem);
	spin_lock_init(&found->lock);
	found->flags = flags;
	found->total_bytes = total_bytes;
	found->bytes_used = bytes_used;
	found->bytes_pinned = 0;
	found->bytes_reserved = 0;
	found->bytes_readonly = 0;
	found->bytes_delalloc = 0;
	found->full = 0;
	found->force_alloc = 0;
	*space_info = found;
	list_add_rcu(&found->list, &info->space_info);
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

static void set_block_group_readonly(struct btrfs_block_group_cache *cache)
{
	spin_lock(&cache->space_info->lock);
	spin_lock(&cache->lock);
	if (!cache->ro) {
		cache->space_info->bytes_readonly += cache->key.offset -
					btrfs_block_group_used(&cache->item);
		cache->ro = 1;
	}
	spin_unlock(&cache->lock);
	spin_unlock(&cache->space_info->lock);
}

u64 btrfs_reduce_alloc_profile(struct btrfs_root *root, u64 flags)
{
	u64 num_devices = root->fs_info->fs_devices->rw_devices;

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

static u64 btrfs_get_alloc_profile(struct btrfs_root *root, u64 data)
{
	struct btrfs_fs_info *info = root->fs_info;
	u64 alloc_profile;

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

	return btrfs_reduce_alloc_profile(root, data);
}

void btrfs_set_inode_space_info(struct btrfs_root *root, struct inode *inode)
{
	u64 alloc_target;

	alloc_target = btrfs_get_alloc_profile(root, 1);
	BTRFS_I(inode)->space_info = __find_space_info(root->fs_info,
						       alloc_target);
}

/*
 * for now this just makes sure we have at least 5% of our metadata space free
 * for use.
 */
int btrfs_check_metadata_free_space(struct btrfs_root *root)
{
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_space_info *meta_sinfo;
	u64 alloc_target, thresh;
	int committed = 0, ret;

	/* get the space info for where the metadata will live */
	alloc_target = btrfs_get_alloc_profile(root, 0);
	meta_sinfo = __find_space_info(info, alloc_target);

again:
	spin_lock(&meta_sinfo->lock);
	if (!meta_sinfo->full)
		thresh = meta_sinfo->total_bytes * 80;
	else
		thresh = meta_sinfo->total_bytes * 95;

	do_div(thresh, 100);

	if (meta_sinfo->bytes_used + meta_sinfo->bytes_reserved +
	    meta_sinfo->bytes_pinned + meta_sinfo->bytes_readonly > thresh) {
		struct btrfs_trans_handle *trans;
		if (!meta_sinfo->full) {
			meta_sinfo->force_alloc = 1;
			spin_unlock(&meta_sinfo->lock);

			trans = btrfs_start_transaction(root, 1);
			if (!trans)
				return -ENOMEM;

			ret = do_chunk_alloc(trans, root->fs_info->extent_root,
					     2 * 1024 * 1024, alloc_target, 0);
			btrfs_end_transaction(trans, root);
			goto again;
		}
		spin_unlock(&meta_sinfo->lock);

		if (!committed) {
			committed = 1;
			trans = btrfs_join_transaction(root, 1);
			if (!trans)
				return -ENOMEM;
			ret = btrfs_commit_transaction(trans, root);
			if (ret)
				return ret;
			goto again;
		}
		return -ENOSPC;
	}
	spin_unlock(&meta_sinfo->lock);

	return 0;
}

/*
 * This will check the space that the inode allocates from to make sure we have
 * enough space for bytes.
 */
int btrfs_check_data_free_space(struct btrfs_root *root, struct inode *inode,
				u64 bytes)
{
	struct btrfs_space_info *data_sinfo;
	int ret = 0, committed = 0;

	/* make sure bytes are sectorsize aligned */
	bytes = (bytes + root->sectorsize - 1) & ~((u64)root->sectorsize - 1);

	data_sinfo = BTRFS_I(inode)->space_info;
again:
	/* make sure we have enough space to handle the data first */
	spin_lock(&data_sinfo->lock);
	if (data_sinfo->total_bytes - data_sinfo->bytes_used -
	    data_sinfo->bytes_delalloc - data_sinfo->bytes_reserved -
	    data_sinfo->bytes_pinned - data_sinfo->bytes_readonly -
	    data_sinfo->bytes_may_use < bytes) {
		struct btrfs_trans_handle *trans;

		/*
		 * if we don't have enough free bytes in this space then we need
		 * to alloc a new chunk.
		 */
		if (!data_sinfo->full) {
			u64 alloc_target;

			data_sinfo->force_alloc = 1;
			spin_unlock(&data_sinfo->lock);

			alloc_target = btrfs_get_alloc_profile(root, 1);
			trans = btrfs_start_transaction(root, 1);
			if (!trans)
				return -ENOMEM;

			ret = do_chunk_alloc(trans, root->fs_info->extent_root,
					     bytes + 2 * 1024 * 1024,
					     alloc_target, 0);
			btrfs_end_transaction(trans, root);
			if (ret)
				return ret;
			goto again;
		}
		spin_unlock(&data_sinfo->lock);

		/* commit the current transaction and try again */
		if (!committed) {
			committed = 1;
			trans = btrfs_join_transaction(root, 1);
			if (!trans)
				return -ENOMEM;
			ret = btrfs_commit_transaction(trans, root);
			if (ret)
				return ret;
			goto again;
		}

		printk(KERN_ERR "no space left, need %llu, %llu delalloc bytes"
		       ", %llu bytes_used, %llu bytes_reserved, "
		       "%llu bytes_pinned, %llu bytes_readonly, %llu may use "
		       "%llu total\n", (unsigned long long)bytes,
		       (unsigned long long)data_sinfo->bytes_delalloc,
		       (unsigned long long)data_sinfo->bytes_used,
		       (unsigned long long)data_sinfo->bytes_reserved,
		       (unsigned long long)data_sinfo->bytes_pinned,
		       (unsigned long long)data_sinfo->bytes_readonly,
		       (unsigned long long)data_sinfo->bytes_may_use,
		       (unsigned long long)data_sinfo->total_bytes);
		return -ENOSPC;
	}
	data_sinfo->bytes_may_use += bytes;
	BTRFS_I(inode)->reserved_bytes += bytes;
	spin_unlock(&data_sinfo->lock);

	return btrfs_check_metadata_free_space(root);
}

/*
 * if there was an error for whatever reason after calling
 * btrfs_check_data_free_space, call this so we can cleanup the counters.
 */
void btrfs_free_reserved_data_space(struct btrfs_root *root,
				    struct inode *inode, u64 bytes)
{
	struct btrfs_space_info *data_sinfo;

	/* make sure bytes are sectorsize aligned */
	bytes = (bytes + root->sectorsize - 1) & ~((u64)root->sectorsize - 1);

	data_sinfo = BTRFS_I(inode)->space_info;
	spin_lock(&data_sinfo->lock);
	data_sinfo->bytes_may_use -= bytes;
	BTRFS_I(inode)->reserved_bytes -= bytes;
	spin_unlock(&data_sinfo->lock);
}

/* called when we are adding a delalloc extent to the inode's io_tree */
void btrfs_delalloc_reserve_space(struct btrfs_root *root, struct inode *inode,
				  u64 bytes)
{
	struct btrfs_space_info *data_sinfo;

	/* get the space info for where this inode will be storing its data */
	data_sinfo = BTRFS_I(inode)->space_info;

	/* make sure we have enough space to handle the data first */
	spin_lock(&data_sinfo->lock);
	data_sinfo->bytes_delalloc += bytes;

	/*
	 * we are adding a delalloc extent without calling
	 * btrfs_check_data_free_space first.  This happens on a weird
	 * writepage condition, but shouldn't hurt our accounting
	 */
	if (unlikely(bytes > BTRFS_I(inode)->reserved_bytes)) {
		data_sinfo->bytes_may_use -= BTRFS_I(inode)->reserved_bytes;
		BTRFS_I(inode)->reserved_bytes = 0;
	} else {
		data_sinfo->bytes_may_use -= bytes;
		BTRFS_I(inode)->reserved_bytes -= bytes;
	}

	spin_unlock(&data_sinfo->lock);
}

/* called when we are clearing an delalloc extent from the inode's io_tree */
void btrfs_delalloc_free_space(struct btrfs_root *root, struct inode *inode,
			      u64 bytes)
{
	struct btrfs_space_info *info;

	info = BTRFS_I(inode)->space_info;

	spin_lock(&info->lock);
	info->bytes_delalloc -= bytes;
	spin_unlock(&info->lock);
}

static void force_metadata_allocation(struct btrfs_fs_info *info)
{
	struct list_head *head = &info->space_info;
	struct btrfs_space_info *found;

	rcu_read_lock();
	list_for_each_entry_rcu(found, head, list) {
		if (found->flags & BTRFS_BLOCK_GROUP_METADATA)
			found->force_alloc = 1;
	}
	rcu_read_unlock();
}

static int do_chunk_alloc(struct btrfs_trans_handle *trans,
			  struct btrfs_root *extent_root, u64 alloc_bytes,
			  u64 flags, int force)
{
	struct btrfs_space_info *space_info;
	struct btrfs_fs_info *fs_info = extent_root->fs_info;
	u64 thresh;
	int ret = 0;

	mutex_lock(&fs_info->chunk_mutex);

	flags = btrfs_reduce_alloc_profile(extent_root, flags);

	space_info = __find_space_info(extent_root->fs_info, flags);
	if (!space_info) {
		ret = update_space_info(extent_root->fs_info, flags,
					0, 0, &space_info);
		BUG_ON(ret);
	}
	BUG_ON(!space_info);

	spin_lock(&space_info->lock);
	if (space_info->force_alloc) {
		force = 1;
		space_info->force_alloc = 0;
	}
	if (space_info->full) {
		spin_unlock(&space_info->lock);
		goto out;
	}

	thresh = space_info->total_bytes - space_info->bytes_readonly;
	thresh = div_factor(thresh, 6);
	if (!force &&
	   (space_info->bytes_used + space_info->bytes_pinned +
	    space_info->bytes_reserved + alloc_bytes) < thresh) {
		spin_unlock(&space_info->lock);
		goto out;
	}
	spin_unlock(&space_info->lock);

	/*
	 * if we're doing a data chunk, go ahead and make sure that
	 * we keep a reasonable number of metadata chunks allocated in the
	 * FS as well.
	 */
	if (flags & BTRFS_BLOCK_GROUP_DATA) {
		fs_info->data_chunk_allocations++;
		if (!(fs_info->data_chunk_allocations %
		      fs_info->metadata_ratio))
			force_metadata_allocation(fs_info);
	}

	ret = btrfs_alloc_chunk(trans, extent_root, flags);
	if (ret)
		space_info->full = 1;
out:
	mutex_unlock(&extent_root->fs_info->chunk_mutex);
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

	/* block accounting for super block */
	spin_lock(&info->delalloc_lock);
	old_val = btrfs_super_bytes_used(&info->super_copy);
	if (alloc)
		old_val += num_bytes;
	else
		old_val -= num_bytes;
	btrfs_set_super_bytes_used(&info->super_copy, old_val);

	/* block accounting for root item */
	old_val = btrfs_root_used(&root->root_item);
	if (alloc)
		old_val += num_bytes;
	else
		old_val -= num_bytes;
	btrfs_set_root_used(&root->root_item, old_val);
	spin_unlock(&info->delalloc_lock);

	while (total) {
		cache = btrfs_lookup_block_group(info, bytenr);
		if (!cache)
			return -1;
		byte_in_group = bytenr - cache->key.objectid;
		WARN_ON(byte_in_group > cache->key.offset);

		spin_lock(&cache->space_info->lock);
		spin_lock(&cache->lock);
		cache->dirty = 1;
		old_val = btrfs_block_group_used(&cache->item);
		num_bytes = min(total, cache->key.offset - byte_in_group);
		if (alloc) {
			old_val += num_bytes;
			cache->space_info->bytes_used += num_bytes;
			if (cache->ro)
				cache->space_info->bytes_readonly -= num_bytes;
			btrfs_set_block_group_used(&cache->item, old_val);
			spin_unlock(&cache->lock);
			spin_unlock(&cache->space_info->lock);
		} else {
			old_val -= num_bytes;
			cache->space_info->bytes_used -= num_bytes;
			if (cache->ro)
				cache->space_info->bytes_readonly += num_bytes;
			btrfs_set_block_group_used(&cache->item, old_val);
			spin_unlock(&cache->lock);
			spin_unlock(&cache->space_info->lock);
			if (mark_free) {
				int ret;

				ret = btrfs_discard_extent(root, bytenr,
							   num_bytes);
				WARN_ON(ret);

				ret = btrfs_add_free_space(cache, bytenr,
							   num_bytes);
				WARN_ON(ret);
			}
		}
		btrfs_put_block_group(cache);
		total -= num_bytes;
		bytenr += num_bytes;
	}
	return 0;
}

static u64 first_logical_byte(struct btrfs_root *root, u64 search_start)
{
	struct btrfs_block_group_cache *cache;
	u64 bytenr;

	cache = btrfs_lookup_first_block_group(root->fs_info, search_start);
	if (!cache)
		return 0;

	bytenr = cache->key.objectid;
	btrfs_put_block_group(cache);

	return bytenr;
}

int btrfs_update_pinned_extents(struct btrfs_root *root,
				u64 bytenr, u64 num, int pin)
{
	u64 len;
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *fs_info = root->fs_info;

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
			spin_lock(&cache->space_info->lock);
			spin_lock(&cache->lock);
			cache->pinned += len;
			cache->space_info->bytes_pinned += len;
			spin_unlock(&cache->lock);
			spin_unlock(&cache->space_info->lock);
			fs_info->total_pinned += len;
		} else {
			spin_lock(&cache->space_info->lock);
			spin_lock(&cache->lock);
			cache->pinned -= len;
			cache->space_info->bytes_pinned -= len;
			spin_unlock(&cache->lock);
			spin_unlock(&cache->space_info->lock);
			fs_info->total_pinned -= len;
			if (cache->cached)
				btrfs_add_free_space(cache, bytenr, len);
		}
		btrfs_put_block_group(cache);
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

	while (num > 0) {
		cache = btrfs_lookup_block_group(fs_info, bytenr);
		BUG_ON(!cache);
		len = min(num, cache->key.offset -
			  (bytenr - cache->key.objectid));

		spin_lock(&cache->space_info->lock);
		spin_lock(&cache->lock);
		if (reserve) {
			cache->reserved += len;
			cache->space_info->bytes_reserved += len;
		} else {
			cache->reserved -= len;
			cache->space_info->bytes_reserved -= len;
		}
		spin_unlock(&cache->lock);
		spin_unlock(&cache->space_info->lock);
		btrfs_put_block_group(cache);
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

	while (1) {
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

	while (1) {
		ret = find_first_extent_bit(unpin, 0, &start, &end,
					    EXTENT_DIRTY);
		if (ret)
			break;

		ret = btrfs_discard_extent(root, start, end + 1 - start);

		/* unlocks the pinned mutex */
		btrfs_update_pinned_extents(root, start, end + 1 - start, 0);
		clear_extent_dirty(unpin, start, end, GFP_NOFS);

		cond_resched();
	}
	return ret;
}

static int pin_down_bytes(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  struct btrfs_path *path,
			  u64 bytenr, u64 num_bytes, int is_data,
			  struct extent_buffer **must_clean)
{
	int err = 0;
	struct extent_buffer *buf;

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
		    header_transid == trans->transid &&
		    !btrfs_header_flag(buf, BTRFS_HEADER_FLAG_WRITTEN)) {
			*must_clean = buf;
			return 1;
		}
		btrfs_tree_unlock(buf);
	}
	free_extent_buffer(buf);
pinit:
	btrfs_set_path_blocking(path);
	/* unlocks the pinned mutex */
	btrfs_update_pinned_extents(root, bytenr, num_bytes, 1);

	BUG_ON(err < 0);
	return 0;
}


static int __btrfs_free_extent(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				u64 bytenr, u64 num_bytes, u64 parent,
				u64 root_objectid, u64 owner_objectid,
				u64 owner_offset, int refs_to_drop,
				struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_key key;
	struct btrfs_path *path;
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_root *extent_root = info->extent_root;
	struct extent_buffer *leaf;
	struct btrfs_extent_item *ei;
	struct btrfs_extent_inline_ref *iref;
	int ret;
	int is_data;
	int extent_slot = 0;
	int found_extent = 0;
	int num_to_del = 1;
	u32 item_size;
	u64 refs;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 1;
	path->leave_spinning = 1;

	is_data = owner_objectid >= BTRFS_FIRST_FREE_OBJECTID;
	BUG_ON(!is_data && refs_to_drop != 1);

	ret = lookup_extent_backref(trans, extent_root, path, &iref,
				    bytenr, num_bytes, parent,
				    root_objectid, owner_objectid,
				    owner_offset);
	if (ret == 0) {
		extent_slot = path->slots[0];
		while (extent_slot >= 0) {
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      extent_slot);
			if (key.objectid != bytenr)
				break;
			if (key.type == BTRFS_EXTENT_ITEM_KEY &&
			    key.offset == num_bytes) {
				found_extent = 1;
				break;
			}
			if (path->slots[0] - extent_slot > 5)
				break;
			extent_slot--;
		}
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
		item_size = btrfs_item_size_nr(path->nodes[0], extent_slot);
		if (found_extent && item_size < sizeof(*ei))
			found_extent = 0;
#endif
		if (!found_extent) {
			BUG_ON(iref);
			ret = remove_extent_backref(trans, extent_root, path,
						    NULL, refs_to_drop,
						    is_data);
			BUG_ON(ret);
			btrfs_release_path(extent_root, path);
			path->leave_spinning = 1;

			key.objectid = bytenr;
			key.type = BTRFS_EXTENT_ITEM_KEY;
			key.offset = num_bytes;

			ret = btrfs_search_slot(trans, extent_root,
						&key, path, -1, 1);
			if (ret) {
				printk(KERN_ERR "umm, got %d back from search"
				       ", was looking for %llu\n", ret,
				       (unsigned long long)bytenr);
				btrfs_print_leaf(extent_root, path->nodes[0]);
			}
			BUG_ON(ret);
			extent_slot = path->slots[0];
		}
	} else {
		btrfs_print_leaf(extent_root, path->nodes[0]);
		WARN_ON(1);
		printk(KERN_ERR "btrfs unable to find ref byte nr %llu "
		       "parent %llu root %llu  owner %llu offset %llu\n",
		       (unsigned long long)bytenr,
		       (unsigned long long)parent,
		       (unsigned long long)root_objectid,
		       (unsigned long long)owner_objectid,
		       (unsigned long long)owner_offset);
	}

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, extent_slot);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		BUG_ON(found_extent || extent_slot != path->slots[0]);
		ret = convert_extent_item_v0(trans, extent_root, path,
					     owner_objectid, 0);
		BUG_ON(ret < 0);

		btrfs_release_path(extent_root, path);
		path->leave_spinning = 1;

		key.objectid = bytenr;
		key.type = BTRFS_EXTENT_ITEM_KEY;
		key.offset = num_bytes;

		ret = btrfs_search_slot(trans, extent_root, &key, path,
					-1, 1);
		if (ret) {
			printk(KERN_ERR "umm, got %d back from search"
			       ", was looking for %llu\n", ret,
			       (unsigned long long)bytenr);
			btrfs_print_leaf(extent_root, path->nodes[0]);
		}
		BUG_ON(ret);
		extent_slot = path->slots[0];
		leaf = path->nodes[0];
		item_size = btrfs_item_size_nr(leaf, extent_slot);
	}
#endif
	BUG_ON(item_size < sizeof(*ei));
	ei = btrfs_item_ptr(leaf, extent_slot,
			    struct btrfs_extent_item);
	if (owner_objectid < BTRFS_FIRST_FREE_OBJECTID) {
		struct btrfs_tree_block_info *bi;
		BUG_ON(item_size < sizeof(*ei) + sizeof(*bi));
		bi = (struct btrfs_tree_block_info *)(ei + 1);
		WARN_ON(owner_objectid != btrfs_tree_block_level(leaf, bi));
	}

	refs = btrfs_extent_refs(leaf, ei);
	BUG_ON(refs < refs_to_drop);
	refs -= refs_to_drop;

	if (refs > 0) {
		if (extent_op)
			__run_delayed_extent_op(extent_op, leaf, ei);
		/*
		 * In the case of inline back ref, reference count will
		 * be updated by remove_extent_backref
		 */
		if (iref) {
			BUG_ON(!found_extent);
		} else {
			btrfs_set_extent_refs(leaf, ei, refs);
			btrfs_mark_buffer_dirty(leaf);
		}
		if (found_extent) {
			ret = remove_extent_backref(trans, extent_root, path,
						    iref, refs_to_drop,
						    is_data);
			BUG_ON(ret);
		}
	} else {
		int mark_free = 0;
		struct extent_buffer *must_clean = NULL;

		if (found_extent) {
			BUG_ON(is_data && refs_to_drop !=
			       extent_data_ref_count(root, path, iref));
			if (iref) {
				BUG_ON(path->slots[0] != extent_slot);
			} else {
				BUG_ON(path->slots[0] != extent_slot + 1);
				path->slots[0] = extent_slot;
				num_to_del = 2;
			}
		}

		ret = pin_down_bytes(trans, root, path, bytenr,
				     num_bytes, is_data, &must_clean);
		if (ret > 0)
			mark_free = 1;
		BUG_ON(ret < 0);
		/*
		 * it is going to be very rare for someone to be waiting
		 * on the block we're freeing.  del_items might need to
		 * schedule, so rather than get fancy, just force it
		 * to blocking here
		 */
		if (must_clean)
			btrfs_set_lock_blocking(must_clean);

		ret = btrfs_del_items(trans, extent_root, path, path->slots[0],
				      num_to_del);
		BUG_ON(ret);
		btrfs_release_path(extent_root, path);

		if (must_clean) {
			clean_tree_block(NULL, root, must_clean);
			btrfs_tree_unlock(must_clean);
			free_extent_buffer(must_clean);
		}

		if (is_data) {
			ret = btrfs_del_csums(trans, root, bytenr, num_bytes);
			BUG_ON(ret);
		} else {
			invalidate_mapping_pages(info->btree_inode->i_mapping,
			     bytenr >> PAGE_CACHE_SHIFT,
			     (bytenr + num_bytes - 1) >> PAGE_CACHE_SHIFT);
		}

		ret = update_block_group(trans, root, bytenr, num_bytes, 0,
					 mark_free);
		BUG_ON(ret);
	}
	btrfs_free_path(path);
	return ret;
}

/*
 * when we free an extent, it is possible (and likely) that we free the last
 * delayed ref for that extent as well.  This searches the delayed ref tree for
 * a given extent, and if there are no other delayed refs to be processed, it
 * removes it from the tree.
 */
static noinline int check_ref_cleanup(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root, u64 bytenr)
{
	struct btrfs_delayed_ref_head *head;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_node *ref;
	struct rb_node *node;
	int ret;

	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);
	head = btrfs_find_delayed_ref_head(trans, bytenr);
	if (!head)
		goto out;

	node = rb_prev(&head->node.rb_node);
	if (!node)
		goto out;

	ref = rb_entry(node, struct btrfs_delayed_ref_node, rb_node);

	/* there are still entries for this ref, we can't drop it */
	if (ref->bytenr == bytenr)
		goto out;

	if (head->extent_op) {
		if (!head->must_insert_reserved)
			goto out;
		kfree(head->extent_op);
		head->extent_op = NULL;
	}

	/*
	 * waiting for the lock here would deadlock.  If someone else has it
	 * locked they are already in the process of dropping it anyway
	 */
	if (!mutex_trylock(&head->mutex))
		goto out;

	/*
	 * at this point we have a head with no other entries.  Go
	 * ahead and process it.
	 */
	head->node.in_tree = 0;
	rb_erase(&head->node.rb_node, &delayed_refs->root);

	delayed_refs->num_entries--;

	/*
	 * we don't take a ref on the node because we're removing it from the
	 * tree, so we just steal the ref the tree was holding.
	 */
	delayed_refs->num_heads--;
	if (list_empty(&head->cluster))
		delayed_refs->num_heads_ready--;

	list_del_init(&head->cluster);
	spin_unlock(&delayed_refs->lock);

	ret = run_one_delayed_ref(trans, root->fs_info->tree_root,
				  &head->node, head->extent_op,
				  head->must_insert_reserved);
	BUG_ON(ret);
	btrfs_put_delayed_ref(&head->node);
	return 0;
out:
	spin_unlock(&delayed_refs->lock);
	return 0;
}

int btrfs_free_extent(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root,
		      u64 bytenr, u64 num_bytes, u64 parent,
		      u64 root_objectid, u64 owner, u64 offset)
{
	int ret;

	/*
	 * tree log blocks never actually go into the extent allocation
	 * tree, just update pinning info and exit early.
	 */
	if (root_objectid == BTRFS_TREE_LOG_OBJECTID) {
		WARN_ON(owner >= BTRFS_FIRST_FREE_OBJECTID);
		/* unlocks the pinned mutex */
		btrfs_update_pinned_extents(root, bytenr, num_bytes, 1);
		update_reserved_extents(root, bytenr, num_bytes, 0);
		ret = 0;
	} else if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_add_delayed_tree_ref(trans, bytenr, num_bytes,
					parent, root_objectid, (int)owner,
					BTRFS_DROP_DELAYED_REF, NULL);
		BUG_ON(ret);
		ret = check_ref_cleanup(trans, root, bytenr);
		BUG_ON(ret);
	} else {
		ret = btrfs_add_delayed_data_ref(trans, bytenr, num_bytes,
					parent, root_objectid, owner,
					offset, BTRFS_DROP_DELAYED_REF, NULL);
		BUG_ON(ret);
	}
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
static noinline int find_free_extent(struct btrfs_trans_handle *trans,
				     struct btrfs_root *orig_root,
				     u64 num_bytes, u64 empty_size,
				     u64 search_start, u64 search_end,
				     u64 hint_byte, struct btrfs_key *ins,
				     u64 exclude_start, u64 exclude_nr,
				     int data)
{
	int ret = 0;
	struct btrfs_root *root = orig_root->fs_info->extent_root;
	struct btrfs_free_cluster *last_ptr = NULL;
	struct btrfs_block_group_cache *block_group = NULL;
	int empty_cluster = 2 * 1024 * 1024;
	int allowed_chunk_alloc = 0;
	struct btrfs_space_info *space_info;
	int last_ptr_loop = 0;
	int loop = 0;

	WARN_ON(num_bytes < root->sectorsize);
	btrfs_set_key_type(ins, BTRFS_EXTENT_ITEM_KEY);
	ins->objectid = 0;
	ins->offset = 0;

	space_info = __find_space_info(root->fs_info, data);

	if (orig_root->ref_cows || empty_size)
		allowed_chunk_alloc = 1;

	if (data & BTRFS_BLOCK_GROUP_METADATA) {
		last_ptr = &root->fs_info->meta_alloc_cluster;
		if (!btrfs_test_opt(root, SSD))
			empty_cluster = 64 * 1024;
	}

	if ((data & BTRFS_BLOCK_GROUP_DATA) && btrfs_test_opt(root, SSD)) {
		last_ptr = &root->fs_info->data_alloc_cluster;
	}

	if (last_ptr) {
		spin_lock(&last_ptr->lock);
		if (last_ptr->block_group)
			hint_byte = last_ptr->window_start;
		spin_unlock(&last_ptr->lock);
	}

	search_start = max(search_start, first_logical_byte(root, 0));
	search_start = max(search_start, hint_byte);

	if (!last_ptr) {
		empty_cluster = 0;
		loop = 1;
	}

	if (search_start == hint_byte) {
		block_group = btrfs_lookup_block_group(root->fs_info,
						       search_start);
		if (block_group && block_group_bits(block_group, data)) {
			down_read(&space_info->groups_sem);
			if (list_empty(&block_group->list) ||
			    block_group->ro) {
				/*
				 * someone is removing this block group,
				 * we can't jump into the have_block_group
				 * target because our list pointers are not
				 * valid
				 */
				btrfs_put_block_group(block_group);
				up_read(&space_info->groups_sem);
			} else
				goto have_block_group;
		} else if (block_group) {
			btrfs_put_block_group(block_group);
		}
	}

search:
	down_read(&space_info->groups_sem);
	list_for_each_entry(block_group, &space_info->block_groups, list) {
		u64 offset;

		atomic_inc(&block_group->count);
		search_start = block_group->key.objectid;

have_block_group:
		if (unlikely(!block_group->cached)) {
			mutex_lock(&block_group->cache_mutex);
			ret = cache_block_group(root, block_group);
			mutex_unlock(&block_group->cache_mutex);
			if (ret) {
				btrfs_put_block_group(block_group);
				break;
			}
		}

		if (unlikely(block_group->ro))
			goto loop;

		if (last_ptr) {
			/*
			 * the refill lock keeps out other
			 * people trying to start a new cluster
			 */
			spin_lock(&last_ptr->refill_lock);
			if (last_ptr->block_group &&
			    (last_ptr->block_group->ro ||
			    !block_group_bits(last_ptr->block_group, data))) {
				offset = 0;
				goto refill_cluster;
			}

			offset = btrfs_alloc_from_cluster(block_group, last_ptr,
						 num_bytes, search_start);
			if (offset) {
				/* we have a block, we're done */
				spin_unlock(&last_ptr->refill_lock);
				goto checks;
			}

			spin_lock(&last_ptr->lock);
			/*
			 * whoops, this cluster doesn't actually point to
			 * this block group.  Get a ref on the block
			 * group is does point to and try again
			 */
			if (!last_ptr_loop && last_ptr->block_group &&
			    last_ptr->block_group != block_group) {

				btrfs_put_block_group(block_group);
				block_group = last_ptr->block_group;
				atomic_inc(&block_group->count);
				spin_unlock(&last_ptr->lock);
				spin_unlock(&last_ptr->refill_lock);

				last_ptr_loop = 1;
				search_start = block_group->key.objectid;
				/*
				 * we know this block group is properly
				 * in the list because
				 * btrfs_remove_block_group, drops the
				 * cluster before it removes the block
				 * group from the list
				 */
				goto have_block_group;
			}
			spin_unlock(&last_ptr->lock);
refill_cluster:
			/*
			 * this cluster didn't work out, free it and
			 * start over
			 */
			btrfs_return_cluster_to_free_space(NULL, last_ptr);

			last_ptr_loop = 0;

			/* allocate a cluster in this block group */
			ret = btrfs_find_space_cluster(trans, root,
					       block_group, last_ptr,
					       offset, num_bytes,
					       empty_cluster + empty_size);
			if (ret == 0) {
				/*
				 * now pull our allocation out of this
				 * cluster
				 */
				offset = btrfs_alloc_from_cluster(block_group,
						  last_ptr, num_bytes,
						  search_start);
				if (offset) {
					/* we found one, proceed */
					spin_unlock(&last_ptr->refill_lock);
					goto checks;
				}
			}
			/*
			 * at this point we either didn't find a cluster
			 * or we weren't able to allocate a block from our
			 * cluster.  Free the cluster we've been trying
			 * to use, and go to the next block group
			 */
			if (loop < 2) {
				btrfs_return_cluster_to_free_space(NULL,
								   last_ptr);
				spin_unlock(&last_ptr->refill_lock);
				goto loop;
			}
			spin_unlock(&last_ptr->refill_lock);
		}

		offset = btrfs_find_space_for_alloc(block_group, search_start,
						    num_bytes, empty_size);
		if (!offset)
			goto loop;
checks:
		search_start = stripe_align(root, offset);

		/* move on to the next group */
		if (search_start + num_bytes >= search_end) {
			btrfs_add_free_space(block_group, offset, num_bytes);
			goto loop;
		}

		/* move on to the next group */
		if (search_start + num_bytes >
		    block_group->key.objectid + block_group->key.offset) {
			btrfs_add_free_space(block_group, offset, num_bytes);
			goto loop;
		}

		if (exclude_nr > 0 &&
		    (search_start + num_bytes > exclude_start &&
		     search_start < exclude_start + exclude_nr)) {
			search_start = exclude_start + exclude_nr;

			btrfs_add_free_space(block_group, offset, num_bytes);
			/*
			 * if search_start is still in this block group
			 * then we just re-search this block group
			 */
			if (search_start >= block_group->key.objectid &&
			    search_start < (block_group->key.objectid +
					    block_group->key.offset))
				goto have_block_group;
			goto loop;
		}

		ins->objectid = search_start;
		ins->offset = num_bytes;

		if (offset < search_start)
			btrfs_add_free_space(block_group, offset,
					     search_start - offset);
		BUG_ON(offset > search_start);

		/* we are all good, lets return */
		break;
loop:
		btrfs_put_block_group(block_group);
	}
	up_read(&space_info->groups_sem);

	/* loop == 0, try to find a clustered alloc in every block group
	 * loop == 1, try again after forcing a chunk allocation
	 * loop == 2, set empty_size and empty_cluster to 0 and try again
	 */
	if (!ins->objectid && loop < 3 &&
	    (empty_size || empty_cluster || allowed_chunk_alloc)) {
		if (loop >= 2) {
			empty_size = 0;
			empty_cluster = 0;
		}

		if (allowed_chunk_alloc) {
			ret = do_chunk_alloc(trans, root, num_bytes +
					     2 * 1024 * 1024, data, 1);
			allowed_chunk_alloc = 0;
		} else {
			space_info->force_alloc = 1;
		}

		if (loop < 3) {
			loop++;
			goto search;
		}
		ret = -ENOSPC;
	} else if (!ins->objectid) {
		ret = -ENOSPC;
	}

	/* we found what we needed */
	if (ins->objectid) {
		if (!(data & BTRFS_BLOCK_GROUP_DATA))
			trans->block_group = block_group->key.objectid;

		btrfs_put_block_group(block_group);
		ret = 0;
	}

	return ret;
}

static void dump_space_info(struct btrfs_space_info *info, u64 bytes)
{
	struct btrfs_block_group_cache *cache;

	printk(KERN_INFO "space_info has %llu free, is %sfull\n",
	       (unsigned long long)(info->total_bytes - info->bytes_used -
				    info->bytes_pinned - info->bytes_reserved),
	       (info->full) ? "" : "not ");
	printk(KERN_INFO "space_info total=%llu, pinned=%llu, delalloc=%llu,"
	       " may_use=%llu, used=%llu\n",
	       (unsigned long long)info->total_bytes,
	       (unsigned long long)info->bytes_pinned,
	       (unsigned long long)info->bytes_delalloc,
	       (unsigned long long)info->bytes_may_use,
	       (unsigned long long)info->bytes_used);

	down_read(&info->groups_sem);
	list_for_each_entry(cache, &info->block_groups, list) {
		spin_lock(&cache->lock);
		printk(KERN_INFO "block group %llu has %llu bytes, %llu used "
		       "%llu pinned %llu reserved\n",
		       (unsigned long long)cache->key.objectid,
		       (unsigned long long)cache->key.offset,
		       (unsigned long long)btrfs_block_group_used(&cache->item),
		       (unsigned long long)cache->pinned,
		       (unsigned long long)cache->reserved);
		btrfs_dump_free_space(cache, bytes);
		spin_unlock(&cache->lock);
	}
	up_read(&info->groups_sem);
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
	struct btrfs_fs_info *info = root->fs_info;

	data = btrfs_get_alloc_profile(root, data);
again:
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
		printk(KERN_ERR "btrfs allocation failed flags %llu, "
		       "wanted %llu\n", (unsigned long long)data,
		       (unsigned long long)num_bytes);
		dump_space_info(sinfo, num_bytes);
		BUG();
	}

	return ret;
}

int btrfs_free_reserved_extent(struct btrfs_root *root, u64 start, u64 len)
{
	struct btrfs_block_group_cache *cache;
	int ret = 0;

	cache = btrfs_lookup_block_group(root->fs_info, start);
	if (!cache) {
		printk(KERN_ERR "Unable to find block group for %llu\n",
		       (unsigned long long)start);
		return -ENOSPC;
	}

	ret = btrfs_discard_extent(root, start, len);

	btrfs_add_free_space(cache, start, len);
	btrfs_put_block_group(cache);
	update_reserved_extents(root, start, len, 0);

	return ret;
}

int btrfs_reserve_extent(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  u64 num_bytes, u64 min_alloc_size,
				  u64 empty_size, u64 hint_byte,
				  u64 search_end, struct btrfs_key *ins,
				  u64 data)
{
	int ret;
	ret = __btrfs_reserve_extent(trans, root, num_bytes, min_alloc_size,
				     empty_size, hint_byte, search_end, ins,
				     data);
	update_reserved_extents(root, ins->objectid, ins->offset, 1);
	return ret;
}

static int alloc_reserved_file_extent(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      u64 parent, u64 root_objectid,
				      u64 flags, u64 owner, u64 offset,
				      struct btrfs_key *ins, int ref_mod)
{
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_extent_item *extent_item;
	struct btrfs_extent_inline_ref *iref;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int type;
	u32 size;

	if (parent > 0)
		type = BTRFS_SHARED_DATA_REF_KEY;
	else
		type = BTRFS_EXTENT_DATA_REF_KEY;

	size = sizeof(*extent_item) + btrfs_extent_inline_ref_size(type);

	path = btrfs_alloc_path();
	BUG_ON(!path);

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(trans, fs_info->extent_root, path,
				      ins, size);
	BUG_ON(ret);

	leaf = path->nodes[0];
	extent_item = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_item);
	btrfs_set_extent_refs(leaf, extent_item, ref_mod);
	btrfs_set_extent_generation(leaf, extent_item, trans->transid);
	btrfs_set_extent_flags(leaf, extent_item,
			       flags | BTRFS_EXTENT_FLAG_DATA);

	iref = (struct btrfs_extent_inline_ref *)(extent_item + 1);
	btrfs_set_extent_inline_ref_type(leaf, iref, type);
	if (parent > 0) {
		struct btrfs_shared_data_ref *ref;
		ref = (struct btrfs_shared_data_ref *)(iref + 1);
		btrfs_set_extent_inline_ref_offset(leaf, iref, parent);
		btrfs_set_shared_data_ref_count(leaf, ref, ref_mod);
	} else {
		struct btrfs_extent_data_ref *ref;
		ref = (struct btrfs_extent_data_ref *)(&iref->offset);
		btrfs_set_extent_data_ref_root(leaf, ref, root_objectid);
		btrfs_set_extent_data_ref_objectid(leaf, ref, owner);
		btrfs_set_extent_data_ref_offset(leaf, ref, offset);
		btrfs_set_extent_data_ref_count(leaf, ref, ref_mod);
	}

	btrfs_mark_buffer_dirty(path->nodes[0]);
	btrfs_free_path(path);

	ret = update_block_group(trans, root, ins->objectid, ins->offset,
				 1, 0);
	if (ret) {
		printk(KERN_ERR "btrfs update block group failed for %llu "
		       "%llu\n", (unsigned long long)ins->objectid,
		       (unsigned long long)ins->offset);
		BUG();
	}
	return ret;
}

static int alloc_reserved_tree_block(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     u64 parent, u64 root_objectid,
				     u64 flags, struct btrfs_disk_key *key,
				     int level, struct btrfs_key *ins)
{
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_extent_item *extent_item;
	struct btrfs_tree_block_info *block_info;
	struct btrfs_extent_inline_ref *iref;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	u32 size = sizeof(*extent_item) + sizeof(*block_info) + sizeof(*iref);

	path = btrfs_alloc_path();
	BUG_ON(!path);

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(trans, fs_info->extent_root, path,
				      ins, size);
	BUG_ON(ret);

	leaf = path->nodes[0];
	extent_item = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_item);
	btrfs_set_extent_refs(leaf, extent_item, 1);
	btrfs_set_extent_generation(leaf, extent_item, trans->transid);
	btrfs_set_extent_flags(leaf, extent_item,
			       flags | BTRFS_EXTENT_FLAG_TREE_BLOCK);
	block_info = (struct btrfs_tree_block_info *)(extent_item + 1);

	btrfs_set_tree_block_key(leaf, block_info, key);
	btrfs_set_tree_block_level(leaf, block_info, level);

	iref = (struct btrfs_extent_inline_ref *)(block_info + 1);
	if (parent > 0) {
		BUG_ON(!(flags & BTRFS_BLOCK_FLAG_FULL_BACKREF));
		btrfs_set_extent_inline_ref_type(leaf, iref,
						 BTRFS_SHARED_BLOCK_REF_KEY);
		btrfs_set_extent_inline_ref_offset(leaf, iref, parent);
	} else {
		btrfs_set_extent_inline_ref_type(leaf, iref,
						 BTRFS_TREE_BLOCK_REF_KEY);
		btrfs_set_extent_inline_ref_offset(leaf, iref, root_objectid);
	}

	btrfs_mark_buffer_dirty(leaf);
	btrfs_free_path(path);

	ret = update_block_group(trans, root, ins->objectid, ins->offset,
				 1, 0);
	if (ret) {
		printk(KERN_ERR "btrfs update block group failed for %llu "
		       "%llu\n", (unsigned long long)ins->objectid,
		       (unsigned long long)ins->offset);
		BUG();
	}
	return ret;
}

int btrfs_alloc_reserved_file_extent(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     u64 root_objectid, u64 owner,
				     u64 offset, struct btrfs_key *ins)
{
	int ret;

	BUG_ON(root_objectid == BTRFS_TREE_LOG_OBJECTID);

	ret = btrfs_add_delayed_data_ref(trans, ins->objectid, ins->offset,
					 0, root_objectid, owner, offset,
					 BTRFS_ADD_DELAYED_EXTENT, NULL);
	return ret;
}

/*
 * this is used by the tree logging recovery code.  It records that
 * an extent has been allocated and makes sure to clear the free
 * space cache bits as well
 */
int btrfs_alloc_logged_file_extent(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   u64 root_objectid, u64 owner, u64 offset,
				   struct btrfs_key *ins)
{
	int ret;
	struct btrfs_block_group_cache *block_group;

	block_group = btrfs_lookup_block_group(root->fs_info, ins->objectid);
	mutex_lock(&block_group->cache_mutex);
	cache_block_group(root, block_group);
	mutex_unlock(&block_group->cache_mutex);

	ret = btrfs_remove_free_space(block_group, ins->objectid,
				      ins->offset);
	BUG_ON(ret);
	btrfs_put_block_group(block_group);
	ret = alloc_reserved_file_extent(trans, root, 0, root_objectid,
					 0, owner, offset, ins, 1);
	return ret;
}

/*
 * finds a free extent and does all the dirty work required for allocation
 * returns the key for the extent through ins, and a tree buffer for
 * the first block of the extent through buf.
 *
 * returns 0 if everything worked, non-zero otherwise.
 */
static int alloc_tree_block(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    u64 num_bytes, u64 parent, u64 root_objectid,
			    struct btrfs_disk_key *key, int level,
			    u64 empty_size, u64 hint_byte, u64 search_end,
			    struct btrfs_key *ins)
{
	int ret;
	u64 flags = 0;

	ret = __btrfs_reserve_extent(trans, root, num_bytes, num_bytes,
				     empty_size, hint_byte, search_end,
				     ins, 0);
	BUG_ON(ret);

	if (root_objectid == BTRFS_TREE_RELOC_OBJECTID) {
		if (parent == 0)
			parent = ins->objectid;
		flags |= BTRFS_BLOCK_FLAG_FULL_BACKREF;
	} else
		BUG_ON(parent > 0);

	update_reserved_extents(root, ins->objectid, ins->offset, 1);
	if (root_objectid != BTRFS_TREE_LOG_OBJECTID) {
		struct btrfs_delayed_extent_op *extent_op;
		extent_op = kmalloc(sizeof(*extent_op), GFP_NOFS);
		BUG_ON(!extent_op);
		if (key)
			memcpy(&extent_op->key, key, sizeof(extent_op->key));
		else
			memset(&extent_op->key, 0, sizeof(extent_op->key));
		extent_op->flags_to_set = flags;
		extent_op->update_key = 1;
		extent_op->update_flags = 1;
		extent_op->is_data = 0;

		ret = btrfs_add_delayed_tree_ref(trans, ins->objectid,
					ins->offset, parent, root_objectid,
					level, BTRFS_ADD_DELAYED_EXTENT,
					extent_op);
		BUG_ON(ret);
	}
	return ret;
}

struct extent_buffer *btrfs_init_new_buffer(struct btrfs_trans_handle *trans,
					    struct btrfs_root *root,
					    u64 bytenr, u32 blocksize,
					    int level)
{
	struct extent_buffer *buf;

	buf = btrfs_find_create_tree_block(root, bytenr, blocksize);
	if (!buf)
		return ERR_PTR(-ENOMEM);
	btrfs_set_header_generation(buf, trans->transid);
	btrfs_set_buffer_lockdep_class(buf, level);
	btrfs_tree_lock(buf);
	clean_tree_block(trans, root, buf);

	btrfs_set_lock_blocking(buf);
	btrfs_set_buffer_uptodate(buf);

	if (root->root_key.objectid == BTRFS_TREE_LOG_OBJECTID) {
		set_extent_dirty(&root->dirty_log_pages, buf->start,
			 buf->start + buf->len - 1, GFP_NOFS);
	} else {
		set_extent_dirty(&trans->transaction->dirty_pages, buf->start,
			 buf->start + buf->len - 1, GFP_NOFS);
	}
	trans->blocks_used++;
	/* this returns a buffer locked for blocking */
	return buf;
}

/*
 * helper function to allocate a block for a given tree
 * returns the tree buffer or NULL.
 */
struct extent_buffer *btrfs_alloc_free_block(struct btrfs_trans_handle *trans,
					struct btrfs_root *root, u32 blocksize,
					u64 parent, u64 root_objectid,
					struct btrfs_disk_key *key, int level,
					u64 hint, u64 empty_size)
{
	struct btrfs_key ins;
	int ret;
	struct extent_buffer *buf;

	ret = alloc_tree_block(trans, root, blocksize, parent, root_objectid,
			       key, level, empty_size, hint, (u64)-1, &ins);
	if (ret) {
		BUG_ON(ret > 0);
		return ERR_PTR(ret);
	}

	buf = btrfs_init_new_buffer(trans, root, ins.objectid,
				    blocksize, level);
	return buf;
}

#if 0
int btrfs_drop_leaf_ref(struct btrfs_trans_handle *trans,
			struct btrfs_root *root, struct extent_buffer *leaf)
{
	u64 disk_bytenr;
	u64 num_bytes;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	u32 nritems;
	int i;
	int ret;

	BUG_ON(!btrfs_is_leaf(leaf));
	nritems = btrfs_header_nritems(leaf);

	for (i = 0; i < nritems; i++) {
		cond_resched();
		btrfs_item_key_to_cpu(leaf, &key, i);

		/* only extents have references, skip everything else */
		if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY)
			continue;

		fi = btrfs_item_ptr(leaf, i, struct btrfs_file_extent_item);

		/* inline extents live in the btree, they don't have refs */
		if (btrfs_file_extent_type(leaf, fi) ==
		    BTRFS_FILE_EXTENT_INLINE)
			continue;

		disk_bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);

		/* holes don't have refs */
		if (disk_bytenr == 0)
			continue;

		num_bytes = btrfs_file_extent_disk_num_bytes(leaf, fi);
		ret = btrfs_free_extent(trans, root, disk_bytenr, num_bytes,
					leaf->start, 0, key.objectid, 0);
		BUG_ON(ret);
	}
	return 0;
}

static noinline int cache_drop_leaf_ref(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_leaf_ref *ref)
{
	int i;
	int ret;
	struct btrfs_extent_info *info;
	struct refsort *sorted;

	if (ref->nritems == 0)
		return 0;

	sorted = kmalloc(sizeof(*sorted) * ref->nritems, GFP_NOFS);
	for (i = 0; i < ref->nritems; i++) {
		sorted[i].bytenr = ref->extents[i].bytenr;
		sorted[i].slot = i;
	}
	sort(sorted, ref->nritems, sizeof(struct refsort), refsort_cmp, NULL);

	/*
	 * the items in the ref were sorted when the ref was inserted
	 * into the ref cache, so this is already in order
	 */
	for (i = 0; i < ref->nritems; i++) {
		info = ref->extents + sorted[i].slot;
		ret = btrfs_free_extent(trans, root, info->bytenr,
					  info->num_bytes, ref->bytenr,
					  ref->owner, ref->generation,
					  info->objectid, 0);

		atomic_inc(&root->fs_info->throttle_gen);
		wake_up(&root->fs_info->transaction_throttle);
		cond_resched();

		BUG_ON(ret);
		info++;
	}

	kfree(sorted);
	return 0;
}


static int drop_snap_lookup_refcount(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root, u64 start,
				     u64 len, u32 *refs)
{
	int ret;

	ret = btrfs_lookup_extent_refs(trans, root, start, len, refs);
	BUG_ON(ret);

#if 0 /* some debugging code in case we see problems here */
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
			printk(KERN_ERR "btrfs block %llu went down to one "
			       "during drop_snap\n", (unsigned long long)start);
		}

	}
#endif

	cond_resched();
	return ret;
}


/*
 * this is used while deleting old snapshots, and it drops the refs
 * on a whole subtree starting from a level 1 node.
 *
 * The idea is to sort all the leaf pointers, and then drop the
 * ref on all the leaves in order.  Most of the time the leaves
 * will have ref cache entries, so no leaf IOs will be required to
 * find the extents they have references on.
 *
 * For each leaf, any references it has are also dropped in order
 *
 * This ends up dropping the references in something close to optimal
 * order for reading and modifying the extent allocation tree.
 */
static noinline int drop_level_one_refs(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_path *path)
{
	u64 bytenr;
	u64 root_owner;
	u64 root_gen;
	struct extent_buffer *eb = path->nodes[1];
	struct extent_buffer *leaf;
	struct btrfs_leaf_ref *ref;
	struct refsort *sorted = NULL;
	int nritems = btrfs_header_nritems(eb);
	int ret;
	int i;
	int refi = 0;
	int slot = path->slots[1];
	u32 blocksize = btrfs_level_size(root, 0);
	u32 refs;

	if (nritems == 0)
		goto out;

	root_owner = btrfs_header_owner(eb);
	root_gen = btrfs_header_generation(eb);
	sorted = kmalloc(sizeof(*sorted) * nritems, GFP_NOFS);

	/*
	 * step one, sort all the leaf pointers so we don't scribble
	 * randomly into the extent allocation tree
	 */
	for (i = slot; i < nritems; i++) {
		sorted[refi].bytenr = btrfs_node_blockptr(eb, i);
		sorted[refi].slot = i;
		refi++;
	}

	/*
	 * nritems won't be zero, but if we're picking up drop_snapshot
	 * after a crash, slot might be > 0, so double check things
	 * just in case.
	 */
	if (refi == 0)
		goto out;

	sort(sorted, refi, sizeof(struct refsort), refsort_cmp, NULL);

	/*
	 * the first loop frees everything the leaves point to
	 */
	for (i = 0; i < refi; i++) {
		u64 ptr_gen;

		bytenr = sorted[i].bytenr;

		/*
		 * check the reference count on this leaf.  If it is > 1
		 * we just decrement it below and don't update any
		 * of the refs the leaf points to.
		 */
		ret = drop_snap_lookup_refcount(trans, root, bytenr,
						blocksize, &refs);
		BUG_ON(ret);
		if (refs != 1)
			continue;

		ptr_gen = btrfs_node_ptr_generation(eb, sorted[i].slot);

		/*
		 * the leaf only had one reference, which means the
		 * only thing pointing to this leaf is the snapshot
		 * we're deleting.  It isn't possible for the reference
		 * count to increase again later
		 *
		 * The reference cache is checked for the leaf,
		 * and if found we'll be able to drop any refs held by
		 * the leaf without needing to read it in.
		 */
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
		} else {
			/*
			 * the leaf wasn't in the reference cache, so
			 * we have to read it.
			 */
			leaf = read_tree_block(root, bytenr, blocksize,
					       ptr_gen);
			ret = btrfs_drop_leaf_ref(trans, root, leaf);
			BUG_ON(ret);
			free_extent_buffer(leaf);
		}
		atomic_inc(&root->fs_info->throttle_gen);
		wake_up(&root->fs_info->transaction_throttle);
		cond_resched();
	}

	/*
	 * run through the loop again to free the refs on the leaves.
	 * This is faster than doing it in the loop above because
	 * the leaves are likely to be clustered together.  We end up
	 * working in nice chunks on the extent allocation tree.
	 */
	for (i = 0; i < refi; i++) {
		bytenr = sorted[i].bytenr;
		ret = btrfs_free_extent(trans, root, bytenr,
					blocksize, eb->start,
					root_owner, root_gen, 0, 1);
		BUG_ON(ret);

		atomic_inc(&root->fs_info->throttle_gen);
		wake_up(&root->fs_info->transaction_throttle);
		cond_resched();
	}
out:
	kfree(sorted);

	/*
	 * update the path to show we've processed the entire level 1
	 * node.  This will get saved into the root's drop_snapshot_progress
	 * field so these drops are not repeated again if this transaction
	 * commits.
	 */
	path->slots[1] = nritems;
	return 0;
}

/*
 * helper function for drop_snapshot, this walks down the tree dropping ref
 * counts as it goes.
 */
static noinline int walk_down_tree(struct btrfs_trans_handle *trans,
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
	u32 blocksize;
	int ret;
	u32 refs;

	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);
	ret = drop_snap_lookup_refcount(trans, root, path->nodes[*level]->start,
				path->nodes[*level]->len, &refs);
	BUG_ON(ret);
	if (refs > 1)
		goto out;

	/*
	 * walk down to the last node level and free all the leaves
	 */
	while (*level >= 0) {
		WARN_ON(*level < 0);
		WARN_ON(*level >= BTRFS_MAX_LEVEL);
		cur = path->nodes[*level];

		if (btrfs_header_level(cur) != *level)
			WARN_ON(1);

		if (path->slots[*level] >=
		    btrfs_header_nritems(cur))
			break;

		/* the new code goes down to level 1 and does all the
		 * leaves pointed to that node in bulk.  So, this check
		 * for level 0 will always be false.
		 *
		 * But, the disk format allows the drop_snapshot_progress
		 * field in the root to leave things in a state where
		 * a leaf will need cleaning up here.  If someone crashes
		 * with the old code and then boots with the new code,
		 * we might find a leaf here.
		 */
		if (*level == 0) {
			ret = btrfs_drop_leaf_ref(trans, root, cur);
			BUG_ON(ret);
			break;
		}

		/*
		 * once we get to level one, process the whole node
		 * at once, including everything below it.
		 */
		if (*level == 1) {
			ret = drop_level_one_refs(trans, root, path);
			BUG_ON(ret);
			break;
		}

		bytenr = btrfs_node_blockptr(cur, path->slots[*level]);
		ptr_gen = btrfs_node_ptr_generation(cur, path->slots[*level]);
		blocksize = btrfs_level_size(root, *level - 1);

		ret = drop_snap_lookup_refcount(trans, root, bytenr,
						blocksize, &refs);
		BUG_ON(ret);

		/*
		 * if there is more than one reference, we don't need
		 * to read that node to drop any references it has.  We
		 * just drop the ref we hold on that node and move on to the
		 * next slot in this level.
		 */
		if (refs != 1) {
			parent = path->nodes[*level];
			root_owner = btrfs_header_owner(parent);
			root_gen = btrfs_header_generation(parent);
			path->slots[*level]++;

			ret = btrfs_free_extent(trans, root, bytenr,
						blocksize, parent->start,
						root_owner, root_gen,
						*level - 1, 1);
			BUG_ON(ret);

			atomic_inc(&root->fs_info->throttle_gen);
			wake_up(&root->fs_info->transaction_throttle);
			cond_resched();

			continue;
		}

		/*
		 * we need to keep freeing things in the next level down.
		 * read the block and loop around to process it
		 */
		next = read_tree_block(root, bytenr, blocksize, ptr_gen);
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

	/*
	 * cleanup and free the reference on the last node
	 * we processed
	 */
	ret = btrfs_free_extent(trans, root, bytenr, blocksize,
				  parent->start, root_owner, root_gen,
				  *level, 1);
	free_extent_buffer(path->nodes[*level]);
	path->nodes[*level] = NULL;

	*level += 1;
	BUG_ON(ret);

	cond_resched();
	return 0;
}
#endif

struct walk_control {
	u64 refs[BTRFS_MAX_LEVEL];
	u64 flags[BTRFS_MAX_LEVEL];
	struct btrfs_key update_progress;
	int stage;
	int level;
	int shared_level;
	int update_ref;
	int keep_locks;
};

#define DROP_REFERENCE	1
#define UPDATE_BACKREF	2

/*
 * hepler to process tree block while walking down the tree.
 *
 * when wc->stage == DROP_REFERENCE, this function checks
 * reference count of the block. if the block is shared and
 * we need update back refs for the subtree rooted at the
 * block, this function changes wc->stage to UPDATE_BACKREF
 *
 * when wc->stage == UPDATE_BACKREF, this function updates
 * back refs for pointers in the block.
 *
 * NOTE: return value 1 means we should stop walking down.
 */
static noinline int walk_down_proc(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct btrfs_path *path,
				   struct walk_control *wc)
{
	int level = wc->level;
	struct extent_buffer *eb = path->nodes[level];
	struct btrfs_key key;
	u64 flag = BTRFS_BLOCK_FLAG_FULL_BACKREF;
	int ret;

	if (wc->stage == UPDATE_BACKREF &&
	    btrfs_header_owner(eb) != root->root_key.objectid)
		return 1;

	/*
	 * when reference count of tree block is 1, it won't increase
	 * again. once full backref flag is set, we never clear it.
	 */
	if ((wc->stage == DROP_REFERENCE && wc->refs[level] != 1) ||
	    (wc->stage == UPDATE_BACKREF && !(wc->flags[level] & flag))) {
		BUG_ON(!path->locks[level]);
		ret = btrfs_lookup_extent_info(trans, root,
					       eb->start, eb->len,
					       &wc->refs[level],
					       &wc->flags[level]);
		BUG_ON(ret);
		BUG_ON(wc->refs[level] == 0);
	}

	if (wc->stage == DROP_REFERENCE &&
	    wc->update_ref && wc->refs[level] > 1) {
		BUG_ON(eb == root->node);
		BUG_ON(path->slots[level] > 0);
		if (level == 0)
			btrfs_item_key_to_cpu(eb, &key, path->slots[level]);
		else
			btrfs_node_key_to_cpu(eb, &key, path->slots[level]);
		if (btrfs_header_owner(eb) == root->root_key.objectid &&
		    btrfs_comp_cpu_keys(&key, &wc->update_progress) >= 0) {
			wc->stage = UPDATE_BACKREF;
			wc->shared_level = level;
		}
	}

	if (wc->stage == DROP_REFERENCE) {
		if (wc->refs[level] > 1)
			return 1;

		if (path->locks[level] && !wc->keep_locks) {
			btrfs_tree_unlock(eb);
			path->locks[level] = 0;
		}
		return 0;
	}

	/* wc->stage == UPDATE_BACKREF */
	if (!(wc->flags[level] & flag)) {
		BUG_ON(!path->locks[level]);
		ret = btrfs_inc_ref(trans, root, eb, 1);
		BUG_ON(ret);
		ret = btrfs_dec_ref(trans, root, eb, 0);
		BUG_ON(ret);
		ret = btrfs_set_disk_extent_flags(trans, root, eb->start,
						  eb->len, flag, 0);
		BUG_ON(ret);
		wc->flags[level] |= flag;
	}

	/*
	 * the block is shared by multiple trees, so it's not good to
	 * keep the tree lock
	 */
	if (path->locks[level] && level > 0) {
		btrfs_tree_unlock(eb);
		path->locks[level] = 0;
	}
	return 0;
}

/*
 * hepler to process tree block while walking up the tree.
 *
 * when wc->stage == DROP_REFERENCE, this function drops
 * reference count on the block.
 *
 * when wc->stage == UPDATE_BACKREF, this function changes
 * wc->stage back to DROP_REFERENCE if we changed wc->stage
 * to UPDATE_BACKREF previously while processing the block.
 *
 * NOTE: return value 1 means we should stop walking up.
 */
static noinline int walk_up_proc(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct walk_control *wc)
{
	int ret = 0;
	int level = wc->level;
	struct extent_buffer *eb = path->nodes[level];
	u64 parent = 0;

	if (wc->stage == UPDATE_BACKREF) {
		BUG_ON(wc->shared_level < level);
		if (level < wc->shared_level)
			goto out;

		BUG_ON(wc->refs[level] <= 1);
		ret = find_next_key(path, level + 1, &wc->update_progress);
		if (ret > 0)
			wc->update_ref = 0;

		wc->stage = DROP_REFERENCE;
		wc->shared_level = -1;
		path->slots[level] = 0;

		/*
		 * check reference count again if the block isn't locked.
		 * we should start walking down the tree again if reference
		 * count is one.
		 */
		if (!path->locks[level]) {
			BUG_ON(level == 0);
			btrfs_tree_lock(eb);
			btrfs_set_lock_blocking(eb);
			path->locks[level] = 1;

			ret = btrfs_lookup_extent_info(trans, root,
						       eb->start, eb->len,
						       &wc->refs[level],
						       &wc->flags[level]);
			BUG_ON(ret);
			BUG_ON(wc->refs[level] == 0);
			if (wc->refs[level] == 1) {
				btrfs_tree_unlock(eb);
				path->locks[level] = 0;
				return 1;
			}
		} else {
			BUG_ON(level != 0);
		}
	}

	/* wc->stage == DROP_REFERENCE */
	BUG_ON(wc->refs[level] > 1 && !path->locks[level]);

	if (wc->refs[level] == 1) {
		if (level == 0) {
			if (wc->flags[level] & BTRFS_BLOCK_FLAG_FULL_BACKREF)
				ret = btrfs_dec_ref(trans, root, eb, 1);
			else
				ret = btrfs_dec_ref(trans, root, eb, 0);
			BUG_ON(ret);
		}
		/* make block locked assertion in clean_tree_block happy */
		if (!path->locks[level] &&
		    btrfs_header_generation(eb) == trans->transid) {
			btrfs_tree_lock(eb);
			btrfs_set_lock_blocking(eb);
			path->locks[level] = 1;
		}
		clean_tree_block(trans, root, eb);
	}

	if (eb == root->node) {
		if (wc->flags[level] & BTRFS_BLOCK_FLAG_FULL_BACKREF)
			parent = eb->start;
		else
			BUG_ON(root->root_key.objectid !=
			       btrfs_header_owner(eb));
	} else {
		if (wc->flags[level + 1] & BTRFS_BLOCK_FLAG_FULL_BACKREF)
			parent = path->nodes[level + 1]->start;
		else
			BUG_ON(root->root_key.objectid !=
			       btrfs_header_owner(path->nodes[level + 1]));
	}

	ret = btrfs_free_extent(trans, root, eb->start, eb->len, parent,
				root->root_key.objectid, level, 0);
	BUG_ON(ret);
out:
	wc->refs[level] = 0;
	wc->flags[level] = 0;
	return ret;
}

static noinline int walk_down_tree(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct btrfs_path *path,
				   struct walk_control *wc)
{
	struct extent_buffer *next;
	struct extent_buffer *cur;
	u64 bytenr;
	u64 ptr_gen;
	u32 blocksize;
	int level = wc->level;
	int ret;

	while (level >= 0) {
		cur = path->nodes[level];
		BUG_ON(path->slots[level] >= btrfs_header_nritems(cur));

		ret = walk_down_proc(trans, root, path, wc);
		if (ret > 0)
			break;

		if (level == 0)
			break;

		bytenr = btrfs_node_blockptr(cur, path->slots[level]);
		blocksize = btrfs_level_size(root, level - 1);
		ptr_gen = btrfs_node_ptr_generation(cur, path->slots[level]);

		next = read_tree_block(root, bytenr, blocksize, ptr_gen);
		btrfs_tree_lock(next);
		btrfs_set_lock_blocking(next);

		level--;
		BUG_ON(level != btrfs_header_level(next));
		path->nodes[level] = next;
		path->slots[level] = 0;
		path->locks[level] = 1;
		wc->level = level;
	}
	return 0;
}

static noinline int walk_up_tree(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct walk_control *wc, int max_level)
{
	int level = wc->level;
	int ret;

	path->slots[level] = btrfs_header_nritems(path->nodes[level]);
	while (level < max_level && path->nodes[level]) {
		wc->level = level;
		if (path->slots[level] + 1 <
		    btrfs_header_nritems(path->nodes[level])) {
			path->slots[level]++;
			return 0;
		} else {
			ret = walk_up_proc(trans, root, path, wc);
			if (ret > 0)
				return 0;

			if (path->locks[level]) {
				btrfs_tree_unlock(path->nodes[level]);
				path->locks[level] = 0;
			}
			free_extent_buffer(path->nodes[level]);
			path->nodes[level] = NULL;
			level++;
		}
	}
	return 1;
}

/*
 * drop a subvolume tree.
 *
 * this function traverses the tree freeing any blocks that only
 * referenced by the tree.
 *
 * when a shared tree block is found. this function decreases its
 * reference count by one. if update_ref is true, this function
 * also make sure backrefs for the shared block and all lower level
 * blocks are properly updated.
 */
int btrfs_drop_snapshot(struct btrfs_root *root, int update_ref)
{
	struct btrfs_path *path;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *tree_root = root->fs_info->tree_root;
	struct btrfs_root_item *root_item = &root->root_item;
	struct walk_control *wc;
	struct btrfs_key key;
	int err = 0;
	int ret;
	int level;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	wc = kzalloc(sizeof(*wc), GFP_NOFS);
	BUG_ON(!wc);

	trans = btrfs_start_transaction(tree_root, 1);

	if (btrfs_disk_key_objectid(&root_item->drop_progress) == 0) {
		level = btrfs_header_level(root->node);
		path->nodes[level] = btrfs_lock_root_node(root);
		btrfs_set_lock_blocking(path->nodes[level]);
		path->slots[level] = 0;
		path->locks[level] = 1;
		memset(&wc->update_progress, 0,
		       sizeof(wc->update_progress));
	} else {
		btrfs_disk_key_to_cpu(&key, &root_item->drop_progress);
		memcpy(&wc->update_progress, &key,
		       sizeof(wc->update_progress));

		level = root_item->drop_level;
		BUG_ON(level == 0);
		path->lowest_level = level;
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		path->lowest_level = 0;
		if (ret < 0) {
			err = ret;
			goto out;
		}
		btrfs_node_key_to_cpu(path->nodes[level], &key,
				      path->slots[level]);
		WARN_ON(memcmp(&key, &wc->update_progress, sizeof(key)));

		/*
		 * unlock our path, this is safe because only this
		 * function is allowed to delete this snapshot
		 */
		btrfs_unlock_up_safe(path, 0);

		level = btrfs_header_level(root->node);
		while (1) {
			btrfs_tree_lock(path->nodes[level]);
			btrfs_set_lock_blocking(path->nodes[level]);

			ret = btrfs_lookup_extent_info(trans, root,
						path->nodes[level]->start,
						path->nodes[level]->len,
						&wc->refs[level],
						&wc->flags[level]);
			BUG_ON(ret);
			BUG_ON(wc->refs[level] == 0);

			if (level == root_item->drop_level)
				break;

			btrfs_tree_unlock(path->nodes[level]);
			WARN_ON(wc->refs[level] != 1);
			level--;
		}
	}

	wc->level = level;
	wc->shared_level = -1;
	wc->stage = DROP_REFERENCE;
	wc->update_ref = update_ref;
	wc->keep_locks = 0;

	while (1) {
		ret = walk_down_tree(trans, root, path, wc);
		if (ret < 0) {
			err = ret;
			break;
		}

		ret = walk_up_tree(trans, root, path, wc, BTRFS_MAX_LEVEL);
		if (ret < 0) {
			err = ret;
			break;
		}

		if (ret > 0) {
			BUG_ON(wc->stage != DROP_REFERENCE);
			break;
		}

		if (wc->stage == DROP_REFERENCE) {
			level = wc->level;
			btrfs_node_key(path->nodes[level],
				       &root_item->drop_progress,
				       path->slots[level]);
			root_item->drop_level = level;
		}

		BUG_ON(wc->level == 0);
		if (trans->transaction->in_commit ||
		    trans->transaction->delayed_refs.flushing) {
			ret = btrfs_update_root(trans, tree_root,
						&root->root_key,
						root_item);
			BUG_ON(ret);

			btrfs_end_transaction(trans, tree_root);
			trans = btrfs_start_transaction(tree_root, 1);
		} else {
			unsigned long update;
			update = trans->delayed_ref_updates;
			trans->delayed_ref_updates = 0;
			if (update)
				btrfs_run_delayed_refs(trans, tree_root,
						       update);
		}
	}
	btrfs_release_path(root, path);
	BUG_ON(err);

	ret = btrfs_del_root(trans, tree_root, &root->root_key);
	BUG_ON(ret);

	free_extent_buffer(root->node);
	free_extent_buffer(root->commit_root);
	kfree(root);
out:
	btrfs_end_transaction(trans, tree_root);
	kfree(wc);
	btrfs_free_path(path);
	return err;
}

/*
 * drop subtree rooted at tree block 'node'.
 *
 * NOTE: this function will unlock and release tree block 'node'
 */
int btrfs_drop_subtree(struct btrfs_trans_handle *trans,
			struct btrfs_root *root,
			struct extent_buffer *node,
			struct extent_buffer *parent)
{
	struct btrfs_path *path;
	struct walk_control *wc;
	int level;
	int parent_level;
	int ret = 0;
	int wret;

	BUG_ON(root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID);

	path = btrfs_alloc_path();
	BUG_ON(!path);

	wc = kzalloc(sizeof(*wc), GFP_NOFS);
	BUG_ON(!wc);

	btrfs_assert_tree_locked(parent);
	parent_level = btrfs_header_level(parent);
	extent_buffer_get(parent);
	path->nodes[parent_level] = parent;
	path->slots[parent_level] = btrfs_header_nritems(parent);

	btrfs_assert_tree_locked(node);
	level = btrfs_header_level(node);
	path->nodes[level] = node;
	path->slots[level] = 0;
	path->locks[level] = 1;

	wc->refs[parent_level] = 1;
	wc->flags[parent_level] = BTRFS_BLOCK_FLAG_FULL_BACKREF;
	wc->level = level;
	wc->shared_level = -1;
	wc->stage = DROP_REFERENCE;
	wc->update_ref = 0;
	wc->keep_locks = 1;

	while (1) {
		wret = walk_down_tree(trans, root, path, wc);
		if (wret < 0) {
			ret = wret;
			break;
		}

		wret = walk_up_tree(trans, root, path, wc, parent_level);
		if (wret < 0)
			ret = wret;
		if (wret != 0)
			break;
	}

	kfree(wc);
	btrfs_free_path(path);
	return ret;
}

#if 0
static unsigned long calc_ra(unsigned long start, unsigned long last,
			     unsigned long nr)
{
	return min(last, start + nr - 1);
}

static noinline int relocate_inode_pages(struct inode *inode, u64 start,
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

		if (i == first_index)
			set_extent_bits(io_tree, page_start, page_end,
					EXTENT_BOUNDARY, GFP_NOFS);
		btrfs_set_extent_delalloc(inode, page_start, page_end);

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

static noinline int relocate_data_extent(struct inode *reloc_inode,
					 struct btrfs_key *extent_key,
					 u64 offset)
{
	struct btrfs_root *root = BTRFS_I(reloc_inode)->root;
	struct extent_map_tree *em_tree = &BTRFS_I(reloc_inode)->extent_tree;
	struct extent_map *em;
	u64 start = extent_key->objectid - offset;
	u64 end = start + extent_key->offset - 1;

	em = alloc_extent_map(GFP_NOFS);
	BUG_ON(!em || IS_ERR(em));

	em->start = start;
	em->len = extent_key->offset;
	em->block_len = extent_key->offset;
	em->block_start = extent_key->objectid;
	em->bdev = root->fs_info->fs_devices->latest_bdev;
	set_bit(EXTENT_FLAG_PINNED, &em->flags);

	/* setup extent map to cheat btrfs_readpage */
	lock_extent(&BTRFS_I(reloc_inode)->io_tree, start, end, GFP_NOFS);
	while (1) {
		int ret;
		spin_lock(&em_tree->lock);
		ret = add_extent_mapping(em_tree, em);
		spin_unlock(&em_tree->lock);
		if (ret != -EEXIST) {
			free_extent_map(em);
			break;
		}
		btrfs_drop_extent_cache(reloc_inode, start, end, 0);
	}
	unlock_extent(&BTRFS_I(reloc_inode)->io_tree, start, end, GFP_NOFS);

	return relocate_inode_pages(reloc_inode, start, extent_key->offset);
}

struct btrfs_ref_path {
	u64 extent_start;
	u64 nodes[BTRFS_MAX_LEVEL];
	u64 root_objectid;
	u64 root_generation;
	u64 owner_objectid;
	u32 num_refs;
	int lowest_level;
	int current_level;
	int shared_level;

	struct btrfs_key node_keys[BTRFS_MAX_LEVEL];
	u64 new_nodes[BTRFS_MAX_LEVEL];
};

struct disk_extent {
	u64 ram_bytes;
	u64 disk_bytenr;
	u64 disk_num_bytes;
	u64 offset;
	u64 num_bytes;
	u8 compression;
	u8 encryption;
	u16 other_encoding;
};

static int is_cowonly_root(u64 root_objectid)
{
	if (root_objectid == BTRFS_ROOT_TREE_OBJECTID ||
	    root_objectid == BTRFS_EXTENT_TREE_OBJECTID ||
	    root_objectid == BTRFS_CHUNK_TREE_OBJECTID ||
	    root_objectid == BTRFS_DEV_TREE_OBJECTID ||
	    root_objectid == BTRFS_TREE_LOG_OBJECTID ||
	    root_objectid == BTRFS_CSUM_TREE_OBJECTID)
		return 1;
	return 0;
}

static noinline int __next_ref_path(struct btrfs_trans_handle *trans,
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

	if (first_time) {
		ref_path->lowest_level = -1;
		ref_path->current_level = -1;
		ref_path->shared_level = -1;
		goto walk_up;
	}
walk_down:
	level = ref_path->current_level - 1;
	while (level >= -1) {
		u64 parent;
		if (level < ref_path->lowest_level)
			break;

		if (level >= 0)
			bytenr = ref_path->nodes[level];
		else
			bytenr = ref_path->extent_start;
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
		    found_key.type == BTRFS_EXTENT_REF_KEY) {
			if (level < ref_path->shared_level)
				ref_path->shared_level = level;
			goto found;
		}
next:
		level--;
		btrfs_release_path(extent_root, path);
		cond_resched();
	}
	/* reached lowest level */
	ret = 1;
	goto out;
walk_up:
	level = ref_path->current_level;
	while (level < BTRFS_MAX_LEVEL - 1) {
		u64 ref_objectid;

		if (level >= 0)
			bytenr = ref_path->nodes[level];
		else
			bytenr = ref_path->extent_start;

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
		cond_resched();
	}
	/* reached max tree level, but no tree root found. */
	BUG();
out:
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

static noinline int get_new_locations(struct inode *reloc_inode,
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
		exts[nr].ram_bytes = btrfs_file_extent_ram_bytes(leaf, fi);
		exts[nr].compression = btrfs_file_extent_compression(leaf, fi);
		exts[nr].encryption = btrfs_file_extent_encryption(leaf, fi);
		exts[nr].other_encoding = btrfs_file_extent_other_encoding(leaf,
									   fi);
		BUG_ON(exts[nr].offset > 0);
		BUG_ON(exts[nr].compression || exts[nr].encryption);
		BUG_ON(exts[nr].num_bytes != exts[nr].disk_num_bytes);

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

	BUG_ON(cur_pos + offset > last_byte);
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

static noinline int replace_one_extent(struct btrfs_trans_handle *trans,
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
	u64 search_end = (u64)-1;
	u32 nritems;
	int nr_scaned = 0;
	int extent_locked = 0;
	int extent_type;
	int ret;

	memcpy(&key, leaf_key, sizeof(key));
	if (ref_path->owner_objectid != BTRFS_MULTIPLE_OBJECTIDS) {
		if (key.objectid < ref_path->owner_objectid ||
		    (key.objectid == ref_path->owner_objectid &&
		     key.type < BTRFS_EXTENT_DATA_KEY)) {
			key.objectid = ref_path->owner_objectid;
			key.type = BTRFS_EXTENT_DATA_KEY;
			key.offset = 0;
		}
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
			unlock_extent(&BTRFS_I(inode)->io_tree, lock_start,
				      lock_end, GFP_NOFS);
			extent_locked = 0;
		}

		if (path->slots[0] >= nritems) {
			if (++nr_scaned > 2)
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
			    key.offset >= search_end)
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
		extent_type = btrfs_file_extent_type(leaf, fi);
		if ((extent_type != BTRFS_FILE_EXTENT_REG &&
		     extent_type != BTRFS_FILE_EXTENT_PREALLOC) ||
		    (btrfs_file_extent_disk_bytenr(leaf, fi) !=
		     extent_key->objectid)) {
			path->slots[0]++;
			ret = 1;
			goto next;
		}

		num_bytes = btrfs_file_extent_num_bytes(leaf, fi);
		ext_offset = btrfs_file_extent_offset(leaf, fi);

		if (search_end == (u64)-1) {
			search_end = key.offset - ext_offset +
				btrfs_file_extent_ram_bytes(leaf, fi);
		}

		if (!extent_locked) {
			lock_start = key.offset;
			lock_end = lock_start + num_bytes - 1;
		} else {
			if (lock_start > key.offset ||
			    lock_end + 1 < key.offset + num_bytes) {
				unlock_extent(&BTRFS_I(inode)->io_tree,
					      lock_start, lock_end, GFP_NOFS);
				extent_locked = 0;
			}
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

			extent_locked = 1;
			continue;
		}

		if (nr_extents == 1) {
			/* update extent pointer in place */
			btrfs_set_file_extent_disk_bytenr(leaf, fi,
						new_extents[0].disk_bytenr);
			btrfs_set_file_extent_disk_num_bytes(leaf, fi,
						new_extents[0].disk_num_bytes);
			btrfs_mark_buffer_dirty(leaf);

			btrfs_drop_extent_cache(inode, key.offset,
						key.offset + num_bytes - 1, 0);

			ret = btrfs_inc_extent_ref(trans, root,
						new_extents[0].disk_bytenr,
						new_extents[0].disk_num_bytes,
						leaf->start,
						root->root_key.objectid,
						trans->transid,
						key.objectid);
			BUG_ON(ret);

			ret = btrfs_free_extent(trans, root,
						extent_key->objectid,
						extent_key->offset,
						leaf->start,
						btrfs_header_owner(leaf),
						btrfs_header_generation(leaf),
						key.objectid, 0);
			BUG_ON(ret);

			btrfs_release_path(root, path);
			key.offset += num_bytes;
		} else {
			BUG_ON(1);
#if 0
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
				btrfs_set_file_extent_ram_bytes(leaf, fi,
						new_extents[i].ram_bytes);

				btrfs_set_file_extent_compression(leaf, fi,
						new_extents[i].compression);
				btrfs_set_file_extent_encryption(leaf, fi,
						new_extents[i].encryption);
				btrfs_set_file_extent_other_encoding(leaf, fi,
						new_extents[i].other_encoding);

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
						trans->transid, key.objectid);
				BUG_ON(ret);
				btrfs_release_path(root, path);

				inode_add_bytes(inode, extent_len);

				ext_offset = 0;
				num_bytes -= extent_len;
				key.offset += extent_len;

				if (num_bytes == 0)
					break;
			}
			BUG_ON(i >= nr_extents);
#endif
		}

		if (extent_locked) {
			unlock_extent(&BTRFS_I(inode)->io_tree, lock_start,
				      lock_end, GFP_NOFS);
			extent_locked = 0;
		}
skip:
		if (ref_path->owner_objectid != BTRFS_MULTIPLE_OBJECTIDS &&
		    key.offset >= search_end)
			break;

		cond_resched();
	}
	ret = 0;
out:
	btrfs_release_path(root, path);
	if (inode) {
		mutex_unlock(&inode->i_mutex);
		if (extent_locked) {
			unlock_extent(&BTRFS_I(inode)->io_tree, lock_start,
				      lock_end, GFP_NOFS);
		}
		iput(inode);
	}
	return ret;
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

static noinline int invalidate_extent_cache(struct btrfs_root *root,
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
		btrfs_drop_extent_cache(inode, key.offset,
					key.offset + num_bytes - 1, 1);
		unlock_extent(&BTRFS_I(inode)->io_tree, key.offset,
			      key.offset + num_bytes - 1, GFP_NOFS);
		cond_resched();
	}
	iput(inode);
	return 0;
}

static noinline int replace_extents_in_leaf(struct btrfs_trans_handle *trans,
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

		btrfs_set_file_extent_disk_bytenr(leaf, fi,
						new_extent->disk_bytenr);
		btrfs_set_file_extent_disk_num_bytes(leaf, fi,
						new_extent->disk_num_bytes);
		btrfs_mark_buffer_dirty(leaf);

		ret = btrfs_inc_extent_ref(trans, root,
					new_extent->disk_bytenr,
					new_extent->disk_num_bytes,
					leaf->start,
					root->root_key.objectid,
					trans->transid, key.objectid);
		BUG_ON(ret);

		ret = btrfs_free_extent(trans, root,
					bytenr, num_bytes, leaf->start,
					btrfs_header_owner(leaf),
					btrfs_header_generation(leaf),
					key.objectid, 0);
		BUG_ON(ret);
		cond_resched();
	}
	kfree(new_extent);
	BUG_ON(ext_index + 1 != ref->nritems);
	btrfs_free_leaf_ref(root, ref);
	return 0;
}

int btrfs_free_reloc_root(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root)
{
	struct btrfs_root *reloc_root;
	int ret;

	if (root->reloc_root) {
		reloc_root = root->reloc_root;
		root->reloc_root = NULL;
		list_add(&reloc_root->dead_list,
			 &root->fs_info->dead_reloc_roots);

		btrfs_set_root_bytenr(&reloc_root->root_item,
				      reloc_root->node->start);
		btrfs_set_root_level(&root->root_item,
				     btrfs_header_level(reloc_root->node));
		memset(&reloc_root->root_item.drop_progress, 0,
			sizeof(struct btrfs_disk_key));
		reloc_root->root_item.drop_level = 0;

		ret = btrfs_update_root(trans, root->fs_info->tree_root,
					&reloc_root->root_key,
					&reloc_root->root_item);
		BUG_ON(ret);
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

static noinline int init_reloc_tree(struct btrfs_trans_handle *trans,
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
	btrfs_set_root_generation(root_item, trans->transid);

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
 * counted roots. There is one reloc tree for each subvol, and all
 * reloc trees share same root key objectid. Reloc trees are snapshots
 * of the latest committed roots of subvols (root->commit_root).
 *
 * To relocate a tree block referenced by a subvol, there are two steps.
 * COW the block through subvol's reloc tree, then update block pointer
 * in the subvol to point to the new block. Since all reloc trees share
 * same root key objectid, doing special handing for tree blocks owned
 * by them is easy. Once a tree block has been COWed in one reloc tree,
 * we can use the resulting new block directly when the same block is
 * required to COW again through other reloc trees. By this way, relocated
 * tree blocks are shared between reloc trees, so they are also shared
 * between subvols.
 */
static noinline int relocate_one_path(struct btrfs_trans_handle *trans,
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
	int shared_level;
	int lowest_level = 0;
	int ret;

	if (ref_path->owner_objectid < BTRFS_FIRST_FREE_OBJECTID)
		lowest_level = ref_path->owner_objectid;

	if (!root->ref_cows) {
		path->lowest_level = lowest_level;
		ret = btrfs_search_slot(trans, root, first_key, path, 0, 1);
		BUG_ON(ret < 0);
		path->lowest_level = 0;
		btrfs_release_path(root, path);
		return 0;
	}

	mutex_lock(&root->fs_info->tree_reloc_mutex);
	ret = init_reloc_tree(trans, root);
	BUG_ON(ret);
	reloc_root = root->reloc_root;

	shared_level = ref_path->shared_level;
	ref_path->shared_level = BTRFS_MAX_LEVEL - 1;

	keys = ref_path->node_keys;
	nodes = ref_path->new_nodes;
	memset(&keys[shared_level + 1], 0,
	       sizeof(*keys) * (BTRFS_MAX_LEVEL - shared_level - 1));
	memset(&nodes[shared_level + 1], 0,
	       sizeof(*nodes) * (BTRFS_MAX_LEVEL - shared_level - 1));

	if (nodes[lowest_level] == 0) {
		path->lowest_level = lowest_level;
		ret = btrfs_search_slot(trans, reloc_root, first_key, path,
					0, 1);
		BUG_ON(ret);
		for (level = lowest_level; level < BTRFS_MAX_LEVEL; level++) {
			eb = path->nodes[level];
			if (!eb || eb == reloc_root->node)
				break;
			nodes[level] = eb->start;
			if (level == 0)
				btrfs_item_key_to_cpu(eb, &keys[level], 0);
			else
				btrfs_node_key_to_cpu(eb, &keys[level], 0);
		}
		if (nodes[0] &&
		    ref_path->owner_objectid >= BTRFS_FIRST_FREE_OBJECTID) {
			eb = path->nodes[0];
			ret = replace_extents_in_leaf(trans, reloc_root, eb,
						      group, reloc_inode);
			BUG_ON(ret);
		}
		btrfs_release_path(reloc_root, path);
	} else {
		ret = btrfs_merge_path(trans, reloc_root, keys, nodes,
				       lowest_level);
		BUG_ON(ret);
	}

	/*
	 * replace tree blocks in the fs tree with tree blocks in
	 * the reloc tree.
	 */
	ret = btrfs_merge_path(trans, root, keys, nodes, lowest_level);
	BUG_ON(ret < 0);

	if (ref_path->owner_objectid >= BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_search_slot(trans, reloc_root, first_key, path,
					0, 0);
		BUG_ON(ret);
		extent_buffer_get(path->nodes[0]);
		eb = path->nodes[0];
		btrfs_release_path(reloc_root, path);
		ret = invalidate_extent_cache(reloc_root, eb, group, root);
		BUG_ON(ret);
		free_extent_buffer(eb);
	}

	mutex_unlock(&root->fs_info->tree_reloc_mutex);
	path->lowest_level = 0;
	return 0;
}

static noinline int relocate_tree_block(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_path *path,
					struct btrfs_key *first_key,
					struct btrfs_ref_path *ref_path)
{
	int ret;

	ret = relocate_one_path(trans, root, path, first_key,
				ref_path, NULL, NULL);
	BUG_ON(ret);

	return 0;
}

static noinline int del_extent_zero(struct btrfs_trans_handle *trans,
				    struct btrfs_root *extent_root,
				    struct btrfs_path *path,
				    struct btrfs_key *extent_key)
{
	int ret;

	ret = btrfs_search_slot(trans, extent_root, extent_key, path, -1, 1);
	if (ret)
		goto out;
	ret = btrfs_del_item(trans, extent_root, path);
out:
	btrfs_release_path(extent_root, path);
	return ret;
}

static noinline struct btrfs_root *read_ref_root(struct btrfs_fs_info *fs_info,
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

static noinline int relocate_one_extent(struct btrfs_root *extent_root,
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

		mutex_lock(&extent_root->fs_info->trans_mutex);
		btrfs_record_root_in_trans(found_root);
		mutex_unlock(&extent_root->fs_info->trans_mutex);
		if (ref_path->owner_objectid >= BTRFS_FIRST_FREE_OBJECTID) {
			/*
			 * try to update data extent references while
			 * keeping metadata shared between snapshots.
			 */
			if (pass == 1) {
				ret = relocate_one_path(trans, found_root,
						path, &first_key, ref_path,
						group, reloc_inode);
				if (ret < 0)
					goto out;
				continue;
			}
			/*
			 * use fallback method to process the remaining
			 * references.
			 */
			if (!new_extents) {
				u64 group_start = group->key.objectid;
				new_extents = kmalloc(sizeof(*new_extents),
						      GFP_NOFS);
				nr_extents = 1;
				ret = get_new_locations(reloc_inode,
							extent_key,
							group_start, 1,
							&new_extents,
							&nr_extents);
				if (ret)
					goto out;
			}
			ret = replace_one_extent(trans, found_root,
						path, extent_key,
						&first_key, ref_path,
						new_extents, nr_extents);
		} else {
			ret = relocate_tree_block(trans, found_root, path,
						  &first_key, ref_path);
		}
		if (ret < 0)
			goto out;
	}
	ret = 0;
out:
	btrfs_end_transaction(trans, extent_root);
	kfree(new_extents);
	kfree(ref_path);
	return ret;
}
#endif

static u64 update_block_group_flags(struct btrfs_root *root, u64 flags)
{
	u64 num_devices;
	u64 stripped = BTRFS_BLOCK_GROUP_RAID0 |
		BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_RAID10;

	num_devices = root->fs_info->fs_devices->rw_devices;
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

static int __alloc_chunk_for_shrink(struct btrfs_root *root,
		     struct btrfs_block_group_cache *shrink_block_group,
		     int force)
{
	struct btrfs_trans_handle *trans;
	u64 new_alloc_flags;
	u64 calc;

	spin_lock(&shrink_block_group->lock);
	if (btrfs_block_group_used(&shrink_block_group->item) +
	    shrink_block_group->reserved > 0) {
		spin_unlock(&shrink_block_group->lock);

		trans = btrfs_start_transaction(root, 1);
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

		btrfs_end_transaction(trans, root);
	} else
		spin_unlock(&shrink_block_group->lock);
	return 0;
}


int btrfs_prepare_block_group_relocation(struct btrfs_root *root,
					 struct btrfs_block_group_cache *group)

{
	__alloc_chunk_for_shrink(root, group, 1);
	set_block_group_readonly(group);
	return 0;
}

#if 0
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

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_inode(trans, root, path, objectid);
	if (ret)
		goto out;

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_inode_item);
	memset_extent_buffer(leaf, 0, (unsigned long)item, sizeof(*item));
	btrfs_set_inode_generation(leaf, item, 1);
	btrfs_set_inode_size(leaf, item, size);
	btrfs_set_inode_mode(leaf, item, S_IFREG | 0600);
	btrfs_set_inode_flags(leaf, item, BTRFS_INODE_NOCOMPRESS);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(root, path);
out:
	btrfs_free_path(path);
	return ret;
}

static noinline struct inode *create_reloc_inode(struct btrfs_fs_info *fs_info,
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
				       group->key.offset, 0, group->key.offset,
				       0, 0, 0);
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
	BTRFS_I(inode)->index_cnt = group->key.objectid;

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

int btrfs_reloc_clone_csums(struct inode *inode, u64 file_pos, u64 len)
{

	struct btrfs_ordered_sum *sums;
	struct btrfs_sector_sum *sector_sum;
	struct btrfs_ordered_extent *ordered;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct list_head list;
	size_t offset;
	int ret;
	u64 disk_bytenr;

	INIT_LIST_HEAD(&list);

	ordered = btrfs_lookup_ordered_extent(inode, file_pos);
	BUG_ON(ordered->file_offset != file_pos || ordered->len != len);

	disk_bytenr = file_pos + BTRFS_I(inode)->index_cnt;
	ret = btrfs_lookup_csums_range(root->fs_info->csum_root, disk_bytenr,
				       disk_bytenr + len - 1, &list);

	while (!list_empty(&list)) {
		sums = list_entry(list.next, struct btrfs_ordered_sum, list);
		list_del_init(&sums->list);

		sector_sum = sums->sums;
		sums->bytenr = ordered->start;

		offset = 0;
		while (offset < sums->len) {
			sector_sum->bytenr += ordered->start - disk_bytenr;
			sector_sum++;
			offset += root->sectorsize;
		}

		btrfs_add_ordered_sum(inode, ordered, sums);
	}
	btrfs_put_ordered_extent(ordered);
	return 0;
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
	u64 skipped;
	u64 cur_byte;
	u64 total_found;
	u32 nritems;
	int ret;
	int progress;
	int pass = 0;

	root = root->fs_info->extent_root;

	block_group = btrfs_lookup_block_group(info, group_start);
	BUG_ON(!block_group);

	printk(KERN_INFO "btrfs relocating block group %llu flags %llu\n",
	       (unsigned long long)block_group->key.objectid,
	       (unsigned long long)block_group->flags);

	path = btrfs_alloc_path();
	BUG_ON(!path);

	reloc_inode = create_reloc_inode(info, block_group);
	BUG_ON(IS_ERR(reloc_inode));

	__alloc_chunk_for_shrink(root, block_group, 1);
	set_block_group_readonly(block_group);

	btrfs_start_delalloc_inodes(info->tree_root);
	btrfs_wait_ordered_extents(info->tree_root, 0);
again:
	skipped = 0;
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

	trans = btrfs_start_transaction(info->tree_root, 1);
	btrfs_commit_transaction(trans, info->tree_root);

	while (1) {
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
			cond_resched();
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
		if (ret > 0)
			skipped++;

		key.objectid = cur_byte;
		key.type = 0;
		key.offset = 0;
	}

	btrfs_release_path(root, path);

	if (pass == 0) {
		btrfs_wait_ordered_range(reloc_inode, 0, (u64)-1);
		invalidate_mapping_pages(reloc_inode->i_mapping, 0, -1);
	}

	if (total_found > 0) {
		printk(KERN_INFO "btrfs found %llu extents in pass %d\n",
		       (unsigned long long)total_found, pass);
		pass++;
		if (total_found == skipped && pass > 2) {
			iput(reloc_inode);
			reloc_inode = create_reloc_inode(info, block_group);
			pass = 0;
		}
		goto again;
	}

	/* delete reloc_inode */
	iput(reloc_inode);

	/* unpin extents in this range */
	trans = btrfs_start_transaction(info->tree_root, 1);
	btrfs_commit_transaction(trans, info->tree_root);

	spin_lock(&block_group->lock);
	WARN_ON(block_group->pinned > 0);
	WARN_ON(block_group->reserved > 0);
	WARN_ON(btrfs_block_group_used(&block_group->item) > 0);
	spin_unlock(&block_group->lock);
	btrfs_put_block_group(block_group);
	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}
#endif

static int find_first_block_group(struct btrfs_root *root,
		struct btrfs_path *path, struct btrfs_key *key)
{
	int ret = 0;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	int slot;

	ret = btrfs_search_slot(NULL, root, key, path, 0, 0);
	if (ret < 0)
		goto out;

	while (1) {
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
	struct btrfs_space_info *space_info;
	struct rb_node *n;

	spin_lock(&info->block_group_cache_lock);
	while ((n = rb_last(&info->block_group_cache_tree)) != NULL) {
		block_group = rb_entry(n, struct btrfs_block_group_cache,
				       cache_node);
		rb_erase(&block_group->cache_node,
			 &info->block_group_cache_tree);
		spin_unlock(&info->block_group_cache_lock);

		btrfs_remove_free_space_cache(block_group);
		down_write(&block_group->space_info->groups_sem);
		list_del(&block_group->list);
		up_write(&block_group->space_info->groups_sem);

		WARN_ON(atomic_read(&block_group->count) != 1);
		kfree(block_group);

		spin_lock(&info->block_group_cache_lock);
	}
	spin_unlock(&info->block_group_cache_lock);

	/* now that all the block groups are freed, go through and
	 * free all the space_info structs.  This is only called during
	 * the final stages of unmount, and so we know nobody is
	 * using them.  We call synchronize_rcu() once before we start,
	 * just to be on the safe side.
	 */
	synchronize_rcu();

	while(!list_empty(&info->space_info)) {
		space_info = list_entry(info->space_info.next,
					struct btrfs_space_info,
					list);

		list_del(&space_info->list);
		kfree(space_info);
	}
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

	while (1) {
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

		atomic_set(&cache->count, 1);
		spin_lock_init(&cache->lock);
		spin_lock_init(&cache->tree_lock);
		mutex_init(&cache->cache_mutex);
		INIT_LIST_HEAD(&cache->list);
		INIT_LIST_HEAD(&cache->cluster_list);
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
		down_write(&space_info->groups_sem);
		list_add_tail(&cache->list, &space_info->block_groups);
		up_write(&space_info->groups_sem);

		ret = btrfs_add_block_group_cache(root->fs_info, cache);
		BUG_ON(ret);

		set_avail_alloc_bits(root->fs_info, cache->flags);
		if (btrfs_chunk_readonly(root, cache->key.objectid))
			set_block_group_readonly(cache);
	}
	ret = 0;
error:
	btrfs_free_path(path);
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

	extent_root = root->fs_info->extent_root;

	root->fs_info->last_trans_log_full_commit = trans->transid;

	cache = kzalloc(sizeof(*cache), GFP_NOFS);
	if (!cache)
		return -ENOMEM;

	cache->key.objectid = chunk_offset;
	cache->key.offset = size;
	cache->key.type = BTRFS_BLOCK_GROUP_ITEM_KEY;
	atomic_set(&cache->count, 1);
	spin_lock_init(&cache->lock);
	spin_lock_init(&cache->tree_lock);
	mutex_init(&cache->cache_mutex);
	INIT_LIST_HEAD(&cache->list);
	INIT_LIST_HEAD(&cache->cluster_list);

	btrfs_set_block_group_used(&cache->item, bytes_used);
	btrfs_set_block_group_chunk_objectid(&cache->item, chunk_objectid);
	cache->flags = type;
	btrfs_set_block_group_flags(&cache->item, type);

	ret = update_space_info(root->fs_info, cache->flags, size, bytes_used,
				&cache->space_info);
	BUG_ON(ret);
	down_write(&cache->space_info->groups_sem);
	list_add_tail(&cache->list, &cache->space_info->block_groups);
	up_write(&cache->space_info->groups_sem);

	ret = btrfs_add_block_group_cache(root->fs_info, cache);
	BUG_ON(ret);

	ret = btrfs_insert_item(trans, extent_root, &cache->key, &cache->item,
				sizeof(cache->item));
	BUG_ON(ret);

	set_avail_alloc_bits(extent_root->fs_info, type);

	return 0;
}

int btrfs_remove_block_group(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, u64 group_start)
{
	struct btrfs_path *path;
	struct btrfs_block_group_cache *block_group;
	struct btrfs_free_cluster *cluster;
	struct btrfs_key key;
	int ret;

	root = root->fs_info->extent_root;

	block_group = btrfs_lookup_block_group(root->fs_info, group_start);
	BUG_ON(!block_group);
	BUG_ON(!block_group->ro);

	memcpy(&key, &block_group->key, sizeof(key));

	/* make sure this block group isn't part of an allocation cluster */
	cluster = &root->fs_info->data_alloc_cluster;
	spin_lock(&cluster->refill_lock);
	btrfs_return_cluster_to_free_space(block_group, cluster);
	spin_unlock(&cluster->refill_lock);

	/*
	 * make sure this block group isn't part of a metadata
	 * allocation cluster
	 */
	cluster = &root->fs_info->meta_alloc_cluster;
	spin_lock(&cluster->refill_lock);
	btrfs_return_cluster_to_free_space(block_group, cluster);
	spin_unlock(&cluster->refill_lock);

	path = btrfs_alloc_path();
	BUG_ON(!path);

	spin_lock(&root->fs_info->block_group_cache_lock);
	rb_erase(&block_group->cache_node,
		 &root->fs_info->block_group_cache_tree);
	spin_unlock(&root->fs_info->block_group_cache_lock);
	btrfs_remove_free_space_cache(block_group);
	down_write(&block_group->space_info->groups_sem);
	/*
	 * we must use list_del_init so people can check to see if they
	 * are still on the list after taking the semaphore
	 */
	list_del_init(&block_group->list);
	up_write(&block_group->space_info->groups_sem);

	spin_lock(&block_group->space_info->lock);
	block_group->space_info->total_bytes -= block_group->key.offset;
	block_group->space_info->bytes_readonly -= block_group->key.offset;
	spin_unlock(&block_group->space_info->lock);
	block_group->space_info->full = 0;

	btrfs_put_block_group(block_group);
	btrfs_put_block_group(block_group);

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
