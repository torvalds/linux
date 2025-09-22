/*	$OpenBSD: stdio.h,v 1.57 2025/07/16 15:33:05 yasuoka Exp $	*/
/*	$NetBSD: stdio.h,v 1.18 1996/04/25 18:29:21 jtc Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
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
 *	@(#)stdio.h	5.17 (Berkeley) 6/3/91
 */

#ifndef	_STDIO_H_
#define	_STDIO_H_

#include <sys/cdefs.h>
#include <sys/_null.h>
#include <sys/_types.h>

#if __BSD_VISIBLE || __POSIX_VISIBLE || __XPG_VISIBLE
#include <sys/types.h>	/* XXX should be removed */
#endif

#ifndef	_SIZE_T_DEFINED_
#define	_SIZE_T_DEFINED_
typedef	__size_t	size_t;
#endif

#ifndef	_OFF_T_DEFINED_
#define	_OFF_T_DEFINED_
typedef	__off_t	off_t;
#endif

#define	_FSTDIO			/* Define for new stdio with functions. */

typedef off_t fpos_t;		/* stdio file position type */

#ifndef	_STDFILES_DECLARED
#define	_STDFILES_DECLARED
typedef	struct __sFILE FILE;
struct __sFstub { long _stub; };

__BEGIN_DECLS
extern struct __sFstub __stdin[];
extern struct __sFstub __stdout[];
extern struct __sFstub __stderr[];
__END_DECLS
#endif

#define	stdin	((struct __sFILE *)__stdin)
#define	stdout	((struct __sFILE *)__stdout)
#define	stderr	((struct __sFILE *)__stderr)

/*
 * The following three definitions are for ANSI C, which took them
 * from System V, which brilliantly took internal interface macros and
 * made them official arguments to setvbuf(), without renaming them.
 * Hence, these ugly _IOxxx names are *supposed* to appear in user code.
 *
 * Although numbered as their counterparts above, the implementation
 * does not rely on this.
 */
#define	_IOFBF	0		/* setvbuf should set fully buffered */
#define	_IOLBF	1		/* setvbuf should set line buffered */
#define	_IONBF	2		/* setvbuf should set unbuffered */

#define	BUFSIZ	1024		/* size of buffer used by setbuf */

#define	EOF	(-1)

/*
 * FOPEN_MAX is a minimum maximum, and should be the number of descriptors
 * that the kernel can provide without allocation of a resource that can
 * fail without the process sleeping.  Do not use this for anything.
 */
#define	FOPEN_MAX	20	/* must be <= OPEN_MAX <sys/syslimits.h> */
#define	FILENAME_MAX	1024	/* must be <= PATH_MAX <sys/syslimits.h> */

/* System V/ANSI C; this is the wrong way to do this, do *not* use these. */
#if __BSD_VISIBLE || __XPG_VISIBLE
#define	P_tmpdir	"/tmp/"
#endif
#define	L_tmpnam	1024	/* XXX must be == PATH_MAX */
#define	TMP_MAX		0x7fffffff	/* more, but don't overflow int */

#ifndef SEEK_SET
#define	SEEK_SET	0	/* set file offset to offset */
#endif
#ifndef SEEK_CUR
#define	SEEK_CUR	1	/* set file offset to current plus offset */
#endif
#ifndef SEEK_END
#define	SEEK_END	2	/* set file offset to EOF plus offset */
#endif

/*
 * Functions defined in ANSI C standard.
 */
__BEGIN_DECLS
void	 clearerr(FILE *);
#if __POSIX_VISIBLE >= 200809
int	 dprintf(int, const char * __restrict, ...)
		__attribute__((__format__ (printf, 2, 3)))
		__attribute__((__nonnull__ (2)));
#endif
int	 fclose(FILE *);
int	 feof(FILE *);
int	 ferror(FILE *);
int	 fflush(FILE *);
int	 fgetc(FILE *);
int	 fgetpos(FILE *, fpos_t *);
char	*fgets(char *, int, FILE *)
		__attribute__((__bounded__ (__string__,1,2)));
FILE	*fopen(const char *, const char *);
int	 fprintf(FILE *, const char * __restrict, ...);
int	 fputc(int, FILE *);
int	 fputs(const char *, FILE *);
size_t	 fread(void *, size_t, size_t, FILE *)
		__attribute__((__bounded__ (__size__,1,3,2)));
FILE	*freopen(const char *, const char *, FILE *);
int	 fscanf(FILE *, const char *, ...);
int	 fseek(FILE *, long, int);
int	 fseeko(FILE *, off_t, int);
int	 fsetpos(FILE *, const fpos_t *);
long	 ftell(FILE *);
off_t	 ftello(FILE *);
size_t	 fwrite(const void *, size_t, size_t, FILE *)
		__attribute__((__bounded__ (__size__,1,3,2)));
int	 getc(FILE *);
int	 getchar(void);
#if __POSIX_VISIBLE >= 200809
ssize_t	 getdelim(char ** __restrict, size_t * __restrict, int,
	    FILE * __restrict);
ssize_t	 getline(char ** __restrict, size_t * __restrict,
	    FILE * __restrict);
#endif
#if __BSD_VISIBLE && !defined(__SYS_ERRLIST)
#define __SYS_ERRLIST

