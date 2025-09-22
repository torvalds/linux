/*	$OpenBSD: ttyname.c,v 1.21 2024/01/22 17:22:58 deraadt Exp $ */
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <db.h>
#include <string.h>
#include <unistd.h>
#include <paths.h>
#include <limits.h>
#include <errno.h>
#include "thread_private.h"

static char buf[TTY_NAME_MAX];
static int oldttyname(struct stat *, char *, size_t);

char *
ttyname(int fd)
{
	_THREAD_PRIVATE_KEY(ttyname);
	char *bufp = (char *) _THREAD_PRIVATE(ttyname, buf, NULL);
	int err;

	if (bufp == NULL)
		return NULL;

	err = ttyname_r(fd, bufp, sizeof buf);
	if (err) {
		errno = err;
		return NULL;
	}

	return bufp;
}
DEF_WEAK(ttyname);

int
ttyname_r(int fd, char *buf, size_t len)
{
	struct stat sb;
	DB *db;
	DBT data, key;
	struct {
		mode_t type;
		dev_t dev;
	} bkey;

	/* Must be a terminal. */
	if (!isatty(fd))
		return (errno);
	/* Must be a character device. */
	if (fstat(fd, &sb))
		return (errno);
	if (!S_ISCHR(sb.st_mode))
		return (ENOTTY);
	if (len < sizeof(_PATH_DEV))
		return (ERANGE);

	memcpy(buf, _PATH_DEV, sizeof(_PATH_DEV));

	if ((db = __hash_open(_PATH_DEVDB, O_RDONLY, 0, NULL, 0))) {
		memset(&bkey, 0, sizeof(bkey));
		bkey.type = S_IFCHR;
		bkey.dev = sb.st_rdev;
		key.data = &bkey;
		key.size = sizeof(bkey);
		if (!(db->get)(db, &key, &data, 0)) {
			if (data.size > len - (sizeof(_PATH_DEV) - 1)) {
				(void)(db->close)(db);
				return (ERANGE);
			}
			memcpy(buf + sizeof(_PATH_DEV) - 1, data.data,
			    data.size);
			(void)(db->close)(db);
			return (0);
		}
		(void)(db->close)(db);
	}
	return (oldttyname(&sb, buf, len));
}
DEF_WEAK(ttyname_r);

static int
oldttyname(struct stat *sb, char *buf, size_t len)
{
	struct dirent *dirp;
	DIR *dp;
	struct stat dsb;
	int error = ENOTTY;

	if ((dp = opendir(_PATH_DEV)) == NULL)
		return (errno);

	while ((dirp = readdir(dp))) {
		if (dirp->d_type != DT_CHR && dirp->d_type != DT_UNKNOWN)
			continue;
		if (fstatat(dirfd(dp), dirp->d_name, &dsb, AT_SYMLINK_NOFOLLOW)
		    || !S_ISCHR(dsb.st_mode) || sb->st_rdev != dsb.st_rdev)
			continue;
		if (dirp->d_namlen > len - sizeof(_PATH_DEV)) {
			error = ERANGE;
		} else {
			memcpy(buf + sizeof(_PATH_DEV) - 1, dirp->d_name,
			    dirp->d_namlen + 1);
			error = 0;
		}
		break;
	}
	(void)closedir(dp);
	return (error);
}
