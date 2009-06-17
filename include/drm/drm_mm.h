/**************************************************************************
 *
 * Copyright 2006-2008 Tungsten Graphics, Inc., Cedar Park, TX. USA.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 **************************************************************************/
/*
 * Authors:
 * Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 */

#ifndef _DRM_MM_H_
#define _DRM_MM_H_

/*
 * Generic range manager structs
 */
#include <linux/list.h>

struct drm_mm_node {
	struct list_head fl_entry;
	struct list_head ml_entry;
	int free;
	unsigned long start;
	unsigned long size;
	struct drm_mm *mm;
	void *private;
};

struct drm_mm {
	struct list_head fl_entry;
	struct list_head ml_entry;
	struct list_head unused_nodes;
	int num_unused;
	spinlock_t unused_lock;
};

/*
 * Basic range manager support (drm_mm.c)
 */

extern struct drm_mm_node *drm_mm_get_block(struct drm_mm_node *parent,
					    unsigned long size,
					    unsigned alignment);
extern struct drm_mm_node *drm_mm_get_block_atomic(struct drm_mm_node *parent,
						   unsigned long size,
						   unsigned alignment);
extern void drm_mm_put_block(struct drm_mm_node *cur);
extern struct drm_mm_node *drm_mm_search_free(const struct drm_mm *mm,
					      unsigned long size,
					      unsigned alignment,
					      int best_match);
extern int drm_mm_init(struct drm_mm *mm, unsigned long start,
		       unsigned long size);
extern void drm_mm_takedown(struct drm_mm *mm);
extern int drm_mm_clean(struct drm_mm *mm);
extern unsigned long drm_mm_tail_space(struct drm_mm *mm);
extern int drm_mm_remove_space_from_tail(struct drm_mm *mm,
					 unsigned long size);
extern int drm_mm_add_space_to_tail(struct drm_mm *mm,
				    unsigned long size, int atomic);
extern int drm_mm_pre_get(struct drm_mm *mm);

static inline struct drm_mm *drm_get_mm(struct drm_mm_node *block)
{
	return block->mm;
}

#endif
