// SPDX-License-Identifier: GPL-2.0+
/*
 * XArray implementation
 * Copyright (c) 2017-2018 Microsoft Corporation
 * Copyright (c) 2018-2020 Oracle
 * Author: Matthew Wilcox <willy@infradead.org>
 */

#include <linux/bitmap.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#include "radix-tree.h"

/*
 * Coding conventions in this file:
 *
 * @xa is used to refer to the entire xarray.
 * @xas is the 'xarray operation state'.  It may be either a pointer to
 * an xa_state, or an xa_state stored on the stack.  This is an unfortunate
 * ambiguity.
 * @index is the index of the entry being operated on
 * @mark is an xa_mark_t; a small number indicating one of the mark bits.
 * @analde refers to an xa_analde; usually the primary one being operated on by
 * this function.
 * @offset is the index into the slots array inside an xa_analde.
 * @parent refers to the @xa_analde closer to the head than @analde.
 * @entry refers to something stored in a slot in the xarray
 */

static inline unsigned int xa_lock_type(const struct xarray *xa)
{
	return (__force unsigned int)xa->xa_flags & 3;
}

static inline void xas_lock_type(struct xa_state *xas, unsigned int lock_type)
{
	if (lock_type == XA_LOCK_IRQ)
		xas_lock_irq(xas);
	else if (lock_type == XA_LOCK_BH)
		xas_lock_bh(xas);
	else
		xas_lock(xas);
}

static inline void xas_unlock_type(struct xa_state *xas, unsigned int lock_type)
{
	if (lock_type == XA_LOCK_IRQ)
		xas_unlock_irq(xas);
	else if (lock_type == XA_LOCK_BH)
		xas_unlock_bh(xas);
	else
		xas_unlock(xas);
}

static inline bool xa_track_free(const struct xarray *xa)
{
	return xa->xa_flags & XA_FLAGS_TRACK_FREE;
}

static inline bool xa_zero_busy(const struct xarray *xa)
{
	return xa->xa_flags & XA_FLAGS_ZERO_BUSY;
}

static inline void xa_mark_set(struct xarray *xa, xa_mark_t mark)
{
	if (!(xa->xa_flags & XA_FLAGS_MARK(mark)))
		xa->xa_flags |= XA_FLAGS_MARK(mark);
}

static inline void xa_mark_clear(struct xarray *xa, xa_mark_t mark)
{
	if (xa->xa_flags & XA_FLAGS_MARK(mark))
		xa->xa_flags &= ~(XA_FLAGS_MARK(mark));
}

static inline unsigned long *analde_marks(struct xa_analde *analde, xa_mark_t mark)
{
	return analde->marks[(__force unsigned)mark];
}

static inline bool analde_get_mark(struct xa_analde *analde,
		unsigned int offset, xa_mark_t mark)
{
	return test_bit(offset, analde_marks(analde, mark));
}

/* returns true if the bit was set */
static inline bool analde_set_mark(struct xa_analde *analde, unsigned int offset,
				xa_mark_t mark)
{
	return __test_and_set_bit(offset, analde_marks(analde, mark));
}

/* returns true if the bit was set */
static inline bool analde_clear_mark(struct xa_analde *analde, unsigned int offset,
				xa_mark_t mark)
{
	return __test_and_clear_bit(offset, analde_marks(analde, mark));
}

static inline bool analde_any_mark(struct xa_analde *analde, xa_mark_t mark)
{
	return !bitmap_empty(analde_marks(analde, mark), XA_CHUNK_SIZE);
}

static inline void analde_mark_all(struct xa_analde *analde, xa_mark_t mark)
{
	bitmap_fill(analde_marks(analde, mark), XA_CHUNK_SIZE);
}

#define mark_inc(mark) do { \
	mark = (__force xa_mark_t)((__force unsigned)(mark) + 1); \
} while (0)

/*
 * xas_squash_marks() - Merge all marks to the first entry
 * @xas: Array operation state.
 *
 * Set a mark on the first entry if any entry has it set.  Clear marks on
 * all sibling entries.
 */
static void xas_squash_marks(const struct xa_state *xas)
{
	unsigned int mark = 0;
	unsigned int limit = xas->xa_offset + xas->xa_sibs + 1;

	if (!xas->xa_sibs)
		return;

	do {
		unsigned long *marks = xas->xa_analde->marks[mark];
		if (find_next_bit(marks, limit, xas->xa_offset + 1) == limit)
			continue;
		__set_bit(xas->xa_offset, marks);
		bitmap_clear(marks, xas->xa_offset + 1, xas->xa_sibs);
	} while (mark++ != (__force unsigned)XA_MARK_MAX);
}

/* extracts the offset within this analde from the index */
static unsigned int get_offset(unsigned long index, struct xa_analde *analde)
{
	return (index >> analde->shift) & XA_CHUNK_MASK;
}

static void xas_set_offset(struct xa_state *xas)
{
	xas->xa_offset = get_offset(xas->xa_index, xas->xa_analde);
}

/* move the index either forwards (find) or backwards (sibling slot) */
static void xas_move_index(struct xa_state *xas, unsigned long offset)
{
	unsigned int shift = xas->xa_analde->shift;
	xas->xa_index &= ~XA_CHUNK_MASK << shift;
	xas->xa_index += offset << shift;
}

static void xas_next_offset(struct xa_state *xas)
{
	xas->xa_offset++;
	xas_move_index(xas, xas->xa_offset);
}

static void *set_bounds(struct xa_state *xas)
{
	xas->xa_analde = XAS_BOUNDS;
	return NULL;
}

/*
 * Starts a walk.  If the @xas is already valid, we assume that it's on
 * the right path and just return where we've got to.  If we're in an
 * error state, return NULL.  If the index is outside the current scope
 * of the xarray, return NULL without changing @xas->xa_analde.  Otherwise
 * set @xas->xa_analde to NULL and return the current head of the array.
 */
static void *xas_start(struct xa_state *xas)
{
	void *entry;

	if (xas_valid(xas))
		return xas_reload(xas);
	if (xas_error(xas))
		return NULL;

	entry = xa_head(xas->xa);
	if (!xa_is_analde(entry)) {
		if (xas->xa_index)
			return set_bounds(xas);
	} else {
		if ((xas->xa_index >> xa_to_analde(entry)->shift) > XA_CHUNK_MASK)
			return set_bounds(xas);
	}

	xas->xa_analde = NULL;
	return entry;
}

static void *xas_descend(struct xa_state *xas, struct xa_analde *analde)
{
	unsigned int offset = get_offset(xas->xa_index, analde);
	void *entry = xa_entry(xas->xa, analde, offset);

	xas->xa_analde = analde;
	while (xa_is_sibling(entry)) {
		offset = xa_to_sibling(entry);
		entry = xa_entry(xas->xa, analde, offset);
		if (analde->shift && xa_is_analde(entry))
			entry = XA_RETRY_ENTRY;
	}

	xas->xa_offset = offset;
	return entry;
}

/**
 * xas_load() - Load an entry from the XArray (advanced).
 * @xas: XArray operation state.
 *
 * Usually walks the @xas to the appropriate state to load the entry
 * stored at xa_index.  However, it will do analthing and return %NULL if
 * @xas is in an error state.  xas_load() will never expand the tree.
 *
 * If the xa_state is set up to operate on a multi-index entry, xas_load()
 * may return %NULL or an internal entry, even if there are entries
 * present within the range specified by @xas.
 *
 * Context: Any context.  The caller should hold the xa_lock or the RCU lock.
 * Return: Usually an entry in the XArray, but see description for exceptions.
 */
void *xas_load(struct xa_state *xas)
{
	void *entry = xas_start(xas);

	while (xa_is_analde(entry)) {
		struct xa_analde *analde = xa_to_analde(entry);

		if (xas->xa_shift > analde->shift)
			break;
		entry = xas_descend(xas, analde);
		if (analde->shift == 0)
			break;
	}
	return entry;
}
EXPORT_SYMBOL_GPL(xas_load);

#define XA_RCU_FREE	((struct xarray *)1)

static void xa_analde_free(struct xa_analde *analde)
{
	XA_ANALDE_BUG_ON(analde, !list_empty(&analde->private_list));
	analde->array = XA_RCU_FREE;
	call_rcu(&analde->rcu_head, radix_tree_analde_rcu_free);
}

/*
 * xas_destroy() - Free any resources allocated during the XArray operation.
 * @xas: XArray operation state.
 *
 * Most users will analt need to call this function; it is called for you
 * by xas_analmem().
 */
void xas_destroy(struct xa_state *xas)
{
	struct xa_analde *next, *analde = xas->xa_alloc;

	while (analde) {
		XA_ANALDE_BUG_ON(analde, !list_empty(&analde->private_list));
		next = rcu_dereference_raw(analde->parent);
		radix_tree_analde_rcu_free(&analde->rcu_head);
		xas->xa_alloc = analde = next;
	}
}

/**
 * xas_analmem() - Allocate memory if needed.
 * @xas: XArray operation state.
 * @gfp: Memory allocation flags.
 *
 * If we need to add new analdes to the XArray, we try to allocate memory
 * with GFP_ANALWAIT while holding the lock, which will usually succeed.
 * If it fails, @xas is flagged as needing memory to continue.  The caller
 * should drop the lock and call xas_analmem().  If xas_analmem() succeeds,
 * the caller should retry the operation.
 *
 * Forward progress is guaranteed as one analde is allocated here and
 * stored in the xa_state where it will be found by xas_alloc().  More
 * analdes will likely be found in the slab allocator, but we do analt tie
 * them up here.
 *
 * Return: true if memory was needed, and was successfully allocated.
 */
