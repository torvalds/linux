/*	$OpenBSD: readlabel.c,v 1.16 2025/09/17 16:16:20 deraadt Exp $	*/

/*
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

#include <sys/types.h>
#include <sys/disk.h>
#include <sys/dkio.h>
#define DKTYPENAMES
#include <sys/disklabel.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

/*
 * Try to get a disklabel for the specified device, and return mount_xxx
 * style filesystem type name for the specified partition.
 */
char *
readlabelfs(char *device, int verbose)
{
	char rpath[PATH_MAX];
	struct dk_diskmap dm;
	struct disklabel dk;
	char part, *type;
	struct stat sbuf;
	int fd = -1, partno;

	/* Perform disk mapping if device is given as a DUID. */
	if (isduid(device, 0)) {
		if ((fd = open("/dev/diskmap", O_RDONLY|O_CLOEXEC)) != -1) {
			bzero(&dm, sizeof(struct dk_diskmap));
			strlcpy(rpath, device, sizeof(rpath));
			part = rpath[strlen(rpath) - 1];
			dm.device = rpath;
			dm.fd = fd;
			dm.flags = DM_OPENPART;
			if (ioctl(fd, DIOCMAP, &dm) == -1)
				close(fd);
			else
				goto disklabel;
		}
	}

	/* Assuming device is of the form /dev/??p, build a raw partition. */
	if (stat(device, &sbuf) == -1) {
		if (verbose)
			warn("%s", device);
		return (NULL);
	}
	switch (sbuf.st_mode & S_IFMT) {
	case S_IFCHR:
		/* Ok... already a raw device.  Hmm. */
		strlcpy(rpath, device, sizeof(rpath));

		/* Change partition name. */
		part = rpath[strlen(rpath) - 1];
		rpath[strlen(rpath) - 1] = 'a' + getrawpartition();
		break;
	case S_IFBLK:
		if (strlen(device) > sizeof(_PATH_DEV) - 1) {
			snprintf(rpath, sizeof(rpath), "%sr%s", _PATH_DEV,
			    &device[sizeof(_PATH_DEV) - 1]);
			/* Change partition name. */
			part = rpath[strlen(rpath) - 1];
			rpath[strlen(rpath) - 1] = 'a' + getrawpartition();
			break;
		}
		/* FALLTHROUGH */
	default:
		if (verbose)
			warnx("%s: not a device node", device);
		return (NULL);
	}

	/* If rpath doesn't exist, change that partition back. */
	fd = open(rpath, O_RDONLY|O_CLOEXEC);
	if (fd == -1) {
		if (errno == ENOENT) {
			rpath[strlen(rpath) - 1] = part;

			fd = open(rpath, O_RDONLY|O_CLOEXEC);
			if (fd == -1) {
				if (verbose)
					warn("%s", rpath);
				return (NULL);
			}
		} else {
			if (verbose)
				warn("%s", rpath);
			return (NULL);
		}
	}

disklabel:
	partno = DL_PARTNAME2NUM(part);
	if (partno == -1) {
		if (verbose)
			warn("%s: diskmap provided weird partition", rpath);
		close(fd);
		return NULL;
	}
	if (ioctl(fd, DIOCGDINFO, &dk) == -1) {
		if (verbose)
			warn("%s: couldn't read disklabel", rpath);
		close(fd);
		return (NULL);
	}
	close(fd);

	if (dk.d_partitions[partno].p_fstype >= FSMAXTYPES) {
		if (verbose)
			warnx("%s: bad filesystem type in label", rpath);
		return (NULL);
	}

	type = fstypesnames[dk.d_partitions[partno].p_fstype];
	return ((type[0] == '\0') ? NULL : type);
}
