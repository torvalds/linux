/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)tape.c	8.9 (Berkeley) 5/1/95";
#endif
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/file.h>
#include <sys/mtio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/extattr.h>
#include <sys/acl.h>

#include <ufs/ufs/extattr.h>
#include <ufs/ufs/dinode.h>
#include <protocols/dumprestore.h>

#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <timeconv.h>
#include <unistd.h>

#include "restore.h"
#include "extern.h"

static long	fssize = MAXBSIZE;
static int	mt = -1;
static int	pipein = 0;
static int	pipecmdin = 0;
static FILE	*popenfp = NULL;
static char	*magtape;
static int	blkcnt;
static int	numtrec;
static char	*tapebuf;
static union	u_spcl endoftapemark;
static long	byteslide = 0;
static long	blksread;		/* blocks read since last header */
static int64_t	tapeaddr = 0;		/* current TP_BSIZE tape record */
static long	tapesread;
static jmp_buf	restart;
static int	gettingfile = 0;	/* restart has a valid frame */
static char	*host = NULL;
static int	readmapflag;

static int	ofile;
static char	*map;
static char	lnkbuf[MAXPATHLEN + 1];
static int	pathlen;

int		Bcvt;		/* Swap Bytes */
int		oldinofmt;	/* FreeBSD 1 inode format needs cvt */

#define	FLUSHTAPEBUF()	blkcnt = ntrec + 1

char *namespace_names[] = EXTATTR_NAMESPACE_NAMES;

static void	 accthdr(struct s_spcl *);
static int	 checksum(int *);
static void	 findinode(struct s_spcl *);
static void	 findtapeblksize(void);
static char	*setupextattr(int);
static void	 xtrattr(char *, size_t);
static void	 skiphole(void (*)(char *, size_t), size_t *);
static int	 gethead(struct s_spcl *);
static void	 readtape(char *);
static void	 setdumpnum(void);
static u_long	 swabl(u_long);
static u_char	*swablong(u_char *, int);
static u_char	*swabshort(u_char *, int);
static void	 terminateinput(void);
static void	 xtrfile(char *, size_t);
static void	 xtrlnkfile(char *, size_t);
static void	 xtrlnkskip(char *, size_t);
static void	 xtrmap(char *, size_t);
static void	 xtrmapskip(char *, size_t);
static void	 xtrskip(char *, size_t);

/*
 * Set up an input source
 */
void
setinput(char *source, int ispipecommand)
{
	FLUSHTAPEBUF();
	if (bflag)
		newtapebuf(ntrec);
	else
		newtapebuf(MAX(NTREC, HIGHDENSITYTREC));
	terminal = stdin;

	if (ispipecommand)
		pipecmdin++;
	else
#ifdef RRESTORE
	if (strchr(source, ':')) {
		host = source;
		source = strchr(host, ':');
		*source++ = '\0';
		if (rmthost(host) == 0)
			done(1);
	} else
#endif
	if (strcmp(source, "-") == 0) {
		/*
		 * Since input is coming from a pipe we must establish
		 * our own connection to the terminal.
		 */
		terminal = fopen(_PATH_TTY, "r");
		if (terminal == NULL) {
			(void)fprintf(stderr, "cannot open %s: %s\n",
			    _PATH_TTY, strerror(errno));
			terminal = fopen(_PATH_DEVNULL, "r");
			if (terminal == NULL) {
				(void)fprintf(stderr, "cannot open %s: %s\n",
				    _PATH_DEVNULL, strerror(errno));
				done(1);
			}
		}
		pipein++;
	}
	/* no longer need or want root privileges */
	if (setuid(getuid()) != 0) {
		fprintf(stderr, "setuid failed\n");
		done(1);
	}
	magtape = strdup(source);
	if (magtape == NULL) {
		fprintf(stderr, "Cannot allocate space for magtape buffer\n");
		done(1);
	}
}