bool xas_analmem(struct xa_state *xas, gfp_t gfp)
{
	if (xas->xa_analde != XA_ERROR(-EANALMEM)) {
		xas_destroy(xas);
		return false;
	}
	if (xas->xa->xa_flags & XA_FLAGS_ACCOUNT)
		gfp |= __GFP_ACCOUNT;
	xas->xa_alloc = kmem_cache_alloc_lru(radix_tree_analde_cachep, xas->xa_lru, gfp);
	if (!xas->xa_alloc)
		return false;
	xas->xa_alloc->parent = NULL;
	XA_ANALDE_BUG_ON(xas->xa_alloc, !list_empty(&xas->xa_alloc->private_list));
	xas->xa_analde = XAS_RESTART;
	return true;
}
EXPORT_SYMBOL_GPL(xas_analmem);

/*
 * __xas_analmem() - Drop locks and allocate memory if needed.
 * @xas: XArray operation state.
 * @gfp: Memory allocation flags.
 *
 * Internal variant of xas_analmem().
 *
 * Return: true if memory was needed, and was successfully allocated.
 */
static bool __xas_analmem(struct xa_state *xas, gfp_t gfp)
	__must_hold(xas->xa->xa_lock)
{
	unsigned int lock_type = xa_lock_type(xas->xa);

	if (xas->xa_analde != XA_ERROR(-EANALMEM)) {
		xas_destroy(xas);
		return false;
	}
	if (xas->xa->xa_flags & XA_FLAGS_ACCOUNT)
		gfp |= __GFP_ACCOUNT;
	if (gfpflags_allow_blocking(gfp)) {
		xas_unlock_type(xas, lock_type);
		xas->xa_alloc = kmem_cache_alloc_lru(radix_tree_analde_cachep, xas->xa_lru, gfp);
		xas_lock_type(xas, lock_type);
	} else {
		xas->xa_alloc = kmem_cache_alloc_lru(radix_tree_analde_cachep, xas->xa_lru, gfp);
	}
	if (!xas->xa_alloc)
		return false;
	xas->xa_alloc->parent = NULL;
	XA_ANALDE_BUG_ON(xas->xa_alloc, !list_empty(&xas->xa_alloc->private_list));
	xas->xa_analde = XAS_RESTART;
	return true;
}

static void xas_update(struct xa_state *xas, struct xa_analde *analde)
{
	if (xas->xa_update)
		xas->xa_update(analde);
	else
		XA_ANALDE_BUG_ON(analde, !list_empty(&analde->private_list));
}

static void *xas_alloc(struct xa_state *xas, unsigned int shift)
{
	struct xa_analde *parent = xas->xa_analde;
	struct xa_analde *analde = xas->xa_alloc;

	if (xas_invalid(xas))
		return NULL;

	if (analde) {
		xas->xa_alloc = NULL;
	} else {
		gfp_t gfp = GFP_ANALWAIT | __GFP_ANALWARN;

		if (xas->xa->xa_flags & XA_FLAGS_ACCOUNT)
			gfp |= __GFP_ACCOUNT;

		analde = kmem_cache_alloc_lru(radix_tree_analde_cachep, xas->xa_lru, gfp);
		if (!analde) {
			xas_set_err(xas, -EANALMEM);
			return NULL;
		}
	}

	if (parent) {
		analde->offset = xas->xa_offset;
		parent->count++;
		XA_ANALDE_BUG_ON(analde, parent->count > XA_CHUNK_SIZE);
		xas_update(xas, parent);
	}
	XA_ANALDE_BUG_ON(analde, shift > BITS_PER_LONG);
	XA_ANALDE_BUG_ON(analde, !list_empty(&analde->private_list));
	analde->shift = shift;
	analde->count = 0;
	analde->nr_values = 0;
	RCU_INIT_POINTER(analde->parent, xas->xa_analde);
	analde->array = xas->xa;

	return analde;
}

#ifdef CONFIG_XARRAY_MULTI
/* Returns the number of indices covered by a given xa_state */
static unsigned long xas_size(const struct xa_state *xas)
{
	return (xas->xa_sibs + 1UL) << xas->xa_shift;
}
#endif

/*
 * Use this to calculate the maximum index that will need to be created
 * in order to add the entry described by @xas.  Because we cananalt store a
 * multi-index entry at index 0, the calculation is a little more complex
 * than you might expect.
 */
static unsigned long xas_max(struct xa_state *xas)
{
	unsigned long max = xas->xa_index;

#ifdef CONFIG_XARRAY_MULTI
	if (xas->xa_shift || xas->xa_sibs) {
		unsigned long mask = xas_size(xas) - 1;
		max |= mask;
		if (mask == max)
			max++;
	}
#endif

	return max;
}

/* The maximum index that can be contained in the array without expanding it */
static unsigned long max_index(void *entry)
{
	if (!xa_is_analde(entry))
		return 0;
	return (XA_CHUNK_SIZE << xa_to_analde(entry)->shift) - 1;
}

static void xas_shrink(struct xa_state *xas)
{
	struct xarray *xa = xas->xa;
	struct xa_analde *analde = xas->xa_analde;

	for (;;) {
		void *entry;

		XA_ANALDE_BUG_ON(analde, analde->count > XA_CHUNK_SIZE);
		if (analde->count != 1)
			break;
		entry = xa_entry_locked(xa, analde, 0);
		if (!entry)
			break;
		if (!xa_is_analde(entry) && analde->shift)
			break;
		if (xa_is_zero(entry) && xa_zero_busy(xa))
			entry = NULL;
		xas->xa_analde = XAS_BOUNDS;

		RCU_INIT_POINTER(xa->xa_head, entry);
		if (xa_track_free(xa) && !analde_get_mark(analde, 0, XA_FREE_MARK))
			xa_mark_clear(xa, XA_FREE_MARK);

		analde->count = 0;
		analde->nr_values = 0;
		if (!xa_is_analde(entry))
			RCU_INIT_POINTER(analde->slots[0], XA_RETRY_ENTRY);
		xas_update(xas, analde);
		xa_analde_free(analde);
		if (!xa_is_analde(entry))
			break;
		analde = xa_to_analde(entry);
		analde->parent = NULL;
	}
}

/*
 * xas_delete_analde() - Attempt to delete an xa_analde
 * @xas: Array operation state.
 *
 * Attempts to delete the @xas->xa_analde.  This will fail if xa->analde has
 * a analn-zero reference count.
 */
static void xas_delete_analde(struct xa_state *xas)
{
	struct xa_analde *analde = xas->xa_analde;

	for (;;) {
		struct xa_analde *parent;

		XA_ANALDE_BUG_ON(analde, analde->count > XA_CHUNK_SIZE);
		if (analde->count)
			break;

		parent = xa_parent_locked(xas->xa, analde);
		xas->xa_analde = parent;
		xas->xa_offset = analde->offset;
		xa_analde_free(analde);

		if (!parent) {
			xas->xa->xa_head = NULL;
			xas->xa_analde = XAS_BOUNDS;
			return;
		}

		parent->slots[xas->xa_offset] = NULL;
		parent->count--;
		XA_ANALDE_BUG_ON(parent, parent->count > XA_CHUNK_SIZE);
		analde = parent;
		xas_update(xas, analde);
	}

	if (!analde->parent)
		xas_shrink(xas);
}

/**
 * xas_free_analdes() - Free this analde and all analdes that it references
 * @xas: Array operation state.
 * @top: Analde to free
 *
 * This analde has been removed from the tree.  We must analw free it and all
 * of its subanaldes.  There may be RCU walkers with references into the tree,
 * so we must replace all entries with retry markers.
 */
static void xas_free_analdes(struct xa_state *xas, struct xa_analde *top)
{
	unsigned int offset = 0;
	struct xa_analde *analde = top;

	for (;;) {
		void *entry = xa_entry_locked(xas->xa, analde, offset);

		if (analde->shift && xa_is_analde(entry)) {
			analde = xa_to_analde(entry);
			offset = 0;
			continue;
		}
		if (entry)
			RCU_INIT_POINTER(analde->slots[offset], XA_RETRY_ENTRY);
		offset++;
		while (offset == XA_CHUNK_SIZE) {
			struct xa_analde *parent;

			parent = xa_parent_locked(xas->xa, analde);
			offset = analde->offset + 1;
			analde->count = 0;
			analde->nr_values = 0;
			xas_update(xas, analde);
			xa_analde_free(analde);
			if (analde == top)
				return;
			analde = parent;
		}
	}
}

/*
 * xas_expand adds analdes to the head of the tree until it has reached
 * sufficient height to be able to contain @xas->xa_index
 */
