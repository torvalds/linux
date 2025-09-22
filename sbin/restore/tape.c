/*	$OpenBSD: tape.c,v 1.54 2024/05/09 08:35:40 florian Exp $	*/
/*	$NetBSD: tape.c,v 1.26 1997/04/15 07:12:25 lukem Exp $	*/

/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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

#include <sys/param.h>	/* MAXBSIZE */
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/stat.h>

#include <ufs/ufs/dinode.h>
#include <protocols/dumprestore.h>

#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "restore.h"
#include "extern.h"

static long	fssize = MAXBSIZE;
static int	mt = -1;
static int	pipein = 0;
static char	magtape[BUFSIZ];
static int	blkcnt;
static int	numtrec;
static char	*tapebuf;
static union	u_spcl endoftapemark;
static long	blksread;		/* blocks read since last header */
static long	tpblksread = 0;		/* TP_BSIZE blocks read */
static long	tapesread;
static jmp_buf	restart;
static int	gettingfile = 0;	/* restart has a valid frame */
#ifdef RRESTORE
static char	*host = NULL;
#endif

static int	ofile;
static char	*map;
static char	lnkbuf[PATH_MAX + 1];
static size_t	pathlen;

struct context	curfile;	/* describes next file available on the tape */
union u_spcl 	u_spcl;		/* mapping of variables in a control block */
int		oldinofmt;	/* old inode format conversion required */
int		Bcvt;		/* Swap Bytes (for CCI or sun) */

#define	FLUSHTAPEBUF()	blkcnt = ntrec + 1

union u_ospcl {
	char dummy[TP_BSIZE];
	struct	s_ospcl {
		int32_t   c_type;
		int32_t   c_date;
		int32_t   c_ddate;
		int32_t   c_volume;
		int32_t   c_tapea;
		u_int16_t c_inumber;
		int32_t   c_magic;
		int32_t   c_checksum;
		struct odinode {
			unsigned short odi_mode;
			u_int16_t odi_nlink;
			u_int16_t odi_uid;
			u_int16_t odi_gid;
			int32_t   odi_size;
			int32_t   odi_rdev;
			char      odi_addr[36];
			int32_t   odi_atime;
			int32_t   odi_mtime;
			int32_t   odi_ctime;
		} c_odinode;
		int32_t c_count;
		char    c_addr[256];
	} s_ospcl;
};

static void	 accthdr(struct s_spcl *);
static int	 checksum(int *);
static void	 findinode(struct s_spcl *);
static void	 findtapeblksize(void);
static int	 gethead(struct s_spcl *);
static void	 readtape(char *);
static void	 setdumpnum(void);
static void	 swap_header(struct s_spcl *);
static void	 swap_old_header(struct s_ospcl *);
static void	 terminateinput(void);
static void	 xtrlnkfile(char *, size_t);
static void	 xtrlnkskip(char *, size_t);
static void	 xtrmap(char *, size_t);
static void	 xtrmapskip(char *, size_t);
static void	 xtrskip(char *, size_t);

/*
 * Set up an input source
 */
void
setinput(char *source)
{
	FLUSHTAPEBUF();
	if (bflag)
		newtapebuf(ntrec);
	else
		/* Max out buffer size, let findtapeblksize() set ntrec. */
		newtapebuf(MAXBSIZE / TP_BSIZE);
	terminal = stdin;

#ifdef RRESTORE
	if (strchr(source, ':')) {
		host = source;
		source = strchr(host, ':');
		*source++ = '\0';
		if (rmthost(host) == 0)
			exit(1);
	} else
#endif
	if (strcmp(source, "-") == 0) {
		/*
		 * Since input is coming from a pipe we must establish
		 * our own connection to the terminal.
		 */
		terminal = fopen(_PATH_TTY, "r");
		if (terminal == NULL) {
			warn("cannot open %s", _PATH_TTY);
			terminal = fopen(_PATH_DEVNULL, "r");
			if (terminal == NULL)
				err(1, "cannot open %s", _PATH_DEVNULL);
		}
		pipein++;
	}
	(void)strlcpy(magtape, source, sizeof magtape);
}

void
newtapebuf(long size)
{
	static long tapebufsize = -1;

	ntrec = size;
	if (size <= tapebufsize)
		return;
	free(tapebuf);
	tapebuf = calloc(size, TP_BSIZE);
	if (tapebuf == NULL)
		errx(1, "Cannot allocate space for tape buffer");
	tapebufsize = size;
}

/*
 * Verify that the tape drive can be accessed and
 * that it actually is a dump tape.
 */
