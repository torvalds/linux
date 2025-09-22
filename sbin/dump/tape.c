/*	$OpenBSD: tape.c,v 1.49 2024/05/09 08:35:40 florian Exp $	*/
/*	$NetBSD: tape.c,v 1.11 1997/06/05 11:13:26 lukem Exp $	*/

/*-
 * Copyright (c) 1980, 1991, 1993
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

#include <sys/param.h>	/* MAXBSIZE DEV_BSIZE */
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ufs/ffs/fs.h>
#include <ufs/ufs/dinode.h>

#include <protocols/dumprestore.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#include "dump.h"
#include "pathnames.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

ino_t   curino; 		/* current inumber; used globally */
int     newtape;		/* new tape flag */
union u_spcl u_spcl;		/* mapping of variables in a control block */

static int tapefd;		/* tape file descriptor */
static int64_t asize;		/* number of 0.1" units written on cur tape */
static int writesize;		/* size of malloc()ed buffer for tape */
static int64_t	lastspclrec = -1; /* tape block number of last written header */
static int trecno = 0;		/* next record to write in current block */
static int64_t blocksthisvol;	/* number of blocks on current output file */
static char *nexttape;

static	ssize_t atomic(ssize_t (*)(int, void *, size_t), int, char *, int);
static	void doslave(int, int);
static	void enslave(void);
static	void flushtape(void);
static	void killall(void);
static	void rollforward(void);

void	tperror(int signo);
void	sigpipe(int signo);
void	proceed(int signo);

/*
 * Concurrent dump mods (Caltech) - disk block reading and tape writing
 * are exported to several slave processes.  While one slave writes the
 * tape, the others read disk blocks; they pass control of the tape in
 * a ring via signals. The parent process traverses the filesystem and
 * sends writeheader()'s and lists of daddr's to the slaves via pipes.
 * The following structure defines the instruction packets sent to slaves.
 */
struct req {
	daddr_t dblk;
	int count;
};
static int reqsiz;

#define SLAVES 3		/* 1 slave writing, 1 reading, 1 for slack */
static struct slave {
	int64_t tapea;		/* header number at start of this chunk */
	int64_t firstrec;	/* record number of this block */
	int count;		/* count to next header (used for TS_TAPE */
				/* after EOT) */
	int inode;		/* inode that we are currently dealing with */
	int fd;			/* FD for this slave */
	pid_t pid;		/* PID for this slave */
	int sent;		/* 1 == we've sent this slave requests */
	char (*tblock)[TP_BSIZE]; /* buffer for data blocks */
	struct req *req;	/* buffer for requests */
} slaves[SLAVES+1];
static struct slave *slp;

static char	(*nextblock)[TP_BSIZE];

static time_t tstart_volume;	/* time of volume start */
static int64_t tapea_volume;	/* value of spcl.c_tapea at volume start */

static pid_t master;		/* pid of master, for sending error signals */
static int tenths;		/* length of tape used per block written */
static volatile sig_atomic_t caught;	/* have we caught the signal to proceed? */

int
alloctape(void)
{
	int pgoff = getpagesize() - 1;
	char *buf;
	int i;

	writesize = ntrec * TP_BSIZE;
	reqsiz = (ntrec + 1) * sizeof(struct req);
	/*
	 * CDC 92181's and 92185's make 0.8" gaps in 1600-bpi start/stop mode
	 * (see DEC TU80 User's Guide).  The shorter gaps of 6250-bpi require
	 * repositioning after stopping, i.e, streaming mode, where the gap is
	 * variable, 0.30" to 0.45".  The gap is maximal when the tape stops.
	 */
	if (blocksperfile == 0 && !unlimited)
		tenths = writesize / density +
		    (cartridge ? 16 : density == 625 ? 5 : 8);
	/*
	 * Allocate tape buffer contiguous with the array of instruction
	 * packets, so flushtape() can write them together with one write().
	 * Align tape buffer on page boundary to speed up tape write().
	 */
	for (i = 0; i <= SLAVES; i++) {
		buf = malloc((unsigned)(reqsiz + writesize + pgoff + TP_BSIZE));
		if (buf == NULL)
			return(0);
		slaves[i].tblock = (char (*)[TP_BSIZE])
		    (((long)&buf[ntrec + 1] + pgoff) &~ pgoff);
		slaves[i].req = (struct req *)slaves[i].tblock - ntrec - 1;
	}
	slp = &slaves[0];
	slp->count = 1;
	slp->tapea = 0;
	slp->firstrec = 0;
	nextblock = slp->tblock;
	return(1);
}

