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
#include "misc.h"
#include "print-tree.h"

static int btrfs_insert_one_raid_extent(struct btrfs_trans_handle *trans,
					struct btrfs_io_context *bioc)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_key stripe_key;
	struct btrfs_root *stripe_root = fs_info->stripe_root;
	const int num_stripes = btrfs_bg_type_to_factor(bioc->map_type);
	u8 encoding = btrfs_bg_flags_to_raid_index(bioc->map_type);
	struct btrfs_stripe_extent *stripe_extent;
	const size_t item_size = struct_size(stripe_extent, strides, num_stripes);
	int ret;

	stripe_extent = kzalloc(item_size, GFP_NOFS);
	if (!stripe_extent) {
		btrfs_abort_transaction(trans, -ENOMEM);
		btrfs_end_transaction(trans);
		return -ENOMEM;
	}

	btrfs_set_stack_stripe_extent_encoding(stripe_extent, encoding);
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

	return ret;
}
