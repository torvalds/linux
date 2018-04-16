// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Red Hat.  All rights reserved.
 */

#include "ctree.h"
#include "disk-io.h"

int btrfs_insert_orphan_item(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, u64 offset)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret = 0;

	key.objectid = BTRFS_ORPHAN_OBJECTID;
	key.type = BTRFS_ORPHAN_ITEM_KEY;
	key.offset = offset;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_insert_empty_item(trans, root, path, &key, 0);

	btrfs_free_path(path);
	return ret;
}

int btrfs_del_orphan_item(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, u64 offset)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret = 0;

	key.objectid = BTRFS_ORPHAN_OBJECTID;
	key.type = BTRFS_ORPHAN_ITEM_KEY;
	key.offset = offset;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	if (ret) { /* JDM: Really? */
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, root, path);

out:
	btrfs_free_path(path);
	return ret;
}
