/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2011 STRATO.  All rights reserved.
 */

#ifndef BTRFS_BACKREF_H
#define BTRFS_BACKREF_H

#include <linux/btrfs.h>
#include "ulist.h"
#include "disk-io.h"
#include "extent_io.h"

struct inode_fs_paths {
	struct btrfs_path		*btrfs_path;
	struct btrfs_root		*fs_root;
	struct btrfs_data_container	*fspath;
};

typedef int (iterate_extent_inodes_t)(u64 inum, u64 offset, u64 root,
		void *ctx);

int extent_from_logical(struct btrfs_fs_info *fs_info, u64 logical,
			struct btrfs_path *path, struct btrfs_key *found_key,
			u64 *flags);

int tree_backref_for_extent(unsigned long *ptr, struct extent_buffer *eb,
			    struct btrfs_key *key, struct btrfs_extent_item *ei,
			    u32 item_size, u64 *out_root, u8 *out_level);

int iterate_extent_inodes(struct btrfs_fs_info *fs_info,
				u64 extent_item_objectid,
				u64 extent_offset, int search_commit_root,
				iterate_extent_inodes_t *iterate, void *ctx,
				bool ignore_offset);

int iterate_inodes_from_logical(u64 logical, struct btrfs_fs_info *fs_info,
				struct btrfs_path *path, void *ctx,
				bool ignore_offset);

int paths_from_inode(u64 inum, struct inode_fs_paths *ipath);

int btrfs_find_all_leafs(struct btrfs_trans_handle *trans,
			 struct btrfs_fs_info *fs_info, u64 bytenr,
			 u64 time_seq, struct ulist **leafs,
			 const u64 *extent_item_pos, bool ignore_offset);
int btrfs_find_all_roots(struct btrfs_trans_handle *trans,
			 struct btrfs_fs_info *fs_info, u64 bytenr,
			 u64 time_seq, struct ulist **roots,
			 bool skip_commit_root_sem);
char *btrfs_ref_to_path(struct btrfs_root *fs_root, struct btrfs_path *path,
			u32 name_len, unsigned long name_off,
			struct extent_buffer *eb_in, u64 parent,
			char *dest, u32 size);

struct btrfs_data_container *init_data_container(u32 total_bytes);
struct inode_fs_paths *init_ipath(s32 total_bytes, struct btrfs_root *fs_root,
					struct btrfs_path *path);
void free_ipath(struct inode_fs_paths *ipath);

int btrfs_find_one_extref(struct btrfs_root *root, u64 inode_objectid,
			  u64 start_off, struct btrfs_path *path,
			  struct btrfs_inode_extref **ret_extref,
			  u64 *found_off);
int btrfs_check_shared(struct btrfs_root *root, u64 inum, u64 bytenr,
		struct ulist *roots, struct ulist *tmp_ulist);

int __init btrfs_prelim_ref_init(void);
void __cold btrfs_prelim_ref_exit(void);

struct prelim_ref {
	struct rb_node rbnode;
	u64 root_id;
	struct btrfs_key key_for_search;
	int level;
	int count;
	struct extent_inode_elem *inode_list;
	u64 parent;
	u64 wanted_disk_byte;
};

/*
 * Iterate backrefs of one extent.
 *
 * Now it only supports iteration of tree block in commit root.
 */
struct btrfs_backref_iter {
	u64 bytenr;
	struct btrfs_path *path;
	struct btrfs_fs_info *fs_info;
	struct btrfs_key cur_key;
	u32 item_ptr;
	u32 cur_ptr;
	u32 end_ptr;
};

struct btrfs_backref_iter *btrfs_backref_iter_alloc(
		struct btrfs_fs_info *fs_info, gfp_t gfp_flag);

static inline void btrfs_backref_iter_free(struct btrfs_backref_iter *iter)
{
	if (!iter)
		return;
	btrfs_free_path(iter->path);
	kfree(iter);
}

static inline struct extent_buffer *btrfs_backref_get_eb(
		struct btrfs_backref_iter *iter)
{
	if (!iter)
		return NULL;
	return iter->path->nodes[0];
}

/*
 * For metadata with EXTENT_ITEM key (non-skinny) case, the first inline data
 * is btrfs_tree_block_info, without a btrfs_extent_inline_ref header.
 *
 * This helper determines if that's the case.
 */
