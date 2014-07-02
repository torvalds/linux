/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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
#ifndef __XFS_SB_H__
#define	__XFS_SB_H__

/*
 * Super block
 * Fits into a sector-sized buffer at address 0 of each allocation group.
 * Only the first of these is ever updated except during growfs.
 */

struct xfs_buf;
struct xfs_mount;
struct xfs_trans;

#define	XFS_SB_MAGIC		0x58465342	/* 'XFSB' */
#define	XFS_SB_VERSION_1	1		/* 5.3, 6.0.1, 6.1 */
#define	XFS_SB_VERSION_2	2		/* 6.2 - attributes */
#define	XFS_SB_VERSION_3	3		/* 6.2 - new inode version */
#define	XFS_SB_VERSION_4	4		/* 6.2+ - bitmask version */
#define	XFS_SB_VERSION_5	5		/* CRC enabled filesystem */
#define	XFS_SB_VERSION_NUMBITS		0x000f
#define	XFS_SB_VERSION_ALLFBITS		0xfff0
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
#define	XFS_SB_VERSION_BORGBIT		0x4000	/* ASCII only case-insens. */
#define	XFS_SB_VERSION_MOREBITSBIT	0x8000

/*
 * Supported feature bit list is just all bits in the versionnum field because
 * we've used them all up and understand them all. Except, of course, for the
 * shared superblock bit, which nobody knows what it does and so is unsupported.
 */
#define	XFS_SB_VERSION_OKBITS		\
	((XFS_SB_VERSION_NUMBITS | XFS_SB_VERSION_ALLFBITS) & \
		~XFS_SB_VERSION_SHAREDBIT)

/*
 * There are two words to hold XFS "feature" bits: the original
 * word, sb_versionnum, and sb_features2.  Whenever a bit is set in
 * sb_features2, the feature bit XFS_SB_VERSION_MOREBITSBIT must be set.
 *
 * These defines represent bits in sb_features2.
 */
#define XFS_SB_VERSION2_RESERVED1BIT	0x00000001
#define XFS_SB_VERSION2_LAZYSBCOUNTBIT	0x00000002	/* Superblk counters */
#define XFS_SB_VERSION2_RESERVED4BIT	0x00000004
#define XFS_SB_VERSION2_ATTR2BIT	0x00000008	/* Inline attr rework */
#define XFS_SB_VERSION2_PARENTBIT	0x00000010	/* parent pointers */
#define XFS_SB_VERSION2_PROJID32BIT	0x00000080	/* 32 bit project id */
#define XFS_SB_VERSION2_CRCBIT		0x00000100	/* metadata CRCs */
#define XFS_SB_VERSION2_FTYPE		0x00000200	/* inode type in dir */

#define	XFS_SB_VERSION2_OKBITS		\
	(XFS_SB_VERSION2_LAZYSBCOUNTBIT	| \
	 XFS_SB_VERSION2_ATTR2BIT	| \
	 XFS_SB_VERSION2_PROJID32BIT	| \
	 XFS_SB_VERSION2_FTYPE)

/*
 * Superblock - in core version.  Must match the ondisk version below.
 * Must be padded to 64 bit alignment.
 */
typedef struct xfs_sb {
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
	__uint32_t	sb_features2;	/* additional feature bits */

	/*
	 * bad features2 field as a result of failing to pad the sb
	 * structure to 64 bits. Some machines will be using this field
	 * for features2 bits. Easiest just to mark it bad and not use
	 * it for anything else.
	 */
	__uint32_t	sb_bad_features2;

	/* version 5 superblock fields start here */

	/* feature masks */
	__uint32_t	sb_features_compat;
	__uint32_t	sb_features_ro_compat;
	__uint32_t	sb_features_incompat;
	__uint32_t	sb_features_log_incompat;

	__uint32_t	sb_crc;		/* superblock crc */
	__uint32_t	sb_pad;

	xfs_ino_t	sb_pquotino;	/* project quota inode */
	xfs_lsn_t	sb_lsn;		/* last write sequence */

	/* must be padded to 64 bit alignment */
} xfs_sb_t;

