/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KASAN_H
#define _LINUX_KASAN_H

#include <linux/static_key.h>
#include <linux/types.h>

struct kmem_cache;
struct page;
struct vm_struct;
struct task_struct;

#ifdef CONFIG_KASAN

#include <linux/linkage.h>
#include <asm/kasan.h>

/* kasan_data struct is used in KUnit tests for KASAN expected failures */
struct kunit_kasan_expectation {
	bool report_expected;
	bool report_found;
};

#endif

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)

#include <linux/pgtable.h>

/* Software KASAN implementations use shadow memory. */

#ifdef CONFIG_KASAN_SW_TAGS
#define KASAN_SHADOW_INIT 0xFF
#else
#define KASAN_SHADOW_INIT 0
#endif

#ifndef PTE_HWTABLE_PTRS
#define PTE_HWTABLE_PTRS 0
#endif

extern unsigned char kasan_early_shadow_page[PAGE_SIZE];
extern pte_t kasan_early_shadow_pte[PTRS_PER_PTE + PTE_HWTABLE_PTRS];
extern pmd_t kasan_early_shadow_pmd[PTRS_PER_PMD];
extern pud_t kasan_early_shadow_pud[PTRS_PER_PUD];
extern p4d_t kasan_early_shadow_p4d[MAX_PTRS_PER_P4D];

int kasan_populate_early_shadow(const void *shadow_start,
				const void *shadow_end);

static inline void *kasan_mem_to_shadow(const void *addr)
{
	return (void *)((unsigned long)addr >> KASAN_SHADOW_SCALE_SHIFT)
		+ KASAN_SHADOW_OFFSET;
}

int kasan_add_zero_shadow(void *start, unsigned long size);
void kasan_remove_zero_shadow(void *start, unsigned long size);

/* Enable reporting bugs after kasan_disable_current() */
extern void kasan_enable_current(void);

/* Disable reporting bugs for current task */
extern void kasan_disable_current(void);

#else /* CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS */

static inline int kasan_add_zero_shadow(void *start, unsigned long size)
{
	return 0;
}
static inline void kasan_remove_zero_shadow(void *start,
					unsigned long size)
{}

static inline void kasan_enable_current(void) {}
static inline void kasan_disable_current(void) {}

#endif /* CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS */

#ifdef CONFIG_KASAN

struct kasan_cache {
	int alloc_meta_offset;
	int free_meta_offset;
};

#ifdef CONFIG_KASAN_HW_TAGS

DECLARE_STATIC_KEY_FALSE(kasan_flag_enabled);

static __always_inline bool kasan_enabled(void)
{
	return static_branch_likely(&kasan_flag_enabled);
}

#else /* CONFIG_KASAN_HW_TAGS */

static inline bool kasan_enabled(void)
{
	return true;
}

#endif /* CONFIG_KASAN_HW_TAGS */

slab_flags_t __kasan_never_merge(void);
static __always_inline slab_flags_t kasan_never_merge(void)
{
	if (kasan_enabled())
		return __kasan_never_merge();
	return 0;
}

void __kasan_unpoison_range(const void *addr, size_t size);
static __always_inline void kasan_unpoison_range(const void *addr, size_t size)
{
	if (kasan_enabled())
		__kasan_unpoison_range(addr, size);
}

void __kasan_alloc_pages(struct page *page, unsigned int order);
static __always_inline void kasan_alloc_pages(struct page *page,
						unsigned int order)
{
	if (kasan_enabled())
		__kasan_alloc_pages(page, order);
}

void __kasan_free_pages(struct page *page, unsigned int order);
static __always_inline void kasan_free_pages(struct page *page,
						unsigned int order)
{
	if (kasan_enabled())
		__kasan_free_pages(page, order);
}

void __kasan_cache_create(struct kmem_cache *cache, unsigned int *size,
				slab_flags_t *flags);
static __always_inline void kasan_cache_create(struct kmem_cache *cache,
				unsigned int *size, slab_flags_t *flags)
{
	if (kasan_enabled())
		__kasan_cache_create(cache, size, flags);
}

size_t __kasan_metadata_size(struct kmem_cache *cache);
static __always_inline size_t kasan_metadata_size(struct kmem_cache *cache)
{
	if (kasan_enabled())
		return __kasan_metadata_size(cache);
	return 0;
}

void __kasan_poison_slab(struct page *page);
static __always_inline void kasan_poison_slab(struct page *page)
{
	if (kasan_enabled())
		__kasan_poison_slab(page);
}

