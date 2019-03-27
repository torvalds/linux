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
static const char sccsid[] = "@(#)utilities.c	8.6 (Berkeley) 5/19/95";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>

#include <err.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <fstab.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <libufs.h>

#include "fsck.h"

static void slowio_start(void);
static void slowio_end(void);
static void printIOstats(void);

static long diskreads, totaldiskreads, totalreads; /* Disk cache statistics */
static struct timespec startpass, finishpass;
struct timeval slowio_starttime;
int slowio_delay_usec = 10000;	/* Initial IO delay for background fsck */
int slowio_pollcnt;
static struct bufarea cgblk;	/* backup buffer for cylinder group blocks */
static TAILQ_HEAD(buflist, bufarea) bufhead;	/* head of buffer cache list */
static int numbufs;				/* size of buffer cache */
static char *buftype[BT_NUMBUFTYPES] = BT_NAMES;
static struct bufarea *cgbufs;	/* header for cylinder group cache */
static int flushtries;		/* number of tries to reclaim memory */

void
fsutilinit(void)
{
	diskreads = totaldiskreads = totalreads = 0;
	bzero(&startpass, sizeof(struct timespec));
	bzero(&finishpass, sizeof(struct timespec));
	bzero(&slowio_starttime, sizeof(struct timeval));
	slowio_delay_usec = 10000;
	slowio_pollcnt = 0;
	bzero(&cgblk, sizeof(struct bufarea));
	TAILQ_INIT(&bufhead);
	numbufs = 0;
	/* buftype ? */
	cgbufs = NULL;
	flushtries = 0;
}

int
ftypeok(union dinode *dp)
{
	switch (DIP(dp, di_mode) & IFMT) {

	case IFDIR:
	case IFREG:
	case IFBLK:
	case IFCHR:
	case IFLNK:
	case IFSOCK:
	case IFIFO:
		return (1);

	default:
		if (debug)
			printf("bad file type 0%o\n", DIP(dp, di_mode));
		return (0);
	}
}

int
reply(const char *question)
{
	int persevere;
	char c;

	if (preen)
		pfatal("INTERNAL ERROR: GOT TO reply()");
	persevere = !strcmp(question, "CONTINUE");
	printf("\n");
	if (!persevere && (nflag || (fswritefd < 0 && bkgrdflag == 0))) {
		printf("%s? no\n\n", question);
		resolved = 0;
		return (0);
	}
	if (yflag || (persevere && nflag)) {
		printf("%s? yes\n\n", question);
		return (1);
	}
	do	{
		printf("%s? [yn] ", question);
		(void) fflush(stdout);
		c = getc(stdin);
		while (c != '\n' && getc(stdin) != '\n') {
			if (feof(stdin)) {
				resolved = 0;
				return (0);
			}
		}
	} while (c != 'y' && c != 'Y' && c != 'n' && c != 'N');
	printf("\n");
	if (c == 'y' || c == 'Y')
		return (1);
	resolved = 0;
	return (0);
}

/*
 * Look up state information for an inode.
 */
struct inostat *
inoinfo(ino_t inum)
{
	static struct inostat unallocated = { USTATE, 0, 0 };
	struct inostatlist *ilp;
	int iloff;

	if (inum > maxino)
		errx(EEXIT, "inoinfo: inumber %ju out of range",
		    (uintmax_t)inum);
	ilp = &inostathead[inum / sblock.fs_ipg];
	iloff = inum % sblock.fs_ipg;
	if (iloff >= ilp->il_numalloced)
		return (&unallocated);
	return (&ilp->il_stat[iloff]);
}

/*
 * Malloc buffers and set up cache.
 */
