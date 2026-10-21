// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020-2022, Red Hat, Inc.
 * All Rights Reserved.
 */
#ifndef XFS_IUNLINK_ITEM_H
#define XFS_IUNLINK_ITEM_H	1

struct xfs_trans;
struct xfs_inode;
struct xfs_perag;

/* in memory log item structure */
struct xfs_iunlink_item {
	struct xfs_log_item	item;
	struct xfs_inode	*ip;
	struct xfs_perag	*pag;
	xfs_agino_t		next_agino;
	xfs_agino_t		old_agino;
};

extern struct kmem_cache *xfs_iunlink_cache;

int xfs_iunlink_log_inode(struct xfs_trans *tp, struct xfs_inode *ip,
			struct xfs_perag *pag, xfs_agino_t next_agino);

#endif	/* XFS_IUNLINK_ITEM_H */
