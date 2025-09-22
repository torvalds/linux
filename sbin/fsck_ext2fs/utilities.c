/*	$OpenBSD: utilities.c,v 1.29 2023/03/08 04:43:06 guenther Exp $	*/
/*	$NetBSD: utilities.c,v 1.6 2001/02/04 21:19:34 christos Exp $	*/

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

#include <sys/param.h>	/* DEV_BSIZE isset setbit */
#include <sys/time.h>
#include <sys/signal.h>
#include <ufs/ext2fs/ext2fs_dinode.h>
#include <ufs/ext2fs/ext2fs_dir.h>
#include <ufs/ext2fs/ext2fs.h>
#include <ufs/ufs/dinode.h> /* for IFMT & friends */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

#include "fsutil.h"
#include "fsck.h"
#include "extern.h"

long	diskreads, totalreads;	/* Disk cache statistics */

static void rwerror(char *, daddr32_t);

int
ftypeok(struct ext2fs_dinode *dp)
{
	switch (letoh16(dp->e2di_mode) & IFMT) {

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
			printf("bad file type 0%o\n", letoh16(dp->e2di_mode));
		return (0);
	}
}

int
reply(char *question)
{
	int persevere;
	int c;

	if (preen)
		pfatal("INTERNAL ERROR: GOT TO reply()");
	persevere = !strcmp(question, "CONTINUE");
	printf("\n");
	if (!persevere && (nflag || fswritefd < 0)) {
		printf("%s? no\n\n", question);
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
		while (c != '\n' && getc(stdin) != '\n')
			if (feof(stdin))
				return (0);
	} while (c != 'y' && c != 'Y' && c != 'n' && c != 'N');
	printf("\n");
	if (c == 'y' || c == 'Y')
		return (1);
	return (0);
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

	diskreads = totalreads = 0;
	pbp = pdirbp = NULL;
	bufhead.b_next = bufhead.b_prev = &bufhead;
	bufcnt = MAXBUFSPACE / sblock.e2fs_bsize;
	if (bufcnt < MINBUFS)
		bufcnt = MINBUFS;
	for (i = 0; i < bufcnt; i++) {
		bp = malloc(sizeof(struct bufarea));
		bufp = malloc((unsigned int)sblock.e2fs_bsize);
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
 * Manage a cache of directory blocks.
 */
struct bufarea *
getdatablk(daddr32_t blkno, long size)
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
	diskreads++;
	/* fall through */
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
getblk(struct bufarea *bp, daddr32_t blk, long size)
{
	daddr32_t dblk;

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
	int i;

	if (!bp->b_dirty)
		return;
	if (bp->b_errs != 0)
		pfatal("WRITING %sZERO'ED BLOCK %d TO DISK\n",
		    (bp->b_errs == bp->b_size / DEV_BSIZE) ? "" : "PARTIALLY ",
		    bp->b_bno);
	bp->b_dirty = 0;
	bp->b_errs = 0;
	bwrite(fd, bp->b_un.b_buf, bp->b_bno, (long)bp->b_size);
	if (bp != &sblk)
		return;
	for (i = 0; i < sblock.e2fs_ngdb; i++) {
		bwrite(fswritefd, (char *)
			&sblock.e2fs_gd[i* sblock.e2fs_bsize / sizeof(struct ext2_gd)],
		    fsbtodb(&sblock, ((sblock.e2fs_bsize>1024)?0:1)+i+1),
		    sblock.e2fs_bsize);
	}
}

static void
rwerror(char *mesg, daddr32_t blk)
{

	if (preen == 0)
		printf("\n");
	pfatal("CANNOT %s: BLK %d", mesg, blk);
	if (reply("CONTINUE") == 0)
		errexit("Program terminated\n");
}

void
ckfini(int markclean)
{
	struct bufarea *bp, *nbp;
	int cnt = 0;

	if (fswritefd < 0) {
		(void)close(fsreadfd);
		return;
	}
	flush(fswritefd, &sblk);
	if (havesb && sblk.b_bno != SBOFF / DEV_BSIZE &&
	    !preen && reply("UPDATE STANDARD SUPERBLOCKS")) {
		sblk.b_bno = SBOFF / DEV_BSIZE;
		sbdirty();
		flush(fswritefd, &sblk);
		copyback_sb(&asblk);
		asblk.b_dirty = 1;
		flush(fswritefd, &asblk);
	}
	for (bp = bufhead.b_prev; bp && bp != &bufhead; bp = nbp) {
		cnt++;
		flush(fswritefd, bp);
		nbp = bp->b_prev;
		free(bp->b_un.b_buf);
		free((char *)bp);
	}
	if (bufhead.b_size != cnt)
		errexit("Panic: lost %d buffers\n", bufhead.b_size - cnt);
	pbp = pdirbp = NULL;
	if (markclean && (sblock.e2fs.e2fs_state & E2FS_ISCLEAN) == 0) {
		/*
		 * Mark the file system as clean, and sync the superblock.
		 */
		if (preen)
			pwarn("MARKING FILE SYSTEM CLEAN\n");
		else if (!reply("MARK FILE SYSTEM CLEAN"))
			markclean = 0;
		if (markclean) {
			sblock.e2fs.e2fs_state = E2FS_ISCLEAN;
			sbdirty();
			flush(fswritefd, &sblk);
		}
	}
	if (debug)
		printf("cache missed %ld of %ld (%d%%)\n", diskreads,
		    totalreads, (int)(diskreads * 100 / totalreads));
	(void)close(fsreadfd);
	(void)close(fswritefd);
}

int
bread(int fd, char *buf, daddr32_t blk, long size)
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
bwrite(int fd, char *buf, daddr32_t blk, long size)
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
 * allocate a data block
 */
int
allocblk(void)
{
	int i;

	for (i = 0; i < maxfsblock - 1; i++) {
		if (testbmap(i))
				continue;
		setbmap(i);
		n_blks ++;
		return (i);
	}
	return (0);
}

/*
 * Free a previously allocated block
 */
void
freeblk(daddr32_t blkno)
{
	struct inodesc idesc;

	idesc.id_blkno = blkno;
	idesc.id_numfrags = 1;
	(void)pass4check(&idesc);
}

/*
 * Find a pathname
 */
void
getpathname(char *namebuf, size_t buflen, ino_t curdir, ino_t ino)
{
	size_t len;
	char *cp;
	struct inodesc idesc;
	static int busy = 0;

	if (curdir == ino && ino == EXT2_ROOTINO) {
		(void)strlcpy(namebuf, "/", buflen);
		return;
	}
	if (busy ||
	    (statemap[curdir] != DSTATE && statemap[curdir] != DFOUND)) {

		(void)strlcpy(namebuf, "?", buflen);
		return;
	}
	busy = 1;
	memset(&idesc, 0, sizeof(struct inodesc));
	idesc.id_type = DATA;
	idesc.id_fix = IGNORE;
	cp = &namebuf[buflen - 1];
	*cp = '\0';
	if (curdir != ino) {
		idesc.id_parent = curdir;
		goto namelookup;
	}
	while (ino != EXT2_ROOTINO) {
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
		memmove(cp, namebuf, len);
		*(--cp) = '/';
		if (cp < &namebuf[EXT2FS_MAXNAMLEN])
			break;
		ino = idesc.id_number;
	}
	busy = 0;
	if (ino != EXT2_ROOTINO)
		*(--cp) = '?';
	memmove(namebuf, cp, (size_t)(&namebuf[buflen] - cp));
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
		errexit("UNKNOWN INODESC FIX MODE %d\n", idesc->id_fix);
	}
	/* NOTREACHED */
}
