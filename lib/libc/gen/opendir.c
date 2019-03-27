/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#include <sys/cdefs.h>
__SCCSID("@(#)opendir.c	8.8 (Berkeley) 5/1/95");
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "un-namespace.h"

#include "gen-private.h"
#include "telldir.h"

static DIR * __opendir_common(int, int, bool);

/*
 * Open a directory.
 */
DIR *
opendir(const char *name)
{

	return (__opendir2(name, DTF_HIDEW|DTF_NODUP));
}

/*
 * Open a directory with existing file descriptor.
 */
DIR *
fdopendir(int fd)
{

	if (_fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
		return (NULL);
	return (__opendir_common(fd, DTF_HIDEW|DTF_NODUP, true));
}

DIR *
__opendir2(const char *name, int flags)
{
	int fd;
	DIR *dir;
	int saved_errno;

	if ((flags & (__DTF_READALL | __DTF_SKIPREAD)) != 0)
		return (NULL);
	if ((fd = _open(name,
	    O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC)) == -1)
		return (NULL);

	dir = __opendir_common(fd, flags, false);
	if (dir == NULL) {
		saved_errno = errno;
		_close(fd);
		errno = saved_errno;
	}
	return (dir);
}

static int
opendir_compar(const void *p1, const void *p2)
{

	return (strcmp((*(const struct dirent **)p1)->d_name,
	    (*(const struct dirent **)p2)->d_name));
}

/*
 * For a directory at the top of a unionfs stack, the entire directory's
 * contents are read and cached locally until the next call to rewinddir().
 * For the fdopendir() case, the initial seek position must be preserved.
 * For rewinddir(), the full directory should always be re-read from the
 * beginning.
 *
 * If an error occurs, the existing buffer and state of 'dirp' is left
 * unchanged.
 */
bool
_filldir(DIR *dirp, bool use_current_pos)
{
	struct dirent **dpv;
	char *buf, *ddptr, *ddeptr;
	off_t pos;
	int fd2, incr, len, n, saved_errno, space;
	
	len = 0;
	space = 0;
	buf = NULL;
	ddptr = NULL;

	/*
	 * Use the system page size if that is a multiple of DIRBLKSIZ.
	 * Hopefully this can be a big win someday by allowing page
	 * trades to user space to be done by _getdirentries().
	 */
	incr = getpagesize();
	if ((incr % DIRBLKSIZ) != 0) 
		incr = DIRBLKSIZ;

	/*
	 * The strategy here is to read all the directory
	 * entries into a buffer, sort the buffer, and
	 * remove duplicate entries by setting the inode
	 * number to zero.
	 *
	 * We reopen the directory because _getdirentries()
	 * on a MNT_UNION mount modifies the open directory,
	 * making it refer to the lower directory after the
	 * upper directory's entries are exhausted.
	 * This would otherwise break software that uses
	 * the directory descriptor for fchdir or *at
	 * functions, such as fts.c.
	 */
	if ((fd2 = _openat(dirp->dd_fd, ".", O_RDONLY | O_CLOEXEC)) == -1)
		return (false);

	if (use_current_pos) {
		pos = lseek(dirp->dd_fd, 0, SEEK_CUR);
		if (pos == -1 || lseek(fd2, pos, SEEK_SET) == -1) {
			saved_errno = errno;
			_close(fd2);
			errno = saved_errno;
			return (false);
		}
	}

	do {
		/*
		 * Always make at least DIRBLKSIZ bytes
		 * available to _getdirentries
		 */
		if (space < DIRBLKSIZ) {
			space += incr;
			len += incr;
			buf = reallocf(buf, len);
			if (buf == NULL) {
				saved_errno = errno;
				_close(fd2);
				errno = saved_errno;
				return (false);
			}
			ddptr = buf + (len - space);
		}

		n = _getdirentries(fd2, ddptr, space, &dirp->dd_seek);
		if (n > 0) {
			ddptr += n;
			space -= n;
		}
		if (n < 0) {
			saved_errno = errno;
			_close(fd2);
			errno = saved_errno;
			return (false);
		}
	} while (n > 0);
	_close(fd2);

	ddeptr = ddptr;

	/*
	 * There is now a buffer full of (possibly) duplicate
	 * names.
	 */
	dirp->dd_buf = buf;

	/*
	 * Go round this loop twice...
	 *
	 * Scan through the buffer, counting entries.
	 * On the second pass, save pointers to each one.
	 * Then sort the pointers and remove duplicate names.
	 */
	for (dpv = NULL;;) {
		n = 0;
		ddptr = buf;
		while (ddptr < ddeptr) {
			struct dirent *dp;

			dp = (struct dirent *) ddptr;
			if ((long)dp & 03L)
				break;
			if ((dp->d_reclen <= 0) ||
			    (dp->d_reclen > (ddeptr + 1 - ddptr)))
				break;
			ddptr += dp->d_reclen;
			if (dp->d_fileno) {
				if (dpv)
					dpv[n] = dp;
				n++;
			}
		}

		if (dpv) {
			struct dirent *xp;

			/*
			 * This sort must be stable.
			 */
			mergesort(dpv, n, sizeof(*dpv), opendir_compar);

			dpv[n] = NULL;
			xp = NULL;

			/*
			 * Scan through the buffer in sort order,
			 * zapping the inode number of any
			 * duplicate names.
			 */
			for (n = 0; dpv[n]; n++) {
				struct dirent *dp = dpv[n];

				if ((xp == NULL) ||
				    strcmp(dp->d_name, xp->d_name)) {
					xp = dp;
				} else {
					dp->d_fileno = 0;
				}
				if (dp->d_type == DT_WHT &&
				    (dirp->dd_flags & DTF_HIDEW))
					dp->d_fileno = 0;
			}

			free(dpv);
			break;
		} else {
			dpv = malloc((n+1) * sizeof(struct dirent *));
			if (dpv == NULL)
				break;
		}
	}

	dirp->dd_len = len;
	dirp->dd_size = ddptr - dirp->dd_buf;
	return (true);
}


/*
 * Common routine for opendir(3), __opendir2(3) and fdopendir(3).
 */
static DIR *
__opendir_common(int fd, int flags, bool use_current_pos)
{
	DIR *dirp;
	int incr;
	int saved_errno;
	int unionstack;

	if ((dirp = malloc(sizeof(DIR) + sizeof(struct _telldir))) == NULL)
		return (NULL);

	dirp->dd_buf = NULL;
	dirp->dd_fd = fd;
	dirp->dd_flags = flags;
	dirp->dd_loc = 0;
	dirp->dd_lock = NULL;
	dirp->dd_td = (struct _telldir *)((char *)dirp + sizeof(DIR));
	LIST_INIT(&dirp->dd_td->td_locq);
	dirp->dd_td->td_loccnt = 0;
	dirp->dd_compat_de = NULL;

	/*
	 * Use the system page size if that is a multiple of DIRBLKSIZ.
	 * Hopefully this can be a big win someday by allowing page
	 * trades to user space to be done by _getdirentries().
	 */
	incr = getpagesize();
	if ((incr % DIRBLKSIZ) != 0) 
		incr = DIRBLKSIZ;

	/*
	 * Determine whether this directory is the top of a union stack.
	 */
	if (flags & DTF_NODUP) {
		struct statfs sfb;

		if (_fstatfs(fd, &sfb) < 0)
			goto fail;
		unionstack = !strcmp(sfb.f_fstypename, "unionfs")
		    || (sfb.f_flags & MNT_UNION);
	} else {
		unionstack = 0;
	}

	if (unionstack) {
		if (!_filldir(dirp, use_current_pos))
			goto fail;
		dirp->dd_flags |= __DTF_READALL;
	} else {
		dirp->dd_len = incr;
		dirp->dd_buf = malloc(dirp->dd_len);
		if (dirp->dd_buf == NULL)
			goto fail;
		if (use_current_pos) {
			/*
			 * Read the first batch of directory entries
			 * to prime dd_seek.  This also checks if the
			 * fd passed to fdopendir() is a directory.
			 */
			dirp->dd_size = _getdirentries(dirp->dd_fd,
			    dirp->dd_buf, dirp->dd_len, &dirp->dd_seek);
			if (dirp->dd_size < 0) {
				if (errno == EINVAL)
					errno = ENOTDIR;
				goto fail;
			}
			dirp->dd_flags |= __DTF_SKIPREAD;
		} else {
			dirp->dd_size = 0;
			dirp->dd_seek = 0;
		}
	}

	return (dirp);

fail:
	saved_errno = errno;
	free(dirp->dd_buf);
	free(dirp);
	errno = saved_errno;
	return (NULL);
}
