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
	DRM_MM_SEARCH_BELOW =		1 << 1,
};

enum drm_mm_allocator_flags {
	DRM_MM_CREATE_DEFAULT =		0,
	DRM_MM_CREATE_TOP =		1 << 0,
};

#define DRM_MM_BOTTOMUP DRM_MM_SEARCH_DEFAULT, DRM_MM_CREATE_DEFAULT
#define DRM_MM_TOPDOWN DRM_MM_SEARCH_BELOW, DRM_MM_CREATE_TOP

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
	u64 start;
	u64 size;
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
	u64 scan_size;
	u64 scan_hit_start;
	u64 scan_hit_end;
	unsigned scanned_blocks;
	u64 scan_start;
	u64 scan_end;
	struct drm_mm_node *prev_scanned_node;

	void (*color_adjust)(struct drm_mm_node *node, unsigned long color,
			     u64 *start, u64 *end);
};

/**
 * drm_mm_node_allocated - checks whether a node is allocated
 * @node: drm_mm_node to check
 *
 * Drivers should use this helpers for proper encapusulation of drm_mm
 * internals.
 *
 * Returns:
 * True if the @node is allocated.
 */
static inline bool drm_mm_node_allocated(struct drm_mm_node *node)
{
	return node->allocated;
}

/**
 * drm_mm_initialized - checks whether an allocator is initialized
 * @mm: drm_mm to check
 *
 * Drivers should use this helpers for proper encapusulation of drm_mm
 * internals.
 *
 * Returns:
 * True if the @mm is initialized.
 */
static inline bool drm_mm_initialized(struct drm_mm *mm)
{
	return mm->hole_stack.next;
}

static inline u64 __drm_mm_hole_node_start(struct drm_mm_node *hole_node)
{
	return hole_node->start + hole_node->size;
}

/**
 * drm_mm_hole_node_start - computes the start of the hole following @node
 * @hole_node: drm_mm_node which implicitly tracks the following hole
 *
 * This is useful for driver-sepific debug dumpers. Otherwise drivers should not
 * inspect holes themselves. Drivers must check first whether a hole indeed
 * follows by looking at node->hole_follows.
 *
 * Returns:
 * Start of the subsequent hole.
 */
static inline u64 drm_mm_hole_node_start(struct drm_mm_node *hole_node)
{
	BUG_ON(!hole_node->hole_follows);
	return __drm_mm_hole_node_start(hole_node);
}

static inline u64 __drm_mm_hole_node_end(struct drm_mm_node *hole_node)
{
	return list_next_entry(hole_node, node_list)->start;
}

/**
 * drm_mm_hole_node_end - computes the end of the hole following @node
 * @hole_node: drm_mm_node which implicitly tracks the following hole
 *
 * This is useful for driver-sepific debug dumpers. Otherwise drivers should not
 * inspect holes themselves. Drivers must check first whether a hole indeed
 * follows by looking at node->hole_follows.
 *
 * Returns:
 * End of the subsequent hole.
 */
static inline u64 drm_mm_hole_node_end(struct drm_mm_node *hole_node)
{
	return __drm_mm_hole_node_end(hole_node);
}

/**
 * drm_mm_for_each_node - iterator to walk over all allocated nodes
 * @entry: drm_mm_node structure to assign to in each iteration step
 * @mm: drm_mm allocator to walk
 *
 * This iterator walks over all nodes in the range allocator. It is implemented
 * with list_for_each, so not save against removal of elements.
 */
#define drm_mm_for_each_node(entry, mm) list_for_each_entry(entry, \
						&(mm)->head_node.node_list, \
						node_list)

#define __drm_mm_for_each_hole(entry, mm, hole_start, hole_end, backwards) \
	for (entry = list_entry((backwards) ? (mm)->hole_stack.prev : (mm)->hole_stack.next, struct drm_mm_node, hole_stack); \
	     &entry->hole_stack != &(mm)->hole_stack ? \
	     hole_start = drm_mm_hole_node_start(entry), \
	     hole_end = drm_mm_hole_node_end(entry), \
	     1 : 0; \
	     entry = list_entry((backwards) ? entry->hole_stack.prev : entry->hole_stack.next, struct drm_mm_node, hole_stack))

