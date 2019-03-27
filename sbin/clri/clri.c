/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rich $alz of BBN Inc.
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
"@(#) Copyright (c) 1990, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)clri.c	8.2 (Berkeley) 9/23/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/disklabel.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libufs.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

static void
usage(void)
{
	(void)fprintf(stderr, "usage: clri special_device inode_number ...\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	union dinodep dp;
	struct uufsd disk;
	long generation;
	int inonum, exitval;
	char *fsname;

	if (argc < 3)
		usage();

	/* get the superblock. */
	fsname = *++argv;
	if (ufs_disk_fillout(&disk, fsname) == -1) {
		printf("loading superblock: %s\n", disk.d_error);
		exit (1);
	}

	/* remaining arguments are inode numbers. */
	exitval = 0;
	while (*++argv) {
		/* get the inode number. */
		if ((inonum = atoi(*argv)) < UFS_ROOTINO) {
			printf("%s is not a valid inode number", *argv);
			exitval = 1;
			continue;
		}
		(void)printf("clearing %d\n", inonum);

		if (getinode(&disk, &dp, inonum) == -1) {
			printf("getinode: %s\n", disk.d_error);
			exitval = 1;
			continue;
		}
		/* clear the inode, and bump the generation count. */
		if (disk.d_fs.fs_magic == FS_UFS1_MAGIC) {
			generation = dp.dp1->di_gen + 1;
			memset(dp.dp1, 0, sizeof(*dp.dp1));
			dp.dp1->di_gen = generation;
		} else {
			generation = dp.dp2->di_gen + 1;
			memset(dp.dp2, 0, sizeof(*dp.dp2));
			dp.dp2->di_gen = generation;
		}
		putinode(&disk);
		(void)fsync(disk.d_fd);
	}
	(void)ufs_disk_close(&disk);
	exit(exitval);
}
