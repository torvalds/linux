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
#include <linux/sched/task_stack.h>

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

struct page *kasan_addr_to_page(const void *addr)
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

static struct kasan_track *kasan_get_free_track(struct kmem_cache *cache,
		void *object, u8 tag)
{
	struct kasan_alloc_meta *alloc_meta;
	int i = 0;

	alloc_meta = get_alloc_info(cache, object);

#ifdef CONFIG_KASAN_SW_TAGS_IDENTIFY
	for (i = 0; i < KASAN_NR_FREE_STACKS; i++) {
		if (alloc_meta->free_pointer_tag[i] == tag)
			break;
	}
	if (i == KASAN_NR_FREE_STACKS)
		i = alloc_meta->free_track_idx;
#endif

	return &alloc_meta->free_track[i];
}

static void describe_object(struct kmem_cache *cache, void *object,
				const void *addr, u8 tag)
{
	struct kasan_alloc_meta *alloc_info = get_alloc_info(cache, object);

	if (cache->flags & SLAB_KASAN) {
		struct kasan_track *free_track;

		print_track(&alloc_info->alloc_track, "Allocated");
		pr_err("\n");
		free_track = kasan_get_free_track(cache, object, tag);
		print_track(free_track, "Freed");
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

static bool __must_check tokenize_frame_descr(const char **frame_descr,
					      char *token, size_t max_tok_len,
					      unsigned long *value)
{
	const char *sep = strchr(*frame_descr, ' ');

	if (sep == NULL)
		sep = *frame_descr + strlen(*frame_descr);

	if (token != NULL) {
		const size_t tok_len = sep - *frame_descr;

		if (tok_len + 1 > max_tok_len) {
			pr_err("KASAN internal error: frame description too long: %s\n",
			       *frame_descr);
			return false;
		}

		/* Copy token (+ 1 byte for '\0'). */
		strlcpy(token, *frame_descr, tok_len + 1);
	}

	/* Advance frame_descr past separator. */
	*frame_descr = sep + 1;

	if (value != NULL && kstrtoul(token, 10, value)) {
		pr_err("KASAN internal error: not a valid number: %s\n", token);
		return false;
	}

	return true;
}

static void print_decoded_frame_descr(const char *frame_descr)
{
	/*
	 * We need to parse the following string:
	 *    "n alloc_1 alloc_2 ... alloc_n"
	 * where alloc_i looks like
	 *    "offset size len name"
	 * or "offset size len name:line".
	 */

	char token[64];
	unsigned long num_objects;

	if (!tokenize_frame_descr(&frame_descr, token, sizeof(token),
				  &num_objects))
		return;

	pr_err("\n");
	pr_err("this frame has %lu %s:\n", num_objects,
	       num_objects == 1 ? "object" : "objects");

	while (num_objects--) {
		unsigned long offset;
		unsigned long size;

		/* access offset */
		if (!tokenize_frame_descr(&frame_descr, token, sizeof(token),
					  &offset))
			return;
		/* access size */
		if (!tokenize_frame_descr(&frame_descr, token, sizeof(token),
					  &size))
			return;
		/* name length (unused) */
		if (!tokenize_frame_descr(&frame_descr, NULL, 0, NULL))
			return;
		/* object name */
		if (!tokenize_frame_descr(&frame_descr, token, sizeof(token),
					  NULL))
			return;

		/* Strip line number; without filename it's not very helpful. */
		strreplace(token, ':', '\0');

		/* Finally, print object information. */
		pr_err(" [%lu, %lu) '%s'", offset, offset + size, token);
	}
}

static bool __must_check get_address_stack_frame_info(const void *addr,
						      unsigned long *offset,
						      const char **frame_descr,
						      const void **frame_pc)
{
	unsigned long aligned_addr;
	unsigned long mem_ptr;
	const u8 *shadow_bottom;
	const u8 *shadow_ptr;
	const unsigned long *frame;

	BUILD_BUG_ON(IS_ENABLED(CONFIG_STACK_GROWSUP));

	/*
	 * NOTE: We currently only support printing frame information for
	 * accesses to the task's own stack.
	 */
	if (!object_is_on_stack(addr))
		return false;

	aligned_addr = round_down((unsigned long)addr, sizeof(long));
	mem_ptr = round_down(aligned_addr, KASAN_SHADOW_SCALE_SIZE);
	shadow_ptr = kasan_mem_to_shadow((void *)aligned_addr);
	shadow_bottom = kasan_mem_to_shadow(end_of_stack(current));

	while (shadow_ptr >= shadow_bottom && *shadow_ptr != KASAN_STACK_LEFT) {
		shadow_ptr--;
		mem_ptr -= KASAN_SHADOW_SCALE_SIZE;
	}

	while (shadow_ptr >= shadow_bottom && *shadow_ptr == KASAN_STACK_LEFT) {
		shadow_ptr--;
		mem_ptr -= KASAN_SHADOW_SCALE_SIZE;
	}

	if (shadow_ptr < shadow_bottom)
		return false;

	frame = (const unsigned long *)(mem_ptr + KASAN_SHADOW_SCALE_SIZE);
	if (frame[0] != KASAN_CURRENT_STACK_FRAME_MAGIC) {
		pr_err("KASAN internal error: frame info validation failed; invalid marker: %lu\n",
		       frame[0]);
		return false;
	}

	*offset = (unsigned long)addr - (unsigned long)frame;
	*frame_descr = (const char *)frame[1];
	*frame_pc = (void *)frame[2];

	return true;
}

static void print_address_stack_frame(const void *addr)
{
	unsigned long offset;
	const char *frame_descr;
	const void *frame_pc;

	if (IS_ENABLED(CONFIG_KASAN_SW_TAGS))
		return;

	if (!get_address_stack_frame_info(addr, &offset, &frame_descr,
					  &frame_pc))
		return;

	/*
	 * get_address_stack_frame_info only returns true if the given addr is
	 * on the current task's stack.
	 */
	pr_err("\n");
	pr_err("addr %px is located in stack of task %s/%d at offset %lu in frame:\n",
	       addr, current->comm, task_pid_nr(current), offset);
	pr_err(" %pS\n", frame_pc);

	if (!frame_descr)
		return;

	print_decoded_frame_descr(frame_descr);
}

static void print_address_description(void *addr, u8 tag)
{
	struct page *page = kasan_addr_to_page(addr);

	dump_stack();
	pr_err("\n");

	if (page && PageSlab(page)) {
		struct kmem_cache *cache = page->slab_cache;
		void *object = nearest_obj(cache, page,	addr);

		describe_object(cache, object, addr, tag);
	}

	if (kernel_or_module_addr(addr) && !init_task_stack_addr(addr)) {
		pr_err("The buggy address belongs to the variable:\n");
		pr_err(" %pS\n", addr);
	}

	if (page) {
		pr_err("The buggy address belongs to the page:\n");
		dump_page(page, "kasan: bad access detected");
	}

	print_address_stack_frame(addr);
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
	u8 tag = get_tag(object);

	object = reset_tag(object);
	start_report(&flags);
	pr_err("BUG: KASAN: double-free or invalid-free in %pS\n", (void *)ip);
	print_tags(tag, object);
	pr_err("\n");
	print_address_description(object, tag);
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
		print_address_description(untagged_addr, get_tag(tagged_addr));
		pr_err("\n");
		print_shadow_for_address(info.first_bad_addr);
	} else {
		dump_stack();
	}

	end_report(&flags);
}

#ifdef CONFIG_KASAN_INLINE
/*
 * With CONFIG_KASAN_INLINE, accesses to bogus pointers (outside the high
 * canonical half of the address space) cause out-of-bounds shadow memory reads
 * before the actual access. For addresses in the low canonical half of the
 * address space, as well as most non-canonical addresses, that out-of-bounds
 * shadow memory access lands in the non-canonical part of the address space.
 * Help the user figure out what the original bogus pointer was.
 */
void kasan_non_canonical_hook(unsigned long addr)
{
	unsigned long orig_addr;
	const char *bug_type;

	if (addr < KASAN_SHADOW_OFFSET)
		return;

	orig_addr = (addr - KASAN_SHADOW_OFFSET) << KASAN_SHADOW_SCALE_SHIFT;
	/*
	 * For faults near the shadow address for NULL, we can be fairly certain
	 * that this is a KASAN shadow memory access.
	 * For faults that correspond to shadow for low canonical addresses, we
	 * can still be pretty sure - that shadow region is a fairly narrow
	 * chunk of the non-canonical address space.
	 * But faults that look like shadow for non-canonical addresses are a
	 * really large chunk of the address space. In that case, we still
	 * print the decoded address, but make it clear that this is not
	 * necessarily what's actually going on.
	 */
	if (orig_addr < PAGE_SIZE)
		bug_type = "null-ptr-deref";
	else if (orig_addr < TASK_SIZE)
		bug_type = "probably user-memory-access";
	else
		bug_type = "maybe wild-memory-access";
	pr_alert("KASAN: %s in range [0x%016lx-0x%016lx]\n", bug_type,
		 orig_addr, orig_addr + KASAN_SHADOW_MASK);
}
#endif
