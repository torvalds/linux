/*
 * This file contains core tag-based KASAN code.
 *
 * Copyright (c) 2018 Google, Inc.
 * Author: Andrey Konovalov <andreyknvl@google.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define DISABLE_BRANCH_PROFILING

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

void check_memory_region(unsigned long addr, size_t size, bool write,
				unsigned long ret_ip)
{
}

#define DEFINE_HWASAN_LOAD_STORE(size)					\
	void __hwasan_load##size##_noabort(unsigned long addr)		\
	{								\
	}								\
	EXPORT_SYMBOL(__hwasan_load##size##_noabort);			\
	void __hwasan_store##size##_noabort(unsigned long addr)		\
	{								\
	}								\
	EXPORT_SYMBOL(__hwasan_store##size##_noabort)

DEFINE_HWASAN_LOAD_STORE(1);
DEFINE_HWASAN_LOAD_STORE(2);
DEFINE_HWASAN_LOAD_STORE(4);
DEFINE_HWASAN_LOAD_STORE(8);
DEFINE_HWASAN_LOAD_STORE(16);

void __hwasan_loadN_noabort(unsigned long addr, unsigned long size)
{
}
EXPORT_SYMBOL(__hwasan_loadN_noabort);

void __hwasan_storeN_noabort(unsigned long addr, unsigned long size)
{
}
EXPORT_SYMBOL(__hwasan_storeN_noabort);

void __hwasan_tag_memory(unsigned long addr, u8 tag, unsigned long size)
{
}
EXPORT_SYMBOL(__hwasan_tag_memory);
