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
	0xd9c01927	/* GRPQUOTA */\
}

#define V2_INITQVERSIONS {\
	0,		/* USRQUOTA */\
	0		/* GRPQUOTA */\
}

/*
 * The following structure defines the format of the disk quota file
 * (as it appears on disk) - the file is a radix tree whose leaves point
 * to blocks of these structures.
 */
struct v2_disk_dqblk {
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

/*
 * Here are header structures as written on disk and their in-memory copies
 */
/* First generic header */
struct v2_disk_dqheader {
	__le32 dqh_magic;	/* Magic number identifying file */
	__le32 dqh_version;	/* File version */
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

/*
 *  Structure of header of block with quota structures. It is padded to 16 bytes so
 *  there will be space for exactly 21 quota-entries in a block
 */
struct v2_disk_dqdbheader {
	__le32 dqdh_next_free;	/* Number of next block with free entry */
	__le32 dqdh_prev_free;	/* Number of previous block with free entry */
	__le16 dqdh_entries;	/* Number of valid entries in block */
	__le16 dqdh_pad1;
	__le32 dqdh_pad2;
};

#define V2_DQINFOOFF	sizeof(struct v2_disk_dqheader)	/* Offset of info header in file */
#define V2_DQBLKSIZE_BITS	10
#define V2_DQBLKSIZE	(1 << V2_DQBLKSIZE_BITS)	/* Size of block with quota structures */
#define V2_DQTREEOFF	1		/* Offset of tree in file in blocks */
#define V2_DQTREEDEPTH	4		/* Depth of quota tree */
#define V2_DQSTRINBLK	((V2_DQBLKSIZE - sizeof(struct v2_disk_dqdbheader)) / sizeof(struct v2_disk_dqblk))	/* Number of entries in one blocks */

#endif /* _LINUX_QUOTAIO_V2_H */