/**
 * drm_mm_for_each_hole - iterator to walk over all holes
 * @entry: drm_mm_node used internally to track progress
 * @mm: drm_mm allocator to walk
 * @hole_start: ulong variable to assign the hole start to on each iteration
 * @hole_end: ulong variable to assign the hole end to on each iteration
 *
 * This iterator walks over all holes in the range allocator. It is implemented
 * with list_for_each, so not save against removal of elements. @entry is used
 * internally and will not reflect a real drm_mm_node for the very first hole.
 * Hence users of this iterator may not access it.
 *
 * Implementation Note:
 * We need to inline list_for_each_entry in order to be able to set hole_start
 * and hole_end on each iteration while keeping the macro sane.
 *
 * The __drm_mm_for_each_hole version is similar, but with added support for
 * going backwards.
 */
#define drm_mm_for_each_hole(entry, mm, hole_start, hole_end) \
	__drm_mm_for_each_hole(entry, mm, hole_start, hole_end, 0)

/*
 * Basic range manager support (drm_mm.c)
 */
int drm_mm_reserve_node(struct drm_mm *mm, struct drm_mm_node *node);

int drm_mm_insert_node_generic(struct drm_mm *mm,
			       struct drm_mm_node *node,
			       u64 size,
			       unsigned alignment,
			       unsigned long color,
			       enum drm_mm_search_flags sflags,
			       enum drm_mm_allocator_flags aflags);
/**
 * drm_mm_insert_node - search for space and insert @node
 * @mm: drm_mm to allocate from
 * @node: preallocate node to insert
 * @size: size of the allocation
 * @alignment: alignment of the allocation
 * @flags: flags to fine-tune the allocation
 *
 * This is a simplified version of drm_mm_insert_node_generic() with @color set
 * to 0.
 *
 * The preallocated node must be cleared to 0.
 *
 * Returns:
 * 0 on success, -ENOSPC if there's no suitable hole.
 */
static inline int drm_mm_insert_node(struct drm_mm *mm,
				     struct drm_mm_node *node,
				     u64 size,
				     unsigned alignment,
				     enum drm_mm_search_flags flags)
{
	return drm_mm_insert_node_generic(mm, node, size, alignment, 0, flags,
					  DRM_MM_CREATE_DEFAULT);
}

int drm_mm_insert_node_in_range_generic(struct drm_mm *mm,
					struct drm_mm_node *node,
					u64 size,
					unsigned alignment,
					unsigned long color,
					u64 start,
					u64 end,
					enum drm_mm_search_flags sflags,
					enum drm_mm_allocator_flags aflags);
/**
 * drm_mm_insert_node_in_range - ranged search for space and insert @node
 * @mm: drm_mm to allocate from
 * @node: preallocate node to insert
 * @size: size of the allocation
 * @alignment: alignment of the allocation
 * @start: start of the allowed range for this node
 * @end: end of the allowed range for this node
 * @flags: flags to fine-tune the allocation
 *
 * This is a simplified version of drm_mm_insert_node_in_range_generic() with
 * @color set to 0.
 *
 * The preallocated node must be cleared to 0.
 *
 * Returns:
 * 0 on success, -ENOSPC if there's no suitable hole.
 */
static inline int drm_mm_insert_node_in_range(struct drm_mm *mm,
					      struct drm_mm_node *node,
					      u64 size,
					      unsigned alignment,
					      u64 start,
					      u64 end,
					      enum drm_mm_search_flags flags)
{
	return drm_mm_insert_node_in_range_generic(mm, node, size, alignment,
						   0, start, end, flags,
						   DRM_MM_CREATE_DEFAULT);
}

void drm_mm_remove_node(struct drm_mm_node *node);
void drm_mm_replace_node(struct drm_mm_node *old, struct drm_mm_node *new);
void drm_mm_init(struct drm_mm *mm,
		 u64 start,
		 u64 size);
void drm_mm_takedown(struct drm_mm *mm);
bool drm_mm_clean(struct drm_mm *mm);

void drm_mm_init_scan(struct drm_mm *mm,
		      u64 size,
		      unsigned alignment,
		      unsigned long color);
void drm_mm_init_scan_with_range(struct drm_mm *mm,
				 u64 size,
				 unsigned alignment,
				 unsigned long color,
				 u64 start,
				 u64 end);
bool drm_mm_scan_add_block(struct drm_mm_node *node);
bool drm_mm_scan_remove_block(struct drm_mm_node *node);

void drm_mm_debug_table(struct drm_mm *mm, const char *prefix);
#ifdef CONFIG_DEBUG_FS
int drm_mm_dump_table(struct seq_file *m, struct drm_mm *mm);
#endif

#endif
