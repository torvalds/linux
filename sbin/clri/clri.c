/*	$OpenBSD: clri.c,v 1.22 2020/06/24 05:46:07 otto Exp $	*/
/*	$NetBSD: clri.c,v 1.19 2005/01/20 15:50:47 xtraeme Exp $	*/

/*
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

#include <sys/param.h>	/* MAXBSIZE DEV_BSIZE */

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <util.h>

/*
 * Possible superblock locations ordered from most to least likely.
 */
static int sblock_try[] = SBLOCKSEARCH;

static void
usage(void)
{
	(void)fprintf(stderr, "usage: clri special_device inode_number ...\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct ufs1_dinode *dp1;
	struct ufs2_dinode *dp2;
	struct fs *sbp;
	char *ibuf[MAXBSIZE];
	char *fs, sblock[SBLOCKSIZE];
	size_t bsize;
	off_t offset;
	int i, fd;
	ino_t imax, inonum;

	if (argc < 3)
		usage();

	fs = *++argv;
	sbp = NULL;

	/* get the superblock. */
	if ((fd = opendev(fs, O_RDWR, 0, NULL)) == -1)
		err(1, "%s", fs);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	for (i = 0; sblock_try[i] != -1; i++) {
		offset = (off_t)(sblock_try[i]);
		if (pread(fd, sblock, sizeof(sblock), offset) != sizeof(sblock))
			err(1, "%s: can't read superblock", fs);
		sbp = (struct fs *)sblock;
		if ((sbp->fs_magic == FS_UFS1_MAGIC ||
		     (sbp->fs_magic == FS_UFS2_MAGIC &&
		      sbp->fs_sblockloc == sblock_try[i])) &&
		    sbp->fs_bsize <= MAXBSIZE &&
		    sbp->fs_bsize >= (int)sizeof(struct fs))
			break;
	}
	if (sblock_try[i] == -1)
		errx(2, "cannot find file system superblock");
	bsize = sbp->fs_bsize;

	/* check that inode numbers are valid */
	imax = sbp->fs_ncg * sbp->fs_ipg;
	for (i = 1; i < (argc - 1); i++) {
		const char *errstr;
		strtonum(argv[i], 1, imax, &errstr);
		if (errstr)
			errx(1, "%s is not a valid inode number: %s", argv[i], errstr);
	}

	/* clear the clean flag in the superblock */
	if (sbp->fs_inodefmt >= FS_44INODEFMT) {
		sbp->fs_clean = 0;
		if (pwrite(fd, sblock, sizeof(sblock), offset) != sizeof(sblock))
			err(1, "%s: can't update superblock", fs);
		(void)fsync(fd);
	}

	/* remaining arguments are inode numbers. */
	while (*++argv) {
		/* get the inode number. */
		inonum = strtonum(*argv, 1, imax, NULL);
		(void)printf("clearing %llu\n", inonum);

		/* read in the appropriate block. */
		offset = ino_to_fsba(sbp, inonum);	/* inode to fs blk */
		offset = fsbtodb(sbp, offset);		/* fs blk disk blk */
		offset *= DEV_BSIZE;			/* disk blk to bytes */

		/* seek and read the block */
		if (pread(fd, ibuf, bsize, offset) != bsize)
			err(1, "%s", fs);

		if (sbp->fs_magic == FS_UFS2_MAGIC) {
			/* get the inode within the block. */
			dp2 = &(((struct ufs2_dinode *)ibuf)
			    [ino_to_fsbo(sbp, inonum)]);

			/* clear the inode, and bump the generation count. */
			memset(dp2, 0, sizeof(*dp2));
			dp2->di_gen = arc4random();
		} else {
			/* get the inode within the block. */
			dp1 = &(((struct ufs1_dinode *)ibuf)
			    [ino_to_fsbo(sbp, inonum)]);

			/* clear the inode, and bump the generation count. */
			memset(dp1, 0, sizeof(*dp1));
			dp1->di_gen = arc4random();
		}

		/* backup and write the block */
		if (pwrite(fd, ibuf, bsize, offset) != bsize)
			err(1, "%s", fs);
		(void)fsync(fd);
	}
	(void)close(fd);
	exit(0);
}
