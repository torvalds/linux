/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KASAN_H
#define _LINUX_KASAN_H

#include <linux/bug.h>
#include <linux/kasan-enabled.h>
#include <linux/kasan-tags.h>
#include <linux/kernel.h>
#include <linux/static_key.h>
#include <linux/types.h>

struct kmem_cache;
struct page;
struct slab;
struct vm_struct;
struct task_struct;

#ifdef CONFIG_KASAN

#include <linux/linkage.h>
#include <asm/kasan.h>

#endif

typedef unsigned int __bitwise kasan_vmalloc_flags_t;

#define KASAN_VMALLOC_NONE		((__force kasan_vmalloc_flags_t)0x00u)
#define KASAN_VMALLOC_INIT		((__force kasan_vmalloc_flags_t)0x01u)
#define KASAN_VMALLOC_VM_ALLOC		((__force kasan_vmalloc_flags_t)0x02u)
#define KASAN_VMALLOC_PROT_NORMAL	((__force kasan_vmalloc_flags_t)0x04u)

#define KASAN_VMALLOC_PAGE_RANGE 0x1 /* Apply exsiting page range */
#define KASAN_VMALLOC_TLB_FLUSH  0x2 /* TLB flush */

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)

#include <linux/pgtable.h>

/* Software KASAN implementations use shadow memory. */

#ifdef CONFIG_KASAN_SW_TAGS
/* This matches KASAN_TAG_INVALID. */
#define KASAN_SHADOW_INIT 0xFE
#else
#define KASAN_SHADOW_INIT 0
#endif

#ifndef PTE_HWTABLE_PTRS
#define PTE_HWTABLE_PTRS 0
#endif

extern unsigned char kasan_early_shadow_page[PAGE_SIZE];
extern pte_t kasan_early_shadow_pte[MAX_PTRS_PER_PTE + PTE_HWTABLE_PTRS];
extern pmd_t kasan_early_shadow_pmd[MAX_PTRS_PER_PMD];
extern pud_t kasan_early_shadow_pud[MAX_PTRS_PER_PUD];
extern p4d_t kasan_early_shadow_p4d[MAX_PTRS_PER_P4D];

int kasan_populate_early_shadow(const void *shadow_start,
				const void *shadow_end);

#ifndef kasan_mem_to_shadow
static inline void *kasan_mem_to_shadow(const void *addr)
{
	return (void *)((unsigned long)addr >> KASAN_SHADOW_SCALE_SHIFT)
		+ KASAN_SHADOW_OFFSET;
}
#endif

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

#ifdef CONFIG_KASAN_HW_TAGS

#else /* CONFIG_KASAN_HW_TAGS */

#endif /* CONFIG_KASAN_HW_TAGS */

static inline bool kasan_has_integrated_init(void)
{
	return kasan_hw_tags_enabled();
}

#ifdef CONFIG_KASAN
void __kasan_unpoison_range(const void *addr, size_t size);
static __always_inline void kasan_unpoison_range(const void *addr, size_t size)
{
	if (kasan_enabled())
		__kasan_unpoison_range(addr, size);
}

void __kasan_poison_pages(struct page *page, unsigned int order, bool init);
static __always_inline void kasan_poison_pages(struct page *page,
						unsigned int order, bool init)
{
	if (kasan_enabled())
		__kasan_poison_pages(page, order, init);
}

bool __kasan_unpoison_pages(struct page *page, unsigned int order, bool init);
static __always_inline bool kasan_unpoison_pages(struct page *page,
						 unsigned int order, bool init)
{
	if (kasan_enabled())
		return __kasan_unpoison_pages(page, order, init);
	return false;
}

void __kasan_poison_slab(struct slab *slab);
static __always_inline void kasan_poison_slab(struct slab *slab)
{
	if (kasan_enabled())
		__kasan_poison_slab(slab);
}

