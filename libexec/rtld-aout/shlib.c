/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1993 Paul Kranenburg
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Paul Kranenburg.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <a.out.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/link_aout.h>
#include "shlib.h"
#include "support.h"

/*
 * Standard directories to search for files specified by -l.
 */
#ifndef STANDARD_SEARCH_DIRS
#define STANDARD_SEARCH_DIRS    "/usr/lib/aout"
#endif

/*
 * Actual vector of library search directories,
 * including `-L'ed and LD_LIBRARY_PATH spec'd ones.
 */
char	 **search_dirs;
int	n_search_dirs;

static const char *standard_search_dirs[] = {
	STANDARD_SEARCH_DIRS
};


void
add_search_dir(const char *name)
{
	int n;

	for (n = 0; n < n_search_dirs; n++)
		if (strcmp(search_dirs[n], name) == 0)
			return;
	n_search_dirs++;
	search_dirs = (char **)
		xrealloc(search_dirs, n_search_dirs * sizeof search_dirs[0]);
	search_dirs[n_search_dirs - 1] = strdup(name);
}

void
add_search_path(char *path)
{
	register char	*cp, *dup;

	if (path == NULL)
		return;

	/* Add search directories from `path' */
	path = dup = strdup(path);
	while ((cp = strsep(&path, ":")) != NULL)
		add_search_dir(cp);
	free(dup);
}

void
std_search_path(void)
{
	int	i, n;

	/* Append standard search directories */
	n = sizeof standard_search_dirs / sizeof standard_search_dirs[0];
	for (i = 0; i < n; i++)
		add_search_dir(standard_search_dirs[i]);
}

/*
 * Return true if CP points to a valid dewey number.
 * Decode and leave the result in the array DEWEY.
 * Return the number of decoded entries in DEWEY.
 */

int
getdewey(int dewey[], char *cp)
{
	int	i, n;

	for (n = 0, i = 0; i < MAXDEWEY; i++) {
		if (*cp == '\0')
			break;

		if (*cp == '.') cp++;
		if (!isdigit(*cp))
			return 0;

		dewey[n++] = strtol(cp, &cp, 10);
	}

	return n;
}

/*
 * Compare two dewey arrays.
 * Return -1 if `d1' represents a smaller value than `d2'.
 * Return  1 if `d1' represents a greater value than `d2'.
 * Return  0 if equal.
 */
int
cmpndewey(int d1[], int n1, int d2[], int n2)
{
	register int	i;

	for (i = 0; i < n1 && i < n2; i++) {
		if (d1[i] < d2[i])
			return -1;
		if (d1[i] > d2[i])
			return 1;
	}

	if (n1 == n2)
		return 0;

	if (i == n1)
		return -1;

	if (i == n2)
		return 1;

	errx(1, "cmpndewey: can't happen");
	return 0;
}

/*
 * Search directories for a shared library matching the given
 * major and minor version numbers.  See search_lib_dir() below for
 * the detailed matching rules.
 *
 * As soon as a directory with an acceptable match is found, the search
 * terminates.  Subsequent directories are not searched for a better
 * match.  This is in conformance with the SunOS searching rules.  Also,
 * it avoids a lot of directory searches that are virtually guaranteed to
 * be fruitless.
 *
 * The return value is a full pathname to the matching library.  The
 * string is dynamically allocated.  If no matching library is found, the
 * function returns NULL.
 */

char *
findshlib(char *name, int *majorp, int *minorp, int do_dot_a)
{
	int		i;

	for (i = 0; i < n_search_dirs; i++) {
		char	*path;

		path = search_lib_dir(search_dirs[i], name, majorp, minorp,
			do_dot_a);
		if(path != NULL)
			return path;
	}

	return NULL;
}

/*
 * Search library directories for a file with the given name.  The
 * return value is a full pathname to the matching file.  The string
 * is dynamically allocated.  If no matching file is found, the function
 * returns NULL.
 */

char *
find_lib_file(const char *name)
{
	int		i;

	for (i = 0; i < n_search_dirs; i++) {
		char		*path = concat(search_dirs[i], "/", name);
		struct stat	sb;

		if (lstat(path, &sb) != -1)	/* We found it */
			return path;

		free(path);
	}

	return NULL;
}

/*
 * Search a given directory for a library (preferably shared) satisfying
 * the given criteria.
 *
 * The matching rules are as follows:
 *
 *	if(*majorp == -1)
 *		find the library with the highest major version;
 *	else
 *		insist on a major version identical to *majorp;
 *
 *	Always find the library with the highest minor version;
 *	if(*minorp != -1)
 *		insist on a minor version >= *minorp;
 *
 * It is invalid to specify a specific minor number while wildcarding
 * the major number.
 *
 * The actual major and minor numbers found are returned via the pointer
 * arguments.
 *
 * A suitable shared library is always preferred over a static (.a) library.
 * If do_dot_a is false, then a static library will not be accepted in
 * any case.
 *
 * The return value is a full pathname to the matching library.  The
 * string is dynamically allocated.  If no matching library is found, the
 * function returns NULL.
 */

char *
search_lib_dir(char *dir, char *name, int *majorp, int *minorp, int do_dot_a)
{
	size_t		namelen;
	DIR		*dd;
	struct dirent	*dp;
	int		best_dewey[MAXDEWEY];
	int		best_ndewey;
	char		dot_a_name[MAXNAMLEN+1];
	char		dot_so_name[MAXNAMLEN+1];

	if((dd = opendir(dir)) == NULL)
		return NULL;

	namelen = strlen(name);
	best_ndewey = 0;
	dot_a_name[0] = '\0';
	dot_so_name[0] = '\0';

	while((dp = readdir(dd)) != NULL) {
		char *extension;

		if(strlen(dp->d_name) < 3 + namelen + 2 ||	/* lib+xxx+.a */
		   strncmp(dp->d_name, "lib", 3) != 0 ||
		   strncmp(dp->d_name + 3, name, namelen) != 0 ||
		   dp->d_name[3+namelen] != '.')
			continue;

		extension = dp->d_name + 3 + namelen + 1;	/* a or so.* */

		if(strncmp(extension, "so.", 3) == 0) {
			int cur_dewey[MAXDEWEY];
			int cur_ndewey;

			cur_ndewey = getdewey(cur_dewey, extension+3);
			if(cur_ndewey < 2)	/* Too few version numbers */
				continue;

			if(*majorp != -1) {	/* Need exact match on major */
				if(cur_dewey[0] != *majorp)
					continue;
				if(*minorp != -1) {  /* Need minor >= minimum */
					if(cur_dewey[1] < *minorp)
						continue;
				}
			}

			if(cmpndewey(cur_dewey, cur_ndewey, best_dewey,
			   best_ndewey) <= 0)	/* No better than prior match */
				continue;

			/* We found a better match */
			strcpy(dot_so_name, dp->d_name);
			bcopy(cur_dewey, best_dewey,
				cur_ndewey * sizeof best_dewey[0]);
			best_ndewey = cur_ndewey;
		} else if(do_dot_a && strcmp(extension, "a") == 0)
			strcpy(dot_a_name, dp->d_name);
	}
	closedir(dd);

	if(dot_so_name[0] != '\0') {
		*majorp = best_dewey[0];
		*minorp = best_dewey[1];
		return concat(dir, "/", dot_so_name);
	}

	if(dot_a_name[0] != '\0')
		return concat(dir, "/", dot_a_name);

	return NULL;
}
