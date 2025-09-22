/*	$OpenBSD: mount_cd9660.c,v 1.23 2022/12/04 23:50:46 cheloha Exp $	*/
/*	$NetBSD: mount_cd9660.c,v 1.3 1996/04/13 01:31:08 jtc Exp $	*/

/*
 * Copyright (c) 1992, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley
 * by Pace Willisson (pace@blitz.com).  The Rock Ridge Extension
 * Support code is derived from software contributed to Berkeley
 * by Atsushi Murai (amurai@spec.co.jp).
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
#define CD9660
#include <sys/mount.h>

#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "mntopts.h"

const struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_UPDATE,
	{ NULL }
};

void	usage(void);

int
main(int argc, char *argv[])
{
	struct iso_args args;
	int ch, mntflags, opts, sess = 0;
	char *dev, dir[PATH_MAX];
	const char *errstr;

	mntflags = opts = 0;
	while ((ch = getopt(argc, argv, "egjo:Rs:")) != -1)
		switch (ch) {
		case 'e':
			opts |= ISOFSMNT_EXTATT;
			break;
		case 'g':
			opts |= ISOFSMNT_GENS;
			break;
		case 'j':
			opts |= ISOFSMNT_NOJOLIET;
			break;
		case 'o':
			getmntopts(optarg, mopts, &mntflags);
			break;
		case 'R':
			opts |= ISOFSMNT_NORRIP;
			break;
		case 's':
			opts |= ISOFSMNT_SESS;
			sess = strtonum(optarg, 0, INT32_MAX, &errstr);
			if (errstr)
				errx(1, "session number is %s: %s", errstr,
				    optarg);
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	dev = argv[0];
	if (realpath(argv[1], dir) == NULL)
		err(1, "realpath %s", argv[1]);

#define DEFAULT_ROOTUID	-2
	args.fspec = dev;
	args.export_info.ex_root = DEFAULT_ROOTUID;

	mntflags |= MNT_RDONLY;
	if (mntflags & MNT_RDONLY)
		args.export_info.ex_flags = MNT_EXRDONLY;
	else
		args.export_info.ex_flags = 0;
	args.flags = opts;
	args.sess = sess;

	if (mount(MOUNT_CD9660, dir, mntflags, &args) == -1) {
		if (errno == EOPNOTSUPP)
			errx(1, "%s: Filesystem not supported by kernel", dir);
		else
			err(1, "%s on %s", args.fspec, dir);
	}
	exit(0);
}

void
usage(void)
{
	(void)fprintf(stderr,
		"usage: mount_cd9660 [-egjR] [-o options] [-s offset] "
		"special node\n");
	exit(1);
}
