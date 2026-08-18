// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <libarena/common.h>
#include <libarena/asan.h>
#include <libarena/buddy.h>

/*
 * Buddy allocator arena-based implementation.
 *
 * Memory is organized into chunks. These chunks
 * cannot be coalesced or split. Allocating
 * chunks allocates their memory eagerly.
 *
 * Internally, each chunk is organized into blocks.
 * Blocks _can_ be coalesced/split, but only inside
 * the chunk. Each block can be allocated or
 * unallocated. If allocated, the entire block holds
 * user data. If unallocated, the block is mostly
 * invalid memory, with the exception of a header
 * used for freelist tracking.
 *
 * The header is placed at an offset inside the block
 * to prevent off-by-one errors from the previous block
 * from trivially overwriting the header. Such an error
 * is also not catchable by ASAN, since the header remains
 * valid memory even after the block is freed. It is still
 * theoretically possible for the header to be corrupted
 * without being caught by ASAN, but harder.
 *
 * Since the allocator needs to track order information for
 * both allocated and free blocks, and allocated blocks cannot
 * store a header, the allocator also stores per-chunk order
 * information in a reserved region at the beginning of the
 * chunk. The header includes a bitmap with the order of blocks
 * and their allocation state. It also includes the freelist
 * heads for the allocation itself.
 */


enum {
	BUDDY_POISONED = (s8)0xef,

	/* Number of pages to be allocated per chunk. */
	BUDDY_CHUNK_PAGES	= BUDDY_CHUNK_BYTES / __PAGE_SIZE
};

static inline int buddy_lock(struct buddy __arena *buddy)
{
	return arena_spin_lock(&buddy->lock);
}

static inline void buddy_unlock(struct buddy __arena *buddy)
{
	arena_spin_unlock(&buddy->lock);
}

/*
 * Reserve part of the arena address space for the allocator. We use
 * this to get aligned addresses for the chunks, since the arena
 * page alloc kfuncs do not support aligning to a boundary (in this
 * case 1 MiB, see buddy.h on how this is derived).
 */
static int buddy_reserve_arena_vaddr(struct buddy __arena *buddy)
{
	buddy->vaddr = 0;

	return bpf_arena_reserve_pages(&arena,
				       (void __arena *)BUDDY_VADDR_OFFSET,
				       BUDDY_VADDR_SIZE / __PAGE_SIZE);
}

/*
 * Free up any unused address space. Used only during teardown.
 */
static void buddy_unreserve_arena_vaddr(struct buddy __arena *buddy)
{
	bpf_arena_free_pages(
		&arena, (void __arena *)(BUDDY_VADDR_OFFSET + buddy->vaddr),
		(BUDDY_VADDR_SIZE - buddy->vaddr) / __PAGE_SIZE);

	buddy->vaddr = 0;
}

/*
 * Carve out part of the reserved address space and hand it over
 * to the buddy allocator.
 *
 * We are assuming the buddy allocator is the only allocator in the
 * system, so there is no race between this function reserving a
 * page range and some other allocator actually making the BPF call
 * to really create and reserve it.
 *
 * However, bump allocation must still be atomic because this function
 * is called without the buddy lock from multiple threads concurrently.
 */
__weak int buddy_alloc_arena_vaddr(struct buddy __arena *buddy, u64 *vaddrp)
{
	u64 vaddr, old, new;

	if (!buddy || !vaddrp)
		return -EINVAL;

	do {
		vaddr = buddy->vaddr;
		new = vaddr + BUDDY_CHUNK_BYTES;

		if (new > BUDDY_VADDR_SIZE)
			return -EINVAL;

		old = __sync_val_compare_and_swap(&buddy->vaddr, vaddr, new);
	} while (old != vaddr && can_loop);

	if (old != vaddr)
		return -EINVAL;

	*vaddrp = BUDDY_VADDR_OFFSET + vaddr;

	return 0;
}

static u64 arena_next_pow2(__u64 n)
{
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n |= n >> 32;
	n++;

	return n;
}

