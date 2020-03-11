/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_BTREE_STAGING_H__
#define __XFS_BTREE_STAGING_H__

/* Fake root for an AG-rooted btree. */
struct xbtree_afakeroot {
	/* AG block number of the new btree root. */
	xfs_agblock_t		af_root;

	/* Height of the new btree. */
	unsigned int		af_levels;

	/* Number of blocks used by the btree. */
	unsigned int		af_blocks;
};

/* Cursor interactions with with fake roots for AG-rooted btrees. */
void xfs_btree_stage_afakeroot(struct xfs_btree_cur *cur,
		struct xbtree_afakeroot *afake);
void xfs_btree_commit_afakeroot(struct xfs_btree_cur *cur, struct xfs_trans *tp,
		struct xfs_buf *agbp, const struct xfs_btree_ops *ops);

#endif	/* __XFS_BTREE_STAGING_H__ */
