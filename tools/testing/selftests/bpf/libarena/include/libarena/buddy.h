// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#pragma once

enum buddy_consts {
	/*
	 * Minimum allocation is 1 << BUDDY_MIN_ALLOC_SHIFT.
	 * Larger sizes increase internal fragmentation, but smaller
	 * sizes increase the space overhead of the block metadata.
	 */
	BUDDY_MIN_ALLOC_SHIFT	= 4,
	BUDDY_MIN_ALLOC_BYTES	= 1 << BUDDY_MIN_ALLOC_SHIFT,

	/*
	 * How many orders the buddy allocator can serve. Minimum block
	 * size is 1 << BUDDY_MIN_ALLOC_SHIFT, maximum block size is
	 * 1 << (BUDDY_MIN_ALLOC_SHIFT + BUDDY_CHUNK_NUM_ORDERS - 1):
	 * Each block has size 1 << BUDDY_MIN_ALLOC_SHIFT, and the
	 * allocation orders are in [0, BUDDY_CHUNK_NUM_ORDERS).
	 * We keep two blocks of the maximum size to retain the
	 * property in the code that all blocks have a buddy.
	 * Higher values increase the maximum allocation size,
	 * but also the size of the metadata for each block.
	 */
	BUDDY_CHUNK_NUM_ORDERS	= 1 << 4,
	BUDDY_CHUNK_BYTES	= BUDDY_MIN_ALLOC_BYTES << (BUDDY_CHUNK_NUM_ORDERS),

	/* Offset of the buddy header within a free block, see buddy.bpf.c for details */
	BUDDY_HEADER_OFF	= 8,

	/* The maximum number of blocks a chunk may have to track. */
	BUDDY_CHUNK_ITEMS	= 1 << (BUDDY_CHUNK_NUM_ORDERS),
	BUDDY_CHUNK_OFFSET_MASK	= BUDDY_CHUNK_BYTES - 1,

	/*
	 * Alignment for chunk allocations based on bpf_arena_alloc_pages.
	 * The arena allocation kfunc does not have an alignment argument,
	 * but that is required for all block calculations in the chunk to
	 * work.
	 */
	BUDDY_VADDR_OFFSET	= BUDDY_CHUNK_BYTES,

	/* Total arena virtual address space the allocator can consume. */
	BUDDY_VADDR_SIZE	= BUDDY_CHUNK_BYTES << 10
};

struct buddy_header {
	u32 prev_index;	/* "Pointer" to the previous available allocation of the same size. */
	u32 next_index; /* Same for the next allocation. */
};

/*
 * We bring memory into the allocator 1 MiB at a time.
 */
struct buddy_chunk {
	/* The order of the current allocation for a item. 4 bits per order. */
	u8		orders[BUDDY_CHUNK_ITEMS / 2];
	/*
	 * Bit to denote whether chunk is allocated. Size of the allocated/free
	 * chunk found from the orders array.
	 */
	u8		allocated[BUDDY_CHUNK_ITEMS / 8];
	/* Freelists for O(1) allocation. */
	u64		freelists[BUDDY_CHUNK_NUM_ORDERS];
	struct buddy_chunk __arena	*next;
};

struct buddy {
	struct buddy_chunk __arena *first_chunk;		/* Pointer to the chunk linked list. */
	arena_spinlock_t lock;			/* Allocator lock */
	u64 vaddr;				/* Allocation into reserved vaddr */
};

#ifdef __BPF__

int buddy_init(struct buddy __arena *buddy);
int buddy_destroy(struct buddy __arena *buddy);
int buddy_free(struct buddy __arena *buddy, void __arena *free);
void __arena *buddy_alloc(struct buddy __arena *buddy, size_t size);

#endif /* __BPF__  */
