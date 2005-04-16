/*
 * Copyright (c) 2000-2001 Silicon Graphics, Inc.  All Rights Reserved.
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
#ifndef __XFS_SB_H__
#define	__XFS_SB_H__

/*
 * Super block
 * Fits into a sector-sized buffer at address 0 of each allocation group.
 * Only the first of these is ever updated except during growfs.
 */

struct xfs_buf;
struct xfs_mount;

#define	XFS_SB_MAGIC		0x58465342	/* 'XFSB' */
#define	XFS_SB_VERSION_1	1		/* 5.3, 6.0.1, 6.1 */
#define	XFS_SB_VERSION_2	2		/* 6.2 - attributes */
#define	XFS_SB_VERSION_3	3		/* 6.2 - new inode version */
#define	XFS_SB_VERSION_4	4		/* 6.2+ - bitmask version */
#define	XFS_SB_VERSION_NUMBITS		0x000f
#define	XFS_SB_VERSION_ALLFBITS		0xfff0
#define	XFS_SB_VERSION_SASHFBITS	0xf000
#define	XFS_SB_VERSION_REALFBITS	0x0ff0
#define	XFS_SB_VERSION_ATTRBIT		0x0010
#define	XFS_SB_VERSION_NLINKBIT		0x0020
#define	XFS_SB_VERSION_QUOTABIT		0x0040
#define	XFS_SB_VERSION_ALIGNBIT		0x0080
#define	XFS_SB_VERSION_DALIGNBIT	0x0100
#define	XFS_SB_VERSION_SHAREDBIT	0x0200
#define XFS_SB_VERSION_LOGV2BIT		0x0400
#define XFS_SB_VERSION_SECTORBIT	0x0800
#define	XFS_SB_VERSION_EXTFLGBIT	0x1000
#define	XFS_SB_VERSION_DIRV2BIT		0x2000
#define	XFS_SB_VERSION_MOREBITSBIT	0x8000
#define	XFS_SB_VERSION_OKSASHFBITS	\
	(XFS_SB_VERSION_EXTFLGBIT | \
	 XFS_SB_VERSION_DIRV2BIT)
#define	XFS_SB_VERSION_OKREALFBITS	\
	(XFS_SB_VERSION_ATTRBIT | \
	 XFS_SB_VERSION_NLINKBIT | \
	 XFS_SB_VERSION_QUOTABIT | \
	 XFS_SB_VERSION_ALIGNBIT | \
	 XFS_SB_VERSION_DALIGNBIT | \
	 XFS_SB_VERSION_SHAREDBIT | \
	 XFS_SB_VERSION_LOGV2BIT | \
	 XFS_SB_VERSION_SECTORBIT)
#define	XFS_SB_VERSION_OKSASHBITS	\
	(XFS_SB_VERSION_NUMBITS | \
	 XFS_SB_VERSION_REALFBITS | \
	 XFS_SB_VERSION_OKSASHFBITS)
#define	XFS_SB_VERSION_OKREALBITS	\
	(XFS_SB_VERSION_NUMBITS | \
	 XFS_SB_VERSION_OKREALFBITS | \
	 XFS_SB_VERSION_OKSASHFBITS)
#define XFS_SB_VERSION_MKFS(ia,dia,extflag,dirv2,na,sflag,morebits)	\
	(((ia) || (dia) || (extflag) || (dirv2) || (na) || (sflag) || \
	  (morebits)) ? \
		(XFS_SB_VERSION_4 | \
		 ((ia) ? XFS_SB_VERSION_ALIGNBIT : 0) | \
		 ((dia) ? XFS_SB_VERSION_DALIGNBIT : 0) | \
		 ((extflag) ? XFS_SB_VERSION_EXTFLGBIT : 0) | \
		 ((dirv2) ? XFS_SB_VERSION_DIRV2BIT : 0) | \
		 ((na) ? XFS_SB_VERSION_LOGV2BIT : 0) | \
		 ((sflag) ? XFS_SB_VERSION_SECTORBIT : 0) | \
		 ((morebits) ? XFS_SB_VERSION_MOREBITSBIT : 0)) : \
		XFS_SB_VERSION_1)

