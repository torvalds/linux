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

/* Fake root for an inode-rooted btree. */
struct xbtree_ifakeroot {
	/* Fake inode fork. */
	struct xfs_ifork	*if_fork;

	/* Number of blocks used by the btree. */
	int64_t			if_blocks;

	/* Height of the new btree. */
	unsigned int		if_levels;

	/* Number of bytes available for this fork in the inode. */
	unsigned int		if_fork_size;

	/* Fork format. */
	unsigned int		if_format;

	/* Number of records. */
	unsigned int		if_extents;
};

/* Cursor interactions with with fake roots for inode-rooted btrees. */
void xfs_btree_stage_ifakeroot(struct xfs_btree_cur *cur,
		struct xbtree_ifakeroot *ifake,
		struct xfs_btree_ops **new_ops);
void xfs_btree_commit_ifakeroot(struct xfs_btree_cur *cur, struct xfs_trans *tp,
		int whichfork, const struct xfs_btree_ops *ops);

/* Bulk loading of staged btrees. */
typedef int (*xfs_btree_bload_get_record_fn)(struct xfs_btree_cur *cur, void *priv);
typedef int (*xfs_btree_bload_claim_block_fn)(struct xfs_btree_cur *cur,
		union xfs_btree_ptr *ptr, void *priv);
typedef size_t (*xfs_btree_bload_iroot_size_fn)(struct xfs_btree_cur *cur,
		unsigned int nr_this_level, void *priv);

struct xfs_btree_bload {
	/*
	 * This function will be called nr_records times to load records into
	 * the btree.  The function does this by setting the cursor's bc_rec
	 * field in in-core format.  Records must be returned in sort order.
	 */
	xfs_btree_bload_get_record_fn	get_record;

	/*
	 * This function will be called nr_blocks times to obtain a pointer
	 * to a new btree block on disk.  Callers must preallocate all space
	 * for the new btree before calling xfs_btree_bload, and this function
	 * is what claims that reservation.
	 */
	xfs_btree_bload_claim_block_fn	claim_block;

	/*
	 * This function should return the size of the in-core btree root
	 * block.  It is only necessary for XFS_BTREE_ROOT_IN_INODE btree
	 * types.
	 */
	xfs_btree_bload_iroot_size_fn	iroot_size;

	/*
	 * The caller should set this to the number of records that will be
	 * stored in the new btree.
	 */
	uint64_t			nr_records;

	/*
	 * Number of free records to leave in each leaf block.  If the caller
	 * sets this to -1, the slack value will be calculated to be be halfway
	 * between maxrecs and minrecs.  This typically leaves the block 75%
	 * full.  Note that slack values are not enforced on inode root blocks.
	 */
	int				leaf_slack;

	/*
	 * Number of free key/ptrs pairs to leave in each node block.  This
	 * field has the same semantics as leaf_slack.
	 */
	int				node_slack;

	/*
	 * The xfs_btree_bload_compute_geometry function will set this to the
	 * number of btree blocks needed to store nr_records records.
	 */
	uint64_t			nr_blocks;

	/*
	 * The xfs_btree_bload_compute_geometry function will set this to the
	 * height of the new btree.
	 */
	unsigned int			btree_height;
};

int xfs_btree_bload_compute_geometry(struct xfs_btree_cur *cur,
		struct xfs_btree_bload *bbl, uint64_t nr_records);
int xfs_btree_bload(struct xfs_btree_cur *cur, struct xfs_btree_bload *bbl,
		void *priv);

#endif	/* __XFS_BTREE_STAGING_H__ */
