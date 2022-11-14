// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009 Oracle.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/error-injection.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "volumes.h"
#include "locking.h"
#include "btrfs_inode.h"
#include "async-thread.h"
#include "free-space-cache.h"
#include "qgroup.h"
#include "print-tree.h"
#include "delalloc-space.h"
#include "block-group.h"
#include "backref.h"
#include "misc.h"
#include "subpage.h"
#include "zoned.h"
#include "inode-item.h"

/*
 * Relocation overview
 *
 * [What does relocation do]
 *
 * The objective of relocation is to relocate all extents of the target block
 * group to other block groups.
 * This is utilized by resize (shrink only), profile converting, compacting
 * space, or balance routine to spread chunks over devices.
 *
 * 		Before		|		After
 * ------------------------------------------------------------------
 *  BG A: 10 data extents	| BG A: deleted
 *  BG B:  2 data extents	| BG B: 10 data extents (2 old + 8 relocated)
 *  BG C:  1 extents		| BG C:  3 data extents (1 old + 2 relocated)
 *
 * [How does relocation work]
 *
 * 1.   Mark the target block group read-only
 *      New extents won't be allocated from the target block group.
 *
 * 2.1  Record each extent in the target block group
 *      To build a proper map of extents to be relocated.
 *
 * 2.2  Build data reloc tree and reloc trees
 *      Data reloc tree will contain an inode, recording all newly relocated
 *      data extents.
 *      There will be only one data reloc tree for one data block group.
 *
 *      Reloc tree will be a special snapshot of its source tree, containing
 *      relocated tree blocks.
 *      Each tree referring to a tree block in target block group will get its
 *      reloc tree built.
 *
 * 2.3  Swap source tree with its corresponding reloc tree
 *      Each involved tree only refers to new extents after swap.
 *
 * 3.   Cleanup reloc trees and data reloc tree.
 *      As old extents in the target block group are still referenced by reloc
 *      trees, we need to clean them up before really freeing the target block
 *      group.
 *
 * The main complexity is in steps 2.2 and 2.3.
 *
 * The entry point of relocation is relocate_block_group() function.
 */

#define RELOCATION_RESERVED_NODES	256
/*
 * map address of tree root to tree
 */
struct mapping_node {
	struct {
		struct rb_node rb_node;
		u64 bytenr;
	}; /* Use rb_simle_node for search/insert */
	void *data;
};

struct mapping_tree {
	struct rb_root rb_root;
	spinlock_t lock;
};

/*
 * present a tree block to process
 */
struct tree_block {
	struct {
		struct rb_node rb_node;
		u64 bytenr;
	}; /* Use rb_simple_node for search/insert */
	u64 owner;
	struct btrfs_key key;
	unsigned int level:8;
	unsigned int key_ready:1;
};

#define MAX_EXTENTS 128

struct file_extent_cluster {
	u64 start;
	u64 end;
	u64 boundary[MAX_EXTENTS];
	unsigned int nr;
};

struct reloc_control {
	/* block group to relocate */
	struct btrfs_block_group *block_group;
	/* extent tree */
	struct btrfs_root *extent_root;
	/* inode for moving data */
	struct inode *data_inode;

	struct btrfs_block_rsv *block_rsv;

	struct btrfs_backref_cache backref_cache;

	struct file_extent_cluster cluster;
	/* tree blocks have been processed */
	struct extent_io_tree processed_blocks;
	/* map start of tree root to corresponding reloc tree */
	struct mapping_tree reloc_root_tree;
	/* list of reloc trees */
	struct list_head reloc_roots;
	/* list of subvolume trees that get relocated */
	struct list_head dirty_subvol_roots;
	/* size of metadata reservation for merging reloc trees */
	u64 merging_rsv_size;
	/* size of relocated tree nodes */
	u64 nodes_relocated;
	/* reserved size for block group relocation*/
	u64 reserved_bytes;

	u64 search_start;
	u64 extents_found;

	unsigned int stage:8;
	unsigned int create_reloc_tree:1;
	unsigned int merge_reloc_tree:1;
	unsigned int found_file_extent:1;
};

/* stages of data relocation */
#define MOVE_DATA_EXTENTS	0
#define UPDATE_DATA_PTRS	1

static void mark_block_processed(struct reloc_control *rc,
				 struct btrfs_backref_node *node)
{
	u32 blocksize;

	if (node->level == 0 ||
	    in_range(node->bytenr, rc->block_group->start,
		     rc->block_group->length)) {
		blocksize = rc->extent_root->fs_info->nodesize;
		set_extent_bits(&rc->processed_blocks, node->bytenr,
				node->bytenr + blocksize - 1, EXTENT_DIRTY);
	}
	node->processed = 1;
}


static void mapping_tree_init(struct mapping_tree *tree)
{
	tree->rb_root = RB_ROOT;
	spin_lock_init(&tree->lock);
}

/*
 * walk up backref nodes until reach node presents tree root
 */
static struct btrfs_backref_node *walk_up_backref(
		struct btrfs_backref_node *node,
		struct btrfs_backref_edge *edges[], int *index)
{
	struct btrfs_backref_edge *edge;
	int idx = *index;

	while (!list_empty(&node->upper)) {
		edge = list_entry(node->upper.next,
				  struct btrfs_backref_edge, list[LOWER]);
		edges[idx++] = edge;
		node = edge->node[UPPER];
	}
	BUG_ON(node->detached);
	*index = idx;
	return node;
}

/*
 * walk down backref nodes to find start of next reference path
 */
static struct btrfs_backref_node *walk_down_backref(
		struct btrfs_backref_edge *edges[], int *index)
{
	struct btrfs_backref_edge *edge;
	struct btrfs_backref_node *lower;
	int idx = *index;

	while (idx > 0) {
		edge = edges[idx - 1];
		lower = edge->node[LOWER];
		if (list_is_last(&edge->list[LOWER], &lower->upper)) {
			idx--;
			continue;
		}
		edge = list_entry(edge->list[LOWER].next,
				  struct btrfs_backref_edge, list[LOWER]);
		edges[idx - 1] = edge;
		*index = idx;
		return edge->node[UPPER];
	}
	*index = 0;
	return NULL;
}

static void update_backref_node(struct btrfs_backref_cache *cache,
				struct btrfs_backref_node *node, u64 bytenr)
{
	struct rb_node *rb_node;
	rb_erase(&node->rb_node, &cache->rb_root);
	node->bytenr = bytenr;
	rb_node = rb_simple_insert(&cache->rb_root, node->bytenr, &node->rb_node);
	if (rb_node)
		btrfs_backref_panic(cache->fs_info, bytenr, -EEXIST);
}

/*
 * update backref cache after a transaction commit
 */
static int update_backref_cache(struct btrfs_trans_handle *trans,
				struct btrfs_backref_cache *cache)
{
	struct btrfs_backref_node *node;
	int level = 0;

	if (cache->last_trans == 0) {
		cache->last_trans = trans->transid;
		return 0;
	}

	if (cache->last_trans == trans->transid)
		return 0;

	/*
	 * detached nodes are used to avoid unnecessary backref
	 * lookup. transaction commit changes the extent tree.
	 * so the detached nodes are no longer useful.
	 */
	while (!list_empty(&cache->detached)) {
		node = list_entry(cache->detached.next,
				  struct btrfs_backref_node, list);
		btrfs_backref_cleanup_node(cache, node);
	}

	while (!list_empty(&cache->changed)) {
		node = list_entry(cache->changed.next,
				  struct btrfs_backref_node, list);
		list_del_init(&node->list);
		BUG_ON(node->pending);
		update_backref_node(cache, node, node->new_bytenr);
	}

	/*
	 * some nodes can be left in the pending list if there were
	 * errors during processing the pending nodes.
	 */
	for (level = 0; level < BTRFS_MAX_LEVEL; level++) {
		list_for_each_entry(node, &cache->pending[level], list) {
			BUG_ON(!node->pending);
			if (node->bytenr == node->new_bytenr)
				continue;
			update_backref_node(cache, node, node->new_bytenr);
		}
	}

	cache->last_trans = 0;
	return 1;
}

static bool reloc_root_is_dead(struct btrfs_root *root)
{
	/*
	 * Pair with set_bit/clear_bit in clean_dirty_subvols and
	 * btrfs_update_reloc_root. We need to see the updated bit before
	 * trying to access reloc_root
	 */
	smp_rmb();
	if (test_bit(BTRFS_ROOT_DEAD_RELOC_TREE, &root->state))
		return true;
	return false;
}

/*
 * Check if this subvolume tree has valid reloc tree.
 *
 * Reloc tree after swap is considered dead, thus not considered as valid.
 * This is enough for most callers, as they don't distinguish dead reloc root
 * from no reloc root.  But btrfs_should_ignore_reloc_root() below is a
 * special case.
 */
static bool have_reloc_root(struct btrfs_root *root)
{
	if (reloc_root_is_dead(root))
		return false;
	if (!root->reloc_root)
		return false;
	return true;
}

int btrfs_should_ignore_reloc_root(struct btrfs_root *root)
{
	struct btrfs_root *reloc_root;

	if (!test_bit(BTRFS_ROOT_SHAREABLE, &root->state))
		return 0;

	/* This root has been merged with its reloc tree, we can ignore it */
	if (reloc_root_is_dead(root))
		return 1;

	reloc_root = root->reloc_root;
	if (!reloc_root)
		return 0;

	if (btrfs_header_generation(reloc_root->commit_root) ==
	    root->fs_info->running_transaction->transid)
		return 0;
	/*
	 * if there is reloc tree and it was created in previous
	 * transaction backref lookup can find the reloc tree,
	 * so backref node for the fs tree root is useless for
	 * relocation.
	 */
	return 1;
}

/*
 * find reloc tree by address of tree root
 */
struct btrfs_root *find_reloc_root(struct btrfs_fs_info *fs_info, u64 bytenr)
{
	struct reloc_control *rc = fs_info->reloc_ctl;
	struct rb_node *rb_node;
	struct mapping_node *node;
	struct btrfs_root *root = NULL;

	ASSERT(rc);
	spin_lock(&rc->reloc_root_tree.lock);
	rb_node = rb_simple_search(&rc->reloc_root_tree.rb_root, bytenr);
	if (rb_node) {
		node = rb_entry(rb_node, struct mapping_node, rb_node);
		root = node->data;
	}
	spin_unlock(&rc->reloc_root_tree.lock);
	return btrfs_grab_root(root);
}

/*
 * For useless nodes, do two major clean ups:
 *
 * - Cleanup the children edges and nodes
 *   If child node is also orphan (no parent) during cleanup, then the child
 *   node will also be cleaned up.
 *
 * - Freeing up leaves (level 0), keeps nodes detached
 *   For nodes, the node is still cached as "detached"
 *
 * Return false if @node is not in the @useless_nodes list.
 * Return true if @node is in the @useless_nodes list.
 */
static bool handle_useless_nodes(struct reloc_control *rc,
				 struct btrfs_backref_node *node)
{
	struct btrfs_backref_cache *cache = &rc->backref_cache;
	struct list_head *useless_node = &cache->useless_node;
	bool ret = false;

	while (!list_empty(useless_node)) {
		struct btrfs_backref_node *cur;

		cur = list_first_entry(useless_node, struct btrfs_backref_node,
				 list);
		list_del_init(&cur->list);

		/* Only tree root nodes can be added to @useless_nodes */
		ASSERT(list_empty(&cur->upper));

		if (cur == node)
			ret = true;

		/* The node is the lowest node */
		if (cur->lowest) {
			list_del_init(&cur->lower);
			cur->lowest = 0;
		}

		/* Cleanup the lower edges */
		while (!list_empty(&cur->lower)) {
			struct btrfs_backref_edge *edge;
			struct btrfs_backref_node *lower;

			edge = list_entry(cur->lower.next,
					struct btrfs_backref_edge, list[UPPER]);
			list_del(&edge->list[UPPER]);
			list_del(&edge->list[LOWER]);
			lower = edge->node[LOWER];
			btrfs_backref_free_edge(cache, edge);

			/* Child node is also orphan, queue for cleanup */
			if (list_empty(&lower->upper))
				list_add(&lower->list, useless_node);
		}
		/* Mark this block processed for relocation */
		mark_block_processed(rc, cur);

		/*
		 * Backref nodes for tree leaves are deleted from the cache.
		 * Backref nodes for upper level tree blocks are left in the
		 * cache to avoid unnecessary backref lookup.
		 */
		if (cur->level > 0) {
			list_add(&cur->list, &cache->detached);
			cur->detached = 1;
		} else {
			rb_erase(&cur->rb_node, &cache->rb_root);
			btrfs_backref_free_node(cache, cur);
		}
	}
	return ret;
}

/*
 * Build backref tree for a given tree block. Root of the backref tree
 * corresponds the tree block, leaves of the backref tree correspond roots of
 * b-trees that reference the tree block.
 *
 * The basic idea of this function is check backrefs of a given block to find
 * upper level blocks that reference the block, and then check backrefs of
 * these upper level blocks recursively. The recursion stops when tree root is
 * reached or backrefs for the block is cached.
 *
 * NOTE: if we find that backrefs for a block are cached, we know backrefs for
 * all upper level blocks that directly/indirectly reference the block are also
 * cached.
 */
static noinline_for_stack struct btrfs_backref_node *build_backref_tree(
			struct reloc_control *rc, struct btrfs_key *node_key,
			int level, u64 bytenr)
{
	struct btrfs_backref_iter *iter;
	struct btrfs_backref_cache *cache = &rc->backref_cache;
	/* For searching parent of TREE_BLOCK_REF */
	struct btrfs_path *path;
	struct btrfs_backref_node *cur;
	struct btrfs_backref_node *node = NULL;
	struct btrfs_backref_edge *edge;
	int ret;
	int err = 0;

	iter = btrfs_backref_iter_alloc(rc->extent_root->fs_info, GFP_NOFS);
	if (!iter)
		return ERR_PTR(-ENOMEM);
	path = btrfs_alloc_path();
	if (!path) {
		err = -ENOMEM;
		goto out;
	}

	node = btrfs_backref_alloc_node(cache, bytenr, level);
	if (!node) {
		err = -ENOMEM;
		goto out;
	}

	node->lowest = 1;
	cur = node;

	/* Breadth-first search to build backref cache */
	do {
		ret = btrfs_backref_add_tree_node(cache, path, iter, node_key,
						  cur);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		edge = list_first_entry_or_null(&cache->pending_edge,
				struct btrfs_backref_edge, list[UPPER]);
		/*
		 * The pending list isn't empty, take the first block to
		 * process
		 */
		if (edge) {
			list_del_init(&edge->list[UPPER]);
			cur = edge->node[UPPER];
		}
	} while (edge);

	/* Finish the upper linkage of newly added edges/nodes */
	ret = btrfs_backref_finish_upper_links(cache, node);
	if (ret < 0) {
		err = ret;
		goto out;
	}

	if (handle_useless_nodes(rc, node))
		node = NULL;
out:
	btrfs_backref_iter_free(iter);
	btrfs_free_path(path);
	if (err) {
		btrfs_backref_error_cleanup(cache, node);
		return ERR_PTR(err);
	}
	ASSERT(!node || !node->detached);
	ASSERT(list_empty(&cache->useless_node) &&
	       list_empty(&cache->pending_edge));
	return node;
}

/*
 * helper to add backref node for the newly created snapshot.
 * the backref node is created by cloning backref node that
 * corresponds to root of source tree
 */
static int clone_backref_node(struct btrfs_trans_handle *trans,
			      struct reloc_control *rc,
			      struct btrfs_root *src,
			      struct btrfs_root *dest)
{
	struct btrfs_root *reloc_root = src->reloc_root;
	struct btrfs_backref_cache *cache = &rc->backref_cache;
	struct btrfs_backref_node *node = NULL;
	struct btrfs_backref_node *new_node;
	struct btrfs_backref_edge *edge;
	struct btrfs_backref_edge *new_edge;
	struct rb_node *rb_node;

	if (cache->last_trans > 0)
		update_backref_cache(trans, cache);

	rb_node = rb_simple_search(&cache->rb_root, src->commit_root->start);
	if (rb_node) {
		node = rb_entry(rb_node, struct btrfs_backref_node, rb_node);
		if (node->detached)
			node = NULL;
		else
			BUG_ON(node->new_bytenr != reloc_root->node->start);
	}

	if (!node) {
		rb_node = rb_simple_search(&cache->rb_root,
					   reloc_root->commit_root->start);
		if (rb_node) {
			node = rb_entry(rb_node, struct btrfs_backref_node,
					rb_node);
			BUG_ON(node->detached);
		}
	}

	if (!node)
		return 0;

	new_node = btrfs_backref_alloc_node(cache, dest->node->start,
					    node->level);
	if (!new_node)
		return -ENOMEM;