void
bufinit(void)
{
	struct bufarea *bp;
	long bufcnt, i;
	char *bufp;

	pbp = pdirbp = (struct bufarea *)0;
	bufp = Malloc((unsigned int)sblock.fs_bsize);
	if (bufp == NULL)
		errx(EEXIT, "cannot allocate buffer pool");
	cgblk.b_un.b_buf = bufp;
	initbarea(&cgblk, BT_CYLGRP);
	TAILQ_INIT(&bufhead);
	bufcnt = MAXBUFS;
	if (bufcnt < MINBUFS)
		bufcnt = MINBUFS;
	for (i = 0; i < bufcnt; i++) {
		bp = (struct bufarea *)Malloc(sizeof(struct bufarea));
		bufp = Malloc((unsigned int)sblock.fs_bsize);
		if (bp == NULL || bufp == NULL) {
			if (i >= MINBUFS)
				break;
			errx(EEXIT, "cannot allocate buffer pool");
		}
		bp->b_un.b_buf = bufp;
		TAILQ_INSERT_HEAD(&bufhead, bp, b_list);
		initbarea(bp, BT_UNKNOWN);
	}
	numbufs = i;	/* save number of buffers */
	for (i = 0; i < BT_NUMBUFTYPES; i++) {
		readtime[i].tv_sec = totalreadtime[i].tv_sec = 0;
		readtime[i].tv_nsec = totalreadtime[i].tv_nsec = 0;
		readcnt[i] = totalreadcnt[i] = 0;
	}
}

/*
 * Manage cylinder group buffers.
 */
static struct bufarea *cgbufs;	/* header for cylinder group cache */
static int flushtries;		/* number of tries to reclaim memory */

struct bufarea *
cglookup(int cg)
{
	struct bufarea *cgbp;
	struct cg *cgp;

	if (cgbufs == NULL) {
		cgbufs = calloc(sblock.fs_ncg, sizeof(struct bufarea));
		if (cgbufs == NULL)
			errx(EEXIT, "cannot allocate cylinder group buffers");
	}
	cgbp = &cgbufs[cg];
	if (cgbp->b_un.b_cg != NULL)
		return (cgbp);
	cgp = NULL;
	if (flushtries == 0)
		cgp = malloc((unsigned int)sblock.fs_cgsize);
	if (cgp == NULL) {
		getblk(&cgblk, cgtod(&sblock, cg), sblock.fs_cgsize);
		return (&cgblk);
	}
	cgbp->b_un.b_cg = cgp;
	initbarea(cgbp, BT_CYLGRP);
	getblk(cgbp, cgtod(&sblock, cg), sblock.fs_cgsize);
	return (cgbp);
}

/*
 * Mark a cylinder group buffer as dirty.
 * Update its check-hash if they are enabled.
 */
void
cgdirty(struct bufarea *cgbp)
{
	struct cg *cg;

	cg = cgbp->b_un.b_cg;
	if ((sblock.fs_metackhash & CK_CYLGRP) != 0) {
		cg->cg_ckhash = 0;
		cg->cg_ckhash =
		    calculate_crc32c(~0L, (void *)cg, sblock.fs_cgsize);
	}
	dirty(cgbp);
}

/*
 * Attempt to flush a cylinder group cache entry.
 * Return whether the flush was successful.
 */
int
flushentry(void)
{
	struct bufarea *cgbp;

	if (flushtries == sblock.fs_ncg || cgbufs == NULL)
		return (0);
	cgbp = &cgbufs[flushtries++];
	if (cgbp->b_un.b_cg == NULL)
		return (0);
	flush(fswritefd, cgbp);
	free(cgbp->b_un.b_buf);
	cgbp->b_un.b_buf = NULL;
	return (1);
}

/*
 * Manage a cache of directory blocks.
 */
struct bufarea *
getdatablk(ufs2_daddr_t blkno, long size, int type)
{
	struct bufarea *bp;

	TAILQ_FOREACH(bp, &bufhead, b_list)
		if (bp->b_bno == fsbtodb(&sblock, blkno))
			goto foundit;
	TAILQ_FOREACH_REVERSE(bp, &bufhead, buflist, b_list)
		if ((bp->b_flags & B_INUSE) == 0)
			break;
	if (bp == NULL)
		errx(EEXIT, "deadlocked buffer pool");
	bp->b_type = type;
	getblk(bp, blkno, size);
	/* fall through */
foundit:
	if (debug && bp->b_type != type)
		printf("Buffer type changed from %s to %s\n",
		    buftype[bp->b_type], buftype[type]);
	TAILQ_REMOVE(&bufhead, bp, b_list);
	TAILQ_INSERT_HEAD(&bufhead, bp, b_list);
	bp->b_flags |= B_INUSE;
	return (bp);
}

