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
	uuid_t		agf_uuid;	/* uuid of filesystem */

	/*
	 * reserve some contiguous space for future logged fields before we add
	 * the unlogged fields. This makes the range logging via flags and
	 * structure offsets much simpler.
	 */
	__be64		agf_spare64[16];

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
#define	XFS_AGF_NUM_BITS	13
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
	{ XFS_AGF_UUID,		"UUID" }

/* disk block (xfs_daddr_t) in the AG */
#define XFS_AGF_DADDR(mp)	((xfs_daddr_t)(1 << (mp)->m_sectbb_log))
#define	XFS_AGF_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGF_DADDR(mp))
#define	XFS_BUF_TO_AGF(bp)	((xfs_agf_t *)((bp)->b_addr))

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
#define	XFS_AGI_NUM_BITS_R2	13

/* disk block (xfs_daddr_t) in the AG */
#define XFS_AGI_DADDR(mp)	((xfs_daddr_t)(2 << (mp)->m_sectbb_log))
#define	XFS_AGI_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGI_DADDR(mp))
#define	XFS_BUF_TO_AGI(bp)	((xfs_agi_t *)((bp)->b_addr))

/*
 * The third a.g. block contains the a.g. freelist, an array
 * of block pointers to blocks owned by the allocation btree code.
 */
#define XFS_AGFL_DADDR(mp)	((xfs_daddr_t)(3 << (mp)->m_sectbb_log))
#define	XFS_AGFL_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGFL_DADDR(mp))
#define	XFS_BUF_TO_AGFL(bp)	((xfs_agfl_t *)((bp)->b_addr))

#define XFS_BUF_TO_AGFL_BNO(mp, bp) \
	(xfs_sb_version_hascrc(&((mp)->m_sb)) ? \
		&(XFS_BUF_TO_AGFL(bp)->agfl_bno[0]) : \
		(__be32 *)(bp)->b_addr)

/*
 * Size of the AGFL.  For CRC-enabled filesystes we steal a couple of
 * slots in the beginning of the block for a proper header with the
 * location information and CRC.
 */
#define XFS_AGFL_SIZE(mp) \
	(((mp)->m_sb.sb_sectsize - \
	 (xfs_sb_version_hascrc(&((mp)->m_sb)) ? \
		sizeof(struct xfs_agfl) : 0)) / \
	  sizeof(xfs_agblock_t))

typedef struct xfs_agfl {
	__be32		agfl_magicnum;
	__be32		agfl_seqno;
	uuid_t		agfl_uuid;
	__be64		agfl_lsn;
	__be32		agfl_crc;
	__be32		agfl_bno[];	/* actually XFS_AGFL_SIZE(mp) */
} xfs_agfl_t;

#define XFS_AGFL_CRC_OFF	offsetof(struct xfs_agfl, agfl_crc)


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

typedef struct xfs_timestamp {
	__be32		t_sec;		/* timestamp seconds */
	__be32		t_nsec;		/* timestamp nanoseconds */
} xfs_timestamp_t;

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
 * There is a very similar struct icdinode in xfs_inode which matches the
 * layout of the first 96 bytes of this structure, but is kept in native
 * format instead of big endian.
 *
 * Note: di_flushiter is only used by v1/2 inodes - it's effectively a zeroed
 * padding field for v3 inodes.
 */
#define	XFS_DINODE_MAGIC		0x494e	/* 'IN' */
#define XFS_DINODE_GOOD_VERSION(v)	((v) >= 1 && (v) <= 3)
typedef struct xfs_dinode {
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
	__u8		di_pad2[16];	/* more padding for future expansion */

	/* fields only written to during inode creation */
	xfs_timestamp_t	di_crtime;	/* time created */
	__be64		di_ino;		/* inode number */
	uuid_t		di_uuid;	/* UUID of the filesystem */

	/* structure must be padded to 64 bit alignment */
} xfs_dinode_t;

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
 * The old inode format had a 16 bit link count, so its maximum is USHRT_MAX.
 */
#define	XFS_MAXLINK		((1U << 31) - 1U)
#define	XFS_MAXLINK_1		65535U

/*
 * Values for di_format
 */
