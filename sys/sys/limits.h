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
 * $FreeBSD$
 */

#ifndef _SYS_LIMITS_H_
#define	_SYS_LIMITS_H_

#include <sys/cdefs.h>
#include <machine/_limits.h>

#define	CHAR_BIT	__CHAR_BIT	/* number of bits in a char */

#define	SCHAR_MAX	__SCHAR_MAX	/* max value for a signed char */
#define	SCHAR_MIN	__SCHAR_MIN	/* min value for a signed char */

#define	UCHAR_MAX	__UCHAR_MAX	/* max value for an unsigned char */

#ifdef __CHAR_UNSIGNED__
#define	CHAR_MAX	UCHAR_MAX	/* max value for a char */
#define	CHAR_MIN	0		/* min value for a char */
#else
#define	CHAR_MAX	SCHAR_MAX
#define	CHAR_MIN	SCHAR_MIN
#endif

#define	USHRT_MAX	__USHRT_MAX	/* max value for an unsigned short */
#define	SHRT_MAX	__SHRT_MAX	/* max value for a short */
#define	SHRT_MIN	__SHRT_MIN	/* min value for a short */

#define	UINT_MAX	__UINT_MAX	/* max value for an unsigned int */
#define	INT_MAX		__INT_MAX	/* max value for an int */
#define	INT_MIN		__INT_MIN	/* min value for an int */

#define	ULONG_MAX	__ULONG_MAX	/* max for an unsigned long */
#define	LONG_MAX	__LONG_MAX	/* max for a long */
#define	LONG_MIN	__LONG_MIN	/* min for a long */

#ifdef __LONG_LONG_SUPPORTED
#define	ULLONG_MAX	__ULLONG_MAX	/* max for an unsigned long long */
#define	LLONG_MAX	__LLONG_MAX	/* max for a long long */
#define	LLONG_MIN	__LLONG_MIN	/* min for a long long */
#endif

#if __POSIX_VISIBLE || __XSI_VISIBLE
#define	SSIZE_MAX	__SSIZE_MAX	/* max value for an ssize_t */
#endif

#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE
#define	SIZE_T_MAX	__SIZE_T_MAX	/* max value for a size_t */

#define	OFF_MAX		__OFF_MAX	/* max value for an off_t */
#define	OFF_MIN		__OFF_MIN	/* min value for an off_t */
#endif

#if __BSD_VISIBLE
#define	GID_MAX		UINT_MAX	/* max value for a gid_t */
#define	UID_MAX		UINT_MAX	/* max value for a uid_t */

#define	UQUAD_MAX	(__UQUAD_MAX)	/* max value for a uquad_t */
#define	QUAD_MAX	(__QUAD_MAX)	/* max value for a quad_t */
#define	QUAD_MIN	(__QUAD_MIN)	/* min value for a quad_t */
#endif

#if __XSI_VISIBLE || __POSIX_VISIBLE >= 200809
#define	LONG_BIT	__LONG_BIT
#define	WORD_BIT	__WORD_BIT
#endif

#if __POSIX_VISIBLE
#define	MQ_PRIO_MAX	64
#endif

#endif /* !_SYS_LIMITS_H_ */
