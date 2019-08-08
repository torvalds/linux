// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains common generic and tag-based KASAN error reporting code.
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

#include <linux/bitops.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/sched.h>
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

/* Shadow layout customization. */
#define SHADOW_BYTES_PER_BLOCK 1
#define SHADOW_BLOCKS_PER_ROW 16
#define SHADOW_BYTES_PER_ROW (SHADOW_BLOCKS_PER_ROW * SHADOW_BYTES_PER_BLOCK)
#define SHADOW_ROWS_AROUND_ADDR 2

static unsigned long kasan_flags;

#define KASAN_BIT_REPORTED	0
#define KASAN_BIT_MULTI_SHOT	1

bool kasan_save_enable_multi_shot(void)
{
	return test_and_set_bit(KASAN_BIT_MULTI_SHOT, &kasan_flags);
}
EXPORT_SYMBOL_GPL(kasan_save_enable_multi_shot);

void kasan_restore_multi_shot(bool enabled)
{
	if (!enabled)
		clear_bit(KASAN_BIT_MULTI_SHOT, &kasan_flags);
}
EXPORT_SYMBOL_GPL(kasan_restore_multi_shot);

static int __init kasan_set_multi_shot(char *str)
{
	set_bit(KASAN_BIT_MULTI_SHOT, &kasan_flags);
	return 1;
}
__setup("kasan_multi_shot", kasan_set_multi_shot);

static void print_error_description(struct kasan_access_info *info)
{
	pr_err("BUG: KASAN: %s in %pS\n",
		get_bug_type(info), (void *)info->ip);
	pr_err("%s of size %zu at addr %px by task %s/%d\n",
		info->is_write ? "Write" : "Read", info->access_size,
		info->access_addr, current->comm, task_pid_nr(current));
}

static DEFINE_SPINLOCK(report_lock);

static void start_report(unsigned long *flags)
{
	/*
	 * Make sure we don't end up in loop.
	 */
	kasan_disable_current();
	spin_lock_irqsave(&report_lock, *flags);
	pr_err("==================================================================\n");
}

static void end_report(unsigned long *flags)
{
	pr_err("==================================================================\n");
	add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);
	spin_unlock_irqrestore(&report_lock, *flags);
	if (panic_on_warn)
		panic("panic_on_warn set ...\n");
	kasan_enable_current();
}

static void print_track(struct kasan_track *track, const char *prefix)
{
	pr_err("%s by task %u:\n", prefix, track->pid);
	if (track->stack) {
		unsigned long *entries;
		unsigned int nr_entries;

		nr_entries = stack_depot_fetch(track->stack, &entries);
		stack_trace_print(entries, nr_entries, 0);
	} else {
		pr_err("(stack is not available)\n");
	}
}

static struct page *addr_to_page(const void *addr)
{
	if ((addr >= (void *)PAGE_OFFSET) &&
			(addr < high_memory))
		return virt_to_head_page(addr);
	return NULL;
}

static void describe_object_addr(struct kmem_cache *cache, void *object,
				const void *addr)
{
	unsigned long access_addr = (unsigned long)addr;
	unsigned long object_addr = (unsigned long)object;
	const char *rel_type;
	int rel_bytes;

	pr_err("The buggy address belongs to the object at %px\n"
	       " which belongs to the cache %s of size %d\n",
		object, cache->name, cache->object_size);

	if (!addr)
		return;

	if (access_addr < object_addr) {
		rel_type = "to the left";
		rel_bytes = object_addr - access_addr;
	} else if (access_addr >= object_addr + cache->object_size) {
		rel_type = "to the right";
		rel_bytes = access_addr - (object_addr + cache->object_size);
	} else {
		rel_type = "inside";
		rel_bytes = access_addr - object_addr;
	}

	pr_err("The buggy address is located %d bytes %s of\n"
	       " %d-byte region [%px, %px)\n",
		rel_bytes, rel_type, cache->object_size, (void *)object_addr,
		(void *)(object_addr + cache->object_size));
}

static void describe_object(struct kmem_cache *cache, void *object,
				const void *addr)
{
	struct kasan_alloc_meta *alloc_info = get_alloc_info(cache, object);

	if (cache->flags & SLAB_KASAN) {
		print_track(&alloc_info->alloc_track, "Allocated");
		pr_err("\n");
		print_track(&alloc_info->free_track, "Freed");
		pr_err("\n");
	}

	describe_object_addr(cache, object, addr);
}

static inline bool kernel_or_module_addr(const void *addr)
{
	if (addr >= (void *)_stext && addr < (void *)_end)
		return true;
	if (is_module_address((unsigned long)addr))
		return true;
	return false;
}

static inline bool init_task_stack_addr(const void *addr)
{
	return addr >= (void *)&init_thread_union.stack &&
		(addr <= (void *)&init_thread_union.stack +
			sizeof(init_thread_union.stack));
}

