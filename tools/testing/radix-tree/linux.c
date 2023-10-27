// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

#include <linux/gfp.h>
#include <linux/poison.h>
#include <linux/slab.h>
#include <linux/radix-tree.h>
#include <urcu/uatomic.h>

int nr_allocated;
int preempt_count;
int test_verbose;

struct kmem_cache {
	pthread_mutex_t lock;
	unsigned int size;
	unsigned int align;
	int nr_objs;
	void *objs;
	void (*ctor)(void *);
	unsigned int non_kernel;
	unsigned long nr_allocated;
	unsigned long nr_tallocated;
};

void kmem_cache_set_non_kernel(struct kmem_cache *cachep, unsigned int val)
{
	cachep->non_kernel = val;
}

unsigned long kmem_cache_get_alloc(struct kmem_cache *cachep)
{
	return cachep->size * cachep->nr_allocated;
}

unsigned long kmem_cache_nr_allocated(struct kmem_cache *cachep)
{
	return cachep->nr_allocated;
}

unsigned long kmem_cache_nr_tallocated(struct kmem_cache *cachep)
{
	return cachep->nr_tallocated;
}

void kmem_cache_zero_nr_tallocated(struct kmem_cache *cachep)
{
	cachep->nr_tallocated = 0;
}

void *kmem_cache_alloc_lru(struct kmem_cache *cachep, struct list_lru *lru,
		int gfp)
{
	void *p;

	if (!(gfp & __GFP_DIRECT_RECLAIM)) {
		if (!cachep->non_kernel)
			return NULL;

		cachep->non_kernel--;
	}

	pthread_mutex_lock(&cachep->lock);
	if (cachep->nr_objs) {
		struct radix_tree_node *node = cachep->objs;
		cachep->nr_objs--;
		cachep->objs = node->parent;
		pthread_mutex_unlock(&cachep->lock);
		node->parent = NULL;
		p = node;
	} else {
		pthread_mutex_unlock(&cachep->lock);
		if (cachep->align)
			posix_memalign(&p, cachep->align, cachep->size);
		else
			p = malloc(cachep->size);
		if (cachep->ctor)
			cachep->ctor(p);
		else if (gfp & __GFP_ZERO)
			memset(p, 0, cachep->size);
	}

	uatomic_inc(&cachep->nr_allocated);
	uatomic_inc(&nr_allocated);
	uatomic_inc(&cachep->nr_tallocated);
	if (kmalloc_verbose)
		printf("Allocating %p from slab\n", p);
	return p;
}

void __kmem_cache_free_locked(struct kmem_cache *cachep, void *objp)
{
	assert(objp);
	if (cachep->nr_objs > 10 || cachep->align) {
		memset(objp, POISON_FREE, cachep->size);
		free(objp);
	} else {
		struct radix_tree_node *node = objp;
		cachep->nr_objs++;
		node->parent = cachep->objs;
		cachep->objs = node;
	}
}

void kmem_cache_free_locked(struct kmem_cache *cachep, void *objp)
{
	uatomic_dec(&nr_allocated);
	uatomic_dec(&cachep->nr_allocated);
	if (kmalloc_verbose)
		printf("Freeing %p to slab\n", objp);
	__kmem_cache_free_locked(cachep, objp);
}

void kmem_cache_free(struct kmem_cache *cachep, void *objp)
{
	pthread_mutex_lock(&cachep->lock);
	kmem_cache_free_locked(cachep, objp);
	pthread_mutex_unlock(&cachep->lock);
}

void kmem_cache_free_bulk(struct kmem_cache *cachep, size_t size, void **list)
{
	if (kmalloc_verbose)
		pr_debug("Bulk free %p[0-%lu]\n", list, size - 1);

	pthread_mutex_lock(&cachep->lock);
	for (int i = 0; i < size; i++)
		kmem_cache_free_locked(cachep, list[i]);
	pthread_mutex_unlock(&cachep->lock);
}

void kmem_cache_shrink(struct kmem_cache *cachep)
{
}

