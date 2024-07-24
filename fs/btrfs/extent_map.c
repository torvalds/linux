// SPDX-License-Identifier: GPL-2.0

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "ctree.h"
#include "volumes.h"
#include "extent_map.h"
#include "compression.h"
#include "btrfs_inode.h"


static struct kmem_cache *extent_map_cache;

int __init extent_map_init(void)
{
	extent_map_cache = kmem_cache_create("btrfs_extent_map",
			sizeof(struct extent_map), 0,
			SLAB_MEM_SPREAD, NULL);
	if (!extent_map_cache)
		return -ENOMEM;
	return 0;
}

void __cold extent_map_exit(void)
{
	kmem_cache_destroy(extent_map_cache);
}

/**
 * extent_map_tree_init - initialize extent map tree
 * @tree:		tree to initialize
 *
 * Initialize the extent tree @tree.  Should be called for each new inode
 * or other user of the extent_map interface.
 */
void extent_map_tree_init(struct extent_map_tree *tree)
{
	tree->map = RB_ROOT_CACHED;
	INIT_LIST_HEAD(&tree->modified_extents);
	rwlock_init(&tree->lock);
}

/**
 * alloc_extent_map - allocate new extent map structure
 *
 * Allocate a new extent_map structure.  The new structure is
 * returned with a reference count of one and needs to be
 * freed using free_extent_map()
 */
struct extent_map *alloc_extent_map(void)
{
	struct extent_map *em;
	em = kmem_cache_zalloc(extent_map_cache, GFP_NOFS);
	if (!em)
		return NULL;
	RB_CLEAR_NODE(&em->rb_node);
	em->compress_type = BTRFS_COMPRESS_NONE;
	refcount_set(&em->refs, 1);
	INIT_LIST_HEAD(&em->list);
	return em;
}

/**
 * free_extent_map - drop reference count of an extent_map
 * @em:		extent map being released
 *
 * Drops the reference out on @em by one and free the structure
 * if the reference count hits zero.
 */
void free_extent_map(struct extent_map *em)
{
	if (!em)
		return;
	if (refcount_dec_and_test(&em->refs)) {
		WARN_ON(extent_map_in_tree(em));
		WARN_ON(!list_empty(&em->list));
		if (test_bit(EXTENT_FLAG_FS_MAPPING, &em->flags))
			kfree(em->map_lookup);
		kmem_cache_free(extent_map_cache, em);
	}
}

/* simple helper to do math around the end of an extent, handling wrap */
static u64 range_end(u64 start, u64 len)
{
	if (start + len < start)
		return (u64)-1;
	return start + len;
}

static int tree_insert(struct rb_root_cached *root, struct extent_map *em)
{
	struct rb_node **p = &root->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct extent_map *entry = NULL;
	struct rb_node *orig_parent = NULL;
	u64 end = range_end(em->start, em->len);
	bool leftmost = true;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct extent_map, rb_node);

		if (em->start < entry->start) {
			p = &(*p)->rb_left;
		} else if (em->start >= extent_map_end(entry)) {
			p = &(*p)->rb_right;
			leftmost = false;
		} else {
			return -EEXIST;
		}
	}

	orig_parent = parent;
	while (parent && em->start >= extent_map_end(entry)) {
		parent = rb_next(parent);
		entry = rb_entry(parent, struct extent_map, rb_node);
	}
	if (parent)
		if (end > entry->start && em->start < extent_map_end(entry))
			return -EEXIST;

	parent = orig_parent;
	entry = rb_entry(parent, struct extent_map, rb_node);
	while (parent && em->start < entry->start) {
		parent = rb_prev(parent);
		entry = rb_entry(parent, struct extent_map, rb_node);
	}
	if (parent)
		if (end > entry->start && em->start < extent_map_end(entry))
			return -EEXIST;

	rb_link_node(&em->rb_node, orig_parent, p);
	rb_insert_color_cached(&em->rb_node, root, leftmost);
	return 0;
}

