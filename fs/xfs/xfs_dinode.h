/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_DINODE_H__
#define	__XFS_DINODE_H__

struct xfs_buf;
struct xfs_mount;

#define	XFS_DINODE_VERSION_1	1
#define	XFS_DINODE_VERSION_2	2
#define XFS_DINODE_GOOD_VERSION(v)	\
	(((v) == XFS_DINODE_VERSION_1 || (v) == XFS_DINODE_VERSION_2))
#define	XFS_DINODE_MAGIC	0x494e	/* 'IN' */

/*
 * Disk inode structure.
 * This is just the header; the inode is expanded to fill a variable size
 * with the last field expanding.  It is split into the core and "other"
 * because we only need the core part in the in-core inode.
 */
typedef struct xfs_timestamp {
	__int32_t	t_sec;		/* timestamp seconds */
	__int32_t	t_nsec;		/* timestamp nanoseconds */
} xfs_timestamp_t;

/*
 * Note: Coordinate changes to this structure with the XFS_DI_* #defines
 * below and the offsets table in xfs_ialloc_log_di().
 */
typedef struct xfs_dinode_core
{
	__uint16_t	di_magic;	/* inode magic # = XFS_DINODE_MAGIC */
	__uint16_t	di_mode;	/* mode and type of file */
	__int8_t	di_version;	/* inode version */
	__int8_t	di_format;	/* format of di_c data */
	__uint16_t	di_onlink;	/* old number of links to file */
	__uint32_t	di_uid;		/* owner's user id */
	__uint32_t	di_gid;		/* owner's group id */
	__uint32_t	di_nlink;	/* number of links to file */
	__uint16_t	di_projid;	/* owner's project id */
	__uint8_t	di_pad[8];	/* unused, zeroed space */
	__uint16_t	di_flushiter;	/* incremented on flush */
	xfs_timestamp_t	di_atime;	/* time last accessed */
	xfs_timestamp_t	di_mtime;	/* time last modified */
	xfs_timestamp_t	di_ctime;	/* time created/inode modified */
	xfs_fsize_t	di_size;	/* number of bytes in file */
	xfs_drfsbno_t	di_nblocks;	/* # of direct & btree blocks used */
	xfs_extlen_t	di_extsize;	/* basic/minimum extent size for file */
	xfs_extnum_t	di_nextents;	/* number of extents in data fork */
	xfs_aextnum_t	di_anextents;	/* number of extents in attribute fork*/
	__uint8_t	di_forkoff;	/* attr fork offs, <<3 for 64b align */
	__int8_t	di_aformat;	/* format of attr fork's data */
	__uint32_t	di_dmevmask;	/* DMIG event mask */
	__uint16_t	di_dmstate;	/* DMIG state info */
	__uint16_t	di_flags;	/* random flags, XFS_DIFLAG_... */
	__uint32_t	di_gen;		/* generation number */
} xfs_dinode_core_t;

#define DI_MAX_FLUSH 0xffff

typedef struct xfs_dinode
{
	xfs_dinode_core_t	di_core;
	/*
	 * In adding anything between the core and the union, be
	 * sure to update the macros like XFS_LITINO below and
	 * XFS_BMAP_RBLOCK_DSIZE in xfs_bmap_btree.h.
	 */
	xfs_agino_t		di_next_unlinked;/* agi unlinked list ptr */
	union {
		xfs_bmdr_block_t di_bmbt;	/* btree root block */
		xfs_bmbt_rec_32_t di_bmx[1];	/* extent list */
		xfs_dir2_sf_t	di_dir2sf;	/* shortform directory v2 */
		char		di_c[1];	/* local contents */
		xfs_dev_t	di_dev;		/* device for S_IFCHR/S_IFBLK */
		uuid_t		di_muuid;	/* mount point value */
		char		di_symlink[1];	/* local symbolic link */
	}		di_u;
	union {
		xfs_bmdr_block_t di_abmbt;	/* btree root block */
		xfs_bmbt_rec_32_t di_abmx[1];	/* extent list */
		xfs_attr_shortform_t di_attrsf;	/* shortform attribute list */
	}		di_a;
} xfs_dinode_t;

/*
 * The 32 bit link count in the inode theoretically maxes out at UINT_MAX.
 * Since the pathconf interface is signed, we use 2^31 - 1 instead.
 * The old inode format had a 16 bit link count, so its maximum is USHRT_MAX.
 */
