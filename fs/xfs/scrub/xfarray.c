// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/scrub.h"
#include "scrub/trace.h"

/*
 * Large Arrays of Fixed-Size Records
 * ==================================
 *
 * This memory array uses an xfile (which itself is a shmem file) to store
 * large numbers of fixed-size records in memory that can be paged out.  This
 * puts less stress on the memory reclaim algorithms during an online repair
 * because we don't have to pin so much memory.  However, array access is less
 * direct than would be in a regular memory array.  Access to the array is
 * performed via indexed load and store methods, and an append method is
 * provided for convenience.  Array elements can be unset, which sets them to
 * all zeroes.  Unset entries are skipped during iteration, though direct loads
 * will return a zeroed buffer.  Callers are responsible for concurrency
 * control.
 */

/*
 * Pointer to scratch space.  Because we can't access the xfile data directly,
 * we allocate a small amount of memory on the end of the xfarray structure to
 * buffer array items when we need space to store values temporarily.
 */
static inline void *xfarray_scratch(struct xfarray *array)
{
	return (array + 1);
}

/* Compute array index given an xfile offset. */
static xfarray_idx_t
xfarray_idx(
	struct xfarray	*array,
	loff_t		pos)
{
	if (array->obj_size_log >= 0)
		return (xfarray_idx_t)pos >> array->obj_size_log;

	return div_u64((xfarray_idx_t)pos, array->obj_size);
}

/* Compute xfile offset of array element. */
static inline loff_t xfarray_pos(struct xfarray *array, xfarray_idx_t idx)
{
	if (array->obj_size_log >= 0)
		return idx << array->obj_size_log;

	return idx * array->obj_size;
}

/*
 * Initialize a big memory array.  Array records cannot be larger than a
 * page, and the array cannot span more bytes than the page cache supports.
 * If @required_capacity is nonzero, the maximum array size will be set to this
 * quantity and the array creation will fail if the underlying storage cannot
 * support that many records.
 */
int
xfarray_create(
	const char		*description,
	unsigned long long	required_capacity,
	size_t			obj_size,
	struct xfarray		**arrayp)
{
	struct xfarray		*array;
	struct xfile		*xfile;
	int			error;

	ASSERT(obj_size < PAGE_SIZE);

	error = xfile_create(description, 0, &xfile);
	if (error)
		return error;

	error = -ENOMEM;
	array = kzalloc(sizeof(struct xfarray) + obj_size, XCHK_GFP_FLAGS);
	if (!array)
		goto out_xfile;

	array->xfile = xfile;
	array->obj_size = obj_size;

	if (is_power_of_2(obj_size))
		array->obj_size_log = ilog2(obj_size);
	else
		array->obj_size_log = -1;

	array->max_nr = xfarray_idx(array, MAX_LFS_FILESIZE);
	trace_xfarray_create(array, required_capacity);

	if (required_capacity > 0) {
		if (array->max_nr < required_capacity) {
			error = -ENOMEM;
			goto out_xfarray;
		}
		array->max_nr = required_capacity;
	}

	*arrayp = array;
	return 0;

out_xfarray:
	kfree(array);
out_xfile:
	xfile_destroy(xfile);
	return error;
}

/* Destroy the array. */
void
xfarray_destroy(
	struct xfarray	*array)
{
	xfile_destroy(array->xfile);
	kfree(array);
}

/* Load an element from the array. */
int
xfarray_load(
	struct xfarray	*array,
	xfarray_idx_t	idx,
	void		*ptr)
{
	if (idx >= array->nr)
		return -ENODATA;

	return xfile_load(array->xfile, ptr, array->obj_size,
			xfarray_pos(array, idx));
}

/* Is this array element potentially unset? */
static inline bool
xfarray_is_unset(
	struct xfarray	*array,
	loff_t		pos)
{
	void		*temp = xfarray_scratch(array);
	int		error;

	if (array->unset_slots == 0)
		return false;

	error = xfile_load(array->xfile, temp, array->obj_size, pos);
	if (!error && xfarray_element_is_null(array, temp))
		return true;

	return false;
}

/*
 * Unset an array element.  If @idx is the last element in the array, the
 * array will be truncated.  Otherwise, the entry will be zeroed.
 */
