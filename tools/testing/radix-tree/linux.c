#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <assert.h>

#include <linux/mempool.h>
#include <linux/slab.h>
#include <urcu/uatomic.h>

int nr_allocated;

void *mempool_alloc(mempool_t *pool, int gfp_mask)
{
	return pool->alloc(gfp_mask, pool->data);
}

void mempool_free(void *element, mempool_t *pool)
{
	pool->free(element, pool->data);
}

mempool_t *mempool_create(int min_nr, mempool_alloc_t *alloc_fn,
			mempool_free_t *free_fn, void *pool_data)
{
	mempool_t *ret = malloc(sizeof(*ret));

	ret->alloc = alloc_fn;
	ret->free = free_fn;
	ret->data = pool_data;
	return ret;
}

void *kmem_cache_alloc(struct kmem_cache *cachep, int flags)
{
	void *ret = malloc(cachep->size);
	if (cachep->ctor)
		cachep->ctor(ret);
	uatomic_inc(&nr_allocated);
	return ret;
}

void kmem_cache_free(struct kmem_cache *cachep, void *objp)
{
	assert(objp);
	uatomic_dec(&nr_allocated);
	memset(objp, 0, cachep->size);
	free(objp);
}

struct kmem_cache *
kmem_cache_create(const char *name, size_t size, size_t offset,
	unsigned long flags, void (*ctor)(void *))
{
	struct kmem_cache *ret = malloc(sizeof(*ret));

	ret->size = size;
	ret->ctor = ctor;
	return ret;
}