/*
 * There are two words to hold XFS "feature" bits: the original
 * word, sb_versionnum, and sb_features2.  Whenever a bit is set in
 * sb_features2, the feature bit XFS_SB_VERSION_MOREBITSBIT must be set.
 *
 * These defines represent bits in sb_features2.
 */
#define XFS_SB_VERSION2_REALFBITS	0x00ffffff	/* Mask: features */
#define XFS_SB_VERSION2_RESERVED1BIT	0x00000001
#define XFS_SB_VERSION2_SASHFBITS	0xff000000	/* Mask: features that
							   require changing
							   PROM and SASH */

#define	XFS_SB_VERSION2_OKREALFBITS	\
	(0)
#define	XFS_SB_VERSION2_OKSASHFBITS	\
	(0)
#define XFS_SB_VERSION2_OKREALBITS	\
	(XFS_SB_VERSION2_OKREALFBITS |	\
	 XFS_SB_VERSION2_OKSASHFBITS )

/*
 * mkfs macro to set up sb_features2 word
 */
#define	XFS_SB_VERSION2_MKFS(xyz)	\
	((xyz) ? 0 : 0)

typedef struct xfs_sb
{
	__uint32_t	sb_magicnum;	/* magic number == XFS_SB_MAGIC */
	__uint32_t	sb_blocksize;	/* logical block size, bytes */
	xfs_drfsbno_t	sb_dblocks;	/* number of data blocks */
	xfs_drfsbno_t	sb_rblocks;	/* number of realtime blocks */
	xfs_drtbno_t	sb_rextents;	/* number of realtime extents */
	uuid_t		sb_uuid;	/* file system unique id */
	xfs_dfsbno_t	sb_logstart;	/* starting block of log if internal */
	xfs_ino_t	sb_rootino;	/* root inode number */
	xfs_ino_t	sb_rbmino;	/* bitmap inode for realtime extents */
	xfs_ino_t	sb_rsumino;	/* summary inode for rt bitmap */
	xfs_agblock_t	sb_rextsize;	/* realtime extent size, blocks */
	xfs_agblock_t	sb_agblocks;	/* size of an allocation group */
	xfs_agnumber_t	sb_agcount;	/* number of allocation groups */
	xfs_extlen_t	sb_rbmblocks;	/* number of rt bitmap blocks */
	xfs_extlen_t	sb_logblocks;	/* number of log blocks */
	__uint16_t	sb_versionnum;	/* header version == XFS_SB_VERSION */
	__uint16_t	sb_sectsize;	/* volume sector size, bytes */
	__uint16_t	sb_inodesize;	/* inode size, bytes */
	__uint16_t	sb_inopblock;	/* inodes per block */
	char		sb_fname[12];	/* file system name */
	__uint8_t	sb_blocklog;	/* log2 of sb_blocksize */
	__uint8_t	sb_sectlog;	/* log2 of sb_sectsize */
	__uint8_t	sb_inodelog;	/* log2 of sb_inodesize */
	__uint8_t	sb_inopblog;	/* log2 of sb_inopblock */
	__uint8_t	sb_agblklog;	/* log2 of sb_agblocks (rounded up) */
	__uint8_t	sb_rextslog;	/* log2 of sb_rextents */
	__uint8_t	sb_inprogress;	/* mkfs is in progress, don't mount */
	__uint8_t	sb_imax_pct;	/* max % of fs for inode space */
					/* statistics */
	/*
	 * These fields must remain contiguous.  If you really
	 * want to change their layout, make sure you fix the
	 * code in xfs_trans_apply_sb_deltas().
	 */
	__uint64_t	sb_icount;	/* allocated inodes */
	__uint64_t	sb_ifree;	/* free inodes */
	__uint64_t	sb_fdblocks;	/* free data blocks */
	__uint64_t	sb_frextents;	/* free realtime extents */
	/*
	 * End contiguous fields.
	 */
	xfs_ino_t	sb_uquotino;	/* user quota inode */
	xfs_ino_t	sb_gquotino;	/* group quota inode */
	__uint16_t	sb_qflags;	/* quota flags */
	__uint8_t	sb_flags;	/* misc. flags */
	__uint8_t	sb_shared_vn;	/* shared version number */
	xfs_extlen_t	sb_inoalignmt;	/* inode chunk alignment, fsblocks */
	__uint32_t	sb_unit;	/* stripe or raid unit */
	__uint32_t	sb_width;	/* stripe or raid width */
	__uint8_t	sb_dirblklog;	/* log2 of dir block size (fsbs) */
	__uint8_t	sb_logsectlog;	/* log2 of the log sector size */
	__uint16_t	sb_logsectsize;	/* sector size for the log, bytes */
	__uint32_t	sb_logsunit;	/* stripe unit size for the log */
	__uint32_t	sb_features2;	/* additonal feature bits */
} xfs_sb_t;

