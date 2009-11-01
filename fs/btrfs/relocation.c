/*
 * Copyright (C) 2009 Oracle.  All rights reserved.
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
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/rbtree.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "volumes.h"
#include "locking.h"
#include "btrfs_inode.h"
#include "async-thread.h"

/*
 * backref_node, mapping_node and tree_block start with this
 */
struct tree_entry {
	struct rb_node rb_node;
	u64 bytenr;
};

/*
 * present a tree block in the backref cache
 */
struct backref_node {
	struct rb_node rb_node;
	u64 bytenr;
	/* objectid tree block owner */
	u64 owner;
	/* list of upper level blocks reference this block */
	struct list_head upper;
	/* list of child blocks in the cache */
	struct list_head lower;
	/* NULL if this node is not tree root */
	struct btrfs_root *root;
	/* extent buffer got by COW the block */
	struct extent_buffer *eb;
	/* level of tree block */
	unsigned int level:8;
	/* 1 if the block is root of old snapshot */
	unsigned int old_root:1;
	/* 1 if no child blocks in the cache */
	unsigned int lowest:1;
	/* is the extent buffer locked */
	unsigned int locked:1;
	/* has the block been processed */
	unsigned int processed:1;
	/* have backrefs of this block been checked */
	unsigned int checked:1;
};

/*
 * present a block pointer in the backref cache
 */
struct backref_edge {
	struct list_head list[2];
	struct backref_node *node[2];
	u64 blockptr;
};

#define LOWER	0
#define UPPER	1

struct backref_cache {
	/* red black tree of all backref nodes in the cache */
	struct rb_root rb_root;
	/* list of backref nodes with no child block in the cache */
	struct list_head pending[BTRFS_MAX_LEVEL];
	spinlock_t lock;
};

/*
 * map address of tree root to tree
 */
struct mapping_node {
	struct rb_node rb_node;
	u64 bytenr;
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
	struct rb_node rb_node;
	u64 bytenr;
	struct btrfs_key key;
	unsigned int level:8;
	unsigned int key_ready:1;
};

/* inode vector */
#define INODEVEC_SIZE 16

struct inodevec {
	struct list_head list;
	struct inode *inode[INODEVEC_SIZE];
	int nr;
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
	struct btrfs_block_group_cache *block_group;
	/* extent tree */
	struct btrfs_root *extent_root;
	/* inode for moving data */
	struct inode *data_inode;
	struct btrfs_workers workers;
	/* tree blocks have been processed */
	struct extent_io_tree processed_blocks;
	/* map start of tree root to corresponding reloc tree */
	struct mapping_tree reloc_root_tree;
	/* list of reloc trees */
	struct list_head reloc_roots;
	u64 search_start;
	u64 extents_found;
	u64 extents_skipped;
	int stage;
	int create_reloc_root;
	unsigned int found_file_extent:1;
	unsigned int found_old_snapshot:1;
};

/* stages of data relocation */
#define MOVE_DATA_EXTENTS	0
#define UPDATE_DATA_PTRS	1

/*
 * merge reloc tree to corresponding fs tree in worker threads
 */
struct async_merge {
	struct btrfs_work work;
	struct reloc_control *rc;
	struct btrfs_root *root;
	struct completion *done;
	atomic_t *num_pending;
};

static void mapping_tree_init(struct mapping_tree *tree)
{
	tree->rb_root.rb_node = NULL;
	spin_lock_init(&tree->lock);
}

static void backref_cache_init(struct backref_cache *cache)
{
	int i;
	cache->rb_root.rb_node = NULL;
	for (i = 0; i < BTRFS_MAX_LEVEL; i++)
		INIT_LIST_HEAD(&cache->pending[i]);
	spin_lock_init(&cache->lock);
}

static void backref_node_init(struct backref_node *node)
{
	memset(node, 0, sizeof(*node));
	INIT_LIST_HEAD(&node->upper);
	INIT_LIST_HEAD(&node->lower);
	RB_CLEAR_NODE(&node->rb_node);
}

static struct rb_node *tree_insert(struct rb_root *root, u64 bytenr,
				   struct rb_node *node)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct tree_entry *entry;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct tree_entry, rb_node);

		if (bytenr < entry->bytenr)
			p = &(*p)->rb_left;
		else if (bytenr > entry->bytenr)
			p = &(*p)->rb_right;
		else
			return parent;
	}

	rb_link_node(node, parent, p);
	rb_insert_color(node, root);
	return NULL;
}

static struct rb_node *tree_search(struct rb_root *root, u64 bytenr)
{
	struct rb_node *n = root->rb_node;
	struct tree_entry *entry;

	while (n) {
		entry = rb_entry(n, struct tree_entry, rb_node);

		if (bytenr < entry->bytenr)
			n = n->rb_left;
		else if (bytenr > entry->bytenr)
			n = n->rb_right;
		else
			return n;
	}
	return NULL;
}

/*
 * walk up backref nodes until reach node presents tree root
 */
static struct backref_node *walk_up_backref(struct backref_node *node,
					    struct backref_edge *edges[],
					    int *index)
{
	struct backref_edge *edge;
	int idx = *index;

	while (!list_empty(&node->upper)) {
		edge = list_entry(node->upper.next,
				  struct backref_edge, list[LOWER]);
		edges[idx++] = edge;
		node = edge->node[UPPER];
	}
	*index = idx;
	return node;
}

/*
 * walk down backref nodes to find start of next reference path
 */
static struct backref_node *walk_down_backref(struct backref_edge *edges[],
					      int *index)
{
	struct backref_edge *edge;
	struct backref_node *lower;
	int idx = *index;

	while (idx > 0) {
		edge = edges[idx - 1];
		lower = edge->node[LOWER];
		if (list_is_last(&edge->list[LOWER], &lower->upper)) {
			idx--;
			continue;
		}
		edge = list_entry(edge->list[LOWER].next,
				  struct backref_edge, list[LOWER]);
		edges[idx - 1] = edge;
		*index = idx;
		return edge->node[UPPER];
	}
	*index = 0;
	return NULL;
}

static void drop_node_buffer(struct backref_node *node)
{
	if (node->eb) {
		if (node->locked) {
			btrfs_tree_unlock(node->eb);
			node->locked = 0;
		}
		free_extent_buffer(node->eb);
		node->eb = NULL;
	}
}

static void drop_backref_node(struct backref_cache *tree,
			      struct backref_node *node)
{
	BUG_ON(!node->lowest);
	BUG_ON(!list_empty(&node->upper));

	drop_node_buffer(node);
	list_del(&node->lower);

	rb_erase(&node->rb_node, &tree->rb_root);
	kfree(node);
}

/*
 * remove a backref node from the backref cache
 */
static void remove_backref_node(struct backref_cache *cache,
				struct backref_node *node)
{
	struct backref_node *upper;
	struct backref_edge *edge;

	if (!node)
		return;

	BUG_ON(!node->lowest);
	while (!list_empty(&node->upper)) {
		edge = list_entry(node->upper.next, struct backref_edge,
				  list[LOWER]);
		upper = edge->node[UPPER];
		list_del(&edge->list[LOWER]);
		list_del(&edge->list[UPPER]);
		kfree(edge);
		/*
		 * add the node to pending list if no other
		 * child block cached.
		 */
		if (list_empty(&upper->lower)) {
			list_add_tail(&upper->lower,
				      &cache->pending[upper->level]);
			upper->lowest = 1;
		}
	}
	drop_backref_node(cache, node);
}

/*
 * find reloc tree by address of tree root
 */
static struct btrfs_root *find_reloc_root(struct reloc_control *rc,
					  u64 bytenr)
{
	struct rb_node *rb_node;
	struct mapping_node *node;
	struct btrfs_root *root = NULL;

	spin_lock(&rc->reloc_root_tree.lock);
	rb_node = tree_search(&rc->reloc_root_tree.rb_root, bytenr);
	if (rb_node) {
		node = rb_entry(rb_node, struct mapping_node, rb_node);
		root = (struct btrfs_root *)node->data;
	}
	spin_unlock(&rc->reloc_root_tree.lock);
	return root;
}

static int is_cowonly_root(u64 root_objectid)
{
	if (root_objectid == BTRFS_ROOT_TREE_OBJECTID ||
	    root_objectid == BTRFS_EXTENT_TREE_OBJECTID ||
	    root_objectid == BTRFS_CHUNK_TREE_OBJECTID ||
	    root_objectid == BTRFS_DEV_TREE_OBJECTID ||
	    root_objectid == BTRFS_TREE_LOG_OBJECTID ||
	    root_objectid == BTRFS_CSUM_TREE_OBJECTID)
		return 1;
	return 0;
}

static struct btrfs_root *read_fs_root(struct btrfs_fs_info *fs_info,
					u64 root_objectid)
{
	struct btrfs_key key;

	key.objectid = root_objectid;
	key.type = BTRFS_ROOT_ITEM_KEY;
	if (is_cowonly_root(root_objectid))
		key.offset = 0;
	else
		key.offset = (u64)-1;

	return btrfs_read_fs_root_no_name(fs_info, &key);
}

#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
static noinline_for_stack
struct btrfs_root *find_tree_root(struct reloc_control *rc,
				  struct extent_buffer *leaf,
				  struct btrfs_extent_ref_v0 *ref0)
{
	struct btrfs_root *root;
	u64 root_objectid = btrfs_ref_root_v0(leaf, ref0);
	u64 generation = btrfs_ref_generation_v0(leaf, ref0);

	BUG_ON(root_objectid == BTRFS_TREE_RELOC_OBJECTID);

	root = read_fs_root(rc->extent_root->fs_info, root_objectid);
	BUG_ON(IS_ERR(root));

	if (root->ref_cows &&
	    generation != btrfs_root_generation(&root->root_item))
		return NULL;

	return root;
}
#endif

static noinline_for_stack
int find_inline_backref(struct extent_buffer *leaf, int slot,
			unsigned long *ptr, unsigned long *end)
{
	struct btrfs_extent_item *ei;
	struct btrfs_tree_block_info *bi;
	u32 item_size;

	item_size = btrfs_item_size_nr(leaf, slot);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		WARN_ON(item_size != sizeof(struct btrfs_extent_item_v0));
		return 1;
	}
#endif
	ei = btrfs_item_ptr(leaf, slot, struct btrfs_extent_item);
	WARN_ON(!(btrfs_extent_flags(leaf, ei) &
		  BTRFS_EXTENT_FLAG_TREE_BLOCK));

	if (item_size <= sizeof(*ei) + sizeof(*bi)) {
		WARN_ON(item_size < sizeof(*ei) + sizeof(*bi));
		return 1;
	}

	bi = (struct btrfs_tree_block_info *)(ei + 1);
	*ptr = (unsigned long)(bi + 1);
	*end = (unsigned long)ei + item_size;
	return 0;
}

/*
 * build backref tree for a given tree block. root of the backref tree
 * corresponds the tree block, leaves of the backref tree correspond
 * roots of b-trees that reference the tree block.
 *
 * the basic idea of this function is check backrefs of a given block
 * to find upper level blocks that refernece the block, and then check
 * bakcrefs of these upper level blocks recursively. the recursion stop
 * when tree root is reached or backrefs for the block is cached.
 *
 * NOTE: if we find backrefs for a block are cached, we know backrefs
 * for all upper level blocks that directly/indirectly reference the
 * block are also cached.
 */
