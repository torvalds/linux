#ifndef _LINUX_KASAN_H
#define _LINUX_KASAN_H

#include <linux/types.h>

struct kmem_cache;
struct page;
struct vm_struct;

#ifdef CONFIG_KASAN

#define KASAN_SHADOW_SCALE_SHIFT 3
#define KASAN_SHADOW_OFFSET _AC(CONFIG_KASAN_SHADOW_OFFSET, UL)

#include <asm/kasan.h>
#include <linux/sched.h>

static inline void *kasan_mem_to_shadow(const void *addr)
{
	return (void *)((unsigned long)addr >> KASAN_SHADOW_SCALE_SHIFT)
		+ KASAN_SHADOW_OFFSET;
}

/* Enable reporting bugs after kasan_disable_current() */
static inline void kasan_enable_current(void)
{
	current->kasan_depth++;
}

/* Disable reporting bugs for current task */
static inline void kasan_disable_current(void)
{
	current->kasan_depth--;
}

void kasan_unpoison_shadow(const void *address, size_t size);

void kasan_alloc_pages(struct page *page, unsigned int order);
void kasan_free_pages(struct page *page, unsigned int order);

void kasan_poison_slab(struct page *page);
void kasan_unpoison_object_data(struct kmem_cache *cache, void *object);
void kasan_poison_object_data(struct kmem_cache *cache, void *object);

void kasan_kmalloc_large(const void *ptr, size_t size);
void kasan_kfree_large(const void *ptr);
void kasan_kfree(void *ptr);
void kasan_kmalloc(struct kmem_cache *s, const void *object, size_t size);
void kasan_krealloc(const void *object, size_t new_size);

void kasan_slab_alloc(struct kmem_cache *s, void *object);
void kasan_slab_free(struct kmem_cache *s, void *object);

int kasan_module_alloc(void *addr, size_t size);
void kasan_free_shadow(const struct vm_struct *vm);

#else /* CONFIG_KASAN */

static inline void kasan_unpoison_shadow(const void *address, size_t size) {}

static inline void kasan_enable_current(void) {}
static inline void kasan_disable_current(void) {}

static inline void kasan_alloc_pages(struct page *page, unsigned int order) {}
static inline void kasan_free_pages(struct page *page, unsigned int order) {}

static inline void kasan_poison_slab(struct page *page) {}
static inline void kasan_unpoison_object_data(struct kmem_cache *cache,
					void *object) {}
static inline void kasan_poison_object_data(struct kmem_cache *cache,
					void *object) {}

static inline void kasan_kmalloc_large(void *ptr, size_t size) {}
static inline void kasan_kfree_large(const void *ptr) {}
static inline void kasan_kfree(void *ptr) {}
static inline void kasan_kmalloc(struct kmem_cache *s, const void *object,
				size_t size) {}
static inline void kasan_krealloc(const void *object, size_t new_size) {}

static inline void kasan_slab_alloc(struct kmem_cache *s, void *object) {}
static inline void kasan_slab_free(struct kmem_cache *s, void *object) {}

static inline int kasan_module_alloc(void *addr, size_t size) { return 0; }
static inline void kasan_free_shadow(const struct vm_struct *vm) {}

#endif /* CONFIG_KASAN */

#endif /* LINUX_KASAN_H */
