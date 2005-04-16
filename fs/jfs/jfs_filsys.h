/*
 *   Copyright (C) International Business Machines Corp., 2000-2003
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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
/* mount time flag to disable journaling to disk */
#define JFS_NOINTEGRITY 0x00000010

/* mount time flags for error handling */
#define JFS_ERR_REMOUNT_RO 0x00000002   /* remount read-only */
#define JFS_ERR_CONTINUE   0x00000004   /* continue */
#define JFS_ERR_PANIC      0x00000008   /* panic */

/* platform option (conditional compilation) */
#define JFS_AIX		0x80000000	/* AIX support */
/*	POSIX name/directory  support */

#define JFS_OS2		0x40000000	/* OS/2 support */
/*	case-insensitive name/directory support */

#define JFS_DFS		0x20000000	/* DCE DFS LFS support */

#define JFS_LINUX      	0x10000000	/* Linux support */
/*	case-sensitive name/directory support */

/* directory option */
#define JFS_UNICODE	0x00000001	/* unicode name */

/* commit option */
#define	JFS_COMMIT	0x00000f00	/* commit option mask */
#define	JFS_GROUPCOMMIT	0x00000100	/* group (of 1) commit */
#define	JFS_LAZYCOMMIT	0x00000200	/* lazy commit */
#define	JFS_TMPFS	0x00000400	/* temporary file system - 
					 * do not log/commit:
					 */

/* log logical volume option */
#define	JFS_INLINELOG	0x00000800	/* inline log within file system */
#define JFS_INLINEMOVE	0x00001000	/* inline log being moved */

/* Secondary aggregate inode table */
#define JFS_BAD_SAIT	0x00010000	/* current secondary ait is bad */

/* sparse regular file support */
#define JFS_SPARSE	0x00020000	/* sparse regular file */

/* DASD Limits		F226941 */
#define JFS_DASD_ENABLED	0x00040000	/* DASD limits enabled */
#define	JFS_DASD_PRIME		0x00080000	/* Prime DASD usage on boot */

/* big endian flag */
#define	JFS_SWAP_BYTES		0x00100000	/* running on big endian computer */

/* Directory index */
#define JFS_DIR_INDEX		0x00200000	/* Persistant index for */
						/* directory entries    */


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

#define DISIZE		512	/* on-disk inode size (in byte) */
#define L2DISIZE	9	/* log2(DISIZE) */

#define IDATASIZE	256	/* inode inline data size */
#define	IXATTRSIZE	128	/* inode inline extended attribute size */

#define XTPAGE_SIZE     4096
#define log2_PAGESIZE     12

#define IAG_SIZE        4096
#define IAG_EXTENT_SIZE 4096
#define	INOSPERIAG	4096	/* number of disk inodes per iag */
#define	L2INOSPERIAG	12	/* l2 number of disk inodes per iag */
#define INOSPEREXT	32	/* number of disk inode per extent */
#define L2INOSPEREXT	5	/* l2 number of disk inode per extent */
#define	IXSIZE		(DISIZE * INOSPEREXT)	/* inode extent size */
#define	INOSPERPAGE	8	/* number of disk inodes per 4K page */
#define	L2INOSPERPAGE	3	/* log2(INOSPERPAGE) */

#define	IAGFREELIST_LWM	64

#define INODE_EXTENT_SIZE	IXSIZE	/* inode extent size */
#define NUM_INODE_PER_EXTENT	INOSPEREXT
#define NUM_INODE_PER_IAG	INOSPERIAG

#define MINBLOCKSIZE		512
#define MAXBLOCKSIZE		4096
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
 * NOTE: since we can't guarantee a physical block size of 512 bytes the use of
 *	 these macros should be removed and the byte offset macros used instead.
 */
#define SUPER1_B	64	/* primary superblock */
#define	AIMAP_B		(SUPER1_B + 8)	/* 1st extent of aggregate inode map */
#define	AITBL_B		(AIMAP_B + 16)	/*
					 * 1st extent of aggregate inode table
					 */
#define	SUPER2_B	(AITBL_B + 32)	/* 2ndary superblock pbn */
#define	BMAP_B		(SUPER2_B + 8)	/* block allocation map */

/*
 * SIZE_OF_SUPER defines the total amount of space reserved on disk for the
 * superblock.  This is not the same as the superblock structure, since all of
 * this space is not currently being used.
 */
#define SIZE_OF_SUPER	PSIZE

/*
 * SIZE_OF_AG_TABLE defines the amount of space reserved to hold the AG table
 */
#define SIZE_OF_AG_TABLE	PSIZE

/*
 * SIZE_OF_MAP_PAGE defines the amount of disk space reserved for each page of
 * the inode allocation map (to hold iag)
 */
#define SIZE_OF_MAP_PAGE	PSIZE

/*
 * fixed byte offset address
 */
#define SUPER1_OFF	0x8000	/* primary superblock */
#define AIMAP_OFF	(SUPER1_OFF + SIZE_OF_SUPER)
					/*
					 * Control page of aggregate inode map
					 * followed by 1st extent of map
					 */
#define AITBL_OFF	(AIMAP_OFF + (SIZE_OF_MAP_PAGE << 1))
					/* 
					 * 1st extent of aggregate inode table
					 */
#define SUPER2_OFF	(AITBL_OFF + INODE_EXTENT_SIZE)
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
 * The following macro defines the byte offset for the first inode extent in
 * the aggregate inode table.  This allows us to find the self inode to find the
 * rest of the table.  Currently this value is 44K.
 */
#define AGGR_INODE_TABLE_START	AITBL_OFF

/*
 *	fixed reserved inode number
 */
/* aggregate inode */
#define AGGR_RESERVED_I	0	/* aggregate inode (reserved) */
#define	AGGREGATE_I	1	/* aggregate inode map inode */
#define	BMAP_I		2	/* aggregate block allocation map inode */
#define	LOG_I		3	/* aggregate inline log inode */
#define BADBLOCK_I	4	/* aggregate bad block inode */
#define	FILESYSTEM_I	16	/* 1st/only fileset inode in ait:
				 * fileset inode map inode
				 */

/* per fileset inode */
#define FILESET_RSVD_I	0	/* fileset inode (reserved) */
#define FILESET_EXT_I	1	/* fileset inode extension */
#define	ROOT_I		2	/* fileset root inode */
#define ACL_I		3	/* fileset ACL inode */

#define FILESET_OBJECT_I 4	/* the first fileset inode available for a file
				 * or directory or link...
				 */
#define FIRST_FILESET_INO 16	/* the first aggregate inode which describes
				 * an inode.  (To fsck this is also the first
				 * inode in part 2 of the agg inode table.)
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
#define FM_DIRTY 0x00000002	/* file system was not unmounted and clean 
				 * when mounted or 
				 * commit failure occurred while being mounted:
				 * fsck() must be run to repair 
				 */
#define	FM_LOGREDO 0x00000004	/* log based recovery (logredo()) failed:
				 * fsck() must be run to repair 
				 */
#define	FM_EXTENDFS 0x00000008	/* file system extendfs() in progress */

#endif				/* _H_JFS_FILSYS */