static struct backref_node *build_backref_tree(struct reloc_control *rc,
					       struct backref_cache *cache,
					       struct btrfs_key *node_key,
					       int level, u64 bytenr)
{
	struct btrfs_path *path1;
	struct btrfs_path *path2;
	struct extent_buffer *eb;
	struct btrfs_root *root;
	struct backref_node *cur;
	struct backref_node *upper;
	struct backref_node *lower;
	struct backref_node *node = NULL;
	struct backref_node *exist = NULL;
	struct backref_edge *edge;
	struct rb_node *rb_node;
	struct btrfs_key key;
	unsigned long end;
	unsigned long ptr;
	LIST_HEAD(list);
	int ret;
	int err = 0;

	path1 = btrfs_alloc_path();
	path2 = btrfs_alloc_path();
	if (!path1 || !path2) {
		err = -ENOMEM;
		goto out;
	}

	node = kmalloc(sizeof(*node), GFP_NOFS);
	if (!node) {
		err = -ENOMEM;
		goto out;
	}

	backref_node_init(node);
	node->bytenr = bytenr;
	node->owner = 0;
	node->level = level;
	node->lowest = 1;
	cur = node;
again:
	end = 0;
	ptr = 0;
	key.objectid = cur->bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = (u64)-1;

	path1->search_commit_root = 1;
	path1->skip_locking = 1;
	ret = btrfs_search_slot(NULL, rc->extent_root, &key, path1,
				0, 0);
	if (ret < 0) {
		err = ret;
		goto out;
	}
	BUG_ON(!ret || !path1->slots[0]);

	path1->slots[0]--;

	WARN_ON(cur->checked);
	if (!list_empty(&cur->upper)) {
		/*
		 * the backref was added previously when processsing
		 * backref of type BTRFS_TREE_BLOCK_REF_KEY
		 */
		BUG_ON(!list_is_singular(&cur->upper));
		edge = list_entry(cur->upper.next, struct backref_edge,
				  list[LOWER]);
		BUG_ON(!list_empty(&edge->list[UPPER]));
		exist = edge->node[UPPER];
		/*
		 * add the upper level block to pending list if we need
		 * check its backrefs
		 */
		if (!exist->checked)
			list_add_tail(&edge->list[UPPER], &list);
	} else {
		exist = NULL;
	}

	while (1) {
		cond_resched();
		eb = path1->nodes[0];

		if (ptr >= end) {
			if (path1->slots[0] >= btrfs_header_nritems(eb)) {
				ret = btrfs_next_leaf(rc->extent_root, path1);
				if (ret < 0) {
					err = ret;
					goto out;
				}
				if (ret > 0)
					break;
				eb = path1->nodes[0];
			}

			btrfs_item_key_to_cpu(eb, &key, path1->slots[0]);
			if (key.objectid != cur->bytenr) {
				WARN_ON(exist);
				break;
			}

			if (key.type == BTRFS_EXTENT_ITEM_KEY) {
				ret = find_inline_backref(eb, path1->slots[0],
							  &ptr, &end);
				if (ret)
					goto next;
			}
		}

		if (ptr < end) {
			/* update key for inline back ref */
			struct btrfs_extent_inline_ref *iref;
			iref = (struct btrfs_extent_inline_ref *)ptr;
			key.type = btrfs_extent_inline_ref_type(eb, iref);
			key.offset = btrfs_extent_inline_ref_offset(eb, iref);
			WARN_ON(key.type != BTRFS_TREE_BLOCK_REF_KEY &&
				key.type != BTRFS_SHARED_BLOCK_REF_KEY);
		}

		if (exist &&
		    ((key.type == BTRFS_TREE_BLOCK_REF_KEY &&
		      exist->owner == key.offset) ||
		     (key.type == BTRFS_SHARED_BLOCK_REF_KEY &&
		      exist->bytenr == key.offset))) {
			exist = NULL;
			goto next;
		}

#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
		if (key.type == BTRFS_SHARED_BLOCK_REF_KEY ||
		    key.type == BTRFS_EXTENT_REF_V0_KEY) {
			if (key.objectid == key.offset &&
			    key.type == BTRFS_EXTENT_REF_V0_KEY) {
				struct btrfs_extent_ref_v0 *ref0;
				ref0 = btrfs_item_ptr(eb, path1->slots[0],
						struct btrfs_extent_ref_v0);
				root = find_tree_root(rc, eb, ref0);
				if (root)
					cur->root = root;
				else
					cur->old_root = 1;
				break;
			}
#else
		BUG_ON(key.type == BTRFS_EXTENT_REF_V0_KEY);
		if (key.type == BTRFS_SHARED_BLOCK_REF_KEY) {
#endif
			if (key.objectid == key.offset) {
				/*
				 * only root blocks of reloc trees use
				 * backref of this type.
				 */
				root = find_reloc_root(rc, cur->bytenr);
				BUG_ON(!root);
				cur->root = root;
				break;
			}

			edge = kzalloc(sizeof(*edge), GFP_NOFS);
			if (!edge) {
				err = -ENOMEM;
				goto out;
			}
			rb_node = tree_search(&cache->rb_root, key.offset);
			if (!rb_node) {
				upper = kmalloc(sizeof(*upper), GFP_NOFS);
				if (!upper) {
					kfree(edge);
					err = -ENOMEM;
					goto out;
				}
				backref_node_init(upper);
				upper->bytenr = key.offset;
				upper->owner = 0;
				upper->level = cur->level + 1;
				/*
				 *  backrefs for the upper level block isn't
				 *  cached, add the block to pending list
				 */
				list_add_tail(&edge->list[UPPER], &list);
			} else {
				upper = rb_entry(rb_node, struct backref_node,
						 rb_node);
				INIT_LIST_HEAD(&edge->list[UPPER]);
			}
			list_add(&edge->list[LOWER], &cur->upper);
			edge->node[UPPER] = upper;
			edge->node[LOWER] = cur;

			goto next;
		} else if (key.type != BTRFS_TREE_BLOCK_REF_KEY) {
			goto next;
		}

		/* key.type == BTRFS_TREE_BLOCK_REF_KEY */
		root = read_fs_root(rc->extent_root->fs_info, key.offset);
		if (IS_ERR(root)) {
			err = PTR_ERR(root);
			goto out;
		}

		if (btrfs_root_level(&root->root_item) == cur->level) {
			/* tree root */
			BUG_ON(btrfs_root_bytenr(&root->root_item) !=
			       cur->bytenr);
			cur->root = root;
			break;
		}

		level = cur->level + 1;

		/*
		 * searching the tree to find upper level blocks
		 * reference the block.
		 */
		path2->search_commit_root = 1;
		path2->skip_locking = 1;
		path2->lowest_level = level;
		ret = btrfs_search_slot(NULL, root, node_key, path2, 0, 0);
		path2->lowest_level = 0;
		if (ret < 0) {
			err = ret;
			goto out;
		}
		if (ret > 0 && path2->slots[level] > 0)
			path2->slots[level]--;

		eb = path2->nodes[level];
		WARN_ON(btrfs_node_blockptr(eb, path2->slots[level]) !=
			cur->bytenr);

		lower = cur;
		for (; level < BTRFS_MAX_LEVEL; level++) {
			if (!path2->nodes[level]) {
				BUG_ON(btrfs_root_bytenr(&root->root_item) !=
				       lower->bytenr);
				lower->root = root;
				break;
			}

			edge = kzalloc(sizeof(*edge), GFP_NOFS);
			if (!edge) {
				err = -ENOMEM;
				goto out;
			}

			eb = path2->nodes[level];
			rb_node = tree_search(&cache->rb_root, eb->start);
			if (!rb_node) {
				upper = kmalloc(sizeof(*upper), GFP_NOFS);
				if (!upper) {
					kfree(edge);
					err = -ENOMEM;
					goto out;
				}
				backref_node_init(upper);
				upper->bytenr = eb->start;
				upper->owner = btrfs_header_owner(eb);
				upper->level = lower->level + 1;

				/*
				 * if we know the block isn't shared
				 * we can void checking its backrefs.
				 */
				if (btrfs_block_can_be_shared(root, eb))
					upper->checked = 0;
				else
					upper->checked = 1;

				/*
				 * add the block to pending list if we
				 * need check its backrefs. only block
				 * at 'cur->level + 1' is added to the
				 * tail of pending list. this guarantees
				 * we check backrefs from lower level
				 * blocks to upper level blocks.
				 */
				if (!upper->checked &&
				    level == cur->level + 1) {
					list_add_tail(&edge->list[UPPER],
						      &list);
				} else
					INIT_LIST_HEAD(&edge->list[UPPER]);
			} else {
				upper = rb_entry(rb_node, struct backref_node,
						 rb_node);
				BUG_ON(!upper->checked);
				INIT_LIST_HEAD(&edge->list[UPPER]);
			}
			list_add_tail(&edge->list[LOWER], &lower->upper);
			edge->node[UPPER] = upper;
			edge->node[LOWER] = lower;

			if (rb_node)
				break;
			lower = upper;
			upper = NULL;
		}
		btrfs_release_path(root, path2);
next:
		if (ptr < end) {
			ptr += btrfs_extent_inline_ref_size(key.type);
			if (ptr >= end) {
				WARN_ON(ptr > end);
				ptr = 0;
				end = 0;
			}
		}
		if (ptr >= end)
			path1->slots[0]++;
	}
	btrfs_release_path(rc->extent_root, path1);

	cur->checked = 1;
	WARN_ON(exist);

	/* the pending list isn't empty, take the first block to process */
	if (!list_empty(&list)) {
		edge = list_entry(list.next, struct backref_edge, list[UPPER]);
		list_del_init(&edge->list[UPPER]);
		cur = edge->node[UPPER];
		goto again;
	}

	/*
	 * everything goes well, connect backref nodes and insert backref nodes
	 * into the cache.
	 */
	BUG_ON(!node->checked);
	rb_node = tree_insert(&cache->rb_root, node->bytenr, &node->rb_node);
	BUG_ON(rb_node);

	list_for_each_entry(edge, &node->upper, list[LOWER])
		list_add_tail(&edge->list[UPPER], &list);

	while (!list_empty(&list)) {
		edge = list_entry(list.next, struct backref_edge, list[UPPER]);
		list_del_init(&edge->list[UPPER]);
		upper = edge->node[UPPER];

		if (!RB_EMPTY_NODE(&upper->rb_node)) {
			if (upper->lowest) {
				list_del_init(&upper->lower);
				upper->lowest = 0;
			}

			list_add_tail(&edge->list[UPPER], &upper->lower);
			continue;
		}

		BUG_ON(!upper->checked);
		rb_node = tree_insert(&cache->rb_root, upper->bytenr,
				      &upper->rb_node);
		BUG_ON(rb_node);

		list_add_tail(&edge->list[UPPER], &upper->lower);

		list_for_each_entry(edge, &upper->upper, list[LOWER])
			list_add_tail(&edge->list[UPPER], &list);
	}
out:
	btrfs_free_path(path1);
	btrfs_free_path(path2);
	if (err) {
		INIT_LIST_HEAD(&list);
		upper = node;
		while (upper) {
			if (RB_EMPTY_NODE(&upper->rb_node)) {
				list_splice_tail(&upper->upper, &list);
				kfree(upper);
			}

			if (list_empty(&list))
				break;

			edge = list_entry(list.next, struct backref_edge,
					  list[LOWER]);
			upper = edge->node[UPPER];
			kfree(edge);
		}
		return ERR_PTR(err);
	}
	return node;
}

/*
 * helper to add 'address of tree root -> reloc tree' mapping
 */
static int __add_reloc_root(struct btrfs_root *root)
{
	struct rb_node *rb_node;
	struct mapping_node *node;
	struct reloc_control *rc = root->fs_info->reloc_ctl;

	node = kmalloc(sizeof(*node), GFP_NOFS);
	BUG_ON(!node);

	node->bytenr = root->node->start;
	node->data = root;

	spin_lock(&rc->reloc_root_tree.lock);
	rb_node = tree_insert(&rc->reloc_root_tree.rb_root,
			      node->bytenr, &node->rb_node);
	spin_unlock(&rc->reloc_root_tree.lock);
	BUG_ON(rb_node);

	list_add_tail(&root->root_list, &rc->reloc_roots);
	return 0;
}

/*
 * helper to update/delete the 'address of tree root -> reloc tree'
 * mapping
 */
static int __update_reloc_root(struct btrfs_root *root, int del)
{
	struct rb_node *rb_node;
	struct mapping_node *node = NULL;
	struct reloc_control *rc = root->fs_info->reloc_ctl;

	spin_lock(&rc->reloc_root_tree.lock);
	rb_node = tree_search(&rc->reloc_root_tree.rb_root,
			      root->commit_root->start);
	if (rb_node) {
		node = rb_entry(rb_node, struct mapping_node, rb_node);
		rb_erase(&node->rb_node, &rc->reloc_root_tree.rb_root);
	}
	spin_unlock(&rc->reloc_root_tree.lock);

	BUG_ON((struct btrfs_root *)node->data != root);

	if (!del) {
		spin_lock(&rc->reloc_root_tree.lock);
		node->bytenr = root->node->start;
		rb_node = tree_insert(&rc->reloc_root_tree.rb_root,
				      node->bytenr, &node->rb_node);
		spin_unlock(&rc->reloc_root_tree.lock);
		BUG_ON(rb_node);
	} else {
		list_del_init(&root->root_list);
		kfree(node);
	}
	return 0;
}

/*
 * create reloc tree for a given fs tree. reloc tree is just a
 * snapshot of the fs tree with special root objectid.
 */
int btrfs_init_reloc_root(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root)
{
	struct btrfs_root *reloc_root;
	struct extent_buffer *eb;
	struct btrfs_root_item *root_item;
	struct btrfs_key root_key;
	int ret;

	if (root->reloc_root) {
		reloc_root = root->reloc_root;
		reloc_root->last_trans = trans->transid;
		return 0;
	}

	if (!root->fs_info->reloc_ctl ||
	    !root->fs_info->reloc_ctl->create_reloc_root ||
	    root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID)
		return 0;

	root_item = kmalloc(sizeof(*root_item), GFP_NOFS);
	BUG_ON(!root_item);

	root_key.objectid = BTRFS_TREE_RELOC_OBJECTID;
	root_key.type = BTRFS_ROOT_ITEM_KEY;
	root_key.offset = root->root_key.objectid;

	ret = btrfs_copy_root(trans, root, root->commit_root, &eb,
			      BTRFS_TREE_RELOC_OBJECTID);
	BUG_ON(ret);

	btrfs_set_root_last_snapshot(&root->root_item, trans->transid - 1);
	memcpy(root_item, &root->root_item, sizeof(*root_item));
	btrfs_set_root_refs(root_item, 1);
	btrfs_set_root_bytenr(root_item, eb->start);
	btrfs_set_root_level(root_item, btrfs_header_level(eb));
	btrfs_set_root_generation(root_item, trans->transid);
	memset(&root_item->drop_progress, 0, sizeof(struct btrfs_disk_key));
	root_item->drop_level = 0;

	btrfs_tree_unlock(eb);
	free_extent_buffer(eb);

	ret = btrfs_insert_root(trans, root->fs_info->tree_root,
				&root_key, root_item);
	BUG_ON(ret);
	kfree(root_item);

	reloc_root = btrfs_read_fs_root_no_radix(root->fs_info->tree_root,
						 &root_key);
	BUG_ON(IS_ERR(reloc_root));
	reloc_root->last_trans = trans->transid;

	__add_reloc_root(reloc_root);
	root->reloc_root = reloc_root;
	return 0;
}

/*
 * update root item of reloc tree
 */
int btrfs_update_reloc_root(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root)
{
	struct btrfs_root *reloc_root;
	struct btrfs_root_item *root_item;
	int del = 0;
	int ret;

	if (!root->reloc_root)
		return 0;

	reloc_root = root->reloc_root;
	root_item = &reloc_root->root_item;

	if (btrfs_root_refs(root_item) == 0) {
		root->reloc_root = NULL;
		del = 1;
	}

	__update_reloc_root(reloc_root, del);

	if (reloc_root->commit_root != reloc_root->node) {
		btrfs_set_root_node(root_item, reloc_root->node);
		free_extent_buffer(reloc_root->commit_root);
		reloc_root->commit_root = btrfs_root_node(reloc_root);
	}

	ret = btrfs_update_root(trans, root->fs_info->tree_root,
				&reloc_root->root_key, root_item);
	BUG_ON(ret);
	return 0;
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

		if (objectid < entry->vfs_inode.i_ino)
			node = node->rb_left;
		else if (objectid > entry->vfs_inode.i_ino)
			node = node->rb_right;
		else
			break;
	}
	if (!node) {
		while (prev) {
			entry = rb_entry(prev, struct btrfs_inode, rb_node);
			if (objectid <= entry->vfs_inode.i_ino) {
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

		objectid = entry->vfs_inode.i_ino + 1;
		if (cond_resched_lock(&root->inode_lock))
			goto again;

		node = rb_next(node);
	}
	spin_unlock(&root->inode_lock);
	return NULL;
}

static int in_block_group(u64 bytenr,
			  struct btrfs_block_group_cache *block_group)
{
	if (bytenr >= block_group->key.objectid &&
	    bytenr < block_group->key.objectid + block_group->key.offset)
		return 1;
	return 0;
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
	ret = btrfs_lookup_file_extent(NULL, root, path, reloc_inode->i_ino,
				       bytenr, 0);
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
		ret = 1;
		goto out;
	}

	if (new_bytenr)
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
static int replace_file_extents(struct btrfs_trans_handle *trans,
				struct reloc_control *rc,
				struct btrfs_root *root,
				struct extent_buffer *leaf,
				struct list_head *inode_list)
{
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	struct inode *inode = NULL;
	struct inodevec *ivec = NULL;
	u64 parent;
	u64 bytenr;
	u64 new_bytenr;
	u64 num_bytes;
	u64 end;
	u32 nritems;
	u32 i;
	int ret;
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
		if (!in_block_group(bytenr, rc->block_group))
			continue;

		/*
		 * if we are modifying block in fs tree, wait for readpage
		 * to complete and drop the extent cache
		 */
		if (root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID) {
			if (!ivec || ivec->nr == INODEVEC_SIZE) {
				ivec = kmalloc(sizeof(*ivec), GFP_NOFS);
				BUG_ON(!ivec);
				ivec->nr = 0;
				list_add_tail(&ivec->list, inode_list);
			}
			if (first) {
				inode = find_next_inode(root, key.objectid);
				if (inode)
					ivec->inode[ivec->nr++] = inode;
				first = 0;
			} else if (inode && inode->i_ino < key.objectid) {
				inode = find_next_inode(root, key.objectid);
				if (inode)
					ivec->inode[ivec->nr++] = inode;
			}
			if (inode && inode->i_ino == key.objectid) {
				end = key.offset +
				      btrfs_file_extent_num_bytes(leaf, fi);
				WARN_ON(!IS_ALIGNED(key.offset,
						    root->sectorsize));
				WARN_ON(!IS_ALIGNED(end, root->sectorsize));
				end--;
				ret = try_lock_extent(&BTRFS_I(inode)->io_tree,
						      key.offset, end,
						      GFP_NOFS);
				if (!ret)
					continue;

				btrfs_drop_extent_cache(inode, key.offset, end,
							1);
				unlock_extent(&BTRFS_I(inode)->io_tree,
					      key.offset, end, GFP_NOFS);
			}
		}

		ret = get_new_location(rc->data_inode, &new_bytenr,
				       bytenr, num_bytes);
		if (ret > 0)
			continue;
		BUG_ON(ret < 0);

		btrfs_set_file_extent_disk_bytenr(leaf, fi, new_bytenr);
		dirty = 1;

		key.offset -= btrfs_file_extent_offset(leaf, fi);
		ret = btrfs_inc_extent_ref(trans, root, new_bytenr,
					   num_bytes, parent,
					   btrfs_header_owner(leaf),
					   key.objectid, key.offset);
		BUG_ON(ret);

		ret = btrfs_free_extent(trans, root, bytenr, num_bytes,
					parent, btrfs_header_owner(leaf),
					key.objectid, key.offset);
		BUG_ON(ret);
	}
	if (dirty)
		btrfs_mark_buffer_dirty(leaf);
	return 0;
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
static int replace_path(struct btrfs_trans_handle *trans,
			struct btrfs_root *dest, struct btrfs_root *src,
			struct btrfs_path *path, struct btrfs_key *next_key,
			struct extent_buffer **leaf,
			int lowest_level, int max_level)
{
	struct extent_buffer *eb;
	struct extent_buffer *parent;
	struct btrfs_key key;
	u64 old_bytenr;
	u64 new_bytenr;
	u64 old_ptr_gen;
	u64 new_ptr_gen;
	u64 last_snapshot;
	u32 blocksize;
	int level;
	int ret;
	int slot;

	BUG_ON(src->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID);
	BUG_ON(dest->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID);
	BUG_ON(lowest_level > 1 && leaf);

	last_snapshot = btrfs_root_last_snapshot(&src->root_item);

	slot = path->slots[lowest_level];
	btrfs_node_key_to_cpu(path->nodes[lowest_level], &key, slot);

	eb = btrfs_lock_root_node(dest);
	btrfs_set_lock_blocking(eb);
	level = btrfs_header_level(eb);

	if (level < lowest_level) {
		btrfs_tree_unlock(eb);
		free_extent_buffer(eb);
		return 0;
	}

	ret = btrfs_cow_block(trans, dest, eb, NULL, 0, &eb);
	BUG_ON(ret);
	btrfs_set_lock_blocking(eb);

	if (next_key) {
		next_key->objectid = (u64)-1;
		next_key->type = (u8)-1;
		next_key->offset = (u64)-1;
	}

	parent = eb;
	while (1) {
		level = btrfs_header_level(parent);
		BUG_ON(level < lowest_level);

		ret = btrfs_bin_search(parent, &key, level, &slot);
		if (ret && slot > 0)
			slot--;

		if (next_key && slot + 1 < btrfs_header_nritems(parent))
			btrfs_node_key_to_cpu(parent, next_key, slot + 1);

		old_bytenr = btrfs_node_blockptr(parent, slot);
		blocksize = btrfs_level_size(dest, level - 1);
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

		if (new_bytenr > 0 && new_bytenr == old_bytenr) {
			WARN_ON(1);
			ret = level;
			break;
		}

		if (new_bytenr == 0 || old_ptr_gen > last_snapshot ||
		    memcmp_node_keys(parent, slot, path, level)) {
			if (level <= lowest_level && !leaf) {
				ret = 0;
				break;
			}

			eb = read_tree_block(dest, old_bytenr, blocksize,
					     old_ptr_gen);
			btrfs_tree_lock(eb);
			ret = btrfs_cow_block(trans, dest, eb, parent,
					      slot, &eb);
			BUG_ON(ret);
			btrfs_set_lock_blocking(eb);

			if (level <= lowest_level) {
				*leaf = eb;
				ret = 0;
				break;
			}

			btrfs_tree_unlock(parent);
			free_extent_buffer(parent);

			parent = eb;
			continue;
		}

		btrfs_node_key_to_cpu(path->nodes[level], &key,
				      path->slots[level]);
		btrfs_release_path(src, path);

		path->lowest_level = level;
		ret = btrfs_search_slot(trans, src, &key, path, 0, 1);
		path->lowest_level = 0;
		BUG_ON(ret);

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

		ret = btrfs_inc_extent_ref(trans, src, old_bytenr, blocksize,
					path->nodes[level]->start,
					src->root_key.objectid, level - 1, 0);
		BUG_ON(ret);
		ret = btrfs_inc_extent_ref(trans, dest, new_bytenr, blocksize,
					0, dest->root_key.objectid, level - 1,
					0);
		BUG_ON(ret);

		ret = btrfs_free_extent(trans, src, new_bytenr, blocksize,
					path->nodes[level]->start,
					src->root_key.objectid, level - 1, 0);
		BUG_ON(ret);

		ret = btrfs_free_extent(trans, dest, old_bytenr, blocksize,
					0, dest->root_key.objectid, level - 1,
					0);
		BUG_ON(ret);

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
	u64 bytenr;
	u64 ptr_gen = 0;
	u64 last_snapshot;
	u32 blocksize;
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

		bytenr = btrfs_node_blockptr(eb, path->slots[i]);
		blocksize = btrfs_level_size(root, i - 1);
		eb = read_tree_block(root, bytenr, blocksize, ptr_gen);
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
	struct inode *inode = NULL;
	u64 objectid;
	u64 start, end;

	objectid = min_key->objectid;
	while (1) {
		cond_resched();
		iput(inode);

		if (objectid > max_key->objectid)
			break;

		inode = find_next_inode(root, objectid);
		if (!inode)
			break;

		if (inode->i_ino > max_key->objectid) {
			iput(inode);
			break;
		}

		objectid = inode->i_ino + 1;
		if (!S_ISREG(inode->i_mode))
			continue;

		if (unlikely(min_key->objectid == inode->i_ino)) {
			if (min_key->type > BTRFS_EXTENT_DATA_KEY)
				continue;
			if (min_key->type < BTRFS_EXTENT_DATA_KEY)
				start = 0;
			else {
				start = min_key->offset;
				WARN_ON(!IS_ALIGNED(start, root->sectorsize));
			}
		} else {
			start = 0;
		}

		if (unlikely(max_key->objectid == inode->i_ino)) {
			if (max_key->type < BTRFS_EXTENT_DATA_KEY)
				continue;
			if (max_key->type > BTRFS_EXTENT_DATA_KEY) {
				end = (u64)-1;
			} else {
				if (max_key->offset == 0)
					continue;
				end = max_key->offset;
				WARN_ON(!IS_ALIGNED(end, root->sectorsize));
				end--;
			}
		} else {
			end = (u64)-1;
		}

		/* the lock_extent waits for readpage to complete */
		lock_extent(&BTRFS_I(inode)->io_tree, start, end, GFP_NOFS);
		btrfs_drop_extent_cache(inode, start, end, 1);
		unlock_extent(&BTRFS_I(inode)->io_tree, start, end, GFP_NOFS);
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
 * merge the relocated tree blocks in reloc tree with corresponding
 * fs tree.
 */
static noinline_for_stack int merge_reloc_root(struct reloc_control *rc,
					       struct btrfs_root *root)
{
	LIST_HEAD(inode_list);
	struct btrfs_key key;
	struct btrfs_key next_key;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *reloc_root;
	struct btrfs_root_item *root_item;
	struct btrfs_path *path;
	struct extent_buffer *leaf = NULL;
	unsigned long nr;
	int level;
	int max_level;
	int replaced = 0;
	int ret;
	int err = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	reloc_root = root->reloc_root;
	root_item = &reloc_root->root_item;

	if (btrfs_disk_key_objectid(&root_item->drop_progress) == 0) {
		level = btrfs_root_level(root_item);
		extent_buffer_get(reloc_root->node);
		path->nodes[level] = reloc_root->node;
		path->slots[level] = 0;
	} else {
		btrfs_disk_key_to_cpu(&key, &root_item->drop_progress);

		level = root_item->drop_level;
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

	if (level == 0 && rc->stage == UPDATE_DATA_PTRS) {
		trans = btrfs_start_transaction(root, 1);

		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &key, 0);
		btrfs_release_path(reloc_root, path);

		ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
		if (ret < 0) {
			err = ret;
			goto out;
		}

		leaf = path->nodes[0];
		btrfs_unlock_up_safe(path, 1);
		ret = replace_file_extents(trans, rc, root, leaf,
					   &inode_list);
		if (ret < 0)
			err = ret;
		goto out;
	}

	memset(&next_key, 0, sizeof(next_key));

	while (1) {
		leaf = NULL;
		replaced = 0;
		trans = btrfs_start_transaction(root, 1);
		max_level = level;

		ret = walk_down_reloc_tree(reloc_root, path, &level);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		if (ret > 0)
			break;

		if (!find_next_key(path, level, &key) &&
		    btrfs_comp_cpu_keys(&next_key, &key) >= 0) {
			ret = 0;
		} else if (level == 1 && rc->stage == UPDATE_DATA_PTRS) {
			ret = replace_path(trans, root, reloc_root,
					   path, &next_key, &leaf,
					   level, max_level);
		} else {
			ret = replace_path(trans, root, reloc_root,
					   path, &next_key, NULL,
					   level, max_level);
		}
		if (ret < 0) {
			err = ret;
			goto out;
		}

		if (ret > 0) {
			level = ret;
			btrfs_node_key_to_cpu(path->nodes[level], &key,
					      path->slots[level]);
			replaced = 1;
		} else if (leaf) {
			/*
			 * no block got replaced, try replacing file extents
			 */
			btrfs_item_key_to_cpu(leaf, &key, 0);
			ret = replace_file_extents(trans, rc, root, leaf,
						   &inode_list);
			btrfs_tree_unlock(leaf);
			free_extent_buffer(leaf);
			BUG_ON(ret < 0);
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
		root_item->drop_level = level;

		nr = trans->blocks_used;
		btrfs_end_transaction(trans, root);

		btrfs_btree_balance_dirty(root, nr);

		if (replaced && rc->stage == UPDATE_DATA_PTRS)
			invalidate_extent_cache(root, &key, &next_key);
	}

	/*
	 * handle the case only one block in the fs tree need to be
	 * relocated and the block is tree root.
	 */
	leaf = btrfs_lock_root_node(root);
	ret = btrfs_cow_block(trans, root, leaf, NULL, 0, &leaf);
	btrfs_tree_unlock(leaf);
	free_extent_buffer(leaf);
	if (ret < 0)
		err = ret;
out:
	btrfs_free_path(path);

	if (err == 0) {
		memset(&root_item->drop_progress, 0,
		       sizeof(root_item->drop_progress));
		root_item->drop_level = 0;
		btrfs_set_root_refs(root_item, 0);
	}

	nr = trans->blocks_used;
	btrfs_end_transaction(trans, root);

	btrfs_btree_balance_dirty(root, nr);

	/*
	 * put inodes while we aren't holding the tree locks
	 */
	while (!list_empty(&inode_list)) {
		struct inodevec *ivec;
		ivec = list_entry(inode_list.next, struct inodevec, list);
		list_del(&ivec->list);
		while (ivec->nr > 0) {
			ivec->nr--;
			iput(ivec->inode[ivec->nr]);
		}
		kfree(ivec);
	}

	if (replaced && rc->stage == UPDATE_DATA_PTRS)
		invalidate_extent_cache(root, &key, &next_key);

	return err;
}

/*
 * callback for the work threads.
 * this function merges reloc tree with corresponding fs tree,
 * and then drops the reloc tree.
 */
static void merge_func(struct btrfs_work *work)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root;
	struct btrfs_root *reloc_root;
	struct async_merge *async;

	async = container_of(work, struct async_merge, work);
	reloc_root = async->root;

	if (btrfs_root_refs(&reloc_root->root_item) > 0) {
		root = read_fs_root(reloc_root->fs_info,
				    reloc_root->root_key.offset);
		BUG_ON(IS_ERR(root));
		BUG_ON(root->reloc_root != reloc_root);

		merge_reloc_root(async->rc, root);

		trans = btrfs_start_transaction(root, 1);
		btrfs_update_reloc_root(trans, root);
		btrfs_end_transaction(trans, root);
	}

	btrfs_drop_snapshot(reloc_root, 0);

	if (atomic_dec_and_test(async->num_pending))
		complete(async->done);

	kfree(async);
}

static int merge_reloc_roots(struct reloc_control *rc)
{
	struct async_merge *async;
	struct btrfs_root *root;
	struct completion done;
	atomic_t num_pending;

	init_completion(&done);
	atomic_set(&num_pending, 1);

	while (!list_empty(&rc->reloc_roots)) {
		root = list_entry(rc->reloc_roots.next,
				  struct btrfs_root, root_list);
		list_del_init(&root->root_list);

		async = kmalloc(sizeof(*async), GFP_NOFS);
		BUG_ON(!async);
		async->work.func = merge_func;
		async->work.flags = 0;
		async->rc = rc;
		async->root = root;
		async->done = &done;
		async->num_pending = &num_pending;
		atomic_inc(&num_pending);
		btrfs_queue_worker(&rc->workers, &async->work);
	}

	if (!atomic_dec_and_test(&num_pending))
		wait_for_completion(&done);

	BUG_ON(!RB_EMPTY_ROOT(&rc->reloc_root_tree.rb_root));
	return 0;
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
	struct btrfs_root *root;

	if (reloc_root->last_trans == trans->transid)
		return 0;

	root = read_fs_root(reloc_root->fs_info, reloc_root->root_key.offset);
	BUG_ON(IS_ERR(root));
	BUG_ON(root->reloc_root != reloc_root);

	return btrfs_record_root_in_trans(trans, root);
}

/*
 * select one tree from trees that references the block.
 * for blocks in refernce counted trees, we preper reloc tree.
 * if no reloc tree found and reloc_only is true, NULL is returned.
 */
static struct btrfs_root *__select_one_root(struct btrfs_trans_handle *trans,
					    struct backref_node *node,
					    struct backref_edge *edges[],
					    int *nr, int reloc_only)
{
	struct backref_node *next;
	struct btrfs_root *root;
	int index;
	int loop = 0;
again:
	index = 0;
	next = node;
	while (1) {
		cond_resched();
		next = walk_up_backref(next, edges, &index);
		root = next->root;
		if (!root) {
			BUG_ON(!node->old_root);
			goto skip;
		}

		/* no other choice for non-refernce counted tree */
		if (!root->ref_cows) {
			BUG_ON(reloc_only);
			break;
		}

		if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID) {
			record_reloc_root_in_trans(trans, root);
			break;
		}

		if (loop) {
			btrfs_record_root_in_trans(trans, root);
			break;
		}

		if (reloc_only || next != node) {
			if (!root->reloc_root)
				btrfs_record_root_in_trans(trans, root);
			root = root->reloc_root;
			/*
			 * if the reloc tree was created in current
			 * transation, there is no node in backref tree
			 * corresponds to the root of the reloc tree.
			 */
			if (btrfs_root_last_snapshot(&root->root_item) ==
			    trans->transid - 1)
				break;
		}
skip:
		root = NULL;
		next = walk_down_backref(edges, &index);
		if (!next || next->level <= node->level)
			break;
	}

	if (!root && !loop && !reloc_only) {
		loop = 1;
		goto again;
	}

	if (root)
		*nr = index;
	else
		*nr = 0;

	return root;
}

static noinline_for_stack
struct btrfs_root *select_one_root(struct btrfs_trans_handle *trans,
				   struct backref_node *node)
{
	struct backref_edge *edges[BTRFS_MAX_LEVEL - 1];
	int nr;
	return __select_one_root(trans, node, edges, &nr, 0);
}

static noinline_for_stack
struct btrfs_root *select_reloc_root(struct btrfs_trans_handle *trans,
				     struct backref_node *node,
				     struct backref_edge *edges[], int *nr)
{
	return __select_one_root(trans, node, edges, nr, 1);
}

static void grab_path_buffers(struct btrfs_path *path,
			      struct backref_node *node,
			      struct backref_edge *edges[], int nr)
{
	int i = 0;
	while (1) {
		drop_node_buffer(node);
		node->eb = path->nodes[node->level];
		BUG_ON(!node->eb);
		if (path->locks[node->level])
			node->locked = 1;
		path->nodes[node->level] = NULL;
		path->locks[node->level] = 0;

		if (i >= nr)
			break;

		edges[i]->blockptr = node->eb->start;
		node = edges[i]->node[UPPER];
		i++;
	}
}

/*
 * relocate a block tree, and then update pointers in upper level
 * blocks that reference the block to point to the new location.
 *
 * if called by link_to_upper, the block has already been relocated.
 * in that case this function just updates pointers.
 */
static int do_relocation(struct btrfs_trans_handle *trans,
			 struct backref_node *node,
			 struct btrfs_key *key,
			 struct btrfs_path *path, int lowest)
{
	struct backref_node *upper;
	struct backref_edge *edge;
	struct backref_edge *edges[BTRFS_MAX_LEVEL - 1];
	struct btrfs_root *root;
	struct extent_buffer *eb;
	u32 blocksize;
	u64 bytenr;
	u64 generation;
	int nr;
	int slot;
	int ret;
	int err = 0;

	BUG_ON(lowest && node->eb);

	path->lowest_level = node->level + 1;
	list_for_each_entry(edge, &node->upper, list[LOWER]) {
		cond_resched();
		if (node->eb && node->eb->start == edge->blockptr)
			continue;

		upper = edge->node[UPPER];
		root = select_reloc_root(trans, upper, edges, &nr);
		if (!root)
			continue;

		if (upper->eb && !upper->locked)
			drop_node_buffer(upper);

		if (!upper->eb) {
			ret = btrfs_search_slot(trans, root, key, path, 0, 1);
			if (ret < 0) {
				err = ret;
				break;
			}
			BUG_ON(ret > 0);

			slot = path->slots[upper->level];

			btrfs_unlock_up_safe(path, upper->level + 1);
			grab_path_buffers(path, upper, edges, nr);

			btrfs_release_path(NULL, path);
		} else {
			ret = btrfs_bin_search(upper->eb, key, upper->level,
					       &slot);
			BUG_ON(ret);
		}

		bytenr = btrfs_node_blockptr(upper->eb, slot);
		if (!lowest) {
			if (node->eb->start == bytenr) {
				btrfs_tree_unlock(upper->eb);
				upper->locked = 0;
				continue;
			}
		} else {
			BUG_ON(node->bytenr != bytenr);
		}

		blocksize = btrfs_level_size(root, node->level);
		generation = btrfs_node_ptr_generation(upper->eb, slot);
		eb = read_tree_block(root, bytenr, blocksize, generation);
		btrfs_tree_lock(eb);
		btrfs_set_lock_blocking(eb);

		if (!node->eb) {
			ret = btrfs_cow_block(trans, root, eb, upper->eb,
					      slot, &eb);
			if (ret < 0) {
				err = ret;
				break;
			}
			btrfs_set_lock_blocking(eb);
			node->eb = eb;
			node->locked = 1;
		} else {
			btrfs_set_node_blockptr(upper->eb, slot,
						node->eb->start);
			btrfs_set_node_ptr_generation(upper->eb, slot,
						      trans->transid);
			btrfs_mark_buffer_dirty(upper->eb);

			ret = btrfs_inc_extent_ref(trans, root,
						node->eb->start, blocksize,
						upper->eb->start,
						btrfs_header_owner(upper->eb),
						node->level, 0);
			BUG_ON(ret);

			ret = btrfs_drop_subtree(trans, root, eb, upper->eb);
			BUG_ON(ret);
		}
		if (!lowest) {
			btrfs_tree_unlock(upper->eb);
			upper->locked = 0;
		}
	}
	path->lowest_level = 0;
	return err;
}

static int link_to_upper(struct btrfs_trans_handle *trans,
			 struct backref_node *node,
			 struct btrfs_path *path)
{
	struct btrfs_key key;
	if (!node->eb || list_empty(&node->upper))
		return 0;

	btrfs_node_key_to_cpu(node->eb, &key, 0);
	return do_relocation(trans, node, &key, path, 0);
}

static int finish_pending_nodes(struct btrfs_trans_handle *trans,
				struct backref_cache *cache,
				struct btrfs_path *path)
{
	struct backref_node *node;
	int level;
	int ret;
	int err = 0;

	for (level = 0; level < BTRFS_MAX_LEVEL; level++) {
		while (!list_empty(&cache->pending[level])) {
			node = list_entry(cache->pending[level].next,
					  struct backref_node, lower);
			BUG_ON(node->level != level);

			ret = link_to_upper(trans, node, path);
			if (ret < 0)
				err = ret;
			/*
			 * this remove the node from the pending list and
			 * may add some other nodes to the level + 1
			 * pending list
			 */
			remove_backref_node(cache, node);
		}
	}
	BUG_ON(!RB_EMPTY_ROOT(&cache->rb_root));
	return err;
}

static void mark_block_processed(struct reloc_control *rc,
				 struct backref_node *node)
{
	u32 blocksize;
	if (node->level == 0 ||
	    in_block_group(node->bytenr, rc->block_group)) {
		blocksize = btrfs_level_size(rc->extent_root, node->level);
		set_extent_bits(&rc->processed_blocks, node->bytenr,
				node->bytenr + blocksize - 1, EXTENT_DIRTY,
				GFP_NOFS);
	}
	node->processed = 1;
}

/*
 * mark a block and all blocks directly/indirectly reference the block
 * as processed.
 */
static void update_processed_blocks(struct reloc_control *rc,
				    struct backref_node *node)
{
	struct backref_node *next = node;
	struct backref_edge *edge;
	struct backref_edge *edges[BTRFS_MAX_LEVEL - 1];
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
					  struct backref_edge, list[LOWER]);
			edges[index++] = edge;
			next = edge->node[UPPER];
		}
		next = walk_down_backref(edges, &index);
	}
}

static int tree_block_processed(u64 bytenr, u32 blocksize,
				struct reloc_control *rc)
{
	if (test_range_bit(&rc->processed_blocks, bytenr,
			   bytenr + blocksize - 1, EXTENT_DIRTY, 1, NULL))
		return 1;
	return 0;
}

/*
 * check if there are any file extent pointers in the leaf point to
 * data require processing
 */
static int check_file_extents(struct reloc_control *rc,
			      u64 bytenr, u32 blocksize, u64 ptr_gen)
{
	struct btrfs_key found_key;
	struct btrfs_file_extent_item *fi;
	struct extent_buffer *leaf;
	u32 nritems;
	int i;
	int ret = 0;

	leaf = read_tree_block(rc->extent_root, bytenr, blocksize, ptr_gen);

	nritems = btrfs_header_nritems(leaf);
	for (i = 0; i < nritems; i++) {
		cond_resched();
		btrfs_item_key_to_cpu(leaf, &found_key, i);
		if (found_key.type != BTRFS_EXTENT_DATA_KEY)
			continue;
		fi = btrfs_item_ptr(leaf, i, struct btrfs_file_extent_item);
		if (btrfs_file_extent_type(leaf, fi) ==
		    BTRFS_FILE_EXTENT_INLINE)
			continue;
		bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
		if (bytenr == 0)
			continue;
		if (in_block_group(bytenr, rc->block_group)) {
			ret = 1;
			break;
		}
	}
	free_extent_buffer(leaf);
	return ret;
}

/*
 * scan child blocks of a given block to find blocks require processing
 */
static int add_child_blocks(struct btrfs_trans_handle *trans,
			    struct reloc_control *rc,
			    struct backref_node *node,
			    struct rb_root *blocks)
{
	struct tree_block *block;
	struct rb_node *rb_node;
	u64 bytenr;
	u64 ptr_gen;
	u32 blocksize;
	u32 nritems;
	int i;
	int err = 0;

	nritems = btrfs_header_nritems(node->eb);
	blocksize = btrfs_level_size(rc->extent_root, node->level - 1);
	for (i = 0; i < nritems; i++) {
		cond_resched();
		bytenr = btrfs_node_blockptr(node->eb, i);
		ptr_gen = btrfs_node_ptr_generation(node->eb, i);
		if (ptr_gen == trans->transid)
			continue;
		if (!in_block_group(bytenr, rc->block_group) &&
		    (node->level > 1 || rc->stage == MOVE_DATA_EXTENTS))
			continue;
		if (tree_block_processed(bytenr, blocksize, rc))
			continue;

		readahead_tree_block(rc->extent_root,
				     bytenr, blocksize, ptr_gen);
	}

	for (i = 0; i < nritems; i++) {
		cond_resched();
		bytenr = btrfs_node_blockptr(node->eb, i);
		ptr_gen = btrfs_node_ptr_generation(node->eb, i);
		if (ptr_gen == trans->transid)
			continue;
		if (!in_block_group(bytenr, rc->block_group) &&
		    (node->level > 1 || rc->stage == MOVE_DATA_EXTENTS))
			continue;
		if (tree_block_processed(bytenr, blocksize, rc))
			continue;
		if (!in_block_group(bytenr, rc->block_group) &&
		    !check_file_extents(rc, bytenr, blocksize, ptr_gen))
			continue;

		block = kmalloc(sizeof(*block), GFP_NOFS);
		if (!block) {
			err = -ENOMEM;
			break;
		}
		block->bytenr = bytenr;
		btrfs_node_key_to_cpu(node->eb, &block->key, i);
		block->level = node->level - 1;
		block->key_ready = 1;
		rb_node = tree_insert(blocks, block->bytenr, &block->rb_node);
		BUG_ON(rb_node);
	}
	if (err)
		free_block_list(blocks);
	return err;
}

/*
 * find adjacent blocks require processing
 */
static noinline_for_stack
int add_adjacent_blocks(struct btrfs_trans_handle *trans,
			struct reloc_control *rc,
			struct backref_cache *cache,
			struct rb_root *blocks, int level,
			struct backref_node **upper)
{
	struct backref_node *node;
	int ret = 0;

	WARN_ON(!list_empty(&cache->pending[level]));

	if (list_empty(&cache->pending[level + 1]))
		return 1;

	node = list_entry(cache->pending[level + 1].next,
			  struct backref_node, lower);
	if (node->eb)
		ret = add_child_blocks(trans, rc, node, blocks);

	*upper = node;
	return ret;
}

static int get_tree_block_key(struct reloc_control *rc,
			      struct tree_block *block)
{
	struct extent_buffer *eb;

	BUG_ON(block->key_ready);
	eb = read_tree_block(rc->extent_root, block->bytenr,
			     block->key.objectid, block->key.offset);
	WARN_ON(btrfs_header_level(eb) != block->level);
	if (block->level == 0)
		btrfs_item_key_to_cpu(eb, &block->key, 0);
	else
		btrfs_node_key_to_cpu(eb, &block->key, 0);
	free_extent_buffer(eb);
	block->key_ready = 1;
	return 0;
}

static int reada_tree_block(struct reloc_control *rc,
			    struct tree_block *block)
{
	BUG_ON(block->key_ready);
	readahead_tree_block(rc->extent_root, block->bytenr,
			     block->key.objectid, block->key.offset);
	return 0;
}

/*
 * helper function to relocate a tree block
 */
static int relocate_tree_block(struct btrfs_trans_handle *trans,
				struct reloc_control *rc,
				struct backref_node *node,
				struct btrfs_key *key,
				struct btrfs_path *path)
{
	struct btrfs_root *root;
	int ret;

	root = select_one_root(trans, node);
	if (unlikely(!root)) {
		rc->found_old_snapshot = 1;
		update_processed_blocks(rc, node);
		return 0;
	}

	if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID) {
		ret = do_relocation(trans, node, key, path, 1);
		if (ret < 0)
			goto out;
		if (node->level == 0 && rc->stage == UPDATE_DATA_PTRS) {
			ret = replace_file_extents(trans, rc, root,
						   node->eb, NULL);
			if (ret < 0)
				goto out;
		}
		drop_node_buffer(node);
	} else if (!root->ref_cows) {
		path->lowest_level = node->level;
		ret = btrfs_search_slot(trans, root, key, path, 0, 1);
		btrfs_release_path(root, path);
		if (ret < 0)
			goto out;
	} else if (root != node->root) {
		WARN_ON(node->level > 0 || rc->stage != UPDATE_DATA_PTRS);
	}

	update_processed_blocks(rc, node);
	ret = 0;
out:
	drop_node_buffer(node);
	return ret;
}

