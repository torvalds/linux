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

/* Cursor interactions with fake roots for AG-rooted btrees. */
void xfs_btree_stage_afakeroot(struct xfs_btree_cur *cur,
		struct xbtree_afakeroot *afake);
void xfs_btree_commit_afakeroot(struct xfs_btree_cur *cur, struct xfs_trans *tp,
		struct xfs_buf *agbp);

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
};

/* Cursor interactions with fake roots for inode-rooted btrees. */
void xfs_btree_stage_ifakeroot(struct xfs_btree_cur *cur,
		struct xbtree_ifakeroot *ifake);
void xfs_btree_commit_ifakeroot(struct xfs_btree_cur *cur, struct xfs_trans *tp,
		int whichfork);

/* Bulk loading of staged btrees. */
typedef int (*xfs_btree_bload_get_records_fn)(struct xfs_btree_cur *cur,
		unsigned int idx, struct xfs_btree_block *block,
		unsigned int nr_wanted, void *priv);
typedef int (*xfs_btree_bload_claim_block_fn)(struct xfs_btree_cur *cur,
		union xfs_btree_ptr *ptr, void *priv);
typedef size_t (*xfs_btree_bload_iroot_size_fn)(struct xfs_btree_cur *cur,
		unsigned int level, unsigned int nr_this_level, void *priv);

struct xfs_btree_bload {
	/*
	 * This function will be called to load @nr_wanted records into the
	 * btree.  The implementation does this by setting the cursor's bc_rec
	 * field in in-core format and using init_rec_from_cur to set the
	 * records in the btree block.  Records must be returned in sort order.
	 * The function must return the number of records loaded or the usual
	 * negative errno.
	 */
	xfs_btree_bload_get_records_fn	get_records;

	/*
	 * This function will be called nr_blocks times to obtain a pointer
	 * to a new btree block on disk.  Callers must preallocate all space
	 * for the new btree before calling xfs_btree_bload, and this function
	 * is what claims that reservation.
	 */
	xfs_btree_bload_claim_block_fn	claim_block;

	/*
	 * This function should return the size of the in-core btree root
	 * block.  It is only necessary for XFS_BTREE_TYPE_INODE btrees.
	 */
	xfs_btree_bload_iroot_size_fn	iroot_size;

	/*
	 * The caller should set this to the number of records that will be
	 * stored in the new btree.
	 */
	uint64_t			nr_records;

	/*
	 * Number of free records to leave in each leaf block.  If the caller
	 * sets this to -1, the slack value will be calculated to be halfway
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

	/*
	 * Flush the new btree block buffer list to disk after this many blocks
	 * have been formatted.  Zero prohibits writing any buffers until all
	 * blocks have been formatted.
	 */
	uint16_t			max_dirty;

	/* Number of dirty buffers. */
	uint16_t			nr_dirty;
};

int xfs_btree_bload_compute_geometry(struct xfs_btree_cur *cur,
		struct xfs_btree_bload *bbl, uint64_t nr_records);
int xfs_btree_bload(struct xfs_btree_cur *cur, struct xfs_btree_bload *bbl,
		void *priv);

#endif	/* __XFS_BTREE_STAGING_H__ */
