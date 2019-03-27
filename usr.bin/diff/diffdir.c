/*	$OpenBSD: diffdir.c,v 1.45 2015/10/05 20:15:00 millert Exp $	*/

/*
 * Copyright (c) 2003, 2010 Todd C. Miller <Todd.Miller@courtesan.com>
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
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "diff.h"

static int selectfile(const struct dirent *);
static void diffit(struct dirent *, char *, size_t, char *, size_t, int);

#define d_status	d_type		/* we need to store status for -l */

/*
 * Diff directory traversal. Will be called recursively if -r was specified.
 */
void
diffdir(char *p1, char *p2, int flags)
{
	struct dirent *dent1, **dp1, **edp1, **dirp1 = NULL;
	struct dirent *dent2, **dp2, **edp2, **dirp2 = NULL;
	size_t dirlen1, dirlen2;
	char path1[PATH_MAX], path2[PATH_MAX];
	int pos;

	edp1 = edp2 = NULL;

	dirlen1 = strlcpy(path1, *p1 ? p1 : ".", sizeof(path1));
	if (dirlen1 >= sizeof(path1) - 1) {
		warnc(ENAMETOOLONG, "%s", p1);
		status |= 2;
		return;
	}
	if (path1[dirlen1 - 1] != '/') {
		path1[dirlen1++] = '/';
		path1[dirlen1] = '\0';
	}
	dirlen2 = strlcpy(path2, *p2 ? p2 : ".", sizeof(path2));
	if (dirlen2 >= sizeof(path2) - 1) {
		warnc(ENAMETOOLONG, "%s", p2);
		status |= 2;
		return;
	}
	if (path2[dirlen2 - 1] != '/') {
		path2[dirlen2++] = '/';
		path2[dirlen2] = '\0';
	}

	/*
	 * Get a list of entries in each directory, skipping "excluded" files
	 * and sorting alphabetically.
	 */
	pos = scandir(path1, &dirp1, selectfile, alphasort);
	if (pos == -1) {
		if (errno == ENOENT && (Nflag || Pflag)) {
			pos = 0;
		} else {
			warn("%s", path1);
			goto closem;
		}
	}
	dp1 = dirp1;
	edp1 = dirp1 + pos;

	pos = scandir(path2, &dirp2, selectfile, alphasort);
	if (pos == -1) {
		if (errno == ENOENT && Nflag) {
			pos = 0;
		} else {
			warn("%s", path2);
			goto closem;
		}
	}
	dp2 = dirp2;
	edp2 = dirp2 + pos;

	/*
	 * If we were given a starting point, find it.
	 */
	if (start != NULL) {
		while (dp1 != edp1 && strcmp((*dp1)->d_name, start) < 0)
			dp1++;
		while (dp2 != edp2 && strcmp((*dp2)->d_name, start) < 0)
			dp2++;
	}

	/*
	 * Iterate through the two directory lists, diffing as we go.
	 */
	while (dp1 != edp1 || dp2 != edp2) {
		dent1 = dp1 != edp1 ? *dp1 : NULL;
		dent2 = dp2 != edp2 ? *dp2 : NULL;

		pos = dent1 == NULL ? 1 : dent2 == NULL ? -1 :
		    ignore_file_case ? strcasecmp(dent1->d_name, dent2->d_name) :
		    strcmp(dent1->d_name, dent2->d_name) ;
		if (pos == 0) {
			/* file exists in both dirs, diff it */
			diffit(dent1, path1, dirlen1, path2, dirlen2, flags);
			dp1++;
			dp2++;
		} else if (pos < 0) {
			/* file only in first dir, only diff if -N */
			if (Nflag) {
				diffit(dent1, path1, dirlen1, path2, dirlen2,
				    flags);
			} else {
				print_only(path1, dirlen1, dent1->d_name);
				status |= 1;
			}
			dp1++;
		} else {
			/* file only in second dir, only diff if -N or -P */
			if (Nflag || Pflag)
				diffit(dent2, path1, dirlen1, path2, dirlen2,
				    flags);
			else {
				print_only(path2, dirlen2, dent2->d_name);
				status |= 1;
			}
			dp2++;
		}
	}

closem:
	if (dirp1 != NULL) {
		for (dp1 = dirp1; dp1 < edp1; dp1++)
			free(*dp1);
		free(dirp1);
	}
	if (dirp2 != NULL) {
		for (dp2 = dirp2; dp2 < edp2; dp2++)
			free(*dp2);
		free(dirp2);
	}
}

/*
 * Do the actual diff by calling either diffreg() or diffdir().
 */
static void
diffit(struct dirent *dp, char *path1, size_t plen1, char *path2, size_t plen2,
    int flags)
{
	flags |= D_HEADER;
	strlcpy(path1 + plen1, dp->d_name, PATH_MAX - plen1);
	if (stat(path1, &stb1) != 0) {
		if (!(Nflag || Pflag) || errno != ENOENT) {
			warn("%s", path1);
			return;
		}
		flags |= D_EMPTY1;
		memset(&stb1, 0, sizeof(stb1));
	}

	strlcpy(path2 + plen2, dp->d_name, PATH_MAX - plen2);
	if (stat(path2, &stb2) != 0) {
		if (!Nflag || errno != ENOENT) {
			warn("%s", path2);
			return;
		}
		flags |= D_EMPTY2;
		memset(&stb2, 0, sizeof(stb2));
		stb2.st_mode = stb1.st_mode;
	}
	if (stb1.st_mode == 0)
		stb1.st_mode = stb2.st_mode;

	if (S_ISDIR(stb1.st_mode) && S_ISDIR(stb2.st_mode)) {
		if (rflag)
			diffdir(path1, path2, flags);
		else
			printf("Common subdirectories: %s and %s\n",
			    path1, path2);
		return;
	}
	if (!S_ISREG(stb1.st_mode) && !S_ISDIR(stb1.st_mode))
		dp->d_status = D_SKIPPED1;
	else if (!S_ISREG(stb2.st_mode) && !S_ISDIR(stb2.st_mode))
		dp->d_status = D_SKIPPED2;
	else
		dp->d_status = diffreg(path1, path2, flags, 0);
	print_status(dp->d_status, path1, path2, "");
}

/*
 * Returns 1 if the directory entry should be included in the
 * diff, else 0.  Checks the excludes list.
 */
static int
selectfile(const struct dirent *dp)
{
	struct excludes *excl;

	if (dp->d_fileno == 0)
		return (0);

	/* always skip "." and ".." */
	if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
	    (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
		return (0);

	/* check excludes list */
	for (excl = excludes_list; excl != NULL; excl = excl->next)
		if (fnmatch(excl->pattern, dp->d_name, FNM_PATHNAME) == 0)
			return (0);

	return (1);
}