/*
 * relocate a list of blocks
 */
static noinline_for_stack
int relocate_tree_blocks(struct btrfs_trans_handle *trans,
			 struct reloc_control *rc, struct rb_root *blocks)
{
	struct backref_cache *cache;
	struct backref_node *node;
	struct btrfs_path *path;
	struct tree_block *block;
	struct rb_node *rb_node;
	int level = -1;
	int ret;
	int err = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	cache = kmalloc(sizeof(*cache), GFP_NOFS);
	if (!cache) {
		btrfs_free_path(path);
		return -ENOMEM;
	}

	backref_cache_init(cache);

	rb_node = rb_first(blocks);
	while (rb_node) {
		block = rb_entry(rb_node, struct tree_block, rb_node);
		if (level == -1)
			level = block->level;
		else
			BUG_ON(level != block->level);
		if (!block->key_ready)
			reada_tree_block(rc, block);
		rb_node = rb_next(rb_node);
	}

	rb_node = rb_first(blocks);
	while (rb_node) {
		block = rb_entry(rb_node, struct tree_block, rb_node);
		if (!block->key_ready)
			get_tree_block_key(rc, block);
		rb_node = rb_next(rb_node);
	}

	rb_node = rb_first(blocks);
	while (rb_node) {
		block = rb_entry(rb_node, struct tree_block, rb_node);

		node = build_backref_tree(rc, cache, &block->key,
					  block->level, block->bytenr);
		if (IS_ERR(node)) {
			err = PTR_ERR(node);
			goto out;
		}

		ret = relocate_tree_block(trans, rc, node, &block->key,
					  path);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		remove_backref_node(cache, node);
		rb_node = rb_next(rb_node);
	}

