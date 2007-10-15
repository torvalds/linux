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
#include "print-tree.h"
#include "transaction.h"

static int finish_current_insert(struct btrfs_trans_handle *trans, struct
				 btrfs_root *extent_root);
static int del_pending_extents(struct btrfs_trans_handle *trans, struct
			       btrfs_root *extent_root);

static int cache_block_group(struct btrfs_root *root,
			     struct btrfs_block_group_cache *block_group)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct radix_tree_root *extent_radix;
	int slot;
	u64 i;
	u64 last = 0;
	u64 hole_size;
	u64 first_free;
	int found = 0;

	root = root->fs_info->extent_root;
	extent_radix = &root->fs_info->extent_map_radix;

	if (block_group->cached)
		return 0;
	if (block_group->data)
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

	if (ret && path->slots[0] > 0)
		path->slots[0]--;

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
			if (key.objectid + key.offset > first_free)
				first_free = key.objectid + key.offset;
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
			hole_size = key.objectid - last;
			for (i = 0; i < hole_size; i++) {
				set_radix_bit(extent_radix, last + i);
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
		for (i = 0; i < hole_size; i++) {
			set_radix_bit(extent_radix, last + i);
		}
	}
	block_group->cached = 1;
err:
	btrfs_free_path(path);
	return 0;
}

struct btrfs_block_group_cache *btrfs_lookup_block_group(struct
							 btrfs_fs_info *info,
							 u64 blocknr)
{
	struct btrfs_block_group_cache *block_group;
	int ret;

	ret = radix_tree_gang_lookup(&info->block_group_radix,
				     (void **)&block_group,
				     blocknr, 1);
	if (ret) {
		if (block_group->key.objectid <= blocknr && blocknr <=
		    block_group->key.objectid + block_group->key.offset)
			return block_group;
	}
	ret = radix_tree_gang_lookup(&info->block_group_data_radix,
				     (void **)&block_group,
				     blocknr, 1);
	if (ret) {
		if (block_group->key.objectid <= blocknr && blocknr <=
		    block_group->key.objectid + block_group->key.offset)
			return block_group;
	}
	return NULL;
}

static u64 leaf_range(struct btrfs_root *root)
{
	u64 size = BTRFS_LEAF_DATA_SIZE(root);
	do_div(size, sizeof(struct btrfs_extent_item) +
		sizeof(struct btrfs_item));
	return size;
}

static u64 find_search_start(struct btrfs_root *root,
			     struct btrfs_block_group_cache **cache_ret,
			     u64 search_start, int num)
{
	unsigned long gang[8];
	int ret;
	struct btrfs_block_group_cache *cache = *cache_ret;
	u64 last = max(search_start, cache->key.objectid);

	if (cache->data)
		goto out;
again:
	ret = cache_block_group(root, cache);
	if (ret)
		goto out;
	while(1) {
		ret = find_first_radix_bit(&root->fs_info->extent_map_radix,
					   gang, last, ARRAY_SIZE(gang));
		if (!ret)
			goto out;
		last = gang[ret-1] + 1;
		if (num > 1) {
			if (ret != ARRAY_SIZE(gang)) {
				goto new_group;
			}
			if (gang[ret-1] - gang[0] > leaf_range(root)) {
				continue;
			}
		}
		if (gang[0] >= cache->key.objectid + cache->key.offset) {
			goto new_group;
		}
		return gang[0];
	}
out:
	return max(cache->last_alloc, search_start);

new_group:
	cache = btrfs_lookup_block_group(root->fs_info,
					 last + cache->key.offset - 1);
	if (!cache) {
		return max((*cache_ret)->last_alloc, search_start);
	}
	cache = btrfs_find_block_group(root, cache,
				       last + cache->key.offset - 1, 0, 0);
	*cache_ret = cache;
	goto again;
}

static u64 div_factor(u64 num, int factor)
{
	num *= factor;
	do_div(num, 10);
	return num;
}