#define XFS_SB_CRC_OFF		offsetof(struct xfs_sb, sb_crc)

/*
 * Superblock - on disk version.  Must match the in core version above.
 * Must be padded to 64 bit alignment.
 */
typedef struct xfs_dsb {
	__be32		sb_magicnum;	/* magic number == XFS_SB_MAGIC */
	__be32		sb_blocksize;	/* logical block size, bytes */
	__be64		sb_dblocks;	/* number of data blocks */
	__be64		sb_rblocks;	/* number of realtime blocks */
	__be64		sb_rextents;	/* number of realtime extents */
	uuid_t		sb_uuid;	/* file system unique id */
	__be64		sb_logstart;	/* starting block of log if internal */
	__be64		sb_rootino;	/* root inode number */
	__be64		sb_rbmino;	/* bitmap inode for realtime extents */
	__be64		sb_rsumino;	/* summary inode for rt bitmap */
	__be32		sb_rextsize;	/* realtime extent size, blocks */
	__be32		sb_agblocks;	/* size of an allocation group */
	__be32		sb_agcount;	/* number of allocation groups */
	__be32		sb_rbmblocks;	/* number of rt bitmap blocks */
	__be32		sb_logblocks;	/* number of log blocks */
	__be16		sb_versionnum;	/* header version == XFS_SB_VERSION */
	__be16		sb_sectsize;	/* volume sector size, bytes */
	__be16		sb_inodesize;	/* inode size, bytes */
	__be16		sb_inopblock;	/* inodes per block */
	char		sb_fname[12];	/* file system name */
	__u8		sb_blocklog;	/* log2 of sb_blocksize */
	__u8		sb_sectlog;	/* log2 of sb_sectsize */
	__u8		sb_inodelog;	/* log2 of sb_inodesize */
	__u8		sb_inopblog;	/* log2 of sb_inopblock */
	__u8		sb_agblklog;	/* log2 of sb_agblocks (rounded up) */
	__u8		sb_rextslog;	/* log2 of sb_rextents */
	__u8		sb_inprogress;	/* mkfs is in progress, don't mount */
	__u8		sb_imax_pct;	/* max % of fs for inode space */
					/* statistics */
	/*
	 * These fields must remain contiguous.  If you really
	 * want to change their layout, make sure you fix the
	 * code in xfs_trans_apply_sb_deltas().
	 */
	__be64		sb_icount;	/* allocated inodes */
	__be64		sb_ifree;	/* free inodes */
	__be64		sb_fdblocks;	/* free data blocks */
	__be64		sb_frextents;	/* free realtime extents */
	/*
	 * End contiguous fields.
	 */
	__be64		sb_uquotino;	/* user quota inode */
	__be64		sb_gquotino;	/* group quota inode */
	__be16		sb_qflags;	/* quota flags */
	__u8		sb_flags;	/* misc. flags */
	__u8		sb_shared_vn;	/* shared version number */
	__be32		sb_inoalignmt;	/* inode chunk alignment, fsblocks */
	__be32		sb_unit;	/* stripe or raid unit */
	__be32		sb_width;	/* stripe or raid width */
	__u8		sb_dirblklog;	/* log2 of dir block size (fsbs) */
	__u8		sb_logsectlog;	/* log2 of the log sector size */
	__be16		sb_logsectsize;	/* sector size for the log, bytes */
	__be32		sb_logsunit;	/* stripe unit size for the log */
	__be32		sb_features2;	/* additional feature bits */
	/*
	 * bad features2 field as a result of failing to pad the sb
	 * structure to 64 bits. Some machines will be using this field
	 * for features2 bits. Easiest just to mark it bad and not use
	 * it for anything else.
	 */
	__be32		sb_bad_features2;

	/* version 5 superblock fields start here */

	/* feature masks */
	__be32		sb_features_compat;
	__be32		sb_features_ro_compat;
	__be32		sb_features_incompat;
	__be32		sb_features_log_incompat;

	__le32		sb_crc;		/* superblock crc */
	__be32		sb_pad;

	__be64		sb_pquotino;	/* project quota inode */
	__be64		sb_lsn;		/* last write sequence */

	/* must be padded to 64 bit alignment */
} xfs_dsb_t;

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
	XFS_SBS_FEATURES2, XFS_SBS_BAD_FEATURES2, XFS_SBS_FEATURES_COMPAT,
	XFS_SBS_FEATURES_RO_COMPAT, XFS_SBS_FEATURES_INCOMPAT,
	XFS_SBS_FEATURES_LOG_INCOMPAT, XFS_SBS_CRC, XFS_SBS_PAD,
	XFS_SBS_PQUOTINO, XFS_SBS_LSN,
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
#define XFS_SB_ICOUNT		XFS_SB_MVAL(ICOUNT)
#define XFS_SB_IFREE		XFS_SB_MVAL(IFREE)
#define XFS_SB_FDBLOCKS		XFS_SB_MVAL(FDBLOCKS)
#define XFS_SB_FEATURES2	XFS_SB_MVAL(FEATURES2)
#define XFS_SB_BAD_FEATURES2	XFS_SB_MVAL(BAD_FEATURES2)
#define XFS_SB_FEATURES_COMPAT	XFS_SB_MVAL(FEATURES_COMPAT)
#define XFS_SB_FEATURES_RO_COMPAT XFS_SB_MVAL(FEATURES_RO_COMPAT)
#define XFS_SB_FEATURES_INCOMPAT XFS_SB_MVAL(FEATURES_INCOMPAT)
#define XFS_SB_FEATURES_LOG_INCOMPAT XFS_SB_MVAL(FEATURES_LOG_INCOMPAT)
#define XFS_SB_CRC		XFS_SB_MVAL(CRC)
#define XFS_SB_PQUOTINO		XFS_SB_MVAL(PQUOTINO)
#define	XFS_SB_NUM_BITS		((int)XFS_SBS_FIELDCOUNT)
#define	XFS_SB_ALL_BITS		((1LL << XFS_SB_NUM_BITS) - 1)
#define	XFS_SB_MOD_BITS		\
	(XFS_SB_UUID | XFS_SB_ROOTINO | XFS_SB_RBMINO | XFS_SB_RSUMINO | \
	 XFS_SB_VERSIONNUM | XFS_SB_UQUOTINO | XFS_SB_GQUOTINO | \
	 XFS_SB_QFLAGS | XFS_SB_SHARED_VN | XFS_SB_UNIT | XFS_SB_WIDTH | \
	 XFS_SB_ICOUNT | XFS_SB_IFREE | XFS_SB_FDBLOCKS | XFS_SB_FEATURES2 | \
	 XFS_SB_BAD_FEATURES2 | XFS_SB_FEATURES_COMPAT | \
	 XFS_SB_FEATURES_RO_COMPAT | XFS_SB_FEATURES_INCOMPAT | \
	 XFS_SB_FEATURES_LOG_INCOMPAT | XFS_SB_PQUOTINO)


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

