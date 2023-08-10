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
 * This memory array uses an xfile (which itself is a memfd "file") to store
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

	return xfile_obj_load(array->xfile, ptr, array->obj_size,
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

	error = xfile_obj_load(array->xfile, temp, array->obj_size, pos);
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
	error = xfile_obj_store(array->xfile, temp, array->obj_size, pos);
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

	ret = xfile_obj_store(array->xfile, ptr, array->obj_size,
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
		error = xfile_obj_load(array->xfile, temp, array->obj_size,
				pos);
		if (error || !xfarray_element_is_null(array, temp))
			continue;

		error = xfile_obj_store(array->xfile, ptr, array->obj_size,
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