void
setup(void)
{
	int i, j, *ip;
	struct stat stbuf;

	Vprintf(stdout, "Verify tape and initialize maps\n");
#ifdef RRESTORE
	if (host)
		mt = rmtopen(magtape, O_RDONLY);
	else
#endif
	if (pipein)
		mt = 0;
	else
		mt = open(magtape, O_RDONLY);
	if (mt == -1)
		err(1, "%s", magtape);
	volno = 1;
	setdumpnum();
	FLUSHTAPEBUF();
	if (!pipein && !bflag)
		findtapeblksize();
	if (gethead(&spcl) == FAIL) {
		blkcnt--; /* push back this block */
		blksread--;
		tpblksread--;
		cvtflag++;
		if (gethead(&spcl) == FAIL)
			errx(1, "Tape is not a dump tape");
		(void)fputs("Converting to new file system format.\n", stderr);
	}
	if (pipein) {
		endoftapemark.s_spcl.c_magic = cvtflag ? OFS_MAGIC :
		    FS_UFS2_MAGIC;
		endoftapemark.s_spcl.c_type = TS_END;
		ip = (int *)&endoftapemark;
		j = sizeof(union u_spcl) / sizeof(int);
		i = 0;
		do
			i += *ip++;
		while (--j);
		endoftapemark.s_spcl.c_checksum = CHECKSUM - i;
	}
	if (vflag || command == 't')
		printdumpinfo();
	dumptime = (time_t)spcl.c_ddate;
	dumpdate = (time_t)spcl.c_date;
	if (stat(".", &stbuf) == -1)
		err(1, "cannot stat .");
	if (stbuf.st_blksize > 0 && stbuf.st_blksize < TP_BSIZE )
		fssize = TP_BSIZE;
	if (stbuf.st_blksize >= TP_BSIZE && stbuf.st_blksize <= MAXBSIZE)
		fssize = stbuf.st_blksize;
	if (((fssize - 1) & fssize) != 0)
		errx(1, "bad block size %ld", fssize);
	if (spcl.c_volume != 1)
		errx(1, "Tape is not volume 1 of the dump");
	if (gethead(&spcl) == FAIL) {
		Dprintf(stdout, "header read failed at %ld blocks\n", blksread);
		panic("no header after volume mark!\n");
	}
	findinode(&spcl);
	if (spcl.c_type != TS_CLRI)
		errx(1, "Cannot find file removal list");
	maxino = (spcl.c_count * TP_BSIZE * NBBY) + 1;
	Dprintf(stdout, "maxino = %llu\n", (unsigned long long)maxino);
	map = calloc(1, howmany(maxino, NBBY));
	if (map == NULL)
		panic("no memory for active inode map\n");
	usedinomap = map;
	curfile.action = USING;
	getfile(xtrmap, xtrmapskip);
	if (spcl.c_type != TS_BITS)
		errx(1, "Cannot find file dump list");
	map = calloc(1, howmany(maxino, NBBY));
	if (map == NULL)
		panic("no memory for file dump list\n");
	dumpmap = map;
	curfile.action = USING;
	getfile(xtrmap, xtrmapskip);
}

/*
 * Prompt user to load a new dump volume.
 * "Nextvol" is the next suggested volume to use.
 * This suggested volume is enforced when doing full
 * or incremental restores, but can be overrridden by
 * the user when only extracting a subset of the files.
 */