#define	XFS_SB_VERSION_NUM(sbp)	((sbp)->sb_versionnum & XFS_SB_VERSION_NUMBITS)

/*
 * The first XFS version we support is a v4 superblock with V2 directories.
 */
static inline bool xfs_sb_good_v4_features(struct xfs_sb *sbp)
{
	if (!(sbp->sb_versionnum & XFS_SB_VERSION_DIRV2BIT))
		return false;

	/* check for unknown features in the fs */
	if ((sbp->sb_versionnum & ~XFS_SB_VERSION_OKBITS) ||
	    ((sbp->sb_versionnum & XFS_SB_VERSION_MOREBITSBIT) &&
	     (sbp->sb_features2 & ~XFS_SB_VERSION2_OKBITS)))
		return false;

	return true;
}

static inline bool xfs_sb_good_version(struct xfs_sb *sbp)
{
	if (XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5)
		return true;
	if (XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_4)
		return xfs_sb_good_v4_features(sbp);
	return false;
}

/*
 * Detect a mismatched features2 field.  Older kernels read/wrote
 * this into the wrong slot, so to be safe we keep them in sync.
 */
static inline bool xfs_sb_has_mismatched_features2(struct xfs_sb *sbp)
{
	return sbp->sb_bad_features2 != sbp->sb_features2;
}