	if (level > 0)
		goto out;

	free_block_list(blocks);

	/*
	 * now backrefs of some upper level tree blocks have been cached,
	 * try relocating blocks referenced by these upper level blocks.
	 */
	while (1) {
		struct backref_node *upper = NULL;
		if (trans->transaction->in_commit ||
		    trans->transaction->delayed_refs.flushing)
			break;

		ret = add_adjacent_blocks(trans, rc, cache, blocks, level,
					  &upper);
		if (ret < 0)
			err = ret;
		if (ret != 0)
			break;

		rb_node = rb_first(blocks);
		while (rb_node) {
			block = rb_entry(rb_node, struct tree_block, rb_node);
			if (trans->transaction->in_commit ||
			    trans->transaction->delayed_refs.flushing)
				goto out;
			BUG_ON(!block->key_ready);
			node = build_backref_tree(rc, cache, &block->key,
						  level, block->bytenr);
			if (IS_ERR(node)) {
				err = PTR_ERR(node);
				goto out;
			}

			ret = relocate_tree_block(trans, rc, node,
						  &block->key, path);
			if (ret < 0) {
				err = ret;
				goto out;
			}
			remove_backref_node(cache, node);
			rb_node = rb_next(rb_node);
		}
		free_block_list(blocks);

		if (upper) {
			ret = link_to_upper(trans, upper, path);
			if (ret < 0) {
				err = ret;
				break;
			}
			remove_backref_node(cache, upper);
		}
	}
out:
	free_block_list(blocks);