	new_node->lowest = node->lowest;
	new_node->checked = 1;
	new_node->root = btrfs_grab_root(dest);
	ASSERT(new_node->root);

	if (!node->lowest) {
		list_for_each_entry(edge, &node->lower, list[UPPER]) {
			new_edge = btrfs_backref_alloc_edge(cache);
			if (!new_edge)
				goto fail;

			btrfs_backref_link_edge(new_edge, edge->node[LOWER],
						new_node, LINK_UPPER);
		}
	} else {
		list_add_tail(&new_node->lower, &cache->leaves);
	}

	rb_node = rb_simple_insert(&cache->rb_root, new_node->bytenr,
				   &new_node->rb_node);
	if (rb_node)
		btrfs_backref_panic(trans->fs_info, new_node->bytenr, -EEXIST);

	if (!new_node->lowest) {
		list_for_each_entry(new_edge, &new_node->lower, list[UPPER]) {
			list_add_tail(&new_edge->list[LOWER],
				      &new_edge->node[LOWER]->upper);
		}
	}
	return 0;
fail:
	while (!list_empty(&new_node->lower)) {
		new_edge = list_entry(new_node->lower.next,
				      struct btrfs_backref_edge, list[UPPER]);
		list_del(&new_edge->list[UPPER]);
		btrfs_backref_free_edge(cache, new_edge);
	}
	btrfs_backref_free_node(cache, new_node);
	return -ENOMEM;
}

/*
 * helper to add 'address of tree root -> reloc tree' mapping
 */
static int __must_check __add_reloc_root(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct rb_node *rb_node;
	struct mapping_node *node;
	struct reloc_control *rc = fs_info->reloc_ctl;

	node = kmalloc(sizeof(*node), GFP_NOFS);
	if (!node)
		return -ENOMEM;

	node->bytenr = root->commit_root->start;
	node->data = root;

	spin_lock(&rc->reloc_root_tree.lock);
	rb_node = rb_simple_insert(&rc->reloc_root_tree.rb_root,
				   node->bytenr, &node->rb_node);
	spin_unlock(&rc->reloc_root_tree.lock);
	if (rb_node) {
		btrfs_err(fs_info,
			    "Duplicate root found for start=%llu while inserting into relocation tree",
			    node->bytenr);
		return -EEXIST;
	}

	list_add_tail(&root->root_list, &rc->reloc_roots);
	return 0;
}

/*
 * helper to delete the 'address of tree root -> reloc tree'
 * mapping
 */
static void __del_reloc_root(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct rb_node *rb_node;
	struct mapping_node *node = NULL;
	struct reloc_control *rc = fs_info->reloc_ctl;
	bool put_ref = false;

	if (rc && root->node) {
		spin_lock(&rc->reloc_root_tree.lock);
		rb_node = rb_simple_search(&rc->reloc_root_tree.rb_root,
					   root->commit_root->start);
		if (rb_node) {
			node = rb_entry(rb_node, struct mapping_node, rb_node);
			rb_erase(&node->rb_node, &rc->reloc_root_tree.rb_root);
			RB_CLEAR_NODE(&node->rb_node);
		}
		spin_unlock(&rc->reloc_root_tree.lock);
		ASSERT(!node || (struct btrfs_root *)node->data == root);
	}

	/*
	 * We only put the reloc root here if it's on the list.  There's a lot
	 * of places where the pattern is to splice the rc->reloc_roots, process
	 * the reloc roots, and then add the reloc root back onto
	 * rc->reloc_roots.  If we call __del_reloc_root while it's off of the
	 * list we don't want the reference being dropped, because the guy
	 * messing with the list is in charge of the reference.
	 */
	spin_lock(&fs_info->trans_lock);
	if (!list_empty(&root->root_list)) {
		put_ref = true;
		list_del_init(&root->root_list);
	}
	spin_unlock(&fs_info->trans_lock);
	if (put_ref)
		btrfs_put_root(root);
	kfree(node);
}

/*
 * helper to update the 'address of tree root -> reloc tree'
 * mapping
 */
static int __update_reloc_root(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct rb_node *rb_node;
	struct mapping_node *node = NULL;
	struct reloc_control *rc = fs_info->reloc_ctl;

	spin_lock(&rc->reloc_root_tree.lock);
	rb_node = rb_simple_search(&rc->reloc_root_tree.rb_root,
				   root->commit_root->start);
	if (rb_node) {
		node = rb_entry(rb_node, struct mapping_node, rb_node);
		rb_erase(&node->rb_node, &rc->reloc_root_tree.rb_root);
	}
	spin_unlock(&rc->reloc_root_tree.lock);

	if (!node)
		return 0;
	BUG_ON((struct btrfs_root *)node->data != root);

	spin_lock(&rc->reloc_root_tree.lock);
	node->bytenr = root->node->start;
	rb_node = rb_simple_insert(&rc->reloc_root_tree.rb_root,
				   node->bytenr, &node->rb_node);
	spin_unlock(&rc->reloc_root_tree.lock);
	if (rb_node)
		btrfs_backref_panic(fs_info, node->bytenr, -EEXIST);
	return 0;
}

static struct btrfs_root *create_reloc_root(struct btrfs_trans_handle *trans,
					struct btrfs_root *root, u64 objectid)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_root *reloc_root;
	struct extent_buffer *eb;
	struct btrfs_root_item *root_item;
	struct btrfs_key root_key;
	int ret = 0;
	bool must_abort = false;

	root_item = kmalloc(sizeof(*root_item), GFP_NOFS);
	if (!root_item)
		return ERR_PTR(-ENOMEM);

	root_key.objectid = BTRFS_TREE_RELOC_OBJECTID;
	root_key.type = BTRFS_ROOT_ITEM_KEY;
	root_key.offset = objectid;

	if (root->root_key.objectid == objectid) {
		u64 commit_root_gen;

		/* called by btrfs_init_reloc_root */
		ret = btrfs_copy_root(trans, root, root->commit_root, &eb,
				      BTRFS_TREE_RELOC_OBJECTID);
		if (ret)
			goto fail;

		/*
		 * Set the last_snapshot field to the generation of the commit
		 * root - like this ctree.c:btrfs_block_can_be_shared() behaves
		 * correctly (returns true) when the relocation root is created
		 * either inside the critical section of a transaction commit
		 * (through transaction.c:qgroup_account_snapshot()) and when
		 * it's created before the transaction commit is started.
		 */
		commit_root_gen = btrfs_header_generation(root->commit_root);
		btrfs_set_root_last_snapshot(&root->root_item, commit_root_gen);
	} else {
		/*
		 * called by btrfs_reloc_post_snapshot_hook.
		 * the source tree is a reloc tree, all tree blocks
		 * modified after it was created have RELOC flag
		 * set in their headers. so it's OK to not update
		 * the 'last_snapshot'.
		 */
		ret = btrfs_copy_root(trans, root, root->node, &eb,
				      BTRFS_TREE_RELOC_OBJECTID);
		if (ret)
			goto fail;
	}

	/*
	 * We have changed references at this point, we must abort the
	 * transaction if anything fails.
	 */
	must_abort = true;

	memcpy(root_item, &root->root_item, sizeof(*root_item));
	btrfs_set_root_bytenr(root_item, eb->start);
	btrfs_set_root_level(root_item, btrfs_header_level(eb));
	btrfs_set_root_generation(root_item, trans->transid);

	if (root->root_key.objectid == objectid) {
		btrfs_set_root_refs(root_item, 0);
		memset(&root_item->drop_progress, 0,
		       sizeof(struct btrfs_disk_key));
		btrfs_set_root_drop_level(root_item, 0);
	}

	btrfs_tree_unlock(eb);
	free_extent_buffer(eb);

	ret = btrfs_insert_root(trans, fs_info->tree_root,
				&root_key, root_item);
	if (ret)
		goto fail;

	kfree(root_item);

	reloc_root = btrfs_read_tree_root(fs_info->tree_root, &root_key);
	if (IS_ERR(reloc_root)) {
		ret = PTR_ERR(reloc_root);
		goto abort;
	}
	set_bit(BTRFS_ROOT_SHAREABLE, &reloc_root->state);
	reloc_root->last_trans = trans->transid;
	return reloc_root;
fail:
	kfree(root_item);
abort:
	if (must_abort)
		btrfs_abort_transaction(trans, ret);
	return ERR_PTR(ret);
}

/*
 * create reloc tree for a given fs tree. reloc tree is just a
 * snapshot of the fs tree with special root objectid.
 *
 * The reloc_root comes out of here with two references, one for
 * root->reloc_root, and another for being on the rc->reloc_roots list.
 */
int btrfs_init_reloc_root(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_root *reloc_root;
	struct reloc_control *rc = fs_info->reloc_ctl;
	struct btrfs_block_rsv *rsv;
	int clear_rsv = 0;
	int ret;

	if (!rc)
		return 0;

	/*
	 * The subvolume has reloc tree but the swap is finished, no need to
	 * create/update the dead reloc tree
	 */
	if (reloc_root_is_dead(root))
		return 0;

	/*
	 * This is subtle but important.  We do not do
	 * record_root_in_transaction for reloc roots, instead we record their
	 * corresponding fs root, and then here we update the last trans for the
	 * reloc root.  This means that we have to do this for the entire life
	 * of the reloc root, regardless of which stage of the relocation we are
	 * in.
	 */
	if (root->reloc_root) {
		reloc_root = root->reloc_root;
		reloc_root->last_trans = trans->transid;
		return 0;
	}

	/*
	 * We are merging reloc roots, we do not need new reloc trees.  Also
	 * reloc trees never need their own reloc tree.
	 */
	if (!rc->create_reloc_tree ||
	    root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID)
		return 0;

	if (!trans->reloc_reserved) {
		rsv = trans->block_rsv;
		trans->block_rsv = rc->block_rsv;
		clear_rsv = 1;
	}
	reloc_root = create_reloc_root(trans, root, root->root_key.objectid);
	if (clear_rsv)
		trans->block_rsv = rsv;
	if (IS_ERR(reloc_root))
		return PTR_ERR(reloc_root);

	ret = __add_reloc_root(reloc_root);
	ASSERT(ret != -EEXIST);
	if (ret) {
		/* Pairs with create_reloc_root */
		btrfs_put_root(reloc_root);
		return ret;
	}
	root->reloc_root = btrfs_grab_root(reloc_root);
	return 0;
}

/*
 * update root item of reloc tree
 */
int btrfs_update_reloc_root(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_root *reloc_root;
	struct btrfs_root_item *root_item;
	int ret;

	if (!have_reloc_root(root))
		return 0;

	reloc_root = root->reloc_root;
	root_item = &reloc_root->root_item;

	/*
	 * We are probably ok here, but __del_reloc_root() will drop its ref of
	 * the root.  We have the ref for root->reloc_root, but just in case
	 * hold it while we update the reloc root.
	 */
	btrfs_grab_root(reloc_root);

	/* root->reloc_root will stay until current relocation finished */
	if (fs_info->reloc_ctl->merge_reloc_tree &&
	    btrfs_root_refs(root_item) == 0) {
		set_bit(BTRFS_ROOT_DEAD_RELOC_TREE, &root->state);
		/*
		 * Mark the tree as dead before we change reloc_root so
		 * have_reloc_root will not touch it from now on.
		 */
		smp_wmb();
		__del_reloc_root(reloc_root);
	}

	if (reloc_root->commit_root != reloc_root->node) {
		__update_reloc_root(reloc_root);
		btrfs_set_root_node(root_item, reloc_root->node);
		free_extent_buffer(reloc_root->commit_root);
		reloc_root->commit_root = btrfs_root_node(reloc_root);
	}

	ret = btrfs_update_root(trans, fs_info->tree_root,
				&reloc_root->root_key, root_item);
	btrfs_put_root(reloc_root);
	return ret;
}

/*
 * helper to find first cached inode with inode number >= objectid
 * in a subvolume
 */
static struct inode *find_next_inode(struct btrfs_root *root, u64 objectid)
{
	struct rb_node *node;
	struct rb_node *prev;
	struct btrfs_inode *entry;
	struct inode *inode;

	spin_lock(&root->inode_lock);
again:
	node = root->inode_tree.rb_node;
	prev = NULL;
	while (node) {
		prev = node;
		entry = rb_entry(node, struct btrfs_inode, rb_node);

		if (objectid < btrfs_ino(entry))
			node = node->rb_left;
		else if (objectid > btrfs_ino(entry))
			node = node->rb_right;
		else
			break;
	}
	if (!node) {
		while (prev) {
			entry = rb_entry(prev, struct btrfs_inode, rb_node);
			if (objectid <= btrfs_ino(entry)) {
				node = prev;
				break;
			}
			prev = rb_next(prev);
		}
	}
	while (node) {
		entry = rb_entry(node, struct btrfs_inode, rb_node);
		inode = igrab(&entry->vfs_inode);
		if (inode) {
			spin_unlock(&root->inode_lock);
			return inode;
		}

		objectid = btrfs_ino(entry) + 1;
		if (cond_resched_lock(&root->inode_lock))
			goto again;

		node = rb_next(node);
	}
	spin_unlock(&root->inode_lock);
	return NULL;
}

/*
 * get new location of data
 */
static int get_new_location(struct inode *reloc_inode, u64 *new_bytenr,
			    u64 bytenr, u64 num_bytes)
{
	struct btrfs_root *root = BTRFS_I(reloc_inode)->root;
	struct btrfs_path *path;
	struct btrfs_file_extent_item *fi;
	struct extent_buffer *leaf;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	bytenr -= BTRFS_I(reloc_inode)->index_cnt;
	ret = btrfs_lookup_file_extent(NULL, root, path,
			btrfs_ino(BTRFS_I(reloc_inode)), bytenr, 0);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	leaf = path->nodes[0];
	fi = btrfs_item_ptr(leaf, path->slots[0],
			    struct btrfs_file_extent_item);

	BUG_ON(btrfs_file_extent_offset(leaf, fi) ||
	       btrfs_file_extent_compression(leaf, fi) ||
	       btrfs_file_extent_encryption(leaf, fi) ||
	       btrfs_file_extent_other_encoding(leaf, fi));

	if (num_bytes != btrfs_file_extent_disk_num_bytes(leaf, fi)) {
		ret = -EINVAL;
		goto out;
	}

	*new_bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * update file extent items in the tree leaf to point to
 * the new locations.
 */
static noinline_for_stack
int replace_file_extents(struct btrfs_trans_handle *trans,
			 struct reloc_control *rc,
			 struct btrfs_root *root,
			 struct extent_buffer *leaf)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	struct inode *inode = NULL;
	u64 parent;
	u64 bytenr;
	u64 new_bytenr = 0;
	u64 num_bytes;
	u64 end;
	u32 nritems;
	u32 i;
	int ret = 0;
	int first = 1;
	int dirty = 0;

	if (rc->stage != UPDATE_DATA_PTRS)
		return 0;

	/* reloc trees always use full backref */
	if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID)
		parent = leaf->start;
	else
		parent = 0;

	nritems = btrfs_header_nritems(leaf);
	for (i = 0; i < nritems; i++) {
		struct btrfs_ref ref = { 0 };

		cond_resched();
		btrfs_item_key_to_cpu(leaf, &key, i);
		if (key.type != BTRFS_EXTENT_DATA_KEY)
			continue;
		fi = btrfs_item_ptr(leaf, i, struct btrfs_file_extent_item);
		if (btrfs_file_extent_type(leaf, fi) ==
		    BTRFS_FILE_EXTENT_INLINE)
			continue;
		bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
		num_bytes = btrfs_file_extent_disk_num_bytes(leaf, fi);
		if (bytenr == 0)
			continue;
		if (!in_range(bytenr, rc->block_group->start,
			      rc->block_group->length))
			continue;

		/*
		 * if we are modifying block in fs tree, wait for read_folio
		 * to complete and drop the extent cache
		 */
		if (root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID) {
			if (first) {
				inode = find_next_inode(root, key.objectid);
				first = 0;
			} else if (inode && btrfs_ino(BTRFS_I(inode)) < key.objectid) {
				btrfs_add_delayed_iput(inode);
				inode = find_next_inode(root, key.objectid);
			}
			if (inode && btrfs_ino(BTRFS_I(inode)) == key.objectid) {
				end = key.offset +
				      btrfs_file_extent_num_bytes(leaf, fi);
				WARN_ON(!IS_ALIGNED(key.offset,
						    fs_info->sectorsize));
				WARN_ON(!IS_ALIGNED(end, fs_info->sectorsize));
				end--;
				ret = try_lock_extent(&BTRFS_I(inode)->io_tree,
						      key.offset, end);
				if (!ret)
					continue;

				btrfs_drop_extent_map_range(BTRFS_I(inode),
							    key.offset, end, true);
				unlock_extent(&BTRFS_I(inode)->io_tree,
					      key.offset, end, NULL);
			}
		}

		ret = get_new_location(rc->data_inode, &new_bytenr,
				       bytenr, num_bytes);
		if (ret) {
			/*
			 * Don't have to abort since we've not changed anything
			 * in the file extent yet.
			 */
			break;
		}

		btrfs_set_file_extent_disk_bytenr(leaf, fi, new_bytenr);
		dirty = 1;

		key.offset -= btrfs_file_extent_offset(leaf, fi);
		btrfs_init_generic_ref(&ref, BTRFS_ADD_DELAYED_REF, new_bytenr,
				       num_bytes, parent);
		btrfs_init_data_ref(&ref, btrfs_header_owner(leaf),
				    key.objectid, key.offset,
				    root->root_key.objectid, false);
		ret = btrfs_inc_extent_ref(trans, &ref);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			break;
		}

		btrfs_init_generic_ref(&ref, BTRFS_DROP_DELAYED_REF, bytenr,
				       num_bytes, parent);
		btrfs_init_data_ref(&ref, btrfs_header_owner(leaf),
				    key.objectid, key.offset,
				    root->root_key.objectid, false);
		ret = btrfs_free_extent(trans, &ref);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			break;
		}
	}
	if (dirty)
		btrfs_mark_buffer_dirty(leaf);
	if (inode)
		btrfs_add_delayed_iput(inode);
	return ret;
}

