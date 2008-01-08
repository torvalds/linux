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
#include <linux/crc32c.h>
#include <linux/pagemap.h>
#include "hash.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"
#include "transaction.h"

#define BLOCK_GROUP_DATA EXTENT_WRITEBACK
#define BLOCK_GROUP_METADATA EXTENT_UPTODATE
#define BLOCK_GROUP_DIRTY EXTENT_DIRTY

static int finish_current_insert(struct btrfs_trans_handle *trans, struct
				 btrfs_root *extent_root);
static int del_pending_extents(struct btrfs_trans_handle *trans, struct
			       btrfs_root *extent_root);
static int find_previous_extent(struct btrfs_root *root,
				struct btrfs_path *path)
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
		if (found_key.type == BTRFS_EXTENT_ITEM_KEY)
			return 0;
	}
	return 1;
}

static int cache_block_group(struct btrfs_root *root,
			     struct btrfs_block_group_cache *block_group)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct extent_map_tree *free_space_cache;
	int slot;
	u64 last = 0;
	u64 hole_size;
	u64 first_free;
	int found = 0;

	if (!block_group)
		return 0;

	root = root->fs_info->extent_root;
	free_space_cache = &root->fs_info->free_space_cache;

	if (block_group->cached)
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 2;
	first_free = block_group->key.objectid;
	key.objectid = block_group->key.objectid;
	key.offset = 0;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		return ret;
	ret = find_previous_extent(root, path);
	if (ret < 0)
		return ret;
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
			if (ret == 0) {
				continue;
			} else {
				break;
			}
		}
		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid < block_group->key.objectid) {
			goto next;
		}
		if (key.objectid >= block_group->key.objectid +
		    block_group->key.offset) {
			break;
		}

		if (btrfs_key_type(&key) == BTRFS_EXTENT_ITEM_KEY) {
			if (!found) {
				last = first_free;
				found = 1;
			}
			if (key.objectid > last) {
				hole_size = key.objectid - last;
				set_extent_dirty(free_space_cache, last,
						 last + hole_size - 1,
						 GFP_NOFS);
			}
			last = key.objectid + key.offset;
		}
next:
		path->slots[0]++;
	}

	if (!found)
		last = first_free;
	if (block_group->key.objectid +
	    block_group->key.offset > last) {
		hole_size = block_group->key.objectid +
			block_group->key.offset - last;
		set_extent_dirty(free_space_cache, last,
				 last + hole_size - 1, GFP_NOFS);
	}
	block_group->cached = 1;
err:
	btrfs_free_path(path);
	return 0;
}

struct btrfs_block_group_cache *btrfs_lookup_block_group(struct
							 btrfs_fs_info *info,
							 u64 bytenr)
{
	struct extent_map_tree *block_group_cache;
	struct btrfs_block_group_cache *block_group = NULL;
	u64 ptr;
	u64 start;
	u64 end;
	int ret;

	block_group_cache = &info->block_group_cache;
	ret = find_first_extent_bit(block_group_cache,
				    bytenr, &start, &end,
				    BLOCK_GROUP_DATA | BLOCK_GROUP_METADATA);
	if (ret) {
		return NULL;
	}
	ret = get_state_private(block_group_cache, start, &ptr);
	if (ret)
		return NULL;

	block_group = (struct btrfs_block_group_cache *)(unsigned long)ptr;
	if (block_group->key.objectid <= bytenr && bytenr <
	    block_group->key.objectid + block_group->key.offset)
		return block_group;
	return NULL;
}
static u64 noinline find_search_start(struct btrfs_root *root,
			      struct btrfs_block_group_cache **cache_ret,
			      u64 search_start, int num, int data)
{
	int ret;
	struct btrfs_block_group_cache *cache = *cache_ret;
	u64 last;
	u64 start = 0;
	u64 end = 0;
	u64 cache_miss = 0;
	int wrapped = 0;

	if (!cache) {
		goto out;
	}
again:
	ret = cache_block_group(root, cache);
	if (ret)
		goto out;

	last = max(search_start, cache->key.objectid);

	while(1) {
		ret = find_first_extent_bit(&root->fs_info->free_space_cache,
					    last, &start, &end, EXTENT_DIRTY);
		if (ret) {
			if (!cache_miss)
				cache_miss = last;
			goto new_group;
		}

		start = max(last, start);
		last = end + 1;
		if (last - start < num) {
			if (last == cache->key.objectid + cache->key.offset)
				cache_miss = start;
			continue;
		}
		if (data != BTRFS_BLOCK_GROUP_MIXED &&
		    start + num > cache->key.objectid + cache->key.offset)
			goto new_group;
		return start;
	}
out:
	cache = btrfs_lookup_block_group(root->fs_info, search_start);
	if (!cache) {
		printk("Unable to find block group for %Lu\n",
		       search_start);
		WARN_ON(1);
		return search_start;
	}
	return search_start;

new_group:
	last = cache->key.objectid + cache->key.offset;
wrapped:
	cache = btrfs_lookup_block_group(root->fs_info, last);
	if (!cache) {
no_cache:
		if (!wrapped) {
			wrapped = 1;
			last = search_start;
			data = BTRFS_BLOCK_GROUP_MIXED;
			goto wrapped;
		}
		goto out;
	}
	if (cache_miss && !cache->cached) {
		cache_block_group(root, cache);
		last = cache_miss;
		cache = btrfs_lookup_block_group(root->fs_info, last);
	}
	cache = btrfs_find_block_group(root, cache, last, data, 0);
	if (!cache)
		goto no_cache;
	*cache_ret = cache;
	cache_miss = 0;
	goto again;
}

static u64 div_factor(u64 num, int factor)
{
	if (factor == 10)
		return num;
	num *= factor;
	do_div(num, 10);
	return num;
}

struct btrfs_block_group_cache *btrfs_find_block_group(struct btrfs_root *root,
						 struct btrfs_block_group_cache
						 *hint, u64 search_start,
						 int data, int owner)
{
	struct btrfs_block_group_cache *cache;
	struct extent_map_tree *block_group_cache;
	struct btrfs_block_group_cache *found_group = NULL;
	struct btrfs_fs_info *info = root->fs_info;
	u64 used;
	u64 last = 0;
	u64 hint_last;
	u64 start;
	u64 end;
	u64 free_check;
	u64 ptr;
	int bit;
	int ret;
	int full_search = 0;
	int factor = 8;
	int data_swap = 0;

	block_group_cache = &info->block_group_cache;

	if (!owner)
		factor = 8;

	if (data == BTRFS_BLOCK_GROUP_MIXED) {
		bit = BLOCK_GROUP_DATA | BLOCK_GROUP_METADATA;
		factor = 10;
	} else if (data)
		bit = BLOCK_GROUP_DATA;
	else
		bit = BLOCK_GROUP_METADATA;

	if (search_start) {
		struct btrfs_block_group_cache *shint;
		shint = btrfs_lookup_block_group(info, search_start);
		if (shint && (shint->data == data ||
			      shint->data == BTRFS_BLOCK_GROUP_MIXED)) {
			used = btrfs_block_group_used(&shint->item);
			if (used + shint->pinned <
			    div_factor(shint->key.offset, factor)) {
				return shint;
			}
		}
	}
	if (hint && (hint->data == data ||
		     hint->data == BTRFS_BLOCK_GROUP_MIXED)) {
		used = btrfs_block_group_used(&hint->item);
		if (used + hint->pinned <
		    div_factor(hint->key.offset, factor)) {
			return hint;
		}
		last = hint->key.objectid + hint->key.offset;
		hint_last = last;
	} else {
		if (hint)
			hint_last = max(hint->key.objectid, search_start);
		else
			hint_last = search_start;

		last = hint_last;
	}
again:
	while(1) {
		ret = find_first_extent_bit(block_group_cache, last,
					    &start, &end, bit);
		if (ret)
			break;

		ret = get_state_private(block_group_cache, start, &ptr);
		if (ret)
			break;

		cache = (struct btrfs_block_group_cache *)(unsigned long)ptr;
		last = cache->key.objectid + cache->key.offset;
		used = btrfs_block_group_used(&cache->item);

		if (full_search)
			free_check = cache->key.offset;
		else
			free_check = div_factor(cache->key.offset, factor);
		if (used + cache->pinned < free_check) {
			found_group = cache;
			goto found;
		}
		cond_resched();
	}
	if (!full_search) {
		last = search_start;
		full_search = 1;
		goto again;
	}
	if (!data_swap) {
		data_swap = 1;
		bit = BLOCK_GROUP_DATA | BLOCK_GROUP_METADATA;
		last = search_start;
		goto again;
	}
found:
	return found_group;
}

