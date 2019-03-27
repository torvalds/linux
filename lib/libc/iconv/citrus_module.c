/* $FreeBSD$ */
/* $NetBSD: citrus_module.c,v 1.9 2009/01/11 02:46:24 christos Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)1999, 2000, 2001, 2002 Citrus Project,
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Borman at Krystal Technologies.
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

#include <sys/cdefs.h>
#include <sys/types.h>

#include <assert.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	I18NMODULE_MAJOR	4

#include "citrus_namespace.h"
#include "citrus_bcs.h"
#include "citrus_module.h"
#include "libc_private.h"

static int		 _getdewey(int[], char *);
static int		 _cmpndewey(int[], int, int[], int);
static const char	*_findshlib(char *, int *, int *);

static const char *_pathI18nModule = NULL;

/* from libexec/ld.aout_so/shlib.c */
#undef major
#undef minor
#define MAXDEWEY	3	/*ELF*/

static int
_getdewey(int dewey[], char *cp)
{
	int i, n;

	for (n = 0, i = 0; i < MAXDEWEY; i++) {
		if (*cp == '\0')
			break;

		if (*cp == '.') cp++;
		if (*cp < '0' || '9' < *cp)
			return (0);

		dewey[n++] = (int)_bcs_strtol(cp, &cp, 10);
	}

	return (n);
}

/*
 * Compare two dewey arrays.
 * Return -1 if `d1' represents a smaller value than `d2'.
 * Return  1 if `d1' represents a greater value than `d2'.
 * Return  0 if equal.
 */
static int
_cmpndewey(int d1[], int n1, int d2[], int n2)
{
	int i;

	for (i = 0; i < n1 && i < n2; i++) {
		if (d1[i] < d2[i])
			return (-1);
		if (d1[i] > d2[i])
			return (1);
	}

	if (n1 == n2)
		return (0);

	if (i == n1)
		return (-1);

	if (i == n2)
		return (1);

	/* cannot happen */
	return (0);
}

static const char *
_findshlib(char *name, int *majorp, int *minorp)
{
	char *lname;
	const char *search_dirs[1];
	static char path[PATH_MAX];
	int dewey[MAXDEWEY], tmp[MAXDEWEY];
	int i, len, major, minor, ndewey, n_search_dirs;

	n_search_dirs = 1;
	major = *majorp;
	minor = *minorp;
	path[0] = '\0';
	search_dirs[0] = _pathI18nModule;
	len = strlen(name);
	lname = name;

	ndewey = 0;

	for (i = 0; i < n_search_dirs; i++) {
		struct dirent *dp;
		DIR *dd = opendir(search_dirs[i]);
		int found_dot_a = 0, found_dot_so = 0;

		if (dd == NULL)
			break;

		while ((dp = readdir(dd)) != NULL) {
			int n;

			if (dp->d_namlen < len + 4)
				continue;
			if (strncmp(dp->d_name, lname, (size_t)len) != 0)
				continue;
			if (strncmp(dp->d_name+len, ".so.", 4) != 0)
				continue;

			if ((n = _getdewey(tmp, dp->d_name+len+4)) == 0)
				continue;

			if (major != -1 && found_dot_a)
				found_dot_a = 0;

			/* XXX should verify the library is a.out/ELF? */

			if (major == -1 && minor == -1)
				goto compare_version;
			else if (major != -1 && minor == -1) {
				if (tmp[0] == major)
					goto compare_version;
			} else if (major != -1 && minor != -1) {
				if (tmp[0] == major) {
					if (n == 1 || tmp[1] >= minor)
						goto compare_version;
				}
			}

			/* else, this file does not qualify */
			continue;

		compare_version:
			if (_cmpndewey(tmp, n, dewey, ndewey) <= 0)
				continue;

			/* We have a better version */
			found_dot_so = 1;
			snprintf(path, sizeof(path), "%s/%s", search_dirs[i],
			    dp->d_name);
			found_dot_a = 0;
			bcopy(tmp, dewey, sizeof(dewey));
			ndewey = n;
			*majorp = dewey[0];
			*minorp = dewey[1];
		}
		closedir(dd);

		if (found_dot_a || found_dot_so)
			/*
			 * There's a lib in this dir; take it.
			 */
			return (path[0] ? path : NULL);
	}

	return (path[0] ? path : NULL);
}

void *
_citrus_find_getops(_citrus_module_t handle, const char *modname,
    const char *ifname)
{
	char name[PATH_MAX];
	void *p;

	snprintf(name, sizeof(name), "_citrus_%s_%s_getops",
	    modname, ifname);
	p = dlsym((void *)handle, name);
	return (p);
}

int
_citrus_load_module(_citrus_module_t *rhandle, const char *encname)
{
	const char *p;
	char path[PATH_MAX];
	void *handle;
	int maj, min;

	if (_pathI18nModule == NULL) {
		p = getenv("PATH_I18NMODULE");
		if (p != NULL && !issetugid()) {
			_pathI18nModule = strdup(p);
			if (_pathI18nModule == NULL)
				return (ENOMEM);
		} else
			_pathI18nModule = _PATH_I18NMODULE;
	}

	(void)snprintf(path, sizeof(path), "lib%s", encname);
	maj = I18NMODULE_MAJOR;
	min = -1;
	p = _findshlib(path, &maj, &min);
	if (!p)
		return (EINVAL);
	handle = libc_dlopen(p, RTLD_LAZY);
	if (!handle) {
		printf("%s", dlerror());
		return (EINVAL);
	}

	*rhandle = (_citrus_module_t)handle;

	return (0);
}

void
_citrus_unload_module(_citrus_module_t handle)
{

	if (handle)
		dlclose((void *)handle);
}
