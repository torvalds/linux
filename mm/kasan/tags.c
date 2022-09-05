// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains common tag-based KASAN code.
 *
 * Copyright (c) 2018 Google, Inc.
 * Copyright (c) 2020 Google, Inc.
 */

#include <linux/init.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/static_key.h>
#include <linux/string.h>
#include <linux/types.h>

#include "kasan.h"

void kasan_save_alloc_info(struct kmem_cache *cache, void *object, gfp_t flags)
{
}

void kasan_save_free_info(struct kmem_cache *cache,
				void *object, u8 tag)
{
}

struct kasan_track *kasan_get_alloc_track(struct kmem_cache *cache,
						void *object)
{
	return NULL;
}

struct kasan_track *kasan_get_free_track(struct kmem_cache *cache,
						void *object, u8 tag)
{
	return NULL;
}