/*
 * search through the tree for an extent_map with a given offset.  If
 * it can't be found, try to find some neighboring extents
 */
static struct rb_node *__tree_search(struct rb_root *root, u64 offset,
				     struct rb_node **prev_or_next_ret)
{
	struct rb_node *n = root->rb_node;
	struct rb_node *prev = NULL;
	struct rb_node *orig_prev = NULL;
	struct extent_map *entry;
	struct extent_map *prev_entry = NULL;

	ASSERT(prev_or_next_ret);

	while (n) {
		entry = rb_entry(n, struct extent_map, rb_node);
		prev = n;
		prev_entry = entry;

		if (offset < entry->start)
			n = n->rb_left;
		else if (offset >= extent_map_end(entry))
			n = n->rb_right;
		else
			return n;
	}

	orig_prev = prev;
	while (prev && offset >= extent_map_end(prev_entry)) {
		prev = rb_next(prev);
		prev_entry = rb_entry(prev, struct extent_map, rb_node);
	}

	/*
	 * Previous extent map found, return as in this case the caller does not
	 * care about the next one.
	 */
	if (prev) {
		*prev_or_next_ret = prev;
		return NULL;
	}

	prev = orig_prev;
	prev_entry = rb_entry(prev, struct extent_map, rb_node);
	while (prev && offset < prev_entry->start) {
		prev = rb_prev(prev);
		prev_entry = rb_entry(prev, struct extent_map, rb_node);
	}
	*prev_or_next_ret = prev;

	return NULL;
}

/* check to see if two extent_map structs are adjacent and safe to merge */
static int mergable_maps(struct extent_map *prev, struct extent_map *next)
{
	if (test_bit(EXTENT_FLAG_PINNED, &prev->flags))
		return 0;

	/*
	 * don't merge compressed extents, we need to know their
	 * actual size
	 */
	if (test_bit(EXTENT_FLAG_COMPRESSED, &prev->flags))
		return 0;

	if (test_bit(EXTENT_FLAG_LOGGING, &prev->flags) ||
	    test_bit(EXTENT_FLAG_LOGGING, &next->flags))
		return 0;

	/*
	 * We don't want to merge stuff that hasn't been written to the log yet
	 * since it may not reflect exactly what is on disk, and that would be
	 * bad.
	 */
	if (!list_empty(&prev->list) || !list_empty(&next->list))
		return 0;

	ASSERT(next->block_start != EXTENT_MAP_DELALLOC &&
	       prev->block_start != EXTENT_MAP_DELALLOC);

	if (prev->map_lookup || next->map_lookup)
		ASSERT(test_bit(EXTENT_FLAG_FS_MAPPING, &prev->flags) &&
		       test_bit(EXTENT_FLAG_FS_MAPPING, &next->flags));

	if (extent_map_end(prev) == next->start &&
	    prev->flags == next->flags &&
	    prev->map_lookup == next->map_lookup &&
	    ((next->block_start == EXTENT_MAP_HOLE &&
	      prev->block_start == EXTENT_MAP_HOLE) ||
	     (next->block_start == EXTENT_MAP_INLINE &&
	      prev->block_start == EXTENT_MAP_INLINE) ||
	     (next->block_start < EXTENT_MAP_LAST_BYTE - 1 &&
	      next->block_start == extent_map_block_end(prev)))) {
		return 1;
	}
	return 0;
}

