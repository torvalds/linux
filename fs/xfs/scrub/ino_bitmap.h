// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2023-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_INO_BITMAP_H__
#define __XFS_SCRUB_INO_BITMAP_H__

/* Bitmaps, but for type-checked for xfs_ino_t */

struct xino_bitmap {
	struct xbitmap64	inobitmap;
};

static inline void xino_bitmap_init(struct xino_bitmap *bitmap)
{
	xbitmap64_init(&bitmap->inobitmap);
}

static inline void xino_bitmap_destroy(struct xino_bitmap *bitmap)
{
	xbitmap64_destroy(&bitmap->inobitmap);
}

static inline int xino_bitmap_set(struct xino_bitmap *bitmap, xfs_ino_t ino)
{
	return xbitmap64_set(&bitmap->inobitmap, ino, 1);
}

static inline int xino_bitmap_test(struct xino_bitmap *bitmap, xfs_ino_t ino)
{
	uint64_t	len = 1;

	return xbitmap64_test(&bitmap->inobitmap, ino, &len);
}

#endif	/* __XFS_SCRUB_INO_BITMAP_H__ */