static inline bool xfs_sb_version_hasattr(struct xfs_sb *sbp)
{
	return (sbp->sb_versionnum & XFS_SB_VERSION_ATTRBIT);
}

static inline void xfs_sb_version_addattr(struct xfs_sb *sbp)
{
	sbp->sb_versionnum |= XFS_SB_VERSION_ATTRBIT;
}

static inline bool xfs_sb_version_hasquota(struct xfs_sb *sbp)
{
	return (sbp->sb_versionnum & XFS_SB_VERSION_QUOTABIT);
}

static inline void xfs_sb_version_addquota(struct xfs_sb *sbp)
{
	sbp->sb_versionnum |= XFS_SB_VERSION_QUOTABIT;
}

static inline bool xfs_sb_version_hasalign(struct xfs_sb *sbp)
{
	return (XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5 ||
		(sbp->sb_versionnum & XFS_SB_VERSION_ALIGNBIT));
}

static inline bool xfs_sb_version_hasdalign(struct xfs_sb *sbp)
{
	return (sbp->sb_versionnum & XFS_SB_VERSION_DALIGNBIT);
}

static inline bool xfs_sb_version_haslogv2(struct xfs_sb *sbp)
{
	return XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5 ||
	       (sbp->sb_versionnum & XFS_SB_VERSION_LOGV2BIT);
}

static inline bool xfs_sb_version_hasextflgbit(struct xfs_sb *sbp)
{
	return XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5 ||
	       (sbp->sb_versionnum & XFS_SB_VERSION_EXTFLGBIT);
}

static inline bool xfs_sb_version_hassector(struct xfs_sb *sbp)
{
	return (sbp->sb_versionnum & XFS_SB_VERSION_SECTORBIT);
}

static inline bool xfs_sb_version_hasasciici(struct xfs_sb *sbp)
{
	return (sbp->sb_versionnum & XFS_SB_VERSION_BORGBIT);
}

static inline bool xfs_sb_version_hasmorebits(struct xfs_sb *sbp)
{
	return XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5 ||
	       (sbp->sb_versionnum & XFS_SB_VERSION_MOREBITSBIT);
}

/*
 * sb_features2 bit version macros.
 */
static inline bool xfs_sb_version_haslazysbcount(struct xfs_sb *sbp)
{
	return (XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5) ||
	       (xfs_sb_version_hasmorebits(sbp) &&
		(sbp->sb_features2 & XFS_SB_VERSION2_LAZYSBCOUNTBIT));
}

static inline bool xfs_sb_version_hasattr2(struct xfs_sb *sbp)
{
	return (XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5) ||
	       (xfs_sb_version_hasmorebits(sbp) &&
		(sbp->sb_features2 & XFS_SB_VERSION2_ATTR2BIT));
}

static inline void xfs_sb_version_addattr2(struct xfs_sb *sbp)
{
	sbp->sb_versionnum |= XFS_SB_VERSION_MOREBITSBIT;
	sbp->sb_features2 |= XFS_SB_VERSION2_ATTR2BIT;
	sbp->sb_bad_features2 |= XFS_SB_VERSION2_ATTR2BIT;
}

static inline void xfs_sb_version_removeattr2(struct xfs_sb *sbp)
{
	sbp->sb_features2 &= ~XFS_SB_VERSION2_ATTR2BIT;
	sbp->sb_bad_features2 &= ~XFS_SB_VERSION2_ATTR2BIT;
	if (!sbp->sb_features2)
		sbp->sb_versionnum &= ~XFS_SB_VERSION_MOREBITSBIT;
}