#define	XFS_MAXLINK		((1U << 31) - 1U)
#define	XFS_MAXLINK_1		65535U

/*
 * Bit names for logging disk inodes only
 */
#define	XFS_DI_MAGIC		0x0000001
#define	XFS_DI_MODE		0x0000002
#define	XFS_DI_VERSION		0x0000004
#define	XFS_DI_FORMAT		0x0000008
#define	XFS_DI_ONLINK		0x0000010
#define	XFS_DI_UID		0x0000020
#define	XFS_DI_GID		0x0000040
#define	XFS_DI_NLINK		0x0000080
#define	XFS_DI_PROJID		0x0000100
#define	XFS_DI_PAD		0x0000200
#define	XFS_DI_ATIME		0x0000400
#define	XFS_DI_MTIME		0x0000800
#define	XFS_DI_CTIME		0x0001000
#define	XFS_DI_SIZE		0x0002000
#define	XFS_DI_NBLOCKS		0x0004000
#define	XFS_DI_EXTSIZE		0x0008000
#define	XFS_DI_NEXTENTS		0x0010000
#define	XFS_DI_NAEXTENTS	0x0020000
#define	XFS_DI_FORKOFF		0x0040000
#define	XFS_DI_AFORMAT		0x0080000
#define	XFS_DI_DMEVMASK		0x0100000
#define	XFS_DI_DMSTATE		0x0200000
#define	XFS_DI_FLAGS		0x0400000
#define	XFS_DI_GEN		0x0800000
#define	XFS_DI_NEXT_UNLINKED	0x1000000
#define	XFS_DI_U		0x2000000
#define	XFS_DI_A		0x4000000
#define	XFS_DI_NUM_BITS		27
#define	XFS_DI_ALL_BITS		((1 << XFS_DI_NUM_BITS) - 1)
#define	XFS_DI_CORE_BITS	(XFS_DI_ALL_BITS & ~(XFS_DI_U|XFS_DI_A))

/*
 * Values for di_format
 */