static u64 hash_extent_ref(u64 root_objectid, u64 ref_generation,
			   u64 owner, u64 owner_offset)
{
	u32 high_crc = ~(u32)0;
	u32 low_crc = ~(u32)0;
	__le64 lenum;

	lenum = cpu_to_le64(root_objectid);
	high_crc = crc32c(high_crc, &lenum, sizeof(lenum));
	lenum = cpu_to_le64(ref_generation);
	low_crc = crc32c(low_crc, &lenum, sizeof(lenum));

#if 0
	lenum = cpu_to_le64(owner);
	low_crc = crc32c(low_crc, &lenum, sizeof(lenum));
	lenum = cpu_to_le64(owner_offset);
	low_crc = crc32c(low_crc, &lenum, sizeof(lenum));
#endif
	return ((u64)high_crc << 32) | (u64)low_crc;
}

static int match_extent_ref(struct extent_buffer *leaf,
			    struct btrfs_extent_ref *disk_ref,
			    struct btrfs_extent_ref *cpu_ref)
{
	int ret;
	int len;

	if (cpu_ref->objectid)
		len = sizeof(*cpu_ref);
	else
		len = 2 * sizeof(u64);
	ret = memcmp_extent_buffer(leaf, cpu_ref, (unsigned long)disk_ref,
				   len);
	return ret == 0;
}

static int noinline lookup_extent_backref(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path, u64 bytenr,
					  u64 root_objectid,
					  u64 ref_generation, u64 owner,
					  u64 owner_offset, int del)
{
	u64 hash;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_extent_ref ref;
	struct extent_buffer *leaf;
	struct btrfs_extent_ref *disk_ref;
	int ret;
	int ret2;

	btrfs_set_stack_ref_root(&ref, root_objectid);
	btrfs_set_stack_ref_generation(&ref, ref_generation);
	btrfs_set_stack_ref_objectid(&ref, owner);
	btrfs_set_stack_ref_offset(&ref, owner_offset);

	hash = hash_extent_ref(root_objectid, ref_generation, owner,
			       owner_offset);
	key.offset = hash;
	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_REF_KEY;

	while (1) {
		ret = btrfs_search_slot(trans, root, &key, path,
					del ? -1 : 0, del);
		if (ret < 0)
			goto out;
		leaf = path->nodes[0];
		if (ret != 0) {
			u32 nritems = btrfs_header_nritems(leaf);
			if (path->slots[0] >= nritems) {
				ret2 = btrfs_next_leaf(root, path);
				if (ret2)
					goto out;
				leaf = path->nodes[0];
			}
			btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
			if (found_key.objectid != bytenr ||
			    found_key.type != BTRFS_EXTENT_REF_KEY)
				goto out;
			key.offset = found_key.offset;
			if (del) {
				btrfs_release_path(root, path);
				continue;
			}
		}
		disk_ref = btrfs_item_ptr(path->nodes[0],
					  path->slots[0],
					  struct btrfs_extent_ref);
		if (match_extent_ref(path->nodes[0], disk_ref, &ref)) {
			ret = 0;
			goto out;
		}
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		key.offset = found_key.offset + 1;
		btrfs_release_path(root, path);
	}
out:
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
 * - different files inside a single subvolume (in theory, not implemented yet)
 * - different offsets inside a file (bookend extents in file.c)
 *
 * The extent ref structure has fields for:
 *
 * - Objectid of the subvolume root
 * - Generation number of the tree holding the reference
 * - objectid of the file holding the reference
 * - offset in the file corresponding to the key holding the reference
 *
 * When a file extent is allocated the fields are filled in:
 *     (root_key.objectid, trans->transid, inode objectid, offset in file)
 *
 * When a leaf is cow'd new references are added for every file extent found
 * in the leaf.  It looks the same as the create case, but trans->transid
 * will be different when the block is cow'd.
 *
 *     (root_key.objectid, trans->transid, inode objectid, offset in file)
 *
 * When a file extent is removed either during snapshot deletion or file
 * truncation, the corresponding back reference is found
 * by searching for:
 *
 *     (btrfs_header_owner(leaf), btrfs_header_generation(leaf),
 *      inode objectid, offset in file)
 *
 * Btree extents can be referenced by:
 *
 * - Different subvolumes
 * - Different generations of the same subvolume
 *
 * Storing sufficient information for a full reverse mapping of a btree
 * block would require storing the lowest key of the block in the backref,
 * and it would require updating that lowest key either before write out or
 * every time it changed.  Instead, the objectid of the lowest key is stored
 * along with the level of the tree block.  This provides a hint
 * about where in the btree the block can be found.  Searches through the
 * btree only need to look for a pointer to that block, so they stop one
 * level higher than the level recorded in the backref.
 *
 * Some btrees do not do reference counting on their extents.  These
 * include the extent tree and the tree of tree roots.  Backrefs for these
 * trees always have a generation of zero.
 *
 * When a tree block is created, back references are inserted:
 *
 * (root->root_key.objectid, trans->transid or zero, level, lowest_key_objectid)
 *
 * When a tree block is cow'd in a reference counted root,
 * new back references are added for all the blocks it points to.
 * These are of the form (trans->transid will have increased since creation):
 *
 * (root->root_key.objectid, trans->transid, level, lowest_key_objectid)
 *
 * Because the lowest_key_objectid and the level are just hints
 * they are not used when backrefs are deleted.  When a backref is deleted:
 *
 * if backref was for a tree root:
 *     root_objectid = root->root_key.objectid
 * else
 *     root_objectid = btrfs_header_owner(parent)
 *
 * (root_objectid, btrfs_header_generation(parent) or zero, 0, 0)
 *
 * Back Reference Key hashing:
 *
 * Back references have four fields, each 64 bits long.  Unfortunately,
 * This is hashed into a single 64 bit number and placed into the key offset.
 * The key objectid corresponds to the first byte in the extent, and the
 * key type is set to BTRFS_EXTENT_REF_KEY
 */
int btrfs_insert_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path, u64 bytenr,
				 u64 root_objectid, u64 ref_generation,
				 u64 owner, u64 owner_offset)
{
	u64 hash;
	struct btrfs_key key;
	struct btrfs_extent_ref ref;
	struct btrfs_extent_ref *disk_ref;
	int ret;

	btrfs_set_stack_ref_root(&ref, root_objectid);
	btrfs_set_stack_ref_generation(&ref, ref_generation);
	btrfs_set_stack_ref_objectid(&ref, owner);
	btrfs_set_stack_ref_offset(&ref, owner_offset);

	hash = hash_extent_ref(root_objectid, ref_generation, owner,
			       owner_offset);
	key.offset = hash;
	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_REF_KEY;

	ret = btrfs_insert_empty_item(trans, root, path, &key, sizeof(ref));
	while (ret == -EEXIST) {
		disk_ref = btrfs_item_ptr(path->nodes[0], path->slots[0],
					  struct btrfs_extent_ref);
		if (match_extent_ref(path->nodes[0], disk_ref, &ref))
			goto out;
		key.offset++;
		btrfs_release_path(root, path);
		ret = btrfs_insert_empty_item(trans, root, path, &key,
					      sizeof(ref));
	}
	if (ret)
		goto out;
	disk_ref = btrfs_item_ptr(path->nodes[0], path->slots[0],
				  struct btrfs_extent_ref);
	write_extent_buffer(path->nodes[0], &ref, (unsigned long)disk_ref,
			    sizeof(ref));
	btrfs_mark_buffer_dirty(path->nodes[0]);
out:
	btrfs_release_path(root, path);
	return ret;
}

int btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				u64 bytenr, u64 num_bytes,
				u64 root_objectid, u64 ref_generation,
				u64 owner, u64 owner_offset)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_extent_item *item;
	u32 refs;

	WARN_ON(num_bytes < root->sectorsize);
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 0;
	key.objectid = bytenr;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	key.offset = num_bytes;
	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key, path,
				0, 1);
	if (ret < 0)
		return ret;
	if (ret != 0) {
		BUG();
	}
	BUG_ON(ret != 0);
	l = path->nodes[0];
	item = btrfs_item_ptr(l, path->slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(l, item);
	btrfs_set_extent_refs(l, item, refs + 1);
	btrfs_mark_buffer_dirty(path->nodes[0]);

	btrfs_release_path(root->fs_info->extent_root, path);

	path->reada = 0;
	ret = btrfs_insert_extent_backref(trans, root->fs_info->extent_root,
					  path, bytenr, root_objectid,
					  ref_generation, owner, owner_offset);
	BUG_ON(ret);
	finish_current_insert(trans, root->fs_info->extent_root);
	del_pending_extents(trans, root->fs_info->extent_root);

	btrfs_free_path(path);
	return 0;
}

int btrfs_extent_post_op(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root)
{
	finish_current_insert(trans, root->fs_info->extent_root);
	del_pending_extents(trans, root->fs_info->extent_root);
	return 0;
}

static int lookup_extent_ref(struct btrfs_trans_handle *trans,
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
	path->reada = 0;
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

u32 btrfs_count_snapshots_in_path(struct btrfs_root *root,
				  struct btrfs_path *count_path,
				  u64 first_extent)
{
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	struct btrfs_path *path;
	u64 bytenr;
	u64 found_objectid;
	u64 root_objectid = root->root_key.objectid;
	u32 total_count = 0;
	u32 cur_count;
	u32 nritems;
	int ret;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *l;
	struct btrfs_extent_item *item;
	struct btrfs_extent_ref *ref_item;
	int level = -1;

	path = btrfs_alloc_path();
again:
	if (level == -1)
		bytenr = first_extent;
	else
		bytenr = count_path->nodes[level]->start;

	cur_count = 0;
	key.objectid = bytenr;
	key.offset = 0;

	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	ret = btrfs_search_slot(NULL, extent_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	BUG_ON(ret == 0);

	l = path->nodes[0];
	btrfs_item_key_to_cpu(l, &found_key, path->slots[0]);

	if (found_key.objectid != bytenr ||
	    found_key.type != BTRFS_EXTENT_ITEM_KEY) {
		goto out;
	}

	item = btrfs_item_ptr(l, path->slots[0], struct btrfs_extent_item);
	while (1) {
		l = path->nodes[0];
		nritems = btrfs_header_nritems(l);
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(extent_root, path);
			if (ret == 0)
				continue;
			break;
		}
		btrfs_item_key_to_cpu(l, &found_key, path->slots[0]);
		if (found_key.objectid != bytenr)
			break;

		if (found_key.type != BTRFS_EXTENT_REF_KEY) {
			path->slots[0]++;
			continue;
		}

		cur_count++;
		ref_item = btrfs_item_ptr(l, path->slots[0],
					  struct btrfs_extent_ref);
		found_objectid = btrfs_ref_root(l, ref_item);

		if (found_objectid != root_objectid) {
			total_count = 2;
			goto out;
		}
		total_count = 1;
		path->slots[0]++;
	}
	if (cur_count == 0) {
		total_count = 0;
		goto out;
	}
	if (level >= 0 && root->node == count_path->nodes[level])
		goto out;
	level++;
	btrfs_release_path(root, path);
	goto again;

out:
	btrfs_free_path(path);
	return total_count;
}
int btrfs_inc_root_ref(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, u64 owner_objectid)
{
	u64 generation;
	u64 key_objectid;
	u64 level;
	u32 nritems;
	struct btrfs_disk_key disk_key;

	level = btrfs_header_level(root->node);
	generation = trans->transid;
	nritems = btrfs_header_nritems(root->node);
	if (nritems > 0) {
		if (level == 0)
			btrfs_item_key(root->node, &disk_key, 0);
		else
			btrfs_node_key(root->node, &disk_key, 0);
		key_objectid = btrfs_disk_key_objectid(&disk_key);
	} else {
		key_objectid = 0;
	}
	return btrfs_inc_extent_ref(trans, root, root->node->start,
				    root->node->len, owner_objectid,
				    generation, level, key_objectid);
}

int btrfs_inc_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf)
{
	u64 bytenr;
	u32 nritems;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	int i;
	int level;
	int ret;
	int faili;

	if (!root->ref_cows)
		return 0;

	level = btrfs_header_level(buf);
	nritems = btrfs_header_nritems(buf);
	for (i = 0; i < nritems; i++) {
		if (level == 0) {
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
			ret = btrfs_inc_extent_ref(trans, root, disk_bytenr,
				    btrfs_file_extent_disk_num_bytes(buf, fi),
				    root->root_key.objectid, trans->transid,
				    key.objectid, key.offset);
			if (ret) {
				faili = i;
				goto fail;
			}
		} else {
			bytenr = btrfs_node_blockptr(buf, i);
			btrfs_node_key_to_cpu(buf, &key, i);
			ret = btrfs_inc_extent_ref(trans, root, bytenr,
					   btrfs_level_size(root, level - 1),
					   root->root_key.objectid,
					   trans->transid,
					   level - 1, key.objectid);
			if (ret) {
				faili = i;
				goto fail;
			}
		}
	}
	return 0;
fail:
	WARN_ON(1);
#if 0
	for (i =0; i < faili; i++) {
		if (level == 0) {
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
			err = btrfs_free_extent(trans, root, disk_bytenr,
				    btrfs_file_extent_disk_num_bytes(buf,
								      fi), 0);
			BUG_ON(err);
		} else {
			bytenr = btrfs_node_blockptr(buf, i);
			err = btrfs_free_extent(trans, root, bytenr,
					btrfs_level_size(root, level - 1), 0);
			BUG_ON(err);
		}
	}
#endif
	return ret;
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
	struct extent_map_tree *block_group_cache;
	struct btrfs_block_group_cache *cache;
	int ret;
	int err = 0;
	int werr = 0;
	struct btrfs_path *path;
	u64 last = 0;
	u64 start;
	u64 end;
	u64 ptr;

	block_group_cache = &root->fs_info->block_group_cache;
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while(1) {
		ret = find_first_extent_bit(block_group_cache, last,
					    &start, &end, BLOCK_GROUP_DIRTY);
		if (ret)
			break;

		last = end + 1;
		ret = get_state_private(block_group_cache, start, &ptr);
		if (ret)
			break;

		cache = (struct btrfs_block_group_cache *)(unsigned long)ptr;
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
		clear_extent_bits(block_group_cache, start, end,
				  BLOCK_GROUP_DIRTY, GFP_NOFS);
	}
	btrfs_free_path(path);
	return werr;
}

static int update_block_group(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      u64 bytenr, u64 num_bytes, int alloc,
			      int mark_free, int data)
{
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *info = root->fs_info;
	u64 total = num_bytes;
	u64 old_val;
	u64 byte_in_group;
	u64 start;
	u64 end;

	while(total) {
		cache = btrfs_lookup_block_group(info, bytenr);
		if (!cache) {
			return -1;
		}
		byte_in_group = bytenr - cache->key.objectid;
		WARN_ON(byte_in_group > cache->key.offset);
		start = cache->key.objectid;
		end = start + cache->key.offset - 1;
		set_extent_bits(&info->block_group_cache, start, end,
				BLOCK_GROUP_DIRTY, GFP_NOFS);

		old_val = btrfs_block_group_used(&cache->item);
		num_bytes = min(total, cache->key.offset - byte_in_group);
		if (alloc) {
			if (cache->data != data &&
			    old_val < (cache->key.offset >> 1)) {
				int bit_to_clear;
				int bit_to_set;
				cache->data = data;
				if (data) {
					bit_to_clear = BLOCK_GROUP_METADATA;
					bit_to_set = BLOCK_GROUP_DATA;
					cache->item.flags &=
						~BTRFS_BLOCK_GROUP_MIXED;
					cache->item.flags |=
						BTRFS_BLOCK_GROUP_DATA;
				} else {
					bit_to_clear = BLOCK_GROUP_DATA;
					bit_to_set = BLOCK_GROUP_METADATA;
					cache->item.flags &=
						~BTRFS_BLOCK_GROUP_MIXED;
					cache->item.flags &=
						~BTRFS_BLOCK_GROUP_DATA;
				}
				clear_extent_bits(&info->block_group_cache,
						  start, end, bit_to_clear,
						  GFP_NOFS);
				set_extent_bits(&info->block_group_cache,
						start, end, bit_to_set,
						GFP_NOFS);
			} else if (cache->data != data &&
				   cache->data != BTRFS_BLOCK_GROUP_MIXED) {
				cache->data = BTRFS_BLOCK_GROUP_MIXED;
				set_extent_bits(&info->block_group_cache,
						start, end,
						BLOCK_GROUP_DATA |
						BLOCK_GROUP_METADATA,
						GFP_NOFS);
			}
			old_val += num_bytes;
		} else {
			old_val -= num_bytes;
			if (mark_free) {
				set_extent_dirty(&info->free_space_cache,
						 bytenr, bytenr + num_bytes - 1,
						 GFP_NOFS);
			}
		}
		btrfs_set_block_group_used(&cache->item, old_val);
		total -= num_bytes;
		bytenr += num_bytes;
	}
	return 0;
}
static int update_pinned_extents(struct btrfs_root *root,
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
		WARN_ON(!cache);
		len = min(num, cache->key.offset -
			  (bytenr - cache->key.objectid));
		if (pin) {
			cache->pinned += len;
			fs_info->total_pinned += len;
		} else {
			cache->pinned -= len;
			fs_info->total_pinned -= len;
		}
		bytenr += len;
		num -= len;
	}
	return 0;
}