static noinline_for_stack
int memcmp_node_keys(struct extent_buffer *eb, int slot,
		     struct btrfs_path *path, int level)
{
	struct btrfs_disk_key key1;
	struct btrfs_disk_key key2;
	btrfs_node_key(eb, &key1, slot);
	btrfs_node_key(path->nodes[level], &key2, path->slots[level]);
	return memcmp(&key1, &key2, sizeof(key1));
}

/*
 * try to replace tree blocks in fs tree with the new blocks
 * in reloc tree. tree blocks haven't been modified since the
 * reloc tree was create can be replaced.
 *
 * if a block was replaced, level of the block + 1 is returned.
 * if no block got replaced, 0 is returned. if there are other
 * errors, a negative error number is returned.
 */
static noinline_for_stack
int replace_path(struct btrfs_trans_handle *trans, struct reloc_control *rc,
		 struct btrfs_root *dest, struct btrfs_root *src,
		 struct btrfs_path *path, struct btrfs_key *next_key,
		 int lowest_level, int max_level)
{
	struct btrfs_fs_info *fs_info = dest->fs_info;
	struct extent_buffer *eb;
	struct extent_buffer *parent;
	struct btrfs_ref ref = { 0 };
	struct btrfs_key key;
	u64 old_bytenr;
	u64 new_bytenr;
	u64 old_ptr_gen;
	u64 new_ptr_gen;
	u64 last_snapshot;
	u32 blocksize;
	int cow = 0;
	int level;
	int ret;
	int slot;

	ASSERT(src->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID);
	ASSERT(dest->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID);

	last_snapshot = btrfs_root_last_snapshot(&src->root_item);
again:
	slot = path->slots[lowest_level];
	btrfs_node_key_to_cpu(path->nodes[lowest_level], &key, slot);

	eb = btrfs_lock_root_node(dest);
	level = btrfs_header_level(eb);

	if (level < lowest_level) {
		btrfs_tree_unlock(eb);
		free_extent_buffer(eb);
		return 0;
	}

	if (cow) {
		ret = btrfs_cow_block(trans, dest, eb, NULL, 0, &eb,
				      BTRFS_NESTING_COW);
		if (ret) {
			btrfs_tree_unlock(eb);
			free_extent_buffer(eb);
			return ret;
		}
	}

	if (next_key) {
		next_key->objectid = (u64)-1;
		next_key->type = (u8)-1;
		next_key->offset = (u64)-1;
	}

	parent = eb;
	while (1) {
		level = btrfs_header_level(parent);
		ASSERT(level >= lowest_level);

		ret = btrfs_bin_search(parent, &key, &slot);
		if (ret < 0)
			break;
		if (ret && slot > 0)
			slot--;

		if (next_key && slot + 1 < btrfs_header_nritems(parent))
			btrfs_node_key_to_cpu(parent, next_key, slot + 1);

		old_bytenr = btrfs_node_blockptr(parent, slot);
		blocksize = fs_info->nodesize;
		old_ptr_gen = btrfs_node_ptr_generation(parent, slot);

		if (level <= max_level) {
			eb = path->nodes[level];
			new_bytenr = btrfs_node_blockptr(eb,
							path->slots[level]);
			new_ptr_gen = btrfs_node_ptr_generation(eb,
							path->slots[level]);
		} else {
			new_bytenr = 0;
			new_ptr_gen = 0;
		}

		if (WARN_ON(new_bytenr > 0 && new_bytenr == old_bytenr)) {
			ret = level;
			break;
		}

		if (new_bytenr == 0 || old_ptr_gen > last_snapshot ||
		    memcmp_node_keys(parent, slot, path, level)) {
			if (level <= lowest_level) {
				ret = 0;
				break;
			}

			eb = btrfs_read_node_slot(parent, slot);
			if (IS_ERR(eb)) {
				ret = PTR_ERR(eb);
				break;
			}
			btrfs_tree_lock(eb);
			if (cow) {
				ret = btrfs_cow_block(trans, dest, eb, parent,
						      slot, &eb,
						      BTRFS_NESTING_COW);
				if (ret) {
					btrfs_tree_unlock(eb);
					free_extent_buffer(eb);
					break;
				}
			}

			btrfs_tree_unlock(parent);
			free_extent_buffer(parent);

			parent = eb;
			continue;
		}

		if (!cow) {
			btrfs_tree_unlock(parent);
			free_extent_buffer(parent);
			cow = 1;
			goto again;
		}

		btrfs_node_key_to_cpu(path->nodes[level], &key,
				      path->slots[level]);
		btrfs_release_path(path);

		path->lowest_level = level;
		set_bit(BTRFS_ROOT_RESET_LOCKDEP_CLASS, &src->state);
		ret = btrfs_search_slot(trans, src, &key, path, 0, 1);
		clear_bit(BTRFS_ROOT_RESET_LOCKDEP_CLASS, &src->state);
		path->lowest_level = 0;
		if (ret) {
			if (ret > 0)
				ret = -ENOENT;
			break;
		}

		/*
		 * Info qgroup to trace both subtrees.
		 *
		 * We must trace both trees.
		 * 1) Tree reloc subtree
		 *    If not traced, we will leak data numbers
		 * 2) Fs subtree
		 *    If not traced, we will double count old data
		 *
		 * We don't scan the subtree right now, but only record
		 * the swapped tree blocks.
		 * The real subtree rescan is delayed until we have new
		 * CoW on the subtree root node before transaction commit.
		 */
		ret = btrfs_qgroup_add_swapped_blocks(trans, dest,
				rc->block_group, parent, slot,
				path->nodes[level], path->slots[level],
				last_snapshot);
		if (ret < 0)
			break;
		/*
		 * swap blocks in fs tree and reloc tree.
		 */
		btrfs_set_node_blockptr(parent, slot, new_bytenr);
		btrfs_set_node_ptr_generation(parent, slot, new_ptr_gen);
		btrfs_mark_buffer_dirty(parent);

		btrfs_set_node_blockptr(path->nodes[level],
					path->slots[level], old_bytenr);
		btrfs_set_node_ptr_generation(path->nodes[level],
					      path->slots[level], old_ptr_gen);
		btrfs_mark_buffer_dirty(path->nodes[level]);

		btrfs_init_generic_ref(&ref, BTRFS_ADD_DELAYED_REF, old_bytenr,
				       blocksize, path->nodes[level]->start);
		btrfs_init_tree_ref(&ref, level - 1, src->root_key.objectid,
				    0, true);
		ret = btrfs_inc_extent_ref(trans, &ref);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			break;
		}
		btrfs_init_generic_ref(&ref, BTRFS_ADD_DELAYED_REF, new_bytenr,
				       blocksize, 0);
		btrfs_init_tree_ref(&ref, level - 1, dest->root_key.objectid, 0,
				    true);
		ret = btrfs_inc_extent_ref(trans, &ref);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			break;
		}

		btrfs_init_generic_ref(&ref, BTRFS_DROP_DELAYED_REF, new_bytenr,
				       blocksize, path->nodes[level]->start);
		btrfs_init_tree_ref(&ref, level - 1, src->root_key.objectid,
				    0, true);
		ret = btrfs_free_extent(trans, &ref);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			break;
		}

		btrfs_init_generic_ref(&ref, BTRFS_DROP_DELAYED_REF, old_bytenr,
				       blocksize, 0);
		btrfs_init_tree_ref(&ref, level - 1, dest->root_key.objectid,
				    0, true);
		ret = btrfs_free_extent(trans, &ref);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			break;
		}

		btrfs_unlock_up_safe(path, 0);

		ret = level;
		break;
	}
	btrfs_tree_unlock(parent);
	free_extent_buffer(parent);
	return ret;
}

/*
 * helper to find next relocated block in reloc tree
 */
static noinline_for_stack
int walk_up_reloc_tree(struct btrfs_root *root, struct btrfs_path *path,
		       int *level)
{
	struct extent_buffer *eb;
	int i;
	u64 last_snapshot;
	u32 nritems;

	last_snapshot = btrfs_root_last_snapshot(&root->root_item);

	for (i = 0; i < *level; i++) {
		free_extent_buffer(path->nodes[i]);
		path->nodes[i] = NULL;
	}

	for (i = *level; i < BTRFS_MAX_LEVEL && path->nodes[i]; i++) {
		eb = path->nodes[i];
		nritems = btrfs_header_nritems(eb);
		while (path->slots[i] + 1 < nritems) {
			path->slots[i]++;
			if (btrfs_node_ptr_generation(eb, path->slots[i]) <=
			    last_snapshot)
				continue;

			*level = i;
			return 0;
		}
		free_extent_buffer(path->nodes[i]);
		path->nodes[i] = NULL;
	}
	return 1;
}

/*
 * walk down reloc tree to find relocated block of lowest level
 */
static noinline_for_stack
int walk_down_reloc_tree(struct btrfs_root *root, struct btrfs_path *path,
			 int *level)
{
	struct extent_buffer *eb = NULL;
	int i;
	u64 ptr_gen = 0;
	u64 last_snapshot;
	u32 nritems;

	last_snapshot = btrfs_root_last_snapshot(&root->root_item);

	for (i = *level; i > 0; i--) {
		eb = path->nodes[i];
		nritems = btrfs_header_nritems(eb);
		while (path->slots[i] < nritems) {
			ptr_gen = btrfs_node_ptr_generation(eb, path->slots[i]);
			if (ptr_gen > last_snapshot)
				break;
			path->slots[i]++;
		}
		if (path->slots[i] >= nritems) {
			if (i == *level)
				break;
			*level = i + 1;
			return 0;
		}
		if (i == 1) {
			*level = i;
			return 0;
		}

		eb = btrfs_read_node_slot(eb, path->slots[i]);
		if (IS_ERR(eb))
			return PTR_ERR(eb);
		BUG_ON(btrfs_header_level(eb) != i - 1);
		path->nodes[i - 1] = eb;
		path->slots[i - 1] = 0;
	}
	return 1;
}

/*
 * invalidate extent cache for file extents whose key in range of
 * [min_key, max_key)
 */
static int invalidate_extent_cache(struct btrfs_root *root,
				   struct btrfs_key *min_key,
				   struct btrfs_key *max_key)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct inode *inode = NULL;
	u64 objectid;
	u64 start, end;
	u64 ino;

	objectid = min_key->objectid;
	while (1) {
		cond_resched();
		iput(inode);

		if (objectid > max_key->objectid)
			break;

		inode = find_next_inode(root, objectid);
		if (!inode)
			break;
		ino = btrfs_ino(BTRFS_I(inode));

		if (ino > max_key->objectid) {
			iput(inode);
			break;
		}

		objectid = ino + 1;
		if (!S_ISREG(inode->i_mode))
			continue;

		if (unlikely(min_key->objectid == ino)) {
			if (min_key->type > BTRFS_EXTENT_DATA_KEY)
				continue;
			if (min_key->type < BTRFS_EXTENT_DATA_KEY)
				start = 0;
			else {
				start = min_key->offset;
				WARN_ON(!IS_ALIGNED(start, fs_info->sectorsize));
			}
		} else {
			start = 0;
		}

		if (unlikely(max_key->objectid == ino)) {
			if (max_key->type < BTRFS_EXTENT_DATA_KEY)
				continue;
			if (max_key->type > BTRFS_EXTENT_DATA_KEY) {
				end = (u64)-1;
			} else {
				if (max_key->offset == 0)
					continue;
				end = max_key->offset;
				WARN_ON(!IS_ALIGNED(end, fs_info->sectorsize));
				end--;
			}
		} else {
			end = (u64)-1;
		}

		/* the lock_extent waits for read_folio to complete */
		lock_extent(&BTRFS_I(inode)->io_tree, start, end, NULL);
		btrfs_drop_extent_map_range(BTRFS_I(inode), start, end, true);
		unlock_extent(&BTRFS_I(inode)->io_tree, start, end, NULL);
	}
	return 0;
}

static int find_next_key(struct btrfs_path *path, int level,
			 struct btrfs_key *key)

{
	while (level < BTRFS_MAX_LEVEL) {
		if (!path->nodes[level])
			break;
		if (path->slots[level] + 1 <
		    btrfs_header_nritems(path->nodes[level])) {
			btrfs_node_key_to_cpu(path->nodes[level], key,
					      path->slots[level] + 1);
			return 0;
		}
		level++;
	}
	return 1;
}

/*
 * Insert current subvolume into reloc_control::dirty_subvol_roots
 */
static int insert_dirty_subvol(struct btrfs_trans_handle *trans,
			       struct reloc_control *rc,
			       struct btrfs_root *root)
{
	struct btrfs_root *reloc_root = root->reloc_root;
	struct btrfs_root_item *reloc_root_item;
	int ret;

	/* @root must be a subvolume tree root with a valid reloc tree */
	ASSERT(root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID);
	ASSERT(reloc_root);

	reloc_root_item = &reloc_root->root_item;
	memset(&reloc_root_item->drop_progress, 0,
		sizeof(reloc_root_item->drop_progress));
	btrfs_set_root_drop_level(reloc_root_item, 0);
	btrfs_set_root_refs(reloc_root_item, 0);
	ret = btrfs_update_reloc_root(trans, root);
	if (ret)
		return ret;

	if (list_empty(&root->reloc_dirty_list)) {
		btrfs_grab_root(root);
		list_add_tail(&root->reloc_dirty_list, &rc->dirty_subvol_roots);
	}

	return 0;
}

static int clean_dirty_subvols(struct reloc_control *rc)
{
	struct btrfs_root *root;
	struct btrfs_root *next;
	int ret = 0;
	int ret2;

	list_for_each_entry_safe(root, next, &rc->dirty_subvol_roots,
				 reloc_dirty_list) {
		if (root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID) {
			/* Merged subvolume, cleanup its reloc root */
			struct btrfs_root *reloc_root = root->reloc_root;

			list_del_init(&root->reloc_dirty_list);
			root->reloc_root = NULL;
			/*
			 * Need barrier to ensure clear_bit() only happens after
			 * root->reloc_root = NULL. Pairs with have_reloc_root.
			 */
			smp_wmb();
			clear_bit(BTRFS_ROOT_DEAD_RELOC_TREE, &root->state);
			if (reloc_root) {
				/*
				 * btrfs_drop_snapshot drops our ref we hold for
				 * ->reloc_root.  If it fails however we must
				 * drop the ref ourselves.
				 */
				ret2 = btrfs_drop_snapshot(reloc_root, 0, 1);
				if (ret2 < 0) {
					btrfs_put_root(reloc_root);
					if (!ret)
						ret = ret2;
				}
			}
			btrfs_put_root(root);
		} else {
			/* Orphan reloc tree, just clean it up */
			ret2 = btrfs_drop_snapshot(root, 0, 1);
			if (ret2 < 0) {
				btrfs_put_root(root);
				if (!ret)
					ret = ret2;
			}
		}
	}
	return ret;
}

/*
 * merge the relocated tree blocks in reloc tree with corresponding
 * fs tree.
 */
