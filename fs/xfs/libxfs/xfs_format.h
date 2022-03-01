/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_FORMAT_H__
#define __XFS_FORMAT_H__

/*
 * XFS On Disk Format Definitions
 *
 * This header file defines all the on-disk format definitions for
 * general XFS objects. Directory and attribute related objects are defined in
 * xfs_da_format.h, which log and log item formats are defined in
 * xfs_log_format.h. Everything else goes here.
 */

struct xfs_mount;
struct xfs_trans;
struct xfs_inode;
struct xfs_buf;
struct xfs_ifork;

/*
 * Super block
 * Fits into a sector-sized buffer at address 0 of each allocation group.
 * Only the first of these is ever updated except during growfs.
 */
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
 * The size of a single extended attribute on disk is limited by
 * the size of index values within the attribute entries themselves.
 * These are be16 fields, so we can only support attribute data
 * sizes up to 2^16 bytes in length.
 */
#define XFS_XATTR_SIZE_MAX (1 << 16)

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

/* Maximum size of the xfs filesystem label, no terminating NULL */
#define XFSLABEL_MAX			12

/*
 * Superblock - in core version.  Must match the ondisk version below.
 * Must be padded to 64 bit alignment.
 */
typedef struct xfs_sb {
	uint32_t	sb_magicnum;	/* magic number == XFS_SB_MAGIC */
	uint32_t	sb_blocksize;	/* logical block size, bytes */
	xfs_rfsblock_t	sb_dblocks;	/* number of data blocks */
	xfs_rfsblock_t	sb_rblocks;	/* number of realtime blocks */
	xfs_rtblock_t	sb_rextents;	/* number of realtime extents */
	uuid_t		sb_uuid;	/* user-visible file system unique id */
	xfs_fsblock_t	sb_logstart;	/* starting block of log if internal */
	xfs_ino_t	sb_rootino;	/* root inode number */
	xfs_ino_t	sb_rbmino;	/* bitmap inode for realtime extents */
	xfs_ino_t	sb_rsumino;	/* summary inode for rt bitmap */
	xfs_agblock_t	sb_rextsize;	/* realtime extent size, blocks */
	xfs_agblock_t	sb_agblocks;	/* size of an allocation group */
	xfs_agnumber_t	sb_agcount;	/* number of allocation groups */
	xfs_extlen_t	sb_rbmblocks;	/* number of rt bitmap blocks */
	xfs_extlen_t	sb_logblocks;	/* number of log blocks */
	uint16_t	sb_versionnum;	/* header version == XFS_SB_VERSION */
	uint16_t	sb_sectsize;	/* volume sector size, bytes */
	uint16_t	sb_inodesize;	/* inode size, bytes */
	uint16_t	sb_inopblock;	/* inodes per block */
	char		sb_fname[XFSLABEL_MAX]; /* file system name */
	uint8_t		sb_blocklog;	/* log2 of sb_blocksize */
	uint8_t		sb_sectlog;	/* log2 of sb_sectsize */
	uint8_t		sb_inodelog;	/* log2 of sb_inodesize */
	uint8_t		sb_inopblog;	/* log2 of sb_inopblock */
	uint8_t		sb_agblklog;	/* log2 of sb_agblocks (rounded up) */
	uint8_t		sb_rextslog;	/* log2 of sb_rextents */
	uint8_t		sb_inprogress;	/* mkfs is in progress, don't mount */
	uint8_t		sb_imax_pct;	/* max % of fs for inode space */
					/* statistics */
	/*
	 * These fields must remain contiguous.  If you really
	 * want to change their layout, make sure you fix the
	 * code in xfs_trans_apply_sb_deltas().
	 */
	uint64_t	sb_icount;	/* allocated inodes */
	uint64_t	sb_ifree;	/* free inodes */
	uint64_t	sb_fdblocks;	/* free data blocks */
	uint64_t	sb_frextents;	/* free realtime extents */
	/*
	 * End contiguous fields.
	 */
	xfs_ino_t	sb_uquotino;	/* user quota inode */
	xfs_ino_t	sb_gquotino;	/* group quota inode */
	uint16_t	sb_qflags;	/* quota flags */
	uint8_t		sb_flags;	/* misc. flags */
	uint8_t		sb_shared_vn;	/* shared version number */
	xfs_extlen_t	sb_inoalignmt;	/* inode chunk alignment, fsblocks */
	uint32_t	sb_unit;	/* stripe or raid unit */
	uint32_t	sb_width;	/* stripe or raid width */
	uint8_t		sb_dirblklog;	/* log2 of dir block size (fsbs) */
	uint8_t		sb_logsectlog;	/* log2 of the log sector size */
	uint16_t	sb_logsectsize;	/* sector size for the log, bytes */
	uint32_t	sb_logsunit;	/* stripe unit size for the log */
	uint32_t	sb_features2;	/* additional feature bits */

	/*
	 * bad features2 field as a result of failing to pad the sb structure to
	 * 64 bits. Some machines will be using this field for features2 bits.
	 * Easiest just to mark it bad and not use it for anything else.
	 *
	 * This is not kept up to date in memory; it is always overwritten by
	 * the value in sb_features2 when formatting the incore superblock to
	 * the disk buffer.
	 */
	uint32_t	sb_bad_features2;

	/* version 5 superblock fields start here */

	/* feature masks */
	uint32_t	sb_features_compat;
	uint32_t	sb_features_ro_compat;
	uint32_t	sb_features_incompat;
	uint32_t	sb_features_log_incompat;

	uint32_t	sb_crc;		/* superblock crc */
	xfs_extlen_t	sb_spino_align;	/* sparse inode chunk alignment */

	xfs_ino_t	sb_pquotino;	/* project quota inode */
	xfs_lsn_t	sb_lsn;		/* last write sequence */
	uuid_t		sb_meta_uuid;	/* metadata file system unique id */

	/* must be padded to 64 bit alignment */
} xfs_sb_t;

#define XFS_SB_CRC_OFF		offsetof(struct xfs_sb, sb_crc)

/*
 * Superblock - on disk version.  Must match the in core version above.
 * Must be padded to 64 bit alignment.
 */
struct xfs_dsb {
	__be32		sb_magicnum;	/* magic number == XFS_SB_MAGIC */
	__be32		sb_blocksize;	/* logical block size, bytes */
	__be64		sb_dblocks;	/* number of data blocks */
	__be64		sb_rblocks;	/* number of realtime blocks */
	__be64		sb_rextents;	/* number of realtime extents */
	uuid_t		sb_uuid;	/* user-visible file system unique id */
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
	char		sb_fname[XFSLABEL_MAX]; /* file system name */
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
	__be32		sb_spino_align;	/* sparse inode chunk alignment */

	__be64		sb_pquotino;	/* project quota inode */
	__be64		sb_lsn;		/* last write sequence */
	uuid_t		sb_meta_uuid;	/* metadata file system unique id */

	/* must be padded to 64 bit alignment */
};

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

static inline bool xfs_sb_is_v5(struct xfs_sb *sbp)
{
	return XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5;
}

/*
 * Detect a mismatched features2 field.  Older kernels read/wrote
 * this into the wrong slot, so to be safe we keep them in sync.
 */
static inline bool xfs_sb_has_mismatched_features2(struct xfs_sb *sbp)
{
	return sbp->sb_bad_features2 != sbp->sb_features2;
}

static inline bool xfs_sb_version_hasmorebits(struct xfs_sb *sbp)
{
	return xfs_sb_is_v5(sbp) ||
	       (sbp->sb_versionnum & XFS_SB_VERSION_MOREBITSBIT);
}

static inline void xfs_sb_version_addattr(struct xfs_sb *sbp)
{
	sbp->sb_versionnum |= XFS_SB_VERSION_ATTRBIT;
}

static inline void xfs_sb_version_addquota(struct xfs_sb *sbp)
{
	sbp->sb_versionnum |= XFS_SB_VERSION_QUOTABIT;
}

static inline void xfs_sb_version_addattr2(struct xfs_sb *sbp)
{
	sbp->sb_versionnum |= XFS_SB_VERSION_MOREBITSBIT;
	sbp->sb_features2 |= XFS_SB_VERSION2_ATTR2BIT;
}

static inline void xfs_sb_version_addprojid32(struct xfs_sb *sbp)
{
	sbp->sb_versionnum |= XFS_SB_VERSION_MOREBITSBIT;
	sbp->sb_features2 |= XFS_SB_VERSION2_PROJID32BIT;
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
	uint32_t	feature)
{
	return (sbp->sb_features_compat & feature) != 0;
}