int btrfs_copy_pinned(struct btrfs_root *root, struct extent_map_tree *copy)
{
	u64 last = 0;
	u64 start;
	u64 end;
	struct extent_map_tree *pinned_extents = &root->fs_info->pinned_extents;
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
			       struct extent_map_tree *unpin)
{
	u64 start;
	u64 end;
	int ret;
	struct extent_map_tree *free_space_cache;
	free_space_cache = &root->fs_info->free_space_cache;

	while(1) {
		ret = find_first_extent_bit(unpin, 0, &start, &end,
					    EXTENT_DIRTY);
		if (ret)
			break;
		update_pinned_extents(root, start, end + 1 - start, 0);
		clear_extent_dirty(unpin, start, end, GFP_NOFS);
		set_extent_dirty(free_space_cache, start, end, GFP_NOFS);
	}
	return 0;
}

static int finish_current_insert(struct btrfs_trans_handle *trans,
				 struct btrfs_root *extent_root)
{
	u64 start;
	u64 end;
	struct btrfs_fs_info *info = extent_root->fs_info;
	struct extent_buffer *eb;
	struct btrfs_path *path;
	struct btrfs_key ins;
	struct btrfs_disk_key first;
	struct btrfs_extent_item extent_item;
	int ret;
	int level;
	int err = 0;

	btrfs_set_stack_extent_refs(&extent_item, 1);
	btrfs_set_key_type(&ins, BTRFS_EXTENT_ITEM_KEY);
	path = btrfs_alloc_path();

	while(1) {
		ret = find_first_extent_bit(&info->extent_ins, 0, &start,
					    &end, EXTENT_LOCKED);
		if (ret)
			break;

		ins.objectid = start;
		ins.offset = end + 1 - start;
		err = btrfs_insert_item(trans, extent_root, &ins,
					&extent_item, sizeof(extent_item));
		clear_extent_bits(&info->extent_ins, start, end, EXTENT_LOCKED,
				  GFP_NOFS);
		eb = read_tree_block(extent_root, ins.objectid, ins.offset);
		level = btrfs_header_level(eb);
		if (level == 0) {
			btrfs_item_key(eb, &first, 0);
		} else {
			btrfs_node_key(eb, &first, 0);
		}
		err = btrfs_insert_extent_backref(trans, extent_root, path,
					  start, extent_root->root_key.objectid,
					  0, level,
					  btrfs_disk_key_objectid(&first));
		BUG_ON(err);
		free_extent_buffer(eb);
	}
	btrfs_free_path(path);
	return 0;
}

static int pin_down_bytes(struct btrfs_root *root, u64 bytenr, u32 num_bytes,
			  int pending)
{
	int err = 0;
	struct extent_buffer *buf;

	if (!pending) {
		buf = btrfs_find_tree_block(root, bytenr, num_bytes);
		if (buf) {
			if (btrfs_buffer_uptodate(buf)) {
				u64 transid =
				    root->fs_info->running_transaction->transid;
				u64 header_transid =
					btrfs_header_generation(buf);
				if (header_transid == transid) {
					free_extent_buffer(buf);
					return 1;
				}
			}
			free_extent_buffer(buf);
		}
		update_pinned_extents(root, bytenr, num_bytes, 1);
	} else {
		set_extent_bits(&root->fs_info->pending_del,
				bytenr, bytenr + num_bytes - 1,
				EXTENT_LOCKED, GFP_NOFS);
	}
	BUG_ON(err < 0);
	return 0;
}

/*
 * remove an extent from the root, returns 0 on success
 */
static int __free_extent(struct btrfs_trans_handle *trans, struct btrfs_root
			 *root, u64 bytenr, u64 num_bytes,
			 u64 root_objectid, u64 ref_generation,
			 u64 owner_objectid, u64 owner_offset, int pin,
			 int mark_free)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_root *extent_root = info->extent_root;
	struct extent_buffer *leaf;
	int ret;
	struct btrfs_extent_item *ei;
	u32 refs;

	key.objectid = bytenr;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	key.offset = num_bytes;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 0;
	ret = lookup_extent_backref(trans, extent_root, path,
				    bytenr, root_objectid,
				    ref_generation,
				    owner_objectid, owner_offset, 1);
	if (ret == 0) {
		ret = btrfs_del_item(trans, extent_root, path);
	} else {
		btrfs_print_leaf(extent_root, path->nodes[0]);
		WARN_ON(1);
		printk("Unable to find ref byte nr %Lu root %Lu "
		       " gen %Lu owner %Lu offset %Lu\n", bytenr,
		       root_objectid, ref_generation, owner_objectid,
		       owner_offset);
	}
	btrfs_release_path(extent_root, path);
	ret = btrfs_search_slot(trans, extent_root, &key, path, -1, 1);
	if (ret < 0)
		return ret;
	BUG_ON(ret);

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0],
			    struct btrfs_extent_item);
	refs = btrfs_extent_refs(leaf, ei);
	BUG_ON(refs == 0);
	refs -= 1;
	btrfs_set_extent_refs(leaf, ei, refs);
	btrfs_mark_buffer_dirty(leaf);

	if (refs == 0) {
		u64 super_used;
		u64 root_used;

		if (pin) {
			ret = pin_down_bytes(root, bytenr, num_bytes, 0);
			if (ret > 0)
				mark_free = 1;
			BUG_ON(ret < 0);
		}

		/* block accounting for super block */
		super_used = btrfs_super_bytes_used(&info->super_copy);
		btrfs_set_super_bytes_used(&info->super_copy,
					   super_used - num_bytes);

		/* block accounting for root item */
		root_used = btrfs_root_used(&root->root_item);
		btrfs_set_root_used(&root->root_item,
					   root_used - num_bytes);

		ret = btrfs_del_item(trans, extent_root, path);
		if (ret) {
			return ret;
		}
		ret = update_block_group(trans, root, bytenr, num_bytes, 0,
					 mark_free, 0);
		BUG_ON(ret);
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
	u64 start;
	u64 end;
	struct extent_map_tree *pending_del;
	struct extent_map_tree *pinned_extents;

	pending_del = &extent_root->fs_info->pending_del;
	pinned_extents = &extent_root->fs_info->pinned_extents;

	while(1) {
		ret = find_first_extent_bit(pending_del, 0, &start, &end,
					    EXTENT_LOCKED);
		if (ret)
			break;
		update_pinned_extents(extent_root, start, end + 1 - start, 1);
		clear_extent_bits(pending_del, start, end, EXTENT_LOCKED,
				  GFP_NOFS);
		ret = __free_extent(trans, extent_root,
				     start, end + 1 - start,
				     extent_root->root_key.objectid,
				     0, 0, 0, 0, 0);
		if (ret)
			err = ret;
	}
	return err;
}

/*
 * remove an extent from the root, returns 0 on success
 */