static noinline_for_stack int merge_reloc_root(struct reloc_control *rc,
					       struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = rc->extent_root->fs_info;
	struct btrfs_key key;
	struct btrfs_key next_key;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_root *reloc_root;
	struct btrfs_root_item *root_item;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int reserve_level;
	int level;
	int max_level;
	int replaced = 0;
	int ret = 0;
	u32 min_reserved;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = READA_FORWARD;

	reloc_root = root->reloc_root;
	root_item = &reloc_root->root_item;

	if (btrfs_disk_key_objectid(&root_item->drop_progress) == 0) {
		level = btrfs_root_level(root_item);
		atomic_inc(&reloc_root->node->refs);
		path->nodes[level] = reloc_root->node;
		path->slots[level] = 0;
	} else {
		btrfs_disk_key_to_cpu(&key, &root_item->drop_progress);

		level = btrfs_root_drop_level(root_item);
		BUG_ON(level == 0);
		path->lowest_level = level;
		ret = btrfs_search_slot(NULL, reloc_root, &key, path, 0, 0);
		path->lowest_level = 0;
		if (ret < 0) {
			btrfs_free_path(path);
			return ret;
		}

		btrfs_node_key_to_cpu(path->nodes[level], &next_key,
				      path->slots[level]);
		WARN_ON(memcmp(&key, &next_key, sizeof(key)));

		btrfs_unlock_up_safe(path, 0);
	}

	/*
	 * In merge_reloc_root(), we modify the upper level pointer to swap the
	 * tree blocks between reloc tree and subvolume tree.  Thus for tree
	 * block COW, we COW at most from level 1 to root level for each tree.
	 *
	 * Thus the needed metadata size is at most root_level * nodesize,
	 * and * 2 since we have two trees to COW.
	 */
	reserve_level = max_t(int, 1, btrfs_root_level(root_item));
	min_reserved = fs_info->nodesize * reserve_level * 2;
	memset(&next_key, 0, sizeof(next_key));

	while (1) {
		ret = btrfs_block_rsv_refill(fs_info, rc->block_rsv,
					     min_reserved,
					     BTRFS_RESERVE_FLUSH_LIMIT);
		if (ret)
			goto out;
		trans = btrfs_start_transaction(root, 0);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			goto out;
		}

		/*
		 * At this point we no longer have a reloc_control, so we can't
		 * depend on btrfs_init_reloc_root to update our last_trans.
		 *
		 * But that's ok, we started the trans handle on our
		 * corresponding fs_root, which means it's been added to the
		 * dirty list.  At commit time we'll still call
		 * btrfs_update_reloc_root() and update our root item
		 * appropriately.
		 */
		reloc_root->last_trans = trans->transid;
		trans->block_rsv = rc->block_rsv;

		replaced = 0;
		max_level = level;

		ret = walk_down_reloc_tree(reloc_root, path, &level);
		if (ret < 0)
			goto out;
		if (ret > 0)
			break;

		if (!find_next_key(path, level, &key) &&
		    btrfs_comp_cpu_keys(&next_key, &key) >= 0) {
			ret = 0;
		} else {
			ret = replace_path(trans, rc, root, reloc_root, path,
					   &next_key, level, max_level);
		}
		if (ret < 0)
			goto out;
		if (ret > 0) {
			level = ret;
			btrfs_node_key_to_cpu(path->nodes[level], &key,
					      path->slots[level]);
			replaced = 1;
		}

		ret = walk_up_reloc_tree(reloc_root, path, &level);
		if (ret > 0)
			break;

		BUG_ON(level == 0);
		/*
		 * save the merging progress in the drop_progress.
		 * this is OK since root refs == 1 in this case.
		 */
		btrfs_node_key(path->nodes[level], &root_item->drop_progress,
			       path->slots[level]);
		btrfs_set_root_drop_level(root_item, level);

		btrfs_end_transaction_throttle(trans);
		trans = NULL;

		btrfs_btree_balance_dirty(fs_info);

		if (replaced && rc->stage == UPDATE_DATA_PTRS)
			invalidate_extent_cache(root, &key, &next_key);
	}

	/*
	 * handle the case only one block in the fs tree need to be
	 * relocated and the block is tree root.
	 */
	leaf = btrfs_lock_root_node(root);
	ret = btrfs_cow_block(trans, root, leaf, NULL, 0, &leaf,
			      BTRFS_NESTING_COW);
	btrfs_tree_unlock(leaf);
	free_extent_buffer(leaf);
out:
	btrfs_free_path(path);

	if (ret == 0) {
		ret = insert_dirty_subvol(trans, rc, root);
		if (ret)
			btrfs_abort_transaction(trans, ret);
	}

	if (trans)
		btrfs_end_transaction_throttle(trans);

	btrfs_btree_balance_dirty(fs_info);

	if (replaced && rc->stage == UPDATE_DATA_PTRS)
		invalidate_extent_cache(root, &key, &next_key);

	return ret;
}

static noinline_for_stack
int prepare_to_merge(struct reloc_control *rc, int err)
{
	struct btrfs_root *root = rc->extent_root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_root *reloc_root;
	struct btrfs_trans_handle *trans;
	LIST_HEAD(reloc_roots);
	u64 num_bytes = 0;
	int ret;

	mutex_lock(&fs_info->reloc_mutex);
	rc->merging_rsv_size += fs_info->nodesize * (BTRFS_MAX_LEVEL - 1) * 2;
	rc->merging_rsv_size += rc->nodes_relocated * 2;
	mutex_unlock(&fs_info->reloc_mutex);

again:
	if (!err) {
		num_bytes = rc->merging_rsv_size;
		ret = btrfs_block_rsv_add(fs_info, rc->block_rsv, num_bytes,
					  BTRFS_RESERVE_FLUSH_ALL);
		if (ret)
			err = ret;
	}

	trans = btrfs_join_transaction(rc->extent_root);
	if (IS_ERR(trans)) {
		if (!err)
			btrfs_block_rsv_release(fs_info, rc->block_rsv,
						num_bytes, NULL);
		return PTR_ERR(trans);
	}

	if (!err) {
		if (num_bytes != rc->merging_rsv_size) {
			btrfs_end_transaction(trans);
			btrfs_block_rsv_release(fs_info, rc->block_rsv,
						num_bytes, NULL);
			goto again;
		}
	}

	rc->merge_reloc_tree = 1;

	while (!list_empty(&rc->reloc_roots)) {
		reloc_root = list_entry(rc->reloc_roots.next,
					struct btrfs_root, root_list);
		list_del_init(&reloc_root->root_list);

		root = btrfs_get_fs_root(fs_info, reloc_root->root_key.offset,
				false);
		if (IS_ERR(root)) {
			/*
			 * Even if we have an error we need this reloc root
			 * back on our list so we can clean up properly.
			 */
			list_add(&reloc_root->root_list, &reloc_roots);
			btrfs_abort_transaction(trans, (int)PTR_ERR(root));
			if (!err)
				err = PTR_ERR(root);
			break;
		}
		ASSERT(root->reloc_root == reloc_root);

		/*
		 * set reference count to 1, so btrfs_recover_relocation
		 * knows it should resumes merging
		 */
		if (!err)
			btrfs_set_root_refs(&reloc_root->root_item, 1);
		ret = btrfs_update_reloc_root(trans, root);

		/*
		 * Even if we have an error we need this reloc root back on our
		 * list so we can clean up properly.
		 */
		list_add(&reloc_root->root_list, &reloc_roots);
		btrfs_put_root(root);

		if (ret) {
			btrfs_abort_transaction(trans, ret);
			if (!err)
				err = ret;
			break;
		}
	}

	list_splice(&reloc_roots, &rc->reloc_roots);

	if (!err)
		err = btrfs_commit_transaction(trans);
	else
		btrfs_end_transaction(trans);
	return err;
}

static noinline_for_stack
void free_reloc_roots(struct list_head *list)
{
	struct btrfs_root *reloc_root, *tmp;

	list_for_each_entry_safe(reloc_root, tmp, list, root_list)
		__del_reloc_root(reloc_root);
}

static noinline_for_stack
void merge_reloc_roots(struct reloc_control *rc)
{
	struct btrfs_fs_info *fs_info = rc->extent_root->fs_info;
	struct btrfs_root *root;
	struct btrfs_root *reloc_root;
	LIST_HEAD(reloc_roots);
	int found = 0;
	int ret = 0;
again:
	root = rc->extent_root;

	/*
	 * this serializes us with btrfs_record_root_in_transaction,
	 * we have to make sure nobody is in the middle of
	 * adding their roots to the list while we are
	 * doing this splice
	 */
	mutex_lock(&fs_info->reloc_mutex);
	list_splice_init(&rc->reloc_roots, &reloc_roots);
	mutex_unlock(&fs_info->reloc_mutex);

	while (!list_empty(&reloc_roots)) {
		found = 1;
		reloc_root = list_entry(reloc_roots.next,
					struct btrfs_root, root_list);

		root = btrfs_get_fs_root(fs_info, reloc_root->root_key.offset,
					 false);
		if (btrfs_root_refs(&reloc_root->root_item) > 0) {
			if (IS_ERR(root)) {
				/*
				 * For recovery we read the fs roots on mount,
				 * and if we didn't find the root then we marked
				 * the reloc root as a garbage root.  For normal
				 * relocation obviously the root should exist in
				 * memory.  However there's no reason we can't
				 * handle the error properly here just in case.
				 */
				ASSERT(0);
				ret = PTR_ERR(root);
				goto out;
			}
			if (root->reloc_root != reloc_root) {
				/*
				 * This is actually impossible without something
				 * going really wrong (like weird race condition
				 * or cosmic rays).
				 */
				ASSERT(0);
				ret = -EINVAL;
				goto out;
			}
			ret = merge_reloc_root(rc, root);
			btrfs_put_root(root);
			if (ret) {
				if (list_empty(&reloc_root->root_list))
					list_add_tail(&reloc_root->root_list,
						      &reloc_roots);
				goto out;
			}
		} else {
			if (!IS_ERR(root)) {
				if (root->reloc_root == reloc_root) {
					root->reloc_root = NULL;
					btrfs_put_root(reloc_root);
				}
				clear_bit(BTRFS_ROOT_DEAD_RELOC_TREE,
					  &root->state);
				btrfs_put_root(root);
			}

			list_del_init(&reloc_root->root_list);
			/* Don't forget to queue this reloc root for cleanup */
			list_add_tail(&reloc_root->reloc_dirty_list,
				      &rc->dirty_subvol_roots);
		}
	}

	if (found) {
		found = 0;
		goto again;
	}
out:
	if (ret) {
		btrfs_handle_fs_error(fs_info, ret, NULL);
		free_reloc_roots(&reloc_roots);

		/* new reloc root may be added */
		mutex_lock(&fs_info->reloc_mutex);
		list_splice_init(&rc->reloc_roots, &reloc_roots);
		mutex_unlock(&fs_info->reloc_mutex);
		free_reloc_roots(&reloc_roots);
	}

	/*
	 * We used to have
	 *
	 * BUG_ON(!RB_EMPTY_ROOT(&rc->reloc_root_tree.rb_root));
	 *
	 * here, but it's wrong.  If we fail to start the transaction in
	 * prepare_to_merge() we will have only 0 ref reloc roots, none of which
	 * have actually been removed from the reloc_root_tree rb tree.  This is
	 * fine because we're bailing here, and we hold a reference on the root
	 * for the list that holds it, so these roots will be cleaned up when we
	 * do the reloc_dirty_list afterwards.  Meanwhile the root->reloc_root
	 * will be cleaned up on unmount.
	 *
	 * The remaining nodes will be cleaned up by free_reloc_control.
	 */
}

static void free_block_list(struct rb_root *blocks)
{
	struct tree_block *block;
	struct rb_node *rb_node;
	while ((rb_node = rb_first(blocks))) {
		block = rb_entry(rb_node, struct tree_block, rb_node);
		rb_erase(rb_node, blocks);
		kfree(block);
	}
}

static int record_reloc_root_in_trans(struct btrfs_trans_handle *trans,
				      struct btrfs_root *reloc_root)
{
	struct btrfs_fs_info *fs_info = reloc_root->fs_info;
	struct btrfs_root *root;
	int ret;

	if (reloc_root->last_trans == trans->transid)
		return 0;

	root = btrfs_get_fs_root(fs_info, reloc_root->root_key.offset, false);

	/*
	 * This should succeed, since we can't have a reloc root without having
	 * already looked up the actual root and created the reloc root for this
	 * root.
	 *
	 * However if there's some sort of corruption where we have a ref to a
	 * reloc root without a corresponding root this could return ENOENT.
	 */
	if (IS_ERR(root)) {
		ASSERT(0);
		return PTR_ERR(root);
	}
	if (root->reloc_root != reloc_root) {
		ASSERT(0);
		btrfs_err(fs_info,
			  "root %llu has two reloc roots associated with it",
			  reloc_root->root_key.offset);
		btrfs_put_root(root);
		return -EUCLEAN;
	}
	ret = btrfs_record_root_in_trans(trans, root);
	btrfs_put_root(root);

	return ret;
}

static noinline_for_stack
struct btrfs_root *select_reloc_root(struct btrfs_trans_handle *trans,
				     struct reloc_control *rc,
				     struct btrfs_backref_node *node,
				     struct btrfs_backref_edge *edges[])
{
	struct btrfs_backref_node *next;
	struct btrfs_root *root;
	int index = 0;
	int ret;

	next = node;
	while (1) {
		cond_resched();
		next = walk_up_backref(next, edges, &index);
		root = next->root;

		/*
		 * If there is no root, then our references for this block are
		 * incomplete, as we should be able to walk all the way up to a
		 * block that is owned by a root.
		 *
		 * This path is only for SHAREABLE roots, so if we come upon a
		 * non-SHAREABLE root then we have backrefs that resolve
		 * improperly.
		 *
		 * Both of these cases indicate file system corruption, or a bug
		 * in the backref walking code.
		 */
		if (!root) {
			ASSERT(0);
			btrfs_err(trans->fs_info,
		"bytenr %llu doesn't have a backref path ending in a root",
				  node->bytenr);
			return ERR_PTR(-EUCLEAN);
		}
		if (!test_bit(BTRFS_ROOT_SHAREABLE, &root->state)) {
			ASSERT(0);
			btrfs_err(trans->fs_info,
	"bytenr %llu has multiple refs with one ending in a non-shareable root",
				  node->bytenr);
			return ERR_PTR(-EUCLEAN);
		}

		if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID) {
			ret = record_reloc_root_in_trans(trans, root);
			if (ret)
				return ERR_PTR(ret);
			break;
		}

		ret = btrfs_record_root_in_trans(trans, root);
		if (ret)
			return ERR_PTR(ret);
		root = root->reloc_root;

		/*
		 * We could have raced with another thread which failed, so
		 * root->reloc_root may not be set, return ENOENT in this case.
		 */
		if (!root)
			return ERR_PTR(-ENOENT);

		if (next->new_bytenr != root->node->start) {
			/*
			 * We just created the reloc root, so we shouldn't have
			 * ->new_bytenr set and this shouldn't be in the changed
			 *  list.  If it is then we have multiple roots pointing
			 *  at the same bytenr which indicates corruption, or
			 *  we've made a mistake in the backref walking code.
			 */
			ASSERT(next->new_bytenr == 0);
			ASSERT(list_empty(&next->list));
			if (next->new_bytenr || !list_empty(&next->list)) {
				btrfs_err(trans->fs_info,
	"bytenr %llu possibly has multiple roots pointing at the same bytenr %llu",
					  node->bytenr, next->bytenr);
				return ERR_PTR(-EUCLEAN);
			}

			next->new_bytenr = root->node->start;
			btrfs_put_root(next->root);
			next->root = btrfs_grab_root(root);
			ASSERT(next->root);
			list_add_tail(&next->list,
				      &rc->backref_cache.changed);
			mark_block_processed(rc, next);
			break;
		}

		WARN_ON(1);
		root = NULL;
		next = walk_down_backref(edges, &index);
		if (!next || next->level <= node->level)
			break;
	}
	if (!root) {
		/*
		 * This can happen if there's fs corruption or if there's a bug
		 * in the backref lookup code.
		 */
		ASSERT(0);
		return ERR_PTR(-ENOENT);
	}

	next = node;
	/* setup backref node path for btrfs_reloc_cow_block */
	while (1) {
		rc->backref_cache.path[next->level] = next;
		if (--index < 0)
			break;
		next = edges[index]->node[UPPER];
	}
	return root;
}

/*
 * Select a tree root for relocation.
 *
 * Return NULL if the block is not shareable. We should use do_relocation() in
 * this case.
 *
 * Return a tree root pointer if the block is shareable.
 * Return -ENOENT if the block is root of reloc tree.
 */
static noinline_for_stack
struct btrfs_root *select_one_root(struct btrfs_backref_node *node)
{
	struct btrfs_backref_node *next;
	struct btrfs_root *root;
	struct btrfs_root *fs_root = NULL;
	struct btrfs_backref_edge *edges[BTRFS_MAX_LEVEL - 1];
	int index = 0;

