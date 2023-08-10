/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2021-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_XFARRAY_H__
#define __XFS_SCRUB_XFARRAY_H__

/* xfile array index type, along with cursor initialization */
typedef uint64_t		xfarray_idx_t;
#define XFARRAY_CURSOR_INIT	((__force xfarray_idx_t)0)

/* Iterate each index of an xfile array. */
#define foreach_xfarray_idx(array, idx) \
	for ((idx) = XFARRAY_CURSOR_INIT; \
	     (idx) < xfarray_length(array); \
	     (idx)++)

struct xfarray {
	/* Underlying file that backs the array. */
	struct xfile	*xfile;

	/* Number of array elements. */
	xfarray_idx_t	nr;

	/* Maximum possible array size. */
	xfarray_idx_t	max_nr;

	/* Number of unset slots in the array below @nr. */
	uint64_t	unset_slots;

	/* Size of an array element. */
	size_t		obj_size;

	/* log2 of array element size, if possible. */
	int		obj_size_log;
};

int xfarray_create(const char *descr, unsigned long long required_capacity,
		size_t obj_size, struct xfarray **arrayp);
void xfarray_destroy(struct xfarray *array);
int xfarray_load(struct xfarray *array, xfarray_idx_t idx, void *ptr);
int xfarray_unset(struct xfarray *array, xfarray_idx_t idx);
int xfarray_store(struct xfarray *array, xfarray_idx_t idx, const void *ptr);
int xfarray_store_anywhere(struct xfarray *array, const void *ptr);
bool xfarray_element_is_null(struct xfarray *array, const void *ptr);

/* Append an element to the array. */
static inline int xfarray_append(struct xfarray *array, const void *ptr)
{
	return xfarray_store(array, array->nr, ptr);
}

uint64_t xfarray_length(struct xfarray *array);
int xfarray_load_next(struct xfarray *array, xfarray_idx_t *idx, void *rec);

#endif /* __XFS_SCRUB_XFARRAY_H__ */
