/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2011 STRATO.  All rights reserved.
 */

#ifndef BTRFS_BACKREF_H
#define BTRFS_BACKREF_H

#include <linux/btrfs.h>
#include "messages.h"
#include "ulist.h"
#include "disk-io.h"
#include "extent_io.h"

/*
 * Used by implementations of iterate_extent_inodes_t (see definition below) to
 * signal that backref iteration can stop immediately and no error happened.
 * The value must be non-negative and must not be 0, 1 (which is a common return
 * value from things like btrfs_search_slot() and used internally in the backref
 * walking code) and different from BACKREF_FOUND_SHARED and
 * BACKREF_FOUND_NOT_SHARED
 */
#define BTRFS_ITERATE_EXTENT_INODES_STOP 5

/*
 * Should return 0 if no errors happened and iteration of backrefs should
 * continue. Can return BTRFS_ITERATE_EXTENT_INODES_STOP or any other non-zero
 * value to immediately stop iteration and possibly signal an error back to
 * the caller.
 */
typedef int (iterate_extent_inodes_t)(u64 inum, u64 offset, u64 num_bytes,
				      u64 root, void *ctx);

/*
 * Context and arguments for backref walking functions. Some of the fields are
 * to be filled by the caller of such functions while other are filled by the
 * functions themselves, as described below.
 */
struct btrfs_backref_walk_ctx {
	/*
	 * The address of the extent for which we are doing backref walking.
	 * Can be either a data extent or a metadata extent.
	 *
	 * Must always be set by the top level caller.
	 */
	u64 bytenr;
	/*
	 * Offset relative to the target extent. This is only used for data
	 * extents, and it's meaningful because we can have file extent items
	 * that point only to a section of a data extent ("bookend" extents),
	 * and we want to filter out any that don't point to a section of the
	 * data extent containing the given offset.
	 *
	 * Must always be set by the top level caller.
	 */
	u64 extent_item_pos;
	/*
	 * If true and bytenr corresponds to a data extent, then references from
	 * all file extent items that point to the data extent are considered,
	 * @extent_item_pos is ignored.
	 */
	bool ignore_extent_item_pos;
	/*
	 * If true and bytenr corresponds to a data extent, then the inode list
	 * (each member describing inode number, file offset and root) is not
	 * added to each reference added to the @refs ulist.
	 */
	bool skip_inode_ref_list;
	/* A valid transaction handle or NULL. */
	struct btrfs_trans_handle *trans;
	/*
	 * The file system's info object, can not be NULL.
	 *
	 * Must always be set by the top level caller.
	 */
	struct btrfs_fs_info *fs_info;
	/*
	 * Time sequence acquired from btrfs_get_tree_mod_seq(), in case the
	 * caller joined the tree mod log to get a consistent view of b+trees
	 * while we do backref walking, or BTRFS_SEQ_LAST.
	 * When using BTRFS_SEQ_LAST, delayed refs are not checked and it uses
	 * commit roots when searching b+trees - this is a special case for
	 * qgroups used during a transaction commit.
	 */
	u64 time_seq;
	/*
	 * Used to collect the bytenr of metadata extents that point to the
	 * target extent.
	 */
	struct ulist *refs;
	/*
	 * List used to collect the IDs of the roots from which the target
	 * extent is accessible. Can be NULL in case the caller does not care
	 * about collecting root IDs.
	 */
	struct ulist *roots;
	/*
	 * Used by iterate_extent_inodes() and the main backref walk code
	 * (find_parent_nodes()). Lookup and store functions for an optional
	 * cache which maps the logical address (bytenr) of leaves to an array
	 * of root IDs.
	 */
	bool (*cache_lookup)(u64 leaf_bytenr, void *user_ctx,
			     const u64 **root_ids_ret, int *root_count_ret);
	void (*cache_store)(u64 leaf_bytenr, const struct ulist *root_ids,
			    void *user_ctx);
	/*
	 * If this is not NULL, then the backref walking code will call this
	 * for each indirect data extent reference as soon as it finds one,
	 * before collecting all the remaining backrefs and before resolving
	 * indirect backrefs. This allows for the caller to terminate backref
	 * walking as soon as it finds one backref that matches some specific
	 * criteria. The @cache_lookup and @cache_store callbacks should not
	 * be NULL in order to use this callback.
	 */
	iterate_extent_inodes_t *indirect_ref_iterator;
	/*
	 * If this is not NULL, then the backref walking code will call this for
	 * each extent item it's meant to process before it actually starts
	 * processing it. If this returns anything other than 0, then it stops
	 * the backref walking code immediately.
	 */
	int (*check_extent_item)(u64 bytenr, const struct btrfs_extent_item *ei,
				 const struct extent_buffer *leaf, void *user_ctx);
	/*
	 * If this is not NULL, then the backref walking code will call this for
	 * each extent data ref it finds (BTRFS_EXTENT_DATA_REF_KEY keys) before
	 * processing that data ref. If this callback return false, then it will
	 * ignore this data ref and it will never resolve the indirect data ref,
	 * saving time searching for leaves in a fs tree with file extent items
	 * matching the data ref.
	 */
	bool (*skip_data_ref)(u64 root, u64 ino, u64 offset, void *user_ctx);
	/* Context object to pass to the callbacks defined above. */
	void *user_ctx;
};

