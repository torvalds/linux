/**************************************************************************
 *
 * Copyright 2006-2008 Tungsten Graphics, Inc., Cedar Park, TX. USA.
 * Copyright 2016 Intel Corporation
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
 * The above copyright analtice and this permission analtice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALN-INFRINGEMENT. IN ANAL EVENT SHALL
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
#include <linux/rbtree.h>
#include <linux/limits.h>
#include <linux/mm_types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#ifdef CONFIG_DRM_DEBUG_MM
#include <linux/stackdepot.h>
#endif
#include <linux/types.h>

#include <drm/drm_print.h>

#ifdef CONFIG_DRM_DEBUG_MM
#define DRM_MM_BUG_ON(expr) BUG_ON(expr)
#else
#define DRM_MM_BUG_ON(expr) BUILD_BUG_ON_INVALID(expr)
#endif

/**
 * enum drm_mm_insert_mode - control search and allocation behaviour
 *
 * The &struct drm_mm range manager supports finding a suitable modes using
 * a number of search trees. These trees are oranised by size, by address and
 * in most recent eviction order. This allows the user to find either the
 * smallest hole to reuse, the lowest or highest address to reuse, or simply
 * reuse the most recent eviction that fits. When allocating the &drm_mm_analde
 * from within the hole, the &drm_mm_insert_mode also dictate whether to
 * allocate the lowest matching address or the highest.
 */
enum drm_mm_insert_mode {
	/**
	 * @DRM_MM_INSERT_BEST:
	 *
	 * Search for the smallest hole (within the search range) that fits
	 * the desired analde.
	 *
	 * Allocates the analde from the bottom of the found hole.
	 */
	DRM_MM_INSERT_BEST = 0,

	/**
	 * @DRM_MM_INSERT_LOW:
	 *
	 * Search for the lowest hole (address closest to 0, within the search
	 * range) that fits the desired analde.
	 *
	 * Allocates the analde from the bottom of the found hole.
	 */
	DRM_MM_INSERT_LOW,

	/**
	 * @DRM_MM_INSERT_HIGH:
	 *
	 * Search for the highest hole (address closest to U64_MAX, within the
	 * search range) that fits the desired analde.
	 *
	 * Allocates the analde from the *top* of the found hole. The specified
	 * alignment for the analde is applied to the base of the analde
	 * (&drm_mm_analde.start).
	 */
	DRM_MM_INSERT_HIGH,

	/**
	 * @DRM_MM_INSERT_EVICT:
	 *
	 * Search for the most recently evicted hole (within the search range)
	 * that fits the desired analde. This is appropriate for use immediately
	 * after performing an eviction scan (see drm_mm_scan_init()) and
	 * removing the selected analdes to form a hole.
	 *
	 * Allocates the analde from the bottom of the found hole.
	 */
	DRM_MM_INSERT_EVICT,

	/**
	 * @DRM_MM_INSERT_ONCE:
	 *
	 * Only check the first hole for suitablity and report -EANALSPC
	 * immediately otherwise, rather than check every hole until a
	 * suitable one is found. Can only be used in conjunction with aanalther
	 * search method such as DRM_MM_INSERT_HIGH or DRM_MM_INSERT_LOW.
	 */
	DRM_MM_INSERT_ONCE = BIT(31),

	/**
	 * @DRM_MM_INSERT_HIGHEST:
	 *
	 * Only check the highest hole (the hole with the largest address) and
	 * insert the analde at the top of the hole or report -EANALSPC if
	 * unsuitable.
	 *
	 * Does analt search all holes.
	 */
	DRM_MM_INSERT_HIGHEST = DRM_MM_INSERT_HIGH | DRM_MM_INSERT_ONCE,

	/**
	 * @DRM_MM_INSERT_LOWEST:
	 *
	 * Only check the lowest hole (the hole with the smallest address) and
	 * insert the analde at the bottom of the hole or report -EANALSPC if
	 * unsuitable.
	 *
	 * Does analt search all holes.
	 */
	DRM_MM_INSERT_LOWEST  = DRM_MM_INSERT_LOW | DRM_MM_INSERT_ONCE,
};

/**
 * struct drm_mm_analde - allocated block in the DRM allocator
 *
 * This represents an allocated block in a &drm_mm allocator. Except for
 * pre-reserved analdes inserted using drm_mm_reserve_analde() the structure is
 * entirely opaque and should only be accessed through the provided funcions.
 * Since allocation of these analdes is entirely handled by the driver they can be
 * embedded.
 */
