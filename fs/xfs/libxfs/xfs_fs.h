/* SPDX-License-Identifier: LGPL-2.1 */
/*
 * Copyright (c) 1995-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_FS_H__
#define __XFS_FS_H__

/*
 * SGI's XFS filesystem's major stuff (constants, structures)
 */

/*
 * Direct I/O attribute record used with XFS_IOC_DIOINFO
 * d_miniosz is the min xfer size, xfer size multiple and file seek offset
 * alignment.
 */
#ifndef HAVE_DIOATTR
struct dioattr {
	__u32		d_mem;		/* data buffer memory alignment */
	__u32		d_miniosz;	/* min xfer size		*/
	__u32		d_maxiosz;	/* max xfer size		*/
};
#endif

/*
 * Structure for XFS_IOC_GETBMAP.
 * On input, fill in bmv_offset and bmv_length of the first structure
 * to indicate the area of interest in the file, and bmv_entries with
 * the number of array elements given back.  The first structure is
 * updated on return to give the offset and length for the next call.
 */
#ifndef HAVE_GETBMAP
struct getbmap {
	__s64		bmv_offset;	/* file offset of segment in blocks */
	__s64		bmv_block;	/* starting block (64-bit daddr_t)  */
	__s64		bmv_length;	/* length of segment, blocks	    */
	__s32		bmv_count;	/* # of entries in array incl. 1st  */
	__s32		bmv_entries;	/* # of entries filled in (output)  */
};
#endif

/*
 *	Structure for XFS_IOC_GETBMAPX.	 Fields bmv_offset through bmv_entries
 *	are used exactly as in the getbmap structure.  The getbmapx structure
 *	has additional bmv_iflags and bmv_oflags fields. The bmv_iflags field
 *	is only used for the first structure.  It contains input flags
 *	specifying XFS_IOC_GETBMAPX actions.  The bmv_oflags field is filled
 *	in by the XFS_IOC_GETBMAPX command for each returned structure after
 *	the first.
 */
#ifndef HAVE_GETBMAPX
struct getbmapx {
	__s64		bmv_offset;	/* file offset of segment in blocks */
	__s64		bmv_block;	/* starting block (64-bit daddr_t)  */
	__s64		bmv_length;	/* length of segment, blocks	    */
	__s32		bmv_count;	/* # of entries in array incl. 1st  */
	__s32		bmv_entries;	/* # of entries filled in (output). */
	__s32		bmv_iflags;	/* input flags (1st structure)	    */
	__s32		bmv_oflags;	/* output flags (after 1st structure)*/
	__s32		bmv_unused1;	/* future use			    */
	__s32		bmv_unused2;	/* future use			    */
};
#endif

/*	bmv_iflags values - set by XFS_IOC_GETBMAPX caller.	*/
#define BMV_IF_ATTRFORK		0x1	/* return attr fork rather than data */
#define BMV_IF_NO_DMAPI_READ	0x2	/* Deprecated */
#define BMV_IF_PREALLOC		0x4	/* rtn status BMV_OF_PREALLOC if req */
#define BMV_IF_DELALLOC		0x8	/* rtn status BMV_OF_DELALLOC if req */
#define BMV_IF_NO_HOLES		0x10	/* Do not return holes */
#define BMV_IF_COWFORK		0x20	/* return CoW fork rather than data */
#define BMV_IF_VALID	\
	(BMV_IF_ATTRFORK|BMV_IF_NO_DMAPI_READ|BMV_IF_PREALLOC|	\
	 BMV_IF_DELALLOC|BMV_IF_NO_HOLES|BMV_IF_COWFORK)

/*	bmv_oflags values - returned for each non-header segment */
#define BMV_OF_PREALLOC		0x1	/* segment = unwritten pre-allocation */
#define BMV_OF_DELALLOC		0x2	/* segment = delayed allocation */
#define BMV_OF_LAST		0x4	/* segment is the last in the file */
#define BMV_OF_SHARED		0x8	/* segment shared with another file */

/*	fmr_owner special values for FS_IOC_GETFSMAP */
#define XFS_FMR_OWN_FREE	FMR_OWN_FREE      /* free space */
#define XFS_FMR_OWN_UNKNOWN	FMR_OWN_UNKNOWN   /* unknown owner */
#define XFS_FMR_OWN_FS		FMR_OWNER('X', 1) /* static fs metadata */
#define XFS_FMR_OWN_LOG		FMR_OWNER('X', 2) /* journalling log */
#define XFS_FMR_OWN_AG		FMR_OWNER('X', 3) /* per-AG metadata */
#define XFS_FMR_OWN_INOBT	FMR_OWNER('X', 4) /* inode btree blocks */
#define XFS_FMR_OWN_INODES	FMR_OWNER('X', 5) /* inodes */
#define XFS_FMR_OWN_REFC	FMR_OWNER('X', 6) /* refcount tree */
#define XFS_FMR_OWN_COW		FMR_OWNER('X', 7) /* cow staging */
#define XFS_FMR_OWN_DEFECTIVE	FMR_OWNER('X', 8) /* bad blocks */

/*
 * File segment locking set data type for 64 bit access.
 * Also used for all the RESV/FREE interfaces.
 */
typedef struct xfs_flock64 {
	__s16		l_type;
	__s16		l_whence;
	__s64		l_start;
	__s64		l_len;		/* len == 0 means until end of file */
	__s32		l_sysid;
	__u32		l_pid;
	__s32		l_pad[4];	/* reserve area			    */
} xfs_flock64_t;

/*
 * Output for XFS_IOC_FSGEOMETRY_V1
 */
struct xfs_fsop_geom_v1 {
	__u32		blocksize;	/* filesystem (data) block size */
	__u32		rtextsize;	/* realtime extent size		*/
	__u32		agblocks;	/* fsblocks in an AG		*/
	__u32		agcount;	/* number of allocation groups	*/
	__u32		logblocks;	/* fsblocks in the log		*/
	__u32		sectsize;	/* (data) sector size, bytes	*/
	__u32		inodesize;	/* inode size in bytes		*/
	__u32		imaxpct;	/* max allowed inode space(%)	*/
	__u64		datablocks;	/* fsblocks in data subvolume	*/
	__u64		rtblocks;	/* fsblocks in realtime subvol	*/
	__u64		rtextents;	/* rt extents in realtime subvol*/
	__u64		logstart;	/* starting fsblock of the log	*/
	unsigned char	uuid[16];	/* unique id of the filesystem	*/
	__u32		sunit;		/* stripe unit, fsblocks	*/
	__u32		swidth;		/* stripe width, fsblocks	*/
	__s32		version;	/* structure version		*/
	__u32		flags;		/* superblock version flags	*/
	__u32		logsectsize;	/* log sector size, bytes	*/
	__u32		rtsectsize;	/* realtime sector size, bytes	*/
	__u32		dirblocksize;	/* directory block size, bytes	*/
};

