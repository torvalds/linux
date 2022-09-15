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
