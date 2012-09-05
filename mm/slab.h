#ifndef MM_SLAB_H
#define MM_SLAB_H
/*
 * Internal slab definitions
 */

/*
 * State of the slab allocator.
 *
 * This is used to describe the states of the allocator during bootup.
 * Allocators use this to gradually bootstrap themselves. Most allocators
 * have the problem that the structures used for managing slab caches are
 * allocated from slab caches themselves.
 */
enum slab_state {
	DOWN,			/* No slab functionality yet */
	PARTIAL,		/* SLUB: kmem_cache_node available */
	PARTIAL_ARRAYCACHE,	/* SLAB: kmalloc size for arraycache available */
	PARTIAL_L3,		/* SLAB: kmalloc size for l3 struct available */
	UP,			/* Slab caches usable but not all extras yet */
	FULL			/* Everything is working */
};

extern enum slab_state slab_state;

/* The slab cache mutex protects the management structures during changes */
extern struct mutex slab_mutex;

/* The list of all slab caches on the system */
extern struct list_head slab_caches;

/* The slab cache that manages slab cache information */
extern struct kmem_cache *kmem_cache;

/* Functions provided by the slab allocators */
struct kmem_cache *__kmem_cache_create(const char *name, size_t size,
	size_t align, unsigned long flags, void (*ctor)(void *));

#ifdef CONFIG_SLUB
struct kmem_cache *__kmem_cache_alias(const char *name, size_t size,
	size_t align, unsigned long flags, void (*ctor)(void *));
extern int sysfs_slab_add(struct kmem_cache *s);
#else
static inline struct kmem_cache *__kmem_cache_alias(const char *name, size_t size,
	size_t align, unsigned long flags, void (*ctor)(void *))
{ return NULL; }
static inline int sysfs_slab_add(struct kmem_cache *s) { return 0; }

#endif


int __kmem_cache_shutdown(struct kmem_cache *);

#endif
