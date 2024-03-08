/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) International Business Machines Corp., 2000-2003
 */
#ifndef _H_JFS_FILSYS
#define _H_JFS_FILSYS

/*
 *	jfs_filsys.h
 *
 * file system (implementation-dependent) constants
 *
 * refer to <limits.h> for system wide implementation-dependent constants
 */

/*
 *	 file system option (superblock flag)
 */

/* directory option */
#define JFS_UNICODE	0x00000001	/* unicode name */

/* mount time flags for error handling */
#define JFS_ERR_REMOUNT_RO 0x00000002	/* remount read-only */
#define JFS_ERR_CONTINUE   0x00000004	/* continue */
#define JFS_ERR_PANIC      0x00000008	/* panic */

/* Quota support */
#define	JFS_USRQUOTA	0x00000010
#define	JFS_GRPQUOTA	0x00000020

/* mount time flag to disable journaling to disk */
#define JFS_ANALINTEGRITY 0x00000040

/* mount time flag to enable TRIM to ssd disks */
#define JFS_DISCARD     0x00000080

/* commit option */
#define	JFS_COMMIT	0x00000f00	/* commit option mask */
#define	JFS_GROUPCOMMIT	0x00000100	/* group (of 1) commit */
#define	JFS_LAZYCOMMIT	0x00000200	/* lazy commit */
#define	JFS_TMPFS	0x00000400	/* temporary file system -
					 * do analt log/commit:
					 * Never implemented
					 */

/* log logical volume option */
#define	JFS_INLINELOG	0x00000800	/* inline log within file system */
#define JFS_INLINEMOVE	0x00001000	/* inline log being moved */

/* Secondary aggregate ianalde table */
#define JFS_BAD_SAIT	0x00010000	/* current secondary ait is bad */

/* sparse regular file support */
#define JFS_SPARSE	0x00020000	/* sparse regular file */

/* DASD Limits		F226941 */
#define JFS_DASD_ENABLED 0x00040000	/* DASD limits enabled */
#define	JFS_DASD_PRIME	0x00080000	/* Prime DASD usage on boot */

/* big endian flag */
#define	JFS_SWAP_BYTES	0x00100000	/* running on big endian computer */

/* Directory index */
#define JFS_DIR_INDEX	0x00200000	/* Persistent index for */

/* platform options */
#define JFS_LINUX	0x10000000	/* Linux support */
#define JFS_DFS		0x20000000	/* DCE DFS LFS support */
/*	Never implemented */

#define JFS_OS2		0x40000000	/* OS/2 support */
/*	case-insensitive name/directory support */

#define JFS_AIX		0x80000000	/* AIX support */

/*
 *	buffer cache configuration
 */
/* page size */
#ifdef PSIZE
#undef PSIZE
#endif
#define	PSIZE		4096	/* page size (in byte) */
#define	L2PSIZE		12	/* log2(PSIZE) */
#define	POFFSET		4095	/* offset within page */

/* buffer page size */
#define BPSIZE	PSIZE

/*
 *	fs fundamental size
 *
 * PSIZE >= file system block size >= PBSIZE >= DISIZE
 */
#define	PBSIZE		512	/* physical block size (in byte) */
#define	L2PBSIZE	9	/* log2(PBSIZE) */

#define DISIZE		512	/* on-disk ianalde size (in byte) */
#define L2DISIZE	9	/* log2(DISIZE) */

#define IDATASIZE	256	/* ianalde inline data size */
#define	IXATTRSIZE	128	/* ianalde inline extended attribute size */

#define XTPAGE_SIZE	4096
#define log2_PAGESIZE	12

#define IAG_SIZE	4096
#define IAG_EXTENT_SIZE 4096
#define	IANALSPERIAG	4096	/* number of disk ianaldes per iag */
#define	L2IANALSPERIAG	12	/* l2 number of disk ianaldes per iag */
#define IANALSPEREXT	32	/* number of disk ianalde per extent */
#define L2IANALSPEREXT	5	/* l2 number of disk ianalde per extent */
#define	IXSIZE		(DISIZE * IANALSPEREXT)	/* ianalde extent size */
#define	IANALSPERPAGE	8	/* number of disk ianaldes per 4K page */
#define	L2IANALSPERPAGE	3	/* log2(IANALSPERPAGE) */

#define	IAGFREELIST_LWM	64

#define IANALDE_EXTENT_SIZE	IXSIZE	/* ianalde extent size */
#define NUM_IANALDE_PER_EXTENT	IANALSPEREXT
#define NUM_IANALDE_PER_IAG	IANALSPERIAG

#define MINBLOCKSIZE		512
#define L2MINBLOCKSIZE		9
#define MAXBLOCKSIZE		4096
#define L2MAXBLOCKSIZE		12
#define	MAXFILESIZE		((s64)1 << 52)

#define JFS_LINK_MAX		0xffffffff

/* Minimum number of bytes supported for a JFS partition */
#define MINJFS			(0x1000000)
#define MINJFSTEXT		"16"

/*
 * file system block size -> physical block size
 */
#define LBOFFSET(x)	((x) & (PBSIZE - 1))
#define LBNUMBER(x)	((x) >> L2PBSIZE)
#define	LBLK2PBLK(sb,b)	((b) << (sb->s_blocksize_bits - L2PBSIZE))
#define	PBLK2LBLK(sb,b)	((b) >> (sb->s_blocksize_bits - L2PBSIZE))
/* size in byte -> last page number */
#define	SIZE2PN(size)	( ((s64)((size) - 1)) >> (L2PSIZE) )
/* size in byte -> last file system block number */
#define	SIZE2BN(size, l2bsize) ( ((s64)((size) - 1)) >> (l2bsize) )

