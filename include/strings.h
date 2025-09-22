/*	$OpenBSD: strings.h,v 1.6 2017/09/10 21:50:36 schwarze Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
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
 *	@(#)strings.h	5.8 (Berkeley) 5/15/90
 */

#ifndef _STRINGS_H_
#define	_STRINGS_H_

#include <sys/cdefs.h>
#include <machine/_types.h>

/*
 * POSIX mandates that certain string functions not present in ISO C
 * be prototyped in strings.h.
 */

#ifndef	_SIZE_T_DEFINED_
#define	_SIZE_T_DEFINED_
typedef	__size_t	size_t;
#endif

#if __POSIX_VISIBLE >= 200809
#ifndef	_LOCALE_T_DEFINED_
#define	_LOCALE_T_DEFINED_
typedef void	*locale_t;
#endif
#endif

__BEGIN_DECLS
#if __BSD_VISIBLE || (__XPG_VISIBLE >= 420 && __POSIX_VISIBLE <= 200112)
/*
 * The following functions were removed from IEEE Std 1003.1-2008
 */
int	 bcmp(const void *, const void *, size_t);
void	 bcopy(const void *, void *, size_t)
		__attribute__ ((__bounded__(__buffer__,1,3)))
		__attribute__ ((__bounded__(__buffer__,2,3)));
void	 bzero(void *, size_t)
		__attribute__ ((__bounded__(__buffer__,1,2)));
char	*index(const char *, int);
char	*rindex(const char *, int);
#endif

#if __XPG_VISIBLE >= 420
int	 ffs(int);
int	 strcasecmp(const char *, const char *);
int	 strncasecmp(const char *, const char *, size_t);
#endif
#if __POSIX_VISIBLE >= 200809
int	 strcasecmp_l(const char *, const char *, locale_t);
int	 strncasecmp_l(const char *, const char *, size_t, locale_t);
#endif
__END_DECLS

#endif /* _STRINGS_H_ */