/*
 * Output for XFS_IOC_FSGEOMETRY_V4
 */
struct xfs_fsop_geom_v4 {
	__u32		blocksize;	/* filesystem (data) block size */
	__u32		rtextsize;	/* realtime extent size		*/
	__u32		agblocks;	/* fsblocks in an AG		*/
	__u32		agcount;	/* number of allocation groups	*/
	__u32		logblocks;	/* fsblocks in the log		*/
	__u32		sectsize;	/* (data) sector size, bytes	*/
	__u32		inodesize;	/* inode size in bytes		*/
	__u32		imaxpct;	/* max allowed inode space(%)	*/
	__u64		datablocks;	/* fsblocks in data subvolume	*/
	__u64		rtblocks;	/* fsblocks in realtime subvol	*/
	__u64		rtextents;	/* rt extents in realtime subvol*/
	__u64		logstart;	/* starting fsblock of the log	*/
	unsigned char	uuid[16];	/* unique id of the filesystem	*/
	__u32		sunit;		/* stripe unit, fsblocks	*/
	__u32		swidth;		/* stripe width, fsblocks	*/
	__s32		version;	/* structure version		*/
	__u32		flags;		/* superblock version flags	*/
	__u32		logsectsize;	/* log sector size, bytes	*/
	__u32		rtsectsize;	/* realtime sector size, bytes	*/
	__u32		dirblocksize;	/* directory block size, bytes	*/
	__u32		logsunit;	/* log stripe unit, bytes	*/
};

/*
 * Output for XFS_IOC_FSGEOMETRY
 */
struct xfs_fsop_geom {
	__u32		blocksize;	/* filesystem (data) block size */
	__u32		rtextsize;	/* realtime extent size		*/
	__u32		agblocks;	/* fsblocks in an AG		*/
	__u32		agcount;	/* number of allocation groups	*/
	__u32		logblocks;	/* fsblocks in the log		*/
	__u32		sectsize;	/* (data) sector size, bytes	*/
	__u32		inodesize;	/* inode size in bytes		*/
	__u32		imaxpct;	/* max allowed inode space(%)	*/
	__u64		datablocks;	/* fsblocks in data subvolume	*/
	__u64		rtblocks;	/* fsblocks in realtime subvol	*/
	__u64		rtextents;	/* rt extents in realtime subvol*/
	__u64		logstart;	/* starting fsblock of the log	*/
	unsigned char	uuid[16];	/* unique id of the filesystem	*/
	__u32		sunit;		/* stripe unit, fsblocks	*/
	__u32		swidth;		/* stripe width, fsblocks	*/
	__s32		version;	/* structure version		*/
	__u32		flags;		/* superblock version flags	*/
	__u32		logsectsize;	/* log sector size, bytes	*/
	__u32		rtsectsize;	/* realtime sector size, bytes	*/
	__u32		dirblocksize;	/* directory block size, bytes	*/
	__u32		logsunit;	/* log stripe unit, bytes	*/
	uint32_t	sick;		/* o: unhealthy fs & rt metadata */
	uint32_t	checked;	/* o: checked fs & rt metadata	*/
	__u64		reserved[17];	/* reserved space		*/
};

#define XFS_FSOP_GEOM_SICK_COUNTERS	(1 << 0)  /* summary counters */
#define XFS_FSOP_GEOM_SICK_UQUOTA	(1 << 1)  /* user quota */
#define XFS_FSOP_GEOM_SICK_GQUOTA	(1 << 2)  /* group quota */
#define XFS_FSOP_GEOM_SICK_PQUOTA	(1 << 3)  /* project quota */
#define XFS_FSOP_GEOM_SICK_RT_BITMAP	(1 << 4)  /* realtime bitmap */
#define XFS_FSOP_GEOM_SICK_RT_SUMMARY	(1 << 5)  /* realtime summary */
#define XFS_FSOP_GEOM_SICK_QUOTACHECK	(1 << 6)  /* quota counts */
#define XFS_FSOP_GEOM_SICK_NLINKS	(1 << 7)  /* inode link counts */

/* Output for XFS_FS_COUNTS */
typedef struct xfs_fsop_counts {
	__u64	freedata;	/* free data section blocks */
	__u64	freertx;	/* free rt extents */
	__u64	freeino;	/* free inodes */
	__u64	allocino;	/* total allocated inodes */
} xfs_fsop_counts_t;

/* Input/Output for XFS_GET_RESBLKS and XFS_SET_RESBLKS */
typedef struct xfs_fsop_resblks {
	__u64  resblks;
	__u64  resblks_avail;
} xfs_fsop_resblks_t;

#define XFS_FSOP_GEOM_VERSION		0
#define XFS_FSOP_GEOM_VERSION_V5	5

#define XFS_FSOP_GEOM_FLAGS_ATTR	(1 << 0)  /* attributes in use	   */
#define XFS_FSOP_GEOM_FLAGS_NLINK	(1 << 1)  /* 32-bit nlink values   */
#define XFS_FSOP_GEOM_FLAGS_QUOTA	(1 << 2)  /* quotas enabled	   */
#define XFS_FSOP_GEOM_FLAGS_IALIGN	(1 << 3)  /* inode alignment	   */
#define XFS_FSOP_GEOM_FLAGS_DALIGN	(1 << 4)  /* large data alignment  */
#define XFS_FSOP_GEOM_FLAGS_SHARED	(1 << 5)  /* read-only shared	   */
#define XFS_FSOP_GEOM_FLAGS_EXTFLG	(1 << 6)  /* special extent flag   */
#define XFS_FSOP_GEOM_FLAGS_DIRV2	(1 << 7)  /* directory version 2   */
#define XFS_FSOP_GEOM_FLAGS_LOGV2	(1 << 8)  /* log format version 2  */
#define XFS_FSOP_GEOM_FLAGS_SECTOR	(1 << 9)  /* sector sizes >1BB	   */
#define XFS_FSOP_GEOM_FLAGS_ATTR2	(1 << 10) /* inline attributes rework */
#define XFS_FSOP_GEOM_FLAGS_PROJID32	(1 << 11) /* 32-bit project IDs	   */
#define XFS_FSOP_GEOM_FLAGS_DIRV2CI	(1 << 12) /* ASCII only CI names   */
	/*  -- Do not use --		(1 << 13)    SGI parent pointers   */
