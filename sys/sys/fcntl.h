/*	$OpenBSD: fcntl.h,v 1.23 2025/08/04 04:59:30 guenther Exp $	*/
/*	$NetBSD: fcntl.h,v 1.8 1995/03/26 20:24:12 jtc Exp $	*/

/*-
 * Copyright (c) 1983, 1990, 1993
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
 *	@(#)fcntl.h	8.3 (Berkeley) 1/21/94
 */

#ifndef _SYS_FCNTL_H_
#define	_SYS_FCNTL_H_

/*
 * This file includes the definitions for open and fcntl
 * described by POSIX for <fcntl.h>; it also includes
 * related kernel definitions.
 */

#include <sys/cdefs.h>
#ifndef _KERNEL
#include <sys/types.h>
#endif

/*
 * File status flags: these are used by open(2), fcntl(2).
 * They are also used (indirectly) in the kernel file structure f_flags,
 * which is a superset of the open/fcntl flags.  Open flags and f_flags
 * are inter-convertible using OFLAGS(fflags) and FFLAGS(oflags).
 * Open/fcntl flags begin with O_; kernel-internal flags begin with F.
 */
/* open-only flags */
#define	O_RDONLY	0x0000		/* open for reading only */
#define	O_WRONLY	0x0001		/* open for writing only */
#define	O_RDWR		0x0002		/* open for reading and writing */
#define	O_ACCMODE	0x0003		/* mask for above modes */

/*
 * Kernel encoding of open mode; separate read and write bits that are
 * independently testable: 1 greater than the above.
 *
 * XXX
 * FREAD and FWRITE are excluded from the #ifdef _KERNEL so that TIOCFLUSH,
 * which was documented to use FREAD/FWRITE, continues to work.
 */
#if __BSD_VISIBLE
#define	FREAD		0x0001
#define	FWRITE		0x0002
#endif
#define	O_NONBLOCK	0x0004		/* no delay */
#define	O_APPEND	0x0008		/* set append mode */
#if __BSD_VISIBLE
#define	O_SHLOCK	0x0010		/* open with shared file lock */
#define	O_EXLOCK	0x0020		/* open with exclusive file lock */
#define	O_ASYNC		0x0040		/* signal pgrp when data ready */
#define	O_FSYNC		0x0080		/* backwards compatibility */
#endif
#if __POSIX_VISIBLE >= 199309 || __XPG_VISIBLE >= 420
#define	O_SYNC		0x0080		/* synchronous writes */
/*
 * POSIX 1003.1 permits a higher granularity for synchronous operations
 * than we support.  Since synchronicity is all or nothing in OpenBSD
 * we just define these to be the same as O_SYNC.
 */
#define	O_DSYNC		O_SYNC		/* synchronous data writes */
#define	O_RSYNC		O_SYNC		/* synchronous reads */
#endif

/* defined by POSIX Issue 7 */
#define	O_NOFOLLOW	0x0100		/* if path is a symlink, don't follow */

#define	O_CREAT		0x0200		/* create if nonexistent */
#define	O_TRUNC		0x0400		/* truncate to zero length */
#define	O_EXCL		0x0800		/* error if already exists */

/* defined by POSIX 1003.1; BSD default, this bit is not required */
#define	O_NOCTTY	0x8000		/* don't assign controlling terminal */

/* defined by POSIX Issue 7 */
#define	O_CLOEXEC	0x10000		/* atomically set FD_CLOEXEC */
#define	O_DIRECTORY	0x20000		/* fail if not a directory */

/* defined by POSIX Issue 8 */
#define	O_CLOFORK	0x40000		/* atomically set FD_CLOFORK */

#ifdef _KERNEL
/*
 * convert from open() flags to/from fflags; convert O_RD/WR to FREAD/FWRITE.
 * For out-of-range values for the flags, be slightly careful (but lossy).
 */
#define	FFLAGS(oflags)	(((oflags) & ~O_ACCMODE) | (((oflags) + 1) & O_ACCMODE))
#define	OFLAGS(fflags)	(((fflags) & ~O_ACCMODE) | (((fflags) - 1) & O_ACCMODE))

/* bits to save after open */
#define	FMASK		(FREAD|FWRITE|FAPPEND|FASYNC|FFSYNC|FNONBLOCK)
/* bits settable by fcntl(F_SETFL, ...) */
#define	FCNTLFLAGS	(FAPPEND|FASYNC|FFSYNC|FNONBLOCK)
#endif

