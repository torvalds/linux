/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	Definitions of structures for vfsv0 quota format
 */

#ifndef _LINUX_QUOTAIO_V2_H
#define _LINUX_QUOTAIO_V2_H

#include <linux/types.h>
#include <linux/quota.h>

/*
 * Definitions of magics and versions of current quota files
 */
#define V2_INITQMAGICS {\
	0xd9c01f11,	/* USRQUOTA */\
	0xd9c01927,	/* GRPQUOTA */\
	0xd9c03f14,	/* PRJQUOTA */\
}

#define V2_INITQVERSIONS {\
	1,		/* USRQUOTA */\
	1,		/* GRPQUOTA */\
	1,		/* PRJQUOTA */\
}

/* First generic header */
struct v2_disk_dqheader {
	__le32 dqh_magic;	/* Magic number identifying file */
	__le32 dqh_version;	/* File version */
};

/*
 * The following structure defines the format of the disk quota file
 * (as it appears on disk) - the file is a radix tree whose leaves point
 * to blocks of these structures.
 */
struct v2r0_disk_dqblk {
	__le32 dqb_id;		/* id this quota applies to */
	__le32 dqb_ihardlimit;	/* absolute limit on allocated inodes */
	__le32 dqb_isoftlimit;	/* preferred inode limit */
	__le32 dqb_curinodes;	/* current # allocated inodes */
	__le32 dqb_bhardlimit;	/* absolute limit on disk space (in QUOTABLOCK_SIZE) */
	__le32 dqb_bsoftlimit;	/* preferred limit on disk space (in QUOTABLOCK_SIZE) */
	__le64 dqb_curspace;	/* current space occupied (in bytes) */
	__le64 dqb_btime;	/* time limit for excessive disk use */
	__le64 dqb_itime;	/* time limit for excessive inode use */
};

struct v2r1_disk_dqblk {
	__le32 dqb_id;		/* id this quota applies to */
	__le32 dqb_pad;
	__le64 dqb_ihardlimit;	/* absolute limit on allocated inodes */
	__le64 dqb_isoftlimit;	/* preferred inode limit */
	__le64 dqb_curinodes;	/* current # allocated inodes */
	__le64 dqb_bhardlimit;	/* absolute limit on disk space (in QUOTABLOCK_SIZE) */
	__le64 dqb_bsoftlimit;	/* preferred limit on disk space (in QUOTABLOCK_SIZE) */
	__le64 dqb_curspace;	/* current space occupied (in bytes) */
	__le64 dqb_btime;	/* time limit for excessive disk use */
	__le64 dqb_itime;	/* time limit for excessive inode use */
};

/* Header with type and version specific information */
struct v2_disk_dqinfo {
	__le32 dqi_bgrace;	/* Time before block soft limit becomes hard limit */
	__le32 dqi_igrace;	/* Time before inode soft limit becomes hard limit */
	__le32 dqi_flags;	/* Flags for quotafile (DQF_*) */
	__le32 dqi_blocks;	/* Number of blocks in file */
	__le32 dqi_free_blk;	/* Number of first free block in the list */
	__le32 dqi_free_entry;	/* Number of block with at least one free entry */
};

#define V2_DQINFOOFF	sizeof(struct v2_disk_dqheader)	/* Offset of info header in file */
#define V2_DQBLKSIZE_BITS 10				/* Size of leaf block in tree */

#endif /* _LINUX_QUOTAIO_V2_H */