void
getvol(long nextvol)
{
	long newvol = 0, savecnt = 0, wantnext = 0, i;
	union u_spcl tmpspcl;
#	define tmpbuf tmpspcl.s_spcl
	char buf[TP_BSIZE];
	const char *errstr;

	if (nextvol == 1) {
		tapesread = 0;
		gettingfile = 0;
	}
	if (pipein) {
		if (nextvol != 1)
			panic("Changing volumes on pipe input?\n");
		if (volno == 1)
			return;
		goto gethdr;
	}
	savecnt = blksread;
again:
	if (pipein)
		exit(1); /* pipes do not get a second chance */
	if (command == 'R' || command == 'r' || curfile.action != SKIP) {
		newvol = nextvol;
		wantnext = 1;
	} else {
		newvol = 0;
		wantnext = 0;
	}
	while (newvol <= 0) {
		if (tapesread == 0) {
			fprintf(stderr, "%s%s%s%s%s",
			    "You have not read any tapes yet.\n",
			    "Unless you know which volume your",
			    " file(s) are on you should start\n",
			    "with the last volume and work",
			    " towards the first.\n");
		} else {
			fprintf(stderr, "You have read volumes");
			strlcpy(buf, ": ", sizeof buf);
			for (i = 1; i < 32; i++)
				if (tapesread & (1 << i)) {
					fprintf(stderr, "%s%ld", buf, i);
					strlcpy(buf, ", ", sizeof buf);
				}
			fprintf(stderr, "\n");
		}
		do	{
			fprintf(stderr, "Specify next volume #: ");
			(void)fflush(stderr);
			if (fgets(buf, sizeof buf, terminal) == NULL)
				exit(1);
			buf[strcspn(buf, "\n")] = '\0';

			newvol = strtonum(buf, 1, INT_MAX, &errstr);
			if (errstr)
				fprintf(stderr, "Volume number %s: %s\n", errstr, buf);
		} while (errstr);
	}
	if (newvol == volno) {
		tapesread |= 1 << volno;
		return;
	}
	closemt();
	fprintf(stderr, "Mount tape volume %ld\n", newvol);
	fprintf(stderr, "Enter ``none'' if there are no more tapes\n");
	fprintf(stderr, "otherwise enter tape name (default: %s) ", magtape);
	(void)fflush(stderr);
	if (fgets(buf, sizeof buf, terminal) == NULL || feof(terminal))
		exit(1);
	buf[strcspn(buf, "\n")] = '\0';
	if (strcmp(buf, "none") == 0) {
		terminateinput();
		return;
	}
	if (buf[0] != '\0')
		(void)strlcpy(magtape, buf, sizeof magtape);

#ifdef RRESTORE
	if (host)
		mt = rmtopen(magtape, O_RDONLY);
	else
#endif
		mt = open(magtape, O_RDONLY);

	if (mt == -1) {
		fprintf(stderr, "Cannot open %s\n", magtape);
		volno = -1;
		goto again;
	}
gethdr:
	volno = newvol;
	setdumpnum();
	FLUSHTAPEBUF();
	if (gethead(&tmpbuf) == FAIL) {
		Dprintf(stdout, "header read failed at %ld blocks\n", blksread);
		fprintf(stderr, "tape is not dump tape\n");
		volno = 0;
		goto again;
	}
	if (tmpbuf.c_volume != volno) {
		fprintf(stderr, "Wrong volume (%d)\n", tmpbuf.c_volume);
		volno = 0;
		goto again;
	}
	if (tmpbuf.c_date != dumpdate || tmpbuf.c_ddate != dumptime) {
		time_t t = (time_t)tmpbuf.c_date;
		char ct1buf[26], ct2buf[26];
		char *ct1, *ct2;

		ct1 = ctime_r(&t, ct1buf);
		ct2 = ctime_r(&dumpdate, ct2buf);
		if (ct1 && ct2) {
			fprintf(stderr, "Wrong dump date\n\tgot: %s", ct1);
			fprintf(stderr, "\twanted: %s", ct2);
		} else {
			fprintf(stderr, "Wrong dump date\n\tgot: %lld\n", t);
			fprintf(stderr, "\twanted: %lld\n", dumpdate);
		}
		volno = 0;
		goto again;
	}
	tapesread |= 1 << volno;
	blksread = savecnt;
	/*
	 * If continuing from the previous volume, skip over any
	 * blocks read already at the end of the previous volume.
	 *
	 * If coming to this volume at random, skip to the beginning
	 * of the next record.
	 */
	Dprintf(stdout, "read %ld recs, tape starts with %lld\n",
		tpblksread, tmpbuf.c_firstrec);
	if (tmpbuf.c_type == TS_TAPE && (tmpbuf.c_flags & DR_NEWHEADER)) {
		if (!wantnext) {
			tpblksread = tmpbuf.c_firstrec;
			for (i = tmpbuf.c_count; i > 0; i--)
				readtape(buf);
		} else if (tmpbuf.c_firstrec > 0 &&
			   tmpbuf.c_firstrec < tpblksread - 1) {
			/*
			 * -1 since we've read the volume header
			 */
			i = tpblksread - tmpbuf.c_firstrec - 1;
			Dprintf(stderr, "Skipping %ld duplicate record%s.\n",
				i, (i == 1) ? "" : "s");
			while (--i >= 0)
				readtape(buf);
		}
	}
	if (curfile.action == USING) {
		if (volno == 1)
			panic("active file into volume 1\n");
		return;
	}
	/*
	 * Skip up to the beginning of the next record
	 */
	if (tmpbuf.c_type == TS_TAPE && (tmpbuf.c_flags & DR_NEWHEADER))
		for (i = tmpbuf.c_count; i > 0; i--)
			readtape(buf);
	(void)gethead(&spcl);
	findinode(&spcl);
	if (gettingfile) {
		gettingfile = 0;
		longjmp(restart, 1);
	}
}

/*
 * Handle unexpected EOF.
 */
static void
terminateinput(void)
{

	if (gettingfile && curfile.action == USING) {
		printf("Warning: %s %s\n",
		    "End-of-input encountered while extracting", curfile.name);
	}
	curfile.name = "<name unknown>";
	curfile.action = UNKNOWN;
	curfile.mode = 0;
	curfile.ino = maxino;
	if (gettingfile) {
		gettingfile = 0;
		longjmp(restart, 1);
	}
}

/*
 * handle multiple dumps per tape by skipping forward to the
 * appropriate one.
 */
static void
setdumpnum(void)
{
	struct mtop tcom;

	if (dumpnum == 1 || volno != 1)
		return;
	if (pipein)
		errx(1, "Cannot have multiple dumps on pipe input");
	tcom.mt_op = MTFSF;
	tcom.mt_count = dumpnum - 1;
#ifdef RRESTORE
	if (host)
		rmtioctl(MTFSF, dumpnum - 1);
	else
#endif
		if (ioctl(mt, MTIOCTOP, (char *)&tcom) == -1)
			warn("ioctl MTFSF");
}

void
printdumpinfo(void)
{
	time_t t;
	char *ct;

	t = (time_t)spcl.c_date;
	ct = ctime(&t);
	if (ct)
		fprintf(stdout, "Dump   date: %s", ct);
	else
		fprintf(stdout, "Dump   date: %lld\n", t);
	t = (time_t)spcl.c_ddate;
	ct = ctime(&t);
	if (ct)
		fprintf(stdout, "Dumped from: %s",
		    (spcl.c_ddate == 0) ? "the epoch\n" : ct);
	else
		fprintf(stdout, "Dumped from: %lld\n", t);
	if (spcl.c_host[0] == '\0')
		return;
	fprintf(stderr, "Level %d dump of %s on %s:%s\n",
		spcl.c_level, spcl.c_filesys, spcl.c_host, spcl.c_dev);
	fprintf(stderr, "Label: %s\n", spcl.c_label);
}

