// SPDX-License-Identifier: GPL-2.0
/*
 * KMSAN shadow implementation.
 *
 * Copyright (C) 2017-2022 Google LLC
 * Author: Alexander Potapenko <glider@google.com>
 *
 */

#include <asm/kmsan.h>
#include <asm/tlbflush.h>
#include <linux/cacheflush.h>
#include <linux/memblock.h>
#include <linux/mm_types.h>
#include <linux/percpu-defs.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/stddef.h>

#include "../internal.h"
#include "kmsan.h"

#define shadow_page_for(page) ((page)->kmsan_shadow)

#define origin_page_for(page) ((page)->kmsan_origin)

static void *shadow_ptr_for(struct page *page)
{
	return page_address(shadow_page_for(page));
}

static void *origin_ptr_for(struct page *page)
{
	return page_address(origin_page_for(page));
}

static bool page_has_metadata(struct page *page)
{
	return shadow_page_for(page) && origin_page_for(page);
}

static void set_no_shadow_origin_page(struct page *page)
{
	shadow_page_for(page) = NULL;
	origin_page_for(page) = NULL;
}

/*
 * Dummy load and store pages to be used when the real metadata is unavailable.
 * There are separate pages for loads and stores, so that every load returns a
 * zero, and every store doesn't affect other loads.
 */
static char dummy_load_page[PAGE_SIZE] __aligned(PAGE_SIZE);
static char dummy_store_page[PAGE_SIZE] __aligned(PAGE_SIZE);

static unsigned long vmalloc_meta(void *addr, bool is_origin)
{
	unsigned long addr64 = (unsigned long)addr, off;

	KMSAN_WARN_ON(is_origin && !IS_ALIGNED(addr64, KMSAN_ORIGIN_SIZE));
	if (kmsan_internal_is_vmalloc_addr(addr)) {
		off = addr64 - VMALLOC_START;
		return off + (is_origin ? KMSAN_VMALLOC_ORIGIN_START :
					  KMSAN_VMALLOC_SHADOW_START);
	}
	if (kmsan_internal_is_module_addr(addr)) {
		off = addr64 - MODULES_VADDR;
		return off + (is_origin ? KMSAN_MODULES_ORIGIN_START :
					  KMSAN_MODULES_SHADOW_START);
	}
	return 0;
}

static struct page *virt_to_page_or_null(void *vaddr)
{
	if (kmsan_virt_addr_valid(vaddr))
		return virt_to_page(vaddr);
	else
		return NULL;
}

struct shadow_origin_ptr kmsan_get_shadow_origin_ptr(void *address, u64 size,
						     bool store)
{
	struct shadow_origin_ptr ret;
	void *shadow;

	/*
	 * Even if we redirect this memory access to the dummy page, it will
	 * go out of bounds.
	 */
	KMSAN_WARN_ON(size > PAGE_SIZE);

	if (!kmsan_enabled)
		goto return_dummy;

	KMSAN_WARN_ON(!kmsan_metadata_is_contiguous(address, size));
	shadow = kmsan_get_metadata(address, KMSAN_META_SHADOW);
	if (!shadow)
		goto return_dummy;

	ret.shadow = shadow;
	ret.origin = kmsan_get_metadata(address, KMSAN_META_ORIGIN);
	return ret;

return_dummy:
	if (store) {
		/* Ignore this store. */
		ret.shadow = dummy_store_page;
		ret.origin = dummy_store_page;
	} else {
		/* This load will return zero. */
		ret.shadow = dummy_load_page;
		ret.origin = dummy_load_page;
	}
	return ret;
}

/*
 * Obtain the shadow or origin pointer for the given address, or NULL if there's
 * none. The caller must check the return value for being non-NULL if needed.
 * The return value of this function should not depend on whether we're in the
 * runtime or not.
 */
void *kmsan_get_metadata(void *address, bool is_origin)
{
	u64 addr = (u64)address, pad, off;
	struct page *page;

	if (is_origin && !IS_ALIGNED(addr, KMSAN_ORIGIN_SIZE)) {
		pad = addr % KMSAN_ORIGIN_SIZE;
		addr -= pad;
	}
	address = (void *)addr;
	if (kmsan_internal_is_vmalloc_addr(address) ||
	    kmsan_internal_is_module_addr(address))
		return (void *)vmalloc_meta(address, is_origin);

	page = virt_to_page_or_null(address);
	if (!page)
		return NULL;
	if (!page_has_metadata(page))
		return NULL;
	off = addr % PAGE_SIZE;

	return (is_origin ? origin_ptr_for(page) : shadow_ptr_for(page)) + off;
}