	ret = finish_pending_nodes(trans, cache, path);
	if (ret < 0)
		err = ret;

	kfree(cache);
	btrfs_free_path(path);
	return err;
}

static noinline_for_stack
int setup_extent_mapping(struct inode *inode, u64 start, u64 end,
			 u64 block_start)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_map *em;
	int ret = 0;

	em = alloc_extent_map(GFP_NOFS);
	if (!em)
		return -ENOMEM;

	em->start = start;
	em->len = end + 1 - start;
	em->block_len = em->len;
	em->block_start = block_start;
	em->bdev = root->fs_info->fs_devices->latest_bdev;
	set_bit(EXTENT_FLAG_PINNED, &em->flags);

	lock_extent(&BTRFS_I(inode)->io_tree, start, end, GFP_NOFS);
	while (1) {
		write_lock(&em_tree->lock);
		ret = add_extent_mapping(em_tree, em);
		write_unlock(&em_tree->lock);
		if (ret != -EEXIST) {
			free_extent_map(em);
			break;
		}
		btrfs_drop_extent_cache(inode, start, end, 0);
	}
	unlock_extent(&BTRFS_I(inode)->io_tree, start, end, GFP_NOFS);
	return ret;
}

static int relocate_file_extent_cluster(struct inode *inode,
					struct file_extent_cluster *cluster)
{
	u64 page_start;
	u64 page_end;
	u64 offset = BTRFS_I(inode)->index_cnt;
	unsigned long index;
	unsigned long last_index;
	unsigned int dirty_page = 0;
	struct page *page;
	struct file_ra_state *ra;
	int nr = 0;
	int ret = 0;