void
getblk(struct bufarea *bp, ufs2_daddr_t blk, long size)
{
	ufs2_daddr_t dblk;
	struct timespec start, finish;

	dblk = fsbtodb(&sblock, blk);
	if (bp->b_bno == dblk) {
		totalreads++;
	} else {
		flush(fswritefd, bp);
		if (debug) {
			readcnt[bp->b_type]++;
			clock_gettime(CLOCK_REALTIME_PRECISE, &start);
		}
		bp->b_errs = blread(fsreadfd, bp->b_un.b_buf, dblk, size);
		if (debug) {
			clock_gettime(CLOCK_REALTIME_PRECISE, &finish);
			timespecsub(&finish, &start, &finish);
			timespecadd(&readtime[bp->b_type], &finish,
			    &readtime[bp->b_type]);
		}
		bp->b_bno = dblk;
		bp->b_size = size;
	}
}

void
flush(int fd, struct bufarea *bp)
{

	if (!bp->b_dirty)
		return;
	bp->b_dirty = 0;
	if (fswritefd < 0) {
		pfatal("WRITING IN READ_ONLY MODE.\n");
		return;
	}
	if (bp->b_errs != 0)
		pfatal("WRITING %sZERO'ED BLOCK %lld TO DISK\n",
		    (bp->b_errs == bp->b_size / dev_bsize) ? "" : "PARTIALLY ",
		    (long long)bp->b_bno);
	bp->b_errs = 0;
	/*
	 * Write using the appropriate function.
	 */
	switch (bp->b_type) {
	case BT_SUPERBLK:
		if (bp != &sblk)
			pfatal("BUFFER %p DOES NOT MATCH SBLK %p\n",
			    bp, &sblk);
		if (sbput(fd, bp->b_un.b_fs, 0) == 0)
			fsmodified = 1;
		break;
	case BT_CYLGRP:
		if (cgput(&disk, bp->b_un.b_cg) == 0)
			fsmodified = 1;
		break;
	default:
		blwrite(fd, bp->b_un.b_buf, bp->b_bno, bp->b_size);
		break;
	}
}

void
rwerror(const char *mesg, ufs2_daddr_t blk)
{

	if (bkgrdcheck)
		exit(EEXIT);
	if (preen == 0)
		printf("\n");
	pfatal("CANNOT %s: %ld", mesg, (long)blk);
	if (reply("CONTINUE") == 0)
		exit(EEXIT);
}

