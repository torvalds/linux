/*	$OpenBSD: param.h,v 1.145 2025/09/10 16:00:04 deraadt Exp $	*/

/*-
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
 */

#ifndef _SYS_PARAM_H_
#define	_SYS_PARAM_H_

#define	BSD	199306		/* System version (year & month). */
#define BSD4_3	1
#define BSD4_4	1

#define OpenBSD	202510		/* OpenBSD version (year & month). */
#define OpenBSD7_8 1		/* OpenBSD 7.8 */

#include <sys/_null.h>

#ifndef _LOCORE
#include <sys/types.h>
#endif

/*
 * Machine-independent constants (some used in following include files).
 * Redefined constants are from POSIX 1003.1 limits file.
 *
 * MAXCOMLEN should be >= sizeof(ac_comm) (see <acct.h>)
 * MAXLOGNAME should be >= UT_NAMESIZE (see <utmp.h>)
 */
#include <sys/syslimits.h>

#define	MAXCOMLEN	_MAXCOMLEN-1	/* max command name remembered, without NUL */
#define	MAXINTERP	128		/* max interpreter file name length */
#define	MAXLOGNAME	LOGIN_NAME_MAX	/* max login name length w/ NUL */
#define	MAXUPRC		CHILD_MAX	/* max simultaneous processes */
#define	NCARGS		ARG_MAX		/* max bytes for an exec function */
#define	NGROUPS		NGROUPS_MAX	/* max number groups */
#define	NOFILE		OPEN_MAX	/* max open files per process (soft) */
#define	NOFILE_MAX	1024		/* max open files per process (hard) */
#define	NOGROUP		65535		/* marker for empty group set member */
#define MAXHOSTNAMELEN	256		/* max hostname length w/ NUL */

/* More types and definitions used throughout the kernel. */
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/ucred.h>
#include <sys/uio.h>
#include <sys/srp.h>
#endif

/* Signals. */
#include <sys/signal.h>

/* Machine type dependent parameters. */
#include <sys/limits.h>
#include <machine/param.h>

#ifdef _KERNEL
/*
 * Priorities.  Note that with 32 run queues, differences less than 4 are
 * insignificant.
 */
#define	PSWP	0
#define	PVM	4
#define	PINOD	8
#define	PRIBIO	16
#define	PVFS	20
#endif /* _KERNEL */
#define	PZERO	22		/* No longer magic, shouldn't be here.  XXX */
#ifdef _KERNEL
#define	PSOCK	24
#define	PWAIT	32
#define	PLOCK	36
#define	PPAUSE	40
#define	PUSER	50
#define	MAXPRI	127		/* Priorities range from 0 through MAXPRI. */

#define	PRIMASK		0x0ff
#define	PCATCH		0x100	/* OR'd with pri for tsleep to check signals */
#define PNORELOCK	0x200	/* OR'd with pri for msleep to not reacquire
				   the mutex */
#endif /* _KERNEL */

#define	NODEV	(dev_t)(-1)	/* non-existent device */

#define	ALIGNBYTES		_ALIGNBYTES
#define	ALIGN(p)		_ALIGN(p)
#define	ALIGNED_POINTER(p,t)	_ALIGNED_POINTER(p,t)

/*
 * File system parameters and macros.
 *
 * The file system is made out of blocks of at most MAXBSIZE units, with
 * smaller units (fragments) only in the last direct block.  MAXBSIZE
 * primarily determines the size of buffers in the buffer pool.  It may be
 * made larger without any effect on existing file systems; however making
 * it smaller makes some file systems unmountable.
 */
#ifdef _KERNEL
#define MAXPHYS		(64 * 1024)	/* max raw I/O transfer size */
#endif /* _KERNEL */
#define	MAXBSIZE	(64 * 1024)

#define	_DEV_BSHIFT	9		/* log2(DEV_BSIZE) */
#define	DEV_BSIZE	(1 << _DEV_BSHIFT)
#ifdef _KERNEL
#define	DEV_BSHIFT	_DEV_BSHIFT
#define	BLKDEV_IOSIZE	PAGE_SIZE
#endif /* _KERNEL */

/* pages to disk blocks */
#ifndef ctod
#define ctod(x)         ((x) << (PAGE_SHIFT - _DEV_BSHIFT))
#endif
#ifndef dtoc
#define dtoc(x)         ((x) >> (PAGE_SHIFT - _DEV_BSHIFT))
#endif

/* bytes to disk blocks */
#ifndef btodb
#define btodb(x)        ((x) >> _DEV_BSHIFT)
#endif
#ifndef dbtob
#define dbtob(x)        ((x) << _DEV_BSHIFT)
#endif

/*
 * MAXPATHLEN defines the longest permissible path length after expanding
 * symbolic links. It is used to allocate a temporary buffer from the buffer
 * pool in which to do the name expansion, hence should be a power of two,
 * and must be less than or equal to MAXBSIZE.  MAXSYMLINKS defines the
 * maximum number of symbolic links that may be expanded in a path name.
 * It should be set high enough to allow all legitimate uses, but halt
 * infinite loops reasonably quickly.
 */
#define	MAXPATHLEN	PATH_MAX
#define MAXSYMLINKS	SYMLOOP_MAX

/* Macros to set/clear/test flags. */
#ifdef _KERNEL
#define SET(t, f)	((t) |= (f))
#define CLR(t, f)	((t) &= ~(f))
#define ISSET(t, f)	((t) & (f))
#endif /* _KERNEL */

/* Bit map related macros. */
#define	setbit(a,i)	((a)[(i)>>3] |= 1<<((i)&(NBBY-1)))
#define	clrbit(a,i)	((a)[(i)>>3] &= ~(1<<((i)&(NBBY-1))))
#define	isset(a,i)	((a)[(i)>>3] & (1<<((i)&(NBBY-1))))
#define	isclr(a,i)	(((a)[(i)>>3] & (1<<((i)&(NBBY-1)))) == 0)

/* Macros for counting and rounding. */
#ifndef howmany
#define	howmany(x, y)	(((x)+((y)-1))/(y))
#endif
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))
#define powerof2(x)	((((x)-1)&(x))==0)

/* Macros for min/max. */
#define	MIN(a,b) (((a)<(b))?(a):(b))
#define	MAX(a,b) (((a)>(b))?(a):(b))

/* Macros for calculating the offset of a field */
#if !defined(offsetof) && defined(_KERNEL)
#if __GNUC_PREREQ__(4, 0)
#define offsetof(s, e) __builtin_offsetof(s, e)
#else
#define offsetof(s, e) ((size_t)&((s *)0)->e)
#endif
#endif /* !defined(offsetof) && defined(_KERNEL) */

#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))

/*
 * Scale factor for scaled integers used to count %cpu time and load avgs.
 *
 * The number of CPU `tick's that map to a unique `%age' can be expressed
 * by the formula (1 / (2 ^ (FSHIFT - 11))).  The maximum load average that
 * can be calculated (assuming 32 bits) can be closely approximated using
 * the formula (2 ^ (2 * (16 - FSHIFT))) for (FSHIFT < 15).
 *
 * For the scheduler to maintain a 1:1 mapping of CPU `tick' to `%age',
 * FSHIFT must be at least 11; this gives us a maximum load avg of ~1024.
 */
#define	_FSHIFT	11		/* bits to right of fixed binary point */
#ifdef _KERNEL
#define	FSHIFT	_FSHIFT	
#endif
#define FSCALE	(1<<_FSHIFT)

#endif /* !_SYS_PARAM_H_ */
