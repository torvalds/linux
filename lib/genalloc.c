/*
 * Basic general purpose allocator for managing special purpose memory
 * not managed by the regular kmalloc/kfree interface.
 * Uses for this includes on-device special memory, uncached memory
 * etc.
 *
 * Copyright 2005 (C) Jes Sorensen <jes@trained-monkey.org>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/bitmap.h>
#include <linux/genalloc.h>


/* General purpose special memory pool descriptor. */
struct gen_pool {
	rwlock_t lock;			/* protects chunks list */
	struct list_head chunks;	/* list of chunks in this pool */
	unsigned order;			/* minimum allocation order */
};

/* General purpose special memory pool chunk descriptor. */
struct gen_pool_chunk {
	spinlock_t lock;		/* protects bits */
	struct list_head next_chunk;	/* next chunk in pool */
	phys_addr_t phys_addr;		/* physical starting address of memory chunk */
	unsigned long start;		/* start of memory chunk */
	unsigned long size;		/* number of bits */
	unsigned long bits[0];		/* bitmap for allocating memory chunk */
};


/**
 * gen_pool_create() - create a new special memory pool
 * @order:	Log base 2 of number of bytes each bitmap bit
 *		represents.
 * @nid:	Node id of the node the pool structure should be allocated
 *		on, or -1.  This will be also used for other allocations.
 *
 * Create a new special memory pool that can be used to manage special purpose
 * memory not managed by the regular kmalloc/kfree interface.
 */
struct gen_pool *__must_check gen_pool_create(unsigned order, int nid)
{
	struct gen_pool *pool;

	if (WARN_ON(order >= BITS_PER_LONG))
		return NULL;

	pool = kmalloc_node(sizeof *pool, GFP_KERNEL, nid);
	if (pool) {
		rwlock_init(&pool->lock);
		INIT_LIST_HEAD(&pool->chunks);
		pool->order = order;
	}
	return pool;
}
EXPORT_SYMBOL(gen_pool_create);

/**
 * gen_pool_add_virt - add a new chunk of special memory to the pool
 * @pool:	Pool to add new memory chunk to
 * @virt:	Virtual starting address of memory chunk to add to pool
 * @phys:	Physical starting address of memory chunk to add to pool
 * @size:	Size in bytes of the memory chunk to add to pool
 * @nid:	Node id of the node the chunk structure and bitmap should be
 *       	allocated on, or -1
 *
 * Add a new chunk of special memory to the specified pool.
 *
 * Returns 0 on success or a -ve errno on failure.
 */
int __must_check
gen_pool_add_virt(struct gen_pool *pool, unsigned long virt, phys_addr_t phys,
		  size_t size, int nid)
{
	struct gen_pool_chunk *chunk;
	size_t nbytes;

	if (WARN_ON(!virt || virt + size < virt ||
		    (virt & ((1 << pool->order) - 1))))
		return -EINVAL;

	size = size >> pool->order;
	if (WARN_ON(!size))
		return -EINVAL;

	nbytes = sizeof(struct gen_pool_chunk) + BITS_TO_LONGS(nbits) * sizeof(long);
	
	chunk = kzalloc_node(nbytes, GFP_KERNEL, nid);
	if (!chunk)
		return -ENOMEM;

	spin_lock_init(&chunk->lock);
	chunk->start = virt >> pool->order;
	chunk->size  = size;
	chunk->phys_addr = phys;

	write_lock(&pool->lock);
	list_add(&chunk->next_chunk, &pool->chunks);
	write_unlock(&pool->lock);

	return 0;
}
EXPORT_SYMBOL(gen_pool_add_virt);

/**
 * gen_pool_virt_to_phys - return the physical address of memory
 * @pool: pool to allocate from
 * @addr: starting address of memory
 *
 * Returns the physical address on success, or -1 on error.
 */
