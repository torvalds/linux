/*	$OpenBSD: findfp.c,v 1.24 2025/08/19 02:34:31 jsg Exp $ */
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

#include <sys/param.h>	/* ALIGN ALIGNBYTES */
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "local.h"
#include "glue.h"
#include "thread_private.h"

int	__sdidinit;

#define	NDYNAMIC 10		/* add ten more whenever necessary */


#define	std(flags, file, cookie) \
	{ ._flags = (flags), ._file = (file), ._cookie = (cookie), \
	  ._close = __sclose, ._read = __sread, ._seek = __sseek, \
	  ._write = __swrite, ._lock = __RCMTX_INITIALIZER() }

				/* the usual - (stdin + stdout + stderr) */
static FILE usual[FOPEN_MAX - 3];
static struct glue uglue = { 0, FOPEN_MAX - 3, usual };
static struct glue *lastglue = &uglue;
static void *sfp_mutex;

/*
 * These are separate variables because they may end up copied
 * into program images via COPY relocations, so their addresses
 * won't be related.  That also means they need separate glue :(
 */
FILE __stdin[1]  = { std(__SRD,         STDIN_FILENO, __stdin) };
FILE __stdout[1] = { std(__SWR,        STDOUT_FILENO, __stdout) };
FILE __stderr[1] = { std(__SWR|__SNBF, STDERR_FILENO, __stderr) };

static struct glue sglue2 = { &uglue, 1, __stderr };
static struct glue sglue1 = { &sglue2, 1, __stdout };
struct glue __sglue = { &sglue1, 1, __stdin };

static struct glue *
moreglue(int n)
{
	struct glue *g;
	char *data;

	data = calloc(1, sizeof(*g) + ALIGNBYTES + n * sizeof(FILE));
	if (data == NULL)
		return (NULL);
	g = (struct glue *)data;
	g->next = NULL;
	g->niobs = n;
	g->iobs = (FILE *)ALIGN(data + sizeof(*g));
	return (g);
}

/*
 * Find a free FILE for fopen et al.
 */
FILE *
__sfp(void)
{
	FILE *fp;
	int n;
	struct glue *g;

	if (!__sdidinit)
		__sinit();

	_MUTEX_LOCK(&sfp_mutex);
	for (g = &__sglue; g != NULL; g = g->next) {
		for (fp = g->iobs, n = g->niobs; --n >= 0; fp++)
			if (fp->_flags == 0)
				goto found;
	}

	/* release lock while mallocing */
	_MUTEX_UNLOCK(&sfp_mutex);
	if ((g = moreglue(NDYNAMIC)) == NULL)
		return (NULL);
	_MUTEX_LOCK(&sfp_mutex);
	lastglue->next = g;
	lastglue = g;
	fp = g->iobs;
found:
	fp->_flags = 1;		/* reserve this slot; caller sets real flags */
	_MUTEX_UNLOCK(&sfp_mutex);

	/* make sure this next memset covers everything but _flags */
	extern char _ctassert[(offsetof(FILE, _flags) == 0) ? 1 : -1 ]
	    __attribute__((__unused__));

	memset((char *)fp + sizeof fp->_flags, 0, sizeof *fp -
	    sizeof fp->_flags);
	fp->_file = -1;		/* no file */
	__rcmtx_init(&fp->_lock);

	return (fp);
}

/*
 * exit() calls _cleanup() through the callback registered
 * with __atexit_register_cleanup(), set whenever we open or buffer a
 * file. This chicanery is done so that programs that do not use stdio
 * need not link it all in.
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
	static void *sinit_mutex;

	_MUTEX_LOCK(&sinit_mutex);
	if (__sdidinit)
		goto out;	/* bail out if caller lost the race */

	/* make sure we clean up on exit */
	__atexit_register_cleanup(_cleanup); /* conservative */
	__sdidinit = 1;
out: 
	_MUTEX_UNLOCK(&sinit_mutex);
}
