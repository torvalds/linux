/*	$OpenBSD: utilities.c,v 1.55 2023/03/08 04:43:06 guenther Exp $	*/
/*	$NetBSD: utilities.c,v 1.18 1996/09/27 22:45:20 christos Exp $	*/

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

#include <sys/param.h>	/* DEV_BSIZE isset setbit clrbit */
#include <sys/time.h>
#include <sys/uio.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>

#include "fsutil.h"
#include "fsck.h"
#include "extern.h"

long				diskreads, totalreads;	/* Disk cache statistics */
static struct bufarea		cgblk;			/* backup buffer for cylinder group blocks */

static void rwerror(char *, daddr_t);

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
reply(char *question)
{
	int persevere, c;

	if (preen)
		pfatal("INTERNAL ERROR: GOT TO reply()");
	persevere = !strcmp(question, "CONTINUE");
	printf("\n");
	if (!persevere && (nflag || fswritefd < 0)) {
		printf("%s? no\n\n", question);
		resolved = 0;
		return (0);
	}
	if (yflag || (persevere && nflag)) {
		printf("%s? yes\n\n", question);
		return (1);
	}

	do {
		printf("%s? [Fyn?] ", question);
		(void) fflush(stdout);
		c = getc(stdin);
		if (c == 'F') {
			yflag = 1;
			return (1);
		}
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
		errexit("inoinfo: inumber %llu out of range",
		    (unsigned long long)inum);
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

	pbp = pdirbp = NULL;
	bufp = malloc((unsigned int)sblock.fs_bsize);
	if (bufp == 0)
		errexit("cannot allocate buffer pool\n");
	cgblk.b_un.b_buf = bufp;
	initbarea(&cgblk);
	bufhead.b_next = bufhead.b_prev = &bufhead;
	bufcnt = MAXBUFSPACE / sblock.fs_bsize;
	if (bufcnt < MINBUFS)
		bufcnt = MINBUFS;
	for (i = 0; i < bufcnt; i++) {
		bp = malloc(sizeof(struct bufarea));
		bufp = malloc((unsigned int)sblock.fs_bsize);
		if (bp == NULL || bufp == NULL) {
			free(bp);
			free(bufp);
			if (i >= MINBUFS)
				break;
			errexit("cannot allocate buffer pool\n");
		}
		bp->b_un.b_buf = bufp;
		bp->b_prev = &bufhead;
		bp->b_next = bufhead.b_next;
		bufhead.b_next->b_prev = bp;
		bufhead.b_next = bp;
		initbarea(bp);
	}
	bufhead.b_size = i;	/* save number of buffers */
}

/*
 * Manage cylinder group buffers.
 */
static struct bufarea *cgbufs;	/* header for cylinder group cache */
static int flushtries;		/* number of tries to reclaim memory */
struct bufarea *
cglookup(u_int cg)
{
	struct bufarea *cgbp;
	struct cg *cgp;

	if (cgbufs == NULL) {
		cgbufs = calloc(sblock.fs_ncg, sizeof(struct bufarea));
		if (cgbufs == NULL)
			errexit("cannot allocate cylinder group buffers");
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
	initbarea(cgbp);
	getblk(cgbp, cgtod(&sblock, cg), sblock.fs_cgsize);
	return (cgbp);
}


/*
 * Manage a cache of directory blocks.
 */
struct bufarea *
getdatablk(daddr_t blkno, long size)
{
	struct bufarea *bp;

	for (bp = bufhead.b_next; bp != &bufhead; bp = bp->b_next)
		if (bp->b_bno == fsbtodb(&sblock, blkno))
			goto foundit;
	for (bp = bufhead.b_prev; bp != &bufhead; bp = bp->b_prev)
		if ((bp->b_flags & B_INUSE) == 0)
			break;
	if (bp == &bufhead)
		errexit("deadlocked buffer pool\n");
	getblk(bp, blkno, size);
	/* FALLTHROUGH */
foundit:
	totalreads++;
	bp->b_prev->b_next = bp->b_next;
	bp->b_next->b_prev = bp->b_prev;
	bp->b_prev = &bufhead;
	bp->b_next = bufhead.b_next;
	bufhead.b_next->b_prev = bp;
	bufhead.b_next = bp;
	bp->b_flags |= B_INUSE;
	return (bp);
}

void
getblk(struct bufarea *bp, daddr_t blk, long size)
{
	daddr_t dblk;

	dblk = fsbtodb(&sblock, blk);
	if (bp->b_bno != dblk) {
		flush(fswritefd, bp);
		diskreads++;
		bp->b_errs = bread(fsreadfd, bp->b_un.b_buf, dblk, size);
		bp->b_bno = dblk;
		bp->b_size = size;
	}
}

void
flush(int fd, struct bufarea *bp)
{
	int i, j;

	if (!bp->b_dirty)
		return;
	if (bp->b_errs != 0)
		pfatal("WRITING %sZERO'ED BLOCK %lld TO DISK\n",
		    (bp->b_errs == bp->b_size / DEV_BSIZE) ? "" : "PARTIALLY ",
		    (long long)bp->b_bno);
	bp->b_dirty = 0;
	bp->b_errs = 0;
	bwrite(fd, bp->b_un.b_buf, bp->b_bno, (long)bp->b_size);
	if (bp != &sblk)
		return;
	for (i = 0, j = 0; i < sblock.fs_cssize; i += sblock.fs_bsize, j++) {
		bwrite(fswritefd, (char *)sblock.fs_csp + i,
		    fsbtodb(&sblock, sblock.fs_csaddr + j * sblock.fs_frag),
		    sblock.fs_cssize - i < sblock.fs_bsize ?
		    sblock.fs_cssize - i : sblock.fs_bsize);
	}
}

static void
rwerror(char *mesg, daddr_t blk)
{

	if (preen == 0)
		printf("\n");
	pfatal("CANNOT %s: BLK %lld", mesg, (long long)blk);
	if (reply("CONTINUE") == 0)
		errexit("Program terminated\n");
}

void
ckfini(int markclean)
{
	struct bufarea *bp, *nbp;
	int cnt = 0;
	sigset_t oset, nset;
	int64_t sblockloc;

	sigemptyset(&nset);
	sigaddset(&nset, SIGINT);
	sigprocmask(SIG_BLOCK, &nset, &oset);

	if (fswritefd < 0) {
		(void)close(fsreadfd);
		fsreadfd = -1;
		sigprocmask(SIG_SETMASK, &oset, NULL);
		return;
	}
	if (sblock.fs_magic == FS_UFS1_MAGIC) {
		sblockloc = SBLOCK_UFS1;
		sblock.fs_ffs1_time = sblock.fs_time;
		sblock.fs_ffs1_size = sblock.fs_size;
		sblock.fs_ffs1_dsize = sblock.fs_dsize;
		sblock.fs_ffs1_csaddr = sblock.fs_csaddr;
		sblock.fs_ffs1_cstotal.cs_ndir = sblock.fs_cstotal.cs_ndir;
		sblock.fs_ffs1_cstotal.cs_nbfree = sblock.fs_cstotal.cs_nbfree;
		sblock.fs_ffs1_cstotal.cs_nifree = sblock.fs_cstotal.cs_nifree;
		sblock.fs_ffs1_cstotal.cs_nffree = sblock.fs_cstotal.cs_nffree;
		/* Force update on next mount */
		sblock.fs_ffs1_flags &= ~FS_FLAGS_UPDATED;
	} else
		sblockloc = SBLOCK_UFS2;
	flush(fswritefd, &sblk);
	if (havesb && sblk.b_bno != sblockloc / DEV_BSIZE && !preen &&
	    reply("UPDATE STANDARD SUPERBLOCK")) {
		sblk.b_bno = sblockloc / DEV_BSIZE;
		sbdirty();
		flush(fswritefd, &sblk);
	}
	flush(fswritefd, &cgblk);
	free(cgblk.b_un.b_buf);
	for (bp = bufhead.b_prev; bp && bp != &bufhead; bp = nbp) {
		cnt++;
		flush(fswritefd, bp);
		nbp = bp->b_prev;
		free(bp->b_un.b_buf);
		free(bp);
	}
	if (bufhead.b_size != cnt)
		errexit("Panic: lost %d buffers\n", bufhead.b_size - cnt);
	if (cgbufs != NULL) {	
		for (cnt = 0; cnt < sblock.fs_ncg; cnt++) {
			if (cgbufs[cnt].b_un.b_cg == NULL)
				continue;
			flush(fswritefd, &cgbufs[cnt]);
			free(cgbufs[cnt].b_un.b_cg);
		}
		free(cgbufs);
	}
	pbp = pdirbp = NULL;
	if (markclean && (sblock.fs_clean & FS_ISCLEAN) == 0) {
		/*
		 * Mark the file system as clean, and sync the superblock.
		 */
		if (preen)
			pwarn("MARKING FILE SYSTEM CLEAN\n");
		else if (!reply("MARK FILE SYSTEM CLEAN"))
			markclean = 0;
		if (markclean) {
			sblock.fs_clean = FS_ISCLEAN;
			sbdirty();
			flush(fswritefd, &sblk);
		}
	}
	if (debug)
		printf("cache missed %ld of %ld (%d%%)\n", diskreads,
		    totalreads, (int)(diskreads * 100 / totalreads));
	(void)close(fsreadfd);
	fsreadfd = -1;
	(void)close(fswritefd);
	fswritefd = -1;
	sigprocmask(SIG_SETMASK, &oset, NULL);
}

int
bread(int fd, char *buf, daddr_t blk, long size)
{
	char *cp;
	int i, errs;
	off_t offset;

	offset = blk;
	offset *= DEV_BSIZE;
	if (pread(fd, buf, size, offset) == size)
		return (0);
	rwerror("READ", blk);
	errs = 0;
	memset(buf, 0, (size_t)size);
	printf("THE FOLLOWING DISK SECTORS COULD NOT BE READ:");
	for (cp = buf, i = 0; i < size; i += secsize, cp += secsize) {
		if (pread(fd, cp, secsize, offset + i) != secsize) {
			if (secsize != DEV_BSIZE)
				printf(" %lld (%lld),",
				    (long long)(offset + i) / secsize,
				    (long long)blk + i / DEV_BSIZE);
			else
				printf(" %lld,", (long long)blk +
				    i / DEV_BSIZE);
			errs++;
		}
	}
	printf("\n");
	return (errs);
}

void
bwrite(int fd, char *buf, daddr_t blk, long size)
{
	int i;
	char *cp;
	off_t offset;

	if (fd < 0)
		return;
	offset = blk;
	offset *= DEV_BSIZE;
	if (pwrite(fd, buf, size, offset) == size) {
		fsmodified = 1;
		return;
	}
	rwerror("WRITE", blk);
	printf("THE FOLLOWING SECTORS COULD NOT BE WRITTEN:");
	for (cp = buf, i = 0; i < size; i += secsize, cp += secsize)
		if (pwrite(fd, cp, secsize, offset + i) != secsize) {
			if (secsize != DEV_BSIZE)
				printf(" %lld (%lld),",
				    (long long)(offset + i) / secsize,
				    (long long)blk + i / DEV_BSIZE);
			else
				printf(" %lld,", (long long)blk +
				    i / DEV_BSIZE);
		}
	printf("\n");
	return;
}

/*
 * allocate a data block with the specified number of fragments
 */
daddr_t
allocblk(int frags)
{
	daddr_t i, baseblk;
	int j, k, cg;
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
			if (!cg_chkmagic(cgp))
				pfatal("CG %d: BAD MAGIC NUMBER\n", cg);
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
			return (i + j);
		}
	}
	return (0);
}

