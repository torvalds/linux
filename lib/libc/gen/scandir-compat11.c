/*
 * Copyright (c) 1983, 1993
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
 * From: @(#)scandir.c	8.3 (Berkeley) 1/2/94
 * From: FreeBSD: head/lib/libc/gen/scandir.c 317372 2017-04-24 14:56:41Z pfg
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Scan the directory dirname calling select to make a list of selected
 * directory entries then sort using qsort and compare routine dcomp.
 * Returns the number of entries and a pointer to a list of pointers to
 * struct dirent (through namelist). Returns -1 if there were any errors.
 */

#include "namespace.h"
#define	_WANT_FREEBSD11_DIRENT
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include "un-namespace.h"

#include "gen-compat.h"

#ifdef	I_AM_SCANDIR_B
#include "block_abi.h"
#define	SELECT(x)	CALL_BLOCK(select, x)
#ifndef __BLOCKS__
void
qsort_b(void *, size_t, size_t, void*);
#endif
#else
#define	SELECT(x)	select(x)
#endif

static int freebsd11_alphasort_thunk(void *thunk, const void *p1,
    const void *p2);

int
#ifdef I_AM_SCANDIR_B
freebsd11_scandir_b(const char *dirname, struct freebsd11_dirent ***namelist,
    DECLARE_BLOCK(int, select, const struct freebsd11_dirent *),
    DECLARE_BLOCK(int, dcomp, const struct freebsd11_dirent **,
    const struct freebsd11_dirent **))
#else
freebsd11_scandir(const char *dirname, struct freebsd11_dirent ***namelist,
    int (*select)(const struct freebsd11_dirent *),
    int (*dcomp)(const struct freebsd11_dirent **,
	const struct freebsd11_dirent **))
#endif
{
	struct freebsd11_dirent *d, *p, **names = NULL;
	size_t arraysz, numitems;
	DIR *dirp;

	if ((dirp = opendir(dirname)) == NULL)
		return(-1);

	numitems = 0;
	arraysz = 32;	/* initial estimate of the array size */
	names = (struct freebsd11_dirent **)malloc(
	    arraysz * sizeof(struct freebsd11_dirent *));
	if (names == NULL)
		goto fail;

	while ((d = freebsd11_readdir(dirp)) != NULL) {
		if (select != NULL && !SELECT(d))
			continue;	/* just selected names */
		/*
		 * Make a minimum size copy of the data
		 */
		p = (struct freebsd11_dirent *)malloc(FREEBSD11_DIRSIZ(d));
		if (p == NULL)
			goto fail;
		p->d_fileno = d->d_fileno;
		p->d_type = d->d_type;
		p->d_reclen = d->d_reclen;
		p->d_namlen = d->d_namlen;
		bcopy(d->d_name, p->d_name, p->d_namlen + 1);
		/*
		 * Check to make sure the array has space left and
		 * realloc the maximum size.
		 */
		if (numitems >= arraysz) {
			struct freebsd11_dirent **names2;

			names2 = reallocarray(names, arraysz,
			    2 * sizeof(struct freebsd11_dirent *));
			if (names2 == NULL) {
				free(p);
				goto fail;
			}
			names = names2;
			arraysz *= 2;
		}
		names[numitems++] = p;
	}
	closedir(dirp);
	if (numitems && dcomp != NULL)
#ifdef I_AM_SCANDIR_B
		qsort_b(names, numitems, sizeof(struct freebsd11_dirent *),
		    (void*)dcomp);
#else
		qsort_r(names, numitems, sizeof(struct freebsd11_dirent *),
		    &dcomp, freebsd11_alphasort_thunk);
#endif
	*namelist = names;
	return (numitems);

fail:
	while (numitems > 0)
		free(names[--numitems]);
	free(names);
	closedir(dirp);
	return (-1);
}

/*
 * Alphabetic order comparison routine for those who want it.
 * POSIX 2008 requires that alphasort() uses strcoll().
 */
int
freebsd11_alphasort(const struct freebsd11_dirent **d1,
    const struct freebsd11_dirent **d2)
{

	return (strcoll((*d1)->d_name, (*d2)->d_name));
}

static int
freebsd11_alphasort_thunk(void *thunk, const void *p1, const void *p2)
{
	int (*dc)(const struct freebsd11_dirent **, const struct
	    freebsd11_dirent **);

	dc = *(int (**)(const struct freebsd11_dirent **,
	    const struct freebsd11_dirent **))thunk;
	return (dc((const struct freebsd11_dirent **)p1,
	    (const struct freebsd11_dirent **)p2));
}

__sym_compat(alphasort, freebsd11_alphasort, FBSD_1.0);
__sym_compat(scandir, freebsd11_scandir, FBSD_1.0);
__sym_compat(scandir_b, freebsd11_scandir_b, FBSD_1.4);
