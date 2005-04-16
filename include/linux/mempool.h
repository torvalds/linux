/*
 * memory buffer pool support
 */
#ifndef _LINUX_MEMPOOL_H
#define _LINUX_MEMPOOL_H

#include <linux/wait.h>

typedef void * (mempool_alloc_t)(unsigned int __nocast gfp_mask, void *pool_data);
typedef void (mempool_free_t)(void *element, void *pool_data);

typedef struct mempool_s {
	spinlock_t lock;
	int min_nr;		/* nr of elements at *elements */
	int curr_nr;		/* Current nr of elements at *elements */
	void **elements;

	void *pool_data;
	mempool_alloc_t *alloc;
	mempool_free_t *free;
	wait_queue_head_t wait;
} mempool_t;
extern mempool_t * mempool_create(int min_nr, mempool_alloc_t *alloc_fn,
				 mempool_free_t *free_fn, void *pool_data);
extern int mempool_resize(mempool_t *pool, int new_min_nr, unsigned int __nocast gfp_mask);
extern void mempool_destroy(mempool_t *pool);
extern void * mempool_alloc(mempool_t *pool, unsigned int __nocast gfp_mask);
extern void mempool_free(void *element, mempool_t *pool);

/*
 * A mempool_alloc_t and mempool_free_t that get the memory from
 * a slab that is passed in through pool_data.
 */
void *mempool_alloc_slab(unsigned int __nocast gfp_mask, void *pool_data);
void mempool_free_slab(void *element, void *pool_data);

#endif /* _LINUX_MEMPOOL_H */
