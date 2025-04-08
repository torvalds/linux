// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_RTB_BITMAP_H__
#define __XFS_SCRUB_RTB_BITMAP_H__

/* Bitmaps, but for type-checked for xfs_rtblock_t */

struct xrtb_bitmap {
	struct xbitmap64	rtbitmap;
};

static inline void xrtb_bitmap_init(struct xrtb_bitmap *bitmap)
{
	xbitmap64_init(&bitmap->rtbitmap);
}

static inline void xrtb_bitmap_destroy(struct xrtb_bitmap *bitmap)
{
	xbitmap64_destroy(&bitmap->rtbitmap);
}

static inline int xrtb_bitmap_set(struct xrtb_bitmap *bitmap,
		xfs_rtblock_t start, xfs_filblks_t len)
{
	return xbitmap64_set(&bitmap->rtbitmap, start, len);
}

static inline int xrtb_bitmap_walk(struct xrtb_bitmap *bitmap,
		xbitmap64_walk_fn fn, void *priv)
{
	return xbitmap64_walk(&bitmap->rtbitmap, fn, priv);
}

#endif	/* __XFS_SCRUB_RTB_BITMAP_H__ */
