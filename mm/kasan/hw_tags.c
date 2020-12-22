// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains core hardware tag-based KASAN code.
 *
 * Copyright (c) 2020 Google, Inc.
 * Author: Andrey Konovalov <andreyknvl@google.com>
 */

#define pr_fmt(fmt) "kasan: " fmt

#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>

#include "kasan.h"

/* kasan_init_hw_tags_cpu() is called for each CPU. */
void kasan_init_hw_tags_cpu(void)
{
	hw_init_tags(KASAN_TAG_MAX);
	hw_enable_tagging();
}

/* kasan_init_hw_tags() is called once on boot CPU. */
void __init kasan_init_hw_tags(void)
{
	pr_info("KernelAddressSanitizer initialized\n");
}

void kasan_set_free_info(struct kmem_cache *cache,
				void *object, u8 tag)
{
	struct kasan_alloc_meta *alloc_meta;

	alloc_meta = kasan_get_alloc_meta(cache, object);
	kasan_set_track(&alloc_meta->free_track[0], GFP_NOWAIT);
}

struct kasan_track *kasan_get_free_track(struct kmem_cache *cache,
				void *object, u8 tag)
{
	struct kasan_alloc_meta *alloc_meta;

	alloc_meta = kasan_get_alloc_meta(cache, object);
	return &alloc_meta->free_track[0];
}