void
writerec(char *dp, int isspcl)
{

	slp->req[trecno].dblk = 0;
	slp->req[trecno].count = 1;
	*(union u_spcl *)(*(nextblock)++) = *(union u_spcl *)dp;
	if (isspcl)
		lastspclrec = spcl.c_tapea;
	trecno++;
	spcl.c_tapea++;
	if (trecno >= ntrec)
		flushtape();
}

void
dumpblock(daddr_t blkno, int size)
{
	int avail, tpblks;
	daddr_t dblkno;

	dblkno = fsbtodb(sblock, blkno);
	tpblks = size >> tp_bshift;
	while ((avail = MINIMUM(tpblks, ntrec - trecno)) > 0) {
		slp->req[trecno].dblk = dblkno;
		slp->req[trecno].count = avail;
		trecno += avail;
		spcl.c_tapea += avail;
		if (trecno >= ntrec)
			flushtape();
		dblkno += avail << (tp_bshift - (ffs(DEV_BSIZE) - 1));
		tpblks -= avail;
	}
}

int	nogripe = 0;

void
tperror(int signo)
{
	/* XXX - signal races */

	if (pipeout) {
		msg("write error on %s\n", tape);
		quit("Cannot recover\n");
		/* NOTREACHED */
	}
	msg("write error %lld blocks into volume %d\n",
	    (long long)blocksthisvol, tapeno);
	broadcast("DUMP WRITE ERROR!\n");
	if (!query("Do you want to restart?"))
		dumpabort(0);
	msg("Closing this volume.  Prepare to restart with new media;\n");
	msg("this dump volume will be rewritten.\n");
	killall();
	nogripe = 1;
	close_rewind();
	Exit(X_REWRITE);
}

void
sigpipe(int signo)
{

	quit("Broken pipe\n");
}

/*
 * do_stats --
 *	Update xferrate stats
 */
time_t
do_stats(void)
{
	time_t tnow, ttaken;
	int64_t blocks;
	char *ct;

	(void)time(&tnow);
	ttaken = tnow - tstart_volume;
	blocks = spcl.c_tapea - tapea_volume;
	ct = ctime(&tnow);
	msg("Volume %d completed at: %s", tapeno, ct ? ct : "?\n");
	if (ttaken > 0) {
		msg("Volume %d took %lld:%02lld:%02lld\n", tapeno,
		    (long long)ttaken / 3600, ((long long)ttaken % 3600) / 60,
		    (long long)ttaken % 60);
		blocks /= ttaken;
		msg("Volume %d transfer rate: %lld KB/s\n", tapeno, blocks);
		xferrate += blocks;
	}
	return(tnow);
}

/*
 * statussig --
 *	information message upon receipt of SIGINFO
 *	(derived from optr.c::timeest())
 * XXX not safe
 */
void
statussig(int signo)
{
	time_t	tnow, deltat;
	int save_errno = errno;

	if (blockswritten < 500)
		return;
	(void) time(&tnow);
	deltat = tstart_writing - tnow + (1.0 * (tnow - tstart_writing))
		/ blockswritten * tapesize;
	/* XXX not safe due to floating point printf */
	dprintf(STDERR_FILENO,
	    "dump: %s %3.2f%% done at %lld KB/s, finished in %d:%02d\n",
	    tape, (blockswritten * 100.0) / tapesize,
	    (spcl.c_tapea - tapea_volume) / (tnow - tstart_volume),
	    (int)(deltat / 3600), (int)((deltat % 3600) / 60));
	errno = save_errno;
}