/*
 * Free a previously allocated block
 */
void
freeblk(daddr_t blkno, int frags)
{
	struct inodesc idesc;

	idesc.id_blkno = blkno;
	idesc.id_numfrags = frags;
	(void)pass4check(&idesc);
}

/*
 * Find a pathname
 */
void
getpathname(char *namebuf, size_t namebuflen, ino_t curdir, ino_t ino)
{
	int len;
	char *cp;
	struct inodesc idesc;
	static int busy = 0;

	if (curdir == ino && ino == ROOTINO) {
		(void)strlcpy(namebuf, "/", namebuflen);
		return;
	}
	if (busy ||
	    (GET_ISTATE(curdir) != DSTATE && GET_ISTATE(curdir) != DFOUND)) {
		(void)strlcpy(namebuf, "?", namebuflen);
		return;
	}
	busy = 1;
	memset(&idesc, 0, sizeof(struct inodesc));
	idesc.id_type = DATA;
	idesc.id_fix = IGNORE;
	cp = &namebuf[PATH_MAX - 1];
	*cp = '\0';
	if (curdir != ino) {
		idesc.id_parent = curdir;
		goto namelookup;
	}
	while (ino != ROOTINO) {
		idesc.id_number = ino;
		idesc.id_func = findino;
		idesc.id_name = "..";
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
		if (cp < &namebuf[MAXNAMLEN])
			break;
		ino = idesc.id_number;
	}
	busy = 0;
	if (ino != ROOTINO)
		*--cp = '?';
	memmove(namebuf, cp, (size_t)(&namebuf[PATH_MAX] - cp));
}