struct btrfs_block_group_cache *btrfs_find_block_group(struct btrfs_root *root,
						 struct btrfs_block_group_cache
						 *hint, u64 search_start,
						 int data, int owner)
{
	struct btrfs_block_group_cache *cache[8];
	struct btrfs_block_group_cache *found_group = NULL;
	struct btrfs_fs_info *info = root->fs_info;
	struct radix_tree_root *radix;
	struct radix_tree_root *swap_radix;
	u64 used;
	u64 last = 0;
	u64 hint_last;
	int i;
	int ret;
	int full_search = 0;
	int factor = 8;
	int data_swap = 0;

	if (!owner)
		factor = 5;

	if (data) {
		radix = &info->block_group_data_radix;
		swap_radix = &info->block_group_radix;
	} else {
		radix = &info->block_group_radix;
		swap_radix = &info->block_group_data_radix;
	}

	if (search_start) {
		struct btrfs_block_group_cache *shint;
		shint = btrfs_lookup_block_group(info, search_start);
		if (shint && shint->data == data) {
			used = btrfs_block_group_used(&shint->item);
			if (used + shint->pinned <
			    div_factor(shint->key.offset, factor)) {
				return shint;
			}
		}
	}
	if (hint && hint->data == data) {
		used = btrfs_block_group_used(&hint->item);
		if (used + hint->pinned <
		    div_factor(hint->key.offset, factor)) {
			return hint;
		}
		if (used >= div_factor(hint->key.offset, 8)) {
			radix_tree_tag_clear(radix,
					     hint->key.objectid +
					     hint->key.offset - 1,
					     BTRFS_BLOCK_GROUP_AVAIL);
		}
		last = hint->key.offset * 3;
		if (hint->key.objectid >= last)
			last = max(search_start + hint->key.offset - 1,
				   hint->key.objectid - last);
		else
			last = hint->key.objectid + hint->key.offset;
		hint_last = last;
	} else {
		if (hint)
			hint_last = max(hint->key.objectid, search_start);
		else
			hint_last = search_start;

		last = hint_last;
	}
	while(1) {
		ret = radix_tree_gang_lookup_tag(radix, (void **)cache,
						 last, ARRAY_SIZE(cache),
						 BTRFS_BLOCK_GROUP_AVAIL);
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			last = cache[i]->key.objectid +
				cache[i]->key.offset;
			used = btrfs_block_group_used(&cache[i]->item);
			if (used + cache[i]->pinned <
			    div_factor(cache[i]->key.offset, factor)) {
				found_group = cache[i];
				goto found;
			}
			if (used >= div_factor(cache[i]->key.offset, 8)) {
				radix_tree_tag_clear(radix,
						     cache[i]->key.objectid +
						     cache[i]->key.offset - 1,
						     BTRFS_BLOCK_GROUP_AVAIL);
			}
		}
		cond_resched();
	}
	last = hint_last;
again:
	while(1) {
		ret = radix_tree_gang_lookup(radix, (void **)cache,
					     last, ARRAY_SIZE(cache));
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			last = cache[i]->key.objectid +
				cache[i]->key.offset;
			used = btrfs_block_group_used(&cache[i]->item);
			if (used + cache[i]->pinned < cache[i]->key.offset) {
				found_group = cache[i];
				goto found;
			}
			if (used >= cache[i]->key.offset) {
				radix_tree_tag_clear(radix,
						     cache[i]->key.objectid +
						     cache[i]->key.offset - 1,
						     BTRFS_BLOCK_GROUP_AVAIL);
			}
		}
		cond_resched();
	}
	if (!full_search) {
		last = search_start;
		full_search = 1;
		goto again;
	}
	if (!data_swap) {
		struct radix_tree_root *tmp = radix;
		data_swap = 1;
		radix = swap_radix;
		swap_radix = tmp;
		last = search_start;
		goto again;
	}
	if (!found_group) {
		ret = radix_tree_gang_lookup(radix,
					     (void **)&found_group, 0, 1);
		if (ret == 0) {
			ret = radix_tree_gang_lookup(swap_radix,
						     (void **)&found_group,
						     0, 1);
		}
		BUG_ON(ret != 1);
	}
found:
	return found_group;
}

int btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				u64 blocknr, u64 num_blocks)
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

	key.objectid = blocknr;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	key.offset = num_blocks;
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
	btrfs_free_path(path);
	finish_current_insert(trans, root->fs_info->extent_root);
	del_pending_extents(trans, root->fs_info->extent_root);
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
			     struct btrfs_root *root, u64 blocknr,
			     u64 num_blocks, u32 *refs)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_extent_item *item;

	path = btrfs_alloc_path();
	key.objectid = blocknr;
	key.offset = num_blocks;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key, path,
				0, 0);
	if (ret < 0)
		goto out;
	if (ret != 0) {
		btrfs_print_leaf(root, path->nodes[0]);
		printk("failed to find block number %Lu\n", blocknr);
		BUG();
	}
	l = path->nodes[0];
	item = btrfs_item_ptr(l, path->slots[0], struct btrfs_extent_item);
	*refs = btrfs_extent_refs(l, item);