void
newtapebuf(long size)
{
	static int tapebufsize = -1;

	ntrec = size;
	if (size <= tapebufsize)
		return;
	if (tapebuf != NULL)
		free(tapebuf - TP_BSIZE);
	tapebuf = malloc((size+1) * TP_BSIZE);
	if (tapebuf == NULL) {
		fprintf(stderr, "Cannot allocate space for tape buffer\n");
		done(1);
	}
	tapebuf += TP_BSIZE;
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

	vprintf(stdout, "Verify tape and initialize maps\n");
	if (pipecmdin) {
		if (setenv("RESTORE_VOLUME", "1", 1) == -1) {
			fprintf(stderr, "Cannot set $RESTORE_VOLUME: %s\n",
			    strerror(errno));
			done(1);
		}
		popenfp = popen(magtape, "r");
		mt = popenfp ? fileno(popenfp) : -1;
	} else
#ifdef RRESTORE
	if (host)
		mt = rmtopen(magtape, 0);
	else
#endif
	if (pipein)
		mt = 0;
	else
		mt = open(magtape, O_RDONLY, 0);
	if (mt < 0) {
		fprintf(stderr, "%s: %s\n", magtape, strerror(errno));
		done(1);
	}
	volno = 1;
	setdumpnum();
	FLUSHTAPEBUF();
	if (!pipein && !pipecmdin && !bflag)
		findtapeblksize();
	if (gethead(&spcl) == FAIL) {
		fprintf(stderr, "Tape is not a dump tape\n");
		done(1);
	}
	if (pipein) {
		endoftapemark.s_spcl.c_magic = FS_UFS2_MAGIC;
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
	dumptime = _time64_to_time(spcl.c_ddate);
	dumpdate = _time64_to_time(spcl.c_date);
	if (stat(".", &stbuf) < 0) {
		fprintf(stderr, "cannot stat .: %s\n", strerror(errno));
		done(1);
	}
	if (stbuf.st_blksize > 0 && stbuf.st_blksize < TP_BSIZE )
		fssize = TP_BSIZE;
	if (stbuf.st_blksize >= TP_BSIZE && stbuf.st_blksize <= MAXBSIZE)
		fssize = stbuf.st_blksize;
	if (((TP_BSIZE - 1) & stbuf.st_blksize) != 0) {
		fprintf(stderr, "Warning: filesystem with non-multiple-of-%d "
		    "blocksize (%d);\n", TP_BSIZE, stbuf.st_blksize);
		fssize = roundup(fssize, TP_BSIZE);
		fprintf(stderr, "\twriting using blocksize %ld\n", fssize);
	}
	if (spcl.c_volume != 1) {
		fprintf(stderr, "Tape is not volume 1 of the dump\n");
		done(1);
	}
	if (gethead(&spcl) == FAIL) {
		dprintf(stdout, "header read failed at %ld blocks\n", blksread);
		panic("no header after volume mark!\n");
	}
	findinode(&spcl);
	if (spcl.c_type != TS_CLRI) {
		fprintf(stderr, "Cannot find file removal list\n");
		done(1);
	}
	maxino = (spcl.c_count * TP_BSIZE * NBBY) + 1;
	dprintf(stdout, "maxino = %ju\n", (uintmax_t)maxino);
	map = calloc((unsigned)1, (unsigned)howmany(maxino, NBBY));
	if (map == NULL)
		panic("no memory for active inode map\n");
	usedinomap = map;
	curfile.action = USING;
	getfile(xtrmap, xtrmapskip, xtrmapskip);
	if (spcl.c_type != TS_BITS) {
		fprintf(stderr, "Cannot find file dump list\n");
		done(1);
	}
	map = calloc((unsigned)1, (unsigned)howmany(maxino, NBBY));
	if (map == (char *)NULL)
		panic("no memory for file dump list\n");
	dumpmap = map;
	curfile.action = USING;
	getfile(xtrmap, xtrmapskip, xtrmapskip);
	/*
	 * If there may be whiteout entries on the tape, pretend that the
	 * whiteout inode exists, so that the whiteout entries can be
	 * extracted.
	 */
	SETINO(UFS_WINO, dumpmap);
	/* 'r' restores don't call getvol() for tape 1, so mark it as read. */
	if (command == 'r')
		tapesread = 1;
}

/*
 * Prompt user to load a new dump volume.
 * "Nextvol" is the next suggested volume to use.
 * This suggested volume is enforced when doing full
 * or incremental restores, but can be overridden by
 * the user when only extracting a subset of the files.
 */
void
getvol(long nextvol)
{
	int64_t prevtapea;
	long i, newvol, savecnt;
	union u_spcl tmpspcl;
#	define tmpbuf tmpspcl.s_spcl
	char buf[TP_BSIZE];

	if (nextvol == 1) {
		tapesread = 0;
		gettingfile = 0;
	}
	prevtapea = tapeaddr;
	savecnt = blksread;
	if (pipein) {
		if (nextvol != 1) {
			panic("Changing volumes on pipe input?\n");
			/* Avoid looping if we couldn't ask the user. */
			if (yflag || ferror(terminal) || feof(terminal))
				done(1);
		}
		if (volno == 1)
			return;
		newvol = 0;
		goto gethdr;
	}
again:
	if (pipein)
		done(1); /* pipes do not get a second chance */
	if (command == 'R' || command == 'r' || curfile.action != SKIP)
		newvol = nextvol;
	else
		newvol = 0;
	while (newvol <= 0) {
		if (tapesread == 0) {
			fprintf(stderr, "%s%s%s%s%s%s%s",
			    "You have not read any tapes yet.\n",
			    "If you are extracting just a few files,",
			    " start with the last volume\n",
			    "and work towards the first; restore",
			    " can quickly skip tapes that\n",
			    "have no further files to extract.",
			    " Otherwise, begin with volume 1.\n");
		} else {
			fprintf(stderr, "You have read volumes");
			strcpy(buf, ": ");
			for (i = 0; i < 32; i++)
				if (tapesread & (1 << i)) {
					fprintf(stderr, "%s%ld", buf, i + 1);
					strcpy(buf, ", ");
				}
			fprintf(stderr, "\n");
		}
		do	{
			fprintf(stderr, "Specify next volume #: ");
			(void) fflush(stderr);
			if (fgets(buf, BUFSIZ, terminal) == NULL)
				done(1);
		} while (buf[0] == '\n');
		newvol = atoi(buf);
		if (newvol <= 0) {
			fprintf(stderr,
			    "Volume numbers are positive numerics\n");
		}
	}
	if (newvol == volno) {
		tapesread |= 1 << (volno - 1);
		return;
	}
	closemt();
	fprintf(stderr, "Mount tape volume %ld\n", newvol);
	fprintf(stderr, "Enter ``none'' if there are no more tapes\n");
	fprintf(stderr, "otherwise enter tape name (default: %s) ", magtape);
	(void) fflush(stderr);
	if (fgets(buf, BUFSIZ, terminal) == NULL)
		done(1);
	if (!strcmp(buf, "none\n")) {
		terminateinput();
		return;
	}
	if (buf[0] != '\n') {
		(void) strcpy(magtape, buf);
		magtape[strlen(magtape) - 1] = '\0';
	}
	if (pipecmdin) {
		char volno[sizeof("2147483647")];

		(void)sprintf(volno, "%ld", newvol);
		if (setenv("RESTORE_VOLUME", volno, 1) == -1) {
			fprintf(stderr, "Cannot set $RESTORE_VOLUME: %s\n",
			    strerror(errno));
			done(1);
		}
		popenfp = popen(magtape, "r");
		mt = popenfp ? fileno(popenfp) : -1;
	} else
#ifdef RRESTORE
	if (host)
		mt = rmtopen(magtape, 0);
	else
#endif
		mt = open(magtape, O_RDONLY, 0);

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
		dprintf(stdout, "header read failed at %ld blocks\n", blksread);
		fprintf(stderr, "tape is not dump tape\n");
		volno = 0;
		goto again;
	}
	if (tmpbuf.c_volume != volno) {
		fprintf(stderr, "Wrong volume (%jd)\n",
		    (intmax_t)tmpbuf.c_volume);
		volno = 0;
		goto again;
	}
	if (_time64_to_time(tmpbuf.c_date) != dumpdate ||
	    _time64_to_time(tmpbuf.c_ddate) != dumptime) {
		time_t t = _time64_to_time(tmpbuf.c_date);
		fprintf(stderr, "Wrong dump date\n\tgot: %s", ctime(&t));
		fprintf(stderr, "\twanted: %s", ctime(&dumpdate));
		volno = 0;
		goto again;
	}
	tapesread |= 1 << (volno - 1);
	blksread = savecnt;
 	/*
 	 * If continuing from the previous volume, skip over any
 	 * blocks read already at the end of the previous volume.
 	 *
 	 * If coming to this volume at random, skip to the beginning
 	 * of the next record.
 	 */
	dprintf(stdout, "last rec %jd, tape starts with %jd\n",
	    (intmax_t)prevtapea, (intmax_t)tmpbuf.c_tapea);
 	if (tmpbuf.c_type == TS_TAPE) {
 		if (curfile.action != USING) {
			/*
			 * XXX Dump incorrectly sets c_count to 1 in the
			 * volume header of the first tape, so ignore
			 * c_count when volno == 1.
			 */
			if (volno != 1)
				for (i = tmpbuf.c_count; i > 0; i--)
					readtape(buf);
 		} else if (tmpbuf.c_tapea <= prevtapea) {
			/*
			 * Normally the value of c_tapea in the volume
			 * header is the record number of the header itself.
			 * However in the volume header following an EOT-
			 * terminated tape, it is the record number of the
			 * first continuation data block (dump bug?).
			 *
			 * The next record we want is `prevtapea + 1'.
			 */
 			i = prevtapea + 1 - tmpbuf.c_tapea;
			dprintf(stderr, "Skipping %ld duplicate record%s.\n",
				i, i > 1 ? "s" : "");
 			while (--i >= 0)
 				readtape(buf);
 		}
 	}
	if (curfile.action == USING) {
		if (volno == 1)
			panic("active file into volume 1\n");
		return;
	}
	(void) gethead(&spcl);
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
	if (pipein) {
		fprintf(stderr, "Cannot have multiple dumps on pipe input\n");
		done(1);
	}
	tcom.mt_op = MTFSF;
	tcom.mt_count = dumpnum - 1;
#ifdef RRESTORE
	if (host)
		rmtioctl(MTFSF, dumpnum - 1);
	else
#endif
		if (!pipecmdin && ioctl(mt, MTIOCTOP, (char *)&tcom) < 0)
			fprintf(stderr, "ioctl MTFSF: %s\n", strerror(errno));
}