#define XFS_SB_FEAT_RO_COMPAT_FINOBT   (1 << 0)		/* free inode btree */
#define XFS_SB_FEAT_RO_COMPAT_RMAPBT   (1 << 1)		/* reverse map btree */
#define XFS_SB_FEAT_RO_COMPAT_REFLINK  (1 << 2)		/* reflinked files */
#define XFS_SB_FEAT_RO_COMPAT_INOBTCNT (1 << 3)		/* inobt block counts */
#define XFS_SB_FEAT_RO_COMPAT_ALL \
		(XFS_SB_FEAT_RO_COMPAT_FINOBT | \
		 XFS_SB_FEAT_RO_COMPAT_RMAPBT | \
		 XFS_SB_FEAT_RO_COMPAT_REFLINK| \
		 XFS_SB_FEAT_RO_COMPAT_INOBTCNT)
#define XFS_SB_FEAT_RO_COMPAT_UNKNOWN	~XFS_SB_FEAT_RO_COMPAT_ALL
static inline bool
xfs_sb_has_ro_compat_feature(
	struct xfs_sb	*sbp,
	uint32_t	feature)
{
	return (sbp->sb_features_ro_compat & feature) != 0;
}

#define XFS_SB_FEAT_INCOMPAT_FTYPE	(1 << 0)	/* filetype in dirent */
#define XFS_SB_FEAT_INCOMPAT_SPINODES	(1 << 1)	/* sparse inode chunks */
#define XFS_SB_FEAT_INCOMPAT_META_UUID	(1 << 2)	/* metadata UUID */
#define XFS_SB_FEAT_INCOMPAT_BIGTIME	(1 << 3)	/* large timestamps */
#define XFS_SB_FEAT_INCOMPAT_NEEDSREPAIR (1 << 4)	/* needs xfs_repair */
#define XFS_SB_FEAT_INCOMPAT_ALL \
		(XFS_SB_FEAT_INCOMPAT_FTYPE|	\
		 XFS_SB_FEAT_INCOMPAT_SPINODES|	\
		 XFS_SB_FEAT_INCOMPAT_META_UUID| \
		 XFS_SB_FEAT_INCOMPAT_BIGTIME| \
		 XFS_SB_FEAT_INCOMPAT_NEEDSREPAIR)

#define XFS_SB_FEAT_INCOMPAT_UNKNOWN	~XFS_SB_FEAT_INCOMPAT_ALL
static inline bool
xfs_sb_has_incompat_feature(
	struct xfs_sb	*sbp,
	uint32_t	feature)
{
	return (sbp->sb_features_incompat & feature) != 0;
}

#define XFS_SB_FEAT_INCOMPAT_LOG_ALL 0
#define XFS_SB_FEAT_INCOMPAT_LOG_UNKNOWN	~XFS_SB_FEAT_INCOMPAT_LOG_ALL
static inline bool
xfs_sb_has_incompat_log_feature(
	struct xfs_sb	*sbp,
	uint32_t	feature)
{
	return (sbp->sb_features_log_incompat & feature) != 0;
}

static inline void
xfs_sb_remove_incompat_log_features(
	struct xfs_sb	*sbp)
{
	sbp->sb_features_log_incompat &= ~XFS_SB_FEAT_INCOMPAT_LOG_ALL;
}

static inline void
xfs_sb_add_incompat_log_features(
	struct xfs_sb	*sbp,
	unsigned int	features)
{
	sbp->sb_features_log_incompat |= features;
}


static inline bool
xfs_is_quota_inode(struct xfs_sb *sbp, xfs_ino_t ino)
{
	return (ino == sbp->sb_uquotino ||
		ino == sbp->sb_gquotino ||
		ino == sbp->sb_pquotino);
}

#define XFS_SB_DADDR		((xfs_daddr_t)0) /* daddr in filesystem/ag */
#define	XFS_SB_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_SB_DADDR)

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
	((((uint64_t)(b)) + (mp)->m_blockmask) >> (mp)->m_sb.sb_blocklog)
#define XFS_B_TO_FSBT(mp,b)	(((uint64_t)(b)) >> (mp)->m_sb.sb_blocklog)

/*
 * Allocation group header
 *
 * This is divided into three structures, placed in sequential 512-byte
 * buffers after a copy of the superblock (also in a 512-byte buffer).
 */
#define	XFS_AGF_MAGIC	0x58414746	/* 'XAGF' */
#define	XFS_AGI_MAGIC	0x58414749	/* 'XAGI' */
#define	XFS_AGFL_MAGIC	0x5841464c	/* 'XAFL' */
#define	XFS_AGF_VERSION	1
#define	XFS_AGI_VERSION	1

#define	XFS_AGF_GOOD_VERSION(v)	((v) == XFS_AGF_VERSION)
#define	XFS_AGI_GOOD_VERSION(v)	((v) == XFS_AGI_VERSION)

/*
 * Btree number 0 is bno, 1 is cnt, 2 is rmap. This value gives the size of the
 * arrays below.
 */
#define	XFS_BTNUM_AGF	((int)XFS_BTNUM_RMAPi + 1)

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
	 * Freespace and rmap information
	 */
	__be32		agf_roots[XFS_BTNUM_AGF];	/* root blocks */
	__be32		agf_levels[XFS_BTNUM_AGF];	/* btree levels */

	__be32		agf_flfirst;	/* first freelist block's index */
	__be32		agf_fllast;	/* last freelist block's index */
	__be32		agf_flcount;	/* count of blocks in freelist */
	__be32		agf_freeblks;	/* total free blocks */

	__be32		agf_longest;	/* longest free space */
	__be32		agf_btreeblks;	/* # of blocks held in AGF btrees */
	uuid_t		agf_uuid;	/* uuid of filesystem */

	__be32		agf_rmap_blocks;	/* rmapbt blocks used */
	__be32		agf_refcount_blocks;	/* refcountbt blocks used */

	__be32		agf_refcount_root;	/* refcount tree root block */
	__be32		agf_refcount_level;	/* refcount btree levels */

	/*
	 * reserve some contiguous space for future logged fields before we add
	 * the unlogged fields. This makes the range logging via flags and
	 * structure offsets much simpler.
	 */
	__be64		agf_spare64[14];

	/* unlogged fields, written during buffer writeback. */
	__be64		agf_lsn;	/* last write sequence */
	__be32		agf_crc;	/* crc of agf sector */
	__be32		agf_spare2;

	/* structure must be padded to 64 bit alignment */
} xfs_agf_t;

#define XFS_AGF_CRC_OFF		offsetof(struct xfs_agf, agf_crc)

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
#define	XFS_AGF_UUID		0x00001000
#define	XFS_AGF_RMAP_BLOCKS	0x00002000
#define	XFS_AGF_REFCOUNT_BLOCKS	0x00004000
#define	XFS_AGF_REFCOUNT_ROOT	0x00008000
#define	XFS_AGF_REFCOUNT_LEVEL	0x00010000
#define	XFS_AGF_SPARE64		0x00020000
#define	XFS_AGF_NUM_BITS	18
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
	{ XFS_AGF_BTREEBLKS,	"BTREEBLKS" }, \
	{ XFS_AGF_UUID,		"UUID" }, \
	{ XFS_AGF_RMAP_BLOCKS,	"RMAP_BLOCKS" }, \
	{ XFS_AGF_REFCOUNT_BLOCKS,	"REFCOUNT_BLOCKS" }, \
	{ XFS_AGF_REFCOUNT_ROOT,	"REFCOUNT_ROOT" }, \
	{ XFS_AGF_REFCOUNT_LEVEL,	"REFCOUNT_LEVEL" }, \
	{ XFS_AGF_SPARE64,	"SPARE64" }

/* disk block (xfs_daddr_t) in the AG */
#define XFS_AGF_DADDR(mp)	((xfs_daddr_t)(1 << (mp)->m_sectbb_log))
#define	XFS_AGF_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGF_DADDR(mp))

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
	/*
	 * This marks the end of logging region 1 and start of logging region 2.
	 */
	uuid_t		agi_uuid;	/* uuid of filesystem */
	__be32		agi_crc;	/* crc of agi sector */
	__be32		agi_pad32;
	__be64		agi_lsn;	/* last write sequence */

	__be32		agi_free_root; /* root of the free inode btree */
	__be32		agi_free_level;/* levels in free inode btree */

	__be32		agi_iblocks;	/* inobt blocks used */
	__be32		agi_fblocks;	/* finobt blocks used */

	/* structure must be padded to 64 bit alignment */
} xfs_agi_t;

#define XFS_AGI_CRC_OFF		offsetof(struct xfs_agi, agi_crc)