/*
 * The O_* flags used to have only F* names, which were used in the kernel
 * and by fcntl.  We retain the F* names for the kernel f_flags field
 * and for backward compatibility for fcntl.
 */
#if __BSD_VISIBLE
#define	FAPPEND		O_APPEND	/* kernel/compat */
#define	FASYNC		O_ASYNC		/* kernel/compat */
#define	FFSYNC		O_SYNC		/* kernel */
#define	FNONBLOCK	O_NONBLOCK	/* kernel */
#define	FNDELAY		O_NONBLOCK	/* compat */
#define	O_NDELAY	O_NONBLOCK	/* compat */
#endif

/*
 * Constants used for fcntl(2)
 */

/* command values */
#define	F_DUPFD		0		/* duplicate file descriptor */
#define	F_GETFD		1		/* get file descriptor flags */
#define	F_SETFD		2		/* set file descriptor flags */
#define	F_GETFL		3		/* get file status flags */
#define	F_SETFL		4		/* set file status flags */
#if __POSIX_VISIBLE >= 200112 || __XPG_VISIBLE >= 500
#define	F_GETOWN	5		/* get SIGIO/SIGURG proc/pgrp */
#define F_SETOWN	6		/* set SIGIO/SIGURG proc/pgrp */
#endif
#define	F_GETLK		7		/* get record locking information */
#define	F_SETLK		8		/* set record locking information */
#define	F_SETLKW	9		/* F_SETLK; wait if blocked */
#if __POSIX_VISIBLE >= 200809
#define	F_DUPFD_CLOEXEC	10		/* duplicate with FD_CLOEXEC set */
#endif
#if __BSD_VISIBLE
#define F_ISATTY	11		/* used by isatty(3) */
#endif
#if __POSIX_VISIBLE >= 202405
#define	F_DUPFD_CLOFORK	12		/* duplicate with FD_CLOFORK set */
#endif

/* file descriptor flags (F_GETFD, F_SETFD) */
#define	FD_CLOEXEC	1		/* close-on-exec flag */
#if __POSIX_VISIBLE >= 202405
#define	FD_CLOFORK	4		/* close-on-fork flag */
#endif

/* record locking flags (F_GETLK, F_SETLK, F_SETLKW) */
#define	F_RDLCK		1		/* shared or read lock */
#define	F_UNLCK		2		/* unlock */
#define	F_WRLCK		3		/* exclusive or write lock */
#ifdef _KERNEL
#define	F_WAIT		0x010		/* Wait until lock is granted */
#define	F_FLOCK		0x020	 	/* Use flock(2) semantics for lock */
#define	F_POSIX		0x040	 	/* Use POSIX semantics for lock */
#define	F_INTR		0x080	 	/* Lock operation interrupted */
#endif

/*
 * Advisory file segment locking data type -
 * information passed to system by user
 */
struct flock {
	off_t	l_start;	/* starting offset */
	off_t	l_len;		/* len = 0 means until end of file */
	pid_t	l_pid;		/* lock owner */
	short	l_type;		/* lock type: read/write, etc. */
	short	l_whence;	/* type of l_start */
};


#if __BSD_VISIBLE
/* lock operations for flock(2) */
#define	LOCK_SH		0x01		/* shared file lock */
#define	LOCK_EX		0x02		/* exclusive file lock */
#define	LOCK_NB		0x04		/* don't block when locking */
#define	LOCK_UN		0x08		/* unlock file */
#endif

#if __POSIX_VISIBLE >= 200809
#define	AT_FDCWD	-100

#define	AT_EACCESS		0x01
#define	AT_SYMLINK_NOFOLLOW	0x02
#define	AT_SYMLINK_FOLLOW	0x04
#define	AT_REMOVEDIR		0x08
#endif

#ifndef _KERNEL
__BEGIN_DECLS
int	open(const char *, int, ...);
int	creat(const char *, mode_t);
int	fcntl(int, int, ...);
#if __BSD_VISIBLE
int	flock(int, int);
#endif
#if __POSIX_VISIBLE >= 200809
int	openat(int, const char *, int, ...);
#endif
__END_DECLS
#endif

#endif /* !_SYS_FCNTL_H_ */
