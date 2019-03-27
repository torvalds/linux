/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.
 * Copyright (c) 2005, 2006 Masanori Ozawa <ozawa@ongs.co.jp>, ONGS Inc.
 * Copyright (c) 2006 Daichi Goto <daichi@freebsd.org>
 * All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1992, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)mount_union.c	8.5 (Berkeley) 3/27/94";
#else
static const char rcsid[] =
  "$FreeBSD$";
#endif
#endif /* not lint */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include <sys/errno.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>

#include "mntopts.h"

static int 
subdir(const char *p, const char *dir)
{
	int		l;

	l = strlen(dir);
	if (l <= 1)
		return (1);

	if ((strncmp(p, dir, l) == 0) && (p[l] == '/' || p[l] == '\0'))
		return (1);

	return (0);
}

static void 
usage(void)
{
	(void)fprintf(stderr,
	    "usage: mount_unionfs [-o options] directory uniondir\n");
	exit(EX_USAGE);
}

static void
parse_gid(const char *s, char *buf, size_t bufsize)
{
	struct group *gr;
	char *inval;

	if ((gr = getgrnam(s)) != NULL)
		snprintf(buf, bufsize, "%d", gr->gr_gid);
	else {
		strtol(s, &inval, 10);
		if (*inval != 0) {
                       errx(EX_NOUSER, "unknown group id: %s", s);
                       usage();
		} else {
			strncpy(buf, s, bufsize);
		}
	}
}

static void
parse_uid(const char *s, char *buf, size_t bufsize)
{
	struct passwd  *pw;
	char *inval;

	if ((pw = getpwnam(s)) != NULL)
		snprintf(buf, bufsize, "%d", pw->pw_uid);
	else {
		strtol(s, &inval, 10);
		if (*inval != 0) {
                       errx(EX_NOUSER, "unknown user id: %s", s);
                       usage();
		} else {
			strncpy(buf, s, bufsize);
		}
	}
}

int 
main(int argc, char *argv[])
{
	struct iovec	*iov;
	int ch, iovlen;
	char source [MAXPATHLEN], target[MAXPATHLEN], errmsg[255];
	char uid_str[20], gid_str[20];
	char fstype[] = "unionfs";
	char *p, *val;

	iov = NULL;
	iovlen = 0;
	memset(errmsg, 0, sizeof(errmsg));

	while ((ch = getopt(argc, argv, "bo:")) != -1) {
		switch (ch) {
		case 'b':
			printf("\n  -b is deprecated.  Use \"-o below\" instead\n");
			build_iovec(&iov, &iovlen, "below", NULL, 0);
			break;
		case 'o':
                        p = strchr(optarg, '=');
                        val = NULL;
                        if (p != NULL) {
                                *p = '\0';
                                val = p + 1;
				if (strcmp(optarg, "gid") == 0) {
					parse_gid(val, gid_str, sizeof(gid_str));
					val = gid_str;
				}
				else if (strcmp(optarg, "uid") == 0) {
					parse_uid(val, uid_str, sizeof(uid_str));
					val = uid_str;
				}
                        }
                        build_iovec(&iov, &iovlen, optarg, val, (size_t)-1);
			break;
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	/* resolve both target and source with realpath(3) */
	if (checkpath(argv[0], target) != 0)
		err(EX_USAGE, "%s", target);
	if (checkpath(argv[1], source) != 0)
		err(EX_USAGE, "%s", source);

	if (subdir(target, source) || subdir(source, target))
		errx(EX_USAGE, "%s (%s) and %s (%s) are not distinct paths",
		     argv[0], target, argv[1], source);

	build_iovec(&iov, &iovlen, "fstype", fstype, (size_t)-1);
	build_iovec(&iov, &iovlen, "fspath", source, (size_t)-1);
	build_iovec(&iov, &iovlen, "from", target, (size_t)-1);
	build_iovec(&iov, &iovlen, "errmsg", errmsg, sizeof(errmsg));

	if (nmount(iov, iovlen, 0))
		err(EX_OSERR, "%s: %s", source, errmsg);
	exit(0);
}
