// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains common generic and tag-based KASAN code.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 *
 * Some code borrowed from https://github.com/xairy/kasan-prototype by
 *        Andrey Konovalov <andreyknvl@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/kmemleak.h>
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
#include <linux/vmalloc.h>
#include <linux/bug.h>
#include <linux/uaccess.h>

#include "kasan.h"
#include "../slab.h"

static inline int in_irqentry_text(unsigned long ptr)
{
	return (ptr >= (unsigned long)&__irqentry_text_start &&
		ptr < (unsigned long)&__irqentry_text_end) ||
		(ptr >= (unsigned long)&__softirqentry_text_start &&
		 ptr < (unsigned long)&__softirqentry_text_end);
}

static inline unsigned int filter_irq_stacks(unsigned long *entries,
					     unsigned int nr_entries)
{
	unsigned int i;

	for (i = 0; i < nr_entries; i++) {
		if (in_irqentry_text(entries[i])) {
			/* Include the irqentry function into the stack. */
			return i + 1;
		}
	}
	return nr_entries;
}

static inline depot_stack_handle_t save_stack(gfp_t flags)
{
	unsigned long entries[KASAN_STACK_DEPTH];
	unsigned int nr_entries;

	nr_entries = stack_trace_save(entries, ARRAY_SIZE(entries), 0);
	nr_entries = filter_irq_stacks(entries, nr_entries);
	return stack_depot_save(entries, nr_entries, flags);
}

static inline void set_track(struct kasan_track *track, gfp_t flags)
{
	track->pid = current->pid;
	track->stack = save_stack(flags);
}

void kasan_enable_current(void)
{
	current->kasan_depth++;
}

void kasan_disable_current(void)
{
	current->kasan_depth--;
}

bool __kasan_check_read(const volatile void *p, unsigned int size)
{
	return check_memory_region((unsigned long)p, size, false, _RET_IP_);
}
EXPORT_SYMBOL(__kasan_check_read);

bool __kasan_check_write(const volatile void *p, unsigned int size)
{
	return check_memory_region((unsigned long)p, size, true, _RET_IP_);
}
EXPORT_SYMBOL(__kasan_check_write);

#undef memset
void *memset(void *addr, int c, size_t len)
{
	check_memory_region((unsigned long)addr, len, true, _RET_IP_);

	return __memset(addr, c, len);
}

#undef memmove
void *memmove(void *dest, const void *src, size_t len)
{
	check_memory_region((unsigned long)src, len, false, _RET_IP_);
	check_memory_region((unsigned long)dest, len, true, _RET_IP_);

	return __memmove(dest, src, len);
}

#undef memcpy
void *memcpy(void *dest, const void *src, size_t len)
{
	check_memory_region((unsigned long)src, len, false, _RET_IP_);
	check_memory_region((unsigned long)dest, len, true, _RET_IP_);

	return __memcpy(dest, src, len);
}

/*
 * Poisons the shadow memory for 'size' bytes starting from 'addr'.
 * Memory addresses should be aligned to KASAN_SHADOW_SCALE_SIZE.
 */
void kasan_poison_shadow(const void *address, size_t size, u8 value)
{
	void *shadow_start, *shadow_end;

	/*
	 * Perform shadow offset calculation based on untagged address, as
	 * some of the callers (e.g. kasan_poison_object_data) pass tagged
	 * addresses to this function.
	 */
	address = reset_tag(address);

	shadow_start = kasan_mem_to_shadow(address);
	shadow_end = kasan_mem_to_shadow(address + size);

	__memset(shadow_start, value, shadow_end - shadow_start);
}

void kasan_unpoison_shadow(const void *address, size_t size)
{
	u8 tag = get_tag(address);

	/*
	 * Perform shadow offset calculation based on untagged address, as
	 * some of the callers (e.g. kasan_unpoison_object_data) pass tagged
	 * addresses to this function.
	 */
	address = reset_tag(address);

	kasan_poison_shadow(address, size, tag);

	if (size & KASAN_SHADOW_MASK) {
		u8 *shadow = (u8 *)kasan_mem_to_shadow(address + size);

		if (IS_ENABLED(CONFIG_KASAN_SW_TAGS))
			*shadow = tag;
		else
			*shadow = size & KASAN_SHADOW_MASK;
	}
}

static void __kasan_unpoison_stack(struct task_struct *task, const void *sp)
{
	void *base = task_stack_page(task);
	size_t size = sp - base;

	kasan_unpoison_shadow(base, size);
}