void
printdumpinfo(void)
{
	time_t t;
	t = _time64_to_time(spcl.c_date);
	fprintf(stdout, "Dump   date: %s", ctime(&t));
	t = _time64_to_time(spcl.c_ddate);
	fprintf(stdout, "Dumped from: %s",
	    (spcl.c_ddate == 0) ? "the epoch\n" : ctime(&t));
	if (spcl.c_host[0] == '\0')
		return;
	fprintf(stderr, "Level %jd dump of %s on %s:%s\n",
	    (intmax_t)spcl.c_level, spcl.c_filesys, spcl.c_host, spcl.c_dev);
	fprintf(stderr, "Label: %s\n", spcl.c_label);
}

int
extractfile(char *name)
{
	u_int flags;
	uid_t uid;
	gid_t gid;
	mode_t mode;
	int extsize;
	struct timespec mtimep[2], ctimep[2];
	struct entry *ep;
	char *buf;

	curfile.name = name;
	curfile.action = USING;
	mtimep[0].tv_sec = curfile.atime_sec;
	mtimep[0].tv_nsec = curfile.atime_nsec;
	mtimep[1].tv_sec = curfile.mtime_sec;
	mtimep[1].tv_nsec = curfile.mtime_nsec;
	ctimep[0].tv_sec = curfile.atime_sec;
	ctimep[0].tv_nsec = curfile.atime_nsec;
	ctimep[1].tv_sec = curfile.birthtime_sec;
	ctimep[1].tv_nsec = curfile.birthtime_nsec;
	extsize = curfile.extsize;
	uid = getuid();
	if (uid == 0)
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
		vprintf(stdout, "skipped socket %s\n", name);
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
		vprintf(stdout, "extract file %s\n", name);
		return (genliteraldir(name, curfile.ino));

	case IFLNK:
		lnkbuf[0] = '\0';
		pathlen = 0;
		buf = setupextattr(extsize);
		getfile(xtrlnkfile, xtrattr, xtrlnkskip);
		if (pathlen == 0) {
			vprintf(stdout,
			    "%s: zero length symbolic link (ignored)\n", name);
			return (GOOD);
		}
		if (linkit(lnkbuf, name, SYMLINK) == GOOD) {
			if (extsize > 0)
				set_extattr(-1, name, buf, extsize, SXA_LINK);
			(void) lchown(name, uid, gid);
			(void) lchmod(name, mode);
			(void) utimensat(AT_FDCWD, name, ctimep,
			    AT_SYMLINK_NOFOLLOW);
			(void) utimensat(AT_FDCWD, name, mtimep,
			    AT_SYMLINK_NOFOLLOW);
			(void) lchflags(name, flags);
			return (GOOD);
		}
		return (FAIL);

	case IFIFO:
		vprintf(stdout, "extract fifo %s\n", name);
		if (Nflag) {
			skipfile();
			return (GOOD);
		}
		if (uflag)
			(void) unlink(name);
		if (mkfifo(name, 0600) < 0) {
			fprintf(stderr, "%s: cannot create fifo: %s\n",
			    name, strerror(errno));
			skipfile();
			return (FAIL);
		}
		if (extsize == 0) {
			skipfile();
		} else {
			buf = setupextattr(extsize);
			getfile(xtrnull, xtrattr, xtrnull);
			set_extattr(-1, name, buf, extsize, SXA_FILE);
		}
		(void) chown(name, uid, gid);
		(void) chmod(name, mode);
		(void) utimensat(AT_FDCWD, name, ctimep, 0);
		(void) utimensat(AT_FDCWD, name, mtimep, 0);
		(void) chflags(name, flags);
		return (GOOD);

	case IFCHR:
	case IFBLK:
		vprintf(stdout, "extract special file %s\n", name);
		if (Nflag) {
			skipfile();
			return (GOOD);
		}
		if (uflag)
			(void) unlink(name);
		if (mknod(name, (mode & (IFCHR | IFBLK)) | 0600,
		    (int)curfile.rdev) < 0) {
			fprintf(stderr, "%s: cannot create special file: %s\n",
			    name, strerror(errno));
			skipfile();
			return (FAIL);
		}
		if (extsize == 0) {
			skipfile();
		} else {
			buf = setupextattr(extsize);
			getfile(xtrnull, xtrattr, xtrnull);
			set_extattr(-1, name, buf, extsize, SXA_FILE);
		}
		(void) chown(name, uid, gid);
		(void) chmod(name, mode);
		(void) utimensat(AT_FDCWD, name, ctimep, 0);
		(void) utimensat(AT_FDCWD, name, mtimep, 0);
		(void) chflags(name, flags);
		return (GOOD);

	case IFREG:
		vprintf(stdout, "extract file %s\n", name);
		if (Nflag) {
			skipfile();
			return (GOOD);
		}
		if (uflag)
			(void) unlink(name);
		if ((ofile = open(name, O_WRONLY | O_CREAT | O_TRUNC,
		    0600)) < 0) {
			fprintf(stderr, "%s: cannot create file: %s\n",
			    name, strerror(errno));
			skipfile();
			return (FAIL);
		}
		buf = setupextattr(extsize);
		getfile(xtrfile, xtrattr, xtrskip);
		if (extsize > 0)
			set_extattr(ofile, name, buf, extsize, SXA_FD);
		(void) fchown(ofile, uid, gid);
		(void) fchmod(ofile, mode);
		(void) futimens(ofile, ctimep);
		(void) futimens(ofile, mtimep);
		(void) fchflags(ofile, flags);
		(void) close(ofile);
		return (GOOD);
	}
	/* NOTREACHED */
}

