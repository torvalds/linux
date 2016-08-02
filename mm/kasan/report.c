/*
 * This file contains error reporting code.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 *
 * Some code borrowed from https://github.com/xairy/kasan-prototype by
 *        Andrey Konovalov <adech.fo@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/ftrace.h>
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

static const void *find_first_bad_addr(const void *addr, size_t size)
{
	u8 shadow_val = *(u8 *)kasan_mem_to_shadow(addr);
	const void *first_bad_addr = addr;

	while (!shadow_val && first_bad_addr < addr + size) {
		first_bad_addr += KASAN_SHADOW_SCALE_SIZE;
		shadow_val = *(u8 *)kasan_mem_to_shadow(first_bad_addr);
	}
	return first_bad_addr;
}

static void print_error_description(struct kasan_access_info *info)
{
	const char *bug_type = "unknown-crash";
	u8 *shadow_addr;

	info->first_bad_addr = find_first_bad_addr(info->access_addr,
						info->access_size);

	shadow_addr = (u8 *)kasan_mem_to_shadow(info->first_bad_addr);

	/*
	 * If shadow byte value is in [0, KASAN_SHADOW_SCALE_SIZE) we can look
	 * at the next shadow byte to determine the type of the bad access.
	 */
	if (*shadow_addr > 0 && *shadow_addr <= KASAN_SHADOW_SCALE_SIZE - 1)
		shadow_addr++;

	switch (*shadow_addr) {
	case 0 ... KASAN_SHADOW_SCALE_SIZE - 1:
		/*
		 * In theory it's still possible to see these shadow values
		 * due to a data race in the kernel code.
		 */
		bug_type = "out-of-bounds";
		break;
	case KASAN_PAGE_REDZONE:
	case KASAN_KMALLOC_REDZONE:
		bug_type = "slab-out-of-bounds";
		break;
	case KASAN_GLOBAL_REDZONE:
		bug_type = "global-out-of-bounds";
		break;
	case KASAN_STACK_LEFT:
	case KASAN_STACK_MID:
	case KASAN_STACK_RIGHT:
	case KASAN_STACK_PARTIAL:
		bug_type = "stack-out-of-bounds";
		break;
	case KASAN_FREE_PAGE:
	case KASAN_KMALLOC_FREE:
		bug_type = "use-after-free";
		break;
	}

	pr_err("BUG: KASAN: %s in %pS at addr %p\n",
		bug_type, (void *)info->ip,
		info->access_addr);
	pr_err("%s of size %zu by task %s/%d\n",
		info->is_write ? "Write" : "Read",
		info->access_size, current->comm, task_pid_nr(current));
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

static void print_track(struct kasan_track *track)
{
	pr_err("PID = %u\n", track->pid);
	if (track->stack) {
		struct stack_trace trace;

		depot_fetch_stack(track->stack, &trace);
		print_stack_trace(&trace, 0);
	} else {
		pr_err("(stack is not available)\n");
	}
}

static void kasan_object_err(struct kmem_cache *cache, struct page *page,
				void *object, char *unused_reason)
{
	struct kasan_alloc_meta *alloc_info = get_alloc_info(cache, object);

	dump_stack();
	pr_err("Object at %p, in cache %s size: %d\n", object, cache->name,
		cache->object_size);

	if (!(cache->flags & SLAB_KASAN))
		return;

	pr_err("Allocated:\n");
	print_track(&alloc_info->alloc_track);
	pr_err("Freed:\n");
	print_track(&alloc_info->free_track);
}

static void print_address_description(struct kasan_access_info *info)
{
	const void *addr = info->access_addr;

	if ((addr >= (void *)PAGE_OFFSET) &&
		(addr < high_memory)) {
		struct page *page = virt_to_head_page(addr);

		if (PageSlab(page)) {
			void *object;
			struct kmem_cache *cache = page->slab_cache;
			object = nearest_obj(cache, page,
						(void *)info->access_addr);
			kasan_object_err(cache, page, object,
					"kasan: bad access detected");
			return;
		}
		dump_page(page, "kasan: bad access detected");
	}

	if (kernel_or_module_addr(addr)) {
		if (!init_task_stack_addr(addr))
			pr_err("Address belongs to variable %pS\n", addr);
	}
	dump_stack();
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
			(i == 0) ? ">%p: " : " %p: ", kaddr);
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

static DEFINE_SPINLOCK(report_lock);

static void kasan_report_error(struct kasan_access_info *info)
{
	unsigned long flags;
	const char *bug_type;

	/*
	 * Make sure we don't end up in loop.
	 */
	kasan_disable_current();
	spin_lock_irqsave(&report_lock, flags);
	pr_err("================================="
		"=================================\n");
	if (info->access_addr <
			kasan_shadow_to_mem((void *)KASAN_SHADOW_START)) {
		if ((unsigned long)info->access_addr < PAGE_SIZE)
			bug_type = "null-ptr-deref";
		else if ((unsigned long)info->access_addr < TASK_SIZE)
			bug_type = "user-memory-access";
		else
			bug_type = "wild-memory-access";
		pr_err("BUG: KASAN: %s on address %p\n",
			bug_type, info->access_addr);
		pr_err("%s of size %zu by task %s/%d\n",
			info->is_write ? "Write" : "Read",
			info->access_size, current->comm,
			task_pid_nr(current));
		dump_stack();
	} else {
		print_error_description(info);
		print_address_description(info);
		print_shadow_for_address(info->first_bad_addr);
	}
	pr_err("================================="
		"=================================\n");
	add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);
	spin_unlock_irqrestore(&report_lock, flags);
	kasan_enable_current();
}

void kasan_report(unsigned long addr, size_t size,
		bool is_write, unsigned long ip)
{
	struct kasan_access_info info;

	if (likely(!kasan_report_enabled()))
		return;

	disable_trace_on_warning();

	info.access_addr = (void *)addr;
	info.access_size = size;
	info.is_write = is_write;
	info.ip = ip;

	kasan_report_error(&info);
}


#define DEFINE_ASAN_REPORT_LOAD(size)                     \
void __asan_report_load##size##_noabort(unsigned long addr) \
{                                                         \
	kasan_report(addr, size, false, _RET_IP_);	  \
}                                                         \
EXPORT_SYMBOL(__asan_report_load##size##_noabort)

#define DEFINE_ASAN_REPORT_STORE(size)                     \
void __asan_report_store##size##_noabort(unsigned long addr) \
{                                                          \
	kasan_report(addr, size, true, _RET_IP_);	   \
}                                                          \
EXPORT_SYMBOL(__asan_report_store##size##_noabort)

DEFINE_ASAN_REPORT_LOAD(1);
DEFINE_ASAN_REPORT_LOAD(2);
DEFINE_ASAN_REPORT_LOAD(4);
DEFINE_ASAN_REPORT_LOAD(8);
DEFINE_ASAN_REPORT_LOAD(16);
DEFINE_ASAN_REPORT_STORE(1);
DEFINE_ASAN_REPORT_STORE(2);
DEFINE_ASAN_REPORT_STORE(4);
DEFINE_ASAN_REPORT_STORE(8);
DEFINE_ASAN_REPORT_STORE(16);

void __asan_report_load_n_noabort(unsigned long addr, size_t size)
{
	kasan_report(addr, size, false, _RET_IP_);
}
EXPORT_SYMBOL(__asan_report_load_n_noabort);

void __asan_report_store_n_noabort(unsigned long addr, size_t size)
{
	kasan_report(addr, size, true, _RET_IP_);
}
EXPORT_SYMBOL(__asan_report_store_n_noabort);