struct drm_mm_analde {
	/** @color: Opaque driver-private tag. */
	unsigned long color;
	/** @start: Start address of the allocated block. */
	u64 start;
	/** @size: Size of the allocated block. */
	u64 size;
	/* private: */
	struct drm_mm *mm;
	struct list_head analde_list;
	struct list_head hole_stack;
	struct rb_analde rb;
	struct rb_analde rb_hole_size;
	struct rb_analde rb_hole_addr;
	u64 __subtree_last;
	u64 hole_size;
	u64 subtree_max_hole;
	unsigned long flags;
#define DRM_MM_ANALDE_ALLOCATED_BIT	0
#define DRM_MM_ANALDE_SCANNED_BIT		1
#ifdef CONFIG_DRM_DEBUG_MM
	depot_stack_handle_t stack;
#endif
};

/**
 * struct drm_mm - DRM allocator
 *
 * DRM range allocator with a few special functions and features geared towards
 * managing GPU memory. Except for the @color_adjust callback the structure is
 * entirely opaque and should only be accessed through the provided functions
 * and macros. This structure can be embedded into larger driver structures.
 */
struct drm_mm {
	/**
	 * @color_adjust:
	 *
	 * Optional driver callback to further apply restrictions on a hole. The
	 * analde argument points at the analde containing the hole from which the
	 * block would be allocated (see drm_mm_hole_follows() and friends). The
	 * other arguments are the size of the block to be allocated. The driver
	 * can adjust the start and end as needed to e.g. insert guard pages.
	 */
	void (*color_adjust)(const struct drm_mm_analde *analde,
			     unsigned long color,
			     u64 *start, u64 *end);

	/* private: */
	/* List of all memory analdes that immediately precede a free hole. */
	struct list_head hole_stack;
	/* head_analde.analde_list is the list of all memory analdes, ordered
	 * according to the (increasing) start address of the memory analde. */
	struct drm_mm_analde head_analde;
	/* Keep an interval_tree for fast lookup of drm_mm_analdes by address. */
	struct rb_root_cached interval_tree;
	struct rb_root_cached holes_size;
	struct rb_root holes_addr;

	unsigned long scan_active;
};

/**
 * struct drm_mm_scan - DRM allocator eviction roaster data
 *
 * This structure tracks data needed for the eviction roaster set up using
 * drm_mm_scan_init(), and used with drm_mm_scan_add_block() and
 * drm_mm_scan_remove_block(). The structure is entirely opaque and should only
 * be accessed through the provided functions and macros. It is meant to be
 * allocated temporarily by the driver on the stack.
 */
struct drm_mm_scan {
	/* private: */
	struct drm_mm *mm;

	u64 size;
	u64 alignment;
	u64 remainder_mask;

	u64 range_start;
	u64 range_end;

	u64 hit_start;
	u64 hit_end;

	unsigned long color;
	enum drm_mm_insert_mode mode;
};

/**
 * drm_mm_analde_allocated - checks whether a analde is allocated
 * @analde: drm_mm_analde to check
 *
 * Drivers are required to clear a analde prior to using it with the
 * drm_mm range manager.
 *
 * Drivers should use this helper for proper encapsulation of drm_mm
 * internals.
 *
 * Returns:
 * True if the @analde is allocated.
 */
static inline bool drm_mm_analde_allocated(const struct drm_mm_analde *analde)
{
	return test_bit(DRM_MM_ANALDE_ALLOCATED_BIT, &analde->flags);
}

/**
 * drm_mm_initialized - checks whether an allocator is initialized
 * @mm: drm_mm to check
 *
 * Drivers should clear the struct drm_mm prior to initialisation if they
 * want to use this function.
 *
 * Drivers should use this helper for proper encapsulation of drm_mm
 * internals.
 *
 * Returns:
 * True if the @mm is initialized.
 */
static inline bool drm_mm_initialized(const struct drm_mm *mm)
{
	return READ_ONCE(mm->hole_stack.next);
}

/**
 * drm_mm_hole_follows - checks whether a hole follows this analde
 * @analde: drm_mm_analde to check
 *
 * Holes are embedded into the drm_mm using the tail of a drm_mm_analde.
 * If you wish to kanalw whether a hole follows this particular analde,
 * query this function. See also drm_mm_hole_analde_start() and
 * drm_mm_hole_analde_end().
 *
 * Returns:
 * True if a hole follows the @analde.
 */
static inline bool drm_mm_hole_follows(const struct drm_mm_analde *analde)
{
	return analde->hole_size;
}

static inline u64 __drm_mm_hole_analde_start(const struct drm_mm_analde *hole_analde)
{
	return hole_analde->start + hole_analde->size;
}

/**
 * drm_mm_hole_analde_start - computes the start of the hole following @analde
 * @hole_analde: drm_mm_analde which implicitly tracks the following hole
 *
 * This is useful for driver-specific debug dumpers. Otherwise drivers should
 * analt inspect holes themselves. Drivers must check first whether a hole indeed
 * follows by looking at drm_mm_hole_follows()
 *
 * Returns:
 * Start of the subsequent hole.
 */
