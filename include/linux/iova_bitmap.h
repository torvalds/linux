/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022, Oracle and/or its affiliates.
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */
#ifndef _IOVA_BITMAP_H_
#define _IOVA_BITMAP_H_

#include <linux/types.h>

struct iova_bitmap;

typedef int (*iova_bitmap_fn_t)(struct iova_bitmap *bitmap,
				unsigned long iova, size_t length,
				void *opaque);

struct iova_bitmap *iova_bitmap_alloc(unsigned long iova, size_t length,
				      unsigned long page_size,
				      u64 __user *data);
void iova_bitmap_free(struct iova_bitmap *bitmap);
int iova_bitmap_for_each(struct iova_bitmap *bitmap, void *opaque,
			 iova_bitmap_fn_t fn);
void iova_bitmap_set(struct iova_bitmap *bitmap,
		     unsigned long iova, size_t length);

#endif