/* Unpoison the entire stack for a task. */
void kasan_unpoison_task_stack(struct task_struct *task)
{
	__kasan_unpoison_stack(task, task_stack_page(task) + THREAD_SIZE);
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

	kasan_unpoison_shadow(base, watermark - base);
}

/*
 * Clear all poison for the region between the current SP and a provided
 * watermark value, as is sometimes required prior to hand-crafted asm function
 * returns in the middle of functions.
 */
void kasan_unpoison_stack_above_sp_to(const void *watermark)
{
	const void *sp = __builtin_frame_address(0);
	size_t size = watermark - sp;

	if (WARN_ON(sp > watermark))
		return;
	kasan_unpoison_shadow(sp, size);
}

void kasan_alloc_pages(struct page *page, unsigned int order)
{
	u8 tag;
	unsigned long i;

	if (unlikely(PageHighMem(page)))
		return;

	tag = random_tag();
	for (i = 0; i < (1 << order); i++)
		page_kasan_tag_set(page + i, tag);
	kasan_unpoison_shadow(page_address(page), PAGE_SIZE << order);
}

void kasan_free_pages(struct page *page, unsigned int order)
{
	if (likely(!PageHighMem(page)))
		kasan_poison_shadow(page_address(page),
				PAGE_SIZE << order,
				KASAN_FREE_PAGE);
}

/*
 * Adaptive redzone policy taken from the userspace AddressSanitizer runtime.
 * For larger allocations larger redzones are used.
 */
static inline unsigned int optimal_redzone(unsigned int object_size)
{
	if (IS_ENABLED(CONFIG_KASAN_SW_TAGS))
		return 0;

	return
		object_size <= 64        - 16   ? 16 :
		object_size <= 128       - 32   ? 32 :
		object_size <= 512       - 64   ? 64 :
		object_size <= 4096      - 128  ? 128 :
		object_size <= (1 << 14) - 256  ? 256 :
		object_size <= (1 << 15) - 512  ? 512 :
		object_size <= (1 << 16) - 1024 ? 1024 : 2048;
}

void kasan_cache_create(struct kmem_cache *cache, unsigned int *size,
			slab_flags_t *flags)
{
	unsigned int orig_size = *size;
	unsigned int redzone_size;
	int redzone_adjust;

	/* Add alloc meta. */
	cache->kasan_info.alloc_meta_offset = *size;
	*size += sizeof(struct kasan_alloc_meta);

	/* Add free meta. */
	if (IS_ENABLED(CONFIG_KASAN_GENERIC) &&
	    (cache->flags & SLAB_TYPESAFE_BY_RCU || cache->ctor ||
	     cache->object_size < sizeof(struct kasan_free_meta))) {
		cache->kasan_info.free_meta_offset = *size;
		*size += sizeof(struct kasan_free_meta);
	}

	redzone_size = optimal_redzone(cache->object_size);
	redzone_adjust = redzone_size -	(*size - cache->object_size);
	if (redzone_adjust > 0)
		*size += redzone_adjust;

	*size = min_t(unsigned int, KMALLOC_MAX_SIZE,
			max(*size, cache->object_size + redzone_size));

	/*
	 * If the metadata doesn't fit, don't enable KASAN at all.
	 */
	if (*size <= cache->kasan_info.alloc_meta_offset ||
			*size <= cache->kasan_info.free_meta_offset) {
		cache->kasan_info.alloc_meta_offset = 0;
		cache->kasan_info.free_meta_offset = 0;
		*size = orig_size;
		return;
	}

	*flags |= SLAB_KASAN;
}

size_t kasan_metadata_size(struct kmem_cache *cache)
{
	return (cache->kasan_info.alloc_meta_offset ?
		sizeof(struct kasan_alloc_meta) : 0) +
		(cache->kasan_info.free_meta_offset ?
		sizeof(struct kasan_free_meta) : 0);
}

struct kasan_alloc_meta *get_alloc_info(struct kmem_cache *cache,
					const void *object)
{
	BUILD_BUG_ON(sizeof(struct kasan_alloc_meta) > 32);
	return (void *)object + cache->kasan_info.alloc_meta_offset;
}

struct kasan_free_meta *get_free_info(struct kmem_cache *cache,
				      const void *object)
{
	BUILD_BUG_ON(sizeof(struct kasan_free_meta) > 32);
	return (void *)object + cache->kasan_info.free_meta_offset;
}

