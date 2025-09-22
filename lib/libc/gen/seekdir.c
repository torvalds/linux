/*	$OpenBSD: seekdir.c,v 1.13 2015/09/12 13:34:22 guenther Exp $ */
/*
 * Copyright (c) 2013 Ingo Schwarze <schwarze@openbsd.org>
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
 */

#include <dirent.h>
#include <unistd.h>

#include "thread_private.h"
#include "telldir.h"

/*
 * Seek to an entry in a directory.
 * Only values returned by "telldir" should be passed to seekdir.
 */
void
seekdir(DIR *dirp, long loc)
{
	struct dirent *dp;

	/*
	 * First check whether the directory entry to seek for
	 * is still buffered in the directory structure in memory.
	 */

	_MUTEX_LOCK(&dirp->dd_lock);
	if (dirp->dd_size && dirp->dd_bufpos == loc) {
		dirp->dd_loc = 0;
		dirp->dd_curpos = loc;
		_MUTEX_UNLOCK(&dirp->dd_lock);
		return;
	}

	for (dirp->dd_loc = 0;
	     dirp->dd_loc < dirp->dd_size;
	     dirp->dd_loc += dp->d_reclen) {
		dp = (struct dirent *)(dirp->dd_buf + dirp->dd_loc);
		if (dp->d_off != loc)
			continue;

		/*
		 * Entry found in the buffer, use it.  If readdir(3)
		 * follows, this will save us a getdents(2) syscall.
		 * Note that d_off is the offset of the _next_ entry,
		 * so advance dd_loc.
		 */

		dirp->dd_loc += dp->d_reclen;
		dirp->dd_curpos = loc;
		_MUTEX_UNLOCK(&dirp->dd_lock);
		return;
	}

	/*
	 * The entry is not in the buffer, prepare a call to getdents(2).
	 * In particular, invalidate dd_loc.
	 */

	dirp->dd_loc = dirp->dd_size;
	dirp->dd_bufpos = dirp->dd_curpos = lseek(dirp->dd_fd, loc, SEEK_SET);
	_MUTEX_UNLOCK(&dirp->dd_lock);
}
DEF_WEAK(seekdir);
