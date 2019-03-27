/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)string.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD$
 */

#ifndef _STRING_H_
#define	_STRING_H_

#include <sys/cdefs.h>
#include <sys/_null.h>
#include <sys/_types.h>

/*
 * Prototype functions which were historically defined in <string.h>, but
 * are required by POSIX to be prototyped in <strings.h>.
 */
#if __BSD_VISIBLE
#include <strings.h>
#endif

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

__BEGIN_DECLS
#if __XSI_VISIBLE >= 600
void	*memccpy(void * __restrict, const void * __restrict, int, size_t);
#endif
void	*memchr(const void *, int, size_t) __pure;
#if __BSD_VISIBLE
void	*memrchr(const void *, int, size_t) __pure;
#endif
int	 memcmp(const void *, const void *, size_t) __pure;
void	*memcpy(void * __restrict, const void * __restrict, size_t);
#if __BSD_VISIBLE
void	*memmem(const void *, size_t, const void *, size_t) __pure;
#endif
void	*memmove(void *, const void *, size_t);
void	*memset(void *, int, size_t);
#if __POSIX_VISIBLE >= 200809
char	*stpcpy(char * __restrict, const char * __restrict);
char	*stpncpy(char * __restrict, const char * __restrict, size_t);
#endif
#if __BSD_VISIBLE
char	*strcasestr(const char *, const char *) __pure;
#endif
char	*strcat(char * __restrict, const char * __restrict);
char	*strchr(const char *, int) __pure;
#if __BSD_VISIBLE
char	*strchrnul(const char*, int) __pure;
#endif
int	 strcmp(const char *, const char *) __pure;
int	 strcoll(const char *, const char *);
char	*strcpy(char * __restrict, const char * __restrict);
size_t	 strcspn(const char *, const char *) __pure;
#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE
char	*strdup(const char *) __malloc_like;
#endif
char	*strerror(int);
#if __POSIX_VISIBLE >= 200112
int	 strerror_r(int, char *, size_t);
#endif
#if __BSD_VISIBLE
size_t	 strlcat(char * __restrict, const char * __restrict, size_t);
size_t	 strlcpy(char * __restrict, const char * __restrict, size_t);
#endif
size_t	 strlen(const char *) __pure;
#if __BSD_VISIBLE
void	 strmode(int, char *);
#endif
char	*strncat(char * __restrict, const char * __restrict, size_t);
int	 strncmp(const char *, const char *, size_t) __pure;
char	*strncpy(char * __restrict, const char * __restrict, size_t);
#if __POSIX_VISIBLE >= 200809
char	*strndup(const char *, size_t) __malloc_like;
size_t	 strnlen(const char *, size_t) __pure;
#endif
#if __BSD_VISIBLE
char	*strnstr(const char *, const char *, size_t) __pure;
#endif
char	*strpbrk(const char *, const char *) __pure;
char	*strrchr(const char *, int) __pure;
#if __BSD_VISIBLE
char	*strsep(char **, const char *);
#endif
#if __POSIX_VISIBLE >= 200809
char	*strsignal(int);
#endif
size_t	 strspn(const char *, const char *) __pure;
char	*strstr(const char *, const char *) __pure;
char	*strtok(char * __restrict, const char * __restrict);
#if __POSIX_VISIBLE >= 199506 || __XSI_VISIBLE >= 500
char	*strtok_r(char *, const char *, char **);
#endif
size_t	 strxfrm(char * __restrict, const char * __restrict, size_t);
#if __BSD_VISIBLE

#ifndef _SWAB_DECLARED
#define _SWAB_DECLARED

#ifndef _SSIZE_T_DECLARED
typedef	__ssize_t	ssize_t;
#define	_SSIZE_T_DECLARED
#endif /* _SIZE_T_DECLARED */

void	 swab(const void * __restrict, void * __restrict, ssize_t);
#endif /* _SWAB_DECLARED */

int	 timingsafe_bcmp(const void *, const void *, size_t);
int	 timingsafe_memcmp(const void *, const void *, size_t);
#endif /* __BSD_VISIBLE */

#if __POSIX_VISIBLE >= 200809 || defined(_XLOCALE_H_)
#include <xlocale/_string.h>
#endif

#if __EXT1_VISIBLE

#ifndef _RSIZE_T_DEFINED
#define _RSIZE_T_DEFINED
typedef size_t rsize_t;
#endif

#ifndef _ERRNO_T_DEFINED
#define _ERRNO_T_DEFINED
typedef int errno_t;
#endif

/* ISO/IEC 9899:2011 K.3.7.4.1.1 */
errno_t memset_s(void *, rsize_t, int, rsize_t);
#endif /* __EXT1_VISIBLE */
__END_DECLS

#endif /* _STRING_H_ */
