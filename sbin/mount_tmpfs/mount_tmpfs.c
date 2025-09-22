/*	$OpenBSD: mount_tmpfs.c,v 1.8 2022/12/04 23:50:47 cheloha Exp $	*/
/*	$NetBSD: mount_tmpfs.c,v 1.24 2008/08/05 20:57:45 pooka Exp $	*/

/*
 * Copyright (c) 2005, 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julio M. Merino Vidal, developed as part of Google's Summer of Code
 * 2005 program.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <mntopts.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <util.h>

#include "mount_tmpfs.h"

/* --------------------------------------------------------------------- */

static const struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_WXALLOWED,
	MOPT_UPDATE,
	{ NULL },
};

/* --------------------------------------------------------------------- */

static void	usage(void) __dead;
static uid_t	a_uid(const char *);
static gid_t	a_gid(const char *);
static uid_t	a_gid(const char *);
static int	a_num(const char *, const char *);
static mode_t	a_mask(const char *);
static void	pathadj(const char *, char *);

/* --------------------------------------------------------------------- */

void
mount_tmpfs_parseargs(int argc, char *argv[],
	struct tmpfs_args *args, int *mntflags,
	char *canon_dev, char *canon_dir)
{
	int gidset, modeset, uidset; /* Ought to be 'bool'. */
	int ch;
	gid_t gid;
	uid_t uid;
	mode_t mode;
	int64_t tmpnumber;
	struct stat sb;

	/* Set default values for mount point arguments. */
	memset(args, 0, sizeof(*args));
	args->ta_version = TMPFS_ARGS_VERSION;
	args->ta_size_max = 0;
	args->ta_nodes_max = 0;
	*mntflags = 0;

	gidset = 0; gid = 0;
	uidset = 0; uid = 0;
	modeset = 0; mode = 0;

	optind = optreset = 1;
	while ((ch = getopt(argc, argv, "g:m:n:o:s:u:")) != -1 ) {
		switch (ch) {
		case 'g':
			gid = a_gid(optarg);
			gidset = 1;
			break;

		case 'm':
			mode = a_mask(optarg);
			modeset = 1;
			break;

		case 'n':

			if (scan_scaled(optarg, &tmpnumber) == -1)
				err(EXIT_FAILURE, "failed to parse nodes `%s'",
				    optarg);
			args->ta_nodes_max = tmpnumber;
			break;

		case 'o':
			getmntopts(optarg, mopts, mntflags);
			break;

		case 's':
			if (scan_scaled(optarg, &tmpnumber) == -1)
				err(EXIT_FAILURE, "failed to parse size `%s'",
				    optarg);
			args->ta_size_max = tmpnumber;
			break;

		case 'u':
			uid = a_uid(optarg);
			uidset = 1;
			break;

		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	strlcpy(canon_dev, argv[0], PATH_MAX);
	pathadj(argv[1], canon_dir);

	if (stat(canon_dir, &sb) == -1)
		err(EXIT_FAILURE, "cannot stat `%s'", canon_dir);

	args->ta_root_uid = uidset ? uid : sb.st_uid;
	args->ta_root_gid = gidset ? gid : sb.st_gid;
	args->ta_root_mode = modeset ? mode : sb.st_mode;
}

/* --------------------------------------------------------------------- */

static void
usage(void)
{
	extern char *__progname;
	(void)fprintf(stderr,
	    "usage: %s [-g group] [-m mode] [-n nodes] [-o options] [-s size]\n"
	    "           [-u user] tmpfs mount_point\n", __progname);
	exit(1);
}

/* --------------------------------------------------------------------- */

int
mount_tmpfs(int argc, char *argv[])
{
	struct tmpfs_args args;
	char canon_dev[PATH_MAX], canon_dir[PATH_MAX];
	int mntflags;

	mount_tmpfs_parseargs(argc, argv, &args, &mntflags,
	    canon_dev, canon_dir);

	if (mount(MOUNT_TMPFS, canon_dir, mntflags, &args) == -1)
		err(EXIT_FAILURE, "tmpfs on %s", canon_dir);

	return EXIT_SUCCESS;
}

int
main(int argc, char *argv[])
{

	/* setprogname(argv[0]); */
	return mount_tmpfs(argc, argv);
}

static uid_t
a_uid(const char *s)
{
	struct passwd *pw;

	if ((pw = getpwnam(s)) != NULL)
		return pw->pw_uid;
	return a_num(s, "user");
}

static gid_t
a_gid(const char *s)
{
	struct group *gr;

	if ((gr = getgrnam(s)) != NULL)
		return gr->gr_gid;
	return a_num(s, "group");
}

static int
a_num(const char *s, const char *id_type)
{
	int id;
	char *ep;

	id = strtol(s, &ep, 0);
	if (*ep || s == ep || id < 0)
		errx(1, "unknown %s id: %s", id_type, s);
	return id;
}

static mode_t
a_mask(const char *s)
{
	int rv;
	char *ep;

	rv = strtol(s, &ep, 8);
	if (s == ep || *ep || rv < 0)
		errx(1, "invalid file mode: %s", s);
	return rv;
}

static void
pathadj(const char *input, char *adjusted)
{

	if (realpath(input, adjusted) == NULL)
		err(1, "realpath %s", input);
	if (strncmp(input, adjusted, PATH_MAX)) {
		warnx("\"%s\" is a relative path.", input);
		warnx("using \"%s\" instead.", adjusted);
	}
}