void
ckfini(int markclean)
{
	struct bufarea *bp, *nbp;
	int ofsmodified, cnt;

	if (bkgrdflag) {
		unlink(snapname);
		if ((!(sblock.fs_flags & FS_UNCLEAN)) != markclean) {
			cmd.value = FS_UNCLEAN;
			cmd.size = markclean ? -1 : 1;
			if (sysctlbyname("vfs.ffs.setflags", 0, 0,
			    &cmd, sizeof cmd) == -1)
				rwerror("SET FILE SYSTEM FLAGS", FS_UNCLEAN);
			if (!preen) {
				printf("\n***** FILE SYSTEM MARKED %s *****\n",
				    markclean ? "CLEAN" : "DIRTY");
				if (!markclean)
					rerun = 1;
			}
		} else if (!preen && !markclean) {
			printf("\n***** FILE SYSTEM STILL DIRTY *****\n");
			rerun = 1;
		}
	}
	if (debug && totalreads > 0)
		printf("cache with %d buffers missed %ld of %ld (%d%%)\n",
		    numbufs, totaldiskreads, totalreads,
		    (int)(totaldiskreads * 100 / totalreads));
	if (fswritefd < 0) {
		(void)close(fsreadfd);
		return;
	}
	flush(fswritefd, &sblk);
	if (havesb && cursnapshot == 0 && sblock.fs_magic == FS_UFS2_MAGIC &&
	    sblk.b_bno != sblock.fs_sblockloc / dev_bsize &&
	    !preen && reply("UPDATE STANDARD SUPERBLOCK")) {
		/* Change the write destination to standard superblock */
		sblock.fs_sblockactualloc = sblock.fs_sblockloc;
		sblk.b_bno = sblock.fs_sblockloc / dev_bsize;
		sbdirty();
		flush(fswritefd, &sblk);
	}
	flush(fswritefd, &cgblk);
	free(cgblk.b_un.b_buf);
	cnt = 0;
	TAILQ_FOREACH_REVERSE_SAFE(bp, &bufhead, buflist, b_list, nbp) {
		TAILQ_REMOVE(&bufhead, bp, b_list);
		cnt++;
		flush(fswritefd, bp);
		free(bp->b_un.b_buf);
		free((char *)bp);
	}
	if (numbufs != cnt)
		errx(EEXIT, "panic: lost %d buffers", numbufs - cnt);
	if (cgbufs != NULL) {
		for (cnt = 0; cnt < sblock.fs_ncg; cnt++) {
			if (cgbufs[cnt].b_un.b_cg == NULL)
				continue;
			flush(fswritefd, &cgbufs[cnt]);
			free(cgbufs[cnt].b_un.b_cg);
		}
		free(cgbufs);
	}
	pbp = pdirbp = (struct bufarea *)0;
	if (cursnapshot == 0 && sblock.fs_clean != markclean) {
		if ((sblock.fs_clean = markclean) != 0) {
			sblock.fs_flags &= ~(FS_UNCLEAN | FS_NEEDSFSCK);
			sblock.fs_pendingblocks = 0;
			sblock.fs_pendinginodes = 0;
		}
		sbdirty();
		ofsmodified = fsmodified;
		flush(fswritefd, &sblk);
		fsmodified = ofsmodified;
		if (!preen) {
			printf("\n***** FILE SYSTEM MARKED %s *****\n",
			    markclean ? "CLEAN" : "DIRTY");
			if (!markclean)
				rerun = 1;
		}
	} else if (!preen) {
		if (markclean) {
			printf("\n***** FILE SYSTEM IS CLEAN *****\n");
		} else {
			printf("\n***** FILE SYSTEM STILL DIRTY *****\n");
			rerun = 1;
		}
	}
	(void)close(fsreadfd);
	(void)close(fswritefd);
}

/*
 * Print out I/O statistics.
 */
void
IOstats(char *what)
{
	int i;

	if (debug == 0)
		return;
	if (diskreads == 0) {
		printf("%s: no I/O\n\n", what);
		return;
	}
	if (startpass.tv_sec == 0)
		startpass = startprog;
	printf("%s: I/O statistics\n", what);
	printIOstats();
	totaldiskreads += diskreads;
	diskreads = 0;
	for (i = 0; i < BT_NUMBUFTYPES; i++) {
		timespecadd(&totalreadtime[i], &readtime[i], &totalreadtime[i]);
		totalreadcnt[i] += readcnt[i];
		readtime[i].tv_sec = readtime[i].tv_nsec = 0;
		readcnt[i] = 0;
	}
	clock_gettime(CLOCK_REALTIME_PRECISE, &startpass);
}

void
finalIOstats(void)
{
	int i;

	if (debug == 0)
		return;
	printf("Final I/O statistics\n");
	totaldiskreads += diskreads;
	diskreads = totaldiskreads;
	startpass = startprog;
	for (i = 0; i < BT_NUMBUFTYPES; i++) {
		timespecadd(&totalreadtime[i], &readtime[i], &totalreadtime[i]);
		totalreadcnt[i] += readcnt[i];
		readtime[i] = totalreadtime[i];
		readcnt[i] = totalreadcnt[i];
	}
	printIOstats();
}