int
xfarray_unset(
	struct xfarray	*array,
	xfarray_idx_t	idx)
{
	void		*temp = xfarray_scratch(array);
	loff_t		pos = xfarray_pos(array, idx);
	int		error;

	if (idx >= array->nr)
		return -ENODATA;

	if (idx == array->nr - 1) {
		array->nr--;
		return 0;
	}

	if (xfarray_is_unset(array, pos))
		return 0;

	memset(temp, 0, array->obj_size);
	error = xfile_store(array->xfile, temp, array->obj_size, pos);
	if (error)
		return error;

	array->unset_slots++;
	return 0;
}

/*
 * Store an element in the array.  The element must not be completely zeroed,
 * because those are considered unset sparse elements.
 */
int
xfarray_store(
	struct xfarray	*array,
	xfarray_idx_t	idx,
	const void	*ptr)
{
	int		ret;

	if (idx >= array->max_nr)
		return -EFBIG;

	ASSERT(!xfarray_element_is_null(array, ptr));

	ret = xfile_store(array->xfile, ptr, array->obj_size,
			xfarray_pos(array, idx));
	if (ret)
		return ret;

	array->nr = max(array->nr, idx + 1);
	return 0;
}

/* Is this array element NULL? */
bool
xfarray_element_is_null(
	struct xfarray	*array,
	const void	*ptr)
{
	return !memchr_inv(ptr, 0, array->obj_size);
}

/*
 * Store an element anywhere in the array that is unset.  If there are no
 * unset slots, append the element to the array.
 */
int
xfarray_store_anywhere(
	struct xfarray	*array,
	const void	*ptr)
{
	void		*temp = xfarray_scratch(array);
	loff_t		endpos = xfarray_pos(array, array->nr);
	loff_t		pos;
	int		error;

	/* Find an unset slot to put it in. */
	for (pos = 0;
	     pos < endpos && array->unset_slots > 0;
	     pos += array->obj_size) {
		error = xfile_load(array->xfile, temp, array->obj_size,
				pos);
		if (error || !xfarray_element_is_null(array, temp))
			continue;

		error = xfile_store(array->xfile, ptr, array->obj_size,
				pos);
		if (error)
			return error;

		array->unset_slots--;
		return 0;
	}

	/* No unset slots found; attach it on the end. */
	array->unset_slots = 0;
	return xfarray_append(array, ptr);
}

/* Return length of array. */
uint64_t
xfarray_length(
	struct xfarray	*array)
{
	return array->nr;
}

/*
 * Decide which array item we're going to read as part of an _iter_get.
 * @cur is the array index, and @pos is the file offset of that array index in
 * the backing xfile.  Returns ENODATA if we reach the end of the records.
 *
 * Reading from a hole in a sparse xfile causes page instantiation, so for
 * iterating a (possibly sparse) array we need to figure out if the cursor is
 * pointing at a totally uninitialized hole and move the cursor up if
 * necessary.
 */
static inline int
xfarray_find_data(
	struct xfarray	*array,
	xfarray_idx_t	*cur,
	loff_t		*pos)
{
	unsigned int	pgoff = offset_in_page(*pos);
	loff_t		end_pos = *pos + array->obj_size - 1;
	loff_t		new_pos;

	/*
	 * If the current array record is not adjacent to a page boundary, we
	 * are in the middle of the page.  We do not need to move the cursor.
	 */
	if (pgoff != 0 && pgoff + array->obj_size - 1 < PAGE_SIZE)
		return 0;

	/*
	 * Call SEEK_DATA on the last byte in the record we're about to read.
	 * If the record ends at (or crosses) the end of a page then we know
	 * that the first byte of the record is backed by pages and don't need
	 * to query it.  If instead the record begins at the start of the page
	 * then we know that querying the last byte is just as good as querying
	 * the first byte, since records cannot be larger than a page.
	 *
	 * If the call returns the same file offset, we know this record is
	 * backed by real pages.  We do not need to move the cursor.
	 */
	new_pos = xfile_seek_data(array->xfile, end_pos);
	if (new_pos == -ENXIO)
		return -ENODATA;
	if (new_pos < 0)
		return new_pos;
	if (new_pos == end_pos)
		return 0;

	/*
	 * Otherwise, SEEK_DATA told us how far up to move the file pointer to
	 * find more data.  Move the array index to the first record past the
	 * byte offset we were given.
	 */
	new_pos = roundup_64(new_pos, array->obj_size);
	*cur = xfarray_idx(array, new_pos);
	*pos = xfarray_pos(array, *cur);
	return 0;
}

