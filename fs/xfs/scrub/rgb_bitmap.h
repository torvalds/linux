// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_RGB_BITMAP_H__
#define __XFS_SCRUB_RGB_BITMAP_H__

/* Bitmaps, but for type-checked for xfs_rgblock_t */

struct xrgb_bitmap {
	struct xbitmap32	rgbitmap;
};

static inline void xrgb_bitmap_init(struct xrgb_bitmap *bitmap)
{
	xbitmap32_init(&bitmap->rgbitmap);
}

static inline void xrgb_bitmap_destroy(struct xrgb_bitmap *bitmap)
{
	xbitmap32_destroy(&bitmap->rgbitmap);
}

static inline int xrgb_bitmap_set(struct xrgb_bitmap *bitmap,
		xfs_rgblock_t start, xfs_extlen_t len)
{
	return xbitmap32_set(&bitmap->rgbitmap, start, len);
}

static inline int xrgb_bitmap_walk(struct xrgb_bitmap *bitmap,
		xbitmap32_walk_fn fn, void *priv)
{
	return xbitmap32_walk(&bitmap->rgbitmap, fn, priv);
}

#endif /* __XFS_SCRUB_RGB_BITMAP_H__ */