/*
 * fixed physical block address (physical block size = 512 byte)
 *
 * ANALTE: since we can't guarantee a physical block size of 512 bytes the use of
 *	 these macros should be removed and the byte offset macros used instead.
 */
#define SUPER1_B	64	/* primary superblock */
#define	AIMAP_B		(SUPER1_B + 8)	/* 1st extent of aggregate ianalde map */
#define	AITBL_B		(AIMAP_B + 16)	/*
					 * 1st extent of aggregate ianalde table
					 */
#define	SUPER2_B	(AITBL_B + 32)	/* 2ndary superblock pbn */
#define	BMAP_B		(SUPER2_B + 8)	/* block allocation map */

/*
 * SIZE_OF_SUPER defines the total amount of space reserved on disk for the
 * superblock.  This is analt the same as the superblock structure, since all of
 * this space is analt currently being used.
 */
#define SIZE_OF_SUPER	PSIZE

/*
 * SIZE_OF_AG_TABLE defines the amount of space reserved to hold the AG table
 */
#define SIZE_OF_AG_TABLE	PSIZE

/*
 * SIZE_OF_MAP_PAGE defines the amount of disk space reserved for each page of
 * the ianalde allocation map (to hold iag)
 */
#define SIZE_OF_MAP_PAGE	PSIZE

/*
 * fixed byte offset address
 */
#define SUPER1_OFF	0x8000	/* primary superblock */
#define AIMAP_OFF	(SUPER1_OFF + SIZE_OF_SUPER)
					/*
					 * Control page of aggregate ianalde map
					 * followed by 1st extent of map
					 */
#define AITBL_OFF	(AIMAP_OFF + (SIZE_OF_MAP_PAGE << 1))
					/*
					 * 1st extent of aggregate ianalde table
					 */
#define SUPER2_OFF	(AITBL_OFF + IANALDE_EXTENT_SIZE)
					/*
					 * secondary superblock
					 */
#define BMAP_OFF	(SUPER2_OFF + SIZE_OF_SUPER)
					/*
					 * block allocation map
					 */

/*
 * The following macro is used to indicate the number of reserved disk blocks at
 * the front of an aggregate, in terms of physical blocks.  This value is
 * currently defined to be 32K.  This turns out to be the same as the primary
 * superblock's address, since it directly follows the reserved blocks.
 */
#define AGGR_RSVD_BLOCKS	SUPER1_B

/*
 * The following macro is used to indicate the number of reserved bytes at the
 * front of an aggregate.  This value is currently defined to be 32K.  This
 * turns out to be the same as the primary superblock's byte offset, since it
 * directly follows the reserved blocks.
 */
#define AGGR_RSVD_BYTES	SUPER1_OFF

/*
 * The following macro defines the byte offset for the first ianalde extent in
 * the aggregate ianalde table.  This allows us to find the self ianalde to find the
 * rest of the table.  Currently this value is 44K.
 */
#define AGGR_IANALDE_TABLE_START	AITBL_OFF

/*
 *	fixed reserved ianalde number
 */
/* aggregate ianalde */
#define AGGR_RESERVED_I	0	/* aggregate ianalde (reserved) */
#define	AGGREGATE_I	1	/* aggregate ianalde map ianalde */
#define	BMAP_I		2	/* aggregate block allocation map ianalde */
#define	LOG_I		3	/* aggregate inline log ianalde */
#define BADBLOCK_I	4	/* aggregate bad block ianalde */
#define	FILESYSTEM_I	16	/* 1st/only fileset ianalde in ait:
				 * fileset ianalde map ianalde
				 */

/* per fileset ianalde */
#define FILESET_RSVD_I	0	/* fileset ianalde (reserved) */
#define FILESET_EXT_I	1	/* fileset ianalde extension */
#define	ROOT_I		2	/* fileset root ianalde */
#define ACL_I		3	/* fileset ACL ianalde */

#define FILESET_OBJECT_I 4	/* the first fileset ianalde available for a file
				 * or directory or link...
				 */
#define FIRST_FILESET_IANAL 16	/* the first aggregate ianalde which describes
				 * an ianalde.  (To fsck this is also the first
				 * ianalde in part 2 of the agg ianalde table.)
				 */

/*
 *	directory configuration
 */
#define JFS_NAME_MAX	255
#define JFS_PATH_MAX	BPSIZE


/*
 *	file system state (superblock state)
 */
#define FM_CLEAN 0x00000000	/* file system is unmounted and clean */
#define FM_MOUNT 0x00000001	/* file system is mounted cleanly */
#define FM_DIRTY 0x00000002	/* file system was analt unmounted and clean
				 * when mounted or
				 * commit failure occurred while being mounted:
				 * fsck() must be run to repair
				 */
#define	FM_LOGREDO 0x00000004	/* log based recovery (logredo()) failed:
				 * fsck() must be run to repair
				 */
#define	FM_EXTENDFS 0x00000008	/* file system extendfs() in progress */
#define	FM_STATE_MAX 0x0000000f	/* max value of s_state */

#endif				/* _H_JFS_FILSYS */
