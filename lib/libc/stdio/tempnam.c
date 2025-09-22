/*	$OpenBSD: tempnam.c,v 1.20 2017/11/28 06:55:49 tb Exp $ */
/*
 * Copyright (c) 1988, 1993
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
 */

#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

__warn_references(tempnam,
    "tempnam() possibly used unsafely; consider using mkstemp()");

char *
tempnam(const char *dir, const char *pfx)
{
	int sverrno, len;
	char *f, *name;

	if (!(name = malloc(PATH_MAX)))
		return(NULL);

	if (!pfx)
		pfx = "tmp.";

	if (issetugid() == 0 && (f = getenv("TMPDIR")) && *f != '\0') {
		len = snprintf(name, PATH_MAX, "%s%s%sXXXXXXXXXX", f,
		    f[strlen(f) - 1] == '/' ? "" : "/", pfx);
		if (len < 0 || len >= PATH_MAX) {
			errno = ENAMETOOLONG;
			goto fail;
		}
		if ((f = _mktemp(name)))
			return(f);
	}

	if (dir != NULL) {
		f = *dir ? (char *)dir : ".";
		len = snprintf(name, PATH_MAX, "%s%s%sXXXXXXXXXX", f,
		    f[strlen(f) - 1] == '/' ? "" : "/", pfx);
		if (len < 0 || len >= PATH_MAX) {
			errno = ENAMETOOLONG;
			goto fail;
		}
		if ((f = _mktemp(name)))
			return(f);
	}

	f = P_tmpdir;
	len = snprintf(name, PATH_MAX, "%s%sXXXXXXXXX", f, pfx);
	if (len < 0 || len >= PATH_MAX) {
		errno = ENAMETOOLONG;
		goto fail;
	}
	if ((f = _mktemp(name)))
		return(f);

	f = _PATH_TMP;
	len = snprintf(name, PATH_MAX, "%s%sXXXXXXXXX", f, pfx);
	if (len < 0 || len >= PATH_MAX) {
		errno = ENAMETOOLONG;
		goto fail;
	}
	if ((f = _mktemp(name)))
		return(f);

fail:
	sverrno = errno;
	free(name);
	errno = sverrno;
	return(NULL);
}