	next = node;
	while (1) {
		cond_resched();
		next = walk_up_backref(next, edges, &index);
		root = next->root;

		/*
		 * This can occur if we have incomplete extent refs leading all
		 * the way up a particular path, in this case return -EUCLEAN.
		 */
		if (!root)
			return ERR_PTR(-EUCLEAN);

		/* No other choice for non-shareable tree */
		if (!test_bit(BTRFS_ROOT_SHAREABLE, &root->state))
			return root;

		if (root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID)
			fs_root = root;

		if (next != node)
			return NULL;

		next = walk_down_backref(edges, &index);
		if (!next || next->level <= node->level)
			break;
	}

	if (!fs_root)
		return ERR_PTR(-ENOENT);
	return fs_root;
}

static noinline_for_stack
u64 calcu_metadata_size(struct reloc_control *rc,
			struct btrfs_backref_node *node, int reserve)
{
	struct btrfs_fs_info *fs_info = rc->extent_root->fs_info;
	struct btrfs_backref_node *next = node;
	struct btrfs_backref_edge *edge;
	struct btrfs_backref_edge *edges[BTRFS_MAX_LEVEL - 1];
	u64 num_bytes = 0;
	int index = 0;

	BUG_ON(reserve && node->processed);

	while (next) {
		cond_resched();
		while (1) {
			if (next->processed && (reserve || next != node))
				break;

			num_bytes += fs_info->nodesize;

			if (list_empty(&next->upper))
				break;

			edge = list_entry(next->upper.next,
					struct btrfs_backref_edge, list[LOWER]);
			edges[index++] = edge;
			next = edge->node[UPPER];
		}
		next = walk_down_backref(edges, &index);
	}
	return num_bytes;
}

static int reserve_metadata_space(struct btrfs_trans_handle *trans,
				  struct reloc_control *rc,
				  struct btrfs_backref_node *node)
{
	struct btrfs_root *root = rc->extent_root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 num_bytes;
	int ret;
	u64 tmp;

	num_bytes = calcu_metadata_size(rc, node, 1) * 2;

	trans->block_rsv = rc->block_rsv;
	rc->reserved_bytes += num_bytes;

	/*
	 * We are under a transaction here so we can only do limited flushing.
	 * If we get an enospc just kick back -EAGAIN so we know to drop the
	 * transaction and try to refill when we can flush all the things.
	 */
	ret = btrfs_block_rsv_refill(fs_info, rc->block_rsv, num_bytes,
				     BTRFS_RESERVE_FLUSH_LIMIT);
	if (ret) {
		tmp = fs_info->nodesize * RELOCATION_RESERVED_NODES;
		while (tmp <= rc->reserved_bytes)
			tmp <<= 1;
		/*
		 * only one thread can access block_rsv at this point,
		 * so we don't need hold lock to protect block_rsv.
		 * we expand more reservation size here to allow enough
		 * space for relocation and we will return earlier in
		 * enospc case.
		 */
		rc->block_rsv->size = tmp + fs_info->nodesize *
				      RELOCATION_RESERVED_NODES;
		return -EAGAIN;
	}

	return 0;
}

/*
 * relocate a block tree, and then update pointers in upper level
 * blocks that reference the block to point to the new location.
 *
 * if called by link_to_upper, the block has already been relocated.
 * in that case this function just updates pointers.
 */
static int do_relocation(struct btrfs_trans_handle *trans,
			 struct reloc_control *rc,
			 struct btrfs_backref_node *node,
			 struct btrfs_key *key,
			 struct btrfs_path *path, int lowest)
{
	struct btrfs_backref_node *upper;
	struct btrfs_backref_edge *edge;
	struct btrfs_backref_edge *edges[BTRFS_MAX_LEVEL - 1];
	struct btrfs_root *root;
	struct extent_buffer *eb;
	u32 blocksize;
	u64 bytenr;
	int slot;
	int ret = 0;

	/*
	 * If we are lowest then this is the first time we're processing this
	 * block, and thus shouldn't have an eb associated with it yet.
	 */
	ASSERT(!lowest || !node->eb);

	path->lowest_level = node->level + 1;
	rc->backref_cache.path[node->level] = node;
	list_for_each_entry(edge, &node->upper, list[LOWER]) {
		struct btrfs_ref ref = { 0 };

		cond_resched();

		upper = edge->node[UPPER];
		root = select_reloc_root(trans, rc, upper, edges);
		if (IS_ERR(root)) {
			ret = PTR_ERR(root);
			goto next;
		}

		if (upper->eb && !upper->locked) {
			if (!lowest) {
				ret = btrfs_bin_search(upper->eb, key, &slot);
				if (ret < 0)
					goto next;
				BUG_ON(ret);
				bytenr = btrfs_node_blockptr(upper->eb, slot);
				if (node->eb->start == bytenr)
					goto next;
			}
			btrfs_backref_drop_node_buffer(upper);
		}

		if (!upper->eb) {
			ret = btrfs_search_slot(trans, root, key, path, 0, 1);
			if (ret) {
				if (ret > 0)
					ret = -ENOENT;

				btrfs_release_path(path);
				break;
			}

			if (!upper->eb) {
				upper->eb = path->nodes[upper->level];
				path->nodes[upper->level] = NULL;
			} else {
				BUG_ON(upper->eb != path->nodes[upper->level]);
			}

			upper->locked = 1;
			path->locks[upper->level] = 0;

			slot = path->slots[upper->level];
			btrfs_release_path(path);
		} else {
			ret = btrfs_bin_search(upper->eb, key, &slot);
			if (ret < 0)
				goto next;
			BUG_ON(ret);
		}

		bytenr = btrfs_node_blockptr(upper->eb, slot);
		if (lowest) {
			if (bytenr != node->bytenr) {
				btrfs_err(root->fs_info,
		"lowest leaf/node mismatch: bytenr %llu node->bytenr %llu slot %d upper %llu",
					  bytenr, node->bytenr, slot,
					  upper->eb->start);
				ret = -EIO;
				goto next;
			}
		} else {
			if (node->eb->start == bytenr)
				goto next;
		}

		blocksize = root->fs_info->nodesize;
		eb = btrfs_read_node_slot(upper->eb, slot);
		if (IS_ERR(eb)) {
			ret = PTR_ERR(eb);
			goto next;
		}
		btrfs_tree_lock(eb);

		if (!node->eb) {
			ret = btrfs_cow_block(trans, root, eb, upper->eb,
					      slot, &eb, BTRFS_NESTING_COW);
			btrfs_tree_unlock(eb);
			free_extent_buffer(eb);
			if (ret < 0)
				goto next;
			/*
			 * We've just COWed this block, it should have updated
			 * the correct backref node entry.
			 */
			ASSERT(node->eb == eb);
		} else {
			btrfs_set_node_blockptr(upper->eb, slot,
						node->eb->start);
			btrfs_set_node_ptr_generation(upper->eb, slot,
						      trans->transid);
			btrfs_mark_buffer_dirty(upper->eb);

			btrfs_init_generic_ref(&ref, BTRFS_ADD_DELAYED_REF,
					       node->eb->start, blocksize,
					       upper->eb->start);
			btrfs_init_tree_ref(&ref, node->level,
					    btrfs_header_owner(upper->eb),
					    root->root_key.objectid, false);
			ret = btrfs_inc_extent_ref(trans, &ref);
			if (!ret)
				ret = btrfs_drop_subtree(trans, root, eb,
							 upper->eb);
			if (ret)
				btrfs_abort_transaction(trans, ret);
		}
next:
		if (!upper->pending)
			btrfs_backref_drop_node_buffer(upper);
		else
			btrfs_backref_unlock_node_buffer(upper);
		if (ret)
			break;
	}

	if (!ret && node->pending) {
		btrfs_backref_drop_node_buffer(node);
		list_move_tail(&node->list, &rc->backref_cache.changed);
		node->pending = 0;
	}

	path->lowest_level = 0;

	/*
	 * We should have allocated all of our space in the block rsv and thus
	 * shouldn't ENOSPC.
	 */
	ASSERT(ret != -ENOSPC);
	return ret;
}

static int link_to_upper(struct btrfs_trans_handle *trans,
			 struct reloc_control *rc,
			 struct btrfs_backref_node *node,
			 struct btrfs_path *path)
{
	struct btrfs_key key;

	btrfs_node_key_to_cpu(node->eb, &key, 0);
	return do_relocation(trans, rc, node, &key, path, 0);
}

static int finish_pending_nodes(struct btrfs_trans_handle *trans,
				struct reloc_control *rc,
				struct btrfs_path *path, int err)
{
	LIST_HEAD(list);
	struct btrfs_backref_cache *cache = &rc->backref_cache;
	struct btrfs_backref_node *node;
	int level;
	int ret;

	for (level = 0; level < BTRFS_MAX_LEVEL; level++) {
		while (!list_empty(&cache->pending[level])) {
			node = list_entry(cache->pending[level].next,
					  struct btrfs_backref_node, list);
			list_move_tail(&node->list, &list);
			BUG_ON(!node->pending);

			if (!err) {
				ret = link_to_upper(trans, rc, node, path);
				if (ret < 0)
					err = ret;
			}
		}
		list_splice_init(&list, &cache->pending[level]);
	}
	return err;
}

/*
 * mark a block and all blocks directly/indirectly reference the block
 * as processed.
 */
static void update_processed_blocks(struct reloc_control *rc,
				    struct btrfs_backref_node *node)
{
	struct btrfs_backref_node *next = node;
	struct btrfs_backref_edge *edge;
	struct btrfs_backref_edge *edges[BTRFS_MAX_LEVEL - 1];
	int index = 0;

	while (next) {
		cond_resched();
		while (1) {
			if (next->processed)
				break;

			mark_block_processed(rc, next);

			if (list_empty(&next->upper))
				break;

			edge = list_entry(next->upper.next,
					struct btrfs_backref_edge, list[LOWER]);
			edges[index++] = edge;
			next = edge->node[UPPER];
		}
		next = walk_down_backref(edges, &index);
	}
}

static int tree_block_processed(u64 bytenr, struct reloc_control *rc)
{
	u32 blocksize = rc->extent_root->fs_info->nodesize;

	if (test_range_bit(&rc->processed_blocks, bytenr,
			   bytenr + blocksize - 1, EXTENT_DIRTY, 1, NULL))
		return 1;
	return 0;
}

static int get_tree_block_key(struct btrfs_fs_info *fs_info,
			      struct tree_block *block)
{
	struct extent_buffer *eb;

	eb = read_tree_block(fs_info, block->bytenr, block->owner,
			     block->key.offset, block->level, NULL);
	if (IS_ERR(eb))
		return PTR_ERR(eb);
	if (!extent_buffer_uptodate(eb)) {
		free_extent_buffer(eb);
		return -EIO;
	}
	if (block->level == 0)
		btrfs_item_key_to_cpu(eb, &block->key, 0);
	else
		btrfs_node_key_to_cpu(eb, &block->key, 0);
	free_extent_buffer(eb);
	block->key_ready = 1;
	return 0;
}

/*
 * helper function to relocate a tree block
 */
static int relocate_tree_block(struct btrfs_trans_handle *trans,
				struct reloc_control *rc,
				struct btrfs_backref_node *node,
				struct btrfs_key *key,
				struct btrfs_path *path)
{
	struct btrfs_root *root;
	int ret = 0;

	if (!node)
		return 0;

	/*
	 * If we fail here we want to drop our backref_node because we are going
	 * to start over and regenerate the tree for it.
	 */
	ret = reserve_metadata_space(trans, rc, node);
	if (ret)
		goto out;

	BUG_ON(node->processed);
	root = select_one_root(node);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);

		/* See explanation in select_one_root for the -EUCLEAN case. */
		ASSERT(ret == -ENOENT);
		if (ret == -ENOENT) {
			ret = 0;
			update_processed_blocks(rc, node);
		}
		goto out;
	}

	if (root) {
		if (test_bit(BTRFS_ROOT_SHAREABLE, &root->state)) {
			/*
			 * This block was the root block of a root, and this is
			 * the first time we're processing the block and thus it
			 * should not have had the ->new_bytenr modified and
			 * should have not been included on the changed list.
			 *
			 * However in the case of corruption we could have
			 * multiple refs pointing to the same block improperly,
			 * and thus we would trip over these checks.  ASSERT()
			 * for the developer case, because it could indicate a
			 * bug in the backref code, however error out for a
			 * normal user in the case of corruption.
			 */
			ASSERT(node->new_bytenr == 0);
			ASSERT(list_empty(&node->list));
			if (node->new_bytenr || !list_empty(&node->list)) {
				btrfs_err(root->fs_info,
				  "bytenr %llu has improper references to it",
					  node->bytenr);
				ret = -EUCLEAN;
				goto out;
			}
			ret = btrfs_record_root_in_trans(trans, root);
			if (ret)
				goto out;
			/*
			 * Another thread could have failed, need to check if we
			 * have reloc_root actually set.
			 */
			if (!root->reloc_root) {
				ret = -ENOENT;
				goto out;
			}
			root = root->reloc_root;
			node->new_bytenr = root->node->start;
			btrfs_put_root(node->root);
			node->root = btrfs_grab_root(root);
			ASSERT(node->root);
			list_add_tail(&node->list, &rc->backref_cache.changed);
		} else {
			path->lowest_level = node->level;
			if (root == root->fs_info->chunk_root)
				btrfs_reserve_chunk_metadata(trans, false);
			ret = btrfs_search_slot(trans, root, key, path, 0, 1);
			btrfs_release_path(path);
			if (root == root->fs_info->chunk_root)
				btrfs_trans_release_chunk_metadata(trans);
			if (ret > 0)
				ret = 0;
		}
		if (!ret)
			update_processed_blocks(rc, node);
	} else {
		ret = do_relocation(trans, rc, node, key, path, 1);
	}
out:
	if (ret || node->level == 0 || node->cowonly)
		btrfs_backref_cleanup_node(&rc->backref_cache, node);
	return ret;
}

/*
 * relocate a list of blocks
 */
static noinline_for_stack
int relocate_tree_blocks(struct btrfs_trans_handle *trans,
			 struct reloc_control *rc, struct rb_root *blocks)
{
	struct btrfs_fs_info *fs_info = rc->extent_root->fs_info;
	struct btrfs_backref_node *node;
	struct btrfs_path *path;
	struct tree_block *block;
	struct tree_block *next;
	int ret;
	int err = 0;

	path = btrfs_alloc_path();
	if (!path) {
		err = -ENOMEM;
		goto out_free_blocks;
	}

	/* Kick in readahead for tree blocks with missing keys */
	rbtree_postorder_for_each_entry_safe(block, next, blocks, rb_node) {
		if (!block->key_ready)
			btrfs_readahead_tree_block(fs_info, block->bytenr,
						   block->owner, 0,
						   block->level);
	}

	/* Get first keys */
	rbtree_postorder_for_each_entry_safe(block, next, blocks, rb_node) {
		if (!block->key_ready) {
			err = get_tree_block_key(fs_info, block);
			if (err)
				goto out_free_path;
		}
	}

	/* Do tree relocation */
	rbtree_postorder_for_each_entry_safe(block, next, blocks, rb_node) {
		node = build_backref_tree(rc, &block->key,
					  block->level, block->bytenr);
		if (IS_ERR(node)) {
			err = PTR_ERR(node);
			goto out;
		}

		ret = relocate_tree_block(trans, rc, node, &block->key,
					  path);
		if (ret < 0) {
			err = ret;
			break;
		}
	}
out:
	err = finish_pending_nodes(trans, rc, path, err);

out_free_path:
	btrfs_free_path(path);
out_free_blocks:
	free_block_list(blocks);
	return err;
}

static noinline_for_stack int prealloc_file_extent_cluster(
				struct btrfs_inode *inode,
				struct file_extent_cluster *cluster)
{
	u64 alloc_hint = 0;
	u64 start;
	u64 end;
	u64 offset = inode->index_cnt;
	u64 num_bytes;
	int nr;
	int ret = 0;
	u64 i_size = i_size_read(&inode->vfs_inode);
	u64 prealloc_start = cluster->start - offset;
	u64 prealloc_end = cluster->end - offset;
	u64 cur_offset = prealloc_start;

