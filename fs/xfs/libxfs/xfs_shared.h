// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_SHARED_H__
#define __XFS_SHARED_H__

/*
 * Definitions shared between kernel and userspace that don't fit into any other
 * header file that is shared with userspace.
 */
struct xfs_ifork;
struct xfs_buf;
struct xfs_buf_ops;
struct xfs_mount;
struct xfs_trans;
struct xfs_inode;

/*
 * Buffer verifier operations are widely used, including userspace tools
 */
extern const struct xfs_buf_ops xfs_agf_buf_ops;
extern const struct xfs_buf_ops xfs_agfl_buf_ops;
extern const struct xfs_buf_ops xfs_agi_buf_ops;
extern const struct xfs_buf_ops xfs_attr3_leaf_buf_ops;
extern const struct xfs_buf_ops xfs_attr3_rmt_buf_ops;
extern const struct xfs_buf_ops xfs_bmbt_buf_ops;
extern const struct xfs_buf_ops xfs_bnobt_buf_ops;
extern const struct xfs_buf_ops xfs_cntbt_buf_ops;
extern const struct xfs_buf_ops xfs_da3_node_buf_ops;
extern const struct xfs_buf_ops xfs_dquot_buf_ops;
extern const struct xfs_buf_ops xfs_dquot_buf_ra_ops;
extern const struct xfs_buf_ops xfs_finobt_buf_ops;
extern const struct xfs_buf_ops xfs_inobt_buf_ops;
extern const struct xfs_buf_ops xfs_inode_buf_ops;
extern const struct xfs_buf_ops xfs_inode_buf_ra_ops;
extern const struct xfs_buf_ops xfs_refcountbt_buf_ops;
extern const struct xfs_buf_ops xfs_rmapbt_buf_ops;
extern const struct xfs_buf_ops xfs_rtbuf_ops;
extern const struct xfs_buf_ops xfs_sb_buf_ops;
extern const struct xfs_buf_ops xfs_sb_quiet_buf_ops;
extern const struct xfs_buf_ops xfs_symlink_buf_ops;

/* btree ops */
extern const struct xfs_btree_ops xfs_bnobt_ops;
extern const struct xfs_btree_ops xfs_cntbt_ops;
extern const struct xfs_btree_ops xfs_inobt_ops;
extern const struct xfs_btree_ops xfs_finobt_ops;
extern const struct xfs_btree_ops xfs_bmbt_ops;
extern const struct xfs_btree_ops xfs_refcountbt_ops;
extern const struct xfs_btree_ops xfs_rmapbt_ops;
extern const struct xfs_btree_ops xfs_rmapbt_mem_ops;

static inline bool xfs_btree_is_bno(const struct xfs_btree_ops *ops)
{
	return ops == &xfs_bnobt_ops;
}

static inline bool xfs_btree_is_cnt(const struct xfs_btree_ops *ops)
{
	return ops == &xfs_cntbt_ops;
}

static inline bool xfs_btree_is_bmap(const struct xfs_btree_ops *ops)
{
	return ops == &xfs_bmbt_ops;
}

static inline bool xfs_btree_is_ino(const struct xfs_btree_ops *ops)
{
	return ops == &xfs_inobt_ops;
}

static inline bool xfs_btree_is_fino(const struct xfs_btree_ops *ops)
{
	return ops == &xfs_finobt_ops;
}

static inline bool xfs_btree_is_refcount(const struct xfs_btree_ops *ops)
{
	return ops == &xfs_refcountbt_ops;
}

static inline bool xfs_btree_is_rmap(const struct xfs_btree_ops *ops)
{
	return ops == &xfs_rmapbt_ops;
}

#ifdef CONFIG_XFS_BTREE_IN_MEM
static inline bool xfs_btree_is_mem_rmap(const struct xfs_btree_ops *ops)
{
	return ops == &xfs_rmapbt_mem_ops;
}
#else
# define xfs_btree_is_mem_rmap(...)	(false)
#endif

/* log size calculation functions */
int	xfs_log_calc_unit_res(struct xfs_mount *mp, int unit_bytes);
int	xfs_log_calc_minimum_size(struct xfs_mount *);

struct xfs_trans_res;
void	xfs_log_get_max_trans_res(struct xfs_mount *mp,
				  struct xfs_trans_res *max_resp);

/*
 * Values for t_flags.
 */
