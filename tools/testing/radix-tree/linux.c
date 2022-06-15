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
};

void *kmem_cache_alloc_lru(struct kmem_cache *cachep, struct list_lru *lru,
		int gfp)
{
	void *p;

	if (!(gfp & __GFP_DIRECT_RECLAIM))
		return NULL;

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

	uatomic_inc(&nr_allocated);
	if (kmalloc_verbose)
		printf("Allocating %p from slab\n", p);
	return p;
}

void kmem_cache_free(struct kmem_cache *cachep, void *objp)
{
	assert(objp);
	uatomic_dec(&nr_allocated);
	if (kmalloc_verbose)
		printf("Freeing %p to slab\n", objp);
	pthread_mutex_lock(&cachep->lock);
	if (cachep->nr_objs > 10 || cachep->align) {
		memset(objp, POISON_FREE, cachep->size);
		free(objp);
	} else {
		struct radix_tree_node *node = objp;
		cachep->nr_objs++;
		node->parent = cachep->objs;
		cachep->objs = node;
	}
	pthread_mutex_unlock(&cachep->lock);
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
	ret->objs = NULL;
	ret->ctor = ctor;
	return ret;
}
