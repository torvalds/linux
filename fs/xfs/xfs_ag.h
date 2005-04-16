/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
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
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_AGF_GOOD_VERSION)
int xfs_agf_good_version(unsigned v);
#define	XFS_AGF_GOOD_VERSION(v)	xfs_agf_good_version(v)
#else
#define XFS_AGF_GOOD_VERSION(v)		((v) == XFS_AGF_VERSION)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_AGI_GOOD_VERSION)
int xfs_agi_good_version(unsigned v);
#define	XFS_AGI_GOOD_VERSION(v)	xfs_agi_good_version(v)
#else
#define XFS_AGI_GOOD_VERSION(v)		((v) == XFS_AGI_VERSION)
#endif

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

typedef struct xfs_agf
{
	/*
	 * Common allocation group header information
	 */
	__uint32_t	agf_magicnum;	/* magic number == XFS_AGF_MAGIC */
	__uint32_t	agf_versionnum;	/* header version == XFS_AGF_VERSION */
	xfs_agnumber_t	agf_seqno;	/* sequence # starting from 0 */
	xfs_agblock_t	agf_length;	/* size in blocks of a.g. */
	/*
	 * Freespace information
	 */
	xfs_agblock_t	agf_roots[XFS_BTNUM_AGF];	/* root blocks */
	__uint32_t	agf_spare0;	/* spare field */
	__uint32_t	agf_levels[XFS_BTNUM_AGF];	/* btree levels */
	__uint32_t	agf_spare1;	/* spare field */
	__uint32_t	agf_flfirst;	/* first freelist block's index */
	__uint32_t	agf_fllast;	/* last freelist block's index */
	__uint32_t	agf_flcount;	/* count of blocks in freelist */
	xfs_extlen_t	agf_freeblks;	/* total free blocks */
	xfs_extlen_t	agf_longest;	/* longest free space */
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
#define	XFS_AGF_NUM_BITS	11
#define	XFS_AGF_ALL_BITS	((1 << XFS_AGF_NUM_BITS) - 1)

/* disk block (xfs_daddr_t) in the AG */
#define XFS_AGF_DADDR(mp)	((xfs_daddr_t)(1 << (mp)->m_sectbb_log))
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_AGF_BLOCK)
xfs_agblock_t xfs_agf_block(struct xfs_mount *mp);
#define	XFS_AGF_BLOCK(mp)	xfs_agf_block(mp)
#else
#define XFS_AGF_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGF_DADDR(mp))
#endif

/*
 * Size of the unlinked inode hash table in the agi.
 */
#define	XFS_AGI_UNLINKED_BUCKETS	64

typedef struct xfs_agi
{
	/*
	 * Common allocation group header information
	 */
	__uint32_t	agi_magicnum;	/* magic number == XFS_AGI_MAGIC */
	__uint32_t	agi_versionnum;	/* header version == XFS_AGI_VERSION */
	xfs_agnumber_t	agi_seqno;	/* sequence # starting from 0 */
	xfs_agblock_t	agi_length;	/* size in blocks of a.g. */
	/*
	 * Inode information
	 * Inodes are mapped by interpreting the inode number, so no
	 * mapping data is needed here.
	 */
	xfs_agino_t	agi_count;	/* count of allocated inodes */
	xfs_agblock_t	agi_root;	/* root of inode btree */
	__uint32_t	agi_level;	/* levels in inode btree */
	xfs_agino_t	agi_freecount;	/* number of free inodes */
	xfs_agino_t	agi_newino;	/* new inode just allocated */
	xfs_agino_t	agi_dirino;	/* last directory inode chunk */
	/*
	 * Hash table of inodes which have been unlinked but are
	 * still being referenced.
	 */
	xfs_agino_t	agi_unlinked[XFS_AGI_UNLINKED_BUCKETS];
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
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_AGI_BLOCK)
xfs_agblock_t xfs_agi_block(struct xfs_mount *mp);
#define	XFS_AGI_BLOCK(mp)	xfs_agi_block(mp)
#else
#define XFS_AGI_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGI_DADDR(mp))
#endif

/*
 * The third a.g. block contains the a.g. freelist, an array
 * of block pointers to blocks owned by the allocation btree code.
 */