	/*
	 * For subpage case, previous i_size may not be aligned to PAGE_SIZE.
	 * This means the range [i_size, PAGE_END + 1) is filled with zeros by
	 * btrfs_do_readpage() call of previously relocated file cluster.
	 *
	 * If the current cluster starts in the above range, btrfs_do_readpage()
	 * will skip the read, and relocate_one_page() will later writeback
	 * the padding zeros as new data, causing data corruption.
	 *
	 * Here we have to manually invalidate the range (i_size, PAGE_END + 1).
	 */
	if (!IS_ALIGNED(i_size, PAGE_SIZE)) {
		struct address_space *mapping = inode->vfs_inode.i_mapping;
		struct btrfs_fs_info *fs_info = inode->root->fs_info;
		const u32 sectorsize = fs_info->sectorsize;
		struct page *page;

		ASSERT(sectorsize < PAGE_SIZE);
		ASSERT(IS_ALIGNED(i_size, sectorsize));

		/*
		 * Subpage can't handle page with DIRTY but without UPTODATE
		 * bit as it can lead to the following deadlock:
		 *
		 * btrfs_read_folio()
		 * | Page already *locked*
		 * |- btrfs_lock_and_flush_ordered_range()
		 *    |- btrfs_start_ordered_extent()
		 *       |- extent_write_cache_pages()
		 *          |- lock_page()
		 *             We try to lock the page we already hold.
		 *
		 * Here we just writeback the whole data reloc inode, so that
		 * we will be ensured to have no dirty range in the page, and
		 * are safe to clear the uptodate bits.
		 *
		 * This shouldn't cause too much overhead, as we need to write
		 * the data back anyway.
		 */
		ret = filemap_write_and_wait(mapping);
		if (ret < 0)
			return ret;

		clear_extent_bits(&inode->io_tree, i_size,
				  round_up(i_size, PAGE_SIZE) - 1,
				  EXTENT_UPTODATE);
		page = find_lock_page(mapping, i_size >> PAGE_SHIFT);
		/*
		 * If page is freed we don't need to do anything then, as we
		 * will re-read the whole page anyway.
		 */
		if (page) {
			btrfs_subpage_clear_uptodate(fs_info, page, i_size,
					round_up(i_size, PAGE_SIZE) - i_size);
			unlock_page(page);
			put_page(page);
		}
	}

	BUG_ON(cluster->start != cluster->boundary[0]);
	ret = btrfs_alloc_data_chunk_ondemand(inode,
					      prealloc_end + 1 - prealloc_start);
	if (ret)
		return ret;

	btrfs_inode_lock(&inode->vfs_inode, 0);
	for (nr = 0; nr < cluster->nr; nr++) {
		start = cluster->boundary[nr] - offset;
		if (nr + 1 < cluster->nr)
			end = cluster->boundary[nr + 1] - 1 - offset;
		else
			end = cluster->end - offset;

		lock_extent(&inode->io_tree, start, end, NULL);
		num_bytes = end + 1 - start;
		ret = btrfs_prealloc_file_range(&inode->vfs_inode, 0, start,
						num_bytes, num_bytes,
						end + 1, &alloc_hint);
		cur_offset = end + 1;
		unlock_extent(&inode->io_tree, start, end, NULL);
		if (ret)
			break;
	}
	btrfs_inode_unlock(&inode->vfs_inode, 0);

	if (cur_offset < prealloc_end)
		btrfs_free_reserved_data_space_noquota(inode->root->fs_info,
					       prealloc_end + 1 - cur_offset);
	return ret;
}

static noinline_for_stack int setup_relocation_extent_mapping(struct inode *inode,
				u64 start, u64 end, u64 block_start)
{
	struct extent_map *em;
	int ret = 0;

	em = alloc_extent_map();
	if (!em)
		return -ENOMEM;

	em->start = start;
	em->len = end + 1 - start;
	em->block_len = em->len;
	em->block_start = block_start;
	set_bit(EXTENT_FLAG_PINNED, &em->flags);

	lock_extent(&BTRFS_I(inode)->io_tree, start, end, NULL);
	ret = btrfs_replace_extent_map_range(BTRFS_I(inode), em, false);
	unlock_extent(&BTRFS_I(inode)->io_tree, start, end, NULL);
	free_extent_map(em);

	return ret;
}

/*
 * Allow error injection to test balance/relocation cancellation
 */
noinline int btrfs_should_cancel_balance(struct btrfs_fs_info *fs_info)
{
	return atomic_read(&fs_info->balance_cancel_req) ||
		atomic_read(&fs_info->reloc_cancel_req) ||
		fatal_signal_pending(current);
}
ALLOW_ERROR_INJECTION(btrfs_should_cancel_balance, TRUE);

static u64 get_cluster_boundary_end(struct file_extent_cluster *cluster,
				    int cluster_nr)
{
	/* Last extent, use cluster end directly */
	if (cluster_nr >= cluster->nr - 1)
		return cluster->end;

	/* Use next boundary start*/
	return cluster->boundary[cluster_nr + 1] - 1;
}

static int relocate_one_page(struct inode *inode, struct file_ra_state *ra,
			     struct file_extent_cluster *cluster,
			     int *cluster_nr, unsigned long page_index)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	u64 offset = BTRFS_I(inode)->index_cnt;
	const unsigned long last_index = (cluster->end - offset) >> PAGE_SHIFT;
	gfp_t mask = btrfs_alloc_write_mask(inode->i_mapping);
	struct page *page;
	u64 page_start;
	u64 page_end;
	u64 cur;
	int ret;

	ASSERT(page_index <= last_index);
	page = find_lock_page(inode->i_mapping, page_index);
	if (!page) {
		page_cache_sync_readahead(inode->i_mapping, ra, NULL,
				page_index, last_index + 1 - page_index);
		page = find_or_create_page(inode->i_mapping, page_index, mask);
		if (!page)
			return -ENOMEM;
	}
	ret = set_page_extent_mapped(page);
	if (ret < 0)
		goto release_page;

	if (PageReadahead(page))
		page_cache_async_readahead(inode->i_mapping, ra, NULL,
				page_folio(page), page_index,
				last_index + 1 - page_index);

	if (!PageUptodate(page)) {
		btrfs_read_folio(NULL, page_folio(page));
		lock_page(page);
		if (!PageUptodate(page)) {
			ret = -EIO;
			goto release_page;
		}
	}

	page_start = page_offset(page);
	page_end = page_start + PAGE_SIZE - 1;

	/*
	 * Start from the cluster, as for subpage case, the cluster can start
	 * inside the page.
	 */
	cur = max(page_start, cluster->boundary[*cluster_nr] - offset);
	while (cur <= page_end) {
		u64 extent_start = cluster->boundary[*cluster_nr] - offset;
		u64 extent_end = get_cluster_boundary_end(cluster,
						*cluster_nr) - offset;
		u64 clamped_start = max(page_start, extent_start);
		u64 clamped_end = min(page_end, extent_end);
		u32 clamped_len = clamped_end + 1 - clamped_start;

		/* Reserve metadata for this range */
		ret = btrfs_delalloc_reserve_metadata(BTRFS_I(inode),
						      clamped_len, clamped_len,
						      false);
		if (ret)
			goto release_page;

		/* Mark the range delalloc and dirty for later writeback */
		lock_extent(&BTRFS_I(inode)->io_tree, clamped_start, clamped_end, NULL);
		ret = btrfs_set_extent_delalloc(BTRFS_I(inode), clamped_start,
						clamped_end, 0, NULL);
		if (ret) {
			clear_extent_bits(&BTRFS_I(inode)->io_tree,
					clamped_start, clamped_end,
					EXTENT_LOCKED | EXTENT_BOUNDARY);
			btrfs_delalloc_release_metadata(BTRFS_I(inode),
							clamped_len, true);
			btrfs_delalloc_release_extents(BTRFS_I(inode),
						       clamped_len);
			goto release_page;
		}
		btrfs_page_set_dirty(fs_info, page, clamped_start, clamped_len);

		/*
		 * Set the boundary if it's inside the page.
		 * Data relocation requires the destination extents to have the
		 * same size as the source.
		 * EXTENT_BOUNDARY bit prevents current extent from being merged
		 * with previous extent.
		 */
		if (in_range(cluster->boundary[*cluster_nr] - offset,
			     page_start, PAGE_SIZE)) {
			u64 boundary_start = cluster->boundary[*cluster_nr] -
						offset;
			u64 boundary_end = boundary_start +
					   fs_info->sectorsize - 1;

			set_extent_bits(&BTRFS_I(inode)->io_tree,
					boundary_start, boundary_end,
					EXTENT_BOUNDARY);
		}
		unlock_extent(&BTRFS_I(inode)->io_tree, clamped_start, clamped_end, NULL);
		btrfs_delalloc_release_extents(BTRFS_I(inode), clamped_len);
		cur += clamped_len;

		/* Crossed extent end, go to next extent */
		if (cur >= extent_end) {
			(*cluster_nr)++;
			/* Just finished the last extent of the cluster, exit. */
			if (*cluster_nr >= cluster->nr)
				break;
		}
	}
	unlock_page(page);
	put_page(page);

	balance_dirty_pages_ratelimited(inode->i_mapping);
	btrfs_throttle(fs_info);
	if (btrfs_should_cancel_balance(fs_info))
		ret = -ECANCELED;
	return ret;

release_page:
	unlock_page(page);
	put_page(page);
	return ret;
}

static int relocate_file_extent_cluster(struct inode *inode,
					struct file_extent_cluster *cluster)
{
	u64 offset = BTRFS_I(inode)->index_cnt;
	unsigned long index;
	unsigned long last_index;
	struct file_ra_state *ra;
	int cluster_nr = 0;
	int ret = 0;

	if (!cluster->nr)
		return 0;

	ra = kzalloc(sizeof(*ra), GFP_NOFS);
	if (!ra)
		return -ENOMEM;

	ret = prealloc_file_extent_cluster(BTRFS_I(inode), cluster);
	if (ret)
		goto out;

	file_ra_state_init(ra, inode->i_mapping);

	ret = setup_relocation_extent_mapping(inode, cluster->start - offset,
				   cluster->end - offset, cluster->start);
	if (ret)
		goto out;

	last_index = (cluster->end - offset) >> PAGE_SHIFT;
	for (index = (cluster->start - offset) >> PAGE_SHIFT;
	     index <= last_index && !ret; index++)
		ret = relocate_one_page(inode, ra, cluster, &cluster_nr, index);
	if (ret == 0)
		WARN_ON(cluster_nr != cluster->nr);
out:
	kfree(ra);
	return ret;
}

static noinline_for_stack
int relocate_data_extent(struct inode *inode, struct btrfs_key *extent_key,
			 struct file_extent_cluster *cluster)
{
	int ret;

	if (cluster->nr > 0 && extent_key->objectid != cluster->end + 1) {
		ret = relocate_file_extent_cluster(inode, cluster);
		if (ret)
			return ret;
		cluster->nr = 0;
	}

	if (!cluster->nr)
		cluster->start = extent_key->objectid;
	else
		BUG_ON(cluster->nr >= MAX_EXTENTS);
	cluster->end = extent_key->objectid + extent_key->offset - 1;
	cluster->boundary[cluster->nr] = extent_key->objectid;
	cluster->nr++;

	if (cluster->nr >= MAX_EXTENTS) {
		ret = relocate_file_extent_cluster(inode, cluster);
		if (ret)
			return ret;
		cluster->nr = 0;
	}
	return 0;
}

/*
 * helper to add a tree block to the list.
 * the major work is getting the generation and level of the block
 */
static int add_tree_block(struct reloc_control *rc,
			  struct btrfs_key *extent_key,
			  struct btrfs_path *path,
			  struct rb_root *blocks)
{
	struct extent_buffer *eb;
	struct btrfs_extent_item *ei;
	struct btrfs_tree_block_info *bi;
	struct tree_block *block;
	struct rb_node *rb_node;
	u32 item_size;
	int level = -1;
	u64 generation;
	u64 owner = 0;

	eb =  path->nodes[0];
	item_size = btrfs_item_size(eb, path->slots[0]);

	if (extent_key->type == BTRFS_METADATA_ITEM_KEY ||
	    item_size >= sizeof(*ei) + sizeof(*bi)) {
		unsigned long ptr = 0, end;

		ei = btrfs_item_ptr(eb, path->slots[0],
				struct btrfs_extent_item);
		end = (unsigned long)ei + item_size;
		if (extent_key->type == BTRFS_EXTENT_ITEM_KEY) {
			bi = (struct btrfs_tree_block_info *)(ei + 1);
			level = btrfs_tree_block_level(eb, bi);
			ptr = (unsigned long)(bi + 1);
		} else {
			level = (int)extent_key->offset;
			ptr = (unsigned long)(ei + 1);
		}
		generation = btrfs_extent_generation(eb, ei);

		/*
		 * We're reading random blocks without knowing their owner ahead
		 * of time.  This is ok most of the time, as all reloc roots and
		 * fs roots have the same lock type.  However normal trees do
		 * not, and the only way to know ahead of time is to read the
		 * inline ref offset.  We know it's an fs root if
		 *
		 * 1. There's more than one ref.
		 * 2. There's a SHARED_DATA_REF_KEY set.
		 * 3. FULL_BACKREF is set on the flags.
		 *
		 * Otherwise it's safe to assume that the ref offset == the
		 * owner of this block, so we can use that when calling
		 * read_tree_block.
		 */
		if (btrfs_extent_refs(eb, ei) == 1 &&
		    !(btrfs_extent_flags(eb, ei) &
		      BTRFS_BLOCK_FLAG_FULL_BACKREF) &&
		    ptr < end) {
			struct btrfs_extent_inline_ref *iref;
			int type;

			iref = (struct btrfs_extent_inline_ref *)ptr;
			type = btrfs_get_extent_inline_ref_type(eb, iref,
							BTRFS_REF_TYPE_BLOCK);
			if (type == BTRFS_REF_TYPE_INVALID)
				return -EINVAL;
			if (type == BTRFS_TREE_BLOCK_REF_KEY)
				owner = btrfs_extent_inline_ref_offset(eb, iref);
		}
	} else if (unlikely(item_size == sizeof(struct btrfs_extent_item_v0))) {
		btrfs_print_v0_err(eb->fs_info);
		btrfs_handle_fs_error(eb->fs_info, -EINVAL, NULL);
		return -EINVAL;
	} else {
		BUG();
	}

	btrfs_release_path(path);

	BUG_ON(level == -1);

	block = kmalloc(sizeof(*block), GFP_NOFS);
	if (!block)
		return -ENOMEM;

	block->bytenr = extent_key->objectid;
	block->key.objectid = rc->extent_root->fs_info->nodesize;
	block->key.offset = generation;
	block->level = level;
	block->key_ready = 0;
	block->owner = owner;

	rb_node = rb_simple_insert(blocks, block->bytenr, &block->rb_node);
	if (rb_node)
		btrfs_backref_panic(rc->extent_root->fs_info, block->bytenr,
				    -EEXIST);

	return 0;
}

/*
 * helper to add tree blocks for backref of type BTRFS_SHARED_DATA_REF_KEY
 */
static int __add_tree_block(struct reloc_control *rc,
			    u64 bytenr, u32 blocksize,
			    struct rb_root *blocks)
{
	struct btrfs_fs_info *fs_info = rc->extent_root->fs_info;
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret;
	bool skinny = btrfs_fs_incompat(fs_info, SKINNY_METADATA);

	if (tree_block_processed(bytenr, rc))
		return 0;

	if (rb_simple_search(blocks, bytenr))
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
again:
	key.objectid = bytenr;
	if (skinny) {
		key.type = BTRFS_METADATA_ITEM_KEY;
		key.offset = (u64)-1;
	} else {
		key.type = BTRFS_EXTENT_ITEM_KEY;
		key.offset = blocksize;
	}

	path->search_commit_root = 1;
	path->skip_locking = 1;
	ret = btrfs_search_slot(NULL, rc->extent_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	if (ret > 0 && skinny) {
		if (path->slots[0]) {
			path->slots[0]--;
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      path->slots[0]);
			if (key.objectid == bytenr &&
			    (key.type == BTRFS_METADATA_ITEM_KEY ||
			     (key.type == BTRFS_EXTENT_ITEM_KEY &&
			      key.offset == blocksize)))
				ret = 0;
		}

		if (ret) {
			skinny = false;
			btrfs_release_path(path);
			goto again;
		}
	}
	if (ret) {
		ASSERT(ret == 1);
		btrfs_print_leaf(path->nodes[0]);
		btrfs_err(fs_info,
	     "tree block extent item (%llu) is not found in extent tree",
		     bytenr);
		WARN_ON(1);
		ret = -EINVAL;
		goto out;
	}

	ret = add_tree_block(rc, &key, path, blocks);
out:
	btrfs_free_path(path);
	return ret;
}

static int delete_block_group_cache(struct btrfs_fs_info *fs_info,
				    struct btrfs_block_group *block_group,
				    struct inode *inode,
				    u64 ino)
{
	struct btrfs_root *root = fs_info->tree_root;
	struct btrfs_trans_handle *trans;
	int ret = 0;

	if (inode)
		goto truncate;

	inode = btrfs_iget(fs_info->sb, ino, root);
	if (IS_ERR(inode))
		return -ENOENT;

truncate:
	ret = btrfs_check_trunc_cache_free_space(fs_info,
						 &fs_info->global_block_rsv);
	if (ret)
		goto out;

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	ret = btrfs_truncate_free_space_cache(trans, block_group, inode);

	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(fs_info);
out:
	iput(inode);
	return ret;
}

