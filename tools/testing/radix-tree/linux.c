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
int kmalloc_verbose;
int test_verbose;

struct kmem_cache {
	pthread_mutex_t lock;
	int size;
	int nr_objs;
	void *objs;
	void (*ctor)(void *);
};

void *kmem_cache_alloc(struct kmem_cache *cachep, int flags)
{
	struct radix_tree_node *node;

	if (flags & __GFP_NOWARN)
		return NULL;

	pthread_mutex_lock(&cachep->lock);
	if (cachep->nr_objs) {
		cachep->nr_objs--;
		node = cachep->objs;
		cachep->objs = node->parent;
		pthread_mutex_unlock(&cachep->lock);
		node->parent = NULL;
	} else {
		pthread_mutex_unlock(&cachep->lock);
		node = malloc(cachep->size);
		if (cachep->ctor)
			cachep->ctor(node);
	}

	uatomic_inc(&nr_allocated);
	if (kmalloc_verbose)
		printf("Allocating %p from slab\n", node);
	return node;
}

void kmem_cache_free(struct kmem_cache *cachep, void *objp)
{
	assert(objp);
	uatomic_dec(&nr_allocated);
	if (kmalloc_verbose)
		printf("Freeing %p to slab\n", objp);
	pthread_mutex_lock(&cachep->lock);
	if (cachep->nr_objs > 10) {
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

void *kmalloc(size_t size, gfp_t gfp)
{
	void *ret = malloc(size);
	uatomic_inc(&nr_allocated);
	if (kmalloc_verbose)
		printf("Allocating %p from malloc\n", ret);
	return ret;
}

void kfree(void *p)
{
	if (!p)
		return;
	uatomic_dec(&nr_allocated);
	if (kmalloc_verbose)
		printf("Freeing %p to malloc\n", p);
	free(p);
}

struct kmem_cache *
kmem_cache_create(const char *name, size_t size, size_t offset,
	unsigned long flags, void (*ctor)(void *))
{
	struct kmem_cache *ret = malloc(sizeof(*ret));

	pthread_mutex_init(&ret->lock, NULL);
	ret->size = size;
	ret->nr_objs = 0;
	ret->objs = NULL;
	ret->ctor = ctor;
	return ret;
}