typedef enum xfs_dinode_fmt {
	XFS_DINODE_FMT_DEV,		/* xfs_dev_t */
	XFS_DINODE_FMT_LOCAL,		/* bulk data */
	XFS_DINODE_FMT_EXTENTS,		/* struct xfs_bmbt_rec */
	XFS_DINODE_FMT_BTREE,		/* struct xfs_bmdr_block */
	XFS_DINODE_FMT_UUID		/* uuid_t */
} xfs_dinode_fmt_t;

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
#define XFS_LITINO(mp, version) \
	((int)(((mp)->m_sb.sb_inodesize) - xfs_dinode_size(version)))

/*
 * Inode data & attribute fork sizes, per inode.
 */
#define XFS_DFORK_Q(dip)		((dip)->di_forkoff != 0)
#define XFS_DFORK_BOFF(dip)		((int)((dip)->di_forkoff << 3))

#define XFS_DFORK_DSIZE(dip,mp) \
	(XFS_DFORK_Q(dip) ? \
		XFS_DFORK_BOFF(dip) : \
		XFS_LITINO(mp, (dip)->di_version))
#define XFS_DFORK_ASIZE(dip,mp) \
	(XFS_DFORK_Q(dip) ? \
		XFS_LITINO(mp, (dip)->di_version) - XFS_DFORK_BOFF(dip) : \
		0)
#define XFS_DFORK_SIZE(dip,mp,w) \
	((w) == XFS_DATA_FORK ? \
		XFS_DFORK_DSIZE(dip, mp) : \
		XFS_DFORK_ASIZE(dip, mp))

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
 * There should be a one-to-one correspondence between these flags and the
 * XFS_XFLAG_s.
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
#define XFS_DQUOT_VERSION	(u_int8_t)0x01	/* latest version number */

/*
 * This is the main portion of the on-disk representation of quota
 * information for a user. This is the q_core of the xfs_dquot_t that
 * is kept in kernel memory. We pad this with some more expansion room
 * to construct the on disk structure.
 */
typedef struct	xfs_disk_dquot {
	__be16		d_magic;	/* dquot magic = XFS_DQUOT_MAGIC */
	__u8		d_version;	/* dquot version */
	__u8		d_flags;	/* XFS_DQ_USER/PROJ/GROUP */
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
} xfs_disk_dquot_t;

/*
 * This is what goes on disk. This is separated from the xfs_disk_dquot because
 * carrying the unnecessary padding would be a waste of memory.
 */
typedef struct xfs_dqblk {
	xfs_disk_dquot_t  dd_diskdq;	/* portion that lives incore as well */
	char		  dd_fill[4];	/* filling for posterity */

	/*
	 * These two are only present on filesystems with the CRC bits set.
	 */
	__be32		  dd_crc;	/* checksum */
	__be64		  dd_lsn;	/* last modification in log */
	uuid_t		  dd_uuid;	/* location information */
} xfs_dqblk_t;

#define XFS_DQUOT_CRC_OFF	offsetof(struct xfs_dqblk, dd_crc)

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

/*
 * The maximum pathlen is 1024 bytes. Since the minimum file system
 * blocksize is 512 bytes, we can get a max of 3 extents back from
 * bmapi when crc headers are taken into account.
 */
#define XFS_SYMLINK_MAPS 3