static void
flushtape(void)
{
	int i, blks, got;
	int64_t lastfirstrec;

	int siz = (char *)nextblock - (char *)slp->req;

	slp->req[trecno].count = 0;			/* Sentinel */

	if (atomic((ssize_t (*)(int, void *, size_t))write, slp->fd,
	    (char *)slp->req, siz) != siz)
		quit("error writing command pipe: %s\n", strerror(errno));
	slp->sent = 1; /* we sent a request, read the response later */

	lastfirstrec = slp->firstrec;

	if (++slp >= &slaves[SLAVES])
		slp = &slaves[0];

	/* Read results back from next slave */
	if (slp->sent) {
		if (atomic(read, slp->fd, (char *)&got, sizeof(got))
		    != sizeof(got)) {
			perror("  DUMP: error reading command pipe in master");
			dumpabort(0);
		}
		slp->sent = 0;

		/* Check for end of tape */
		if (got < writesize) {
			msg("End of tape detected\n");

			/*
			 * Drain the results, don't care what the values were.
			 * If we read them here then trewind won't...
			 */
			for (i = 0; i < SLAVES; i++) {
				if (slaves[i].sent) {
					if (atomic(read, slaves[i].fd,
					    (char *)&got, sizeof(got))
					    != sizeof(got)) {
						perror("  DUMP: error reading command pipe in master");
						dumpabort(0);
					}
					slaves[i].sent = 0;
				}
			}

			close_rewind();
			rollforward();
			return;
		}
	}

	blks = 0;
	if (spcl.c_type != TS_END && spcl.c_type != TS_CLRI &&
	    spcl.c_type != TS_BITS) {
		if (spcl.c_count > TP_NINDIR)
			quit("c_count too large\n");
		for (i = 0; i < spcl.c_count; i++)
			if (spcl.c_addr[i] != 0)
				blks++;
	}
	slp->count = lastspclrec + blks + 1 - spcl.c_tapea;
	slp->tapea = spcl.c_tapea;
	slp->firstrec = lastfirstrec + ntrec;
	slp->inode = curino;
	nextblock = slp->tblock;
	trecno = 0;
	asize += tenths;
	blockswritten += ntrec;
	blocksthisvol += ntrec;
	if (!pipeout && !unlimited && (blocksperfile ?
	    (blocksthisvol >= blocksperfile) : (asize > tsize))) {
		close_rewind();
		startnewtape(0);
	}
	timeest();
}

void
trewind(void)
{
	struct stat sb;
	int f, got;

	for (f = 0; f < SLAVES; f++) {
		/*
		 * Drain the results, but unlike EOT we DO (or should) care
		 * what the return values were, since if we detect EOT after
		 * we think we've written the last blocks to the tape anyway,
		 * we have to replay those blocks with rollforward.
		 *
		 * fixme: punt for now.
		 */
		if (slaves[f].sent) {
			if (atomic(read, slaves[f].fd, (char *)&got, sizeof(got))
			    != sizeof(got)) {
				perror("  DUMP: error reading command pipe in master");
				dumpabort(0);
			}
			slaves[f].sent = 0;
			if (got != writesize) {
				msg("EOT detected in last 2 tape records!\n");
				msg("Use a longer tape, decrease the size estimate\n");
				quit("or use no size estimate at all.\n");
			}
		}
		(void) close(slaves[f].fd);
	}
	while (wait((int *)NULL) >= 0)	/* wait for any signals from slaves */
		/* void */;

	if (pipeout)
		return;

	msg("Closing %s\n", tape);

#ifdef RDUMP
	if (host) {
		rmtclose();
		while (rmtopen(tape, O_RDONLY) < 0)
			sleep(10);
		rmtclose();
		return;
	}
#endif
	/*
	 * st(4) says: "Bit 1 of the minor number specifies whether an eject is
	 * attempted when the device is closed.  When it is set, the device
	 * will attempt to eject its media on close ...".
	 *
	 * If the tape has been ejected, looping on open() will generate 'Media
	 * not present' errors until a tape is loaded. Once loaded the tape
	 * will be immediately ejected as a result of the second close().
	 *
	 * So if the tape will be ejected, just close and return.
	 */
	if ((fstat(tapefd, &sb) == 0) && (minor(sb.st_rdev) & 0x02)) {
		(void) close(tapefd);
		return;
	}

	(void) close(tapefd);
	while ((f = open(tape, O_RDONLY)) == -1)
		sleep (10);
	(void) close(f);
}