#define	XFS_AGI_MAGICNUM	(1 << 0)
#define	XFS_AGI_VERSIONNUM	(1 << 1)
#define	XFS_AGI_SEQNO		(1 << 2)
#define	XFS_AGI_LENGTH		(1 << 3)
#define	XFS_AGI_COUNT		(1 << 4)
#define	XFS_AGI_ROOT		(1 << 5)
#define	XFS_AGI_LEVEL		(1 << 6)
#define	XFS_AGI_FREECOUNT	(1 << 7)
#define	XFS_AGI_NEWINO		(1 << 8)
#define	XFS_AGI_DIRINO		(1 << 9)
#define	XFS_AGI_UNLINKED	(1 << 10)
#define	XFS_AGI_NUM_BITS_R1	11	/* end of the 1st agi logging region */
#define	XFS_AGI_ALL_BITS_R1	((1 << XFS_AGI_NUM_BITS_R1) - 1)
#define	XFS_AGI_FREE_ROOT	(1 << 11)
#define	XFS_AGI_FREE_LEVEL	(1 << 12)
#define	XFS_AGI_IBLOCKS		(1 << 13) /* both inobt/finobt block counters */
#define	XFS_AGI_NUM_BITS_R2	14

/* disk block (xfs_daddr_t) in the AG */
#define XFS_AGI_DADDR(mp)	((xfs_daddr_t)(2 << (mp)->m_sectbb_log))
#define	XFS_AGI_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGI_DADDR(mp))

/*
 * The third a.g. block contains the a.g. freelist, an array
 * of block pointers to blocks owned by the allocation btree code.
 */
#define XFS_AGFL_DADDR(mp)	((xfs_daddr_t)(3 << (mp)->m_sectbb_log))
#define	XFS_AGFL_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGFL_DADDR(mp))
#define	XFS_BUF_TO_AGFL(bp)	((struct xfs_agfl *)((bp)->b_addr))

struct xfs_agfl {
	__be32		agfl_magicnum;
	__be32		agfl_seqno;
	uuid_t		agfl_uuid;
	__be64		agfl_lsn;
	__be32		agfl_crc;
} __attribute__((packed));

#define XFS_AGFL_CRC_OFF	offsetof(struct xfs_agfl, agfl_crc)

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

/*
 * XFS Timestamps
 * ==============
 *
 * Traditional ondisk inode timestamps consist of signed 32-bit counters for
 * seconds and nanoseconds; time zero is the Unix epoch, Jan  1 00:00:00 UTC
 * 1970, which means that the timestamp epoch is the same as the Unix epoch.
 * Therefore, the ondisk min and max defined here can be used directly to
 * constrain the incore timestamps on a Unix system.  Note that we actually
 * encode a __be64 value on disk.
 *
 * When the bigtime feature is enabled, ondisk inode timestamps become an
 * unsigned 64-bit nanoseconds counter.  This means that the bigtime inode
 * timestamp epoch is the start of the classic timestamp range, which is
 * Dec 31 20:45:52 UTC 1901.  Because the epochs are not the same, callers
 * /must/ use the bigtime conversion functions when encoding and decoding raw
 * timestamps.
 */
typedef __be64 xfs_timestamp_t;

/* Legacy timestamp encoding format. */
struct xfs_legacy_timestamp {
	__be32		t_sec;		/* timestamp seconds */
	__be32		t_nsec;		/* timestamp nanoseconds */
};

/*
 * Smallest possible ondisk seconds value with traditional timestamps.  This
 * corresponds exactly with the incore timestamp Dec 13 20:45:52 UTC 1901.
 */
#define XFS_LEGACY_TIME_MIN	((int64_t)S32_MIN)

/*
 * Largest possible ondisk seconds value with traditional timestamps.  This
 * corresponds exactly with the incore timestamp Jan 19 03:14:07 UTC 2038.
 */
#define XFS_LEGACY_TIME_MAX	((int64_t)S32_MAX)

/*
 * Smallest possible ondisk seconds value with bigtime timestamps.  This
 * corresponds (after conversion to a Unix timestamp) with the traditional
 * minimum timestamp of Dec 13 20:45:52 UTC 1901.
 */
#define XFS_BIGTIME_TIME_MIN	((int64_t)0)

/*
 * Largest supported ondisk seconds value with bigtime timestamps.  This
 * corresponds (after conversion to a Unix timestamp) with an incore timestamp
 * of Jul  2 20:20:24 UTC 2486.
 *
 * We round down the ondisk limit so that the bigtime quota and inode max
 * timestamps will be the same.
 */
#define XFS_BIGTIME_TIME_MAX	((int64_t)((-1ULL / NSEC_PER_SEC) & ~0x3ULL))

/*
 * Bigtime epoch is set exactly to the minimum time value that a traditional
 * 32-bit timestamp can represent when using the Unix epoch as a reference.
 * Hence the Unix epoch is at a fixed offset into the supported bigtime
 * timestamp range.
 *
 * The bigtime epoch also matches the minimum value an on-disk 32-bit XFS
 * timestamp can represent so we will not lose any fidelity in converting
 * to/from unix and bigtime timestamps.
 *
 * The following conversion factor converts a seconds counter from the Unix
 * epoch to the bigtime epoch.
 */
#define XFS_BIGTIME_EPOCH_OFFSET	(-(int64_t)S32_MIN)

/* Convert a timestamp from the Unix epoch to the bigtime epoch. */
static inline uint64_t xfs_unix_to_bigtime(time64_t unix_seconds)
{
	return (uint64_t)unix_seconds + XFS_BIGTIME_EPOCH_OFFSET;
}

/* Convert a timestamp from the bigtime epoch to the Unix epoch. */
static inline time64_t xfs_bigtime_to_unix(uint64_t ondisk_seconds)
{
	return (time64_t)ondisk_seconds - XFS_BIGTIME_EPOCH_OFFSET;
}

/*
 * On-disk inode structure.
 *
 * This is just the header or "dinode core", the inode is expanded to fill a
 * variable size the leftover area split into a data and an attribute fork.
 * The format of the data and attribute fork depends on the format of the
 * inode as indicated by di_format and di_aformat.  To access the data and
 * attribute use the XFS_DFORK_DPTR, XFS_DFORK_APTR, and XFS_DFORK_PTR macros
 * below.
 *
 * There is a very similar struct xfs_log_dinode which matches the layout of
 * this structure, but is kept in native format instead of big endian.
 *
 * Note: di_flushiter is only used by v1/2 inodes - it's effectively a zeroed
 * padding field for v3 inodes.
 */
#define	XFS_DINODE_MAGIC		0x494e	/* 'IN' */
struct xfs_dinode {
	__be16		di_magic;	/* inode magic # = XFS_DINODE_MAGIC */
	__be16		di_mode;	/* mode and type of file */
	__u8		di_version;	/* inode version */
	__u8		di_format;	/* format of di_c data */
	__be16		di_onlink;	/* old number of links to file */
	__be32		di_uid;		/* owner's user id */
	__be32		di_gid;		/* owner's group id */
	__be32		di_nlink;	/* number of links to file */
	__be16		di_projid_lo;	/* lower part of owner's project id */
	__be16		di_projid_hi;	/* higher part owner's project id */
	__u8		di_pad[6];	/* unused, zeroed space */
	__be16		di_flushiter;	/* incremented on flush */
	xfs_timestamp_t	di_atime;	/* time last accessed */
	xfs_timestamp_t	di_mtime;	/* time last modified */
	xfs_timestamp_t	di_ctime;	/* time created/inode modified */
	__be64		di_size;	/* number of bytes in file */
	__be64		di_nblocks;	/* # of direct & btree blocks used */
	__be32		di_extsize;	/* basic/minimum extent size for file */
	__be32		di_nextents;	/* number of extents in data fork */
	__be16		di_anextents;	/* number of extents in attribute fork*/
	__u8		di_forkoff;	/* attr fork offs, <<3 for 64b align */
	__s8		di_aformat;	/* format of attr fork's data */
	__be32		di_dmevmask;	/* DMIG event mask */
	__be16		di_dmstate;	/* DMIG state info */
	__be16		di_flags;	/* random flags, XFS_DIFLAG_... */
	__be32		di_gen;		/* generation number */

	/* di_next_unlinked is the only non-core field in the old dinode */
	__be32		di_next_unlinked;/* agi unlinked list ptr */

	/* start of the extended dinode, writable fields */
	__le32		di_crc;		/* CRC of the inode */
	__be64		di_changecount;	/* number of attribute changes */
	__be64		di_lsn;		/* flush sequence */
	__be64		di_flags2;	/* more random flags */
	__be32		di_cowextsize;	/* basic cow extent size for file */
	__u8		di_pad2[12];	/* more padding for future expansion */

	/* fields only written to during inode creation */
	xfs_timestamp_t	di_crtime;	/* time created */
	__be64		di_ino;		/* inode number */
	uuid_t		di_uuid;	/* UUID of the filesystem */

	/* structure must be padded to 64 bit alignment */
};

#define XFS_DINODE_CRC_OFF	offsetof(struct xfs_dinode, di_crc)

#define DI_MAX_FLUSH 0xffff

/*
 * Size of the core inode on disk.  Version 1 and 2 inodes have
 * the same size, but version 3 has grown a few additional fields.
 */
static inline uint xfs_dinode_size(int version)
{
	if (version == 3)
		return sizeof(struct xfs_dinode);
	return offsetof(struct xfs_dinode, di_crc);
}