/*
 * Starting at *idx, fetch the next non-null array entry and advance the index
 * to set up the next _load_next call.  Returns ENODATA if we reach the end of
 * the array.  Callers must set @*idx to XFARRAY_CURSOR_INIT before the first
 * call to this function.
 */
int
xfarray_load_next(
	struct xfarray	*array,
	xfarray_idx_t	*idx,
	void		*rec)
{
	xfarray_idx_t	cur = *idx;
	loff_t		pos = xfarray_pos(array, cur);
	int		error;

	do {
		if (cur >= array->nr)
			return -ENODATA;

		/*
		 * Ask the backing store for the location of next possible
		 * written record, then retrieve that record.
		 */
		error = xfarray_find_data(array, &cur, &pos);
		if (error)
			return error;
		error = xfarray_load(array, cur, rec);
		if (error)
			return error;

		cur++;
		pos += array->obj_size;
	} while (xfarray_element_is_null(array, rec));

	*idx = cur;
	return 0;
}

/* Sorting functions */

#ifdef DEBUG
# define xfarray_sort_bump_loads(si)	do { (si)->loads++; } while (0)
# define xfarray_sort_bump_stores(si)	do { (si)->stores++; } while (0)
# define xfarray_sort_bump_compares(si)	do { (si)->compares++; } while (0)
# define xfarray_sort_bump_heapsorts(si) do { (si)->heapsorts++; } while (0)
#else
# define xfarray_sort_bump_loads(si)
# define xfarray_sort_bump_stores(si)
# define xfarray_sort_bump_compares(si)
# define xfarray_sort_bump_heapsorts(si)
#endif /* DEBUG */

/* Load an array element for sorting. */
static inline int
xfarray_sort_load(
	struct xfarray_sortinfo	*si,
	xfarray_idx_t		idx,
	void			*ptr)
{
	xfarray_sort_bump_loads(si);
	return xfarray_load(si->array, idx, ptr);
}

/* Store an array element for sorting. */
static inline int
xfarray_sort_store(
	struct xfarray_sortinfo	*si,
	xfarray_idx_t		idx,
	void			*ptr)
{
	xfarray_sort_bump_stores(si);
	return xfarray_store(si->array, idx, ptr);
}

/* Compare an array element for sorting. */
static inline int
xfarray_sort_cmp(
	struct xfarray_sortinfo	*si,
	const void		*a,
	const void		*b)
{
	xfarray_sort_bump_compares(si);
	return si->cmp_fn(a, b);
}

/* Return a pointer to the low index stack for quicksort partitioning. */
static inline xfarray_idx_t *xfarray_sortinfo_lo(struct xfarray_sortinfo *si)
{
	return (xfarray_idx_t *)(si + 1);
}

/* Return a pointer to the high index stack for quicksort partitioning. */
static inline xfarray_idx_t *xfarray_sortinfo_hi(struct xfarray_sortinfo *si)
{
	return xfarray_sortinfo_lo(si) + si->max_stack_depth;
}

/* Size of each element in the quicksort pivot array. */
static inline size_t
xfarray_pivot_rec_sz(
	struct xfarray		*array)
{
	return round_up(array->obj_size, 8) + sizeof(xfarray_idx_t);
}

/* Allocate memory to handle the sort. */
static inline int
xfarray_sortinfo_alloc(
	struct xfarray		*array,
	xfarray_cmp_fn		cmp_fn,
	unsigned int		flags,
	struct xfarray_sortinfo	**infop)
{
	struct xfarray_sortinfo	*si;
	size_t			nr_bytes = sizeof(struct xfarray_sortinfo);
	size_t			pivot_rec_sz = xfarray_pivot_rec_sz(array);
	int			max_stack_depth;