extern int sys_nerr;			/* perror(3) external variables */
extern char *sys_errlist[];
#endif
void	 perror(const char *);
int	 printf(const char * __restrict, ...);
int	 putc(int, FILE *);
int	 putchar(int);
int	 puts(const char *);
int	 remove(const char *);
int	 rename(const char *, const char *);
#if __POSIX_VISIBLE >= 200809
int	 renameat(int, const char *, int, const char *);
#endif
void	 rewind(FILE *);
int	 scanf(const char *, ...);
void	 setbuf(FILE *, char *);
int	 setvbuf(FILE *, char *, int, size_t);
int	 sprintf(char * __restrict, const char * __restrict, ...);
int	 sscanf(const char *, const char *, ...);
FILE	*tmpfile(void);
char	*tmpnam(char *);
int	 ungetc(int, FILE *);
int	 vfprintf(FILE *, const char * __restrict, __va_list);
int	 vprintf(const char * __restrict, __va_list);
int	 vsprintf(char * __restrict, const char * __restrict, __va_list);
#if __POSIX_VISIBLE >= 200809
int	 vdprintf(int, const char * __restrict, __va_list)
		__attribute__((__format__ (printf, 2, 0)))
		__attribute__((__nonnull__ (2)));
#endif

#if __ISO_C_VISIBLE >= 1999 || __XPG_VISIBLE >= 500 || __BSD_VISIBLE
int	 snprintf(char * __restrict, size_t, const char * __restrict, ...)
		__attribute__((__format__ (printf, 3, 4)))
		__attribute__((__nonnull__ (3)))
		__attribute__((__bounded__ (__string__,1,2)));
int	 vsnprintf(char * __restrict, size_t, const char * __restrict,
	     __va_list)
		__attribute__((__format__ (printf, 3, 0)))
		__attribute__((__nonnull__ (3)))
		__attribute__((__bounded__(__string__,1,2)));
#endif /* __ISO_C_VISIBLE >= 1999 || __XPG_VISIBLE >= 500 || __BSD_VISIBLE */

#if __ISO_C_VISIBLE >= 1999 || __BSD_VISIBLE
int	 vfscanf(FILE *, const char *, __va_list)
		__attribute__((__format__ (scanf, 2, 0)))
		__attribute__((__nonnull__ (2)));
int	 vscanf(const char *, __va_list)
		__attribute__((__format__ (scanf, 1, 0)))
		__attribute__((__nonnull__ (1)));
int	 vsscanf(const char *, const char *, __va_list)
		__attribute__((__format__ (scanf, 2, 0)))
		__attribute__((__nonnull__ (2)));
#endif /* __ISO_C_VISIBLE >= 1999 || __BSD_VISIBLE */

__END_DECLS


/*
 * Functions defined in POSIX 1003.1.
 */
#if __BSD_VISIBLE || __POSIX_VISIBLE || __XPG_VISIBLE
#define	L_ctermid	1024	/* size for ctermid(); PATH_MAX */

__BEGIN_DECLS
char	*ctermid(char *);
FILE	*fdopen(int, const char *);
int	 fileno(FILE *);

#if __POSIX_VISIBLE >= 199209
int	 pclose(FILE *);
FILE	*popen(const char *, const char *);
#endif

#if __POSIX_VISIBLE >= 199506
void	 flockfile(FILE *);
int	 ftrylockfile(FILE *);
void	 funlockfile(FILE *);

/*
 * These are normally used through macros as defined below, but POSIX
 * requires functions as well.
 */
int	 getc_unlocked(FILE *);
int	 getchar_unlocked(void);
int	 putc_unlocked(int, FILE *);
int	 putchar_unlocked(int);
#endif /* __POSIX_VISIBLE >= 199506 */

#if __POSIX_VISIBLE >= 200809
FILE	*fmemopen(void *, size_t, const char *);
FILE	*open_memstream(char **, size_t *);
#endif /* __POSIX_VISIBLE >= 200809 */

#if __XPG_VISIBLE
char	*tempnam(const char *, const char *);
#endif

#if __POSIX_VISIBLE >= 202405 || __BSD_VISIBLE
int	 asprintf(char ** __restrict, const char * __restrict, ...)
		__attribute__((__format__ (printf, 2, 3)))
		__attribute__((__nonnull__ (2)));
int	 vasprintf(char ** __restrict, const char * __restrict, __va_list)
		__attribute__((__format__ (printf, 2, 0)))
		__attribute__((__nonnull__ (2)));
#endif /* __POSIX_VISIBLE >= 202405 || __BSD_VISIBLE */
__END_DECLS

#endif /* __BSD_VISIBLE || __POSIX_VISIBLE || __XPG_VISIBLE */

/*
 * Routines that are purely local.
 */
#if __BSD_VISIBLE
__BEGIN_DECLS
int	 fdclose(FILE *, int *_fdp);
char	*fgetln(FILE *, size_t *);
int	 fpurge(FILE *);
int	 getw(FILE *);
int	 putw(int, FILE *);
void	 setbuffer(FILE *, char *, int);
int	 setlinebuf(FILE *);
__END_DECLS

/*
 * Stdio function-access interface.
 */
__BEGIN_DECLS
FILE	*funopen(const void *,
		int (*)(void *, char *, int),
		int (*)(void *, const char *, int),
		off_t (*)(void *, off_t, int),
		int (*)(void *));
__END_DECLS
#define	fropen(cookie, fn) funopen(cookie, fn, 0, 0, 0)
#define	fwopen(cookie, fn) funopen(cookie, 0, fn, 0, 0)
#endif /* __BSD_VISIBLE */

#define	getchar()	getc(stdin)
#define	putchar(x)	putc(x, stdout)
#define getchar_unlocked()	getc_unlocked(stdin)
#define putchar_unlocked(c)	putc_unlocked(c, stdout)

#endif /* _STDIO_H_ */