__weak
int idx_set_allocated(struct buddy_chunk __arena *chunk, u64 idx, bool allocated)
{
	bool already_allocated;

	if (unlikely(idx >= BUDDY_CHUNK_ITEMS)) {
		arena_stderr("setting state of invalid idx (%ld, max %d)\n", idx,
			     BUDDY_CHUNK_ITEMS);
		return -EINVAL;
	}

	already_allocated = chunk->allocated[idx / 8] & (1 << (idx % 8));
	if (unlikely(already_allocated == allocated)) {
		arena_stderr("Double %s of idx %ld for chunk %p",
				allocated ? "alloc" : "free",
				idx, chunk);
		return -EINVAL;
	}

	if (allocated)
		chunk->allocated[idx / 8] |= 1 << (idx % 8);
	else
		chunk->allocated[idx / 8] &= ~(1 << (idx % 8));

	return 0;
}

static int idx_is_allocated(struct buddy_chunk __arena *chunk, u64 idx, bool *allocated)
{
	if (unlikely(idx >= BUDDY_CHUNK_ITEMS)) {
		arena_stderr("getting state of invalid idx (%llu, max %d)\n", idx,
			     BUDDY_CHUNK_ITEMS);
		return -EINVAL;
	}

	*allocated = chunk->allocated[idx / 8] & (1 << (idx % 8));
	return 0;
}

__weak
int idx_set_order(struct buddy_chunk __arena *chunk, u64 idx, u8 order)
{
	u8 prev_order;

	if (unlikely(order >= BUDDY_CHUNK_NUM_ORDERS)) {
		arena_stderr("setting invalid order %u\n", order);
		return -EINVAL;
	}

	if (unlikely(idx >= BUDDY_CHUNK_ITEMS)) {
		arena_stderr("setting order of invalid idx (%d, max %d)\n", idx,
			     BUDDY_CHUNK_ITEMS);
		return -EINVAL;
	}

	/*
	 * We store two order instances per byte, one per nibble.
	 * Retain the existing nibble.
	 */
	prev_order = chunk->orders[idx / 2];
	if (idx & 0x1) {
		order &= 0xf;
		order |= (prev_order & 0xf0);
	} else {
		order <<= 4;
		order |= (prev_order & 0xf);
	}

	chunk->orders[idx / 2] = order;

	return 0;
}

static u8 idx_get_order(struct buddy_chunk __arena *chunk, u64 idx)
{
	u8 result;

	_Static_assert(BUDDY_CHUNK_NUM_ORDERS <= 16,
		       "order must fit in 4 bits");

	if (unlikely(idx >= BUDDY_CHUNK_ITEMS)) {
		arena_stderr("getting order of invalid idx %u\n", idx);
		return BUDDY_CHUNK_NUM_ORDERS;
	}

	result = chunk->orders[idx / 2];

	return (idx & 0x1) ? (result & 0xf) : (result >> 4);
}

static void __arena *idx_to_addr(struct buddy_chunk __arena *chunk, size_t idx)
{
	u64 address;

	if (unlikely(idx >= BUDDY_CHUNK_ITEMS)) {
		arena_stderr("translating invalid idx %u\n", idx);
		return NULL;
	}

	/*
	 * The data blocks start in the chunk after the metadata block.
	 * We find the actual address by indexing into the region at an
	 * BUDDY_MIN_ALLOC_BYTES granularity, the minimum allowed.
	 * The index number already accounts for the fact that the first
	 * blocks in the chunk are occupied by the metadata, so we do
	 * not need to offset it.
	 */

	address = (u64)chunk + (idx * BUDDY_MIN_ALLOC_BYTES);

	return (void __arena *)address;
}

static struct buddy_header __arena *idx_to_header(struct buddy_chunk __arena *chunk, size_t idx)
{
	bool allocated;
	u64 address;

	if (unlikely(idx_is_allocated(chunk, idx, &allocated))) {
		arena_stderr("accessing invalid idx 0x%lx\n", idx);
		return NULL;
	}

	if (unlikely(allocated)) {
		arena_stderr("accessing allocated idx 0x%lx as header\n", idx);
		return NULL;
	}

	address = (u64)idx_to_addr(chunk, idx);
	if (!address)
		return NULL;

