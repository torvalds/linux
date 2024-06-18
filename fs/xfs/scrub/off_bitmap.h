// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_OFF_BITMAP_H__
#define __XFS_SCRUB_OFF_BITMAP_H__

/* Bitmaps, but for type-checked for xfs_fileoff_t */

struct xoff_bitmap {
	struct xbitmap64	offbitmap;
};

static inline void xoff_bitmap_init(struct xoff_bitmap *bitmap)
{
	xbitmap64_init(&bitmap->offbitmap);
}

static inline void xoff_bitmap_destroy(struct xoff_bitmap *bitmap)
{
	xbitmap64_destroy(&bitmap->offbitmap);
}

static inline int xoff_bitmap_set(struct xoff_bitmap *bitmap,
		xfs_fileoff_t off, xfs_filblks_t len)
{
	return xbitmap64_set(&bitmap->offbitmap, off, len);
}

static inline int xoff_bitmap_walk(struct xoff_bitmap *bitmap,
		xbitmap64_walk_fn fn, void *priv)
{
	return xbitmap64_walk(&bitmap->offbitmap, fn, priv);
}

#endif	/* __XFS_SCRUB_OFF_BITMAP_H__ */