/*
 * Sequence number values for the fields.
 */
typedef enum {
	XFS_SBS_MAGICNUM, XFS_SBS_BLOCKSIZE, XFS_SBS_DBLOCKS, XFS_SBS_RBLOCKS,
	XFS_SBS_REXTENTS, XFS_SBS_UUID, XFS_SBS_LOGSTART, XFS_SBS_ROOTINO,
	XFS_SBS_RBMINO, XFS_SBS_RSUMINO, XFS_SBS_REXTSIZE, XFS_SBS_AGBLOCKS,
	XFS_SBS_AGCOUNT, XFS_SBS_RBMBLOCKS, XFS_SBS_LOGBLOCKS,
	XFS_SBS_VERSIONNUM, XFS_SBS_SECTSIZE, XFS_SBS_INODESIZE,
	XFS_SBS_INOPBLOCK, XFS_SBS_FNAME, XFS_SBS_BLOCKLOG,
	XFS_SBS_SECTLOG, XFS_SBS_INODELOG, XFS_SBS_INOPBLOG, XFS_SBS_AGBLKLOG,
	XFS_SBS_REXTSLOG, XFS_SBS_INPROGRESS, XFS_SBS_IMAX_PCT, XFS_SBS_ICOUNT,
	XFS_SBS_IFREE, XFS_SBS_FDBLOCKS, XFS_SBS_FREXTENTS, XFS_SBS_UQUOTINO,
	XFS_SBS_GQUOTINO, XFS_SBS_QFLAGS, XFS_SBS_FLAGS, XFS_SBS_SHARED_VN,
	XFS_SBS_INOALIGNMT, XFS_SBS_UNIT, XFS_SBS_WIDTH, XFS_SBS_DIRBLKLOG,
	XFS_SBS_LOGSECTLOG, XFS_SBS_LOGSECTSIZE, XFS_SBS_LOGSUNIT,
	XFS_SBS_FEATURES2,
	XFS_SBS_FIELDCOUNT
} xfs_sb_field_t;

/*
 * Mask values, defined based on the xfs_sb_field_t values.
 * Only define the ones we're using.
 */