void kasan_poison_slab(struct page *page)
{
	unsigned long i;

	for (i = 0; i < (1 << compound_order(page)); i++)
		page_kasan_tag_reset(page + i);
	kasan_poison_shadow(page_address(page),
			PAGE_SIZE << compound_order(page),
			KASAN_KMALLOC_REDZONE);
}

void kasan_unpoison_object_data(struct kmem_cache *cache, void *object)
{
	kasan_unpoison_shadow(object, cache->object_size);
}

void kasan_poison_object_data(struct kmem_cache *cache, void *object)
{
	kasan_poison_shadow(object,
			round_up(cache->object_size, KASAN_SHADOW_SCALE_SIZE),
			KASAN_KMALLOC_REDZONE);
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
static u8 assign_tag(struct kmem_cache *cache, const void *object,
			bool init, bool keep_tag)
{
	/*
	 * 1. When an object is kmalloc()'ed, two hooks are called:
	 *    kasan_slab_alloc() and kasan_kmalloc(). We assign the
	 *    tag only in the first one.
	 * 2. We reuse the same tag for krealloc'ed objects.
	 */
	if (keep_tag)
		return get_tag(object);

	/*
	 * If the cache neither has a constructor nor has SLAB_TYPESAFE_BY_RCU
	 * set, assign a tag when the object is being allocated (init == false).
	 */
	if (!cache->ctor && !(cache->flags & SLAB_TYPESAFE_BY_RCU))
		return init ? KASAN_TAG_KERNEL : random_tag();

	/* For caches that either have a constructor or SLAB_TYPESAFE_BY_RCU: */
#ifdef CONFIG_SLAB
	/* For SLAB assign tags based on the object index in the freelist. */
	return (u8)obj_to_index(cache, virt_to_page(object), (void *)object);
#else
	/*
	 * For SLUB assign a random tag during slab creation, otherwise reuse
	 * the already assigned tag.
	 */
	return init ? random_tag() : get_tag(object);
#endif
}

void * __must_check kasan_init_slab_obj(struct kmem_cache *cache,
						const void *object)
{
	struct kasan_alloc_meta *alloc_info;

	if (!(cache->flags & SLAB_KASAN))
		return (void *)object;

	alloc_info = get_alloc_info(cache, object);
	__memset(alloc_info, 0, sizeof(*alloc_info));

	if (IS_ENABLED(CONFIG_KASAN_SW_TAGS))
		object = set_tag(object,
				assign_tag(cache, object, true, false));

	return (void *)object;
}

static inline bool shadow_invalid(u8 tag, s8 shadow_byte)
{
	if (IS_ENABLED(CONFIG_KASAN_GENERIC))
		return shadow_byte < 0 ||
			shadow_byte >= KASAN_SHADOW_SCALE_SIZE;

	/* else CONFIG_KASAN_SW_TAGS: */
	if ((u8)shadow_byte == KASAN_TAG_INVALID)
		return true;
	if ((tag != KASAN_TAG_KERNEL) && (tag != (u8)shadow_byte))
		return true;

	return false;
}

static bool __kasan_slab_free(struct kmem_cache *cache, void *object,
			      unsigned long ip, bool quarantine)
{
	s8 shadow_byte;
	u8 tag;
	void *tagged_object;
	unsigned long rounded_up_size;

	tag = get_tag(object);
	tagged_object = object;
	object = reset_tag(object);

	if (unlikely(nearest_obj(cache, virt_to_head_page(object), object) !=
	    object)) {
		kasan_report_invalid_free(tagged_object, ip);
		return true;
	}

	/* RCU slabs could be legally used after free within the RCU period */
	if (unlikely(cache->flags & SLAB_TYPESAFE_BY_RCU))
		return false;

	shadow_byte = READ_ONCE(*(s8 *)kasan_mem_to_shadow(object));
	if (shadow_invalid(tag, shadow_byte)) {
		kasan_report_invalid_free(tagged_object, ip);
		return true;
	}

	rounded_up_size = round_up(cache->object_size, KASAN_SHADOW_SCALE_SIZE);
	kasan_poison_shadow(object, rounded_up_size, KASAN_KMALLOC_FREE);

	if ((IS_ENABLED(CONFIG_KASAN_GENERIC) && !quarantine) ||
			unlikely(!(cache->flags & SLAB_KASAN)))
		return false;

	set_track(&get_alloc_info(cache, object)->free_track, GFP_NOWAIT);
	quarantine_put(get_free_info(cache, object), cache);

	return IS_ENABLED(CONFIG_KASAN_GENERIC);
}

bool kasan_slab_free(struct kmem_cache *cache, void *object, unsigned long ip)
{
	return __kasan_slab_free(cache, object, ip, true);
}

static void *__kasan_kmalloc(struct kmem_cache *cache, const void *object,
				size_t size, gfp_t flags, bool keep_tag)
{
	unsigned long redzone_start;
	unsigned long redzone_end;
	u8 tag = 0xff;

	if (gfpflags_allow_blocking(flags))
		quarantine_reduce();

	if (unlikely(object == NULL))
		return NULL;

	redzone_start = round_up((unsigned long)(object + size),
				KASAN_SHADOW_SCALE_SIZE);
	redzone_end = round_up((unsigned long)object + cache->object_size,
				KASAN_SHADOW_SCALE_SIZE);

	if (IS_ENABLED(CONFIG_KASAN_SW_TAGS))
		tag = assign_tag(cache, object, false, keep_tag);

	/* Tag is ignored in set_tag without CONFIG_KASAN_SW_TAGS */
	kasan_unpoison_shadow(set_tag(object, tag), size);
	kasan_poison_shadow((void *)redzone_start, redzone_end - redzone_start,
		KASAN_KMALLOC_REDZONE);

	if (cache->flags & SLAB_KASAN)
		set_track(&get_alloc_info(cache, object)->alloc_track, flags);

	return set_tag(object, tag);
}

void * __must_check kasan_slab_alloc(struct kmem_cache *cache, void *object,
					gfp_t flags)
{
	return __kasan_kmalloc(cache, object, cache->object_size, flags, false);
}

void * __must_check kasan_kmalloc(struct kmem_cache *cache, const void *object,
				size_t size, gfp_t flags)
{
	return __kasan_kmalloc(cache, object, size, flags, true);
}
EXPORT_SYMBOL(kasan_kmalloc);

void * __must_check kasan_kmalloc_large(const void *ptr, size_t size,
						gfp_t flags)
{
	struct page *page;
	unsigned long redzone_start;
	unsigned long redzone_end;

	if (gfpflags_allow_blocking(flags))
		quarantine_reduce();

	if (unlikely(ptr == NULL))
		return NULL;

	page = virt_to_page(ptr);
	redzone_start = round_up((unsigned long)(ptr + size),
				KASAN_SHADOW_SCALE_SIZE);
	redzone_end = (unsigned long)ptr + (PAGE_SIZE << compound_order(page));

	kasan_unpoison_shadow(ptr, size);
	kasan_poison_shadow((void *)redzone_start, redzone_end - redzone_start,
		KASAN_PAGE_REDZONE);

	return (void *)ptr;
}

void * __must_check kasan_krealloc(const void *object, size_t size, gfp_t flags)
{
	struct page *page;

	if (unlikely(object == ZERO_SIZE_PTR))
		return (void *)object;

	page = virt_to_head_page(object);

	if (unlikely(!PageSlab(page)))
		return kasan_kmalloc_large(object, size, flags);
	else
		return __kasan_kmalloc(page->slab_cache, object, size,
						flags, true);
}

void kasan_poison_kfree(void *ptr, unsigned long ip)
{
	struct page *page;

	page = virt_to_head_page(ptr);

	if (unlikely(!PageSlab(page))) {
		if (ptr != page_address(page)) {
			kasan_report_invalid_free(ptr, ip);
			return;
		}
		kasan_poison_shadow(ptr, PAGE_SIZE << compound_order(page),
				KASAN_FREE_PAGE);
	} else {
		__kasan_slab_free(page->slab_cache, ptr, ip, false);
	}
}

void kasan_kfree_large(void *ptr, unsigned long ip)
{
	if (ptr != page_address(virt_to_head_page(ptr)))
		kasan_report_invalid_free(ptr, ip);
	/* The object will be poisoned by page_alloc. */
}

int kasan_module_alloc(void *addr, size_t size)
{
	void *ret;
	size_t scaled_size;
	size_t shadow_size;
	unsigned long shadow_start;

	shadow_start = (unsigned long)kasan_mem_to_shadow(addr);
	scaled_size = (size + KASAN_SHADOW_MASK) >> KASAN_SHADOW_SCALE_SHIFT;
	shadow_size = round_up(scaled_size, PAGE_SIZE);

	if (WARN_ON(!PAGE_ALIGNED(shadow_start)))
		return -EINVAL;

	ret = __vmalloc_node_range(shadow_size, 1, shadow_start,
			shadow_start + shadow_size,
			GFP_KERNEL,
			PAGE_KERNEL, VM_NO_GUARD, NUMA_NO_NODE,
			__builtin_return_address(0));

	if (ret) {
		__memset(ret, KASAN_SHADOW_INIT, shadow_size);
		find_vm_area(addr)->flags |= VM_KASAN;
		kmemleak_ignore(ret);
		return 0;
	}

	return -ENOMEM;
}

void kasan_free_shadow(const struct vm_struct *vm)
{
	if (vm->flags & VM_KASAN)
		vfree(kasan_mem_to_shadow(vm->addr));
}

extern void __kasan_report(unsigned long addr, size_t size, bool is_write, unsigned long ip);

void kasan_report(unsigned long addr, size_t size, bool is_write, unsigned long ip)
{
	unsigned long flags = user_access_save();
	__kasan_report(addr, size, is_write, ip);
	user_access_restore(flags);
}

#ifdef CONFIG_MEMORY_HOTPLUG
static bool shadow_mapped(unsigned long addr)
{
	pgd_t *pgd = pgd_offset_k(addr);
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	if (pgd_none(*pgd))
		return false;
	p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d))
		return false;
	pud = pud_offset(p4d, addr);
	if (pud_none(*pud))
		return false;

	/*
	 * We can't use pud_large() or pud_huge(), the first one is
	 * arch-specific, the last one depends on HUGETLB_PAGE.  So let's abuse
	 * pud_bad(), if pud is bad then it's bad because it's huge.
	 */
	if (pud_bad(*pud))
		return true;
	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return false;

	if (pmd_bad(*pmd))
		return true;
	pte = pte_offset_kernel(pmd, addr);
	return !pte_none(*pte);
}