	/*
	 * The median-of-nine pivot algorithm doesn't work if a subset has
	 * fewer than 9 items.  Make sure the in-memory sort will always take
	 * over for subsets where this wouldn't be the case.
	 */
	BUILD_BUG_ON(XFARRAY_QSORT_PIVOT_NR >= XFARRAY_ISORT_NR);

	/*
	 * Tail-call recursion during the partitioning phase means that
	 * quicksort will never recurse more than log2(nr) times.  We need one
	 * extra level of stack to hold the initial parameters.  In-memory
	 * sort will always take care of the last few levels of recursion for
	 * us, so we can reduce the stack depth by that much.
	 */
	max_stack_depth = ilog2(array->nr) + 1 - (XFARRAY_ISORT_SHIFT - 1);
	if (max_stack_depth < 1)
		max_stack_depth = 1;

	/* Each level of quicksort uses a lo and a hi index */
	nr_bytes += max_stack_depth * sizeof(xfarray_idx_t) * 2;

	/* Scratchpad for in-memory sort, or finding the pivot */
	nr_bytes += max_t(size_t,
			(XFARRAY_QSORT_PIVOT_NR + 1) * pivot_rec_sz,
			XFARRAY_ISORT_NR * array->obj_size);

	si = kvzalloc(nr_bytes, XCHK_GFP_FLAGS);
	if (!si)
		return -ENOMEM;

	si->array = array;
	si->cmp_fn = cmp_fn;
	si->flags = flags;
	si->max_stack_depth = max_stack_depth;
	si->max_stack_used = 1;

	xfarray_sortinfo_lo(si)[0] = 0;
	xfarray_sortinfo_hi(si)[0] = array->nr - 1;

	trace_xfarray_sort(si, nr_bytes);
	*infop = si;
	return 0;
}

/* Should this sort be terminated by a fatal signal? */
static inline bool
xfarray_sort_terminated(
	struct xfarray_sortinfo	*si,
	int			*error)
{
	/*
	 * If preemption is disabled, we need to yield to the scheduler every
	 * few seconds so that we don't run afoul of the soft lockup watchdog
	 * or RCU stall detector.
	 */
	cond_resched();

	if ((si->flags & XFARRAY_SORT_KILLABLE) &&
	    fatal_signal_pending(current)) {
		if (*error == 0)
			*error = -EINTR;
		return true;
	}
	return false;
}

/* Do we want an in-memory sort? */
static inline bool
xfarray_want_isort(
	struct xfarray_sortinfo *si,
	xfarray_idx_t		start,
	xfarray_idx_t		end)
{
	/*
	 * For array subsets that fit in the scratchpad, it's much faster to
	 * use the kernel's heapsort than quicksort's stack machine.
	 */
	return (end - start) < XFARRAY_ISORT_NR;
}

/* Return the scratch space within the sortinfo structure. */
static inline void *xfarray_sortinfo_isort_scratch(struct xfarray_sortinfo *si)
{
	return xfarray_sortinfo_hi(si) + si->max_stack_depth;
}

/*
 * Sort a small number of array records using scratchpad memory.  The records
 * need not be contiguous in the xfile's memory pages.
 */
STATIC int
xfarray_isort(
	struct xfarray_sortinfo	*si,
	xfarray_idx_t		lo,
	xfarray_idx_t		hi)
{
	void			*scratch = xfarray_sortinfo_isort_scratch(si);
	loff_t			lo_pos = xfarray_pos(si->array, lo);
	loff_t			len = xfarray_pos(si->array, hi - lo + 1);
	int			error;

	trace_xfarray_isort(si, lo, hi);

	xfarray_sort_bump_loads(si);
	error = xfile_load(si->array->xfile, scratch, len, lo_pos);
	if (error)
		return error;

	xfarray_sort_bump_heapsorts(si);
	sort(scratch, hi - lo + 1, si->array->obj_size, si->cmp_fn, NULL);

	xfarray_sort_bump_stores(si);
	return xfile_store(si->array->xfile, scratch, len, lo_pos);
}

/*
 * Sort the records from lo to hi (inclusive) if they are all backed by the
 * same memory folio.  Returns 1 if it sorted, 0 if it did not, or a negative
 * errno.
 */
