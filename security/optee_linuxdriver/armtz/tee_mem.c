/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
/**
 * \file tee_mem.c
 * \brief Functions to manage a pool of memory chunks.
 *
 * This module provides basic functions to manage dynamically a fixed amount
 * of memory (memory region).  For this current implementation the provided
 * memory region used for the allocations should be physically AND logically
 * contiguous (only one region is supported for a given allocator).
 *
 * Principle of the allocator algorithm: "best fit"
 *
 */

#include <linux/slab.h>

#include "tee_mem.h"

#define _DUMP_INFO_ALLOCATOR 0
#define USE_DEVM_ALLOC 1

#ifndef USE_DEVM_ALLOC
#define _KMALLOC(s, f) kmalloc(s, f)
#define _KFREE(a)     kfree(a)
#else
#define _KMALLOC(s, f) devm_kzalloc(dev, s, f)
#define _KFREE(a)     devm_kfree(dev, a)
#endif

/**
 * \struct mem_chunk
 * \brief Elementary descriptor of an allocated memory block
 *
 * \param node      Node for linked list
 * \param counter   Reference counter
 *                  (0 indicates that the block is not used/freed)
 * \param size      Total size in bytes
 * \param paddr     Physical base address
 *
 * Elementary memory block definition
 */
struct mem_chunk {
	struct list_head node;
	uint32_t counter;
	size_t size;
	unsigned long paddr;
};

/**
 * \struct shm_pool
 * \brief Main structure to describe a shared memory pool
 *
 * \param size    Total size in bytes of the associated memory region
 * \param vaddr   Logical base address
 * \param paddr   Physical base address
 * \param used    Total size in bytes of the used memory
 * \param mchunks     List head for handle the elementary memory blocks
 *
 * Shared memory pool structure definition
 */
struct shm_pool {
	struct mutex lock;
	size_t size;		/* Size of pool/heap memory segment */
	size_t used;		/* Number of bytes allocated */
	void *vaddr;		/* Associated Virtual address */
	unsigned long paddr;	/* Associated Physical address */
	bool cached;		/* true if pool is cacheable */
	struct list_head mchunks;	/* Head of memory chunk/block list */
};

#define __CALCULATE_RATIO_MEM_USED(a) (((a->used)*100)/(a->size))

/**
 * \brief Dumps the information of the shared memory pool
 *
 * \param pool           Pointer on the pool
 * \param detailforced   Flag to force the log for the detailed informations
 *
 * Dump/log the meta data of the shared memory pool on the standard output.
 *
 */
void tee_shm_pool_dump(struct device *dev, struct shm_pool *pool, bool forced)
{
	struct mem_chunk *chunk;

	if (WARN_ON(!dev || !pool))
		return;

	dev_info(dev,
		 "tee_shm_pool_dump() poolH(0x%p) pAddr=0x%p vAddr=0x%p size=%zu used=%zu(%zu%%)\n",
		 (void *)pool,
		 (void *)pool->paddr,
		 (void *)pool->vaddr,
		 pool->size, pool->used, __CALCULATE_RATIO_MEM_USED(pool));

	if ((pool->used != 0) || (forced == true)) {
		dev_info(dev, "  \\ HEAD next:[0x%p] prev:[0x%p]\n",
			 (void *)pool->mchunks.next,
			 (void *)pool->mchunks.prev);

		dev_info(dev,
			 "  |-[@]        next       prev       pAddr      size     refCount\n");

		list_for_each_entry(chunk, &pool->mchunks, node) {
			dev_info(dev, "  | [0x%p] 0x%p 0x%p 0x%p %08zu %d\n",
				 (void *)chunk,
				 (void *)chunk->node.next,
				 (void *)chunk->node.prev,
				 (void *)chunk->paddr,
				 chunk->size, chunk->counter);
		}
	}
}

bool tee_shm_pool_is_cached(struct shm_pool *pool)
{
	return pool->cached;
}

void tee_shm_pool_set_cached(struct shm_pool *pool)
{
	pool->cached = true;
}

