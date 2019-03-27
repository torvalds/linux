/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)limits.h	8.2 (Berkeley) 1/4/94
 * $FreeBSD$
 */

#ifndef _LIMITS_H_
#define	_LIMITS_H_

#include <sys/cdefs.h>

#if __POSIX_VISIBLE
#define	_POSIX_ARG_MAX		4096
#define	_POSIX_LINK_MAX		8
#define	_POSIX_MAX_CANON	255
#define	_POSIX_MAX_INPUT	255
#define	_POSIX_NAME_MAX		14
#define	_POSIX_PIPE_BUF		512
#define	_POSIX_SSIZE_MAX	32767
#define	_POSIX_STREAM_MAX	8

#if __POSIX_VISIBLE >= 200112
#define	_POSIX_CHILD_MAX	25
#define	_POSIX_NGROUPS_MAX	8
#define	_POSIX_OPEN_MAX		20
#define	_POSIX_PATH_MAX		256
#define	_POSIX_TZNAME_MAX	6
#else
#define	_POSIX_CHILD_MAX	6
#define	_POSIX_NGROUPS_MAX	0
#define	_POSIX_OPEN_MAX		16
#define	_POSIX_PATH_MAX		255
#define	_POSIX_TZNAME_MAX	3
#endif

#if __POSIX_VISIBLE >= 200112
#define	BC_BASE_MAX		   99	/* max ibase/obase values in bc(1) */
#define	BC_DIM_MAX		 2048	/* max array elements in bc(1) */
#define	BC_SCALE_MAX		   99	/* max scale value in bc(1) */
#define	BC_STRING_MAX		 1000	/* max const string length in bc(1) */
#define	CHARCLASS_NAME_MAX	   14	/* max character class name size */
#define	COLL_WEIGHTS_MAX	   10	/* max weights for order keyword */
#define	EXPR_NEST_MAX		   32	/* max expressions nested in expr(1) */
#define	LINE_MAX		 2048	/* max bytes in an input line */
#define	RE_DUP_MAX		  255	/* max RE's in interval notation */

#define	_POSIX2_BC_BASE_MAX	99
#define	_POSIX2_BC_DIM_MAX	2048
#define	_POSIX2_BC_SCALE_MAX	99
#define	_POSIX2_BC_STRING_MAX	1000
#define	_POSIX2_CHARCLASS_NAME_MAX 14
#define	_POSIX2_COLL_WEIGHTS_MAX 2
#define	_POSIX2_EQUIV_CLASS_MAX	2
#define	_POSIX2_EXPR_NEST_MAX	32
#define	_POSIX2_LINE_MAX	2048
#define	_POSIX2_RE_DUP_MAX	255
#endif
#endif

#if __POSIX_VISIBLE >= 199309
#define	_POSIX_AIO_LISTIO_MAX	2
#define	_POSIX_AIO_MAX		1
#define	_POSIX_DELAYTIMER_MAX	32
#define	_POSIX_MQ_OPEN_MAX	8
#define	_POSIX_MQ_PRIO_MAX	32
#define	_POSIX_RTSIG_MAX	8
#define	_POSIX_SEM_NSEMS_MAX	256
#define	_POSIX_SEM_VALUE_MAX	32767
#define	_POSIX_SIGQUEUE_MAX	32
#define	_POSIX_TIMER_MAX	32

#define	_POSIX_CLOCKRES_MIN	20000000
#endif

#if __POSIX_VISIBLE >= 199506
#define	_POSIX_THREAD_DESTRUCTOR_ITERATIONS 4
#define	_POSIX_THREAD_KEYS_MAX	128
#define	_POSIX_THREAD_THREADS_MAX 64
#endif

#if __POSIX_VISIBLE >= 200112
#define	_POSIX_HOST_NAME_MAX	255
#define	_POSIX_LOGIN_NAME_MAX	9
#define	_POSIX_SS_REPL_MAX	4
#define	_POSIX_SYMLINK_MAX	255
#define	_POSIX_SYMLOOP_MAX	8
#define	_POSIX_TRACE_EVENT_NAME_MAX 30
#define	_POSIX_TRACE_NAME_MAX	8
#define	_POSIX_TRACE_SYS_MAX	8
#define	_POSIX_TRACE_USER_EVENT_MAX 32
#define	_POSIX_TTY_NAME_MAX	9

#define	_POSIX_RE_DUP_MAX	_POSIX2_RE_DUP_MAX
#endif

#if __XSI_VISIBLE || __POSIX_VISIBLE >= 200809
#define	NL_ARGMAX		4096	/* max # of position args for printf */
#define	NL_MSGMAX		32767
#define	NL_SETMAX		255
#define	NL_TEXTMAX		2048
#endif

#if __XSI_VISIBLE
#define	_XOPEN_IOV_MAX		16
#define	_XOPEN_NAME_MAX		255
#define	_XOPEN_PATH_MAX		1024
#define	PASS_MAX		128	/* _PASSWORD_LEN from <pwd.h> */

#define	NL_LANGMAX		31	/* max LANG name length */
#define	NL_NMAX			1
#endif

#define	MB_LEN_MAX		6	/* 31-bit UTF-8 */

#include <sys/limits.h>

#if __POSIX_VISIBLE
#include <sys/syslimits.h>
#endif

#endif /* !_LIMITS_H_ */