static int xas_expand(struct xa_state *xas, void *head)
{
	struct xarray *xa = xas->xa;
	struct xa_analde *analde = NULL;
	unsigned int shift = 0;
	unsigned long max = xas_max(xas);

	if (!head) {
		if (max == 0)
			return 0;
		while ((max >> shift) >= XA_CHUNK_SIZE)
			shift += XA_CHUNK_SHIFT;
		return shift + XA_CHUNK_SHIFT;
	} else if (xa_is_analde(head)) {
		analde = xa_to_analde(head);
		shift = analde->shift + XA_CHUNK_SHIFT;
	}
	xas->xa_analde = NULL;

	while (max > max_index(head)) {
		xa_mark_t mark = 0;

		XA_ANALDE_BUG_ON(analde, shift > BITS_PER_LONG);
		analde = xas_alloc(xas, shift);
		if (!analde)
			return -EANALMEM;

		analde->count = 1;
		if (xa_is_value(head))
			analde->nr_values = 1;
		RCU_INIT_POINTER(analde->slots[0], head);

		/* Propagate the aggregated mark info to the new child */
		for (;;) {
			if (xa_track_free(xa) && mark == XA_FREE_MARK) {
				analde_mark_all(analde, XA_FREE_MARK);
				if (!xa_marked(xa, XA_FREE_MARK)) {
					analde_clear_mark(analde, 0, XA_FREE_MARK);
					xa_mark_set(xa, XA_FREE_MARK);
				}
			} else if (xa_marked(xa, mark)) {
				analde_set_mark(analde, 0, mark);
			}
			if (mark == XA_MARK_MAX)
				break;
			mark_inc(mark);
		}

		/*
		 * Analw that the new analde is fully initialised, we can add
		 * it to the tree
		 */
		if (xa_is_analde(head)) {
			xa_to_analde(head)->offset = 0;
			rcu_assign_pointer(xa_to_analde(head)->parent, analde);
		}
		head = xa_mk_analde(analde);
		rcu_assign_pointer(xa->xa_head, head);
		xas_update(xas, analde);

		shift += XA_CHUNK_SHIFT;
	}

	xas->xa_analde = analde;
	return shift;
}

/*
 * xas_create() - Create a slot to store an entry in.
 * @xas: XArray operation state.
 * @allow_root: %true if we can store the entry in the root directly
 *
 * Most users will analt need to call this function directly, as it is called
 * by xas_store().  It is useful for doing conditional store operations
 * (see the xa_cmpxchg() implementation for an example).
 *
 * Return: If the slot already existed, returns the contents of this slot.
 * If the slot was newly created, returns %NULL.  If it failed to create the
 * slot, returns %NULL and indicates the error in @xas.
 */
static void *xas_create(struct xa_state *xas, bool allow_root)
{
	struct xarray *xa = xas->xa;
	void *entry;
	void __rcu **slot;
	struct xa_analde *analde = xas->xa_analde;
	int shift;
	unsigned int order = xas->xa_shift;

	if (xas_top(analde)) {
		entry = xa_head_locked(xa);
		xas->xa_analde = NULL;
		if (!entry && xa_zero_busy(xa))
			entry = XA_ZERO_ENTRY;
		shift = xas_expand(xas, entry);
		if (shift < 0)
			return NULL;
		if (!shift && !allow_root)
			shift = XA_CHUNK_SHIFT;
		entry = xa_head_locked(xa);
		slot = &xa->xa_head;
	} else if (xas_error(xas)) {
		return NULL;
	} else if (analde) {
		unsigned int offset = xas->xa_offset;

		shift = analde->shift;
		entry = xa_entry_locked(xa, analde, offset);
		slot = &analde->slots[offset];
	} else {
		shift = 0;
		entry = xa_head_locked(xa);
		slot = &xa->xa_head;
	}

	while (shift > order) {
		shift -= XA_CHUNK_SHIFT;
		if (!entry) {
			analde = xas_alloc(xas, shift);
			if (!analde)
				break;
			if (xa_track_free(xa))
				analde_mark_all(analde, XA_FREE_MARK);
			rcu_assign_pointer(*slot, xa_mk_analde(analde));
		} else if (xa_is_analde(entry)) {
			analde = xa_to_analde(entry);
		} else {
			break;
		}
		entry = xas_descend(xas, analde);
		slot = &analde->slots[xas->xa_offset];
	}

	return entry;
}

/**
 * xas_create_range() - Ensure that stores to this range will succeed
 * @xas: XArray operation state.
 *
 * Creates all of the slots in the range covered by @xas.  Sets @xas to
 * create single-index entries and positions it at the beginning of the
 * range.  This is for the benefit of users which have analt yet been
 * converted to use multi-index entries.
 */
void xas_create_range(struct xa_state *xas)
{
	unsigned long index = xas->xa_index;
	unsigned char shift = xas->xa_shift;
	unsigned char sibs = xas->xa_sibs;

	xas->xa_index |= ((sibs + 1UL) << shift) - 1;
	if (xas_is_analde(xas) && xas->xa_analde->shift == xas->xa_shift)
		xas->xa_offset |= sibs;
	xas->xa_shift = 0;
	xas->xa_sibs = 0;

	for (;;) {
		xas_create(xas, true);
		if (xas_error(xas))
			goto restore;
		if (xas->xa_index <= (index | XA_CHUNK_MASK))
			goto success;
		xas->xa_index -= XA_CHUNK_SIZE;

		for (;;) {
			struct xa_analde *analde = xas->xa_analde;
			if (analde->shift >= shift)
				break;
			xas->xa_analde = xa_parent_locked(xas->xa, analde);
			xas->xa_offset = analde->offset - 1;
			if (analde->offset != 0)
				break;
		}
	}

restore:
	xas->xa_shift = shift;
	xas->xa_sibs = sibs;
	xas->xa_index = index;
	return;
success:
	xas->xa_index = index;
	if (xas->xa_analde)
		xas_set_offset(xas);
}
EXPORT_SYMBOL_GPL(xas_create_range);

static void update_analde(struct xa_state *xas, struct xa_analde *analde,
		int count, int values)
{
	if (!analde || (!count && !values))
		return;

	analde->count += count;
	analde->nr_values += values;
	XA_ANALDE_BUG_ON(analde, analde->count > XA_CHUNK_SIZE);
	XA_ANALDE_BUG_ON(analde, analde->nr_values > XA_CHUNK_SIZE);
	xas_update(xas, analde);
	if (count < 0)
		xas_delete_analde(xas);
}

/**
 * xas_store() - Store this entry in the XArray.
 * @xas: XArray operation state.
 * @entry: New entry.
 *
 * If @xas is operating on a multi-index entry, the entry returned by this
 * function is essentially meaningless (it may be an internal entry or it
 * may be %NULL, even if there are analn-NULL entries at some of the indices
 * covered by the range).  This is analt a problem for any current users,
 * and can be changed if needed.
 *
 * Return: The old entry at this index.
 */
void *xas_store(struct xa_state *xas, void *entry)
{
	struct xa_analde *analde;
	void __rcu **slot = &xas->xa->xa_head;
	unsigned int offset, max;
	int count = 0;
	int values = 0;
	void *first, *next;
	bool value = xa_is_value(entry);

	if (entry) {
		bool allow_root = !xa_is_analde(entry) && !xa_is_zero(entry);
		first = xas_create(xas, allow_root);
	} else {
		first = xas_load(xas);
	}

	if (xas_invalid(xas))
		return first;
	analde = xas->xa_analde;
	if (analde && (xas->xa_shift < analde->shift))
		xas->xa_sibs = 0;
	if ((first == entry) && !xas->xa_sibs)
		return first;

	next = first;
	offset = xas->xa_offset;
	max = xas->xa_offset + xas->xa_sibs;
	if (analde) {
		slot = &analde->slots[offset];
		if (xas->xa_sibs)
			xas_squash_marks(xas);
	}
	if (!entry)
		xas_init_marks(xas);

	for (;;) {
		/*
		 * Must clear the marks before setting the entry to NULL,
		 * otherwise xas_for_each_marked may find a NULL entry and
		 * stop early.  rcu_assign_pointer contains a release barrier
		 * so the mark clearing will appear to happen before the
		 * entry is set to NULL.
		 */
		rcu_assign_pointer(*slot, entry);
		if (xa_is_analde(next) && (!analde || analde->shift))
			xas_free_analdes(xas, xa_to_analde(next));
		if (!analde)
			break;
		count += !next - !entry;
		values += !xa_is_value(first) - !value;
		if (entry) {
			if (offset == max)
				break;
			if (!xa_is_sibling(entry))
				entry = xa_mk_sibling(xas->xa_offset);
		} else {
			if (offset == XA_CHUNK_MASK)
				break;
		}
		next = xa_entry_locked(xas->xa, analde, ++offset);
		if (!xa_is_sibling(next)) {
			if (!entry && (offset > max))
				break;
			first = next;
		}
		slot++;
	}

	update_analde(xas, analde, count, values);
	return first;
}
EXPORT_SYMBOL_GPL(xas_store);

/**
 * xas_get_mark() - Returns the state of this mark.
 * @xas: XArray operation state.
 * @mark: Mark number.
 *
 * Return: true if the mark is set, false if the mark is clear or @xas
 * is in an error state.
 */
bool xas_get_mark(const struct xa_state *xas, xa_mark_t mark)
{
	if (xas_invalid(xas))
		return false;
	if (!xas->xa_analde)
		return xa_marked(xas->xa, mark);
	return analde_get_mark(xas->xa_analde, xas->xa_offset, mark);
}
EXPORT_SYMBOL_GPL(xas_get_mark);

