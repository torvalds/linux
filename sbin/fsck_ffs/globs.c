/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1986, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)main.c	8.6 (Berkeley) 5/14/95";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>
#include <string.h>
#include "fsck.h"
 
long readcnt[BT_NUMBUFTYPES];
long totalreadcnt[BT_NUMBUFTYPES];
struct timespec readtime[BT_NUMBUFTYPES];
struct timespec totalreadtime[BT_NUMBUFTYPES];
struct timespec startprog;
struct bufarea sblk;		/* file system superblock */
struct bufarea *pdirbp;		/* current directory contents */
struct bufarea *pbp;		/* current inode block */
ino_t cursnapshot;
long  dirhash, inplast;
unsigned long  numdirs, listmax;
long countdirs;		/* number of directories we actually found */
int	adjrefcnt[MIBSIZE];	/* MIB command to adjust inode reference cnt */
int	adjblkcnt[MIBSIZE];	/* MIB command to adjust inode block count */
int	setsize[MIBSIZE];	/* MIB command to set inode size */
int	adjndir[MIBSIZE];	/* MIB command to adjust number of directories */
int	adjnbfree[MIBSIZE];	/* MIB command to adjust number of free blocks */
int	adjnifree[MIBSIZE];	/* MIB command to adjust number of free inodes */
int	adjnffree[MIBSIZE];	/* MIB command to adjust number of free frags */
int	adjnumclusters[MIBSIZE];	/* MIB command to adjust number of free clusters */
int	freefiles[MIBSIZE];	/* MIB command to free a set of files */
int	freedirs[MIBSIZE];	/* MIB command to free a set of directories */
int	freeblks[MIBSIZE];	/* MIB command to free a set of data blocks */
struct	fsck_cmd cmd;		/* sysctl file system update commands */
char	snapname[BUFSIZ];	/* when doing snapshots, the name of the file */
char	*cdevname;		/* name of device being checked */
long	dev_bsize;		/* computed value of DEV_BSIZE */
long	secsize;		/* actual disk sector size */
u_int	real_dev_bsize;		/* actual disk sector size, not overridden */
char	nflag;			/* assume a no response */
char	yflag;			/* assume a yes response */
int	bkgrdflag;		/* use a snapshot to run on an active system */
off_t	bflag;			/* location of alternate super block */
int	debug;			/* output debugging info */
int	Eflag;			/* delete empty data blocks */
int	Zflag;			/* zero empty data blocks */
int	inoopt;			/* trim out unused inodes */
char	ckclean;		/* only do work if not cleanly unmounted */
int	cvtlevel;		/* convert to newer file system format */
int	ckhashadd;		/* check hashes to be added */
int	bkgrdcheck;		/* determine if background check is possible */
int	bkgrdsumadj;		/* whether the kernel have ability to adjust superblock summary */
char	usedsoftdep;		/* just fix soft dependency inconsistencies */
char	preen;			/* just fix normal inconsistencies */
char	rerun;			/* rerun fsck. Only used in non-preen mode */
int	returntosingle;		/* 1 => return to single user mode on exit */
char	resolved;		/* cleared if unresolved changes => not clean */
char	havesb;			/* superblock has been read */
char	skipclean;		/* skip clean file systems if preening */
int	fsmodified;		/* 1 => write done to file system */
int	fsreadfd;		/* file descriptor for reading file system */
int	fswritefd;		/* file descriptor for writing file system */
int	surrender;		/* Give up if reads fail */
int	wantrestart;		/* Restart fsck on early termination */
ufs2_daddr_t maxfsblock;	/* number of blocks in the file system */
char	*blockmap;		/* ptr to primary blk allocation map */
ino_t	maxino;			/* number of inodes in file system */
ino_t	lfdir;			/* lost & found directory inode number */
const char *lfname;		/* lost & found directory name */
int	lfmode;			/* lost & found directory creation mode */
ufs2_daddr_t n_blks;		/* number of blocks in use */
ino_t n_files;			/* number of files in use */
volatile sig_atomic_t	got_siginfo;	/* received a SIGINFO */
volatile sig_atomic_t	got_sigalarm;	/* received a SIGALRM */
struct	ufs1_dinode ufs1_zino;
struct	ufs2_dinode ufs2_zino;

void
fsckinit(void)
{
	bzero(readcnt, sizeof(long) * BT_NUMBUFTYPES);
	bzero(totalreadcnt, sizeof(long) * BT_NUMBUFTYPES);
	bzero(readtime, sizeof(struct timespec) * BT_NUMBUFTYPES);
	bzero(totalreadtime, sizeof(struct timespec) * BT_NUMBUFTYPES);
	bzero(&startprog, sizeof(struct timespec));
	bzero(&sblk, sizeof(struct bufarea));
	pdirbp = NULL;
	pbp = NULL;
	cursnapshot = 0;
	listmax = numdirs = dirhash = inplast = 0;
	countdirs = 0;
	bzero(adjrefcnt, sizeof(int) * MIBSIZE);
	bzero(adjblkcnt, sizeof(int) * MIBSIZE);
	bzero(setsize, sizeof(int) * MIBSIZE);
	bzero(adjndir, sizeof(int) * MIBSIZE);
	bzero(adjnbfree, sizeof(int) * MIBSIZE);
	bzero(adjnifree, sizeof(int) * MIBSIZE);
	bzero(adjnffree, sizeof(int) * MIBSIZE);
	bzero(adjnumclusters, sizeof(int) * MIBSIZE);
	bzero(freefiles, sizeof(int) * MIBSIZE);
	bzero(freedirs, sizeof(int) * MIBSIZE);
	bzero(freeblks, sizeof(int) * MIBSIZE);
	bzero(&cmd, sizeof(struct fsck_cmd));
	bzero(snapname, sizeof(char) * BUFSIZ);
	cdevname = NULL;
	dev_bsize = 0;
	secsize = 0;
	real_dev_bsize = 0;	
	bkgrdsumadj = 0;
	usedsoftdep = 0;
	rerun = 0;
	returntosingle = 0;
	resolved = 0;
	havesb = 0;
	fsmodified = 0;
	fsreadfd = 0;
	fswritefd = 0;
	maxfsblock = 0;
	blockmap = NULL;
	maxino = 0;
	lfdir = 0;
	lfname = "lost+found";
	lfmode = 0700;
	n_blks = 0;
	n_files = 0;
	got_siginfo = 0;
	got_sigalarm = 0;
	bzero(&ufs1_zino, sizeof(struct ufs1_dinode));
	bzero(&ufs2_zino, sizeof(struct ufs2_dinode));
}
