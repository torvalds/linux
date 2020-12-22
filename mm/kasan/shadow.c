// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains KASAN runtime code that manages shadow memory for
 * generic and software tag-based KASAN modes.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 *
 * Some code borrowed from https://github.com/xairy/kasan-prototype by
 *        Andrey Konovalov <andreyknvl@gmail.com>
 */

#include <linux/init.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/kmemleak.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#include "kasan.h"

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
	if (!check_memory_region((unsigned long)addr, len, true, _RET_IP_))
		return NULL;

	return __memset(addr, c, len);
}

#ifdef __HAVE_ARCH_MEMMOVE
#undef memmove
void *memmove(void *dest, const void *src, size_t len)
{
	if (!check_memory_region((unsigned long)src, len, false, _RET_IP_) ||
	    !check_memory_region((unsigned long)dest, len, true, _RET_IP_))
		return NULL;

	return __memmove(dest, src, len);
}
#endif

#undef memcpy
void *memcpy(void *dest, const void *src, size_t len)
{
	if (!check_memory_region((unsigned long)src, len, false, _RET_IP_) ||
	    !check_memory_region((unsigned long)dest, len, true, _RET_IP_))
		return NULL;

	return __memcpy(dest, src, len);
}

/*
 * Poisons the shadow memory for 'size' bytes starting from 'addr'.
 * Memory addresses should be aligned to KASAN_GRANULE_SIZE.
 */
void poison_range(const void *address, size_t size, u8 value)
{
	void *shadow_start, *shadow_end;

	/*
	 * Perform shadow offset calculation based on untagged address, as
	 * some of the callers (e.g. kasan_poison_object_data) pass tagged
	 * addresses to this function.
	 */
	address = kasan_reset_tag(address);
	size = round_up(size, KASAN_GRANULE_SIZE);

	shadow_start = kasan_mem_to_shadow(address);
	shadow_end = kasan_mem_to_shadow(address + size);

	__memset(shadow_start, value, shadow_end - shadow_start);
}