STATIC int
xfarray_foliosort(
	struct xfarray_sortinfo	*si,
	xfarray_idx_t		lo,
	xfarray_idx_t		hi)
{
	struct folio		*folio;
	void			*startp;
	loff_t			lo_pos = xfarray_pos(si->array, lo);
	uint64_t		len = xfarray_pos(si->array, hi - lo + 1);

	/* No single folio could back this many records. */
	if (len > XFILE_MAX_FOLIO_SIZE)
		return 0;

	xfarray_sort_bump_loads(si);
	folio = xfile_get_folio(si->array->xfile, lo_pos, len, XFILE_ALLOC);
	if (IS_ERR(folio))
		return PTR_ERR(folio);
	if (!folio)
		return 0;

	trace_xfarray_foliosort(si, lo, hi);

	xfarray_sort_bump_heapsorts(si);
	startp = folio_address(folio) + offset_in_folio(folio, lo_pos);
	sort(startp, hi - lo + 1, si->array->obj_size, si->cmp_fn, NULL);

	xfarray_sort_bump_stores(si);
	xfile_put_folio(si->array->xfile, folio);
	return 1;
}

/* Return a pointer to the xfarray pivot record within the sortinfo struct. */
static inline void *xfarray_sortinfo_pivot(struct xfarray_sortinfo *si)
{
	return xfarray_sortinfo_hi(si) + si->max_stack_depth;
}

/* Return a pointer to the start of the pivot array. */
static inline void *
xfarray_sortinfo_pivot_array(
	struct xfarray_sortinfo	*si)
{
	return xfarray_sortinfo_pivot(si) + si->array->obj_size;
}

/* The xfarray record is stored at the start of each pivot array element. */
static inline void *
xfarray_pivot_array_rec(
	void			*pa,
	size_t			pa_recsz,
	unsigned int		pa_idx)
{
	return pa + (pa_recsz * pa_idx);
}

/* The xfarray index is stored at the end of each pivot array element. */
static inline xfarray_idx_t *
xfarray_pivot_array_idx(
	void			*pa,
	size_t			pa_recsz,
	unsigned int		pa_idx)
{
	return xfarray_pivot_array_rec(pa, pa_recsz, pa_idx + 1) -
			sizeof(xfarray_idx_t);
}

/*
 * Find a pivot value for quicksort partitioning, swap it with a[lo], and save
 * the cached pivot record for the next step.
 *
 * Load evenly-spaced records within the given range into memory, sort them,
 * and choose the pivot from the median record.  Using multiple points will
 * improve the quality of the pivot selection, and hopefully avoid the worst
 * quicksort behavior, since our array values are nearly always evenly sorted.
 */