/*
 * The 32 bit link count in the inode theoretically maxes out at UINT_MAX.
 * Since the pathconf interface is signed, we use 2^31 - 1 instead.
 */
#define	XFS_MAXLINK		((1U << 31) - 1U)

/*
 * Values for di_format
 *
 * This enum is used in string mapping in xfs_trace.h; please keep the
 * TRACE_DEFINE_ENUMs for it up to date.
 */
enum xfs_dinode_fmt {
	XFS_DINODE_FMT_DEV,		/* xfs_dev_t */
	XFS_DINODE_FMT_LOCAL,		/* bulk data */
	XFS_DINODE_FMT_EXTENTS,		/* struct xfs_bmbt_rec */
	XFS_DINODE_FMT_BTREE,		/* struct xfs_bmdr_block */
	XFS_DINODE_FMT_UUID		/* added long ago, but never used */
};

#define XFS_INODE_FORMAT_STR \
	{ XFS_DINODE_FMT_DEV,		"dev" }, \
	{ XFS_DINODE_FMT_LOCAL,		"local" }, \
	{ XFS_DINODE_FMT_EXTENTS,	"extent" }, \
	{ XFS_DINODE_FMT_BTREE,		"btree" }, \
	{ XFS_DINODE_FMT_UUID,		"uuid" }

/*
 * Inode minimum and maximum sizes.
 */
#define	XFS_DINODE_MIN_LOG	8
#define	XFS_DINODE_MAX_LOG	11
#define	XFS_DINODE_MIN_SIZE	(1 << XFS_DINODE_MIN_LOG)
#define	XFS_DINODE_MAX_SIZE	(1 << XFS_DINODE_MAX_LOG)

/*
 * Inode size for given fs.
 */
#define XFS_DINODE_SIZE(mp) \
	(xfs_has_v3inodes(mp) ? \
		sizeof(struct xfs_dinode) : \
		offsetof(struct xfs_dinode, di_crc))
#define XFS_LITINO(mp) \
	((mp)->m_sb.sb_inodesize - XFS_DINODE_SIZE(mp))

/*
 * Inode data & attribute fork sizes, per inode.
 */
#define XFS_DFORK_BOFF(dip)		((int)((dip)->di_forkoff << 3))

#define XFS_DFORK_DSIZE(dip,mp) \
	((dip)->di_forkoff ? XFS_DFORK_BOFF(dip) : XFS_LITINO(mp))
#define XFS_DFORK_ASIZE(dip,mp) \
	((dip)->di_forkoff ? XFS_LITINO(mp) - XFS_DFORK_BOFF(dip) : 0)
#define XFS_DFORK_SIZE(dip,mp,w) \
	((w) == XFS_DATA_FORK ? \
		XFS_DFORK_DSIZE(dip, mp) : \
		XFS_DFORK_ASIZE(dip, mp))

#define XFS_DFORK_MAXEXT(dip, mp, w) \
	(XFS_DFORK_SIZE(dip, mp, w) / sizeof(struct xfs_bmbt_rec))

/*
 * Return pointers to the data or attribute forks.
 */
#define XFS_DFORK_DPTR(dip) \
	((char *)dip + xfs_dinode_size(dip->di_version))
#define XFS_DFORK_APTR(dip)	\
	(XFS_DFORK_DPTR(dip) + XFS_DFORK_BOFF(dip))
#define XFS_DFORK_PTR(dip,w)	\
	((w) == XFS_DATA_FORK ? XFS_DFORK_DPTR(dip) : XFS_DFORK_APTR(dip))

#define XFS_DFORK_FORMAT(dip,w) \
	((w) == XFS_DATA_FORK ? \
		(dip)->di_format : \
		(dip)->di_aformat)
#define XFS_DFORK_NEXTENTS(dip,w) \
	((w) == XFS_DATA_FORK ? \
		be32_to_cpu((dip)->di_nextents) : \
		be16_to_cpu((dip)->di_anextents))

/*
 * For block and character special files the 32bit dev_t is stored at the
 * beginning of the data fork.
 */
static inline xfs_dev_t xfs_dinode_get_rdev(struct xfs_dinode *dip)
{
	return be32_to_cpu(*(__be32 *)XFS_DFORK_DPTR(dip));
}

static inline void xfs_dinode_put_rdev(struct xfs_dinode *dip, xfs_dev_t rdev)
{
	*(__be32 *)XFS_DFORK_DPTR(dip) = cpu_to_be32(rdev);
}

/*
 * Values for di_flags
 */
#define XFS_DIFLAG_REALTIME_BIT  0	/* file's blocks come from rt area */
#define XFS_DIFLAG_PREALLOC_BIT  1	/* file space has been preallocated */
#define XFS_DIFLAG_NEWRTBM_BIT   2	/* for rtbitmap inode, new format */
#define XFS_DIFLAG_IMMUTABLE_BIT 3	/* inode is immutable */
#define XFS_DIFLAG_APPEND_BIT    4	/* inode is append-only */
#define XFS_DIFLAG_SYNC_BIT      5	/* inode is written synchronously */
#define XFS_DIFLAG_NOATIME_BIT   6	/* do not update atime */
#define XFS_DIFLAG_NODUMP_BIT    7	/* do not dump */
#define XFS_DIFLAG_RTINHERIT_BIT 8	/* create with realtime bit set */
#define XFS_DIFLAG_PROJINHERIT_BIT   9	/* create with parents projid */
#define XFS_DIFLAG_NOSYMLINKS_BIT   10	/* disallow symlink creation */
#define XFS_DIFLAG_EXTSIZE_BIT      11	/* inode extent size allocator hint */
#define XFS_DIFLAG_EXTSZINHERIT_BIT 12	/* inherit inode extent size */
#define XFS_DIFLAG_NODEFRAG_BIT     13	/* do not reorganize/defragment */
#define XFS_DIFLAG_FILESTREAM_BIT   14  /* use filestream allocator */
/* Do not use bit 15, di_flags is legacy and unchanging now */

#define XFS_DIFLAG_REALTIME      (1 << XFS_DIFLAG_REALTIME_BIT)
#define XFS_DIFLAG_PREALLOC      (1 << XFS_DIFLAG_PREALLOC_BIT)
#define XFS_DIFLAG_NEWRTBM       (1 << XFS_DIFLAG_NEWRTBM_BIT)
#define XFS_DIFLAG_IMMUTABLE     (1 << XFS_DIFLAG_IMMUTABLE_BIT)
#define XFS_DIFLAG_APPEND        (1 << XFS_DIFLAG_APPEND_BIT)
#define XFS_DIFLAG_SYNC          (1 << XFS_DIFLAG_SYNC_BIT)
#define XFS_DIFLAG_NOATIME       (1 << XFS_DIFLAG_NOATIME_BIT)
#define XFS_DIFLAG_NODUMP        (1 << XFS_DIFLAG_NODUMP_BIT)
#define XFS_DIFLAG_RTINHERIT     (1 << XFS_DIFLAG_RTINHERIT_BIT)
#define XFS_DIFLAG_PROJINHERIT   (1 << XFS_DIFLAG_PROJINHERIT_BIT)
#define XFS_DIFLAG_NOSYMLINKS    (1 << XFS_DIFLAG_NOSYMLINKS_BIT)
#define XFS_DIFLAG_EXTSIZE       (1 << XFS_DIFLAG_EXTSIZE_BIT)
#define XFS_DIFLAG_EXTSZINHERIT  (1 << XFS_DIFLAG_EXTSZINHERIT_BIT)
#define XFS_DIFLAG_NODEFRAG      (1 << XFS_DIFLAG_NODEFRAG_BIT)
#define XFS_DIFLAG_FILESTREAM    (1 << XFS_DIFLAG_FILESTREAM_BIT)

#define XFS_DIFLAG_ANY \
	(XFS_DIFLAG_REALTIME | XFS_DIFLAG_PREALLOC | XFS_DIFLAG_NEWRTBM | \
	 XFS_DIFLAG_IMMUTABLE | XFS_DIFLAG_APPEND | XFS_DIFLAG_SYNC | \
	 XFS_DIFLAG_NOATIME | XFS_DIFLAG_NODUMP | XFS_DIFLAG_RTINHERIT | \
	 XFS_DIFLAG_PROJINHERIT | XFS_DIFLAG_NOSYMLINKS | XFS_DIFLAG_EXTSIZE | \
	 XFS_DIFLAG_EXTSZINHERIT | XFS_DIFLAG_NODEFRAG | XFS_DIFLAG_FILESTREAM)

/*
 * Values for di_flags2 These start by being exposed to userspace in the upper
 * 16 bits of the XFS_XFLAG_s range.
 */
