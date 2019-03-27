/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <ufs/ufs/ufsmount.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <mntopts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

static void
usage(void)
{

	errx(EX_USAGE, "usage: mksnap_ffs snapshot_name");
}

static int
isdir(const char *path, struct stat *stbufp)
{

	if (stat(path, stbufp) < 0)
		return (-1);
        if (!S_ISDIR(stbufp->st_mode))
		return (0);
	return (1);
}

static int
issamefs(const char *path, struct statfs *stfsp)
{
	struct statfs stfsbuf;
	struct stat stbuf;

	if (isdir(path, &stbuf) != 1)
		return (-1);
	if (statfs(path, &stfsbuf) < 0)
		return (-1);
	if ((stfsbuf.f_fsid.val[0] != stfsp->f_fsid.val[0]) ||
	    (stfsbuf.f_fsid.val[1] != stfsp->f_fsid.val[1]))
		return (0);
	return (1);
}

int
main(int argc, char **argv)
{
	char errmsg[255], path[PATH_MAX];
	char *cp, *snapname;
	struct statfs stfsbuf;
	struct group *grp;
	struct stat stbuf;
	struct iovec *iov;
	int fd, iovlen;

	if (argc == 2)
		snapname = argv[1];
	else if (argc == 3)
		snapname = argv[2];	/* Old usage. */
	else
		usage();

	/*
	 * Check that the user running this program has permission
	 * to create and remove a snapshot file from the directory
	 * in which they have requested to have it made. If the 
	 * directory is sticky and not owned by the user, then they
	 * will not be able to remove the snapshot when they are
	 * done with it.
	 */
	if (strlen(snapname) >= PATH_MAX)
		errx(1, "pathname too long %s", snapname);
	cp = strrchr(snapname, '/');
	if (cp == NULL) {
		strlcpy(path, ".", PATH_MAX);
	} else if (cp == snapname) {
		strlcpy(path, "/", PATH_MAX);
	} else {
		strlcpy(path, snapname, cp - snapname + 1);
	}
	if (statfs(path, &stfsbuf) < 0)
		err(1, "%s", path);
	switch (isdir(path, &stbuf)) {
	case -1:
		err(1, "%s", path);
	case 0:
		errx(1, "%s: Not a directory", path);
	default:
		break;
	}
	if (access(path, W_OK) < 0)
		err(1, "Lack write permission in %s", path);
	if ((stbuf.st_mode & S_ISTXT) && stbuf.st_uid != getuid())
		errx(1, "Lack write permission in %s: Sticky bit set", path);

	/*
	 * Work around an issue when mksnap_ffs is started in chroot'ed
	 * environment and f_mntonname contains absolute path within
	 * real root.
	 */
	for (cp = stfsbuf.f_mntonname; issamefs(cp, &stfsbuf) != 1;
	    cp = strchrnul(cp + 1, '/')) {
		if (cp[0] == '\0')
			errx(1, "%s: Not a mount point", stfsbuf.f_mntonname);
	}
	if (cp != stfsbuf.f_mntonname)
		strlcpy(stfsbuf.f_mntonname, cp, sizeof(stfsbuf.f_mntonname));

	/*
	 * Having verified access to the directory in which the
	 * snapshot is to be built, proceed with creating it.
	 */
	if ((grp = getgrnam("operator")) == NULL)
		errx(1, "Cannot retrieve operator gid");

	iov = NULL;
	iovlen = 0;
	build_iovec(&iov, &iovlen, "fstype", "ffs", 4);
	build_iovec(&iov, &iovlen, "from", snapname, (size_t)-1);
	build_iovec(&iov, &iovlen, "fspath", stfsbuf.f_mntonname, (size_t)-1);
	build_iovec(&iov, &iovlen, "errmsg", errmsg, sizeof(errmsg));
	build_iovec(&iov, &iovlen, "update", NULL, 0);
	build_iovec(&iov, &iovlen, "snapshot", NULL, 0);

	*errmsg = '\0';
	if (nmount(iov, iovlen, stfsbuf.f_flags) < 0) {
		errmsg[sizeof(errmsg) - 1] = '\0';
		err(1, "Cannot create snapshot %s%s%s", snapname,
		    *errmsg != '\0' ? ": " : "", errmsg);
	}
	if ((fd = open(snapname, O_RDONLY)) < 0)
		err(1, "Cannot open %s", snapname);
	if (fstat(fd, &stbuf) != 0)
		err(1, "Cannot stat %s", snapname);
	if ((stbuf.st_flags & SF_SNAPSHOT) == 0)
		errx(1, "File %s is not a snapshot", snapname);
	if (fchown(fd, -1, grp->gr_gid) != 0)
		err(1, "Cannot chown %s", snapname);
	if (fchmod(fd, S_IRUSR | S_IRGRP) != 0)
		err(1, "Cannot chmod %s", snapname);

	exit(EXIT_SUCCESS);
}
