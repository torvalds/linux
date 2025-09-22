/*	$OpenBSD: stdio.h,v 1.11 2025/08/08 15:58:53 yasuoka Exp $	*/
/*
 * Copyright (c) 2015 Philip Guenther <guenther@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_LIBC_STDIO_H_
#define	_LIBC_STDIO_H_

/* we want the const-correct declarations inside libc */
#define __SYS_ERRLIST

#ifndef _STDFILES_DECLARED
#define _STDFILES_DECLARED
typedef struct __sFILE FILE;
#endif

#include_next <stdio.h>
#include <_stdio.h>		/* struct __sFILE, std{in,out,err} */

__BEGIN_HIDDEN_DECLS
int	__cleanfile(FILE *, int _doclose);
void	__relefile(FILE *);
int	__srget(FILE *);
int	__swbuf(int, FILE *);
char	*_mktemp(char *);
__END_HIDDEN_DECLS

extern const int sys_nerr;
extern const char *const sys_errlist[];

#if 0
extern PROTO_NORMAL(sys_nerr);
extern PROTO_NORMAL(sys_errlist);
#endif

PROTO_NORMAL(asprintf);
PROTO_NORMAL(clearerr);
PROTO_NORMAL(ctermid);
PROTO_NORMAL(dprintf);
PROTO_NORMAL(fclose);
PROTO_DEPRECATED(fdclose);
PROTO_NORMAL(fdopen);
PROTO_NORMAL(feof);
PROTO_NORMAL(ferror);
PROTO_NORMAL(fflush);
PROTO_NORMAL(fgetc);
PROTO_NORMAL(fgetln);
PROTO_NORMAL(fgetpos);
PROTO_NORMAL(fgets);
PROTO_NORMAL(fileno);
PROTO_NORMAL(flockfile);
PROTO_NORMAL(fmemopen);
PROTO_NORMAL(fopen);
PROTO_NORMAL(fprintf);
PROTO_NORMAL(fpurge);
PROTO_NORMAL(fputc);
PROTO_NORMAL(fputs);
PROTO_NORMAL(fread);
PROTO_NORMAL(freopen);
PROTO_NORMAL(fscanf);
PROTO_STD_DEPRECATED(fseek);
PROTO_NORMAL(fseeko);
PROTO_NORMAL(fsetpos);
PROTO_NORMAL(ftell);
PROTO_NORMAL(ftello);
PROTO_NORMAL(ftrylockfile);
PROTO_NORMAL(funlockfile);
PROTO_NORMAL(funopen);
PROTO_NORMAL(fwrite);
PROTO_NORMAL(getc);
PROTO_NORMAL(getc_unlocked);
PROTO_NORMAL(getchar);
PROTO_NORMAL(getchar_unlocked);
PROTO_NORMAL(getdelim);
PROTO_NORMAL(getline);
PROTO_NORMAL(getw);
PROTO_NORMAL(open_memstream);
PROTO_NORMAL(pclose);
PROTO_NORMAL(perror);
PROTO_NORMAL(popen);
PROTO_NORMAL(printf);
PROTO_NORMAL(putc);
PROTO_NORMAL(putc_unlocked);
PROTO_NORMAL(putchar);
PROTO_NORMAL(putchar_unlocked);
PROTO_NORMAL(puts);
PROTO_NORMAL(putw);
PROTO_NORMAL(remove);
PROTO_NORMAL(rename);
PROTO_NORMAL(renameat);
PROTO_NORMAL(rewind);
PROTO_NORMAL(scanf);
PROTO_NORMAL(setbuf);
PROTO_NORMAL(setbuffer);
PROTO_NORMAL(setlinebuf);
PROTO_NORMAL(setvbuf);
PROTO_NORMAL(snprintf);
PROTO_STD_DEPRECATED(sprintf);
PROTO_NORMAL(sscanf);
PROTO_DEPRECATED(tempnam);
PROTO_NORMAL(tmpfile);
PROTO_STD_DEPRECATED(tmpnam);
PROTO_NORMAL(ungetc);
PROTO_NORMAL(vasprintf);
PROTO_NORMAL(vdprintf);
PROTO_NORMAL(vfprintf);
PROTO_NORMAL(vfscanf);
PROTO_NORMAL(vprintf);
PROTO_NORMAL(vscanf);
PROTO_NORMAL(vsnprintf);
PROTO_STD_DEPRECATED(vsprintf);
PROTO_NORMAL(vsscanf);

/*
 * The __sfoo macros are here so that we can 
 * define function versions in the C library.
 */
#define	__sgetc(p) (--(p)->_r < 0 ? __srget(p) : (int)(*(p)->_p++))
static __inline int __sputc(int _c, FILE *_p) {
	if (--_p->_w >= 0 || (_p->_w >= _p->_lbfsize && (char)_c != '\n'))
		return (*_p->_p++ = _c);
	else
		return (__swbuf(_c, _p));
}

#define	__sfeof(p)	(((p)->_flags & __SEOF) != 0)
#define	__sferror(p)	(((p)->_flags & __SERR) != 0)
#define	__sclearerr(p)	((void)((p)->_flags &= ~(__SERR|__SEOF)))
#define	__sfileno(p)	((p)->_file)

extern int __isthreaded;

#define feof(p)		(!__isthreaded ? __sfeof(p) : (feof)(p))
#define ferror(p)	(!__isthreaded ? __sferror(p) : (ferror)(p))
#define clearerr(p)	(!__isthreaded ? __sclearerr(p) : (clearerr)(p))
#define fileno(p)	(!__isthreaded ? __sfileno(p) : (fileno)(p))
#define getc(fp)	(!__isthreaded ? __sgetc(fp) : (getc)(fp))

/*
 * The macro implementations of putc and putc_unlocked are not
 * fully POSIX compliant; they do not set errno on failure
 */
#define putc(x, fp)	(!__isthreaded ? __sputc(x, fp) : (putc)(x, fp))
#define getc_unlocked(fp)	__sgetc(fp)
#define putc_unlocked(x, fp)	__sputc(x, fp)

#endif /* _LIBC_STDIO_H_ */