static void try_merge_map(struct extent_map_tree *tree, struct extent_map *em)
{
	struct extent_map *merge = NULL;
	struct rb_node *rb;

	/*
	 * We can't modify an extent map that is in the tree and that is being
	 * used by another task, as it can cause that other task to see it in
	 * inconsistent state during the merging. We always have 1 reference for
	 * the tree and 1 for this task (which is unpinning the extent map or
	 * clearing the logging flag), so anything > 2 means it's being used by
	 * other tasks too.
	 */
	if (refcount_read(&em->refs) > 2)
		return;

	if (em->start != 0) {
		rb = rb_prev(&em->rb_node);
		if (rb)
			merge = rb_entry(rb, struct extent_map, rb_node);
		if (rb && mergable_maps(merge, em)) {
			em->start = merge->start;
			em->orig_start = merge->orig_start;
			em->len += merge->len;
			em->block_len += merge->block_len;
			em->block_start = merge->block_start;
			em->mod_len = (em->mod_len + em->mod_start) - merge->mod_start;
			em->mod_start = merge->mod_start;
			em->generation = max(em->generation, merge->generation);
			set_bit(EXTENT_FLAG_MERGED, &em->flags);

			rb_erase_cached(&merge->rb_node, &tree->map);
			RB_CLEAR_NODE(&merge->rb_node);
			free_extent_map(merge);
		}
	}

	rb = rb_next(&em->rb_node);
	if (rb)
		merge = rb_entry(rb, struct extent_map, rb_node);
	if (rb && mergable_maps(em, merge)) {
		em->len += merge->len;
		em->block_len += merge->block_len;
		rb_erase_cached(&merge->rb_node, &tree->map);
		RB_CLEAR_NODE(&merge->rb_node);
		em->mod_len = (merge->mod_start + merge->mod_len) - em->mod_start;
		em->generation = max(em->generation, merge->generation);
		set_bit(EXTENT_FLAG_MERGED, &em->flags);
		free_extent_map(merge);
	}
}

/**
 * unpin_extent_cache - unpin an extent from the cache
 * @tree:	tree to unpin the extent in
 * @start:	logical offset in the file
 * @len:	length of the extent
 * @gen:	generation that this extent has been modified in
 *
 * Called after an extent has been written to disk properly.  Set the generation
 * to the generation that actually added the file item to the inode so we know
 * we need to sync this extent when we call fsync().
 */
int unpin_extent_cache(struct extent_map_tree *tree, u64 start, u64 len,
		       u64 gen)
{
	int ret = 0;
	struct extent_map *em;
	bool prealloc = false;

	write_lock(&tree->lock);
	em = lookup_extent_mapping(tree, start, len);

	WARN_ON(!em || em->start != start);

	if (!em)
		goto out;

	em->generation = gen;
	clear_bit(EXTENT_FLAG_PINNED, &em->flags);
	em->mod_start = em->start;
	em->mod_len = em->len;

	if (test_bit(EXTENT_FLAG_FILLING, &em->flags)) {
		prealloc = true;
		clear_bit(EXTENT_FLAG_FILLING, &em->flags);
	}

	try_merge_map(tree, em);

	if (prealloc) {
		em->mod_start = em->start;
		em->mod_len = em->len;
	}

	free_extent_map(em);
out:
	write_unlock(&tree->lock);
	return ret;

}

void clear_em_logging(struct extent_map_tree *tree, struct extent_map *em)
{
	lockdep_assert_held_write(&tree->lock);

	clear_bit(EXTENT_FLAG_LOGGING, &em->flags);
	if (extent_map_in_tree(em))
		try_merge_map(tree, em);
}

static inline void setup_extent_mapping(struct extent_map_tree *tree,
					struct extent_map *em,
					int modified)
{
	refcount_inc(&em->refs);
	em->mod_start = em->start;
	em->mod_len = em->len;

	if (modified)
		list_move(&em->list, &tree->modified_extents);
	else
		try_merge_map(tree, em);
}

static void extent_map_device_set_bits(struct extent_map *em, unsigned bits)
{
	struct map_lookup *map = em->map_lookup;
	u64 stripe_size = em->orig_block_len;
	int i;

	for (i = 0; i < map->num_stripes; i++) {
		struct btrfs_io_stripe *stripe = &map->stripes[i];
		struct btrfs_device *device = stripe->dev;

		set_extent_bits_nowait(&device->alloc_state, stripe->physical,
				 stripe->physical + stripe_size - 1, bits);
	}
}

