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
			 struct extent_buffer *node)
{
	int i;
	u32 nritems;
	u64 blocknr;
	int ret;

	nritems = btrfs_header_nritems(node);
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
			    int cache_only, u64 *last_ret)
{
	struct extent_buffer *next;
	struct extent_buffer *cur;
	u64 blocknr;
	int ret = 0;
	int is_extent = 0;

	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);

	if (root->fs_info->extent_root == root)
		is_extent = 1;

	while(*level > 0) {
		WARN_ON(*level < 0);
		WARN_ON(*level >= BTRFS_MAX_LEVEL);
		cur = path->nodes[*level];

		if (!cache_only && *level > 1 && path->slots[*level] == 0)
			reada_defrag(root, cur);

		if (btrfs_header_level(cur) != *level)
			WARN_ON(1);

		if (path->slots[*level] >=
		    btrfs_header_nritems(cur))
			break;

		if (*level == 1) {
			ret = btrfs_realloc_node(trans, root,
						 path->nodes[*level],
						 cache_only, last_ret);
			if (is_extent)
				btrfs_extent_post_op(trans, root);

			break;
		}
		blocknr = btrfs_node_blockptr(cur, path->slots[*level]);

		if (cache_only) {
			next = btrfs_find_tree_block(root, blocknr);
			/* FIXME, test for defrag */
			if (!next || !btrfs_buffer_uptodate(next)) {
				free_extent_buffer(next);
				path->slots[*level]++;
				continue;
			}
		} else {
			next = read_tree_block(root, blocknr);
		}
		ret = btrfs_cow_block(trans, root, next, path->nodes[*level],
				      path->slots[*level], &next);
		BUG_ON(ret);
		ret = btrfs_realloc_node(trans, root, next, cache_only,
					 last_ret);
		BUG_ON(ret);

		if (is_extent)
			btrfs_extent_post_op(trans, root);

		WARN_ON(*level <= 0);
		if (path->nodes[*level-1])
			free_extent_buffer(path->nodes[*level-1]);
		path->nodes[*level-1] = next;
		*level = btrfs_header_level(next);
		path->slots[*level] = 0;
	}
	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);
#if 0
	clear_buffer_defrag(path->nodes[*level]);
	clear_buffer_defrag_done(path->nodes[*level]);
#endif
	free_extent_buffer(path->nodes[*level]);
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
	struct extent_buffer *node;

	for(i = *level; i < BTRFS_MAX_LEVEL - 1 && path->nodes[i]; i++) {
		slot = path->slots[i];
		if (slot < btrfs_header_nritems(path->nodes[i]) - 1) {
			path->slots[i]++;
			*level = i;
			node = path->nodes[i];
			WARN_ON(i == 0);
			btrfs_node_key_to_cpu(node, &root->defrag_progress,
					      path->slots[i]);
			root->defrag_level = i;
			return 0;
		} else {
			/*
			clear_buffer_defrag(path->nodes[*level]);
			clear_buffer_defrag_done(path->nodes[*level]);
			*/
			free_extent_buffer(path->nodes[*level]);
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
	struct extent_buffer *tmp;
	int ret = 0;
	int wret;
	int level;
	int orig_level;
	int i;
	int is_extent = 0;
	u64 last_ret = 0;

	if (root->fs_info->extent_root == root)
		is_extent = 1;

	if (root->ref_cows == 0 && !is_extent)
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
		extent_buffer_get(root->node);
		ret = btrfs_cow_block(trans, root, root->node, NULL, 0, &tmp);
		BUG_ON(ret);
		ret = btrfs_realloc_node(trans, root, root->node, cache_only,
					 &last_ret);
		BUG_ON(ret);
		path->nodes[level] = root->node;
		path->slots[level] = 0;
		if (is_extent)
			btrfs_extent_post_op(trans, root);
	} else {
		level = root->defrag_level;
		path->lowest_level = level;
		wret = btrfs_search_slot(trans, root, &root->defrag_progress,
					 path, 0, 1);

		if (is_extent)
			btrfs_extent_post_op(trans, root);

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
		wret = defrag_walk_down(trans, root, path, &level, cache_only,
					&last_ret);
		if (wret > 0)
			break;
		if (wret < 0)
			ret = wret;

		wret = defrag_walk_up(trans, root, path, &level, cache_only);
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