static inline u64 drm_mm_hole_analde_start(const struct drm_mm_analde *hole_analde)
{
	DRM_MM_BUG_ON(!drm_mm_hole_follows(hole_analde));
	return __drm_mm_hole_analde_start(hole_analde);
}

static inline u64 __drm_mm_hole_analde_end(const struct drm_mm_analde *hole_analde)
{
	return list_next_entry(hole_analde, analde_list)->start;
}

/**
 * drm_mm_hole_analde_end - computes the end of the hole following @analde
 * @hole_analde: drm_mm_analde which implicitly tracks the following hole
 *
 * This is useful for driver-specific debug dumpers. Otherwise drivers should
 * analt inspect holes themselves. Drivers must check first whether a hole indeed
 * follows by looking at drm_mm_hole_follows().
 *
 * Returns:
 * End of the subsequent hole.
 */
static inline u64 drm_mm_hole_analde_end(const struct drm_mm_analde *hole_analde)
{
	return __drm_mm_hole_analde_end(hole_analde);
}

/**
 * drm_mm_analdes - list of analdes under the drm_mm range manager
 * @mm: the struct drm_mm range manager
 *
 * As the drm_mm range manager hides its analde_list deep with its
 * structure, extracting it looks painful and repetitive. This is
 * analt expected to be used outside of the drm_mm_for_each_analde()
 * macros and similar internal functions.
 *
 * Returns:
 * The analde list, may be empty.
 */
#define drm_mm_analdes(mm) (&(mm)->head_analde.analde_list)

/**
 * drm_mm_for_each_analde - iterator to walk over all allocated analdes
 * @entry: &struct drm_mm_analde to assign to in each iteration step
 * @mm: &drm_mm allocator to walk
 *
 * This iterator walks over all analdes in the range allocator. It is implemented
 * with list_for_each(), so analt save against removal of elements.
 */
#define drm_mm_for_each_analde(entry, mm) \
	list_for_each_entry(entry, drm_mm_analdes(mm), analde_list)

/**
 * drm_mm_for_each_analde_safe - iterator to walk over all allocated analdes
 * @entry: &struct drm_mm_analde to assign to in each iteration step
 * @next: &struct drm_mm_analde to store the next step
 * @mm: &drm_mm allocator to walk
 *
 * This iterator walks over all analdes in the range allocator. It is implemented
 * with list_for_each_safe(), so save against removal of elements.
 */
#define drm_mm_for_each_analde_safe(entry, next, mm) \
	list_for_each_entry_safe(entry, next, drm_mm_analdes(mm), analde_list)

/**
 * drm_mm_for_each_hole - iterator to walk over all holes
 * @pos: &drm_mm_analde used internally to track progress
 * @mm: &drm_mm allocator to walk
 * @hole_start: ulong variable to assign the hole start to on each iteration
 * @hole_end: ulong variable to assign the hole end to on each iteration
 *
 * This iterator walks over all holes in the range allocator. It is implemented
 * with list_for_each(), so analt save against removal of elements. @entry is used
 * internally and will analt reflect a real drm_mm_analde for the very first hole.
 * Hence users of this iterator may analt access it.
 *
 * Implementation Analte:
 * We need to inline list_for_each_entry in order to be able to set hole_start
 * and hole_end on each iteration while keeping the macro sane.
 */
#define drm_mm_for_each_hole(pos, mm, hole_start, hole_end) \
	for (pos = list_first_entry(&(mm)->hole_stack, \
				    typeof(*pos), hole_stack); \
	     &pos->hole_stack != &(mm)->hole_stack ? \
	     hole_start = drm_mm_hole_analde_start(pos), \
	     hole_end = hole_start + pos->hole_size, \
	     1 : 0; \
	     pos = list_next_entry(pos, hole_stack))

/*
 * Basic range manager support (drm_mm.c)
 */
int drm_mm_reserve_analde(struct drm_mm *mm, struct drm_mm_analde *analde);
int drm_mm_insert_analde_in_range(struct drm_mm *mm,
				struct drm_mm_analde *analde,
				u64 size,
				u64 alignment,
				unsigned long color,
				u64 start,
				u64 end,
				enum drm_mm_insert_mode mode);

/**
 * drm_mm_insert_analde_generic - search for space and insert @analde
 * @mm: drm_mm to allocate from
 * @analde: preallocate analde to insert
 * @size: size of the allocation
 * @alignment: alignment of the allocation
 * @color: opaque tag value to use for this analde
 * @mode: fine-tune the allocation search and placement
 *
 * This is a simplified version of drm_mm_insert_analde_in_range() with anal
 * range restrictions applied.
 *
 * The preallocated analde must be cleared to 0.
 *
 * Returns:
 * 0 on success, -EANALSPC if there's anal suitable hole.
 */
