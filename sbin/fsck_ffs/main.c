/*	$OpenBSD: main.c,v 1.55 2024/02/03 18:51:57 beck Exp $	*/
/*	$NetBSD: main.c,v 1.22 1996/10/11 20:15:48 thorpej Exp $	*/

/*
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

#include <sys/time.h>
#include <sys/signal.h>
#include <sys/mount.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <err.h>

#include "fsck.h"
#include "extern.h"
#include "fsutil.h"

volatile sig_atomic_t returntosingle;

long long argtoi(int, char *, char *, int);
int	checkfilesys(char *, char *, long, int);
int	main(int, char *[]);

extern char *__progname;

struct inostatlist *inostathead;

struct bufarea bufhead;		/* head of list of other blks in filesys */
struct bufarea sblk;		/* file system superblock */
struct bufarea asblk;		/* alternate file system superblock */
struct bufarea *pdirbp;		/* current directory contents */
struct bufarea *pbp;		/* current inode block */

struct dups *duplist;		/* head of dup list */
struct dups *muldup;		/* end of unique duplicate dup block numbers */

struct zlncnt *zlnhead;		/* head of zero link count list */

struct inoinfo **inphead, **inpsort;

extern long numdirs, listmax, inplast;

long	secsize;		/* actual disk sector size */
char	nflag;			/* assume a no response */
char	yflag;			/* assume a yes response */
daddr_t	bflag;			/* location of alternate super block */
int	debug;			/* output debugging info */
int	cvtlevel;		/* convert to newer file system format */
int	preen;			/* just fix normal inconsistencies */
char    resolved;               /* cleared if unresolved changes => not clean */
char	havesb;			/* superblock has been read */
char	skipclean;		/* skip clean file systems if preening */
int	fsmodified;		/* 1 => write done to file system */
int	fsreadfd;		/* file descriptor for reading file system */
int	fswritefd;		/* file descriptor for writing file system */
int	rerun;			/* rerun fsck.  Only used in non-preen mode */

daddr_t	maxfsblock;		/* number of blocks in the file system */
char	*blockmap;		/* ptr to primary blk allocation map */
ino_t	maxino;			/* number of inodes in file system */
ino_t	lastino;		/* last inode in use */

ino_t	lfdir;			/* lost & found directory inode number */

daddr_t	n_blks;			/* number of blocks in use */
int64_t	n_files;		/* number of files in use */

struct ufs1_dinode ufs1_zino;
struct ufs2_dinode ufs2_zino;