out:
	btrfs_free_path(path);
	return 0;
}

int btrfs_inc_root_ref(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root)
{
	return btrfs_inc_extent_ref(trans, root,
				    extent_buffer_blocknr(root->node), 1);
}

int btrfs_inc_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf)
{
	u64 blocknr;
	u32 nritems;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	int i;
	int leaf;
	int ret;
	int faili;
	int err;

	if (!root->ref_cows)
		return 0;

	leaf = btrfs_is_leaf(buf);
	nritems = btrfs_header_nritems(buf);
	for (i = 0; i < nritems; i++) {
		if (leaf) {
			u64 disk_blocknr;
			btrfs_item_key_to_cpu(buf, &key, i);
			if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY)
				continue;
			fi = btrfs_item_ptr(buf, i,
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(buf, fi) ==
			    BTRFS_FILE_EXTENT_INLINE)
				continue;
			disk_blocknr = btrfs_file_extent_disk_blocknr(buf, fi);
			if (disk_blocknr == 0)
				continue;
			ret = btrfs_inc_extent_ref(trans, root, disk_blocknr,
				    btrfs_file_extent_disk_num_blocks(buf, fi));
			if (ret) {
				faili = i;
				goto fail;
			}
		} else {
			blocknr = btrfs_node_blockptr(buf, i);
			ret = btrfs_inc_extent_ref(trans, root, blocknr, 1);
			if (ret) {
				faili = i;
				goto fail;
			}
		}
	}
	return 0;
fail:
	WARN_ON(1);
	for (i =0; i < faili; i++) {
		if (leaf) {
			u64 disk_blocknr;
			btrfs_item_key_to_cpu(buf, &key, i);
			if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY)
				continue;
			fi = btrfs_item_ptr(buf, i,
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(buf, fi) ==
			    BTRFS_FILE_EXTENT_INLINE)
				continue;
			disk_blocknr = btrfs_file_extent_disk_blocknr(buf, fi);
			if (disk_blocknr == 0)
				continue;
			err = btrfs_free_extent(trans, root, disk_blocknr,
				    btrfs_file_extent_disk_num_blocks(buf,
								      fi), 0);
			BUG_ON(err);
		} else {
			blocknr = btrfs_node_blockptr(buf, i);
			err = btrfs_free_extent(trans, root, blocknr, 1, 0);
			BUG_ON(err);
		}
	}
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
	if (cache->data)
		cache->last_alloc = cache->first_free;
	return 0;

}

static int write_dirty_block_radix(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct radix_tree_root *radix)
{
	struct btrfs_block_group_cache *cache[8];
	int ret;
	int err = 0;
	int werr = 0;
	int i;
	struct btrfs_path *path;
	unsigned long off = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while(1) {
		ret = radix_tree_gang_lookup_tag(radix, (void **)cache,
						 off, ARRAY_SIZE(cache),
						 BTRFS_BLOCK_GROUP_DIRTY);
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			err = write_one_cache_group(trans, root,
						    path, cache[i]);
			/*
			 * if we fail to write the cache group, we want
			 * to keep it marked dirty in hopes that a later
			 * write will work
			 */
			if (err) {
				werr = err;
				off = cache[i]->key.objectid +
					cache[i]->key.offset;
				continue;
			}

			radix_tree_tag_clear(radix, cache[i]->key.objectid +
					     cache[i]->key.offset - 1,
					     BTRFS_BLOCK_GROUP_DIRTY);
		}
	}
	btrfs_free_path(path);
	return werr;
}

int btrfs_write_dirty_block_groups(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root)
{
	int ret;
	int ret2;
	ret = write_dirty_block_radix(trans, root,
				      &root->fs_info->block_group_radix);
	ret2 = write_dirty_block_radix(trans, root,
				      &root->fs_info->block_group_data_radix);
	if (ret)
		return ret;
	if (ret2)
		return ret2;
	return 0;
}

