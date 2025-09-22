/*	$OpenBSD: main.c,v 1.30 2023/03/08 04:43:06 guenther Exp $	*/
/*	$NetBSD: main.c,v 1.1 1997/06/11 11:21:50 bouyer Exp $	*/

/*
 * Copyright (c) 1997 Manuel Bouyer.
 * Copyright (c) 1980, 1986, 1993
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
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <ufs/ext2fs/ext2fs_dinode.h>
#include <ufs/ext2fs/ext2fs.h>
#include <fstab.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <err.h>

#include "fsck.h"
#include "extern.h"
#include "fsutil.h"

volatile sig_atomic_t	returntosingle;

int	main(int, char *[]);

static int	argtoi(int, char *, char *, int);
static int	checkfilesys(char *, char *, long, int);
static  void usage(void);

struct bufarea bufhead;		/* head of list of other blks in filesys */
struct bufarea sblk;		/* file system superblock */
struct bufarea asblk;		/* first alternate superblock */
struct bufarea *pdirbp;		/* current directory contents */
struct bufarea *pbp;		/* current inode block */
struct bufarea *getdatablk(daddr32_t, long);
struct m_ext2fs sblock;

struct dups *duplist;		/* head of dup list */
struct dups *muldup;		/* end of unique duplicate dup block numbers */

struct zlncnt *zlnhead;		/* head of zero link count list */

struct inoinfo **inphead, **inpsort;
long numdirs, listmax, inplast;

long	secsize;		/* actual disk sector size */
char	nflag;			/* assume a no response */
char	yflag;			/* assume a yes response */
int	bflag;			/* location of alternate super block */
int	debug;			/* output debugging info */
int	preen;			/* just fix normal inconsistencies */
char	havesb;			/* superblock has been read */
char	skipclean;		/* skip clean file systems if preening */
int	fsmodified;		/* 1 => write done to file system */
int	fsreadfd;		/* file descriptor for reading file system */
int	fswritefd;		/* file descriptor for writing file system */
int	rerun;			/* rerun fsck.  Only used in non-preen mode */

daddr32_t	maxfsblock;		/* number of blocks in the file system */
char	*blockmap;		/* ptr to primary blk allocation map */
ino_t	maxino;			/* number of inodes in file system */
ino_t	lastino;		/* last inode in use */
char	*statemap;		/* ptr to inode state table */
u_char	*typemap;		/* ptr to inode type table */
int16_t	*lncntp;		/* ptr to link count table */

ino_t	lfdir;			/* lost & found directory inode number */

daddr32_t	n_blks;			/* number of blocks in use */
daddr32_t	n_files;		/* number of files in use */

struct	ext2fs_dinode zino;

