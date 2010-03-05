/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_AG_H__
#define	__XFS_AG_H__

/*
 * Allocation group header
 * This is divided into three structures, placed in sequential 512-byte
 * buffers after a copy of the superblock (also in a 512-byte buffer).
 */

struct xfs_buf;
struct xfs_mount;
struct xfs_trans;

#define	XFS_AGF_MAGIC	0x58414746	/* 'XAGF' */
#define	XFS_AGI_MAGIC	0x58414749	/* 'XAGI' */
#define	XFS_AGF_VERSION	1
#define	XFS_AGI_VERSION	1

#define	XFS_AGF_GOOD_VERSION(v)	((v) == XFS_AGF_VERSION)
#define	XFS_AGI_GOOD_VERSION(v)	((v) == XFS_AGI_VERSION)

/*
 * Btree number 0 is bno, 1 is cnt.  This value gives the size of the
 * arrays below.
 */
#define	XFS_BTNUM_AGF	((int)XFS_BTNUM_CNTi + 1)

/*
 * The second word of agf_levels in the first a.g. overlaps the EFS
 * superblock's magic number.  Since the magic numbers valid for EFS
 * are > 64k, our value cannot be confused for an EFS superblock's.
 */

typedef struct xfs_agf {
	/*
	 * Common allocation group header information
	 */
	__be32		agf_magicnum;	/* magic number == XFS_AGF_MAGIC */
	__be32		agf_versionnum;	/* header version == XFS_AGF_VERSION */
	__be32		agf_seqno;	/* sequence # starting from 0 */
	__be32		agf_length;	/* size in blocks of a.g. */
	/*
	 * Freespace information
	 */
	__be32		agf_roots[XFS_BTNUM_AGF];	/* root blocks */
	__be32		agf_spare0;	/* spare field */
	__be32		agf_levels[XFS_BTNUM_AGF];	/* btree levels */
	__be32		agf_spare1;	/* spare field */
	__be32		agf_flfirst;	/* first freelist block's index */
	__be32		agf_fllast;	/* last freelist block's index */
	__be32		agf_flcount;	/* count of blocks in freelist */
	__be32		agf_freeblks;	/* total free blocks */
	__be32		agf_longest;	/* longest free space */
	__be32		agf_btreeblks;	/* # of blocks held in AGF btrees */
} xfs_agf_t;

#define	XFS_AGF_MAGICNUM	0x00000001
#define	XFS_AGF_VERSIONNUM	0x00000002
#define	XFS_AGF_SEQNO		0x00000004
#define	XFS_AGF_LENGTH		0x00000008
#define	XFS_AGF_ROOTS		0x00000010
#define	XFS_AGF_LEVELS		0x00000020
#define	XFS_AGF_FLFIRST		0x00000040
#define	XFS_AGF_FLLAST		0x00000080
#define	XFS_AGF_FLCOUNT		0x00000100
#define	XFS_AGF_FREEBLKS	0x00000200
#define	XFS_AGF_LONGEST		0x00000400
#define	XFS_AGF_BTREEBLKS	0x00000800
#define	XFS_AGF_NUM_BITS	12
#define	XFS_AGF_ALL_BITS	((1 << XFS_AGF_NUM_BITS) - 1)

#define XFS_AGF_FLAGS \
	{ XFS_AGF_MAGICNUM,	"MAGICNUM" }, \
	{ XFS_AGF_VERSIONNUM,	"VERSIONNUM" }, \
	{ XFS_AGF_SEQNO,	"SEQNO" }, \
	{ XFS_AGF_LENGTH,	"LENGTH" }, \
	{ XFS_AGF_ROOTS,	"ROOTS" }, \
	{ XFS_AGF_LEVELS,	"LEVELS" }, \
	{ XFS_AGF_FLFIRST,	"FLFIRST" }, \
	{ XFS_AGF_FLLAST,	"FLLAST" }, \
	{ XFS_AGF_FLCOUNT,	"FLCOUNT" }, \
	{ XFS_AGF_FREEBLKS,	"FREEBLKS" }, \
	{ XFS_AGF_LONGEST,	"LONGEST" }, \
	{ XFS_AGF_BTREEBLKS,	"BTREEBLKS" }

/* disk block (xfs_daddr_t) in the AG */
#define XFS_AGF_DADDR(mp)	((xfs_daddr_t)(1 << (mp)->m_sectbb_log))
#define	XFS_AGF_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGF_DADDR(mp))
#define	XFS_BUF_TO_AGF(bp)	((xfs_agf_t *)XFS_BUF_PTR(bp))