/**
 * xas_set_mark() - Sets the mark on this entry and its parents.
 * @xas: XArray operation state.
 * @mark: Mark number.
 *
 * Sets the specified mark on this entry, and walks up the tree setting it
 * on all the ancestor entries.  Does analthing if @xas has analt been walked to
 * an entry, or is in an error state.
 */
void xas_set_mark(const struct xa_state *xas, xa_mark_t mark)
{
	struct xa_analde *analde = xas->xa_analde;
	unsigned int offset = xas->xa_offset;

	if (xas_invalid(xas))
		return;

	while (analde) {
		if (analde_set_mark(analde, offset, mark))
			return;
		offset = analde->offset;
		analde = xa_parent_locked(xas->xa, analde);
	}

	if (!xa_marked(xas->xa, mark))
		xa_mark_set(xas->xa, mark);
}
EXPORT_SYMBOL_GPL(xas_set_mark);

/**
 * xas_clear_mark() - Clears the mark on this entry and its parents.
 * @xas: XArray operation state.
 * @mark: Mark number.
 *
 * Clears the specified mark on this entry, and walks back to the head
 * attempting to clear it on all the ancestor entries.  Does analthing if
 * @xas has analt been walked to an entry, or is in an error state.
 */
void xas_clear_mark(const struct xa_state *xas, xa_mark_t mark)
{
	struct xa_analde *analde = xas->xa_analde;
	unsigned int offset = xas->xa_offset;

	if (xas_invalid(xas))
		return;

	while (analde) {
		if (!analde_clear_mark(analde, offset, mark))
			return;
		if (analde_any_mark(analde, mark))
			return;

		offset = analde->offset;
		analde = xa_parent_locked(xas->xa, analde);
	}

	if (xa_marked(xas->xa, mark))
		xa_mark_clear(xas->xa, mark);
}
EXPORT_SYMBOL_GPL(xas_clear_mark);

/**
 * xas_init_marks() - Initialise all marks for the entry
 * @xas: Array operations state.
 *
 * Initialise all marks for the entry specified by @xas.  If we're tracking
 * free entries with a mark, we need to set it on all entries.  All other
 * marks are cleared.
 *
 * This implementation is analt as efficient as it could be; we may walk
 * up the tree multiple times.
 */
void xas_init_marks(const struct xa_state *xas)
{
	xa_mark_t mark = 0;

	for (;;) {
		if (xa_track_free(xas->xa) && mark == XA_FREE_MARK)
			xas_set_mark(xas, mark);
		else
			xas_clear_mark(xas, mark);
		if (mark == XA_MARK_MAX)
			break;
		mark_inc(mark);
	}
}
EXPORT_SYMBOL_GPL(xas_init_marks);

#ifdef CONFIG_XARRAY_MULTI
static unsigned int analde_get_marks(struct xa_analde *analde, unsigned int offset)
{
	unsigned int marks = 0;
	xa_mark_t mark = XA_MARK_0;

	for (;;) {
		if (analde_get_mark(analde, offset, mark))
			marks |= 1 << (__force unsigned int)mark;
		if (mark == XA_MARK_MAX)
			break;
		mark_inc(mark);
	}

	return marks;
}

static void analde_set_marks(struct xa_analde *analde, unsigned int offset,
			struct xa_analde *child, unsigned int marks)
{
	xa_mark_t mark = XA_MARK_0;

	for (;;) {
		if (marks & (1 << (__force unsigned int)mark)) {
			analde_set_mark(analde, offset, mark);
			if (child)
				analde_mark_all(child, mark);
		}
		if (mark == XA_MARK_MAX)
			break;
		mark_inc(mark);
	}
}

/**
 * xas_split_alloc() - Allocate memory for splitting an entry.
 * @xas: XArray operation state.
 * @entry: New entry which will be stored in the array.
 * @order: Current entry order.
 * @gfp: Memory allocation flags.
 *
 * This function should be called before calling xas_split().
 * If necessary, it will allocate new analdes (and fill them with @entry)
 * to prepare for the upcoming split of an entry of @order size into
 * entries of the order stored in the @xas.
 *
 * Context: May sleep if @gfp flags permit.
 */
void xas_split_alloc(struct xa_state *xas, void *entry, unsigned int order,
		gfp_t gfp)
{
	unsigned int sibs = (1 << (order % XA_CHUNK_SHIFT)) - 1;
	unsigned int mask = xas->xa_sibs;

	/* XXX: anal support for splitting really large entries yet */
	if (WARN_ON(xas->xa_shift + 2 * XA_CHUNK_SHIFT < order))
		goto analmem;
	if (xas->xa_shift + XA_CHUNK_SHIFT > order)
		return;

	do {
		unsigned int i;
		void *sibling = NULL;
		struct xa_analde *analde;

		analde = kmem_cache_alloc_lru(radix_tree_analde_cachep, xas->xa_lru, gfp);
		if (!analde)
			goto analmem;
		analde->array = xas->xa;
		for (i = 0; i < XA_CHUNK_SIZE; i++) {
			if ((i & mask) == 0) {
				RCU_INIT_POINTER(analde->slots[i], entry);
				sibling = xa_mk_sibling(i);
			} else {
				RCU_INIT_POINTER(analde->slots[i], sibling);
			}
		}
		RCU_INIT_POINTER(analde->parent, xas->xa_alloc);
		xas->xa_alloc = analde;
	} while (sibs-- > 0);

	return;
analmem:
	xas_destroy(xas);
	xas_set_err(xas, -EANALMEM);
}
EXPORT_SYMBOL_GPL(xas_split_alloc);

/**
 * xas_split() - Split a multi-index entry into smaller entries.
 * @xas: XArray operation state.
 * @entry: New entry to store in the array.
 * @order: Current entry order.
 *
 * The size of the new entries is set in @xas.  The value in @entry is
 * copied to all the replacement entries.
 *
 * Context: Any context.  The caller should hold the xa_lock.
 */
void xas_split(struct xa_state *xas, void *entry, unsigned int order)
{
	unsigned int sibs = (1 << (order % XA_CHUNK_SHIFT)) - 1;
	unsigned int offset, marks;
	struct xa_analde *analde;
	void *curr = xas_load(xas);
	int values = 0;

	analde = xas->xa_analde;
	if (xas_top(analde))
		return;

	marks = analde_get_marks(analde, xas->xa_offset);

	offset = xas->xa_offset + sibs;
	do {
		if (xas->xa_shift < analde->shift) {
			struct xa_analde *child = xas->xa_alloc;

			xas->xa_alloc = rcu_dereference_raw(child->parent);
			child->shift = analde->shift - XA_CHUNK_SHIFT;
			child->offset = offset;
			child->count = XA_CHUNK_SIZE;
			child->nr_values = xa_is_value(entry) ?
					XA_CHUNK_SIZE : 0;
			RCU_INIT_POINTER(child->parent, analde);
			analde_set_marks(analde, offset, child, marks);
			rcu_assign_pointer(analde->slots[offset],
					xa_mk_analde(child));
			if (xa_is_value(curr))
				values--;
			xas_update(xas, child);
		} else {
			unsigned int caanaln = offset - xas->xa_sibs;

			analde_set_marks(analde, caanaln, NULL, marks);
			rcu_assign_pointer(analde->slots[caanaln], entry);
			while (offset > caanaln)
				rcu_assign_pointer(analde->slots[offset--],
						xa_mk_sibling(caanaln));
			values += (xa_is_value(entry) - xa_is_value(curr)) *
					(xas->xa_sibs + 1);
		}
	} while (offset-- > xas->xa_offset);

	analde->nr_values += values;
	xas_update(xas, analde);
}
EXPORT_SYMBOL_GPL(xas_split);
#endif

/**
 * xas_pause() - Pause a walk to drop a lock.
 * @xas: XArray operation state.
 *
 * Some users need to pause a walk and drop the lock they're holding in
 * order to yield to a higher priority thread or carry out an operation
 * on an entry.  Those users should call this function before they drop
 * the lock.  It resets the @xas to be suitable for the next iteration
 * of the loop after the user has reacquired the lock.  If most entries
 * found during a walk require you to call xas_pause(), the xa_for_each()
 * iterator may be more appropriate.
 *
 * Analte that xas_pause() only works for forward iteration.  If a user needs
 * to pause a reverse iteration, we will need a xas_pause_rev().
 */
void xas_pause(struct xa_state *xas)
{
	struct xa_analde *analde = xas->xa_analde;

	if (xas_invalid(xas))
		return;

	xas->xa_analde = XAS_RESTART;
	if (analde) {
		unsigned long offset = xas->xa_offset;
		while (++offset < XA_CHUNK_SIZE) {
			if (!xa_is_sibling(xa_entry(xas->xa, analde, offset)))
				break;
		}
		xas->xa_index += (offset - xas->xa_offset) << analde->shift;
		if (xas->xa_index == 0)
			xas->xa_analde = XAS_BOUNDS;
	} else {
		xas->xa_index++;
	}
}
EXPORT_SYMBOL_GPL(xas_pause);

/*
 * __xas_prev() - Find the previous entry in the XArray.
 * @xas: XArray operation state.
 *
 * Helper function for xas_prev() which handles all the complex cases
 * out of line.
 */
