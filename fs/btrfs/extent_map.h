#ifndef __EXTENTMAP__
#define __EXTENTMAP__

#include <linux/rbtree.h>

#define EXTENT_MAP_LAST_BYTE (u64)-4
#define EXTENT_MAP_HOLE (u64)-3
#define EXTENT_MAP_INLINE (u64)-2
#define EXTENT_MAP_DELALLOC (u64)-1

/* bits for the flags field */
#define EXTENT_FLAG_PINNED 0 /* this entry not yet on disk, don't free it */
#define EXTENT_FLAG_COMPRESSED 1
#define EXTENT_FLAG_VACANCY 2 /* no file extent item found */
#define EXTENT_FLAG_PREALLOC 3 /* pre-allocated extent */

struct extent_map {
	struct rb_node rb_node;

	/* all of these are in bytes */
	u64 start;
	u64 len;
	u64 orig_start;
	u64 block_start;
	u64 block_len;
	unsigned long flags;
	struct block_device *bdev;
	atomic_t refs;
	int in_tree;
};

struct extent_map_tree {
	struct rb_root map;
	rwlock_t lock;
};

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

void extent_map_tree_init(struct extent_map_tree *tree, gfp_t mask);
struct extent_map *lookup_extent_mapping(struct extent_map_tree *tree,
					 u64 start, u64 len);
int add_extent_mapping(struct extent_map_tree *tree,
		       struct extent_map *em);
int remove_extent_mapping(struct extent_map_tree *tree, struct extent_map *em);

struct extent_map *alloc_extent_map(gfp_t mask);
void free_extent_map(struct extent_map *em);
int __init extent_map_init(void);
void extent_map_exit(void);
int unpin_extent_cache(struct extent_map_tree *tree, u64 start, u64 len);
struct extent_map *search_extent_mapping(struct extent_map_tree *tree,
					 u64 start, u64 len);
#endif
