// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_AGINO_BITMAP_H__
#define __XFS_SCRUB_AGINO_BITMAP_H__

/* Bitmaps, but for type-checked for xfs_agino_t */

struct xagino_bitmap {
	struct xbitmap32	aginobitmap;
};

static inline void xagino_bitmap_init(struct xagino_bitmap *bitmap)
{
	xbitmap32_init(&bitmap->aginobitmap);
}

static inline void xagino_bitmap_destroy(struct xagino_bitmap *bitmap)
{
	xbitmap32_destroy(&bitmap->aginobitmap);
}

static inline int xagino_bitmap_clear(struct xagino_bitmap *bitmap,
		xfs_agino_t agino, unsigned int len)
{
	return xbitmap32_clear(&bitmap->aginobitmap, agino, len);
}

static inline int xagino_bitmap_set(struct xagino_bitmap *bitmap,
		xfs_agino_t agino, unsigned int len)
{
	return xbitmap32_set(&bitmap->aginobitmap, agino, len);
}

static inline bool xagino_bitmap_test(struct xagino_bitmap *bitmap,
		xfs_agino_t agino, unsigned int *len)
{
	return xbitmap32_test(&bitmap->aginobitmap, agino, len);
}

static inline int xagino_bitmap_walk(struct xagino_bitmap *bitmap,
		xbitmap32_walk_fn fn, void *priv)
{
	return xbitmap32_walk(&bitmap->aginobitmap, fn, priv);
}

#endif	/* __XFS_SCRUB_AGINO_BITMAP_H__ */