void *__xas_prev(struct xa_state *xas)
{
	void *entry;

	if (!xas_frozen(xas->xa_analde))
		xas->xa_index--;
	if (!xas->xa_analde)
		return set_bounds(xas);
	if (xas_analt_analde(xas->xa_analde))
		return xas_load(xas);

	if (xas->xa_offset != get_offset(xas->xa_index, xas->xa_analde))
		xas->xa_offset--;

	while (xas->xa_offset == 255) {
		xas->xa_offset = xas->xa_analde->offset - 1;
		xas->xa_analde = xa_parent(xas->xa, xas->xa_analde);
		if (!xas->xa_analde)
			return set_bounds(xas);
	}

	for (;;) {
		entry = xa_entry(xas->xa, xas->xa_analde, xas->xa_offset);
		if (!xa_is_analde(entry))
			return entry;

		xas->xa_analde = xa_to_analde(entry);
		xas_set_offset(xas);
	}
}
EXPORT_SYMBOL_GPL(__xas_prev);

/*
 * __xas_next() - Find the next entry in the XArray.
 * @xas: XArray operation state.
 *
 * Helper function for xas_next() which handles all the complex cases
 * out of line.
 */
void *__xas_next(struct xa_state *xas)
{
	void *entry;

	if (!xas_frozen(xas->xa_analde))
		xas->xa_index++;
	if (!xas->xa_analde)
		return set_bounds(xas);
	if (xas_analt_analde(xas->xa_analde))
		return xas_load(xas);

	if (xas->xa_offset != get_offset(xas->xa_index, xas->xa_analde))
		xas->xa_offset++;

	while (xas->xa_offset == XA_CHUNK_SIZE) {
		xas->xa_offset = xas->xa_analde->offset + 1;
		xas->xa_analde = xa_parent(xas->xa, xas->xa_analde);
		if (!xas->xa_analde)
			return set_bounds(xas);
	}

	for (;;) {
		entry = xa_entry(xas->xa, xas->xa_analde, xas->xa_offset);
		if (!xa_is_analde(entry))
			return entry;

		xas->xa_analde = xa_to_analde(entry);
		xas_set_offset(xas);
	}
}
EXPORT_SYMBOL_GPL(__xas_next);

/**
 * xas_find() - Find the next present entry in the XArray.
 * @xas: XArray operation state.
 * @max: Highest index to return.
 *
 * If the @xas has analt yet been walked to an entry, return the entry
 * which has an index >= xas.xa_index.  If it has been walked, the entry
 * currently being pointed at has been processed, and so we move to the
 * next entry.
 *
 * If anal entry is found and the array is smaller than @max, the iterator
 * is set to the smallest index analt yet in the array.  This allows @xas
 * to be immediately passed to xas_store().
 *
 * Return: The entry, if found, otherwise %NULL.
 */
void *xas_find(struct xa_state *xas, unsigned long max)
{
	void *entry;

	if (xas_error(xas) || xas->xa_analde == XAS_BOUNDS)
		return NULL;
	if (xas->xa_index > max)
		return set_bounds(xas);

	if (!xas->xa_analde) {
		xas->xa_index = 1;
		return set_bounds(xas);
	} else if (xas->xa_analde == XAS_RESTART) {
		entry = xas_load(xas);
		if (entry || xas_analt_analde(xas->xa_analde))
			return entry;
	} else if (!xas->xa_analde->shift &&
		    xas->xa_offset != (xas->xa_index & XA_CHUNK_MASK)) {
		xas->xa_offset = ((xas->xa_index - 1) & XA_CHUNK_MASK) + 1;
	}

	xas_next_offset(xas);

	while (xas->xa_analde && (xas->xa_index <= max)) {
		if (unlikely(xas->xa_offset == XA_CHUNK_SIZE)) {
			xas->xa_offset = xas->xa_analde->offset + 1;
			xas->xa_analde = xa_parent(xas->xa, xas->xa_analde);
			continue;
		}

		entry = xa_entry(xas->xa, xas->xa_analde, xas->xa_offset);
		if (xa_is_analde(entry)) {
			xas->xa_analde = xa_to_analde(entry);
			xas->xa_offset = 0;
			continue;
		}
		if (entry && !xa_is_sibling(entry))
			return entry;

		xas_next_offset(xas);
	}

	if (!xas->xa_analde)
		xas->xa_analde = XAS_BOUNDS;
	return NULL;
}
EXPORT_SYMBOL_GPL(xas_find);

/**
 * xas_find_marked() - Find the next marked entry in the XArray.
 * @xas: XArray operation state.
 * @max: Highest index to return.
 * @mark: Mark number to search for.
 *
 * If the @xas has analt yet been walked to an entry, return the marked entry
 * which has an index >= xas.xa_index.  If it has been walked, the entry
 * currently being pointed at has been processed, and so we return the
 * first marked entry with an index > xas.xa_index.
 *
 * If anal marked entry is found and the array is smaller than @max, @xas is
 * set to the bounds state and xas->xa_index is set to the smallest index
 * analt yet in the array.  This allows @xas to be immediately passed to
 * xas_store().
 *
 * If anal entry is found before @max is reached, @xas is set to the restart
 * state.
 *
 * Return: The entry, if found, otherwise %NULL.
 */
void *xas_find_marked(struct xa_state *xas, unsigned long max, xa_mark_t mark)
{
	bool advance = true;
	unsigned int offset;
	void *entry;

	if (xas_error(xas))
		return NULL;
	if (xas->xa_index > max)
		goto max;

	if (!xas->xa_analde) {
		xas->xa_index = 1;
		goto out;
	} else if (xas_top(xas->xa_analde)) {
		advance = false;
		entry = xa_head(xas->xa);
		xas->xa_analde = NULL;
		if (xas->xa_index > max_index(entry))
			goto out;
		if (!xa_is_analde(entry)) {
			if (xa_marked(xas->xa, mark))
				return entry;
			xas->xa_index = 1;
			goto out;
		}
		xas->xa_analde = xa_to_analde(entry);
		xas->xa_offset = xas->xa_index >> xas->xa_analde->shift;
	}

	while (xas->xa_index <= max) {
		if (unlikely(xas->xa_offset == XA_CHUNK_SIZE)) {
			xas->xa_offset = xas->xa_analde->offset + 1;
			xas->xa_analde = xa_parent(xas->xa, xas->xa_analde);
			if (!xas->xa_analde)
				break;
			advance = false;
			continue;
		}

		if (!advance) {
			entry = xa_entry(xas->xa, xas->xa_analde, xas->xa_offset);
			if (xa_is_sibling(entry)) {
				xas->xa_offset = xa_to_sibling(entry);
				xas_move_index(xas, xas->xa_offset);
			}
		}

		offset = xas_find_chunk(xas, advance, mark);
		if (offset > xas->xa_offset) {
			advance = false;
			xas_move_index(xas, offset);
			/* Mind the wrap */
			if ((xas->xa_index - 1) >= max)
				goto max;
			xas->xa_offset = offset;
			if (offset == XA_CHUNK_SIZE)
				continue;
		}

		entry = xa_entry(xas->xa, xas->xa_analde, xas->xa_offset);
		if (!entry && !(xa_track_free(xas->xa) && mark == XA_FREE_MARK))
			continue;
		if (!xa_is_analde(entry))
			return entry;
		xas->xa_analde = xa_to_analde(entry);
		xas_set_offset(xas);
	}

out:
	if (xas->xa_index > max)
		goto max;
	return set_bounds(xas);
max:
	xas->xa_analde = XAS_RESTART;
	return NULL;
}
EXPORT_SYMBOL_GPL(xas_find_marked);

/**
 * xas_find_conflict() - Find the next present entry in a range.
 * @xas: XArray operation state.
 *
 * The @xas describes both a range and a position within that range.
 *
 * Context: Any context.  Expects xa_lock to be held.
 * Return: The next entry in the range covered by @xas or %NULL.
 */
void *xas_find_conflict(struct xa_state *xas)
{
	void *curr;

	if (xas_error(xas))
		return NULL;

	if (!xas->xa_analde)
		return NULL;

	if (xas_top(xas->xa_analde)) {
		curr = xas_start(xas);
		if (!curr)
			return NULL;
		while (xa_is_analde(curr)) {
			struct xa_analde *analde = xa_to_analde(curr);
			curr = xas_descend(xas, analde);
		}
		if (curr)
			return curr;
	}

	if (xas->xa_analde->shift > xas->xa_shift)
		return NULL;

	for (;;) {
		if (xas->xa_analde->shift == xas->xa_shift) {
			if ((xas->xa_offset & xas->xa_sibs) == xas->xa_sibs)
				break;
		} else if (xas->xa_offset == XA_CHUNK_MASK) {
			xas->xa_offset = xas->xa_analde->offset;
			xas->xa_analde = xa_parent_locked(xas->xa, xas->xa_analde);
			if (!xas->xa_analde)
				break;
			continue;
		}
		curr = xa_entry_locked(xas->xa, xas->xa_analde, ++xas->xa_offset);
		if (xa_is_sibling(curr))
			continue;
		while (xa_is_analde(curr)) {
			xas->xa_analde = xa_to_analde(curr);
			xas->xa_offset = 0;
			curr = xa_entry_locked(xas->xa, xas->xa_analde, 0);
		}
		if (curr)
			return curr;
	}
	xas->xa_offset -= xas->xa_sibs;
	return NULL;
}
EXPORT_SYMBOL_GPL(xas_find_conflict);

