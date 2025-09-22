/*	$OpenBSD: mount_ffs.c,v 1.26 2022/12/04 23:50:46 cheloha Exp $	*/
/*	$NetBSD: mount_ffs.c,v 1.3 1996/04/13 01:31:19 jtc Exp $	*/

/*-
 * Copyright (c) 1993, 1994
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
#include <sys/mount.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "mntopts.h"

void	ffs_usage(void);

static const struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_WXALLOWED,
	MOPT_NOPERM,
	MOPT_ASYNC,
	MOPT_SYNC,
	MOPT_UPDATE,
	MOPT_RELOAD,
	MOPT_FORCE,
	MOPT_SOFTDEP,
	{ NULL }
};

int
main(int argc, char *argv[])
{
	struct ufs_args args;		/* XXX ffs_args */
	int ch, mntflags;
	char fs_name[PATH_MAX], *errcause;

	mntflags = 0;
	optind = optreset = 1;		/* Reset for parse of new argv. */
	while ((ch = getopt(argc, argv, "o:")) != -1)
		switch (ch) {
		case 'o':
			getmntopts(optarg, mopts, &mntflags);
			break;
		default:
			ffs_usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		ffs_usage();

	args.fspec = argv[0];		/* The name of the device file. */
	if (realpath(argv[1], fs_name) == NULL) 	/* The mount point. */
		err(1, "realpath %s", argv[1]);

#define DEFAULT_ROOTUID	-2
	args.export_info.ex_root = DEFAULT_ROOTUID;
	if (mntflags & MNT_RDONLY)
		args.export_info.ex_flags = MNT_EXRDONLY;
	else
		args.export_info.ex_flags = 0;

	if (mntflags & MNT_NOPERM)
		mntflags |= MNT_NODEV | MNT_NOEXEC;

	if (mount(MOUNT_FFS, fs_name, mntflags, &args) == -1) {
		switch (errno) {
		case EMFILE:
			errcause = "mount table full";
			break;
		case EOPNOTSUPP:
			errcause = "filesystem not supported by kernel";
			break;
		case EROFS:
			errcause =
			    "filesystem must be mounted read-only; you may need to run fsck";
			break;
		default:
			errcause = strerror(errno);
			break;
		}
		errx(1, "%s on %s: %s", args.fspec, fs_name, errcause);
	}
	exit(0);
}

void
ffs_usage(void)
{
	(void)fprintf(stderr, "usage: mount_ffs [-o options] special node\n");
	exit(1);
}