static inline bool xfs_sb_version_hasprojid32bit(struct xfs_sb *sbp)
{
	return (XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5) ||
	       (xfs_sb_version_hasmorebits(sbp) &&
		(sbp->sb_features2 & XFS_SB_VERSION2_PROJID32BIT));
}

static inline void xfs_sb_version_addprojid32bit(struct xfs_sb *sbp)
{
	sbp->sb_versionnum |= XFS_SB_VERSION_MOREBITSBIT;
	sbp->sb_features2 |= XFS_SB_VERSION2_PROJID32BIT;
	sbp->sb_bad_features2 |= XFS_SB_VERSION2_PROJID32BIT;
}

/*
 * Extended v5 superblock feature masks. These are to be used for new v5
 * superblock features only.
 *
 * Compat features are new features that old kernels will not notice or affect
 * and so can mount read-write without issues.
 *
 * RO-Compat (read only) are features that old kernels can read but will break
 * if they write. Hence only read-only mounts of such filesystems are allowed on
 * kernels that don't support the feature bit.
 *
 * InCompat features are features which old kernels will not understand and so
 * must not mount.
 *
 * Log-InCompat features are for changes to log formats or new transactions that
 * can't be replayed on older kernels. The fields are set when the filesystem is
 * mounted, and a clean unmount clears the fields.
 */
#define XFS_SB_FEAT_COMPAT_ALL 0
#define XFS_SB_FEAT_COMPAT_UNKNOWN	~XFS_SB_FEAT_COMPAT_ALL
static inline bool
xfs_sb_has_compat_feature(
	struct xfs_sb	*sbp,
	__uint32_t	feature)
{
	return (sbp->sb_features_compat & feature) != 0;
}

#define XFS_SB_FEAT_RO_COMPAT_FINOBT   (1 << 0)		/* free inode btree */
#define XFS_SB_FEAT_RO_COMPAT_ALL \
		(XFS_SB_FEAT_RO_COMPAT_FINOBT)
#define XFS_SB_FEAT_RO_COMPAT_UNKNOWN	~XFS_SB_FEAT_RO_COMPAT_ALL
static inline bool
xfs_sb_has_ro_compat_feature(
	struct xfs_sb	*sbp,
	__uint32_t	feature)
{
	return (sbp->sb_features_ro_compat & feature) != 0;
}

#define XFS_SB_FEAT_INCOMPAT_FTYPE	(1 << 0)	/* filetype in dirent */
#define XFS_SB_FEAT_INCOMPAT_ALL \
		(XFS_SB_FEAT_INCOMPAT_FTYPE)

#define XFS_SB_FEAT_INCOMPAT_UNKNOWN	~XFS_SB_FEAT_INCOMPAT_ALL
static inline bool
xfs_sb_has_incompat_feature(
	struct xfs_sb	*sbp,
	__uint32_t	feature)
{
	return (sbp->sb_features_incompat & feature) != 0;
}

#define XFS_SB_FEAT_INCOMPAT_LOG_ALL 0
#define XFS_SB_FEAT_INCOMPAT_LOG_UNKNOWN	~XFS_SB_FEAT_INCOMPAT_LOG_ALL
static inline bool
xfs_sb_has_incompat_log_feature(
	struct xfs_sb	*sbp,
	__uint32_t	feature)
{
	return (sbp->sb_features_log_incompat & feature) != 0;
}

/*
 * V5 superblock specific feature checks
 */
static inline int xfs_sb_version_hascrc(struct xfs_sb *sbp)
{
	return XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5;
}

static inline int xfs_sb_version_has_pquotino(struct xfs_sb *sbp)
{
	return XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5;
}

