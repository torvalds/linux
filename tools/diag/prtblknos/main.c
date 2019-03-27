/*
 * Copyright (c) 1998, 2003, 2013, 2018 Marshall Kirk McKusick.
 * All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY MARSHALL KIRK MCKUSICK ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL MARSHALL KIRK MCKUSICK BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <ufs/ffs/fs.h>

#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <libufs.h>

union dinode {
	struct ufs1_dinode *dp1;
	struct ufs2_dinode *dp2;
};

void prtblknos(struct uufsd *disk, union dinode *dp);

int
main(argc, argv)
	int argc;
	char *argv[];
{
	struct uufsd disk;
	union dinodep dp;
	struct fs *fs;
	struct stat sb;
	struct statfs sfb;
	char *xargv[4];
	char ibuf[64];
	char *fsname, *filename;
	ino_t inonum;
	int error;

	filename = NULL;
	if (argc == 2) {
		filename = argv[1];
		if (lstat(filename, &sb) != 0)
			err(1, "stat(%s)", filename);
		if (statfs(filename, &sfb) != 0)
			err(1, "statfs(%s)", filename);
		xargv[0] = argv[0];
		xargv[1] = sfb.f_mntfromname;
		sprintf(ibuf, "%jd", (intmax_t)sb.st_ino);
		xargv[2] = ibuf;
		xargv[3] = NULL;
		argv = xargv;
		argc = 3;
	}
	if (argc < 3) {
		(void)fprintf(stderr, "%s\n%s\n",
		    "usage: prtblknos filename",
		    "       prtblknos filesystem inode ...");
		exit(1);
	}

	fsname = *++argv;

	/* get the superblock. */
	if ((error = ufs_disk_fillout(&disk, fsname)) < 0)
		err(1, "Cannot access file system superblock on %s", fsname);
	fs = (struct fs *)&disk.d_sb;

	/* remaining arguments are inode numbers. */
	while (*++argv) {
		/* get the inode number. */
		if ((inonum = atoi(*argv)) <= 0 ||
		     inonum >= (ino_t)fs->fs_ipg * fs->fs_ncg)
			warnx("%s is not a valid inode number", *argv);
		if (filename == NULL)
			(void)printf("inode #%jd: ", (intmax_t)inonum);
		else
			(void)printf("%s (inode #%jd): ", filename,
			    (intmax_t)inonum);

		if ((error = getinode(&disk, &dp, inonum)) < 0)
			warn("Read of inode %jd on %s failed: %s",
			    (intmax_t)inonum, fsname, disk.d_error);

		prtblknos(&disk, (union dinode *)dp.dp1);
	}
	exit(0);
}