int
extractfile(char *name)
{
	u_int flags;
	uid_t uid;
	gid_t gid;
	mode_t mode;
	struct timespec mtimep[2], ctimep[2];
	struct entry *ep;
	int setbirth;

	curfile.name = name;
	curfile.action = USING;
	mtimep[0].tv_sec = curfile.atime_sec;
	mtimep[0].tv_nsec = curfile.atime_nsec;
	mtimep[1].tv_sec = curfile.mtime_sec;
	mtimep[1].tv_nsec = curfile.mtime_nsec;

	setbirth = curfile.birthtime_sec != 0;
	if (setbirth) {
		ctimep[0].tv_sec = curfile.atime_sec;
		ctimep[0].tv_nsec = curfile.atime_nsec;
		ctimep[1].tv_sec = curfile.birthtime_sec;
		ctimep[1].tv_nsec = curfile.birthtime_nsec;
	}
	uid = curfile.uid;
	gid = curfile.gid;
	mode = curfile.mode;
	flags = curfile.file_flags;
	switch (mode & IFMT) {

	default:
		fprintf(stderr, "%s: unknown file mode 0%o\n", name, mode);
		skipfile();
		return (FAIL);

	case IFSOCK:
		Vprintf(stdout, "skipped socket %s\n", name);
		skipfile();
		return (GOOD);

	case IFDIR:
		if (mflag) {
			ep = lookupname(name);
			if (ep == NULL || ep->e_flags & EXTRACT)
				panic("unextracted directory %s\n", name);
			skipfile();
			return (GOOD);
		}
		Vprintf(stdout, "extract file %s\n", name);
		return (genliteraldir(name, curfile.ino));

	case IFLNK: {
			lnkbuf[0] = '\0';
			pathlen = 0;
			getfile(xtrlnkfile, xtrlnkskip);
			if (pathlen == 0) {
				Vprintf(stdout,
				    "%s: zero length symbolic link (ignored)\n",
				     name);
				return (GOOD);
			}
			if (linkit(lnkbuf, name, SYMLINK) == FAIL)
				return (FAIL);
			(void)lchown(name, uid, gid);
			(void)fchmodat(AT_FDCWD, name, mode,
			    AT_SYMLINK_NOFOLLOW);
			return (GOOD);
		}

	case IFCHR:
	case IFBLK:
		Vprintf(stdout, "extract special file %s\n", name);
		if (Nflag) {
			skipfile();
			return (GOOD);
		}
		if (mknod(name, mode, (int)curfile.rdev) == -1) {
			warn("%s: cannot create special file", name);
			skipfile();
			return (FAIL);
		}
		(void)chown(name, uid, gid);
		(void)chmod(name, mode);
		(void)chflags(name, flags);
		skipfile();
		if (setbirth)
			(void)utimensat(AT_FDCWD, name, ctimep, 0);
		(void)utimensat(AT_FDCWD, name, mtimep, 0);
		return (GOOD);

	case IFIFO:
		Vprintf(stdout, "extract fifo %s\n", name);
		if (Nflag) {
			skipfile();
			return (GOOD);
		}
		if (mkfifo(name, mode) == -1) {
			warn("%s: cannot create fifo", name);
			skipfile();
			return (FAIL);
		}
		(void)chown(name, uid, gid);
		(void)chmod(name, mode);
		(void)chflags(name, flags);
		skipfile();
		if (setbirth)
			(void)utimensat(AT_FDCWD, name, ctimep, 0);
		(void)utimensat(AT_FDCWD, name, mtimep, 0);
		return (GOOD);

	case IFREG:
		Vprintf(stdout, "extract file %s\n", name);
		if (Nflag) {
			skipfile();
			return (GOOD);
		}
		if ((ofile = open(name, O_WRONLY | O_CREAT | O_TRUNC,
		    0666)) == -1) {
			warn("%s: cannot create file", name);
			skipfile();
			return (FAIL);
		}
		(void)fchown(ofile, curfile.uid, curfile.gid);
		(void)fchmod(ofile, mode);
		(void)fchflags(ofile, flags);
		getfile(xtrfile, xtrskip);
		(void)close(ofile);
		if (setbirth)
			(void)utimensat(AT_FDCWD, name, ctimep, 0);
		(void)utimensat(AT_FDCWD, name, mtimep, 0);
		return (GOOD);
	}
	/* NOTREACHED */
}

/*
 * skip over bit maps on the tape
 */
void
skipmaps(void)
{

	while (spcl.c_type == TS_BITS || spcl.c_type == TS_CLRI)
		skipfile();
}

/*
 * skip over a file on the tape
 */
void
skipfile(void)
{

	curfile.action = SKIP;
	getfile(xtrnull, xtrnull);
}

/*
 * Extract a file from the tape.
 * When an allocated block is found it is passed to the fill function;
 * when an unallocated block (hole) is found, a zeroed buffer is passed
 * to the skip function.
 *
 * For some block types (TS_BITS, TS_CLRI), the c_addr map is not meaningful
 * and no blocks should be skipped.
 */
