// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Western Digital Corporation or its affiliates.
 */

#include <linux/btrfs_tree.h>
#include "ctree.h"
#include "fs.h"
#include "accessors.h"
#include "transaction.h"
#include "disk-io.h"
#include "raid-stripe-tree.h"
#include "volumes.h"
#include "print-tree.h"

int btrfs_delete_raid_extent(struct btrfs_trans_handle *trans, u64 start, u64 length)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *stripe_root = fs_info->stripe_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	u64 found_start;
	u64 found_end;
	u64 end = start + length;
	int slot;
	int ret;

	if (!stripe_root)
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while (1) {
		key.objectid = start;
		key.type = BTRFS_RAID_STRIPE_KEY;
		key.offset = length;

		ret = btrfs_search_slot(trans, stripe_root, &key, path, -1, 1);
		if (ret < 0)
			break;
		if (ret > 0) {
			ret = 0;
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		}

		leaf = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(leaf, &key, slot);
		found_start = key.objectid;
		found_end = found_start + key.offset;

		/* That stripe ends before we start, we're done. */
		if (found_end <= start)
			break;

		trace_btrfs_raid_extent_delete(fs_info, start, end,
					       found_start, found_end);

		ASSERT(found_start >= start && found_end <= end);
		ret = btrfs_del_item(trans, stripe_root, path);
		if (ret)
			break;

		start += key.offset;
		length -= key.offset;
		if (length == 0)
			break;

		btrfs_release_path(path);
	}

	btrfs_free_path(path);
	return ret;
}

static int update_raid_extent_item(struct btrfs_trans_handle *trans,
				   struct btrfs_key *key,
				   struct btrfs_stripe_extent *stripe_extent,
				   const size_t item_size)
{
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int ret;
	int slot;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, trans->fs_info->stripe_root, key, path,
				0, 1);
	if (ret)
		return (ret == 1 ? ret : -EINVAL);

	leaf = path->nodes[0];
	slot = path->slots[0];

	write_extent_buffer(leaf, stripe_extent, btrfs_item_ptr_offset(leaf, slot),
			    item_size);
	btrfs_mark_buffer_dirty(trans, leaf);
	btrfs_free_path(path);

	return ret;
}

static int btrfs_insert_one_raid_extent(struct btrfs_trans_handle *trans,
					struct btrfs_io_context *bioc)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_key stripe_key;
	struct btrfs_root *stripe_root = fs_info->stripe_root;
	const int num_stripes = btrfs_bg_type_to_factor(bioc->map_type);
	struct btrfs_stripe_extent *stripe_extent;
	const size_t item_size = struct_size(stripe_extent, strides, num_stripes);
	int ret;

	stripe_extent = kzalloc(item_size, GFP_NOFS);
	if (!stripe_extent) {
		btrfs_abort_transaction(trans, -ENOMEM);
		btrfs_end_transaction(trans);
		return -ENOMEM;
	}

	trace_btrfs_insert_one_raid_extent(fs_info, bioc->logical, bioc->size,
					   num_stripes);
	for (int i = 0; i < num_stripes; i++) {
		u64 devid = bioc->stripes[i].dev->devid;
		u64 physical = bioc->stripes[i].physical;
		u64 length = bioc->stripes[i].length;
		struct btrfs_raid_stride *raid_stride = &stripe_extent->strides[i];

		if (length == 0)
			length = bioc->size;

		btrfs_set_stack_raid_stride_devid(raid_stride, devid);
		btrfs_set_stack_raid_stride_physical(raid_stride, physical);
	}

	stripe_key.objectid = bioc->logical;
	stripe_key.type = BTRFS_RAID_STRIPE_KEY;
	stripe_key.offset = bioc->size;

	ret = btrfs_insert_item(trans, stripe_root, &stripe_key, stripe_extent,
				item_size);
	if (ret == -EEXIST)
		ret = update_raid_extent_item(trans, &stripe_key, stripe_extent,
					      item_size);
	if (ret)
		btrfs_abort_transaction(trans, ret);

	kfree(stripe_extent);

	return ret;
}