static void extent_map_device_clear_bits(struct extent_map *em, unsigned bits)
{
	struct map_lookup *map = em->map_lookup;
	u64 stripe_size = em->orig_block_len;
	int i;

	for (i = 0; i < map->num_stripes; i++) {
		struct btrfs_io_stripe *stripe = &map->stripes[i];
		struct btrfs_device *device = stripe->dev;

		__clear_extent_bit(&device->alloc_state, stripe->physical,
				   stripe->physical + stripe_size - 1, bits,
				   NULL, GFP_NOWAIT, NULL);
	}
}

/**
 * Add new extent map to the extent tree
 *
 * @tree:	tree to insert new map in
 * @em:		map to insert
 * @modified:	indicate whether the given @em should be added to the
 *	        modified list, which indicates the extent needs to be logged
 *
 * Insert @em into @tree or perform a simple forward/backward merge with
 * existing mappings.  The extent_map struct passed in will be inserted
 * into the tree directly, with an additional reference taken, or a
 * reference dropped if the merge attempt was successful.
 */
int add_extent_mapping(struct extent_map_tree *tree,
		       struct extent_map *em, int modified)
{
	int ret = 0;

	lockdep_assert_held_write(&tree->lock);

	ret = tree_insert(&tree->map, em);
	if (ret)
		goto out;

	setup_extent_mapping(tree, em, modified);
	if (test_bit(EXTENT_FLAG_FS_MAPPING, &em->flags)) {
		extent_map_device_set_bits(em, CHUNK_ALLOCATED);
		extent_map_device_clear_bits(em, CHUNK_TRIMMED);
	}
out:
	return ret;
}

static struct extent_map *
__lookup_extent_mapping(struct extent_map_tree *tree,
			u64 start, u64 len, int strict)
{
	struct extent_map *em;
	struct rb_node *rb_node;
	struct rb_node *prev_or_next = NULL;
	u64 end = range_end(start, len);

	rb_node = __tree_search(&tree->map.rb_root, start, &prev_or_next);
	if (!rb_node) {
		if (prev_or_next)
			rb_node = prev_or_next;
		else
			return NULL;
	}

	em = rb_entry(rb_node, struct extent_map, rb_node);

	if (strict && !(end > em->start && start < extent_map_end(em)))
		return NULL;

	refcount_inc(&em->refs);
	return em;
}

/**
 * lookup_extent_mapping - lookup extent_map
 * @tree:	tree to lookup in
 * @start:	byte offset to start the search
 * @len:	length of the lookup range
 *
 * Find and return the first extent_map struct in @tree that intersects the
 * [start, len] range.  There may be additional objects in the tree that
 * intersect, so check the object returned carefully to make sure that no
 * additional lookups are needed.
 */
struct extent_map *lookup_extent_mapping(struct extent_map_tree *tree,
					 u64 start, u64 len)
{
	return __lookup_extent_mapping(tree, start, len, 1);
}

/**
 * search_extent_mapping - find a nearby extent map
 * @tree:	tree to lookup in
 * @start:	byte offset to start the search
 * @len:	length of the lookup range
 *
 * Find and return the first extent_map struct in @tree that intersects the
 * [start, len] range.
 *
 * If one can't be found, any nearby extent may be returned
 */
struct extent_map *search_extent_mapping(struct extent_map_tree *tree,
					 u64 start, u64 len)
{
	return __lookup_extent_mapping(tree, start, len, 0);
}

/**
 * remove_extent_mapping - removes an extent_map from the extent tree
 * @tree:	extent tree to remove from
 * @em:		extent map being removed
 *
 * Removes @em from @tree.  No reference counts are dropped, and no checks
 * are done to see if the range is in use
 */
