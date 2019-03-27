/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2002 Mike Barcroft <mike@FreeBSD.org>
 * Copyright (c) 1990, 1993
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	From: @(#)ansi.h	8.2 (Berkeley) 1/4/94
 *	From: @(#)types.h	8.3 (Berkeley) 1/5/94
 * $FreeBSD$
 */

#ifndef _MACHINE__TYPES_H_
#define	_MACHINE__TYPES_H_

#ifndef _SYS_CDEFS_H_
#error this file needs sys/cdefs.h as a prerequisite
#endif

#include <machine/_limits.h>

#define __NO_STRICT_ALIGNMENT

/*
 * Basic types upon which most other types are built.
 */
typedef	signed char		__int8_t;
typedef	unsigned char		__uint8_t;
typedef	short			__int16_t;
typedef	unsigned short		__uint16_t;
typedef	int			__int32_t;
typedef	unsigned int		__uint32_t;
#ifdef	__LP64__
typedef	long			__int64_t;
typedef	unsigned long		__uint64_t;
#else
__extension__
typedef	long long		__int64_t;
__extension__
typedef	unsigned long long	__uint64_t;
#endif

/*
 * Standard type definitions.
 */
#ifdef	__LP64__
typedef	__int32_t	__clock_t;		/* clock()... */
typedef	__int64_t	__critical_t;
#ifndef _STANDALONE
typedef	double		__double_t;
typedef	float		__float_t;
#endif
typedef	__int64_t	__intfptr_t;
typedef	__int64_t	__intptr_t;
#else
typedef	unsigned long	__clock_t;
typedef	__int32_t	__critical_t;
#ifndef _STANDALONE
typedef	long double	__double_t;
typedef	long double	__float_t;
#endif
typedef	__int32_t	__intfptr_t;
typedef	__int32_t	__intptr_t;
#endif
typedef	__int64_t	__intmax_t;
typedef	__int32_t	__int_fast8_t;
typedef	__int32_t	__int_fast16_t;
typedef	__int32_t	__int_fast32_t;
typedef	__int64_t	__int_fast64_t;
typedef	__int8_t	__int_least8_t;
typedef	__int16_t	__int_least16_t;
typedef	__int32_t	__int_least32_t;
typedef	__int64_t	__int_least64_t;
#ifdef	__LP64__
typedef	__int64_t	__ptrdiff_t;		/* ptr1 - ptr2 */
typedef	__int64_t	__register_t;
typedef	__int64_t	__segsz_t;		/* segment size (in pages) */
typedef	__uint64_t	__size_t;		/* sizeof() */
typedef	__int64_t	__ssize_t;		/* byte count or error */
typedef	__int64_t	__time_t;		/* time()... */
typedef	__uint64_t	__uintfptr_t;
typedef	__uint64_t	__uintptr_t;
#else
typedef	__int32_t	__ptrdiff_t;
typedef	__int32_t	__register_t;
typedef	__int32_t	__segsz_t;
typedef	__uint32_t	__size_t;
typedef	__int32_t	__ssize_t;
typedef	__int32_t	__time_t;
typedef	__uint32_t	__uintfptr_t;
typedef	__uint32_t	__uintptr_t;
#endif
typedef	__uint64_t	__uintmax_t;
typedef	__uint32_t	__uint_fast8_t;
typedef	__uint32_t	__uint_fast16_t;
typedef	__uint32_t	__uint_fast32_t;
typedef	__uint64_t	__uint_fast64_t;
typedef	__uint8_t	__uint_least8_t;
typedef	__uint16_t	__uint_least16_t;
typedef	__uint32_t	__uint_least32_t;
typedef	__uint64_t	__uint_least64_t;
#ifdef	__LP64__
typedef	__uint64_t	__u_register_t;
typedef	__uint64_t	__vm_offset_t;
typedef	__uint64_t	__vm_paddr_t;
typedef	__uint64_t	__vm_size_t;
#else
typedef	__uint32_t	__u_register_t;
typedef	__uint32_t	__vm_offset_t;
typedef	__uint64_t	__vm_paddr_t;
typedef	__uint32_t	__vm_size_t;
#endif
typedef	int		___wchar_t;

#define	__WCHAR_MIN	__INT_MIN	/* min value for a wchar_t */
#define	__WCHAR_MAX	__INT_MAX	/* max value for a wchar_t */

#endif /* !_MACHINE__TYPES_H_ */