extern int xfs_read_agf(struct xfs_mount *mp, struct xfs_trans *tp,
			xfs_agnumber_t agno, int flags, struct xfs_buf **bpp);

/*
 * Size of the unlinked inode hash table in the agi.
 */
#define	XFS_AGI_UNLINKED_BUCKETS	64

typedef struct xfs_agi {
	/*
	 * Common allocation group header information
	 */
	__be32		agi_magicnum;	/* magic number == XFS_AGI_MAGIC */
	__be32		agi_versionnum;	/* header version == XFS_AGI_VERSION */
	__be32		agi_seqno;	/* sequence # starting from 0 */
	__be32		agi_length;	/* size in blocks of a.g. */
	/*
	 * Inode information
	 * Inodes are mapped by interpreting the inode number, so no
	 * mapping data is needed here.
	 */
	__be32		agi_count;	/* count of allocated inodes */
	__be32		agi_root;	/* root of inode btree */
	__be32		agi_level;	/* levels in inode btree */
	__be32		agi_freecount;	/* number of free inodes */
	__be32		agi_newino;	/* new inode just allocated */
	__be32		agi_dirino;	/* last directory inode chunk */
	/*
	 * Hash table of inodes which have been unlinked but are
	 * still being referenced.
	 */
	__be32		agi_unlinked[XFS_AGI_UNLINKED_BUCKETS];
} xfs_agi_t;

#define	XFS_AGI_MAGICNUM	0x00000001
#define	XFS_AGI_VERSIONNUM	0x00000002
#define	XFS_AGI_SEQNO		0x00000004
#define	XFS_AGI_LENGTH		0x00000008
#define	XFS_AGI_COUNT		0x00000010
#define	XFS_AGI_ROOT		0x00000020
#define	XFS_AGI_LEVEL		0x00000040
#define	XFS_AGI_FREECOUNT	0x00000080
#define	XFS_AGI_NEWINO		0x00000100
#define	XFS_AGI_DIRINO		0x00000200
#define	XFS_AGI_UNLINKED	0x00000400
#define	XFS_AGI_NUM_BITS	11
#define	XFS_AGI_ALL_BITS	((1 << XFS_AGI_NUM_BITS) - 1)

/* disk block (xfs_daddr_t) in the AG */
#define XFS_AGI_DADDR(mp)	((xfs_daddr_t)(2 << (mp)->m_sectbb_log))
#define	XFS_AGI_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGI_DADDR(mp))
#define	XFS_BUF_TO_AGI(bp)	((xfs_agi_t *)XFS_BUF_PTR(bp))

extern int xfs_read_agi(struct xfs_mount *mp, struct xfs_trans *tp,
				xfs_agnumber_t agno, struct xfs_buf **bpp);

/*
 * The third a.g. block contains the a.g. freelist, an array
 * of block pointers to blocks owned by the allocation btree code.
 */
#define XFS_AGFL_DADDR(mp)	((xfs_daddr_t)(3 << (mp)->m_sectbb_log))
#define	XFS_AGFL_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGFL_DADDR(mp))
#define XFS_AGFL_SIZE(mp)	((mp)->m_sb.sb_sectsize / sizeof(xfs_agblock_t))
#define	XFS_BUF_TO_AGFL(bp)	((xfs_agfl_t *)XFS_BUF_PTR(bp))

typedef struct xfs_agfl {
	__be32		agfl_bno[1];	/* actually XFS_AGFL_SIZE(mp) */
} xfs_agfl_t;

/*
 * Busy block/extent entry.  Used in perag to mark blocks that have been freed
 * but whose transactions aren't committed to disk yet.
 */
typedef struct xfs_perag_busy {
	xfs_agblock_t	busy_start;
	xfs_extlen_t	busy_length;
	struct xfs_trans *busy_tp;	/* transaction that did the free */
} xfs_perag_busy_t;

/*
 * Per-ag incore structure, copies of information in agf and agi,
 * to improve the performance of allocation group selection.
 */
#define XFS_PAGB_NUM_SLOTS	128

