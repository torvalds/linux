/*	$OpenBSD: opendir.c,v 1.32 2025/08/04 04:59:31 guenther Exp $ */
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

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "telldir.h"


/*
 * Open a directory specified by name.
 */
DIR *
opendir(const char *name)
{
	DIR *dirp;
	int fd;

	if ((fd = open(name, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) == -1)
		return (NULL);
	dirp = __fdopendir(fd);
	if (dirp == NULL)
		close(fd);
	return (dirp);
}
DEF_WEAK(opendir);

/*
 * Open a directory specified by file descriptor.
 */
DIR *
fdopendir(int fd)
{
	DIR *dirp;
	int flags;

	if ((flags = fcntl(fd, F_GETFL)) == -1)
		return (NULL);
	if ((flags & O_ACCMODE) != O_RDONLY && (flags & O_ACCMODE) != O_RDWR) {
		errno = EBADF;
		return (NULL);
	}
	dirp = __fdopendir(fd);
	if (dirp != NULL) {
		/* Record current offset for immediate telldir() */
		dirp->dd_bufpos = dirp->dd_curpos = lseek(fd, 0, SEEK_CUR);

		/*
		 * POSIX doesn't require fdopendir() to set
		 * FD_CLOEXEC, so it's okay for this to fail.
		 */
		flags = fcntl(fd, F_GETFD);
		if (flags != -1 && (flags & FD_CLOEXEC) == 0)
			(void)fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
	}
	return (dirp);
}
DEF_WEAK(fdopendir);

DIR *
__fdopendir(int fd)
{
	DIR *dirp;
	struct stat sb;
	int pageoffset;

	if (fstat(fd, &sb))
		return (NULL);
	if (!S_ISDIR(sb.st_mode)) {
		errno = ENOTDIR;
		return (NULL);
	}
	if ((dirp = malloc(sizeof(DIR))) == NULL)
		return (NULL);

	pageoffset = getpagesize() - 1;
	dirp->dd_len = ((int)sb.st_blksize * 4 + pageoffset) & ~pageoffset;
	dirp->dd_buf = malloc(dirp->dd_len);
	if (dirp->dd_buf == NULL) {
		free(dirp);
		return (NULL);
	}

	dirp->dd_size = 0;
	dirp->dd_loc = 0;
	dirp->dd_fd = fd;
	dirp->dd_lock = NULL;
	dirp->dd_curpos = 0;
	dirp->dd_bufpos = 0;

	return (dirp);
}
