/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018 Google LLC
 */
#ifndef _INCFS_INTERNAL_H
#define _INCFS_INTERNAL_H
#include <linux/types.h>

struct mem_range {
	u8 *data;
	size_t len;
};

static inline struct mem_range range(u8 *data, size_t len)
{
	return (struct mem_range){ .data = data, .len = len };
}

#define LOCK_REQUIRED(lock)  WARN_ON_ONCE(!mutex_is_locked(&lock))

#endif /* _INCFS_INTERNAL_H */