int btrfs_free_extent(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, u64 bytenr, u64 num_bytes,
		      u64 root_objectid, u64 ref_generation,
		      u64 owner_objectid, u64 owner_offset, int pin)
{
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	int pending_ret;
	int ret;

	WARN_ON(num_bytes < root->sectorsize);
	if (!root->ref_cows)
		ref_generation = 0;

	if (root == extent_root) {
		pin_down_bytes(root, bytenr, num_bytes, 1);
		return 0;
	}
	ret = __free_extent(trans, root, bytenr, num_bytes, root_objectid,
			    ref_generation, owner_objectid, owner_offset,
			    pin, pin == 0);
	pending_ret = del_pending_extents(trans, root->fs_info->extent_root);
	return ret ? ret : pending_ret;
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
	struct btrfs_path *path;
	struct btrfs_key key;
	u64 hole_size = 0;
	u64 aligned;
	int ret;
	int slot = 0;
	u64 last_byte = 0;
	u64 orig_search_start = search_start;
	int start_found;
	struct extent_buffer *l;
	struct btrfs_root * root = orig_root->fs_info->extent_root;
	struct btrfs_fs_info *info = root->fs_info;
	u64 total_needed = num_bytes;
	int level;
	struct btrfs_block_group_cache *block_group;
	int full_scan = 0;
	int wrapped = 0;
	u64 cached_start;

	WARN_ON(num_bytes < root->sectorsize);
	btrfs_set_key_type(ins, BTRFS_EXTENT_ITEM_KEY);

	level = btrfs_header_level(root->node);

	if (num_bytes >= 32 * 1024 * 1024 && hint_byte) {
		data = BTRFS_BLOCK_GROUP_MIXED;
	}

	if (search_end == (u64)-1)
		search_end = btrfs_super_total_bytes(&info->super_copy);
	if (hint_byte) {
		block_group = btrfs_lookup_block_group(info, hint_byte);
		if (!block_group)
			hint_byte = search_start;
		block_group = btrfs_find_block_group(root, block_group,
						     hint_byte, data, 1);
	} else {
		block_group = btrfs_find_block_group(root,
						     trans->block_group,
						     search_start, data, 1);
	}

	total_needed += empty_size;
	path = btrfs_alloc_path();
check_failed:
	if (!block_group) {
		block_group = btrfs_lookup_block_group(info, search_start);
		if (!block_group)
			block_group = btrfs_lookup_block_group(info,
						       orig_search_start);
	}
	search_start = find_search_start(root, &block_group, search_start,
					 total_needed, data);
	search_start = stripe_align(root, search_start);
	cached_start = search_start;
	btrfs_init_path(path);
	ins->objectid = search_start;
	ins->offset = 0;
	start_found = 0;
	path->reada = 2;

	ret = btrfs_search_slot(trans, root, ins, path, 0, 0);
	if (ret < 0)
		goto error;
	ret = find_previous_extent(root, path);
	if (ret < 0)
		goto error;
	l = path->nodes[0];
	btrfs_item_key_to_cpu(l, &key, path->slots[0]);
	while (1) {
		l = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(l)) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto error;

			search_start = max(search_start,
					   block_group->key.objectid);
			if (!start_found) {
				aligned = stripe_align(root, search_start);
				ins->objectid = aligned;
				if (aligned >= search_end) {
					ret = -ENOSPC;
					goto error;
				}
				ins->offset = search_end - aligned;
				start_found = 1;
				goto check_pending;
			}
			ins->objectid = stripe_align(root,
						     last_byte > search_start ?
						     last_byte : search_start);
			if (search_end <= ins->objectid) {
				ret = -ENOSPC;
				goto error;
			}
			ins->offset = search_end - ins->objectid;
			BUG_ON(ins->objectid >= search_end);
			goto check_pending;
		}
		btrfs_item_key_to_cpu(l, &key, slot);

		if (key.objectid >= search_start && key.objectid > last_byte &&
		    start_found) {
			if (last_byte < search_start)
				last_byte = search_start;
			aligned = stripe_align(root, last_byte);
			hole_size = key.objectid - aligned;
			if (key.objectid > aligned && hole_size >= num_bytes) {
				ins->objectid = aligned;
				ins->offset = hole_size;
				goto check_pending;
			}
		}
		if (btrfs_key_type(&key) != BTRFS_EXTENT_ITEM_KEY) {
			if (!start_found && btrfs_key_type(&key) ==
			    BTRFS_BLOCK_GROUP_ITEM_KEY) {
				last_byte = key.objectid;
				start_found = 1;
			}
			goto next;
		}


		start_found = 1;
		last_byte = key.objectid + key.offset;

		if (!full_scan && data != BTRFS_BLOCK_GROUP_MIXED &&
		    last_byte >= block_group->key.objectid +
		    block_group->key.offset) {
			btrfs_release_path(root, path);
			search_start = block_group->key.objectid +
				block_group->key.offset;
			goto new_group;
		}
next:
		path->slots[0]++;
		cond_resched();
	}
check_pending:
	/* we have to make sure we didn't find an extent that has already
	 * been allocated by the map tree or the original allocation
	 */
	btrfs_release_path(root, path);
	BUG_ON(ins->objectid < search_start);

	if (ins->objectid + num_bytes >= search_end)
		goto enospc;
	if (!full_scan && data != BTRFS_BLOCK_GROUP_MIXED &&
	    ins->objectid + num_bytes > block_group->
	    key.objectid + block_group->key.offset) {
		search_start = block_group->key.objectid +
			block_group->key.offset;
		goto new_group;
	}
	if (test_range_bit(&info->extent_ins, ins->objectid,
			   ins->objectid + num_bytes -1, EXTENT_LOCKED, 0)) {
		search_start = ins->objectid + num_bytes;
		goto new_group;
	}
	if (test_range_bit(&info->pinned_extents, ins->objectid,
			   ins->objectid + num_bytes -1, EXTENT_DIRTY, 0)) {
		search_start = ins->objectid + num_bytes;
		goto new_group;
	}
	if (exclude_nr > 0 && (ins->objectid + num_bytes > exclude_start &&
	    ins->objectid < exclude_start + exclude_nr)) {
		search_start = exclude_start + exclude_nr;
		goto new_group;
	}
	if (!data) {
		block_group = btrfs_lookup_block_group(info, ins->objectid);
		if (block_group)
			trans->block_group = block_group;
	}
	ins->offset = num_bytes;
	btrfs_free_path(path);
	return 0;

new_group:
	if (search_start + num_bytes >= search_end) {
enospc:
		search_start = orig_search_start;
		if (full_scan) {
			ret = -ENOSPC;
			goto error;
		}
		if (wrapped) {
			if (!full_scan)
				total_needed -= empty_size;
			full_scan = 1;
			data = BTRFS_BLOCK_GROUP_MIXED;
		} else
			wrapped = 1;
	}
	block_group = btrfs_lookup_block_group(info, search_start);
	cond_resched();
	block_group = btrfs_find_block_group(root, block_group,
					     search_start, data, 0);
	goto check_failed;

error:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
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
		       u64 num_bytes, u64 root_objectid, u64 ref_generation,
		       u64 owner, u64 owner_offset,
		       u64 empty_size, u64 hint_byte,
		       u64 search_end, struct btrfs_key *ins, int data)
{
	int ret;
	int pending_ret;
	u64 super_used, root_used;
	u64 search_start = 0;
	u64 new_hint;
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_root *extent_root = info->extent_root;
	struct btrfs_extent_item extent_item;
	struct btrfs_path *path;

	btrfs_set_stack_extent_refs(&extent_item, 1);

	new_hint = max(hint_byte, root->fs_info->alloc_start);
	if (new_hint < btrfs_super_total_bytes(&info->super_copy))
		hint_byte = new_hint;

	WARN_ON(num_bytes < root->sectorsize);
	ret = find_free_extent(trans, root, num_bytes, empty_size,
			       search_start, search_end, hint_byte, ins,
			       trans->alloc_exclude_start,
			       trans->alloc_exclude_nr, data);
	BUG_ON(ret);
	if (ret)
		return ret;

	/* block accounting for super block */
	super_used = btrfs_super_bytes_used(&info->super_copy);
	btrfs_set_super_bytes_used(&info->super_copy, super_used + num_bytes);

	/* block accounting for root item */
	root_used = btrfs_root_used(&root->root_item);
	btrfs_set_root_used(&root->root_item, root_used + num_bytes);

	clear_extent_dirty(&root->fs_info->free_space_cache,
			   ins->objectid, ins->objectid + ins->offset - 1,
			   GFP_NOFS);

	if (root == extent_root) {
		set_extent_bits(&root->fs_info->extent_ins, ins->objectid,
				ins->objectid + ins->offset - 1,
				EXTENT_LOCKED, GFP_NOFS);
		WARN_ON(data == 1);
		goto update_block;
	}

	WARN_ON(trans->alloc_exclude_nr);
	trans->alloc_exclude_start = ins->objectid;
	trans->alloc_exclude_nr = ins->offset;
	ret = btrfs_insert_item(trans, extent_root, ins, &extent_item,
				sizeof(extent_item));

	trans->alloc_exclude_start = 0;
	trans->alloc_exclude_nr = 0;
	BUG_ON(ret);

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_insert_extent_backref(trans, extent_root, path,
					  ins->objectid, root_objectid,
					  ref_generation, owner, owner_offset);

	BUG_ON(ret);
	btrfs_free_path(path);
	finish_current_insert(trans, extent_root);
	pending_ret = del_pending_extents(trans, extent_root);

	if (ret) {
		return ret;
	}
	if (pending_ret) {
		return pending_ret;
	}

update_block:
	ret = update_block_group(trans, root, ins->objectid, ins->offset, 1, 0,
				 data);
	BUG_ON(ret);
	return 0;
}

