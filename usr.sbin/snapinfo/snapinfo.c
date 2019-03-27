/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Mark Santcroos <marks@freebsd.org>
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
 *
 * $FreeBSD$
 *
 */

#include <sys/param.h>
#include <sys/mount.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <errno.h>
#include <ftw.h>
#include <libufs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void	find_inum(char *path);
static void	usage(void);
static int	compare_function(const char *, const struct stat *,
		    int, struct FTW *);
static int	find_snapshot(struct statfs *sfs);

static int	verbose;
static int	cont_search;
static uint32_t	inode;

int
main(int argc, char **argv)
{
	char *path;
	struct stat st;
	struct statfs *mntbuf;
	int all = 0, ch, done = 0, fscount, n;

	while ((ch = getopt(argc, argv, "adv")) != -1) {
		switch (ch) {
		case 'a':
			all++;
			break;
		case 'd':
			/* continue to search when matching inode is found 
			 * this feature is not documented */
			cont_search++;
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if ((all == 0 && argc != 1) || (all == 1 && argc > 0))
		usage();

	if (!all) {
		char resolved[PATH_MAX];

		path = *argv;
		/*
		 * mount(8) use realpath(3) before mounting file system,
		 * so let's do the same with the given path.
		 */
		if (realpath(path, resolved) == NULL ||	/* can create full path */
		    stat(resolved, &st) == -1 ||	/* is it stat'able */
		    !S_ISDIR(st.st_mode)) {		/* is it a directory */
			usage();
		}
		path = resolved;
	}

	fscount = getmntinfo(&mntbuf, MNT_WAIT);
	for (n = 0; n < fscount; n++) {
		if (!strncmp(mntbuf[n].f_fstypename, "ufs", 3)) {
			if (all || strcmp(path, mntbuf[n].f_mntonname) == 0) {
				find_snapshot(&mntbuf[n]);
				done++;
			}
		}
	}

	if (done == 0)
		usage();

	return (0);
}

static int
find_snapshot(struct statfs *sfs)
{
	struct uufsd disk;
	int j, snapcount = 0;

	if (ufs_disk_fillout(&disk, sfs->f_mntfromname) == -1)
		perror("ufs_disk_fillout");

	if (verbose)
		printf("%s mounted on %s\n", disk.d_name, disk.d_fs.fs_fsmnt);

	for (j = 0; j < FSMAXSNAP; j++) {
		if (disk.d_fs.fs_snapinum[j]) {
			inode = disk.d_fs.fs_snapinum[j];
			find_inum(sfs->f_mntonname);
			snapcount++;
		}
	}

	if (!snapcount && verbose)
		printf("\tno snapshots found\n");
				
	return 0;
}

static int
compare_function(const char *path, const struct stat *st, int flags,
    struct FTW * ftwv __unused)
{

	if (flags == FTW_F && st->st_ino == inode) {
		if (verbose)
			printf("\tsnapshot ");
		printf("%s", path);
		if (verbose)
			printf(" (inode %ju)", (uintmax_t)st->st_ino);
		printf("\n");
		if (!cont_search)
			return (EEXIST);
	}

	return (0);
}

static void
find_inum(char *path)
{
	int ret;

	ret = nftw(path, compare_function, 1, FTW_PHYS|FTW_MOUNT);
	if (ret != EEXIST && ret != 0) {
		perror("ftw");
		exit(ret);
	}
}

static void
usage(void)
{

	printf("usage: snapinfo [-v] -a\n");
	printf("       snapinfo [-v] mountpoint\n");
	exit(1);
}
