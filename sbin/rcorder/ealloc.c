/*	$FreeBSD$	*/
/*	$NetBSD: ealloc.c,v 1.1.1.1 1999/11/19 04:30:56 mrg Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1988, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#include <sys/cdefs.h>
#ifndef lint
__RCSID("$NetBSD: ealloc.c,v 1.1.1.1 1999/11/19 04:30:56 mrg Exp $");
#endif /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include "ealloc.h"

static void enomem(void);

/*
 * enomem --
 *	die when out of memory.
 */
static void
enomem(void)
{
	errx(2, "Cannot allocate memory.");
}

/*
 * emalloc --
 *	malloc, but die on error.
 */
void *
emalloc(size_t len)
{
	void *p;

	if ((p = malloc(len)) == NULL)
		enomem();
	return(p);
}

/*
 * estrdup --
 *	strdup, but die on error.
 */
char *
estrdup(const char *str)
{
	char *p;

	if ((p = strdup(str)) == NULL)
		enomem();
	return(p);
}

/*
 * erealloc --
 *	realloc, but die on error.
 */
void *
erealloc(void *ptr, size_t size)
{
	if ((ptr = realloc(ptr, size)) == NULL)
		enomem();
	return(ptr);
}

/*
 * ecalloc --
 *	calloc, but die on error.
 */
void *
ecalloc(size_t nmemb, size_t size)
{
	void	*ptr;

	if ((ptr = calloc(nmemb, size)) == NULL)
		enomem();
	return(ptr);
}
