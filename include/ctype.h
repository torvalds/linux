/*	$OpenBSD: ctype.h,v 1.26 2024/02/04 13:03:18 jca Exp $	*/
/*	$NetBSD: ctype.h,v 1.14 1994/10/26 00:55:47 cgd Exp $	*/

/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
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
 *	@(#)ctype.h	5.3 (Berkeley) 4/3/91
 */

#ifndef _CTYPE_H_
#define _CTYPE_H_

#include <sys/cdefs.h>

#define	_CTYPE_U	0x01
#define	_CTYPE_L	0x02
#define	_CTYPE_N	0x04
#define	_CTYPE_S	0x08
#define	_CTYPE_P	0x10
#define	_CTYPE_C	0x20
#define	_CTYPE_X	0x40
#define	_CTYPE_B	0x80

#if __POSIX_VISIBLE >= 200809
#ifndef	_LOCALE_T_DEFINED_
#define	_LOCALE_T_DEFINED_
typedef void	*locale_t;
#endif
#endif

__BEGIN_DECLS

extern const char	*_ctype_;
extern const short	*_tolower_tab_;
extern const short	*_toupper_tab_;

#if defined(__GNUC__) || defined(_ANSI_LIBRARY)
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

#if __BSD_VISIBLE || __ISO_C_VISIBLE >= 1999 || __POSIX_VISIBLE > 200112 \
    || __XPG_VISIBLE > 600
int	isblank(int);
#endif

#if __BSD_VISIBLE || __XPG_VISIBLE
int	isascii(int);
int	toascii(int);
int	_tolower(int);
int	_toupper(int);
#endif /* __BSD_VISIBLE || __XPG_VISIBLE */

#if __POSIX_VISIBLE >= 200809
int	isalnum_l(int, locale_t);
int	isalpha_l(int, locale_t);
int	isblank_l(int, locale_t);
int	iscntrl_l(int, locale_t);
int	isdigit_l(int, locale_t);
int	isgraph_l(int, locale_t);
int	islower_l(int, locale_t);
int	isprint_l(int, locale_t);
int	ispunct_l(int, locale_t);
int	isspace_l(int, locale_t);
int	isupper_l(int, locale_t);
int	isxdigit_l(int, locale_t);
int	tolower_l(int, locale_t);
int	toupper_l(int, locale_t);
#endif

#endif /* __GNUC__ || _ANSI_LIBRARY */

#if !defined(_ANSI_LIBRARY) && !defined(__cplusplus)

__only_inline int isalnum(int _c)
{
	return (_c == -1 ? 0 : ((_ctype_ + 1)[(unsigned char)_c] &
	    (_CTYPE_U|_CTYPE_L|_CTYPE_N)));
}

__only_inline int isalpha(int _c)
{
	return (_c == -1 ? 0 : ((_ctype_ + 1)[(unsigned char)_c] &
	    (_CTYPE_U|_CTYPE_L)));
}

__only_inline int iscntrl(int _c)
{
	return (_c == -1 ? 0 : ((_ctype_ + 1)[(unsigned char)_c] & _CTYPE_C));
}

__only_inline int isdigit(int _c)
{
	return (_c == -1 ? 0 : ((_ctype_ + 1)[(unsigned char)_c] & _CTYPE_N));
}

__only_inline int isgraph(int _c)
{
	return (_c == -1 ? 0 : ((_ctype_ + 1)[(unsigned char)_c] &
	    (_CTYPE_P|_CTYPE_U|_CTYPE_L|_CTYPE_N)));
}

__only_inline int islower(int _c)
{
	return (_c == -1 ? 0 : ((_ctype_ + 1)[(unsigned char)_c] & _CTYPE_L));
}

__only_inline int isprint(int _c)
{
	return (_c == -1 ? 0 : ((_ctype_ + 1)[(unsigned char)_c] &
	    (_CTYPE_P|_CTYPE_U|_CTYPE_L|_CTYPE_N|_CTYPE_B)));
}

__only_inline int ispunct(int _c)
{
	return (_c == -1 ? 0 : ((_ctype_ + 1)[(unsigned char)_c] & _CTYPE_P));
}

__only_inline int isspace(int _c)
{
	return (_c == -1 ? 0 : ((_ctype_ + 1)[(unsigned char)_c] & _CTYPE_S));
}

__only_inline int isupper(int _c)
{
	return (_c == -1 ? 0 : ((_ctype_ + 1)[(unsigned char)_c] & _CTYPE_U));
}

__only_inline int isxdigit(int _c)
{
	return (_c == -1 ? 0 : ((_ctype_ + 1)[(unsigned char)_c] &
	    (_CTYPE_N|_CTYPE_X)));
}

__only_inline int tolower(int _c)
{
	if ((unsigned int)_c > 255)
		return (_c);
	return ((_tolower_tab_ + 1)[_c]);
}

__only_inline int toupper(int _c)
{
	if ((unsigned int)_c > 255)
		return (_c);
	return ((_toupper_tab_ + 1)[_c]);
}

#if __BSD_VISIBLE || __ISO_C_VISIBLE >= 1999 || __POSIX_VISIBLE > 200112 \
    || __XPG_VISIBLE > 600
__only_inline int isblank(int _c)
{
	return (_c == ' ' || _c == '\t');
}
#endif

#if __BSD_VISIBLE || __XPG_VISIBLE
__only_inline int isascii(int _c)
{
	return ((unsigned int)_c <= 0177);
}

__only_inline int toascii(int _c)
{
	return (_c & 0177);
}

__only_inline int _tolower(int _c)
{
	return (_c - 'A' + 'a');
}

__only_inline int _toupper(int _c)
{
	return (_c - 'a' + 'A');
}
#endif /* __BSD_VISIBLE || __XPG_VISIBLE */

#if __POSIX_VISIBLE >= 200809
__only_inline int
isalnum_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return isalnum(_c);
}

__only_inline int
isalpha_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return isalpha(_c);
}

__only_inline int
isblank_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return isblank(_c);
}

__only_inline int
iscntrl_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return iscntrl(_c);
}

__only_inline int
isdigit_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return isdigit(_c);
}

__only_inline int
isgraph_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return isgraph(_c);
}

__only_inline int
islower_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return islower(_c);
}

__only_inline int
isprint_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return isprint(_c);
}

__only_inline int
ispunct_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return ispunct(_c);
}

__only_inline int
isspace_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return isspace(_c);
}

__only_inline int
isupper_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return isupper(_c);
}

__only_inline int
isxdigit_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return isxdigit(_c);
}

__only_inline int
tolower_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return tolower(_c);
}

__only_inline int
toupper_l(int _c, locale_t _l __attribute__((__unused__)))
{
	return toupper(_c);
}
#endif /* __POSIX_VISIBLE >= 200809 */

#endif /* !_ANSI_LIBRARY */

__END_DECLS

#endif /* !_CTYPE_H_ */
