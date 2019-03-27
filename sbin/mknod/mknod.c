/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kevin Fall.
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
"@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)mknod.c	8.1 (Berkeley) 6/5/93";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>

static void
usage(void)
{

	(void)fprintf(stderr,
	    "usage: mknod name\n"
	    "       mknod name [b | c] major minor [owner:group]\n");
	exit(1);
}

static u_long
id(const char *name, const char *type)
{
	u_long val;
	char *ep;

	/*
	 * XXX
	 * We know that uid_t's and gid_t's are unsigned longs.
	 */
	errno = 0;
	val = strtoul(name, &ep, 10);
	if (errno)
		err(1, "%s", name);
	if (*ep != '\0')
		errx(1, "%s: illegal %s name", name, type);
	return (val);
}

static gid_t
a_gid(const char *s)
{
	struct group *gr;

	if (*s == '\0')			/* Argument was "uid[:.]". */
		errx(1, "group must be specified when the owner is");
	return ((gr = getgrnam(s)) == NULL) ? id(s, "group") : gr->gr_gid;
}

static uid_t
a_uid(const char *s)
{
	struct passwd *pw;

	if (*s == '\0')			/* Argument was "[:.]gid". */
		errx(1, "owner must be specified when the group is");
	return ((pw = getpwnam(s)) == NULL) ? id(s, "user") : pw->pw_uid;
}

int
main(int argc, char **argv)
{
	int range_error;
	uid_t uid;
	gid_t gid;
	mode_t mode;
	dev_t dev;
	char *cp, *endp;
	long mymajor, myminor;

	if (argc != 2 && argc != 5 && argc != 6)
		usage();

	if (argc >= 5) {
		mode = 0666;
		if (argv[2][0] == 'c')
			mode |= S_IFCHR;
		else if (argv[2][0] == 'b')
			mode |= S_IFBLK;
		else
			errx(1, "node must be type 'b' or 'c'");

		errno = 0;
		mymajor = (long)strtoul(argv[3], &endp, 0);
		if (endp == argv[3] || *endp != '\0')
			errx(1, "%s: non-numeric major number", argv[3]);
		range_error = errno;
		errno = 0;
		myminor = (long)strtoul(argv[4], &endp, 0);
		if (endp == argv[4] || *endp != '\0')
			errx(1, "%s: non-numeric minor number", argv[4]);
		range_error |= errno;
		dev = makedev(mymajor, myminor);
		if (range_error || major(dev) != mymajor ||
		    (long)(u_int)minor(dev) != myminor)
			errx(1, "major or minor number too large");
	} else {
		mode = 0666 | S_IFCHR;
		dev = 0;
	}

	uid = gid = -1;
	if (6 == argc) {
	    	/* have owner:group */
		if ((cp = strchr(argv[5], ':')) != NULL) {
			*cp++ = '\0';
			gid = a_gid(cp);
		} else
		usage();
		uid = a_uid(argv[5]);
	}

	if (mknod(argv[1], mode, dev) != 0)
		err(1, "%s", argv[1]);
	if (6 == argc)
		if (chown(argv[1], uid, gid))
			err(1, "setting ownership on %s", argv[1]);
	exit(0);
}
