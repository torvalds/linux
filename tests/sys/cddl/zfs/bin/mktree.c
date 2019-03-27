/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 * $FreeBSD$
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)mktree.c	1.3	07/05/25 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/param.h>

#define	TYPE_D 'D'
#define	TYPE_F 'F'

extern int errno;

static char fdname[MAXPATHLEN] = {0};
static char *pbasedir = NULL;
static int nlevel = 2;
static int ndir = 2;
static int nfile = 2;

static void  usage(char *this);
static void  crtfile(char *pname);
static char *getfdname(char *pdir, char type, int level, int dir, int file);
static int   mktree(char *pbasedir, int level);

int
main(int argc, char *argv[])
{
	int c, ret;

	while ((c = getopt(argc, argv, "b:l:d:f:")) != -1) {
		switch (c) {
		case 'b':
			pbasedir = optarg;
			break;
		case 'l':
			nlevel = atoi(optarg);
			break;
		case 'd':
			ndir = atoi(optarg);
			break;
		case 'f':
			nfile = atoi(optarg);
			break;
		case '?':
			usage(argv[0]);
		}
	}
	if (nlevel < 0 || ndir < 0 || nfile < 0 || pbasedir == NULL) {
		usage(argv[0]);
	}

	ret = mktree(pbasedir, 1);

	return (ret);
}

static void
usage(char *this)
{
	(void) fprintf(stderr,
	    "\tUsage: %s -b <base_dir> -l [nlevel] -d [ndir] -f [nfile]\n",
	    this);
	exit(1);
}

static int
mktree(char *pdir, int level)
{
	int d, f;
	char dname[MAXPATHLEN] = {0};
	char fname[MAXPATHLEN] = {0};

	if (level > nlevel) {
		return (1);
	}

	for (d = 0; d < ndir; d++) {
		(void) memset(dname, '\0', sizeof (dname));
		(void) strcpy(dname, getfdname(pdir, TYPE_D, level, d, 0));

		if (mkdir(dname, 0777) != 0) {
			(void) fprintf(stderr, "mkdir(%s) failed."
			    "\n[%d]: %s.\n",
			    dname, errno, strerror(errno));
			exit(errno);
		}

		/*
		 * No sub-directory need be created, only create files in it.
		 */
		if (mktree(dname, level+1) != 0) {
			for (f = 0; f < nfile; f++) {
				(void) memset(fname, '\0', sizeof (fname));
				(void) strcpy(fname,
				    getfdname(dname, TYPE_F, level+1, d, f));
				crtfile(fname);
			}
		}
	}

	for (f = 0; f < nfile; f++) {
		(void) memset(fname, '\0', sizeof (fname));
		(void) strcpy(fname, getfdname(pdir, TYPE_F, level, d, f));
		crtfile(fname);
	}

	return (0);
}

static char *
getfdname(char *pdir, char type, int level, int dir, int file)
{
	(void) snprintf(fdname, sizeof (fdname),
	    "%s/%c-l%dd%df%d", pdir, type, level, dir, file);
	return (fdname);
}

static void
crtfile(char *pname)
{
	int fd = -1;
	int afd = -1;
	int i, size;
	char *context = "0123456789ABCDF";
	char *pbuf;

	if (pname == NULL) {
		exit(1);
	}

	size = sizeof (char) * 1024;
	pbuf = (char *)valloc(size);
	for (i = 0; i < size / strlen(context); i++) {
		int offset = i * strlen(context);
		(void) snprintf(pbuf+offset, size-offset, "%s", context);
	}

	if ((fd = open(pname, O_CREAT|O_RDWR, 0777)) < 0) {
		(void) fprintf(stderr, "open(%s, O_CREAT|O_RDWR, 0777) failed."
		    "\n[%d]: %s.\n", pname, errno, strerror(errno));
		exit(errno);
	}
	if (write(fd, pbuf, 1024) < 1024) {
		(void) fprintf(stderr, "write(fd, pbuf, 1024) failed."
		    "\n[%d]: %s.\n", errno, strerror(errno));
		exit(errno);
	}

#if UNSUPPORTED
	if ((afd = openat(fd, "xattr", O_CREAT | O_RDWR | O_XATTR, 0777)) < 0) {
		(void) fprintf(stderr, "openat failed.\n[%d]: %s.\n",
		    errno, strerror(errno));
		exit(errno);
	}
	if (write(afd, pbuf, 1024) < 1024) {
		(void) fprintf(stderr, "write(afd, pbuf, 1024) failed."
		    "\n[%d]: %s.\n", errno, strerror(errno));
		exit(errno);
	}

	(void) close(afd);
#endif
	(void) close(fd);
	free(pbuf);
}
