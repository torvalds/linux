/*
 * This file contains tag-based KASAN specific error reporting code.
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

const char *get_bug_type(struct kasan_access_info *info)
{
	return "invalid-access";
}

void *find_first_bad_addr(void *addr, size_t size)
{
	u8 tag = get_tag(addr);
	void *p = reset_tag(addr);
	void *end = p + size;

	while (p < end && tag == *(u8 *)kasan_mem_to_shadow(p))
		p += KASAN_SHADOW_SCALE_SIZE;
	return p;
}

void print_tags(u8 addr_tag, const void *addr)
{
	u8 *shadow = (u8 *)kasan_mem_to_shadow(addr);

	pr_err("Pointer tag: [%02x], memory tag: [%02x]\n", addr_tag, *shadow);
}