#define XFS_DIFLAG2_DAX_BIT	0	/* use DAX for this inode */
#define XFS_DIFLAG2_REFLINK_BIT	1	/* file's blocks may be shared */
#define XFS_DIFLAG2_COWEXTSIZE_BIT   2  /* copy on write extent size hint */
#define XFS_DIFLAG2_BIGTIME_BIT	3	/* big timestamps */

#define XFS_DIFLAG2_DAX		(1 << XFS_DIFLAG2_DAX_BIT)
#define XFS_DIFLAG2_REFLINK     (1 << XFS_DIFLAG2_REFLINK_BIT)
#define XFS_DIFLAG2_COWEXTSIZE  (1 << XFS_DIFLAG2_COWEXTSIZE_BIT)
#define XFS_DIFLAG2_BIGTIME	(1 << XFS_DIFLAG2_BIGTIME_BIT)

#define XFS_DIFLAG2_ANY \
	(XFS_DIFLAG2_DAX | XFS_DIFLAG2_REFLINK | XFS_DIFLAG2_COWEXTSIZE | \
	 XFS_DIFLAG2_BIGTIME)

static inline bool xfs_dinode_has_bigtime(const struct xfs_dinode *dip)
{
	return dip->di_version >= 3 &&
	       (dip->di_flags2 & cpu_to_be64(XFS_DIFLAG2_BIGTIME));
}

/*
 * Inode number format:
 * low inopblog bits - offset in block
 * next agblklog bits - block number in ag
 * next agno_log bits - ag number
 * high agno_log-agblklog-inopblog bits - 0
 */
#define	XFS_INO_MASK(k)			(uint32_t)((1ULL << (k)) - 1)
#define	XFS_INO_OFFSET_BITS(mp)		(mp)->m_sb.sb_inopblog
#define	XFS_INO_AGBNO_BITS(mp)		(mp)->m_sb.sb_agblklog
#define	XFS_INO_AGINO_BITS(mp)		((mp)->m_ino_geo.agino_log)
#define	XFS_INO_AGNO_BITS(mp)		(mp)->m_agno_log
#define	XFS_INO_BITS(mp)		\
	XFS_INO_AGNO_BITS(mp) + XFS_INO_AGINO_BITS(mp)
#define	XFS_INO_TO_AGNO(mp,i)		\
	((xfs_agnumber_t)((i) >> XFS_INO_AGINO_BITS(mp)))
#define	XFS_INO_TO_AGINO(mp,i)		\
	((xfs_agino_t)(i) & XFS_INO_MASK(XFS_INO_AGINO_BITS(mp)))
#define	XFS_INO_TO_AGBNO(mp,i)		\
	(((xfs_agblock_t)(i) >> XFS_INO_OFFSET_BITS(mp)) & \
		XFS_INO_MASK(XFS_INO_AGBNO_BITS(mp)))
#define	XFS_INO_TO_OFFSET(mp,i)		\
	((int)(i) & XFS_INO_MASK(XFS_INO_OFFSET_BITS(mp)))
#define	XFS_INO_TO_FSB(mp,i)		\
	XFS_AGB_TO_FSB(mp, XFS_INO_TO_AGNO(mp,i), XFS_INO_TO_AGBNO(mp,i))
#define	XFS_AGINO_TO_INO(mp,a,i)	\
	(((xfs_ino_t)(a) << XFS_INO_AGINO_BITS(mp)) | (i))
#define	XFS_AGINO_TO_AGBNO(mp,i)	((i) >> XFS_INO_OFFSET_BITS(mp))
#define	XFS_AGINO_TO_OFFSET(mp,i)	\
	((i) & XFS_INO_MASK(XFS_INO_OFFSET_BITS(mp)))
#define	XFS_OFFBNO_TO_AGINO(mp,b,o)	\
	((xfs_agino_t)(((b) << XFS_INO_OFFSET_BITS(mp)) | (o)))
#define	XFS_FSB_TO_INO(mp, b)	((xfs_ino_t)((b) << XFS_INO_OFFSET_BITS(mp)))
#define	XFS_AGB_TO_AGINO(mp, b)	((xfs_agino_t)((b) << XFS_INO_OFFSET_BITS(mp)))

#define	XFS_MAXINUMBER		((xfs_ino_t)((1ULL << 56) - 1ULL))
#define	XFS_MAXINUMBER_32	((xfs_ino_t)((1ULL << 32) - 1ULL))

/*
 * RealTime Device format definitions
 */

/* Min and max rt extent sizes, specified in bytes */
#define	XFS_MAX_RTEXTSIZE	(1024 * 1024 * 1024)	/* 1GB */
#define	XFS_DFL_RTEXTSIZE	(64 * 1024)	        /* 64kB */
#define	XFS_MIN_RTEXTSIZE	(4 * 1024)		/* 4kB */

#define	XFS_BLOCKSIZE(mp)	((mp)->m_sb.sb_blocksize)
#define	XFS_BLOCKMASK(mp)	((mp)->m_blockmask)
#define	XFS_BLOCKWSIZE(mp)	((mp)->m_blockwsize)
#define	XFS_BLOCKWMASK(mp)	((mp)->m_blockwmask)

/*
 * RT Summary and bit manipulation macros.
 */
#define	XFS_SUMOFFS(mp,ls,bb)	((int)((ls) * (mp)->m_sb.sb_rbmblocks + (bb)))
#define	XFS_SUMOFFSTOBLOCK(mp,s)	\
	(((s) * (uint)sizeof(xfs_suminfo_t)) >> (mp)->m_sb.sb_blocklog)
#define	XFS_SUMPTR(mp,bp,so)	\
	((xfs_suminfo_t *)((bp)->b_addr + \
		(((so) * (uint)sizeof(xfs_suminfo_t)) & XFS_BLOCKMASK(mp))))

#define	XFS_BITTOBLOCK(mp,bi)	((bi) >> (mp)->m_blkbit_log)
#define	XFS_BLOCKTOBIT(mp,bb)	((bb) << (mp)->m_blkbit_log)
#define	XFS_BITTOWORD(mp,bi)	\
	((int)(((bi) >> XFS_NBWORDLOG) & XFS_BLOCKWMASK(mp)))

#define	XFS_RTMIN(a,b)	((a) < (b) ? (a) : (b))
#define	XFS_RTMAX(a,b)	((a) > (b) ? (a) : (b))

#define	XFS_RTLOBIT(w)	xfs_lowbit32(w)
#define	XFS_RTHIBIT(w)	xfs_highbit32(w)

#define	XFS_RTBLOCKLOG(b)	xfs_highbit64(b)

/*
 * Dquot and dquot block format definitions
 */
#define XFS_DQUOT_MAGIC		0x4451		/* 'DQ' */
#define XFS_DQUOT_VERSION	(uint8_t)0x01	/* latest version number */

#define XFS_DQTYPE_USER		0x01		/* user dquot record */
#define XFS_DQTYPE_PROJ		0x02		/* project dquot record */
#define XFS_DQTYPE_GROUP	0x04		/* group dquot record */
#define XFS_DQTYPE_BIGTIME	0x80		/* large expiry timestamps */

/* bitmask to determine if this is a user/group/project dquot */
#define XFS_DQTYPE_REC_MASK	(XFS_DQTYPE_USER | \
				 XFS_DQTYPE_PROJ | \
				 XFS_DQTYPE_GROUP)

#define XFS_DQTYPE_ANY		(XFS_DQTYPE_REC_MASK | \
				 XFS_DQTYPE_BIGTIME)

/*
 * XFS Quota Timers
 * ================
 *
 * Traditional quota grace period expiration timers are an unsigned 32-bit
 * seconds counter; time zero is the Unix epoch, Jan  1 00:00:01 UTC 1970.
 * Note that an expiration value of zero means that the quota limit has not
 * been reached, and therefore no expiration has been set.  Therefore, the
 * ondisk min and max defined here can be used directly to constrain the incore
 * quota expiration timestamps on a Unix system.
 *
 * When bigtime is enabled, we trade two bits of precision to expand the
 * expiration timeout range to match that of big inode timestamps.  The min and
 * max recorded here are the on-disk limits, not a Unix timestamp.
 *
 * The grace period for each quota type is stored in the root dquot (id = 0)
 * and is applied to a non-root dquot when it exceeds the soft or hard limits.
 * The length of quota grace periods are unsigned 32-bit quantities measured in
 * units of seconds.  A value of zero means to use the default period.
 */

/*
 * Smallest possible ondisk quota expiration value with traditional timestamps.
 * This corresponds exactly with the incore expiration Jan  1 00:00:01 UTC 1970.
 */
#define XFS_DQ_LEGACY_EXPIRY_MIN	((int64_t)1)

/*
 * Largest possible ondisk quota expiration value with traditional timestamps.
 * This corresponds exactly with the incore expiration Feb  7 06:28:15 UTC 2106.
 */
#define XFS_DQ_LEGACY_EXPIRY_MAX	((int64_t)U32_MAX)