	if (!cluster->nr)
		return 0;

	ra = kzalloc(sizeof(*ra), GFP_NOFS);
	if (!ra)
		return -ENOMEM;

	index = (cluster->start - offset) >> PAGE_CACHE_SHIFT;
	last_index = (cluster->end - offset) >> PAGE_CACHE_SHIFT;

	mutex_lock(&inode->i_mutex);

	i_size_write(inode, cluster->end + 1 - offset);
	ret = setup_extent_mapping(inode, cluster->start - offset,
				   cluster->end - offset, cluster->start);
	if (ret)
		goto out_unlock;

	file_ra_state_init(ra, inode->i_mapping);

	WARN_ON(cluster->start != cluster->boundary[0]);
	while (index <= last_index) {
		page = find_lock_page(inode->i_mapping, index);
		if (!page) {
			page_cache_sync_readahead(inode->i_mapping,
						  ra, NULL, index,
						  last_index + 1 - index);
			page = grab_cache_page(inode->i_mapping, index);
			if (!page) {
				ret = -ENOMEM;
				goto out_unlock;
			}
		}

		if (PageReadahead(page)) {
			page_cache_async_readahead(inode->i_mapping,
						   ra, NULL, page, index,
						   last_index + 1 - index);
		}

		if (!PageUptodate(page)) {
			btrfs_readpage(NULL, page);
			lock_page(page);
			if (!PageUptodate(page)) {
				unlock_page(page);
				page_cache_release(page);
				ret = -EIO;
				goto out_unlock;
			}
		}

		page_start = (u64)page->index << PAGE_CACHE_SHIFT;
		page_end = page_start + PAGE_CACHE_SIZE - 1;

		lock_extent(&BTRFS_I(inode)->io_tree,
			    page_start, page_end, GFP_NOFS);

		set_page_extent_mapped(page);

		if (nr < cluster->nr &&
		    page_start + offset == cluster->boundary[nr]) {
			set_extent_bits(&BTRFS_I(inode)->io_tree,
					page_start, page_end,
					EXTENT_BOUNDARY, GFP_NOFS);
			nr++;
		}
		btrfs_set_extent_delalloc(inode, page_start, page_end);

		set_page_dirty(page);
		dirty_page++;

		unlock_extent(&BTRFS_I(inode)->io_tree,
			      page_start, page_end, GFP_NOFS);
		unlock_page(page);
		page_cache_release(page);

		index++;
		if (nr < cluster->nr &&
		    page_end + 1 + offset == cluster->boundary[nr]) {
			balance_dirty_pages_ratelimited_nr(inode->i_mapping,
							   dirty_page);
			dirty_page = 0;
		}
	}
	if (dirty_page) {
		balance_dirty_pages_ratelimited_nr(inode->i_mapping,
						   dirty_page);
	}
	WARN_ON(nr != cluster->nr);
out_unlock:
	mutex_unlock(&inode->i_mutex);
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

#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
static int get_ref_objectid_v0(struct reloc_control *rc,
			       struct btrfs_path *path,
			       struct btrfs_key *extent_key,
			       u64 *ref_objectid, int *path_change)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_extent_ref_v0 *ref0;
	int ret;
	int slot;

	leaf = path->nodes[0];
	slot = path->slots[0];
	while (1) {
		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(rc->extent_root, path);
			if (ret < 0)
				return ret;
			BUG_ON(ret > 0);
			leaf = path->nodes[0];
			slot = path->slots[0];
			if (path_change)
				*path_change = 1;
		}
		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid != extent_key->objectid)
			return -ENOENT;

		if (key.type != BTRFS_EXTENT_REF_V0_KEY) {
			slot++;
			continue;
		}
		ref0 = btrfs_item_ptr(leaf, slot,
				struct btrfs_extent_ref_v0);
		*ref_objectid = btrfs_ref_objectid_v0(leaf, ref0);
		break;
	}
	return 0;
}
#endif

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
	int generation;

	eb =  path->nodes[0];
	item_size = btrfs_item_size_nr(eb, path->slots[0]);

	if (item_size >= sizeof(*ei) + sizeof(*bi)) {
		ei = btrfs_item_ptr(eb, path->slots[0],
				struct btrfs_extent_item);
		bi = (struct btrfs_tree_block_info *)(ei + 1);
		generation = btrfs_extent_generation(eb, ei);
		level = btrfs_tree_block_level(eb, bi);
	} else {
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
		u64 ref_owner;
		int ret;

		BUG_ON(item_size != sizeof(struct btrfs_extent_item_v0));
		ret = get_ref_objectid_v0(rc, path, extent_key,
					  &ref_owner, NULL);
		BUG_ON(ref_owner >= BTRFS_MAX_LEVEL);
		level = (int)ref_owner;
		/* FIXME: get real generation */
		generation = 0;
#else
		BUG();
#endif
	}

	btrfs_release_path(rc->extent_root, path);

	BUG_ON(level == -1);

	block = kmalloc(sizeof(*block), GFP_NOFS);
	if (!block)
		return -ENOMEM;

	block->bytenr = extent_key->objectid;
	block->key.objectid = extent_key->offset;
	block->key.offset = generation;
	block->level = level;
	block->key_ready = 0;

	rb_node = tree_insert(blocks, block->bytenr, &block->rb_node);
	BUG_ON(rb_node);

	return 0;
}

/*
 * helper to add tree blocks for backref of type BTRFS_SHARED_DATA_REF_KEY
 */