#define	XFS_SB_MVAL(x)		(1LL << XFS_SBS_ ## x)
#define	XFS_SB_UUID		XFS_SB_MVAL(UUID)
#define	XFS_SB_FNAME		XFS_SB_MVAL(FNAME)
#define	XFS_SB_ROOTINO		XFS_SB_MVAL(ROOTINO)
#define	XFS_SB_RBMINO		XFS_SB_MVAL(RBMINO)
#define	XFS_SB_RSUMINO		XFS_SB_MVAL(RSUMINO)
#define	XFS_SB_VERSIONNUM	XFS_SB_MVAL(VERSIONNUM)
#define XFS_SB_UQUOTINO		XFS_SB_MVAL(UQUOTINO)
#define XFS_SB_GQUOTINO		XFS_SB_MVAL(GQUOTINO)
#define XFS_SB_QFLAGS		XFS_SB_MVAL(QFLAGS)
#define XFS_SB_SHARED_VN	XFS_SB_MVAL(SHARED_VN)
#define XFS_SB_UNIT		XFS_SB_MVAL(UNIT)
#define XFS_SB_WIDTH		XFS_SB_MVAL(WIDTH)
#define	XFS_SB_NUM_BITS		((int)XFS_SBS_FIELDCOUNT)
#define	XFS_SB_ALL_BITS		((1LL << XFS_SB_NUM_BITS) - 1)
#define	XFS_SB_MOD_BITS		\
	(XFS_SB_UUID | XFS_SB_ROOTINO | XFS_SB_RBMINO | XFS_SB_RSUMINO | \
	 XFS_SB_VERSIONNUM | XFS_SB_UQUOTINO | XFS_SB_GQUOTINO | \
	 XFS_SB_QFLAGS | XFS_SB_SHARED_VN | XFS_SB_UNIT | XFS_SB_WIDTH)

/*
 * Misc. Flags - warning - these will be cleared by xfs_repair unless
 * a feature bit is set when the flag is used.
 */
#define XFS_SBF_NOFLAGS		0x00	/* no flags set */
#define XFS_SBF_READONLY	0x01	/* only read-only mounts allowed */

/*
 * define max. shared version we can interoperate with
 */
#define XFS_SB_MAX_SHARED_VN	0

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_NUM)
int xfs_sb_version_num(xfs_sb_t *sbp);
#define	XFS_SB_VERSION_NUM(sbp)	xfs_sb_version_num(sbp)
#else
#define	XFS_SB_VERSION_NUM(sbp)	((sbp)->sb_versionnum & XFS_SB_VERSION_NUMBITS)
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_GOOD_VERSION)
int xfs_sb_good_version(xfs_sb_t *sbp);
#define	XFS_SB_GOOD_VERSION(sbp)	xfs_sb_good_version(sbp)
#else
#define	XFS_SB_GOOD_VERSION_INT(sbp)	\
	((((sbp)->sb_versionnum >= XFS_SB_VERSION_1) && \
	  ((sbp)->sb_versionnum <= XFS_SB_VERSION_3)) || \
	   ((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) && \
	    !(((sbp)->sb_versionnum & ~XFS_SB_VERSION_OKREALBITS) || \
	      (((sbp)->sb_versionnum & XFS_SB_VERSION_MOREBITSBIT) && \
	       ((sbp)->sb_features2 & ~XFS_SB_VERSION2_OKREALBITS)))

#ifdef __KERNEL__
#define	XFS_SB_GOOD_VERSION(sbp)	\
	(XFS_SB_GOOD_VERSION_INT(sbp) && \
	  (sbp)->sb_shared_vn <= XFS_SB_MAX_SHARED_VN) ))
#else
/*
 * extra 2 paren's here (( to unconfuse paren-matching editors
 * like vi because XFS_SB_GOOD_VERSION_INT is a partial expression
 * and the two XFS_SB_GOOD_VERSION's each 2 more close paren's to
 * complete the expression.
 */
#define XFS_SB_GOOD_VERSION(sbp)	\
	(XFS_SB_GOOD_VERSION_INT(sbp) && \
	  (!((sbp)->sb_versionnum & XFS_SB_VERSION_SHAREDBIT) || \
	   (sbp)->sb_shared_vn <= XFS_SB_MAX_SHARED_VN)) ))
#endif /* __KERNEL__ */
#endif

