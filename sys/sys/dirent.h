/*	$OpenBSD: dirent.h,v 1.11 2013/12/13 18:09:27 zhuk Exp $	*/
/*	$NetBSD: dirent.h,v 1.12 1996/04/09 20:55:25 cgd Exp $	*/

/*-
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)dirent.h	8.3 (Berkeley) 8/10/94
 */

#ifndef _SYS_DIRENT_H_
#define _SYS_DIRENT_H_

#include <sys/cdefs.h>

/*
 * The dirent structure defines the format of directory entries returned by
 * the getdents(2) system call.
 *
 * A directory entry has a struct dirent at the front of it, containing its
 * inode number, the length of the entry, and the length of the name
 * contained in the entry.  These are followed by the name padded to a 4
 * byte boundary with null bytes.  All names are guaranteed null terminated.
 * The maximum length of a name in a directory is MAXNAMLEN.
 */

struct dirent {
	__ino_t    d_fileno;		/* file number of entry */
	__off_t    d_off;		/* offset after this entry */
	__uint16_t d_reclen;		/* length of this record */
	__uint8_t  d_type; 		/* file type, see below */
	__uint8_t  d_namlen;		/* length of string in d_name */
	__uint8_t  __d_padding[4];	/* suppress padding after d_name */
#if __BSD_VISIBLE
#define	MAXNAMLEN	255
	char	d_name[MAXNAMLEN + 1];	/* name must be no longer than this */
#else
	char	d_name[255 + 1];	/* name must be no longer than this */
#endif
};

#if __BSD_VISIBLE
/*
 * File types
 */
#define	DT_UNKNOWN	 0
#define	DT_FIFO		 1
#define	DT_CHR		 2
#define	DT_DIR		 4
#define	DT_BLK		 6
#define	DT_REG		 8
#define	DT_LNK		10
#define	DT_SOCK		12

/*
 * Convert between stat structure types and directory types.
 */
#define	IFTODT(mode)	(((mode) & 0170000) >> 12)
#define	DTTOIF(dirtype)	((dirtype) << 12)

#ifdef _KERNEL
/*
 * The DIRENT_RECSIZE macro gives the minimum record length which will hold
 * a directory entry with a name of the given length, including the terminating
 * nul byte, rounded up to proper alignment.
 * The DIRENT_SIZE macro does the same when given a pointer to a struct dirent
 */
#define DIRENT_RECSIZE(namelen) \
    ((offsetof(struct dirent, d_name) + (namelen) + 1 + 7) &~ 7)
#define	DIRENT_SIZE(dp) \
    DIRENT_RECSIZE((dp)->d_namlen)
#endif

#endif /* __BSD_VISIBLE */

#endif /* _SYS_DIRENT_H_ */