#define XFS_SYMLINK_BUF_SPACE(mp, bufsize)	\
	((bufsize) - (xfs_sb_version_hascrc(&(mp)->m_sb) ? \
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

typedef	__uint64_t	xfs_inofree_t;
#define	XFS_INODES_PER_CHUNK		(NBBY * sizeof(xfs_inofree_t))
#define	XFS_INODES_PER_CHUNK_LOG	(XFS_NBBYLOG + 3)
#define	XFS_INOBT_ALL_FREE		((xfs_inofree_t)-1)
#define	XFS_INOBT_MASK(i)		((xfs_inofree_t)1 << (i))

static inline xfs_inofree_t xfs_inobt_maskn(int i, int n)
{
	return ((n >= XFS_INODES_PER_CHUNK ? 0 : XFS_INOBT_MASK(n)) - 1) << i;
}

/*
 * Data record structure
 */
typedef struct xfs_inobt_rec {
	__be32		ir_startino;	/* starting inode number */
	__be32		ir_freecount;	/* count of free inodes (set bits) */
	__be64		ir_free;	/* free inode mask */
} xfs_inobt_rec_t;

typedef struct xfs_inobt_rec_incore {
	xfs_agino_t	ir_startino;	/* starting inode number */
	__int32_t	ir_freecount;	/* count of free inodes (set bits) */
	xfs_inofree_t	ir_free;	/* free inode mask */
} xfs_inobt_rec_incore_t;


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
 * The first data block of an AG depends on whether the filesystem was formatted
 * with the finobt feature. If so, account for the finobt reserved root btree
 * block.
 */
#define XFS_PREALLOC_BLOCKS(mp) \
	(xfs_sb_version_hasfinobt(&((mp)->m_sb)) ? \
	 XFS_FIBT_BLOCK(mp) + 1 : \
	 XFS_IBT_BLOCK(mp) + 1)



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

typedef struct xfs_bmbt_rec {
	__be64			l0, l1;
} xfs_bmbt_rec_t;

typedef __uint64_t	xfs_bmbt_rec_base_t;	/* use this for casts */
typedef xfs_bmbt_rec_t xfs_bmdr_rec_t;

typedef struct xfs_bmbt_rec_host {
	__uint64_t		l0, l1;
} xfs_bmbt_rec_host_t;

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
 * Possible extent formats.
 */
typedef enum {
	XFS_EXTFMT_NOSTATE = 0,
	XFS_EXTFMT_HASSTATE
} xfs_exntfmt_t;

/*
 * Possible extent states.
 */
typedef enum {
	XFS_EXT_NORM, XFS_EXT_UNWRITTEN,
	XFS_EXT_DMAPI_OFFLINE, XFS_EXT_INVALID
} xfs_exntst_t;

/*
 * Incore version of above.
 */
typedef struct xfs_bmbt_irec
{
	xfs_fileoff_t	br_startoff;	/* starting file offset */
	xfs_fsblock_t	br_startblock;	/* starting block number */
	xfs_filblks_t	br_blockcount;	/* number of blocks */
	xfs_exntst_t	br_state;	/* extent state */
} xfs_bmbt_irec_t;

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
struct xfs_btree_block {
	__be32		bb_magic;	/* magic number for block type */
	__be16		bb_level;	/* 0 is a leaf */
	__be16		bb_numrecs;	/* current # of data records */
	union {
		struct {
			__be32		bb_leftsib;
			__be32		bb_rightsib;

			__be64		bb_blkno;
			__be64		bb_lsn;
			uuid_t		bb_uuid;
			__be32		bb_owner;
			__le32		bb_crc;
		} s;			/* short form pointers */
		struct	{
			__be64		bb_leftsib;
			__be64		bb_rightsib;

			__be64		bb_blkno;
			__be64		bb_lsn;
			uuid_t		bb_uuid;
			__be64		bb_owner;
			__le32		bb_crc;
			__be32		bb_pad; /* padding for alignment */
		} l;			/* long form pointers */
	} bb_u;				/* rest */
};

#define XFS_BTREE_SBLOCK_LEN	16	/* size of a short form block */
#define XFS_BTREE_LBLOCK_LEN	24	/* size of a long form block */

/* sizes of CRC enabled btree blocks */
#define XFS_BTREE_SBLOCK_CRC_LEN	(XFS_BTREE_SBLOCK_LEN + 40)
#define XFS_BTREE_LBLOCK_CRC_LEN	(XFS_BTREE_LBLOCK_LEN + 48)

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
	struct xfs_acl_entry	acl_entry[0];
};

/*
 * The number of ACL entries allowed is defined by the on-disk format.
 * For v4 superblocks, that is limited to 25 entries. For v5 superblocks, it is
 * limited only by the maximum size of the xattr that stores the information.
 */
#define XFS_ACL_MAX_ENTRIES(mp)	\
	(xfs_sb_version_hascrc(&mp->m_sb) \
		?  (XATTR_SIZE_MAX - sizeof(struct xfs_acl)) / \
						sizeof(struct xfs_acl_entry) \
		: 25)

#define XFS_ACL_MAX_SIZE(mp) \
	(sizeof(struct xfs_acl) + \
		sizeof(struct xfs_acl_entry) * XFS_ACL_MAX_ENTRIES((mp)))

/* On-disk XFS extended attribute names */
#define SGI_ACL_FILE		(unsigned char *)"SGI_ACL_FILE"
#define SGI_ACL_DEFAULT		(unsigned char *)"SGI_ACL_DEFAULT"
#define SGI_ACL_FILE_SIZE	(sizeof(SGI_ACL_FILE)-1)
#define SGI_ACL_DEFAULT_SIZE	(sizeof(SGI_ACL_DEFAULT)-1)

#endif /* __XFS_FORMAT_H__ */