#define	XFS_SB_GOOD_SASH_VERSION(sbp)	\
	((((sbp)->sb_versionnum >= XFS_SB_VERSION_1) && \
	  ((sbp)->sb_versionnum <= XFS_SB_VERSION_3)) || \
	 ((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) && \
	  !((sbp)->sb_versionnum & ~XFS_SB_VERSION_OKSASHBITS)))

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_TONEW)
unsigned xfs_sb_version_tonew(unsigned v);
#define	XFS_SB_VERSION_TONEW(v)	xfs_sb_version_tonew(v)
#else
#define	XFS_SB_VERSION_TONEW(v)	\
	((((v) == XFS_SB_VERSION_1) ? \
		0 : \
		(((v) == XFS_SB_VERSION_2) ? \
			XFS_SB_VERSION_ATTRBIT : \
			(XFS_SB_VERSION_ATTRBIT | XFS_SB_VERSION_NLINKBIT))) | \
	 XFS_SB_VERSION_4)
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_TOOLD)
unsigned xfs_sb_version_toold(unsigned v);
#define	XFS_SB_VERSION_TOOLD(v)	xfs_sb_version_toold(v)
#else
#define	XFS_SB_VERSION_TOOLD(v)	\
	(((v) & (XFS_SB_VERSION_QUOTABIT | XFS_SB_VERSION_ALIGNBIT)) ? \
		0 : \
		(((v) & XFS_SB_VERSION_NLINKBIT) ? \
			XFS_SB_VERSION_3 : \
			(((v) & XFS_SB_VERSION_ATTRBIT) ?  \
				XFS_SB_VERSION_2 : \
				XFS_SB_VERSION_1)))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_HASATTR)
int xfs_sb_version_hasattr(xfs_sb_t *sbp);
#define	XFS_SB_VERSION_HASATTR(sbp)	xfs_sb_version_hasattr(sbp)
#else
#define	XFS_SB_VERSION_HASATTR(sbp)	\
	(((sbp)->sb_versionnum == XFS_SB_VERSION_2) || \
	 ((sbp)->sb_versionnum == XFS_SB_VERSION_3) || \
	 ((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) && \
	  ((sbp)->sb_versionnum & XFS_SB_VERSION_ATTRBIT)))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_ADDATTR)
void xfs_sb_version_addattr(xfs_sb_t *sbp);
#define	XFS_SB_VERSION_ADDATTR(sbp)	xfs_sb_version_addattr(sbp)
#else
#define	XFS_SB_VERSION_ADDATTR(sbp)	\
	((sbp)->sb_versionnum = \
	 (((sbp)->sb_versionnum == XFS_SB_VERSION_1) ? \
		XFS_SB_VERSION_2 : \
		((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) ? \
			((sbp)->sb_versionnum | XFS_SB_VERSION_ATTRBIT) : \
			(XFS_SB_VERSION_4 | XFS_SB_VERSION_ATTRBIT))))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_HASNLINK)
int xfs_sb_version_hasnlink(xfs_sb_t *sbp);
#define	XFS_SB_VERSION_HASNLINK(sbp)	xfs_sb_version_hasnlink(sbp)
#else
#define	XFS_SB_VERSION_HASNLINK(sbp)	\
	(((sbp)->sb_versionnum == XFS_SB_VERSION_3) || \
	 ((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) && \
	  ((sbp)->sb_versionnum & XFS_SB_VERSION_NLINKBIT)))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_ADDNLINK)
void xfs_sb_version_addnlink(xfs_sb_t *sbp);
#define	XFS_SB_VERSION_ADDNLINK(sbp)	xfs_sb_version_addnlink(sbp)
#else
#define	XFS_SB_VERSION_ADDNLINK(sbp)	\
	((sbp)->sb_versionnum = \
	 ((sbp)->sb_versionnum <= XFS_SB_VERSION_2 ? \
		XFS_SB_VERSION_3 : \
		((sbp)->sb_versionnum | XFS_SB_VERSION_NLINKBIT)))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_HASQUOTA)