void
getfile(void (*fill)(char *, size_t), void (*skip)(char *, size_t))
{
	int i;
	volatile int curblk = 0;
	volatile off_t size = spcl.c_size;
	static char clearedbuf[MAXBSIZE];
	char buf[MAXBSIZE / TP_BSIZE][TP_BSIZE];
	char junk[TP_BSIZE];
	volatile int noskip = (spcl.c_type == TS_BITS || spcl.c_type == TS_CLRI);

	if (spcl.c_type == TS_END)
		panic("ran off end of tape\n");
	if (spcl.c_magic != FS_UFS2_MAGIC)
		panic("not at beginning of a file\n");
	if (!gettingfile && setjmp(restart) != 0)
		return;
	gettingfile++;
loop:
	for (i = 0; i < spcl.c_count; i++) {
		if (noskip || spcl.c_addr[i]) {
			readtape(&buf[curblk++][0]);
			if (curblk == fssize / TP_BSIZE) {
				(*fill)((char *)buf, size > TP_BSIZE ?
				     fssize :
				     ((off_t)curblk - 1) * TP_BSIZE + size);
				curblk = 0;
			}
		} else {
			if (curblk > 0) {
				(*fill)((char *)buf, size > TP_BSIZE ?
				     (curblk * TP_BSIZE) :
				     ((off_t)curblk - 1) * TP_BSIZE + size);
				curblk = 0;
			}
			(*skip)(clearedbuf, size > TP_BSIZE ?
				TP_BSIZE : size);
		}
		if ((size -= TP_BSIZE) <= 0) {
			for (i++; i < spcl.c_count; i++)
				if (noskip || spcl.c_addr[i])
					readtape(junk);
			break;
		}
	}
	if (gethead(&spcl) == GOOD && size > 0) {
		if (spcl.c_type == TS_ADDR)
			goto loop;
		Dprintf(stdout,
			"Missing address (header) block for %s at %ld blocks\n",
			curfile.name, blksread);
	}
	if (curblk > 0)
		(*fill)((char *)buf, ((off_t)curblk * TP_BSIZE) + size);
	findinode(&spcl);
	gettingfile = 0;
}

/*
 * Write out the next block of a file.
 */
void
xtrfile(char *buf, size_t size)
{

	if (Nflag)
		return;
	if (write(ofile, buf, size) == -1)
		err(1, "write error extracting inode %llu, name %s\nwrite",
		    (unsigned long long)curfile.ino, curfile.name);
}

/*
 * Skip over a hole in a file.
 */
static void
xtrskip(char *buf, size_t size)
{

	if (lseek(ofile, (off_t)size, SEEK_CUR) == -1)
		err(1, "seek error extracting inode %llu, name %s\nlseek",
		    (unsigned long long)curfile.ino, curfile.name);
}

/*
 * Collect the next block of a symbolic link.
 */
static void
xtrlnkfile(char *buf, size_t size)
{

	pathlen += size;
	if (pathlen > PATH_MAX)
		errx(1, "symbolic link name: %s->%s%s; too long %lu",
		    curfile.name, lnkbuf, buf, (u_long)pathlen);
	(void)strlcat(lnkbuf, buf, sizeof(lnkbuf));
}

/*
 * Skip over a hole in a symbolic link (should never happen).
 */
static void
xtrlnkskip(char *buf, size_t size)
{

	errx(1, "unallocated block in symbolic link %s", curfile.name);
}

/*
 * Collect the next block of a bit map.
 */
static void
xtrmap(char *buf, size_t size)
{

	memcpy(map, buf, size);
	map += size;
}

/*
 * Skip over a hole in a bit map (should never happen).
 */
static void
xtrmapskip(char *buf, size_t size)
{

	panic("hole in map\n");
	map += size;
}

/*
 * Noop, when an extraction function is not needed.
 */
void
xtrnull(char *buf, size_t size)
{

	return;
}

/*
 * Read TP_BSIZE blocks from the input.
 * Handle read errors, and end of media.
 */
