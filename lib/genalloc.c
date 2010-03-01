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

#include <linux/module.h>
#include <linux/bitmap.h>
#include <linux/genalloc.h>


/**
 * gen_pool_create - create a new special memory pool
 * @min_alloc_order: log base 2 of number of bytes each bitmap bit represents
 * @nid: node id of the node the pool structure should be allocated on, or -1
 *
 * Create a new special memory pool that can be used to manage special purpose
 * memory not managed by the regular kmalloc/kfree interface.
 */
struct gen_pool *gen_pool_create(int min_alloc_order, int nid)
{
	struct gen_pool *pool;

	pool = kmalloc_node(sizeof(struct gen_pool), GFP_KERNEL, nid);
	if (pool != NULL) {
		rwlock_init(&pool->lock);
		INIT_LIST_HEAD(&pool->chunks);
		pool->min_alloc_order = min_alloc_order;
	}
	return pool;
}
EXPORT_SYMBOL(gen_pool_create);

/**
 * gen_pool_add - add a new chunk of special memory to the pool
 * @pool: pool to add new memory chunk to
 * @addr: starting address of memory chunk to add to pool
 * @size: size in bytes of the memory chunk to add to pool
 * @nid: node id of the node the chunk structure and bitmap should be
 *       allocated on, or -1
 *
 * Add a new chunk of special memory to the specified pool.
 */
int gen_pool_add(struct gen_pool *pool, unsigned long addr, size_t size,
		 int nid)
{
	struct gen_pool_chunk *chunk;
	int nbits = size >> pool->min_alloc_order;
	int nbytes = sizeof(struct gen_pool_chunk) +
				(nbits + BITS_PER_BYTE - 1) / BITS_PER_BYTE;

	chunk = kmalloc_node(nbytes, GFP_KERNEL | __GFP_ZERO, nid);
	if (unlikely(chunk == NULL))
		return -1;

	spin_lock_init(&chunk->lock);
	chunk->start_addr = addr;
	chunk->end_addr = addr + size;

	write_lock(&pool->lock);
	list_add(&chunk->next_chunk, &pool->chunks);
	write_unlock(&pool->lock);

	return 0;
}
EXPORT_SYMBOL(gen_pool_add);

/**
 * gen_pool_destroy - destroy a special memory pool
 * @pool: pool to destroy
 *
 * Destroy the specified special memory pool. Verifies that there are no
 * outstanding allocations.
 */
void gen_pool_destroy(struct gen_pool *pool)
{
	struct list_head *_chunk, *_next_chunk;
	struct gen_pool_chunk *chunk;
	int order = pool->min_alloc_order;
	int bit, end_bit;


	list_for_each_safe(_chunk, _next_chunk, &pool->chunks) {
		chunk = list_entry(_chunk, struct gen_pool_chunk, next_chunk);
		list_del(&chunk->next_chunk);

		end_bit = (chunk->end_addr - chunk->start_addr) >> order;
		bit = find_next_bit(chunk->bits, end_bit, 0);
		BUG_ON(bit < end_bit);

		kfree(chunk);
	}
	kfree(pool);
	return;
}
EXPORT_SYMBOL(gen_pool_destroy);

/**
 * gen_pool_alloc - allocate special memory from the pool
 * @pool: pool to allocate from
 * @size: number of bytes to allocate from the pool
 *
 * Allocate the requested number of bytes from the specified pool.
 * Uses a first-fit algorithm.
 */
unsigned long gen_pool_alloc(struct gen_pool *pool, size_t size)
{
	struct list_head *_chunk;
	struct gen_pool_chunk *chunk;
	unsigned long addr, flags;
	int order = pool->min_alloc_order;
	int nbits, start_bit, end_bit;

	if (size == 0)
		return 0;

	nbits = (size + (1UL << order) - 1) >> order;

	read_lock(&pool->lock);
	list_for_each(_chunk, &pool->chunks) {
		chunk = list_entry(_chunk, struct gen_pool_chunk, next_chunk);

		end_bit = (chunk->end_addr - chunk->start_addr) >> order;
		end_bit -= nbits + 1;

		spin_lock_irqsave(&chunk->lock, flags);
		start_bit = bitmap_find_next_zero_area(chunk->bits, end_bit, 0,
						nbits, 0);
		if (start_bit >= end_bit) {
			spin_unlock_irqrestore(&chunk->lock, flags);
			continue;
		}

		addr = chunk->start_addr + ((unsigned long)start_bit << order);

		bitmap_set(chunk->bits, start_bit, nbits);
		spin_unlock_irqrestore(&chunk->lock, flags);
		read_unlock(&pool->lock);
		return addr;
	}
	read_unlock(&pool->lock);
	return 0;
}
EXPORT_SYMBOL(gen_pool_alloc);

/**
 * gen_pool_free - free allocated special memory back to the pool
 * @pool: pool to free to
 * @addr: starting address of memory to free back to pool
 * @size: size in bytes of memory to free
 *
 * Free previously allocated special memory back to the specified pool.
 */
void gen_pool_free(struct gen_pool *pool, unsigned long addr, size_t size)
{
	struct list_head *_chunk;
	struct gen_pool_chunk *chunk;
	unsigned long flags;
	int order = pool->min_alloc_order;
	int bit, nbits;

	nbits = (size + (1UL << order) - 1) >> order;

	read_lock(&pool->lock);
	list_for_each(_chunk, &pool->chunks) {
		chunk = list_entry(_chunk, struct gen_pool_chunk, next_chunk);

		if (addr >= chunk->start_addr && addr < chunk->end_addr) {
			BUG_ON(addr + size > chunk->end_addr);
			spin_lock_irqsave(&chunk->lock, flags);
			bit = (addr - chunk->start_addr) >> order;
			while (nbits--)
				__clear_bit(bit++, chunk->bits);
			spin_unlock_irqrestore(&chunk->lock, flags);
			break;
		}
	}
	BUG_ON(nbits > 0);
	read_unlock(&pool->lock);
}
EXPORT_SYMBOL(gen_pool_free);
