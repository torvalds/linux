/*
 * Basic general purpose allocator for managing special purpose memory
 * not managed by the regular kmalloc/kfree interface.
 * Uses for this includes on-device special memory, uncached memory
 * etc.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */


#ifndef __GENALLOC_H__
#define __GENALLOC_H__
/*
 *  General purpose special memory pool descriptor.
 */
struct gen_pool {
	rwlock_t lock;
	struct list_head chunks;	/* list of chunks in this pool */
	int min_alloc_order;		/* minimum allocation order */
};

/*
 *  General purpose special memory pool chunk descriptor.
 */
struct gen_pool_chunk {
	spinlock_t lock;
	struct list_head next_chunk;	/* next chunk in pool */
	phys_addr_t phys_addr;		/* physical starting address of memory chunk */
	unsigned long start_addr;	/* starting address of memory chunk */
	unsigned long end_addr;		/* ending address of memory chunk */
	unsigned long bits[0];		/* bitmap for allocating memory chunk */
};

extern struct gen_pool *gen_pool_create(int, int);
extern phys_addr_t gen_pool_virt_to_phys(struct gen_pool *pool, unsigned long);
extern int gen_pool_add_virt(struct gen_pool *, unsigned long, phys_addr_t,
			     size_t, int);
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
extern unsigned long gen_pool_alloc(struct gen_pool *, size_t);
extern void gen_pool_free(struct gen_pool *, unsigned long, size_t);
#endif /* __GENALLOC_H__ */