void remove_extent_mapping(struct extent_map_tree *tree, struct extent_map *em)
{
	lockdep_assert_held_write(&tree->lock);

	WARN_ON(test_bit(EXTENT_FLAG_PINNED, &em->flags));
	rb_erase_cached(&em->rb_node, &tree->map);
	if (!test_bit(EXTENT_FLAG_LOGGING, &em->flags))
		list_del_init(&em->list);
	if (test_bit(EXTENT_FLAG_FS_MAPPING, &em->flags))
		extent_map_device_clear_bits(em, CHUNK_ALLOCATED);
	RB_CLEAR_NODE(&em->rb_node);
}

void replace_extent_mapping(struct extent_map_tree *tree,
			    struct extent_map *cur,
			    struct extent_map *new,
			    int modified)
{
	lockdep_assert_held_write(&tree->lock);

	WARN_ON(test_bit(EXTENT_FLAG_PINNED, &cur->flags));
	ASSERT(extent_map_in_tree(cur));
	if (!test_bit(EXTENT_FLAG_LOGGING, &cur->flags))
		list_del_init(&cur->list);
	rb_replace_node_cached(&cur->rb_node, &new->rb_node, &tree->map);
	RB_CLEAR_NODE(&cur->rb_node);

	setup_extent_mapping(tree, new, modified);
}

static struct extent_map *next_extent_map(const struct extent_map *em)
{
	struct rb_node *next;

	next = rb_next(&em->rb_node);
	if (!next)
		return NULL;
	return container_of(next, struct extent_map, rb_node);
}

/*
 * Get the extent map that immediately follows another one.
 *
 * @tree:       The extent map tree that the extent map belong to.
 *              Holding read or write access on the tree's lock is required.
 * @em:         An extent map from the given tree. The caller must ensure that
 *              between getting @em and between calling this function, the
 *              extent map @em is not removed from the tree - for example, by
 *              holding the tree's lock for the duration of those 2 operations.
 *
 * Returns the extent map that immediately follows @em, or NULL if @em is the
 * last extent map in the tree.
 */
struct extent_map *btrfs_next_extent_map(const struct extent_map_tree *tree,
					 const struct extent_map *em)
{
	struct extent_map *next;

	/* The lock must be acquired either in read mode or write mode. */
	lockdep_assert_held(&tree->lock);
	ASSERT(extent_map_in_tree(em));

	next = next_extent_map(em);
	if (next)
		refcount_inc(&next->refs);

	return next;
}

static struct extent_map *prev_extent_map(struct extent_map *em)
{
	struct rb_node *prev;

	prev = rb_prev(&em->rb_node);
	if (!prev)
		return NULL;
	return container_of(prev, struct extent_map, rb_node);
}

/*
 * Helper for btrfs_get_extent.  Given an existing extent in the tree,
 * the existing extent is the nearest extent to map_start,
 * and an extent that you want to insert, deal with overlap and insert
 * the best fitted new extent into the tree.
 */
static noinline int merge_extent_mapping(struct extent_map_tree *em_tree,
					 struct extent_map *existing,
					 struct extent_map *em,
					 u64 map_start)
{
	struct extent_map *prev;
	struct extent_map *next;
	u64 start;
	u64 end;
	u64 start_diff;

	BUG_ON(map_start < em->start || map_start >= extent_map_end(em));

	if (existing->start > map_start) {
		next = existing;
		prev = prev_extent_map(next);
	} else {
		prev = existing;
		next = next_extent_map(prev);
	}

	start = prev ? extent_map_end(prev) : em->start;
	start = max_t(u64, start, em->start);
	end = next ? next->start : extent_map_end(em);
	end = min_t(u64, end, extent_map_end(em));
	start_diff = start - em->start;
	em->start = start;
	em->len = end - start;
	if (em->block_start < EXTENT_MAP_LAST_BYTE &&
	    !test_bit(EXTENT_FLAG_COMPRESSED, &em->flags)) {
		em->block_start += start_diff;
		em->block_len = em->len;
	}
	return add_extent_mapping(em_tree, em, 0);
}

