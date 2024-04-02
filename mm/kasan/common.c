// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains common KASAN code.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 *
 * Some code borrowed from https://github.com/xairy/kasan-prototype by
 *        Andrey Konovalov <andreyknvl@gmail.com>
 */

#include <linux/export.h>
#include <linux/init.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/memblock.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/stackdepot.h>
#include <linux/stacktrace.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/bug.h>

#include "kasan.h"
#include "../slab.h"

struct slab *kasan_addr_to_slab(const void *addr)
{
	if (virt_addr_valid(addr))
		return virt_to_slab(addr);
	return NULL;
}

depot_stack_handle_t kasan_save_stack(gfp_t flags, depot_flags_t depot_flags)
{
	unsigned long entries[KASAN_STACK_DEPTH];
	unsigned int nr_entries;

	nr_entries = stack_trace_save(entries, ARRAY_SIZE(entries), 0);
	return stack_depot_save_flags(entries, nr_entries, flags, depot_flags);
}

void kasan_set_track(struct kasan_track *track, depot_stack_handle_t stack)
{
#ifdef CONFIG_KASAN_EXTRA_INFO
	u32 cpu = raw_smp_processor_id();
	u64 ts_nsec = local_clock();

	track->cpu = cpu;
	track->timestamp = ts_nsec >> 9;
#endif /* CONFIG_KASAN_EXTRA_INFO */
	track->pid = current->pid;
	track->stack = stack;
}

void kasan_save_track(struct kasan_track *track, gfp_t flags)
{
	depot_stack_handle_t stack;

	stack = kasan_save_stack(flags, STACK_DEPOT_FLAG_CAN_ALLOC);
	kasan_set_track(track, stack);
}

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)
void kasan_enable_current(void)
{
	current->kasan_depth++;
}
EXPORT_SYMBOL(kasan_enable_current);

void kasan_disable_current(void)
{
	current->kasan_depth--;
}
EXPORT_SYMBOL(kasan_disable_current);

#endif /* CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS */

void __kasan_unpoison_range(const void *address, size_t size)
{
	if (is_kfence_address(address))
		return;

	kasan_unpoison(address, size, false);
}

#ifdef CONFIG_KASAN_STACK
/* Unpoison the entire stack for a task. */
void kasan_unpoison_task_stack(struct task_struct *task)
{
	void *base = task_stack_page(task);

	kasan_unpoison(base, THREAD_SIZE, false);
}

/* Unpoison the stack for the current task beyond a watermark sp value. */
asmlinkage void kasan_unpoison_task_stack_below(const void *watermark)
{
	/*
	 * Calculate the task stack base address.  Avoid using 'current'
	 * because this function is called by early resume code which hasn't
	 * yet set up the percpu register (%gs).
	 */
	void *base = (void *)((unsigned long)watermark & ~(THREAD_SIZE - 1));

	kasan_unpoison(base, watermark - base, false);
}
#endif /* CONFIG_KASAN_STACK */

bool __kasan_unpoison_pages(struct page *page, unsigned int order, bool init)
{
	u8 tag;
	unsigned long i;

	if (unlikely(PageHighMem(page)))
		return false;

	if (!kasan_sample_page_alloc(order))
		return false;

	tag = kasan_random_tag();
	kasan_unpoison(set_tag(page_address(page), tag),
		       PAGE_SIZE << order, init);
	for (i = 0; i < (1 << order); i++)
		page_kasan_tag_set(page + i, tag);

	return true;
}

void __kasan_poison_pages(struct page *page, unsigned int order, bool init)
{
	if (likely(!PageHighMem(page)))
		kasan_poison(page_address(page), PAGE_SIZE << order,
			     KASAN_PAGE_FREE, init);
}

void __kasan_poison_slab(struct slab *slab)
{
	struct page *page = slab_page(slab);
	unsigned long i;

	for (i = 0; i < compound_nr(page); i++)
		page_kasan_tag_reset(page + i);
	kasan_poison(page_address(page), page_size(page),
		     KASAN_SLAB_REDZONE, false);
}

void __kasan_unpoison_new_object(struct kmem_cache *cache, void *object)
{
	kasan_unpoison(object, cache->object_size, false);
}