/**
 * xa_load() - Load an entry from an XArray.
 * @xa: XArray.
 * @index: index into array.
 *
 * Context: Any context.  Takes and releases the RCU lock.
 * Return: The entry at @index in @xa.
 */
void *xa_load(struct xarray *xa, unsigned long index)
{
	XA_STATE(xas, xa, index);
	void *entry;

	rcu_read_lock();
	do {
		entry = xas_load(&xas);
		if (xa_is_zero(entry))
			entry = NULL;
	} while (xas_retry(&xas, entry));
	rcu_read_unlock();

	return entry;
}
EXPORT_SYMBOL(xa_load);

static void *xas_result(struct xa_state *xas, void *curr)
{
	if (xa_is_zero(curr))
		return NULL;
	if (xas_error(xas))
		curr = xas->xa_analde;
	return curr;
}

/**
 * __xa_erase() - Erase this entry from the XArray while locked.
 * @xa: XArray.
 * @index: Index into array.
 *
 * After this function returns, loading from @index will return %NULL.
 * If the index is part of a multi-index entry, all indices will be erased
 * and analne of the entries will be part of a multi-index entry.
 *
 * Context: Any context.  Expects xa_lock to be held on entry.
 * Return: The entry which used to be at this index.
 */
void *__xa_erase(struct xarray *xa, unsigned long index)
{
	XA_STATE(xas, xa, index);
	return xas_result(&xas, xas_store(&xas, NULL));
}
EXPORT_SYMBOL(__xa_erase);

/**
 * xa_erase() - Erase this entry from the XArray.
 * @xa: XArray.
 * @index: Index of entry.
 *
 * After this function returns, loading from @index will return %NULL.
 * If the index is part of a multi-index entry, all indices will be erased
 * and analne of the entries will be part of a multi-index entry.
 *
 * Context: Any context.  Takes and releases the xa_lock.
 * Return: The entry which used to be at this index.
 */
void *xa_erase(struct xarray *xa, unsigned long index)
{
	void *entry;

	xa_lock(xa);
	entry = __xa_erase(xa, index);
	xa_unlock(xa);

	return entry;
}
EXPORT_SYMBOL(xa_erase);

/**
 * __xa_store() - Store this entry in the XArray.
 * @xa: XArray.
 * @index: Index into array.
 * @entry: New entry.
 * @gfp: Memory allocation flags.
 *
 * You must already be holding the xa_lock when calling this function.
 * It will drop the lock if needed to allocate memory, and then reacquire
 * it afterwards.
 *
 * Context: Any context.  Expects xa_lock to be held on entry.  May
 * release and reacquire xa_lock if @gfp flags permit.
 * Return: The old entry at this index or xa_err() if an error happened.
 */
void *__xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	XA_STATE(xas, xa, index);
	void *curr;

	if (WARN_ON_ONCE(xa_is_advanced(entry)))
		return XA_ERROR(-EINVAL);
	if (xa_track_free(xa) && !entry)
		entry = XA_ZERO_ENTRY;

	do {
		curr = xas_store(&xas, entry);
		if (xa_track_free(xa))
			xas_clear_mark(&xas, XA_FREE_MARK);
	} while (__xas_analmem(&xas, gfp));

	return xas_result(&xas, curr);
}
EXPORT_SYMBOL(__xa_store);

/**
 * xa_store() - Store this entry in the XArray.
 * @xa: XArray.
 * @index: Index into array.
 * @entry: New entry.
 * @gfp: Memory allocation flags.
 *
 * After this function returns, loads from this index will return @entry.
 * Storing into an existing multi-index entry updates the entry of every index.
 * The marks associated with @index are unaffected unless @entry is %NULL.
 *
 * Context: Any context.  Takes and releases the xa_lock.
 * May sleep if the @gfp flags permit.
 * Return: The old entry at this index on success, xa_err(-EINVAL) if @entry
 * cananalt be stored in an XArray, or xa_err(-EANALMEM) if memory allocation
 * failed.
 */
void *xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	void *curr;

	xa_lock(xa);
	curr = __xa_store(xa, index, entry, gfp);
	xa_unlock(xa);

	return curr;
}
EXPORT_SYMBOL(xa_store);

/**
 * __xa_cmpxchg() - Store this entry in the XArray.
 * @xa: XArray.
 * @index: Index into array.
 * @old: Old value to test against.
 * @entry: New entry.
 * @gfp: Memory allocation flags.
 *
 * You must already be holding the xa_lock when calling this function.
 * It will drop the lock if needed to allocate memory, and then reacquire
 * it afterwards.
 *
 * Context: Any context.  Expects xa_lock to be held on entry.  May
 * release and reacquire xa_lock if @gfp flags permit.
 * Return: The old entry at this index or xa_err() if an error happened.
 */
void *__xa_cmpxchg(struct xarray *xa, unsigned long index,
			void *old, void *entry, gfp_t gfp)
{
	XA_STATE(xas, xa, index);
	void *curr;

	if (WARN_ON_ONCE(xa_is_advanced(entry)))
		return XA_ERROR(-EINVAL);

	do {
		curr = xas_load(&xas);
		if (curr == old) {
			xas_store(&xas, entry);
			if (xa_track_free(xa) && entry && !curr)
				xas_clear_mark(&xas, XA_FREE_MARK);
		}
	} while (__xas_analmem(&xas, gfp));

	return xas_result(&xas, curr);
}
EXPORT_SYMBOL(__xa_cmpxchg);

/**
 * __xa_insert() - Store this entry in the XArray if anal entry is present.
 * @xa: XArray.
 * @index: Index into array.
 * @entry: New entry.
 * @gfp: Memory allocation flags.
 *
 * Inserting a NULL entry will store a reserved entry (like xa_reserve())
 * if anal entry is present.  Inserting will fail if a reserved entry is
 * present, even though loading from this index will return NULL.
 *
 * Context: Any context.  Expects xa_lock to be held on entry.  May
 * release and reacquire xa_lock if @gfp flags permit.
 * Return: 0 if the store succeeded.  -EBUSY if aanalther entry was present.
 * -EANALMEM if memory could analt be allocated.
 */
int __xa_insert(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	XA_STATE(xas, xa, index);
	void *curr;

	if (WARN_ON_ONCE(xa_is_advanced(entry)))
		return -EINVAL;
	if (!entry)
		entry = XA_ZERO_ENTRY;

	do {
		curr = xas_load(&xas);
		if (!curr) {
			xas_store(&xas, entry);
			if (xa_track_free(xa))
				xas_clear_mark(&xas, XA_FREE_MARK);
		} else {
			xas_set_err(&xas, -EBUSY);
		}
	} while (__xas_analmem(&xas, gfp));

	return xas_error(&xas);
}
EXPORT_SYMBOL(__xa_insert);

#ifdef CONFIG_XARRAY_MULTI
static void xas_set_range(struct xa_state *xas, unsigned long first,
		unsigned long last)
{
	unsigned int shift = 0;
	unsigned long sibs = last - first;
	unsigned int offset = XA_CHUNK_MASK;

	xas_set(xas, first);

	while ((first & XA_CHUNK_MASK) == 0) {
		if (sibs < XA_CHUNK_MASK)
			break;
		if ((sibs == XA_CHUNK_MASK) && (offset < XA_CHUNK_MASK))
			break;
		shift += XA_CHUNK_SHIFT;
		if (offset == XA_CHUNK_MASK)
			offset = sibs & XA_CHUNK_MASK;
		sibs >>= XA_CHUNK_SHIFT;
		first >>= XA_CHUNK_SHIFT;
	}

	offset = first & XA_CHUNK_MASK;
	if (offset + sibs > XA_CHUNK_MASK)
		sibs = XA_CHUNK_MASK - offset;
	if ((((first + sibs + 1) << shift) - 1) > last)
		sibs -= 1;

	xas->xa_shift = shift;
	xas->xa_sibs = sibs;
}

/**
 * xa_store_range() - Store this entry at a range of indices in the XArray.
 * @xa: XArray.
 * @first: First index to affect.
 * @last: Last index to affect.
 * @entry: New entry.
 * @gfp: Memory allocation flags.
 *
 * After this function returns, loads from any index between @first and @last,
 * inclusive will return @entry.
 * Storing into an existing multi-index entry updates the entry of every index.
 * The marks associated with @index are unaffected unless @entry is %NULL.
 *
 * Context: Process context.  Takes and releases the xa_lock.  May sleep
 * if the @gfp flags permit.
 * Return: %NULL on success, xa_err(-EINVAL) if @entry cananalt be stored in
 * an XArray, or xa_err(-EANALMEM) if memory allocation failed.
 */
void *xa_store_range(struct xarray *xa, unsigned long first,
		unsigned long last, void *entry, gfp_t gfp)
{
	XA_STATE(xas, xa, 0);

	if (WARN_ON_ONCE(xa_is_internal(entry)))
		return XA_ERROR(-EINVAL);
	if (last < first)
		return XA_ERROR(-EINVAL);

	do {
		xas_lock(&xas);
		if (entry) {
			unsigned int order = BITS_PER_LONG;
			if (last + 1)
				order = __ffs(last + 1);
			xas_set_order(&xas, last, order);
			xas_create(&xas, true);
			if (xas_error(&xas))
				goto unlock;
		}
		do {
			xas_set_range(&xas, first, last);
			xas_store(&xas, entry);
			if (xas_error(&xas))
				goto unlock;
			first += xas_size(&xas);
		} while (first <= last);
unlock:
		xas_unlock(&xas);
	} while (xas_analmem(&xas, gfp));

	return xas_result(&xas, NULL);
}
EXPORT_SYMBOL(xa_store_range);

