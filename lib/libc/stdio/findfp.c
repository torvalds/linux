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
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)findfp.c	8.2 (Berkeley) 1/4/94";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <machine/atomic.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <spinlock.h>

#include "libc_private.h"
#include "local.h"
#include "glue.h"

int	__sdidinit;

#define	NDYNAMIC 10		/* add ten more whenever necessary */

#define	std(flags, file) {		\
	._flags = (flags),		\
	._file = (file),		\
	._cookie = __sF + (file),	\
	._close = __sclose,		\
	._read = __sread,		\
	._seek = __sseek,		\
	._write = __swrite,		\
	._fl_mutex = PTHREAD_MUTEX_INITIALIZER, \
}
				/* the usual - (stdin + stdout + stderr) */
static FILE usual[FOPEN_MAX - 3];
static struct glue uglue = { NULL, FOPEN_MAX - 3, usual };

static FILE __sF[3] = {
	std(__SRD, STDIN_FILENO),
	std(__SWR, STDOUT_FILENO),
	std(__SWR|__SNBF, STDERR_FILENO)
};

FILE *__stdinp = &__sF[0];
FILE *__stdoutp = &__sF[1];
FILE *__stderrp = &__sF[2];

struct glue __sglue = { &uglue, 3, __sF };
static struct glue *lastglue = &uglue;

static struct glue *	moreglue(int);

spinlock_t __stdio_thread_lock = _SPINLOCK_INITIALIZER;

#if NOT_YET
#define	SET_GLUE_PTR(ptr, val)	atomic_set_rel_ptr(&(ptr), (uintptr_t)(val))
#else
#define	SET_GLUE_PTR(ptr, val)	ptr = val
#endif

static struct glue *
moreglue(int n)
{
	struct glue *g;
	static FILE empty = { ._fl_mutex = PTHREAD_MUTEX_INITIALIZER };
	FILE *p;
	size_t align;

	align = __alignof__(FILE);
	g = (struct glue *)malloc(sizeof(*g) + align + n * sizeof(FILE));
	if (g == NULL)
		return (NULL);
	p = (FILE *)roundup((uintptr_t)(g + 1), align);
	g->next = NULL;
	g->niobs = n;
	g->iobs = p;
	while (--n >= 0)
		*p++ = empty;
	return (g);
}

/*
 * Find a free FILE for fopen et al.
 */
FILE *
__sfp(void)
{
	FILE	*fp;
	int	n;
	struct glue *g;

	if (!__sdidinit)
		__sinit();
	/*
	 * The list must be locked because a FILE may be updated.
	 */
	STDIO_THREAD_LOCK();
	for (g = &__sglue; g != NULL; g = g->next) {
		for (fp = g->iobs, n = g->niobs; --n >= 0; fp++)
			if (fp->_flags == 0)
				goto found;
	}
	STDIO_THREAD_UNLOCK();	/* don't hold lock while malloc()ing. */
	if ((g = moreglue(NDYNAMIC)) == NULL)
		return (NULL);
	STDIO_THREAD_LOCK();	/* reacquire the lock */
	SET_GLUE_PTR(lastglue->next, g); /* atomically append glue to list */
	lastglue = g;		/* not atomic; only accessed when locked */
	fp = g->iobs;
found:
	fp->_flags = 1;		/* reserve this slot; caller sets real flags */
	STDIO_THREAD_UNLOCK();
	fp->_p = NULL;		/* no current pointer */
	fp->_w = 0;		/* nothing to read or write */
	fp->_r = 0;
	fp->_bf._base = NULL;	/* no buffer */
	fp->_bf._size = 0;
	fp->_lbfsize = 0;	/* not line buffered */
	fp->_file = -1;		/* no file */
/*	fp->_cookie = <any>; */	/* caller sets cookie, _read/_write etc */
	fp->_ub._base = NULL;	/* no ungetc buffer */
	fp->_ub._size = 0;
	fp->_lb._base = NULL;	/* no line buffer */
	fp->_lb._size = 0;
/*	fp->_fl_mutex = NULL; */ /* once set always set (reused) */
	fp->_orientation = 0;
	memset(&fp->_mbstate, 0, sizeof(mbstate_t));
	fp->_flags2 = 0;
	return (fp);
}

/*
 * XXX.  Force immediate allocation of internal memory.  Not used by stdio,
 * but documented historically for certain applications.  Bad applications.
 */
__warn_references(f_prealloc, 
	"warning: this program uses f_prealloc(), which is not recommended.");
void f_prealloc(void);

void
f_prealloc(void)
{
	struct glue *g;
	int n;

	n = getdtablesize() - FOPEN_MAX + 20;		/* 20 for slop. */
	/*
	 * It should be safe to walk the list without locking it;
	 * new nodes are only added to the end and none are ever
	 * removed.
	 */
	for (g = &__sglue; (n -= g->niobs) > 0 && g->next; g = g->next)
		/* void */;
	if ((n > 0) && ((g = moreglue(n)) != NULL)) {
		STDIO_THREAD_LOCK();
		SET_GLUE_PTR(lastglue->next, g);
		lastglue = g;
		STDIO_THREAD_UNLOCK();
	}
}

/*
 * exit() calls _cleanup() through *__cleanup, set whenever we
 * open or buffer a file.  This chicanery is done so that programs
 * that do not use stdio need not link it all in.
 *
 * The name `_cleanup' is, alas, fairly well known outside stdio.
 */
void
_cleanup(void)
{
	/* (void) _fwalk(fclose); */
	(void) _fwalk(__sflush);		/* `cheating' */
}

/*
 * __sinit() is called whenever stdio's internal variables must be set up.
 */
void
__sinit(void)
{

	/* Make sure we clean up on exit. */
	__cleanup = _cleanup;		/* conservative */
	__sdidinit = 1;
}
