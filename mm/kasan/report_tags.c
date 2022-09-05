// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Copyright (c) 2020 Google, Inc.
 */

#include "kasan.h"

const char *kasan_get_bug_type(struct kasan_report_info *info)
{
	/*
	 * If access_size is a negative number, then it has reason to be
	 * defined as out-of-bounds bug type.
	 *
	 * Casting negative numbers to size_t would indeed turn up as
	 * a large size_t and its value will be larger than ULONG_MAX/2,
	 * so that this can qualify as out-of-bounds.
	 */
	if (info->access_addr + info->access_size < info->access_addr)
		return "out-of-bounds";

	return "invalid-access";
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