/**
 * Add extent mapping into em_tree
 *
 * @fs_info:  the filesystem
 * @em_tree:  extent tree into which we want to insert the extent mapping
 * @em_in:    extent we are inserting
 * @start:    start of the logical range btrfs_get_extent() is requesting
 * @len:      length of the logical range btrfs_get_extent() is requesting
 *
 * Note that @em_in's range may be different from [start, start+len),
 * but they must be overlapped.
 *
 * Insert @em_in into @em_tree. In case there is an overlapping range, handle
 * the -EEXIST by either:
 * a) Returning the existing extent in @em_in if @start is within the
 *    existing em.
 * b) Merge the existing extent with @em_in passed in.
 *
 * Return 0 on success, otherwise -EEXIST.
 *
 */
int btrfs_add_extent_mapping(struct btrfs_fs_info *fs_info,
			     struct extent_map_tree *em_tree,
			     struct extent_map **em_in, u64 start, u64 len)
{
	int ret;
	struct extent_map *em = *em_in;

	ret = add_extent_mapping(em_tree, em, 0);
	/* it is possible that someone inserted the extent into the tree
	 * while we had the lock dropped.  It is also possible that
	 * an overlapping map exists in the tree
	 */
	if (ret == -EEXIST) {
		struct extent_map *existing;

		ret = 0;

		existing = search_extent_mapping(em_tree, start, len);

		trace_btrfs_handle_em_exist(fs_info, existing, em, start, len);

		/*
		 * existing will always be non-NULL, since there must be
		 * extent causing the -EEXIST.
		 */
		if (start >= existing->start &&
		    start < extent_map_end(existing)) {
			free_extent_map(em);
			*em_in = existing;
			ret = 0;
		} else {
			u64 orig_start = em->start;
			u64 orig_len = em->len;

			/*
			 * The existing extent map is the one nearest to
			 * the [start, start + len) range which overlaps
			 */
			ret = merge_extent_mapping(em_tree, existing,
						   em, start);
			if (ret) {
				free_extent_map(em);
				*em_in = NULL;
				WARN_ONCE(ret,
"unexpected error %d: merge existing(start %llu len %llu) with em(start %llu len %llu)\n",
					  ret, existing->start, existing->len,
					  orig_start, orig_len);
			}
			free_extent_map(existing);
		}
	}

	ASSERT(ret == 0 || ret == -EEXIST);
	return ret;
}

/*
 * Drop all extent maps from a tree in the fastest possible way, rescheduling
 * if needed. This avoids searching the tree, from the root down to the first
 * extent map, before each deletion.
 */
static void drop_all_extent_maps_fast(struct extent_map_tree *tree)
{
	write_lock(&tree->lock);
	while (!RB_EMPTY_ROOT(&tree->map.rb_root)) {
		struct extent_map *em;
		struct rb_node *node;

		node = rb_first_cached(&tree->map);
		em = rb_entry(node, struct extent_map, rb_node);
		clear_bit(EXTENT_FLAG_PINNED, &em->flags);
		clear_bit(EXTENT_FLAG_LOGGING, &em->flags);
		remove_extent_mapping(tree, em);
		free_extent_map(em);
		cond_resched_rwlock_write(&tree->lock);
	}
	write_unlock(&tree->lock);
}

/*
 * Drop all extent maps in a given range.
 *
 * @inode:       The target inode.
 * @start:       Start offset of the range.
 * @end:         End offset of the range (inclusive value).
 * @skip_pinned: Indicate if pinned extent maps should be ignored or not.
 *
 * This drops all the extent maps that intersect the given range [@start, @end].
 * Extent maps that partially overlap the range and extend behind or beyond it,
 * are split.
 * The caller should have locked an appropriate file range in the inode's io
 * tree before calling this function.
 */