#define XFS_FSOP_GEOM_FLAGS_LAZYSB	(1 << 14) /* lazy superblock counters */
#define XFS_FSOP_GEOM_FLAGS_V5SB	(1 << 15) /* version 5 superblock  */
#define XFS_FSOP_GEOM_FLAGS_FTYPE	(1 << 16) /* inode directory types */
#define XFS_FSOP_GEOM_FLAGS_FINOBT	(1 << 17) /* free inode btree	   */
#define XFS_FSOP_GEOM_FLAGS_SPINODES	(1 << 18) /* sparse inode chunks   */
#define XFS_FSOP_GEOM_FLAGS_RMAPBT	(1 << 19) /* reverse mapping btree */
#define XFS_FSOP_GEOM_FLAGS_REFLINK	(1 << 20) /* files can share blocks */
#define XFS_FSOP_GEOM_FLAGS_BIGTIME	(1 << 21) /* 64-bit nsec timestamps */
#define XFS_FSOP_GEOM_FLAGS_INOBTCNT	(1 << 22) /* inobt btree counter */
#define XFS_FSOP_GEOM_FLAGS_NREXT64	(1 << 23) /* large extent counters */
#define XFS_FSOP_GEOM_FLAGS_EXCHANGE_RANGE (1 << 24) /* exchange range */
#define XFS_FSOP_GEOM_FLAGS_PARENT	(1 << 25) /* linux parent pointers */

/*
 * Minimum and maximum sizes need for growth checks.
 *
 * Block counts are in units of filesystem blocks, not basic blocks.
 */
#define XFS_MIN_AG_BLOCKS	64
#define XFS_MIN_LOG_BLOCKS	512ULL
#define XFS_MAX_LOG_BLOCKS	(1024 * 1024ULL)
#define XFS_MIN_LOG_BYTES	(10 * 1024 * 1024ULL)

/*
 * Limits on sb_agblocks/sb_agblklog -- mkfs won't format AGs smaller than
 * 16MB or larger than 1TB.
 */
#define XFS_MIN_AG_BYTES	(1ULL << 24)	/* 16 MB */
#define XFS_MAX_AG_BYTES	(1ULL << 40)	/* 1 TB */
#define XFS_MAX_AG_BLOCKS	(XFS_MAX_AG_BYTES / XFS_MIN_BLOCKSIZE)
#define XFS_MAX_CRC_AG_BLOCKS	(XFS_MAX_AG_BYTES / XFS_MIN_CRC_BLOCKSIZE)

#define XFS_MAX_AGNUMBER	((xfs_agnumber_t)(NULLAGNUMBER - 1))

/* keep the maximum size under 2^31 by a small amount */
#define XFS_MAX_LOG_BYTES \
	((2 * 1024 * 1024 * 1024ULL) - XFS_MIN_LOG_BYTES)

/* Used for sanity checks on superblock */
#define XFS_MAX_DBLOCKS(s) ((xfs_rfsblock_t)(s)->sb_agcount * (s)->sb_agblocks)
#define XFS_MIN_DBLOCKS(s) ((xfs_rfsblock_t)((s)->sb_agcount - 1) *	\
			 (s)->sb_agblocks + XFS_MIN_AG_BLOCKS)

/*
 * Output for XFS_IOC_AG_GEOMETRY
 */
struct xfs_ag_geometry {
	uint32_t	ag_number;	/* i/o: AG number */
	uint32_t	ag_length;	/* o: length in blocks */
	uint32_t	ag_freeblks;	/* o: free space */
	uint32_t	ag_icount;	/* o: inodes allocated */
	uint32_t	ag_ifree;	/* o: inodes free */
	uint32_t	ag_sick;	/* o: sick things in ag */
	uint32_t	ag_checked;	/* o: checked metadata in ag */
	uint32_t	ag_flags;	/* i/o: flags for this ag */
	uint64_t	ag_reserved[12];/* o: zero */
};
#define XFS_AG_GEOM_SICK_SB	(1 << 0)  /* superblock */
#define XFS_AG_GEOM_SICK_AGF	(1 << 1)  /* AGF header */
#define XFS_AG_GEOM_SICK_AGFL	(1 << 2)  /* AGFL header */
#define XFS_AG_GEOM_SICK_AGI	(1 << 3)  /* AGI header */
#define XFS_AG_GEOM_SICK_BNOBT	(1 << 4)  /* free space by block */
#define XFS_AG_GEOM_SICK_CNTBT	(1 << 5)  /* free space by length */
#define XFS_AG_GEOM_SICK_INOBT	(1 << 6)  /* inode index */
#define XFS_AG_GEOM_SICK_FINOBT	(1 << 7)  /* free inode index */
#define XFS_AG_GEOM_SICK_RMAPBT	(1 << 8)  /* reverse mappings */
#define XFS_AG_GEOM_SICK_REFCNTBT (1 << 9)  /* reference counts */
#define XFS_AG_GEOM_SICK_INODES	(1 << 10) /* bad inodes were seen */

/*
 * Structures for XFS_IOC_FSGROWFSDATA, XFS_IOC_FSGROWFSLOG & XFS_IOC_FSGROWFSRT
 */
typedef struct xfs_growfs_data {
	__u64		newblocks;	/* new data subvol size, fsblocks */
	__u32		imaxpct;	/* new inode space percentage limit */
} xfs_growfs_data_t;

typedef struct xfs_growfs_log {
	__u32		newblocks;	/* new log size, fsblocks */
	__u32		isint;		/* 1 if new log is internal */
} xfs_growfs_log_t;

typedef struct xfs_growfs_rt {
	__u64		newblocks;	/* new realtime size, fsblocks */
	__u32		extsize;	/* new realtime extent size, fsblocks */
} xfs_growfs_rt_t;


/*
 * Structures returned from ioctl XFS_IOC_FSBULKSTAT & XFS_IOC_FSBULKSTAT_SINGLE
 */
typedef struct xfs_bstime {
	__kernel_long_t tv_sec;		/* seconds		*/
	__s32		tv_nsec;	/* and nanoseconds	*/
} xfs_bstime_t;

struct xfs_bstat {
	__u64		bs_ino;		/* inode number			*/
	__u16		bs_mode;	/* type and mode		*/
	__u16		bs_nlink;	/* number of links		*/
	__u32		bs_uid;		/* user id			*/
	__u32		bs_gid;		/* group id			*/
	__u32		bs_rdev;	/* device value			*/
	__s32		bs_blksize;	/* block size			*/
	__s64		bs_size;	/* file size			*/
	xfs_bstime_t	bs_atime;	/* access time			*/
	xfs_bstime_t	bs_mtime;	/* modify time			*/
	xfs_bstime_t	bs_ctime;	/* inode change time		*/
	int64_t		bs_blocks;	/* number of blocks		*/
	__u32		bs_xflags;	/* extended flags		*/
	__s32		bs_extsize;	/* extent size			*/
	__s32		bs_extents;	/* number of extents		*/
	__u32		bs_gen;		/* generation count		*/
	__u16		bs_projid_lo;	/* lower part of project id	*/
#define	bs_projid	bs_projid_lo	/* (previously just bs_projid)	*/
	__u16		bs_forkoff;	/* inode fork offset in bytes	*/
	__u16		bs_projid_hi;	/* higher part of project id	*/
	uint16_t	bs_sick;	/* sick inode metadata		*/
	uint16_t	bs_checked;	/* checked inode metadata	*/
	unsigned char	bs_pad[2];	/* pad space, unused		*/
	__u32		bs_cowextsize;	/* cow extent size		*/
	__u32		bs_dmevmask;	/* DMIG event mask		*/
	__u16		bs_dmstate;	/* DMIG state info		*/
	__u16		bs_aextents;	/* attribute number of extents	*/
};

