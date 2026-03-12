/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __GPU_BUDDY_H__
#define __GPU_BUDDY_H__

#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/rbtree.h>

/**
 * GPU_BUDDY_RANGE_ALLOCATION - Allocate within a specific address range
 *
 * When set, allocation is restricted to the range [start, end) specified
 * in gpu_buddy_alloc_blocks(). Without this flag, start/end are ignored
 * and allocation can use any free space.
 */
#define GPU_BUDDY_RANGE_ALLOCATION		BIT(0)

/**
 * GPU_BUDDY_TOPDOWN_ALLOCATION - Allocate from top of address space
 *
 * Allocate starting from high addresses and working down. Useful for
 * separating different allocation types (e.g., kernel vs userspace)
 * to reduce fragmentation.
 */
#define GPU_BUDDY_TOPDOWN_ALLOCATION		BIT(1)

/**
 * GPU_BUDDY_CONTIGUOUS_ALLOCATION - Require physically contiguous blocks
 *
 * The allocation must be satisfied with a single contiguous block.
 * If the requested size cannot be allocated contiguously, the
 * allocation fails with -ENOSPC.
 */
#define GPU_BUDDY_CONTIGUOUS_ALLOCATION		BIT(2)

/**
 * GPU_BUDDY_CLEAR_ALLOCATION - Prefer pre-cleared (zeroed) memory
 *
 * Attempt to allocate from the clear tree first. If insufficient clear
 * memory is available, falls back to dirty memory. Useful when the
 * caller needs zeroed memory and wants to avoid GPU clear operations.
 */
#define GPU_BUDDY_CLEAR_ALLOCATION		BIT(3)

/**
 * GPU_BUDDY_CLEARED - Mark returned blocks as cleared
 *
 * Used with gpu_buddy_free_list() to indicate that the memory being
 * freed has been cleared (zeroed). The blocks will be placed in the
 * clear tree for future GPU_BUDDY_CLEAR_ALLOCATION requests.
 */
#define GPU_BUDDY_CLEARED			BIT(4)

/**
 * GPU_BUDDY_TRIM_DISABLE - Disable automatic block trimming
 *
 * By default, if an allocation is smaller than the allocated block,
 * excess memory is trimmed and returned to the free pool. This flag
 * disables trimming, keeping the full power-of-two block size.
 */
#define GPU_BUDDY_TRIM_DISABLE			BIT(5)

enum gpu_buddy_free_tree {
	GPU_BUDDY_CLEAR_TREE = 0,
	GPU_BUDDY_DIRTY_TREE,
	GPU_BUDDY_MAX_FREE_TREES,
};

#define for_each_free_tree(tree) \
	for ((tree) = 0; (tree) < GPU_BUDDY_MAX_FREE_TREES; (tree)++)

/**
 * struct gpu_buddy_block - Block within a buddy allocator
 *
 * Each block in the buddy allocator is represented by this structure.
 * Blocks are organized in a binary tree where each parent block can be
 * split into two children (left and right buddies). The allocator manages
 * blocks at various orders (power-of-2 sizes) from chunk_size up to the
 * largest contiguous region.
 *
 * @private: Private data owned by the allocator user (e.g., driver-specific data)
 * @link: List node for user ownership while block is allocated
 */
struct gpu_buddy_block {
/* private: */
	/*
	 * Header bit layout:
	 * - Bits 63:12: block offset within the address space
	 * - Bits 11:10: state (ALLOCATED, FREE, or SPLIT)
	 * - Bit 9: clear bit (1 if memory is zeroed)
	 * - Bits 8:6: reserved
	 * - Bits 5:0: order (log2 of size relative to chunk_size)
	 */
#define GPU_BUDDY_HEADER_OFFSET GENMASK_ULL(63, 12)
#define GPU_BUDDY_HEADER_STATE  GENMASK_ULL(11, 10)
#define   GPU_BUDDY_ALLOCATED	   (1 << 10)
#define   GPU_BUDDY_FREE	   (2 << 10)
#define   GPU_BUDDY_SPLIT	   (3 << 10)
#define GPU_BUDDY_HEADER_CLEAR  GENMASK_ULL(9, 9)
/* Free to be used, if needed in the future */
#define GPU_BUDDY_HEADER_UNUSED GENMASK_ULL(8, 6)
#define GPU_BUDDY_HEADER_ORDER  GENMASK_ULL(5, 0)
	u64 header;