	/*
	 * Offset the header within the block. This avoids accidental overwrites
	 * to the header because of off-by-one errors when using adjacent blocks.
	 *
	 * The offset has been chosen as a compromise between ASAN effectiveness
	 * and allocator granularity:
	 * 1) ASAN dictates valid data runs are 8-byte aligned.
	 * 2) We want to keep a low minimum allocation size (currently 16).
	 *
	 * As a result, we have only two possible positions for the header: Bytes
	 * 0 and 8. Keeping the header in byte 0 means off-by-ones from the previous
	 * block touch the header, and, since the header must be accessible, ASAN
	 * will not trigger. Keeping the header on byte 8 means off-by-one errors from
	 * the previous block are caught by ASAN. Negative offsets are rarer, so
	 * while accesses into the block from the next block are possible, they are
	 * less probable.
	 */

	return (struct buddy_header __arena *)(address + BUDDY_HEADER_OFF);
}

static void header_add_freelist(struct buddy_chunk __arena *chunk, struct buddy_header __arena *header,
		u64 idx, u8 order)
{
	struct buddy_header __arena *tmp_header;

	idx_set_order(chunk, idx, order);

	header->next_index = chunk->freelists[order];
	header->prev_index = BUDDY_CHUNK_ITEMS;

	if (header->next_index != BUDDY_CHUNK_ITEMS) {
		tmp_header = idx_to_header(chunk, header->next_index);
		tmp_header->prev_index = idx;
	}

	chunk->freelists[order] = idx;
}

static void header_remove_freelist(struct buddy_chunk __arena  *chunk,
				   struct buddy_header __arena *header, u8 order)
{
	struct buddy_header __arena *tmp_header;

	if (header->prev_index != BUDDY_CHUNK_ITEMS) {
		tmp_header = idx_to_header(chunk, header->prev_index);
		tmp_header->next_index = header->next_index;
	}

	if (header->next_index != BUDDY_CHUNK_ITEMS) {
		tmp_header = idx_to_header(chunk, header->next_index);
		tmp_header->prev_index = header->prev_index;
	}

	/* Pop off the list head if necessary. */
	if (idx_to_header(chunk, chunk->freelists[order]) == header)
		chunk->freelists[order] = header->next_index;

	header->prev_index = BUDDY_CHUNK_ITEMS;
	header->next_index = BUDDY_CHUNK_ITEMS;
}

static u64 size_to_order(size_t size)
{
	u64 order;

	/*
	 * Legal sizes are [1, 4GiB] (the biggest possible arena).
	 * Of course, sizes close to GiB are practically impossible
	 * to fulfill and allocation will fail, but that's taken care
	 * of by the caller.
	 */

	if (unlikely(size == 0 || size > (1UL << 32))) {
		arena_stderr("illegal size request %lu\n", size);
		return 64;
	}
	/*
	 * To find the order of the allocation we find the first power of two
	 * >= the requested size, take the log2, then adjust it for the minimum
	 * allocation size by removing the minimum shift from it. Requests
	 * smaller than the minimum allocation size are rounded up.
	 */
	order = arena_fls(arena_next_pow2(size)) - 1;
	if (order < BUDDY_MIN_ALLOC_SHIFT)
		return 0;

	return order - BUDDY_MIN_ALLOC_SHIFT;
}

__weak
int add_leftovers_to_freelist(struct buddy_chunk __arena *chunk, u32 cur_idx,
		u64 min_order, u64 max_order)
{
	struct buddy_header __arena *header;
	u64 ord;
	u32 idx;

	for (ord = min_order; ord < max_order && can_loop; ord++) {
		/* Mark the buddy as free and add it to the freelists. */
		idx = cur_idx + (1 << ord);

		header = idx_to_header(chunk, idx);
		if (unlikely(!header)) {
			arena_stderr("idx %u has no header", idx);
			return -EINVAL;
		}

		asan_unpoison(header, sizeof(*header));

		header_add_freelist(chunk, header, idx, ord);
	}

	return 0;
}

static struct buddy_chunk __arena *buddy_chunk_get(struct buddy __arena *buddy)
{
	u64 order, ord, min_order, max_order;
	struct buddy_chunk __arena  *chunk;
	size_t left;
	int power2;
	u64 vaddr;
	u32 idx;
	int ret;

