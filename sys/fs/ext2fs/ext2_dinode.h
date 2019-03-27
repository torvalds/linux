/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Aditya Sarawgi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _FS_EXT2FS_EXT2_DINODE_H_
#define	_FS_EXT2FS_EXT2_DINODE_H_

/*
 * Special inode numbers
 * The root inode is the root of the file system.  Inode 0 can't be used for
 * normal purposes and bad blocks are normally linked to inode 1, thus
 * the root inode is 2.
 * Inode 3 to 10 are reserved in ext2fs.
 */
#define	EXT2_BADBLKINO		((ino_t)1)
#define	EXT2_ROOTINO		((ino_t)2)
#define	EXT2_ACLIDXINO		((ino_t)3)
#define	EXT2_ACLDATAINO		((ino_t)4)
#define	EXT2_BOOTLOADERINO	((ino_t)5)
#define	EXT2_UNDELDIRINO	((ino_t)6)
#define	EXT2_RESIZEINO		((ino_t)7)
#define	EXT2_JOURNALINO		((ino_t)8)
#define	EXT2_EXCLUDEINO		((ino_t)9)
#define	EXT2_REPLICAINO		((ino_t)10)
#define	EXT2_FIRSTINO		((ino_t)11)

/*
 * Inode flags
 * The system supports EXT2_IMMUTABLE, EXT2_APPEND and EXT2_NODUMP flags.
 * The current implementation also uses EXT3_INDEX, EXT4_EXTENTS and
 * EXT4_HUGE_FILE with some restrictions imposed by the lack of write
 * support.
 */
#define	EXT2_SECRM		0x00000001	/* Secure deletion */
#define	EXT2_UNRM		0x00000002	/* Undelete */
#define	EXT2_COMPR		0x00000004	/* Compress file */
#define	EXT2_SYNC		0x00000008	/* Synchronous updates */
#define	EXT2_IMMUTABLE		0x00000010	/* Immutable file */
#define	EXT2_APPEND		0x00000020 /* Writes to file may only append */
#define	EXT2_NODUMP		0x00000040	/* Do not dump file */
#define	EXT2_NOATIME		0x00000080	/* Do not update atime */
#define	EXT3_INDEX		0x00001000	/* Hash-indexed directory */
#define	EXT4_IMAGIC		0x00002000	/* AFS directory */
#define	EXT4_JOURNAL_DATA	0x00004000 /* File data should be journaled */
#define	EXT4_NOTAIL		0x00008000 /* File tail should not be merged */
#define	EXT4_DIRSYNC		0x00010000	/* Dirsync behaviour */
#define	EXT4_TOPDIR		0x00020000 /* Top of directory hierarchies*/
#define	EXT4_HUGE_FILE		0x00040000	/* Set to each huge file */
#define	EXT4_EXTENTS		0x00080000	/* Inode uses extents */
#define	EXT4_EA_INODE		0x00200000	/* Inode used for large EA */
#define	EXT4_EOFBLOCKS		0x00400000 /* Blocks allocated beyond EOF */
#define	EXT4_INLINE_DATA	0x10000000 /* Inode has inline data */
#define	EXT4_PROJINHERIT	0x20000000 /* Children inherit project ID */

/*
 * Definitions for nanosecond timestamps.
 * Ext3 inode versioning, 2006-12-13.
 */
#define	EXT3_EPOCH_BITS	2
#define	EXT3_EPOCH_MASK	((1 << EXT3_EPOCH_BITS) - 1)
#define	EXT3_NSEC_MASK	(~0UL << EXT3_EPOCH_BITS)

#define	E2DI_HAS_XTIME(ip)	(EXT2_HAS_RO_COMPAT_FEATURE(ip->i_e2fs,	\
				    EXT2F_ROCOMPAT_EXTRA_ISIZE))
#define	E2DI_HAS_HUGE_FILE(ip)	(EXT2_HAS_RO_COMPAT_FEATURE(ip->i_e2fs,	\
				    EXT2F_ROCOMPAT_HUGE_FILE))

/*
 * Constants relative to the data blocks
 */
#define	EXT2_NDIR_BLOCKS		12
#define	EXT2_IND_BLOCK			EXT2_NDIR_BLOCKS
#define	EXT2_DIND_BLOCK			(EXT2_IND_BLOCK + 1)
#define	EXT2_TIND_BLOCK			(EXT2_DIND_BLOCK + 1)
#define	EXT2_N_BLOCKS			(EXT2_TIND_BLOCK + 1)
#define	EXT2_MAXSYMLINKLEN		(EXT2_N_BLOCKS * sizeof(uint32_t))

/*
 * Structure of an inode on the disk
 */
struct ext2fs_dinode {
	uint16_t	e2di_mode;	/*   0: IFMT, permissions; see below. */
	uint16_t	e2di_uid;	/*   2: Owner UID */
	uint32_t	e2di_size;	/*   4: Size (in bytes) */
	uint32_t	e2di_atime;	/*   8: Access time */
	uint32_t	e2di_ctime;	/*  12: Change time */
	uint32_t	e2di_mtime;	/*  16: Modification time */
	uint32_t	e2di_dtime;	/*  20: Deletion time */
	uint16_t	e2di_gid;	/*  24: Owner GID */
	uint16_t	e2di_nlink;	/*  26: File link count */
	uint32_t	e2di_nblock;	/*  28: Blocks count */
	uint32_t	e2di_flags;	/*  32: Status flags (chflags) */
	uint32_t	e2di_version;	/*  36: Low 32 bits inode version */
	uint32_t	e2di_blocks[EXT2_N_BLOCKS]; /* 40: disk blocks */
	uint32_t	e2di_gen;	/* 100: generation number */
	uint32_t	e2di_facl;	/* 104: Low EA block */
	uint32_t	e2di_size_high;	/* 108: Upper bits of file size */
	uint32_t	e2di_faddr;	/* 112: Fragment address (obsolete) */
	uint16_t	e2di_nblock_high; /* 116: Blocks count bits 47:32 */
	uint16_t	e2di_facl_high;	/* 118: File EA bits 47:32 */
	uint16_t	e2di_uid_high;	/* 120: Owner UID top 16 bits */
	uint16_t	e2di_gid_high;	/* 122: Owner GID top 16 bits */
	uint16_t	e2di_chksum_lo;	  /* 124: Lower inode checksum */
	uint16_t	e2di_lx_reserved; /* 126: Unused */
	uint16_t	e2di_extra_isize; /* 128: Size of this inode */
	uint16_t	e2di_chksum_hi;	/* 130: High inode checksum */
	uint32_t	e2di_ctime_extra; /* 132: Extra change time */
	uint32_t	e2di_mtime_extra; /* 136: Extra modification time */
	uint32_t	e2di_atime_extra; /* 140: Extra access time */
	uint32_t	e2di_crtime;	/* 144: Creation (birth)time */
	uint32_t	e2di_crtime_extra; /* 148: Extra creation (birth)time */
	uint32_t	e2di_version_hi;  /* 152: High bits of inode version */
	uint32_t	e2di_projid;	/* 156: Project ID */
};

#endif /* !_FS_EXT2FS_EXT2_DINODE_H_ */