	struct gpu_buddy_block *left;
	struct gpu_buddy_block *right;
	struct gpu_buddy_block *parent;
/* public: */
	void *private; /* owned by creator */

	/*
	 * While the block is allocated by the user through gpu_buddy_alloc*,
	 * the user has ownership of the link, for example to maintain within
	 * a list, if so desired. As soon as the block is freed with
	 * gpu_buddy_free* ownership is given back to the mm.
	 */
	union {
/* private: */
		struct rb_node rb;
/* public: */
		struct list_head link;
	};
/* private: */
	struct list_head tmp_link;
};

/* Order-zero must be at least SZ_4K */
#define GPU_BUDDY_MAX_ORDER (63 - 12)

/**
 * struct gpu_buddy - GPU binary buddy allocator
 *
 * The buddy allocator provides efficient power-of-two memory allocation
 * with fast allocation and free operations. It is commonly used for GPU
 * memory management where allocations can be split into power-of-two
 * block sizes.
 *
 * Locking should be handled by the user; a simple mutex around
 * gpu_buddy_alloc_blocks() and gpu_buddy_free_block()/gpu_buddy_free_list()
 * should suffice.
 *
 * @n_roots: Number of root blocks in the roots array.
 * @max_order: Maximum block order (log2 of largest block size / chunk_size).
 * @chunk_size: Minimum allocation granularity in bytes. Must be at least SZ_4K.
 * @size: Total size of the address space managed by this allocator in bytes.
 * @avail: Total free space currently available for allocation in bytes.
 * @clear_avail: Free space available in the clear tree (zeroed memory) in bytes.
 *               This is a subset of @avail.
 */
struct gpu_buddy {
/* private: */
	/*
	 * Array of red-black trees for free block management.
	 * Indexed as free_trees[clear/dirty][order] where:
	 * - Index 0 (GPU_BUDDY_CLEAR_TREE): blocks with zeroed content
	 * - Index 1 (GPU_BUDDY_DIRTY_TREE): blocks with unknown content
	 * Each tree holds free blocks of the corresponding order.
	 */
	struct rb_root **free_trees;
	/*
	 * Array of root blocks representing the top-level blocks of the
	 * binary tree(s). Multiple roots exist when the total size is not
	 * a power of two, with each root being the largest power-of-two
	 * that fits in the remaining space.
	 */
	struct gpu_buddy_block **roots;
/* public: */
	unsigned int n_roots;
	unsigned int max_order;
	u64 chunk_size;
	u64 size;
	u64 avail;
	u64 clear_avail;
};

static inline u64
gpu_buddy_block_offset(const struct gpu_buddy_block *block)
{
	return block->header & GPU_BUDDY_HEADER_OFFSET;
}

static inline unsigned int
gpu_buddy_block_order(struct gpu_buddy_block *block)
{
	return block->header & GPU_BUDDY_HEADER_ORDER;
}

static inline bool
gpu_buddy_block_is_free(struct gpu_buddy_block *block)
{
	return (block->header & GPU_BUDDY_HEADER_STATE) == GPU_BUDDY_FREE;
}

static inline bool
gpu_buddy_block_is_clear(struct gpu_buddy_block *block)
{
	return block->header & GPU_BUDDY_HEADER_CLEAR;
}

static inline u64
gpu_buddy_block_size(struct gpu_buddy *mm,
		     struct gpu_buddy_block *block)
{
	return mm->chunk_size << gpu_buddy_block_order(block);
}

int gpu_buddy_init(struct gpu_buddy *mm, u64 size, u64 chunk_size);

void gpu_buddy_fini(struct gpu_buddy *mm);

int gpu_buddy_alloc_blocks(struct gpu_buddy *mm,
			   u64 start, u64 end, u64 size,
			   u64 min_page_size,
			   struct list_head *blocks,
			   unsigned long flags);

int gpu_buddy_block_trim(struct gpu_buddy *mm,
			 u64 *start,
			 u64 new_size,
			 struct list_head *blocks);

void gpu_buddy_reset_clear(struct gpu_buddy *mm, bool is_clear);

void gpu_buddy_free_block(struct gpu_buddy *mm, struct gpu_buddy_block *block);

void gpu_buddy_free_list(struct gpu_buddy *mm,
			 struct list_head *objects,
			 unsigned int flags);

void gpu_buddy_print(struct gpu_buddy *mm);
void gpu_buddy_block_print(struct gpu_buddy *mm,
			   struct gpu_buddy_block *block);
#endif