int xfs_sb_version_hasquota(xfs_sb_t *sbp);
#define	XFS_SB_VERSION_HASQUOTA(sbp)	xfs_sb_version_hasquota(sbp)
#else
#define	XFS_SB_VERSION_HASQUOTA(sbp)	\
	((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) && \
	 ((sbp)->sb_versionnum & XFS_SB_VERSION_QUOTABIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_ADDQUOTA)
void xfs_sb_version_addquota(xfs_sb_t *sbp);
#define	XFS_SB_VERSION_ADDQUOTA(sbp)	xfs_sb_version_addquota(sbp)
#else
#define	XFS_SB_VERSION_ADDQUOTA(sbp)	\
	((sbp)->sb_versionnum = \
	 (XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4 ? \
		((sbp)->sb_versionnum | XFS_SB_VERSION_QUOTABIT) : \
		(XFS_SB_VERSION_TONEW((sbp)->sb_versionnum) | \
		 XFS_SB_VERSION_QUOTABIT)))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_HASALIGN)
int xfs_sb_version_hasalign(xfs_sb_t *sbp);
#define	XFS_SB_VERSION_HASALIGN(sbp)	xfs_sb_version_hasalign(sbp)
#else
#define	XFS_SB_VERSION_HASALIGN(sbp)	\
	((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) && \
	 ((sbp)->sb_versionnum & XFS_SB_VERSION_ALIGNBIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_SUBALIGN)
void xfs_sb_version_subalign(xfs_sb_t *sbp);
#define	XFS_SB_VERSION_SUBALIGN(sbp)	xfs_sb_version_subalign(sbp)
#else
#define	XFS_SB_VERSION_SUBALIGN(sbp)	\
	((sbp)->sb_versionnum = \
	 XFS_SB_VERSION_TOOLD((sbp)->sb_versionnum & ~XFS_SB_VERSION_ALIGNBIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_HASDALIGN)
int xfs_sb_version_hasdalign(xfs_sb_t *sbp);
#define XFS_SB_VERSION_HASDALIGN(sbp)	xfs_sb_version_hasdalign(sbp)
#else
#define XFS_SB_VERSION_HASDALIGN(sbp)	\
	((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) && \
	 ((sbp)->sb_versionnum & XFS_SB_VERSION_DALIGNBIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_ADDDALIGN)
int xfs_sb_version_adddalign(xfs_sb_t *sbp);
#define XFS_SB_VERSION_ADDDALIGN(sbp)	xfs_sb_version_adddalign(sbp)
#else
#define XFS_SB_VERSION_ADDDALIGN(sbp)	\
	((sbp)->sb_versionnum = \
		((sbp)->sb_versionnum | XFS_SB_VERSION_DALIGNBIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_HASSHARED)
int xfs_sb_version_hasshared(xfs_sb_t *sbp);
#define XFS_SB_VERSION_HASSHARED(sbp)	xfs_sb_version_hasshared(sbp)
#else
#define XFS_SB_VERSION_HASSHARED(sbp)	\
	((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) && \
	 ((sbp)->sb_versionnum & XFS_SB_VERSION_SHAREDBIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_ADDSHARED)
int xfs_sb_version_addshared(xfs_sb_t *sbp);
#define XFS_SB_VERSION_ADDSHARED(sbp)	xfs_sb_version_addshared(sbp)
#else
#define XFS_SB_VERSION_ADDSHARED(sbp)	\
	((sbp)->sb_versionnum = \
		((sbp)->sb_versionnum | XFS_SB_VERSION_SHAREDBIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_SUBSHARED)
int xfs_sb_version_subshared(xfs_sb_t *sbp);
#define XFS_SB_VERSION_SUBSHARED(sbp)	xfs_sb_version_subshared(sbp)
#else
#define XFS_SB_VERSION_SUBSHARED(sbp)	\
	((sbp)->sb_versionnum = \
		((sbp)->sb_versionnum & ~XFS_SB_VERSION_SHAREDBIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_HASDIRV2)
int xfs_sb_version_hasdirv2(xfs_sb_t *sbp);
#define XFS_SB_VERSION_HASDIRV2(sbp)	xfs_sb_version_hasdirv2(sbp)
#else
#define XFS_SB_VERSION_HASDIRV2(sbp)	\
	((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) && \
	 ((sbp)->sb_versionnum & XFS_SB_VERSION_DIRV2BIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_HASLOGV2)
int xfs_sb_version_haslogv2(xfs_sb_t *sbp);
#define XFS_SB_VERSION_HASLOGV2(sbp)   xfs_sb_version_haslogv2(sbp)
#else
#define XFS_SB_VERSION_HASLOGV2(sbp)   \
	((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) && \
	((sbp)->sb_versionnum & XFS_SB_VERSION_LOGV2BIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_HASEXTFLGBIT)
int xfs_sb_version_hasextflgbit(xfs_sb_t *sbp);
#define XFS_SB_VERSION_HASEXTFLGBIT(sbp)	xfs_sb_version_hasextflgbit(sbp)
#else
#define XFS_SB_VERSION_HASEXTFLGBIT(sbp)	\
	((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) && \
	 ((sbp)->sb_versionnum & XFS_SB_VERSION_EXTFLGBIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_ADDEXTFLGBIT)
int xfs_sb_version_addextflgbit(xfs_sb_t *sbp);
#define XFS_SB_VERSION_ADDEXTFLGBIT(sbp)	xfs_sb_version_addextflgbit(sbp)
#else
#define XFS_SB_VERSION_ADDEXTFLGBIT(sbp)	\
	((sbp)->sb_versionnum = \
		((sbp)->sb_versionnum | XFS_SB_VERSION_EXTFLGBIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_SUBEXTFLGBIT)
int xfs_sb_version_subextflgbit(xfs_sb_t *sbp);
#define XFS_SB_VERSION_SUBEXTFLGBIT(sbp)	xfs_sb_version_subextflgbit(sbp)
#else
#define XFS_SB_VERSION_SUBEXTFLGBIT(sbp)	\
	((sbp)->sb_versionnum = \
		((sbp)->sb_versionnum & ~XFS_SB_VERSION_EXTFLGBIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_HASSECTOR)
int xfs_sb_version_hassector(xfs_sb_t *sbp);
#define XFS_SB_VERSION_HASSECTOR(sbp)   xfs_sb_version_hassector(sbp)
#else
#define XFS_SB_VERSION_HASSECTOR(sbp)   \
	((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) && \
	((sbp)->sb_versionnum & XFS_SB_VERSION_SECTORBIT))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_VERSION_HASMOREBITSBIT)
int xfs_sb_version_hasmorebits(xfs_sb_t *sbp);
#define XFS_SB_VERSION_HASMOREBITS(sbp)	xfs_sb_version_hasmorebits(sbp)
#else
#define XFS_SB_VERSION_HASMOREBITS(sbp)	\
	((XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4) && \
	 ((sbp)->sb_versionnum & XFS_SB_VERSION_MOREBITSBIT))
#endif

/*
 * sb_features2 bit version macros.
 *
 * For example, for a bit defined as XFS_SB_VERSION2_YBIT, has a macro:
 *
 * SB_VERSION_HASYBIT(xfs_sb_t *sbp)
 *	((XFS_SB_VERSION_HASMOREBITS(sbp) &&
 *	 ((sbp)->sb_versionnum & XFS_SB_VERSION2_YBIT)
 */

/*
 * end of superblock version macros
 */

#define XFS_SB_DADDR	((xfs_daddr_t)0)	/* daddr in filesystem/ag */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_BLOCK)
xfs_agblock_t xfs_sb_block(struct xfs_mount *mp);
#define	XFS_SB_BLOCK(mp)	xfs_sb_block(mp)
#else
#define	XFS_SB_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_SB_DADDR)
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_HDR_BLOCK)
xfs_agblock_t xfs_hdr_block(struct xfs_mount *mp, xfs_daddr_t d);
#define	XFS_HDR_BLOCK(mp,d)	xfs_hdr_block(mp,d)
#else
#define	XFS_HDR_BLOCK(mp,d)	((xfs_agblock_t)(XFS_BB_TO_FSBT(mp,d)))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DADDR_TO_FSB)
xfs_fsblock_t xfs_daddr_to_fsb(struct xfs_mount *mp, xfs_daddr_t d);
#define	XFS_DADDR_TO_FSB(mp,d)		xfs_daddr_to_fsb(mp,d)
#else
#define	XFS_DADDR_TO_FSB(mp,d) \
	XFS_AGB_TO_FSB(mp, XFS_DADDR_TO_AGNO(mp,d), XFS_DADDR_TO_AGBNO(mp,d))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_FSB_TO_DADDR)
xfs_daddr_t xfs_fsb_to_daddr(struct xfs_mount *mp, xfs_fsblock_t fsbno);
#define	XFS_FSB_TO_DADDR(mp,fsbno)	xfs_fsb_to_daddr(mp,fsbno)
#else
#define	XFS_FSB_TO_DADDR(mp,fsbno) \
	XFS_AGB_TO_DADDR(mp, XFS_FSB_TO_AGNO(mp,fsbno), \
			 XFS_FSB_TO_AGBNO(mp,fsbno))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_BUF_TO_SBP)
xfs_sb_t *xfs_buf_to_sbp(struct xfs_buf *bp);
#define XFS_BUF_TO_SBP(bp)	xfs_buf_to_sbp(bp)
#else
#define XFS_BUF_TO_SBP(bp)	((xfs_sb_t *)XFS_BUF_PTR(bp))
#endif

/*
 * File system sector to basic block conversions.
 */
#define XFS_FSS_TO_BB(mp,sec)	((sec) << (mp)->m_sectbb_log)
#define XFS_BB_TO_FSS(mp,bb)	\
	(((bb) + (XFS_FSS_TO_BB(mp,1) - 1)) >> (mp)->m_sectbb_log)
#define XFS_BB_TO_FSST(mp,bb)	((bb) >> (mp)->m_sectbb_log)

/*
 * File system sector to byte conversions.
 */
#define XFS_FSS_TO_B(mp,sectno)	((xfs_fsize_t)(sectno) << (mp)->m_sb.sb_sectlog)
#define XFS_B_TO_FSST(mp,b)	(((__uint64_t)(b)) >> (mp)->m_sb.sb_sectlog)

/*
 * File system block to basic block conversions.
 */
#define	XFS_FSB_TO_BB(mp,fsbno)	((fsbno) << (mp)->m_blkbb_log)
#define	XFS_BB_TO_FSB(mp,bb)	\
	(((bb) + (XFS_FSB_TO_BB(mp,1) - 1)) >> (mp)->m_blkbb_log)
#define	XFS_BB_TO_FSBT(mp,bb)	((bb) >> (mp)->m_blkbb_log)
#define	XFS_BB_FSB_OFFSET(mp,bb) ((bb) & ((mp)->m_bsize - 1))

/*
 * File system block to byte conversions.
 */
#define XFS_FSB_TO_B(mp,fsbno)	((xfs_fsize_t)(fsbno) << (mp)->m_sb.sb_blocklog)
#define XFS_B_TO_FSB(mp,b)	\
	((((__uint64_t)(b)) + (mp)->m_blockmask) >> (mp)->m_sb.sb_blocklog)
#define XFS_B_TO_FSBT(mp,b)	(((__uint64_t)(b)) >> (mp)->m_sb.sb_blocklog)
#define XFS_B_FSB_OFFSET(mp,b)	((b) & (mp)->m_blockmask)

#endif	/* __XFS_SB_H__ */
