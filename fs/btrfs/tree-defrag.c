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

static void reada_defrag(struct btrfs_root *root,
			 struct btrfs_node *node)
{
	int i;
	u32 nritems;
	u64 blocknr;
	int ret;

	nritems = btrfs_header_nritems(&node->header);
	for (i = 0; i < nritems; i++) {
		blocknr = btrfs_node_blockptr(node, i);
		ret = readahead_tree_block(root, blocknr);
		if (ret)
			break;
	}
}

static int defrag_walk_down(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct btrfs_path *path, int *level,
			    int cache_only)
{
	struct buffer_head *next;
	struct buffer_head *cur;
	u64 blocknr;
	int ret = 0;

	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);

	while(*level > 0) {
		WARN_ON(*level < 0);
		WARN_ON(*level >= BTRFS_MAX_LEVEL);
		cur = path->nodes[*level];

		if (!cache_only && *level > 1 && path->slots[*level] == 0)
			reada_defrag(root, btrfs_buffer_node(cur));

		if (btrfs_header_level(btrfs_buffer_header(cur)) != *level)
			WARN_ON(1);

		if (path->slots[*level] >=
		    btrfs_header_nritems(btrfs_buffer_header(cur)))
			break;

		if (*level == 1) {
			ret = btrfs_realloc_node(trans, root,
						 path->nodes[*level],
						 cache_only);
			break;
		}
		blocknr = btrfs_node_blockptr(btrfs_buffer_node(cur),
					      path->slots[*level]);

		if (cache_only) {
			next = btrfs_find_tree_block(root, blocknr);
			if (!next || !buffer_uptodate(next) ||
			   buffer_locked(next)) {
				brelse(next);
				path->slots[*level]++;
				continue;
			}
		} else {
			next = read_tree_block(root, blocknr);
		}
		ret = btrfs_cow_block(trans, root, next, path->nodes[*level],
				      path->slots[*level], &next);
		BUG_ON(ret);
		ret = btrfs_realloc_node(trans, root, next, cache_only);
		BUG_ON(ret);
		WARN_ON(*level <= 0);
		if (path->nodes[*level-1])
			btrfs_block_release(root, path->nodes[*level-1]);
		path->nodes[*level-1] = next;
		*level = btrfs_header_level(btrfs_buffer_header(next));
		path->slots[*level] = 0;
	}
	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);
	btrfs_block_release(root, path->nodes[*level]);
	path->nodes[*level] = NULL;
	*level += 1;
	WARN_ON(ret);
	return 0;
}

static int defrag_walk_up(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  struct btrfs_path *path, int *level,
			  int cache_only)
{
	int i;
	int slot;
	struct btrfs_node *node;

	for(i = *level; i < BTRFS_MAX_LEVEL - 1 && path->nodes[i]; i++) {
		slot = path->slots[i];
		if (slot < btrfs_header_nritems(
		    btrfs_buffer_header(path->nodes[i])) - 1) {
			path->slots[i]++;
			*level = i;
			node = btrfs_buffer_node(path->nodes[i]);
			WARN_ON(i == 0);
			btrfs_disk_key_to_cpu(&root->defrag_progress,
					      &node->ptrs[path->slots[i]].key);
			root->defrag_level = i;
			return 0;
		} else {
			btrfs_block_release(root, path->nodes[*level]);
			path->nodes[*level] = NULL;
			*level = i + 1;
		}
	}
	return 1;
}

int btrfs_defrag_leaves(struct btrfs_trans_handle *trans,
			struct btrfs_root *root, int cache_only)
{
	struct btrfs_path *path = NULL;
	struct buffer_head *tmp;
	int ret = 0;
	int wret;
	int level;
	int orig_level;
	int i;
	int num_runs = 0;

	if (root->ref_cows == 0) {
		goto out;
	}
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	level = btrfs_header_level(btrfs_buffer_header(root->node));
	orig_level = level;
	if (level == 0) {
		goto out;
	}
	if (root->defrag_progress.objectid == 0) {
		get_bh(root->node);
		ret = btrfs_cow_block(trans, root, root->node, NULL, 0, &tmp);
		BUG_ON(ret);
		ret = btrfs_realloc_node(trans, root, root->node, cache_only);
		BUG_ON(ret);
		path->nodes[level] = root->node;
		path->slots[level] = 0;
	} else {
		level = root->defrag_level;
		path->lowest_level = level;
		wret = btrfs_search_slot(trans, root, &root->defrag_progress,
					 path, 0, 1);

		if (wret < 0) {
			ret = wret;
			goto out;
		}
		while(level > 0 && !path->nodes[level])
			level--;
		if (!path->nodes[level]) {
			ret = 0;
			goto out;
		}
	}

	while(1) {
		wret = defrag_walk_down(trans, root, path, &level, cache_only);
		if (wret > 0)
			break;
		if (wret < 0)
			ret = wret;

		wret = defrag_walk_up(trans, root, path, &level, cache_only);
		if (wret > 0)
			break;
		if (wret < 0)
			ret = wret;
		if (num_runs++ > 8) {
			ret = -EAGAIN;
			break;
		}
	}
	for (i = 0; i <= orig_level; i++) {
		if (path->nodes[i]) {
			btrfs_block_release(root, path->nodes[i]);
			path->nodes[i] = 0;
		}
	}
out:
	if (path)
		btrfs_free_path(path);
	if (ret != -EAGAIN) {
		memset(&root->defrag_progress, 0,
		       sizeof(root->defrag_progress));
	}
	return ret;
}