static int __meminit kasan_mem_notifier(struct notifier_block *nb,
			unsigned long action, void *data)
{
	struct memory_notify *mem_data = data;
	unsigned long nr_shadow_pages, start_kaddr, shadow_start;
	unsigned long shadow_end, shadow_size;

	nr_shadow_pages = mem_data->nr_pages >> KASAN_SHADOW_SCALE_SHIFT;
	start_kaddr = (unsigned long)pfn_to_kaddr(mem_data->start_pfn);
	shadow_start = (unsigned long)kasan_mem_to_shadow((void *)start_kaddr);
	shadow_size = nr_shadow_pages << PAGE_SHIFT;
	shadow_end = shadow_start + shadow_size;

	if (WARN_ON(mem_data->nr_pages % KASAN_SHADOW_SCALE_SIZE) ||
		WARN_ON(start_kaddr % (KASAN_SHADOW_SCALE_SIZE << PAGE_SHIFT)))
		return NOTIFY_BAD;

	switch (action) {
	case MEM_GOING_ONLINE: {
		void *ret;

		/*
		 * If shadow is mapped already than it must have been mapped
		 * during the boot. This could happen if we onlining previously
		 * offlined memory.
		 */
		if (shadow_mapped(shadow_start))
			return NOTIFY_OK;

		ret = __vmalloc_node_range(shadow_size, PAGE_SIZE, shadow_start,
					shadow_end, GFP_KERNEL,
					PAGE_KERNEL, VM_NO_GUARD,
					pfn_to_nid(mem_data->start_pfn),
					__builtin_return_address(0));
		if (!ret)
			return NOTIFY_BAD;

		kmemleak_ignore(ret);
		return NOTIFY_OK;
	}
	case MEM_CANCEL_ONLINE:
	case MEM_OFFLINE: {
		struct vm_struct *vm;

		/*
		 * shadow_start was either mapped during boot by kasan_init()
		 * or during memory online by __vmalloc_node_range().
		 * In the latter case we can use vfree() to free shadow.
		 * Non-NULL result of the find_vm_area() will tell us if
		 * that was the second case.
		 *
		 * Currently it's not possible to free shadow mapped
		 * during boot by kasan_init(). It's because the code
		 * to do that hasn't been written yet. So we'll just
		 * leak the memory.
		 */
		vm = find_vm_area((void *)shadow_start);
		if (vm)
			vfree((void *)shadow_start);
	}
	}

	return NOTIFY_OK;
}

static int __init kasan_memhotplug_init(void)
{
	hotplug_memory_notifier(kasan_mem_notifier, 0);

	return 0;
}

core_initcall(kasan_memhotplug_init);
#endif