/*
 * helper function to allocate a block for a given tree
 * returns the tree buffer or NULL.
 */
struct extent_buffer *btrfs_alloc_free_block(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     u32 blocksize,
					     u64 root_objectid, u64 hint,
					     u64 empty_size)
{
	u64 ref_generation;

	if (root->ref_cows)
		ref_generation = trans->transid;
	else
		ref_generation = 0;


	return __btrfs_alloc_free_block(trans, root, blocksize, root_objectid,
					ref_generation, 0, 0, hint, empty_size);
}

/*
 * helper function to allocate a block for a given tree
 * returns the tree buffer or NULL.
 */
struct extent_buffer *__btrfs_alloc_free_block(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     u32 blocksize,
					     u64 root_objectid,
					     u64 ref_generation,
					     u64 first_objectid,
					     int level,
					     u64 hint,
					     u64 empty_size)
{
	struct btrfs_key ins;
	int ret;
	struct extent_buffer *buf;

	ret = btrfs_alloc_extent(trans, root, blocksize,
				 root_objectid, ref_generation,
				 level, first_objectid, empty_size, hint,
				 (u64)-1, &ins, 0);
	if (ret) {
		BUG_ON(ret > 0);
		return ERR_PTR(ret);
	}
	buf = btrfs_find_create_tree_block(root, ins.objectid, blocksize);
	if (!buf) {
		btrfs_free_extent(trans, root, ins.objectid, blocksize,
				  root->root_key.objectid, ref_generation,
				  0, 0, 0);
		return ERR_PTR(-ENOMEM);
	}
	btrfs_set_buffer_uptodate(buf);
	set_extent_dirty(&trans->transaction->dirty_pages, buf->start,
			 buf->start + buf->len - 1, GFP_NOFS);
	set_extent_bits(&BTRFS_I(root->fs_info->btree_inode)->extent_tree,
			buf->start, buf->start + buf->len - 1,
			EXTENT_CSUM, GFP_NOFS);
	buf->flags |= EXTENT_CSUM;
	btrfs_set_buffer_defrag(buf);
	trans->blocks_used++;
	return buf;
}

static int noinline drop_leaf_ref(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  struct extent_buffer *leaf)
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
		ret = btrfs_free_extent(trans, root, disk_bytenr,
				btrfs_file_extent_disk_num_bytes(leaf, fi),
				leaf_owner, leaf_generation,
				key.objectid, key.offset, 0);
		BUG_ON(ret);
	}
	return 0;
}

static void noinline reada_walk_down(struct btrfs_root *root,
				     struct extent_buffer *node)
{
	int i;
	u32 nritems;
	u64 bytenr;
	int ret;
	u32 refs;
	int level;
	u32 blocksize;

	nritems = btrfs_header_nritems(node);
	level = btrfs_header_level(node);
	for (i = 0; i < nritems; i++) {
		bytenr = btrfs_node_blockptr(node, i);
		blocksize = btrfs_level_size(root, level - 1);
		ret = lookup_extent_ref(NULL, root, bytenr, blocksize, &refs);
		BUG_ON(ret);
		if (refs != 1)
			continue;
		mutex_unlock(&root->fs_info->fs_mutex);
		ret = readahead_tree_block(root, bytenr, blocksize);
		cond_resched();
		mutex_lock(&root->fs_info->fs_mutex);
		if (ret)
			break;
	}
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
	struct extent_buffer *next;
	struct extent_buffer *cur;
	struct extent_buffer *parent;
	u32 blocksize;
	int ret;
	u32 refs;

	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);
	ret = lookup_extent_ref(trans, root,
				path->nodes[*level]->start,
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

		if (*level > 0 && path->slots[*level] == 0)
			reada_walk_down(root, cur);

		if (btrfs_header_level(cur) != *level)
			WARN_ON(1);

		if (path->slots[*level] >=
		    btrfs_header_nritems(cur))
			break;
		if (*level == 0) {
			ret = drop_leaf_ref(trans, root, cur);
			BUG_ON(ret);
			break;
		}
		bytenr = btrfs_node_blockptr(cur, path->slots[*level]);
		blocksize = btrfs_level_size(root, *level - 1);
		ret = lookup_extent_ref(trans, root, bytenr, blocksize, &refs);
		BUG_ON(ret);
		if (refs != 1) {
			parent = path->nodes[*level];
			root_owner = btrfs_header_owner(parent);
			root_gen = btrfs_header_generation(parent);
			path->slots[*level]++;
			ret = btrfs_free_extent(trans, root, bytenr,
						blocksize, root_owner,
						root_gen, 0, 0, 1);
			BUG_ON(ret);
			continue;
		}
		next = btrfs_find_tree_block(root, bytenr, blocksize);
		if (!next || !btrfs_buffer_uptodate(next)) {
			free_extent_buffer(next);
			mutex_unlock(&root->fs_info->fs_mutex);
			next = read_tree_block(root, bytenr, blocksize);
			mutex_lock(&root->fs_info->fs_mutex);

			/* we dropped the lock, check one more time */
			ret = lookup_extent_ref(trans, root, bytenr,
						blocksize, &refs);
			BUG_ON(ret);
			if (refs != 1) {
				parent = path->nodes[*level];
				root_owner = btrfs_header_owner(parent);
				root_gen = btrfs_header_generation(parent);

				path->slots[*level]++;
				free_extent_buffer(next);
				ret = btrfs_free_extent(trans, root, bytenr,
							blocksize,
							root_owner,
							root_gen, 0, 0, 1);
				BUG_ON(ret);
				continue;
			}
		}
		WARN_ON(*level <= 0);
		if (path->nodes[*level-1])
			free_extent_buffer(path->nodes[*level-1]);
		path->nodes[*level-1] = next;
		*level = btrfs_header_level(next);
		path->slots[*level] = 0;
	}
