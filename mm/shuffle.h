// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2018 Intel Corporation. All rights reserved.
#ifndef _MM_SHUFFLE_H
#define _MM_SHUFFLE_H
#include <linux/jump_label.h>

#define SHUFFLE_ORDER MAX_ORDER

#ifdef CONFIG_SHUFFLE_PAGE_ALLOCATOR
DECLARE_STATIC_KEY_FALSE(page_alloc_shuffle_key);
extern void __shuffle_free_memory(pg_data_t *pgdat);
extern bool shuffle_pick_tail(void);
static inline void __meminit shuffle_free_memory(pg_data_t *pgdat)
{
	if (!static_branch_unlikely(&page_alloc_shuffle_key))
		return;
	__shuffle_free_memory(pgdat);
}

extern void __shuffle_zone(struct zone *z);
static inline void __meminit shuffle_zone(struct zone *z)
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
static inline bool shuffle_pick_tail(void)
{
	return false;
}

static inline void shuffle_free_memory(pg_data_t *pgdat)
{
}

static inline void shuffle_zone(struct zone *z)
{
}

static inline bool is_shuffle_order(int order)
{
	return false;
}
#endif
#endif /* _MM_SHUFFLE_H */