static inline bool btrfs_backref_has_tree_block_info(
		struct btrfs_backref_iter *iter)
{
	if (iter->cur_key.type == BTRFS_EXTENT_ITEM_KEY &&
	    iter->cur_ptr - iter->item_ptr == sizeof(struct btrfs_extent_item))
		return true;
	return false;
}

int btrfs_backref_iter_start(struct btrfs_backref_iter *iter, u64 bytenr);

int btrfs_backref_iter_next(struct btrfs_backref_iter *iter);

static inline bool btrfs_backref_iter_is_inline_ref(
		struct btrfs_backref_iter *iter)
{
	if (iter->cur_key.type == BTRFS_EXTENT_ITEM_KEY ||
	    iter->cur_key.type == BTRFS_METADATA_ITEM_KEY)
		return true;
	return false;
}

static inline void btrfs_backref_iter_release(struct btrfs_backref_iter *iter)
{
	iter->bytenr = 0;
	iter->item_ptr = 0;
	iter->cur_ptr = 0;
	iter->end_ptr = 0;
	btrfs_release_path(iter->path);
	memset(&iter->cur_key, 0, sizeof(iter->cur_key));
}

/*
 * Backref cache related structures
 *
 * The whole objective of backref_cache is to build a bi-directional map
 * of tree blocks (represented by backref_node) and all their parents.
 */

/*
 * Represent a tree block in the backref cache
 */
struct btrfs_backref_node {
	struct {
		struct rb_node rb_node;
		u64 bytenr;
	}; /* Use rb_simple_node for search/insert */

	u64 new_bytenr;
	/* Objectid of tree block owner, can be not uptodate */
	u64 owner;
	/* Link to pending, changed or detached list */
	struct list_head list;

	/* List of upper level edges, which link this node to its parents */
	struct list_head upper;
	/* List of lower level edges, which link this node to its children */
	struct list_head lower;

	/* NULL if this node is not tree root */
	struct btrfs_root *root;
	/* Extent buffer got by COWing the block */
	struct extent_buffer *eb;
	/* Level of the tree block */
	unsigned int level:8;
	/* Is the block in a non-shareable tree */
	unsigned int cowonly:1;
	/* 1 if no child node is in the cache */
	unsigned int lowest:1;
	/* Is the extent buffer locked */
	unsigned int locked:1;
	/* Has the block been processed */
	unsigned int processed:1;
	/* Have backrefs of this block been checked */
	unsigned int checked:1;
	/*
	 * 1 if corresponding block has been COWed but some upper level block
	 * pointers may not point to the new location
	 */
	unsigned int pending:1;
	/* 1 if the backref node isn't connected to any other backref node */
	unsigned int detached:1;

	/*
	 * For generic purpose backref cache, where we only care if it's a reloc
	 * root, doesn't care the source subvolid.
	 */
	unsigned int is_reloc_root:1;
};

#define LOWER	0
#define UPPER	1

/*
 * Represent an edge connecting upper and lower backref nodes.
 */
struct btrfs_backref_edge {
	/*
	 * list[LOWER] is linked to btrfs_backref_node::upper of lower level
	 * node, and list[UPPER] is linked to btrfs_backref_node::lower of
	 * upper level node.
	 *
	 * Also, build_backref_tree() uses list[UPPER] for pending edges, before
	 * linking list[UPPER] to its upper level nodes.
	 */
	struct list_head list[2];

	/* Two related nodes */
	struct btrfs_backref_node *node[2];
};

struct btrfs_backref_cache {
	/* Red black tree of all backref nodes in the cache */
	struct rb_root rb_root;
	/* For passing backref nodes to btrfs_reloc_cow_block */
	struct btrfs_backref_node *path[BTRFS_MAX_LEVEL];
	/*
	 * List of blocks that have been COWed but some block pointers in upper
	 * level blocks may not reflect the new location
	 */
	struct list_head pending[BTRFS_MAX_LEVEL];
	/* List of backref nodes with no child node */
	struct list_head leaves;
	/* List of blocks that have been COWed in current transaction */
	struct list_head changed;
	/* List of detached backref node. */
	struct list_head detached;

	u64 last_trans;