void __kasan_poison_new_object(struct kmem_cache *cache, void *object)
{
	kasan_poison(object, round_up(cache->object_size, KASAN_GRANULE_SIZE),
			KASAN_SLAB_REDZONE, false);
}

/*
 * This function assigns a tag to an object considering the following:
 * 1. A cache might have a constructor, which might save a pointer to a slab
 *    object somewhere (e.g. in the object itself). We preassign a tag for
 *    each object in caches with constructors during slab creation and reuse
 *    the same tag each time a particular object is allocated.
 * 2. A cache might be SLAB_TYPESAFE_BY_RCU, which means objects can be
 *    accessed after being freed. We preassign tags for objects in these
 *    caches as well.
 */
static inline u8 assign_tag(struct kmem_cache *cache,
					const void *object, bool init)
{
	if (IS_ENABLED(CONFIG_KASAN_GENERIC))
		return 0xff;

	/*
	 * If the cache neither has a constructor nor has SLAB_TYPESAFE_BY_RCU
	 * set, assign a tag when the object is being allocated (init == false).
	 */
	if (!cache->ctor && !(cache->flags & SLAB_TYPESAFE_BY_RCU))
		return init ? KASAN_TAG_KERNEL : kasan_random_tag();

	/*
	 * For caches that either have a constructor or SLAB_TYPESAFE_BY_RCU,
	 * assign a random tag during slab creation, otherwise reuse
	 * the already assigned tag.
	 */
	return init ? kasan_random_tag() : get_tag(object);
}

void * __must_check __kasan_init_slab_obj(struct kmem_cache *cache,
						const void *object)
{
	/* Initialize per-object metadata if it is present. */
	if (kasan_requires_meta())
		kasan_init_object_meta(cache, object);

	/* Tag is ignored in set_tag() without CONFIG_KASAN_SW/HW_TAGS */
	object = set_tag(object, assign_tag(cache, object, true));

	return (void *)object;
}

static inline bool poison_slab_object(struct kmem_cache *cache, void *object,
				      unsigned long ip, bool init)
{
	void *tagged_object;

	if (!kasan_arch_is_ready())
		return false;

	tagged_object = object;
	object = kasan_reset_tag(object);

	if (unlikely(nearest_obj(cache, virt_to_slab(object), object) != object)) {
		kasan_report_invalid_free(tagged_object, ip, KASAN_REPORT_INVALID_FREE);
		return true;
	}

	/* RCU slabs could be legally used after free within the RCU period. */
	if (unlikely(cache->flags & SLAB_TYPESAFE_BY_RCU))
		return false;

	if (!kasan_byte_accessible(tagged_object)) {
		kasan_report_invalid_free(tagged_object, ip, KASAN_REPORT_DOUBLE_FREE);
		return true;
	}

	kasan_poison(object, round_up(cache->object_size, KASAN_GRANULE_SIZE),
			KASAN_SLAB_FREE, init);

	if (kasan_stack_collection_enabled())
		kasan_save_free_info(cache, tagged_object);

	return false;
}

bool __kasan_slab_free(struct kmem_cache *cache, void *object,
				unsigned long ip, bool init)
{
	if (is_kfence_address(object))
		return false;

	/*
	 * If the object is buggy, do not let slab put the object onto the
	 * freelist. The object will thus never be allocated again and its
	 * metadata will never get released.
	 */
	if (poison_slab_object(cache, object, ip, init))
		return true;

	/*
	 * If the object is put into quarantine, do not let slab put the object
	 * onto the freelist for now. The object's metadata is kept until the
	 * object gets evicted from quarantine.
	 */
	if (kasan_quarantine_put(cache, object))
		return true;

	/*
	 * Note: Keep per-object metadata to allow KASAN print stack traces for
	 * use-after-free-before-realloc bugs.
	 */

	/* Let slab put the object onto the freelist. */
	return false;
}

static inline bool check_page_allocation(void *ptr, unsigned long ip)
{
	if (!kasan_arch_is_ready())
		return false;

	if (ptr != page_address(virt_to_head_page(ptr))) {
		kasan_report_invalid_free(ptr, ip, KASAN_REPORT_INVALID_FREE);
		return true;
	}

	if (!kasan_byte_accessible(ptr)) {
		kasan_report_invalid_free(ptr, ip, KASAN_REPORT_DOUBLE_FREE);
		return true;
	}

	return false;
}