/*
 * Set attributes on a file descriptor, link, or file.
 */
void
set_extattr(int fd, char *name, void *buf, int size, enum set_extattr_mode mode)
{
	struct extattr *eap, *eaend;
	const char *method;
	ssize_t res;
	int error;
	char eaname[EXTATTR_MAXNAMELEN + 1];

	vprintf(stdout, "Set attributes for %s:", name);
	eaend = buf + size;
	for (eap = buf; eap < eaend; eap = EXTATTR_NEXT(eap)) {
		/*
		 * Make sure this entry is complete.
		 */
		if (EXTATTR_NEXT(eap) > eaend || eap->ea_length <= 0) {
			dprintf(stdout, "\n\t%scorrupted",
				eap == buf ? "" : "remainder ");
			break;
		}
		if (eap->ea_namespace == EXTATTR_NAMESPACE_EMPTY)
			continue;
		snprintf(eaname, sizeof(eaname), "%.*s",
		    (int)eap->ea_namelength, eap->ea_name);
		vprintf(stdout, "\n\t%s, (%d bytes), %s",
			namespace_names[eap->ea_namespace], eap->ea_length,
			eaname);
		/*
		 * First we try the general attribute setting interface.
		 * However, some attributes can only be set by root or
		 * by using special interfaces (for example, ACLs).
		 */
		if (mode == SXA_FD) {
			res = extattr_set_fd(fd, eap->ea_namespace,
			    eaname, EXTATTR_CONTENT(eap),
			    EXTATTR_CONTENT_SIZE(eap));
			method = "extattr_set_fd";
		} else if (mode == SXA_LINK) {
			res = extattr_set_link(name, eap->ea_namespace,
			    eaname, EXTATTR_CONTENT(eap),
			    EXTATTR_CONTENT_SIZE(eap));
			method = "extattr_set_link";
		} else if (mode == SXA_FILE) {
			res = extattr_set_file(name, eap->ea_namespace,
			    eaname, EXTATTR_CONTENT(eap),
			    EXTATTR_CONTENT_SIZE(eap));
			method = "extattr_set_file";
		}
		if (res != -1) {
			dprintf(stdout, " (set using %s)", method);
			continue;
		}
		/*
		 * If the general interface refuses to set the attribute,
		 * then we try all the specialized interfaces that we
		 * know about.
		 */
		if (eap->ea_namespace == EXTATTR_NAMESPACE_SYSTEM &&
		    strcmp(eaname, POSIX1E_ACL_ACCESS_EXTATTR_NAME) == 0) {
			if (mode == SXA_FD) {
				error = acl_set_fd(fd, EXTATTR_CONTENT(eap));
				method = "acl_set_fd";
			} else if (mode == SXA_LINK) {
				error = acl_set_link_np(name, ACL_TYPE_ACCESS,
				    EXTATTR_CONTENT(eap));
				method = "acl_set_link_np";
			} else if (mode == SXA_FILE) {
				error = acl_set_file(name, ACL_TYPE_ACCESS,
				    EXTATTR_CONTENT(eap));
				method = "acl_set_file";
			}
			if (error != -1) {
				dprintf(stdout, " (set using %s)", method);
				continue;
			}
		}
		if (eap->ea_namespace == EXTATTR_NAMESPACE_SYSTEM &&
		    strcmp(eaname, POSIX1E_ACL_DEFAULT_EXTATTR_NAME) == 0) {
			if (mode == SXA_LINK) {
				error = acl_set_link_np(name, ACL_TYPE_DEFAULT,
				    EXTATTR_CONTENT(eap));
				method = "acl_set_link_np";
			} else {
				error = acl_set_file(name, ACL_TYPE_DEFAULT,
				    EXTATTR_CONTENT(eap));
				method = "acl_set_file";
			}
			if (error != -1) {
				dprintf(stdout, " (set using %s)", method);
				continue;
			}
		}
		vprintf(stdout, " (unable to set)");
	}
	vprintf(stdout, "\n");
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
	getfile(xtrnull, xtrnull, xtrnull);
}

