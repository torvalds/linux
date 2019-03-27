/*	$OpenBSD: fsirand.c,v 1.9 1997/02/28 00:46:33 millert Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Todd C. Miller <Todd.Miller@courtesan.com>
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
 *	This product includes software developed by Todd C. Miller.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/resource.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libufs.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void usage(void) __dead2;
int fsirand(char *);

static int printonly = 0, force = 0, ignorelabel = 0;

int
main(int argc, char *argv[])
{
	int n, ex = 0;
	struct rlimit rl;

	while ((n = getopt(argc, argv, "bfp")) != -1) {
		switch (n) {
		case 'b':
			ignorelabel++;
			break;
		case 'p':
			printonly++;
			break;
		case 'f':
			force++;
			break;
		default:
			usage();
		}
	}
	if (argc - optind < 1)
		usage();

	srandomdev();

	/* Increase our data size to the max */
	if (getrlimit(RLIMIT_DATA, &rl) == 0) {
		rl.rlim_cur = rl.rlim_max;
		if (setrlimit(RLIMIT_DATA, &rl) < 0)
			warn("can't get resource limit to max data size");
	} else
		warn("can't get resource limit for data size");

	for (n = optind; n < argc; n++) {
		if (argc - optind != 1)
			(void)puts(argv[n]);
		ex += fsirand(argv[n]);
		if (n < argc - 1)
			putchar('\n');
	}

	exit(ex);
}

int
fsirand(char *device)
{
	struct ufs1_dinode *dp1;
	struct ufs2_dinode *dp2;
	caddr_t inodebuf;
	ssize_t ibufsize;
	struct fs *sblock;
	ino_t inumber;
	ufs2_daddr_t dblk;
	int devfd, n, cg, ret;
	u_int32_t bsize = DEV_BSIZE;

	if ((devfd = open(device, printonly ? O_RDONLY : O_RDWR)) < 0) {
		warn("can't open %s", device);
		return (1);
	}

	dp1 = NULL;
	dp2 = NULL;

	/* Read in master superblock */
	if ((ret = sbget(devfd, &sblock, STDSB)) != 0) {
		switch (ret) {
		case ENOENT:
			warn("Cannot find file system superblock");
			return (1);
		default:
			warn("Unable to read file system superblock");
			return (1);
		}
	}

	if (sblock->fs_magic == FS_UFS1_MAGIC &&
	    sblock->fs_old_inodefmt < FS_44INODEFMT) {
		warnx("file system format is too old, sorry");
		return (1);
	}
	if (!force && !printonly && sblock->fs_clean != 1) {
		warnx("file system is not clean, fsck %s first", device);
		return (1);
	}

	/* XXX - should really cap buffer at 512kb or so */
	if (sblock->fs_magic == FS_UFS1_MAGIC)
		ibufsize = sizeof(struct ufs1_dinode) * sblock->fs_ipg;
	else
		ibufsize = sizeof(struct ufs2_dinode) * sblock->fs_ipg;
	if ((inodebuf = malloc(ibufsize)) == NULL)
		errx(1, "can't allocate memory for inode buffer");

	if (printonly && (sblock->fs_id[0] || sblock->fs_id[1])) {
		if (sblock->fs_id[0])
			(void)printf("%s was randomized on %s", device,
			    ctime((void *)&(sblock->fs_id[0])));
		(void)printf("fsid: %x %x\n", sblock->fs_id[0],
			    sblock->fs_id[1]);
	}

	/* Randomize fs_id unless old 4.2BSD file system */
	if (!printonly) {
		/* Randomize fs_id and write out new sblock and backups */
		sblock->fs_id[0] = (u_int32_t)time(NULL);
		sblock->fs_id[1] = random();
		if (sbput(devfd, sblock, sblock->fs_ncg) != 0) {
			warn("could not write updated superblock");
			return (1);
		}
	}

	/* For each cylinder group, randomize inodes and update backup sblock */
	for (cg = 0, inumber = UFS_ROOTINO; cg < (int)sblock->fs_ncg; cg++) {
		/* Read in inodes, then print or randomize generation nums */
		dblk = fsbtodb(sblock, ino_to_fsba(sblock, inumber));
		if (lseek(devfd, (off_t)dblk * bsize, SEEK_SET) < 0) {
			warn("can't seek to %jd", (intmax_t)dblk * bsize);
			return (1);
		} else if ((n = read(devfd, inodebuf, ibufsize)) != ibufsize) {
			warnx("can't read inodes: %s",
			     (n < ibufsize) ? "short read" : strerror(errno));
			return (1);
		}

		dp1 = (struct ufs1_dinode *)(void *)inodebuf;
		dp2 = (struct ufs2_dinode *)(void *)inodebuf;
		for (n = cg > 0 ? 0 : UFS_ROOTINO;
		     n < (int)sblock->fs_ipg;
		     n++, inumber++) {
			if (printonly) {
				(void)printf("ino %ju gen %08x\n",
				    (uintmax_t)inumber,
				    sblock->fs_magic == FS_UFS1_MAGIC ?
				    dp1->di_gen : dp2->di_gen);
			} else if (sblock->fs_magic == FS_UFS1_MAGIC) {
				dp1->di_gen = arc4random(); 
				dp1++;
			} else {
				dp2->di_gen = arc4random();
				ffs_update_dinode_ckhash(sblock, dp2);
				dp2++;
			}
		}

		/* Write out modified inodes */
		if (!printonly) {
			if (lseek(devfd, (off_t)dblk * bsize, SEEK_SET) < 0) {
				warn("can't seek to %jd",
				    (intmax_t)dblk * bsize);
				return (1);
			} else if ((n = write(devfd, inodebuf, ibufsize)) !=
				 ibufsize) {
				warnx("can't write inodes: %s",
				     (n != ibufsize) ? "short write" :
				     strerror(errno));
				return (1);
			}
		}
	}
	(void)close(devfd);

	return(0);
}

static void
usage(void)
{
	(void)fprintf(stderr, 
		"usage: fsirand [-b] [-f] [-p] special [special ...]\n");
	exit(1);
}
