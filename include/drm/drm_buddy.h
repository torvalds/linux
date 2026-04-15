/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __DRM_BUDDY_H__
#define __DRM_BUDDY_H__

#include <linux/gpu_buddy.h>

struct drm_printer;

/* DRM-specific GPU Buddy Allocator print helpers */
void drm_buddy_print(struct gpu_buddy *mm, struct drm_printer *p);
void drm_buddy_block_print(struct gpu_buddy *mm,
			   struct gpu_buddy_block *block,
			   struct drm_printer *p);
#endif