/**
 * \brief Creates and returns a new shared memory pool manager structure
 *
 * \param shm_size      Size of the associated memory chunk
 * \param shm_vaddr     Virtual/logical base address
 * \param shm_paddr     Physical base address
 *
 * \return Reference of the created shared memory pool manager
 *
 * Create and initialize a shared memory pool manager.
 * The description of the memory region (shm_size, shm_vaddr, shm_paddr)
 * which is passed should be a physically AND virtually contiguous
 * (no check is performed by the function).
 * If a error is detected returned pool is NULL.
 */
struct shm_pool *tee_shm_pool_create(struct device *dev, size_t shm_size,
				     void *shm_vaddr, unsigned long shm_paddr)
{
	struct mem_chunk *chunk = NULL;
	struct shm_pool *pool = NULL;

	if (WARN_ON(!dev))
		goto alloc_failed;

	dev_dbg(dev, "> vaddr=0x%p, paddr=0x%p, size=%zuKiB\n",
		shm_vaddr, (void *)shm_paddr, shm_size / 1024);

	/* Alloc and initialize the shm_pool structure */
	pool = _KMALLOC(sizeof(struct shm_pool), GFP_KERNEL);
	if (!pool) {
		dev_err(dev, "kmalloc <struct shm_pool> failed\n");
		goto alloc_failed;
	}
	memset(pool, 0, sizeof(*pool));
	mutex_init(&pool->lock);
	mutex_lock(&pool->lock);

	INIT_LIST_HEAD(&(pool->mchunks));
	pool->size = shm_size;
	pool->vaddr = shm_vaddr;
	pool->paddr = shm_paddr;

	/* Create the initial elementary memory chunk    */
	/* which handles the whole memory region         */
	chunk = _KMALLOC(sizeof(struct mem_chunk), GFP_KERNEL);
	if (!chunk) {
		dev_err(dev, "kmalloc <struct MemChunk> failed\n");
		goto alloc_failed;
	}
	memset(chunk, 0, sizeof(*chunk));
	chunk->paddr = shm_paddr;
	chunk->size = shm_size;

	/* Adds the new entry immediately after the list head */
	list_add(&(chunk->node), &(pool->mchunks));

#if defined(_DUMP_INFO_ALLOCATOR) && (_DUMP_INFO_ALLOCATOR > 0)
	tee_shm_pool_dump(dev, pool, true);
#endif
	dev_dbg(dev, "< poolH(0x%p) chunkH=0x%p\n",
		(void *)(pool), (void *)chunk);

	mutex_unlock(&pool->lock);
	return pool;

alloc_failed:
	if (chunk)
		_KFREE(chunk);

	if (pool)
		_KFREE(pool);

	return NULL;
}

/**
 * Local helper function to check that the physical address is valid
 */
static inline int is_valid_paddr(struct shm_pool *pool, unsigned long paddr)
{
	return (paddr >= pool->paddr && paddr < (pool->paddr + pool->size));
}

/**
 * Local helper function to check that the virtual address is valid
 */
static inline int is_valid_vaddr(struct shm_pool *pool, void *vaddr)
{
	return (vaddr >= pool->vaddr && vaddr < (pool->vaddr + pool->size));
}

/**
 * \brief Destroy the shared memory pool manager
 *
 * \param pool           Pointer on the pool
 *
 * Destroy a memory pool manager
 *
 */
void tee_shm_pool_destroy(struct device *dev, struct shm_pool *pool)
{
	struct mem_chunk *chunk;

	if (WARN_ON(!dev || !pool))
		return;

	dev_dbg(dev, "> poolH(0x%p)\n", (void *)pool);

#if defined(_DUMP_INFO_ALLOCATOR) && (_DUMP_INFO_ALLOCATOR > 0)
	tee_shm_pool_dump(dev, pool, true);
#endif

	tee_shm_pool_reset(dev, pool);

	chunk = list_first_entry(&pool->mchunks, struct mem_chunk, node);
	dev_dbg(dev, "free chunkH=0x%p\n", (void *)chunk);
	_KFREE(chunk);
	_KFREE(pool);

	dev_dbg(dev, "<\n");
}

/**
 * \brief Free all reserved chunk if any, and set pool at it initial state
 *
 * \param pool  Pointer on the pool
 *
 */