void
close_rewind(void)
{
	trewind();
	(void)do_stats();
	if (nexttape)
		return;
	if (!nogripe) {
		msg("Change Volumes: Mount volume #%d\n", tapeno+1);
		broadcast("CHANGE DUMP VOLUMES!\7\7\n");
	}
	while (!query("Is the new volume mounted and ready to go?"))
		if (query("Do you want to abort?")) {
			dumpabort(0);
			/*NOTREACHED*/
		}
}

void
rollforward(void)
{
	struct req *p, *q, *prev;
	struct slave *tslp;
	int i, size, got;
	int64_t savedtapea;
	union u_spcl *ntb, *otb;
	tslp = &slaves[SLAVES];
	ntb = (union u_spcl *)tslp->tblock[1];

	/*
	 * Each of the N slaves should have requests that need to
	 * be replayed on the next tape.  Use the extra slave buffers
	 * (slaves[SLAVES]) to construct request lists to be sent to
	 * each slave in turn.
	 */
	for (i = 0; i < SLAVES; i++) {
		q = &tslp->req[1];
		otb = (union u_spcl *)slp->tblock;

		/*
		 * For each request in the current slave, copy it to tslp.
		 */

		prev = NULL;
		for (p = slp->req; p->count > 0; p += p->count) {
			*q = *p;
			if (p->dblk == 0)
				*ntb++ = *otb++; /* copy the datablock also */
			prev = q;
			q += q->count;
		}
		if (prev == NULL)
			quit("rollforward: protocol botch\n");
		if (prev->dblk != 0)
			prev->count -= 1;
		else
			ntb--;
		q -= 1;
		q->count = 0;
		q = &tslp->req[0];
		if (i == 0) {
			q->dblk = 0;
			q->count = 1;
			trecno = 0;
			nextblock = tslp->tblock;
			savedtapea = spcl.c_tapea;
			spcl.c_tapea = slp->tapea;
			startnewtape(0);
			spcl.c_tapea = savedtapea;
			lastspclrec = savedtapea - 1;
		}
		size = (char *)ntb - (char *)q;
		if (atomic((ssize_t (*)(int, void *, size_t))write,
		    slp->fd, (char *)q, size) != size) {
			perror("  DUMP: error writing command pipe");
			dumpabort(0);
		}
		slp->sent = 1;
		if (++slp >= &slaves[SLAVES])
			slp = &slaves[0];

		q->count = 1;

		if (prev->dblk != 0) {
			/*
			 * If the last one was a disk block, make the
			 * first of this one be the last bit of that disk
			 * block...
			 */
			q->dblk = prev->dblk +
				prev->count * (TP_BSIZE / DEV_BSIZE);
			ntb = (union u_spcl *)tslp->tblock;
		} else {
			/*
			 * It wasn't a disk block.  Copy the data to its
			 * new location in the buffer.
			 */
			q->dblk = 0;
			*((union u_spcl *)tslp->tblock) = *ntb;
			ntb = (union u_spcl *)tslp->tblock[1];
		}
	}
	slp->req[0] = *q;
	nextblock = slp->tblock;
	if (q->dblk == 0)
		nextblock++;
	trecno = 1;

	/*
	 * Clear the first slaves' response.  One hopes that it
	 * worked ok, otherwise the tape is much too short!
	 */
	if (slp->sent) {
		if (atomic(read, slp->fd, (char *)&got, sizeof(got))
		    != sizeof(got)) {
			perror("  DUMP: error reading command pipe in master");
			dumpabort(0);
		}
		slp->sent = 0;

		if (got != writesize) {
			quit("EOT detected at start of the tape!\n");
		}
	}
}

