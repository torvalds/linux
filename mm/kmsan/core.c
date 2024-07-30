// SPDX-License-Identifier: GPL-2.0
/*
 * KMSAN runtime library.
 *
 * Copyright (C) 2017-2022 Google LLC
 * Author: Alexander Potapenko <glider@google.com>
 *
 */

#include <asm/page.h>
#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kmsan_types.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/mmzone.h>
#include <linux/percpu-defs.h>
#include <linux/preempt.h>
#include <linux/slab.h>
#include <linux/stackdepot.h>
#include <linux/stacktrace.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include "../slab.h"
#include "kmsan.h"

bool kmsan_enabled __read_mostly;

/*
 * Per-CPU KMSAN context to be used in interrupts, where current->kmsan is
 * unavaliable.
 */
DEFINE_PER_CPU(struct kmsan_ctx, kmsan_percpu_ctx);

void kmsan_internal_task_create(struct task_struct *task)
{
	struct kmsan_ctx *ctx = &task->kmsan_ctx;
	struct thread_info *info = current_thread_info();

	__memset(ctx, 0, sizeof(*ctx));
	kmsan_internal_unpoison_memory(info, sizeof(*info), false);
}

void kmsan_internal_poison_memory(void *address, size_t size, gfp_t flags,
				  unsigned int poison_flags)
{
	u32 extra_bits =
		kmsan_extra_bits(/*depth*/ 0, poison_flags & KMSAN_POISON_FREE);
	bool checked = poison_flags & KMSAN_POISON_CHECK;
	depot_stack_handle_t handle;

	handle = kmsan_save_stack_with_flags(flags, extra_bits);
	kmsan_internal_set_shadow_origin(address, size, -1, handle, checked);
}

void kmsan_internal_unpoison_memory(void *address, size_t size, bool checked)
{
	kmsan_internal_set_shadow_origin(address, size, 0, 0, checked);
}

depot_stack_handle_t kmsan_save_stack_with_flags(gfp_t flags,
						 unsigned int extra)
{
	unsigned long entries[KMSAN_STACK_DEPTH];
	unsigned int nr_entries;
	depot_stack_handle_t handle;

	nr_entries = stack_trace_save(entries, KMSAN_STACK_DEPTH, 0);

	/* Don't sleep. */
	flags &= ~(__GFP_DIRECT_RECLAIM | __GFP_KSWAPD_RECLAIM);

	handle = stack_depot_save(entries, nr_entries, flags);
	return stack_depot_set_extra_bits(handle, extra);
}

/* Copy the metadata following the memmove() behavior. */
void kmsan_internal_memmove_metadata(void *dst, void *src, size_t n)
{
	depot_stack_handle_t prev_old_origin = 0, prev_new_origin = 0;
	int i, iter, step, src_off, dst_off, oiter_src, oiter_dst;
	depot_stack_handle_t old_origin = 0, new_origin = 0;
	depot_stack_handle_t *origin_src, *origin_dst;
	u8 *shadow_src, *shadow_dst;
	u32 *align_shadow_dst;
	bool backwards;

	shadow_dst = kmsan_get_metadata(dst, KMSAN_META_SHADOW);
	if (!shadow_dst)
		return;
	KMSAN_WARN_ON(!kmsan_metadata_is_contiguous(dst, n));
	align_shadow_dst =
		(u32 *)ALIGN_DOWN((u64)shadow_dst, KMSAN_ORIGIN_SIZE);

	shadow_src = kmsan_get_metadata(src, KMSAN_META_SHADOW);
	if (!shadow_src) {
		/* @src is untracked: mark @dst as initialized. */
		kmsan_internal_unpoison_memory(dst, n, /*checked*/ false);
		return;
	}
	KMSAN_WARN_ON(!kmsan_metadata_is_contiguous(src, n));

	origin_dst = kmsan_get_metadata(dst, KMSAN_META_ORIGIN);
	origin_src = kmsan_get_metadata(src, KMSAN_META_ORIGIN);
	KMSAN_WARN_ON(!origin_dst || !origin_src);

	backwards = dst > src;
	step = backwards ? -1 : 1;
	iter = backwards ? n - 1 : 0;
	src_off = (u64)src % KMSAN_ORIGIN_SIZE;
	dst_off = (u64)dst % KMSAN_ORIGIN_SIZE;

	/* Copy shadow bytes one by one, updating the origins if necessary. */
	for (i = 0; i < n; i++, iter += step) {
		oiter_src = (iter + src_off) / KMSAN_ORIGIN_SIZE;
		oiter_dst = (iter + dst_off) / KMSAN_ORIGIN_SIZE;
		if (!shadow_src[iter]) {
			shadow_dst[iter] = 0;
			if (!align_shadow_dst[oiter_dst])
				origin_dst[oiter_dst] = 0;
			continue;
		}
		shadow_dst[iter] = shadow_src[iter];
		old_origin = origin_src[oiter_src];
		if (old_origin == prev_old_origin)
			new_origin = prev_new_origin;
		else {
			/*
			 * kmsan_internal_chain_origin() may return
			 * NULL, but we don't want to lose the previous
			 * origin value.
			 */
			new_origin = kmsan_internal_chain_origin(old_origin);
			if (!new_origin)
				new_origin = old_origin;
		}
		origin_dst[oiter_dst] = new_origin;
		prev_new_origin = new_origin;
		prev_old_origin = old_origin;
	}
}