static int update_block_group(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      u64 blocknr, u64 num, int alloc, int mark_free,
			      int data)
{
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *info = root->fs_info;
	u64 total = num;
	u64 old_val;
	u64 block_in_group;
	u64 i;
	int ret;

	while(total) {
		cache = btrfs_lookup_block_group(info, blocknr);
		if (!cache) {
			return -1;
		}
		block_in_group = blocknr - cache->key.objectid;
		WARN_ON(block_in_group > cache->key.offset);
		radix_tree_tag_set(cache->radix, cache->key.objectid +
				   cache->key.offset - 1,
				   BTRFS_BLOCK_GROUP_DIRTY);

		old_val = btrfs_block_group_used(&cache->item);
		num = min(total, cache->key.offset - block_in_group);
		if (alloc) {
			if (blocknr > cache->last_alloc)
				cache->last_alloc = blocknr;
			if (!cache->data) {
				for (i = 0; i < num; i++) {
					clear_radix_bit(&info->extent_map_radix,
						        blocknr + i);
				}
			}
			if (cache->data != data &&
			    old_val < (cache->key.offset >> 1)) {
				cache->data = data;
				radix_tree_delete(cache->radix,
						  cache->key.objectid +
						  cache->key.offset - 1);

				if (data) {
					cache->radix =
						&info->block_group_data_radix;
					cache->item.flags |=
						BTRFS_BLOCK_GROUP_DATA;
				} else {
					cache->radix = &info->block_group_radix;
					cache->item.flags &=
						~BTRFS_BLOCK_GROUP_DATA;
				}
				ret = radix_tree_insert(cache->radix,
							cache->key.objectid +
							cache->key.offset - 1,
							(void *)cache);
			}
			old_val += num;
		} else {
			old_val -= num;
			if (blocknr < cache->first_free)
				cache->first_free = blocknr;
			if (!cache->data && mark_free) {
				for (i = 0; i < num; i++) {
					set_radix_bit(&info->extent_map_radix,
						      blocknr + i);
				}
			}
			if (old_val < (cache->key.offset >> 1) &&
			    old_val + num >= (cache->key.offset >> 1)) {
				radix_tree_tag_set(cache->radix,
						   cache->key.objectid +
						   cache->key.offset - 1,
						   BTRFS_BLOCK_GROUP_AVAIL);
			}
		}
		btrfs_set_block_group_used(&cache->item, old_val);
		total -= num;
		blocknr += num;
	}
	return 0;
}

int btrfs_copy_pinned(struct btrfs_root *root, struct radix_tree_root *copy)
{
	unsigned long gang[8];
	u64 last = 0;
	struct radix_tree_root *pinned_radix = &root->fs_info->pinned_radix;
	int ret;
	int i;

	while(1) {
		ret = find_first_radix_bit(pinned_radix, gang, last,
					   ARRAY_SIZE(gang));
		if (!ret)
			break;
		for (i = 0 ; i < ret; i++) {
			set_radix_bit(copy, gang[i]);
			last = gang[i] + 1;
		}
	}
	ret = find_first_radix_bit(&root->fs_info->extent_ins_radix, gang, 0,
				   ARRAY_SIZE(gang));
	WARN_ON(ret);
	return 0;
}

int btrfs_finish_extent_commit(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct radix_tree_root *unpin_radix)
{
	unsigned long gang[8];
	struct btrfs_block_group_cache *block_group;
	u64 first = 0;
	int ret;
	int i;
	struct radix_tree_root *pinned_radix = &root->fs_info->pinned_radix;
	struct radix_tree_root *extent_radix = &root->fs_info->extent_map_radix;

	while(1) {
		ret = find_first_radix_bit(unpin_radix, gang, 0,
					   ARRAY_SIZE(gang));
		if (!ret)
			break;
		if (!first)
			first = gang[0];
		for (i = 0; i < ret; i++) {
			clear_radix_bit(pinned_radix, gang[i]);
			clear_radix_bit(unpin_radix, gang[i]);
			block_group = btrfs_lookup_block_group(root->fs_info,
							       gang[i]);
			if (block_group) {
				WARN_ON(block_group->pinned == 0);
				block_group->pinned--;
				if (gang[i] < block_group->last_alloc)
					block_group->last_alloc = gang[i];
				if (!block_group->data)
					set_radix_bit(extent_radix, gang[i]);
			}
		}
	}
	return 0;
}

static int finish_current_insert(struct btrfs_trans_handle *trans, struct
				 btrfs_root *extent_root)
{
	struct btrfs_key ins;
	struct btrfs_extent_item extent_item;
	int i;
	int ret;
	int err;
	unsigned long gang[8];
	struct btrfs_fs_info *info = extent_root->fs_info;

	btrfs_set_stack_extent_refs(&extent_item, 1);
	ins.offset = 1;
	btrfs_set_key_type(&ins, BTRFS_EXTENT_ITEM_KEY);
	btrfs_set_stack_extent_owner(&extent_item,
				     extent_root->root_key.objectid);

	while(1) {
		ret = find_first_radix_bit(&info->extent_ins_radix, gang, 0,
					   ARRAY_SIZE(gang));
		if (!ret)
			break;

		for (i = 0; i < ret; i++) {
			ins.objectid = gang[i];
			err = btrfs_insert_item(trans, extent_root, &ins,
						&extent_item,
						sizeof(extent_item));
			clear_radix_bit(&info->extent_ins_radix, gang[i]);
			WARN_ON(err);
		}
	}
	return 0;
}

