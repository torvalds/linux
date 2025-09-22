/*	$OpenBSD: util.c,v 1.18 2016/10/16 09:35:40 tb Exp $	*/
/*	$NetBSD: util.c,v 1.5 1996/08/31 20:58:29 mycroft Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratories.
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
 *	from: @(#)util.c	8.1 (Berkeley) 6/6/93
 */

#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

static void vxerror(const char *, int, const char *, va_list)
		__attribute__((__format__ (printf, 3, 0)));

/*
 * Malloc, with abort on error.
 */
void *
emalloc(size_t size)
{
	void *p;

	if ((p = calloc(1, size)) == NULL)
		err(1, NULL);
	return p;
}

/*
 * Reallocarray, with abort on error.
 */
void *
ereallocarray(void *p, size_t sz1, size_t sz2)
{

	if ((p = reallocarray(p, sz1, sz2)) == NULL)
		err(1, NULL);
	return p;
}

/*
 * Calloc, with abort on error.
 */
void *
ecalloc(size_t sz1, size_t sz2)
{
	void *p;

	if ((p = calloc(sz1, sz2)) == NULL)
		err(1, NULL);
	return p;
}

/*
 * Prepend the source path to a file name.
 */
char *
sourcepath(const char *file)
{
	char *cp;

	if (asprintf(&cp, "%s/%s", srcdir, file) == -1)
		err(1, NULL);

	return cp;
}

static struct nvlist *nvhead;

struct nvlist *
newnv(const char *name, const char *str, void *ptr, int i, struct nvlist *next)
{
	struct nvlist *nv;

	if ((nv = nvhead) == NULL)
		nv = emalloc(sizeof(*nv));
	else
		nvhead = nv->nv_next;
	nv->nv_next = next;
	nv->nv_name = (char *)name;
	if (ptr == NULL)
		nv->nv_str = str;
	else {
		if (str != NULL)
			panic("newnv");
		nv->nv_ptr = ptr;
	}
	nv->nv_int = i;
	return nv;
}

/*
 * Free an nvlist structure (just one).
 */
void
nvfree(struct nvlist *nv)
{

	nv->nv_next = nvhead;
	nvhead = nv;
}

/*
 * Free an nvlist (the whole list).
 */
void
nvfreel(struct nvlist *nv)
{
	struct nvlist *next;

	for (; nv != NULL; nv = next) {
		next = nv->nv_next;
		nv->nv_next = nvhead;
		nvhead = nv;
	}
}

/*
 * External (config file) error.  Complain, using current file
 * and line number.
 */
void
error(const char *fmt, ...)
{
	va_list ap;
	extern const char *yyfile;

	va_start(ap, fmt);
	vxerror(yyfile, currentline(), fmt, ap);
	va_end(ap);
}

/*
 * Delayed config file error (i.e., something was wrong but we could not
 * find out about it until later).
 */
void
xerror(const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vxerror(file, line, fmt, ap);
	va_end(ap);
}

/*
 * Internal form of error() and xerror().
 */
static void
vxerror(const char *file, int line, const char *fmt, va_list ap)
{

	(void)fprintf(stderr, "%s:%d: ", file, line);
	(void)vfprintf(stderr, fmt, ap);
	(void)putc('\n', stderr);
	errors++;
}

/*
 * Internal error, abort.
 */
__dead void
panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)fprintf(stderr, "config: panic: ");
	(void)vfprintf(stderr, fmt, ap);
	(void)putc('\n', stderr);
	va_end(ap);
	exit(2);
}