/*
 * Smallest possible ondisk quota expiration value with bigtime timestamps.
 * This corresponds (after conversion to a Unix timestamp) with the incore
 * expiration of Jan  1 00:00:04 UTC 1970.
 */
#define XFS_DQ_BIGTIME_EXPIRY_MIN	(XFS_DQ_LEGACY_EXPIRY_MIN)

/*
 * Largest supported ondisk quota expiration value with bigtime timestamps.
 * This corresponds (after conversion to a Unix timestamp) with an incore
 * expiration of Jul  2 20:20:24 UTC 2486.
 *
 * The ondisk field supports values up to -1U, which corresponds to an incore
 * expiration in 2514.  This is beyond the maximum the bigtime inode timestamp,
 * so we cap the maximum bigtime quota expiration to the max inode timestamp.
 */
#define XFS_DQ_BIGTIME_EXPIRY_MAX	((int64_t)4074815106U)

/*
 * The following conversion factors assist in converting a quota expiration
 * timestamp between the incore and ondisk formats.
 */
#define XFS_DQ_BIGTIME_SHIFT	(2)
#define XFS_DQ_BIGTIME_SLACK	((int64_t)(1ULL << XFS_DQ_BIGTIME_SHIFT) - 1)

/* Convert an incore quota expiration timestamp to an ondisk bigtime value. */
static inline uint32_t xfs_dq_unix_to_bigtime(time64_t unix_seconds)
{
	/*
	 * Round the expiration timestamp up to the nearest bigtime timestamp
	 * that we can store, to give users the most time to fix problems.
	 */
	return ((uint64_t)unix_seconds + XFS_DQ_BIGTIME_SLACK) >>
			XFS_DQ_BIGTIME_SHIFT;
}

/* Convert an ondisk bigtime quota expiration value to an incore timestamp. */
static inline time64_t xfs_dq_bigtime_to_unix(uint32_t ondisk_seconds)
{
	return (time64_t)ondisk_seconds << XFS_DQ_BIGTIME_SHIFT;
}

/*
 * Default quota grace periods, ranging from zero (use the compiled defaults)
 * to ~136 years.  These are applied to a non-root dquot that has exceeded
 * either limit.
 */
#define XFS_DQ_GRACE_MIN		((int64_t)0)
#define XFS_DQ_GRACE_MAX		((int64_t)U32_MAX)

/*
 * This is the main portion of the on-disk representation of quota information
 * for a user.  We pad this with some more expansion room to construct the on
 * disk structure.
 */
struct xfs_disk_dquot {
	__be16		d_magic;	/* dquot magic = XFS_DQUOT_MAGIC */
	__u8		d_version;	/* dquot version */
	__u8		d_type;		/* XFS_DQTYPE_USER/PROJ/GROUP */
	__be32		d_id;		/* user,project,group id */
	__be64		d_blk_hardlimit;/* absolute limit on disk blks */
	__be64		d_blk_softlimit;/* preferred limit on disk blks */
	__be64		d_ino_hardlimit;/* maximum # allocated inodes */
	__be64		d_ino_softlimit;/* preferred inode limit */
	__be64		d_bcount;	/* disk blocks owned by the user */
	__be64		d_icount;	/* inodes owned by the user */
	__be32		d_itimer;	/* zero if within inode limits if not,
					   this is when we refuse service */
	__be32		d_btimer;	/* similar to above; for disk blocks */
	__be16		d_iwarns;	/* warnings issued wrt num inodes */
	__be16		d_bwarns;	/* warnings issued wrt disk blocks */
	__be32		d_pad0;		/* 64 bit align */
	__be64		d_rtb_hardlimit;/* absolute limit on realtime blks */
	__be64		d_rtb_softlimit;/* preferred limit on RT disk blks */
	__be64		d_rtbcount;	/* realtime blocks owned */
	__be32		d_rtbtimer;	/* similar to above; for RT disk blocks */
	__be16		d_rtbwarns;	/* warnings issued wrt RT disk blocks */
	__be16		d_pad;
};

/*
 * This is what goes on disk. This is separated from the xfs_disk_dquot because
 * carrying the unnecessary padding would be a waste of memory.
 */
struct xfs_dqblk {
	struct xfs_disk_dquot	dd_diskdq; /* portion living incore as well */
	char			dd_fill[4];/* filling for posterity */

	/*
	 * These two are only present on filesystems with the CRC bits set.
	 */
	__be32		  dd_crc;	/* checksum */
	__be64		  dd_lsn;	/* last modification in log */
	uuid_t		  dd_uuid;	/* location information */
};

#define XFS_DQUOT_CRC_OFF	offsetof(struct xfs_dqblk, dd_crc)

/*
 * This defines the unit of allocation of dquots.
 *
 * Currently, it is just one file system block, and a 4K blk contains 30
 * (136 * 30 = 4080) dquots. It's probably not worth trying to make
 * this more dynamic.
 *
 * However, if this number is changed, we have to make sure that we don't
 * implicitly assume that we do allocations in chunks of a single filesystem
 * block in the dquot/xqm code.
 *
 * This is part of the ondisk format because the structure size is not a power
 * of two, which leaves slack at the end of the disk block.
 */
#define XFS_DQUOT_CLUSTER_SIZE_FSB	(xfs_filblks_t)1

/*
 * Remote symlink format and access functions.
 */
#define XFS_SYMLINK_MAGIC	0x58534c4d	/* XSLM */

struct xfs_dsymlink_hdr {
	__be32	sl_magic;
	__be32	sl_offset;
	__be32	sl_bytes;
	__be32	sl_crc;
	uuid_t	sl_uuid;
	__be64	sl_owner;
	__be64	sl_blkno;
	__be64	sl_lsn;
};

#define XFS_SYMLINK_CRC_OFF	offsetof(struct xfs_dsymlink_hdr, sl_crc)

#define XFS_SYMLINK_MAXLEN	1024
/*
 * The maximum pathlen is 1024 bytes. Since the minimum file system
 * blocksize is 512 bytes, we can get a max of 3 extents back from
 * bmapi when crc headers are taken into account.
 */
#define XFS_SYMLINK_MAPS 3

#define XFS_SYMLINK_BUF_SPACE(mp, bufsize)	\
	((bufsize) - (xfs_has_crc((mp)) ? \
			sizeof(struct xfs_dsymlink_hdr) : 0))


/*
 * Allocation Btree format definitions
 *
 * There are two on-disk btrees, one sorted by blockno and one sorted
 * by blockcount and blockno.  All blocks look the same to make the code
 * simpler; if we have time later, we'll make the optimizations.
 */
#define	XFS_ABTB_MAGIC		0x41425442	/* 'ABTB' for bno tree */
#define	XFS_ABTB_CRC_MAGIC	0x41423342	/* 'AB3B' */
#define	XFS_ABTC_MAGIC		0x41425443	/* 'ABTC' for cnt tree */
#define	XFS_ABTC_CRC_MAGIC	0x41423343	/* 'AB3C' */

/*
 * Data record/key structure
 */
typedef struct xfs_alloc_rec {
	__be32		ar_startblock;	/* starting block number */
	__be32		ar_blockcount;	/* count of free blocks */
} xfs_alloc_rec_t, xfs_alloc_key_t;

typedef struct xfs_alloc_rec_incore {
	xfs_agblock_t	ar_startblock;	/* starting block number */
	xfs_extlen_t	ar_blockcount;	/* count of free blocks */
} xfs_alloc_rec_incore_t;

/* btree pointer type */
typedef __be32 xfs_alloc_ptr_t;

/*
 * Block numbers in the AG:
 * SB is sector 0, AGF is sector 1, AGI is sector 2, AGFL is sector 3.
 */
#define	XFS_BNO_BLOCK(mp)	((xfs_agblock_t)(XFS_AGFL_BLOCK(mp) + 1))
#define	XFS_CNT_BLOCK(mp)	((xfs_agblock_t)(XFS_BNO_BLOCK(mp) + 1))


/*
 * Inode Allocation Btree format definitions
 *
 * There is a btree for the inode map per allocation group.
 */
#define	XFS_IBT_MAGIC		0x49414254	/* 'IABT' */
#define	XFS_IBT_CRC_MAGIC	0x49414233	/* 'IAB3' */
#define	XFS_FIBT_MAGIC		0x46494254	/* 'FIBT' */
#define	XFS_FIBT_CRC_MAGIC	0x46494233	/* 'FIB3' */

typedef uint64_t	xfs_inofree_t;
#define	XFS_INODES_PER_CHUNK		(NBBY * sizeof(xfs_inofree_t))
#define	XFS_INODES_PER_CHUNK_LOG	(XFS_NBBYLOG + 3)
#define	XFS_INOBT_ALL_FREE		((xfs_inofree_t)-1)
#define	XFS_INOBT_MASK(i)		((xfs_inofree_t)1 << (i))

