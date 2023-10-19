/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * efs_fs_sb.h
 *
 * Copyright (c) 1999 Al Smith
 *
 * Portions derived from IRIX header files (c) 1988 Silicon Graphics
 */

#ifndef __EFS_FS_SB_H__
#define __EFS_FS_SB_H__

#include <linux/types.h>
#include <linux/magic.h>

/* EFS superblock magic numbers */
#define EFS_MAGIC	0x072959
#define EFS_NEWMAGIC	0x07295a

#define IS_EFS_MAGIC(x)	((x == EFS_MAGIC) || (x == EFS_NEWMAGIC))

#define EFS_SUPER		1
#define EFS_ROOTINODE		2

/* efs superblock on disk */
struct efs_super {
	__be32		fs_size;        /* size of filesystem, in sectors */
	__be32		fs_firstcg;     /* bb offset to first cg */
	__be32		fs_cgfsize;     /* size of cylinder group in bb's */
	__be16		fs_cgisize;     /* bb's of inodes per cylinder group */
	__be16		fs_sectors;     /* sectors per track */
	__be16		fs_heads;       /* heads per cylinder */
	__be16		fs_ncg;         /* # of cylinder groups in filesystem */
	__be16		fs_dirty;       /* fs needs to be fsck'd */
	__be32		fs_time;        /* last super-block update */
	__be32		fs_magic;       /* magic number */
	char		fs_fname[6];    /* file system name */
	char		fs_fpack[6];    /* file system pack name */
	__be32		fs_bmsize;      /* size of bitmap in bytes */
	__be32		fs_tfree;       /* total free data blocks */
	__be32		fs_tinode;      /* total free inodes */
	__be32		fs_bmblock;     /* bitmap location. */
	__be32		fs_replsb;      /* Location of replicated superblock. */
	__be32		fs_lastialloc;  /* last allocated inode */
	char		fs_spare[20];   /* space for expansion - MUST BE ZERO */
	__be32		fs_checksum;    /* checksum of volume portion of fs */
};

/* efs superblock information in memory */
struct efs_sb_info {
	__u32	fs_magic;	/* superblock magic number */
	__u32	fs_start;	/* first block of filesystem */
	__u32	first_block;	/* first data block in filesystem */
	__u32	total_blocks;	/* total number of blocks in filesystem */
	__u32	group_size;	/* # of blocks a group consists of */ 
	__u32	data_free;	/* # of free data blocks */
	__u32	inode_free;	/* # of free inodes */
	__u16	inode_blocks;	/* # of blocks used for inodes in every grp */
	__u16	total_groups;	/* # of groups */
};

#endif /* __EFS_FS_SB_H__ */