#define XFS_AGFL_DADDR(mp)	((xfs_daddr_t)(3 << (mp)->m_sectbb_log))
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_AGFL_BLOCK)
xfs_agblock_t xfs_agfl_block(struct xfs_mount *mp);
#define	XFS_AGFL_BLOCK(mp)	xfs_agfl_block(mp)
#else
#define XFS_AGFL_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGFL_DADDR(mp))
#endif
#define XFS_AGFL_SIZE(mp)	((mp)->m_sb.sb_sectsize / sizeof(xfs_agblock_t))

typedef struct xfs_agfl {
	xfs_agblock_t	agfl_bno[1];	/* actually XFS_AGFL_SIZE(mp) */
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
 *
 * pick sizes which fit in allocation buckets well
 */
#if (BITS_PER_LONG == 32)
#define XFS_PAGB_NUM_SLOTS	84
#elif (BITS_PER_LONG == 64)
#define XFS_PAGB_NUM_SLOTS	128
#endif

typedef struct xfs_perag
{
	char		pagf_init;	/* this agf's entry is initialized */
	char		pagi_init;	/* this agi's entry is initialized */
	char		pagf_metadata;	/* the agf is prefered to be metadata */
	char		pagi_inodeok;	/* The agi is ok for inodes */
	__uint8_t	pagf_levels[XFS_BTNUM_AGF];
					/* # of levels in bno & cnt btree */
	__uint32_t	pagf_flcount;	/* count of blocks in freelist */
	xfs_extlen_t	pagf_freeblks;	/* total free blocks */
	xfs_extlen_t	pagf_longest;	/* longest free space */
	xfs_agino_t	pagi_freecount;	/* number of free inodes */
#ifdef __KERNEL__
	lock_t		pagb_lock;	/* lock for pagb_list */
#endif
	int		pagb_count;	/* pagb slots in use */
	xfs_perag_busy_t *pagb_list;	/* unstable blocks */
} xfs_perag_t;

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_AG_MAXLEVELS)
int xfs_ag_maxlevels(struct xfs_mount *mp);
#define	XFS_AG_MAXLEVELS(mp)		xfs_ag_maxlevels(mp)
#else
#define	XFS_AG_MAXLEVELS(mp)	((mp)->m_ag_maxlevels)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_MIN_FREELIST)
int xfs_min_freelist(xfs_agf_t *a, struct xfs_mount *mp);
#define	XFS_MIN_FREELIST(a,mp)		xfs_min_freelist(a,mp)
#else
#define	XFS_MIN_FREELIST(a,mp)	\
	XFS_MIN_FREELIST_RAW(	\
		INT_GET((a)->agf_levels[XFS_BTNUM_BNOi], ARCH_CONVERT), \
		INT_GET((a)->agf_levels[XFS_BTNUM_CNTi], ARCH_CONVERT), mp)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_MIN_FREELIST_PAG)
int xfs_min_freelist_pag(xfs_perag_t *pag, struct xfs_mount *mp);
#define	XFS_MIN_FREELIST_PAG(pag,mp)	xfs_min_freelist_pag(pag,mp)
#else
#define	XFS_MIN_FREELIST_PAG(pag,mp)	\
	XFS_MIN_FREELIST_RAW((uint_t)(pag)->pagf_levels[XFS_BTNUM_BNOi], \
			     (uint_t)(pag)->pagf_levels[XFS_BTNUM_CNTi], mp)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_MIN_FREELIST_RAW)
int xfs_min_freelist_raw(int bl, int cl, struct xfs_mount *mp);
#define	XFS_MIN_FREELIST_RAW(bl,cl,mp)	xfs_min_freelist_raw(bl,cl,mp)
#else
#define	XFS_MIN_FREELIST_RAW(bl,cl,mp)	\
	(MIN(bl + 1, XFS_AG_MAXLEVELS(mp)) + \
	 MIN(cl + 1, XFS_AG_MAXLEVELS(mp)))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_AGB_TO_FSB)
xfs_fsblock_t xfs_agb_to_fsb(struct xfs_mount *mp, xfs_agnumber_t agno,
			     xfs_agblock_t agbno);
#define XFS_AGB_TO_FSB(mp,agno,agbno)	xfs_agb_to_fsb(mp,agno,agbno)
#else
#define	XFS_AGB_TO_FSB(mp,agno,agbno) \
	(((xfs_fsblock_t)(agno) << (mp)->m_sb.sb_agblklog) | (agbno))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_FSB_TO_AGNO)