static void printIOstats(void)
{
	long long msec, totalmsec;
	int i;

	clock_gettime(CLOCK_REALTIME_PRECISE, &finishpass);
	timespecsub(&finishpass, &startpass, &finishpass);
	printf("Running time: %jd.%03ld sec\n",
		(intmax_t)finishpass.tv_sec, finishpass.tv_nsec / 1000000);
	printf("buffer reads by type:\n");
	for (totalmsec = 0, i = 0; i < BT_NUMBUFTYPES; i++)
		totalmsec += readtime[i].tv_sec * 1000 +
		    readtime[i].tv_nsec / 1000000;
	if (totalmsec == 0)
		totalmsec = 1;
	for (i = 0; i < BT_NUMBUFTYPES; i++) {
		if (readcnt[i] == 0)
			continue;
		msec =
		    readtime[i].tv_sec * 1000 + readtime[i].tv_nsec / 1000000;
		printf("%21s:%8ld %2ld.%ld%% %4jd.%03ld sec %2lld.%lld%%\n",
		    buftype[i], readcnt[i], readcnt[i] * 100 / diskreads,
		    (readcnt[i] * 1000 / diskreads) % 10,
		    (intmax_t)readtime[i].tv_sec, readtime[i].tv_nsec / 1000000,
		    msec * 100 / totalmsec, (msec * 1000 / totalmsec) % 10);
	}
	printf("\n");
}

int
blread(int fd, char *buf, ufs2_daddr_t blk, long size)
{
	char *cp;
	int i, errs;
	off_t offset;

	offset = blk;
	offset *= dev_bsize;
	if (bkgrdflag)
		slowio_start();
	totalreads++;
	diskreads++;
	if (pread(fd, buf, (int)size, offset) == size) {
		if (bkgrdflag)
			slowio_end();
		return (0);
	}

	/*
	 * This is handled specially here instead of in rwerror because
	 * rwerror is used for all sorts of errors, not just true read/write
	 * errors.  It should be refactored and fixed.
	 */
	if (surrender) {
		pfatal("CANNOT READ_BLK: %ld", (long)blk);
		errx(EEXIT, "ABORTING DUE TO READ ERRORS");
	} else
		rwerror("READ BLK", blk);

	errs = 0;
	memset(buf, 0, (size_t)size);
	printf("THE FOLLOWING DISK SECTORS COULD NOT BE READ:");
	for (cp = buf, i = 0; i < size; i += secsize, cp += secsize) {
		if (pread(fd, cp, (int)secsize, offset + i) != secsize) {
			if (secsize != dev_bsize && dev_bsize != 1)
				printf(" %jd (%jd),",
				    (intmax_t)(blk * dev_bsize + i) / secsize,
				    (intmax_t)blk + i / dev_bsize);
			else
				printf(" %jd,", (intmax_t)blk + i / dev_bsize);
			errs++;
		}
	}
	printf("\n");
	if (errs)
		resolved = 0;
	return (errs);
}

void
blwrite(int fd, char *buf, ufs2_daddr_t blk, ssize_t size)
{
	int i;
	char *cp;
	off_t offset;

	if (fd < 0)
		return;
	offset = blk;
	offset *= dev_bsize;
	if (pwrite(fd, buf, size, offset) == size) {
		fsmodified = 1;
		return;
	}
	resolved = 0;
	rwerror("WRITE BLK", blk);
	printf("THE FOLLOWING SECTORS COULD NOT BE WRITTEN:");
	for (cp = buf, i = 0; i < size; i += dev_bsize, cp += dev_bsize)
		if (pwrite(fd, cp, dev_bsize, offset + i) != dev_bsize)
			printf(" %jd,", (intmax_t)blk + i / dev_bsize);
	printf("\n");
	return;
}

void
blerase(int fd, ufs2_daddr_t blk, long size)
{
	off_t ioarg[2];

	if (fd < 0)
		return;
	ioarg[0] = blk * dev_bsize;
	ioarg[1] = size;
	ioctl(fd, DIOCGDELETE, ioarg);
	/* we don't really care if we succeed or not */
	return;
}

/*
 * Fill a contiguous region with all-zeroes.  Note ZEROBUFSIZE is by
 * definition a multiple of dev_bsize.
 */
