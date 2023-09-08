// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains software tag-based KASAN specific error reporting code.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 *
 * Some code borrowed from https://github.com/xairy/kasan-prototype by
 *        Andrey Konovalov <andreyknvl@gmail.com>
 */

#include <linux/bitops.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/stackdepot.h>
#include <linux/stacktrace.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kasan.h>
#include <linux/module.h>

#include <asm/sections.h>

#include "kasan.h"
#include "../slab.h"

void *kasan_find_first_bad_addr(void *addr, size_t size)
{
	u8 tag = get_tag(addr);
	void *p = kasan_reset_tag(addr);
	void *end = p + size;

	if (!addr_has_metadata(p))
		return p;

	while (p < end && tag == *(u8 *)kasan_mem_to_shadow(p))
		p += KASAN_GRANULE_SIZE;

	return p;
}

size_t kasan_get_alloc_size(void *object, struct kmem_cache *cache)
{
	size_t size = 0;
	u8 *shadow;

	/*
	 * Skip the addr_has_metadata check, as this function only operates on
	 * slab memory, which must have metadata.
	 */

	/*
	 * The loop below returns 0 for freed objects, for which KASAN cannot
	 * calculate the allocation size based on the metadata.
	 */
	shadow = (u8 *)kasan_mem_to_shadow(object);
	while (size < cache->object_size) {
		if (*shadow != KASAN_TAG_INVALID)
			size += KASAN_GRANULE_SIZE;
		else
			return size;
		shadow++;
	}

	return cache->object_size;
}

void kasan_metadata_fetch_row(char *buffer, void *row)
{
	memcpy(buffer, kasan_mem_to_shadow(row), META_BYTES_PER_ROW);
}

void kasan_print_tags(u8 addr_tag, const void *addr)
{
	u8 *shadow = (u8 *)kasan_mem_to_shadow(addr);

	pr_err("Pointer tag: [%02x], memory tag: [%02x]\n", addr_tag, *shadow);
}

#ifdef CONFIG_KASAN_STACK
void kasan_print_address_stack_frame(const void *addr)
{
	if (WARN_ON(!object_is_on_stack(addr)))
		return;

	pr_err("The buggy address belongs to stack of task %s/%d\n",
	       current->comm, task_pid_nr(current));
}
#endif
