// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_TRANS_SPACE_H__
#define __XFS_TRANS_SPACE_H__

/*
 * Components of space reservations.
 */

/* Worst case number of bmaps that can be held in a block. */
#define XFS_MAX_CONTIG_BMAPS_PER_BLOCK(mp)    \
		(((mp)->m_bmap_dmxr[0]) - ((mp)->m_bmap_dmnr[0]))

/* Worst case number of realtime rmaps that can be held in a block. */
#define XFS_MAX_CONTIG_RTRMAPS_PER_BLOCK(mp)    \
		(((mp)->m_rtrmap_mxr[0]) - ((mp)->m_rtrmap_mnr[0]))

/* Adding one realtime rmap could split every level to the top of the tree. */
#define XFS_RTRMAPADD_SPACE_RES(mp) ((mp)->m_rtrmap_maxlevels)

/* Blocks we might need to add "b" realtime rmaps to a tree. */
#define XFS_NRTRMAPADD_SPACE_RES(mp, b) \
	((((b) + XFS_MAX_CONTIG_RTRMAPS_PER_BLOCK(mp) - 1) / \
	  XFS_MAX_CONTIG_RTRMAPS_PER_BLOCK(mp)) * \
	  XFS_RTRMAPADD_SPACE_RES(mp))

/* Worst case number of rmaps that can be held in a block. */
#define XFS_MAX_CONTIG_RMAPS_PER_BLOCK(mp)    \
		(((mp)->m_rmap_mxr[0]) - ((mp)->m_rmap_mnr[0]))

/* Adding one rmap could split every level up to the top of the tree. */
#define XFS_RMAPADD_SPACE_RES(mp) ((mp)->m_rmap_maxlevels)

/*
 * Note that we historically set m_rmap_maxlevels to 9 when reflink is enabled,
 * so we must preserve this behavior to avoid changing the transaction space
 * reservations and minimum log size calculations for existing filesystems.
 */
#define XFS_OLD_REFLINK_RMAP_MAXLEVELS		9

/* Blocks we might need to add "b" rmaps to a tree. */
#define XFS_NRMAPADD_SPACE_RES(mp, b)\
	(((b + XFS_MAX_CONTIG_RMAPS_PER_BLOCK(mp) - 1) / \
	  XFS_MAX_CONTIG_RMAPS_PER_BLOCK(mp)) * \
	  XFS_RMAPADD_SPACE_RES(mp))

#define XFS_MAX_CONTIG_EXTENTS_PER_BLOCK(mp)    \
		(((mp)->m_alloc_mxr[0]) - ((mp)->m_alloc_mnr[0]))
#define	XFS_EXTENTADD_SPACE_RES(mp,w)	(XFS_BM_MAXLEVELS(mp,w) - 1)
#define XFS_NEXTENTADD_SPACE_RES(mp,b,w)\
	(((b + XFS_MAX_CONTIG_EXTENTS_PER_BLOCK(mp) - 1) / \
	  XFS_MAX_CONTIG_EXTENTS_PER_BLOCK(mp)) * \
	  XFS_EXTENTADD_SPACE_RES(mp,w))

/* Blocks we might need to add "b" mappings & rmappings to a file. */
#define XFS_SWAP_RMAP_SPACE_RES(mp,b,w)\
	(XFS_NEXTENTADD_SPACE_RES((mp), (b), (w)) + \
	 XFS_NRMAPADD_SPACE_RES((mp), (b)))

#define	XFS_DAENTER_1B(mp,w)	\
	((w) == XFS_DATA_FORK ? (mp)->m_dir_geo->fsbcount : 1)
#define	XFS_DAENTER_DBS(mp,w)	\
	(XFS_DA_NODE_MAXDEPTH + (((w) == XFS_DATA_FORK) ? 2 : 0))
#define	XFS_DAENTER_BLOCKS(mp,w)	\
	(XFS_DAENTER_1B(mp,w) * XFS_DAENTER_DBS(mp,w))