void __kasan_kfree_large(void *ptr, unsigned long ip)
{
	check_page_allocation(ptr, ip);

	/* The object will be poisoned by kasan_poison_pages(). */
}

static inline void unpoison_slab_object(struct kmem_cache *cache, void *object,
					gfp_t flags, bool init)
{
	/*
	 * Unpoison the whole object. For kmalloc() allocations,
	 * poison_kmalloc_redzone() will do precise poisoning.
	 */
	kasan_unpoison(object, cache->object_size, init);

	/* Save alloc info (if possible) for non-kmalloc() allocations. */
	if (kasan_stack_collection_enabled() && !is_kmalloc_cache(cache))
		kasan_save_alloc_info(cache, object, flags);
}

void * __must_check __kasan_slab_alloc(struct kmem_cache *cache,
					void *object, gfp_t flags, bool init)
{
	u8 tag;
	void *tagged_object;

	if (gfpflags_allow_blocking(flags))
		kasan_quarantine_reduce();

	if (unlikely(object == NULL))
		return NULL;

	if (is_kfence_address(object))
		return (void *)object;

	/*
	 * Generate and assign random tag for tag-based modes.
	 * Tag is ignored in set_tag() for the generic mode.
	 */
	tag = assign_tag(cache, object, false);
	tagged_object = set_tag(object, tag);

	/* Unpoison the object and save alloc info for non-kmalloc() allocations. */
	unpoison_slab_object(cache, tagged_object, flags, init);

	return tagged_object;
}

static inline void poison_kmalloc_redzone(struct kmem_cache *cache,
				const void *object, size_t size, gfp_t flags)
{
	unsigned long redzone_start;
	unsigned long redzone_end;

	/*
	 * The redzone has byte-level precision for the generic mode.
	 * Partially poison the last object granule to cover the unaligned
	 * part of the redzone.
	 */
	if (IS_ENABLED(CONFIG_KASAN_GENERIC))
		kasan_poison_last_granule((void *)object, size);

	/* Poison the aligned part of the redzone. */
	redzone_start = round_up((unsigned long)(object + size),
				KASAN_GRANULE_SIZE);
	redzone_end = round_up((unsigned long)(object + cache->object_size),
				KASAN_GRANULE_SIZE);
	kasan_poison((void *)redzone_start, redzone_end - redzone_start,
			   KASAN_SLAB_REDZONE, false);

	/*
	 * Save alloc info (if possible) for kmalloc() allocations.
	 * This also rewrites the alloc info when called from kasan_krealloc().
	 */
	if (kasan_stack_collection_enabled() && is_kmalloc_cache(cache))
		kasan_save_alloc_info(cache, (void *)object, flags);

}

void * __must_check __kasan_kmalloc(struct kmem_cache *cache, const void *object,
					size_t size, gfp_t flags)
{
	if (gfpflags_allow_blocking(flags))
		kasan_quarantine_reduce();

	if (unlikely(object == NULL))
		return NULL;

	if (is_kfence_address(object))
		return (void *)object;

	/* The object has already been unpoisoned by kasan_slab_alloc(). */
	poison_kmalloc_redzone(cache, object, size, flags);

	/* Keep the tag that was set by kasan_slab_alloc(). */
	return (void *)object;
}
EXPORT_SYMBOL(__kasan_kmalloc);

static inline void poison_kmalloc_large_redzone(const void *ptr, size_t size,
						gfp_t flags)
{
	unsigned long redzone_start;
	unsigned long redzone_end;

	/*
	 * The redzone has byte-level precision for the generic mode.
	 * Partially poison the last object granule to cover the unaligned
	 * part of the redzone.
	 */
	if (IS_ENABLED(CONFIG_KASAN_GENERIC))
		kasan_poison_last_granule(ptr, size);

	/* Poison the aligned part of the redzone. */
	redzone_start = round_up((unsigned long)(ptr + size), KASAN_GRANULE_SIZE);
	redzone_end = (unsigned long)ptr + page_size(virt_to_page(ptr));
	kasan_poison((void *)redzone_start, redzone_end - redzone_start,
		     KASAN_PAGE_REDZONE, false);
}

