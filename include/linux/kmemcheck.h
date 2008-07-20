#ifndef LINUX_KMEMCHECK_H
#define LINUX_KMEMCHECK_H

#include <linux/mm_types.h>
#include <linux/types.h>

#ifdef CONFIG_KMEMCHECK
extern int kmemcheck_enabled;

/* The slab-related functions. */
void kmemcheck_alloc_shadow(struct kmem_cache *s, gfp_t flags, int node,
			    struct page *page, int order);
void kmemcheck_free_shadow(struct kmem_cache *s, struct page *page, int order);
void kmemcheck_slab_alloc(struct kmem_cache *s, gfp_t gfpflags, void *object,
			  size_t size);
void kmemcheck_slab_free(struct kmem_cache *s, void *object, size_t size);

void kmemcheck_show_pages(struct page *p, unsigned int n);
void kmemcheck_hide_pages(struct page *p, unsigned int n);

bool kmemcheck_page_is_tracked(struct page *p);

void kmemcheck_mark_unallocated(void *address, unsigned int n);
void kmemcheck_mark_uninitialized(void *address, unsigned int n);
void kmemcheck_mark_initialized(void *address, unsigned int n);
void kmemcheck_mark_freed(void *address, unsigned int n);

void kmemcheck_mark_unallocated_pages(struct page *p, unsigned int n);
void kmemcheck_mark_uninitialized_pages(struct page *p, unsigned int n);

int kmemcheck_show_addr(unsigned long address);
int kmemcheck_hide_addr(unsigned long address);
#else
#define kmemcheck_enabled 0

static inline void
kmemcheck_alloc_shadow(struct kmem_cache *s, gfp_t flags, int node,
		       struct page *page, int order)
{
}

static inline void
kmemcheck_free_shadow(struct kmem_cache *s, struct page *page, int order)
{
}

static inline void
kmemcheck_slab_alloc(struct kmem_cache *s, gfp_t gfpflags, void *object,
		     size_t size)
{
}

static inline void kmemcheck_slab_free(struct kmem_cache *s, void *object,
				       size_t size)
{
}

static inline bool kmemcheck_page_is_tracked(struct page *p)
{
	return false;
}

static inline void kmemcheck_mark_unallocated(void *address, unsigned int n)
{
}

static inline void kmemcheck_mark_uninitialized(void *address, unsigned int n)
{
}

static inline void kmemcheck_mark_initialized(void *address, unsigned int n)
{
}

static inline void kmemcheck_mark_freed(void *address, unsigned int n)
{
}
#endif /* CONFIG_KMEMCHECK */

#endif /* LINUX_KMEMCHECK_H */