/* New bulkstat structure that reports v5 features and fixes padding issues */
struct xfs_bulkstat {
	uint64_t	bs_ino;		/* inode number			*/
	uint64_t	bs_size;	/* file size			*/

	uint64_t	bs_blocks;	/* number of blocks		*/
	uint64_t	bs_xflags;	/* extended flags		*/

	int64_t		bs_atime;	/* access time, seconds		*/
	int64_t		bs_mtime;	/* modify time, seconds		*/

	int64_t		bs_ctime;	/* inode change time, seconds	*/
	int64_t		bs_btime;	/* creation time, seconds	*/

	uint32_t	bs_gen;		/* generation count		*/
	uint32_t	bs_uid;		/* user id			*/
	uint32_t	bs_gid;		/* group id			*/
	uint32_t	bs_projectid;	/* project id			*/

	uint32_t	bs_atime_nsec;	/* access time, nanoseconds	*/
	uint32_t	bs_mtime_nsec;	/* modify time, nanoseconds	*/
	uint32_t	bs_ctime_nsec;	/* inode change time, nanoseconds */
	uint32_t	bs_btime_nsec;	/* creation time, nanoseconds	*/

	uint32_t	bs_blksize;	/* block size			*/
	uint32_t	bs_rdev;	/* device value			*/
	uint32_t	bs_cowextsize_blks; /* cow extent size hint, blocks */
	uint32_t	bs_extsize_blks; /* extent size hint, blocks	*/

	uint32_t	bs_nlink;	/* number of links		*/
	uint32_t	bs_extents;	/* 32-bit data fork extent counter */
	uint32_t	bs_aextents;	/* attribute number of extents	*/
	uint16_t	bs_version;	/* structure version		*/
	uint16_t	bs_forkoff;	/* inode fork offset in bytes	*/

	uint16_t	bs_sick;	/* sick inode metadata		*/
	uint16_t	bs_checked;	/* checked inode metadata	*/
	uint16_t	bs_mode;	/* type and mode		*/
	uint16_t	bs_pad2;	/* zeroed			*/
	uint64_t	bs_extents64;	/* 64-bit data fork extent counter */

	uint64_t	bs_pad[6];	/* zeroed			*/
};

#define XFS_BULKSTAT_VERSION_V1	(1)
#define XFS_BULKSTAT_VERSION_V5	(5)

/* bs_sick flags */
#define XFS_BS_SICK_INODE	(1 << 0)  /* inode core */
#define XFS_BS_SICK_BMBTD	(1 << 1)  /* data fork */
#define XFS_BS_SICK_BMBTA	(1 << 2)  /* attr fork */
#define XFS_BS_SICK_BMBTC	(1 << 3)  /* cow fork */
#define XFS_BS_SICK_DIR		(1 << 4)  /* directory */
#define XFS_BS_SICK_XATTR	(1 << 5)  /* extended attributes */
#define XFS_BS_SICK_SYMLINK	(1 << 6)  /* symbolic link remote target */
#define XFS_BS_SICK_PARENT	(1 << 7)  /* parent pointers */
#define XFS_BS_SICK_DIRTREE	(1 << 8)  /* directory tree structure */

/*
 * Project quota id helpers (previously projid was 16bit only
 * and using two 16bit values to hold new 32bit projid was chosen
 * to retain compatibility with "old" filesystems).
 */
static inline uint32_t
bstat_get_projid(const struct xfs_bstat *bs)
{
	return (uint32_t)bs->bs_projid_hi << 16 | bs->bs_projid_lo;
}

/*
 * The user-level BulkStat Request interface structure.
 */
struct xfs_fsop_bulkreq {
	__u64		__user *lastip;	/* last inode # pointer		*/
	__s32		icount;		/* count of entries in buffer	*/
	void		__user *ubuffer;/* user buffer for inode desc.	*/
	__s32		__user *ocount;	/* output count pointer		*/
};

/*
 * Structures returned from xfs_inumbers routine (XFS_IOC_FSINUMBERS).
 */
struct xfs_inogrp {
	__u64		xi_startino;	/* starting inode number	*/
	__s32		xi_alloccount;	/* # bits set in allocmask	*/
	__u64		xi_allocmask;	/* mask of allocated inodes	*/
};

/* New inumbers structure that reports v5 features and fixes padding issues */
struct xfs_inumbers {
	uint64_t	xi_startino;	/* starting inode number	*/
	uint64_t	xi_allocmask;	/* mask of allocated inodes	*/
	uint8_t		xi_alloccount;	/* # bits set in allocmask	*/
	uint8_t		xi_version;	/* version			*/
	uint8_t		xi_padding[6];	/* zero				*/
};

#define XFS_INUMBERS_VERSION_V1	(1)
#define XFS_INUMBERS_VERSION_V5	(5)

/* Header for bulk inode requests. */
struct xfs_bulk_ireq {
	uint64_t	ino;		/* I/O: start with this inode	*/
	uint32_t	flags;		/* I/O: operation flags		*/
	uint32_t	icount;		/* I: count of entries in buffer */
	uint32_t	ocount;		/* O: count of entries filled out */
	uint32_t	agno;		/* I: see comment for IREQ_AGNO	*/
	uint64_t	reserved[5];	/* must be zero			*/
};

/*
 * Only return results from the specified @agno.  If @ino is zero, start
 * with the first inode of @agno.
 */
#define XFS_BULK_IREQ_AGNO	(1U << 0)

/*
 * Return bulkstat information for a single inode, where @ino value is a
 * special value, not a literal inode number.  See the XFS_BULK_IREQ_SPECIAL_*
 * values below.  Not compatible with XFS_BULK_IREQ_AGNO.
 */
#define XFS_BULK_IREQ_SPECIAL	(1U << 1)