void
blzero(int fd, ufs2_daddr_t blk, long size)
{
	static char *zero;
	off_t offset, len;

	if (fd < 0)
		return;
	if (zero == NULL) {
		zero = calloc(ZEROBUFSIZE, 1);
		if (zero == NULL)
			errx(EEXIT, "cannot allocate buffer pool");
	}
	offset = blk * dev_bsize;
	if (lseek(fd, offset, 0) < 0)
		rwerror("SEEK BLK", blk);
	while (size > 0) {
		len = MIN(ZEROBUFSIZE, size);
		if (write(fd, zero, len) != len)
			rwerror("WRITE BLK", blk);
		blk += len / dev_bsize;
		size -= len;
	}
}

/*
 * Verify cylinder group's magic number and other parameters.  If the
 * test fails, offer an option to rebuild the whole cylinder group.
 */
int
check_cgmagic(int cg, struct bufarea *cgbp)
{
	struct cg *cgp = cgbp->b_un.b_cg;

	/*
	 * Extended cylinder group checks.
	 */
	if (cg_chkmagic(cgp) &&
	    ((sblock.fs_magic == FS_UFS1_MAGIC &&
	      cgp->cg_old_niblk == sblock.fs_ipg &&
	      cgp->cg_ndblk <= sblock.fs_fpg &&
	      cgp->cg_old_ncyl <= sblock.fs_old_cpg) ||
	     (sblock.fs_magic == FS_UFS2_MAGIC &&
	      cgp->cg_niblk == sblock.fs_ipg &&
	      cgp->cg_ndblk <= sblock.fs_fpg &&
	      cgp->cg_initediblk <= sblock.fs_ipg))) {
		return (1);
	}
	pfatal("CYLINDER GROUP %d: BAD MAGIC NUMBER", cg);
	if (!reply("REBUILD CYLINDER GROUP")) {
		printf("YOU WILL NEED TO RERUN FSCK.\n");
		rerun = 1;
		return (1);
	}
	/*
	 * Zero out the cylinder group and then initialize critical fields.
	 * Bit maps and summaries will be recalculated by later passes.
	 */
	memset(cgp, 0, (size_t)sblock.fs_cgsize);
	cgp->cg_magic = CG_MAGIC;
	cgp->cg_cgx = cg;
	cgp->cg_niblk = sblock.fs_ipg;
	cgp->cg_initediblk = MIN(sblock.fs_ipg, 2 * INOPB(&sblock));
	if (cgbase(&sblock, cg) + sblock.fs_fpg < sblock.fs_size)
		cgp->cg_ndblk = sblock.fs_fpg;
	else
		cgp->cg_ndblk = sblock.fs_size - cgbase(&sblock, cg);
	cgp->cg_iusedoff = &cgp->cg_space[0] - (u_char *)(&cgp->cg_firstfield);
	if (sblock.fs_magic == FS_UFS1_MAGIC) {
		cgp->cg_niblk = 0;
		cgp->cg_initediblk = 0;
		cgp->cg_old_ncyl = sblock.fs_old_cpg;
		cgp->cg_old_niblk = sblock.fs_ipg;
		cgp->cg_old_btotoff = cgp->cg_iusedoff;
		cgp->cg_old_boff = cgp->cg_old_btotoff +
		    sblock.fs_old_cpg * sizeof(int32_t);
		cgp->cg_iusedoff = cgp->cg_old_boff +
		    sblock.fs_old_cpg * sizeof(u_int16_t);
	}
	cgp->cg_freeoff = cgp->cg_iusedoff + howmany(sblock.fs_ipg, CHAR_BIT);
	cgp->cg_nextfreeoff = cgp->cg_freeoff + howmany(sblock.fs_fpg,CHAR_BIT);
	if (sblock.fs_contigsumsize > 0) {
		cgp->cg_nclusterblks = cgp->cg_ndblk / sblock.fs_frag;
		cgp->cg_clustersumoff =
		    roundup(cgp->cg_nextfreeoff, sizeof(u_int32_t));
		cgp->cg_clustersumoff -= sizeof(u_int32_t);
		cgp->cg_clusteroff = cgp->cg_clustersumoff +
		    (sblock.fs_contigsumsize + 1) * sizeof(u_int32_t);
		cgp->cg_nextfreeoff = cgp->cg_clusteroff +
		    howmany(fragstoblks(&sblock, sblock.fs_fpg), CHAR_BIT);
	}
	cgdirty(cgbp);
	return (0);
}

