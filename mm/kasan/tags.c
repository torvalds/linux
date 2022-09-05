// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains common tag-based KASAN code.
 *
 * Copyright (c) 2018 Google, Inc.
 * Copyright (c) 2020 Google, Inc.
 */

#include <linux/atomic.h>
#include <linux/init.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/static_key.h>
#include <linux/string.h>
#include <linux/types.h>

#include "kasan.h"
#include "../slab.h"

/* Non-zero, as initial pointer values are 0. */
#define STACK_RING_BUSY_PTR ((void *)1)

struct kasan_stack_ring stack_ring = {
	.lock = __RW_LOCK_UNLOCKED(stack_ring.lock)
};

static void save_stack_info(struct kmem_cache *cache, void *object,
			gfp_t gfp_flags, bool is_free)
{
	unsigned long flags;
	depot_stack_handle_t stack;
	u64 pos;
	struct kasan_stack_ring_entry *entry;
	void *old_ptr;

	stack = kasan_save_stack(gfp_flags, true);

	/*
	 * Prevent save_stack_info() from modifying stack ring
	 * when kasan_complete_mode_report_info() is walking it.
	 */
	read_lock_irqsave(&stack_ring.lock, flags);

next:
	pos = atomic64_fetch_add(1, &stack_ring.pos);
	entry = &stack_ring.entries[pos % KASAN_STACK_RING_SIZE];

	/* Detect stack ring entry slots that are being written to. */
	old_ptr = READ_ONCE(entry->ptr);
	if (old_ptr == STACK_RING_BUSY_PTR)
		goto next; /* Busy slot. */
	if (!try_cmpxchg(&entry->ptr, &old_ptr, STACK_RING_BUSY_PTR))
		goto next; /* Busy slot. */

	WRITE_ONCE(entry->size, cache->object_size);
	WRITE_ONCE(entry->pid, current->pid);
	WRITE_ONCE(entry->stack, stack);
	WRITE_ONCE(entry->is_free, is_free);

	/*
	 * Paired with smp_load_acquire() in kasan_complete_mode_report_info().
	 */
	smp_store_release(&entry->ptr, (s64)object);

	read_unlock_irqrestore(&stack_ring.lock, flags);
}

void kasan_save_alloc_info(struct kmem_cache *cache, void *object, gfp_t flags)
{
	save_stack_info(cache, object, flags, false);
}

void kasan_save_free_info(struct kmem_cache *cache, void *object)
{
	save_stack_info(cache, object, GFP_NOWAIT, true);
}