/*
 * Return data fork extent count via xfs_bulkstat->bs_extents64 field and assign
 * 0 to xfs_bulkstat->bs_extents when the flag is set.  Otherwise, use
 * xfs_bulkstat->bs_extents for returning data fork extent count and set
 * xfs_bulkstat->bs_extents64 to 0. In the second case, return -EOVERFLOW and
 * assign 0 to xfs_bulkstat->bs_extents if data fork extent count is larger than
 * XFS_MAX_EXTCNT_DATA_FORK_OLD.
 */
#define XFS_BULK_IREQ_NREXT64	(1U << 2)

#define XFS_BULK_IREQ_FLAGS_ALL	(XFS_BULK_IREQ_AGNO |	 \
				 XFS_BULK_IREQ_SPECIAL | \
				 XFS_BULK_IREQ_NREXT64)

/* Operate on the root directory inode. */
#define XFS_BULK_IREQ_SPECIAL_ROOT	(1)

/*
 * ioctl structures for v5 bulkstat and inumbers requests
 */
struct xfs_bulkstat_req {
	struct xfs_bulk_ireq	hdr;
	struct xfs_bulkstat	bulkstat[];
};
#define XFS_BULKSTAT_REQ_SIZE(nr)	(sizeof(struct xfs_bulkstat_req) + \
					 (nr) * sizeof(struct xfs_bulkstat))

struct xfs_inumbers_req {
	struct xfs_bulk_ireq	hdr;
	struct xfs_inumbers	inumbers[];
};
#define XFS_INUMBERS_REQ_SIZE(nr)	(sizeof(struct xfs_inumbers_req) + \
					 (nr) * sizeof(struct xfs_inumbers))

/*
 * Error injection.
 */
typedef struct xfs_error_injection {
	__s32		fd;
	__s32		errtag;
} xfs_error_injection_t;


/*
 * Speculative preallocation trimming.
 */
#define XFS_EOFBLOCKS_VERSION		1
struct xfs_fs_eofblocks {
	__u32		eof_version;
	__u32		eof_flags;
	uid_t		eof_uid;
	gid_t		eof_gid;
	prid_t		eof_prid;
	__u32		pad32;
	__u64		eof_min_file_size;
	__u64		pad64[12];
};

/* eof_flags values */
#define XFS_EOF_FLAGS_SYNC		(1 << 0) /* sync/wait mode scan */
#define XFS_EOF_FLAGS_UID		(1 << 1) /* filter by uid */
#define XFS_EOF_FLAGS_GID		(1 << 2) /* filter by gid */
#define XFS_EOF_FLAGS_PRID		(1 << 3) /* filter by project id */
#define XFS_EOF_FLAGS_MINFILESIZE	(1 << 4) /* filter by min file size */
#define XFS_EOF_FLAGS_UNION		(1 << 5) /* union filter algorithm;
						  * kernel only, not included in
						  * valid mask */
#define XFS_EOF_FLAGS_VALID	\
	(XFS_EOF_FLAGS_SYNC |	\
	 XFS_EOF_FLAGS_UID |	\
	 XFS_EOF_FLAGS_GID |	\
	 XFS_EOF_FLAGS_PRID |	\
	 XFS_EOF_FLAGS_MINFILESIZE)


/*
 * The user-level Handle Request interface structure.
 */
typedef struct xfs_fsop_handlereq {
	__u32		fd;		/* fd for FD_TO_HANDLE		*/
	void		__user *path;	/* user pathname		*/
	__u32		oflags;		/* open flags			*/
	void		__user *ihandle;/* user supplied handle		*/
	__u32		ihandlen;	/* user supplied length		*/
	void		__user *ohandle;/* user buffer for handle	*/
	__u32		__user *ohandlen;/* user buffer length		*/
} xfs_fsop_handlereq_t;

/*
 * Compound structures for passing args through Handle Request interfaces
 * xfs_attrlist_by_handle, xfs_attrmulti_by_handle
 * - ioctls: XFS_IOC_ATTRLIST_BY_HANDLE, and XFS_IOC_ATTRMULTI_BY_HANDLE
 */

/*
 * Flags passed in xfs_attr_multiop.am_flags for the attr ioctl interface.
 *
 * NOTE: Must match the values declared in libattr without the XFS_IOC_ prefix.
 */
#define XFS_IOC_ATTR_ROOT	0x0002	/* use attrs in root namespace */
#define XFS_IOC_ATTR_SECURE	0x0008	/* use attrs in security namespace */
#define XFS_IOC_ATTR_CREATE	0x0010	/* fail if attr already exists */
#define XFS_IOC_ATTR_REPLACE	0x0020	/* fail if attr does not exist */

typedef struct xfs_attrlist_cursor {
	__u32		opaque[4];
} xfs_attrlist_cursor_t;

/*
 * Define how lists of attribute names are returned to userspace from the
 * XFS_IOC_ATTRLIST_BY_HANDLE ioctl.  struct xfs_attrlist is the header at the
 * beginning of the returned buffer, and a each entry in al_offset contains the
 * relative offset of an xfs_attrlist_ent containing the actual entry.
 *
 * NOTE: struct xfs_attrlist must match struct attrlist defined in libattr, and
 * struct xfs_attrlist_ent must match struct attrlist_ent defined in libattr.
 */
struct xfs_attrlist {
	__s32	al_count;	/* number of entries in attrlist */
	__s32	al_more;	/* T/F: more attrs (do call again) */
	__s32	al_offset[];	/* byte offsets of attrs [var-sized] */
};

struct xfs_attrlist_ent {	/* data from attr_list() */
	__u32	a_valuelen;	/* number bytes in value of attr */
	char	a_name[];	/* attr name (NULL terminated) */
};

typedef struct xfs_fsop_attrlist_handlereq {
	struct xfs_fsop_handlereq	hreq; /* handle interface structure */
	struct xfs_attrlist_cursor	pos; /* opaque cookie, list offset */
	__u32				flags;	/* which namespace to use */
	__u32				buflen;	/* length of buffer supplied */
	void				__user *buffer;	/* returned names */
} xfs_fsop_attrlist_handlereq_t;

typedef struct xfs_attr_multiop {
	__u32		am_opcode;
#define ATTR_OP_GET	1	/* return the indicated attr's value */
#define ATTR_OP_SET	2	/* set/create the indicated attr/value pair */
#define ATTR_OP_REMOVE	3	/* remove the indicated attr */
	__s32		am_error;
	void		__user *am_attrname;
	void		__user *am_attrvalue;
	__u32		am_length;
	__u32		am_flags; /* XFS_IOC_ATTR_* */
} xfs_attr_multiop_t;

typedef struct xfs_fsop_attrmulti_handlereq {
	struct xfs_fsop_handlereq	hreq; /* handle interface structure */
	__u32				opcount;/* count of following multiop */
	struct xfs_attr_multiop		__user *ops; /* attr_multi data */
} xfs_fsop_attrmulti_handlereq_t;

/*
 * per machine unique filesystem identifier types.
 */
