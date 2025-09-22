/*	$OpenBSD: opendev.c,v 1.18 2025/08/25 14:59:13 claudio Exp $	*/

/*
 * Copyright (c) 2000, Todd C. Miller.  All rights reserved.
 * Copyright (c) 1996, Jason Downs.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/limits.h>
#include <sys/disk.h>
#include <sys/dkio.h>

#include "util.h"

/*
 * This routine is a generic rewrite of the original code found in
 * disklabel(8).
 */
int
opendev(const char *path, int oflags, int dflags, char **realpath)
{
	static char namebuf[PATH_MAX];
	struct dk_diskmap dm;
	char *slash, *prefix;
	int fd, ret;

	/* Initial state */
	fd = -1;
	errno = ENOENT;

	if (dflags & OPENDEV_BLCK)
		prefix = "";			/* block device */
	else
		prefix = "r";			/* character device */

	if ((slash = strchr(path, '/'))) {
		strlcpy(namebuf, path, sizeof(namebuf));
		fd = open(namebuf, oflags);
	} else if (isduid(path, dflags)) {
		strlcpy(namebuf, path, sizeof(namebuf));
		if ((fd = open("/dev/diskmap", oflags)) != -1) {
			bzero(&dm, sizeof(struct dk_diskmap));
			dm.device = namebuf;
			dm.fd = fd;
			if (dflags & OPENDEV_PART)
				dm.flags |= DM_OPENPART;
			if (dflags & OPENDEV_BLCK)
				dm.flags |= DM_OPENBLCK;

			if (ioctl(fd, DIOCMAP, &dm) == -1) {
				close(fd);
				fd = -1;
				errno = ENOENT;
			}
		}
	}
	if (!slash && fd == -1 && errno == ENOENT) {
		if (dflags & OPENDEV_PART) {
			/*
			 * First try raw partition (for removable drives)
			 */
			ret = snprintf(namebuf, sizeof(namebuf), "%s%s%s%c",
			    _PATH_DEV, prefix, path, 'a' + getrawpartition());
			if (ret < 0 || (size_t)ret >= sizeof(namebuf))
				errno = ENAMETOOLONG;
			else
				fd = open(namebuf, oflags);
		}
		if (fd == -1 && errno == ENOENT) {
			ret = snprintf(namebuf, sizeof(namebuf), "%s%s%s",
			    _PATH_DEV, prefix, path);
			if (ret < 0 || (size_t)ret >= sizeof(namebuf))
				errno = ENAMETOOLONG;
			else
				fd = open(namebuf, oflags);
		}
	}
	if (realpath)
		*realpath = namebuf;

	return (fd);
}
