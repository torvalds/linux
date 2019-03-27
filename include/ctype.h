/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Borman at Krystal Technologies.
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
 *	@(#)ctype.h	8.4 (Berkeley) 1/21/94
 *      $FreeBSD$
 */

#ifndef _CTYPE_H_
#define	_CTYPE_H_

#include <sys/cdefs.h>
#include <sys/_types.h>
#include <_ctype.h>

__BEGIN_DECLS
int	isalnum(int);
int	isalpha(int);
int	iscntrl(int);
int	isdigit(int);
int	isgraph(int);
int	islower(int);
int	isprint(int);
int	ispunct(int);
int	isspace(int);
int	isupper(int);
int	isxdigit(int);
int	tolower(int);
int	toupper(int);

#if __XSI_VISIBLE
int	isascii(int);
int	toascii(int);
#endif

#if __ISO_C_VISIBLE >= 1999
int	isblank(int);
#endif

#if __BSD_VISIBLE
int	digittoint(int);
int	ishexnumber(int);
int	isideogram(int);
int	isnumber(int);
int	isphonogram(int);
int	isrune(int);
int	isspecial(int);
#endif

#if __POSIX_VISIBLE >= 200809 || defined(_XLOCALE_H_)
#include <xlocale/_ctype.h>
#endif
__END_DECLS

#ifndef __cplusplus
#define	isalnum(c)	__sbistype((c), _CTYPE_A|_CTYPE_D|_CTYPE_N)
#define	isalpha(c)	__sbistype((c), _CTYPE_A)
#define	iscntrl(c)	__sbistype((c), _CTYPE_C)
#define	isdigit(c)	__sbistype((c), _CTYPE_D)
#define	isgraph(c)	__sbistype((c), _CTYPE_G)
#define	islower(c)	__sbistype((c), _CTYPE_L)
#define	isprint(c)	__sbistype((c), _CTYPE_R)
#define	ispunct(c)	__sbistype((c), _CTYPE_P)
#define	isspace(c)	__sbistype((c), _CTYPE_S)
#define	isupper(c)	__sbistype((c), _CTYPE_U)
#define	isxdigit(c)	__sbistype((c), _CTYPE_X)
#define	tolower(c)	__sbtolower(c)
#define	toupper(c)	__sbtoupper(c)
#endif /* !__cplusplus */

#if __XSI_VISIBLE
/*
 * POSIX.1-2001 specifies _tolower() and _toupper() to be macros equivalent to
 * tolower() and toupper() respectively, minus extra checking to ensure that
 * the argument is a lower or uppercase letter respectively.  We've chosen to
 * implement these macros with the same error checking as tolower() and
 * toupper() since this doesn't violate the specification itself, only its
 * intent.  We purposely leave _tolower() and _toupper() undocumented to
 * discourage their use.
 *
 * XXX isascii() and toascii() should similarly be undocumented.
 */
#define	_tolower(c)	__sbtolower(c)
#define	_toupper(c)	__sbtoupper(c)
#define	isascii(c)	(((c) & ~0x7F) == 0)
#define	toascii(c)	((c) & 0x7F)
#endif

#if __ISO_C_VISIBLE >= 1999 && !defined(__cplusplus)
#define	isblank(c)	__sbistype((c), _CTYPE_B)
#endif

#if __BSD_VISIBLE
#define	digittoint(c)	__sbmaskrune((c), 0xFF)
#define	ishexnumber(c)	__sbistype((c), _CTYPE_X)
#define	isideogram(c)	__sbistype((c), _CTYPE_I)
#define	isnumber(c)	__sbistype((c), _CTYPE_D|_CTYPE_N)
#define	isphonogram(c)	__sbistype((c), _CTYPE_Q)
#define	isrune(c)	__sbistype((c), 0xFFFFFF00L)
#define	isspecial(c)	__sbistype((c), _CTYPE_T)
#endif

#endif /* !_CTYPE_H_ */