typedef struct xfs_fsid {
	__u32	val[2];			/* file system id type */
} xfs_fsid_t;

typedef struct xfs_fid {
	__u16	fid_len;		/* length of remainder	*/
	__u16	fid_pad;
	__u32	fid_gen;		/* generation number	*/
	__u64	fid_ino;		/* 64 bits inode number */
} xfs_fid_t;

typedef struct xfs_handle {
	union {
		__s64	    align;	/* force alignment of ha_fid	 */
		xfs_fsid_t  _ha_fsid;	/* unique file system identifier */
	} ha_u;
	xfs_fid_t	ha_fid;		/* file system specific file ID	 */
} xfs_handle_t;
#define ha_fsid ha_u._ha_fsid

/*
 * Structure passed to XFS_IOC_SWAPEXT
 */
typedef struct xfs_swapext
{
	int64_t		sx_version;	/* version */
#define XFS_SX_VERSION		0
	int64_t		sx_fdtarget;	/* fd of target file */
	int64_t		sx_fdtmp;	/* fd of tmp file */
	xfs_off_t	sx_offset;	/* offset into file */
	xfs_off_t	sx_length;	/* leng from offset */
	char		sx_pad[16];	/* pad space, unused */
	struct xfs_bstat sx_stat;	/* stat of target b4 copy */
} xfs_swapext_t;

/*
 * Flags for going down operation
 */
#define XFS_FSOP_GOING_FLAGS_DEFAULT		0x0	/* going down */
#define XFS_FSOP_GOING_FLAGS_LOGFLUSH		0x1	/* flush log but not data */
#define XFS_FSOP_GOING_FLAGS_NOLOGFLUSH		0x2	/* don't flush log nor data */

/* metadata scrubbing */
struct xfs_scrub_metadata {
	__u32 sm_type;		/* What to check? */
	__u32 sm_flags;		/* flags; see below. */
	__u64 sm_ino;		/* inode number. */
	__u32 sm_gen;		/* inode generation. */
	__u32 sm_agno;		/* ag number. */
	__u64 sm_reserved[5];	/* pad to 64 bytes */
};

/*
 * Metadata types and flags for scrub operation.
 */

/* Scrub subcommands. */
#define XFS_SCRUB_TYPE_PROBE	0	/* presence test ioctl */
#define XFS_SCRUB_TYPE_SB	1	/* superblock */
#define XFS_SCRUB_TYPE_AGF	2	/* AG free header */
#define XFS_SCRUB_TYPE_AGFL	3	/* AG free list */
#define XFS_SCRUB_TYPE_AGI	4	/* AG inode header */
#define XFS_SCRUB_TYPE_BNOBT	5	/* freesp by block btree */
#define XFS_SCRUB_TYPE_CNTBT	6	/* freesp by length btree */
#define XFS_SCRUB_TYPE_INOBT	7	/* inode btree */
#define XFS_SCRUB_TYPE_FINOBT	8	/* free inode btree */
#define XFS_SCRUB_TYPE_RMAPBT	9	/* reverse mapping btree */
#define XFS_SCRUB_TYPE_REFCNTBT	10	/* reference count btree */
#define XFS_SCRUB_TYPE_INODE	11	/* inode record */
#define XFS_SCRUB_TYPE_BMBTD	12	/* data fork block mapping */
#define XFS_SCRUB_TYPE_BMBTA	13	/* attr fork block mapping */
#define XFS_SCRUB_TYPE_BMBTC	14	/* CoW fork block mapping */
#define XFS_SCRUB_TYPE_DIR	15	/* directory */
#define XFS_SCRUB_TYPE_XATTR	16	/* extended attribute */
#define XFS_SCRUB_TYPE_SYMLINK	17	/* symbolic link */
#define XFS_SCRUB_TYPE_PARENT	18	/* parent pointers */
#define XFS_SCRUB_TYPE_RTBITMAP	19	/* realtime bitmap */
#define XFS_SCRUB_TYPE_RTSUM	20	/* realtime summary */
#define XFS_SCRUB_TYPE_UQUOTA	21	/* user quotas */
#define XFS_SCRUB_TYPE_GQUOTA	22	/* group quotas */
#define XFS_SCRUB_TYPE_PQUOTA	23	/* project quotas */
#define XFS_SCRUB_TYPE_FSCOUNTERS 24	/* fs summary counters */
#define XFS_SCRUB_TYPE_QUOTACHECK 25	/* quota counters */
#define XFS_SCRUB_TYPE_NLINKS	26	/* inode link counts */
#define XFS_SCRUB_TYPE_HEALTHY	27	/* everything checked out ok */
#define XFS_SCRUB_TYPE_DIRTREE	28	/* directory tree structure */

/* Number of scrub subcommands. */
#define XFS_SCRUB_TYPE_NR	29

/*
 * This special type code only applies to the vectored scrub implementation.
 *
 * If any of the previous scrub vectors recorded runtime errors or have
 * sv_flags bits set that match the OFLAG bits in the barrier vector's
 * sv_flags, set the barrier's sv_ret to -ECANCELED and return to userspace.
 */
#define XFS_SCRUB_TYPE_BARRIER	(0xFFFFFFFF)

/* i: Repair this metadata. */
#define XFS_SCRUB_IFLAG_REPAIR		(1u << 0)

/* o: Metadata object needs repair. */
#define XFS_SCRUB_OFLAG_CORRUPT		(1u << 1)

/*
 * o: Metadata object could be optimized.  It's not corrupt, but
 *    we could improve on it somehow.
 */
#define XFS_SCRUB_OFLAG_PREEN		(1u << 2)

/* o: Cross-referencing failed. */
#define XFS_SCRUB_OFLAG_XFAIL		(1u << 3)

/* o: Metadata object disagrees with cross-referenced metadata. */
#define XFS_SCRUB_OFLAG_XCORRUPT	(1u << 4)

/* o: Scan was not complete. */
#define XFS_SCRUB_OFLAG_INCOMPLETE	(1u << 5)

/* o: Metadata object looked funny but isn't corrupt. */
#define XFS_SCRUB_OFLAG_WARNING		(1u << 6)

/*
 * o: IFLAG_REPAIR was set but metadata object did not need fixing or
 *    optimization and has therefore not been altered.
 */
#define XFS_SCRUB_OFLAG_NO_REPAIR_NEEDED (1u << 7)

/* i: Rebuild the data structure. */
#define XFS_SCRUB_IFLAG_FORCE_REBUILD	(1u << 8)

#define XFS_SCRUB_FLAGS_IN	(XFS_SCRUB_IFLAG_REPAIR | \
				 XFS_SCRUB_IFLAG_FORCE_REBUILD)
