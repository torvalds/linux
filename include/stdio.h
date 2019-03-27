/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)stdio.h	8.5 (Berkeley) 4/29/95
 * $FreeBSD$
 */

#ifndef	_STDIO_H_
#define	_STDIO_H_

#include <sys/cdefs.h>
#include <sys/_null.h>
#include <sys/_types.h>

__NULLABILITY_PRAGMA_PUSH

typedef	__off_t		fpos_t;

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

#ifndef _RSIZE_T_DEFINED
#define _RSIZE_T_DEFINED
typedef size_t rsize_t;
#endif

#if __POSIX_VISIBLE >= 200809
#ifndef _OFF_T_DECLARED
#define	_OFF_T_DECLARED
typedef	__off_t		off_t;
#endif
#ifndef _SSIZE_T_DECLARED
#define	_SSIZE_T_DECLARED
typedef	__ssize_t	ssize_t;
#endif
#endif

#ifndef _OFF64_T_DECLARED
#define	_OFF64_T_DECLARED
typedef	__off64_t	off64_t;
#endif

#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE
#ifndef _VA_LIST_DECLARED
typedef	__va_list	va_list;
#define	_VA_LIST_DECLARED
#endif
#endif

#define	_FSTDIO			/* Define for new stdio with functions. */

/*
 * NB: to fit things in six character monocase externals, the stdio
 * code uses the prefix `__s' for stdio objects, typically followed
 * by a three-character attempt at a mnemonic.
 */

/* stdio buffers */
struct __sbuf {
	unsigned char *_base;
	int	_size;
};

/*
 * stdio state variables.
 *
 * The following always hold:
 *
 *	if (_flags&(__SLBF|__SWR)) == (__SLBF|__SWR),
 *		_lbfsize is -_bf._size, else _lbfsize is 0
 *	if _flags&__SRD, _w is 0
 *	if _flags&__SWR, _r is 0
 *
 * This ensures that the getc and putc macros (or inline functions) never
 * try to write or read from a file that is in `read' or `write' mode.
 * (Moreover, they can, and do, automatically switch from read mode to
 * write mode, and back, on "r+" and "w+" files.)
 *
 * _lbfsize is used only to make the inline line-buffered output stream
 * code as compact as possible.
 *
 * _ub, _up, and _ur are used when ungetc() pushes back more characters
 * than fit in the current _bf, or when ungetc() pushes back a character
 * that does not match the previous one in _bf.  When this happens,
 * _ub._base becomes non-nil (i.e., a stream has ungetc() data iff
 * _ub._base!=NULL) and _up and _ur save the current values of _p and _r.
 *
 * Certain members of __sFILE are accessed directly via macros or
 * inline functions.  To preserve ABI compat, these members must not
 * be disturbed.  These members are marked below with (*).
 */
struct __sFILE {
	unsigned char *_p;	/* (*) current position in (some) buffer */
	int	_r;		/* (*) read space left for getc() */
	int	_w;		/* (*) write space left for putc() */
	short	_flags;		/* (*) flags, below; this FILE is free if 0 */
	short	_file;		/* (*) fileno, if Unix descriptor, else -1 */
	struct	__sbuf _bf;	/* (*) the buffer (at least 1 byte, if !NULL) */
	int	_lbfsize;	/* (*) 0 or -_bf._size, for inline putc */

	/* operations */
	void	*_cookie;	/* (*) cookie passed to io functions */
	int	(* _Nullable _close)(void *);
	int	(* _Nullable _read)(void *, char *, int);
	fpos_t	(* _Nullable _seek)(void *, fpos_t, int);
	int	(* _Nullable _write)(void *, const char *, int);

	/* separate buffer for long sequences of ungetc() */
	struct	__sbuf _ub;	/* ungetc buffer */
	unsigned char	*_up;	/* saved _p when _p is doing ungetc data */
	int	_ur;		/* saved _r when _r is counting ungetc data */

	/* tricks to meet minimum requirements even when malloc() fails */
	unsigned char _ubuf[3];	/* guarantee an ungetc() buffer */
	unsigned char _nbuf[1];	/* guarantee a getc() buffer */

	/* separate buffer for fgetln() when line crosses buffer boundary */
	struct	__sbuf _lb;	/* buffer for fgetln() */

	/* Unix stdio files get aligned to block boundaries on fseek() */
	int	_blksize;	/* stat.st_blksize (may be != _bf._size) */
	fpos_t	_offset;	/* current lseek offset */