void __kasan_unpoison_object_data(struct kmem_cache *cache, void *object);
static __always_inline void kasan_unpoison_object_data(struct kmem_cache *cache,
							void *object)
{
	if (kasan_enabled())
		__kasan_unpoison_object_data(cache, object);
}

void __kasan_poison_object_data(struct kmem_cache *cache, void *object);
static __always_inline void kasan_poison_object_data(struct kmem_cache *cache,
							void *object)
{
	if (kasan_enabled())
		__kasan_poison_object_data(cache, object);
}

void * __must_check __kasan_init_slab_obj(struct kmem_cache *cache,
					  const void *object);
static __always_inline void * __must_check kasan_init_slab_obj(
				struct kmem_cache *cache, const void *object)
{
	if (kasan_enabled())
		return __kasan_init_slab_obj(cache, object);
	return (void *)object;
}

bool __kasan_slab_free(struct kmem_cache *s, void *object, unsigned long ip);
static __always_inline bool kasan_slab_free(struct kmem_cache *s, void *object,
						unsigned long ip)
{
	if (kasan_enabled())
		return __kasan_slab_free(s, object, ip);
	return false;
}

void __kasan_slab_free_mempool(void *ptr, unsigned long ip);
static __always_inline void kasan_slab_free_mempool(void *ptr, unsigned long ip)
{
	if (kasan_enabled())
		__kasan_slab_free_mempool(ptr, ip);
}

void * __must_check __kasan_slab_alloc(struct kmem_cache *s,
				       void *object, gfp_t flags);
static __always_inline void * __must_check kasan_slab_alloc(
				struct kmem_cache *s, void *object, gfp_t flags)
{
	if (kasan_enabled())
		return __kasan_slab_alloc(s, object, flags);
	return object;
}

void * __must_check __kasan_kmalloc(struct kmem_cache *s, const void *object,
				    size_t size, gfp_t flags);
static __always_inline void * __must_check kasan_kmalloc(struct kmem_cache *s,
				const void *object, size_t size, gfp_t flags)
{
	if (kasan_enabled())
		return __kasan_kmalloc(s, object, size, flags);
	return (void *)object;
}

void * __must_check __kasan_kmalloc_large(const void *ptr,
					  size_t size, gfp_t flags);
static __always_inline void * __must_check kasan_kmalloc_large(const void *ptr,
						      size_t size, gfp_t flags)
{
	if (kasan_enabled())
		return __kasan_kmalloc_large(ptr, size, flags);
	return (void *)ptr;
}

void * __must_check __kasan_krealloc(const void *object,
				     size_t new_size, gfp_t flags);
static __always_inline void * __must_check kasan_krealloc(const void *object,
						 size_t new_size, gfp_t flags)
{
	if (kasan_enabled())
		return __kasan_krealloc(object, new_size, flags);
	return (void *)object;
}

void __kasan_kfree_large(void *ptr, unsigned long ip);
static __always_inline void kasan_kfree_large(void *ptr, unsigned long ip)
{
	if (kasan_enabled())
		__kasan_kfree_large(ptr, ip);
}

bool kasan_save_enable_multi_shot(void);
void kasan_restore_multi_shot(bool enabled);

#else /* CONFIG_KASAN */

static inline bool kasan_enabled(void)
{
	return false;
}
static inline slab_flags_t kasan_never_merge(void)
{
	return 0;
}
static inline void kasan_unpoison_range(const void *address, size_t size) {}
static inline void kasan_alloc_pages(struct page *page, unsigned int order) {}
static inline void kasan_free_pages(struct page *page, unsigned int order) {}
static inline void kasan_cache_create(struct kmem_cache *cache,
				      unsigned int *size,
				      slab_flags_t *flags) {}
static inline size_t kasan_metadata_size(struct kmem_cache *cache) { return 0; }
static inline void kasan_poison_slab(struct page *page) {}
static inline void kasan_unpoison_object_data(struct kmem_cache *cache,
					void *object) {}
static inline void kasan_poison_object_data(struct kmem_cache *cache,
					void *object) {}
