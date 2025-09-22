/*	$OpenBSD: stdlib.h,v 1.78 2024/08/03 20:09:24 guenther Exp $	*/
/*	$NetBSD: stdlib.h,v 1.25 1995/12/27 21:19:08 jtc Exp $	*/

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
 *	@(#)stdlib.h	5.13 (Berkeley) 6/4/91
 */

#ifndef _STDLIB_H_
#define _STDLIB_H_

#include <sys/cdefs.h>
#include <sys/_null.h>
#include <machine/_types.h>
#if __BSD_VISIBLE	/* for quad_t, etc. (XXX - use protected types) */
#include <sys/types.h>
#endif

#ifndef	_SIZE_T_DEFINED_
#define	_SIZE_T_DEFINED_
typedef	__size_t	size_t;
#endif

/* in C++, wchar_t is a built-in type */
#if !defined(_WCHAR_T_DEFINED_) && !defined(__cplusplus)
#define _WCHAR_T_DEFINED_
typedef	__wchar_t	wchar_t;
#endif

typedef struct {
	int quot;		/* quotient */
	int rem;		/* remainder */
} div_t;

typedef struct {
	long quot;		/* quotient */
	long rem;		/* remainder */
} ldiv_t;

#if __ISO_C_VISIBLE >= 1999
typedef struct {
	long long quot;		/* quotient */
	long long rem;		/* remainder */
} lldiv_t;
#endif

#if __BSD_VISIBLE
typedef struct {
	quad_t quot;		/* quotient */
	quad_t rem;		/* remainder */
} qdiv_t;
#endif

#define	EXIT_FAILURE	1
#define	EXIT_SUCCESS	0

#define	RAND_MAX	0x7fffffff

#define	MB_CUR_MAX	__mb_cur_max()

/*
 * Some header files may define an abs macro.
 * If defined, undef it to prevent a syntax error and issue a warning.
 */
#ifdef abs
#undef abs
#warning abs macro collides with abs() prototype, undefining
#endif

__BEGIN_DECLS
__dead void	 abort(void);
int	 abs(int);
int	 atexit(void (*)(void));
double	 atof(const char *);
int	 atoi(const char *);
long	 atol(const char *);
void	*bsearch(const void *, const void *, size_t, size_t,
	    int (*)(const void *, const void *));
void	*calloc(size_t, size_t);
div_t	 div(int, int);
__dead void	 exit(int);
__dead void	 _Exit(int);
void	 free(void *);
char	*getenv(const char *);
long	 labs(long);
ldiv_t	 ldiv(long, long);
void	*malloc(size_t);
#if __BSD_VISIBLE
void	freezero(void *, size_t)
		 __attribute__ ((__bounded__(__buffer__,1,2)));
void	*calloc_conceal(size_t, size_t);
void	*malloc_conceal(size_t);
void	*recallocarray(void *, size_t, size_t, size_t);
#endif /* __BSD_VISIBLE */
void	 qsort(void *, size_t, size_t, int (*)(const void *, const void *));
int	 rand(void);
void	*realloc(void *, size_t);
void	 srand(unsigned);
void	 srand_deterministic(unsigned);
double	 strtod(const char *__restrict, char **__restrict);
float	 strtof(const char *__restrict, char **__restrict);
long	 strtol(const char *__restrict, char **__restrict, int);
long double
	 strtold(const char *__restrict, char **__restrict);
unsigned long
	 strtoul(const char *__restrict, char **__restrict, int);
int	 system(const char *);

size_t	 __mb_cur_max(void);
int	 mblen(const char *, size_t);
size_t	 mbstowcs(wchar_t *, const char *, size_t);
int	 wctomb(char *, wchar_t);
int	 mbtowc(wchar_t *, const char *, size_t);
size_t	 wcstombs(char *, const wchar_t *, size_t);

/*
 * IEEE Std 1003.1c-95, also adopted by X/Open CAE Spec Issue 5 Version 2
 */
#if __BSD_VISIBLE || __POSIX_VISIBLE >= 199506 || defined(_REENTRANT)
int	 rand_r(unsigned int *);
#endif

#if __BSD_VISIBLE || __XPG_VISIBLE >= 400
double	 drand48(void);
double	 erand48(unsigned short[3]);
long	 jrand48(unsigned short[3]);
void	 lcong48(unsigned short[7]);
void	 lcong48_deterministic(unsigned short[7]);
long	 lrand48(void);
long	 mrand48(void);
long	 nrand48(unsigned short[3]);
unsigned short *seed48(unsigned short[3]);
unsigned short *seed48_deterministic(unsigned short[3]);
void	 srand48(long);
void	 srand48_deterministic(long);

int	 putenv(char *);
#endif

/*
 * XSI functions marked LEGACY in IEEE Std 1003.1-2001 (POSIX) and
 * removed in IEEE Std 1003.1-2008
 */
#if __BSD_VISIBLE || __XPG_VISIBLE < 700
char	*ecvt(double, int, int *, int *);
char	*fcvt(double, int, int *, int *);
char	*gcvt(double, int, char *);
#if __BSD_VISIBLE || __XPG_VISIBLE >= 420
char	*mktemp(char *);
#endif
#endif	/* __BSD_VISIBLE || __XPG_VISIBLE < 700 */

#if __BSD_VISIBLE || __XPG_VISIBLE >= 420
long	 a64l(const char *);
char	*l64a(long);

char	*initstate(unsigned int, char *, size_t)
		__attribute__((__bounded__ (__string__,2,3)));
long	 random(void);
char	*setstate(char *);
void	 srandom(unsigned int);
void	 srandom_deterministic(unsigned int);

char	*realpath(const char *, char *)
		__attribute__((__bounded__ (__minbytes__,2,1024)));

/*
 * XSI functions marked LEGACY in XPG5 and removed in IEEE Std 1003.1-2001
 */
#if __BSD_VISIBLE || __XPG_VISIBLE < 600
int	 ttyslot(void);
void	*valloc(size_t);		/* obsoleted by malloc() */
#endif
#endif /* __BSD_VISIBLE || __XPG_VISIBLE >= 420 */

/*
 * 4.4BSD, then XSI in XPG4.2, then added to POSIX base in IEEE Std 1003.1-2008
 */
#if __BSD_VISIBLE || __XPG_VISIBLE >= 420 || __POSIX_VISIBLE >= 200809
int	 mkstemp(char *);
#endif

/*
 * ISO C99
 */
#if __ISO_C_VISIBLE >= 1999
long long
	 atoll(const char *);
long long
	 llabs(long long);
lldiv_t
	 lldiv(long long, long long);
long long
	 strtoll(const char *__restrict, char **__restrict, int);
unsigned long long
	 strtoull(const char *__restrict, char **__restrict, int);
#endif

#if __ISO_C_VISIBLE >= 2011
void *
	aligned_alloc(size_t, size_t);
#endif

/*
 * The Open Group Base Specifications, Issue 6; IEEE Std 1003.1-2001 (POSIX)
 */
#if __BSD_VISIBLE || __POSIX_VISIBLE >= 200112
int	 posix_memalign(void **, size_t, size_t);
int	 setenv(const char *, const char *, int);
int	 unsetenv(const char *);
#endif
#if __XPG_VISIBLE >= 420 || __POSIX_VISIBLE >= 200112
char	*ptsname(int);
int	 grantpt(int);
int	 unlockpt(int);
#endif
#if __POSIX_VISIBLE >= 200112
int	 posix_openpt(int);
#endif

/*
 * The Open Group Base Specifications, Issue 7; IEEE Std 1003.1-2008 (POSIX)
 */
#if __BSD_VISIBLE || __POSIX_VISIBLE >= 200809
char	*mkdtemp(char *);
#endif

#if __XPG_VISIBLE >= 420 || __POSIX_VISIBLE >= 200809
int     getsubopt(char **, char * const *, char **);
#endif

/*
 * The Open Group Base Specifications, Issue 8
 */
#if __POSIX_VISIBLE >= 202405 || __BSD_VISIBLE
int	mkostemp(char *, int);
void	*reallocarray(void *, size_t, size_t);
#endif

#if __BSD_VISIBLE
#define alloca(n) __builtin_alloca(n)

char	*getbsize(int *, long *);
char	*cgetcap(char *, const char *, int);
int	 cgetclose(void);
int	 cgetent(char **, char **, const char *);
int	 cgetfirst(char **, char **);
int	 cgetmatch(char *, const char *);
int	 cgetnext(char **, char **);
int	 cgetnum(char *, const char *, long *);
int	 cgetset(const char *);
int	 cgetusedb(int);
int	 cgetstr(char *, const char *, char **);
int	 cgetustr(char *, const char *, char **);

int	 daemon(int, int);
char	*devname(dev_t, mode_t);
int	 getloadavg(double [], int);

const char *
	getprogname(void);
void	setprogname(const char *);

extern	 char *suboptarg;		/* getsubopt(3) external variable */

char *	 mkdtemps(char *, int);
int	 mkstemps(char *, int);
int	 mkostemps(char *, int, int);

int	 heapsort(void *, size_t, size_t, int (*)(const void *, const void *));
int	 mergesort(void *, size_t, size_t, int (*)(const void *, const void *));
int	 radixsort(const unsigned char **, int, const unsigned char *,
	    unsigned);
int	 sradixsort(const unsigned char **, int, const unsigned char *,
	    unsigned);

void	 srandomdev(void);
long long
	 strtonum(const char *, long long, long long, const char **);

void	 setproctitle(const char *, ...)
	__attribute__((__format__ (__printf__, 1, 2)));

quad_t	 qabs(quad_t);
qdiv_t	 qdiv(quad_t, quad_t);
quad_t	 strtoq(const char *__restrict, char **__restrict, int);
u_quad_t strtouq(const char *__restrict, char **__restrict, int);

uint32_t arc4random(void);
uint32_t arc4random_uniform(uint32_t);
void arc4random_buf(void *, size_t)
	__attribute__((__bounded__ (__buffer__,1,2)));

#endif /* __BSD_VISIBLE */

__END_DECLS

#endif /* _STDLIB_H_ */