void * __must_check __kasan_kmalloc_large(const void *ptr, size_t size,
						gfp_t flags)
{
	if (gfpflags_allow_blocking(flags))
		kasan_quarantine_reduce();

	if (unlikely(ptr == NULL))
		return NULL;

	/* The object has already been unpoisoned by kasan_unpoison_pages(). */
	poison_kmalloc_large_redzone(ptr, size, flags);

	/* Keep the tag that was set by alloc_pages(). */
	return (void *)ptr;
}

void * __must_check __kasan_krealloc(const void *object, size_t size, gfp_t flags)
{
	struct slab *slab;

	if (gfpflags_allow_blocking(flags))
		kasan_quarantine_reduce();

	if (unlikely(object == ZERO_SIZE_PTR))
		return (void *)object;

	if (is_kfence_address(object))
		return (void *)object;

	/*
	 * Unpoison the object's data.
	 * Part of it might already have been unpoisoned, but it's unknown
	 * how big that part is.
	 */
	kasan_unpoison(object, size, false);

	slab = virt_to_slab(object);

	/* Piggy-back on kmalloc() instrumentation to poison the redzone. */
	if (unlikely(!slab))
		poison_kmalloc_large_redzone(object, size, flags);
	else
		poison_kmalloc_redzone(slab->slab_cache, object, size, flags);

	return (void *)object;
}

bool __kasan_mempool_poison_pages(struct page *page, unsigned int order,
				  unsigned long ip)
{
	unsigned long *ptr;

	if (unlikely(PageHighMem(page)))
		return true;

	/* Bail out if allocation was excluded due to sampling. */
	if (!IS_ENABLED(CONFIG_KASAN_GENERIC) &&
	    page_kasan_tag(page) == KASAN_TAG_KERNEL)
		return true;

	ptr = page_address(page);

	if (check_page_allocation(ptr, ip))
		return false;

	kasan_poison(ptr, PAGE_SIZE << order, KASAN_PAGE_FREE, false);

	return true;
}

void __kasan_mempool_unpoison_pages(struct page *page, unsigned int order,
				    unsigned long ip)
{
	__kasan_unpoison_pages(page, order, false);
}

bool __kasan_mempool_poison_object(void *ptr, unsigned long ip)
{
	struct folio *folio = virt_to_folio(ptr);
	struct slab *slab;

	/*
	 * This function can be called for large kmalloc allocation that get
	 * their memory from page_alloc. Thus, the folio might not be a slab.
	 */
	if (unlikely(!folio_test_slab(folio))) {
		if (check_page_allocation(ptr, ip))
			return false;
		kasan_poison(ptr, folio_size(folio), KASAN_PAGE_FREE, false);
		return true;
	}

	if (is_kfence_address(ptr))
		return false;

	slab = folio_slab(folio);
	return !poison_slab_object(slab->slab_cache, ptr, ip, false);
}

void __kasan_mempool_unpoison_object(void *ptr, size_t size, unsigned long ip)
{
	struct slab *slab;
	gfp_t flags = 0; /* Might be executing under a lock. */

	slab = virt_to_slab(ptr);

	/*
	 * This function can be called for large kmalloc allocation that get
	 * their memory from page_alloc.
	 */
	if (unlikely(!slab)) {
		kasan_unpoison(ptr, size, false);
		poison_kmalloc_large_redzone(ptr, size, flags);
		return;
	}

	if (is_kfence_address(ptr))
		return;

	/* Unpoison the object and save alloc info for non-kmalloc() allocations. */
	unpoison_slab_object(slab->slab_cache, ptr, size, flags);

	/* Poison the redzone and save alloc info for kmalloc() allocations. */
	if (is_kmalloc_cache(slab->slab_cache))
		poison_kmalloc_redzone(slab->slab_cache, ptr, size, flags);
}

bool __kasan_check_byte(const void *address, unsigned long ip)
{
	if (!kasan_byte_accessible(address)) {
		kasan_report(address, 1, false, ip);
		return false;
	}
	return true;
}
