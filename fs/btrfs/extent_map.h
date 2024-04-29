/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_EXTENT_MAP_H
#define BTRFS_EXTENT_MAP_H

#include <linux/compiler_types.h>
#include <linux/rwlock_types.h>
#include <linux/rbtree.h>
#include <linux/list.h>
#include <linux/refcount.h>
#include "misc.h"
#include "extent_map.h"
#include "compression.h"

struct btrfs_inode;
struct btrfs_fs_info;

#define EXTENT_MAP_LAST_BYTE ((u64)-4)
#define EXTENT_MAP_HOLE ((u64)-3)
#define EXTENT_MAP_INLINE ((u64)-2)

/* bits for the extent_map::flags field */
enum {
	/* this entry not yet on disk, don't free it */
	ENUM_BIT(EXTENT_FLAG_PINNED),
	ENUM_BIT(EXTENT_FLAG_COMPRESS_ZLIB),
	ENUM_BIT(EXTENT_FLAG_COMPRESS_LZO),
	ENUM_BIT(EXTENT_FLAG_COMPRESS_ZSTD),
	/* pre-allocated extent */
	ENUM_BIT(EXTENT_FLAG_PREALLOC),
	/* Logging this extent */
	ENUM_BIT(EXTENT_FLAG_LOGGING),
	/* This em is merged from two or more physically adjacent ems */
	ENUM_BIT(EXTENT_FLAG_MERGED),
};

/*
 * This structure represents file extents and holes.
 *
 * Unlike on-disk file extent items, extent maps can be merged to save memory.
 * This means members only match file extent items before any merging.
 *
 * Keep this structure as compact as possible, as we can have really large
 * amounts of allocated extent maps at any time.
 */
struct extent_map {
	struct rb_node rb_node;

	/* All of these are in bytes. */

	/* File offset matching the offset of a BTRFS_EXTENT_ITEM_KEY key. */
	u64 start;

	/*
	 * Length of the file extent.
	 *
	 * For non-inlined file extents it's btrfs_file_extent_item::num_bytes.
	 * For inline extents it's sectorsize, since inline data starts at
	 * offsetof(struct btrfs_file_extent_item, disk_bytenr) thus
	 * btrfs_file_extent_item::num_bytes is not valid.
	 */
	u64 len;

	/*
	 * The bytenr of the full on-disk extent.
	 *
	 * For regular extents it's btrfs_file_extent_item::disk_bytenr.
	 * For holes it's EXTENT_MAP_HOLE and for inline extents it's
	 * EXTENT_MAP_INLINE.
	 */
	u64 disk_bytenr;

	/*
	 * The full on-disk extent length, matching
	 * btrfs_file_extent_item::disk_num_bytes.
	 */
	u64 disk_num_bytes;

	/*
	 * Offset inside the decompressed extent.
	 *
	 * For regular extents it's btrfs_file_extent_item::offset.
	 * For holes and inline extents it's 0.
	 */
	u64 offset;

	/*
	 * The decompressed size of the whole on-disk extent, matching
	 * btrfs_file_extent_item::ram_bytes.
	 */
	u64 ram_bytes;

	/*
	 * Generation of the extent map, for merged em it's the highest
	 * generation of all merged ems.
	 * For non-merged extents, it's from btrfs_file_extent_item::generation.
	 */
	u64 generation;
	u32 flags;
	refcount_t refs;
	struct list_head list;
};

struct extent_map_tree {
	struct rb_root root;
	struct list_head modified_extents;
	rwlock_t lock;
};

struct btrfs_inode;

static inline void extent_map_set_compression(struct extent_map *em,
					      enum btrfs_compression_type type)
{
	if (type == BTRFS_COMPRESS_ZLIB)
		em->flags |= EXTENT_FLAG_COMPRESS_ZLIB;
	else if (type == BTRFS_COMPRESS_LZO)
		em->flags |= EXTENT_FLAG_COMPRESS_LZO;
	else if (type == BTRFS_COMPRESS_ZSTD)
		em->flags |= EXTENT_FLAG_COMPRESS_ZSTD;
}

static inline enum btrfs_compression_type extent_map_compression(const struct extent_map *em)
{
	if (em->flags & EXTENT_FLAG_COMPRESS_ZLIB)
		return BTRFS_COMPRESS_ZLIB;

	if (em->flags & EXTENT_FLAG_COMPRESS_LZO)
		return BTRFS_COMPRESS_LZO;

	if (em->flags & EXTENT_FLAG_COMPRESS_ZSTD)
		return BTRFS_COMPRESS_ZSTD;

	return BTRFS_COMPRESS_NONE;
}

/*
 * More efficient way to determine if extent is compressed, instead of using
 * 'extent_map_compression() != BTRFS_COMPRESS_NONE'.
 */
static inline bool extent_map_is_compressed(const struct extent_map *em)
{
	return (em->flags & (EXTENT_FLAG_COMPRESS_ZLIB |
			     EXTENT_FLAG_COMPRESS_LZO |
			     EXTENT_FLAG_COMPRESS_ZSTD)) != 0;
}

static inline int extent_map_in_tree(const struct extent_map *em)
{
	return !RB_EMPTY_NODE(&em->rb_node);
}

static inline u64 extent_map_block_start(const struct extent_map *em)
{
	if (em->disk_bytenr < EXTENT_MAP_LAST_BYTE) {
		if (extent_map_is_compressed(em))
			return em->disk_bytenr;
		return em->disk_bytenr + em->offset;
	}
	return em->disk_bytenr;
}

static inline u64 extent_map_end(const struct extent_map *em)
{
	if (em->start + em->len < em->start)
		return (u64)-1;
	return em->start + em->len;
}

void extent_map_tree_init(struct extent_map_tree *tree);
struct extent_map *lookup_extent_mapping(struct extent_map_tree *tree,
					 u64 start, u64 len);
void remove_extent_mapping(struct btrfs_inode *inode, struct extent_map *em);
int split_extent_map(struct btrfs_inode *inode, u64 start, u64 len, u64 pre,
		     u64 new_logical);

struct extent_map *alloc_extent_map(void);
void free_extent_map(struct extent_map *em);
int __init extent_map_init(void);
void __cold extent_map_exit(void);
int unpin_extent_cache(struct btrfs_inode *inode, u64 start, u64 len, u64 gen);
void clear_em_logging(struct btrfs_inode *inode, struct extent_map *em);
struct extent_map *search_extent_mapping(struct extent_map_tree *tree,
					 u64 start, u64 len);
int btrfs_add_extent_mapping(struct btrfs_inode *inode,
			     struct extent_map **em_in, u64 start, u64 len);
void btrfs_drop_extent_map_range(struct btrfs_inode *inode,
				 u64 start, u64 end,
				 bool skip_pinned);
int btrfs_replace_extent_map_range(struct btrfs_inode *inode,
				   struct extent_map *new_em,
				   bool modified);
long btrfs_free_extent_maps(struct btrfs_fs_info *fs_info, long nr_to_scan);

#endif