/*
 * allocate a data block with the specified number of fragments
 */
ufs2_daddr_t
allocblk(long frags)
{
	int i, j, k, cg, baseblk;
	struct bufarea *cgbp;
	struct cg *cgp;

	if (frags <= 0 || frags > sblock.fs_frag)
		return (0);
	for (i = 0; i < maxfsblock - sblock.fs_frag; i += sblock.fs_frag) {
		for (j = 0; j <= sblock.fs_frag - frags; j++) {
			if (testbmap(i + j))
				continue;
			for (k = 1; k < frags; k++)
				if (testbmap(i + j + k))
					break;
			if (k < frags) {
				j += k;
				continue;
			}
			cg = dtog(&sblock, i + j);
			cgbp = cglookup(cg);
			cgp = cgbp->b_un.b_cg;
			if (!check_cgmagic(cg, cgbp))
				return (0);
			baseblk = dtogd(&sblock, i + j);
			for (k = 0; k < frags; k++) {
				setbmap(i + j + k);
				clrbit(cg_blksfree(cgp), baseblk + k);
			}
			n_blks += frags;
			if (frags == sblock.fs_frag)
				cgp->cg_cs.cs_nbfree--;
			else
				cgp->cg_cs.cs_nffree -= frags;
			cgdirty(cgbp);
			return (i + j);
		}
	}
	return (0);
}

/*
 * Free a previously allocated block
 */
void
freeblk(ufs2_daddr_t blkno, long frags)
{
	struct inodesc idesc;

	idesc.id_blkno = blkno;
	idesc.id_numfrags = frags;
	(void)pass4check(&idesc);
}

/* Slow down IO so as to leave some disk bandwidth for other processes */
void
slowio_start()
{

	/* Delay one in every 8 operations */
	slowio_pollcnt = (slowio_pollcnt + 1) & 7;
	if (slowio_pollcnt == 0) {
		gettimeofday(&slowio_starttime, NULL);
	}
}

void
slowio_end()
{
	struct timeval tv;
	int delay_usec;

	if (slowio_pollcnt != 0)
		return;

	/* Update the slowdown interval. */
	gettimeofday(&tv, NULL);
	delay_usec = (tv.tv_sec - slowio_starttime.tv_sec) * 1000000 +
	    (tv.tv_usec - slowio_starttime.tv_usec);
	if (delay_usec < 64)
		delay_usec = 64;
	if (delay_usec > 2500000)
		delay_usec = 2500000;
	slowio_delay_usec = (slowio_delay_usec * 63 + delay_usec) >> 6;
	/* delay by 8 times the average IO delay */
	if (slowio_delay_usec > 64)
		usleep(slowio_delay_usec * 8);
}

/*
 * Find a pathname
 */
void
getpathname(char *namebuf, ino_t curdir, ino_t ino)
{
	int len;
	char *cp;
	struct inodesc idesc;
	static int busy = 0;

	if (curdir == ino && ino == UFS_ROOTINO) {
		(void)strcpy(namebuf, "/");
		return;
	}
	if (busy || !INO_IS_DVALID(curdir)) {
		(void)strcpy(namebuf, "?");
		return;
	}
	busy = 1;
	memset(&idesc, 0, sizeof(struct inodesc));
	idesc.id_type = DATA;
	idesc.id_fix = IGNORE;
	cp = &namebuf[MAXPATHLEN - 1];
	*cp = '\0';
	if (curdir != ino) {
		idesc.id_parent = curdir;
		goto namelookup;
	}
	while (ino != UFS_ROOTINO) {
		idesc.id_number = ino;
		idesc.id_func = findino;
		idesc.id_name = strdup("..");
		if ((ckinode(ginode(ino), &idesc) & FOUND) == 0)
			break;
	namelookup:
		idesc.id_number = idesc.id_parent;
		idesc.id_parent = ino;
		idesc.id_func = findname;
		idesc.id_name = namebuf;
		if ((ckinode(ginode(idesc.id_number), &idesc)&FOUND) == 0)
			break;
		len = strlen(namebuf);
		cp -= len;
		memmove(cp, namebuf, (size_t)len);
		*--cp = '/';
		if (cp < &namebuf[UFS_MAXNAMLEN])
			break;
		ino = idesc.id_number;
	}
	busy = 0;
	if (ino != UFS_ROOTINO)
		*--cp = '?';
	memmove(namebuf, cp, (size_t)(&namebuf[MAXPATHLEN] - cp));
}

