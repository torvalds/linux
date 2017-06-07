#ifndef __EXTENTMAP__
#define __EXTENTMAP__

#include <linux/rbtree.h>
#include <linux/refcount.h>

#define EXTENT_MAP_LAST_BYTE ((u64)-4)
#define EXTENT_MAP_HOLE ((u64)-3)
#define EXTENT_MAP_INLINE ((u64)-2)
#define EXTENT_MAP_DELALLOC ((u64)-1)

/* bits for the flags field */
#define EXTENT_FLAG_PINNED 0 /* this entry not yet on disk, don't free it */
#define EXTENT_FLAG_COMPRESSED 1
#define EXTENT_FLAG_VACANCY 2 /* no file extent item found */
#define EXTENT_FLAG_PREALLOC 3 /* pre-allocated extent */
#define EXTENT_FLAG_LOGGING 4 /* Logging this extent */
#define EXTENT_FLAG_FILLING 5 /* Filling in a preallocated extent */
#define EXTENT_FLAG_FS_MAPPING 6 /* filesystem extent mapping type */

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
	union {
		struct block_device *bdev;

		/*
		 * used for chunk mappings
		 * flags & EXTENT_FLAG_FS_MAPPING must be set
		 */
		struct map_lookup *map_lookup;
	};
	refcount_t refs;
	unsigned int compress_type;
	struct list_head list;
};

struct extent_map_tree {
	struct rb_root map;
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
int remove_extent_mapping(struct extent_map_tree *tree, struct extent_map *em);
void replace_extent_mapping(struct extent_map_tree *tree,
			    struct extent_map *cur,
			    struct extent_map *new,
			    int modified);

struct extent_map *alloc_extent_map(void);
void free_extent_map(struct extent_map *em);
int __init extent_map_init(void);
void extent_map_exit(void);
int unpin_extent_cache(struct extent_map_tree *tree, u64 start, u64 len, u64 gen);
void clear_em_logging(struct extent_map_tree *tree, struct extent_map *em);
struct extent_map *search_extent_mapping(struct extent_map_tree *tree,
					 u64 start, u64 len);
#endif
