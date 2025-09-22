/*	$OpenBSD: string.h,v 1.34 2024/08/03 20:13:23 guenther Exp $	*/
/*	$NetBSD: string.h,v 1.6 1994/10/26 00:56:30 cgd Exp $	*/

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
 *	@(#)string.h	5.10 (Berkeley) 3/9/91
 */

#ifndef _STRING_H_
#define	_STRING_H_

#include <sys/cdefs.h>
#include <sys/_null.h>
#include <sys/_types.h>

/*
 * POSIX mandates that certain string functions not present in ISO C
 * be prototyped in strings.h.  Historically, we've included them here.
 */
#if __BSD_VISIBLE
#include <strings.h>
#endif

#ifndef	_SIZE_T_DEFINED_
#define	_SIZE_T_DEFINED_
typedef	__size_t	size_t;
#endif

#if __POSIX_VISIBLE >= 200809
#ifndef _LOCALE_T_DEFINED_
#define _LOCALE_T_DEFINED_
typedef void	*locale_t;
#endif
#endif

__BEGIN_DECLS
void	*memchr(const void *, int, size_t);
int	 memcmp(const void *, const void *, size_t);
void	*memcpy(void *__restrict, const void *__restrict, size_t)
		__attribute__ ((__bounded__(__buffer__,1,3)))
		__attribute__ ((__bounded__(__buffer__,2,3)));
void	*memmove(void *, const void *, size_t)
		__attribute__ ((__bounded__(__buffer__,1,3)))
		__attribute__ ((__bounded__(__buffer__,2,3)));
void	*memset(void *, int, size_t)
		__attribute__ ((__bounded__(__buffer__,1,3)));
char	*strcat(char *__restrict, const char *__restrict);
char	*strchr(const char *, int);
int	 strcmp(const char *, const char *);
int	 strcoll(const char *, const char *);
char	*strcpy(char *__restrict, const char *__restrict);
size_t	 strcspn(const char *, const char *);
char	*strerror(int);
size_t	 strlen(const char *);
char	*strncat(char *__restrict, const char *__restrict, size_t)
		__attribute__ ((__bounded__(__string__,1,3)));
int	 strncmp(const char *, const char *, size_t);
char	*strncpy(char *__restrict, const char *__restrict, size_t)
		__attribute__ ((__bounded__(__string__,1,3)));
char	*strpbrk(const char *, const char *);
char	*strrchr(const char *, int);
size_t	 strspn(const char *, const char *);
char	*strstr(const char *, const char *);
char	*strtok(char *__restrict, const char *__restrict);
char	*strtok_r(char *__restrict, const char *__restrict, char **__restrict);
size_t	 strxfrm(char *__restrict, const char *__restrict, size_t)
		__attribute__ ((__bounded__(__string__,1,3)));

#if __XPG_VISIBLE
void	*memccpy(void *__restrict, const void *__restrict, int, size_t)
		__attribute__ ((__bounded__(__buffer__,1,4)));
#endif

#if __POSIX_VISIBLE >= 200112
int	 strerror_r(int, char *, size_t)
	    __attribute__ ((__bounded__(__string__,2,3)));
#endif

#if __XPG_VISIBLE >= 420 || __POSIX_VISIBLE >= 200809
char	*strdup(const char *);
#endif

#if __POSIX_VISIBLE >= 200809
char	*stpcpy(char *__restrict, const char *__restrict);
char	*stpncpy(char *__restrict, const char *__restrict, size_t);
int	 strcoll_l(const char *, const char *, locale_t);
char	*strerror_l(int, locale_t);
char	*strndup(const char *, size_t);
size_t	 strnlen(const char *, size_t);
char	*strsignal(int);
size_t	 strxfrm_l(char *__restrict, const char *__restrict, size_t, locale_t)
		__attribute__ ((__bounded__(__string__,1,3)));
#endif

#if __POSIX_VISIBLE >= 202405 || __BSD_VISIBLE
void	*memmem(const void *, size_t, const void *, size_t);
size_t	 strlcat(char *__restrict, const char *__restrict, size_t)
		__attribute__ ((__bounded__(__string__,1,3)));
size_t	 strlcpy(char *__restrict, const char *__restrict, size_t)
		__attribute__ ((__bounded__(__string__,1,3)));
#endif

#if __BSD_VISIBLE
void	 explicit_bzero(void *, size_t)
		__attribute__ ((__bounded__(__buffer__,1,2)));
void	*memrchr(const void *, int, size_t);
char	*strcasestr(const char *, const char *);
void	 strmode(__mode_t, char *);
char	*strsep(char **, const char *);
int	 timingsafe_bcmp(const void *, const void *, size_t);
int	 timingsafe_memcmp(const void *, const void *, size_t);
#endif 
__END_DECLS

#endif /* _STRING_H_ */
