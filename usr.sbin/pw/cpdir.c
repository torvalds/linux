/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1996
 *	David L. Nugent.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY DAVID L. NUGENT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL DAVID L. NUGENT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "pw.h"

void
copymkdir(int rootfd, char const * dir, int skelfd, mode_t mode, uid_t uid,
    gid_t gid, int flags)
{
	char		*p, lnk[MAXPATHLEN], copybuf[4096];
	int		len, homefd, srcfd, destfd;
	ssize_t		sz;
	struct stat     st;
	struct dirent  *e;
	DIR		*d;

	if (*dir == '/')
		dir++;

	if (mkdirat(rootfd, dir, mode) != 0 && errno != EEXIST) {
		warn("mkdir(%s)", dir);
		return;
	}
	fchownat(rootfd, dir, uid, gid, AT_SYMLINK_NOFOLLOW);
	if (flags > 0)
		chflagsat(rootfd, dir, flags, AT_SYMLINK_NOFOLLOW);

	if (skelfd == -1)
		return;

	homefd = openat(rootfd, dir, O_DIRECTORY);
	if ((d = fdopendir(skelfd)) == NULL) {
		close(skelfd);
		close(homefd);
		return;
	}

	while ((e = readdir(d)) != NULL) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
			continue;

		p = e->d_name;
		if (fstatat(skelfd, p, &st, AT_SYMLINK_NOFOLLOW) == -1)
			continue;

		if (strncmp(p, "dot.", 4) == 0)	/* Conversion */
			p += 3;

		if (S_ISDIR(st.st_mode)) {
			copymkdir(homefd, p, openat(skelfd, e->d_name, O_DIRECTORY),
			    st.st_mode & _DEF_DIRMODE, uid, gid, st.st_flags);
			continue;
		}

		if (S_ISLNK(st.st_mode) &&
		    (len = readlinkat(skelfd, e->d_name, lnk, sizeof(lnk) -1))
		    != -1) {
			lnk[len] = '\0';
			symlinkat(lnk, homefd, p);
			fchownat(homefd, p, uid, gid, AT_SYMLINK_NOFOLLOW);
			continue;
		}

		if (!S_ISREG(st.st_mode))
			continue;

		if ((srcfd = openat(skelfd, e->d_name, O_RDONLY)) == -1)
			continue;
		destfd = openat(homefd, p, O_RDWR | O_CREAT | O_EXCL,
		    st.st_mode);
		if (destfd == -1) {
			close(srcfd);
			continue;
		}

		while ((sz = read(srcfd, copybuf, sizeof(copybuf))) > 0)
			write(destfd, copybuf, sz);

		close(srcfd);
		/*
		 * Propagate special filesystem flags
		 */
		fchown(destfd, uid, gid);
		fchflags(destfd, st.st_flags);
		close(destfd);
	}
	closedir(d);
}
