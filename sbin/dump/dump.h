/*	$OpenBSD: dump.h,v 1.25 2021/01/21 00:16:36 mortimer Exp $	*/
/*	$NetBSD: dump.h,v 1.11 1997/06/05 11:13:20 lukem Exp $	*/

/*-
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
 *	@(#)dump.h	8.1 (Berkeley) 6/5/93
 */

/*
 * Dump maps used to describe what is to be dumped.
 */
extern int	mapsize;	/* size of the state maps */
extern char	*usedinomap;	/* map of allocated inodes */
extern char	*dumpdirmap;	/* map of directories to be dumped */
extern char	*dumpinomap;	/* map of files to be dumped */
/*
 * Map manipulation macros.
 */
#define	SETINO(ino, map) \
	map[(u_int)((ino) - 1) / NBBY] |=  1 << ((u_int)((ino) - 1) % NBBY)
#define	CLRINO(ino, map) \
	map[(u_int)((ino) - 1) / NBBY] &=  ~(1 << ((u_int)((ino) - 1) % NBBY))
#define	TSTINO(ino, map) \
	(map[(u_int)((ino) - 1) / NBBY] &  (1 << ((u_int)((ino) - 1) % NBBY)))

/*
 *	All calculations done in 0.1" units!
 */
extern char	*disk;		/* name of the disk file */
extern char	*tape;		/* name of the tape file */
extern char	*dumpdates;	/* name of the file containing dump date information*/
extern char	*duid;		/* duid of the disk being dumped */
extern char	lastlevel;	/* dump level of previous dump */
extern char	level;		/* dump level of this dump */
extern int	uflag;		/* update flag */
extern int	diskfd;		/* disk file descriptor */
extern int	pipeout;	/* true => output to standard output */
extern ino_t	curino;		/* current inumber; used globally */
extern int	newtape;	/* new tape flag */
extern int	density;	/* density in 0.1" units */
extern int64_t	tapesize;	/* estimated tape size, blocks */
extern int64_t	tsize;		/* tape size in 0.1" units */
extern int	unlimited;	/* if set, write to end of medium */
extern int	etapes;		/* estimated number of tapes */
extern int	nonodump;	/* if set, do not honor UF_NODUMP user flags */
extern int	notify;		/* notify operator flag */
extern int64_t	blockswritten;	/* number of blocks written on current tape */
extern int	tapeno;		/* current tape number */
extern int 	ntrec;		/* blocking factor on tape */
extern int64_t	blocksperfile;  /* number of blocks per output file */
extern int	cartridge;	/* assume non-cartridge tape */
extern char 	*host;		/* remote host (if any) */
extern time_t	tstart_writing;	/* when started writing the first tape block */
extern long	xferrate;	/* averaged transfer rate of all volumes */
extern struct	fs *sblock;	/* the file system super block */
extern char	sblock_buf[MAXBSIZE];
extern int	tp_bshift;	/* log2(TP_BSIZE) */

/* operator interface functions */
void	broadcast(char *message);
time_t	do_stats(void);
void	lastdump(int arg);	/* int should be char */
void	msg(const char *fmt, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	msgtail(const char *fmt, ...)
	    __attribute__((__format__ (printf, 1, 2)));
int	query(char *question);
__dead void quit(const char *fmt, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	statussig(int);
void	timeest(void);

/* mapping routines */
union	dinode;
int64_t	blockest(union dinode *dp);
void	mapfileino(ino_t, int64_t *, int *);
int	mapfiles(ino_t maxino, int64_t *tapesize, char *disk,
	    char * const *dirv);
int	mapdirs(ino_t maxino, int64_t *tapesize);

/* file dumping routines */
void	ufs1_blksout(int32_t *blkp, int frags, ino_t ino);
void	ufs2_blksout(daddr_t *blkp, int frags, ino_t ino);
void	bread(daddr_t blkno, char *buf, int size);
void	dumpino(union dinode *dp, ino_t ino);
void	dumpmap(char *map, int type, ino_t ino);
void	writeheader(ino_t ino);

/* tape writing routines */
int	alloctape(void);
void	close_rewind(void);
void	dumpblock(daddr_t blkno, int size);
void	startnewtape(int top);
void	trewind(void);
void	writerec(char *dp, int isspcl);

__dead void Exit(int status);
__dead void dumpabort(int signo);
void	getfstab(void);

char	*rawname(char *cp);
char	*getduid(char *path);
union	dinode *getino(ino_t inum, int *mode);

/* rdump routines */
#ifdef RDUMP
void	rmtclose(void);
int	rmthost(char *host);
int	rmtopen(char *tape, int mode);
int	rmtwrite(char *buf, int count);
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

struct	fstab *fstabsearch(char *key);	/* search fs_file and fs_spec */

/*
 *	The contents of the file _PATH_DUMPDATES is maintained both on
 *	a linked list, and then (eventually) arrayified.
 */
struct dumpdates {
	char	dd_name[NAME_MAX+3];
	char	dd_level;
	time_t	dd_ddate;
};
struct dumptime {
	struct	dumpdates dt_value;
	struct	dumptime *dt_next;
};
extern struct	dumptime *dthead;	/* head of the list version */
extern int	ddates_in;		/* we have read the increment file */
extern int	nddates;		/* number of records (might be zero) */
extern struct	dumpdates **ddatev;	/* the arrayfied version */
void	initdumptimes(void);
void	getdumptime(void);
void	putdumptime(void);
#define	ITITERATE(i, ddp) \
	for (i = 0; i < nddates && (ddp = ddatev[i]); i++)

void	sig(int signo);

