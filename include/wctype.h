/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)1999 Citrus Project,
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	citrus Id: wctype.h,v 1.4 2000/12/21 01:50:21 itojun Exp
 *	$NetBSD: wctype.h,v 1.3 2000/12/22 14:16:16 itojun Exp $
 * $FreeBSD$
 */

#ifndef _WCTYPE_H_
#define	_WCTYPE_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#include <_ctype.h>

#ifndef _WCTRANS_T
typedef	int	wctrans_t;
#define	_WCTRANS_T
#endif

#ifndef _WCTYPE_T
typedef	unsigned long	wctype_t;
#define	_WCTYPE_T
#endif

#ifndef _WINT_T_DECLARED
typedef	__wint_t	wint_t;
#define	_WINT_T_DECLARED
#endif

#ifndef WEOF
#define	WEOF	((wint_t)-1)
#endif

__BEGIN_DECLS
int	iswalnum(wint_t);
int	iswalpha(wint_t);
int	iswblank(wint_t);
int	iswcntrl(wint_t);
int	iswctype(wint_t, wctype_t);
int	iswdigit(wint_t);
int	iswgraph(wint_t);
int	iswlower(wint_t);
int	iswprint(wint_t);
int	iswpunct(wint_t);
int	iswspace(wint_t);
int	iswupper(wint_t);
int	iswxdigit(wint_t);
wint_t	towctrans(wint_t, wctrans_t);
wint_t	towlower(wint_t);
wint_t	towupper(wint_t);
wctrans_t
	wctrans(const char *);
wctype_t
	wctype(const char *);

#if __BSD_VISIBLE
wint_t	iswascii(wint_t);
wint_t	iswhexnumber(wint_t);
wint_t	iswideogram(wint_t);
wint_t	iswnumber(wint_t);
wint_t	iswphonogram(wint_t);
wint_t	iswrune(wint_t);
wint_t	iswspecial(wint_t);
wint_t	nextwctype(wint_t, wctype_t);
#endif

#if __POSIX_VISIBLE >= 200809
#define _XLOCALE_WCTYPES 1
#include <xlocale/_ctype.h>
#endif /* __POSIX_VISIBLE >= 200809 */
__END_DECLS

#ifndef __cplusplus
#define	iswalnum(wc)		__istype((wc), _CTYPE_A|_CTYPE_D|_CTYPE_N)
#define	iswalpha(wc)		__istype((wc), _CTYPE_A)
#define	iswblank(wc)		__istype((wc), _CTYPE_B)
#define	iswcntrl(wc)		__istype((wc), _CTYPE_C)
#define	iswctype(wc, charclass)	__istype((wc), (charclass))
#define	iswdigit(wc)		__istype((wc), _CTYPE_D)
#define	iswgraph(wc)		__istype((wc), _CTYPE_G)
#define	iswlower(wc)		__istype((wc), _CTYPE_L)
#define	iswprint(wc)		__istype((wc), _CTYPE_R)
#define	iswpunct(wc)		__istype((wc), _CTYPE_P)
#define	iswspace(wc)		__istype((wc), _CTYPE_S)
#define	iswupper(wc)		__istype((wc), _CTYPE_U)
#define	iswxdigit(wc)		__istype((wc), _CTYPE_X)
#define	towlower(wc)		__tolower(wc)
#define	towupper(wc)		__toupper(wc)

#if __BSD_VISIBLE
#define	iswascii(wc)		(((wc) & ~0x7F) == 0)
#define	iswhexnumber(wc)	__istype((wc), _CTYPE_X) /* alias of iswxdigit */
#define	iswideogram(wc)		__istype((wc), _CTYPE_I)
#define	iswnumber(wc)		__istype((wc), _CTYPE_D|_CTYPE_N)
#define	iswphonogram(wc)	__istype((wc), _CTYPE_Q)
#define	iswrune(wc)		__istype((wc), 0xFFFFFF00L)
#define	iswspecial(wc)		__istype((wc), _CTYPE_T)
#endif /* __BSD_VISIBLE */
#endif /* __cplusplus */

#endif		/* _WCTYPE_H_ */