typedef enum xfs_dinode_fmt
{
	XFS_DINODE_FMT_DEV,		/* CHR, BLK: di_dev */
	XFS_DINODE_FMT_LOCAL,		/* DIR, REG: di_c */
					/* LNK: di_symlink */
	XFS_DINODE_FMT_EXTENTS,		/* DIR, REG, LNK: di_bmx */
	XFS_DINODE_FMT_BTREE,		/* DIR, REG, LNK: di_bmbt */
	XFS_DINODE_FMT_UUID		/* MNT: di_uuid */
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
#define	XFS_LITINO(mp)	((mp)->m_litino)
#define	XFS_BROOT_SIZE_ADJ	\
	(sizeof(xfs_bmbt_block_t) - sizeof(xfs_bmdr_block_t))

/*
 * Inode data & attribute fork sizes, per inode.
 */
#define XFS_CFORK_Q(dcp)                    ((dcp)->di_forkoff != 0)
#define	XFS_CFORK_Q_DISK(dcp)		    ((dcp)->di_forkoff != 0)

#define XFS_CFORK_BOFF(dcp)                 ((int)((dcp)->di_forkoff << 3))
#define	XFS_CFORK_BOFF_DISK(dcp) \
	((int)(INT_GET((dcp)->di_forkoff, ARCH_CONVERT) << 3))

#define	XFS_CFORK_DSIZE_DISK(dcp,mp) \
	(XFS_CFORK_Q_DISK(dcp) ? XFS_CFORK_BOFF_DISK(dcp) : XFS_LITINO(mp))
#define XFS_CFORK_DSIZE(dcp,mp) \
	(XFS_CFORK_Q(dcp) ? XFS_CFORK_BOFF(dcp) : XFS_LITINO(mp))

#define	XFS_CFORK_ASIZE_DISK(dcp,mp) \
	(XFS_CFORK_Q_DISK(dcp) ? XFS_LITINO(mp) - XFS_CFORK_BOFF_DISK(dcp) : 0)
#define XFS_CFORK_ASIZE(dcp,mp) \
	(XFS_CFORK_Q(dcp) ? XFS_LITINO(mp) - XFS_CFORK_BOFF(dcp) : 0)

#define	XFS_CFORK_SIZE_DISK(dcp,mp,w) \
	((w) == XFS_DATA_FORK ? \
		XFS_CFORK_DSIZE_DISK(dcp, mp) : \
	 	XFS_CFORK_ASIZE_DISK(dcp, mp))
#define XFS_CFORK_SIZE(dcp,mp,w) \
	((w) == XFS_DATA_FORK ? \
		XFS_CFORK_DSIZE(dcp, mp) : XFS_CFORK_ASIZE(dcp, mp))

#define XFS_DFORK_DSIZE(dip,mp) \
	XFS_CFORK_DSIZE_DISK(&(dip)->di_core, mp)
#define XFS_DFORK_DSIZE_HOST(dip,mp) \
	XFS_CFORK_DSIZE(&(dip)->di_core, mp)
#define XFS_DFORK_ASIZE(dip,mp) \
	XFS_CFORK_ASIZE_DISK(&(dip)->di_core, mp)
#define XFS_DFORK_ASIZE_HOST(dip,mp) \
	XFS_CFORK_ASIZE(&(dip)->di_core, mp)
#define	XFS_DFORK_SIZE(dip,mp,w) \
	XFS_CFORK_SIZE_DISK(&(dip)->di_core, mp, w)
#define	XFS_DFORK_SIZE_HOST(dip,mp,w) \
	XFS_CFORK_SIZE(&(dip)->di_core, mp, w)

#define	XFS_DFORK_Q(dip)                    XFS_CFORK_Q_DISK(&(dip)->di_core)
#define	XFS_DFORK_BOFF(dip)		    XFS_CFORK_BOFF_DISK(&(dip)->di_core)
#define	XFS_DFORK_DPTR(dip)		    ((dip)->di_u.di_c)
#define	XFS_DFORK_APTR(dip)	\
	((dip)->di_u.di_c + XFS_DFORK_BOFF(dip))
#define	XFS_DFORK_PTR(dip,w)	\
	((w) == XFS_DATA_FORK ? XFS_DFORK_DPTR(dip) : XFS_DFORK_APTR(dip))
#define	XFS_CFORK_FORMAT(dcp,w)	\
	((w) == XFS_DATA_FORK ? (dcp)->di_format : (dcp)->di_aformat)
#define	XFS_CFORK_FMT_SET(dcp,w,n) \
	((w) == XFS_DATA_FORK ? \
		((dcp)->di_format = (n)) : ((dcp)->di_aformat = (n)))
#define	XFS_DFORK_FORMAT(dip,w) XFS_CFORK_FORMAT(&(dip)->di_core, w)

#define	XFS_CFORK_NEXTENTS_DISK(dcp,w) \
	((w) == XFS_DATA_FORK ? \
	 	INT_GET((dcp)->di_nextents, ARCH_CONVERT) : \
	 	INT_GET((dcp)->di_anextents, ARCH_CONVERT))
#define XFS_CFORK_NEXTENTS(dcp,w) \
	((w) == XFS_DATA_FORK ? (dcp)->di_nextents : (dcp)->di_anextents)
#define	XFS_DFORK_NEXTENTS(dip,w) XFS_CFORK_NEXTENTS_DISK(&(dip)->di_core, w)
#define	XFS_DFORK_NEXTENTS_HOST(dip,w) XFS_CFORK_NEXTENTS(&(dip)->di_core, w)

#define	XFS_CFORK_NEXT_SET(dcp,w,n) \
	((w) == XFS_DATA_FORK ? \
		((dcp)->di_nextents = (n)) : ((dcp)->di_anextents = (n)))

#define	XFS_BUF_TO_DINODE(bp)	((xfs_dinode_t *)XFS_BUF_PTR(bp))

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

#define XFS_DIFLAG_ANY \
	(XFS_DIFLAG_REALTIME | XFS_DIFLAG_PREALLOC | XFS_DIFLAG_NEWRTBM | \
	 XFS_DIFLAG_IMMUTABLE | XFS_DIFLAG_APPEND | XFS_DIFLAG_SYNC | \
	 XFS_DIFLAG_NOATIME | XFS_DIFLAG_NODUMP | XFS_DIFLAG_RTINHERIT | \
	 XFS_DIFLAG_PROJINHERIT | XFS_DIFLAG_NOSYMLINKS | XFS_DIFLAG_EXTSIZE | \
	 XFS_DIFLAG_EXTSZINHERIT | XFS_DIFLAG_NODEFRAG)

#endif	/* __XFS_DINODE_H__ */