void btrfs_drop_extent_map_range(struct btrfs_inode *inode, u64 start, u64 end,
				 bool skip_pinned)
{
	struct extent_map *split;
	struct extent_map *split2;
	struct extent_map *em;
	struct extent_map_tree *em_tree = &inode->extent_tree;
	u64 len = end - start + 1;

	WARN_ON(end < start);
	if (end == (u64)-1) {
		if (start == 0 && !skip_pinned) {
			drop_all_extent_maps_fast(em_tree);
			return;
		}
		len = (u64)-1;
	} else {
		/* Make end offset exclusive for use in the loop below. */
		end++;
	}

	/*
	 * It's ok if we fail to allocate the extent maps, see the comment near
	 * the bottom of the loop below. We only need two spare extent maps in
	 * the worst case, where the first extent map that intersects our range
	 * starts before the range and the last extent map that intersects our
	 * range ends after our range (and they might be the same extent map),
	 * because we need to split those two extent maps at the boundaries.
	 */
	split = alloc_extent_map();
	split2 = alloc_extent_map();

	write_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);

	while (em) {
		/* extent_map_end() returns exclusive value (last byte + 1). */
		const u64 em_end = extent_map_end(em);
		struct extent_map *next_em = NULL;
		u64 gen;
		unsigned long flags;
		bool modified;
		bool compressed;

		if (em_end < end) {
			next_em = next_extent_map(em);
			if (next_em) {
				if (next_em->start < end)
					refcount_inc(&next_em->refs);
				else
					next_em = NULL;
			}
		}

		if (skip_pinned && test_bit(EXTENT_FLAG_PINNED, &em->flags)) {
			start = em_end;
			goto next;
		}

		flags = em->flags;
		clear_bit(EXTENT_FLAG_PINNED, &em->flags);
		/*
		 * In case we split the extent map, we want to preserve the
		 * EXTENT_FLAG_LOGGING flag on our extent map, but we don't want
		 * it on the new extent maps.
		 */
		clear_bit(EXTENT_FLAG_LOGGING, &flags);
		modified = !list_empty(&em->list);

		/*
		 * The extent map does not cross our target range, so no need to
		 * split it, we can remove it directly.
		 */
		if (em->start >= start && em_end <= end)
			goto remove_em;

		gen = em->generation;
		compressed = test_bit(EXTENT_FLAG_COMPRESSED, &em->flags);

		if (em->start < start) {
			if (!split) {
				split = split2;
				split2 = NULL;
				if (!split)
					goto remove_em;
			}
			split->start = em->start;
			split->len = start - em->start;

			if (em->block_start < EXTENT_MAP_LAST_BYTE) {
				split->orig_start = em->orig_start;
				split->block_start = em->block_start;

				if (compressed)
					split->block_len = em->block_len;
				else
					split->block_len = split->len;
				split->orig_block_len = max(split->block_len,
						em->orig_block_len);
				split->ram_bytes = em->ram_bytes;
			} else {
				split->orig_start = split->start;
				split->block_len = 0;
				split->block_start = em->block_start;
				split->orig_block_len = 0;
				split->ram_bytes = split->len;
			}

			split->generation = gen;
			split->flags = flags;
			split->compress_type = em->compress_type;
			replace_extent_mapping(em_tree, em, split, modified);
			free_extent_map(split);
			split = split2;
			split2 = NULL;
		}
		if (em_end > end) {
			if (!split) {
				split = split2;
				split2 = NULL;
				if (!split)
					goto remove_em;
			}
			split->start = end;
			split->len = em_end - end;
			split->block_start = em->block_start;
			split->flags = flags;
			split->compress_type = em->compress_type;
			split->generation = gen;

			if (em->block_start < EXTENT_MAP_LAST_BYTE) {
				split->orig_block_len = max(em->block_len,
						    em->orig_block_len);

				split->ram_bytes = em->ram_bytes;
				if (compressed) {
					split->block_len = em->block_len;
					split->orig_start = em->orig_start;
				} else {
					const u64 diff = end - em->start;

					split->block_len = split->len;
					split->block_start += diff;
					split->orig_start = em->orig_start;
				}
			} else {
				split->ram_bytes = split->len;
				split->orig_start = split->start;
				split->block_len = 0;
				split->orig_block_len = 0;
			}

			if (extent_map_in_tree(em)) {
				replace_extent_mapping(em_tree, em, split,
						       modified);
			} else {
				int ret;

				ret = add_extent_mapping(em_tree, split,
							 modified);
				/* Logic error, shouldn't happen. */
				ASSERT(ret == 0);
				if (WARN_ON(ret != 0) && modified)
					btrfs_set_inode_full_sync(inode);
			}
			free_extent_map(split);
			split = NULL;
		}
remove_em:
		if (extent_map_in_tree(em)) {
			/*
			 * If the extent map is still in the tree it means that
			 * either of the following is true:
			 *
			 * 1) It fits entirely in our range (doesn't end beyond
			 *    it or starts before it);
			 *
			 * 2) It starts before our range and/or ends after our
			 *    range, and we were not able to allocate the extent
			 *    maps for split operations, @split and @split2.
			 *
			 * If we are at case 2) then we just remove the entire
			 * extent map - this is fine since if anyone needs it to
			 * access the subranges outside our range, will just
			 * load it again from the subvolume tree's file extent
			 * item. However if the extent map was in the list of
			 * modified extents, then we must mark the inode for a
			 * full fsync, otherwise a fast fsync will miss this
			 * extent if it's new and needs to be logged.
			 */
			if ((em->start < start || em_end > end) && modified) {
				ASSERT(!split);
				btrfs_set_inode_full_sync(inode);
			}
			remove_extent_mapping(em_tree, em);
		}

		/*
		 * Once for the tree reference (we replaced or removed the
		 * extent map from the tree).
		 */
		free_extent_map(em);
next:
		/* Once for us (for our lookup reference). */
		free_extent_map(em);

		em = next_em;
	}

	write_unlock(&em_tree->lock);

	free_extent_map(split);
	free_extent_map(split2);
}

