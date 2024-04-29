// SPDX-License-Identifier: GPL-2.0

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "messages.h"
#include "ctree.h"
#include "extent_map.h"
#include "compression.h"
#include "btrfs_inode.h"
#include "disk-io.h"


static struct kmem_cache *extent_map_cache;

int __init extent_map_init(void)
{
	extent_map_cache = kmem_cache_create("btrfs_extent_map",
					     sizeof(struct extent_map), 0, 0, NULL);
	if (!extent_map_cache)
		return -ENOMEM;
	return 0;
}

void __cold extent_map_exit(void)
{
	kmem_cache_destroy(extent_map_cache);
}

/*
 * Initialize the extent tree @tree.  Should be called for each new inode or
 * other user of the extent_map interface.
 */
void extent_map_tree_init(struct extent_map_tree *tree)
{
	tree->root = RB_ROOT;
	INIT_LIST_HEAD(&tree->modified_extents);
	rwlock_init(&tree->lock);
}

/*
 * Allocate a new extent_map structure.  The new structure is returned with a
 * reference count of one and needs to be freed using free_extent_map()
 */
struct extent_map *alloc_extent_map(void)
{
	struct extent_map *em;
	em = kmem_cache_zalloc(extent_map_cache, GFP_NOFS);
	if (!em)
		return NULL;
	RB_CLEAR_NODE(&em->rb_node);
	refcount_set(&em->refs, 1);
	INIT_LIST_HEAD(&em->list);
	return em;
}

/*
 * Drop the reference out on @em by one and free the structure if the reference
 * count hits zero.
 */
void free_extent_map(struct extent_map *em)
{
	if (!em)
		return;
	if (refcount_dec_and_test(&em->refs)) {
		WARN_ON(extent_map_in_tree(em));
		WARN_ON(!list_empty(&em->list));
		kmem_cache_free(extent_map_cache, em);
	}
}

/* Do the math around the end of an extent, handling wrapping. */
static u64 range_end(u64 start, u64 len)
{
	if (start + len < start)
		return (u64)-1;
	return start + len;
}

static void dec_evictable_extent_maps(struct btrfs_inode *inode)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;

	if (!btrfs_is_testing(fs_info) && is_fstree(btrfs_root_id(inode->root)))
		percpu_counter_dec(&fs_info->evictable_extent_maps);
}

static int tree_insert(struct rb_root *root, struct extent_map *em)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct extent_map *entry = NULL;
	struct rb_node *orig_parent = NULL;
	u64 end = range_end(em->start, em->len);

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct extent_map, rb_node);

		if (em->start < entry->start)
			p = &(*p)->rb_left;
		else if (em->start >= extent_map_end(entry))
			p = &(*p)->rb_right;
		else
			return -EEXIST;
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
	rb_insert_color(&em->rb_node, root);
	return 0;
}

/*
 * Search through the tree for an extent_map with a given offset.  If it can't
 * be found, try to find some neighboring extents
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

static inline u64 extent_map_block_len(const struct extent_map *em)
{
	if (extent_map_is_compressed(em))
		return em->disk_num_bytes;
	return em->len;
}

static inline u64 extent_map_block_end(const struct extent_map *em)
{
	if (extent_map_block_start(em) + extent_map_block_len(em) <
	    extent_map_block_start(em))
		return (u64)-1;
	return extent_map_block_start(em) + extent_map_block_len(em);
}

static bool can_merge_extent_map(const struct extent_map *em)
{
	if (em->flags & EXTENT_FLAG_PINNED)
		return false;

	/* Don't merge compressed extents, we need to know their actual size. */
	if (extent_map_is_compressed(em))
		return false;

	if (em->flags & EXTENT_FLAG_LOGGING)
		return false;

	/*
	 * We don't want to merge stuff that hasn't been written to the log yet
	 * since it may not reflect exactly what is on disk, and that would be
	 * bad.
	 */
	if (!list_empty(&em->list))
		return false;

	return true;
}

/* Check to see if two extent_map structs are adjacent and safe to merge. */
static bool mergeable_maps(const struct extent_map *prev, const struct extent_map *next)
{
	if (extent_map_end(prev) != next->start)
		return false;

	if (prev->flags != next->flags)
		return false;

	if (next->disk_bytenr < EXTENT_MAP_LAST_BYTE - 1)
		return extent_map_block_start(next) == extent_map_block_end(prev);

	/* HOLES and INLINE extents. */
	return next->disk_bytenr == prev->disk_bytenr;
}