	/*
	 * Step 1:  Allocate a properly aligned chunk, and
	 * prep it for insertion into the buddy allocator.
	 * We don't need the allocator lock until step 2.
	 */

	ret = buddy_alloc_arena_vaddr(buddy, &vaddr);
	if (ret)
		return NULL;

	/* Addresses must be aligned to the chunk boundary. */
	if (vaddr % BUDDY_CHUNK_BYTES)
		return NULL;

	/* Unreserve the address space. */
	bpf_arena_free_pages(&arena, (void __arena *)vaddr,
			     BUDDY_CHUNK_PAGES);

	chunk = bpf_arena_alloc_pages(&arena, (void __arena *)vaddr,
				      BUDDY_CHUNK_PAGES, NUMA_NO_NODE, 0);
	if (!chunk) {
		arena_stderr("[ALLOC FAILED]");
		return NULL;
	}

	if (buddy_lock(buddy)) {
		/*
		 * We cannot reclaim the vaddr space, but that is ok - this
		 * operation should always succeed. The error path is to catch
		 * accidental deadlocks that will cause -ENOMEMs to the program as
		 * the allocator fails to refill itself, in which case vaddr usage
		 * is the least of our worries.
		 */
		bpf_arena_free_pages(&arena, (void __arena *)vaddr, BUDDY_CHUNK_PAGES);
		return NULL;
	}

	asan_poison(chunk, BUDDY_POISONED, BUDDY_CHUNK_PAGES * __PAGE_SIZE);

	/* Unpoison the chunk itself. */
	asan_unpoison(chunk, sizeof(*chunk));

	/* Mark all freelists as empty. */
	for (ord = zero; ord < BUDDY_CHUNK_NUM_ORDERS && can_loop; ord++)
		chunk->freelists[ord] = BUDDY_CHUNK_ITEMS;

	/*
	 * Initialize the chunk by carving out a page range to hold the metadata
	 * struct above, then dumping the rest of the pages into the allocator.
	 */

	_Static_assert(BUDDY_CHUNK_PAGES * __PAGE_SIZE >=
			       BUDDY_MIN_ALLOC_BYTES *
				       BUDDY_CHUNK_ITEMS,
		       "chunk must fit within the allocation");