xfs_agnumber_t xfs_fsb_to_agno(struct xfs_mount *mp, xfs_fsblock_t fsbno);
#define	XFS_FSB_TO_AGNO(mp,fsbno)	xfs_fsb_to_agno(mp,fsbno)
#else
#define	XFS_FSB_TO_AGNO(mp,fsbno) \
	((xfs_agnumber_t)((fsbno) >> (mp)->m_sb.sb_agblklog))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_FSB_TO_AGBNO)
xfs_agblock_t xfs_fsb_to_agbno(struct xfs_mount *mp, xfs_fsblock_t fsbno);
#define	XFS_FSB_TO_AGBNO(mp,fsbno)	xfs_fsb_to_agbno(mp,fsbno)
#else
#define	XFS_FSB_TO_AGBNO(mp,fsbno) \
	((xfs_agblock_t)((fsbno) & XFS_MASK32LO((mp)->m_sb.sb_agblklog)))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_AGB_TO_DADDR)
xfs_daddr_t xfs_agb_to_daddr(struct xfs_mount *mp, xfs_agnumber_t agno,
				xfs_agblock_t agbno);
#define	XFS_AGB_TO_DADDR(mp,agno,agbno)	xfs_agb_to_daddr(mp,agno,agbno)
#else
#define	XFS_AGB_TO_DADDR(mp,agno,agbno) \
	((xfs_daddr_t)(XFS_FSB_TO_BB(mp, \
		(xfs_fsblock_t)(agno) * (mp)->m_sb.sb_agblocks + (agbno))))
#endif
/*
 * XFS_DADDR_TO_AGNO and XFS_DADDR_TO_AGBNO moved to xfs_mount.h
 * to avoid header file ordering change
 */

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_AG_DADDR)
xfs_daddr_t xfs_ag_daddr(struct xfs_mount *mp, xfs_agnumber_t agno,
				xfs_daddr_t d);
#define	XFS_AG_DADDR(mp,agno,d)		xfs_ag_daddr(mp,agno,d)
#else
#define	XFS_AG_DADDR(mp,agno,d)	(XFS_AGB_TO_DADDR(mp, agno, 0) + (d))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_BUF_TO_AGF)
xfs_agf_t *xfs_buf_to_agf(struct xfs_buf *bp);
#define	XFS_BUF_TO_AGF(bp)		xfs_buf_to_agf(bp)
#else
#define	XFS_BUF_TO_AGF(bp)	((xfs_agf_t *)XFS_BUF_PTR(bp))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_BUF_TO_AGI)
xfs_agi_t *xfs_buf_to_agi(struct xfs_buf *bp);
#define	XFS_BUF_TO_AGI(bp)		xfs_buf_to_agi(bp)
#else
#define	XFS_BUF_TO_AGI(bp)	((xfs_agi_t *)XFS_BUF_PTR(bp))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_BUF_TO_AGFL)
xfs_agfl_t *xfs_buf_to_agfl(struct xfs_buf *bp);
#define	XFS_BUF_TO_AGFL(bp)		xfs_buf_to_agfl(bp)
#else
#define	XFS_BUF_TO_AGFL(bp)	((xfs_agfl_t *)XFS_BUF_PTR(bp))
#endif

/*
 * For checking for bad ranges of xfs_daddr_t's, covering multiple
 * allocation groups or a single xfs_daddr_t that's a superblock copy.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_AG_CHECK_DADDR)
void xfs_ag_check_daddr(struct xfs_mount *mp, xfs_daddr_t d, xfs_extlen_t len);
#define	XFS_AG_CHECK_DADDR(mp,d,len)	xfs_ag_check_daddr(mp,d,len)
#else
#define	XFS_AG_CHECK_DADDR(mp,d,len)	\
	((len) == 1 ? \
	    ASSERT((d) == XFS_SB_DADDR || \
		   XFS_DADDR_TO_AGBNO(mp, d) != XFS_SB_DADDR) : \
	    ASSERT(XFS_DADDR_TO_AGNO(mp, d) == \
		   XFS_DADDR_TO_AGNO(mp, (d) + (len) - 1)))
#endif

#endif	/* __XFS_AG_H__ */