struct inode_fs_paths {
	struct btrfs_path		*btrfs_path;
	struct btrfs_root		*fs_root;
	struct btrfs_data_container	*fspath;
};

struct btrfs_backref_shared_cache_entry {
	u64 bytenr;
	u64 gen;
	bool is_shared;
};

#define BTRFS_BACKREF_CTX_PREV_EXTENTS_SIZE 8

struct btrfs_backref_share_check_ctx {
	/* Ulists used during backref walking. */
	struct ulist refs;
	/*
	 * The current leaf the caller of btrfs_is_data_extent_shared() is at.
	 * Typically the caller (at the moment only fiemap) tries to determine
	 * the sharedness of data extents point by file extent items from entire
	 * leaves.
	 */
	u64 curr_leaf_bytenr;
	/*
	 * The previous leaf the caller was at in the previous call to
	 * btrfs_is_data_extent_shared(). This may be the same as the current
	 * leaf. On the first call it must be 0.
	 */
	u64 prev_leaf_bytenr;
	/*
	 * A path from a root to a leaf that has a file extent item pointing to
	 * a given data extent should never exceed the maximum b+tree height.
	 */
	struct btrfs_backref_shared_cache_entry path_cache_entries[BTRFS_MAX_LEVEL];
	bool use_path_cache;
	/*
	 * Cache the sharedness result for the last few extents we have found,
	 * but only for extents for which we have multiple file extent items
	 * that point to them.
	 * It's very common to have several file extent items that point to the
	 * same extent (bytenr) but with different offsets and lengths. This
	 * typically happens for COW writes, partial writes into prealloc
	 * extents, NOCOW writes after snapshoting a root, hole punching or
	 * reflinking within the same file (less common perhaps).
	 * So keep a small cache with the lookup results for the extent pointed
	 * by the last few file extent items. This cache is checked, with a
	 * linear scan, whenever btrfs_is_data_extent_shared() is called, so
	 * it must be small so that it does not negatively affect performance in
	 * case we don't have multiple file extent items that point to the same
	 * data extent.
	 */
	struct {
		u64 bytenr;
		bool is_shared;
	} prev_extents_cache[BTRFS_BACKREF_CTX_PREV_EXTENTS_SIZE];
	/*
	 * The slot in the prev_extents_cache array that will be used for
	 * storing the sharedness result of a new data extent.
	 */
	int prev_extents_cache_slot;
};

struct btrfs_backref_share_check_ctx *btrfs_alloc_backref_share_check_ctx(void);
void btrfs_free_backref_share_ctx(struct btrfs_backref_share_check_ctx *ctx);

int extent_from_logical(struct btrfs_fs_info *fs_info, u64 logical,
			struct btrfs_path *path, struct btrfs_key *found_key,
			u64 *flags);

int tree_backref_for_extent(unsigned long *ptr, struct extent_buffer *eb,
			    struct btrfs_key *key, struct btrfs_extent_item *ei,
			    u32 item_size, u64 *out_root, u8 *out_level);

int iterate_extent_inodes(struct btrfs_backref_walk_ctx *ctx,
			  bool search_commit_root,
			  iterate_extent_inodes_t *iterate, void *user_ctx);

int iterate_inodes_from_logical(u64 logical, struct btrfs_fs_info *fs_info,
				struct btrfs_path *path, void *ctx,
				bool ignore_offset);

int paths_from_inode(u64 inum, struct inode_fs_paths *ipath);

int btrfs_find_all_leafs(struct btrfs_backref_walk_ctx *ctx);
int btrfs_find_all_roots(struct btrfs_backref_walk_ctx *ctx,
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
int btrfs_is_data_extent_shared(struct btrfs_inode *inode, u64 bytenr,
				u64 extent_gen,
				struct btrfs_backref_share_check_ctx *ctx);

int __init btrfs_prelim_ref_init(void);
void __cold btrfs_prelim_ref_exit(void);

struct prelim_ref {
	struct rb_node rbnode;
	u64 root_id;
	struct btrfs_key key_for_search;
	u8 level;
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

struct btrfs_backref_iter *btrfs_backref_iter_alloc(struct btrfs_fs_info *fs_info);

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
	bool is_reloc;
};

void btrfs_backref_init_cache(struct btrfs_fs_info *fs_info,
			      struct btrfs_backref_cache *cache, bool is_reloc);
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
				       u64 bytenr, int error)
{
	btrfs_panic(fs_info, error,
		    "Inconsistency in backref cache found at offset %llu",
		    bytenr);
}

int btrfs_backref_add_tree_node(struct btrfs_trans_handle *trans,
				struct btrfs_backref_cache *cache,
				struct btrfs_path *path,
				struct btrfs_backref_iter *iter,
				struct btrfs_key *node_key,
				struct btrfs_backref_node *cur);

int btrfs_backref_finish_upper_links(struct btrfs_backref_cache *cache,
				     struct btrfs_backref_node *start);

void btrfs_backref_error_cleanup(struct btrfs_backref_cache *cache,
				 struct btrfs_backref_node *node);

#endif