int kmem_cache_alloc_bulk(struct kmem_cache *cachep, gfp_t gfp, size_t size,
			  void **p)
{
	size_t i;

	if (kmalloc_verbose)
		pr_debug("Bulk alloc %lu\n", size);

	pthread_mutex_lock(&cachep->lock);
	if (cachep->nr_objs >= size) {
		struct radix_tree_node *node;

		for (i = 0; i < size; i++) {
			if (!(gfp & __GFP_DIRECT_RECLAIM)) {
				if (!cachep->non_kernel)
					break;
				cachep->non_kernel--;
			}

			node = cachep->objs;
			cachep->nr_objs--;
			cachep->objs = node->parent;
			p[i] = node;
			node->parent = NULL;
		}
		pthread_mutex_unlock(&cachep->lock);
	} else {
		pthread_mutex_unlock(&cachep->lock);
		for (i = 0; i < size; i++) {
			if (!(gfp & __GFP_DIRECT_RECLAIM)) {
				if (!cachep->non_kernel)
					break;
				cachep->non_kernel--;
			}

			if (cachep->align) {
				posix_memalign(&p[i], cachep->align,
					       cachep->size * size);
			} else {
				p[i] = malloc(cachep->size * size);
				if (!p[i])
					break;
			}
			if (cachep->ctor)
				cachep->ctor(p[i]);
			else if (gfp & __GFP_ZERO)
				memset(p[i], 0, cachep->size);
		}
	}

	if (i < size) {
		size = i;
		pthread_mutex_lock(&cachep->lock);
		for (i = 0; i < size; i++)
			__kmem_cache_free_locked(cachep, p[i]);
		pthread_mutex_unlock(&cachep->lock);
		return 0;
	}

	for (i = 0; i < size; i++) {
		uatomic_inc(&nr_allocated);
		uatomic_inc(&cachep->nr_allocated);
		uatomic_inc(&cachep->nr_tallocated);
		if (kmalloc_verbose)
			printf("Allocating %p from slab\n", p[i]);
	}

	return size;
}

struct kmem_cache *
kmem_cache_create(const char *name, unsigned int size, unsigned int align,
		unsigned int flags, void (*ctor)(void *))
{
	struct kmem_cache *ret = malloc(sizeof(*ret));

	pthread_mutex_init(&ret->lock, NULL);
	ret->size = size;
	ret->align = align;
	ret->nr_objs = 0;
	ret->nr_allocated = 0;
	ret->nr_tallocated = 0;
	ret->objs = NULL;
	ret->ctor = ctor;
	ret->non_kernel = 0;
	return ret;
}

/*
 * Test the test infrastructure for kem_cache_alloc/free and bulk counterparts.
 */
void test_kmem_cache_bulk(void)
{
	int i;
	void *list[12];
	static struct kmem_cache *test_cache, *test_cache2;

	/*
	 * Testing the bulk allocators without aligned kmem_cache to force the
	 * bulk alloc/free to reuse
	 */
	test_cache = kmem_cache_create("test_cache", 256, 0, SLAB_PANIC, NULL);

	for (i = 0; i < 5; i++)
		list[i] = kmem_cache_alloc(test_cache, __GFP_DIRECT_RECLAIM);

	for (i = 0; i < 5; i++)
		kmem_cache_free(test_cache, list[i]);
	assert(test_cache->nr_objs == 5);

	kmem_cache_alloc_bulk(test_cache, __GFP_DIRECT_RECLAIM, 5, list);
	kmem_cache_free_bulk(test_cache, 5, list);

	for (i = 0; i < 12 ; i++)
		list[i] = kmem_cache_alloc(test_cache, __GFP_DIRECT_RECLAIM);

	for (i = 0; i < 12; i++)
		kmem_cache_free(test_cache, list[i]);

	/* The last free will not be kept around */
	assert(test_cache->nr_objs == 11);

	/* Aligned caches will immediately free */
	test_cache2 = kmem_cache_create("test_cache2", 128, 128, SLAB_PANIC, NULL);

	kmem_cache_alloc_bulk(test_cache2, __GFP_DIRECT_RECLAIM, 10, list);
	kmem_cache_free_bulk(test_cache2, 10, list);
	assert(!test_cache2->nr_objs);


}