STATIC int
xfarray_qsort_pivot(
	struct xfarray_sortinfo	*si,
	xfarray_idx_t		lo,
	xfarray_idx_t		hi)
{
	void			*pivot = xfarray_sortinfo_pivot(si);
	void			*parray = xfarray_sortinfo_pivot_array(si);
	void			*recp;
	xfarray_idx_t		*idxp;
	xfarray_idx_t		step = (hi - lo) / (XFARRAY_QSORT_PIVOT_NR - 1);
	size_t			pivot_rec_sz = xfarray_pivot_rec_sz(si->array);
	int			i, j;
	int			error;

	ASSERT(step > 0);

	/*
	 * Load the xfarray indexes of the records we intend to sample into the
	 * pivot array.
	 */
	idxp = xfarray_pivot_array_idx(parray, pivot_rec_sz, 0);
	*idxp = lo;
	for (i = 1; i < XFARRAY_QSORT_PIVOT_NR - 1; i++) {
		idxp = xfarray_pivot_array_idx(parray, pivot_rec_sz, i);
		*idxp = lo + (i * step);
	}
	idxp = xfarray_pivot_array_idx(parray, pivot_rec_sz,
			XFARRAY_QSORT_PIVOT_NR - 1);
	*idxp = hi;

	/* Load the selected xfarray records into the pivot array. */
	for (i = 0; i < XFARRAY_QSORT_PIVOT_NR; i++) {
		xfarray_idx_t	idx;

		recp = xfarray_pivot_array_rec(parray, pivot_rec_sz, i);
		idxp = xfarray_pivot_array_idx(parray, pivot_rec_sz, i);

		/* No unset records; load directly into the array. */
		if (likely(si->array->unset_slots == 0)) {
			error = xfarray_sort_load(si, *idxp, recp);
			if (error)
				return error;
			continue;
		}

		/*
		 * Load non-null records into the scratchpad without changing
		 * the xfarray_idx_t in the pivot array.
		 */
		idx = *idxp;
		xfarray_sort_bump_loads(si);
		error = xfarray_load_next(si->array, &idx, recp);
		if (error)
			return error;
	}

	xfarray_sort_bump_heapsorts(si);
	sort(parray, XFARRAY_QSORT_PIVOT_NR, pivot_rec_sz, si->cmp_fn, NULL);

	/*
	 * We sorted the pivot array records (which includes the xfarray
	 * indices) in xfarray record order.  The median element of the pivot
	 * array contains the xfarray record that we will use as the pivot.
	 * Copy that xfarray record to the designated space.
	 */
	recp = xfarray_pivot_array_rec(parray, pivot_rec_sz,
			XFARRAY_QSORT_PIVOT_NR / 2);
	memcpy(pivot, recp, si->array->obj_size);

	/* If the pivot record we chose was already in a[lo] then we're done. */
	idxp = xfarray_pivot_array_idx(parray, pivot_rec_sz,
			XFARRAY_QSORT_PIVOT_NR / 2);
	if (*idxp == lo)
		return 0;

	/*
	 * Find the cached copy of a[lo] in the pivot array so that we can swap
	 * a[lo] and a[pivot].
	 */
	for (i = 0, j = -1; i < XFARRAY_QSORT_PIVOT_NR; i++) {
		idxp = xfarray_pivot_array_idx(parray, pivot_rec_sz, i);
		if (*idxp == lo)
			j = i;
	}
	if (j < 0) {
		ASSERT(j >= 0);
		return -EFSCORRUPTED;
	}

	/* Swap a[lo] and a[pivot]. */
	error = xfarray_sort_store(si, lo, pivot);
	if (error)
		return error;

	recp = xfarray_pivot_array_rec(parray, pivot_rec_sz, j);
	idxp = xfarray_pivot_array_idx(parray, pivot_rec_sz,
			XFARRAY_QSORT_PIVOT_NR / 2);
	return xfarray_sort_store(si, *idxp, recp);
}

/*
 * Set up the pointers for the next iteration.  We push onto the stack all of
 * the unsorted values between a[lo + 1] and a[end[i]], and we tweak the
 * current stack frame to point to the unsorted values between a[beg[i]] and
 * a[lo] so that those values will be sorted when we pop the stack.
 */
static inline int
xfarray_qsort_push(
	struct xfarray_sortinfo	*si,
	xfarray_idx_t		*si_lo,
	xfarray_idx_t		*si_hi,
	xfarray_idx_t		lo,
	xfarray_idx_t		hi)
{
	/* Check for stack overflows */
	if (si->stack_depth >= si->max_stack_depth - 1) {
		ASSERT(si->stack_depth < si->max_stack_depth - 1);
		return -EFSCORRUPTED;
	}

	si->max_stack_used = max_t(uint8_t, si->max_stack_used,
					    si->stack_depth + 2);

	si_lo[si->stack_depth + 1] = lo + 1;
	si_hi[si->stack_depth + 1] = si_hi[si->stack_depth];
	si_hi[si->stack_depth++] = lo - 1;

	/*
	 * Always start with the smaller of the two partitions to keep the
	 * amount of recursion in check.
	 */
	if (si_hi[si->stack_depth]     - si_lo[si->stack_depth] >
	    si_hi[si->stack_depth - 1] - si_lo[si->stack_depth - 1]) {
		swap(si_lo[si->stack_depth], si_lo[si->stack_depth - 1]);
		swap(si_hi[si->stack_depth], si_hi[si->stack_depth - 1]);
	}

	return 0;
}

static inline void
xfarray_sort_scan_done(
	struct xfarray_sortinfo	*si)
{
	if (si->folio)
		xfile_put_folio(si->array->xfile, si->folio);
	si->folio = NULL;
}

