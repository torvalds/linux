/*-
 * SPDX-License-Identifier: (BSD-2-Clause-FreeBSD AND BSD-3-Clause)
 *
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program
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
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 *	@(#)dinode.h	8.3 (Berkeley) 1/21/94
 * $FreeBSD$
 */

#ifndef _UFS_UFS_DINODE_H_
#define	_UFS_UFS_DINODE_H_

/*
 * The root inode is the root of the filesystem.  Inode 0 can't be used for
 * normal purposes and historically bad blocks were linked to inode 1, thus
 * the root inode is 2.  (Inode 1 is no longer used for this purpose, however
 * numerous dump tapes make this assumption, so we are stuck with it).
 */
#define	UFS_ROOTINO	((ino_t)2)

/*
 * The Whiteout inode# is a dummy non-zero inode number which will
 * never be allocated to a real file.  It is used as a place holder
 * in the directory entry which has been tagged as a DT_WHT entry.
 * See the comments about UFS_ROOTINO above.
 */
#define	UFS_WINO	((ino_t)1)

/*
 * The size of physical and logical block numbers and time fields in UFS.
 */
typedef	int32_t	ufs1_daddr_t;
typedef	int64_t	ufs2_daddr_t;
typedef int64_t ufs_lbn_t;
typedef int64_t ufs_time_t;

/* File permissions. */
#define	IEXEC		0000100		/* Executable. */
#define	IWRITE		0000200		/* Writeable. */
#define	IREAD		0000400		/* Readable. */
#define	ISVTX		0001000		/* Sticky bit. */
#define	ISGID		0002000		/* Set-gid. */
#define	ISUID		0004000		/* Set-uid. */

/* File types. */
#define	IFMT		0170000		/* Mask of file type. */
#define	IFIFO		0010000		/* Named pipe (fifo). */
#define	IFCHR		0020000		/* Character device. */
#define	IFDIR		0040000		/* Directory file. */
#define	IFBLK		0060000		/* Block device. */
#define	IFREG		0100000		/* Regular file. */
#define	IFLNK		0120000		/* Symbolic link. */
#define	IFSOCK		0140000		/* UNIX domain socket. */
#define	IFWHT		0160000		/* Whiteout. */

/*
 * A dinode contains all the meta-data associated with a UFS2 file.
 * This structure defines the on-disk format of a dinode. Since
 * this structure describes an on-disk structure, all its fields
 * are defined by types with precise widths.
 */

#define	UFS_NXADDR	2		/* External addresses in inode. */
#define	UFS_NDADDR	12		/* Direct addresses in inode. */
#define	UFS_NIADDR	3		/* Indirect addresses in inode. */

struct ufs2_dinode {
	u_int16_t	di_mode;	/*   0: IFMT, permissions; see below. */
	int16_t		di_nlink;	/*   2: File link count. */
	u_int32_t	di_uid;		/*   4: File owner. */
	u_int32_t	di_gid;		/*   8: File group. */
	u_int32_t	di_blksize;	/*  12: Inode blocksize. */
	u_int64_t	di_size;	/*  16: File byte count. */
	u_int64_t	di_blocks;	/*  24: Blocks actually held. */
	ufs_time_t	di_atime;	/*  32: Last access time. */
	ufs_time_t	di_mtime;	/*  40: Last modified time. */
	ufs_time_t	di_ctime;	/*  48: Last inode change time. */
	ufs_time_t	di_birthtime;	/*  56: Inode creation time. */
	int32_t		di_mtimensec;	/*  64: Last modified time. */
	int32_t		di_atimensec;	/*  68: Last access time. */
	int32_t		di_ctimensec;	/*  72: Last inode change time. */
	int32_t		di_birthnsec;	/*  76: Inode creation time. */
	u_int32_t	di_gen;		/*  80: Generation number. */
	u_int32_t	di_kernflags;	/*  84: Kernel flags. */
	u_int32_t	di_flags;	/*  88: Status flags (chflags). */
	u_int32_t	di_extsize;	/*  92: External attributes size. */
	ufs2_daddr_t	di_extb[UFS_NXADDR];/* 96: External attributes block. */
	ufs2_daddr_t	di_db[UFS_NDADDR]; /* 112: Direct disk blocks. */
	ufs2_daddr_t	di_ib[UFS_NIADDR]; /* 208: Indirect disk blocks. */
	u_int64_t	di_modrev;	/* 232: i_modrev for NFSv4 */
	uint32_t	di_freelink;	/* 240: SUJ: Next unlinked inode. */
	uint32_t	di_ckhash;	/* 244: if CK_INODE, its check-hash */
	uint32_t	di_spare[2];	/* 248: Reserved; currently unused */
};

/*
 * The di_db fields may be overlaid with other information for
 * file types that do not have associated disk storage. Block
 * and character devices overlay the first data block with their
 * dev_t value. Short symbolic links place their path in the
 * di_db area.
 */
#define	di_rdev di_db[0]

/*
 * A UFS1 dinode contains all the meta-data associated with a UFS1 file.
 * This structure defines the on-disk format of a UFS1 dinode. Since
 * this structure describes an on-disk structure, all its fields
 * are defined by types with precise widths.
 */
struct ufs1_dinode {
	u_int16_t	di_mode;	/*   0: IFMT, permissions; see below. */
	int16_t		di_nlink;	/*   2: File link count. */
	uint32_t	di_freelink;	/*   4: SUJ: Next unlinked inode. */
	u_int64_t	di_size;	/*   8: File byte count. */
	int32_t		di_atime;	/*  16: Last access time. */
	int32_t		di_atimensec;	/*  20: Last access time. */
	int32_t		di_mtime;	/*  24: Last modified time. */
	int32_t		di_mtimensec;	/*  28: Last modified time. */
	int32_t		di_ctime;	/*  32: Last inode change time. */
	int32_t		di_ctimensec;	/*  36: Last inode change time. */
	ufs1_daddr_t	di_db[UFS_NDADDR]; /*  40: Direct disk blocks. */
	ufs1_daddr_t	di_ib[UFS_NIADDR]; /*  88: Indirect disk blocks. */
	u_int32_t	di_flags;	/* 100: Status flags (chflags). */
	u_int32_t	di_blocks;	/* 104: Blocks actually held. */
	u_int32_t	di_gen;		/* 108: Generation number. */
	u_int32_t	di_uid;		/* 112: File owner. */
	u_int32_t	di_gid;		/* 116: File group. */
	u_int64_t	di_modrev;	/* 120: i_modrev for NFSv4 */
};

#define	UFS_LINK_MAX	32767

#endif /* _UFS_UFS_DINODE_H_ */