void __kasan_unpoison_new_object(struct kmem_cache *cache, void *object);
/**
 * kasan_unpoison_new_object - Temporarily unpoison a new slab object.
 * @cache: Cache the object belong to.
 * @object: Pointer to the object.
 *
 * This function is intended for the slab allocator's internal use. It
 * temporarily unpoisons an object from a newly allocated slab without doing
 * anything else. The object must later be repoisoned by
 * kasan_poison_new_object().
 */
static __always_inline void kasan_unpoison_new_object(struct kmem_cache *cache,
							void *object)
{
	if (kasan_enabled())
		__kasan_unpoison_new_object(cache, object);
}

void __kasan_poison_new_object(struct kmem_cache *cache, void *object);
/**
 * kasan_poison_new_object - Repoison a new slab object.
 * @cache: Cache the object belong to.
 * @object: Pointer to the object.
 *
 * This function is intended for the slab allocator's internal use. It
 * repoisons an object that was previously unpoisoned by
 * kasan_unpoison_new_object() without doing anything else.
 */
static __always_inline void kasan_poison_new_object(struct kmem_cache *cache,
							void *object)
{
	if (kasan_enabled())
		__kasan_poison_new_object(cache, object);
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

bool __kasan_slab_pre_free(struct kmem_cache *s, void *object,
			unsigned long ip);
/**
 * kasan_slab_pre_free - Check whether freeing a slab object is safe.
 * @object: Object to be freed.
 *
 * This function checks whether freeing the given object is safe. It may
 * check for double-free and invalid-free bugs and report them.
 *
 * This function is intended only for use by the slab allocator.
 *
 * @Return true if freeing the object is unsafe; false otherwise.
 */
static __always_inline bool kasan_slab_pre_free(struct kmem_cache *s,
						void *object)
{
	if (kasan_enabled())
		return __kasan_slab_pre_free(s, object, _RET_IP_);
	return false;
}

bool __kasan_slab_free(struct kmem_cache *s, void *object, bool init,
		       bool still_accessible, bool no_quarantine);
/**
 * kasan_slab_free - Poison, initialize, and quarantine a slab object.
 * @object: Object to be freed.
 * @init: Whether to initialize the object.
 * @still_accessible: Whether the object contents are still accessible.
 *
 * This function informs that a slab object has been freed and is not
 * supposed to be accessed anymore, except when @still_accessible is set
 * (indicating that the object is in a SLAB_TYPESAFE_BY_RCU cache and an RCU
 * grace period might not have passed yet).
 *
 * For KASAN modes that have integrated memory initialization
 * (kasan_has_integrated_init() == true), this function also initializes
 * the object's memory. For other modes, the @init argument is ignored.
 *
 * This function might also take ownership of the object to quarantine it.
 * When this happens, KASAN will defer freeing the object to a later
 * stage and handle it internally until then. The return value indicates
 * whether KASAN took ownership of the object.
 *
 * This function is intended only for use by the slab allocator.
 *
 * @Return true if KASAN took ownership of the object; false otherwise.
 */
static __always_inline bool kasan_slab_free(struct kmem_cache *s,
					    void *object, bool init,
					    bool still_accessible,
					    bool no_quarantine)
{
	if (kasan_enabled())
		return __kasan_slab_free(s, object, init, still_accessible,
					 no_quarantine);
	return false;
}

void __kasan_kfree_large(void *ptr, unsigned long ip);
static __always_inline void kasan_kfree_large(void *ptr)
{
	if (kasan_enabled())
		__kasan_kfree_large(ptr, _RET_IP_);
}

void * __must_check __kasan_slab_alloc(struct kmem_cache *s,
				       void *object, gfp_t flags, bool init);
static __always_inline void * __must_check kasan_slab_alloc(
		struct kmem_cache *s, void *object, gfp_t flags, bool init)
{
	if (kasan_enabled())
		return __kasan_slab_alloc(s, object, flags, init);
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

bool __kasan_mempool_poison_pages(struct page *page, unsigned int order,
				  unsigned long ip);
/**
 * kasan_mempool_poison_pages - Check and poison a mempool page allocation.
 * @page: Pointer to the page allocation.
 * @order: Order of the allocation.
 *
 * This function is intended for kernel subsystems that cache page allocations
 * to reuse them instead of freeing them back to page_alloc (e.g. mempool).
 *
 * This function is similar to kasan_mempool_poison_object() but operates on
 * page allocations.
 *
 * Before the poisoned allocation can be reused, it must be unpoisoned via
 * kasan_mempool_unpoison_pages().
 *
 * Return: true if the allocation can be safely reused; false otherwise.
 */
static __always_inline bool kasan_mempool_poison_pages(struct page *page,
						       unsigned int order)
{
	if (kasan_enabled())
		return __kasan_mempool_poison_pages(page, order, _RET_IP_);
	return true;
}

void __kasan_mempool_unpoison_pages(struct page *page, unsigned int order,
				    unsigned long ip);
/**
 * kasan_mempool_unpoison_pages - Unpoison a mempool page allocation.
 * @page: Pointer to the page allocation.
 * @order: Order of the allocation.
 *
 * This function is intended for kernel subsystems that cache page allocations
 * to reuse them instead of freeing them back to page_alloc (e.g. mempool).
 *
 * This function unpoisons a page allocation that was previously poisoned by
 * kasan_mempool_poison_pages() without zeroing the allocation's memory. For
 * the tag-based modes, this function assigns a new tag to the allocation.
 */
static __always_inline void kasan_mempool_unpoison_pages(struct page *page,
							 unsigned int order)
{
	if (kasan_enabled())
		__kasan_mempool_unpoison_pages(page, order, _RET_IP_);
}

bool __kasan_mempool_poison_object(void *ptr, unsigned long ip);
/**
 * kasan_mempool_poison_object - Check and poison a mempool slab allocation.
 * @ptr: Pointer to the slab allocation.
 *
 * This function is intended for kernel subsystems that cache slab allocations
 * to reuse them instead of freeing them back to the slab allocator (e.g.
 * mempool).
 *
 * This function poisons a slab allocation and saves a free stack trace for it
 * without initializing the allocation's memory and without putting it into the
 * quarantine (for the Generic mode).
 *
 * This function also performs checks to detect double-free and invalid-free
 * bugs and reports them. The caller can use the return value of this function
 * to find out if the allocation is buggy.
 *
 * Before the poisoned allocation can be reused, it must be unpoisoned via
 * kasan_mempool_unpoison_object().
 *
 * This function operates on all slab allocations including large kmalloc
 * allocations (the ones returned by kmalloc_large() or by kmalloc() with the
 * size > KMALLOC_MAX_SIZE).
 *
 * Return: true if the allocation can be safely reused; false otherwise.
 */
static __always_inline bool kasan_mempool_poison_object(void *ptr)
{
	if (kasan_enabled())
		return __kasan_mempool_poison_object(ptr, _RET_IP_);
	return true;
}

void __kasan_mempool_unpoison_object(void *ptr, size_t size, unsigned long ip);
/**
 * kasan_mempool_unpoison_object - Unpoison a mempool slab allocation.
 * @ptr: Pointer to the slab allocation.
 * @size: Size to be unpoisoned.
 *
 * This function is intended for kernel subsystems that cache slab allocations
 * to reuse them instead of freeing them back to the slab allocator (e.g.
 * mempool).
 *
 * This function unpoisons a slab allocation that was previously poisoned via
 * kasan_mempool_poison_object() and saves an alloc stack trace for it without
 * initializing the allocation's memory. For the tag-based modes, this function
 * does not assign a new tag to the allocation and instead restores the
 * original tags based on the pointer value.
 *
 * This function operates on all slab allocations including large kmalloc
 * allocations (the ones returned by kmalloc_large() or by kmalloc() with the
 * size > KMALLOC_MAX_SIZE).
 */
static __always_inline void kasan_mempool_unpoison_object(void *ptr,
							  size_t size)
{
	if (kasan_enabled())
		__kasan_mempool_unpoison_object(ptr, size, _RET_IP_);
}

/*
 * Unlike kasan_check_read/write(), kasan_check_byte() is performed even for
 * the hardware tag-based mode that doesn't rely on compiler instrumentation.
 */
bool __kasan_check_byte(const void *addr, unsigned long ip);
static __always_inline bool kasan_check_byte(const void *addr)
{
	if (kasan_enabled())
		return __kasan_check_byte(addr, _RET_IP_);
	return true;
}

#else /* CONFIG_KASAN */

static inline void kasan_unpoison_range(const void *address, size_t size) {}
static inline void kasan_poison_pages(struct page *page, unsigned int order,
				      bool init) {}
static inline bool kasan_unpoison_pages(struct page *page, unsigned int order,
					bool init)
{
	return false;
}
static inline void kasan_poison_slab(struct slab *slab) {}
static inline void kasan_unpoison_new_object(struct kmem_cache *cache,
					void *object) {}
static inline void kasan_poison_new_object(struct kmem_cache *cache,
					void *object) {}
static inline void *kasan_init_slab_obj(struct kmem_cache *cache,
				const void *object)
{
	return (void *)object;
}

static inline bool kasan_slab_pre_free(struct kmem_cache *s, void *object)
{
	return false;
}

static inline bool kasan_slab_free(struct kmem_cache *s, void *object,
				   bool init, bool still_accessible,
				   bool no_quarantine)
{
	return false;
}
static inline void kasan_kfree_large(void *ptr) {}
static inline void *kasan_slab_alloc(struct kmem_cache *s, void *object,
				   gfp_t flags, bool init)
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
static inline bool kasan_mempool_poison_pages(struct page *page, unsigned int order)
{
	return true;
}
static inline void kasan_mempool_unpoison_pages(struct page *page, unsigned int order) {}
static inline bool kasan_mempool_poison_object(void *ptr)
{
	return true;
}
static inline void kasan_mempool_unpoison_object(void *ptr, size_t size) {}

static inline bool kasan_check_byte(const void *address)
{
	return true;
}

#endif /* CONFIG_KASAN */

#if defined(CONFIG_KASAN) && defined(CONFIG_KASAN_STACK)
void kasan_unpoison_task_stack(struct task_struct *task);
asmlinkage void kasan_unpoison_task_stack_below(const void *watermark);
#else
static inline void kasan_unpoison_task_stack(struct task_struct *task) {}
static inline void kasan_unpoison_task_stack_below(const void *watermark) {}
#endif

#ifdef CONFIG_KASAN_GENERIC

struct kasan_cache {
	int alloc_meta_offset;
	int free_meta_offset;
};

size_t kasan_metadata_size(struct kmem_cache *cache, bool in_object);
void kasan_cache_create(struct kmem_cache *cache, unsigned int *size,
			slab_flags_t *flags);

void kasan_cache_shrink(struct kmem_cache *cache);
void kasan_cache_shutdown(struct kmem_cache *cache);
void kasan_record_aux_stack(void *ptr);

#else /* CONFIG_KASAN_GENERIC */

/* Tag-based KASAN modes do not use per-object metadata. */
static inline size_t kasan_metadata_size(struct kmem_cache *cache,
						bool in_object)
{
	return 0;
}
/* And no cache-related metadata initialization is required. */
static inline void kasan_cache_create(struct kmem_cache *cache,
				      unsigned int *size,
				      slab_flags_t *flags) {}

static inline void kasan_cache_shrink(struct kmem_cache *cache) {}
static inline void kasan_cache_shutdown(struct kmem_cache *cache) {}
static inline void kasan_record_aux_stack(void *ptr) {}

#endif /* CONFIG_KASAN_GENERIC */

#if defined(CONFIG_KASAN_SW_TAGS) || defined(CONFIG_KASAN_HW_TAGS)

static inline void *kasan_reset_tag(const void *addr)
{
	return (void *)arch_kasan_reset_tag(addr);
}

/**
 * kasan_report - print a report about a bad memory access detected by KASAN
 * @addr: address of the bad access
 * @size: size of the bad access
 * @is_write: whether the bad access is a write or a read
 * @ip: instruction pointer for the accessibility check or the bad access itself
 */
bool kasan_report(const void *addr, size_t size,
		bool is_write, unsigned long ip);

#else /* CONFIG_KASAN_SW_TAGS || CONFIG_KASAN_HW_TAGS */

static inline void *kasan_reset_tag(const void *addr)
{
	return (void *)addr;
}

#endif /* CONFIG_KASAN_SW_TAGS || CONFIG_KASAN_HW_TAGS*/

#ifdef CONFIG_KASAN_HW_TAGS

void kasan_report_async(void);

#endif /* CONFIG_KASAN_HW_TAGS */

#ifdef CONFIG_KASAN_GENERIC
void __init kasan_init_generic(void);
#else
static inline void kasan_init_generic(void) { }
#endif

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

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)

void kasan_populate_early_vm_area_shadow(void *start, unsigned long size);
int kasan_populate_vmalloc(unsigned long addr, unsigned long size, gfp_t gfp_mask);
void kasan_release_vmalloc(unsigned long start, unsigned long end,
			   unsigned long free_region_start,
			   unsigned long free_region_end,
			   unsigned long flags);

#else /* CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS */

static inline void kasan_populate_early_vm_area_shadow(void *start,
						       unsigned long size)
{ }
static inline int kasan_populate_vmalloc(unsigned long start,
					unsigned long size, gfp_t gfp_mask)
{
	return 0;
}
static inline void kasan_release_vmalloc(unsigned long start,
					 unsigned long end,
					 unsigned long free_region_start,
					 unsigned long free_region_end,
					 unsigned long flags) { }

#endif /* CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS */

void *__kasan_unpoison_vmalloc(const void *start, unsigned long size,
			       kasan_vmalloc_flags_t flags);
static __always_inline void *kasan_unpoison_vmalloc(const void *start,
						unsigned long size,
						kasan_vmalloc_flags_t flags)
{
	if (kasan_enabled())
		return __kasan_unpoison_vmalloc(start, size, flags);
	return (void *)start;
}

void __kasan_poison_vmalloc(const void *start, unsigned long size);
static __always_inline void kasan_poison_vmalloc(const void *start,
						 unsigned long size)
{
	if (kasan_enabled())
		__kasan_poison_vmalloc(start, size);
}

#else /* CONFIG_KASAN_VMALLOC */

static inline void kasan_populate_early_vm_area_shadow(void *start,
						       unsigned long size) { }
static inline int kasan_populate_vmalloc(unsigned long start,
					unsigned long size, gfp_t gfp_mask)
{
	return 0;
}
static inline void kasan_release_vmalloc(unsigned long start,
					 unsigned long end,
					 unsigned long free_region_start,
					 unsigned long free_region_end,
					 unsigned long flags) { }

static inline void *kasan_unpoison_vmalloc(const void *start,
					   unsigned long size,
					   kasan_vmalloc_flags_t flags)
{
	return (void *)start;
}
static inline void kasan_poison_vmalloc(const void *start, unsigned long size)
{ }

#endif /* CONFIG_KASAN_VMALLOC */

#if (defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)) && \
		!defined(CONFIG_KASAN_VMALLOC)

/*
 * These functions allocate and free shadow memory for kernel modules.
 * They are only required when KASAN_VMALLOC is not supported, as otherwise
 * shadow memory is allocated by the generic vmalloc handlers.
 */
int kasan_alloc_module_shadow(void *addr, size_t size, gfp_t gfp_mask);
void kasan_free_module_shadow(const struct vm_struct *vm);

#else /* (CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS) && !CONFIG_KASAN_VMALLOC */

static inline int kasan_alloc_module_shadow(void *addr, size_t size, gfp_t gfp_mask) { return 0; }
static inline void kasan_free_module_shadow(const struct vm_struct *vm) {}

#endif /* (CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS) && !CONFIG_KASAN_VMALLOC */

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)
void kasan_non_canonical_hook(unsigned long addr);
#else /* CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS */
static inline void kasan_non_canonical_hook(unsigned long addr) { }
#endif /* CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS */

#endif /* LINUX_KASAN_H */