void unpoison_range(const void *address, size_t size)
{
	u8 tag = get_tag(address);

	/*
	 * Perform shadow offset calculation based on untagged address, as
	 * some of the callers (e.g. kasan_unpoison_object_data) pass tagged
	 * addresses to this function.
	 */
	address = kasan_reset_tag(address);

	poison_range(address, size, tag);

	if (size & KASAN_GRANULE_MASK) {
		u8 *shadow = (u8 *)kasan_mem_to_shadow(address + size);

		if (IS_ENABLED(CONFIG_KASAN_SW_TAGS))
			*shadow = tag;
		else /* CONFIG_KASAN_GENERIC */
			*shadow = size & KASAN_GRANULE_MASK;
	}
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

	if (WARN_ON(mem_data->nr_pages % KASAN_GRANULE_SIZE) ||
		WARN_ON(start_kaddr % KASAN_MEMORY_PER_SHADOW_PAGE))
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

#ifdef CONFIG_KASAN_VMALLOC

static int kasan_populate_vmalloc_pte(pte_t *ptep, unsigned long addr,
				      void *unused)
{
	unsigned long page;
	pte_t pte;

	if (likely(!pte_none(*ptep)))
		return 0;

	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	memset((void *)page, KASAN_VMALLOC_INVALID, PAGE_SIZE);
	pte = pfn_pte(PFN_DOWN(__pa(page)), PAGE_KERNEL);

	spin_lock(&init_mm.page_table_lock);
	if (likely(pte_none(*ptep))) {
		set_pte_at(&init_mm, addr, ptep, pte);
		page = 0;
	}
	spin_unlock(&init_mm.page_table_lock);
	if (page)
		free_page(page);
	return 0;
}

int kasan_populate_vmalloc(unsigned long addr, unsigned long size)
{
	unsigned long shadow_start, shadow_end;
	int ret;

	if (!is_vmalloc_or_module_addr((void *)addr))
		return 0;

	shadow_start = (unsigned long)kasan_mem_to_shadow((void *)addr);
	shadow_start = ALIGN_DOWN(shadow_start, PAGE_SIZE);
	shadow_end = (unsigned long)kasan_mem_to_shadow((void *)addr + size);
	shadow_end = ALIGN(shadow_end, PAGE_SIZE);

	ret = apply_to_page_range(&init_mm, shadow_start,
				  shadow_end - shadow_start,
				  kasan_populate_vmalloc_pte, NULL);
	if (ret)
		return ret;

	flush_cache_vmap(shadow_start, shadow_end);

	/*
	 * We need to be careful about inter-cpu effects here. Consider:
	 *
	 *   CPU#0				  CPU#1
	 * WRITE_ONCE(p, vmalloc(100));		while (x = READ_ONCE(p)) ;
	 *					p[99] = 1;
	 *
	 * With compiler instrumentation, that ends up looking like this:
	 *
	 *   CPU#0				  CPU#1
	 * // vmalloc() allocates memory
	 * // let a = area->addr
	 * // we reach kasan_populate_vmalloc
	 * // and call unpoison_range:
	 * STORE shadow(a), unpoison_val
	 * ...
	 * STORE shadow(a+99), unpoison_val	x = LOAD p
	 * // rest of vmalloc process		<data dependency>
	 * STORE p, a				LOAD shadow(x+99)
	 *
	 * If there is no barrier between the end of unpoisioning the shadow
	 * and the store of the result to p, the stores could be committed
	 * in a different order by CPU#0, and CPU#1 could erroneously observe
	 * poison in the shadow.
	 *
	 * We need some sort of barrier between the stores.
	 *
	 * In the vmalloc() case, this is provided by a smp_wmb() in
	 * clear_vm_uninitialized_flag(). In the per-cpu allocator and in
	 * get_vm_area() and friends, the caller gets shadow allocated but
	 * doesn't have any pages mapped into the virtual address space that
	 * has been reserved. Mapping those pages in will involve taking and
	 * releasing a page-table lock, which will provide the barrier.
	 */

	return 0;
}

/*
 * Poison the shadow for a vmalloc region. Called as part of the
 * freeing process at the time the region is freed.
 */
void kasan_poison_vmalloc(const void *start, unsigned long size)
{
	if (!is_vmalloc_or_module_addr(start))
		return;

	size = round_up(size, KASAN_GRANULE_SIZE);
	poison_range(start, size, KASAN_VMALLOC_INVALID);
}

void kasan_unpoison_vmalloc(const void *start, unsigned long size)
{
	if (!is_vmalloc_or_module_addr(start))
		return;

	unpoison_range(start, size);
}

static int kasan_depopulate_vmalloc_pte(pte_t *ptep, unsigned long addr,
					void *unused)
{
	unsigned long page;

	page = (unsigned long)__va(pte_pfn(*ptep) << PAGE_SHIFT);

	spin_lock(&init_mm.page_table_lock);

	if (likely(!pte_none(*ptep))) {
		pte_clear(&init_mm, addr, ptep);
		free_page(page);
	}
	spin_unlock(&init_mm.page_table_lock);

	return 0;
}

/*
 * Release the backing for the vmalloc region [start, end), which
 * lies within the free region [free_region_start, free_region_end).
 *
 * This can be run lazily, long after the region was freed. It runs
 * under vmap_area_lock, so it's not safe to interact with the vmalloc/vmap
 * infrastructure.
 *
 * How does this work?
 * -------------------
 *
 * We have a region that is page aligned, labelled as A.
 * That might not map onto the shadow in a way that is page-aligned:
 *
 *                    start                     end
 *                    v                         v
 * |????????|????????|AAAAAAAA|AA....AA|AAAAAAAA|????????| < vmalloc
 *  -------- -------- --------          -------- --------
 *      |        |       |                 |        |
 *      |        |       |         /-------/        |
 *      \-------\|/------/         |/---------------/
 *              |||                ||
 *             |??AAAAAA|AAAAAAAA|AA??????|                < shadow
 *                 (1)      (2)      (3)
 *
 * First we align the start upwards and the end downwards, so that the
 * shadow of the region aligns with shadow page boundaries. In the
 * example, this gives us the shadow page (2). This is the shadow entirely
 * covered by this allocation.
 *
 * Then we have the tricky bits. We want to know if we can free the
 * partially covered shadow pages - (1) and (3) in the example. For this,
 * we are given the start and end of the free region that contains this
 * allocation. Extending our previous example, we could have:
 *
 *  free_region_start                                    free_region_end
 *  |                 start                     end      |
 *  v                 v                         v        v
 * |FFFFFFFF|FFFFFFFF|AAAAAAAA|AA....AA|AAAAAAAA|FFFFFFFF| < vmalloc
 *  -------- -------- --------          -------- --------
 *      |        |       |                 |        |
 *      |        |       |         /-------/        |
 *      \-------\|/------/         |/---------------/
 *              |||                ||
 *             |FFAAAAAA|AAAAAAAA|AAF?????|                < shadow
 *                 (1)      (2)      (3)
 *
 * Once again, we align the start of the free region up, and the end of
 * the free region down so that the shadow is page aligned. So we can free
 * page (1) - we know no allocation currently uses anything in that page,
 * because all of it is in the vmalloc free region. But we cannot free
 * page (3), because we can't be sure that the rest of it is unused.
 *
 * We only consider pages that contain part of the original region for
 * freeing: we don't try to free other pages from the free region or we'd
 * end up trying to free huge chunks of virtual address space.
 *
 * Concurrency
 * -----------
 *
 * How do we know that we're not freeing a page that is simultaneously
 * being used for a fresh allocation in kasan_populate_vmalloc(_pte)?
 *
 * We _can_ have kasan_release_vmalloc and kasan_populate_vmalloc running
 * at the same time. While we run under free_vmap_area_lock, the population
 * code does not.
 *
 * free_vmap_area_lock instead operates to ensure that the larger range
 * [free_region_start, free_region_end) is safe: because __alloc_vmap_area and
 * the per-cpu region-finding algorithm both run under free_vmap_area_lock,
 * no space identified as free will become used while we are running. This
 * means that so long as we are careful with alignment and only free shadow
 * pages entirely covered by the free region, we will not run in to any
 * trouble - any simultaneous allocations will be for disjoint regions.
 */
void kasan_release_vmalloc(unsigned long start, unsigned long end,
			   unsigned long free_region_start,
			   unsigned long free_region_end)
{
	void *shadow_start, *shadow_end;
	unsigned long region_start, region_end;
	unsigned long size;

	region_start = ALIGN(start, KASAN_MEMORY_PER_SHADOW_PAGE);
	region_end = ALIGN_DOWN(end, KASAN_MEMORY_PER_SHADOW_PAGE);

	free_region_start = ALIGN(free_region_start, KASAN_MEMORY_PER_SHADOW_PAGE);

	if (start != region_start &&
	    free_region_start < region_start)
		region_start -= KASAN_MEMORY_PER_SHADOW_PAGE;

	free_region_end = ALIGN_DOWN(free_region_end, KASAN_MEMORY_PER_SHADOW_PAGE);

	if (end != region_end &&
	    free_region_end > region_end)
		region_end += KASAN_MEMORY_PER_SHADOW_PAGE;

	shadow_start = kasan_mem_to_shadow((void *)region_start);
	shadow_end = kasan_mem_to_shadow((void *)region_end);

	if (shadow_end > shadow_start) {
		size = shadow_end - shadow_start;
		apply_to_existing_page_range(&init_mm,
					     (unsigned long)shadow_start,
					     size, kasan_depopulate_vmalloc_pte,
					     NULL);
		flush_tlb_kernel_range((unsigned long)shadow_start,
				       (unsigned long)shadow_end);
	}
}

#else /* CONFIG_KASAN_VMALLOC */

int kasan_module_alloc(void *addr, size_t size)
{
	void *ret;
	size_t scaled_size;
	size_t shadow_size;
	unsigned long shadow_start;

	shadow_start = (unsigned long)kasan_mem_to_shadow(addr);
	scaled_size = (size + KASAN_GRANULE_SIZE - 1) >>
				KASAN_SHADOW_SCALE_SHIFT;
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

#endif
