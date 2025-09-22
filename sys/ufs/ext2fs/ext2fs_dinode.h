/*	$OpenBSD: ext2fs_dinode.h,v 1.17 2014/07/31 17:37:52 pelikan Exp $	*/
/*	$NetBSD: ext2fs_dinode.h,v 1.6 2000/01/26 16:21:33 bouyer Exp $	*/

/*
 * Copyright (c) 1997 Manuel Bouyer.
 * Copyright (c) 1982, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)dinode.h	8.6 (Berkeley) 9/13/94
 *  Modified for ext2fs by Manuel Bouyer.
 */

#include <sys/stat.h>
#include <ufs/ufs/dinode.h>	/* for ufsino_t */

/*
 * The root inode is the root of the file system.  Inode 0 can't be used for
 * normal purposes and bad blocks are normally linked to inode 1, thus
 * the root inode is 2.
 * Inode 3 to 10 are reserved in ext2fs.
 */
#define	EXT2_ROOTINO ((ufsino_t)2)
#define EXT2_RESIZEINO ((ufsino_t)7)
#define EXT2_FIRSTINO ((ufsino_t)11)

/*
 * A dinode contains all the meta-data associated with a UFS file.
 * This structure defines the on-disk format of a dinode. Since
 * this structure describes an on-disk structure, all its fields
 * are defined by types with precise widths.
 */

#define	NDADDR	12			/* Direct addresses in inode. */
#define	NIADDR	3			/* Indirect addresses in inode. */

#define EXT2_MAXSYMLINKLEN ((NDADDR+NIADDR) * sizeof (u_int32_t))

struct ext2fs_dinode {
	u_int16_t	e2di_mode;	/*   0: IFMT, permissions; see below. */
	u_int16_t	e2di_uid_low;	/*   2: owner UID, bits 15:0 */
	u_int32_t	e2di_size;	/*   4: file size (bytes) bits 31:0 */
	u_int32_t	e2di_atime;	/*   8: Access time */
	u_int32_t	e2di_ctime;	/*  12: Change time */
	u_int32_t	e2di_mtime;	/*  16: Modification time */
	u_int32_t	e2di_dtime;	/*  20: Deletion time */
	u_int16_t	e2di_gid_low;	/*  24: Owner GID, lowest bits */
	u_int16_t	e2di_nlink;	/*  26: File link count */
	u_int32_t	e2di_nblock;	/*  28: blocks count */
	u_int32_t	e2di_flags;	/*  32: status flags (chflags) */
	u_int32_t	e2di_version_lo; /* 36: inode version, bits 31:0 */
	u_int32_t	e2di_blocks[NDADDR+NIADDR]; /* 40: disk blocks */
	u_int32_t	e2di_gen;	/* 100: generation number */
	u_int32_t	e2di_facl;	/* 104: file ACL, bits 31:0 */
	u_int32_t	e2di_size_hi;	/* 108: file size (bytes), bits 63:32 */
	u_int32_t	e2di_faddr;	/* 112: fragment address (obsolete) */
	u_int16_t	e2di_nblock_hi;	/* 116: blocks count, bits 47:32 */
	u_int16_t	e2di_facl_hi;	/* 118: file ACL, bits 47:32 */
	u_int16_t	e2di_uid_high;	/* 120: owner UID, bits 31:16 */
	u_int16_t	e2di_gid_high;	/* 122: owner GID, bits 31:16 */
	u_int16_t	e2di_chksum_lo;	/* 124: inode checksum, bits 15:0 */
	u_int16_t	e2di__reserved;	/* 126: 	unused */
	u_int16_t	e2di_isize;	/* 128: size of this inode */
	u_int16_t	e2di_chksum_hi;	/* 130: inode checksum, bits 31:16 */
	u_int32_t	e2di_x_ctime;	/* 132: extra Change time */
	u_int32_t	e2di_x_mtime;	/* 136: extra Modification time */
	u_int32_t	e2di_x_atime;	/* 140: extra Access time */
	u_int32_t	e2di_crtime;	/* 144: Creation (birth) time */
	u_int32_t	e2di_x_crtime;	/* 148: extra Creation (birth) time */
	u_int32_t	e2di_version_hi; /* 152: inode version, bits 63:31 */
};