void tee_shm_pool_reset(struct device *dev, struct shm_pool *pool)
{
	struct mem_chunk *chunk;
	struct mem_chunk *tmp;
	struct mem_chunk *first = NULL;

	if (WARN_ON(!dev || !pool))
		return;

	mutex_lock(&pool->lock);

	list_for_each_entry_safe(chunk, tmp, &pool->mchunks, node) {
		if (first != NULL) {
			dev_err(dev, "Free lost chunkH=0x%p\n", (void *)chunk);
			list_del(&chunk->node);
			_KFREE(chunk);
		} else {
			first = chunk;
		}
	}

	first->counter = 0;
	first->paddr = pool->paddr;
	first->size = pool->size;
	pool->used = 0;

	mutex_unlock(&pool->lock);
}

/**
 * \brief Return the logical address
 *
 * \param pool          Pointer on the pool
 * \param paddr         Physical address
 *
 * \return Virtual/logical address
 *
 * Return the associated virtual/logical address. The address should be inside
 * the range of addresses managed by the shm pool.
 *
 */
void *tee_shm_pool_p2v(struct device *dev, struct shm_pool *pool,
		       unsigned long paddr)
{
	if (WARN_ON(!dev || !pool))
		return NULL;

	mutex_lock(&pool->lock);
	if (!is_valid_paddr(pool, paddr)) {
		mutex_unlock(&pool->lock);
		dev_err(dev,
			"tee_shm_pool_p2v() paddr=0x%p not in the shm pool\n",
			(void *)paddr);
		return NULL;
	} else {
		unsigned long offset = paddr - pool->paddr;
		void *p = (void *)((unsigned long)pool->vaddr + offset);

		mutex_unlock(&pool->lock);
		return p;
	}
}

/**
 * \brief Return the physical address
 *
 * \param pool          Pointer on the pool
 * \param vaddr         Logical/Virtual address
 *
 * \return Physical address
 *
 * Return the associated physical address. The address should be inside
 * the range of addresses managed by the pool.
 *
 */
unsigned long tee_shm_pool_v2p(struct device *dev, struct shm_pool *pool,
			       void *vaddr)
{
	if (WARN_ON(!dev || !pool))
		return 0UL;

	mutex_lock(&pool->lock);
	if (!is_valid_vaddr(pool, vaddr)) {
		dev_err(dev,
			"tee_shm_pool_v2p() vaddr=0x%p not in shm pool\n",
			(void *)vaddr);
		mutex_unlock(&pool->lock);
		return 0UL;
	} else {
		unsigned long offset = vaddr - pool->vaddr;
		unsigned long p = pool->paddr + offset;

		mutex_unlock(&pool->lock);
		return p;
	}
}

/**
 * \brief Allocate a new block of memory
 *
 * \param pool          Pointer on the pool
 * \param size          Expected size (in byte)
 * \param alignment     Alignment constraint (in byte)
 *
 * \return Physical base address of the allocated block
 *
 * Allocate a memory chunk inside the memory region managed by the pool.
 *
 */