phys_addr_t gen_pool_virt_to_phys(struct gen_pool *pool, unsigned long addr)
{
	struct list_head *_chunk;
	struct gen_pool_chunk *chunk;

	read_lock(&pool->lock);
	list_for_each(_chunk, &pool->chunks) {
		unsigned long start_addr;
		chunk = list_entry(_chunk, struct gen_pool_chunk, next_chunk);

		start_addr = chunk->start << pool->order;
		if (addr >= start_addr && addr < start_addr + chunk->size)
			return chunk->phys_addr + addr - start_addr;
	}
	read_unlock(&pool->lock);

	return -1;
}
EXPORT_SYMBOL(gen_pool_virt_to_phys);

/**
 * gen_pool_destroy() - destroy a special memory pool
 * @pool:	Pool to destroy.
 *
 * Destroy the specified special memory pool. Verifies that there are no
 * outstanding allocations.
 */
void gen_pool_destroy(struct gen_pool *pool)
{
	struct gen_pool_chunk *chunk;
	int bit;

	while (!list_empty(&pool->chunks)) {
		chunk = list_entry(pool->chunks.next, struct gen_pool_chunk,
				   next_chunk);
		list_del(&chunk->next_chunk);

		bit = find_next_bit(chunk->bits, chunk->size, 0);
		BUG_ON(bit < chunk->size);

		kfree(chunk);
	}
	kfree(pool);
}
EXPORT_SYMBOL(gen_pool_destroy);

/**
 * gen_pool_alloc_aligned() - allocate special memory from the pool
 * @pool:	Pool to allocate from.
 * @size:	Number of bytes to allocate from the pool.
 * @alignment_order:	Order the allocated space should be
 *			aligned to (eg. 20 means allocated space
 *			must be aligned to 1MiB).
 *
 * Allocate the requested number of bytes from the specified pool.
 * Uses a first-fit algorithm.
 */
unsigned long __must_check
gen_pool_alloc_aligned(struct gen_pool *pool, size_t size,
		       unsigned alignment_order)
{
	unsigned long addr, align_mask = 0, flags, start;
	struct gen_pool_chunk *chunk;

	if (size == 0)
		return 0;

	if (alignment_order > pool->order)
		align_mask = (1 << (alignment_order - pool->order)) - 1;

	size = (size + (1UL << pool->order) - 1) >> pool->order;

	read_lock(&pool->lock);
	list_for_each_entry(chunk, &pool->chunks, next_chunk) {
		if (chunk->size < size)
			continue;

		spin_lock_irqsave(&chunk->lock, flags);
		start = bitmap_find_next_zero_area_off(chunk->bits, chunk->size,
						       0, size, align_mask,
						       chunk->start);
		if (start >= chunk->size) {
			spin_unlock_irqrestore(&chunk->lock, flags);
			continue;
		}

		bitmap_set(chunk->bits, start, size);
		spin_unlock_irqrestore(&chunk->lock, flags);
		addr = (chunk->start + start) << pool->order;
		goto done;
	}

	addr = 0;
done:
	read_unlock(&pool->lock);
	return addr;
}
EXPORT_SYMBOL(gen_pool_alloc_aligned);

/**
 * gen_pool_free() - free allocated special memory back to the pool
 * @pool:	Pool to free to.
 * @addr:	Starting address of memory to free back to pool.
 * @size:	Size in bytes of memory to free.
 *
 * Free previously allocated special memory back to the specified pool.
 */
void gen_pool_free(struct gen_pool *pool, unsigned long addr, size_t size)
{
	struct gen_pool_chunk *chunk;
	unsigned long flags;

	if (!size)
		return;

	addr = addr >> pool->order;
	size = (size + (1UL << pool->order) - 1) >> pool->order;

	BUG_ON(addr + size < addr);

	read_lock(&pool->lock);
	list_for_each_entry(chunk, &pool->chunks, next_chunk)
		if (addr >= chunk->start &&
		    addr + size <= chunk->start + chunk->size) {
			spin_lock_irqsave(&chunk->lock, flags);
			bitmap_clear(chunk->bits, addr - chunk->start, size);
			spin_unlock_irqrestore(&chunk->lock, flags);
			goto done;
		}
	BUG_ON(1);
done:
	read_unlock(&pool->lock);
}
EXPORT_SYMBOL(gen_pool_free);