out:
	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);

	if (path->nodes[*level] == root->node) {
		root_owner = root->root_key.objectid;
		parent = path->nodes[*level];
	} else {
		parent = path->nodes[*level + 1];
		root_owner = btrfs_header_owner(parent);
	}

	root_gen = btrfs_header_generation(parent);
	ret = btrfs_free_extent(trans, root, path->nodes[*level]->start,
				path->nodes[*level]->len,
				root_owner, root_gen, 0, 0, 1);
	free_extent_buffer(path->nodes[*level]);
	path->nodes[*level] = NULL;
	*level += 1;
	BUG_ON(ret);
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
			if (path->nodes[*level] == root->node) {
				root_owner = root->root_key.objectid;
				root_gen =
				   btrfs_header_generation(path->nodes[*level]);
			} else {
				struct extent_buffer *node;
				node = path->nodes[*level + 1];
				root_owner = btrfs_header_owner(node);
				root_gen = btrfs_header_generation(node);
			}
			ret = btrfs_free_extent(trans, root,
						path->nodes[*level]->start,
						path->nodes[*level]->len,
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
		ret = -EAGAIN;
		break;
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

int btrfs_free_block_groups(struct btrfs_fs_info *info)
{
	u64 start;
	u64 end;
	u64 ptr;
	int ret;
	while(1) {
		ret = find_first_extent_bit(&info->block_group_cache, 0,
					    &start, &end, (unsigned int)-1);
		if (ret)
			break;
		ret = get_state_private(&info->block_group_cache, start, &ptr);
		if (!ret)
			kfree((void *)(unsigned long)ptr);
		clear_extent_bits(&info->block_group_cache, start,
				  end, (unsigned int)-1, GFP_NOFS);
	}
	while(1) {
		ret = find_first_extent_bit(&info->free_space_cache, 0,
					    &start, &end, EXTENT_DIRTY);
		if (ret)
			break;
		clear_extent_dirty(&info->free_space_cache, start,
				   end, GFP_NOFS);
	}
	return 0;
}

static int noinline relocate_inode_pages(struct inode *inode, u64 start,
					 u64 len)
{
	u64 page_start;
	u64 page_end;
	u64 delalloc_start;
	u64 existing_delalloc;
	unsigned long last_index;
	unsigned long i;
	struct page *page;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct file_ra_state *ra;

	ra = kzalloc(sizeof(*ra), GFP_NOFS);

	mutex_lock(&inode->i_mutex);
	i = start >> PAGE_CACHE_SHIFT;
	last_index = (start + len - 1) >> PAGE_CACHE_SHIFT;

	file_ra_state_init(ra, inode->i_mapping);
	btrfs_force_ra(inode->i_mapping, ra, NULL, i, last_index);
	kfree(ra);

	for (; i <= last_index; i++) {
		page = grab_cache_page(inode->i_mapping, i);
		if (!page)
			goto out_unlock;
		if (!PageUptodate(page)) {
			btrfs_readpage(NULL, page);
			lock_page(page);
			if (!PageUptodate(page)) {
				unlock_page(page);
				page_cache_release(page);
				goto out_unlock;
			}
		}
		page_start = (u64)page->index << PAGE_CACHE_SHIFT;
		page_end = page_start + PAGE_CACHE_SIZE - 1;

		lock_extent(em_tree, page_start, page_end, GFP_NOFS);

		delalloc_start = page_start;
		existing_delalloc =
			count_range_bits(&BTRFS_I(inode)->extent_tree,
					 &delalloc_start, page_end,
					 PAGE_CACHE_SIZE, EXTENT_DELALLOC);

		set_extent_delalloc(em_tree, page_start,
				    page_end, GFP_NOFS);

		spin_lock(&root->fs_info->delalloc_lock);
		root->fs_info->delalloc_bytes += PAGE_CACHE_SIZE -
						 existing_delalloc;
		spin_unlock(&root->fs_info->delalloc_lock);

		unlock_extent(em_tree, page_start, page_end, GFP_NOFS);
		set_page_dirty(page);
		unlock_page(page);
		page_cache_release(page);
	}

out_unlock:
	mutex_unlock(&inode->i_mutex);
	return 0;
}

/*
 * note, this releases the path
 */
static int noinline relocate_one_reference(struct btrfs_root *extent_root,
				  struct btrfs_path *path,
				  struct btrfs_key *extent_key)
{
	struct inode *inode;
	struct btrfs_root *found_root;
	struct btrfs_key *root_location;
	struct btrfs_extent_ref *ref;
	u64 ref_root;
	u64 ref_gen;
	u64 ref_objectid;
	u64 ref_offset;
	int ret;

	ref = btrfs_item_ptr(path->nodes[0], path->slots[0],
			     struct btrfs_extent_ref);
	ref_root = btrfs_ref_root(path->nodes[0], ref);
	ref_gen = btrfs_ref_generation(path->nodes[0], ref);
	ref_objectid = btrfs_ref_objectid(path->nodes[0], ref);
	ref_offset = btrfs_ref_offset(path->nodes[0], ref);
	btrfs_release_path(extent_root, path);

	root_location = kmalloc(sizeof(*root_location), GFP_NOFS);
	root_location->objectid = ref_root;
	if (ref_gen == 0)
		root_location->offset = 0;
	else
		root_location->offset = (u64)-1;
	root_location->type = BTRFS_ROOT_ITEM_KEY;

	found_root = btrfs_read_fs_root_no_name(extent_root->fs_info,
						root_location);
	BUG_ON(!found_root);
	kfree(root_location);

	if (ref_objectid >= BTRFS_FIRST_FREE_OBJECTID) {
		mutex_unlock(&extent_root->fs_info->fs_mutex);
		inode = btrfs_iget_locked(extent_root->fs_info->sb,
					  ref_objectid, found_root);
		if (inode->i_state & I_NEW) {
			/* the inode and parent dir are two different roots */
			BTRFS_I(inode)->root = found_root;
			BTRFS_I(inode)->location.objectid = ref_objectid;
			BTRFS_I(inode)->location.type = BTRFS_INODE_ITEM_KEY;
			BTRFS_I(inode)->location.offset = 0;
			btrfs_read_locked_inode(inode);
			unlock_new_inode(inode);

		}
		/* this can happen if the reference is not against
		 * the latest version of the tree root
		 */
		if (is_bad_inode(inode)) {
			mutex_lock(&extent_root->fs_info->fs_mutex);
			goto out;
		}
		relocate_inode_pages(inode, ref_offset, extent_key->offset);
		/* FIXME, data=ordered will help get rid of this */
		filemap_fdatawrite(inode->i_mapping);
		iput(inode);
		mutex_lock(&extent_root->fs_info->fs_mutex);
	} else {
		struct btrfs_trans_handle *trans;
		struct btrfs_key found_key;
		struct extent_buffer *eb;
		int level;
		int i;

		trans = btrfs_start_transaction(found_root, 1);
		eb = read_tree_block(found_root, extent_key->objectid,
				     extent_key->offset);
		level = btrfs_header_level(eb);

		if (level == 0)
			btrfs_item_key_to_cpu(eb, &found_key, 0);
		else
			btrfs_node_key_to_cpu(eb, &found_key, 0);

		free_extent_buffer(eb);

		path->lowest_level = level;
		path->reada = 2;
		ret = btrfs_search_slot(trans, found_root, &found_key, path,
					0, 1);
		path->lowest_level = 0;
		for (i = level; i < BTRFS_MAX_LEVEL; i++) {
			if (!path->nodes[i])
				break;
			free_extent_buffer(path->nodes[i]);
			path->nodes[i] = NULL;
		}
		btrfs_release_path(found_root, path);
		btrfs_end_transaction(trans, found_root);
	}

out:
	return 0;
}

static int noinline relocate_one_extent(struct btrfs_root *extent_root,
					struct btrfs_path *path,
					struct btrfs_key *extent_key)
{
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	u32 nritems;
	u32 item_size;
	int ret = 0;

	key.objectid = extent_key->objectid;
	key.type = BTRFS_EXTENT_REF_KEY;
	key.offset = 0;

	while(1) {
		ret = btrfs_search_slot(NULL, extent_root, &key, path, 0, 0);

		BUG_ON(ret == 0);

		if (ret < 0)
			goto out;

		ret = 0;
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (path->slots[0] == nritems)
			goto out;

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid != extent_key->objectid)
			break;

		if (found_key.type != BTRFS_EXTENT_REF_KEY)
			break;

		key.offset = found_key.offset + 1;
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);

		ret = relocate_one_reference(extent_root, path, extent_key);
		if (ret)
			goto out;
	}
	ret = 0;
out:
	btrfs_release_path(extent_root, path);
	return ret;
}

int btrfs_shrink_extent_tree(struct btrfs_root *root, u64 new_size)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *tree_root = root->fs_info->tree_root;
	struct btrfs_path *path;
	u64 cur_byte;
	u64 total_found;
	struct btrfs_fs_info *info = root->fs_info;
	struct extent_map_tree *block_group_cache;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	u32 nritems;
	int ret;
	int progress = 0;

	btrfs_set_super_total_bytes(&info->super_copy, new_size);
	block_group_cache = &info->block_group_cache;
	path = btrfs_alloc_path();
	root = root->fs_info->extent_root;
	path->reada = 2;