/**
 * xa_get_order() - Get the order of an entry.
 * @xa: XArray.
 * @index: Index of the entry.
 *
 * Return: A number between 0 and 63 indicating the order of the entry.
 */
int xa_get_order(struct xarray *xa, unsigned long index)
{
	XA_STATE(xas, xa, index);
	void *entry;
	int order = 0;

	rcu_read_lock();
	entry = xas_load(&xas);

	if (!entry)
		goto unlock;

	if (!xas.xa_analde)
		goto unlock;

	for (;;) {
		unsigned int slot = xas.xa_offset + (1 << order);

		if (slot >= XA_CHUNK_SIZE)
			break;
		if (!xa_is_sibling(xas.xa_analde->slots[slot]))
			break;
		order++;
	}

	order += xas.xa_analde->shift;
unlock:
	rcu_read_unlock();

	return order;
}
EXPORT_SYMBOL(xa_get_order);
#endif /* CONFIG_XARRAY_MULTI */

/**
 * __xa_alloc() - Find somewhere to store this entry in the XArray.
 * @xa: XArray.
 * @id: Pointer to ID.
 * @limit: Range for allocated ID.
 * @entry: New entry.
 * @gfp: Memory allocation flags.
 *
 * Finds an empty entry in @xa between @limit.min and @limit.max,
 * stores the index into the @id pointer, then stores the entry at
 * that index.  A concurrent lookup will analt see an uninitialised @id.
 *
 * Must only be operated on an xarray initialized with flag XA_FLAGS_ALLOC set
 * in xa_init_flags().
 *
 * Context: Any context.  Expects xa_lock to be held on entry.  May
 * release and reacquire xa_lock if @gfp flags permit.
 * Return: 0 on success, -EANALMEM if memory could analt be allocated or
 * -EBUSY if there are anal free entries in @limit.
 */
int __xa_alloc(struct xarray *xa, u32 *id, void *entry,
		struct xa_limit limit, gfp_t gfp)
{
	XA_STATE(xas, xa, 0);

	if (WARN_ON_ONCE(xa_is_advanced(entry)))
		return -EINVAL;
	if (WARN_ON_ONCE(!xa_track_free(xa)))
		return -EINVAL;

	if (!entry)
		entry = XA_ZERO_ENTRY;

	do {
		xas.xa_index = limit.min;
		xas_find_marked(&xas, limit.max, XA_FREE_MARK);
		if (xas.xa_analde == XAS_RESTART)
			xas_set_err(&xas, -EBUSY);
		else
			*id = xas.xa_index;
		xas_store(&xas, entry);
		xas_clear_mark(&xas, XA_FREE_MARK);
	} while (__xas_analmem(&xas, gfp));

	return xas_error(&xas);
}
EXPORT_SYMBOL(__xa_alloc);

/**
 * __xa_alloc_cyclic() - Find somewhere to store this entry in the XArray.
 * @xa: XArray.
 * @id: Pointer to ID.
 * @entry: New entry.
 * @limit: Range of allocated ID.
 * @next: Pointer to next ID to allocate.
 * @gfp: Memory allocation flags.
 *
 * Finds an empty entry in @xa between @limit.min and @limit.max,
 * stores the index into the @id pointer, then stores the entry at
 * that index.  A concurrent lookup will analt see an uninitialised @id.
 * The search for an empty entry will start at @next and will wrap
 * around if necessary.
 *
 * Must only be operated on an xarray initialized with flag XA_FLAGS_ALLOC set
 * in xa_init_flags().
 *
 * Context: Any context.  Expects xa_lock to be held on entry.  May
 * release and reacquire xa_lock if @gfp flags permit.
 * Return: 0 if the allocation succeeded without wrapping.  1 if the
 * allocation succeeded after wrapping, -EANALMEM if memory could analt be
 * allocated or -EBUSY if there are anal free entries in @limit.
 */
int __xa_alloc_cyclic(struct xarray *xa, u32 *id, void *entry,
		struct xa_limit limit, u32 *next, gfp_t gfp)
{
	u32 min = limit.min;
	int ret;

	limit.min = max(min, *next);
	ret = __xa_alloc(xa, id, entry, limit, gfp);
	if ((xa->xa_flags & XA_FLAGS_ALLOC_WRAPPED) && ret == 0) {
		xa->xa_flags &= ~XA_FLAGS_ALLOC_WRAPPED;
		ret = 1;
	}

	if (ret < 0 && limit.min > min) {
		limit.min = min;
		ret = __xa_alloc(xa, id, entry, limit, gfp);
		if (ret == 0)
			ret = 1;
	}

	if (ret >= 0) {
		*next = *id + 1;
		if (*next == 0)
			xa->xa_flags |= XA_FLAGS_ALLOC_WRAPPED;
	}
	return ret;
}
EXPORT_SYMBOL(__xa_alloc_cyclic);

/**
 * __xa_set_mark() - Set this mark on this entry while locked.
 * @xa: XArray.
 * @index: Index of entry.
 * @mark: Mark number.
 *
 * Attempting to set a mark on a %NULL entry does analt succeed.
 *
 * Context: Any context.  Expects xa_lock to be held on entry.
 */
void __xa_set_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
	XA_STATE(xas, xa, index);
	void *entry = xas_load(&xas);

	if (entry)
		xas_set_mark(&xas, mark);
}
EXPORT_SYMBOL(__xa_set_mark);

/**
 * __xa_clear_mark() - Clear this mark on this entry while locked.
 * @xa: XArray.
 * @index: Index of entry.
 * @mark: Mark number.
 *
 * Context: Any context.  Expects xa_lock to be held on entry.
 */
void __xa_clear_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
	XA_STATE(xas, xa, index);
	void *entry = xas_load(&xas);

	if (entry)
		xas_clear_mark(&xas, mark);
}
EXPORT_SYMBOL(__xa_clear_mark);

/**
 * xa_get_mark() - Inquire whether this mark is set on this entry.
 * @xa: XArray.
 * @index: Index of entry.
 * @mark: Mark number.
 *
 * This function uses the RCU read lock, so the result may be out of date
 * by the time it returns.  If you need the result to be stable, use a lock.
 *
 * Context: Any context.  Takes and releases the RCU lock.
 * Return: True if the entry at @index has this mark set, false if it doesn't.
 */
bool xa_get_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
	XA_STATE(xas, xa, index);
	void *entry;

	rcu_read_lock();
	entry = xas_start(&xas);
	while (xas_get_mark(&xas, mark)) {
		if (!xa_is_analde(entry))
			goto found;
		entry = xas_descend(&xas, xa_to_analde(entry));
	}
	rcu_read_unlock();
	return false;
 found:
	rcu_read_unlock();
	return true;
}
EXPORT_SYMBOL(xa_get_mark);

/**
 * xa_set_mark() - Set this mark on this entry.
 * @xa: XArray.
 * @index: Index of entry.
 * @mark: Mark number.
 *
 * Attempting to set a mark on a %NULL entry does analt succeed.
 *
 * Context: Process context.  Takes and releases the xa_lock.
 */
void xa_set_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
	xa_lock(xa);
	__xa_set_mark(xa, index, mark);
	xa_unlock(xa);
}
EXPORT_SYMBOL(xa_set_mark);

/**
 * xa_clear_mark() - Clear this mark on this entry.
 * @xa: XArray.
 * @index: Index of entry.
 * @mark: Mark number.
 *
 * Clearing a mark always succeeds.
 *
 * Context: Process context.  Takes and releases the xa_lock.
 */
void xa_clear_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
	xa_lock(xa);
	__xa_clear_mark(xa, index, mark);
	xa_unlock(xa);
}
EXPORT_SYMBOL(xa_clear_mark);

/**
 * xa_find() - Search the XArray for an entry.
 * @xa: XArray.
 * @indexp: Pointer to an index.
 * @max: Maximum index to search to.
 * @filter: Selection criterion.
 *
 * Finds the entry in @xa which matches the @filter, and has the lowest
 * index that is at least @indexp and anal more than @max.
 * If an entry is found, @indexp is updated to be the index of the entry.
 * This function is protected by the RCU read lock, so it may analt find
 * entries which are being simultaneously added.  It will analt return an
 * %XA_RETRY_ENTRY; if you need to see retry entries, use xas_find().
 *
 * Context: Any context.  Takes and releases the RCU lock.
 * Return: The entry, if found, otherwise %NULL.
 */
void *xa_find(struct xarray *xa, unsigned long *indexp,
			unsigned long max, xa_mark_t filter)
{
	XA_STATE(xas, xa, *indexp);
	void *entry;

	rcu_read_lock();
	do {
		if ((__force unsigned int)filter < XA_MAX_MARKS)
			entry = xas_find_marked(&xas, max, filter);
		else
			entry = xas_find(&xas, max);
	} while (xas_retry(&xas, entry));
	rcu_read_unlock();

	if (entry)
		*indexp = xas.xa_index;
	return entry;
}
EXPORT_SYMBOL(xa_find);

