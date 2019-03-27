/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Robert Elz at The University of Melbourne.
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

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1990, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)quotaon.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Turn quota on/off for a filesystem.
 */
#include <sys/param.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <ufs/ufs/quota.h>
#include <err.h>
#include <fstab.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *qfextension[] = INITQFNAMES;

static int	aflag;		/* all filesystems */
static int	gflag;		/* operate on group quotas */
static int	uflag;		/* operate on user quotas */
static int	vflag;		/* verbose */

static int oneof(char *, char *[], int);
static int quotaonoff(struct fstab *fs, int, int);
static void usage(void);

int
main(int argc, char **argv)
{
	struct fstab *fs;
	const char *whoami;
	long argnum, done = 0;
	int ch, i, offmode = 0, errs = 0;

	whoami = getprogname();
	if (strcmp(whoami, "quotaoff") == 0)
		offmode++;
	else if (strcmp(whoami, "quotaon") != 0)
		errx(1, "name must be quotaon or quotaoff");
	while ((ch = getopt(argc, argv, "avug")) != -1) {
		switch(ch) {
		case 'a':
			aflag++;
			break;
		case 'g':
			gflag++;
			break;
		case 'u':
			uflag++;
			break;
		case 'v':
			vflag++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc <= 0 && !aflag)
		usage();
	if (!gflag && !uflag) {
		gflag++;
		uflag++;
	}
	setfsent();
	while ((fs = getfsent()) != NULL) {
		if (strcmp(fs->fs_vfstype, "ufs") ||
		    strcmp(fs->fs_type, FSTAB_RW))
			continue;
		if (aflag) {
			if (gflag)
				errs += quotaonoff(fs, offmode, GRPQUOTA);
			if (uflag)
				errs += quotaonoff(fs, offmode, USRQUOTA);
			continue;
		}
		if ((argnum = oneof(fs->fs_file, argv, argc)) >= 0 ||
		    (argnum = oneof(fs->fs_spec, argv, argc)) >= 0) {
			done |= 1 << argnum;
			if (gflag)
				errs += quotaonoff(fs, offmode, GRPQUOTA);
			if (uflag)
				errs += quotaonoff(fs, offmode, USRQUOTA);
		}
	}
	endfsent();
	for (i = 0; i < argc; i++)
		if ((done & (1 << i)) == 0)
			warnx("%s not found in fstab", argv[i]);
	exit(errs);
}

static void
usage(void)
{

	fprintf(stderr, "%s\n%s\n%s\n%s\n",
		"usage: quotaon [-g] [-u] [-v] -a",
		"       quotaon [-g] [-u] [-v] filesystem ...",
		"       quotaoff [-g] [-u] [-v] -a",
		"       quotaoff [-g] [-u] [-v] filesystem ...");
	exit(1);
}

static int
quotaonoff(struct fstab *fs, int offmode, int type)
{
	struct quotafile *qf;

	if ((qf = quota_open(fs, type, O_RDONLY)) == NULL)
		return (0);
	if (offmode) {
		if (quota_off(qf) != 0) {
			warn("%s", quota_fsname(qf));
			return (1);
		}
		if (vflag)
			printf("%s: quotas turned off\n", quota_fsname(qf));
		quota_close(qf);
		return(0);
	}
	if (quota_on(qf) != 0) {
		warn("using %s on %s", quota_qfname(qf), quota_fsname(qf));
		return (1);
	}
	if (vflag)
		printf("%s: %s quotas turned on with data file %s\n", 
		    quota_fsname(qf), qfextension[type], quota_qfname(qf));
	quota_close(qf);
	return(0);
}

/*
 * Check to see if target appears in list of size cnt.
 */
static int
oneof(char *target, char *list[], int cnt)
{
	int i;

	for (i = 0; i < cnt; i++)
		if (strcmp(target, list[i]) == 0)
			return (i);
	return (-1);
}
