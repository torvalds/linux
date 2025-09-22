/*	$OpenBSD: mount_msdos.c,v 1.36 2022/12/04 23:50:47 cheloha Exp $	*/
/*	$NetBSD: mount_msdos.c,v 1.16 1996/10/24 00:12:50 cgd Exp $	*/

/*
 * Copyright (c) 1994 Christopher G. Demetriou
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <ctype.h>
#include <err.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#include "mntopts.h"

const struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_UPDATE,
	MOPT_ASYNC,
	{ NULL }
};

gid_t	a_gid(char *);
uid_t	a_uid(char *);
mode_t	a_mask(char *);
void	usage(void);

int
main(int argc, char **argv)
{
	struct msdosfs_args args;
	struct stat sb;
	int c, mntflags, set_gid, set_uid, set_mask;
	char *dev, dir[PATH_MAX];
	char *errcause;

	mntflags = set_gid = set_uid = set_mask = 0;
	(void)memset(&args, '\0', sizeof(args));

	while ((c = getopt(argc, argv, "sl9u:g:m:o:")) != -1) {
		switch (c) {
		case 's':
			args.flags |= MSDOSFSMNT_SHORTNAME;
			break;
		case 'l':
			args.flags |= MSDOSFSMNT_LONGNAME;
			break;
		case '9':
			args.flags |= MSDOSFSMNT_NOWIN95;
			break;
		case 'u':
			args.uid = a_uid(optarg);
			set_uid = 1;
			break;
		case 'g':
			args.gid = a_gid(optarg);
			set_gid = 1;
			break;
		case 'm':
			args.mask = a_mask(optarg);
			set_mask = 1;
			break;
		case 'o':
			getmntopts(optarg, mopts, &mntflags);
			break;
		default:
			usage();
			break;
		}
	}

	if (optind + 2 != argc)
		usage();

	dev = argv[optind];
	if (realpath(argv[optind + 1], dir) == NULL)
		err(1, "realpath %s", argv[optind + 1]);

	args.fspec = dev;
	args.export_info.ex_root = -2;	/* unchecked anyway on DOS fs */
	if (mntflags & MNT_RDONLY)
		args.export_info.ex_flags = MNT_EXRDONLY;
	else
		args.export_info.ex_flags = 0;
	if (!set_gid || !set_uid || !set_mask) {
		if (stat(dir, &sb) == -1)
			err(1, "stat %s", dir);

		if (!set_uid)
			args.uid = sb.st_uid;
		if (!set_gid)
			args.gid = sb.st_gid;
		if (!set_mask)
			args.mask = sb.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
	}

	if (mount(MOUNT_MSDOS, dir, mntflags, &args) == -1) {
		switch (errno) {
		case EOPNOTSUPP:
			errcause = "filesystem not supported by kernel";
			break;
		case EMFILE:
			errcause = "mount table full";
			break;
		case EINVAL:
			errcause =
			    "not an MSDOS filesystem";
			break;
		default:
			errcause = strerror(errno);
			break;
		}
		errx(1, "%s on %s: %s", args.fspec, dir, errcause);
	}

	exit (0);
}

gid_t
a_gid(char *s)
{
	struct group *gr;
	const char *errstr;
	gid_t gid;

	if ((gr = getgrnam(s)) != NULL)
		return gr->gr_gid;
	gid = strtonum(s, 0, GID_MAX, &errstr);
	if (errstr)
		errx(1, "group is %s: %s", errstr, s);
	return (gid);
}

uid_t
a_uid(char *s)
{
	struct passwd *pw;
	const char *errstr;
	uid_t uid;

	if ((pw = getpwnam(s)) != NULL)
		return pw->pw_uid;
	uid = strtonum(s, 0, UID_MAX, &errstr);
	if (errstr)
		errx(1, "user is %s: %s", errstr, s);
	return (uid);
}

mode_t
a_mask(char *s)
{
	int done, rv;
	char *ep;

	done = 0;
	if (*s >= '0' && *s <= '7') {
		done = 1;
		rv = strtol(optarg, &ep, 8);
	}
	if (!done || rv < 0 || *ep)
		errx(1, "invalid file mode: %s", s);
	return (rv);
}

void
usage(void)
{

	fprintf(stderr,
	    "usage: mount_msdos [-9ls] [-g group] [-m mask] [-o options] [-u user] special node\n");
	exit(1);
}
