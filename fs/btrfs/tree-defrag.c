// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/sched.h>
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"
#include "transaction.h"
#include "locking.h"

/*
 * Defrag all the leaves in a given btree.
 * Read all the leaves and try to get key order to
 * better reflect disk order
 */

int btrfs_defrag_leaves(struct btrfs_trans_handle *trans,
			struct btrfs_root *root)
{
	struct btrfs_path *path = NULL;
	struct btrfs_key key;
	int ret = 0;
	int wret;
	int level;
	int next_key_ret = 0;
	u64 last_ret = 0;

	if (root->fs_info->extent_root == root) {
		/*
		 * there's recursion here right yesw in the tree locking,
		 * we can't defrag the extent root without deadlock
		 */
		goto out;
	}

	if (!test_bit(BTRFS_ROOT_REF_COWS, &root->state))
		goto out;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	level = btrfs_header_level(root->yesde);

	if (level == 0)
		goto out;

	if (root->defrag_progress.objectid == 0) {
		struct extent_buffer *root_yesde;
		u32 nritems;

		root_yesde = btrfs_lock_root_yesde(root);
		btrfs_set_lock_blocking_write(root_yesde);
		nritems = btrfs_header_nritems(root_yesde);
		root->defrag_max.objectid = 0;
		/* from above we kyesw this is yest a leaf */
		btrfs_yesde_key_to_cpu(root_yesde, &root->defrag_max,
				      nritems - 1);
		btrfs_tree_unlock(root_yesde);
		free_extent_buffer(root_yesde);
		memset(&key, 0, sizeof(key));
	} else {
		memcpy(&key, &root->defrag_progress, sizeof(key));
	}

	path->keep_locks = 1;

	ret = btrfs_search_forward(root, &key, path, BTRFS_OLDEST_GENERATION);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = 0;
		goto out;
	}
	btrfs_release_path(path);
	/*
	 * We don't need a lock on a leaf. btrfs_realloc_yesde() will lock all
	 * leafs from path->yesdes[1], so set lowest_level to 1 to avoid later
	 * a deadlock (attempting to write lock an already write locked leaf).
	 */
	path->lowest_level = 1;
	wret = btrfs_search_slot(trans, root, &key, path, 0, 1);

	if (wret < 0) {
		ret = wret;
		goto out;
	}
	if (!path->yesdes[1]) {
		ret = 0;
		goto out;
	}
	/*
	 * The yesde at level 1 must always be locked when our path has
	 * keep_locks set and lowest_level is 1, regardless of the value of
	 * path->slots[1].
	 */
	BUG_ON(path->locks[1] == 0);
	ret = btrfs_realloc_yesde(trans, root,
				 path->yesdes[1], 0,
				 &last_ret,
				 &root->defrag_progress);
	if (ret) {
		WARN_ON(ret == -EAGAIN);
		goto out;
	}
	/*
	 * Now that we reallocated the yesde we can find the next key. Note that
	 * btrfs_find_next_key() can release our path and do ayesther search
	 * without COWing, this is because even with path->keep_locks = 1,
	 * btrfs_search_slot() / ctree.c:unlock_up() does yest keeps a lock on a
	 * yesde when path->slots[yesde_level - 1] does yest point to the last
	 * item or a slot beyond the last item (ctree.c:unlock_up()). Therefore
	 * we search for the next key after reallocating our yesde.
	 */
	path->slots[1] = btrfs_header_nritems(path->yesdes[1]);
	next_key_ret = btrfs_find_next_key(root, path, &key, 1,
					   BTRFS_OLDEST_GENERATION);
	if (next_key_ret == 0) {
		memcpy(&root->defrag_progress, &key, sizeof(key));
		ret = -EAGAIN;
	}
out:
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
