// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_FSB_BITMAP_H__
#define __XFS_SCRUB_FSB_BITMAP_H__

/* Bitmaps, but for type-checked for xfs_fsblock_t */

struct xfsb_bitmap {
	struct xbitmap64	fsbitmap;
};

static inline void xfsb_bitmap_init(struct xfsb_bitmap *bitmap)
{
	xbitmap64_init(&bitmap->fsbitmap);
}

static inline void xfsb_bitmap_destroy(struct xfsb_bitmap *bitmap)
{
	xbitmap64_destroy(&bitmap->fsbitmap);
}

static inline int xfsb_bitmap_set(struct xfsb_bitmap *bitmap,
		xfs_fsblock_t start, xfs_filblks_t len)
{
	return xbitmap64_set(&bitmap->fsbitmap, start, len);
}

static inline int xfsb_bitmap_walk(struct xfsb_bitmap *bitmap,
		xbitmap64_walk_fn fn, void *priv)
{
	return xbitmap64_walk(&bitmap->fsbitmap, fn, priv);
}

#endif	/* __XFS_SCRUB_FSB_BITMAP_H__ */
