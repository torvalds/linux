/*	$OpenBSD: syslimits.h,v 1.16 2024/08/02 01:53:21 guenther Exp $	*/
/*	$NetBSD: syslimits.h,v 1.12 1995/10/05 05:26:19 thorpej Exp $	*/

/*
 * Copyright (c) 1988, 1993
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
 *	@(#)syslimits.h	8.1 (Berkeley) 6/2/93
 */

#include <sys/cdefs.h>

#if __POSIX_VISIBLE || __XPG_VISIBLE || __BSD_VISIBLE
#define	ARG_MAX		 (512 * 1024)	/* max bytes for an exec function */
#define	CHILD_MAX		   80	/* max simultaneous processes */
#define	LINK_MAX		32767	/* max file link count */
#define	MAX_CANON		  255	/* max bytes in term canon input line */
#define	MAX_INPUT		  255	/* max bytes in terminal input */
#define	NAME_MAX		  255	/* max bytes in a file name */
#define	NGROUPS_MAX		   16	/* max supplemental group id's */
#define	OPEN_MAX		   64	/* max open files per process */
#define	PATH_MAX		 1024	/* max bytes in pathname */
#define	PIPE_BUF		  512	/* max bytes for atomic pipe writes */
#define	SYMLINK_MAX	     PATH_MAX	/* max bytes in a symbolic link */
#define	SYMLOOP_MAX		   32	/* max symlinks per path (for loops) */

#define	BC_BASE_MAX	      INT_MAX	/* max ibase/obase values in bc(1) */
#define	BC_DIM_MAX		65535	/* max array elements in bc(1) */
#define	BC_SCALE_MAX	      INT_MAX	/* max scale value in bc(1) */
#define	BC_STRING_MAX	      INT_MAX	/* max const string length in bc(1) */
#define	COLL_WEIGHTS_MAX	    2	/* max weights for order keyword */
#define	EXPR_NEST_MAX		   32	/* max expressions nested in expr(1) */
#define	LINE_MAX		 2048	/* max bytes in an input line */
#ifndef RE_DUP_MAX
#define	RE_DUP_MAX		  255	/* max RE's in interval notation */
#define	SEM_VALUE_MAX	     UINT_MAX	/* max value of a sem_* semaphore */
#endif

#if __XPG_VISIBLE
#define	IOV_MAX			 1024	/* max # of iov's (readv,sendmsg,etc) */
#define	NZERO			   20	/* default "nice" */
#endif /* __XPG_VISIBLE */

#endif /* __POSIX_VISIBLE || __XPG_VISIBLE || __BSD_VISIBLE */

#if __XPG_VISIBLE >= 500 || __POSIX_VISIBLE >= 199506 || __BSD_VISIBLE
#define TTY_NAME_MAX		260	/* max tty device name length w/ NUL */
#define LOGIN_NAME_MAX          32	/* max login name length w/ NUL */
#endif /* __XPG_VISIBLE >= 500 || __POSIX_VISIBLE >= 199506 || __BSD_VISIBLE */

#if __POSIX_VISIBLE >= 200112
#define HOST_NAME_MAX		255	/* max hostname length w/o NUL */
#endif

#if __POSIX_VISIBLE >= 202405
#define	GETENTROPY_MAX		  256	/* max bytes from getentropy(2) */
#endif

#define _MAXCOMLEN		24	/* includes NUL */
