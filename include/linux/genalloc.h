/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Basic general purpose allocator for managing special purpose
 * memory, for example, memory that is not managed by the regular
 * kmalloc/kfree interface.  Uses for this includes on-device special
 * memory, uncached memory etc.
 *
 * It is safe to use the allocator in NMI handlers and other special
 * unblockable contexts that could otherwise deadlock on locks.  This
 * is implemented by using atomic operations and retries on any
 * conflicts.  The disadvantage is that there may be livelocks in
 * extreme cases.  For better scalability, one allocator can be used
 * for each CPU.
 *
 * The lockless operation only works if there is enough memory
 * available.  If new memory is added to the pool a lock has to be
 * still taken.  So any user relying on locklessness has to ensure
 * that sufficient memory is preallocated.
 *
 * The basic atomic operation of this allocator is cmpxchg on long.
 * On architectures that don't have NMI-safe cmpxchg implementation,
 * the allocator can NOT be used in NMI handler.  So code uses the
 * allocator in NMI handler should depend on
 * CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG.
 */


#ifndef __GENALLOC_H__
#define __GENALLOC_H__

#include <linux/types.h>
#include <linux/spinlock_types.h>
#include <linux/atomic.h>

struct device;
struct device_node;
struct gen_pool;

/**
 * typedef genpool_algo_t: Allocation callback function type definition
 * @map: Pointer to bitmap
 * @size: The bitmap size in bits
 * @start: The bitnumber to start searching at
 * @nr: The number of zeroed bits we're looking for
 * @data: optional additional data used by the callback
 * @pool: the pool being allocated from
 */
typedef unsigned long (*genpool_algo_t)(unsigned long *map,
			unsigned long size,
			unsigned long start,
			unsigned int nr,
			void *data, struct gen_pool *pool,
			unsigned long start_addr);

/*
 *  General purpose special memory pool descriptor.
 */
struct gen_pool {
	spinlock_t lock;
	struct list_head chunks;	/* list of chunks in this pool */
	int min_alloc_order;		/* minimum allocation order */

	genpool_algo_t algo;		/* allocation function */
	void *data;

	const char *name;
};

/*
 *  General purpose special memory pool chunk descriptor.
 */
struct gen_pool_chunk {
	struct list_head next_chunk;	/* next chunk in pool */
	atomic_long_t avail;
	phys_addr_t phys_addr;		/* physical starting address of memory chunk */
	void *owner;			/* private data to retrieve at alloc time */
	unsigned long start_addr;	/* start address of memory chunk */
	unsigned long end_addr;		/* end address of memory chunk (inclusive) */
	unsigned long bits[0];		/* bitmap for allocating memory chunk */
};

/*
 *  gen_pool data descriptor for gen_pool_first_fit_align.
 */
struct genpool_data_align {
	int align;		/* alignment by bytes for starting address */
};

/*
 *  gen_pool data descriptor for gen_pool_fixed_alloc.
 */
struct genpool_data_fixed {
	unsigned long offset;		/* The offset of the specific region */
};

extern struct gen_pool *gen_pool_create(int, int);
extern phys_addr_t gen_pool_virt_to_phys(struct gen_pool *pool, unsigned long);
extern int gen_pool_add_owner(struct gen_pool *, unsigned long, phys_addr_t,
			     size_t, int, void *);

static inline int gen_pool_add_virt(struct gen_pool *pool, unsigned long addr,
		phys_addr_t phys, size_t size, int nid)
{
	return gen_pool_add_owner(pool, addr, phys, size, nid, NULL);
}

/**
 * gen_pool_add - add a new chunk of special memory to the pool
 * @pool: pool to add new memory chunk to
 * @addr: starting address of memory chunk to add to pool
 * @size: size in bytes of the memory chunk to add to pool
 * @nid: node id of the node the chunk structure and bitmap should be
 *       allocated on, or -1
 *
 * Add a new chunk of special memory to the specified pool.
 *
 * Returns 0 on success or a -ve errno on failure.
 */