	/*
	 * Step 2: Reserve a chunk for the chunk metadata, then breaks
	 * the rest of the full allocation into the different buckets.
	 * We allocating the memory by grabbing blocks of progressively
	 * smaller sizes from the allocator, which are guaranteed to be
	 * continuous.
	 *
	 * This operation also populates the allocator.
	 *
	 * Algorithm:
	 *
	 * - max_order: The last order allocation we made
	 * - left: How many bytes are left to allocate
	 * - cur_index: Current index into the top-level block we are
	 * allocating from.
	 *
	 * Step 3:
	 * - Find the largest power-of-2 allocation still smaller than left (infimum)
	 * - Reserve a chunk of that size, along with its buddy
	 * - For every order from [infimum + 1, last order), carve out a block
	 *   and put it into the allocator.
	 *
	 *  Example: Chunk size 0b1010000 (80 bytes)
	 *
	 *  Step 1:
	 *
	 *   idx  infimum                             1 << max_order
	 *   0        64        128                    1 << 20
	 *   |________|_________|______________________|
	 *
	 *   Blocks set aside:
	 *   	[0, 64)         - Completely allocated
	 *   	[64, 128)       - Will be further split in the next iteration
	 *
	 *   Blocks added to the allocator:
	 *   	[128, 256)
	 *   	[256, 512)
	 *   	...
	 *   	[1 << 18, 1 << 19)
	 *   	[1 << 19, 1 << 20)
	 *
	 *  Step 2:
	 *
	 *   idx  infimum			   idx + 1 << max_order
	 *   64	      80	96		   	64 + 1 << 6 = 128
	 *   |________|_________|______________________|
	 *
	 *   Blocks set aside:
	 *   	[64, 80)	- Completely allocated
	 *
	 *   Blocks added to the allocator:
	 *      [80, 96) - left == 0 so the buddy is unused and marked as freed
	 *   	[96, 128)
	 */
	 max_order = BUDDY_CHUNK_NUM_ORDERS;
	left = sizeof(*chunk);
	idx = 0;
	while (left && can_loop) {
		power2 = arena_fls(left) - 1;
		/*
		 * Note: The condition below only triggers to catch serious bugs
		 * early. There is no sane way to undo any block insertions from
		 * the allocated chunk, so just leak any leftover allocations,
		 * emit a diagnostic, unlock and exit.
		 *
		 */
		if (unlikely(power2 >= BUDDY_CHUNK_NUM_ORDERS)) {
			arena_stderr(
				"buddy chunk metadata require allocation of order %d\n",
				power2);
			arena_stderr(
				"chunk has size of 0x%lx bytes (left %lx bytes)\n",
				sizeof(*chunk), left);
			buddy_unlock(buddy);

			return NULL;
		}

		/* Round up allocations that are too small. */

		left -= (power2 >= BUDDY_MIN_ALLOC_SHIFT) ? 1 << power2 : left;
		order = (power2 >= BUDDY_MIN_ALLOC_SHIFT) ? power2 - BUDDY_MIN_ALLOC_SHIFT : 0;

		if (idx_set_allocated(chunk, idx, true)) {
			buddy_unlock(buddy);
			return NULL;
		}

		/*
		 * Starting an order above the one we allocated, populate
		 * the allocator with free blocks. If this is the last
		 * allocation (left == 0), also mark the buddy as free.
		 *
		 * See comment above about error handling: The error path
		 * is only there as a way to mitigate deeply buggy allocator
		 * states by emitting a diagnostic in add_leftovers_to_freelist()
		 * and leaking any memory not added in the freelists.
		 */
		min_order = left ? order + 1 : order;
		if (add_leftovers_to_freelist(chunk, idx, min_order, max_order)) {
			buddy_unlock(buddy);
			return NULL;
		}

		/* Adjust the index. */
		idx += 1 << order;
		max_order = order;
	}

	buddy_unlock(buddy);

	return chunk;
}

__weak int buddy_init(struct buddy __arena *buddy)
{
	struct buddy_chunk __arena *chunk;
	int ret;

	if (!asan_ready())
		return -EINVAL;

	/* Reserve enough address space to ensure allocations are aligned. */
	ret = buddy_reserve_arena_vaddr(buddy);
	if (ret)
		return ret;

	_Static_assert(BUDDY_CHUNK_PAGES > 0,
		       "chunk must use one or more pages");

	chunk = buddy_chunk_get(buddy);

	if (buddy_lock(buddy)) {
		bpf_arena_free_pages(&arena, chunk, BUDDY_CHUNK_PAGES);
		return -EINVAL;
	}

	/* Chunk is already properly unpoisoned if allocated. */
	if (chunk)
		chunk->next = buddy->first_chunk;

	/* Put the chunk at the beginning of the list. */
	buddy->first_chunk = chunk;

	buddy_unlock(buddy);

	return chunk ? 0 : -ENOMEM;
}

/*
 * Destroy the allocator. This does not check whether there are any allocations
 * currently in use, so any pages being accessed will start taking arena faults.
 * We do not take a lock because we are freeing arena pages, and nobody should
 * be using the allocator at that point in the execution.
 */
__weak int buddy_destroy(struct buddy __arena *buddy)
{
	struct buddy_chunk __arena *chunk, *next;

	if (!buddy)
		return -EINVAL;

	/*
	 * Traverse all buddy chunks and free them back to the arena
	 * with the same granularity they were allocated with.
	 */
	for (chunk = buddy->first_chunk; chunk && can_loop; chunk = next) {
		next = chunk->next;

		/* Wholesale poison the entire block. */
		asan_poison(chunk, BUDDY_POISONED,
			    BUDDY_CHUNK_PAGES * __PAGE_SIZE);
		bpf_arena_free_pages(&arena, chunk, BUDDY_CHUNK_PAGES);
	}

	/* Free up any part of the address space that did not get used. */
	buddy_unreserve_arena_vaddr(buddy);

	/* Clear all fields. */
	buddy->first_chunk = NULL;

	return 0;
}