#define	XFS_DAENTER_BMAP1B(mp,w)	\
	XFS_NEXTENTADD_SPACE_RES(mp, XFS_DAENTER_1B(mp, w), w)
#define	XFS_DAENTER_BMAPS(mp,w)		\
	(XFS_DAENTER_DBS(mp,w) * XFS_DAENTER_BMAP1B(mp,w))
#define	XFS_DAENTER_SPACE_RES(mp,w)	\
	(XFS_DAENTER_BLOCKS(mp,w) + XFS_DAENTER_BMAPS(mp,w))
#define	XFS_DAREMOVE_SPACE_RES(mp,w)	XFS_DAENTER_BMAPS(mp,w)
#define	XFS_DIRENTER_MAX_SPLIT(mp,nl)	1
#define	XFS_DIRENTER_SPACE_RES(mp,nl)	\
	(XFS_DAENTER_SPACE_RES(mp, XFS_DATA_FORK) * \
	 XFS_DIRENTER_MAX_SPLIT(mp,nl))
#define	XFS_DIRREMOVE_SPACE_RES(mp)	\
	XFS_DAREMOVE_SPACE_RES(mp, XFS_DATA_FORK)
#define	XFS_IALLOC_SPACE_RES(mp)	\
	(M_IGEO(mp)->ialloc_blks + \
	 ((xfs_has_finobt(mp) ? 2 : 1) * M_IGEO(mp)->inobt_maxlevels))

/*
 * Space reservation values for various transactions.
 */
#define	XFS_ADDAFORK_SPACE_RES(mp)	\
	((mp)->m_dir_geo->fsbcount + XFS_DAENTER_BMAP1B(mp, XFS_DATA_FORK))
#define	XFS_ATTRRM_SPACE_RES(mp)	\
	XFS_DAREMOVE_SPACE_RES(mp, XFS_ATTR_FORK)
/* This macro is not used - see inline code in xfs_attr_set */
#define	XFS_ATTRSET_SPACE_RES(mp, v)	\
	(XFS_DAENTER_SPACE_RES(mp, XFS_ATTR_FORK) + XFS_B_TO_FSB(mp, v))
#define	XFS_DIOSTRAT_SPACE_RES(mp, v)	\
	(XFS_EXTENTADD_SPACE_RES(mp, XFS_DATA_FORK) + (v))
#define	XFS_GROWFS_SPACE_RES(mp)	\
	(2 * (mp)->m_alloc_maxlevels)
#define	XFS_GROWFSRT_SPACE_RES(mp,b)	\
	((b) + XFS_EXTENTADD_SPACE_RES(mp, XFS_DATA_FORK))
#define	XFS_QM_DQALLOC_SPACE_RES(mp)	\
	(XFS_EXTENTADD_SPACE_RES(mp, XFS_DATA_FORK) + \
	 XFS_DQUOT_CLUSTER_SIZE_FSB)
#define	XFS_QM_QINOCREATE_SPACE_RES(mp)	\
	XFS_IALLOC_SPACE_RES(mp)
#define XFS_IFREE_SPACE_RES(mp)		\
	(xfs_has_finobt(mp) ? M_IGEO(mp)->inobt_maxlevels : 0)

unsigned int xfs_parent_calc_space_res(struct xfs_mount *mp,
		unsigned int namelen);

unsigned int xfs_create_space_res(struct xfs_mount *mp, unsigned int namelen);
unsigned int xfs_mkdir_space_res(struct xfs_mount *mp, unsigned int namelen);
unsigned int xfs_link_space_res(struct xfs_mount *mp, unsigned int namelen);
unsigned int xfs_symlink_space_res(struct xfs_mount *mp, unsigned int namelen,
		unsigned int fsblocks);
unsigned int xfs_remove_space_res(struct xfs_mount *mp, unsigned int namelen);

unsigned int xfs_rename_space_res(struct xfs_mount *mp,
		unsigned int src_namelen, bool target_exists,
		unsigned int target_namelen, bool has_whiteout);

#endif	/* __XFS_TRANS_SPACE_H__ */