/*
 * We implement taking and restoring checkpoints on the tape level.
 * When each tape is opened, a new process is created by forking; this
 * saves all of the necessary context in the parent.  The child
 * continues the dump; the parent waits around, saving the context.
 * If the child returns X_REWRITE, then it had problems writing that tape;
 * this causes the parent to fork again, duplicating the context, and
 * everything continues as if nothing had happened.
 */
void
startnewtape(int top)
{
	pid_t	parentpid;
	pid_t	childpid;
	int	status;
	pid_t	waitingpid;
	char	*p, *ct;
	sig_t	interrupt_save;

	interrupt_save = signal(SIGINT, SIG_IGN);
	parentpid = getpid();
	tapea_volume = spcl.c_tapea;
	(void)time(&tstart_volume);

restore_check_point:
	(void)signal(SIGINT, interrupt_save);
	/*
	 *	All signals are inherited...
	 */
	childpid = fork();
	if (childpid == -1) {
		msg("Context save fork fails in parent %d\n", parentpid);
		Exit(X_ABORT);
	}
	if (childpid != 0) {
		/*
		 *	PARENT:
		 *	save the context by waiting
		 *	until the child doing all of the work returns.
		 *	don't catch the interrupt
		 */
		signal(SIGINT, SIG_IGN);
#ifdef TDEBUG
		msg("Tape: %d; parent process: %d child process %d\n",
			tapeno+1, parentpid, childpid);
#endif /* TDEBUG */
		while ((waitingpid = wait(&status)) != childpid)
			msg("Parent %d waiting for child %d has another child %d return\n",
				parentpid, childpid, waitingpid);
		if (status & 0xFF) {
			msg("Child %d returns LOB status %o\n",
				childpid, status&0xFF);
		}
		status = (status >> 8) & 0xFF;
#ifdef TDEBUG
		switch(status) {
			case X_FINOK:
				msg("Child %d finishes X_FINOK\n", childpid);
				break;
			case X_ABORT:
				msg("Child %d finishes X_ABORT\n", childpid);
				break;
			case X_REWRITE:
				msg("Child %d finishes X_REWRITE\n", childpid);
				break;
			default:
				msg("Child %d finishes unknown %d\n",
					childpid, status);
				break;
		}
#endif /* TDEBUG */
		switch(status) {
			case X_FINOK:
				Exit(X_FINOK);
				break;
			case X_ABORT:
				Exit(X_ABORT);
				break;
			case X_REWRITE:
				goto restore_check_point;
			default:
				msg("Bad return code from dump: %d\n", status);
				Exit(X_ABORT);
		}
		/*NOTREACHED*/
	} else {	/* we are the child; just continue */
#ifdef TDEBUG
		sleep(4);	/* allow time for parent's message to get out */
		msg("Child on Tape %d has parent %d, my pid = %d\n",
			tapeno+1, parentpid, getpid());
#endif /* TDEBUG */
		/*
		 * If we have a name like "/dev/rst0,/dev/rst1",
		 * use the name before the comma first, and save
		 * the remaining names for subsequent volumes.
		 */
		tapeno++;               /* current tape sequence */
		if (nexttape || strchr(tape, ',')) {
			if (nexttape && *nexttape)
				tape = nexttape;
			if ((p = strchr(tape, ',')) != NULL) {
				*p = '\0';
				nexttape = p + 1;
			} else
				nexttape = NULL;
			msg("Dumping volume %d on %s\n", tapeno, tape);
		}
#ifdef RDUMP
		while ((tapefd = (host ? rmtopen(tape, O_WRONLY|O_CREAT) :
			pipeout ? 1 : open(tape, O_WRONLY|O_CREAT, 0666))) == -1)
#else
		while ((tapefd = (pipeout ? 1 :
				  open(tape, O_WRONLY|O_CREAT, 0666))) == -1)
#endif
		    {
			msg("Cannot open output \"%s\".\n", tape);
			if (!query("Do you want to retry the open?"))
				dumpabort(0);
		}

		enslave();  /* Share open tape file descriptor with slaves */

		asize = 0;
		blocksthisvol = 0;
		if (top)
			newtape++;		/* new tape signal */
		spcl.c_count = slp->count;
		/*
		 * measure firstrec in TP_BSIZE units since restore doesn't
		 * know the correct ntrec value...
		 */
		spcl.c_firstrec = slp->firstrec;
		spcl.c_volume++;
		spcl.c_type = TS_TAPE;
		if (sblock->fs_magic != FS_UFS2_MAGIC)
			spcl.c_flags |= DR_NEWHEADER;
		writeheader((ino_t)slp->inode);
		if (sblock->fs_magic != FS_UFS2_MAGIC)
			spcl.c_flags &=~ DR_NEWHEADER;
		ct = ctime(&tstart_volume);
		msg("Volume %d started at: %s", tapeno, ct ? ct : "?\n");
		if (tapeno > 1)
			msg("Volume %d begins with blocks from inode %llu\n",
			    tapeno, (unsigned long long)slp->inode);
	}
}

