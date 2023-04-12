// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_BITMAP_H__
#define __XFS_SCRUB_BITMAP_H__

struct xbitmap_range {
	struct list_head	list;
	uint64_t		start;
	uint64_t		len;
};

struct xbitmap {
	struct list_head	list;
};

void xbitmap_init(struct xbitmap *bitmap);
void xbitmap_destroy(struct xbitmap *bitmap);

int xbitmap_set(struct xbitmap *bitmap, uint64_t start, uint64_t len);
int xbitmap_disunion(struct xbitmap *bitmap, struct xbitmap *sub);
int xbitmap_set_btcur_path(struct xbitmap *bitmap,
		struct xfs_btree_cur *cur);
int xbitmap_set_btblocks(struct xbitmap *bitmap,
		struct xfs_btree_cur *cur);
uint64_t xbitmap_hweight(struct xbitmap *bitmap);

/*
 * Return codes for the bitmap iterator functions are 0 to continue iterating,
 * and non-zero to stop iterating.  Any non-zero value will be passed up to the
 * iteration caller.  The special value -ECANCELED can be used to stop
 * iteration, because neither bitmap iterator ever generates that error code on
 * its own.  Callers must not modify the bitmap while walking it.
 */
typedef int (*xbitmap_walk_fn)(uint64_t start, uint64_t len, void *priv);
int xbitmap_walk(struct xbitmap *bitmap, xbitmap_walk_fn fn,
		void *priv);

typedef int (*xbitmap_walk_bits_fn)(uint64_t bit, void *priv);
int xbitmap_walk_bits(struct xbitmap *bitmap, xbitmap_walk_bits_fn fn,
		void *priv);

#endif	/* __XFS_SCRUB_BITMAP_H__ */