/*
 * Cache the folio backing the start of the given array element.  If the array
 * element is contained entirely within the folio, return a pointer to the
 * cached folio.  Otherwise, load the element into the scratchpad and return a
 * pointer to the scratchpad.
 */
static inline int
xfarray_sort_scan(
	struct xfarray_sortinfo	*si,
	xfarray_idx_t		idx,
	void			**ptrp)
{
	loff_t			idx_pos = xfarray_pos(si->array, idx);
	int			error = 0;

	if (xfarray_sort_terminated(si, &error))
		return error;

	trace_xfarray_sort_scan(si, idx);

	/* If the cached folio doesn't cover this index, release it. */
	if (si->folio &&
	    (idx < si->first_folio_idx || idx > si->last_folio_idx))
		xfarray_sort_scan_done(si);

	/* Grab the first folio that backs this array element. */
	if (!si->folio) {
		loff_t		next_pos;

		si->folio = xfile_get_folio(si->array->xfile, idx_pos,
				si->array->obj_size, XFILE_ALLOC);
		if (IS_ERR(si->folio))
			return PTR_ERR(si->folio);

		si->first_folio_idx = xfarray_idx(si->array,
				folio_pos(si->folio) + si->array->obj_size - 1);

		next_pos = folio_pos(si->folio) + folio_size(si->folio);
		si->last_folio_idx = xfarray_idx(si->array, next_pos - 1);
		if (xfarray_pos(si->array, si->last_folio_idx + 1) > next_pos)
			si->last_folio_idx--;

		trace_xfarray_sort_scan(si, idx);
	}

	/*
	 * If this folio still doesn't cover the desired element, it must cross
	 * a folio boundary.  Read into the scratchpad and we're done.
	 */
	if (idx < si->first_folio_idx || idx > si->last_folio_idx) {
		void		*temp = xfarray_scratch(si->array);

		error = xfile_load(si->array->xfile, temp, si->array->obj_size,
				idx_pos);
		if (error)
			return error;

		*ptrp = temp;
		return 0;
	}

	/* Otherwise return a pointer to the array element in the folio. */
	*ptrp = folio_address(si->folio) + offset_in_folio(si->folio, idx_pos);
	return 0;
}

/*
 * Sort the array elements via quicksort.  This implementation incorporates
 * four optimizations discussed in Sedgewick:
 *
 * 1. Use an explicit stack of array indices to store the next array partition
 *    to sort.  This helps us to avoid recursion in the call stack, which is
 *    particularly expensive in the kernel.
 *
 * 2. For arrays with records in arbitrary or user-controlled order, choose the
 *    pivot element using a median-of-nine decision tree.  This reduces the
 *    probability of selecting a bad pivot value which causes worst case
 *    behavior (i.e. partition sizes of 1).
 *
 * 3. The smaller of the two sub-partitions is pushed onto the stack to start
 *    the next level of recursion, and the larger sub-partition replaces the
 *    current stack frame.  This guarantees that we won't need more than
 *    log2(nr) stack space.
 *
 * 4. For small sets, load the records into the scratchpad and run heapsort on
 *    them because that is very fast.  In the author's experience, this yields
 *    a ~10% reduction in runtime.
 *
 *    If a small set is contained entirely within a single xfile memory page,
 *    map the page directly and run heap sort directly on the xfile page
 *    instead of using the load/store interface.  This halves the runtime.
 *
 * 5. This optimization is specific to the implementation.  When converging lo
 *    and hi after selecting a pivot, we will try to retain the xfile memory
 *    page between load calls, which reduces run time by 50%.
 */

/*
 * Due to the use of signed indices, we can only support up to 2^63 records.
 * Files can only grow to 2^63 bytes, so this is not much of a limitation.
 */
#define QSORT_MAX_RECS		(1ULL << 63)