static void
readtape(char *buf)
{
	long rd, newvol, i;
	int cnt, seek_failed;

	if (blkcnt < numtrec) {
		memcpy(buf, &tapebuf[(blkcnt++ * TP_BSIZE)], TP_BSIZE);
		blksread++;
		tpblksread++;
		return;
	}
	for (i = 0; i < ntrec; i++)
		((struct s_spcl *)&tapebuf[i * TP_BSIZE])->c_magic = 0;
	if (numtrec == 0)
		numtrec = ntrec;
	cnt = ntrec * TP_BSIZE;
	rd = 0;
getmore:
#ifdef RRESTORE
	if (host)
		i = rmtread(&tapebuf[rd], cnt);
	else
#endif
		i = read(mt, &tapebuf[rd], cnt);
	/*
	 * Check for mid-tape short read error.
	 * If found, skip rest of buffer and start with the next.
	 */
	if (!pipein && numtrec < ntrec && i > 0) {
		Dprintf(stdout, "mid-media short read error.\n");
		numtrec = ntrec;
	}
	/*
	 * Handle partial block read.
	 */
	if (pipein && i == 0 && rd > 0)
		i = rd;
	else if (i > 0 && i != ntrec * TP_BSIZE) {
		if (pipein) {
			rd += i;
			cnt -= i;
			if (cnt > 0)
				goto getmore;
			i = rd;
		} else {
			/*
			 * Short read. Process the blocks read.
			 */
			if (i % TP_BSIZE != 0)
				Vprintf(stdout,
				    "partial block read: %ld should be %ld\n",
				    i, ntrec * TP_BSIZE);
			numtrec = i / TP_BSIZE;
		}
	}
	/*
	 * Handle read error.
	 */
	if (i < 0) {
		fprintf(stderr, "Tape read error while ");
		switch (curfile.action) {
		default:
			fprintf(stderr, "trying to set up tape\n");
			break;
		case UNKNOWN:
			fprintf(stderr, "trying to resynchronize\n");
			break;
		case USING:
			fprintf(stderr, "restoring %s\n", curfile.name);
			break;
		case SKIP:
			fprintf(stderr, "skipping over inode %llu\n",
			    (unsigned long long)curfile.ino);
			break;
		}
		if (!yflag && !reply("continue"))
			exit(1);
		i = ntrec * TP_BSIZE;
		memset(tapebuf, 0, i);
#ifdef RRESTORE
		if (host)
			seek_failed = (rmtseek(i, 1) < 0);
		else
#endif
			seek_failed = (lseek(mt, i, SEEK_CUR) == (off_t)-1);

		if (seek_failed)
			err(1, "continuation failed");
	}
	/*
	 * Handle end of tape.
	 */
	if (i == 0) {
		Vprintf(stdout, "End-of-tape encountered\n");
		if (!pipein) {
			newvol = volno + 1;
			volno = 0;
			numtrec = 0;
			getvol(newvol);
			readtape(buf);
			return;
		}
		if (rd % TP_BSIZE != 0)
			panic("partial block read: %ld should be %ld\n",
				rd, ntrec * TP_BSIZE);
		terminateinput();
		memcpy(&tapebuf[rd], &endoftapemark, TP_BSIZE);
	}
	blkcnt = 0;
	memcpy(buf, &tapebuf[(blkcnt++ * TP_BSIZE)], TP_BSIZE);
	blksread++;
	tpblksread++;
}

static void
findtapeblksize(void)
{
	long i;

	for (i = 0; i < ntrec; i++)
		((struct s_spcl *)&tapebuf[i * TP_BSIZE])->c_magic = 0;
	blkcnt = 0;
#ifdef RRESTORE
	if (host)
		i = rmtread(tapebuf, ntrec * TP_BSIZE);
	else
#endif
		i = read(mt, tapebuf, ntrec * TP_BSIZE);

	if (i <= 0)
		err(1, "tape read error");
	if (i % TP_BSIZE != 0) {
		errx(1,
		    "Tape block size (%ld) is not a multiple of dump block size (%d)",
		    i, TP_BSIZE);
	}
	ntrec = i / TP_BSIZE;
	numtrec = ntrec;
	Vprintf(stdout, "Tape block size is %ld\n", ntrec);
}

void
closemt(void)
{

	if (mt < 0)
		return;
#ifdef RRESTORE
	if (host)
		rmtclose();
	else
#endif
		(void)close(mt);
}

/*
 * Read the next block from the tape.
 * Check to see if it is one of several vintage headers.
 * If it is an old style header, convert it to a new style header.
 * If it is not any valid header, return an error.
 */