__weak u64 buddy_chunk_alloc(struct buddy_chunk __arena *chunk, int order_req)
{
	struct buddy_header __arena *header, *tmp_header, *next_header;
	u32 idx, tmpidx, retidx;
	u64 address;
	u64 order = 0;
	u64 i;

	for (order = order_req; order < BUDDY_CHUNK_NUM_ORDERS && can_loop; order++) {
		if (chunk->freelists[order] != BUDDY_CHUNK_ITEMS)
			break;
	}

	if (order >= BUDDY_CHUNK_NUM_ORDERS)
		return (u64)NULL;

	retidx = chunk->freelists[order];
	header = idx_to_header(chunk, retidx);
	if (unlikely(!header))
		return (u64) NULL;

	chunk->freelists[order] = header->next_index;

	if (header->next_index != BUDDY_CHUNK_ITEMS) {
		next_header = idx_to_header(chunk, header->next_index);
		next_header->prev_index = BUDDY_CHUNK_ITEMS;
	}

	header->prev_index = BUDDY_CHUNK_ITEMS;
	header->next_index = BUDDY_CHUNK_ITEMS;
	if (idx_set_order(chunk, retidx, order_req))
		return (u64)NULL;

	if (idx_set_allocated(chunk, retidx, true))
		return (u64)NULL;

	/*
	 * Do not unpoison the address yet, will be done by the caller
	 * because the caller has the exact allocation size requested.
	 */
	address = (u64)idx_to_addr(chunk, retidx);
	if (!address)
		return (u64)NULL;

	/* If we allocated from a larger-order chunk, split the buddies. */
	for (i = order_req; i < order && can_loop; i++) {
		/*
		 * Flip the bit for the current order (the bit is guaranteed
		 * to be 0, so just add 1 << i).
		 */
		idx = retidx + (1 << i);

		/* Add the buddy of the allocation to the free list. */
		header = idx_to_header(chunk, idx);
		/* Unpoison the buddy header */
		asan_unpoison(header, sizeof(*header));

		if (idx_set_order(chunk, idx, i))
			return (u64)NULL;

		/* Push the header to the beginning of the freelists list. */
		tmpidx = chunk->freelists[i];

		header->prev_index = BUDDY_CHUNK_ITEMS;
		header->next_index = tmpidx;

		if (tmpidx != BUDDY_CHUNK_ITEMS) {
			tmp_header = idx_to_header(chunk, tmpidx);
			tmp_header->prev_index = idx;
		}

		chunk->freelists[i] = idx;
	}

	return address;
}

/* Scan the existing chunks for available memory. */
static u64 buddy_alloc_from_existing_chunks(struct buddy __arena *buddy, int order)
{
	struct buddy_chunk __arena *chunk;
	u64 address;

	for (chunk = buddy->first_chunk; chunk != NULL && can_loop;
	     chunk = chunk->next) {
		address = buddy_chunk_alloc(chunk, order);
		if (address)
			return address;
	}

	return (u64)NULL;
}

/*
 * Try an allocation from a newly allocated chunk. Also
 * incorporate the chunk into the linked list.
 */
static u64 buddy_alloc_from_new_chunk(struct buddy __arena *buddy, struct buddy_chunk __arena *chunk, int order)
{
	u64 address;

	if (buddy_lock(buddy))
		return (u64)NULL;


	/*
	 * Add the chunk into the allocator and try
	 * to allocate specifically from that chunk.
	 */
	chunk->next = buddy->first_chunk;
	buddy->first_chunk = chunk;

	address = buddy_chunk_alloc(buddy->first_chunk, order);

	buddy_unlock(buddy);

	return (u64)address;
}
__weak
void __arena *buddy_alloc(struct buddy __arena *buddy, size_t size)
{
	void __arena *address = NULL;
	struct buddy_chunk __arena *chunk;
	int order;

	if (!buddy)
		return NULL;

	order = size_to_order(size);
	if (order >= BUDDY_CHUNK_NUM_ORDERS || order < 0) {
		arena_stderr("invalid order %d (sz %lu)\n", order, size);
		return NULL;
	}

	if (buddy_lock(buddy))
		return NULL;

	address = (u8 __arena *)buddy_alloc_from_existing_chunks(buddy, order);
	buddy_unlock(buddy);
	if (address)
		goto done;

	/* Get a new chunk. */
	chunk = buddy_chunk_get(buddy);
	if (chunk)
		address = (u8 __arena *)buddy_alloc_from_new_chunk(buddy, chunk, order);

done:
	/* If we failed to allocate memory, return NULL. */
	if (!address)
		return NULL;

	/*
	 * Unpoison exactly the amount of bytes requested. If the
	 * data is smaller than the header, we must poison any
	 * unused bytes that were part of the header.
	 */
	if (size < BUDDY_HEADER_OFF + sizeof(struct buddy_header __arena))
		asan_poison(address + BUDDY_HEADER_OFF, BUDDY_POISONED,
			    sizeof(struct buddy_header __arena));

	asan_unpoison(address, size);

	return address;
}