unsigned long rk_tee_shm_pool_alloc(struct device *dev,
				 struct shm_pool *pool,
				 size_t size, size_t alignment)
{
	struct mem_chunk *chunk;
	struct mem_chunk *betterchunk = NULL;
	struct mem_chunk *prev_chunk = NULL;
	struct mem_chunk *next_chunk = NULL;
	unsigned long begAddr;
	unsigned long endAddr;

	if (WARN_ON(!dev || !pool))
		return 0UL;

	dev_dbg(dev, "> poolH(%p:%p:%x) size=0x%zx align=0x%zx\n",
		pool, (void *)pool->paddr, (unsigned int)pool->size, size,
		alignment);

	/* Align on cache line of the target */
	/* \todo(jmd) Should be defined by a global target specific parameter */
	/*  size = (size + (32-1)) & ~(32-1)            */

	if (ALIGN(size, 0x20) < size)
		goto failed_out;

	if (alignment == 0)
		alignment = 1;

	size = ALIGN(size, 0x20);

	alignment = ALIGN(alignment, 0x20);

	if (size > (pool->size - pool->used))
		goto failed_out;

	mutex_lock(&pool->lock);

	/**
	 * Algorithm: Smallest waste (best fit): We choose the block that has the
	 * smallest waste. In other words we choose the block so that
	 * size(b) - size is as small as possible.
	 */
	list_for_each_entry(chunk, &pool->mchunks, node) {
		if (chunk->counter == 0) {	/* Free chunk */
			begAddr = ALIGN(chunk->paddr, alignment);
			endAddr = begAddr + size;

			if (begAddr >= chunk->paddr
			    && endAddr <= (chunk->paddr + chunk->size)
			    && (betterchunk == NULL
				/* Always split smaller block */
				|| chunk->size < betterchunk->size))
				betterchunk = chunk;
		}
	}

	/**
	 * Update the linked list
	 */
	if (betterchunk != NULL) {
		prev_chunk = _KMALLOC(sizeof(struct mem_chunk), GFP_KERNEL);
		next_chunk = _KMALLOC(sizeof(struct mem_chunk), GFP_KERNEL);

		if ((!prev_chunk) || (!next_chunk))
			goto failed_out_unlock;

		begAddr = ALIGN(betterchunk->paddr, alignment);
		endAddr = begAddr + size;

		if (betterchunk->paddr < begAddr) {
			/* memory between begin of chunk and begin
			 * of created memory => create a free chunk */
			prev_chunk->counter = 0;
			prev_chunk->paddr = betterchunk->paddr;
			prev_chunk->size = begAddr - betterchunk->paddr;

			betterchunk->paddr = begAddr;
			betterchunk->size -= prev_chunk->size;

			dev_dbg(dev,
				"create p_chunkH=0x%p paddr=0x%p (s=%zu)\n",
				(void *)prev_chunk,
				(void *)prev_chunk->paddr, prev_chunk->size);

			list_add_tail(&(prev_chunk->node),
				      &(betterchunk->node));
			prev_chunk = NULL;
		} else {
			_KFREE(prev_chunk);
		}

		if (betterchunk->paddr + betterchunk->size > endAddr) {
			/* memory between end of chunk and end of
			 * created memory => create a free chunk */
			next_chunk->counter = 0;
			next_chunk->paddr = endAddr;
			next_chunk->size = betterchunk->size - size;

			dev_dbg(dev,
				"create n_chunkH=0x%p paddr=0x%p (s=%zu)\n",
				(void *)next_chunk,
				(void *)next_chunk->paddr, next_chunk->size);

			betterchunk->size = size;

			list_add(&(next_chunk->node), &(betterchunk->node));
			next_chunk = NULL;
		} else {
			_KFREE(next_chunk);
		}

		betterchunk->counter = 1;
		pool->used += size;

		mutex_unlock(&pool->lock);

#if defined(_DUMP_INFO_ALLOCATOR) && (_DUMP_INFO_ALLOCATOR > 1)
		tee_shm_pool_dump(dev, pool, false);
#endif

		dev_dbg(dev,
			"< chunkH=0x%p paddr=%p (s=%zu) align=0x%zx\n",
			(void *)betterchunk,
			(void *)betterchunk->paddr,
			betterchunk->size, alignment);

		return betterchunk->paddr;
	}

failed_out_unlock:
	mutex_unlock(&pool->lock);
failed_out:
	if (prev_chunk)
		_KFREE(prev_chunk);
	if (next_chunk)
		_KFREE(next_chunk);

	dev_err(dev,
		"rk_tee_shm_pool_alloc() FAILED, size=0x%zx, align=0x%zx free=%zu\n",
		size, alignment, pool->size - pool->used);

#if defined(_DUMP_INFO_ALLOCATOR) && (_DUMP_INFO_ALLOCATOR > 1)
	tee_shm_pool_dump(dev, pool, true);
#endif

	return 0UL;
}

/**
 * \brief Release a allocated block of memory
 *
 * \param pool          Pointer on the pool
 * \param paddr         Physical @ of the block which must be released
 * \param size          Reference to return the size of the block
 *
 * Free a allocated memory block inside
 * the memory region managed by the pool.
 *
 */
