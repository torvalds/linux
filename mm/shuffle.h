// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2018 Intel Corporation. All rights reserved.
#ifndef _MM_SHUFFLE_H
#define _MM_SHUFFLE_H
#include <linux/jump_label.h>

/*
 * SHUFFLE_ENABLE is called from the command line enabling path, or by
 * platform-firmware enabling that indicates the presence of a
 * direct-mapped memory-side-cache. SHUFFLE_FORCE_DISABLE is called from
 * the command line path and overrides any previous or future
 * SHUFFLE_ENABLE.
 */
enum mm_shuffle_ctl {
	SHUFFLE_ENABLE,
	SHUFFLE_FORCE_DISABLE,
};

#define SHUFFLE_ORDER (MAX_ORDER-1)

#ifdef CONFIG_SHUFFLE_PAGE_ALLOCATOR
DECLARE_STATIC_KEY_FALSE(page_alloc_shuffle_key);
extern void page_alloc_shuffle(enum mm_shuffle_ctl ctl);
extern void __shuffle_free_memory(pg_data_t *pgdat);
static inline void shuffle_free_memory(pg_data_t *pgdat)
{
	if (!static_branch_unlikely(&page_alloc_shuffle_key))
		return;
	__shuffle_free_memory(pgdat);
}

extern void __shuffle_zone(struct zone *z);
static inline void shuffle_zone(struct zone *z)
{
	if (!static_branch_unlikely(&page_alloc_shuffle_key))
		return;
	__shuffle_zone(z);
}

static inline bool is_shuffle_order(int order)
{
	if (!static_branch_unlikely(&page_alloc_shuffle_key))
		return false;
	return order >= SHUFFLE_ORDER;
}
#else
static inline void shuffle_free_memory(pg_data_t *pgdat)
{
}

static inline void shuffle_zone(struct zone *z)
{
}

static inline void page_alloc_shuffle(enum mm_shuffle_ctl ctl)
{
}

static inline bool is_shuffle_order(int order)
{
	return false;
}
#endif
#endif /* _MM_SHUFFLE_H */
