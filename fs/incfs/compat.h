/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */
#ifndef _INCFS_COMPAT_H
#define _INCFS_COMPAT_H

#include <linux/lz4.h>
#include <linux/version.h>

typedef unsigned int __poll_t;

#ifndef u64_to_user_ptr
#define u64_to_user_ptr(x) (		\
{					\
	typecheck(u64, x);		\
	(void __user *)(uintptr_t)x;	\
}					\
)
#endif

#ifndef lru_to_page
#define lru_to_page(head) (list_entry((head)->prev, struct page, lru))
#endif

#define readahead_gfp_mask(x)	\
	(mapping_gfp_mask(x) | __GFP_NORETRY | __GFP_NOWARN)

#ifndef SB_ACTIVE
#define SB_ACTIVE MS_ACTIVE
#endif

#endif /* _INCFS_COMPAT_H */