void
usage(void)
{
	fprintf(stderr, "usage: %s [-fnpy] [-b block#] [-c level] "
	    "[-m mode] filesystem\n", __progname);
	exit(1);
}
int
main(int argc, char *argv[])
{
	int ch;
	int ret = 0;

	checkroot();

	sync();
	skipclean = 1;
	while ((ch = getopt(argc, argv, "dfpnNyYb:c:m:")) != -1) {
		switch (ch) {
		case 'p':
			preen = 1;
			break;

		case 'b':
			skipclean = 0;
			bflag = argtoi('b', "number", optarg, 10);
			printf("Alternate super block location: %lld\n",
			    (long long)bflag);
			break;

		case 'c':
			skipclean = 0;
			cvtlevel = argtoi('c', "conversion level", optarg, 10);
			if (cvtlevel < 3)
				errexit("cannot do level %d conversion\n",
				    cvtlevel);
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
		case 'N':
			nflag = 1;
			yflag = 0;
			break;

		case 'y':
		case 'Y':
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
	catchinfo(0);

	(void)checkfilesys(blockcheck(*argv), 0, 0L, 0);

	if (returntosingle)
		ret = 2;

	exit(ret);
}

long long
argtoi(int flag, char *req, char *str, int base)
{
	char *cp;
	long long ret;

	ret = strtoll(str, &cp, base);
	if (cp == str || *cp)
		errexit("-%c flag requires a %s\n", flag, req);
	return (ret);
}

/*
 * Check the specified filesystem.
 */
int
checkfilesys(char *filesys, char *mntpt, long auxdata, int child)
{
	daddr_t n_ffree, n_bfree;
	struct dups *dp;
	struct zlncnt *zlnp;
	int cylno;

	if (preen && child)
		(void)signal(SIGQUIT, voidquit);
	setcdevname(filesys, NULL, preen);
	if (debug && preen)
		pwarn("starting\n");

	switch (setup(filesys, 0)) {
	case 0:
		if (preen)
			pfatal("CAN'T CHECK FILE SYSTEM.");
		/* FALLTHROUGH */
	case -1:
		if (fsreadfd != -1) {
			(void)close(fsreadfd);
			fsreadfd = -1;
		}
		if (fswritefd != -1) {
			(void)close(fswritefd);
			fswritefd = -1;
		}
		return (0);
	}
	info_filesys = filesys;

	/*
	 * Cleared if any questions answered no. Used to decide if
	 * the superblock should be marked clean.
	 */
	resolved = 1;

	/*
	 * 1: scan inodes tallying blocks used
	 */
	if (preen == 0) {
		printf("** Last Mounted on %s\n", sblock.fs_fsmnt);
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
	n_ffree = sblock.fs_cstotal.cs_nffree;
	n_bfree = sblock.fs_cstotal.cs_nbfree;
	pwarn("%lld files, %lld used, %lld free ",
	    n_files, (long long)n_blks,
	    (long long)(n_ffree + sblock.fs_frag * n_bfree));
	printf("(%lld frags, %lld blocks, %lld.%lld%% fragmentation)\n",
	    (long long)n_ffree, (long long)n_bfree,
	    (long long)((n_ffree * 100) / sblock.fs_dsize),
	    (long long)(((n_ffree * 1000 + sblock.fs_dsize / 2) /
	    sblock.fs_dsize) % 10));
	if (debug &&
	    (n_files -= maxino - ROOTINO - sblock.fs_cstotal.cs_nifree))
		printf("%lld files missing\n", n_files);
	if (debug) {
		n_blks += sblock.fs_ncg *
			(cgdmin(&sblock, 0) - cgsblock(&sblock, 0));
		n_blks += cgsblock(&sblock, 0) - cgbase(&sblock, 0);
		n_blks += howmany(sblock.fs_cssize, sblock.fs_fsize);
		if (n_blks -= maxfsblock - (n_ffree + sblock.fs_frag * n_bfree))
			printf("%lld blocks missing\n", (long long)n_blks);
		if (duplist != NULL) {
			printf("The following duplicate blocks remain:");
			for (dp = duplist; dp; dp = dp->next)
				printf(" %lld,", (long long)dp->dup);
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
		sblock.fs_time = (time_t)time(NULL);
		sbdirty();
	}
	if (cvtlevel && sblk.b_dirty) {
		/*
		 * Write out the duplicate super blocks
		 */
		for (cylno = 0; cylno < sblock.fs_ncg; cylno++)
			bwrite(fswritefd, (char *)&sblock,
			    fsbtodb(&sblock, cgsblock(&sblock, cylno)), SBSIZE);
	}
	if (rerun)
		resolved = 0;
	ckfini(resolved); /* Don't mark fs clean if fsck needs to be re-run */

	for (cylno = 0; cylno < sblock.fs_ncg; cylno++)
		free(inostathead[cylno].il_stat);
	free(inostathead);
	inostathead = NULL;

	free(blockmap);
	blockmap = NULL;
	free(sblock.fs_csp);
	free(sblk.b_un.b_buf);
	free(asblk.b_un.b_buf);

	if (!fsmodified)
		return (0);
	if (!preen)
		printf("\n***** FILE SYSTEM WAS MODIFIED *****\n");
	if (rerun || !resolved)
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
				ret = mount(MOUNT_FFS, "/", flags, &args);
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