static int pin_down_block(struct btrfs_root *root, u64 blocknr, int pending)
{
	int err;
	struct extent_buffer *buf;

	if (!pending) {
		buf = btrfs_find_tree_block(root, blocknr);
		if (buf) {
			if (btrfs_buffer_uptodate(buf)) {
				u64 transid =
				    root->fs_info->running_transaction->transid;
				if (btrfs_header_generation(buf) == transid) {
					free_extent_buffer(buf);
					return 0;
				}
			}
			free_extent_buffer(buf);
		}
		err = set_radix_bit(&root->fs_info->pinned_radix, blocknr);
		if (!err) {
			struct btrfs_block_group_cache *cache;
			cache = btrfs_lookup_block_group(root->fs_info,
							 blocknr);
			if (cache)
				cache->pinned++;
		}
	} else {
		err = set_radix_bit(&root->fs_info->pending_del_radix, blocknr);
	}
	BUG_ON(err < 0);
	return 0;
}

/*
 * remove an extent from the root, returns 0 on success
 */
static int __free_extent(struct btrfs_trans_handle *trans, struct btrfs_root
			 *root, u64 blocknr, u64 num_blocks, int pin,
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

	key.objectid = blocknr;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	key.offset = num_blocks;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

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
		u64 super_blocks_used, root_blocks_used;

		if (pin) {
			ret = pin_down_block(root, blocknr, 0);
			BUG_ON(ret);
		}

		/* block accounting for super block */
		super_blocks_used = btrfs_super_blocks_used(&info->super_copy);
		btrfs_set_super_blocks_used(&info->super_copy,
					    super_blocks_used - num_blocks);

		/* block accounting for root item */
		root_blocks_used = btrfs_root_used(&root->root_item);
		btrfs_set_root_used(&root->root_item,
					   root_blocks_used - num_blocks);

		ret = btrfs_del_item(trans, extent_root, path);
		if (ret) {
			return ret;
		}
		ret = update_block_group(trans, root, blocknr, num_blocks, 0,
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
	int wret;
	int err = 0;
	unsigned long gang[4];
	int i;
	struct radix_tree_root *pending_radix;
	struct radix_tree_root *pinned_radix;
	struct btrfs_block_group_cache *cache;

	pending_radix = &extent_root->fs_info->pending_del_radix;
	pinned_radix = &extent_root->fs_info->pinned_radix;

	while(1) {
		ret = find_first_radix_bit(pending_radix, gang, 0,
					   ARRAY_SIZE(gang));
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			wret = set_radix_bit(pinned_radix, gang[i]);
			if (wret == 0) {
				cache =
				  btrfs_lookup_block_group(extent_root->fs_info,
							   gang[i]);
				if (cache)
					cache->pinned++;
			}
			if (wret < 0) {
				printk(KERN_CRIT "set_radix_bit, err %d\n",
				       wret);
				BUG_ON(wret < 0);
			}
			wret = clear_radix_bit(pending_radix, gang[i]);
			BUG_ON(wret);
			wret = __free_extent(trans, extent_root,
					     gang[i], 1, 0, 0);
			if (wret)
				err = wret;
		}
	}
	return err;
}

/*
 * remove an extent from the root, returns 0 on success
 */
int btrfs_free_extent(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, u64 blocknr, u64 num_blocks, int pin)
{
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	int pending_ret;
	int ret;

	if (root == extent_root) {
		pin_down_block(root, blocknr, 1);
		return 0;
	}
	ret = __free_extent(trans, root, blocknr, num_blocks, pin, pin == 0);
	pending_ret = del_pending_extents(trans, root->fs_info->extent_root);
	return ret ? ret : pending_ret;
}

/*
 * walks the btree of allocated extents and find a hole of a given size.
 * The key ins is changed to record the hole:
 * ins->objectid == block start
 * ins->flags = BTRFS_EXTENT_ITEM_KEY
 * ins->offset == number of blocks
 * Any available blocks before search_start are skipped.
 */
