// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains hardware tag-based KASAN specific error reporting code.
 *
 * Copyright (c) 2020 Google, Inc.
 * Author: Andrey Konovalov <andreyknvl@google.com>
 */

#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>

#include "kasan.h"

const void *kasan_find_first_bad_addr(const void *addr, size_t size)
{
	/*
	 * Hardware Tag-Based KASAN only calls this function for normal memory
	 * accesses, and thus addr points precisely to the first bad address
	 * with an invalid (and present) memory tag. Therefore:
	 * 1. Return the address as is without walking memory tags.
	 * 2. Skip the addr_has_metadata check.
	 */
	return kasan_reset_tag(addr);
}

size_t kasan_get_alloc_size(void *object, struct kmem_cache *cache)
{
	size_t size = 0;
	int i = 0;
	u8 memory_tag;

	/*
	 * Skip the addr_has_metadata check, as this function only operates on
	 * slab memory, which must have metadata.
	 */

	/*
	 * The loop below returns 0 for freed objects, for which KASAN cannot
	 * calculate the allocation size based on the metadata.
	 */
	while (size < cache->object_size) {
		memory_tag = hw_get_mem_tag(object + i * KASAN_GRANULE_SIZE);
		if (memory_tag != KASAN_TAG_INVALID)
			size += KASAN_GRANULE_SIZE;
		else
			return size;
		i++;
	}

	return cache->object_size;
}

void kasan_metadata_fetch_row(char *buffer, void *row)
{
	int i;

	for (i = 0; i < META_BYTES_PER_ROW; i++)
		buffer[i] = hw_get_mem_tag(row + i * KASAN_GRANULE_SIZE);
}

void kasan_print_tags(u8 addr_tag, const void *addr)
{
	u8 memory_tag = hw_get_mem_tag((void *)addr);

	pr_err("Pointer tag: [%02x], memory tag: [%02x]\n",
		addr_tag, memory_tag);
}
