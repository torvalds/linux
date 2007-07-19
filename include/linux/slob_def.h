#ifndef __LINUX_SLOB_DEF_H
#define __LINUX_SLOB_DEF_H

void *kmem_cache_alloc_node(struct kmem_cache *, gfp_t flags, int node);

static inline void *kmem_cache_alloc(struct kmem_cache *cachep, gfp_t flags)
{
	return kmem_cache_alloc_node(cachep, flags, -1);
}

void *__kmalloc_node(size_t size, gfp_t flags, int node);

static inline void *kmalloc_node(size_t size, gfp_t flags, int node)
{
	return __kmalloc_node(size, flags, node);
}

/**
 * kmalloc - allocate memory
 * @size: how many bytes of memory are required.
 * @flags: the type of memory to allocate (see kcalloc).
 *
 * kmalloc is the normal method of allocating memory
 * in the kernel.
 */
static inline void *kmalloc(size_t size, gfp_t flags)
{
	return __kmalloc_node(size, flags, -1);
}

static inline void *__kmalloc(size_t size, gfp_t flags)
{
	return kmalloc(size, flags);
}

#endif /* __LINUX_SLOB_DEF_H */