#define XFS_SCRUB_FLAGS_OUT	(XFS_SCRUB_OFLAG_CORRUPT | \
				 XFS_SCRUB_OFLAG_PREEN | \
				 XFS_SCRUB_OFLAG_XFAIL | \
				 XFS_SCRUB_OFLAG_XCORRUPT | \
				 XFS_SCRUB_OFLAG_INCOMPLETE | \
				 XFS_SCRUB_OFLAG_WARNING | \
				 XFS_SCRUB_OFLAG_NO_REPAIR_NEEDED)
#define XFS_SCRUB_FLAGS_ALL	(XFS_SCRUB_FLAGS_IN | XFS_SCRUB_FLAGS_OUT)

/* Vectored scrub calls to reduce the number of kernel transitions. */

struct xfs_scrub_vec {
	__u32 sv_type;		/* XFS_SCRUB_TYPE_* */
	__u32 sv_flags;		/* XFS_SCRUB_FLAGS_* */
	__s32 sv_ret;		/* 0 or a negative error code */
	__u32 sv_reserved;	/* must be zero */
};

/* Vectored metadata scrub control structure. */
struct xfs_scrub_vec_head {
	__u64 svh_ino;		/* inode number. */
	__u32 svh_gen;		/* inode generation. */
	__u32 svh_agno;		/* ag number. */
	__u32 svh_flags;	/* XFS_SCRUB_VEC_FLAGS_* */
	__u16 svh_rest_us;	/* wait this much time between vector items */
	__u16 svh_nr;		/* number of svh_vectors */
	__u64 svh_reserved;	/* must be zero */
	__u64 svh_vectors;	/* pointer to buffer of xfs_scrub_vec */
};

#define XFS_SCRUB_VEC_FLAGS_ALL		(0)

/*
 * ioctl limits
 */
#ifdef XATTR_LIST_MAX
#  define XFS_XATTR_LIST_MAX XATTR_LIST_MAX
#else
#  define XFS_XATTR_LIST_MAX 65536
#endif

/*
 * Exchange part of file1 with part of the file that this ioctl that is being
 * called against (which we'll call file2).  Filesystems must be able to
 * restart and complete the operation even after the system goes down.
 */
struct xfs_exchange_range {
	__s32		file1_fd;
	__u32		pad;		/* must be zeroes */
	__u64		file1_offset;	/* file1 offset, bytes */
	__u64		file2_offset;	/* file2 offset, bytes */
	__u64		length;		/* bytes to exchange */

	__u64		flags;		/* see XFS_EXCHANGE_RANGE_* below */
};

/*
 * Exchange file data all the way to the ends of both files, and then exchange
 * the file sizes.  This flag can be used to replace a file's contents with a
 * different amount of data.  length will be ignored.
 */
#define XFS_EXCHANGE_RANGE_TO_EOF	(1ULL << 0)

/* Flush all changes in file data and file metadata to disk before returning. */
#define XFS_EXCHANGE_RANGE_DSYNC	(1ULL << 1)

/* Dry run; do all the parameter verification but do not change anything. */
#define XFS_EXCHANGE_RANGE_DRY_RUN	(1ULL << 2)

/*
 * Exchange only the parts of the two files where the file allocation units
 * mapped to file1's range have been written to.  This can accelerate
 * scatter-gather atomic writes with a temp file if all writes are aligned to
 * the file allocation unit.
 */
#define XFS_EXCHANGE_RANGE_FILE1_WRITTEN (1ULL << 3)

#define XFS_EXCHANGE_RANGE_ALL_FLAGS	(XFS_EXCHANGE_RANGE_TO_EOF | \
					 XFS_EXCHANGE_RANGE_DSYNC | \
					 XFS_EXCHANGE_RANGE_DRY_RUN | \
					 XFS_EXCHANGE_RANGE_FILE1_WRITTEN)

/* Iterating parent pointers of files. */

/* target was the root directory */
#define XFS_GETPARENTS_OFLAG_ROOT	(1U << 0)

/* Cursor is done iterating pptrs */
#define XFS_GETPARENTS_OFLAG_DONE	(1U << 1)

#define XFS_GETPARENTS_OFLAGS_ALL	(XFS_GETPARENTS_OFLAG_ROOT | \
					 XFS_GETPARENTS_OFLAG_DONE)

#define XFS_GETPARENTS_IFLAGS_ALL	(0)

struct xfs_getparents_rec {
	struct xfs_handle	gpr_parent; /* Handle to parent */
	__u32			gpr_reclen; /* Length of entire record */
	__u32			gpr_reserved; /* zero */
	char			gpr_name[]; /* Null-terminated filename */
};

/* Iterate through this file's directory parent pointers */
struct xfs_getparents {
	/*
	 * Structure to track progress in iterating the parent pointers.
	 * Must be initialized to zeroes before the first ioctl call, and
	 * not touched by callers after that.
	 */
	struct xfs_attrlist_cursor	gp_cursor;

	/* Input flags: XFS_GETPARENTS_IFLAG* */
	__u16				gp_iflags;

	/* Output flags: XFS_GETPARENTS_OFLAG* */
	__u16				gp_oflags;

	/* Size of the gp_buffer in bytes */
	__u32				gp_bufsize;

	/* Must be set to zero */
	__u64				gp_reserved;

	/* Pointer to a buffer in which to place xfs_getparents_rec */
	__u64				gp_buffer;
};

static inline struct xfs_getparents_rec *
xfs_getparents_first_rec(struct xfs_getparents *gp)
{
	return (struct xfs_getparents_rec *)(uintptr_t)gp->gp_buffer;
}

static inline struct xfs_getparents_rec *
xfs_getparents_next_rec(struct xfs_getparents *gp,
			struct xfs_getparents_rec *gpr)
{
	void *next = ((void *)gpr + gpr->gpr_reclen);
	void *end = (void *)(uintptr_t)(gp->gp_buffer + gp->gp_bufsize);

	if (next >= end)
		return NULL;

	return next;
}

/* Iterate through this file handle's directory parent pointers. */
struct xfs_getparents_by_handle {
	/* Handle to file whose parents we want. */
	struct xfs_handle		gph_handle;

	struct xfs_getparents		gph_request;
};

/*
 * ioctl commands that are used by Linux filesystems
 */
#define XFS_IOC_GETXFLAGS	FS_IOC_GETFLAGS
#define XFS_IOC_SETXFLAGS	FS_IOC_SETFLAGS
#define XFS_IOC_GETVERSION	FS_IOC_GETVERSION

/*
 * ioctl commands that replace IRIX fcntl()'s
 * For 'documentation' purposed more than anything else,
 * the "cmd #" field reflects the IRIX fcntl number.
 */