static int __add_tree_block(struct reloc_control *rc,
			    u64 bytenr, u32 blocksize,
			    struct rb_root *blocks)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret;

	if (tree_block_processed(bytenr, blocksize, rc))
		return 0;

	if (tree_search(blocks, bytenr))
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = blocksize;

	path->search_commit_root = 1;
	path->skip_locking = 1;
	ret = btrfs_search_slot(NULL, rc->extent_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	BUG_ON(ret);

	btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
	ret = add_tree_block(rc, &key, path, blocks);
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * helper to check if the block use full backrefs for pointers in it
 */
static int block_use_full_backref(struct reloc_control *rc,
				  struct extent_buffer *eb)
{
	struct btrfs_path *path;
	struct btrfs_extent_item *ei;
	struct btrfs_key key;
	u64 flags;
	int ret;

	if (btrfs_header_flag(eb, BTRFS_HEADER_FLAG_RELOC) ||
	    btrfs_header_backref_rev(eb) < BTRFS_MIXED_BACKREF_REV)
		return 1;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	key.objectid = eb->start;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = eb->len;

	path->search_commit_root = 1;
	path->skip_locking = 1;
	ret = btrfs_search_slot(NULL, rc->extent_root,
				&key, path, 0, 0);
	BUG_ON(ret);

	ei = btrfs_item_ptr(path->nodes[0], path->slots[0],
			    struct btrfs_extent_item);
	flags = btrfs_extent_flags(path->nodes[0], ei);
	BUG_ON(!(flags & BTRFS_EXTENT_FLAG_TREE_BLOCK));
	if (flags & BTRFS_BLOCK_FLAG_FULL_BACKREF)
		ret = 1;
	else
		ret = 0;
	btrfs_free_path(path);
	return ret;
}

/*
 * helper to add tree blocks for backref of type BTRFS_EXTENT_DATA_REF_KEY
 * this function scans fs tree to find blocks reference the data extent
 */
static int find_data_references(struct reloc_control *rc,
				struct btrfs_key *extent_key,
				struct extent_buffer *leaf,
				struct btrfs_extent_data_ref *ref,
				struct rb_root *blocks)
{
	struct btrfs_path *path;
	struct tree_block *block;
	struct btrfs_root *root;
	struct btrfs_file_extent_item *fi;
	struct rb_node *rb_node;
	struct btrfs_key key;
	u64 ref_root;
	u64 ref_objectid;
	u64 ref_offset;
	u32 ref_count;
	u32 nritems;
	int err = 0;
	int added = 0;
	int counted;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ref_root = btrfs_extent_data_ref_root(leaf, ref);
	ref_objectid = btrfs_extent_data_ref_objectid(leaf, ref);
	ref_offset = btrfs_extent_data_ref_offset(leaf, ref);
	ref_count = btrfs_extent_data_ref_count(leaf, ref);

	root = read_fs_root(rc->extent_root->fs_info, ref_root);
	if (IS_ERR(root)) {
		err = PTR_ERR(root);
		goto out;
	}

	key.objectid = ref_objectid;
	key.offset = ref_offset;
	key.type = BTRFS_EXTENT_DATA_KEY;

	path->search_commit_root = 1;
	path->skip_locking = 1;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		err = ret;
		goto out;
	}

	leaf = path->nodes[0];
	nritems = btrfs_header_nritems(leaf);
	/*
	 * the references in tree blocks that use full backrefs
	 * are not counted in
	 */
	if (block_use_full_backref(rc, leaf))
		counted = 0;
	else
		counted = 1;
	rb_node = tree_search(blocks, leaf->start);
	if (rb_node) {
		if (counted)
			added = 1;
		else
			path->slots[0] = nritems;
	}

	while (ref_count > 0) {
		while (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0) {
				err = ret;
				goto out;
			}
			if (ret > 0) {
				WARN_ON(1);
				goto out;
			}

			leaf = path->nodes[0];
			nritems = btrfs_header_nritems(leaf);
			added = 0;

			if (block_use_full_backref(rc, leaf))
				counted = 0;
			else
				counted = 1;
			rb_node = tree_search(blocks, leaf->start);
			if (rb_node) {
				if (counted)
					added = 1;
				else
					path->slots[0] = nritems;
			}
		}

		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		if (key.objectid != ref_objectid ||
		    key.type != BTRFS_EXTENT_DATA_KEY) {
			WARN_ON(1);
			break;
		}

		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);

		if (btrfs_file_extent_type(leaf, fi) ==
		    BTRFS_FILE_EXTENT_INLINE)
			goto next;

		if (btrfs_file_extent_disk_bytenr(leaf, fi) !=
		    extent_key->objectid)
			goto next;

		key.offset -= btrfs_file_extent_offset(leaf, fi);
		if (key.offset != ref_offset)
			goto next;

		if (counted)
			ref_count--;
		if (added)
			goto next;

		if (!tree_block_processed(leaf->start, leaf->len, rc)) {
			block = kmalloc(sizeof(*block), GFP_NOFS);
			if (!block) {
				err = -ENOMEM;
				break;
			}
			block->bytenr = leaf->start;
			btrfs_item_key_to_cpu(leaf, &block->key, 0);
			block->level = 0;
			block->key_ready = 1;
			rb_node = tree_insert(blocks, block->bytenr,
					      &block->rb_node);
			BUG_ON(rb_node);
		}
		if (counted)
			added = 1;
		else
			path->slots[0] = nritems;
next:
		path->slots[0]++;

	}
out:
	btrfs_free_path(path);
	return err;
}

/*
 * hepler to find all tree blocks that reference a given data extent
 */
static noinline_for_stack
int add_data_references(struct reloc_control *rc,
			struct btrfs_key *extent_key,
			struct btrfs_path *path,
			struct rb_root *blocks)
{
	struct btrfs_key key;
	struct extent_buffer *eb;
	struct btrfs_extent_data_ref *dref;
	struct btrfs_extent_inline_ref *iref;
	unsigned long ptr;
	unsigned long end;
	u32 blocksize;
	int ret;
	int err = 0;

	ret = get_new_location(rc->data_inode, NULL, extent_key->objectid,
			       extent_key->offset);
	BUG_ON(ret < 0);
	if (ret > 0) {
		/* the relocated data is fragmented */
		rc->extents_skipped++;
		btrfs_release_path(rc->extent_root, path);
		return 0;
	}

	blocksize = btrfs_level_size(rc->extent_root, 0);

	eb = path->nodes[0];
	ptr = btrfs_item_ptr_offset(eb, path->slots[0]);
	end = ptr + btrfs_item_size_nr(eb, path->slots[0]);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (ptr + sizeof(struct btrfs_extent_item_v0) == end)
		ptr = end;
	else
#endif
		ptr += sizeof(struct btrfs_extent_item);

	while (ptr < end) {
		iref = (struct btrfs_extent_inline_ref *)ptr;
		key.type = btrfs_extent_inline_ref_type(eb, iref);
		if (key.type == BTRFS_SHARED_DATA_REF_KEY) {
			key.offset = btrfs_extent_inline_ref_offset(eb, iref);
			ret = __add_tree_block(rc, key.offset, blocksize,
					       blocks);
		} else if (key.type == BTRFS_EXTENT_DATA_REF_KEY) {
			dref = (struct btrfs_extent_data_ref *)(&iref->offset);
			ret = find_data_references(rc, extent_key,
						   eb, dref, blocks);
		} else {
			BUG();
		}
		ptr += btrfs_extent_inline_ref_size(key.type);
	}
	WARN_ON(ptr > end);

	while (1) {
		cond_resched();
		eb = path->nodes[0];
		if (path->slots[0] >= btrfs_header_nritems(eb)) {
			ret = btrfs_next_leaf(rc->extent_root, path);
			if (ret < 0) {
				err = ret;
				break;
			}
			if (ret > 0)
				break;
			eb = path->nodes[0];
		}

		btrfs_item_key_to_cpu(eb, &key, path->slots[0]);
		if (key.objectid != extent_key->objectid)
			break;

#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
		if (key.type == BTRFS_SHARED_DATA_REF_KEY ||
		    key.type == BTRFS_EXTENT_REF_V0_KEY) {
#else
		BUG_ON(key.type == BTRFS_EXTENT_REF_V0_KEY);
		if (key.type == BTRFS_SHARED_DATA_REF_KEY) {
#endif
			ret = __add_tree_block(rc, key.offset, blocksize,
					       blocks);
		} else if (key.type == BTRFS_EXTENT_DATA_REF_KEY) {
			dref = btrfs_item_ptr(eb, path->slots[0],
					      struct btrfs_extent_data_ref);
			ret = find_data_references(rc, extent_key,
						   eb, dref, blocks);
		} else {
			ret = 0;
		}
		if (ret) {
			err = ret;
			break;
		}
		path->slots[0]++;
	}
	btrfs_release_path(rc->extent_root, path);
	if (err)
		free_block_list(blocks);
	return err;
}

/*
 * hepler to find next unprocessed extent
 */
static noinline_for_stack
int find_next_extent(struct btrfs_trans_handle *trans,
		     struct reloc_control *rc, struct btrfs_path *path)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	u64 start, end, last;
	int ret;

	last = rc->block_group->key.objectid + rc->block_group->key.offset;
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

		if (key.type != BTRFS_EXTENT_ITEM_KEY ||
		    key.objectid + key.offset <= rc->search_start) {
			path->slots[0]++;
			goto next;
		}

		ret = find_first_extent_bit(&rc->processed_blocks,
					    key.objectid, &start, &end,
					    EXTENT_DIRTY);

		if (ret == 0 && start <= key.objectid) {
			btrfs_release_path(rc->extent_root, path);
			rc->search_start = end + 1;
		} else {
			rc->search_start = key.objectid + key.offset;
			return 0;
		}
	}
	btrfs_release_path(rc->extent_root, path);
	return ret;
}

static void set_reloc_control(struct reloc_control *rc)
{
	struct btrfs_fs_info *fs_info = rc->extent_root->fs_info;
	mutex_lock(&fs_info->trans_mutex);
	fs_info->reloc_ctl = rc;
	mutex_unlock(&fs_info->trans_mutex);
}

static void unset_reloc_control(struct reloc_control *rc)
{
	struct btrfs_fs_info *fs_info = rc->extent_root->fs_info;
	mutex_lock(&fs_info->trans_mutex);
	fs_info->reloc_ctl = NULL;
	mutex_unlock(&fs_info->trans_mutex);
}

static int check_extent_flags(u64 flags)
{
	if ((flags & BTRFS_EXTENT_FLAG_DATA) &&
	    (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK))
		return 1;
	if (!(flags & BTRFS_EXTENT_FLAG_DATA) &&
	    !(flags & BTRFS_EXTENT_FLAG_TREE_BLOCK))
		return 1;
	if ((flags & BTRFS_EXTENT_FLAG_DATA) &&
	    (flags & BTRFS_BLOCK_FLAG_FULL_BACKREF))
		return 1;
	return 0;
}


static noinline_for_stack int relocate_block_group(struct reloc_control *rc)
{
	struct rb_root blocks = RB_ROOT;
	struct btrfs_key key;
	struct file_extent_cluster *cluster;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_path *path;
	struct btrfs_extent_item *ei;
	unsigned long nr;
	u64 flags;
	u32 item_size;
	int ret;
	int err = 0;

	cluster = kzalloc(sizeof(*cluster), GFP_NOFS);
	if (!cluster)
		return -ENOMEM;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	rc->extents_found = 0;
	rc->extents_skipped = 0;

	rc->search_start = rc->block_group->key.objectid;
	clear_extent_bits(&rc->processed_blocks, 0, (u64)-1, EXTENT_DIRTY,
			  GFP_NOFS);

	rc->create_reloc_root = 1;
	set_reloc_control(rc);

	trans = btrfs_start_transaction(rc->extent_root, 1);
	btrfs_commit_transaction(trans, rc->extent_root);

	while (1) {
		trans = btrfs_start_transaction(rc->extent_root, 1);

		ret = find_next_extent(trans, rc, path);
		if (ret < 0)
			err = ret;
		if (ret != 0)
			break;

		rc->extents_found++;

		ei = btrfs_item_ptr(path->nodes[0], path->slots[0],
				    struct btrfs_extent_item);
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		item_size = btrfs_item_size_nr(path->nodes[0],
					       path->slots[0]);
		if (item_size >= sizeof(*ei)) {
			flags = btrfs_extent_flags(path->nodes[0], ei);
			ret = check_extent_flags(flags);
			BUG_ON(ret);

		} else {
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
			u64 ref_owner;
			int path_change = 0;

			BUG_ON(item_size !=
			       sizeof(struct btrfs_extent_item_v0));
			ret = get_ref_objectid_v0(rc, path, &key, &ref_owner,
						  &path_change);
			if (ref_owner < BTRFS_FIRST_FREE_OBJECTID)
				flags = BTRFS_EXTENT_FLAG_TREE_BLOCK;
			else
				flags = BTRFS_EXTENT_FLAG_DATA;

			if (path_change) {
				btrfs_release_path(rc->extent_root, path);

				path->search_commit_root = 1;
				path->skip_locking = 1;
				ret = btrfs_search_slot(NULL, rc->extent_root,
							&key, path, 0, 0);
				if (ret < 0) {
					err = ret;
					break;
				}
				BUG_ON(ret > 0);
			}
#else
			BUG();
#endif
		}

		if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
			ret = add_tree_block(rc, &key, path, &blocks);
		} else if (rc->stage == UPDATE_DATA_PTRS &&
			 (flags & BTRFS_EXTENT_FLAG_DATA)) {
			ret = add_data_references(rc, &key, path, &blocks);
		} else {
			btrfs_release_path(rc->extent_root, path);
			ret = 0;
		}
		if (ret < 0) {
			err = 0;
			break;
		}

		if (!RB_EMPTY_ROOT(&blocks)) {
			ret = relocate_tree_blocks(trans, rc, &blocks);
			if (ret < 0) {
				err = ret;
				break;
			}
		}

		nr = trans->blocks_used;
		btrfs_end_transaction(trans, rc->extent_root);
		trans = NULL;
		btrfs_btree_balance_dirty(rc->extent_root, nr);

		if (rc->stage == MOVE_DATA_EXTENTS &&
		    (flags & BTRFS_EXTENT_FLAG_DATA)) {
			rc->found_file_extent = 1;
			ret = relocate_data_extent(rc->data_inode,
						   &key, cluster);
			if (ret < 0) {
				err = ret;
				break;
			}
		}
	}
	btrfs_free_path(path);

	if (trans) {
		nr = trans->blocks_used;
		btrfs_end_transaction(trans, rc->extent_root);
		btrfs_btree_balance_dirty(rc->extent_root, nr);
	}

	if (!err) {
		ret = relocate_file_extent_cluster(rc->data_inode, cluster);
		if (ret < 0)
			err = ret;
	}

	kfree(cluster);

	rc->create_reloc_root = 0;
	smp_mb();

	if (rc->extents_found > 0) {
		trans = btrfs_start_transaction(rc->extent_root, 1);
		btrfs_commit_transaction(trans, rc->extent_root);
	}

	merge_reloc_roots(rc);

	unset_reloc_control(rc);

	/* get rid of pinned extents */
	trans = btrfs_start_transaction(rc->extent_root, 1);
	btrfs_commit_transaction(trans, rc->extent_root);

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
	memset_extent_buffer(leaf, 0, (unsigned long)item, sizeof(*item));
	btrfs_set_inode_generation(leaf, item, 1);
	btrfs_set_inode_size(leaf, item, 0);
	btrfs_set_inode_mode(leaf, item, S_IFREG | 0600);
	btrfs_set_inode_flags(leaf, item, BTRFS_INODE_NOCOMPRESS);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(root, path);
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * helper to create inode for data relocation.
 * the inode is in data relocation tree and its link count is 0
 */
