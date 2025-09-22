/*	$OpenBSD: ext2fs_dir.h,v 1.12 2024/01/09 03:16:00 guenther Exp $	*/
/*	$NetBSD: ext2fs_dir.h,v 1.4 2000/01/28 16:00:23 bouyer Exp $	*/

/*
 * Copyright (c) 1997 Manuel Bouyer.
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)dir.h	8.4 (Berkeley) 8/10/94
 * Modified for ext2fs by Manuel Bouyer.
 */

#ifndef _EXT2FS_DIR_H_
#define	_EXT2FS_DIR_H_

/*
 * Theoretically, directories can be more than 2Gb in length, however, in
 * practice this seems unlikely. So, we define the type doff_t as a 32-bit
 * quantity to keep down the cost of doing lookup on a 32-bit machine.
 */
#define	doff_t		int32_t
#define	EXT2FS_MAXDIRSIZE	(0x7fffffff)

/*
 * A directory consists of some number of blocks of e2fs_bsize bytes.
 *
 * Each block contains some number of directory entry
 * structures, which are of variable length.  Each directory entry has
 * a struct direct at the front of it, containing its inode number,
 * the length of the entry, and the length of the name contained in
 * the entry.  These are followed by the name padded to a 4 byte boundary
 * with null bytes.  All names are guaranteed null terminated.
 * The maximum length of a name in a directory is EXT2FS_MAXNAMLEN.
 *
 * The macro EXT2FS_DIRSIZ(dp) gives the amount of space required to
 * represent a directory entry.  Free space in a directory is represented by
 * entries which have dp->e2d_reclen > DIRSIZ(dp).  All d2fs_bsize bytes
 * in a directory block are claimed by the directory entries.  This
 * usually results in the last entry in a directory having a large
 * dp->e2d_reclen.  When entries are deleted from a directory, the
 * space is returned to the previous entry in the same directory
 * block by increasing its dp->e2d_reclen.  If the first entry of
 * a directory block is free, then its dp->e2d_ino is set to 0.
 * Entries other than the first in a directory do not normally have
 * dp->e2d_ino set to 0.
 * Ext2 rev 0 has a 16 bits e2d_namlen. For Ext2 rev 1 this has been split
 * into a 8 bits e2d_namlen and 8 bits e2d_type (looks like ffs, isnt't it ? :)
 * It's safe to use this for rev 0 as well because all ext2 are little-endian.
 */

#define	EXT2FS_MAXNAMLEN	255

struct	ext2fs_direct {
	u_int32_t e2d_ino;		/* inode number of entry */
	u_int16_t e2d_reclen;		/* length of this record */
	u_int8_t e2d_namlen;		/* length of string in d_name */
	u_int8_t e2d_type;		/* file type */
	char	  e2d_name[EXT2FS_MAXNAMLEN];/* name with length <= EXT2FS_MAXNAMLEN */
};

enum slotstatus {
	NONE,
	COMPACT,
	FOUND
};

struct ext2fs_searchslot {
	enum slotstatus	slotstatus;
	doff_t		slotoffset;	/* offset of area with free space */
	int		slotsize;	/* size of area at slotoffset */
	int		slotfreespace;	/* amount of space free in slot */
	int		slotneeded;	/* sizeof the entry we are seeking */
};

/* Ext2 directory file types (not the same as FFS. Sigh. */
#define EXT2_FT_UNKNOWN         0
#define EXT2_FT_REG_FILE        1
#define EXT2_FT_DIR             2
#define EXT2_FT_CHRDEV          3
#define EXT2_FT_BLKDEV          4
#define EXT2_FT_FIFO            5
#define EXT2_FT_SOCK            6
#define EXT2_FT_SYMLINK         7

#define EXT2_FT_MAX             8

#define E2IFTODT(mode)    (((mode) & 0170000) >> 12)

static __inline__ u_int8_t inot2ext2dt(u_int16_t)
    __attribute__((__unused__));
static __inline__ u_int8_t
inot2ext2dt(u_int16_t type)
{
	switch(type) {
	case E2IFTODT(EXT2_IFIFO):
		return EXT2_FT_FIFO;
	case E2IFTODT(EXT2_IFCHR):
		return EXT2_FT_CHRDEV;
	case E2IFTODT(EXT2_IFDIR):
		return EXT2_FT_DIR;
	case E2IFTODT(EXT2_IFBLK):
		return EXT2_FT_BLKDEV;
	case E2IFTODT(EXT2_IFREG):
		return EXT2_FT_REG_FILE;
	case E2IFTODT(EXT2_IFLNK):
		return EXT2_FT_SYMLINK;
	case E2IFTODT(EXT2_IFSOCK):
		return EXT2_FT_SOCK;
	default:
		return 0;
	}
}

/*
 * The EXT2FS_DIRSIZ macro gives the minimum record length which will hold
 * the directory entryfor a name len "len" (without the terminating null byte).
 * This requires the amount of space in struct direct
 * without the d_name field, plus enough space for the name without a
 * terminating null byte, rounded up to a 4 byte boundary.
 */
#define EXT2FS_DIRSIZ(len) \
   (( 8 + len + 3) &~ 3)

/*
 * Template for manipulating directories.  Should use struct direct's,
 * but the name field is EXT2FS_MAXNAMLEN - 1, and this just won't do.
 */
struct ext2fs_dirtemplate {
	u_int32_t	dot_ino;
	int16_t		dot_reclen;
	u_int8_t	dot_namlen;
	u_int8_t	dot_type;
	char		dot_name[4];	/* must be multiple of 4 */
	u_int32_t	dotdot_ino;
	int16_t		dotdot_reclen;
	u_int8_t	dotdot_namlen;
	u_int8_t	dotdot_type;
	char		dotdot_name[4];	/* ditto */
};

#endif /* !_EXT2FS_DIR_H_ */
