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
 *	@(#)stdlib.h	8.5 (Berkeley) 5/19/95
 * $FreeBSD$
 */

#ifndef _STDLIB_H_
#define	_STDLIB_H_

#include <sys/cdefs.h>
#include <sys/_null.h>
#include <sys/_types.h>

__NULLABILITY_PRAGMA_PUSH

#if __BSD_VISIBLE
#ifndef _RUNE_T_DECLARED
typedef	__rune_t	rune_t;
#define	_RUNE_T_DECLARED
#endif
#endif

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

#ifndef	__cplusplus
#ifndef _WCHAR_T_DECLARED
typedef	___wchar_t	wchar_t;
#define	_WCHAR_T_DECLARED
#endif
#endif

typedef struct {
	int	quot;		/* quotient */
	int	rem;		/* remainder */
} div_t;

typedef struct {
	long	quot;
	long	rem;
} ldiv_t;

#define	EXIT_FAILURE	1
#define	EXIT_SUCCESS	0

#define	RAND_MAX	0x7ffffffd

__BEGIN_DECLS
#ifdef _XLOCALE_H_
#include <xlocale/_stdlib.h>
#endif
extern int __mb_cur_max;
extern int ___mb_cur_max(void);
#define	MB_CUR_MAX	((size_t)___mb_cur_max())

_Noreturn void	 abort(void);
int	 abs(int) __pure2;
int	 atexit(void (* _Nonnull)(void));
double	 atof(const char *);
int	 atoi(const char *);
long	 atol(const char *);
void	*bsearch(const void *, const void *, size_t,
	    size_t, int (*)(const void * _Nonnull, const void *));
void	*calloc(size_t, size_t) __malloc_like __result_use_check
	     __alloc_size2(1, 2);
div_t	 div(int, int) __pure2;
_Noreturn void	 exit(int);
void	 free(void *);
char	*getenv(const char *);
long	 labs(long) __pure2;
ldiv_t	 ldiv(long, long) __pure2;
void	*malloc(size_t) __malloc_like __result_use_check __alloc_size(1);
int	 mblen(const char *, size_t);
size_t	 mbstowcs(wchar_t * __restrict , const char * __restrict, size_t);
int	 mbtowc(wchar_t * __restrict, const char * __restrict, size_t);
void	 qsort(void *, size_t, size_t,
	    int (* _Nonnull)(const void *, const void *));
int	 rand(void);
void	*realloc(void *, size_t) __result_use_check __alloc_size(2);
void	 srand(unsigned);
double	 strtod(const char * __restrict, char ** __restrict);
float	 strtof(const char * __restrict, char ** __restrict);
long	 strtol(const char * __restrict, char ** __restrict, int);
long double
	 strtold(const char * __restrict, char ** __restrict);
unsigned long
	 strtoul(const char * __restrict, char ** __restrict, int);
int	 system(const char *);
int	 wctomb(char *, wchar_t);
size_t	 wcstombs(char * __restrict, const wchar_t * __restrict, size_t);

/*
 * Functions added in C99 which we make conditionally available in the
 * BSD^C89 namespace if the compiler supports `long long'.
 * The #if test is more complicated than it ought to be because
 * __BSD_VISIBLE implies __ISO_C_VISIBLE == 1999 *even if* `long long'
 * is not supported in the compilation environment (which therefore means
 * that it can't really be ISO C99).
 *
 * (The only other extension made by C99 in thie header is _Exit().)
 */
#if __ISO_C_VISIBLE >= 1999 || defined(__cplusplus)
#ifdef __LONG_LONG_SUPPORTED
/* LONGLONG */
typedef struct {
	long long quot;
	long long rem;
} lldiv_t;

/* LONGLONG */
long long
	 atoll(const char *);
/* LONGLONG */
long long
	 llabs(long long) __pure2;
/* LONGLONG */
lldiv_t	 lldiv(long long, long long) __pure2;
/* LONGLONG */
long long
	 strtoll(const char * __restrict, char ** __restrict, int);
/* LONGLONG */
unsigned long long
	 strtoull(const char * __restrict, char ** __restrict, int);
#endif /* __LONG_LONG_SUPPORTED */

_Noreturn void	 _Exit(int);
#endif /* __ISO_C_VISIBLE >= 1999 */

/*
 * If we're in a mode greater than C99, expose C11 functions.
 */
#if __ISO_C_VISIBLE >= 2011 || __cplusplus >= 201103L
void *	aligned_alloc(size_t, size_t) __malloc_like __alloc_align(1)
	    __alloc_size(2);
int	at_quick_exit(void (*)(void));
_Noreturn void
	quick_exit(int);
#endif /* __ISO_C_VISIBLE >= 2011 */
/*
 * Extensions made by POSIX relative to C.
 */
#if __POSIX_VISIBLE >= 199506 || __XSI_VISIBLE
char	*realpath(const char * __restrict, char * __restrict);
#endif
#if __POSIX_VISIBLE >= 199506
int	 rand_r(unsigned *);			/* (TSF) */
#endif
#if __POSIX_VISIBLE >= 200112
int	 posix_memalign(void **, size_t, size_t); /* (ADV) */
int	 setenv(const char *, const char *, int);
int	 unsetenv(const char *);
#endif

#if __POSIX_VISIBLE >= 200809 || __XSI_VISIBLE
int	 getsubopt(char **, char *const *, char **);
#ifndef _MKDTEMP_DECLARED
char	*mkdtemp(char *);
#define	_MKDTEMP_DECLARED
#endif
#ifndef _MKSTEMP_DECLARED
int	 mkstemp(char *);
#define	_MKSTEMP_DECLARED
#endif
#endif /* __POSIX_VISIBLE >= 200809 || __XSI_VISIBLE */

/*
 * The only changes to the XSI namespace in revision 6 were the deletion
 * of the ttyslot() and valloc() functions, which FreeBSD never declared
 * in this header.  For revision 7, ecvt(), fcvt(), and gcvt(), which
 * FreeBSD also does not have, and mktemp(), are to be deleted.
 */
#if __XSI_VISIBLE
/* XXX XSI requires pollution from <sys/wait.h> here.  We'd rather not. */
long	 a64l(const char *);
double	 drand48(void);
/* char	*ecvt(double, int, int * __restrict, int * __restrict); */
double	 erand48(unsigned short[3]);
/* char	*fcvt(double, int, int * __restrict, int * __restrict); */
/* char	*gcvt(double, int, int * __restrict, int * __restrict); */
int	 grantpt(int);
char	*initstate(unsigned int, char *, size_t);
long	 jrand48(unsigned short[3]);
char	*l64a(long);
void	 lcong48(unsigned short[7]);
long	 lrand48(void);
#if !defined(_MKTEMP_DECLARED) && (__BSD_VISIBLE || __XSI_VISIBLE <= 600)
char	*mktemp(char *);
#define	_MKTEMP_DECLARED
#endif
long	 mrand48(void);
long	 nrand48(unsigned short[3]);
int	 posix_openpt(int);
char	*ptsname(int);
int	 putenv(char *);
long	 random(void);
unsigned short
	*seed48(unsigned short[3]);
char	*setstate(/* const */ char *);
void	 srand48(long);
void	 srandom(unsigned int);
int	 unlockpt(int);
#endif /* __XSI_VISIBLE */

#if __BSD_VISIBLE
extern const char *malloc_conf;
extern void (*malloc_message)(void *, const char *);

/*
 * The alloca() function can't be implemented in C, and on some
 * platforms it can't be implemented at all as a callable function.
 * The GNU C compiler provides a built-in alloca() which we can use.
 * On platforms where alloca() is not in libc, programs which use it
 * will fail to link when compiled with non-GNU compilers.
 */
#if __GNUC__ >= 2 || defined(__INTEL_COMPILER)
#undef  alloca	/* some GNU bits try to get cute and define this on their own */
#define alloca(sz) __builtin_alloca(sz)
#endif

void	 abort2(const char *, int, void **) __dead2;
__uint32_t
	 arc4random(void);
void	 arc4random_buf(void *, size_t);
__uint32_t 
	 arc4random_uniform(__uint32_t);

#ifdef __BLOCKS__
int	 atexit_b(void (^ _Nonnull)(void));
void	*bsearch_b(const void *, const void *, size_t,
	    size_t, int (^ _Nonnull)(const void *, const void *));
#endif
char	*getbsize(int *, long *);
					/* getcap(3) functions */
char	*cgetcap(char *, const char *, int);
int	 cgetclose(void);
int	 cgetent(char **, char **, const char *);
int	 cgetfirst(char **, char **);
int	 cgetmatch(const char *, const char *);
int	 cgetnext(char **, char **);
int	 cgetnum(char *, const char *, long *);
int	 cgetset(const char *);
int	 cgetstr(char *, const char *, char **);
int	 cgetustr(char *, const char *, char **);

int	 daemon(int, int);
int	 daemonfd(int, int);
char	*devname(__dev_t, __mode_t);
char	*devname_r(__dev_t, __mode_t, char *, int);
char	*fdevname(int);
char	*fdevname_r(int, char *, int);
int	 getloadavg(double [], int);
const char *
	 getprogname(void);

int	 heapsort(void *, size_t, size_t,
	    int (* _Nonnull)(const void *, const void *));
#ifdef __BLOCKS__
int	 heapsort_b(void *, size_t, size_t,
	    int (^ _Nonnull)(const void *, const void *));
void	 qsort_b(void *, size_t, size_t,
	    int (^ _Nonnull)(const void *, const void *));
#endif
int	 l64a_r(long, char *, int);
int	 mergesort(void *, size_t, size_t, int (*)(const void *, const void *));
#ifdef __BLOCKS__
int	 mergesort_b(void *, size_t, size_t, int (^)(const void *, const void *));
#endif
int	 mkostemp(char *, int);
int	 mkostemps(char *, int, int);
void	 qsort_r(void *, size_t, size_t, void *,
	    int (*)(void *, const void *, const void *));
int	 radixsort(const unsigned char **, int, const unsigned char *,
	    unsigned);
void	*reallocarray(void *, size_t, size_t) __result_use_check
	    __alloc_size2(2, 3);
void	*reallocf(void *, size_t) __result_use_check __alloc_size(2);
int	 rpmatch(const char *);
void	 setprogname(const char *);
int	 sradixsort(const unsigned char **, int, const unsigned char *,
	    unsigned);
void	 sranddev(void);
void	 srandomdev(void);
long long
	strtonum(const char *, long long, long long, const char **);

/* Deprecated interfaces, to be removed. */
__int64_t
	 strtoq(const char *, char **, int);
__uint64_t
	 strtouq(const char *, char **, int);

extern char *suboptarg;			/* getsubopt(3) external variable */
#endif /* __BSD_VISIBLE */

#if __EXT1_VISIBLE

#ifndef _ERRNO_T_DEFINED
#define _ERRNO_T_DEFINED
typedef int errno_t;
#endif

/* K.3.6 */
typedef void (*constraint_handler_t)(const char * __restrict,
    void * __restrict, errno_t);
/* K.3.6.1.1 */
constraint_handler_t set_constraint_handler_s(constraint_handler_t handler);
/* K.3.6.1.2 */
_Noreturn void abort_handler_s(const char * __restrict, void * __restrict,
    errno_t);
/* K3.6.1.3 */
void ignore_handler_s(const char * __restrict, void * __restrict, errno_t);
#endif /* __EXT1_VISIBLE */

__END_DECLS
__NULLABILITY_PRAGMA_POP

#endif /* !_STDLIB_H_ */