	struct pthread_mutex *_fl_mutex;	/* used for MT-safety */
	struct pthread *_fl_owner;	/* current owner */
	int	_fl_count;	/* recursive lock count */
	int	_orientation;	/* orientation for fwide() */
	__mbstate_t _mbstate;	/* multibyte conversion state */
	int	_flags2;	/* additional flags */
};
#ifndef _STDFILE_DECLARED
#define _STDFILE_DECLARED
typedef struct __sFILE FILE;
#endif
#ifndef _STDSTREAM_DECLARED
__BEGIN_DECLS
extern FILE *__stdinp;
extern FILE *__stdoutp;
extern FILE *__stderrp;
__END_DECLS
#define	_STDSTREAM_DECLARED
#endif

#define	__SLBF	0x0001		/* line buffered */
#define	__SNBF	0x0002		/* unbuffered */
#define	__SRD	0x0004		/* OK to read */
#define	__SWR	0x0008		/* OK to write */
	/* RD and WR are never simultaneously asserted */
#define	__SRW	0x0010		/* open for reading & writing */
#define	__SEOF	0x0020		/* found EOF */
#define	__SERR	0x0040		/* found error */
#define	__SMBF	0x0080		/* _bf._base is from malloc */
#define	__SAPP	0x0100		/* fdopen()ed in append mode */
#define	__SSTR	0x0200		/* this is an sprintf/snprintf string */
#define	__SOPT	0x0400		/* do fseek() optimization */
#define	__SNPT	0x0800		/* do not do fseek() optimization */
#define	__SOFF	0x1000		/* set iff _offset is in fact correct */
#define	__SMOD	0x2000		/* true => fgetln modified _p text */
#define	__SALC	0x4000		/* allocate string space dynamically */
#define	__SIGN	0x8000		/* ignore this file in _fwalk */

#define	__S2OAP	0x0001		/* O_APPEND mode is set */

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
 * FOPEN_MAX is a minimum maximum, and is the number of streams that
 * stdio can provide without attempting to allocate further resources
 * (which could fail).  Do not use this for anything.
 */
				/* must be == _POSIX_STREAM_MAX <limits.h> */
#ifndef FOPEN_MAX
#define	FOPEN_MAX	20	/* must be <= OPEN_MAX <sys/syslimits.h> */
#endif
#define	FILENAME_MAX	1024	/* must be <= PATH_MAX <sys/syslimits.h> */

/* System V/ANSI C; this is the wrong way to do this, do *not* use these. */
#if __XSI_VISIBLE
#define	P_tmpdir	"/tmp/"
#endif
#define	L_tmpnam	1024	/* XXX must be == PATH_MAX */
#define	TMP_MAX		308915776

#ifndef SEEK_SET
#define	SEEK_SET	0	/* set file offset to offset */
#endif
#ifndef SEEK_CUR
#define	SEEK_CUR	1	/* set file offset to current plus offset */
#endif
#ifndef SEEK_END
#define	SEEK_END	2	/* set file offset to EOF plus offset */
#endif

#define	stdin	__stdinp
#define	stdout	__stdoutp
#define	stderr	__stderrp

__BEGIN_DECLS
#ifdef _XLOCALE_H_
#include <xlocale/_stdio.h>
#endif
/*
 * Functions defined in ANSI C standard.
 */
void	 clearerr(FILE *);
int	 fclose(FILE *);
int	 feof(FILE *);
int	 ferror(FILE *);
int	 fflush(FILE *);
int	 fgetc(FILE *);
int	 fgetpos(FILE * __restrict, fpos_t * __restrict);
char	*fgets(char * __restrict, int, FILE * __restrict);
FILE	*fopen(const char * __restrict, const char * __restrict);
int	 fprintf(FILE * __restrict, const char * __restrict, ...);
int	 fputc(int, FILE *);
int	 fputs(const char * __restrict, FILE * __restrict);
size_t	 fread(void * __restrict, size_t, size_t, FILE * __restrict);
FILE	*freopen(const char * __restrict, const char * __restrict, FILE * __restrict);
int	 fscanf(FILE * __restrict, const char * __restrict, ...);
int	 fseek(FILE *, long, int);
int	 fsetpos(FILE *, const fpos_t *);
long	 ftell(FILE *);
size_t	 fwrite(const void * __restrict, size_t, size_t, FILE * __restrict);
int	 getc(FILE *);
int	 getchar(void);
char	*gets(char *);
#if __EXT1_VISIBLE
char	*gets_s(char *, rsize_t);
#endif
void	 perror(const char *);
int	 printf(const char * __restrict, ...);
int	 putc(int, FILE *);
int	 putchar(int);
int	 puts(const char *);
int	 remove(const char *);
int	 rename(const char *, const char *);
void	 rewind(FILE *);
int	 scanf(const char * __restrict, ...);
void	 setbuf(FILE * __restrict, char * __restrict);
int	 setvbuf(FILE * __restrict, char * __restrict, int, size_t);
int	 sprintf(char * __restrict, const char * __restrict, ...);
int	 sscanf(const char * __restrict, const char * __restrict, ...);
FILE	*tmpfile(void);
char	*tmpnam(char *);
int	 ungetc(int, FILE *);
int	 vfprintf(FILE * __restrict, const char * __restrict,
	    __va_list);