/*
 * Replace a range in the inode's extent map tree with a new extent map.
 *
 * @inode:      The target inode.
 * @new_em:     The new extent map to add to the inode's extent map tree.
 * @modified:   Indicate if the new extent map should be added to the list of
 *              modified extents (for fast fsync tracking).
 *
 * Drops all the extent maps in the inode's extent map tree that intersect the
 * range of the new extent map and adds the new extent map to the tree.
 * The caller should have locked an appropriate file range in the inode's io
 * tree before calling this function.
 */
int btrfs_replace_extent_map_range(struct btrfs_inode *inode,
				   struct extent_map *new_em,
				   bool modified)
{
	const u64 end = new_em->start + new_em->len - 1;
	struct extent_map_tree *tree = &inode->extent_tree;
	int ret;

	ASSERT(!extent_map_in_tree(new_em));

	/*
	 * The caller has locked an appropriate file range in the inode's io
	 * tree, but getting -EEXIST when adding the new extent map can still
	 * happen in case there are extents that partially cover the range, and
	 * this is due to two tasks operating on different parts of the extent.
	 * See commit 18e83ac75bfe67 ("Btrfs: fix unexpected EEXIST from
	 * btrfs_get_extent") for an example and details.
	 */
	do {
		btrfs_drop_extent_map_range(inode, new_em->start, end, false);
		write_lock(&tree->lock);
		ret = add_extent_mapping(tree, new_em, modified);
		write_unlock(&tree->lock);
	} while (ret == -EEXIST);

	return ret;
}