	int nr_nodes;
	int nr_edges;

	/* List of unchecked backref edges during backref cache build */
	struct list_head pending_edge;

	/* List of useless backref nodes during backref cache build */
	struct list_head useless_node;

	struct btrfs_fs_info *fs_info;

	/*
	 * Whether this cache is for relocation
	 *
	 * Reloction backref cache require more info for reloc root compared
	 * to generic backref cache.
	 */
	unsigned int is_reloc;
};

void btrfs_backref_init_cache(struct btrfs_fs_info *fs_info,
			      struct btrfs_backref_cache *cache, int is_reloc);
struct btrfs_backref_node *btrfs_backref_alloc_node(
		struct btrfs_backref_cache *cache, u64 bytenr, int level);
struct btrfs_backref_edge *btrfs_backref_alloc_edge(
		struct btrfs_backref_cache *cache);

#define		LINK_LOWER	(1 << 0)
#define		LINK_UPPER	(1 << 1)
static inline void btrfs_backref_link_edge(struct btrfs_backref_edge *edge,
					   struct btrfs_backref_node *lower,
					   struct btrfs_backref_node *upper,
					   int link_which)
{
	ASSERT(upper && lower && upper->level == lower->level + 1);
	edge->node[LOWER] = lower;
	edge->node[UPPER] = upper;
	if (link_which & LINK_LOWER)
		list_add_tail(&edge->list[LOWER], &lower->upper);
	if (link_which & LINK_UPPER)
		list_add_tail(&edge->list[UPPER], &upper->lower);
}

static inline void btrfs_backref_free_node(struct btrfs_backref_cache *cache,
					   struct btrfs_backref_node *node)
{
	if (node) {
		ASSERT(list_empty(&node->list));
		ASSERT(list_empty(&node->lower));
		ASSERT(node->eb == NULL);
		cache->nr_nodes--;
		btrfs_put_root(node->root);
		kfree(node);
	}
}

static inline void btrfs_backref_free_edge(struct btrfs_backref_cache *cache,
					   struct btrfs_backref_edge *edge)
{
	if (edge) {
		cache->nr_edges--;
		kfree(edge);
	}
}

static inline void btrfs_backref_unlock_node_buffer(
		struct btrfs_backref_node *node)
{
	if (node->locked) {
		btrfs_tree_unlock(node->eb);
		node->locked = 0;
	}
}

static inline void btrfs_backref_drop_node_buffer(
		struct btrfs_backref_node *node)
{
	if (node->eb) {
		btrfs_backref_unlock_node_buffer(node);
		free_extent_buffer(node->eb);
		node->eb = NULL;
	}
}

/*
 * Drop the backref node from cache without cleaning up its children
 * edges.
 *
 * This can only be called on node without parent edges.
 * The children edges are still kept as is.
 */
static inline void btrfs_backref_drop_node(struct btrfs_backref_cache *tree,
					   struct btrfs_backref_node *node)
{
	ASSERT(list_empty(&node->upper));

	btrfs_backref_drop_node_buffer(node);
	list_del_init(&node->list);
	list_del_init(&node->lower);
	if (!RB_EMPTY_NODE(&node->rb_node))
		rb_erase(&node->rb_node, &tree->rb_root);
	btrfs_backref_free_node(tree, node);
}

void btrfs_backref_cleanup_node(struct btrfs_backref_cache *cache,
				struct btrfs_backref_node *node);

void btrfs_backref_release_cache(struct btrfs_backref_cache *cache);

static inline void btrfs_backref_panic(struct btrfs_fs_info *fs_info,
				       u64 bytenr, int errno)
{
	btrfs_panic(fs_info, errno,
		    "Inconsistency in backref cache found at offset %llu",
		    bytenr);
}

int btrfs_backref_add_tree_node(struct btrfs_backref_cache *cache,
				struct btrfs_path *path,
				struct btrfs_backref_iter *iter,
				struct btrfs_key *node_key,
				struct btrfs_backref_node *cur);

int btrfs_backref_finish_upper_links(struct btrfs_backref_cache *cache,
				     struct btrfs_backref_node *start);

void btrfs_backref_error_cleanup(struct btrfs_backref_cache *cache,
				 struct btrfs_backref_node *node);

#endif