/*
 * Skip a hole in an output file
 */
static void
skiphole(void (*skip)(char *, size_t), size_t *seekpos)
{
	char buf[MAXBSIZE];

	if (*seekpos > 0) {
		(*skip)(buf, *seekpos);
		*seekpos = 0;
	}
}

/*
 * Extract a file from the tape.
 * When an allocated block is found it is passed to the fill function;
 * when an unallocated block (hole) is found, a zeroed buffer is passed
 * to the skip function.
 */
void
getfile(void (*datafill)(char *, size_t), void (*attrfill)(char *, size_t),
	void (*skip)(char *, size_t))
{
	int i;
	volatile off_t size;
	size_t seekpos;
	int curblk, attrsize;
	void (*fillit)(char *, size_t);
	char buf[MAXBSIZE / TP_BSIZE][TP_BSIZE];
	char junk[TP_BSIZE];

	curblk = 0;
	size = spcl.c_size;
	seekpos = 0;
	attrsize = spcl.c_extsize;
	if (spcl.c_type == TS_END)
		panic("ran off end of tape\n");
	if (spcl.c_magic != FS_UFS2_MAGIC)
		panic("not at beginning of a file\n");
	if (!gettingfile && setjmp(restart) != 0)
		return;
	gettingfile++;
	fillit = datafill;
	if (size == 0 && attrsize > 0) {
		fillit = attrfill;
		size = attrsize;
		attrsize = 0;
	}
loop:
	for (i = 0; i < spcl.c_count; i++) {
		if (!readmapflag && i > TP_NINDIR) {
			if (Dflag) {
				fprintf(stderr, "spcl.c_count = %jd\n",
				    (intmax_t)spcl.c_count);
				break;
			} else
				panic("spcl.c_count = %jd\n",
				    (intmax_t)spcl.c_count);
		}
		if (readmapflag || spcl.c_addr[i]) {
			readtape(&buf[curblk++][0]);
			if (curblk == fssize / TP_BSIZE) {
				skiphole(skip, &seekpos);
				(*fillit)((char *)buf, (long)(size > TP_BSIZE ?
				     fssize : (curblk - 1) * TP_BSIZE + size));
				curblk = 0;
			}
		} else {
			if (curblk > 0) {
				skiphole(skip, &seekpos);
				(*fillit)((char *)buf, (long)(size > TP_BSIZE ?
				     curblk * TP_BSIZE :
				     (curblk - 1) * TP_BSIZE + size));
				curblk = 0;
			}
			/*
			 * We have a block of a hole. Don't skip it
			 * now, because there may be next adjacent
			 * block of the hole in the file. Postpone the
			 * seek until next file write.
			 */
			seekpos += (long)MIN(TP_BSIZE, size);
		}
		if ((size -= TP_BSIZE) <= 0) {
			if (size > -TP_BSIZE && curblk > 0) {
				skiphole(skip, &seekpos);
				(*fillit)((char *)buf,
					(long)((curblk * TP_BSIZE) + size));
				curblk = 0;
			}
			if (attrsize > 0) {
				fillit = attrfill;
				size = attrsize;
				attrsize = 0;
				continue;
			}
			if (spcl.c_count - i > 1)
				dprintf(stdout, "skipping %d junk block(s)\n",
					spcl.c_count - i - 1);
			for (i++; i < spcl.c_count; i++) {
				if (!readmapflag && i > TP_NINDIR) {
					if (Dflag) {
						fprintf(stderr,
						    "spcl.c_count = %jd\n",
						    (intmax_t)spcl.c_count);
						break;
					} else 
						panic("spcl.c_count = %jd\n",
						    (intmax_t)spcl.c_count);
				}
				if (readmapflag || spcl.c_addr[i])
					readtape(junk);
			}
			break;
		}
	}
	if (gethead(&spcl) == GOOD && size > 0) {
		if (spcl.c_type == TS_ADDR)
			goto loop;
		dprintf(stdout,
			"Missing address (header) block for %s at %ld blocks\n",
			curfile.name, blksread);
	}
	if (curblk > 0)
		panic("getfile: lost data\n");
	findinode(&spcl);
	gettingfile = 0;
}