again:
	total_found = 0;
	key.objectid = new_size;
	key.offset = 0;
	key.type = 0;
	cur_byte = key.objectid;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	ret = find_previous_extent(root, path);
	if (ret < 0)
		goto out;
	if (ret == 0) {
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid + found_key.offset > new_size) {
			cur_byte = found_key.objectid;
			key.objectid = cur_byte;
		}
	}
	btrfs_release_path(root, path);

	while(1) {
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;

		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
next:
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

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		if (progress && need_resched()) {
			memcpy(&key, &found_key, sizeof(key));
			mutex_unlock(&root->fs_info->fs_mutex);
			cond_resched();
			mutex_lock(&root->fs_info->fs_mutex);
			btrfs_release_path(root, path);
			btrfs_search_slot(NULL, root, &key, path, 0, 0);
			progress = 0;
			goto next;
		}
		progress = 1;

		if (btrfs_key_type(&found_key) != BTRFS_EXTENT_ITEM_KEY ||
		    found_key.objectid + found_key.offset <= cur_byte) {
			path->slots[0]++;
			goto next;
		}

		total_found++;
		cur_byte = found_key.objectid + found_key.offset;
		key.objectid = cur_byte;
		btrfs_release_path(root, path);
		ret = relocate_one_extent(root, path, &found_key);
	}

	btrfs_release_path(root, path);

	if (total_found > 0) {
		trans = btrfs_start_transaction(tree_root, 1);
		btrfs_commit_transaction(trans, tree_root);

		mutex_unlock(&root->fs_info->fs_mutex);
		btrfs_clean_old_snapshots(tree_root);
		mutex_lock(&root->fs_info->fs_mutex);

		trans = btrfs_start_transaction(tree_root, 1);
		btrfs_commit_transaction(trans, tree_root);
		goto again;
	}

	trans = btrfs_start_transaction(root, 1);
	key.objectid = new_size;
	key.offset = 0;
	key.type = 0;
	while(1) {
		u64 ptr;

		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0)
			goto out;

		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
bg_next:
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				break;
			if (ret == 1) {
				ret = 0;
				break;
			}
			leaf = path->nodes[0];
			btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

			/*
			 * btrfs_next_leaf doesn't cow buffers, we have to
			 * do the search again
			 */
			memcpy(&key, &found_key, sizeof(key));
			btrfs_release_path(root, path);
			goto resched_check;
		}

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (btrfs_key_type(&found_key) != BTRFS_BLOCK_GROUP_ITEM_KEY) {
			printk("shrinker found key %Lu %u %Lu\n",
				found_key.objectid, found_key.type,
				found_key.offset);
			path->slots[0]++;
			goto bg_next;
		}
		ret = get_state_private(&info->block_group_cache,
					found_key.objectid, &ptr);
		if (!ret)
			kfree((void *)(unsigned long)ptr);

		clear_extent_bits(&info->block_group_cache, found_key.objectid,
				  found_key.objectid + found_key.offset - 1,
				  (unsigned int)-1, GFP_NOFS);

		key.objectid = found_key.objectid + 1;
		btrfs_del_item(trans, root, path);
		btrfs_release_path(root, path);
resched_check:
		if (need_resched()) {
			mutex_unlock(&root->fs_info->fs_mutex);
			cond_resched();
			mutex_lock(&root->fs_info->fs_mutex);
		}
	}
	clear_extent_dirty(&info->free_space_cache, new_size, (u64)-1,
			   GFP_NOFS);
	btrfs_commit_transaction(trans, root);
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_grow_extent_tree(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, u64 new_size)
{
	struct btrfs_path *path;
	u64 nr = 0;
	u64 cur_byte;
	u64 old_size;
	unsigned long rem;
	struct btrfs_block_group_cache *cache;
	struct btrfs_block_group_item *item;
	struct btrfs_fs_info *info = root->fs_info;
	struct extent_map_tree *block_group_cache;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	int ret;
	int bit;

	old_size = btrfs_super_total_bytes(&info->super_copy);
	block_group_cache = &info->block_group_cache;

	root = info->extent_root;

	cache = btrfs_lookup_block_group(root->fs_info, old_size - 1);

	cur_byte = cache->key.objectid + cache->key.offset;
	if (cur_byte >= new_size)
		goto set_size;

	key.offset = BTRFS_BLOCK_GROUP_SIZE;
	btrfs_set_key_type(&key, BTRFS_BLOCK_GROUP_ITEM_KEY);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while(cur_byte < new_size) {
		key.objectid = cur_byte;
		ret = btrfs_insert_empty_item(trans, root, path, &key,
				        sizeof(struct btrfs_block_group_item));
		BUG_ON(ret);
		leaf = path->nodes[0];
		item = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_block_group_item);

		btrfs_set_disk_block_group_used(leaf, item, 0);
		div_long_long_rem(nr, 3, &rem);
		if (rem) {
			btrfs_set_disk_block_group_flags(leaf, item,
						 BTRFS_BLOCK_GROUP_DATA);
		} else {
			btrfs_set_disk_block_group_flags(leaf, item, 0);
		}
		nr++;

		cache = kmalloc(sizeof(*cache), GFP_NOFS);
		BUG_ON(!cache);

		read_extent_buffer(leaf, &cache->item, (unsigned long)item,
				   sizeof(cache->item));

		memcpy(&cache->key, &key, sizeof(key));
		cache->cached = 0;
		cache->pinned = 0;
		cur_byte = key.objectid + key.offset;
		btrfs_release_path(root, path);

		if (cache->item.flags & BTRFS_BLOCK_GROUP_DATA) {
			bit = BLOCK_GROUP_DATA;
			cache->data = BTRFS_BLOCK_GROUP_DATA;
		} else {
			bit = BLOCK_GROUP_METADATA;
			cache->data = 0;
		}

		/* use EXTENT_LOCKED to prevent merging */
		set_extent_bits(block_group_cache, key.objectid,
				key.objectid + key.offset - 1,
				bit | EXTENT_LOCKED, GFP_NOFS);
		set_state_private(block_group_cache, key.objectid,
				  (unsigned long)cache);
	}
	btrfs_free_path(path);
set_size:
	btrfs_set_super_total_bytes(&info->super_copy, new_size);
	return 0;
}

int btrfs_read_block_groups(struct btrfs_root *root)
{
	struct btrfs_path *path;
	int ret;
	int err = 0;
	int bit;
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *info = root->fs_info;
	struct extent_map_tree *block_group_cache;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;

	block_group_cache = &info->block_group_cache;

	root = info->extent_root;
	key.objectid = 0;
	key.offset = BTRFS_BLOCK_GROUP_SIZE;
	btrfs_set_key_type(&key, BTRFS_BLOCK_GROUP_ITEM_KEY);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while(1) {
		ret = btrfs_search_slot(NULL, info->extent_root,
					&key, path, 0, 0);
		if (ret != 0) {
			err = ret;
			break;
		}
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		cache = kmalloc(sizeof(*cache), GFP_NOFS);
		if (!cache) {
			err = -1;
			break;
		}

		read_extent_buffer(leaf, &cache->item,
				   btrfs_item_ptr_offset(leaf, path->slots[0]),
				   sizeof(cache->item));
		memcpy(&cache->key, &found_key, sizeof(found_key));
		cache->cached = 0;
		cache->pinned = 0;
		key.objectid = found_key.objectid + found_key.offset;
		btrfs_release_path(root, path);

		if (cache->item.flags & BTRFS_BLOCK_GROUP_MIXED) {
			bit = BLOCK_GROUP_DATA | BLOCK_GROUP_METADATA;
			cache->data = BTRFS_BLOCK_GROUP_MIXED;
		} else if (cache->item.flags & BTRFS_BLOCK_GROUP_DATA) {
			bit = BLOCK_GROUP_DATA;
			cache->data = BTRFS_BLOCK_GROUP_DATA;
		} else {
			bit = BLOCK_GROUP_METADATA;
			cache->data = 0;
		}

		/* use EXTENT_LOCKED to prevent merging */
		set_extent_bits(block_group_cache, found_key.objectid,
				found_key.objectid + found_key.offset - 1,
				bit | EXTENT_LOCKED, GFP_NOFS);
		set_state_private(block_group_cache, found_key.objectid,
				  (unsigned long)cache);

		if (key.objectid >=
		    btrfs_super_total_bytes(&info->super_copy))
			break;
	}

	btrfs_free_path(path);
	return 0;
}