depot_stack_handle_t kmsan_internal_chain_origin(depot_stack_handle_t id)
{
	unsigned long entries[3];
	u32 extra_bits;
	int depth;
	bool uaf;
	depot_stack_handle_t handle;

	if (!id)
		return id;
	/*
	 * Make sure we have enough spare bits in @id to hold the UAF bit and
	 * the chain depth.
	 */
	BUILD_BUG_ON(
		(1 << STACK_DEPOT_EXTRA_BITS) <= (KMSAN_MAX_ORIGIN_DEPTH << 1));

	extra_bits = stack_depot_get_extra_bits(id);
	depth = kmsan_depth_from_eb(extra_bits);
	uaf = kmsan_uaf_from_eb(extra_bits);

	/*
	 * Stop chaining origins once the depth reached KMSAN_MAX_ORIGIN_DEPTH.
	 * This mostly happens in the case structures with uninitialized padding
	 * are copied around many times. Origin chains for such structures are
	 * usually periodic, and it does not make sense to fully store them.
	 */
	if (depth == KMSAN_MAX_ORIGIN_DEPTH)
		return id;

	depth++;
	extra_bits = kmsan_extra_bits(depth, uaf);

	entries[0] = KMSAN_CHAIN_MAGIC_ORIGIN;
	entries[1] = kmsan_save_stack_with_flags(__GFP_HIGH, 0);
	entries[2] = id;
	/*
	 * @entries is a local var in non-instrumented code, so KMSAN does not
	 * know it is initialized. Explicitly unpoison it to avoid false
	 * positives when stack_depot_save() passes it to instrumented code.
	 */
	kmsan_internal_unpoison_memory(entries, sizeof(entries), false);
	handle = stack_depot_save(entries, ARRAY_SIZE(entries), __GFP_HIGH);
	return stack_depot_set_extra_bits(handle, extra_bits);
}

void kmsan_internal_set_shadow_origin(void *addr, size_t size, int b,
				      u32 origin, bool checked)
{
	u64 address = (u64)addr;
	u32 *shadow_start, *origin_start;
	size_t pad = 0;

	KMSAN_WARN_ON(!kmsan_metadata_is_contiguous(addr, size));
	shadow_start = kmsan_get_metadata(addr, KMSAN_META_SHADOW);
	if (!shadow_start) {
		/*
		 * kmsan_metadata_is_contiguous() is true, so either all shadow
		 * and origin pages are NULL, or all are non-NULL.
		 */
		if (checked) {
			pr_err("%s: not memsetting %ld bytes starting at %px, because the shadow is NULL\n",
			       __func__, size, addr);
			KMSAN_WARN_ON(true);
		}
		return;
	}
	__memset(shadow_start, b, size);

	if (!IS_ALIGNED(address, KMSAN_ORIGIN_SIZE)) {
		pad = address % KMSAN_ORIGIN_SIZE;
		address -= pad;
		size += pad;
	}
	size = ALIGN(size, KMSAN_ORIGIN_SIZE);
	origin_start =
		(u32 *)kmsan_get_metadata((void *)address, KMSAN_META_ORIGIN);

	/*
	 * If the new origin is non-zero, assume that the shadow byte is also non-zero,
	 * and unconditionally overwrite the old origin slot.
	 * If the new origin is zero, overwrite the old origin slot iff the
	 * corresponding shadow slot is zero.
	 */
	for (int i = 0; i < size / KMSAN_ORIGIN_SIZE; i++) {
		if (origin || !shadow_start[i])
			origin_start[i] = origin;
	}
}

struct page *kmsan_vmalloc_to_page_or_null(void *vaddr)
{
	struct page *page;

	if (!kmsan_internal_is_vmalloc_addr(vaddr) &&
	    !kmsan_internal_is_module_addr(vaddr))
		return NULL;
	page = vmalloc_to_page(vaddr);
	if (pfn_valid(page_to_pfn(page)))
		return page;
	else
		return NULL;
}

