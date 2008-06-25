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
#include "locking.h"

int btrfs_defrag_leaves(struct btrfs_trans_handle *trans,
			struct btrfs_root *root, int cache_only)
{
	struct btrfs_path *path = NULL;
	struct btrfs_key key;
	int ret = 0;
	int wret;
	int level;
	int orig_level;
	int is_extent = 0;
	int next_key_ret = 0;
	u64 last_ret = 0;
	u64 min_trans = 0;

	if (cache_only)
		goto out;

	if (root->fs_info->extent_root == root) {
		/*
		 * there's recursion here right now in the tree locking,
		 * we can't defrag the extent root without deadlock
		 */
		goto out;
	}

	if (root->ref_cows == 0 && !is_extent)
		goto out;

	if (btrfs_test_opt(root, SSD))
		goto out;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	level = btrfs_header_level(root->node);
	orig_level = level;

	if (level == 0) {
		goto out;
	}
	if (root->defrag_progress.objectid == 0) {
		struct extent_buffer *root_node;
		u32 nritems;

		root_node = btrfs_lock_root_node(root);
		nritems = btrfs_header_nritems(root_node);
		root->defrag_max.objectid = 0;
		/* from above we know this is not a leaf */
		btrfs_node_key_to_cpu(root_node, &root->defrag_max,
				      nritems - 1);
		btrfs_tree_unlock(root_node);
		free_extent_buffer(root_node);
		memset(&key, 0, sizeof(key));
	} else {
		memcpy(&key, &root->defrag_progress, sizeof(key));
	}

	path->lowest_level = 1;
	path->keep_locks = 1;
	if (cache_only)
		min_trans = root->defrag_trans_start;

	ret = btrfs_search_forward(root, &key, path, cache_only, min_trans);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = 0;
		goto out;
	}
	btrfs_release_path(root, path);
	wret = btrfs_search_slot(trans, root, &key, path, 0, 1);

	if (wret < 0) {
		ret = wret;
		goto out;
	}
	if (!path->nodes[1]) {
		ret = 0;
		goto out;
	}
	path->slots[1] = btrfs_header_nritems(path->nodes[1]);
	next_key_ret = btrfs_find_next_key(root, path, &key, 1, cache_only,
					   min_trans);
	ret = btrfs_realloc_node(trans, root,
				 path->nodes[1], 0,
				 cache_only, &last_ret,
				 &root->defrag_progress);
	WARN_ON(ret && ret != -EAGAIN);
	if (next_key_ret == 0) {
		memcpy(&root->defrag_progress, &key, sizeof(key));
		ret = -EAGAIN;
	}

	btrfs_release_path(root, path);
	if (is_extent)
		btrfs_extent_post_op(trans, root);
out:
	if (is_extent)
		mutex_unlock(&root->fs_info->alloc_mutex);

	if (path)
		btrfs_free_path(path);
	if (ret == -EAGAIN) {
		if (root->defrag_max.objectid > root->defrag_progress.objectid)
			goto done;
		if (root->defrag_max.type > root->defrag_progress.type)
			goto done;
		if (root->defrag_max.offset > root->defrag_progress.offset)
			goto done;
		ret = 0;
	}
done:
	if (ret != -EAGAIN) {
		memset(&root->defrag_progress, 0,
		       sizeof(root->defrag_progress));
		root->defrag_trans_start = trans->transid;
	}
	return ret;
}
