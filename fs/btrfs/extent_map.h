/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_EXTENT_MAP_H
#define BTRFS_EXTENT_MAP_H

#include <linux/rbtree.h>
#include <linux/refcount.h>

#define EXTENT_MAP_LAST_BYTE ((u64)-4)
#define EXTENT_MAP_HOLE ((u64)-3)
#define EXTENT_MAP_INLINE ((u64)-2)
/* used only during fiemap calls */
#define EXTENT_MAP_DELALLOC ((u64)-1)

/* bits for the extent_map::flags field */
enum {
	/* this entry not yet on disk, don't free it */
	EXTENT_FLAG_PINNED,
	EXTENT_FLAG_COMPRESSED,
	/* pre-allocated extent */
	EXTENT_FLAG_PREALLOC,
	/* Logging this extent */
	EXTENT_FLAG_LOGGING,
	/* Filling in a preallocated extent */
	EXTENT_FLAG_FILLING,
	/* filesystem extent mapping type */
	EXTENT_FLAG_FS_MAPPING,
};

struct extent_map {
	struct rb_node rb_node;

	/* all of these are in bytes */
	u64 start;
	u64 len;
	u64 mod_start;
	u64 mod_len;
	u64 orig_start;
	u64 orig_block_len;
	u64 ram_bytes;
	u64 block_start;
	u64 block_len;
	u64 generation;
	unsigned long flags;
	/* Used for chunk mappings, flag EXTENT_FLAG_FS_MAPPING must be set */
	struct map_lookup *map_lookup;
	refcount_t refs;
	unsigned int compress_type;
	struct list_head list;
};

struct extent_map_tree {
	struct rb_root_cached map;
	struct list_head modified_extents;
	rwlock_t lock;
};

static inline int extent_map_in_tree(const struct extent_map *em)
{
	return !RB_EMPTY_NODE(&em->rb_node);
}

static inline u64 extent_map_end(struct extent_map *em)
{
	if (em->start + em->len < em->start)
		return (u64)-1;
	return em->start + em->len;
}

static inline u64 extent_map_block_end(struct extent_map *em)
{
	if (em->block_start + em->block_len < em->block_start)
		return (u64)-1;
	return em->block_start + em->block_len;
}

void extent_map_tree_init(struct extent_map_tree *tree);
struct extent_map *lookup_extent_mapping(struct extent_map_tree *tree,
					 u64 start, u64 len);
int add_extent_mapping(struct extent_map_tree *tree,
		       struct extent_map *em, int modified);
void remove_extent_mapping(struct extent_map_tree *tree, struct extent_map *em);
void replace_extent_mapping(struct extent_map_tree *tree,
			    struct extent_map *cur,
			    struct extent_map *new,
			    int modified);

struct extent_map *alloc_extent_map(void);
void free_extent_map(struct extent_map *em);
int __init extent_map_init(void);
void __cold extent_map_exit(void);
int unpin_extent_cache(struct extent_map_tree *tree, u64 start, u64 len, u64 gen);
void clear_em_logging(struct extent_map_tree *tree, struct extent_map *em);
struct extent_map *search_extent_mapping(struct extent_map_tree *tree,
					 u64 start, u64 len);
int btrfs_add_extent_mapping(struct btrfs_fs_info *fs_info,
			     struct extent_map_tree *em_tree,
			     struct extent_map **em_in, u64 start, u64 len);

#endif
