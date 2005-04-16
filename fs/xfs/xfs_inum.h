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
#ifndef __XFS_INUM_H__
#define	__XFS_INUM_H__

/*
 * Inode number format:
 * low inopblog bits - offset in block
 * next agblklog bits - block number in ag
 * next agno_log bits - ag number
 * high agno_log-agblklog-inopblog bits - 0
 */

typedef	__uint32_t	xfs_agino_t;	/* within allocation grp inode number */

/*
 * Useful inode bits for this kernel.
 * Used in some places where having 64-bits in the 32-bit kernels
 * costs too much.
 */
#if XFS_BIG_INUMS
typedef	xfs_ino_t	xfs_intino_t;
#else
typedef	__uint32_t	xfs_intino_t;
#endif

#define	NULLFSINO	((xfs_ino_t)-1)
#define	NULLAGINO	((xfs_agino_t)-1)

struct xfs_mount;

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INO_MASK)
__uint32_t xfs_ino_mask(int k);
#define	XFS_INO_MASK(k)			xfs_ino_mask(k)
#else
#define	XFS_INO_MASK(k)	((__uint32_t)((1ULL << (k)) - 1))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INO_OFFSET_BITS)
int xfs_ino_offset_bits(struct xfs_mount *mp);
#define	XFS_INO_OFFSET_BITS(mp)		xfs_ino_offset_bits(mp)
#else
#define	XFS_INO_OFFSET_BITS(mp)	((mp)->m_sb.sb_inopblog)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INO_AGBNO_BITS)
int xfs_ino_agbno_bits(struct xfs_mount *mp);
#define	XFS_INO_AGBNO_BITS(mp)		xfs_ino_agbno_bits(mp)
#else
#define	XFS_INO_AGBNO_BITS(mp)	((mp)->m_sb.sb_agblklog)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INO_AGINO_BITS)
int xfs_ino_agino_bits(struct xfs_mount *mp);
#define	XFS_INO_AGINO_BITS(mp)		xfs_ino_agino_bits(mp)
#else
#define	XFS_INO_AGINO_BITS(mp)		((mp)->m_agino_log)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INO_AGNO_BITS)
int xfs_ino_agno_bits(struct xfs_mount *mp);
#define	XFS_INO_AGNO_BITS(mp)		xfs_ino_agno_bits(mp)
#else
#define	XFS_INO_AGNO_BITS(mp)	((mp)->m_agno_log)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INO_BITS)
int xfs_ino_bits(struct xfs_mount *mp);
#define	XFS_INO_BITS(mp)		xfs_ino_bits(mp)
#else
#define	XFS_INO_BITS(mp)	(XFS_INO_AGNO_BITS(mp) + XFS_INO_AGINO_BITS(mp))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INO_TO_AGNO)
xfs_agnumber_t xfs_ino_to_agno(struct xfs_mount *mp, xfs_ino_t i);
#define	XFS_INO_TO_AGNO(mp,i)		xfs_ino_to_agno(mp,i)
#else
#define	XFS_INO_TO_AGNO(mp,i)	\
	((xfs_agnumber_t)((i) >> XFS_INO_AGINO_BITS(mp)))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INO_TO_AGINO)
xfs_agino_t xfs_ino_to_agino(struct xfs_mount *mp, xfs_ino_t i);
#define	XFS_INO_TO_AGINO(mp,i)		xfs_ino_to_agino(mp,i)
#else
#define	XFS_INO_TO_AGINO(mp,i)	\
	((xfs_agino_t)(i) & XFS_INO_MASK(XFS_INO_AGINO_BITS(mp)))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INO_TO_AGBNO)
xfs_agblock_t xfs_ino_to_agbno(struct xfs_mount *mp, xfs_ino_t i);
#define	XFS_INO_TO_AGBNO(mp,i)		xfs_ino_to_agbno(mp,i)
#else
#define	XFS_INO_TO_AGBNO(mp,i)	\
	(((xfs_agblock_t)(i) >> XFS_INO_OFFSET_BITS(mp)) & \
	 XFS_INO_MASK(XFS_INO_AGBNO_BITS(mp)))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INO_TO_OFFSET)
int xfs_ino_to_offset(struct xfs_mount *mp, xfs_ino_t i);
#define	XFS_INO_TO_OFFSET(mp,i)		xfs_ino_to_offset(mp,i)
#else
#define	XFS_INO_TO_OFFSET(mp,i)	\
	((int)(i) & XFS_INO_MASK(XFS_INO_OFFSET_BITS(mp)))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INO_TO_FSB)
xfs_fsblock_t xfs_ino_to_fsb(struct xfs_mount *mp, xfs_ino_t i);
#define	XFS_INO_TO_FSB(mp,i)		xfs_ino_to_fsb(mp,i)
#else
#define	XFS_INO_TO_FSB(mp,i)	\
	XFS_AGB_TO_FSB(mp, XFS_INO_TO_AGNO(mp,i), XFS_INO_TO_AGBNO(mp,i))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_AGINO_TO_INO)
xfs_ino_t
xfs_agino_to_ino(struct xfs_mount *mp, xfs_agnumber_t a, xfs_agino_t i);
#define	XFS_AGINO_TO_INO(mp,a,i)	xfs_agino_to_ino(mp,a,i)
#else
#define	XFS_AGINO_TO_INO(mp,a,i)	\
	(((xfs_ino_t)(a) << XFS_INO_AGINO_BITS(mp)) | (i))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_AGINO_TO_AGBNO)
xfs_agblock_t xfs_agino_to_agbno(struct xfs_mount *mp, xfs_agino_t i);
#define	XFS_AGINO_TO_AGBNO(mp,i)	xfs_agino_to_agbno(mp,i)
#else
#define	XFS_AGINO_TO_AGBNO(mp,i)	((i) >> XFS_INO_OFFSET_BITS(mp))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_AGINO_TO_OFFSET)
int xfs_agino_to_offset(struct xfs_mount *mp, xfs_agino_t i);
#define	XFS_AGINO_TO_OFFSET(mp,i)	xfs_agino_to_offset(mp,i)
#else
#define	XFS_AGINO_TO_OFFSET(mp,i)	\
	((i) & XFS_INO_MASK(XFS_INO_OFFSET_BITS(mp)))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_OFFBNO_TO_AGINO)
xfs_agino_t xfs_offbno_to_agino(struct xfs_mount *mp, xfs_agblock_t b, int o);
#define	XFS_OFFBNO_TO_AGINO(mp,b,o)	xfs_offbno_to_agino(mp,b,o)
#else
#define	XFS_OFFBNO_TO_AGINO(mp,b,o)	\
	((xfs_agino_t)(((b) << XFS_INO_OFFSET_BITS(mp)) | (o)))
#endif

#if XFS_BIG_INUMS
#define	XFS_MAXINUMBER		((xfs_ino_t)((1ULL << 56) - 1ULL))
#define	XFS_INO64_OFFSET	((xfs_ino_t)(1ULL << 32))
#else
#define	XFS_MAXINUMBER		((xfs_ino_t)((1ULL << 32) - 1ULL))
#endif
#define	XFS_MAXINUMBER_32	((xfs_ino_t)((1ULL << 32) - 1ULL))

#endif	/* __XFS_INUM_H__ */