/*
 * Locate the free space cache EXTENT_DATA in root tree leaf and delete the
 * cache inode, to avoid free space cache data extent blocking data relocation.
 */
static int delete_v1_space_cache(struct extent_buffer *leaf,
				 struct btrfs_block_group *block_group,
				 u64 data_bytenr)
{
	u64 space_cache_ino;
	struct btrfs_file_extent_item *ei;
	struct btrfs_key key;
	bool found = false;
	int i;
	int ret;

	if (btrfs_header_owner(leaf) != BTRFS_ROOT_TREE_OBJECTID)
		return 0;

	for (i = 0; i < btrfs_header_nritems(leaf); i++) {
		u8 type;

		btrfs_item_key_to_cpu(leaf, &key, i);
		if (key.type != BTRFS_EXTENT_DATA_KEY)
			continue;
		ei = btrfs_item_ptr(leaf, i, struct btrfs_file_extent_item);
		type = btrfs_file_extent_type(leaf, ei);

		if ((type == BTRFS_FILE_EXTENT_REG ||
		     type == BTRFS_FILE_EXTENT_PREALLOC) &&
		    btrfs_file_extent_disk_bytenr(leaf, ei) == data_bytenr) {
			found = true;
			space_cache_ino = key.objectid;
			break;
		}
	}
	if (!found)
		return -ENOENT;
	ret = delete_block_group_cache(leaf->fs_info, block_group, NULL,
					space_cache_ino);
	return ret;
}

/*
 * helper to find all tree blocks that reference a given data extent
 */
static noinline_for_stack
int add_data_references(struct reloc_control *rc,
			struct btrfs_key *extent_key,
			struct btrfs_path *path,
			struct rb_root *blocks)
{
	struct btrfs_fs_info *fs_info = rc->extent_root->fs_info;
	struct ulist *leaves = NULL;
	struct ulist_iterator leaf_uiter;
	struct ulist_node *ref_node = NULL;
	const u32 blocksize = fs_info->nodesize;
	int ret = 0;

	btrfs_release_path(path);
	ret = btrfs_find_all_leafs(NULL, fs_info, extent_key->objectid,
				   0, &leaves, NULL, true);
	if (ret < 0)
		return ret;

	ULIST_ITER_INIT(&leaf_uiter);
	while ((ref_node = ulist_next(leaves, &leaf_uiter))) {
		struct extent_buffer *eb;

		eb = read_tree_block(fs_info, ref_node->val, 0, 0, 0, NULL);
		if (IS_ERR(eb)) {
			ret = PTR_ERR(eb);
			break;
		}
		ret = delete_v1_space_cache(eb, rc->block_group,
					    extent_key->objectid);
		free_extent_buffer(eb);
		if (ret < 0)
			break;
		ret = __add_tree_block(rc, ref_node->val, blocksize, blocks);
		if (ret < 0)
			break;
	}
	if (ret < 0)
		free_block_list(blocks);
	ulist_free(leaves);
	return ret;
}

/*
 * helper to find next unprocessed extent
 */
static noinline_for_stack
int find_next_extent(struct reloc_control *rc, struct btrfs_path *path,
		     struct btrfs_key *extent_key)
{
	struct btrfs_fs_info *fs_info = rc->extent_root->fs_info;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	u64 start, end, last;
	int ret;

	last = rc->block_group->start + rc->block_group->length;
	while (1) {
		cond_resched();
		if (rc->search_start >= last) {
			ret = 1;
			break;
		}

		key.objectid = rc->search_start;
		key.type = BTRFS_EXTENT_ITEM_KEY;
		key.offset = 0;

		path->search_commit_root = 1;
		path->skip_locking = 1;
		ret = btrfs_search_slot(NULL, rc->extent_root, &key, path,
					0, 0);
		if (ret < 0)
			break;
next:
		leaf = path->nodes[0];
		if (path->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(rc->extent_root, path);
			if (ret != 0)
				break;
			leaf = path->nodes[0];
		}

		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		if (key.objectid >= last) {
			ret = 1;
			break;
		}

		if (key.type != BTRFS_EXTENT_ITEM_KEY &&
		    key.type != BTRFS_METADATA_ITEM_KEY) {
			path->slots[0]++;
			goto next;
		}

		if (key.type == BTRFS_EXTENT_ITEM_KEY &&
		    key.objectid + key.offset <= rc->search_start) {
			path->slots[0]++;
			goto next;
		}

		if (key.type == BTRFS_METADATA_ITEM_KEY &&
		    key.objectid + fs_info->nodesize <=
		    rc->search_start) {
			path->slots[0]++;
			goto next;
		}

		ret = find_first_extent_bit(&rc->processed_blocks,
					    key.objectid, &start, &end,
					    EXTENT_DIRTY, NULL);

		if (ret == 0 && start <= key.objectid) {
			btrfs_release_path(path);
			rc->search_start = end + 1;
		} else {
			if (key.type == BTRFS_EXTENT_ITEM_KEY)
				rc->search_start = key.objectid + key.offset;
			else
				rc->search_start = key.objectid +
					fs_info->nodesize;
			memcpy(extent_key, &key, sizeof(key));
			return 0;
		}
	}
	btrfs_release_path(path);
	return ret;
}

static void set_reloc_control(struct reloc_control *rc)
{
	struct btrfs_fs_info *fs_info = rc->extent_root->fs_info;

	mutex_lock(&fs_info->reloc_mutex);
	fs_info->reloc_ctl = rc;
	mutex_unlock(&fs_info->reloc_mutex);
}

static void unset_reloc_control(struct reloc_control *rc)
{
	struct btrfs_fs_info *fs_info = rc->extent_root->fs_info;

	mutex_lock(&fs_info->reloc_mutex);
	fs_info->reloc_ctl = NULL;
	mutex_unlock(&fs_info->reloc_mutex);
}

static noinline_for_stack
int prepare_to_relocate(struct reloc_control *rc)
{
	struct btrfs_trans_handle *trans;
	int ret;

	rc->block_rsv = btrfs_alloc_block_rsv(rc->extent_root->fs_info,
					      BTRFS_BLOCK_RSV_TEMP);
	if (!rc->block_rsv)
		return -ENOMEM;

	memset(&rc->cluster, 0, sizeof(rc->cluster));
	rc->search_start = rc->block_group->start;
	rc->extents_found = 0;
	rc->nodes_relocated = 0;
	rc->merging_rsv_size = 0;
	rc->reserved_bytes = 0;
	rc->block_rsv->size = rc->extent_root->fs_info->nodesize *
			      RELOCATION_RESERVED_NODES;
	ret = btrfs_block_rsv_refill(rc->extent_root->fs_info,
				     rc->block_rsv, rc->block_rsv->size,
				     BTRFS_RESERVE_FLUSH_ALL);
	if (ret)
		return ret;

	rc->create_reloc_tree = 1;
	set_reloc_control(rc);

	trans = btrfs_join_transaction(rc->extent_root);
	if (IS_ERR(trans)) {
		unset_reloc_control(rc);
		/*
		 * extent tree is not a ref_cow tree and has no reloc_root to
		 * cleanup.  And callers are responsible to free the above
		 * block rsv.
		 */
		return PTR_ERR(trans);
	}

	ret = btrfs_commit_transaction(trans);
	if (ret)
		unset_reloc_control(rc);

	return ret;
}

static noinline_for_stack int relocate_block_group(struct reloc_control *rc)
{
	struct btrfs_fs_info *fs_info = rc->extent_root->fs_info;
	struct rb_root blocks = RB_ROOT;
	struct btrfs_key key;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_path *path;
	struct btrfs_extent_item *ei;
	u64 flags;
	int ret;
	int err = 0;
	int progress = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = READA_FORWARD;

	ret = prepare_to_relocate(rc);
	if (ret) {
		err = ret;
		goto out_free;
	}

	while (1) {
		rc->reserved_bytes = 0;
		ret = btrfs_block_rsv_refill(fs_info, rc->block_rsv,
					     rc->block_rsv->size,
					     BTRFS_RESERVE_FLUSH_ALL);
		if (ret) {
			err = ret;
			break;
		}
		progress++;
		trans = btrfs_start_transaction(rc->extent_root, 0);
		if (IS_ERR(trans)) {
			err = PTR_ERR(trans);
			trans = NULL;
			break;
		}
restart:
		if (update_backref_cache(trans, &rc->backref_cache)) {
			btrfs_end_transaction(trans);
			trans = NULL;
			continue;
		}

		ret = find_next_extent(rc, path, &key);
		if (ret < 0)
			err = ret;
		if (ret != 0)
			break;

		rc->extents_found++;

		ei = btrfs_item_ptr(path->nodes[0], path->slots[0],
				    struct btrfs_extent_item);
		flags = btrfs_extent_flags(path->nodes[0], ei);

		if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
			ret = add_tree_block(rc, &key, path, &blocks);
		} else if (rc->stage == UPDATE_DATA_PTRS &&
			   (flags & BTRFS_EXTENT_FLAG_DATA)) {
			ret = add_data_references(rc, &key, path, &blocks);
		} else {
			btrfs_release_path(path);
			ret = 0;
		}
		if (ret < 0) {
			err = ret;
			break;
		}

		if (!RB_EMPTY_ROOT(&blocks)) {
			ret = relocate_tree_blocks(trans, rc, &blocks);
			if (ret < 0) {
				if (ret != -EAGAIN) {
					err = ret;
					break;
				}
				rc->extents_found--;
				rc->search_start = key.objectid;
			}
		}

		btrfs_end_transaction_throttle(trans);
		btrfs_btree_balance_dirty(fs_info);
		trans = NULL;

		if (rc->stage == MOVE_DATA_EXTENTS &&
		    (flags & BTRFS_EXTENT_FLAG_DATA)) {
			rc->found_file_extent = 1;
			ret = relocate_data_extent(rc->data_inode,
						   &key, &rc->cluster);
			if (ret < 0) {
				err = ret;
				break;
			}
		}
		if (btrfs_should_cancel_balance(fs_info)) {
			err = -ECANCELED;
			break;
		}
	}
	if (trans && progress && err == -ENOSPC) {
		ret = btrfs_force_chunk_alloc(trans, rc->block_group->flags);
		if (ret == 1) {
			err = 0;
			progress = 0;
			goto restart;
		}
	}

	btrfs_release_path(path);
	clear_extent_bits(&rc->processed_blocks, 0, (u64)-1, EXTENT_DIRTY);

	if (trans) {
		btrfs_end_transaction_throttle(trans);
		btrfs_btree_balance_dirty(fs_info);
	}

	if (!err) {
		ret = relocate_file_extent_cluster(rc->data_inode,
						   &rc->cluster);
		if (ret < 0)
			err = ret;
	}

	rc->create_reloc_tree = 0;
	set_reloc_control(rc);

	btrfs_backref_release_cache(&rc->backref_cache);
	btrfs_block_rsv_release(fs_info, rc->block_rsv, (u64)-1, NULL);

	/*
	 * Even in the case when the relocation is cancelled, we should all go
	 * through prepare_to_merge() and merge_reloc_roots().
	 *
	 * For error (including cancelled balance), prepare_to_merge() will
	 * mark all reloc trees orphan, then queue them for cleanup in
	 * merge_reloc_roots()
	 */
	err = prepare_to_merge(rc, err);

	merge_reloc_roots(rc);

	rc->merge_reloc_tree = 0;
	unset_reloc_control(rc);
	btrfs_block_rsv_release(fs_info, rc->block_rsv, (u64)-1, NULL);

	/* get rid of pinned extents */
	trans = btrfs_join_transaction(rc->extent_root);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out_free;
	}
	ret = btrfs_commit_transaction(trans);
	if (ret && !err)
		err = ret;
out_free:
	ret = clean_dirty_subvols(rc);
	if (ret < 0 && !err)
		err = ret;
	btrfs_free_block_rsv(fs_info, rc->block_rsv);
	btrfs_free_path(path);
	return err;
}

static int __insert_orphan_inode(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root, u64 objectid)
{
	struct btrfs_path *path;
	struct btrfs_inode_item *item;
	struct extent_buffer *leaf;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_insert_empty_inode(trans, root, path, objectid);
	if (ret)
		goto out;

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_inode_item);
	memzero_extent_buffer(leaf, (unsigned long)item, sizeof(*item));
	btrfs_set_inode_generation(leaf, item, 1);
	btrfs_set_inode_size(leaf, item, 0);
	btrfs_set_inode_mode(leaf, item, S_IFREG | 0600);
	btrfs_set_inode_flags(leaf, item, BTRFS_INODE_NOCOMPRESS |
					  BTRFS_INODE_PREALLOC);
	btrfs_mark_buffer_dirty(leaf);
out:
	btrfs_free_path(path);
	return ret;
}

static void delete_orphan_inode(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, u64 objectid)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret = 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = objectid;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto out;
	}
	ret = btrfs_del_item(trans, root, path);
out:
	if (ret)
		btrfs_abort_transaction(trans, ret);
	btrfs_free_path(path);
}

/*
 * helper to create inode for data relocation.
 * the inode is in data relocation tree and its link count is 0
 */
static noinline_for_stack
struct inode *create_reloc_inode(struct btrfs_fs_info *fs_info,
				 struct btrfs_block_group *group)
{
	struct inode *inode = NULL;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root;
	u64 objectid;
	int err = 0;

	root = btrfs_grab_root(fs_info->data_reloc_root);
	trans = btrfs_start_transaction(root, 6);
	if (IS_ERR(trans)) {
		btrfs_put_root(root);
		return ERR_CAST(trans);
	}

	err = btrfs_get_free_objectid(root, &objectid);
	if (err)
		goto out;

	err = __insert_orphan_inode(trans, root, objectid);
	if (err)
		goto out;

	inode = btrfs_iget(fs_info->sb, objectid, root);
	if (IS_ERR(inode)) {
		delete_orphan_inode(trans, root, objectid);
		err = PTR_ERR(inode);
		inode = NULL;
		goto out;
	}
	BTRFS_I(inode)->index_cnt = group->start;

	err = btrfs_orphan_add(trans, BTRFS_I(inode));
out:
	btrfs_put_root(root);
	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(fs_info);
	if (err) {
		iput(inode);
		inode = ERR_PTR(err);
	}
	return inode;
}

/*
 * Mark start of chunk relocation that is cancellable. Check if the cancellation
 * has been requested meanwhile and don't start in that case.
 *
 * Return:
 *   0             success
 *   -EINPROGRESS  operation is already in progress, that's probably a bug
 *   -ECANCELED    cancellation request was set before the operation started
 */
static int reloc_chunk_start(struct btrfs_fs_info *fs_info)
{
	if (test_and_set_bit(BTRFS_FS_RELOC_RUNNING, &fs_info->flags)) {
		/* This should not happen */
		btrfs_err(fs_info, "reloc already running, cannot start");
		return -EINPROGRESS;
	}

	if (atomic_read(&fs_info->reloc_cancel_req) > 0) {
		btrfs_info(fs_info, "chunk relocation canceled on start");
		/*
		 * On cancel, clear all requests but let the caller mark
		 * the end after cleanup operations.
		 */
		atomic_set(&fs_info->reloc_cancel_req, 0);
		return -ECANCELED;
	}
	return 0;
}

/*
 * Mark end of chunk relocation that is cancellable and wake any waiters.
 */
static void reloc_chunk_end(struct btrfs_fs_info *fs_info)
{
	/* Requested after start, clear bit first so any waiters can continue */
	if (atomic_read(&fs_info->reloc_cancel_req) > 0)
		btrfs_info(fs_info, "chunk relocation canceled during operation");
	clear_and_wake_up_bit(BTRFS_FS_RELOC_RUNNING, &fs_info->flags);
	atomic_set(&fs_info->reloc_cancel_req, 0);
}

static struct reloc_control *alloc_reloc_control(struct btrfs_fs_info *fs_info)
{
	struct reloc_control *rc;

	rc = kzalloc(sizeof(*rc), GFP_NOFS);
	if (!rc)
		return NULL;

	INIT_LIST_HEAD(&rc->reloc_roots);
	INIT_LIST_HEAD(&rc->dirty_subvol_roots);
	btrfs_backref_init_cache(fs_info, &rc->backref_cache, 1);
	mapping_tree_init(&rc->reloc_root_tree);
	extent_io_tree_init(fs_info, &rc->processed_blocks,
			    IO_TREE_RELOC_BLOCKS, NULL);
	return rc;
}

static void free_reloc_control(struct reloc_control *rc)
{
	struct mapping_node *node, *tmp;

	free_reloc_roots(&rc->reloc_roots);
	rbtree_postorder_for_each_entry_safe(node, tmp,
			&rc->reloc_root_tree.rb_root, rb_node)
		kfree(node);

	kfree(rc);
}