void kmsan_internal_check_memory(void *addr, size_t size,
				 const void __user *user_addr, int reason)
{
	depot_stack_handle_t cur_origin = 0, new_origin = 0;
	unsigned long addr64 = (unsigned long)addr;
	depot_stack_handle_t *origin = NULL;
	unsigned char *shadow = NULL;
	int cur_off_start = -1;
	int chunk_size;
	size_t pos = 0;

	if (!size)
		return;
	KMSAN_WARN_ON(!kmsan_metadata_is_contiguous(addr, size));
	while (pos < size) {
		chunk_size = min(size - pos,
				 PAGE_SIZE - ((addr64 + pos) % PAGE_SIZE));
		shadow = kmsan_get_metadata((void *)(addr64 + pos),
					    KMSAN_META_SHADOW);
		if (!shadow) {
			/*
			 * This page is untracked. If there were uninitialized
			 * bytes before, report them.
			 */
			if (cur_origin) {
				kmsan_enter_runtime();
				kmsan_report(cur_origin, addr, size,
					     cur_off_start, pos - 1, user_addr,
					     reason);
				kmsan_leave_runtime();
			}
			cur_origin = 0;
			cur_off_start = -1;
			pos += chunk_size;
			continue;
		}
		for (int i = 0; i < chunk_size; i++) {
			if (!shadow[i]) {
				/*
				 * This byte is unpoisoned. If there were
				 * poisoned bytes before, report them.
				 */
				if (cur_origin) {
					kmsan_enter_runtime();
					kmsan_report(cur_origin, addr, size,
						     cur_off_start, pos + i - 1,
						     user_addr, reason);
					kmsan_leave_runtime();
				}
				cur_origin = 0;
				cur_off_start = -1;
				continue;
			}
			origin = kmsan_get_metadata((void *)(addr64 + pos + i),
						    KMSAN_META_ORIGIN);
			KMSAN_WARN_ON(!origin);
			new_origin = *origin;
			/*
			 * Encountered new origin - report the previous
			 * uninitialized range.
			 */
			if (cur_origin != new_origin) {
				if (cur_origin) {
					kmsan_enter_runtime();
					kmsan_report(cur_origin, addr, size,
						     cur_off_start, pos + i - 1,
						     user_addr, reason);
					kmsan_leave_runtime();
				}
				cur_origin = new_origin;
				cur_off_start = pos + i;
			}
		}
		pos += chunk_size;
	}
	KMSAN_WARN_ON(pos != size);
	if (cur_origin) {
		kmsan_enter_runtime();
		kmsan_report(cur_origin, addr, size, cur_off_start, pos - 1,
			     user_addr, reason);
		kmsan_leave_runtime();
	}
}

bool kmsan_metadata_is_contiguous(void *addr, size_t size)
{
	char *cur_shadow = NULL, *next_shadow = NULL, *cur_origin = NULL,
	     *next_origin = NULL;
	u64 cur_addr = (u64)addr, next_addr = cur_addr + PAGE_SIZE;
	depot_stack_handle_t *origin_p;
	bool all_untracked = false;

	if (!size)
		return true;

	/* The whole range belongs to the same page. */
	if (ALIGN_DOWN(cur_addr + size - 1, PAGE_SIZE) ==
	    ALIGN_DOWN(cur_addr, PAGE_SIZE))
		return true;

	cur_shadow = kmsan_get_metadata((void *)cur_addr, /*is_origin*/ false);
	if (!cur_shadow)
		all_untracked = true;
	cur_origin = kmsan_get_metadata((void *)cur_addr, /*is_origin*/ true);
	if (all_untracked && cur_origin)
		goto report;

	for (; next_addr < (u64)addr + size;
	     cur_addr = next_addr, cur_shadow = next_shadow,
	     cur_origin = next_origin, next_addr += PAGE_SIZE) {
		next_shadow = kmsan_get_metadata((void *)next_addr, false);
		next_origin = kmsan_get_metadata((void *)next_addr, true);
		if (all_untracked) {
			if (next_shadow || next_origin)
				goto report;
			if (!next_shadow && !next_origin)
				continue;
		}
		if (((u64)cur_shadow == ((u64)next_shadow - PAGE_SIZE)) &&
		    ((u64)cur_origin == ((u64)next_origin - PAGE_SIZE)))
			continue;
		goto report;
	}
	return true;

report:
	pr_err("%s: attempting to access two shadow page ranges.\n", __func__);
	pr_err("Access of size %ld at %px.\n", size, addr);
	pr_err("Addresses belonging to different ranges: %px and %px\n",
	       (void *)cur_addr, (void *)next_addr);
	pr_err("page[0].shadow: %px, page[1].shadow: %px\n", cur_shadow,
	       next_shadow);
	pr_err("page[0].origin: %px, page[1].origin: %px\n", cur_origin,
	       next_origin);
	origin_p = kmsan_get_metadata(addr, KMSAN_META_ORIGIN);
	if (origin_p) {
		pr_err("Origin: %08x\n", *origin_p);
		kmsan_print_origin(*origin_p);
	} else {
		pr_err("Origin: unavailable\n");
	}
	return false;
}
