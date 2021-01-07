// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains core software tag-based KASAN code.
 *
 * Copyright (c) 2018 Google, Inc.
 * Author: Andrey Konovalov <andreyknvl@google.com>
 */

#define pr_fmt(fmt) "kasan: " fmt

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
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/bug.h>

#include "kasan.h"
#include "../slab.h"

static DEFINE_PER_CPU(u32, prng_state);

void __init kasan_init_sw_tags(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		per_cpu(prng_state, cpu) = (u32)get_cycles();

	pr_info("KernelAddressSanitizer initialized\n");
}

/*
 * If a preemption happens between this_cpu_read and this_cpu_write, the only
 * side effect is that we'll give a few allocated in different contexts objects
 * the same tag. Since tag-based KASAN is meant to be used a probabilistic
 * bug-detection debug feature, this doesn't have significant negative impact.
 *
 * Ideally the tags use strong randomness to prevent any attempts to predict
 * them during explicit exploit attempts. But strong randomness is expensive,
 * and we did an intentional trade-off to use a PRNG. This non-atomic RMW
 * sequence has in fact positive effect, since interrupts that randomly skew
 * PRNG at unpredictable points do only good.
 */
u8 random_tag(void)
{
	u32 state = this_cpu_read(prng_state);

	state = 1664525 * state + 1013904223;
	this_cpu_write(prng_state, state);

	return (u8)(state % (KASAN_TAG_MAX + 1));
}

bool check_memory_region(unsigned long addr, size_t size, bool write,
				unsigned long ret_ip)
{
	u8 tag;
	u8 *shadow_first, *shadow_last, *shadow;
	void *untagged_addr;

	if (unlikely(size == 0))
		return true;

	if (unlikely(addr + size < addr))
		return !kasan_report(addr, size, write, ret_ip);

	tag = get_tag((const void *)addr);

	/*
	 * Ignore accesses for pointers tagged with 0xff (native kernel
	 * pointer tag) to suppress false positives caused by kmap.
	 *
	 * Some kernel code was written to account for archs that don't keep
	 * high memory mapped all the time, but rather map and unmap particular
	 * pages when needed. Instead of storing a pointer to the kernel memory,
	 * this code saves the address of the page structure and offset within
	 * that page for later use. Those pages are then mapped and unmapped
	 * with kmap/kunmap when necessary and virt_to_page is used to get the
	 * virtual address of the page. For arm64 (that keeps the high memory
	 * mapped all the time), kmap is turned into a page_address call.

	 * The issue is that with use of the page_address + virt_to_page
	 * sequence the top byte value of the original pointer gets lost (gets
	 * set to KASAN_TAG_KERNEL (0xFF)).
	 */
	if (tag == KASAN_TAG_KERNEL)
		return true;

	untagged_addr = kasan_reset_tag((const void *)addr);
	if (unlikely(untagged_addr <
			kasan_shadow_to_mem((void *)KASAN_SHADOW_START))) {
		return !kasan_report(addr, size, write, ret_ip);
	}
	shadow_first = kasan_mem_to_shadow(untagged_addr);
	shadow_last = kasan_mem_to_shadow(untagged_addr + size - 1);
	for (shadow = shadow_first; shadow <= shadow_last; shadow++) {
		if (*shadow != tag) {
			return !kasan_report(addr, size, write, ret_ip);
		}
	}

	return true;
}

bool check_invalid_free(void *addr)
{
	u8 tag = get_tag(addr);
	u8 shadow_byte = READ_ONCE(*(u8 *)kasan_mem_to_shadow(kasan_reset_tag(addr)));

	return (shadow_byte == KASAN_TAG_INVALID) ||
		(tag != KASAN_TAG_KERNEL && tag != shadow_byte);
}

#define DEFINE_HWASAN_LOAD_STORE(size)					\
	void __hwasan_load##size##_noabort(unsigned long addr)		\
	{								\
		check_memory_region(addr, size, false, _RET_IP_);	\
	}								\
	EXPORT_SYMBOL(__hwasan_load##size##_noabort);			\
	void __hwasan_store##size##_noabort(unsigned long addr)		\
	{								\
		check_memory_region(addr, size, true, _RET_IP_);	\
	}								\
	EXPORT_SYMBOL(__hwasan_store##size##_noabort)

DEFINE_HWASAN_LOAD_STORE(1);
DEFINE_HWASAN_LOAD_STORE(2);
DEFINE_HWASAN_LOAD_STORE(4);
DEFINE_HWASAN_LOAD_STORE(8);
DEFINE_HWASAN_LOAD_STORE(16);

void __hwasan_loadN_noabort(unsigned long addr, unsigned long size)
{
	check_memory_region(addr, size, false, _RET_IP_);
}
EXPORT_SYMBOL(__hwasan_loadN_noabort);

void __hwasan_storeN_noabort(unsigned long addr, unsigned long size)
{
	check_memory_region(addr, size, true, _RET_IP_);
}
EXPORT_SYMBOL(__hwasan_storeN_noabort);

void __hwasan_tag_memory(unsigned long addr, u8 tag, unsigned long size)
{
	poison_range((void *)addr, size, tag);
}
EXPORT_SYMBOL(__hwasan_tag_memory);

void kasan_set_free_info(struct kmem_cache *cache,
				void *object, u8 tag)
{
	struct kasan_alloc_meta *alloc_meta;
	u8 idx = 0;

	alloc_meta = kasan_get_alloc_meta(cache, object);
	if (!alloc_meta)
		return;

#ifdef CONFIG_KASAN_SW_TAGS_IDENTIFY
	idx = alloc_meta->free_track_idx;
	alloc_meta->free_pointer_tag[idx] = tag;
	alloc_meta->free_track_idx = (idx + 1) % KASAN_NR_FREE_STACKS;
#endif

	kasan_set_track(&alloc_meta->free_track[idx], GFP_NOWAIT);
}

struct kasan_track *kasan_get_free_track(struct kmem_cache *cache,
				void *object, u8 tag)
{
	struct kasan_alloc_meta *alloc_meta;
	int i = 0;

	alloc_meta = kasan_get_alloc_meta(cache, object);
	if (!alloc_meta)
		return NULL;

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