static int find_free_extent(struct btrfs_trans_handle *trans, struct btrfs_root
			    *orig_root, u64 num_blocks, u64 empty_size,
			    u64 search_start, u64 search_end, u64 hint_block,
			    struct btrfs_key *ins, u64 exclude_start,
			    u64 exclude_nr, int data)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret;
	u64 hole_size = 0;
	int slot = 0;
	u64 last_block = 0;
	u64 test_block;
	u64 orig_search_start = search_start;
	int start_found;
	struct extent_buffer *l;
	struct btrfs_root * root = orig_root->fs_info->extent_root;
	struct btrfs_fs_info *info = root->fs_info;
	int total_needed = num_blocks;
	int level;
	struct btrfs_block_group_cache *block_group;
	int full_scan = 0;
	int wrapped = 0;

	WARN_ON(num_blocks < 1);
	btrfs_set_key_type(ins, BTRFS_EXTENT_ITEM_KEY);

	level = btrfs_header_level(root->node);

	if (search_end == (u64)-1)
		search_end = btrfs_super_total_blocks(&info->super_copy);
	if (hint_block) {
		block_group = btrfs_lookup_block_group(info, hint_block);
		block_group = btrfs_find_block_group(root, block_group,
						     hint_block, data, 1);
	} else {
		block_group = btrfs_find_block_group(root,
						     trans->block_group, 0,
						     data, 1);
	}

	total_needed += empty_size;
	path = btrfs_alloc_path();