#define	E2MAXSYMLINKLEN	((NDADDR + NIADDR) * sizeof(u_int32_t))

/* File permissions. */
#define	EXT2_IEXEC		0000100		/* Executable. */
#define	EXT2_IWRITE		0000200		/* Writeable. */
#define	EXT2_IREAD		0000400		/* Readable. */
#define	EXT2_ISVTX		0001000		/* Sticky bit. */
#define	EXT2_ISGID		0002000		/* Set-gid. */
#define	EXT2_ISUID		0004000		/* Set-uid. */

/* File types. */
#define	EXT2_IFMT		0170000		/* Mask of file type. */
#define	EXT2_IFIFO		0010000		/* Named pipe (fifo). */
#define	EXT2_IFCHR		0020000		/* Character device. */
#define	EXT2_IFDIR		0040000		/* Directory file. */
#define	EXT2_IFBLK		0060000		/* Block device. */
#define	EXT2_IFREG		0100000		/* Regular file. */
#define	EXT2_IFLNK		0120000		/* Symbolic link. */
#define	EXT2_IFSOCK		0140000		/* UNIX domain socket. */

/* file flags */
#define EXT2_SECRM		0x00000001	/* Secure deletion */
#define EXT2_UNRM		0x00000002	/* Undelete */
#define EXT2_COMPR		0x00000004	/* Compress file */
#define EXT2_SYNC		0x00000008	/* Synchronous updates */
#define EXT2_IMMUTABLE		0x00000010	/* Immutable file */
#define EXT2_APPEND		0x00000020	/* writes to file may only append */
#define EXT2_NODUMP		0x00000040	/* do not dump file */
#define EXT2_NOATIME		0x00000080	/* do not update access time */
#define EXT4_INDEX		0x00001000	/* hash-indexed directory */
#define EXT4_JOURNAL_DATA	0x00004000	/* file data should be journaled */
#define EXT4_DIRSYNC		0x00010000	/* all dirent updates done synchronously */
#define EXT4_TOPDIR		0x00020000	/* top of directory hierarchies */
#define EXT4_HUGE_FILE		0x00040000	/* nblocks unit is fsb, not db */
#define EXT4_EXTENTS		0x00080000	/* inode uses extents */
#define EXT4_EOFBLOCKS		0x00400000	/* blocks allocated beyond EOF */

/* Size of on-disk inode. */
#define EXT2_REV0_DINODE_SIZE	128
#define EXT2_DINODE_SIZE(fs)	((fs)->e2fs.e2fs_rev > E2FS_REV0 ?  \
				    (fs)->e2fs.e2fs_inode_size : \
				    EXT2_REV0_DINODE_SIZE)

/*
 * The e2di_blocks fields may be overlaid with other information for
 * file types that do not have associated disk storage. Block
 * and character devices overlay the first data block with their
 * dev_t value. Short symbolic links place their path in the
 * di_db area.
 */

#define e2di_rdev		e2di_blocks[0]
#define e2di_shortlink	e2di_blocks

/* e2fs needs byte swapping on big-endian systems */
#if BYTE_ORDER == LITTLE_ENDIAN
#	define e2fs_iload(fs, old, new)	\
		memcpy((new),(old), MIN(EXT2_DINODE_SIZE(fs), sizeof(*new)))
#	define e2fs_isave(fs, old, new) \
		memcpy((new),(old), MIN(EXT2_DINODE_SIZE(fs), sizeof(*new)))
#else
struct m_ext2fs;
void e2fs_i_bswap(struct m_ext2fs *, struct ext2fs_dinode *, struct ext2fs_dinode *);
#	define e2fs_iload(fs, old, new) e2fs_i_bswap((fs), (old), (new))
#	define e2fs_isave(fs, old, new) e2fs_i_bswap((fs), (old), (new))
#endif