static void print_address_description(void *addr)
{
	struct page *page = addr_to_page(addr);

	dump_stack();
	pr_err("\n");

	if (page && PageSlab(page)) {
		struct kmem_cache *cache = page->slab_cache;
		void *object = nearest_obj(cache, page,	addr);

		describe_object(cache, object, addr);
	}

	if (kernel_or_module_addr(addr) && !init_task_stack_addr(addr)) {
		pr_err("The buggy address belongs to the variable:\n");
		pr_err(" %pS\n", addr);
	}

	if (page) {
		pr_err("The buggy address belongs to the page:\n");
		dump_page(page, "kasan: bad access detected");
	}
}

static bool row_is_guilty(const void *row, const void *guilty)
{
	return (row <= guilty) && (guilty < row + SHADOW_BYTES_PER_ROW);
}

static int shadow_pointer_offset(const void *row, const void *shadow)
{
	/* The length of ">ff00ff00ff00ff00: " is
	 *    3 + (BITS_PER_LONG/8)*2 chars.
	 */
	return 3 + (BITS_PER_LONG/8)*2 + (shadow - row)*2 +
		(shadow - row) / SHADOW_BYTES_PER_BLOCK + 1;
}

static void print_shadow_for_address(const void *addr)
{
	int i;
	const void *shadow = kasan_mem_to_shadow(addr);
	const void *shadow_row;

	shadow_row = (void *)round_down((unsigned long)shadow,
					SHADOW_BYTES_PER_ROW)
		- SHADOW_ROWS_AROUND_ADDR * SHADOW_BYTES_PER_ROW;

	pr_err("Memory state around the buggy address:\n");

	for (i = -SHADOW_ROWS_AROUND_ADDR; i <= SHADOW_ROWS_AROUND_ADDR; i++) {
		const void *kaddr = kasan_shadow_to_mem(shadow_row);
		char buffer[4 + (BITS_PER_LONG/8)*2];
		char shadow_buf[SHADOW_BYTES_PER_ROW];

		snprintf(buffer, sizeof(buffer),
			(i == 0) ? ">%px: " : " %px: ", kaddr);
		/*
		 * We should not pass a shadow pointer to generic
		 * function, because generic functions may try to
		 * access kasan mapping for the passed address.
		 */
		memcpy(shadow_buf, shadow_row, SHADOW_BYTES_PER_ROW);
		print_hex_dump(KERN_ERR, buffer,
			DUMP_PREFIX_NONE, SHADOW_BYTES_PER_ROW, 1,
			shadow_buf, SHADOW_BYTES_PER_ROW, 0);

		if (row_is_guilty(shadow_row, shadow))
			pr_err("%*c\n",
				shadow_pointer_offset(shadow_row, shadow),
				'^');

		shadow_row += SHADOW_BYTES_PER_ROW;
	}
}

static bool report_enabled(void)
{
	if (current->kasan_depth)
		return false;
	if (test_bit(KASAN_BIT_MULTI_SHOT, &kasan_flags))
		return true;
	return !test_and_set_bit(KASAN_BIT_REPORTED, &kasan_flags);
}

void kasan_report_invalid_free(void *object, unsigned long ip)
{
	unsigned long flags;

	start_report(&flags);
	pr_err("BUG: KASAN: double-free or invalid-free in %pS\n", (void *)ip);
	print_tags(get_tag(object), reset_tag(object));
	object = reset_tag(object);
	pr_err("\n");
	print_address_description(object);
	pr_err("\n");
	print_shadow_for_address(object);
	end_report(&flags);
}

void __kasan_report(unsigned long addr, size_t size, bool is_write, unsigned long ip)
{
	struct kasan_access_info info;
	void *tagged_addr;
	void *untagged_addr;
	unsigned long flags;

	if (likely(!report_enabled()))
		return;

	disable_trace_on_warning();

	tagged_addr = (void *)addr;
	untagged_addr = reset_tag(tagged_addr);

	info.access_addr = tagged_addr;
	if (addr_has_shadow(untagged_addr))
		info.first_bad_addr = find_first_bad_addr(tagged_addr, size);
	else
		info.first_bad_addr = untagged_addr;
	info.access_size = size;
	info.is_write = is_write;
	info.ip = ip;

	start_report(&flags);

	print_error_description(&info);
	if (addr_has_shadow(untagged_addr))
		print_tags(get_tag(tagged_addr), info.first_bad_addr);
	pr_err("\n");

	if (addr_has_shadow(untagged_addr)) {
		print_address_description(untagged_addr);
		pr_err("\n");
		print_shadow_for_address(info.first_bad_addr);
	} else {
		dump_stack();
	}

	end_report(&flags);
}