int
xfarray_sort(
	struct xfarray		*array,
	xfarray_cmp_fn		cmp_fn,
	unsigned int		flags)
{
	struct xfarray_sortinfo	*si;
	xfarray_idx_t		*si_lo, *si_hi;
	void			*pivot;
	void			*scratch = xfarray_scratch(array);
	xfarray_idx_t		lo, hi;
	int			error = 0;

	if (array->nr < 2)
		return 0;
	if (array->nr >= QSORT_MAX_RECS)
		return -E2BIG;

	error = xfarray_sortinfo_alloc(array, cmp_fn, flags, &si);
	if (error)
		return error;
	si_lo = xfarray_sortinfo_lo(si);
	si_hi = xfarray_sortinfo_hi(si);
	pivot = xfarray_sortinfo_pivot(si);

	while (si->stack_depth >= 0) {
		int		ret;

		lo = si_lo[si->stack_depth];
		hi = si_hi[si->stack_depth];

		trace_xfarray_qsort(si, lo, hi);

		/* Nothing left in this partition to sort; pop stack. */
		if (lo >= hi) {
			si->stack_depth--;
			continue;
		}

		/*
		 * If directly mapping the folio and sorting can solve our
		 * problems, we're done.
		 */
		ret = xfarray_foliosort(si, lo, hi);
		if (ret < 0)
			goto out_free;
		if (ret == 1) {
			si->stack_depth--;
			continue;
		}

		/* If insertion sort can solve our problems, we're done. */
		if (xfarray_want_isort(si, lo, hi)) {
			error = xfarray_isort(si, lo, hi);
			if (error)
				goto out_free;
			si->stack_depth--;
			continue;
		}

		/* Pick a pivot, move it to a[lo] and stash it. */
		error = xfarray_qsort_pivot(si, lo, hi);
		if (error)
			goto out_free;

		/*
		 * Rearrange a[lo..hi] such that everything smaller than the
		 * pivot is on the left side of the range and everything larger
		 * than the pivot is on the right side of the range.
		 */
		while (lo < hi) {
			void	*p;

			/*
			 * Decrement hi until it finds an a[hi] less than the
			 * pivot value.
			 */
			error = xfarray_sort_scan(si, hi, &p);
			if (error)
				goto out_free;
			while (xfarray_sort_cmp(si, p, pivot) >= 0 && lo < hi) {
				hi--;
				error = xfarray_sort_scan(si, hi, &p);
				if (error)
					goto out_free;
			}
			if (p != scratch)
				memcpy(scratch, p, si->array->obj_size);
			xfarray_sort_scan_done(si);
			if (xfarray_sort_terminated(si, &error))
				goto out_free;

			/* Copy that item (a[hi]) to a[lo]. */
			if (lo < hi) {
				error = xfarray_sort_store(si, lo++, scratch);
				if (error)
					goto out_free;
			}

			/*
			 * Increment lo until it finds an a[lo] greater than
			 * the pivot value.
			 */
			error = xfarray_sort_scan(si, lo, &p);
			if (error)
				goto out_free;
			while (xfarray_sort_cmp(si, p, pivot) <= 0 && lo < hi) {
				lo++;
				error = xfarray_sort_scan(si, lo, &p);
				if (error)
					goto out_free;
			}
			if (p != scratch)
				memcpy(scratch, p, si->array->obj_size);
			xfarray_sort_scan_done(si);
			if (xfarray_sort_terminated(si, &error))
				goto out_free;

			/* Copy that item (a[lo]) to a[hi]. */
			if (lo < hi) {
				error = xfarray_sort_store(si, hi--, scratch);
				if (error)
					goto out_free;
			}

			if (xfarray_sort_terminated(si, &error))
				goto out_free;
		}

		/*
		 * Put our pivot value in the correct place at a[lo].  All
		 * values between a[beg[i]] and a[lo - 1] should be less than
		 * the pivot; and all values between a[lo + 1] and a[end[i]-1]
		 * should be greater than the pivot.
		 */
		error = xfarray_sort_store(si, lo, pivot);
		if (error)
			goto out_free;

		/* Set up the stack frame to process the two partitions. */
		error = xfarray_qsort_push(si, si_lo, si_hi, lo, hi);
		if (error)
			goto out_free;

		if (xfarray_sort_terminated(si, &error))
			goto out_free;
	}

out_free:
	trace_xfarray_sort_stats(si, error);
	kvfree(si);
	return error;
}