#define XFS_INOBT_HOLEMASK_FULL		0	/* holemask for full chunk */
#define XFS_INOBT_HOLEMASK_BITS		(NBBY * sizeof(uint16_t))
#define XFS_INODES_PER_HOLEMASK_BIT	\
	(XFS_INODES_PER_CHUNK / (NBBY * sizeof(uint16_t)))

static inline xfs_inofree_t xfs_inobt_maskn(int i, int n)
{
	return ((n >= XFS_INODES_PER_CHUNK ? 0 : XFS_INOBT_MASK(n)) - 1) << i;
}

/*
 * The on-disk inode record structure has two formats. The original "full"
 * format uses a 4-byte freecount. The "sparse" format uses a 1-byte freecount
 * and replaces the 3 high-order freecount bytes wth the holemask and inode
 * count.
 *
 * The holemask of the sparse record format allows an inode chunk to have holes
 * that refer to blocks not owned by the inode record. This facilitates inode
 * allocation in the event of severe free space fragmentation.
 */
typedef struct xfs_inobt_rec {
	__be32		ir_startino;	/* starting inode number */
	union {
		struct {
			__be32	ir_freecount;	/* count of free inodes */
		} f;
		struct {
			__be16	ir_holemask;/* hole mask for sparse chunks */
			__u8	ir_count;	/* total inode count */
			__u8	ir_freecount;	/* count of free inodes */
		} sp;
	} ir_u;
	__be64		ir_free;	/* free inode mask */
} xfs_inobt_rec_t;

typedef struct xfs_inobt_rec_incore {
	xfs_agino_t	ir_startino;	/* starting inode number */
	uint16_t	ir_holemask;	/* hole mask for sparse chunks */
	uint8_t		ir_count;	/* total inode count */
	uint8_t		ir_freecount;	/* count of free inodes (set bits) */
	xfs_inofree_t	ir_free;	/* free inode mask */
} xfs_inobt_rec_incore_t;

static inline bool xfs_inobt_issparse(uint16_t holemask)
{
	/* non-zero holemask represents a sparse rec. */
	return holemask;
}

/*
 * Key structure
 */
typedef struct xfs_inobt_key {
	__be32		ir_startino;	/* starting inode number */
} xfs_inobt_key_t;

/* btree pointer type */
typedef __be32 xfs_inobt_ptr_t;

/*
 * block numbers in the AG.
 */
#define	XFS_IBT_BLOCK(mp)		((xfs_agblock_t)(XFS_CNT_BLOCK(mp) + 1))
#define	XFS_FIBT_BLOCK(mp)		((xfs_agblock_t)(XFS_IBT_BLOCK(mp) + 1))

/*
 * Reverse mapping btree format definitions
 *
 * There is a btree for the reverse map per allocation group
 */
#define	XFS_RMAP_CRC_MAGIC	0x524d4233	/* 'RMB3' */

/*
 * Ownership info for an extent.  This is used to create reverse-mapping
 * entries.
 */
#define XFS_OWNER_INFO_ATTR_FORK	(1 << 0)
#define XFS_OWNER_INFO_BMBT_BLOCK	(1 << 1)
struct xfs_owner_info {
	uint64_t		oi_owner;
	xfs_fileoff_t		oi_offset;
	unsigned int		oi_flags;
};

/*
 * Special owner types.
 *
 * Seeing as we only support up to 8EB, we have the upper bit of the owner field
 * to tell us we have a special owner value. We use these for static metadata
 * allocated at mkfs/growfs time, as well as for freespace management metadata.
 */
#define XFS_RMAP_OWN_NULL	(-1ULL)	/* No owner, for growfs */
#define XFS_RMAP_OWN_UNKNOWN	(-2ULL)	/* Unknown owner, for EFI recovery */
#define XFS_RMAP_OWN_FS		(-3ULL)	/* static fs metadata */
#define XFS_RMAP_OWN_LOG	(-4ULL)	/* static fs metadata */
#define XFS_RMAP_OWN_AG		(-5ULL)	/* AG freespace btree blocks */
#define XFS_RMAP_OWN_INOBT	(-6ULL)	/* Inode btree blocks */
#define XFS_RMAP_OWN_INODES	(-7ULL)	/* Inode chunk */
#define XFS_RMAP_OWN_REFC	(-8ULL) /* refcount tree */
#define XFS_RMAP_OWN_COW	(-9ULL) /* cow allocations */
#define XFS_RMAP_OWN_MIN	(-10ULL) /* guard */

#define XFS_RMAP_NON_INODE_OWNER(owner)	(!!((owner) & (1ULL << 63)))

/*
 * Data record structure
 */
struct xfs_rmap_rec {
	__be32		rm_startblock;	/* extent start block */
	__be32		rm_blockcount;	/* extent length */
	__be64		rm_owner;	/* extent owner */
	__be64		rm_offset;	/* offset within the owner */
};

/*
 * rmap btree record
 *  rm_offset:63 is the attribute fork flag
 *  rm_offset:62 is the bmbt block flag
 *  rm_offset:61 is the unwritten extent flag (same as l0:63 in bmbt)
 *  rm_offset:54-60 aren't used and should be zero
 *  rm_offset:0-53 is the block offset within the inode
 */
#define XFS_RMAP_OFF_ATTR_FORK	((uint64_t)1ULL << 63)
#define XFS_RMAP_OFF_BMBT_BLOCK	((uint64_t)1ULL << 62)
#define XFS_RMAP_OFF_UNWRITTEN	((uint64_t)1ULL << 61)

#define XFS_RMAP_LEN_MAX	((uint32_t)~0U)
#define XFS_RMAP_OFF_FLAGS	(XFS_RMAP_OFF_ATTR_FORK | \
				 XFS_RMAP_OFF_BMBT_BLOCK | \
				 XFS_RMAP_OFF_UNWRITTEN)
#define XFS_RMAP_OFF_MASK	((uint64_t)0x3FFFFFFFFFFFFFULL)

#define XFS_RMAP_OFF(off)		((off) & XFS_RMAP_OFF_MASK)

#define XFS_RMAP_IS_BMBT_BLOCK(off)	(!!((off) & XFS_RMAP_OFF_BMBT_BLOCK))
#define XFS_RMAP_IS_ATTR_FORK(off)	(!!((off) & XFS_RMAP_OFF_ATTR_FORK))
#define XFS_RMAP_IS_UNWRITTEN(len)	(!!((off) & XFS_RMAP_OFF_UNWRITTEN))

#define RMAPBT_STARTBLOCK_BITLEN	32
#define RMAPBT_BLOCKCOUNT_BITLEN	32
#define RMAPBT_OWNER_BITLEN		64
#define RMAPBT_ATTRFLAG_BITLEN		1
#define RMAPBT_BMBTFLAG_BITLEN		1
#define RMAPBT_EXNTFLAG_BITLEN		1
#define RMAPBT_UNUSED_OFFSET_BITLEN	7
#define RMAPBT_OFFSET_BITLEN		54

#define XFS_RMAP_ATTR_FORK		(1 << 0)
#define XFS_RMAP_BMBT_BLOCK		(1 << 1)
#define XFS_RMAP_UNWRITTEN		(1 << 2)
#define XFS_RMAP_KEY_FLAGS		(XFS_RMAP_ATTR_FORK | \
					 XFS_RMAP_BMBT_BLOCK)
#define XFS_RMAP_REC_FLAGS		(XFS_RMAP_UNWRITTEN)
struct xfs_rmap_irec {
	xfs_agblock_t	rm_startblock;	/* extent start block */
	xfs_extlen_t	rm_blockcount;	/* extent length */
	uint64_t	rm_owner;	/* extent owner */
	uint64_t	rm_offset;	/* offset within the owner */
	unsigned int	rm_flags;	/* state flags */
};

/*
 * Key structure
 *
 * We don't use the length for lookups
 */
struct xfs_rmap_key {
	__be32		rm_startblock;	/* extent start block */
	__be64		rm_owner;	/* extent owner */
	__be64		rm_offset;	/* offset within the owner */
} __attribute__((packed));

/* btree pointer type */
typedef __be32 xfs_rmap_ptr_t;

#define	XFS_RMAP_BLOCK(mp) \
	(xfs_has_finobt(((mp))) ? \
	 XFS_FIBT_BLOCK(mp) + 1 : \
	 XFS_IBT_BLOCK(mp) + 1)

/*
 * Reference Count Btree format definitions
 *
 */
#define	XFS_REFC_CRC_MAGIC	0x52334643	/* 'R3FC' */

unsigned int xfs_refc_block(struct xfs_mount *mp);

/*
 * Data record/key structure
 *
 * Each record associates a range of physical blocks (starting at
 * rc_startblock and ending rc_blockcount blocks later) with a reference
 * count (rc_refcount).  Extents that are being used to stage a copy on
 * write (CoW) operation are recorded in the refcount btree with a
 * refcount of 1.  All other records must have a refcount > 1 and must
 * track an extent mapped only by file data forks.
 *
 * Extents with a single owner (attributes, metadata, non-shared file
 * data) are not tracked here.  Free space is also not tracked here.
 * This is consistent with pre-reflink XFS.
 */

