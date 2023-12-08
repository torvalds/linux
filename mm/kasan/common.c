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
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
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

depot_stack_handle_t kasan_save_stack(gfp_t flags, bool can_alloc)
{
	unsigned long entries[KASAN_STACK_DEPTH];
	unsigned int nr_entries;

	nr_entries = stack_trace_save(entries, ARRAY_SIZE(entries), 0);
	return __stack_depot_save(entries, nr_entries, 0, flags, can_alloc);
}

void kasan_set_track(struct kasan_track *track, gfp_t flags)
{
	track->pid = current->pid;
	track->stack = kasan_save_stack(flags, true);
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

void __kasan_cache_create_kmalloc(struct kmem_cache *cache)
{
	cache->kasan_info.is_kmalloc = true;
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

void __kasan_unpoison_object_data(struct kmem_cache *cache, void *object)
{
	kasan_unpoison(object, cache->object_size, false);
}

void __kasan_poison_object_data(struct kmem_cache *cache, void *object)
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
 * 3. For SLAB allocator we can't preassign tags randomly since the freelist
 *    is stored as an array of indexes instead of a linked list. Assign tags
 *    based on objects indexes, so that objects that are next to each other
 *    get different tags.
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

	/* For caches that either have a constructor or SLAB_TYPESAFE_BY_RCU: */
#ifdef CONFIG_SLAB
	/* For SLAB assign tags based on the object index in the freelist. */
	return (u8)obj_to_index(cache, virt_to_slab(object), (void *)object);
#else
	/*
	 * For SLUB assign a random tag during slab creation, otherwise reuse
	 * the already assigned tag.
	 */
	return init ? kasan_random_tag() : get_tag(object);
#endif
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

static inline bool ____kasan_slab_free(struct kmem_cache *cache, void *object,
				unsigned long ip, bool quarantine, bool init)
{
	void *tagged_object;

	if (!kasan_arch_is_ready())
		return false;

	tagged_object = object;
	object = kasan_reset_tag(object);

	if (is_kfence_address(object))
		return false;

	if (unlikely(nearest_obj(cache, virt_to_slab(object), object) !=
	    object)) {
		kasan_report_invalid_free(tagged_object, ip, KASAN_REPORT_INVALID_FREE);
		return true;
	}

	/* RCU slabs could be legally used after free within the RCU period */
	if (unlikely(cache->flags & SLAB_TYPESAFE_BY_RCU))
		return false;

	if (!kasan_byte_accessible(tagged_object)) {
		kasan_report_invalid_free(tagged_object, ip, KASAN_REPORT_DOUBLE_FREE);
		return true;
	}

	kasan_poison(object, round_up(cache->object_size, KASAN_GRANULE_SIZE),
			KASAN_SLAB_FREE, init);

	if ((IS_ENABLED(CONFIG_KASAN_GENERIC) && !quarantine))
		return false;

	if (kasan_stack_collection_enabled())
		kasan_save_free_info(cache, tagged_object);

	return kasan_quarantine_put(cache, object);
}

bool __kasan_slab_free(struct kmem_cache *cache, void *object,
				unsigned long ip, bool init)
{
	return ____kasan_slab_free(cache, object, ip, true, init);
}

static inline bool ____kasan_kfree_large(void *ptr, unsigned long ip)
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

	/*
	 * The object will be poisoned by kasan_poison_pages() or
	 * kasan_slab_free_mempool().
	 */

	return false;
}

void __kasan_kfree_large(void *ptr, unsigned long ip)
{
	____kasan_kfree_large(ptr, ip);
}

void __kasan_slab_free_mempool(void *ptr, unsigned long ip)
{
	struct folio *folio;

	folio = virt_to_folio(ptr);

	/*
	 * Even though this function is only called for kmem_cache_alloc and
	 * kmalloc backed mempool allocations, those allocations can still be
	 * !PageSlab() when the size provided to kmalloc is larger than
	 * KMALLOC_MAX_SIZE, and kmalloc falls back onto page_alloc.
	 */
	if (unlikely(!folio_test_slab(folio))) {
		if (____kasan_kfree_large(ptr, ip))
			return;
		kasan_poison(ptr, folio_size(folio), KASAN_PAGE_FREE, false);
	} else {
		struct slab *slab = folio_slab(folio);

		____kasan_slab_free(slab->slab_cache, ptr, ip, false, false);
	}
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

	/*
	 * Unpoison the whole object.
	 * For kmalloc() allocations, kasan_kmalloc() will do precise poisoning.
	 */
	kasan_unpoison(tagged_object, cache->object_size, init);

	/* Save alloc info (if possible) for non-kmalloc() allocations. */
	if (kasan_stack_collection_enabled() && !cache->kasan_info.is_kmalloc)
		kasan_save_alloc_info(cache, tagged_object, flags);

	return tagged_object;
}

static inline void *____kasan_kmalloc(struct kmem_cache *cache,
				const void *object, size_t size, gfp_t flags)
{
	unsigned long redzone_start;
	unsigned long redzone_end;

	if (gfpflags_allow_blocking(flags))
		kasan_quarantine_reduce();

	if (unlikely(object == NULL))
		return NULL;

	if (is_kfence_address(kasan_reset_tag(object)))
		return (void *)object;

	/*
	 * The object has already been unpoisoned by kasan_slab_alloc() for
	 * kmalloc() or by kasan_krealloc() for krealloc().
	 */

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
	if (kasan_stack_collection_enabled() && cache->kasan_info.is_kmalloc)
		kasan_save_alloc_info(cache, (void *)object, flags);

	/* Keep the tag that was set by kasan_slab_alloc(). */
	return (void *)object;
}

void * __must_check __kasan_kmalloc(struct kmem_cache *cache, const void *object,
					size_t size, gfp_t flags)
{
	return ____kasan_kmalloc(cache, object, size, flags);
}
EXPORT_SYMBOL(__kasan_kmalloc);

void * __must_check __kasan_kmalloc_large(const void *ptr, size_t size,
						gfp_t flags)
{
	unsigned long redzone_start;
	unsigned long redzone_end;

	if (gfpflags_allow_blocking(flags))
		kasan_quarantine_reduce();

	if (unlikely(ptr == NULL))
		return NULL;

	/*
	 * The object has already been unpoisoned by kasan_unpoison_pages() for
	 * alloc_pages() or by kasan_krealloc() for krealloc().
	 */

	/*
	 * The redzone has byte-level precision for the generic mode.
	 * Partially poison the last object granule to cover the unaligned
	 * part of the redzone.
	 */
	if (IS_ENABLED(CONFIG_KASAN_GENERIC))
		kasan_poison_last_granule(ptr, size);

	/* Poison the aligned part of the redzone. */
	redzone_start = round_up((unsigned long)(ptr + size),
				KASAN_GRANULE_SIZE);
	redzone_end = (unsigned long)ptr + page_size(virt_to_page(ptr));
	kasan_poison((void *)redzone_start, redzone_end - redzone_start,
		     KASAN_PAGE_REDZONE, false);

	return (void *)ptr;
}

void * __must_check __kasan_krealloc(const void *object, size_t size, gfp_t flags)
{
	struct slab *slab;

	if (unlikely(object == ZERO_SIZE_PTR))
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
		return __kasan_kmalloc_large(object, size, flags);
	else
		return ____kasan_kmalloc(slab->slab_cache, object, size, flags);
}

bool __kasan_check_byte(const void *address, unsigned long ip)
{
	if (!kasan_byte_accessible(address)) {
		kasan_report((unsigned long)address, 1, false, ip);
		return false;
	}
	return true;
}