static inline void *kasan_init_slab_obj(struct kmem_cache *cache,
				const void *object)
{
	return (void *)object;
}
static inline bool kasan_slab_free(struct kmem_cache *s, void *object,
				   unsigned long ip)
{
	return false;
}
static inline void kasan_slab_free_mempool(void *ptr, unsigned long ip) {}
static inline void *kasan_slab_alloc(struct kmem_cache *s, void *object,
				   gfp_t flags)
{
	return object;
}
static inline void *kasan_kmalloc(struct kmem_cache *s, const void *object,
				size_t size, gfp_t flags)
{
	return (void *)object;
}
static inline void *kasan_kmalloc_large(const void *ptr, size_t size, gfp_t flags)
{
	return (void *)ptr;
}
static inline void *kasan_krealloc(const void *object, size_t new_size,
				 gfp_t flags)
{
	return (void *)object;
}
static inline void kasan_kfree_large(void *ptr, unsigned long ip) {}

#endif /* CONFIG_KASAN */

#if defined(CONFIG_KASAN) && CONFIG_KASAN_STACK
void kasan_unpoison_task_stack(struct task_struct *task);
#else
static inline void kasan_unpoison_task_stack(struct task_struct *task) {}
#endif

#ifdef CONFIG_KASAN_GENERIC

void kasan_cache_shrink(struct kmem_cache *cache);
void kasan_cache_shutdown(struct kmem_cache *cache);
void kasan_record_aux_stack(void *ptr);

#else /* CONFIG_KASAN_GENERIC */

static inline void kasan_cache_shrink(struct kmem_cache *cache) {}
static inline void kasan_cache_shutdown(struct kmem_cache *cache) {}
static inline void kasan_record_aux_stack(void *ptr) {}

#endif /* CONFIG_KASAN_GENERIC */

#if defined(CONFIG_KASAN_SW_TAGS) || defined(CONFIG_KASAN_HW_TAGS)

static inline void *kasan_reset_tag(const void *addr)
{
	return (void *)arch_kasan_reset_tag(addr);
}

bool kasan_report(unsigned long addr, size_t size,
		bool is_write, unsigned long ip);

#else /* CONFIG_KASAN_SW_TAGS || CONFIG_KASAN_HW_TAGS */

static inline void *kasan_reset_tag(const void *addr)
{
	return (void *)addr;
}

#endif /* CONFIG_KASAN_SW_TAGS || CONFIG_KASAN_HW_TAGS*/

#ifdef CONFIG_KASAN_SW_TAGS
void __init kasan_init_sw_tags(void);
#else
static inline void kasan_init_sw_tags(void) { }
#endif

#ifdef CONFIG_KASAN_HW_TAGS
void kasan_init_hw_tags_cpu(void);
void __init kasan_init_hw_tags(void);
#else
static inline void kasan_init_hw_tags_cpu(void) { }
static inline void kasan_init_hw_tags(void) { }
#endif

#ifdef CONFIG_KASAN_VMALLOC

int kasan_populate_vmalloc(unsigned long addr, unsigned long size);
void kasan_poison_vmalloc(const void *start, unsigned long size);
void kasan_unpoison_vmalloc(const void *start, unsigned long size);
void kasan_release_vmalloc(unsigned long start, unsigned long end,
			   unsigned long free_region_start,
			   unsigned long free_region_end);

#else /* CONFIG_KASAN_VMALLOC */

static inline int kasan_populate_vmalloc(unsigned long start,
					unsigned long size)
{
	return 0;
}

static inline void kasan_poison_vmalloc(const void *start, unsigned long size)
{ }
static inline void kasan_unpoison_vmalloc(const void *start, unsigned long size)
{ }
static inline void kasan_release_vmalloc(unsigned long start,
					 unsigned long end,
					 unsigned long free_region_start,
					 unsigned long free_region_end) {}

#endif /* CONFIG_KASAN_VMALLOC */

#if (defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)) && \
		!defined(CONFIG_KASAN_VMALLOC)

/*
 * These functions provide a special case to support backing module
 * allocations with real shadow memory. With KASAN vmalloc, the special
 * case is unnecessary, as the work is handled in the generic case.
 */
int kasan_module_alloc(void *addr, size_t size);
void kasan_free_shadow(const struct vm_struct *vm);

#else /* (CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS) && !CONFIG_KASAN_VMALLOC */

static inline int kasan_module_alloc(void *addr, size_t size) { return 0; }
static inline void kasan_free_shadow(const struct vm_struct *vm) {}

#endif /* (CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS) && !CONFIG_KASAN_VMALLOC */

#ifdef CONFIG_KASAN_INLINE
void kasan_non_canonical_hook(unsigned long addr);
#else /* CONFIG_KASAN_INLINE */
static inline void kasan_non_canonical_hook(unsigned long addr) { }
#endif /* CONFIG_KASAN_INLINE */

#endif /* LINUX_KASAN_H */