/*
 * These variables are shared between the next two functions.
 */
static int extbufsize = 0;
static char *extbuf;
static int extloc;

/*
 * Allocate a buffer into which to extract extended attributes.
 */
static char *
setupextattr(int extsize)
{

	extloc = 0;
	if (extsize <= extbufsize)
		return (extbuf);
	if (extbufsize > 0)
		free(extbuf);
	if ((extbuf = malloc(extsize)) != NULL) {
		extbufsize = extsize;
		return (extbuf);
	}
	extbufsize = 0;
	extbuf = NULL;
	fprintf(stderr, "Cannot extract %d bytes %s for inode %ju, name %s\n",
	    extsize, "of extended attributes", (uintmax_t)curfile.ino,
	    curfile.name);
	return (NULL);
}

/*
 * Extract the next block of extended attributes.
 */
static void
xtrattr(char *buf, size_t size)
{

	if (extloc + size > extbufsize)
		panic("overrun attribute buffer\n");
	memmove(&extbuf[extloc], buf, size);
	extloc += size;
}

/*
 * Write out the next block of a file.
 */
static void
xtrfile(char *buf, size_t size)
{

	if (Nflag)
		return;
	if (write(ofile, buf, (int) size) == -1) {
		fprintf(stderr,
		    "write error extracting inode %ju, name %s\nwrite: %s\n",
		    (uintmax_t)curfile.ino, curfile.name, strerror(errno));
	}
}

/*
 * Skip over a hole in a file.
 */
/* ARGSUSED */
static void
xtrskip(char *buf, size_t size)
{

	if (lseek(ofile, size, SEEK_CUR) == -1) {
		fprintf(stderr,
		    "seek error extracting inode %ju, name %s\nlseek: %s\n",
		    (uintmax_t)curfile.ino, curfile.name, strerror(errno));
		done(1);
	}
}

/*
 * Collect the next block of a symbolic link.
 */
static void
xtrlnkfile(char *buf, size_t size)
{

	pathlen += size;
	if (pathlen > MAXPATHLEN) {
		fprintf(stderr, "symbolic link name: %s->%s%s; too long %d\n",
		    curfile.name, lnkbuf, buf, pathlen);
		done(1);
	}
	(void) strcat(lnkbuf, buf);
}

/*
 * Skip over a hole in a symbolic link (should never happen).
 */
/* ARGSUSED */
static void
xtrlnkskip(char *buf, size_t size)
{

	fprintf(stderr, "unallocated block in symbolic link %s\n",
		curfile.name);
	done(1);
}

/*
 * Collect the next block of a bit map.
 */
static void
xtrmap(char *buf, size_t size)
{

	memmove(map, buf, size);
	map += size;
}

/*
 * Skip over a hole in a bit map (should never happen).
 */
/* ARGSUSED */
static void
xtrmapskip(char *buf, size_t size)
{

	panic("hole in map\n");
	map += size;
}

/*
 * Noop, when an extraction function is not needed.
 */
/* ARGSUSED */
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
	long rd, newvol, i, oldnumtrec;
	int cnt, seek_failed;

	if (blkcnt + (byteslide > 0) < numtrec) {
		memmove(buf, &tapebuf[(blkcnt++ * TP_BSIZE) + byteslide], (long)TP_BSIZE);
		blksread++;
		tapeaddr++;
		return;
	}
	if (numtrec > 0)
		memmove(&tapebuf[-TP_BSIZE],
		    &tapebuf[(numtrec-1) * TP_BSIZE], (long)TP_BSIZE);
	oldnumtrec = numtrec;
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
	if (!pipein && !pipecmdin && numtrec < ntrec && i > 0) {
		dprintf(stdout, "mid-media short read error.\n");
		numtrec = ntrec;
	}
	/*
	 * Handle partial block read.
	 */
	if ((pipein || pipecmdin) && i == 0 && rd > 0)
		i = rd;
	else if (i > 0 && i != ntrec * TP_BSIZE) {
		if (pipein || pipecmdin) {
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
				vprintf(stdout,
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
			fprintf(stderr, "skipping over inode %ju\n",
			    (uintmax_t)curfile.ino);
			break;
		}
		if (!yflag && !reply("continue"))
			done(1);
		i = ntrec * TP_BSIZE;
		memset(tapebuf, 0, i);
#ifdef RRESTORE
		if (host)
			seek_failed = (rmtseek(i, 1) < 0);
		else
#endif
			seek_failed = (lseek(mt, i, SEEK_CUR) == (off_t)-1);

		if (seek_failed) {
			fprintf(stderr,
			    "continuation failed: %s\n", strerror(errno));
			done(1);
		}
	}
	/*
	 * Handle end of tape.
	 */
	if (i == 0) {
		vprintf(stdout, "End-of-tape encountered\n");
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
		memmove(&tapebuf[rd], &endoftapemark, (long)TP_BSIZE);
	}
	if (oldnumtrec == 0)
		blkcnt = 0;
	else
		blkcnt -= oldnumtrec;
	memmove(buf,
	    &tapebuf[(blkcnt++ * TP_BSIZE) + byteslide], (long)TP_BSIZE);
	blksread++;
	tapeaddr++;
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

	if (i <= 0) {
		fprintf(stderr, "tape read error: %s\n", strerror(errno));
		done(1);
	}
	if (i % TP_BSIZE != 0) {
		fprintf(stderr, "Tape block size (%ld) %s (%d)\n",
			i, "is not a multiple of dump block size", TP_BSIZE);
		done(1);
	}
	ntrec = i / TP_BSIZE;
	numtrec = ntrec;
	vprintf(stdout, "Tape block size is %ld\n", ntrec);
}

