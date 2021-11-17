// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
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

#define for_each_xbitmap_extent(bex, n, bitmap) \
	list_for_each_entry_safe((bex), (n), &(bitmap)->list, list)

#define for_each_xbitmap_block(b, bex, n, bitmap) \
	list_for_each_entry_safe((bex), (n), &(bitmap)->list, list) \
		for ((b) = (bex)->start; (b) < (bex)->start + (bex)->len; (b)++)

int xbitmap_set(struct xbitmap *bitmap, uint64_t start, uint64_t len);
int xbitmap_disunion(struct xbitmap *bitmap, struct xbitmap *sub);
int xbitmap_set_btcur_path(struct xbitmap *bitmap,
		struct xfs_btree_cur *cur);
int xbitmap_set_btblocks(struct xbitmap *bitmap,
		struct xfs_btree_cur *cur);
uint64_t xbitmap_hweight(struct xbitmap *bitmap);

#endif	/* __XFS_SCRUB_BITMAP_H__ */