void
dumpabort(int signo)
{

	if (master != 0 && master != getpid())
		/* Signals master to call dumpabort */
		(void) kill(master, SIGTERM);
	else {
		killall();
		msg("The ENTIRE dump is aborted.\n");
	}
#ifdef RDUMP
	rmtclose();
#endif
	Exit(X_ABORT);
}

__dead void
Exit(int status)
{

#ifdef TDEBUG
	msg("pid = %d exits with status %d\n", getpid(), status);
#endif /* TDEBUG */
	exit(status);
}

/*
 * proceed - handler for SIGUSR2, used to synchronize IO between the slaves.
 */
void
proceed(int signo)
{
	caught++;
}

void
enslave(void)
{
	int cmd[2];
	int i, j;

	master = getpid();

	signal(SIGTERM, dumpabort);  /* Slave sends SIGTERM on dumpabort() */
	signal(SIGPIPE, sigpipe);
	signal(SIGUSR1, tperror);    /* Slave sends SIGUSR1 on tape errors */
	signal(SIGUSR2, proceed);    /* Slave sends SIGUSR2 to next slave */

	for (i = 0; i < SLAVES; i++) {
		if (i == slp - &slaves[0]) {
			caught = 1;
		} else {
			caught = 0;
		}

		if (socketpair(AF_UNIX, SOCK_STREAM, 0, cmd) == -1 ||
		    (slaves[i].pid = fork()) == -1)
			quit("too many slaves, %d (recompile smaller): %s\n",
			    i, strerror(errno));

		slaves[i].fd = cmd[1];
		slaves[i].sent = 0;
		if (slaves[i].pid == 0) { 	    /* Slave starts up here */
			for (j = 0; j <= i; j++)
			        (void) close(slaves[j].fd);
			signal(SIGINT, SIG_IGN);    /* Master handles this */
			signal(SIGINFO, SIG_IGN);
			doslave(cmd[0], i);
			Exit(X_FINOK);
		}
	}

	for (i = 0; i < SLAVES; i++)
		(void) atomic((ssize_t (*)(int, void *, size_t))write,
		    slaves[i].fd, (char *) &slaves[(i + 1) % SLAVES].pid,
		    sizeof(slaves[0].pid));
	master = 0;
}

void
killall(void)
{
	int i;

	for (i = 0; i < SLAVES; i++)
		if (slaves[i].pid > 0) {
			(void) kill(slaves[i].pid, SIGKILL);
			slaves[i].pid = 0;
		}
}