int	 vprintf(const char * __restrict, __va_list);
int	 vsprintf(char * __restrict, const char * __restrict,
	    __va_list);

#if __ISO_C_VISIBLE >= 1999
int	 snprintf(char * __restrict, size_t, const char * __restrict,
	    ...) __printflike(3, 4);
int	 vfscanf(FILE * __restrict, const char * __restrict, __va_list)
	    __scanflike(2, 0);
int	 vscanf(const char * __restrict, __va_list) __scanflike(1, 0);
int	 vsnprintf(char * __restrict, size_t, const char * __restrict,
	    __va_list) __printflike(3, 0);
int	 vsscanf(const char * __restrict, const char * __restrict, __va_list)
	    __scanflike(2, 0);
#endif

/*
 * Functions defined in all versions of POSIX 1003.1.
 */
#if __BSD_VISIBLE || (__POSIX_VISIBLE && __POSIX_VISIBLE <= 199506)
#define	L_cuserid	17	/* size for cuserid(3); MAXLOGNAME, legacy */
#endif

#if __POSIX_VISIBLE
#define	L_ctermid	1024	/* size for ctermid(3); PATH_MAX */

char	*ctermid(char *);
FILE	*fdopen(int, const char *);
int	 fileno(FILE *);
#endif /* __POSIX_VISIBLE */

#if __POSIX_VISIBLE >= 199209
int	 pclose(FILE *);
FILE	*popen(const char *, const char *);
#endif

#if __POSIX_VISIBLE >= 199506
int	 ftrylockfile(FILE *);
void	 flockfile(FILE *);
void	 funlockfile(FILE *);

/*
 * These are normally used through macros as defined below, but POSIX
 * requires functions as well.
 */
int	 getc_unlocked(FILE *);
int	 getchar_unlocked(void);
int	 putc_unlocked(int, FILE *);
int	 putchar_unlocked(int);
#endif
#if __BSD_VISIBLE
void	 clearerr_unlocked(FILE *);
int	 feof_unlocked(FILE *);
int	 ferror_unlocked(FILE *);
int	 fileno_unlocked(FILE *);
#endif

#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE >= 500
int	 fseeko(FILE *, __off_t, int);
__off_t	 ftello(FILE *);
#endif

#if __BSD_VISIBLE || __XSI_VISIBLE > 0 && __XSI_VISIBLE < 600
int	 getw(FILE *);
int	 putw(int, FILE *);
#endif /* BSD or X/Open before issue 6 */

#if __XSI_VISIBLE
char	*tempnam(const char *, const char *);
#endif

#if __POSIX_VISIBLE >= 200809
FILE	*fmemopen(void * __restrict, size_t, const char * __restrict);
ssize_t	 getdelim(char ** __restrict, size_t * __restrict, int,
	    FILE * __restrict);
FILE	*open_memstream(char **, size_t *);
int	 renameat(int, const char *, int, const char *);
int	 vdprintf(int, const char * __restrict, __va_list) __printflike(2, 0);
/* _WITH_GETLINE to allow pre 11 sources to build on 11+ systems */
ssize_t	 getline(char ** __restrict, size_t * __restrict, FILE * __restrict);
int	 dprintf(int, const char * __restrict, ...) __printflike(2, 3);
#endif /* __POSIX_VISIBLE >= 200809 */

/*
 * Routines that are purely local.
 */
#if __BSD_VISIBLE
int	 asprintf(char **, const char *, ...) __printflike(2, 3);
char	*ctermid_r(char *);
void	 fcloseall(void);
int	 fdclose(FILE *, int *);
char	*fgetln(FILE *, size_t *);
const char *fmtcheck(const char *, const char *) __format_arg(2);
int	 fpurge(FILE *);
void	 setbuffer(FILE *, char *, int);
int	 setlinebuf(FILE *);
int	 vasprintf(char **, const char *, __va_list)
	    __printflike(2, 0);

/*
 * The system error table contains messages for the first sys_nerr
 * positive errno values.  Use strerror() or strerror_r() from <string.h>
 * instead.
 */
extern const int sys_nerr;
extern const char * const sys_errlist[];

/*
 * Stdio function-access interface.
 */
FILE	*funopen(const void *,
	    int (* _Nullable)(void *, char *, int),
	    int (* _Nullable)(void *, const char *, int),
	    fpos_t (* _Nullable)(void *, fpos_t, int),
	    int (* _Nullable)(void *));
