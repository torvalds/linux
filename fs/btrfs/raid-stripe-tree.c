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

static int btrfs_partially_delete_raid_extent(struct btrfs_trans_handle *trans,
					       struct btrfs_path *path,
					       const struct btrfs_key *oldkey,
					       u64 newlen, u64 frontpad)
{
	struct btrfs_root *stripe_root = trans->fs_info->stripe_root;
	struct btrfs_stripe_extent *extent, *newitem;
	struct extent_buffer *leaf;
	int slot;
	size_t item_size;
	struct btrfs_key newkey = {
		.objectid = oldkey->objectid + frontpad,
		.type = BTRFS_RAID_STRIPE_KEY,
		.offset = newlen,
	};
	int ret;

	ASSERT(newlen > 0);
	ASSERT(oldkey->type == BTRFS_RAID_STRIPE_KEY);

	leaf = path->nodes[0];
	slot = path->slots[0];
	item_size = btrfs_item_size(leaf, slot);

	newitem = kzalloc(item_size, GFP_NOFS);
	if (!newitem)
		return -ENOMEM;

	extent = btrfs_item_ptr(leaf, slot, struct btrfs_stripe_extent);

	for (int i = 0; i < btrfs_num_raid_stripes(item_size); i++) {
		struct btrfs_raid_stride *stride = &extent->strides[i];
		u64 phys;

		phys = btrfs_raid_stride_physical(leaf, stride) + frontpad;
		btrfs_set_stack_raid_stride_physical(&newitem->strides[i], phys);
	}

	ret = btrfs_del_item(trans, stripe_root, path);
	if (ret)
		goto out;

	btrfs_release_path(path);
	ret = btrfs_insert_item(trans, stripe_root, &newkey, newitem, item_size);

out:
	kfree(newitem);
	return ret;
}

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

	if (!btrfs_fs_incompat(fs_info, RAID_STRIPE_TREE) || !stripe_root)
		return 0;

	if (!btrfs_is_testing(fs_info)) {
		struct btrfs_chunk_map *map;
		bool use_rst;

		map = btrfs_find_chunk_map(fs_info, start, length);
		if (!map)
			return -EINVAL;
		use_rst = btrfs_need_stripe_tree_update(fs_info, map->type);
		btrfs_free_chunk_map(map);
		if (!use_rst)
			return 0;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while (1) {
		key.objectid = start;
		key.type = BTRFS_RAID_STRIPE_KEY;
		key.offset = 0;

		ret = btrfs_search_slot(trans, stripe_root, &key, path, -1, 1);
		if (ret < 0)
			break;

		if (path->slots[0] == btrfs_header_nritems(path->nodes[0]))
			path->slots[0]--;

		leaf = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(leaf, &key, slot);
		found_start = key.objectid;
		found_end = found_start + key.offset;
		ret = 0;

		/*
		 * The stripe extent starts before the range we want to delete,
		 * but the range spans more than one stripe extent:
		 *
		 * |--- RAID Stripe Extent ---||--- RAID Stripe Extent ---|
		 *        |--- keep  ---|--- drop ---|
		 *
		 * This means we have to get the previous item, truncate its
		 * length and then restart the search.
		 */
		if (found_start > start) {
			if (slot == 0) {
				ret = btrfs_previous_item(stripe_root, path, start,
							  BTRFS_RAID_STRIPE_KEY);
				if (ret) {
					if (ret > 0)
						ret = -ENOENT;
					break;
				}
			} else {
				path->slots[0]--;
			}

			leaf = path->nodes[0];
			slot = path->slots[0];
			btrfs_item_key_to_cpu(leaf, &key, slot);
			found_start = key.objectid;
			found_end = found_start + key.offset;
			ASSERT(found_start <= start);
		}

		if (key.type != BTRFS_RAID_STRIPE_KEY)
			break;

		/* That stripe ends before we start, we're done. */
		if (found_end <= start)
			break;

		trace_btrfs_raid_extent_delete(fs_info, start, end,
					       found_start, found_end);

		/*
		 * The stripe extent starts before the range we want to delete
		 * and ends after the range we want to delete, i.e. we're
		 * punching a hole in the stripe extent:
		 *
		 *  |--- RAID Stripe Extent ---|
		 *  | keep |--- drop ---| keep |
		 *
		 * This means we need to a) truncate the existing item and b)
		 * create a second item for the remaining range.
		 */
		if (found_start < start && found_end > end) {
			size_t item_size;
			u64 diff_start = start - found_start;
			u64 diff_end = found_end - end;
			struct btrfs_stripe_extent *extent;
			struct btrfs_key newkey = {
				.objectid = end,
				.type = BTRFS_RAID_STRIPE_KEY,
				.offset = diff_end,
			};

			/* The "right" item. */
			ret = btrfs_duplicate_item(trans, stripe_root, path, &newkey);
			if (ret)
				break;

			item_size = btrfs_item_size(leaf, path->slots[0]);
			extent = btrfs_item_ptr(leaf, path->slots[0],
						struct btrfs_stripe_extent);

			for (int i = 0; i < btrfs_num_raid_stripes(item_size); i++) {
				struct btrfs_raid_stride *stride = &extent->strides[i];
				u64 phys;

				phys = btrfs_raid_stride_physical(leaf, stride);
				phys += diff_start + length;
				btrfs_set_raid_stride_physical(leaf, stride, phys);
			}

			/* The "left" item. */
			path->slots[0]--;
			btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
			btrfs_partially_delete_raid_extent(trans, path, &key,
							   diff_start, 0);
			break;
		}

		/*
		 * The stripe extent starts before the range we want to delete:
		 *
		 * |--- RAID Stripe Extent ---|
		 * |--- keep  ---|--- drop ---|
		 *
		 * This means we have to duplicate the tree item, truncate the
		 * length to the new size and then re-insert the item.
		 */
		if (found_start < start) {
			u64 diff_start = start - found_start;

			btrfs_partially_delete_raid_extent(trans, path, &key,
							   diff_start, 0);

			start += (key.offset - diff_start);
			length -= (key.offset - diff_start);
			if (length == 0)
				break;

			btrfs_release_path(path);
			continue;
		}

		/*
		 * The stripe extent ends after the range we want to delete:
		 *
		 * |--- RAID Stripe Extent ---|
		 * |--- drop  ---|--- keep ---|
		 *
		 * This means we have to duplicate the tree item, truncate the
		 * length to the new size and then re-insert the item.
		 */
		if (found_end > end) {
			u64 diff_end = found_end - end;

			btrfs_partially_delete_raid_extent(trans, path, &key,
							   key.offset - length,
							   length);
			ASSERT(key.offset - diff_end == length);
			break;
		}

		/* Finally we can delete the whole item, no more special cases. */
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
	btrfs_free_path(path);

	return ret;
}

EXPORT_FOR_TESTS
int btrfs_insert_one_raid_extent(struct btrfs_trans_handle *trans,
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
		struct btrfs_raid_stride *raid_stride = &stripe_extent->strides[i];

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
			ret = -ENODATA;
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
	ret = -ENODATA;
out:
	if (ret > 0)
		ret = -ENODATA;
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
