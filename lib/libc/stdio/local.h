/*	$OpenBSD: local.h,v 1.27 2025/08/08 15:58:53 yasuoka Exp $	*/

/*-
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
 */

/*
 * Information local to this implementation of stdio,
 * in particular, macros and private variables.
 */

#include <wchar.h> 
#include "thread_private.h"

__BEGIN_HIDDEN_DECLS
void	_cleanup(void);
int	_fwalk(int (*)(FILE *));
int	__sflush(FILE *);
int	__sflush_locked(FILE *);
FILE	*__sfp(void);
int	__srefill(FILE *);
int	__sread(void *, char *, int);
int	__swrite(void *, const char *, int);
fpos_t	__sseek(void *, fpos_t, int);
int	__sclose(void *);
void	__sinit(void);
void	__smakebuf(FILE *);
int	__swhatbuf(FILE *, size_t *, int *);
int	__swsetup(FILE *);
int	__sflags(const char *, int *);
wint_t __fgetwc_unlock(FILE *);
wint_t	__ungetwc(wint_t, FILE *);
int	__vfprintf(FILE *, const char *, __va_list);
int	__svfscanf(FILE * __restrict, const char * __restrict, __va_list);
int	__vfwprintf(FILE * __restrict, const wchar_t * __restrict, __va_list);
int	__vfwscanf(FILE * __restrict, const wchar_t * __restrict, __va_list);

extern int __sdidinit;
__END_HIDDEN_DECLS

/*
 * Return true if the given FILE cannot be written now.
 */
#define	cantwrite(fp) \
	((((fp)->_flags & __SWR) == 0 || (fp)->_bf._base == NULL) && \
	 __swsetup(fp))

/* XXX reduce noise during the conversion */
#define	_UB(fp)	(fp)->_ub

#define	_SET_ORIENTATION(fp, mode)				\
do {								\
	if (((fp)->_flags & (__SONW|__SOWD)) == 0)		\
	       (fp)->_flags |= ((mode) > 0 ? __SOWD : __SONW);	\
} while (0)

/* initializer for function-local FILEs */
#define FILEINIT(flags) \
    { ._flags = (flags), ._file = -1, ._lock = __RCMTX_INITIALIZER() }

/*
 * Test whether the given stdio file has an active ungetc buffer;
 * release such a buffer, without restoring ordinary unread data.
 */
#define	HASUB(fp) (_UB(fp)._base != NULL)
#define	FREEUB(fp) { \
	if (_UB(fp)._base != (fp)->_ubuf) \
		free(_UB(fp)._base); \
	_UB(fp)._base = NULL; \
}

/*
 * test for an fgetln() buffer.
 */
#define	HASLB(fp) ((fp)->_lb._base != NULL)
#define	FREELB(fp) { \
	free((char *)(fp)->_lb._base); \
	(fp)->_lb._base = NULL; \
}

#define FLOCKFILE(fp)							\
	do {								\
		if (__isthreaded)					\
			__rcmtx_enter(&(fp)->_lock);			\
	} while (0)
#define FUNLOCKFILE(fp)							\
	do {								\
		if (__isthreaded)					\
			__rcmtx_leave(&(fp)->_lock);			\
	} while (0)