int btrfs_insert_raid_extent(struct btrfs_trans_handle *trans,
			     struct btrfs_ordered_extent *ordered_extent)
{
	struct btrfs_io_context *bioc;
	int ret;

	if (!btrfs_fs_incompat(trans->fs_info, RAID_STRIPE_TREE))
		return 0;

	list_for_each_entry(bioc, &ordered_extent->bioc_list, rst_ordered_entry) {
		ret = btrfs_insert_one_raid_extent(trans, bioc);
		if (ret)
			return ret;
	}

	while (!list_empty(&ordered_extent->bioc_list)) {
		bioc = list_first_entry(&ordered_extent->bioc_list,
					typeof(*bioc), rst_ordered_entry);
		list_del(&bioc->rst_ordered_entry);
		btrfs_put_bioc(bioc);
	}

	return 0;
}

int btrfs_get_raid_extent_offset(struct btrfs_fs_info *fs_info,
				 u64 logical, u64 *length, u64 map_type,
				 u32 stripe_index, struct btrfs_io_stripe *stripe)
{
	struct btrfs_root *stripe_root = fs_info->stripe_root;
	struct btrfs_stripe_extent *stripe_extent;
	struct btrfs_key stripe_key;
	struct btrfs_key found_key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	const u64 end = logical + *length;
	int num_stripes;
	u64 offset;
	u64 found_logical;
	u64 found_length;
	u64 found_end;
	int slot;
	int ret;

	stripe_key.objectid = logical;
	stripe_key.type = BTRFS_RAID_STRIPE_KEY;
	stripe_key.offset = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	if (stripe->rst_search_commit_root) {
		path->skip_locking = 1;
		path->search_commit_root = 1;
	}

	ret = btrfs_search_slot(NULL, stripe_root, &stripe_key, path, 0, 0);
	if (ret < 0)
		goto free_path;
	if (ret) {
		if (path->slots[0] != 0)
			path->slots[0]--;
	}

	while (1) {
		leaf = path->nodes[0];
		slot = path->slots[0];

		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		found_logical = found_key.objectid;
		found_length = found_key.offset;
		found_end = found_logical + found_length;

		if (found_logical > end) {
			ret = -ENOENT;
			goto out;
		}

		if (in_range(logical, found_logical, found_length))
			break;

		ret = btrfs_next_item(stripe_root, path);
		if (ret)
			goto out;
	}

	offset = logical - found_logical;

	/*
	 * If we have a logically contiguous, but physically non-continuous
	 * range, we need to split the bio. Record the length after which we
	 * must split the bio.
	 */
	if (end > found_end)
		*length -= end - found_end;

	num_stripes = btrfs_num_raid_stripes(btrfs_item_size(leaf, slot));
	stripe_extent = btrfs_item_ptr(leaf, slot, struct btrfs_stripe_extent);

	for (int i = 0; i < num_stripes; i++) {
		struct btrfs_raid_stride *stride = &stripe_extent->strides[i];
		u64 devid = btrfs_raid_stride_devid(leaf, stride);
		u64 physical = btrfs_raid_stride_physical(leaf, stride);

		if (devid != stripe->dev->devid)
			continue;

		if ((map_type & BTRFS_BLOCK_GROUP_DUP) && stripe_index != i)
			continue;

		stripe->physical = physical + offset;

		trace_btrfs_get_raid_extent_offset(fs_info, logical, *length,
						   stripe->physical, devid);

		ret = 0;
		goto free_path;
	}

	/* If we're here, we haven't found the requested devid in the stripe. */
	ret = -ENOENT;
out:
	if (ret > 0)
		ret = -ENOENT;
	if (ret && ret != -EIO && !stripe->rst_search_commit_root) {
		btrfs_debug(fs_info,
		"cannot find raid-stripe for logical [%llu, %llu] devid %llu, profile %s",
			  logical, logical + *length, stripe->dev->devid,
			  btrfs_bg_type_to_raid_name(map_type));
	}
free_path:
	btrfs_free_path(path);

	return ret;
}
