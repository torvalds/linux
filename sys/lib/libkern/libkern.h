/*	$OpenBSD: libkern.h,v 1.37 2023/12/21 02:57:14 jsg Exp $	*/
/*	$NetBSD: libkern.h,v 1.7 1996/03/14 18:52:08 christos Exp $	*/

/*-
 * Copyright (c) 1992, 1993
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
 *	@(#)libkern.h	8.1 (Berkeley) 6/10/93
 */

#ifndef __LIBKERN_H__
#define __LIBKERN_H__

#include <sys/types.h>

#ifndef LIBKERN_INLINE
#define LIBKERN_INLINE	static __inline
#define LIBKERN_BODY
#endif


LIBKERN_INLINE int imax(int, int);
LIBKERN_INLINE int imin(int, int);
LIBKERN_INLINE u_int max(u_int, u_int);
LIBKERN_INLINE u_int min(u_int, u_int);
LIBKERN_INLINE long lmax(long, long);
LIBKERN_INLINE long lmin(long, long);
LIBKERN_INLINE u_long ulmax(u_long, u_long);
LIBKERN_INLINE u_long ulmin(u_long, u_long);
LIBKERN_INLINE int abs(int);

#ifdef LIBKERN_BODY
LIBKERN_INLINE int
imax(int a, int b)
{
	return (a > b ? a : b);
}
LIBKERN_INLINE int
imin(int a, int b)
{
	return (a < b ? a : b);
}
LIBKERN_INLINE long
lmax(long a, long b)
{
	return (a > b ? a : b);
}
LIBKERN_INLINE long
lmin(long a, long b)
{
	return (a < b ? a : b);
}
LIBKERN_INLINE u_int
max(u_int a, u_int b)
{
	return (a > b ? a : b);
}
LIBKERN_INLINE u_int
min(u_int a, u_int b)
{
	return (a < b ? a : b);
}
LIBKERN_INLINE u_long
ulmax(u_long a, u_long b)
{
	return (a > b ? a : b);
}
LIBKERN_INLINE u_long
ulmin(u_long a, u_long b)
{
	return (a < b ? a : b);
}

LIBKERN_INLINE int
abs(int j)
{
	return(j < 0 ? -j : j);
}
#endif

#ifdef NDEBUG						/* tradition! */
#define	assert(e)	((void)0)
#else
#define	assert(e)	((e) ? (void)0 :				    \
			    __assert("", __FILE__, __LINE__, #e))
#endif

#define	__KASSERTSTR	"kernel %sassertion \"%s\" failed: file \"%s\", line %d"

#ifndef DIAGNOSTIC
#define	KASSERTMSG(e, msg, ...)	((void)0)
#define	KASSERT(e)	((void)0)
#else
#define	KASSERTMSG(e, msg, ...)	((e) ? (void)0 :			    \
			    panic(__KASSERTSTR " " msg, "diagnostic ", #e,  \
			    __FILE__, __LINE__, ## __VA_ARGS__))
#define	KASSERT(e)	((e) ? (void)0 :				    \
			    __assert("diagnostic ", __FILE__, __LINE__, #e))
#endif

#ifndef DEBUG
#define	KDASSERTMSG(e, msg, ...)	((void)0)
#define	KDASSERT(e)	((void)0)
#else
#define	KDASSERTMSG(e, msg, ...)	((e) ? (void)0 :		    \
			    panic(__KASSERTSTR " " msg, "debugging ", #e,   \
			    __FILE__, __LINE__, ## __VA_ARGS__))
#define	KDASSERT(e)	((e) ? (void)0 :				    \
			    __assert("debugging ", __FILE__, __LINE__, #e))
#endif

#define	CTASSERT(x)	extern char  _ctassert[(x) ? 1 : -1 ]	\
			    __attribute__((__unused__))

/* Prototypes for non-quad routines. */
void	 __assert(const char *, const char *, int, const char *)
	    __attribute__ ((__noreturn__));
int	 bcmp(const void *, const void *, size_t);
void	 bzero(void *, size_t);
void	 explicit_bzero(void *, size_t);
int	 ffs(int);
int	 fls(int);
int	 flsl(long);
void	*memchr(const void *, int, size_t);
int	 memcmp(const void *, const void *, size_t);
void	*memset(void *, int c, size_t len);
u_int32_t random(void);
int	 scanc(u_int, const u_char *, const u_char [], int);
int	 skpc(int, size_t, u_char *);
size_t	 strlen(const char *);
char	*strncpy(char *, const char *, size_t)
		__attribute__ ((__bounded__(__string__,1,3)));
size_t	 strnlen(const char *, size_t);
size_t	 strlcpy(char *, const char *, size_t)
		__attribute__ ((__bounded__(__string__,1,3)));
size_t	 strlcat(char *, const char *, size_t)
		__attribute__ ((__bounded__(__string__,1,3)));
int	 strcmp(const char *, const char *);
int	 strncmp(const char *, const char *, size_t);
int	 strncasecmp(const char *, const char *, size_t);
size_t	 getsn(char *, size_t)
		__attribute__ ((__bounded__(__string__,1,2)));
char	*strchr(const char *, int);
char	*strrchr(const char *, int);
int	 timingsafe_bcmp(const void *, const void *, size_t);
char	*strnstr(const char *, const char *, size_t);

#endif /* __LIBKERN_H__ */
