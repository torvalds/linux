/*	$OpenBSD: scandir.c,v 1.23 2024/04/15 15:47:58 florian Exp $ */
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
 */

/*
 * Scan the directory dirname calling select to make a list of selected
 * directory entries then sort using qsort and compare routine dcomp.
 * Returns the number of entries and a pointer to a list of pointers to
 * struct dirent (through namelist). Returns -1 if there were any errors.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "telldir.h"

#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

/*
 * The DIRSIZ macro is the minimum record length which will hold the directory
 * entry.  This requires the amount of space in struct dirent without the
 * d_name field, plus enough space for the name and a terminating nul byte
 * (dp->d_namlen + 1), rounded up to a 4 byte boundary.
 */
#undef DIRSIZ
#define DIRSIZ(dp)							\
	((sizeof(struct dirent) - sizeof(dp)->d_name) +			\
	    (((dp)->d_namlen + 1 + 3) &~ 3))

static int
scandir_dirp(DIR *dirp, struct dirent ***namelist,
    int (*select)(const struct dirent *),
    int (*dcomp)(const struct dirent **, const struct dirent **))
{
	struct dirent *d, *p, **names = NULL;
	size_t nitems = 0;
	struct stat stb;
	long arraysz;

	if (fstat(dirp->dd_fd, &stb) == -1)
		goto fail;

	/*
	 * estimate the array size by taking the size of the directory file
	 * and dividing it by a multiple of the minimum size entry.
	 */
	arraysz = MAXIMUM(stb.st_size / 24, 16);
	if (arraysz > SIZE_MAX / sizeof(struct dirent *)) {
		errno = ENOMEM;
		goto fail;
	}
	names = calloc(arraysz, sizeof(struct dirent *));
	if (names == NULL)
		goto fail;

	while ((d = readdir(dirp)) != NULL) {
		if (select != NULL && !(*select)(d))
			continue;	/* just selected names */

		/*
		 * Check to make sure the array has space left and
		 * realloc the maximum size.
		 */
		if (nitems >= arraysz) {
			struct dirent **nnames;

			if (fstat(dirp->dd_fd, &stb) == -1)
				goto fail;

			arraysz *= 2;
			if (SIZE_MAX / sizeof(struct dirent *) < arraysz)
				goto fail;
			nnames = reallocarray(names,
			    arraysz, sizeof(struct dirent *));
			if (nnames == NULL)
				goto fail;

			names = nnames;
		}

		/*
		 * Make a minimum size copy of the data
		 */
		p = malloc(DIRSIZ(d));
		if (p == NULL)
			goto fail;

		p->d_ino = d->d_ino;
		p->d_type = d->d_type;
		p->d_reclen = d->d_reclen;
		p->d_namlen = d->d_namlen;
		bcopy(d->d_name, p->d_name, p->d_namlen + 1);
		names[nitems++] = p;
	}
	closedir(dirp);
	if (nitems && dcomp != NULL)
		qsort(names, nitems, sizeof(struct dirent *),
		    (int(*)(const void *, const void *))dcomp);
	*namelist = names;
	return (nitems);

fail:
	while (nitems > 0)
		free(names[--nitems]);
	free(names);
	closedir(dirp);
	return (-1);
}

int
scandir(const char *dirname, struct dirent ***namelist,
    int (*select)(const struct dirent *),
    int (*dcomp)(const struct dirent **, const struct dirent **))
{
	DIR *dirp;

	if ((dirp = opendir(dirname)) == NULL)
		return (-1);

	return (scandir_dirp(dirp, namelist, select, dcomp));
}

int
scandirat(int dirfd, const char *dirname, struct dirent ***namelist,
    int (*select)(const struct dirent *),
    int (*dcomp)(const struct dirent **, const struct dirent **))
{
	DIR *dirp;
	int fd;

	fd = HIDDEN(openat)(dirfd, dirname, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd == -1)
		return (-1);
	dirp = __fdopendir(fd);
	if (dirp == NULL) {
		HIDDEN(close)(fd);
		return (-1);
	}
	return (scandir_dirp(dirp, namelist, select, dcomp));
}

/*
 * Alphabetic order comparison routine for those who want it.
 */
int
alphasort(const struct dirent **d1, const struct dirent **d2)
{
	return(strcmp((*d1)->d_name, (*d2)->d_name));
}