check_failed:
	if (!block_group->data)
		search_start = find_search_start(root, &block_group,
						 search_start, total_needed);
	else if (!full_scan)
		search_start = max(block_group->last_alloc, search_start);

	btrfs_init_path(path);
	ins->objectid = search_start;
	ins->offset = 0;
	start_found = 0;
	path->reada = 2;

	ret = btrfs_search_slot(trans, root, ins, path, 0, 0);
	if (ret < 0)
		goto error;

	if (path->slots[0] > 0) {
		path->slots[0]--;
	}

	l = path->nodes[0];
	btrfs_item_key_to_cpu(l, &key, path->slots[0]);

	/*
	 * a rare case, go back one key if we hit a block group item
	 * instead of an extent item
	 */
	if (btrfs_key_type(&key) != BTRFS_EXTENT_ITEM_KEY &&
	    key.objectid + key.offset >= search_start) {
		ins->objectid = key.objectid;
		ins->offset = key.offset - 1;
		btrfs_release_path(root, path);
		ret = btrfs_search_slot(trans, root, ins, path, 0, 0);
		if (ret < 0)
			goto error;

		if (path->slots[0] > 0) {
			path->slots[0]--;
		}
	}

	while (1) {
		l = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(l)) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto error;
			if (!start_found) {
				ins->objectid = search_start;
				ins->offset = search_end - search_start;
				start_found = 1;
				goto check_pending;
			}
			ins->objectid = last_block > search_start ?
					last_block : search_start;
			ins->offset = search_end - ins->objectid;
			goto check_pending;
		}

		btrfs_item_key_to_cpu(l, &key, slot);
		if (key.objectid >= search_start && key.objectid > last_block &&
		    start_found) {
			if (last_block < search_start)
				last_block = search_start;
			hole_size = key.objectid - last_block;
			if (hole_size >= num_blocks) {
				ins->objectid = last_block;
				ins->offset = hole_size;
				goto check_pending;
			}
		}

		if (btrfs_key_type(&key) != BTRFS_EXTENT_ITEM_KEY)
			goto next;

		start_found = 1;
		last_block = key.objectid + key.offset;
		if (!full_scan && last_block >= block_group->key.objectid +
		    block_group->key.offset) {
			btrfs_release_path(root, path);
			search_start = block_group->key.objectid +
				block_group->key.offset * 2;
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

	if (ins->objectid + num_blocks >= search_end)
		goto enospc;

	for (test_block = ins->objectid;
	     test_block < ins->objectid + num_blocks; test_block++) {
		if (test_radix_bit(&info->pinned_radix, test_block) ||
		    test_radix_bit(&info->extent_ins_radix, test_block)) {
			search_start = test_block + 1;
			goto new_group;
		}
	}
	if (exclude_nr > 0 && (ins->objectid + num_blocks > exclude_start &&
	    ins->objectid < exclude_start + exclude_nr)) {
		search_start = exclude_start + exclude_nr;
		goto new_group;
	}
	if (!data) {
		block_group = btrfs_lookup_block_group(info, ins->objectid);
		if (block_group)
			trans->block_group = block_group;
	}
	ins->offset = num_blocks;
	btrfs_free_path(path);
	return 0;

new_group:
	if (search_start + num_blocks >= search_end) {
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
		} else
			wrapped = 1;
	}
	block_group = btrfs_lookup_block_group(info, search_start);
	cond_resched();
	if (!full_scan)
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
		       struct btrfs_root *root, u64 owner,
		       u64 num_blocks, u64 empty_size, u64 hint_block,
		       u64 search_end, struct btrfs_key *ins, int data)
{
	int ret;
	int pending_ret;
	u64 super_blocks_used, root_blocks_used;
	u64 search_start = 0;
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_root *extent_root = info->extent_root;
	struct btrfs_extent_item extent_item;

	btrfs_set_stack_extent_refs(&extent_item, 1);
	btrfs_set_stack_extent_owner(&extent_item, owner);

	WARN_ON(num_blocks < 1);
	ret = find_free_extent(trans, root, num_blocks, empty_size,
			       search_start, search_end, hint_block, ins,
			       trans->alloc_exclude_start,
			       trans->alloc_exclude_nr, data);
	BUG_ON(ret);
	if (ret)
		return ret;

	/* block accounting for super block */
	super_blocks_used = btrfs_super_blocks_used(&info->super_copy);
	btrfs_set_super_blocks_used(&info->super_copy, super_blocks_used +
				    num_blocks);

	/* block accounting for root item */
	root_blocks_used = btrfs_root_used(&root->root_item);
	btrfs_set_root_used(&root->root_item, root_blocks_used +
				   num_blocks);

	if (root == extent_root) {
		BUG_ON(num_blocks != 1);
		set_radix_bit(&root->fs_info->extent_ins_radix, ins->objectid);
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
					     struct btrfs_root *root, u64 hint,
					     u64 empty_size)
{
	struct btrfs_key ins;
	int ret;
	struct extent_buffer *buf;

	ret = btrfs_alloc_extent(trans, root, root->root_key.objectid,
				 1, empty_size, hint, (u64)-1, &ins, 0);
	if (ret) {
		BUG_ON(ret > 0);
		return ERR_PTR(ret);
	}
	buf = btrfs_find_create_tree_block(root, ins.objectid);
	if (!buf) {
		btrfs_free_extent(trans, root, ins.objectid, 1, 0);
		return ERR_PTR(-ENOMEM);
	}
	btrfs_set_buffer_uptodate(buf);
	set_extent_dirty(&trans->transaction->dirty_pages, buf->start,
			 buf->start + buf->len - 1, GFP_NOFS);
	/*
	set_buffer_checked(buf);
	set_buffer_defrag(buf);
	*/
	/* FIXME!!!!!!!!!!!!!!!!
	set_radix_bit(&trans->transaction->dirty_pages, buf->pages[0]->index);
	*/
	trans->blocks_used++;
	return buf;
}

static int drop_leaf_ref(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root, struct extent_buffer *leaf)
{
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	int i;
	int nritems;
	int ret;

	BUG_ON(!btrfs_is_leaf(leaf));
	nritems = btrfs_header_nritems(leaf);
	for (i = 0; i < nritems; i++) {
		u64 disk_blocknr;

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
		disk_blocknr = btrfs_file_extent_disk_blocknr(leaf, fi);
		if (disk_blocknr == 0)
			continue;
		ret = btrfs_free_extent(trans, root, disk_blocknr,
				btrfs_file_extent_disk_num_blocks(leaf, fi), 0);
		BUG_ON(ret);
	}
	return 0;
}

static void reada_walk_down(struct btrfs_root *root,
			    struct extent_buffer *node)
{
	int i;
	u32 nritems;
	u64 blocknr;
	int ret;
	u32 refs;

	nritems = btrfs_header_nritems(node);
	for (i = 0; i < nritems; i++) {
		blocknr = btrfs_node_blockptr(node, i);
		ret = lookup_extent_ref(NULL, root, blocknr, 1, &refs);
		BUG_ON(ret);
		if (refs != 1)
			continue;
		mutex_unlock(&root->fs_info->fs_mutex);
		ret = readahead_tree_block(root, blocknr);
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
static int walk_down_tree(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, struct btrfs_path *path, int *level)
{
	struct extent_buffer *next;
	struct extent_buffer *cur;
	u64 blocknr;
	int ret;
	u32 refs;

	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);
	ret = lookup_extent_ref(trans, root,
				extent_buffer_blocknr(path->nodes[*level]),
				1, &refs);
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
		blocknr = btrfs_node_blockptr(cur, path->slots[*level]);
		ret = lookup_extent_ref(trans, root, blocknr, 1, &refs);
		BUG_ON(ret);
		if (refs != 1) {
			path->slots[*level]++;
			ret = btrfs_free_extent(trans, root, blocknr, 1, 1);
			BUG_ON(ret);
			continue;
		}
		next = btrfs_find_tree_block(root, blocknr);
		if (!next || !btrfs_buffer_uptodate(next)) {
			free_extent_buffer(next);
			mutex_unlock(&root->fs_info->fs_mutex);
			next = read_tree_block(root, blocknr);
			mutex_lock(&root->fs_info->fs_mutex);

			/* we dropped the lock, check one more time */
			ret = lookup_extent_ref(trans, root, blocknr, 1, &refs);
			BUG_ON(ret);
			if (refs != 1) {
				path->slots[*level]++;
				free_extent_buffer(next);
				ret = btrfs_free_extent(trans, root,
							blocknr, 1, 1);
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
	ret = btrfs_free_extent(trans, root,
			extent_buffer_blocknr(path->nodes[*level]), 1, 1);
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
static int walk_up_tree(struct btrfs_trans_handle *trans, struct btrfs_root
			*root, struct btrfs_path *path, int *level)
{
	int i;
	int slot;
	int ret;
	struct btrfs_root_item *root_item = &root->root_item;

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
			ret = btrfs_free_extent(trans, root,
				    extent_buffer_blocknr(path->nodes[*level]),
				    1, 1);
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
		extent_buffer_get(root->node);
		break;
	}
	for (i = 0; i <= orig_level; i++) {
		if (path->nodes[i]) {
			free_extent_buffer(path->nodes[i]);
			path->nodes[i] = 0;
		}
	}
out:
	btrfs_free_path(path);
	return ret;
}

static int free_block_group_radix(struct radix_tree_root *radix)
{
	int ret;
	struct btrfs_block_group_cache *cache[8];
	int i;

	while(1) {
		ret = radix_tree_gang_lookup(radix, (void **)cache, 0,
					     ARRAY_SIZE(cache));
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			radix_tree_delete(radix, cache[i]->key.objectid +
					  cache[i]->key.offset - 1);
			kfree(cache[i]);
		}
	}
	return 0;
}

int btrfs_free_block_groups(struct btrfs_fs_info *info)
{
	int ret;
	int ret2;
	unsigned long gang[16];
	int i;

	ret = free_block_group_radix(&info->block_group_radix);
	ret2 = free_block_group_radix(&info->block_group_data_radix);
	if (ret)
		return ret;
	if (ret2)
		return ret2;

	while(1) {
		ret = find_first_radix_bit(&info->extent_map_radix,
					   gang, 0, ARRAY_SIZE(gang));
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			clear_radix_bit(&info->extent_map_radix, gang[i]);
		}
	}
	return 0;
}

int btrfs_read_block_groups(struct btrfs_root *root)
{
	struct btrfs_path *path;
	int ret;
	int err = 0;
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *info = root->fs_info;
	struct radix_tree_root *radix;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	u64 group_size_blocks;
	u64 used;

	group_size_blocks = BTRFS_BLOCK_GROUP_SIZE >>
		root->fs_info->sb->s_blocksize_bits;
	root = info->extent_root;
	key.objectid = 0;
	key.offset = group_size_blocks;
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
		if (cache->item.flags & BTRFS_BLOCK_GROUP_DATA) {
			radix = &info->block_group_data_radix;
			cache->data = 1;
		} else {
			radix = &info->block_group_radix;
			cache->data = 0;
		}

		memcpy(&cache->key, &found_key, sizeof(found_key));
		cache->last_alloc = cache->key.objectid;
		cache->first_free = cache->key.objectid;
		cache->pinned = 0;
		cache->cached = 0;

		cache->radix = radix;

		key.objectid = found_key.objectid + found_key.offset;
		btrfs_release_path(root, path);

		ret = radix_tree_insert(radix, found_key.objectid +
					found_key.offset - 1,
					(void *)cache);
		BUG_ON(ret);
		used = btrfs_block_group_used(&cache->item);
		if (used < div_factor(key.offset, 8)) {
			radix_tree_tag_set(radix, found_key.objectid +
					   found_key.offset - 1,
					   BTRFS_BLOCK_GROUP_AVAIL);
		}
		if (key.objectid >=
		    btrfs_super_total_blocks(&info->super_copy))
			break;
	}

	btrfs_free_path(path);
	return 0;
}