static inline int gen_pool_add(struct gen_pool *pool, unsigned long addr,
			       size_t size, int nid)
{
	return gen_pool_add_virt(pool, addr, -1, size, nid);
}
extern void gen_pool_destroy(struct gen_pool *);
unsigned long gen_pool_alloc_algo_owner(struct gen_pool *pool, size_t size,
		genpool_algo_t algo, void *data, void **owner);

static inline unsigned long gen_pool_alloc_owner(struct gen_pool *pool,
		size_t size, void **owner)
{
	return gen_pool_alloc_algo_owner(pool, size, pool->algo, pool->data,
			owner);
}

static inline unsigned long gen_pool_alloc_algo(struct gen_pool *pool,
		size_t size, genpool_algo_t algo, void *data)
{
	return gen_pool_alloc_algo_owner(pool, size, algo, data, NULL);
}

/**
 * gen_pool_alloc - allocate special memory from the pool
 * @pool: pool to allocate from
 * @size: number of bytes to allocate from the pool
 *
 * Allocate the requested number of bytes from the specified pool.
 * Uses the pool allocation function (with first-fit algorithm by default).
 * Can not be used in NMI handler on architectures without
 * NMI-safe cmpxchg implementation.
 */
static inline unsigned long gen_pool_alloc(struct gen_pool *pool, size_t size)
{
	return gen_pool_alloc_algo(pool, size, pool->algo, pool->data);
}

extern void *gen_pool_dma_alloc(struct gen_pool *pool, size_t size,
		dma_addr_t *dma);
extern void gen_pool_free_owner(struct gen_pool *pool, unsigned long addr,
		size_t size, void **owner);
static inline void gen_pool_free(struct gen_pool *pool, unsigned long addr,
                size_t size)
{
	gen_pool_free_owner(pool, addr, size, NULL);
}

extern void gen_pool_for_each_chunk(struct gen_pool *,
	void (*)(struct gen_pool *, struct gen_pool_chunk *, void *), void *);
extern size_t gen_pool_avail(struct gen_pool *);
extern size_t gen_pool_size(struct gen_pool *);

extern void gen_pool_set_algo(struct gen_pool *pool, genpool_algo_t algo,
		void *data);

extern unsigned long gen_pool_first_fit(unsigned long *map, unsigned long size,
		unsigned long start, unsigned int nr, void *data,
		struct gen_pool *pool, unsigned long start_addr);

extern unsigned long gen_pool_fixed_alloc(unsigned long *map,
		unsigned long size, unsigned long start, unsigned int nr,
		void *data, struct gen_pool *pool, unsigned long start_addr);

extern unsigned long gen_pool_first_fit_align(unsigned long *map,
		unsigned long size, unsigned long start, unsigned int nr,
		void *data, struct gen_pool *pool, unsigned long start_addr);


extern unsigned long gen_pool_first_fit_order_align(unsigned long *map,
		unsigned long size, unsigned long start, unsigned int nr,
		void *data, struct gen_pool *pool, unsigned long start_addr);

extern unsigned long gen_pool_best_fit(unsigned long *map, unsigned long size,
		unsigned long start, unsigned int nr, void *data,
		struct gen_pool *pool, unsigned long start_addr);


extern struct gen_pool *devm_gen_pool_create(struct device *dev,
		int min_alloc_order, int nid, const char *name);
extern struct gen_pool *gen_pool_get(struct device *dev, const char *name);

bool addr_in_gen_pool(struct gen_pool *pool, unsigned long start,
			size_t size);

#ifdef CONFIG_OF
extern struct gen_pool *of_gen_pool_get(struct device_node *np,
	const char *propname, int index);
#else
static inline struct gen_pool *of_gen_pool_get(struct device_node *np,
	const char *propname, int index)
{
	return NULL;
}
#endif
#endif /* __GENALLOC_H__ */
