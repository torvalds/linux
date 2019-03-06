// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains generic KASAN specific error reporting code.
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

void *find_first_bad_addr(void *addr, size_t size)
{
	void *p = addr;

	while (p < addr + size && !(*(u8 *)kasan_mem_to_shadow(p)))
		p += KASAN_SHADOW_SCALE_SIZE;
	return p;
}

static const char *get_shadow_bug_type(struct kasan_access_info *info)
{
	const char *bug_type = "unknown-crash";
	u8 *shadow_addr;

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
	case KASAN_USE_AFTER_SCOPE:
		bug_type = "use-after-scope";
		break;
	case KASAN_ALLOCA_LEFT:
	case KASAN_ALLOCA_RIGHT:
		bug_type = "alloca-out-of-bounds";
		break;
	}

	return bug_type;
}

static const char *get_wild_bug_type(struct kasan_access_info *info)
{
	const char *bug_type = "unknown-crash";

	if ((unsigned long)info->access_addr < PAGE_SIZE)
		bug_type = "null-ptr-deref";
	else if ((unsigned long)info->access_addr < TASK_SIZE)
		bug_type = "user-memory-access";
	else
		bug_type = "wild-memory-access";

	return bug_type;
}

const char *get_bug_type(struct kasan_access_info *info)
{
	if (addr_has_shadow(info->access_addr))
		return get_shadow_bug_type(info);
	return get_wild_bug_type(info);
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