int
main(int argc, char *argv[])
{
	int ch;
	int ret = 0;

	checkroot();

	sync();
	skipclean = 1;
	while ((ch = getopt(argc, argv, "b:dfm:npy")) != -1) {
		switch (ch) {
		case 'b':
			skipclean = 0;
			bflag = argtoi('b', "number", optarg, 10);
			printf("Alternate super block location: %d\n", bflag);
			break;

		case 'd':
			debug = 1;
			break;

		case 'f':
			skipclean = 0;
			break;

		case 'm':
			lfmode = argtoi('m', "mode", optarg, 8);
			if (lfmode &~ 07777)
				errexit("bad mode to -m: %o\n", lfmode);
			printf("** lost+found creation mode %o\n", lfmode);
			break;

		case 'n':
			nflag = 1;
			yflag = 0;
			break;

		case 'p':
			preen = 1;
			break;

		case 'y':
			yflag = 1;
			nflag = 0;
			break;

		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		(void)signal(SIGINT, catch);
	if (preen)
		(void)signal(SIGQUIT, catchquit);

	(void)checkfilesys(blockcheck(*argv), 0, 0L, 0);

	if (returntosingle)
		ret = 2;

	exit(ret);
}

static int
argtoi(int flag, char *req, char *str, int base)
{
	char *cp;
	int ret;

	ret = (int)strtol(str, &cp, base);
	if (cp == str || *cp)
		errexit("-%c flag requires a %s\n", flag, req);
	return (ret);
}

/*
 * Check the specified filesystem.
 */
static int
checkfilesys(char *filesys, char *mntpt, long auxdata, int child)
{
	daddr32_t n_bfree;
	struct dups *dp;
	struct zlncnt *zlnp;
	int i;

	if (preen && child)
		(void)signal(SIGQUIT, voidquit);
	setcdevname(filesys, NULL, preen);
	if (debug && preen)
		pwarn("starting\n");

	switch (setup(filesys)) {
	case 0:
		if (preen)
			pfatal("CAN'T CHECK FILE SYSTEM.");
	case -1:
		return (0);
	}
	/*
	 * 1: scan inodes tallying blocks used
	 */
	if (preen == 0) {
		if (sblock.e2fs.e2fs_rev > E2FS_REV0) {
			printf("** Last Mounted on %s\n",
			    sblock.e2fs.e2fs_fsmnt);
		}
		if (hotroot())
			printf("** Root file system\n");
		printf("** Phase 1 - Check Blocks and Sizes\n");
	}
	pass1();

	/*
	 * 1b: locate first references to duplicates, if any
	 */
	if (duplist) {
		if (preen)
			pfatal("INTERNAL ERROR: dups with -p");
		printf("** Phase 1b - Rescan For More DUPS\n");
		pass1b();
	}

	/*
	 * 2: traverse directories from root to mark all connected directories
	 */
	if (preen == 0)
		printf("** Phase 2 - Check Pathnames\n");
	pass2();

	/*
	 * 3: scan inodes looking for disconnected directories
	 */
	if (preen == 0)
		printf("** Phase 3 - Check Connectivity\n");
	pass3();

	/*
	 * 4: scan inodes looking for disconnected files; check reference counts
	 */
	if (preen == 0)
		printf("** Phase 4 - Check Reference Counts\n");
	pass4();

	/*
	 * 5: check and repair resource counts in cylinder groups
	 */
	if (preen == 0)
		printf("** Phase 5 - Check Cyl groups\n");
	pass5();

	/*
	 * print out summary statistics
	 */
	n_bfree = sblock.e2fs.e2fs_fbcount;

	pwarn("%d files, %d used, %d free\n",
	    n_files, n_blks, n_bfree);
	if (debug &&
		/* 9 reserved and unused inodes in FS */
	    (n_files -= maxino - 9 - sblock.e2fs.e2fs_ficount))
		printf("%d files missing\n", n_files);
	if (debug) {
		for (i = 0; i < sblock.e2fs_ncg; i++)
			n_blks +=  cgoverhead(i);
		n_blks += sblock.e2fs.e2fs_first_dblock;
		if (n_blks -= maxfsblock - n_bfree)
			printf("%d blocks missing\n", n_blks);
		if (duplist != NULL) {
			printf("The following duplicate blocks remain:");
			for (dp = duplist; dp; dp = dp->next)
				printf(" %d,", dp->dup);
			printf("\n");
		}
		if (zlnhead != NULL) {
			printf("The following zero link count inodes remain:");
			for (zlnp = zlnhead; zlnp; zlnp = zlnp->next)
				printf(" %llu,",
				    (unsigned long long)zlnp->zlncnt);
			printf("\n");
		}
	}
	zlnhead = NULL;
	duplist = NULL;
	muldup = NULL;
	inocleanup();
	if (fsmodified) {
		time_t t;
		(void)time(&t);
		sblock.e2fs.e2fs_wtime = t;
		sblock.e2fs.e2fs_lastfsck = t;
		sbdirty();
	}
	ckfini(1);
	free(blockmap);
	free(statemap);
	free((char *)lncntp);
	if (!fsmodified)
		return (0);
	if (!preen)
		printf("\n***** FILE SYSTEM WAS MODIFIED *****\n");
	if (rerun)
		printf("\n***** PLEASE RERUN FSCK *****\n");
	if (hotroot()) {
		struct statfs stfs_buf;
		/*
		 * We modified the root.  Do a mount update on
		 * it, unless it is read-write, so we can continue.
		 */
		if (statfs("/", &stfs_buf) == 0) {
			long flags = stfs_buf.f_flags;
			struct ufs_args args;
			int ret;

			if (flags & MNT_RDONLY) {
				args.fspec = 0;
				args.export_info.ex_flags = 0;
				args.export_info.ex_root = 0;
				flags |= MNT_UPDATE | MNT_RELOAD;
				ret = mount(MOUNT_EXT2FS, "/", flags, &args);
				if (ret == 0)
					return(0);
			}
		}
		if (!preen)
			printf("\n***** REBOOT NOW *****\n");
		sync();
		return (4);
	}
	return (0);
}

static void
usage(void)
{
	extern char *__progname;

	(void) fprintf(stderr,
	    "usage: %s [-dfnpy] [-b block#] [-m mode] filesystem\n",
	    __progname);
	exit(1);
}