/*
 * Synchronization - each process has a lockfile, and shares file
 * descriptors to the following process's lockfile.  When our write
 * completes, we release our lock on the following process's lock-
 * file, allowing the following process to lock it and proceed. We
 * get the lock back for the next cycle by swapping descriptors.
 */
static void
doslave(int cmd, int slave_number)
{
	int nread, nextslave, size, wrote = 0, eot_count;
	sigset_t nsigset, osigset;

	/*
	 * Need our own seek pointer.
	 */
	(void) close(diskfd);
	if ((diskfd = open(disk, O_RDONLY)) == -1)
		quit("slave couldn't reopen disk: %s\n", strerror(errno));

	/*
	 * Need the pid of the next slave in the loop...
	 */
	if ((nread = atomic(read, cmd, (char *)&nextslave, sizeof(nextslave)))
	    != sizeof(nextslave)) {
		quit("master/slave protocol botched - didn't get pid of next slave.\n");
	}

	/*
	 * Get list of blocks to dump, read the blocks into tape buffer
	 */
	while ((nread = atomic(read, cmd, (char *)slp->req, reqsiz)) == reqsiz) {
		struct req *p = slp->req;

		for (trecno = 0; trecno < ntrec;
		     trecno += p->count, p += p->count) {
			if (p->dblk) {
				bread(p->dblk, slp->tblock[trecno],
					p->count * TP_BSIZE);
			} else {
				if (p->count != 1 || atomic(read, cmd,
				    (char *)slp->tblock[trecno],
				    TP_BSIZE) != TP_BSIZE)
				       quit("master/slave protocol botched.\n");
			}
		}

		sigemptyset(&nsigset);
		sigaddset(&nsigset, SIGUSR2);
		sigprocmask(SIG_BLOCK, &nsigset, &osigset);
		while (!caught)
			sigsuspend(&osigset);
		caught = 0;
		sigprocmask(SIG_SETMASK, &osigset, NULL);

		/* Try to write the data... */
		eot_count = 0;
		size = 0;

		while (eot_count < 10 && size < writesize) {
#ifdef RDUMP
			if (host)
				wrote = rmtwrite(slp->tblock[0]+size,
				    writesize-size);
			else
#endif
				wrote = write(tapefd, slp->tblock[0]+size,
				    writesize-size);
#ifdef WRITEDEBUG
			printf("slave %d wrote %d\n", slave_number, wrote);
#endif
			if (wrote < 0)
				break;
			if (wrote == 0)
				eot_count++;
			size += wrote;
		}

#ifdef WRITEDEBUG
		if (size != writesize)
		 printf("slave %d only wrote %d out of %d bytes and gave up.\n",
		     slave_number, size, writesize);
#endif

		if (eot_count > 0)
			size = 0;

		/*
		 * Handle ENOSPC as an EOT condition
		 */
		if (wrote < 0 && errno == ENOSPC) {
			wrote = 0;
			eot_count++;
		}

		if (size < 0) {
			(void) kill(master, SIGUSR1);
			sigemptyset(&nsigset);
			for (;;)
				sigsuspend(&nsigset);
		} else {
			/*
			 * pass size of write back to master
			 * (for EOT handling)
			 */
			(void) atomic((ssize_t (*)(int, void *, size_t))write,
			    cmd, (char *)&size, sizeof(size));
		}

		/*
		 * If partial write, don't want next slave to go.
		 * Also jolts him awake.
		 */
		(void) kill(nextslave, SIGUSR2);
	}
	if (nread != 0)
		quit("error reading command pipe: %s\n", strerror(errno));
}

/*
 * Since a read from a pipe may not return all we asked for,
 * or a write may not write all we ask if we get a signal,
 * loop until the count is satisfied (or error).
 */
static ssize_t
atomic(ssize_t (*func)(int, void *, size_t), int fd, char *buf, int count)
{
	ssize_t got, need = count;

	while ((got = (*func)(fd, buf, need)) > 0 && (need -= got) > 0)
		buf += got;
	return (got < 0 ? got : count - need);
}
