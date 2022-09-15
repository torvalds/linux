// SPDX-License-Identifier: GPL-2.0
/*
 * KMSAN hooks for kernel subsystems.
 *
 * These functions handle creation of KMSAN metadata for memory allocations.
 *
 * Copyright (C) 2018-2022 Google LLC
 * Author: Alexander Potapenko <glider@google.com>
 *
 */

#include <linux/cacheflush.h>
#include <linux/gfp.h>
#include <linux/kmsan.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "../internal.h"
#include "../slab.h"
#include "kmsan.h"

/*
 * Instrumented functions shouldn't be called under
 * kmsan_enter_runtime()/kmsan_leave_runtime(), because this will lead to
 * skipping effects of functions like memset() inside instrumented code.
 */

static unsigned long vmalloc_shadow(unsigned long addr)
{
	return (unsigned long)kmsan_get_metadata((void *)addr,
						 KMSAN_META_SHADOW);
}

static unsigned long vmalloc_origin(unsigned long addr)
{
	return (unsigned long)kmsan_get_metadata((void *)addr,
						 KMSAN_META_ORIGIN);
}

void kmsan_vunmap_range_noflush(unsigned long start, unsigned long end)
{
	__vunmap_range_noflush(vmalloc_shadow(start), vmalloc_shadow(end));
	__vunmap_range_noflush(vmalloc_origin(start), vmalloc_origin(end));
	flush_cache_vmap(vmalloc_shadow(start), vmalloc_shadow(end));
	flush_cache_vmap(vmalloc_origin(start), vmalloc_origin(end));
}

/*
 * This function creates new shadow/origin pages for the physical pages mapped
 * into the virtual memory. If those physical pages already had shadow/origin,
 * those are ignored.
 */
void kmsan_ioremap_page_range(unsigned long start, unsigned long end,
			      phys_addr_t phys_addr, pgprot_t prot,
			      unsigned int page_shift)
{
	gfp_t gfp_mask = GFP_KERNEL | __GFP_ZERO;
	struct page *shadow, *origin;
	unsigned long off = 0;
	int nr;

	if (!kmsan_enabled || kmsan_in_runtime())
		return;

	nr = (end - start) / PAGE_SIZE;
	kmsan_enter_runtime();
	for (int i = 0; i < nr; i++, off += PAGE_SIZE) {
		shadow = alloc_pages(gfp_mask, 1);
		origin = alloc_pages(gfp_mask, 1);
		__vmap_pages_range_noflush(
			vmalloc_shadow(start + off),
			vmalloc_shadow(start + off + PAGE_SIZE), prot, &shadow,
			PAGE_SHIFT);
		__vmap_pages_range_noflush(
			vmalloc_origin(start + off),
			vmalloc_origin(start + off + PAGE_SIZE), prot, &origin,
			PAGE_SHIFT);
	}
	flush_cache_vmap(vmalloc_shadow(start), vmalloc_shadow(end));
	flush_cache_vmap(vmalloc_origin(start), vmalloc_origin(end));
	kmsan_leave_runtime();
}

void kmsan_iounmap_page_range(unsigned long start, unsigned long end)
{
	unsigned long v_shadow, v_origin;
	struct page *shadow, *origin;
	int nr;

	if (!kmsan_enabled || kmsan_in_runtime())
		return;

	nr = (end - start) / PAGE_SIZE;
	kmsan_enter_runtime();
	v_shadow = (unsigned long)vmalloc_shadow(start);
	v_origin = (unsigned long)vmalloc_origin(start);
	for (int i = 0; i < nr;
	     i++, v_shadow += PAGE_SIZE, v_origin += PAGE_SIZE) {
		shadow = kmsan_vmalloc_to_page_or_null((void *)v_shadow);
		origin = kmsan_vmalloc_to_page_or_null((void *)v_origin);
		__vunmap_range_noflush(v_shadow, vmalloc_shadow(end));
		__vunmap_range_noflush(v_origin, vmalloc_origin(end));
		if (shadow)
			__free_pages(shadow, 1);
		if (origin)
			__free_pages(origin, 1);
	}
	flush_cache_vmap(vmalloc_shadow(start), vmalloc_shadow(end));
	flush_cache_vmap(vmalloc_origin(start), vmalloc_origin(end));
	kmsan_leave_runtime();
}

/* Functions from kmsan-checks.h follow. */
void kmsan_poison_memory(const void *address, size_t size, gfp_t flags)
{
	if (!kmsan_enabled || kmsan_in_runtime())
		return;
	kmsan_enter_runtime();
	/* The users may want to poison/unpoison random memory. */
	kmsan_internal_poison_memory((void *)address, size, flags,
				     KMSAN_POISON_NOCHECK);
	kmsan_leave_runtime();
}
EXPORT_SYMBOL(kmsan_poison_memory);

void kmsan_unpoison_memory(const void *address, size_t size)
{
	unsigned long ua_flags;

	if (!kmsan_enabled || kmsan_in_runtime())
		return;

	ua_flags = user_access_save();
	kmsan_enter_runtime();
	/* The users may want to poison/unpoison random memory. */
	kmsan_internal_unpoison_memory((void *)address, size,
				       KMSAN_POISON_NOCHECK);
	kmsan_leave_runtime();
	user_access_restore(ua_flags);
}
EXPORT_SYMBOL(kmsan_unpoison_memory);

void kmsan_check_memory(const void *addr, size_t size)
{
	if (!kmsan_enabled)
		return;
	return kmsan_internal_check_memory((void *)addr, size, /*user_addr*/ 0,
					   REASON_ANY);
}
EXPORT_SYMBOL(kmsan_check_memory);