/*
 * Handle the on-disk data extents merge for @prev and @next.
 *
 * Only touches disk_bytenr/disk_num_bytes/offset/ram_bytes.
 * For now only uncompressed regular extent can be merged.
 *
 * @prev and @next will be both updated to point to the new merged range.
 * Thus one of them should be removed by the caller.
 */
static void merge_ondisk_extents(struct extent_map *prev, struct extent_map *next)
{
	u64 new_disk_bytenr;
	u64 new_disk_num_bytes;
	u64 new_offset;

	/* @prev and @next should not be compressed. */
	ASSERT(!extent_map_is_compressed(prev));
	ASSERT(!extent_map_is_compressed(next));

	/*
	 * There are two different cases where @prev and @next can be merged.
	 *
	 * 1) They are referring to the same data extent:
	 *
	 * |<----- data extent A ----->|
	 *    |<- prev ->|<- next ->|
	 *
	 * 2) They are referring to different data extents but still adjacent:
	 *
	 * |<-- data extent A -->|<-- data extent B -->|
	 *            |<- prev ->|<- next ->|
	 *
	 * The calculation here always merges the data extents first, then updates
	 * @offset using the new data extents.
	 *
	 * For case 1), the merged data extent would be the same.
	 * For case 2), we just merge the two data extents into one.
	 */
	new_disk_bytenr = min(prev->disk_bytenr, next->disk_bytenr);
	new_disk_num_bytes = max(prev->disk_bytenr + prev->disk_num_bytes,
				 next->disk_bytenr + next->disk_num_bytes) -
			     new_disk_bytenr;
	new_offset = prev->disk_bytenr + prev->offset - new_disk_bytenr;

	prev->disk_bytenr = new_disk_bytenr;
	prev->disk_num_bytes = new_disk_num_bytes;
	prev->ram_bytes = new_disk_num_bytes;
	prev->offset = new_offset;

	next->disk_bytenr = new_disk_bytenr;
	next->disk_num_bytes = new_disk_num_bytes;
	next->ram_bytes = new_disk_num_bytes;
	next->offset = new_offset;
}

static void dump_extent_map(struct btrfs_fs_info *fs_info, const char *prefix,
			    struct extent_map *em)
{
	if (!IS_ENABLED(CONFIG_BTRFS_DEBUG))
		return;
	btrfs_crit(fs_info,
"%s, start=%llu len=%llu disk_bytenr=%llu disk_num_bytes=%llu ram_bytes=%llu offset=%llu flags=0x%x",
		prefix, em->start, em->len, em->disk_bytenr, em->disk_num_bytes,
		em->ram_bytes, em->offset, em->flags);
	ASSERT(0);
}

/* Internal sanity checks for btrfs debug builds. */
static void validate_extent_map(struct btrfs_fs_info *fs_info, struct extent_map *em)
{
	if (!IS_ENABLED(CONFIG_BTRFS_DEBUG))
		return;
	if (em->disk_bytenr < EXTENT_MAP_LAST_BYTE) {
		if (em->disk_num_bytes == 0)
			dump_extent_map(fs_info, "zero disk_num_bytes", em);
		if (em->offset + em->len > em->ram_bytes)
			dump_extent_map(fs_info, "ram_bytes too small", em);
		if (em->offset + em->len > em->disk_num_bytes &&
		    !extent_map_is_compressed(em))
			dump_extent_map(fs_info, "disk_num_bytes too small", em);
	} else if (em->offset) {
		dump_extent_map(fs_info, "non-zero offset for hole/inline", em);
	}
}

