/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 * $FreeBSD$
 */

#ifndef	_SYS_DIRENT_H_
#define	_SYS_DIRENT_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#ifndef _INO_T_DECLARED
typedef	__ino_t		ino_t;
#define	_INO_T_DECLARED
#endif

#ifndef _OFF_T_DECLARED
typedef	__off_t		off_t;
#define	_OFF_T_DECLARED
#endif

/*
 * The dirent structure defines the format of directory entries returned by
 * the getdirentries(2) system call.
 *
 * A directory entry has a struct dirent at the front of it, containing its
 * inode number, the length of the entry, and the length of the name
 * contained in the entry.  These are followed by the name padded to an 8
 * byte boundary with null bytes.  All names are guaranteed null terminated.
 * The maximum length of a name in a directory is MAXNAMLEN.
 *
 * Explicit padding between the last member of the header (d_namlen) and
 * d_name avoids ABI padding at the end of dirent on LP64 architectures.
 * There is code depending on d_name being last.
 */

struct dirent {
	ino_t      d_fileno;		/* file number of entry */
	off_t      d_off;		/* directory offset of entry */
	__uint16_t d_reclen;		/* length of this record */
	__uint8_t  d_type;		/* file type, see below */
	__uint8_t  d_pad0;
	__uint16_t d_namlen;		/* length of string in d_name */
	__uint16_t d_pad1;
#if __BSD_VISIBLE
#define	MAXNAMLEN	255
	char	d_name[MAXNAMLEN + 1];	/* name must be no longer than this */
#else
	char	d_name[255 + 1];	/* name must be no longer than this */
#endif
};

#if defined(_WANT_FREEBSD11_DIRENT) || defined(_KERNEL)
struct freebsd11_dirent {
	__uint32_t d_fileno;		/* file number of entry */
	__uint16_t d_reclen;		/* length of this record */
	__uint8_t  d_type;		/* file type, see below */
	__uint8_t  d_namlen;		/* length of string in d_name */
	char	d_name[255 + 1];	/* name must be no longer than this */
};
#endif /* _WANT_FREEBSD11_DIRENT || _KERNEL */

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
#define	DT_WHT		14

/*
 * Convert between stat structure types and directory types.
 */
#define	IFTODT(mode)	(((mode) & 0170000) >> 12)
#define	DTTOIF(dirtype)	((dirtype) << 12)

/*
 * The _GENERIC_DIRSIZ macro gives the minimum record length which will hold
 * the directory entry.  This returns the amount of space in struct dirent
 * without the d_name field, plus enough space for the name with a terminating
 * null byte (dp->d_namlen+1), rounded up to a 8 byte boundary.
 *
 * XXX although this macro is in the implementation namespace, it requires
 * a manifest constant that is not.
 */
#define	_GENERIC_DIRLEN(namlen)					\
	((__offsetof(struct dirent, d_name) + (namlen) + 1 + 7) & ~7)
#define	_GENERIC_DIRSIZ(dp)	_GENERIC_DIRLEN((dp)->d_namlen)
#endif /* __BSD_VISIBLE */

#ifdef _KERNEL
#define	GENERIC_DIRSIZ(dp)	_GENERIC_DIRSIZ(dp)

/*
 * Ensure that padding bytes are zeroed and that the name is NUL-terminated.
 */
static inline void
dirent_terminate(struct dirent *dp)
{

	dp->d_pad0 = 0;
	dp->d_pad1 = 0;
	memset(dp->d_name + dp->d_namlen, 0,
	    dp->d_reclen - (__offsetof(struct dirent, d_name) + dp->d_namlen));
}
#endif

#endif /* !_SYS_DIRENT_H_ */