/*
 * Extents that are being used to stage a copy on write are stored
 * in the refcount btree with a refcount of 1 and the upper bit set
 * on the startblock.  This speeds up mount time deletion of stale
 * staging extents because they're all at the right side of the tree.
 */
#define XFS_REFC_COW_START		((xfs_agblock_t)(1U << 31))
#define REFCNTBT_COWFLAG_BITLEN		1
#define REFCNTBT_AGBLOCK_BITLEN		31

struct xfs_refcount_rec {
	__be32		rc_startblock;	/* starting block number */
	__be32		rc_blockcount;	/* count of blocks */
	__be32		rc_refcount;	/* number of inodes linked here */
};

struct xfs_refcount_key {
	__be32		rc_startblock;	/* starting block number */
};

struct xfs_refcount_irec {
	xfs_agblock_t	rc_startblock;	/* starting block number */
	xfs_extlen_t	rc_blockcount;	/* count of free blocks */
	xfs_nlink_t	rc_refcount;	/* number of inodes linked here */
};

#define MAXREFCOUNT	((xfs_nlink_t)~0U)
#define MAXREFCEXTLEN	((xfs_extlen_t)~0U)

/* btree pointer type */
typedef __be32 xfs_refcount_ptr_t;


/*
 * BMAP Btree format definitions
 *
 * This includes both the root block definition that sits inside an inode fork
 * and the record/pointer formats for the leaf/node in the blocks.
 */
#define XFS_BMAP_MAGIC		0x424d4150	/* 'BMAP' */
#define XFS_BMAP_CRC_MAGIC	0x424d4133	/* 'BMA3' */

/*
 * Bmap root header, on-disk form only.
 */
typedef struct xfs_bmdr_block {
	__be16		bb_level;	/* 0 is a leaf */
	__be16		bb_numrecs;	/* current # of data records */
} xfs_bmdr_block_t;

/*
 * Bmap btree record and extent descriptor.
 *  l0:63 is an extent flag (value 1 indicates non-normal).
 *  l0:9-62 are startoff.
 *  l0:0-8 and l1:21-63 are startblock.
 *  l1:0-20 are blockcount.
 */
#define BMBT_EXNTFLAG_BITLEN	1
#define BMBT_STARTOFF_BITLEN	54
#define BMBT_STARTBLOCK_BITLEN	52
#define BMBT_BLOCKCOUNT_BITLEN	21

#define BMBT_STARTOFF_MASK	((1ULL << BMBT_STARTOFF_BITLEN) - 1)
#define BMBT_BLOCKCOUNT_MASK	((1ULL << BMBT_BLOCKCOUNT_BITLEN) - 1)

/*
 * bmbt records have a file offset (block) field that is 54 bits wide, so this
 * is the largest xfs_fileoff_t that we ever expect to see.
 */
#define XFS_MAX_FILEOFF		(BMBT_STARTOFF_MASK + BMBT_BLOCKCOUNT_MASK)

typedef struct xfs_bmbt_rec {
	__be64			l0, l1;
} xfs_bmbt_rec_t;

typedef uint64_t	xfs_bmbt_rec_base_t;	/* use this for casts */
typedef xfs_bmbt_rec_t xfs_bmdr_rec_t;

/*
 * Values and macros for delayed-allocation startblock fields.
 */
#define STARTBLOCKVALBITS	17
#define STARTBLOCKMASKBITS	(15 + 20)
#define STARTBLOCKMASK		\
	(((((xfs_fsblock_t)1) << STARTBLOCKMASKBITS) - 1) << STARTBLOCKVALBITS)

static inline int isnullstartblock(xfs_fsblock_t x)
{
	return ((x) & STARTBLOCKMASK) == STARTBLOCKMASK;
}

static inline xfs_fsblock_t nullstartblock(int k)
{
	ASSERT(k < (1 << STARTBLOCKVALBITS));
	return STARTBLOCKMASK | (k);
}

static inline xfs_filblks_t startblockval(xfs_fsblock_t x)
{
	return (xfs_filblks_t)((x) & ~STARTBLOCKMASK);
}

/*
 * Key structure for non-leaf levels of the tree.
 */
typedef struct xfs_bmbt_key {
	__be64		br_startoff;	/* starting file offset */
} xfs_bmbt_key_t, xfs_bmdr_key_t;

/* btree pointer type */
typedef __be64 xfs_bmbt_ptr_t, xfs_bmdr_ptr_t;


/*
 * Generic Btree block format definitions
 *
 * This is a combination of the actual format used on disk for short and long
 * format btrees.  The first three fields are shared by both format, but the
 * pointers are different and should be used with care.
 *
 * To get the size of the actual short or long form headers please use the size
 * macros below.  Never use sizeof(xfs_btree_block).
 *
 * The blkno, crc, lsn, owner and uuid fields are only available in filesystems
 * with the crc feature bit, and all accesses to them must be conditional on
 * that flag.
 */
/* short form block header */
struct xfs_btree_block_shdr {
	__be32		bb_leftsib;
	__be32		bb_rightsib;

	__be64		bb_blkno;
	__be64		bb_lsn;
	uuid_t		bb_uuid;
	__be32		bb_owner;
	__le32		bb_crc;
};

/* long form block header */
struct xfs_btree_block_lhdr {
	__be64		bb_leftsib;
	__be64		bb_rightsib;

	__be64		bb_blkno;
	__be64		bb_lsn;
	uuid_t		bb_uuid;
	__be64		bb_owner;
	__le32		bb_crc;
	__be32		bb_pad; /* padding for alignment */
};

struct xfs_btree_block {
	__be32		bb_magic;	/* magic number for block type */
	__be16		bb_level;	/* 0 is a leaf */
	__be16		bb_numrecs;	/* current # of data records */
	union {
		struct xfs_btree_block_shdr s;
		struct xfs_btree_block_lhdr l;
	} bb_u;				/* rest */
};

/* size of a short form block */
#define XFS_BTREE_SBLOCK_LEN \
	(offsetof(struct xfs_btree_block, bb_u) + \
	 offsetof(struct xfs_btree_block_shdr, bb_blkno))
/* size of a long form block */
#define XFS_BTREE_LBLOCK_LEN \
	(offsetof(struct xfs_btree_block, bb_u) + \
	 offsetof(struct xfs_btree_block_lhdr, bb_blkno))

/* sizes of CRC enabled btree blocks */
#define XFS_BTREE_SBLOCK_CRC_LEN \
	(offsetof(struct xfs_btree_block, bb_u) + \
	 sizeof(struct xfs_btree_block_shdr))
#define XFS_BTREE_LBLOCK_CRC_LEN \
	(offsetof(struct xfs_btree_block, bb_u) + \
	 sizeof(struct xfs_btree_block_lhdr))

#define XFS_BTREE_SBLOCK_CRC_OFF \
	offsetof(struct xfs_btree_block, bb_u.s.bb_crc)
#define XFS_BTREE_LBLOCK_CRC_OFF \
	offsetof(struct xfs_btree_block, bb_u.l.bb_crc)

/*
 * On-disk XFS access control list structure.
 */
struct xfs_acl_entry {
	__be32	ae_tag;
	__be32	ae_id;
	__be16	ae_perm;
	__be16	ae_pad;		/* fill the implicit hole in the structure */
};

struct xfs_acl {
	__be32			acl_cnt;
	struct xfs_acl_entry	acl_entry[];
};

/*
 * The number of ACL entries allowed is defined by the on-disk format.
 * For v4 superblocks, that is limited to 25 entries. For v5 superblocks, it is
 * limited only by the maximum size of the xattr that stores the information.
 */
#define XFS_ACL_MAX_ENTRIES(mp)	\
	(xfs_has_crc(mp) \
		?  (XFS_XATTR_SIZE_MAX - sizeof(struct xfs_acl)) / \
						sizeof(struct xfs_acl_entry) \
		: 25)

#define XFS_ACL_SIZE(cnt) \
	(sizeof(struct xfs_acl) + \
		sizeof(struct xfs_acl_entry) * cnt)

#define XFS_ACL_MAX_SIZE(mp) \
	XFS_ACL_SIZE(XFS_ACL_MAX_ENTRIES((mp)))


/* On-disk XFS extended attribute names */
#define SGI_ACL_FILE		"SGI_ACL_FILE"
#define SGI_ACL_DEFAULT		"SGI_ACL_DEFAULT"
#define SGI_ACL_FILE_SIZE	(sizeof(SGI_ACL_FILE)-1)
#define SGI_ACL_DEFAULT_SIZE	(sizeof(SGI_ACL_DEFAULT)-1)

#endif /* __XFS_FORMAT_H__ */
