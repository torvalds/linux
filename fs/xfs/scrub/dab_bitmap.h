// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_DAB_BITMAP_H__
#define __XFS_SCRUB_DAB_BITMAP_H__

/* Bitmaps, but for type-checked for xfs_dablk_t */

struct xdab_bitmap {
	struct xbitmap32	dabitmap;
};

static inline void xdab_bitmap_init(struct xdab_bitmap *bitmap)
{
	xbitmap32_init(&bitmap->dabitmap);
}

static inline void xdab_bitmap_destroy(struct xdab_bitmap *bitmap)
{
	xbitmap32_destroy(&bitmap->dabitmap);
}

static inline int xdab_bitmap_set(struct xdab_bitmap *bitmap,
		xfs_dablk_t dabno, xfs_extlen_t len)
{
	return xbitmap32_set(&bitmap->dabitmap, dabno, len);
}

static inline bool xdab_bitmap_test(struct xdab_bitmap *bitmap,
		xfs_dablk_t dabno, xfs_extlen_t *len)
{
	return xbitmap32_test(&bitmap->dabitmap, dabno, len);
}

#endif	/* __XFS_SCRUB_DAB_BITMAP_H__ */