void
catch(int signo)
{
	ckfini(0);			/* XXX signal race */
	_exit(12);
}

/*
 * When preening, allow a single quit to signal
 * a special exit after filesystem checks complete
 * so that reboot sequence may be interrupted.
 */
void
catchquit(int signo)
{
	extern volatile sig_atomic_t returntosingle;
	static const char message[] =
	    "returning to single-user after filesystem check\n";

	write(STDOUT_FILENO, message, sizeof(message)-1);
	returntosingle = 1;
	(void)signal(SIGQUIT, SIG_DFL);
}

/*
 * Ignore a single quit signal; wait and flush just in case.
 * Used by child processes in preen.
 */
void
voidquit(int signo)
{
	int save_errno = errno;

	sleep(1);
	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGQUIT, SIG_DFL);
	errno = save_errno;
}

/*
 * determine whether an inode should be fixed.
 */
int
dofix(struct inodesc *idesc, char *msg)
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
		errexit("UNKNOWN INODESC FIX MODE %u\n", idesc->id_fix);
	}
	/* NOTREACHED */
}

int (* info_fn)(char *, size_t) = NULL;
char *info_filesys = "?";

void
catchinfo(int signo)
{
	static int info_fd;
	int save_errno = errno;
	struct iovec iov[4];
	char buf[1024];

	if (signo == 0) {
		info_fd = open(_PATH_TTY, O_WRONLY);
		signal(SIGINFO, catchinfo);
	} else if (info_fd > 0 && info_fn != NULL && info_fn(buf, sizeof buf)) {
		iov[0].iov_base = info_filesys;
		iov[0].iov_len = strlen(info_filesys);
		iov[1].iov_base = ": ";
		iov[1].iov_len = sizeof ": " - 1;
		iov[2].iov_base = buf;
		iov[2].iov_len = strlen(buf);
		iov[3].iov_base = "\n";
		iov[3].iov_len = sizeof "\n" - 1;

		writev(info_fd, iov, 4);
	}
	errno = save_errno;
}
/*
 * Attempt to flush a cylinder group cache entry.
 * Return whether the flush was successful.
 */
static int
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
 * Wrapper for malloc() that flushes the cylinder group cache to try
 * to get space.
 */
void *
Malloc(size_t size)
{
	void *retval;

	while ((retval = malloc(size)) == NULL)
		if (flushentry() == 0)
			break;
	return (retval);
}

/*
 * Wrapper for calloc() that flushes the cylinder group cache to try
 * to get space.
 */
void*
Calloc(size_t cnt, size_t size)
{
	void *retval;

	while ((retval = calloc(cnt, size)) == NULL)
		if (flushentry() == 0)
			break;
	return (retval);
}

/*
 * Wrapper for reallocarray() that flushes the cylinder group cache to try
 * to get space.
 */
void*
Reallocarray(void *p, size_t cnt, size_t size)
{
	void *retval;

	while ((retval = reallocarray(p, cnt, size)) == NULL)
		if (flushentry() == 0)
			break;
	return (retval);
}