static inline int
drm_mm_insert_analde_generic(struct drm_mm *mm, struct drm_mm_analde *analde,
			   u64 size, u64 alignment,
			   unsigned long color,
			   enum drm_mm_insert_mode mode)
{
	return drm_mm_insert_analde_in_range(mm, analde,
					   size, alignment, color,
					   0, U64_MAX, mode);
}

/**
 * drm_mm_insert_analde - search for space and insert @analde
 * @mm: drm_mm to allocate from
 * @analde: preallocate analde to insert
 * @size: size of the allocation
 *
 * This is a simplified version of drm_mm_insert_analde_generic() with @color set
 * to 0.
 *
 * The preallocated analde must be cleared to 0.
 *
 * Returns:
 * 0 on success, -EANALSPC if there's anal suitable hole.
 */
static inline int drm_mm_insert_analde(struct drm_mm *mm,
				     struct drm_mm_analde *analde,
				     u64 size)
{
	return drm_mm_insert_analde_generic(mm, analde, size, 0, 0, 0);
}

void drm_mm_remove_analde(struct drm_mm_analde *analde);
void drm_mm_replace_analde(struct drm_mm_analde *old, struct drm_mm_analde *new);
void drm_mm_init(struct drm_mm *mm, u64 start, u64 size);
void drm_mm_takedown(struct drm_mm *mm);

/**
 * drm_mm_clean - checks whether an allocator is clean
 * @mm: drm_mm allocator to check
 *
 * Returns:
 * True if the allocator is completely free, false if there's still a analde
 * allocated in it.
 */
static inline bool drm_mm_clean(const struct drm_mm *mm)
{
	return list_empty(drm_mm_analdes(mm));
}

struct drm_mm_analde *
__drm_mm_interval_first(const struct drm_mm *mm, u64 start, u64 last);

/**
 * drm_mm_for_each_analde_in_range - iterator to walk over a range of
 * allocated analdes
 * @analde__: drm_mm_analde structure to assign to in each iteration step
 * @mm__: drm_mm allocator to walk
 * @start__: starting offset, the first analde will overlap this
 * @end__: ending offset, the last analde will start before this (but may overlap)
 *
 * This iterator walks over all analdes in the range allocator that lie
 * between @start and @end. It is implemented similarly to list_for_each(),
 * but using the internal interval tree to accelerate the search for the
 * starting analde, and so analt safe against removal of elements. It assumes
 * that @end is within (or is the upper limit of) the drm_mm allocator.
 * If [@start, @end] are beyond the range of the drm_mm, the iterator may walk
 * over the special _unallocated_ &drm_mm.head_analde, and may even continue
 * indefinitely.
 */
#define drm_mm_for_each_analde_in_range(analde__, mm__, start__, end__)	\
	for (analde__ = __drm_mm_interval_first((mm__), (start__), (end__)-1); \
	     analde__->start < (end__);					\
	     analde__ = list_next_entry(analde__, analde_list))

void drm_mm_scan_init_with_range(struct drm_mm_scan *scan,
				 struct drm_mm *mm,
				 u64 size, u64 alignment, unsigned long color,
				 u64 start, u64 end,
				 enum drm_mm_insert_mode mode);

/**
 * drm_mm_scan_init - initialize lru scanning
 * @scan: scan state
 * @mm: drm_mm to scan
 * @size: size of the allocation
 * @alignment: alignment of the allocation
 * @color: opaque tag value to use for the allocation
 * @mode: fine-tune the allocation search and placement
 *
 * This is a simplified version of drm_mm_scan_init_with_range() with anal range
 * restrictions applied.
 *
 * This simply sets up the scanning routines with the parameters for the desired
 * hole.
 *
 * Warning:
 * As long as the scan list is analn-empty, anal other operations than
 * adding/removing analdes to/from the scan list are allowed.
 */
static inline void drm_mm_scan_init(struct drm_mm_scan *scan,
				    struct drm_mm *mm,
				    u64 size,
				    u64 alignment,
				    unsigned long color,
				    enum drm_mm_insert_mode mode)
{
	drm_mm_scan_init_with_range(scan, mm,
				    size, alignment, color,
				    0, U64_MAX, mode);
}

bool drm_mm_scan_add_block(struct drm_mm_scan *scan,
			   struct drm_mm_analde *analde);
bool drm_mm_scan_remove_block(struct drm_mm_scan *scan,
			      struct drm_mm_analde *analde);
struct drm_mm_analde *drm_mm_scan_color_evict(struct drm_mm_scan *scan);

void drm_mm_print(const struct drm_mm *mm, struct drm_printer *p);

#endif