static void try_merge_map(struct btrfs_inode *inode, struct extent_map *em)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct extent_map_tree *tree = &inode->extent_tree;
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

	if (!can_merge_extent_map(em))
		return;

	if (em->start != 0) {
		rb = rb_prev(&em->rb_node);
		if (rb)
			merge = rb_entry(rb, struct extent_map, rb_node);
		if (rb && can_merge_extent_map(merge) && mergeable_maps(merge, em)) {
			em->start = merge->start;
			em->len += merge->len;
			em->generation = max(em->generation, merge->generation);

			if (em->disk_bytenr < EXTENT_MAP_LAST_BYTE)
				merge_ondisk_extents(merge, em);
			em->flags |= EXTENT_FLAG_MERGED;

			validate_extent_map(fs_info, em);
			rb_erase(&merge->rb_node, &tree->root);
			RB_CLEAR_NODE(&merge->rb_node);
			free_extent_map(merge);
			dec_evictable_extent_maps(inode);
		}
	}

	rb = rb_next(&em->rb_node);
	if (rb)
		merge = rb_entry(rb, struct extent_map, rb_node);
	if (rb && can_merge_extent_map(merge) && mergeable_maps(em, merge)) {
		em->len += merge->len;
		if (em->disk_bytenr < EXTENT_MAP_LAST_BYTE)
			merge_ondisk_extents(em, merge);
		validate_extent_map(fs_info, em);
		rb_erase(&merge->rb_node, &tree->root);
		RB_CLEAR_NODE(&merge->rb_node);
		em->generation = max(em->generation, merge->generation);
		em->flags |= EXTENT_FLAG_MERGED;
		free_extent_map(merge);
		dec_evictable_extent_maps(inode);
	}
}

/*
 * Unpin an extent from the cache.
 *
 * @inode:	the inode from which we are unpinning an extent range
 * @start:	logical offset in the file
 * @len:	length of the extent
 * @gen:	generation that this extent has been modified in
 *
 * Called after an extent has been written to disk properly.  Set the generation
 * to the generation that actually added the file item to the inode so we know
 * we need to sync this extent when we call fsync().
 *
 * Returns: 0	     on success
 * 	    -ENOENT  when the extent is not found in the tree
 * 	    -EUCLEAN if the found extent does not match the expected start
 */
int unpin_extent_cache(struct btrfs_inode *inode, u64 start, u64 len, u64 gen)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct extent_map_tree *tree = &inode->extent_tree;
	int ret = 0;
	struct extent_map *em;

	write_lock(&tree->lock);
	em = lookup_extent_mapping(tree, start, len);

	if (WARN_ON(!em)) {
		btrfs_warn(fs_info,
"no extent map found for inode %llu (root %lld) when unpinning extent range [%llu, %llu), generation %llu",
			   btrfs_ino(inode), btrfs_root_id(inode->root),
			   start, start + len, gen);
		ret = -ENOENT;
		goto out;
	}

	if (WARN_ON(em->start != start)) {
		btrfs_warn(fs_info,
"found extent map for inode %llu (root %lld) with unexpected start offset %llu when unpinning extent range [%llu, %llu), generation %llu",
			   btrfs_ino(inode), btrfs_root_id(inode->root),
			   em->start, start, start + len, gen);
		ret = -EUCLEAN;
		goto out;
	}

	em->generation = gen;
	em->flags &= ~EXTENT_FLAG_PINNED;

	try_merge_map(inode, em);

out:
	write_unlock(&tree->lock);
	free_extent_map(em);
	return ret;

}

void clear_em_logging(struct btrfs_inode *inode, struct extent_map *em)
{
	lockdep_assert_held_write(&inode->extent_tree.lock);

	em->flags &= ~EXTENT_FLAG_LOGGING;
	if (extent_map_in_tree(em))
		try_merge_map(inode, em);
}

static inline void setup_extent_mapping(struct btrfs_inode *inode,
					struct extent_map *em,
					int modified)
{
	refcount_inc(&em->refs);

	ASSERT(list_empty(&em->list));

	if (modified)
		list_add(&em->list, &inode->extent_tree.modified_extents);
	else
		try_merge_map(inode, em);
}

/*
 * Add a new extent map to an inode's extent map tree.
 *
 * @inode:	the target inode
 * @em:		map to insert
 * @modified:	indicate whether the given @em should be added to the
 *	        modified list, which indicates the extent needs to be logged
 *
 * Insert @em into the @inode's extent map tree or perform a simple
 * forward/backward merge with existing mappings.  The extent_map struct passed
 * in will be inserted into the tree directly, with an additional reference
 * taken, or a reference dropped if the merge attempt was successful.
 */
static int add_extent_mapping(struct btrfs_inode *inode,
			      struct extent_map *em, int modified)
{
	struct extent_map_tree *tree = &inode->extent_tree;
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;

	lockdep_assert_held_write(&tree->lock);

	validate_extent_map(fs_info, em);
	ret = tree_insert(&tree->root, em);
	if (ret)
		return ret;