/*
 * Print the block group being relocated
 */
static void describe_relocation(struct btrfs_fs_info *fs_info,
				struct btrfs_block_group *block_group)
{
	char buf[128] = {'\0'};

	btrfs_describe_block_groups(block_group->flags, buf, sizeof(buf));

	btrfs_info(fs_info,
		   "relocating block group %llu flags %s",
		   block_group->start, buf);
}

static const char *stage_to_string(int stage)
{
	if (stage == MOVE_DATA_EXTENTS)
		return "move data extents";
	if (stage == UPDATE_DATA_PTRS)
		return "update data pointers";
	return "unknown";
}

/*
 * function to relocate all extents in a block group.
 */
int btrfs_relocate_block_group(struct btrfs_fs_info *fs_info, u64 group_start)
{
	struct btrfs_block_group *bg;
	struct btrfs_root *extent_root = btrfs_extent_root(fs_info, group_start);
	struct reloc_control *rc;
	struct inode *inode;
	struct btrfs_path *path;
	int ret;
	int rw = 0;
	int err = 0;

	/*
	 * This only gets set if we had a half-deleted snapshot on mount.  We
	 * cannot allow relocation to start while we're still trying to clean up
	 * these pending deletions.
	 */
	ret = wait_on_bit(&fs_info->flags, BTRFS_FS_UNFINISHED_DROPS, TASK_INTERRUPTIBLE);
	if (ret)
		return ret;

	/* We may have been woken up by close_ctree, so bail if we're closing. */
	if (btrfs_fs_closing(fs_info))
		return -EINTR;

	bg = btrfs_lookup_block_group(fs_info, group_start);
	if (!bg)
		return -ENOENT;

	/*
	 * Relocation of a data block group creates ordered extents.  Without
	 * sb_start_write(), we can freeze the filesystem while unfinished
	 * ordered extents are left. Such ordered extents can cause a deadlock
	 * e.g. when syncfs() is waiting for their completion but they can't
	 * finish because they block when joining a transaction, due to the
	 * fact that the freeze locks are being held in write mode.
	 */
	if (bg->flags & BTRFS_BLOCK_GROUP_DATA)
		ASSERT(sb_write_started(fs_info->sb));

	if (btrfs_pinned_by_swapfile(fs_info, bg)) {
		btrfs_put_block_group(bg);
		return -ETXTBSY;
	}

	rc = alloc_reloc_control(fs_info);
	if (!rc) {
		btrfs_put_block_group(bg);
		return -ENOMEM;
	}

	ret = reloc_chunk_start(fs_info);
	if (ret < 0) {
		err = ret;
		goto out_put_bg;
	}

	rc->extent_root = extent_root;
	rc->block_group = bg;

	ret = btrfs_inc_block_group_ro(rc->block_group, true);
	if (ret) {
		err = ret;
		goto out;
	}
	rw = 1;

	path = btrfs_alloc_path();
	if (!path) {
		err = -ENOMEM;
		goto out;
	}

	inode = lookup_free_space_inode(rc->block_group, path);
	btrfs_free_path(path);

	if (!IS_ERR(inode))
		ret = delete_block_group_cache(fs_info, rc->block_group, inode, 0);
	else
		ret = PTR_ERR(inode);

	if (ret && ret != -ENOENT) {
		err = ret;
		goto out;
	}

	rc->data_inode = create_reloc_inode(fs_info, rc->block_group);
	if (IS_ERR(rc->data_inode)) {
		err = PTR_ERR(rc->data_inode);
		rc->data_inode = NULL;
		goto out;
	}

	describe_relocation(fs_info, rc->block_group);

	btrfs_wait_block_group_reservations(rc->block_group);
	btrfs_wait_nocow_writers(rc->block_group);
	btrfs_wait_ordered_roots(fs_info, U64_MAX,
				 rc->block_group->start,
				 rc->block_group->length);

	ret = btrfs_zone_finish(rc->block_group);
	WARN_ON(ret && ret != -EAGAIN);

	while (1) {
		int finishes_stage;

		mutex_lock(&fs_info->cleaner_mutex);
		ret = relocate_block_group(rc);
		mutex_unlock(&fs_info->cleaner_mutex);
		if (ret < 0)
			err = ret;

		finishes_stage = rc->stage;
		/*
		 * We may have gotten ENOSPC after we already dirtied some
		 * extents.  If writeout happens while we're relocating a
		 * different block group we could end up hitting the
		 * BUG_ON(rc->stage == UPDATE_DATA_PTRS) in
		 * btrfs_reloc_cow_block.  Make sure we write everything out
		 * properly so we don't trip over this problem, and then break
		 * out of the loop if we hit an error.
		 */
		if (rc->stage == MOVE_DATA_EXTENTS && rc->found_file_extent) {
			ret = btrfs_wait_ordered_range(rc->data_inode, 0,
						       (u64)-1);
			if (ret)
				err = ret;
			invalidate_mapping_pages(rc->data_inode->i_mapping,
						 0, -1);
			rc->stage = UPDATE_DATA_PTRS;
		}

		if (err < 0)
			goto out;

		if (rc->extents_found == 0)
			break;

		btrfs_info(fs_info, "found %llu extents, stage: %s",
			   rc->extents_found, stage_to_string(finishes_stage));
	}

	WARN_ON(rc->block_group->pinned > 0);
	WARN_ON(rc->block_group->reserved > 0);
	WARN_ON(rc->block_group->used > 0);
out:
	if (err && rw)
		btrfs_dec_block_group_ro(rc->block_group);
	iput(rc->data_inode);
out_put_bg:
	btrfs_put_block_group(bg);
	reloc_chunk_end(fs_info);
	free_reloc_control(rc);
	return err;
}

static noinline_for_stack int mark_garbage_root(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans;
	int ret, err;

	trans = btrfs_start_transaction(fs_info->tree_root, 0);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	memset(&root->root_item.drop_progress, 0,
		sizeof(root->root_item.drop_progress));
	btrfs_set_root_drop_level(&root->root_item, 0);
	btrfs_set_root_refs(&root->root_item, 0);
	ret = btrfs_update_root(trans, fs_info->tree_root,
				&root->root_key, &root->root_item);

	err = btrfs_end_transaction(trans);
	if (err)
		return err;
	return ret;
}

/*
 * recover relocation interrupted by system crash.
 *
 * this function resumes merging reloc trees with corresponding fs trees.
 * this is important for keeping the sharing of tree blocks
 */
int btrfs_recover_relocation(struct btrfs_fs_info *fs_info)
{
	LIST_HEAD(reloc_roots);
	struct btrfs_key key;
	struct btrfs_root *fs_root;
	struct btrfs_root *reloc_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct reloc_control *rc = NULL;
	struct btrfs_trans_handle *trans;
	int ret;
	int err = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = READA_BACK;

	key.objectid = BTRFS_TREE_RELOC_OBJECTID;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = (u64)-1;

	while (1) {
		ret = btrfs_search_slot(NULL, fs_info->tree_root, &key,
					path, 0, 0);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		if (ret > 0) {
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		}
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		btrfs_release_path(path);

		if (key.objectid != BTRFS_TREE_RELOC_OBJECTID ||
		    key.type != BTRFS_ROOT_ITEM_KEY)
			break;

		reloc_root = btrfs_read_tree_root(fs_info->tree_root, &key);
		if (IS_ERR(reloc_root)) {
			err = PTR_ERR(reloc_root);
			goto out;
		}

		set_bit(BTRFS_ROOT_SHAREABLE, &reloc_root->state);
		list_add(&reloc_root->root_list, &reloc_roots);

		if (btrfs_root_refs(&reloc_root->root_item) > 0) {
			fs_root = btrfs_get_fs_root(fs_info,
					reloc_root->root_key.offset, false);
			if (IS_ERR(fs_root)) {
				ret = PTR_ERR(fs_root);
				if (ret != -ENOENT) {
					err = ret;
					goto out;
				}
				ret = mark_garbage_root(reloc_root);
				if (ret < 0) {
					err = ret;
					goto out;
				}
			} else {
				btrfs_put_root(fs_root);
			}
		}

		if (key.offset == 0)
			break;

		key.offset--;
	}
	btrfs_release_path(path);

	if (list_empty(&reloc_roots))
		goto out;

	rc = alloc_reloc_control(fs_info);
	if (!rc) {
		err = -ENOMEM;
		goto out;
	}

	ret = reloc_chunk_start(fs_info);
	if (ret < 0) {
		err = ret;
		goto out_end;
	}

	rc->extent_root = btrfs_extent_root(fs_info, 0);

	set_reloc_control(rc);

	trans = btrfs_join_transaction(rc->extent_root);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out_unset;
	}

	rc->merge_reloc_tree = 1;

	while (!list_empty(&reloc_roots)) {
		reloc_root = list_entry(reloc_roots.next,
					struct btrfs_root, root_list);
		list_del(&reloc_root->root_list);

		if (btrfs_root_refs(&reloc_root->root_item) == 0) {
			list_add_tail(&reloc_root->root_list,
				      &rc->reloc_roots);
			continue;
		}

		fs_root = btrfs_get_fs_root(fs_info, reloc_root->root_key.offset,
					    false);
		if (IS_ERR(fs_root)) {
			err = PTR_ERR(fs_root);
			list_add_tail(&reloc_root->root_list, &reloc_roots);
			btrfs_end_transaction(trans);
			goto out_unset;
		}

		err = __add_reloc_root(reloc_root);
		ASSERT(err != -EEXIST);
		if (err) {
			list_add_tail(&reloc_root->root_list, &reloc_roots);
			btrfs_put_root(fs_root);
			btrfs_end_transaction(trans);
			goto out_unset;
		}
		fs_root->reloc_root = btrfs_grab_root(reloc_root);
		btrfs_put_root(fs_root);
	}

	err = btrfs_commit_transaction(trans);
	if (err)
		goto out_unset;

	merge_reloc_roots(rc);

	unset_reloc_control(rc);

	trans = btrfs_join_transaction(rc->extent_root);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out_clean;
	}
	err = btrfs_commit_transaction(trans);
out_clean:
	ret = clean_dirty_subvols(rc);
	if (ret < 0 && !err)
		err = ret;
out_unset:
	unset_reloc_control(rc);
out_end:
	reloc_chunk_end(fs_info);
	free_reloc_control(rc);
out:
	free_reloc_roots(&reloc_roots);

	btrfs_free_path(path);

	if (err == 0) {
		/* cleanup orphan inode in data relocation tree */
		fs_root = btrfs_grab_root(fs_info->data_reloc_root);
		ASSERT(fs_root);
		err = btrfs_orphan_cleanup(fs_root);
		btrfs_put_root(fs_root);
	}
	return err;
}

/*
 * helper to add ordered checksum for data relocation.
 *
 * cloning checksum properly handles the nodatasum extents.
 * it also saves CPU time to re-calculate the checksum.
 */
int btrfs_reloc_clone_csums(struct btrfs_inode *inode, u64 file_pos, u64 len)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_root *csum_root;
	struct btrfs_ordered_sum *sums;
	struct btrfs_ordered_extent *ordered;
	int ret;
	u64 disk_bytenr;
	u64 new_bytenr;
	LIST_HEAD(list);

	ordered = btrfs_lookup_ordered_extent(inode, file_pos);
	BUG_ON(ordered->file_offset != file_pos || ordered->num_bytes != len);

	disk_bytenr = file_pos + inode->index_cnt;
	csum_root = btrfs_csum_root(fs_info, disk_bytenr);
	ret = btrfs_lookup_csums_range(csum_root, disk_bytenr,
				       disk_bytenr + len - 1, &list, 0, false);
	if (ret)
		goto out;

	while (!list_empty(&list)) {
		sums = list_entry(list.next, struct btrfs_ordered_sum, list);
		list_del_init(&sums->list);

		/*
		 * We need to offset the new_bytenr based on where the csum is.
		 * We need to do this because we will read in entire prealloc
		 * extents but we may have written to say the middle of the
		 * prealloc extent, so we need to make sure the csum goes with
		 * the right disk offset.
		 *
		 * We can do this because the data reloc inode refers strictly
		 * to the on disk bytes, so we don't have to worry about
		 * disk_len vs real len like with real inodes since it's all
		 * disk length.
		 */
		new_bytenr = ordered->disk_bytenr + sums->bytenr - disk_bytenr;
		sums->bytenr = new_bytenr;

		btrfs_add_ordered_sum(ordered, sums);
	}
out:
	btrfs_put_ordered_extent(ordered);
	return ret;
}

int btrfs_reloc_cow_block(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct extent_buffer *buf,
			  struct extent_buffer *cow)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct reloc_control *rc;
	struct btrfs_backref_node *node;
	int first_cow = 0;
	int level;
	int ret = 0;

	rc = fs_info->reloc_ctl;
	if (!rc)
		return 0;

	BUG_ON(rc->stage == UPDATE_DATA_PTRS && btrfs_is_data_reloc_root(root));

	level = btrfs_header_level(buf);
	if (btrfs_header_generation(buf) <=
	    btrfs_root_last_snapshot(&root->root_item))
		first_cow = 1;

	if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID &&
	    rc->create_reloc_tree) {
		WARN_ON(!first_cow && level == 0);

		node = rc->backref_cache.path[level];
		BUG_ON(node->bytenr != buf->start &&
		       node->new_bytenr != buf->start);

		btrfs_backref_drop_node_buffer(node);
		atomic_inc(&cow->refs);
		node->eb = cow;
		node->new_bytenr = cow->start;

		if (!node->pending) {
			list_move_tail(&node->list,
				       &rc->backref_cache.pending[level]);
			node->pending = 1;
		}

		if (first_cow)
			mark_block_processed(rc, node);

		if (first_cow && level > 0)
			rc->nodes_relocated += buf->len;
	}

	if (level == 0 && first_cow && rc->stage == UPDATE_DATA_PTRS)
		ret = replace_file_extents(trans, rc, root, cow);
	return ret;
}

/*
 * called before creating snapshot. it calculates metadata reservation
 * required for relocating tree blocks in the snapshot
 */
void btrfs_reloc_pre_snapshot(struct btrfs_pending_snapshot *pending,
			      u64 *bytes_to_reserve)
{
	struct btrfs_root *root = pending->root;
	struct reloc_control *rc = root->fs_info->reloc_ctl;

	if (!rc || !have_reloc_root(root))
		return;

	if (!rc->merge_reloc_tree)
		return;

	root = root->reloc_root;
	BUG_ON(btrfs_root_refs(&root->root_item) == 0);
	/*
	 * relocation is in the stage of merging trees. the space
	 * used by merging a reloc tree is twice the size of
	 * relocated tree nodes in the worst case. half for cowing
	 * the reloc tree, half for cowing the fs tree. the space
	 * used by cowing the reloc tree will be freed after the
	 * tree is dropped. if we create snapshot, cowing the fs
	 * tree may use more space than it frees. so we need
	 * reserve extra space.
	 */
	*bytes_to_reserve += rc->nodes_relocated;
}

/*
 * called after snapshot is created. migrate block reservation
 * and create reloc root for the newly created snapshot
 *
 * This is similar to btrfs_init_reloc_root(), we come out of here with two
 * references held on the reloc_root, one for root->reloc_root and one for
 * rc->reloc_roots.
 */
int btrfs_reloc_post_snapshot(struct btrfs_trans_handle *trans,
			       struct btrfs_pending_snapshot *pending)
{
	struct btrfs_root *root = pending->root;
	struct btrfs_root *reloc_root;
	struct btrfs_root *new_root;
	struct reloc_control *rc = root->fs_info->reloc_ctl;
	int ret;

	if (!rc || !have_reloc_root(root))
		return 0;

	rc = root->fs_info->reloc_ctl;
	rc->merging_rsv_size += rc->nodes_relocated;

	if (rc->merge_reloc_tree) {
		ret = btrfs_block_rsv_migrate(&pending->block_rsv,
					      rc->block_rsv,
					      rc->nodes_relocated, true);
		if (ret)
			return ret;
	}

	new_root = pending->snap;
	reloc_root = create_reloc_root(trans, root->reloc_root,
				       new_root->root_key.objectid);
	if (IS_ERR(reloc_root))
		return PTR_ERR(reloc_root);

	ret = __add_reloc_root(reloc_root);
	ASSERT(ret != -EEXIST);
	if (ret) {
		/* Pairs with create_reloc_root */
		btrfs_put_root(reloc_root);
		return ret;
	}
	new_root->reloc_root = btrfs_grab_root(reloc_root);

	if (rc->create_reloc_tree)
		ret = clone_backref_node(trans, rc, root, reloc_root);
	return ret;
}