void
catch(int sig __unused)
{

	ckfini(0);
	exit(12);
}

/*
 * When preening, allow a single quit to signal
 * a special exit after file system checks complete
 * so that reboot sequence may be interrupted.
 */
void
catchquit(int sig __unused)
{
	printf("returning to single-user after file system check\n");
	returntosingle = 1;
	(void)signal(SIGQUIT, SIG_DFL);
}

/*
 * determine whether an inode should be fixed.
 */
int
dofix(struct inodesc *idesc, const char *msg)
{

	switch (idesc->id_fix) {

	case DONTKNOW:
		if (idesc->id_type == DATA)
			direrror(idesc->id_number, msg);
		else
			pwarn("%s", msg);
		if (preen) {
			printf(" (SALVAGED)\n");
			idesc->id_fix = FIX;
			return (ALTERED);
		}
		if (reply("SALVAGE") == 0) {
			idesc->id_fix = NOFIX;
			return (0);
		}
		idesc->id_fix = FIX;
		return (ALTERED);

	case FIX:
		return (ALTERED);

	case NOFIX:
	case IGNORE:
		return (0);

	default:
		errx(EEXIT, "UNKNOWN INODESC FIX MODE %d", idesc->id_fix);
	}
	/* NOTREACHED */
	return (0);
}

#include <stdarg.h>

/*
 * An unexpected inconsistency occurred.
 * Die if preening or file system is running with soft dependency protocol,
 * otherwise just print message and continue.
 */
void
pfatal(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (!preen) {
		(void)vfprintf(stdout, fmt, ap);
		va_end(ap);
		if (usedsoftdep)
			(void)fprintf(stdout,
			    "\nUNEXPECTED SOFT UPDATE INCONSISTENCY\n");
		/*
		 * Force foreground fsck to clean up inconsistency.
		 */
		if (bkgrdflag) {
			cmd.value = FS_NEEDSFSCK;
			cmd.size = 1;
			if (sysctlbyname("vfs.ffs.setflags", 0, 0,
			    &cmd, sizeof cmd) == -1)
				pwarn("CANNOT SET FS_NEEDSFSCK FLAG\n");
			fprintf(stdout, "CANNOT RUN IN BACKGROUND\n");
			ckfini(0);
			exit(EEXIT);
		}
		return;
	}
	if (cdevname == NULL)
		cdevname = strdup("fsck");
	(void)fprintf(stdout, "%s: ", cdevname);
	(void)vfprintf(stdout, fmt, ap);
	(void)fprintf(stdout,
	    "\n%s: UNEXPECTED%sINCONSISTENCY; RUN fsck MANUALLY.\n",
	    cdevname, usedsoftdep ? " SOFT UPDATE " : " ");
	/*
	 * Force foreground fsck to clean up inconsistency.
	 */
	if (bkgrdflag) {
		cmd.value = FS_NEEDSFSCK;
		cmd.size = 1;
		if (sysctlbyname("vfs.ffs.setflags", 0, 0,
		    &cmd, sizeof cmd) == -1)
			pwarn("CANNOT SET FS_NEEDSFSCK FLAG\n");
	}
	ckfini(0);
	exit(EEXIT);
}

/*
 * Pwarn just prints a message when not preening or running soft dependency
 * protocol, or a warning (preceded by filename) when preening.
 */
void
pwarn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (preen)
		(void)fprintf(stdout, "%s: ", cdevname);
	(void)vfprintf(stdout, fmt, ap);
	va_end(ap);
}

/*
 * Stub for routines from kernel.
 */
void
panic(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	pfatal("INTERNAL INCONSISTENCY:");
	(void)vfprintf(stdout, fmt, ap);
	va_end(ap);
	exit(EEXIT);
}