	setup_extent_mapping(inode, em, modified);

	if (!btrfs_is_testing(fs_info) && is_fstree(btrfs_root_id(root)))
		percpu_counter_inc(&fs_info->evictable_extent_maps);

	return 0;
}

static struct extent_map *
__lookup_extent_mapping(struct extent_map_tree *tree,
			u64 start, u64 len, int strict)
{
	struct extent_map *em;
	struct rb_node *rb_node;
	struct rb_node *prev_or_next = NULL;
	u64 end = range_end(start, len);

	rb_node = __tree_search(&tree->root, start, &prev_or_next);
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

/*
 * Lookup extent_map that intersects @start + @len range.
 *
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

/*
 * Find a nearby extent map intersecting @start + @len (not an exact search).
 *
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

/*
 * Remove an extent_map from its inode's extent tree.
 *
 * @inode:	the inode the extent map belongs to
 * @em:		extent map being removed
 *
 * Remove @em from the extent tree of @inode.  No reference counts are dropped,
 * and no checks are done to see if the range is in use.
 */
void remove_extent_mapping(struct btrfs_inode *inode, struct extent_map *em)
{
	struct extent_map_tree *tree = &inode->extent_tree;

	lockdep_assert_held_write(&tree->lock);

	WARN_ON(em->flags & EXTENT_FLAG_PINNED);
	rb_erase(&em->rb_node, &tree->root);
	if (!(em->flags & EXTENT_FLAG_LOGGING))
		list_del_init(&em->list);
	RB_CLEAR_NODE(&em->rb_node);

	dec_evictable_extent_maps(inode);
}

static void replace_extent_mapping(struct btrfs_inode *inode,
				   struct extent_map *cur,
				   struct extent_map *new,
				   int modified)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct extent_map_tree *tree = &inode->extent_tree;

	lockdep_assert_held_write(&tree->lock);

	validate_extent_map(fs_info, new);

	WARN_ON(cur->flags & EXTENT_FLAG_PINNED);
	ASSERT(extent_map_in_tree(cur));
	if (!(cur->flags & EXTENT_FLAG_LOGGING))
		list_del_init(&cur->list);
	rb_replace_node(&cur->rb_node, &new->rb_node, &tree->root);
	RB_CLEAR_NODE(&cur->rb_node);

	setup_extent_mapping(inode, new, modified);
}

static struct extent_map *next_extent_map(const struct extent_map *em)
{
	struct rb_node *next;

	next = rb_next(&em->rb_node);
	if (!next)
		return NULL;
	return container_of(next, struct extent_map, rb_node);
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
static noinline int merge_extent_mapping(struct btrfs_inode *inode,
					 struct extent_map *existing,
					 struct extent_map *em,
					 u64 map_start)
{
	struct extent_map *prev;
	struct extent_map *next;
	u64 start;
	u64 end;
	u64 start_diff;

	if (map_start < em->start || map_start >= extent_map_end(em))
		return -EINVAL;

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
	if (em->disk_bytenr < EXTENT_MAP_LAST_BYTE && !extent_map_is_compressed(em))
		em->offset += start_diff;
	return add_extent_mapping(inode, em, 0);
}

/*
 * Add extent mapping into an inode's extent map tree.
 *
 * @inode:    target inode
 * @em_in:    extent we are inserting
 * @start:    start of the logical range btrfs_get_extent() is requesting
 * @len:      length of the logical range btrfs_get_extent() is requesting
 *
 * Note that @em_in's range may be different from [start, start+len),
 * but they must be overlapped.
 *
 * Insert @em_in into the inode's extent map tree. In case there is an
 * overlapping range, handle the -EEXIST by either:
 * a) Returning the existing extent in @em_in if @start is within the
 *    existing em.
 * b) Merge the existing extent with @em_in passed in.
 *
 * Return 0 on success, otherwise -EEXIST.
 *
 */
int btrfs_add_extent_mapping(struct btrfs_inode *inode,
			     struct extent_map **em_in, u64 start, u64 len)
{
	int ret;
	struct extent_map *em = *em_in;
	struct btrfs_fs_info *fs_info = inode->root->fs_info;

	/*
	 * Tree-checker should have rejected any inline extent with non-zero
	 * file offset. Here just do a sanity check.
	 */
	if (em->disk_bytenr == EXTENT_MAP_INLINE)
		ASSERT(em->start == 0);