#define	fropen(cookie, fn) funopen(cookie, fn, 0, 0, 0)
#define	fwopen(cookie, fn) funopen(cookie, 0, fn, 0, 0)

typedef __ssize_t cookie_read_function_t(void *, char *, size_t);
typedef __ssize_t cookie_write_function_t(void *, const char *, size_t);
typedef int cookie_seek_function_t(void *, off64_t *, int);
typedef int cookie_close_function_t(void *);
typedef struct {
	cookie_read_function_t	*read;
	cookie_write_function_t	*write;
	cookie_seek_function_t	*seek;
	cookie_close_function_t	*close;
} cookie_io_functions_t;
FILE	*fopencookie(void *, const char *, cookie_io_functions_t);

/*
 * Portability hacks.  See <sys/types.h>.
 */
#ifndef _FTRUNCATE_DECLARED
#define	_FTRUNCATE_DECLARED
int	 ftruncate(int, __off_t);
#endif
#ifndef _LSEEK_DECLARED
#define	_LSEEK_DECLARED
__off_t	 lseek(int, __off_t, int);
#endif
#ifndef _MMAP_DECLARED
#define	_MMAP_DECLARED
void	*mmap(void *, size_t, int, int, int, __off_t);
#endif
#ifndef _TRUNCATE_DECLARED
#define	_TRUNCATE_DECLARED
int	 truncate(const char *, __off_t);
#endif
#endif /* __BSD_VISIBLE */

/*
 * Functions internal to the implementation.
 */
int	__srget(FILE *);
int	__swbuf(int, FILE *);

/*
 * The __sfoo macros are here so that we can
 * define function versions in the C library.
 */
#define	__sgetc(p) (--(p)->_r < 0 ? __srget(p) : (int)(*(p)->_p++))
#if defined(__GNUC__) && defined(__STDC__)
static __inline int __sputc(int _c, FILE *_p) {
	if (--_p->_w >= 0 || (_p->_w >= _p->_lbfsize && (char)_c != '\n'))
		return (*_p->_p++ = _c);
	else
		return (__swbuf(_c, _p));
}
#else
/*
 * This has been tuned to generate reasonable code on the vax using pcc.
 */
#define	__sputc(c, p) \
	(--(p)->_w < 0 ? \
		(p)->_w >= (p)->_lbfsize ? \
			(*(p)->_p = (c)), *(p)->_p != '\n' ? \
				(int)*(p)->_p++ : \
				__swbuf('\n', p) : \
			__swbuf((int)(c), p) : \
		(*(p)->_p = (c), (int)*(p)->_p++))
#endif

#ifndef __LIBC_ISTHREADED_DECLARED
#define __LIBC_ISTHREADED_DECLARED
extern int __isthreaded;
#endif

#ifndef __cplusplus

#define	__sfeof(p)	(((p)->_flags & __SEOF) != 0)
#define	__sferror(p)	(((p)->_flags & __SERR) != 0)
#define	__sclearerr(p)	((void)((p)->_flags &= ~(__SERR|__SEOF)))
#define	__sfileno(p)	((p)->_file)


#define	feof(p)		(!__isthreaded ? __sfeof(p) : (feof)(p))
#define	ferror(p)	(!__isthreaded ? __sferror(p) : (ferror)(p))
#define	clearerr(p)	(!__isthreaded ? __sclearerr(p) : (clearerr)(p))

#if __POSIX_VISIBLE
#define	fileno(p)	(!__isthreaded ? __sfileno(p) : (fileno)(p))
#endif

#define	getc(fp)	(!__isthreaded ? __sgetc(fp) : (getc)(fp))
#define	putc(x, fp)	(!__isthreaded ? __sputc(x, fp) : (putc)(x, fp))

#define	getchar()	getc(stdin)
#define	putchar(x)	putc(x, stdout)

#if __BSD_VISIBLE
/*
 * See ISO/IEC 9945-1 ANSI/IEEE Std 1003.1 Second Edition 1996-07-12
 * B.8.2.7 for the rationale behind the *_unlocked() macros.
 */
#define	feof_unlocked(p)	__sfeof(p)
#define	ferror_unlocked(p)	__sferror(p)
#define	clearerr_unlocked(p)	__sclearerr(p)
#define	fileno_unlocked(p)	__sfileno(p)
#endif
#if __POSIX_VISIBLE >= 199506
#define	getc_unlocked(fp)	__sgetc(fp)
#define	putc_unlocked(x, fp)	__sputc(x, fp)

#define	getchar_unlocked()	getc_unlocked(stdin)
#define	putchar_unlocked(x)	putc_unlocked(x, stdout)
#endif
#endif /* __cplusplus */

__END_DECLS
__NULLABILITY_PRAGMA_POP

#endif /* !_STDIO_H_ */