/*	XFS_IOC_ALLOCSP ------- deprecated 10	 */
/*	XFS_IOC_FREESP -------- deprecated 11	 */
#define XFS_IOC_DIOINFO		_IOR ('X', 30, struct dioattr)
#define XFS_IOC_FSGETXATTR	FS_IOC_FSGETXATTR
#define XFS_IOC_FSSETXATTR	FS_IOC_FSSETXATTR
/*	XFS_IOC_ALLOCSP64 ----- deprecated 36	 */
/*	XFS_IOC_FREESP64 ------ deprecated 37	 */
#define XFS_IOC_GETBMAP		_IOWR('X', 38, struct getbmap)
/*      XFS_IOC_FSSETDM ------- deprecated 39    */
#define XFS_IOC_RESVSP		_IOW ('X', 40, struct xfs_flock64)
#define XFS_IOC_UNRESVSP	_IOW ('X', 41, struct xfs_flock64)
#define XFS_IOC_RESVSP64	_IOW ('X', 42, struct xfs_flock64)
#define XFS_IOC_UNRESVSP64	_IOW ('X', 43, struct xfs_flock64)
#define XFS_IOC_GETBMAPA	_IOWR('X', 44, struct getbmap)
#define XFS_IOC_FSGETXATTRA	_IOR ('X', 45, struct fsxattr)
/*	XFS_IOC_SETBIOSIZE ---- deprecated 46	   */
/*	XFS_IOC_GETBIOSIZE ---- deprecated 47	   */
#define XFS_IOC_GETBMAPX	_IOWR('X', 56, struct getbmap)
#define XFS_IOC_ZERO_RANGE	_IOW ('X', 57, struct xfs_flock64)
#define XFS_IOC_FREE_EOFBLOCKS	_IOR ('X', 58, struct xfs_fs_eofblocks)
/*	XFS_IOC_GETFSMAP ------ hoisted 59         */
#define XFS_IOC_SCRUB_METADATA	_IOWR('X', 60, struct xfs_scrub_metadata)
#define XFS_IOC_AG_GEOMETRY	_IOWR('X', 61, struct xfs_ag_geometry)
#define XFS_IOC_GETPARENTS	_IOWR('X', 62, struct xfs_getparents)
#define XFS_IOC_GETPARENTS_BY_HANDLE _IOWR('X', 63, struct xfs_getparents_by_handle)
#define XFS_IOC_SCRUBV_METADATA	_IOWR('X', 64, struct xfs_scrub_vec_head)

/*
 * ioctl commands that replace IRIX syssgi()'s
 */
#define XFS_IOC_FSGEOMETRY_V1	     _IOR ('X', 100, struct xfs_fsop_geom_v1)
#define XFS_IOC_FSBULKSTAT	     _IOWR('X', 101, struct xfs_fsop_bulkreq)
#define XFS_IOC_FSBULKSTAT_SINGLE    _IOWR('X', 102, struct xfs_fsop_bulkreq)
#define XFS_IOC_FSINUMBERS	     _IOWR('X', 103, struct xfs_fsop_bulkreq)
#define XFS_IOC_PATH_TO_FSHANDLE     _IOWR('X', 104, struct xfs_fsop_handlereq)
#define XFS_IOC_PATH_TO_HANDLE	     _IOWR('X', 105, struct xfs_fsop_handlereq)
#define XFS_IOC_FD_TO_HANDLE	     _IOWR('X', 106, struct xfs_fsop_handlereq)
#define XFS_IOC_OPEN_BY_HANDLE	     _IOWR('X', 107, struct xfs_fsop_handlereq)
#define XFS_IOC_READLINK_BY_HANDLE   _IOWR('X', 108, struct xfs_fsop_handlereq)
#define XFS_IOC_SWAPEXT		     _IOWR('X', 109, struct xfs_swapext)
#define XFS_IOC_FSGROWFSDATA	     _IOW ('X', 110, struct xfs_growfs_data)
#define XFS_IOC_FSGROWFSLOG	     _IOW ('X', 111, struct xfs_growfs_log)
#define XFS_IOC_FSGROWFSRT	     _IOW ('X', 112, struct xfs_growfs_rt)
#define XFS_IOC_FSCOUNTS	     _IOR ('X', 113, struct xfs_fsop_counts)
#define XFS_IOC_SET_RESBLKS	     _IOWR('X', 114, struct xfs_fsop_resblks)
#define XFS_IOC_GET_RESBLKS	     _IOR ('X', 115, struct xfs_fsop_resblks)
#define XFS_IOC_ERROR_INJECTION	     _IOW ('X', 116, struct xfs_error_injection)
#define XFS_IOC_ERROR_CLEARALL	     _IOW ('X', 117, struct xfs_error_injection)
/*	XFS_IOC_ATTRCTL_BY_HANDLE -- deprecated 118	 */

#define XFS_IOC_FREEZE		     _IOWR('X', 119, int)	/* aka FIFREEZE */
#define XFS_IOC_THAW		     _IOWR('X', 120, int)	/* aka FITHAW */

/*      XFS_IOC_FSSETDM_BY_HANDLE -- deprecated 121      */
#define XFS_IOC_ATTRLIST_BY_HANDLE   _IOW ('X', 122, struct xfs_fsop_attrlist_handlereq)
#define XFS_IOC_ATTRMULTI_BY_HANDLE  _IOW ('X', 123, struct xfs_fsop_attrmulti_handlereq)
#define XFS_IOC_FSGEOMETRY_V4	     _IOR ('X', 124, struct xfs_fsop_geom_v4)
#define XFS_IOC_GOINGDOWN	     _IOR ('X', 125, uint32_t)
#define XFS_IOC_FSGEOMETRY	     _IOR ('X', 126, struct xfs_fsop_geom)
#define XFS_IOC_BULKSTAT	     _IOR ('X', 127, struct xfs_bulkstat_req)
#define XFS_IOC_INUMBERS	     _IOR ('X', 128, struct xfs_inumbers_req)
#define XFS_IOC_EXCHANGE_RANGE	     _IOW ('X', 129, struct xfs_exchange_range)
/*	XFS_IOC_GETFSUUID ---------- deprecated 140	 */


#ifndef HAVE_BBMACROS
/*
 * Block I/O parameterization.	A basic block (BB) is the lowest size of
 * filesystem allocation, and must equal 512.  Length units given to bio
 * routines are in BB's.
 */
#define BBSHIFT		9
#define BBSIZE		(1<<BBSHIFT)
#define BBMASK		(BBSIZE-1)
#define BTOBB(bytes)	(((__u64)(bytes) + BBSIZE - 1) >> BBSHIFT)
#define BTOBBT(bytes)	((__u64)(bytes) >> BBSHIFT)
#define BBTOB(bbs)	((bbs) << BBSHIFT)
#endif

#endif	/* __XFS_FS_H__ */
