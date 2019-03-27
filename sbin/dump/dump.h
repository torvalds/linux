/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1993
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
 *
 *	@(#)dump.h	8.2 (Berkeley) 4/28/95
 *
 * $FreeBSD$
 */

/*
 * Dump maps used to describe what is to be dumped.
 */
int	mapsize;	/* size of the state maps */
char	*usedinomap;	/* map of allocated inodes */
char	*dumpdirmap;	/* map of directories to be dumped */
char	*dumpinomap;	/* map of files to be dumped */
/*
 * Map manipulation macros.
 */
#define	SETINO(ino, map) \
	map[(u_int)((ino) - 1) / CHAR_BIT] |= \
	    1 << ((u_int)((ino) - 1) % CHAR_BIT)
#define	CLRINO(ino, map) \
	map[(u_int)((ino) - 1) / CHAR_BIT] &= \
	    ~(1 << ((u_int)((ino) - 1) % CHAR_BIT))
#define	TSTINO(ino, map) \
	(map[(u_int)((ino) - 1) / CHAR_BIT] & \
	    (1 << ((u_int)((ino) - 1) % CHAR_BIT)))

/*
 *	All calculations done in 0.1" units!
 */
char	*disk;		/* name of the disk file */
char	*tape;		/* name of the tape file */
char	*popenout;	/* popen(3) per-"tape" command */
char	*dumpdates;	/* name of the file containing dump date information*/
char	*temp;		/* name of the file for doing rewrite of dumpdates */
int	lastlevel;	/* dump level of previous dump */
int	level;		/* dump level of this dump */
int	uflag;		/* update flag */
int	diskfd;		/* disk file descriptor */
int	tapefd;		/* tape file descriptor */
int	pipeout;	/* true => output to standard output */
ino_t	curino;		/* current inumber; used globally */
int	newtape;	/* new tape flag */
int	density;	/* density in 0.1" units */
long	tapesize;	/* estimated tape size, blocks */
long	tsize;		/* tape size in 0.1" units */
long	asize;		/* number of 0.1" units written on current tape */
int	etapes;		/* estimated number of tapes */
int	nonodump;	/* if set, do not honor UF_NODUMP user flags */
int	unlimited;	/* if set, write to end of medium */
int	cachesize;	/* size of block cache in bytes */
int	rsync_friendly;	/* be friendly with rsync */

int	notify;		/* notify operator flag */
int	blockswritten;	/* number of blocks written on current tape */
int	tapeno;		/* current tape number */
time_t	tstart_writing;	/* when started writing the first tape block */
time_t	tend_writing;	/* after writing the last tape block */
int	passno;		/* current dump pass number */
struct	fs *sblock;	/* the file system super block */
long	dev_bsize;	/* block size of underlying disk device */
int	dev_bshift;	/* log2(dev_bsize) */
int	tp_bshift;	/* log2(TP_BSIZE) */

/* operator interface functions */
void	broadcast(const char *message);
void	infosch(int);
void	lastdump(int arg);	/* int should be char */
void	msg(const char *fmt, ...) __printflike(1, 2);
void	msgtail(const char *fmt, ...) __printflike(1, 2);
int	query(const char *question);
void	quit(const char *fmt, ...) __printflike(1, 2);
void	timeest(void);
time_t	unctime(char *str);

/* mapping rouintes */
union	dinode;
int	mapfiles(ino_t maxino, long *tapesize);
int	mapdirs(ino_t maxino, long *tapesize);

/* file dumping routines */
void	blkread(ufs2_daddr_t blkno, char *buf, int size);
ssize_t cread(int fd, void *buf, size_t nbytes, off_t offset);
void	dumpino(union dinode *dp, ino_t ino);
void	dumpmap(char *map, int type, ino_t ino);
void	writeheader(ino_t ino);

/* tape writing routines */
int	alloctape(void);
void	close_rewind(void);
void	dumpblock(ufs2_daddr_t blkno, int size);
void	startnewtape(int top);
void	trewind(void);
void	writerec(char *dp, int isspcl);

void	Exit(int status) __dead2;
void	dumpabort(int signo) __dead2;
void	dump_getfstab(void);

char	*rawname(char *cp);
union	dinode *getino(ino_t inum, int *mode);

/* rdump routines */
#ifdef RDUMP
void	rmtclose(void);
int	rmthost(const char *host);
int	rmtopen(const char *tape, int mode);
int	rmtwrite(const char *buf, int count);
#endif /* RDUMP */

void	interrupt(int signo);	/* in case operator bangs on console */

/*
 *	Exit status codes
 */
#define	X_FINOK		0	/* normal exit */
#define	X_STARTUP	1	/* startup error */
#define	X_REWRITE	2	/* restart writing from the check point */
#define	X_ABORT		3	/* abort dump; don't attempt checkpointing */

#define	OPGRENT	"operator"		/* group entry to notify */

struct	fstab *fstabsearch(const char *key); /* search fs_file and fs_spec */

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

/*
 *	The contents of the file _PATH_DUMPDATES is maintained both on
 *	a linked list, and then (eventually) arrayified.
 */
struct dumpdates {
	char	dd_name[NAME_MAX+3];
	int	dd_level;
	time_t	dd_ddate;
};
int	nddates;		/* number of records (might be zero) */
struct	dumpdates **ddatev;	/* the arrayfied version */
void	initdumptimes(void);
void	getdumptime(void);
void	putdumptime(void);
#define	ITITERATE(i, ddp) \
    	if (ddatev != NULL) \
		for (ddp = ddatev[i = 0]; i < nddates; ddp = ddatev[++i])

#define	DUMPFMTLEN	53			/* max device pathname length */
#define	DUMPOUTFMT	"%-*s %d %s"		/* for printf */
						/* name, level, ctime(date) */
#define	DUMPINFMT	"%s %d %[^\n]\n"	/* inverse for scanf */

void	sig(int signo);

#ifndef	_PATH_FSTAB
#define	_PATH_FSTAB	"/etc/fstab"
#endif