static inline int xfs_sb_version_hasftype(struct xfs_sb *sbp)
{
	return (XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5 &&
		xfs_sb_has_incompat_feature(sbp, XFS_SB_FEAT_INCOMPAT_FTYPE)) ||
	       (xfs_sb_version_hasmorebits(sbp) &&
		 (sbp->sb_features2 & XFS_SB_VERSION2_FTYPE));
}

static inline int xfs_sb_version_hasfinobt(xfs_sb_t *sbp)
{
	return (XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5) &&
		(sbp->sb_features_ro_compat & XFS_SB_FEAT_RO_COMPAT_FINOBT);
}

/*
 * end of superblock version macros
 */

static inline bool
xfs_is_quota_inode(struct xfs_sb *sbp, xfs_ino_t ino)
{
	return (ino == sbp->sb_uquotino ||
		ino == sbp->sb_gquotino ||
		ino == sbp->sb_pquotino);
}

#define XFS_SB_DADDR		((xfs_daddr_t)0) /* daddr in filesystem/ag */
#define	XFS_SB_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_SB_DADDR)
#define XFS_BUF_TO_SBP(bp)	((xfs_dsb_t *)((bp)->b_addr))

#define	XFS_HDR_BLOCK(mp,d)	((xfs_agblock_t)XFS_BB_TO_FSBT(mp,d))
#define	XFS_DADDR_TO_FSB(mp,d)	XFS_AGB_TO_FSB(mp, \
			xfs_daddr_to_agno(mp,d), xfs_daddr_to_agbno(mp,d))
#define	XFS_FSB_TO_DADDR(mp,fsbno)	XFS_AGB_TO_DADDR(mp, \
			XFS_FSB_TO_AGNO(mp,fsbno), XFS_FSB_TO_AGBNO(mp,fsbno))

/*
 * File system sector to basic block conversions.
 */
#define XFS_FSS_TO_BB(mp,sec)	((sec) << (mp)->m_sectbb_log)

/*
 * File system block to basic block conversions.
 */
#define	XFS_FSB_TO_BB(mp,fsbno)	((fsbno) << (mp)->m_blkbb_log)
#define	XFS_BB_TO_FSB(mp,bb)	\
	(((bb) + (XFS_FSB_TO_BB(mp,1) - 1)) >> (mp)->m_blkbb_log)
#define	XFS_BB_TO_FSBT(mp,bb)	((bb) >> (mp)->m_blkbb_log)

/*
 * File system block to byte conversions.
 */
#define XFS_FSB_TO_B(mp,fsbno)	((xfs_fsize_t)(fsbno) << (mp)->m_sb.sb_blocklog)
#define XFS_B_TO_FSB(mp,b)	\
	((((__uint64_t)(b)) + (mp)->m_blockmask) >> (mp)->m_sb.sb_blocklog)
#define XFS_B_TO_FSBT(mp,b)	(((__uint64_t)(b)) >> (mp)->m_sb.sb_blocklog)
#define XFS_B_FSB_OFFSET(mp,b)	((b) & (mp)->m_blockmask)

/*
 * perag get/put wrappers for ref counting
 */
extern struct xfs_perag *xfs_perag_get(struct xfs_mount *, xfs_agnumber_t);
extern struct xfs_perag *xfs_perag_get_tag(struct xfs_mount *, xfs_agnumber_t,
					   int tag);
extern void	xfs_perag_put(struct xfs_perag *pag);
extern int	xfs_initialize_perag_data(struct xfs_mount *, xfs_agnumber_t);

extern void	xfs_sb_calc_crc(struct xfs_buf	*);
extern void	xfs_mod_sb(struct xfs_trans *, __int64_t);
extern void	xfs_sb_mount_common(struct xfs_mount *, struct xfs_sb *);
extern void	xfs_sb_from_disk(struct xfs_sb *, struct xfs_dsb *);
extern void	xfs_sb_to_disk(struct xfs_dsb *, struct xfs_sb *, __int64_t);
extern void	xfs_sb_quota_from_disk(struct xfs_sb *sbp);

#endif	/* __XFS_SB_H__ */