static struct inode *create_reloc_inode(struct btrfs_fs_info *fs_info,
					struct btrfs_block_group_cache *group)
{
	struct inode *inode = NULL;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root;
	struct btrfs_key key;
	unsigned long nr;
	u64 objectid = BTRFS_FIRST_FREE_OBJECTID;
	int err = 0;

	root = read_fs_root(fs_info, BTRFS_DATA_RELOC_TREE_OBJECTID);
	if (IS_ERR(root))
		return ERR_CAST(root);

	trans = btrfs_start_transaction(root, 1);
	BUG_ON(!trans);

	err = btrfs_find_free_objectid(trans, root, objectid, &objectid);
	if (err)
		goto out;

	err = __insert_orphan_inode(trans, root, objectid);
	BUG_ON(err);

	key.objectid = objectid;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;
	inode = btrfs_iget(root->fs_info->sb, &key, root);
	BUG_ON(IS_ERR(inode) || is_bad_inode(inode));
	BTRFS_I(inode)->index_cnt = group->key.objectid;

	err = btrfs_orphan_add(trans, inode);
out:
	nr = trans->blocks_used;
	btrfs_end_transaction(trans, root);

	btrfs_btree_balance_dirty(root, nr);
	if (err) {
		if (inode)
			iput(inode);
		inode = ERR_PTR(err);
	}
	return inode;
}

/*
 * function to relocate all extents in a block group.
 */
int btrfs_relocate_block_group(struct btrfs_root *extent_root, u64 group_start)
{
	struct btrfs_fs_info *fs_info = extent_root->fs_info;
	struct reloc_control *rc;
	int ret;
	int err = 0;

	rc = kzalloc(sizeof(*rc), GFP_NOFS);
	if (!rc)
		return -ENOMEM;

	mapping_tree_init(&rc->reloc_root_tree);
	extent_io_tree_init(&rc->processed_blocks, NULL, GFP_NOFS);
	INIT_LIST_HEAD(&rc->reloc_roots);

	rc->block_group = btrfs_lookup_block_group(fs_info, group_start);
	BUG_ON(!rc->block_group);

	btrfs_init_workers(&rc->workers, "relocate",
			   fs_info->thread_pool_size);

	rc->extent_root = extent_root;
	btrfs_prepare_block_group_relocation(extent_root, rc->block_group);

	rc->data_inode = create_reloc_inode(fs_info, rc->block_group);
	if (IS_ERR(rc->data_inode)) {
		err = PTR_ERR(rc->data_inode);
		rc->data_inode = NULL;
		goto out;
	}

	printk(KERN_INFO "btrfs: relocating block group %llu flags %llu\n",
	       (unsigned long long)rc->block_group->key.objectid,
	       (unsigned long long)rc->block_group->flags);

	btrfs_start_delalloc_inodes(fs_info->tree_root);
	btrfs_wait_ordered_extents(fs_info->tree_root, 0);

	while (1) {
		rc->extents_found = 0;
		rc->extents_skipped = 0;

		mutex_lock(&fs_info->cleaner_mutex);

		btrfs_clean_old_snapshots(fs_info->tree_root);
		ret = relocate_block_group(rc);

		mutex_unlock(&fs_info->cleaner_mutex);
		if (ret < 0) {
			err = ret;
			break;
		}

		if (rc->extents_found == 0)
			break;

		printk(KERN_INFO "btrfs: found %llu extents\n",
			(unsigned long long)rc->extents_found);

		if (rc->stage == MOVE_DATA_EXTENTS && rc->found_file_extent) {
			btrfs_wait_ordered_range(rc->data_inode, 0, (u64)-1);
			invalidate_mapping_pages(rc->data_inode->i_mapping,
						 0, -1);
			rc->stage = UPDATE_DATA_PTRS;
		} else if (rc->stage == UPDATE_DATA_PTRS &&
			   rc->extents_skipped >= rc->extents_found) {
			iput(rc->data_inode);
			rc->data_inode = create_reloc_inode(fs_info,
							    rc->block_group);
			if (IS_ERR(rc->data_inode)) {
				err = PTR_ERR(rc->data_inode);
				rc->data_inode = NULL;
				break;
			}
			rc->stage = MOVE_DATA_EXTENTS;
			rc->found_file_extent = 0;
		}
	}

	filemap_write_and_wait_range(fs_info->btree_inode->i_mapping,
				     rc->block_group->key.objectid,
				     rc->block_group->key.objectid +
				     rc->block_group->key.offset - 1);

	WARN_ON(rc->block_group->pinned > 0);
	WARN_ON(rc->block_group->reserved > 0);
	WARN_ON(btrfs_block_group_used(&rc->block_group->item) > 0);
out:
	iput(rc->data_inode);
	btrfs_stop_workers(&rc->workers);
	btrfs_put_block_group(rc->block_group);
	kfree(rc);
	return err;
}

static noinline_for_stack int mark_garbage_root(struct btrfs_root *root)
{
	struct btrfs_trans_handle *trans;
	int ret;

	trans = btrfs_start_transaction(root->fs_info->tree_root, 1);

	memset(&root->root_item.drop_progress, 0,
		sizeof(root->root_item.drop_progress));
	root->root_item.drop_level = 0;
	btrfs_set_root_refs(&root->root_item, 0);
	ret = btrfs_update_root(trans, root->fs_info->tree_root,
				&root->root_key, &root->root_item);
	BUG_ON(ret);

	ret = btrfs_end_transaction(trans, root->fs_info->tree_root);
	BUG_ON(ret);
	return 0;
}

/*
 * recover relocation interrupted by system crash.
 *
 * this function resumes merging reloc trees with corresponding fs trees.
 * this is important for keeping the sharing of tree blocks
 */
int btrfs_recover_relocation(struct btrfs_root *root)
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

	key.objectid = BTRFS_TREE_RELOC_OBJECTID;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = (u64)-1;

	while (1) {
		ret = btrfs_search_slot(NULL, root->fs_info->tree_root, &key,
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
		btrfs_release_path(root->fs_info->tree_root, path);

		if (key.objectid != BTRFS_TREE_RELOC_OBJECTID ||
		    key.type != BTRFS_ROOT_ITEM_KEY)
			break;

		reloc_root = btrfs_read_fs_root_no_radix(root, &key);
		if (IS_ERR(reloc_root)) {
			err = PTR_ERR(reloc_root);
			goto out;
		}

		list_add(&reloc_root->root_list, &reloc_roots);

		if (btrfs_root_refs(&reloc_root->root_item) > 0) {
			fs_root = read_fs_root(root->fs_info,
					       reloc_root->root_key.offset);
			if (IS_ERR(fs_root)) {
				ret = PTR_ERR(fs_root);
				if (ret != -ENOENT) {
					err = ret;
					goto out;
				}
				mark_garbage_root(reloc_root);
			}
		}

		if (key.offset == 0)
			break;

		key.offset--;
	}
	btrfs_release_path(root->fs_info->tree_root, path);

	if (list_empty(&reloc_roots))
		goto out;

	rc = kzalloc(sizeof(*rc), GFP_NOFS);
	if (!rc) {
		err = -ENOMEM;
		goto out;
	}

	mapping_tree_init(&rc->reloc_root_tree);
	INIT_LIST_HEAD(&rc->reloc_roots);
	btrfs_init_workers(&rc->workers, "relocate",
			   root->fs_info->thread_pool_size);
	rc->extent_root = root->fs_info->extent_root;

	set_reloc_control(rc);

	while (!list_empty(&reloc_roots)) {
		reloc_root = list_entry(reloc_roots.next,
					struct btrfs_root, root_list);
		list_del(&reloc_root->root_list);

		if (btrfs_root_refs(&reloc_root->root_item) == 0) {
			list_add_tail(&reloc_root->root_list,
				      &rc->reloc_roots);
			continue;
		}

		fs_root = read_fs_root(root->fs_info,
				       reloc_root->root_key.offset);
		BUG_ON(IS_ERR(fs_root));

		__add_reloc_root(reloc_root);
		fs_root->reloc_root = reloc_root;
	}

	trans = btrfs_start_transaction(rc->extent_root, 1);
	btrfs_commit_transaction(trans, rc->extent_root);

	merge_reloc_roots(rc);

	unset_reloc_control(rc);

	trans = btrfs_start_transaction(rc->extent_root, 1);
	btrfs_commit_transaction(trans, rc->extent_root);
out:
	if (rc) {
		btrfs_stop_workers(&rc->workers);
		kfree(rc);
	}
	while (!list_empty(&reloc_roots)) {
		reloc_root = list_entry(reloc_roots.next,
					struct btrfs_root, root_list);
		list_del(&reloc_root->root_list);
		free_extent_buffer(reloc_root->node);
		free_extent_buffer(reloc_root->commit_root);
		kfree(reloc_root);
	}
	btrfs_free_path(path);

	if (err == 0) {
		/* cleanup orphan inode in data relocation tree */
		fs_root = read_fs_root(root->fs_info,
				       BTRFS_DATA_RELOC_TREE_OBJECTID);
		if (IS_ERR(fs_root))
			err = PTR_ERR(fs_root);
	}
	return err;
}

/*
 * helper to add ordered checksum for data relocation.
 *
 * cloning checksum properly handles the nodatasum extents.
 * it also saves CPU time to re-calculate the checksum.
 */
int btrfs_reloc_clone_csums(struct inode *inode, u64 file_pos, u64 len)
{
	struct btrfs_ordered_sum *sums;
	struct btrfs_sector_sum *sector_sum;
	struct btrfs_ordered_extent *ordered;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	size_t offset;
	int ret;
	u64 disk_bytenr;
	LIST_HEAD(list);

	ordered = btrfs_lookup_ordered_extent(inode, file_pos);
	BUG_ON(ordered->file_offset != file_pos || ordered->len != len);

	disk_bytenr = file_pos + BTRFS_I(inode)->index_cnt;
	ret = btrfs_lookup_csums_range(root->fs_info->csum_root, disk_bytenr,
				       disk_bytenr + len - 1, &list);

	while (!list_empty(&list)) {
		sums = list_entry(list.next, struct btrfs_ordered_sum, list);
		list_del_init(&sums->list);

		sector_sum = sums->sums;
		sums->bytenr = ordered->start;

		offset = 0;
		while (offset < sums->len) {
			sector_sum->bytenr += ordered->start - disk_bytenr;
			sector_sum++;
			offset += root->sectorsize;
		}

		btrfs_add_ordered_sum(inode, ordered, sums);
	}
	btrfs_put_ordered_extent(ordered);
	return 0;
}