/* Transaction needs to be logged */
#define XFS_TRANS_DIRTY			(1u << 0)
/* Superblock is dirty and needs to be logged */
#define XFS_TRANS_SB_DIRTY		(1u << 1)
/* Transaction took a permanent log reservation */
#define XFS_TRANS_PERM_LOG_RES		(1u << 2)
/* Synchronous transaction commit needed */
#define XFS_TRANS_SYNC			(1u << 3)
/* Transaction can use reserve block pool */
#define XFS_TRANS_RESERVE		(1u << 4)
/* Transaction should avoid VFS level superblock write accounting */
#define XFS_TRANS_NO_WRITECOUNT		(1u << 5)
/* Transaction has freed blocks returned to it's reservation */
#define XFS_TRANS_RES_FDBLKS		(1u << 6)
/* Transaction contains an intent done log item */
#define XFS_TRANS_HAS_INTENT_DONE	(1u << 7)
/*
 * LOWMODE is used by the allocator to activate the lowspace algorithm - when
 * free space is running low the extent allocator may choose to allocate an
 * extent from an AG without leaving sufficient space for a btree split when
 * inserting the new extent. In this case the allocator will enable the
 * lowspace algorithm which is supposed to allow further allocations (such as
 * btree splits and newroots) to allocate from sequential AGs. In order to
 * avoid locking AGs out of order the lowspace algorithm will start searching
 * for free space from AG 0. If the correct transaction reservations have been
 * made then this algorithm will eventually find all the space it needs.
 */
#define XFS_TRANS_LOWMODE		(1u << 8)

/* Transaction has locked the rtbitmap and rtsum inodes */
#define XFS_TRANS_RTBITMAP_LOCKED	(1u << 9)

/*
 * Field values for xfs_trans_mod_sb.
 */
#define	XFS_TRANS_SB_ICOUNT		0x00000001
#define	XFS_TRANS_SB_IFREE		0x00000002
#define	XFS_TRANS_SB_FDBLOCKS		0x00000004
#define	XFS_TRANS_SB_RES_FDBLOCKS	0x00000008
#define	XFS_TRANS_SB_FREXTENTS		0x00000010
#define	XFS_TRANS_SB_RES_FREXTENTS	0x00000020
#define	XFS_TRANS_SB_DBLOCKS		0x00000040
#define	XFS_TRANS_SB_AGCOUNT		0x00000080
#define	XFS_TRANS_SB_IMAXPCT		0x00000100
#define	XFS_TRANS_SB_REXTSIZE		0x00000200
#define	XFS_TRANS_SB_RBMBLOCKS		0x00000400
#define	XFS_TRANS_SB_RBLOCKS		0x00000800
#define	XFS_TRANS_SB_REXTENTS		0x00001000
#define	XFS_TRANS_SB_REXTSLOG		0x00002000

/*
 * Here we centralize the specification of XFS meta-data buffer reference count
 * values.  This determines how hard the buffer cache tries to hold onto the
 * buffer.
 */
#define	XFS_AGF_REF		4
#define	XFS_AGI_REF		4
#define	XFS_AGFL_REF		3
#define	XFS_INO_BTREE_REF	3
#define	XFS_ALLOC_BTREE_REF	2
#define	XFS_BMAP_BTREE_REF	2
#define	XFS_RMAP_BTREE_REF	2
#define	XFS_DIR_BTREE_REF	2
#define	XFS_INO_REF		2
#define	XFS_ATTR_BTREE_REF	1
#define	XFS_DQUOT_REF		1
#define	XFS_REFC_BTREE_REF	1
#define	XFS_SSB_REF		0

/* Computed inode geometry for the filesystem. */
struct xfs_ino_geometry {
	/* Maximum inode count in this filesystem. */
	uint64_t	maxicount;

	/* Actual inode cluster buffer size, in bytes. */
	unsigned int	inode_cluster_size;

	/*
	 * Desired inode cluster buffer size, in bytes.  This value is not
	 * rounded up to at least one filesystem block, which is necessary for
	 * the sole purpose of validating sb_spino_align.  Runtime code must
	 * only ever use inode_cluster_size.
	 */
	unsigned int	inode_cluster_size_raw;

	/* Inode cluster sizes, adjusted to be at least 1 fsb. */
	unsigned int	inodes_per_cluster;
	unsigned int	blocks_per_cluster;

	/* Inode cluster alignment. */
	unsigned int	cluster_align;
	unsigned int	cluster_align_inodes;
	unsigned int	inoalign_mask;	/* mask sb_inoalignmt if used */

	unsigned int	inobt_mxr[2]; /* max inobt btree records */
	unsigned int	inobt_mnr[2]; /* min inobt btree records */
	unsigned int	inobt_maxlevels; /* max inobt btree levels. */

	/* Size of inode allocations under normal operation. */
	unsigned int	ialloc_inos;
	unsigned int	ialloc_blks;

	/* Minimum inode blocks for a sparse allocation. */
	unsigned int	ialloc_min_blks;

	/* stripe unit inode alignment */
	unsigned int	ialloc_align;

	unsigned int	agino_log;	/* #bits for agino in inum */

	/* precomputed default inode attribute fork offset */
	unsigned int	attr_fork_offset;

	/* precomputed value for di_flags2 */
	uint64_t	new_diflags2;

	/* minimum folio order of a page cache allocation */
	unsigned int	min_folio_order;

};

#endif /* __XFS_SHARED_H__ */
