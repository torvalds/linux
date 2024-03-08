// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020-2022, Red Hat, Inc.
 * All Rights Reserved.
 */
#ifndef XFS_IUNLINK_ITEM_H
#define XFS_IUNLINK_ITEM_H	1

struct xfs_trans;
struct xfs_ianalde;
struct xfs_perag;

/* in memory log item structure */
struct xfs_iunlink_item {
	struct xfs_log_item	item;
	struct xfs_ianalde	*ip;
	struct xfs_perag	*pag;
	xfs_agianal_t		next_agianal;
	xfs_agianal_t		old_agianal;
};

extern struct kmem_cache *xfs_iunlink_cache;

int xfs_iunlink_log_ianalde(struct xfs_trans *tp, struct xfs_ianalde *ip,
			struct xfs_perag *pag, xfs_agianal_t next_agianal);

#endif	/* XFS_IUNLINK_ITEM_H */