void
closemt(void)
{

	if (mt < 0)
		return;
	if (pipecmdin) {
		pclose(popenfp);
		popenfp = NULL;
	} else
#ifdef RRESTORE
	if (host)
		rmtclose();
	else
#endif
		(void) close(mt);
}

/*
 * Read the next block from the tape.
 * If it is not any valid header, return an error.
 */
static int
gethead(struct s_spcl *buf)
{
	long i;

	readtape((char *)buf);
	if (buf->c_magic != FS_UFS2_MAGIC && buf->c_magic != NFS_MAGIC) {
		if (buf->c_magic == OFS_MAGIC) {
			fprintf(stderr,
			    "Format of dump tape is too old. Must use\n");
			fprintf(stderr,
			    "a version of restore from before 2002.\n");
			return (FAIL);
		}
		if (swabl(buf->c_magic) != FS_UFS2_MAGIC &&
		    swabl(buf->c_magic) != NFS_MAGIC) {
			if (swabl(buf->c_magic) == OFS_MAGIC) {
				fprintf(stderr,
				  "Format of dump tape is too old. Must use\n");
				fprintf(stderr,
				  "a version of restore from before 2002.\n");
			}
			return (FAIL);
		}
		if (!Bcvt) {
			vprintf(stdout, "Note: Doing Byte swapping\n");
			Bcvt = 1;
		}
	}
	if (checksum((int *)buf) == FAIL)
		return (FAIL);
	if (Bcvt) {
		swabst((u_char *)"8l4s1q8l2q17l", (u_char *)buf);
		swabst((u_char *)"l",(u_char *) &buf->c_level);
		swabst((u_char *)"2l4q",(u_char *) &buf->c_flags);
	}
	readmapflag = 0;

	switch (buf->c_type) {

	case TS_CLRI:
	case TS_BITS:
		/*
		 * Have to patch up missing information in bit map headers
		 */
		buf->c_size = buf->c_count * TP_BSIZE;
		if (buf->c_count > TP_NINDIR)
			readmapflag = 1;
		else 
			for (i = 0; i < buf->c_count; i++)
				buf->c_addr[i]++;
		/* FALL THROUGH */

	case TS_TAPE:
		if (buf->c_magic == NFS_MAGIC &&
		    (buf->c_flags & NFS_DR_NEWINODEFMT) == 0)
			oldinofmt = 1;
		/* FALL THROUGH */

	case TS_END:
		buf->c_inumber = 0;
		/* FALL THROUGH */

	case TS_ADDR:
	case TS_INODE:
		/*
		 * For old dump tapes, have to copy up old fields to
		 * new locations.
		 */
		if (buf->c_magic == NFS_MAGIC) {
			buf->c_tapea = buf->c_old_tapea;
			buf->c_firstrec = buf->c_old_firstrec;
			buf->c_date = _time32_to_time(buf->c_old_date);
			buf->c_ddate = _time32_to_time(buf->c_old_ddate);
			buf->c_atime = _time32_to_time(buf->c_old_atime);
			buf->c_mtime = _time32_to_time(buf->c_old_mtime);
			buf->c_birthtime = 0;
			buf->c_birthtimensec = 0;
			buf->c_extsize = 0;
		}
		break;

	default:
		panic("gethead: unknown inode type %d\n", buf->c_type);
		break;
	}
	if (dumpdate != 0 && _time64_to_time(buf->c_date) != dumpdate)
		fprintf(stderr, "Header with wrong dumpdate.\n");
	/*
	 * If we're restoring a filesystem with the old (FreeBSD 1)
	 * format inodes, copy the uid/gid to the new location
	 */
	if (oldinofmt) {
		buf->c_uid = buf->c_spare1[1];
		buf->c_gid = buf->c_spare1[2];
	}
	buf->c_magic = FS_UFS2_MAGIC;
	tapeaddr = buf->c_tapea;
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
	static ino_t previno = 0x7fffffff;
	static int prevtype;
	static long predict;
	long blks, i;

	if (header->c_type == TS_TAPE) {
		fprintf(stderr, "Volume header ");
 		if (header->c_firstrec)
 			fprintf(stderr, "begins with record %jd",
			    (intmax_t)header->c_firstrec);
 		fprintf(stderr, "\n");
		previno = 0x7fffffff;
		return;
	}
	if (previno == 0x7fffffff)
		goto newcalc;
	switch (prevtype) {
	case TS_BITS:
		fprintf(stderr, "Dumped inodes map header");
		break;
	case TS_CLRI:
		fprintf(stderr, "Used inodes map header");
		break;
	case TS_INODE:
		fprintf(stderr, "File header, ino %ju", (uintmax_t)previno);
		break;
	case TS_ADDR:
		fprintf(stderr, "File continuation header, ino %ju",
		    (uintmax_t)previno);
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
	if (header->c_type != TS_END)
		for (i = 0; i < header->c_count; i++)
			if (readmapflag || header->c_addr[i] != 0)
				blks++;
	predict = blks;
	blksread = 0;
	prevtype = header->c_type;
	previno = header->c_inumber;
}

/*
 * Find an inode header.
 * Complain if had to skip.
 */
static void
findinode(struct s_spcl *header)
{
	static long skipcnt = 0;
	long i;
	char buf[TP_BSIZE];
	int htype;

	curfile.name = "<name unknown>";
	curfile.action = UNKNOWN;
	curfile.mode = 0;
	curfile.ino = 0;
	do {
		htype = header->c_type;
		switch (htype) {

		case TS_ADDR:
			/*
			 * Skip up to the beginning of the next record
			 */
			for (i = 0; i < header->c_count; i++)
				if (header->c_addr[i])
					readtape(buf);
			while (gethead(header) == FAIL ||
			    _time64_to_time(header->c_date) != dumpdate) {
				skipcnt++;
				if (Dflag) {
					byteslide++;
					if (byteslide < TP_BSIZE) {
						blkcnt--;
						blksread--;
					} else 
						byteslide = 0;
				}
			}
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
			curfile.extsize = header->c_extsize;
			curfile.size = header->c_size;
			curfile.ino = header->c_inumber;
			break;

		case TS_END:
			/* If we missed some tapes, get another volume. */
			if (tapesread & (tapesread + 1)) {
				getvol(0);
				continue;
			}
			curfile.ino = maxino;
			break;

		case TS_CLRI:
			curfile.name = "<file removal list>";
			break;

		case TS_BITS:
			curfile.name = "<file dump list>";
			break;

		case TS_TAPE:
			if (Dflag)
				fprintf(stderr, "unexpected tape header\n");
			else
				panic("unexpected tape header\n");

		default:
			if (Dflag)
				fprintf(stderr, "unknown tape header type %d\n",
				    spcl.c_type);
			else
				panic("unknown tape header type %d\n",
				    spcl.c_type);
			while (gethead(header) == FAIL ||
			    _time64_to_time(header->c_date) != dumpdate) {
				skipcnt++;
				if (Dflag) {
					byteslide++;
					if (byteslide < TP_BSIZE) {
						blkcnt--;
						blksread--;
					} else 
						byteslide = 0;
				}
			}

		}
	} while (htype == TS_ADDR);
	if (skipcnt > 0)
		fprintf(stderr, "resync restore, skipped %ld %s\n",
		    skipcnt, Dflag ? "bytes" : "blocks");
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
		/* What happens if we want to read restore tapes
			for a 16bit int machine??? */
		do
			i += swabl(*buf++);
		while (--j);
	}

	if (i != CHECKSUM) {
		fprintf(stderr, "Checksum error %o, inode %ju file %s\n", i,
		    (uintmax_t)curfile.ino, curfile.name);
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

static u_char *
swabshort(u_char *sp, int n)
{
	char c;

	while (--n >= 0) {
		c = sp[0]; sp[0] = sp[1]; sp[1] = c;
		sp += 2;
	}
	return (sp);
}

static u_char *
swablong(u_char *sp, int n)
{
	char c;

	while (--n >= 0) {
		c = sp[0]; sp[0] = sp[3]; sp[3] = c;
		c = sp[2]; sp[2] = sp[1]; sp[1] = c;
		sp += 4;
	}
	return (sp);
}

static u_char *
swabquad(u_char *sp, int n)
{
	char c;

	while (--n >= 0) {
		c = sp[0]; sp[0] = sp[7]; sp[7] = c;
		c = sp[1]; sp[1] = sp[6]; sp[6] = c;
		c = sp[2]; sp[2] = sp[5]; sp[5] = c;
		c = sp[3]; sp[3] = sp[4]; sp[4] = c;
		sp += 8;
	}
	return (sp);
}

void
swabst(u_char *cp, u_char *sp)
{
	int n = 0;

	while (*cp) {
		switch (*cp) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			n = (n * 10) + (*cp++ - '0');
			continue;

		case 's': case 'w': case 'h':
			if (n == 0)
				n = 1;
			sp = swabshort(sp, n);
			break;

		case 'l':
			if (n == 0)
				n = 1;
			sp = swablong(sp, n);
			break;

		case 'q':
			if (n == 0)
				n = 1;
			sp = swabquad(sp, n);
			break;

		case 'b':
			if (n == 0)
				n = 1;
			sp += n;
			break;

		default:
			fprintf(stderr, "Unknown conversion character: %c\n",
			    *cp);
			done(0);
			break;
		}
		cp++;
		n = 0;
	}
}

static u_long
swabl(u_long x)
{
	swabst((u_char *)"l", (u_char *)&x);
	return (x);
}