typedef struct xfs_perag {
	struct xfs_mount *pag_mount;	/* owner filesystem */
	xfs_agnumber_t	pag_agno;	/* AG this structure belongs to */
	atomic_t	pag_ref;	/* perag reference count */
	char		pagf_init;	/* this agf's entry is initialized */
	char		pagi_init;	/* this agi's entry is initialized */
	char		pagf_metadata;	/* the agf is preferred to be metadata */
	char		pagi_inodeok;	/* The agi is ok for inodes */
	__uint8_t	pagf_levels[XFS_BTNUM_AGF];
					/* # of levels in bno & cnt btree */
	__uint32_t	pagf_flcount;	/* count of blocks in freelist */
	xfs_extlen_t	pagf_freeblks;	/* total free blocks */
	xfs_extlen_t	pagf_longest;	/* longest free space */
	__uint32_t	pagf_btreeblks;	/* # of blocks held in AGF btrees */
	xfs_agino_t	pagi_freecount;	/* number of free inodes */
	xfs_agino_t	pagi_count;	/* number of allocated inodes */

	/*
	 * Inode allocation search lookup optimisation.
	 * If the pagino matches, the search for new inodes
	 * doesn't need to search the near ones again straight away
	 */
	xfs_agino_t	pagl_pagino;
	xfs_agino_t	pagl_leftrec;
	xfs_agino_t	pagl_rightrec;
#ifdef __KERNEL__
	spinlock_t	pagb_lock;	/* lock for pagb_list */

	atomic_t        pagf_fstrms;    /* # of filestreams active in this AG */

	int		pag_ici_init;	/* incore inode cache initialised */
	rwlock_t	pag_ici_lock;	/* incore inode lock */
	struct radix_tree_root pag_ici_root;	/* incore inode cache root */
#endif
	int		pagb_count;	/* pagb slots in use */
	xfs_perag_busy_t pagb_list[XFS_PAGB_NUM_SLOTS];	/* unstable blocks */
} xfs_perag_t;

/*
 * tags for inode radix tree
 */
#define XFS_ICI_NO_TAG		(-1)	/* special flag for an untagged lookup
					   in xfs_inode_ag_iterator */
#define XFS_ICI_RECLAIM_TAG	0	/* inode is to be reclaimed */

#define	XFS_AG_MAXLEVELS(mp)		((mp)->m_ag_maxlevels)
#define	XFS_MIN_FREELIST_RAW(bl,cl,mp)	\
	(MIN(bl + 1, XFS_AG_MAXLEVELS(mp)) + MIN(cl + 1, XFS_AG_MAXLEVELS(mp)))
#define	XFS_MIN_FREELIST(a,mp)		\
	(XFS_MIN_FREELIST_RAW(		\
		be32_to_cpu((a)->agf_levels[XFS_BTNUM_BNOi]), \
		be32_to_cpu((a)->agf_levels[XFS_BTNUM_CNTi]), mp))
#define	XFS_MIN_FREELIST_PAG(pag,mp)	\
	(XFS_MIN_FREELIST_RAW(		\
		(unsigned int)(pag)->pagf_levels[XFS_BTNUM_BNOi], \
		(unsigned int)(pag)->pagf_levels[XFS_BTNUM_CNTi], mp))

#define XFS_AGB_TO_FSB(mp,agno,agbno)	\
	(((xfs_fsblock_t)(agno) << (mp)->m_sb.sb_agblklog) | (agbno))
#define	XFS_FSB_TO_AGNO(mp,fsbno)	\
	((xfs_agnumber_t)((fsbno) >> (mp)->m_sb.sb_agblklog))
#define	XFS_FSB_TO_AGBNO(mp,fsbno)	\
	((xfs_agblock_t)((fsbno) & xfs_mask32lo((mp)->m_sb.sb_agblklog)))
#define	XFS_AGB_TO_DADDR(mp,agno,agbno)	\
	((xfs_daddr_t)XFS_FSB_TO_BB(mp, \
		(xfs_fsblock_t)(agno) * (mp)->m_sb.sb_agblocks + (agbno)))
#define	XFS_AG_DADDR(mp,agno,d)		(XFS_AGB_TO_DADDR(mp, agno, 0) + (d))

/*
 * For checking for bad ranges of xfs_daddr_t's, covering multiple
 * allocation groups or a single xfs_daddr_t that's a superblock copy.
 */
#define	XFS_AG_CHECK_DADDR(mp,d,len)	\
	((len) == 1 ? \
	    ASSERT((d) == XFS_SB_DADDR || \
		   xfs_daddr_to_agbno(mp, d) != XFS_SB_DADDR) : \
	    ASSERT(xfs_daddr_to_agno(mp, d) == \
		   xfs_daddr_to_agno(mp, (d) + (len) - 1)))

#endif	/* __XFS_AG_H__ */