static __always_inline int buddy_free_unlocked(struct buddy __arena *buddy, u64 addr)
{
	struct buddy_header __arena *header, *buddy_header;
	u64 idx, buddy_idx, tmp_idx;
	struct buddy_chunk __arena *chunk;
	bool allocated;
	u8 order;
	int ret;

	if (!buddy)
		return -EINVAL;

	if (addr & (BUDDY_MIN_ALLOC_BYTES - 1)) {
		arena_stderr("Freeing unaligned address %llx\n", addr);
		return -EINVAL;
	}

	/* Get (chunk, idx) out of the address. */
	chunk = (void __arena *)(addr & ~BUDDY_CHUNK_OFFSET_MASK);
	idx = (addr & BUDDY_CHUNK_OFFSET_MASK) / BUDDY_MIN_ALLOC_BYTES;

	/* Mark the block as unallocated so we can access the header. */
	ret = idx_set_allocated(chunk, idx, false);
	if (ret)
		return ret;

	order  = idx_get_order(chunk, idx);
	header = idx_to_header(chunk, idx);

	/* The header is in the block itself, keep it unpoisoned. */
	asan_poison((u8 __arena *)addr, BUDDY_POISONED,
		    BUDDY_MIN_ALLOC_BYTES << order);
	asan_unpoison(header, sizeof(*header));

	/*
	 * Coalescing loop. Merge with free buddies of equal order.
	 * For every coalescing step, keep the left buddy and
	 * drop the right buddy's header.
	 */
	for (; order < BUDDY_CHUNK_NUM_ORDERS && can_loop; order++) {
		buddy_idx = idx ^ (1 << order);

		/* Check if the buddy is actually free. */
		idx_is_allocated(chunk, buddy_idx, &allocated);
		if (allocated)
			break;

		/*
		 * If buddy is not the same order as the chunk
		 * being freed, then we're done coalescing.
		 */
		if (idx_get_order(chunk, buddy_idx) != order)
			break;

		buddy_header = idx_to_header(chunk, buddy_idx);
		header_remove_freelist(chunk, buddy_header, order);

		/* Keep the left header out of the two buddies, drop the other one. */
		if (buddy_idx < idx) {
			tmp_idx = idx;
			idx = buddy_idx;
			buddy_idx = tmp_idx;
		}

		/* Remove the buddy from the freelists so that we can merge it. */
		idx_set_order(chunk, buddy_idx, order);

		buddy_header = idx_to_header(chunk, buddy_idx);
		asan_poison(buddy_header, BUDDY_POISONED,
			    sizeof(*buddy_header));
	}

	/* Header properly freed but not in any freelists yet .*/
	idx_set_order(chunk, idx, order);

	header = idx_to_header(chunk, idx);
	header_add_freelist(chunk, header, idx, order);

	return 0;
}

__weak int buddy_free(struct buddy __arena *buddy, void __arena *addr)
{
	int ret;

	if (!buddy)
		return -EINVAL;

	/* Freeing NULL is a valid no-op. */
	if (!addr)
		return 0;

	ret = buddy_lock(buddy);
	if (ret)
		return ret;

	ret = buddy_free_unlocked(buddy, (u64)addr);

	buddy_unlock(buddy);

	return ret;
}

__weak char _license[] SEC("license") = "GPL";