static int
gethead(struct s_spcl *buf)
{
	union u_ospcl u_ospcl;

	if (!cvtflag) {
		readtape((char *)buf);
		if (buf->c_magic != NFS_MAGIC &&
		    buf->c_magic != FS_UFS2_MAGIC) {
			if (swap32(buf->c_magic) != NFS_MAGIC &&
			    swap32(buf->c_magic) != FS_UFS2_MAGIC)
				return (FAIL);
			if (!Bcvt) {
				Vprintf(stdout, "Note: Doing Byte swapping\n");
				Bcvt = 1;
			}
		}
		if (checksum((int *)buf) == FAIL)
			return (FAIL);
		if (Bcvt)
			swap_header(buf);
		goto good;
	}

	readtape((char *)(&u_ospcl.s_ospcl));
	if (checksum((int *)(&u_ospcl.s_ospcl)) == FAIL)
		return (FAIL);
	if (u_ospcl.s_ospcl.c_magic != OFS_MAGIC) {
		if (swap32(u_ospcl.s_ospcl.c_magic) != OFS_MAGIC)
			return (FAIL);
		if (!Bcvt) {
			fprintf(stdout, "Note: Doing Byte swapping\n");
			Bcvt = 1;
		}
		swap_old_header(&u_ospcl.s_ospcl);
	}

	memset(buf, 0, TP_BSIZE);
	buf->c_type = u_ospcl.s_ospcl.c_type;
	buf->c_date = u_ospcl.s_ospcl.c_date;
	buf->c_ddate = u_ospcl.s_ospcl.c_ddate;
	buf->c_volume = u_ospcl.s_ospcl.c_volume;
	buf->c_tapea = u_ospcl.s_ospcl.c_tapea;
	buf->c_inumber = u_ospcl.s_ospcl.c_inumber;
	buf->c_checksum = u_ospcl.s_ospcl.c_checksum;
	buf->c_mode = u_ospcl.s_ospcl.c_odinode.odi_mode;
	buf->c_uid = u_ospcl.s_ospcl.c_odinode.odi_uid;
	buf->c_gid = u_ospcl.s_ospcl.c_odinode.odi_gid;
	buf->c_size = u_ospcl.s_ospcl.c_odinode.odi_size;
	buf->c_rdev = u_ospcl.s_ospcl.c_odinode.odi_rdev;
	buf->c_atime = u_ospcl.s_ospcl.c_odinode.odi_atime;
	buf->c_mtime = u_ospcl.s_ospcl.c_odinode.odi_mtime;
	buf->c_count = u_ospcl.s_ospcl.c_count;
	memcpy(buf->c_addr, u_ospcl.s_ospcl.c_addr, 256);
	buf->c_magic = FS_UFS2_MAGIC;
good:
	switch (buf->c_type) {

	case TS_CLRI:
	case TS_BITS:
		/*
		 * Have to patch up missing information in bit map headers
		 */
		buf->c_inumber = 0;
		buf->c_size = buf->c_count * TP_BSIZE;
		break;

	case TS_TAPE:
		if ((buf->c_flags & DR_NEWINODEFMT) == 0)
			oldinofmt = 1;
		/* fall through */
	case TS_END:
		buf->c_inumber = 0;
		break;

	case TS_INODE:
		if (buf->c_magic == NFS_MAGIC) {
			buf->c_tapea = buf->c_old_tapea;
			buf->c_firstrec = buf->c_old_firstrec;
			buf->c_date = buf->c_old_date;
			buf->c_ddate = buf->c_old_ddate;
			buf->c_atime = buf->c_old_atime;
			buf->c_mtime = buf->c_old_mtime;
			buf->c_birthtime = 0;
			buf->c_birthtimensec = 0;
			buf->c_atimensec = buf->c_mtimensec = 0;
		}

	case TS_ADDR:
		break;

	default:
		panic("gethead: unknown inode type %d\n", buf->c_type);
		break;
	}

	buf->c_magic = FS_UFS2_MAGIC;

	/*
	 * If we are restoring a filesystem with old format inodes,
	 * copy the uid/gid to the new location.
	 */
	if (oldinofmt) {
		buf->c_uid = buf->c_spare1[1];
		buf->c_gid = buf->c_spare1[2];
	}
	if (dflag)
		accthdr(buf);
	return(GOOD);
}

/*
 * Check that a header is where it belongs and predict the next header
 */
static void
accthdr(struct s_spcl *header)
{
	static ino_t previno = (ino_t)-1;
	static int prevtype;
	static long predict;
	long blks, i;

	if (header->c_type == TS_TAPE) {
		fprintf(stderr, "Volume header (%s inode format) ",
		    oldinofmt ? "old" : "new");
		if (header->c_firstrec)
			fprintf(stderr, "begins with record %lld",
				(long long)header->c_firstrec);
		fprintf(stderr, "\n");
		previno = (ino_t)-1;
		return;
	}
	if (previno == (ino_t)-1)
		goto newcalc;
	switch (prevtype) {
	case TS_BITS:
		fprintf(stderr, "Dumped inodes map header");
		break;
	case TS_CLRI:
		fprintf(stderr, "Used inodes map header");
		break;
	case TS_INODE:
		fprintf(stderr, "File header, ino %llu",
		    (unsigned long long)previno);
		break;
	case TS_ADDR:
		fprintf(stderr, "File continuation header, ino %llu",
		    (unsigned long long)previno);
		break;
	case TS_END:
		fprintf(stderr, "End of tape header");
		break;
	}
	if (predict != blksread - 1)
		fprintf(stderr, "; predicted %ld blocks, got %ld blocks",
			predict, blksread - 1);
	fprintf(stderr, "\n");
newcalc:
	blks = 0;
	switch (header->c_type) {

	case TS_BITS:
	case TS_CLRI:
		blks = header->c_count;
		break;

	case TS_END:
		break;

	default:
		for (i = 0; i < header->c_count; i++)
			if (header->c_addr[i] != 0)
				blks++;
	}
	predict = blks;
	blksread = 0;
	prevtype = header->c_type;
	previno = header->c_inumber;
}

/*
 * Find an inode header.
 * Complain if had to skip, and complain is set.
 */