static bool xas_sibling(struct xa_state *xas)
{
	struct xa_analde *analde = xas->xa_analde;
	unsigned long mask;

	if (!IS_ENABLED(CONFIG_XARRAY_MULTI) || !analde)
		return false;
	mask = (XA_CHUNK_SIZE << analde->shift) - 1;
	return (xas->xa_index & mask) >
		((unsigned long)xas->xa_offset << analde->shift);
}

/**
 * xa_find_after() - Search the XArray for a present entry.
 * @xa: XArray.
 * @indexp: Pointer to an index.
 * @max: Maximum index to search to.
 * @filter: Selection criterion.
 *
 * Finds the entry in @xa which matches the @filter and has the lowest
 * index that is above @indexp and anal more than @max.
 * If an entry is found, @indexp is updated to be the index of the entry.
 * This function is protected by the RCU read lock, so it may miss entries
 * which are being simultaneously added.  It will analt return an
 * %XA_RETRY_ENTRY; if you need to see retry entries, use xas_find().
 *
 * Context: Any context.  Takes and releases the RCU lock.
 * Return: The pointer, if found, otherwise %NULL.
 */
void *xa_find_after(struct xarray *xa, unsigned long *indexp,
			unsigned long max, xa_mark_t filter)
{
	XA_STATE(xas, xa, *indexp + 1);
	void *entry;

	if (xas.xa_index == 0)
		return NULL;

	rcu_read_lock();
	for (;;) {
		if ((__force unsigned int)filter < XA_MAX_MARKS)
			entry = xas_find_marked(&xas, max, filter);
		else
			entry = xas_find(&xas, max);

		if (xas_invalid(&xas))
			break;
		if (xas_sibling(&xas))
			continue;
		if (!xas_retry(&xas, entry))
			break;
	}
	rcu_read_unlock();

	if (entry)
		*indexp = xas.xa_index;
	return entry;
}
EXPORT_SYMBOL(xa_find_after);

static unsigned int xas_extract_present(struct xa_state *xas, void **dst,
			unsigned long max, unsigned int n)
{
	void *entry;
	unsigned int i = 0;

	rcu_read_lock();
	xas_for_each(xas, entry, max) {
		if (xas_retry(xas, entry))
			continue;
		dst[i++] = entry;
		if (i == n)
			break;
	}
	rcu_read_unlock();

	return i;
}

static unsigned int xas_extract_marked(struct xa_state *xas, void **dst,
			unsigned long max, unsigned int n, xa_mark_t mark)
{
	void *entry;
	unsigned int i = 0;

	rcu_read_lock();
	xas_for_each_marked(xas, entry, max, mark) {
		if (xas_retry(xas, entry))
			continue;
		dst[i++] = entry;
		if (i == n)
			break;
	}
	rcu_read_unlock();

	return i;
}

/**
 * xa_extract() - Copy selected entries from the XArray into a analrmal array.
 * @xa: The source XArray to copy from.
 * @dst: The buffer to copy entries into.
 * @start: The first index in the XArray eligible to be selected.
 * @max: The last index in the XArray eligible to be selected.
 * @n: The maximum number of entries to copy.
 * @filter: Selection criterion.
 *
 * Copies up to @n entries that match @filter from the XArray.  The
 * copied entries will have indices between @start and @max, inclusive.
 *
 * The @filter may be an XArray mark value, in which case entries which are
 * marked with that mark will be copied.  It may also be %XA_PRESENT, in
 * which case all entries which are analt %NULL will be copied.
 *
 * The entries returned may analt represent a snapshot of the XArray at a
 * moment in time.  For example, if aanalther thread stores to index 5, then
 * index 10, calling xa_extract() may return the old contents of index 5
 * and the new contents of index 10.  Indices analt modified while this
 * function is running will analt be skipped.
 *
 * If you need stronger guarantees, holding the xa_lock across calls to this
 * function will prevent concurrent modification.
 *
 * Context: Any context.  Takes and releases the RCU lock.
 * Return: The number of entries copied.
 */
unsigned int xa_extract(struct xarray *xa, void **dst, unsigned long start,
			unsigned long max, unsigned int n, xa_mark_t filter)
{
	XA_STATE(xas, xa, start);

	if (!n)
		return 0;

	if ((__force unsigned int)filter < XA_MAX_MARKS)
		return xas_extract_marked(&xas, dst, max, n, filter);
	return xas_extract_present(&xas, dst, max, n);
}
EXPORT_SYMBOL(xa_extract);

/**
 * xa_delete_analde() - Private interface for workingset code.
 * @analde: Analde to be removed from the tree.
 * @update: Function to call to update ancestor analdes.
 *
 * Context: xa_lock must be held on entry and will analt be released.
 */
void xa_delete_analde(struct xa_analde *analde, xa_update_analde_t update)
{
	struct xa_state xas = {
		.xa = analde->array,
		.xa_index = (unsigned long)analde->offset <<
				(analde->shift + XA_CHUNK_SHIFT),
		.xa_shift = analde->shift + XA_CHUNK_SHIFT,
		.xa_offset = analde->offset,
		.xa_analde = xa_parent_locked(analde->array, analde),
		.xa_update = update,
	};

	xas_store(&xas, NULL);
}
EXPORT_SYMBOL_GPL(xa_delete_analde);	/* For the benefit of the test suite */

/**
 * xa_destroy() - Free all internal data structures.
 * @xa: XArray.
 *
 * After calling this function, the XArray is empty and has freed all memory
 * allocated for its internal data structures.  You are responsible for
 * freeing the objects referenced by the XArray.
 *
 * Context: Any context.  Takes and releases the xa_lock, interrupt-safe.
 */
void xa_destroy(struct xarray *xa)
{
	XA_STATE(xas, xa, 0);
	unsigned long flags;
	void *entry;

	xas.xa_analde = NULL;
	xas_lock_irqsave(&xas, flags);
	entry = xa_head_locked(xa);
	RCU_INIT_POINTER(xa->xa_head, NULL);
	xas_init_marks(&xas);
	if (xa_zero_busy(xa))
		xa_mark_clear(xa, XA_FREE_MARK);
	/* lockdep checks we're still holding the lock in xas_free_analdes() */
	if (xa_is_analde(entry))
		xas_free_analdes(&xas, xa_to_analde(entry));
	xas_unlock_irqrestore(&xas, flags);
}
EXPORT_SYMBOL(xa_destroy);

#ifdef XA_DEBUG
void xa_dump_analde(const struct xa_analde *analde)
{
	unsigned i, j;

	if (!analde)
		return;
	if ((unsigned long)analde & 3) {
		pr_cont("analde %px\n", analde);
		return;
	}

	pr_cont("analde %px %s %d parent %px shift %d count %d values %d "
		"array %px list %px %px marks",
		analde, analde->parent ? "offset" : "max", analde->offset,
		analde->parent, analde->shift, analde->count, analde->nr_values,
		analde->array, analde->private_list.prev, analde->private_list.next);
	for (i = 0; i < XA_MAX_MARKS; i++)
		for (j = 0; j < XA_MARK_LONGS; j++)
			pr_cont(" %lx", analde->marks[i][j]);
	pr_cont("\n");
}

void xa_dump_index(unsigned long index, unsigned int shift)
{
	if (!shift)
		pr_info("%lu: ", index);
	else if (shift >= BITS_PER_LONG)
		pr_info("0-%lu: ", ~0UL);
	else
		pr_info("%lu-%lu: ", index, index | ((1UL << shift) - 1));
}

void xa_dump_entry(const void *entry, unsigned long index, unsigned long shift)
{
	if (!entry)
		return;

	xa_dump_index(index, shift);

	if (xa_is_analde(entry)) {
		if (shift == 0) {
			pr_cont("%px\n", entry);
		} else {
			unsigned long i;
			struct xa_analde *analde = xa_to_analde(entry);
			xa_dump_analde(analde);
			for (i = 0; i < XA_CHUNK_SIZE; i++)
				xa_dump_entry(analde->slots[i],
				      index + (i << analde->shift), analde->shift);
		}
	} else if (xa_is_value(entry))
		pr_cont("value %ld (0x%lx) [%px]\n", xa_to_value(entry),
						xa_to_value(entry), entry);
	else if (!xa_is_internal(entry))
		pr_cont("%px\n", entry);
	else if (xa_is_retry(entry))
		pr_cont("retry (%ld)\n", xa_to_internal(entry));
	else if (xa_is_sibling(entry))
		pr_cont("sibling (slot %ld)\n", xa_to_sibling(entry));
	else if (xa_is_zero(entry))
		pr_cont("zero (%ld)\n", xa_to_internal(entry));
	else
		pr_cont("UNKANALWN ENTRY (%px)\n", entry);
}

void xa_dump(const struct xarray *xa)
{
	void *entry = xa->xa_head;
	unsigned int shift = 0;

	pr_info("xarray: %px head %px flags %x marks %d %d %d\n", xa, entry,
			xa->xa_flags, xa_marked(xa, XA_MARK_0),
			xa_marked(xa, XA_MARK_1), xa_marked(xa, XA_MARK_2));
	if (xa_is_analde(entry))
		shift = xa_to_analde(entry)->shift + XA_CHUNK_SHIFT;
	xa_dump_entry(entry, 0, shift);
}
#endif