int rk_tee_shm_pool_free(struct device *dev, struct shm_pool *pool,
		      unsigned long paddr, size_t *size)
{
	struct mem_chunk *chunk;

	if (WARN_ON(!dev || !pool))
		return -EINVAL;

	dev_dbg(dev, "> Try to free ... poolH(0x%p) paddr=0x%p\n",
		(void *)pool, (void *)paddr);

#if defined(_DUMP_INFO_ALLOCATOR) && (_DUMP_INFO_ALLOCATOR > 1)
	tee_shm_pool_dump(dev, pool, false);
#endif

	mutex_lock(&pool->lock);

	if (!is_valid_paddr(pool, paddr))
		goto out_failed;

	list_for_each_entry(chunk, &pool->mchunks, node) {
		if (chunk->paddr == paddr) {
			if (size != NULL)
				*size = chunk->size;

			if (chunk->counter == 0) {
				dev_warn(dev,
					 "< tee_shm_pool_free() WARNING, paddr=0x%p already released\n",
					 (void *)paddr);
				return -EINVAL;
			} else if (--chunk->counter == 0) {
				dev_dbg(dev, "paddr=%p\n", (void *)paddr);

				pool->used -= chunk->size;

				/* Merge with previous */
				if (chunk->node.prev != &pool->mchunks) {
					struct mem_chunk *prev =
					    list_entry(chunk->node.prev,
						       struct mem_chunk, node);
					if (prev->counter == 0) {
						dev_dbg(dev,
							"chunkH=0x%p paddr=0x%p free ok\n",
							(void *)chunk,
							(void *)paddr);
						prev->size += chunk->size;
						list_del(&chunk->node);
						_KFREE(chunk);
						chunk = prev;
					}
				}
				/* Merge with next */
				if (chunk->node.next != &pool->mchunks) {
					struct mem_chunk *next =
					    list_entry(chunk->node.next,
						       struct mem_chunk, node);
					if (next->counter == 0) {
						dev_dbg(dev,
							"chunkH=0x%p paddr=0x%p free ok\n",
							(void *)chunk,
							(void *)paddr);
						chunk->size += next->size;
						list_del(&next->node);
						_KFREE(next);
					}
				}
				mutex_unlock(&pool->lock);

#if defined(_DUMP_INFO_ALLOCATOR) && (_DUMP_INFO_ALLOCATOR > 1)
				tee_shm_pool_dump(dev, pool, false);
#endif
				dev_dbg(dev, "< freed\n");
				return 0;

			} else {
				mutex_unlock(&pool->lock);
				dev_dbg(dev,
					"< paddr=0x%p  (--) refcounter is decremented ret=1\n",
					(void *)paddr);
				return 1;
			}
		}
	}

out_failed:
	mutex_unlock(&pool->lock);
#if defined(_DUMP_INFO_ALLOCATOR) && (_DUMP_INFO_ALLOCATOR > 1)
	tee_shm_pool_dump(dev, pool, false);
#endif
	dev_err(dev,
		"< tee_shm_pool_free() FAILED, pAddr=0x%p not found\n",
		(void *)paddr);
	return -EINVAL;
}

/**
 * \brief Increase the reference count of the memory chunk
 *
 * \param pool          Pointer on the pool
 * \param paddr         Physical address
 *
 * \return true if successful (false otherwise)
 *
 * Increment the reference count of the allocated block of memory.
 * paddr should a valid address returned by the tee_shm_pool_alloc().
 *
 */
bool tee_shm_pool_incref(struct device *dev, struct shm_pool *pool,
			 unsigned long paddr)
{
	struct mem_chunk *chunk;

	if (WARN_ON(!dev || !pool))
		return false;

	mutex_lock(&pool->lock);

	if (!is_valid_paddr(pool, paddr))
		goto out_failed;

	list_for_each_entry(chunk, &pool->mchunks, node) {
		if (chunk->paddr == paddr) {
			dev_dbg(dev,
				"pAddr=%p (++) refcounter is incremented\n",
				(void *)paddr);
			chunk->counter++;

#if defined(_DUMP_INFO_ALLOCATOR) && (_DUMP_INFO_ALLOCATOR > 0)
			tee_shm_pool_dump(dev, pool, false);
#endif
			mutex_unlock(&pool->lock);
			return true;
		}
	}

out_failed:
	mutex_unlock(&pool->lock);

	dev_err(dev,
		"tee_shm_pool_incref() FAILED, pAddr=%p is not a valid @\n",
		(void *)paddr);

	return false;
}
