// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Copyright (c) 2020 Google, Inc.
 */

#include "kasan.h"
#include "../slab.h"

const char *kasan_get_bug_type(struct kasan_access_info *info)
{
#ifdef CONFIG_KASAN_TAGS_IDENTIFY
	struct kasan_alloc_meta *alloc_meta;
	struct kmem_cache *cache;
	struct slab *slab;
	const void *addr;
	void *object;
	u8 tag;
	int i;

	tag = get_tag(info->access_addr);
	addr = kasan_reset_tag(info->access_addr);
	slab = kasan_addr_to_slab(addr);
	if (slab) {
		cache = slab->slab_cache;
		object = nearest_obj(cache, slab, (void *)addr);
		alloc_meta = kasan_get_alloc_meta(cache, object);

		if (alloc_meta) {
			for (i = 0; i < KASAN_NR_FREE_STACKS; i++) {
				if (alloc_meta->free_pointer_tag[i] == tag)
					return "use-after-free";
			}
		}
		return "out-of-bounds";
	}
#endif

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