	ret = add_extent_mapping(inode, em, 0);
	/* it is possible that someone inserted the extent into the tree
	 * while we had the lock dropped.  It is also possible that
	 * an overlapping map exists in the tree
	 */
	if (ret == -EEXIST) {
		struct extent_map *existing;

		existing = search_extent_mapping(&inode->extent_tree, start, len);

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
			ret = merge_extent_mapping(inode, existing, em, start);
			if (WARN_ON(ret)) {
				free_extent_map(em);
				*em_in = NULL;
				btrfs_warn(fs_info,
"extent map merge error existing [%llu, %llu) with em [%llu, %llu) start %llu",
					   existing->start, extent_map_end(existing),
					   orig_start, orig_start + orig_len, start);
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
static void drop_all_extent_maps_fast(struct btrfs_inode *inode)
{
	struct extent_map_tree *tree = &inode->extent_tree;
	struct rb_node *node;

	write_lock(&tree->lock);
	node = rb_first(&tree->root);
	while (node) {
		struct extent_map *em;
		struct rb_node *next = rb_next(node);

		em = rb_entry(node, struct extent_map, rb_node);
		em->flags &= ~(EXTENT_FLAG_PINNED | EXTENT_FLAG_LOGGING);
		remove_extent_mapping(inode, em);
		free_extent_map(em);

		if (cond_resched_rwlock_write(&tree->lock))
			node = rb_first(&tree->root);
		else
			node = next;
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
			drop_all_extent_maps_fast(inode);
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

		if (em_end < end) {
			next_em = next_extent_map(em);
			if (next_em) {
				if (next_em->start < end)
					refcount_inc(&next_em->refs);
				else
					next_em = NULL;
			}
		}

		if (skip_pinned && (em->flags & EXTENT_FLAG_PINNED)) {
			start = em_end;
			goto next;
		}

		flags = em->flags;
		/*
		 * In case we split the extent map, we want to preserve the
		 * EXTENT_FLAG_LOGGING flag on our extent map, but we don't want
		 * it on the new extent maps.
		 */
		em->flags &= ~(EXTENT_FLAG_PINNED | EXTENT_FLAG_LOGGING);
		modified = !list_empty(&em->list);

		/*
		 * The extent map does not cross our target range, so no need to
		 * split it, we can remove it directly.
		 */
		if (em->start >= start && em_end <= end)
			goto remove_em;

		gen = em->generation;

		if (em->start < start) {
			if (!split) {
				split = split2;
				split2 = NULL;
				if (!split)
					goto remove_em;
			}
			split->start = em->start;
			split->len = start - em->start;

			if (em->disk_bytenr < EXTENT_MAP_LAST_BYTE) {
				split->disk_bytenr = em->disk_bytenr;
				split->disk_num_bytes = em->disk_num_bytes;
				split->offset = em->offset;
				split->ram_bytes = em->ram_bytes;
			} else {
				split->disk_bytenr = em->disk_bytenr;
				split->disk_num_bytes = 0;
				split->offset = 0;
				split->ram_bytes = split->len;
			}

			split->generation = gen;
			split->flags = flags;
			replace_extent_mapping(inode, em, split, modified);
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
			split->disk_bytenr = em->disk_bytenr;
			split->flags = flags;
			split->generation = gen;

			if (em->disk_bytenr < EXTENT_MAP_LAST_BYTE) {
				split->disk_num_bytes = em->disk_num_bytes;
				split->offset = em->offset + end - em->start;
				split->ram_bytes = em->ram_bytes;
			} else {
				split->disk_num_bytes = 0;
				split->offset = 0;
				split->ram_bytes = split->len;
			}

			if (extent_map_in_tree(em)) {
				replace_extent_mapping(inode, em, split, modified);
			} else {
				int ret;

				ret = add_extent_mapping(inode, split, modified);
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
			remove_extent_mapping(inode, em);
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
		ret = add_extent_mapping(inode, new_em, modified);
		write_unlock(&tree->lock);
	} while (ret == -EEXIST);

	return ret;
}

/*
 * Split off the first pre bytes from the extent_map at [start, start + len],
 * and set the block_start for it to new_logical.
 *
 * This function is used when an ordered_extent needs to be split.
 */
int split_extent_map(struct btrfs_inode *inode, u64 start, u64 len, u64 pre,
		     u64 new_logical)
{
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_map *em;
	struct extent_map *split_pre = NULL;
	struct extent_map *split_mid = NULL;
	int ret = 0;
	unsigned long flags;

	ASSERT(pre != 0);
	ASSERT(pre < len);

	split_pre = alloc_extent_map();
	if (!split_pre)
		return -ENOMEM;
	split_mid = alloc_extent_map();
	if (!split_mid) {
		ret = -ENOMEM;
		goto out_free_pre;
	}

	lock_extent(&inode->io_tree, start, start + len - 1, NULL);
	write_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
	if (!em) {
		ret = -EIO;
		goto out_unlock;
	}

	ASSERT(em->len == len);
	ASSERT(!extent_map_is_compressed(em));
	ASSERT(em->disk_bytenr < EXTENT_MAP_LAST_BYTE);
	ASSERT(em->flags & EXTENT_FLAG_PINNED);
	ASSERT(!(em->flags & EXTENT_FLAG_LOGGING));
	ASSERT(!list_empty(&em->list));

	flags = em->flags;
	em->flags &= ~EXTENT_FLAG_PINNED;

	/* First, replace the em with a new extent_map starting from * em->start */
	split_pre->start = em->start;
	split_pre->len = pre;
	split_pre->disk_bytenr = new_logical;
	split_pre->disk_num_bytes = split_pre->len;
	split_pre->offset = 0;
	split_pre->ram_bytes = split_pre->len;
	split_pre->flags = flags;
	split_pre->generation = em->generation;

	replace_extent_mapping(inode, em, split_pre, 1);

	/*
	 * Now we only have an extent_map at:
	 *     [em->start, em->start + pre]
	 */

	/* Insert the middle extent_map. */
	split_mid->start = em->start + pre;
	split_mid->len = em->len - pre;
	split_mid->disk_bytenr = extent_map_block_start(em) + pre;
	split_mid->disk_num_bytes = split_mid->len;
	split_mid->offset = 0;
	split_mid->ram_bytes = split_mid->len;
	split_mid->flags = flags;
	split_mid->generation = em->generation;
	add_extent_mapping(inode, split_mid, 1);

	/* Once for us */
	free_extent_map(em);
	/* Once for the tree */
	free_extent_map(em);

out_unlock:
	write_unlock(&em_tree->lock);
	unlock_extent(&inode->io_tree, start, start + len - 1, NULL);
	free_extent_map(split_mid);
out_free_pre:
	free_extent_map(split_pre);
	return ret;
}

static long btrfs_scan_inode(struct btrfs_inode *inode, long *scanned, long nr_to_scan)
{
	const u64 cur_fs_gen = btrfs_get_fs_generation(inode->root->fs_info);
	struct extent_map_tree *tree = &inode->extent_tree;
	long nr_dropped = 0;
	struct rb_node *node;

	/*
	 * Take the mmap lock so that we serialize with the inode logging phase
	 * of fsync because we may need to set the full sync flag on the inode,
	 * in case we have to remove extent maps in the tree's list of modified
	 * extents. If we set the full sync flag in the inode while an fsync is
	 * in progress, we may risk missing new extents because before the flag
	 * is set, fsync decides to only wait for writeback to complete and then
	 * during inode logging it sees the flag set and uses the subvolume tree
	 * to find new extents, which may not be there yet because ordered
	 * extents haven't completed yet.
	 *
	 * We also do a try lock because otherwise we could deadlock. This is
	 * because the shrinker for this filesystem may be invoked while we are
	 * in a path that is holding the mmap lock in write mode. For example in
	 * a reflink operation while COWing an extent buffer, when allocating
	 * pages for a new extent buffer and under memory pressure, the shrinker
	 * may be invoked, and therefore we would deadlock by attempting to read
	 * lock the mmap lock while we are holding already a write lock on it.
	 */
	if (!down_read_trylock(&inode->i_mmap_lock))
		return 0;

	write_lock(&tree->lock);
	node = rb_first(&tree->root);
	while (node) {
		struct rb_node *next = rb_next(node);
		struct extent_map *em;

		em = rb_entry(node, struct extent_map, rb_node);
		(*scanned)++;

		if (em->flags & EXTENT_FLAG_PINNED)
			goto next;

		/*
		 * If the inode is in the list of modified extents (new) and its
		 * generation is the same (or is greater than) the current fs
		 * generation, it means it was not yet persisted so we have to
		 * set the full sync flag so that the next fsync will not miss
		 * it.
		 */
		if (!list_empty(&em->list) && em->generation >= cur_fs_gen)
			btrfs_set_inode_full_sync(inode);

		remove_extent_mapping(inode, em);
		trace_btrfs_extent_map_shrinker_remove_em(inode, em);
		/* Drop the reference for the tree. */
		free_extent_map(em);
		nr_dropped++;
next:
		if (*scanned >= nr_to_scan)
			break;

		/*
		 * Restart if we had to reschedule, and any extent maps that were
		 * pinned before may have become unpinned after we released the
		 * lock and took it again.
		 */
		if (cond_resched_rwlock_write(&tree->lock))
			node = rb_first(&tree->root);
		else
			node = next;
	}
	write_unlock(&tree->lock);
	up_read(&inode->i_mmap_lock);

	return nr_dropped;
}

static long btrfs_scan_root(struct btrfs_root *root, long *scanned, long nr_to_scan)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_inode *inode;
	long nr_dropped = 0;
	u64 min_ino = fs_info->extent_map_shrinker_last_ino + 1;

	inode = btrfs_find_first_inode(root, min_ino);
	while (inode) {
		nr_dropped += btrfs_scan_inode(inode, scanned, nr_to_scan);

		min_ino = btrfs_ino(inode) + 1;
		fs_info->extent_map_shrinker_last_ino = btrfs_ino(inode);
		iput(&inode->vfs_inode);

		if (*scanned >= nr_to_scan)
			break;

		cond_resched();
		inode = btrfs_find_first_inode(root, min_ino);
	}

	if (inode) {
		/*
		 * There are still inodes in this root or we happened to process
		 * the last one and reached the scan limit. In either case set
		 * the current root to this one, so we'll resume from the next
		 * inode if there is one or we will find out this was the last
		 * one and move to the next root.
		 */
		fs_info->extent_map_shrinker_last_root = btrfs_root_id(root);
	} else {
		/*
		 * No more inodes in this root, set extent_map_shrinker_last_ino to 0 so
		 * that when processing the next root we start from its first inode.
		 */
		fs_info->extent_map_shrinker_last_ino = 0;
		fs_info->extent_map_shrinker_last_root = btrfs_root_id(root) + 1;
	}

	return nr_dropped;
}

long btrfs_free_extent_maps(struct btrfs_fs_info *fs_info, long nr_to_scan)
{
	const u64 start_root_id = fs_info->extent_map_shrinker_last_root;
	u64 next_root_id = start_root_id;
	bool cycled = false;
	long nr_dropped = 0;
	long scanned = 0;

	if (trace_btrfs_extent_map_shrinker_scan_enter_enabled()) {
		s64 nr = percpu_counter_sum_positive(&fs_info->evictable_extent_maps);

		trace_btrfs_extent_map_shrinker_scan_enter(fs_info, nr_to_scan, nr);
	}

	while (scanned < nr_to_scan) {
		struct btrfs_root *root;
		unsigned long count;

		spin_lock(&fs_info->fs_roots_radix_lock);
		count = radix_tree_gang_lookup(&fs_info->fs_roots_radix,
					       (void **)&root,
					       (unsigned long)next_root_id, 1);
		if (count == 0) {
			spin_unlock(&fs_info->fs_roots_radix_lock);
			if (start_root_id > 0 && !cycled) {
				next_root_id = 0;
				fs_info->extent_map_shrinker_last_root = 0;
				fs_info->extent_map_shrinker_last_ino = 0;
				cycled = true;
				continue;
			}
			break;
		}
		next_root_id = btrfs_root_id(root) + 1;
		root = btrfs_grab_root(root);
		spin_unlock(&fs_info->fs_roots_radix_lock);

		if (!root)
			continue;

		if (is_fstree(btrfs_root_id(root)))
			nr_dropped += btrfs_scan_root(root, &scanned, nr_to_scan);

		btrfs_put_root(root);
	}

	if (trace_btrfs_extent_map_shrinker_scan_exit_enabled()) {
		s64 nr = percpu_counter_sum_positive(&fs_info->evictable_extent_maps);

		trace_btrfs_extent_map_shrinker_scan_exit(fs_info, nr_dropped, nr);
	}

	return nr_dropped;
}
