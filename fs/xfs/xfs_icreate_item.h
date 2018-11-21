// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2008-2010, Dave Chinner
 * All Rights Reserved.
 */
#ifndef XFS_ICREATE_ITEM_H
#define XFS_ICREATE_ITEM_H	1

/* in memory log item structure */
struct xfs_icreate_item {
	struct xfs_log_item	ic_item;
	struct xfs_icreate_log	ic_format;
};

extern kmem_zone_t *xfs_icreate_zone;	/* inode create item zone */

void xfs_icreate_log(struct xfs_trans *tp, xfs_agnumber_t agno,
			xfs_agblock_t agbno, unsigned int count,
			unsigned int inode_size, xfs_agblock_t length,
			unsigned int generation);

#endif	/* XFS_ICREATE_ITEM_H */
