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
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>
#endif

enum drm_mm_search_flags {
	DRM_MM_SEARCH_DEFAULT =		0,
	DRM_MM_SEARCH_BEST =		1 << 0,
};

struct drm_mm_node {
	struct list_head node_list;
	struct list_head hole_stack;
	unsigned hole_follows : 1;
	unsigned scanned_block : 1;
	unsigned scanned_prev_free : 1;
	unsigned scanned_next_free : 1;
	unsigned scanned_preceeds_hole : 1;
	unsigned allocated : 1;
	unsigned long color;
	unsigned long start;
	unsigned long size;
	struct drm_mm *mm;
};

struct drm_mm {
	/* List of all memory nodes that immediately precede a free hole. */
	struct list_head hole_stack;
	/* head_node.node_list is the list of all memory nodes, ordered
	 * according to the (increasing) start address of the memory node. */
	struct drm_mm_node head_node;
	unsigned int scan_check_range : 1;
	unsigned scan_alignment;
	unsigned long scan_color;
	unsigned long scan_size;
	unsigned long scan_hit_start;
	unsigned long scan_hit_end;
	unsigned scanned_blocks;
	unsigned long scan_start;
	unsigned long scan_end;
	struct drm_mm_node *prev_scanned_node;

	void (*color_adjust)(struct drm_mm_node *node, unsigned long color,
			     unsigned long *start, unsigned long *end);
};

static inline bool drm_mm_node_allocated(struct drm_mm_node *node)
{
	return node->allocated;
}

static inline bool drm_mm_initialized(struct drm_mm *mm)
{
	return mm->hole_stack.next;
}

static inline unsigned long __drm_mm_hole_node_start(struct drm_mm_node *hole_node)
{
	return hole_node->start + hole_node->size;
}

static inline unsigned long drm_mm_hole_node_start(struct drm_mm_node *hole_node)
{
	BUG_ON(!hole_node->hole_follows);
	return __drm_mm_hole_node_start(hole_node);
}

static inline unsigned long __drm_mm_hole_node_end(struct drm_mm_node *hole_node)
{
	return list_entry(hole_node->node_list.next,
			  struct drm_mm_node, node_list)->start;
}

static inline unsigned long drm_mm_hole_node_end(struct drm_mm_node *hole_node)
{
	return __drm_mm_hole_node_end(hole_node);
}

#define drm_mm_for_each_node(entry, mm) list_for_each_entry(entry, \
						&(mm)->head_node.node_list, \
						node_list)

/* Note that we need to unroll list_for_each_entry in order to inline
 * setting hole_start and hole_end on each iteration and keep the
 * macro sane.
 */
#define drm_mm_for_each_hole(entry, mm, hole_start, hole_end) \
	for (entry = list_entry((mm)->hole_stack.next, struct drm_mm_node, hole_stack); \
	     &entry->hole_stack != &(mm)->hole_stack ? \
	     hole_start = drm_mm_hole_node_start(entry), \
	     hole_end = drm_mm_hole_node_end(entry), \
	     1 : 0; \
	     entry = list_entry(entry->hole_stack.next, struct drm_mm_node, hole_stack))

/*
 * Basic range manager support (drm_mm.c)
 */
extern int drm_mm_reserve_node(struct drm_mm *mm, struct drm_mm_node *node);

extern int drm_mm_insert_node_generic(struct drm_mm *mm,
				      struct drm_mm_node *node,
				      unsigned long size,
				      unsigned alignment,
				      unsigned long color,
				      enum drm_mm_search_flags flags);
static inline int drm_mm_insert_node(struct drm_mm *mm,
				     struct drm_mm_node *node,
				     unsigned long size,
				     unsigned alignment,
				     enum drm_mm_search_flags flags)
{
	return drm_mm_insert_node_generic(mm, node, size, alignment, 0, flags);
}

extern int drm_mm_insert_node_in_range_generic(struct drm_mm *mm,
				       struct drm_mm_node *node,
				       unsigned long size,
				       unsigned alignment,
				       unsigned long color,
				       unsigned long start,
				       unsigned long end,
				       enum drm_mm_search_flags flags);
static inline int drm_mm_insert_node_in_range(struct drm_mm *mm,
					      struct drm_mm_node *node,
					      unsigned long size,
					      unsigned alignment,
					      unsigned long start,
					      unsigned long end,
					      enum drm_mm_search_flags flags)
{
	return drm_mm_insert_node_in_range_generic(mm, node, size, alignment,
						   0, start, end, flags);
}

extern void drm_mm_remove_node(struct drm_mm_node *node);
extern void drm_mm_replace_node(struct drm_mm_node *old, struct drm_mm_node *new);
extern void drm_mm_init(struct drm_mm *mm,
			unsigned long start,
			unsigned long size);
extern void drm_mm_takedown(struct drm_mm *mm);
extern int drm_mm_clean(struct drm_mm *mm);

void drm_mm_init_scan(struct drm_mm *mm,
		      unsigned long size,
		      unsigned alignment,
		      unsigned long color);
void drm_mm_init_scan_with_range(struct drm_mm *mm,
				 unsigned long size,
				 unsigned alignment,
				 unsigned long color,
				 unsigned long start,
				 unsigned long end);
int drm_mm_scan_add_block(struct drm_mm_node *node);
int drm_mm_scan_remove_block(struct drm_mm_node *node);

extern void drm_mm_debug_table(struct drm_mm *mm, const char *prefix);
#ifdef CONFIG_DEBUG_FS
int drm_mm_dump_table(struct seq_file *m, struct drm_mm *mm);
#endif

#endif