static void
findinode(struct s_spcl *header)
{
	static long skipcnt = 0;
	long i;
	char buf[TP_BSIZE];

	curfile.name = "<name unknown>";
	curfile.action = UNKNOWN;
	curfile.mode = 0;
	curfile.ino = 0;
	do {
		if (header->c_magic != FS_UFS2_MAGIC) {
			skipcnt++;
			while (gethead(header) == FAIL ||
			    header->c_date != dumpdate)
				skipcnt++;
		}
		switch (header->c_type) {

		case TS_ADDR:
			/*
			 * Skip up to the beginning of the next record
			 */
			for (i = 0; i < header->c_count; i++)
				if (header->c_addr[i])
					readtape(buf);
			while (gethead(header) == FAIL ||
			    header->c_date != dumpdate)
				skipcnt++;
			break;

		case TS_INODE:
			curfile.mode = header->c_mode;
			curfile.uid = header->c_uid;
			curfile.gid = header->c_gid;
			curfile.file_flags = header->c_file_flags;
			curfile.rdev = header->c_rdev;
			curfile.atime_sec = header->c_atime;
			curfile.atime_nsec = header->c_atimensec;
			curfile.mtime_sec = header->c_mtime;
			curfile.mtime_nsec = header->c_mtimensec;
			curfile.birthtime_sec = header->c_birthtime;
			curfile.birthtime_nsec = header->c_birthtimensec;
			curfile.size = header->c_size;
			curfile.ino = header->c_inumber;
			break;

		case TS_END:
			curfile.ino = maxino;
			break;

		case TS_CLRI:
			curfile.name = "<file removal list>";
			break;

		case TS_BITS:
			curfile.name = "<file dump list>";
			break;

		case TS_TAPE:
			panic("unexpected tape header\n");
			/* NOTREACHED */

		default:
			panic("unknown tape header type %d\n", spcl.c_type);
			/* NOTREACHED */

		}
	} while (header->c_type == TS_ADDR);
	if (skipcnt > 0)
		fprintf(stderr, "resync restore, skipped %ld blocks\n", skipcnt);
	skipcnt = 0;
}

static int
checksum(int *buf)
{
	int i, j;

	j = sizeof(union u_spcl) / sizeof(int);
	i = 0;
	if (!Bcvt) {
		do
			i += *buf++;
		while (--j);
	} else {
		do
			i += swap32(*buf++);
		while (--j);
	}

	if (i != CHECKSUM) {
		fprintf(stderr, "Checksum error %o, inode %llu file %s\n", i,
		    (unsigned long long)curfile.ino, curfile.name);
		return(FAIL);
	}
	return(GOOD);
}

#ifdef RRESTORE
#include <stdarg.h>

void
msg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
}
#endif /* RRESTORE */

static void
swap_header(struct s_spcl *s)
{
	s->c_type = swap32(s->c_type);
	s->c_old_date = swap32(s->c_old_date);
	s->c_old_ddate = swap32(s->c_old_ddate);
	s->c_volume = swap32(s->c_volume);
	s->c_old_tapea = swap32(s->c_old_tapea);
	s->c_inumber = swap32(s->c_inumber);
	s->c_magic = swap32(s->c_magic);
	s->c_checksum = swap32(s->c_checksum);

	s->c_mode = swap16(s->c_mode);
	s->c_size = swap64(s->c_size);
	s->c_old_atime = swap32(s->c_old_atime);
	s->c_atimensec = swap32(s->c_atimensec);
	s->c_old_mtime = swap32(s->c_old_mtime);
	s->c_mtimensec = swap32(s->c_mtimensec);
	s->c_rdev = swap32(s->c_rdev);
	s->c_birthtimensec = swap32(s->c_birthtimensec);
	s->c_birthtime = swap64(s->c_birthtime);
	s->c_atime = swap64(s->c_atime);
	s->c_mtime = swap64(s->c_mtime);
	s->c_file_flags = swap32(s->c_file_flags);
	s->c_uid = swap32(s->c_uid);
	s->c_gid = swap32(s->c_gid);

	s->c_count = swap32(s->c_count);
	s->c_level = swap32(s->c_level);
	s->c_flags = swap32(s->c_flags);
	s->c_old_firstrec = swap32(s->c_old_firstrec);

	s->c_date = swap64(s->c_date);
	s->c_ddate = swap64(s->c_ddate);
	s->c_tapea = swap64(s->c_tapea);
	s->c_firstrec = swap64(s->c_firstrec);

	/*
	 * These are ouid and ogid.
	 */
	s->c_spare1[1] = swap16(s->c_spare1[1]);
	s->c_spare1[2] = swap16(s->c_spare1[2]);
}

static void
swap_old_header(struct s_ospcl *os)
{
	os->c_type = swap32(os->c_type);
	os->c_date = swap32(os->c_date);
	os->c_ddate = swap32(os->c_ddate);
	os->c_volume = swap32(os->c_volume);
	os->c_tapea = swap32(os->c_tapea);
	os->c_inumber = swap16(os->c_inumber);
	os->c_magic = swap32(os->c_magic);
	os->c_checksum = swap32(os->c_checksum);

	os->c_odinode.odi_mode = swap16(os->c_odinode.odi_mode);
	os->c_odinode.odi_nlink = swap16(os->c_odinode.odi_nlink);
	os->c_odinode.odi_uid = swap16(os->c_odinode.odi_uid);
	os->c_odinode.odi_gid = swap16(os->c_odinode.odi_gid);

	os->c_odinode.odi_size = swap32(os->c_odinode.odi_size);
	os->c_odinode.odi_rdev = swap32(os->c_odinode.odi_rdev);
	os->c_odinode.odi_atime = swap32(os->c_odinode.odi_atime);
	os->c_odinode.odi_mtime = swap32(os->c_odinode.odi_mtime);
	os->c_odinode.odi_ctime = swap32(os->c_odinode.odi_ctime);

	os->c_count = swap32(os->c_count);
}
