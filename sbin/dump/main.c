/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1991, 1993, 1994
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1991, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)main.c	8.6 (Berkeley) 5/1/95";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/disklabel.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ffs/fs.h>

#include <protocols/dumprestore.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fstab.h>
#include <libufs.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <timeconv.h>
#include <unistd.h>

#include "dump.h"
#include "pathnames.h"

int	notify = 0;	/* notify operator flag */
int	snapdump = 0;	/* dumping live filesystem, so use snapshot */
int	blockswritten = 0;	/* number of blocks written on current tape */
int	tapeno = 0;	/* current tape number */
int	density = 0;	/* density in bytes/0.1" " <- this is for hilit19 */
int	ntrec = NTREC;	/* # tape blocks in each tape record */
int	cartridge = 0;	/* Assume non-cartridge tape */
int	cachesize = 0;	/* block cache size (in bytes), defaults to 0 */
long	dev_bsize = 1;	/* recalculated below */
long	blocksperfile;	/* output blocks per file */
char	*host = NULL;	/* remote host (if any) */

static char *getmntpt(char *, int *);
static long numarg(const char *, long, long);
static void obsolete(int *, char **[]);
static void usage(void) __dead2;

int
main(int argc, char *argv[])
{
	struct stat sb;
	ino_t ino;
	int dirty;
	union dinode *dp;
	struct fstab *dt;
	char *map, *mntpt;
	int ch, mode, mntflags;
	int i, ret, anydirskipped, bflag = 0, Tflag = 0, honorlevel = 1;
	int just_estimate = 0;
	ino_t maxino;
	char *tmsg;

	spcl.c_date = _time_to_time64(time(NULL));

	tsize = 0;	/* Default later, based on 'c' option for cart tapes */
	dumpdates = _PATH_DUMPDATES;
	popenout = NULL;
	tape = NULL;
	temp = _PATH_DTMP;
	if (TP_BSIZE / DEV_BSIZE == 0 || TP_BSIZE % DEV_BSIZE != 0)
		quit("TP_BSIZE must be a multiple of DEV_BSIZE\n");
	level = 0;
	rsync_friendly = 0;

	if (argc < 2)
		usage();

	obsolete(&argc, &argv);
	while ((ch = getopt(argc, argv,
	    "0123456789aB:b:C:cD:d:f:h:LnP:RrSs:T:uWw")) != -1)
		switch (ch) {
		/* dump level */
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			level = 10 * level + ch - '0';
			break;

		case 'a':		/* `auto-size', Write to EOM. */
			unlimited = 1;
			break;

		case 'B':		/* blocks per output file */
			blocksperfile = numarg("number of blocks per file",
			    1L, 0L);
			break;

		case 'b':		/* blocks per tape write */
			ntrec = numarg("number of blocks per write",
			    1L, 1000L);
			break;

		case 'C':
			cachesize = numarg("cachesize", 0, 0) * 1024 * 1024;
			break;

		case 'c':		/* Tape is cart. not 9-track */
			cartridge = 1;
			break;

		case 'D':
			dumpdates = optarg;
			break;

		case 'd':		/* density, in bits per inch */
			density = numarg("density", 10L, 327670L) / 10;
			if (density >= 625 && !bflag)
				ntrec = HIGHDENSITYTREC;
			break;

		case 'f':		/* output file */
			if (popenout != NULL)
				errx(X_STARTUP, "You cannot use the P and f "
				    "flags together.\n");
			tape = optarg;
			break;

		case 'h':
			honorlevel = numarg("honor level", 0L, 10L);
			break;

		case 'L':
			snapdump = 1;
			break;

		case 'n':		/* notify operators */
			notify = 1;
			break;

		case 'P':
			if (tape != NULL)
				errx(X_STARTUP, "You cannot use the P and f "
				    "flags together.\n");
			popenout = optarg;
			break;

		case 'r': /* store slightly less data to be friendly to rsync */
			if (rsync_friendly < 1)
				rsync_friendly = 1;
			break;

		case 'R': /* store even less data to be friendlier to rsync */
			if (rsync_friendly < 2)
				rsync_friendly = 2;
			break;

		case 'S':               /* exit after estimating # of tapes */
			just_estimate = 1;
			break;

		case 's':		/* tape size, feet */
			tsize = numarg("tape size", 1L, 0L) * 12 * 10;
			break;

		case 'T':		/* time of last dump */
			spcl.c_ddate = unctime(optarg);
			if (spcl.c_ddate < 0) {
				(void)fprintf(stderr, "bad time \"%s\"\n",
				    optarg);
				exit(X_STARTUP);
			}
			Tflag = 1;
			lastlevel = -1;
			break;

		case 'u':		/* update /etc/dumpdates */
			uflag = 1;
			break;

		case 'W':		/* what to do */
		case 'w':
			lastdump(ch);
			exit(X_FINOK);	/* do nothing else */

		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void)fprintf(stderr, "Must specify disk or file system\n");
		exit(X_STARTUP);
	}
	disk = *argv++;
	argc--;
	if (argc >= 1) {
		(void)fprintf(stderr, "Unknown arguments to dump:");
		while (argc--)
			(void)fprintf(stderr, " %s", *argv++);
		(void)fprintf(stderr, "\n");
		exit(X_STARTUP);
	}
	if (rsync_friendly && (level > 0)) {
		(void)fprintf(stderr, "%s %s\n", "rsync friendly options",
		    "can be used only with level 0 dumps.");
		exit(X_STARTUP);
	}
	if (Tflag && uflag) {
	        (void)fprintf(stderr,
		    "You cannot use the T and u flags together.\n");
		exit(X_STARTUP);
	}
	if (popenout) {
		tape = "child pipeline process";
	} else if (tape == NULL && (tape = getenv("TAPE")) == NULL)
		tape = _PATH_DEFTAPE;
	if (strcmp(tape, "-") == 0) {
		pipeout++;
		tape = "standard output";
	}

	if (blocksperfile)
		blocksperfile = rounddown(blocksperfile, ntrec);
	else if (!unlimited) {
		/*
		 * Determine how to default tape size and density
		 *
		 *         	density				tape size
		 * 9-track	1600 bpi (160 bytes/.1")	2300 ft.
		 * 9-track	6250 bpi (625 bytes/.1")	2300 ft.
		 * cartridge	8000 bpi (100 bytes/.1")	1700 ft.
		 *						(450*4 - slop)
		 * hilit19 hits again: "
		 */
		if (density == 0)
			density = cartridge ? 100 : 160;
		if (tsize == 0)
			tsize = cartridge ? 1700L*120L : 2300L*120L;
	}

	if (strchr(tape, ':')) {
		host = tape;
		tape = strchr(host, ':');
		*tape++ = '\0';
#ifdef RDUMP
		if (strchr(tape, '\n')) {
		    (void)fprintf(stderr, "invalid characters in tape\n");
		    exit(X_STARTUP);
		}
		if (rmthost(host) == 0)
			exit(X_STARTUP);
#else
		(void)fprintf(stderr, "remote dump not enabled\n");
		exit(X_STARTUP);
#endif
	}
	(void)setuid(getuid()); /* rmthost() is the only reason to be setuid */

	if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
		signal(SIGHUP, sig);
	if (signal(SIGTRAP, SIG_IGN) != SIG_IGN)
		signal(SIGTRAP, sig);
	if (signal(SIGFPE, SIG_IGN) != SIG_IGN)
		signal(SIGFPE, sig);
	if (signal(SIGBUS, SIG_IGN) != SIG_IGN)
		signal(SIGBUS, sig);
	if (signal(SIGSEGV, SIG_IGN) != SIG_IGN)
		signal(SIGSEGV, sig);
	if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
		signal(SIGTERM, sig);
	if (signal(SIGINT, interrupt) == SIG_IGN)
		signal(SIGINT, SIG_IGN);

	dump_getfstab();	/* /etc/fstab snarfed */
	/*
	 *	disk can be either the full special file name,
	 *	the suffix of the special file name,
	 *	the special name missing the leading '/',
	 *	the file system name with or without the leading '/'.
	 */
	dt = fstabsearch(disk);
	if (dt != NULL) {
		disk = rawname(dt->fs_spec);
 		if (disk == NULL)
 			errx(X_STARTUP, "%s: unknown file system", dt->fs_spec);
		(void)strncpy(spcl.c_dev, dt->fs_spec, NAMELEN);
		(void)strncpy(spcl.c_filesys, dt->fs_file, NAMELEN);
	} else {
		(void)strncpy(spcl.c_dev, disk, NAMELEN);
		(void)strncpy(spcl.c_filesys, "an unlisted file system",
		    NAMELEN);
	}
	spcl.c_dev[NAMELEN-1]='\0';
	spcl.c_filesys[NAMELEN-1]='\0';

	if ((mntpt = getmntpt(disk, &mntflags)) != NULL) {
		if (mntflags & MNT_RDONLY) {
			if (snapdump != 0) {
				msg("WARNING: %s\n",
				    "-L ignored for read-only filesystem.");
				snapdump = 0;
			}
		} else if (snapdump == 0) {
			msg("WARNING: %s\n",
			    "should use -L when dumping live read-write "
			    "filesystems!");
		} else {
			char snapname[BUFSIZ], snapcmd[BUFSIZ];

			snprintf(snapname, sizeof snapname, "%s/.snap", mntpt);
			if ((stat(snapname, &sb) < 0) || !S_ISDIR(sb.st_mode)) {
				msg("WARNING: %s %s\n",
				    "-L requested but snapshot location",
				    snapname);
				msg("         %s: %s\n",
				    "is not a directory",
				    "dump downgraded, -L ignored");
				snapdump = 0;
			} else {
				snprintf(snapname, sizeof snapname,
				    "%s/.snap/dump_snapshot", mntpt);
				snprintf(snapcmd, sizeof snapcmd, "%s %s %s",
				    _PATH_MKSNAP_FFS, mntpt, snapname);
				unlink(snapname);
				if (system(snapcmd) != 0)
					errx(X_STARTUP, "Cannot create %s: %s\n",
					    snapname, strerror(errno));
				if ((diskfd = open(snapname, O_RDONLY)) < 0) {
					unlink(snapname);
					errx(X_STARTUP, "Cannot open %s: %s\n",
					    snapname, strerror(errno));
				}
				unlink(snapname);
				if (fstat(diskfd, &sb) != 0)
					err(X_STARTUP, "%s: stat", snapname);
				spcl.c_date = _time_to_time64(sb.st_mtime);
			}
		}
	} else if (snapdump != 0) {
		msg("WARNING: Cannot use -L on an unmounted filesystem.\n");
		snapdump = 0;
	}
	if (snapdump == 0) {
		if ((diskfd = open(disk, O_RDONLY)) < 0)
			err(X_STARTUP, "Cannot open %s", disk);
		if (fstat(diskfd, &sb) != 0)
			err(X_STARTUP, "%s: stat", disk);
		if (S_ISDIR(sb.st_mode))
			errx(X_STARTUP, "%s: unknown file system", disk);
	}

	(void)strcpy(spcl.c_label, "none");
	(void)gethostname(spcl.c_host, NAMELEN);
	spcl.c_level = level;
	spcl.c_type = TS_TAPE;
	if (rsync_friendly) {
		/* don't store real dump times */
		spcl.c_date = 0;
		spcl.c_ddate = 0;
	}
	if (spcl.c_date == 0) {
		tmsg = "the epoch\n";
	} else {
		time_t t = _time64_to_time(spcl.c_date);
		tmsg = ctime(&t);
	}
	msg("Date of this level %d dump: %s", level, tmsg);

	if (!Tflag && (!rsync_friendly))
	        getdumptime();		/* /etc/dumpdates snarfed */
	if (spcl.c_ddate == 0) {
		tmsg = "the epoch\n";
	} else {
		time_t t = _time64_to_time(spcl.c_ddate);
		tmsg = ctime(&t);
	}
	if (lastlevel < 0)
		msg("Date of last (level unknown) dump: %s", tmsg);
	else
		msg("Date of last level %d dump: %s", lastlevel, tmsg);

	msg("Dumping %s%s ", snapdump ? "snapshot of ": "", disk);
	if (dt != NULL)
		msgtail("(%s) ", dt->fs_file);
	if (host)
		msgtail("to %s on host %s\n", tape, host);
	else
		msgtail("to %s\n", tape);

	sync();
	if ((ret = sbget(diskfd, &sblock, STDSB)) != 0) {
		switch (ret) {
		case ENOENT:
			warn("Cannot find file system superblock");
			return (1);
		default:
			warn("Unable to read file system superblock");
			return (1);
		}
	}
	dev_bsize = sblock->fs_fsize / fsbtodb(sblock, 1);
	dev_bshift = ffs(dev_bsize) - 1;
	if (dev_bsize != (1 << dev_bshift))
		quit("dev_bsize (%ld) is not a power of 2", dev_bsize);
	tp_bshift = ffs(TP_BSIZE) - 1;
	if (TP_BSIZE != (1 << tp_bshift))
		quit("TP_BSIZE (%d) is not a power of 2", TP_BSIZE);
	maxino = sblock->fs_ipg * sblock->fs_ncg;
	mapsize = roundup(howmany(maxino, CHAR_BIT), TP_BSIZE);
	usedinomap = (char *)calloc((unsigned) mapsize, sizeof(char));
	dumpdirmap = (char *)calloc((unsigned) mapsize, sizeof(char));
	dumpinomap = (char *)calloc((unsigned) mapsize, sizeof(char));
	tapesize = 3 * (howmany(mapsize * sizeof(char), TP_BSIZE) + 1);

	nonodump = spcl.c_level < honorlevel;

	passno = 1;
	setproctitle("%s: pass 1: regular files", disk);
	msg("mapping (Pass I) [regular files]\n");
	anydirskipped = mapfiles(maxino, &tapesize);

	passno = 2;
	setproctitle("%s: pass 2: directories", disk);
	msg("mapping (Pass II) [directories]\n");
	while (anydirskipped) {
		anydirskipped = mapdirs(maxino, &tapesize);
	}

	if (pipeout || unlimited) {
		tapesize += 10;	/* 10 trailer blocks */
		msg("estimated %ld tape blocks.\n", tapesize);
	} else {
		double fetapes;

		if (blocksperfile)
			fetapes = (double) tapesize / blocksperfile;
		else if (cartridge) {
			/* Estimate number of tapes, assuming streaming stops at
			   the end of each block written, and not in mid-block.
			   Assume no erroneous blocks; this can be compensated
			   for with an artificially low tape size. */
			fetapes =
			(	  (double) tapesize	/* blocks */
				* TP_BSIZE	/* bytes/block */
				* (1.0/density)	/* 0.1" / byte " */
			  +
				  (double) tapesize	/* blocks */
				* (1.0/ntrec)	/* streaming-stops per block */
				* 15.48		/* 0.1" / streaming-stop " */
			) * (1.0 / tsize );	/* tape / 0.1" " */
		} else {
			/* Estimate number of tapes, for old fashioned 9-track
			   tape */
			int tenthsperirg = (density == 625) ? 3 : 7;
			fetapes =
			(	  (double) tapesize	/* blocks */
				* TP_BSIZE	/* bytes / block */
				* (1.0/density)	/* 0.1" / byte " */
			  +
				  (double) tapesize	/* blocks */
				* (1.0/ntrec)	/* IRG's / block */
				* tenthsperirg	/* 0.1" / IRG " */
			) * (1.0 / tsize );	/* tape / 0.1" " */
		}
		etapes = fetapes;		/* truncating assignment */
		etapes++;
		/* count the dumped inodes map on each additional tape */
		tapesize += (etapes - 1) *
			(howmany(mapsize * sizeof(char), TP_BSIZE) + 1);
		tapesize += etapes + 10;	/* headers + 10 trailer blks */
		msg("estimated %ld tape blocks on %3.2f tape(s).\n",
		    tapesize, fetapes);
	}

        /*
         * If the user only wants an estimate of the number of
         * tapes, exit now.
         */
        if (just_estimate)
                exit(0);

	/*
	 * Allocate tape buffer.
	 */
	if (!alloctape())
		quit(
	"can't allocate tape buffers - try a smaller blocking factor.\n");

	startnewtape(1);
	(void)time((time_t *)&(tstart_writing));
	dumpmap(usedinomap, TS_CLRI, maxino - 1);

	passno = 3;
	setproctitle("%s: pass 3: directories", disk);
	msg("dumping (Pass III) [directories]\n");
	dirty = 0;		/* XXX just to get gcc to shut up */
	for (map = dumpdirmap, ino = 1; ino < maxino; ino++) {
		if (((ino - 1) % CHAR_BIT) == 0)	/* map is offset by 1 */
			dirty = *map++;
		else
			dirty >>= 1;
		if ((dirty & 1) == 0)
			continue;
		/*
		 * Skip directory inodes deleted and maybe reallocated
		 */
		dp = getino(ino, &mode);
		if (mode != IFDIR)
			continue;
		(void)dumpino(dp, ino);
	}

	passno = 4;
	setproctitle("%s: pass 4: regular files", disk);
	msg("dumping (Pass IV) [regular files]\n");
	for (map = dumpinomap, ino = 1; ino < maxino; ino++) {
		if (((ino - 1) % CHAR_BIT) == 0)	/* map is offset by 1 */
			dirty = *map++;
		else
			dirty >>= 1;
		if ((dirty & 1) == 0)
			continue;
		/*
		 * Skip inodes deleted and reallocated as directories.
		 */
		dp = getino(ino, &mode);
		if (mode == IFDIR)
			continue;
		(void)dumpino(dp, ino);
	}

	(void)time((time_t *)&(tend_writing));
	spcl.c_type = TS_END;
	for (i = 0; i < ntrec; i++)
		writeheader(maxino - 1);
	if (pipeout)
		msg("DUMP: %jd tape blocks\n", (intmax_t)spcl.c_tapea);
	else
		msg("DUMP: %jd tape blocks on %d volume%s\n",
		    (intmax_t)spcl.c_tapea, spcl.c_volume,
		    (spcl.c_volume == 1) ? "" : "s");

	/* report dump performance, avoid division through zero */
	if (tend_writing - tstart_writing == 0)
		msg("finished in less than a second\n");
	else
		msg("finished in %jd seconds, throughput %jd KBytes/sec\n",
		    (intmax_t)tend_writing - tstart_writing, 
		    (intmax_t)(spcl.c_tapea / 
		    (tend_writing - tstart_writing)));

	putdumptime();
	trewind();
	broadcast("DUMP IS DONE!\a\a\n");
	msg("DUMP IS DONE\n");
	Exit(X_FINOK);
	/* NOTREACHED */
}

static void
usage(void)
{
	fprintf(stderr,
		"usage: dump [-0123456789acLnSu] [-B records] [-b blocksize] [-C cachesize]\n"
		"            [-D dumpdates] [-d density] [-f file | -P pipecommand] [-h level]\n"
		"            [-s feet] [-T date] filesystem\n"
		"       dump -W | -w\n");
	exit(X_STARTUP);
}

/*
 * Check to see if a disk is currently mounted.
 */
static char *
getmntpt(char *name, int *mntflagsp)
{
	long mntsize, i;
	struct statfs *mntbuf;

	mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
	for (i = 0; i < mntsize; i++) {
		if (!strcmp(mntbuf[i].f_mntfromname, name)) {
			*mntflagsp = mntbuf[i].f_flags;
			return (mntbuf[i].f_mntonname);
		}
	}
	return (0);
}

/*
 * Pick up a numeric argument.  It must be nonnegative and in the given
 * range (except that a vmax of 0 means unlimited).
 */
static long
numarg(const char *meaning, long vmin, long vmax)
{
	char *p;
	long val;

	val = strtol(optarg, &p, 10);
	if (*p)
		errx(1, "illegal %s -- %s", meaning, optarg);
	if (val < vmin || (vmax && val > vmax))
		errx(1, "%s must be between %ld and %ld", meaning, vmin, vmax);
	return (val);
}

void
sig(int signo)
{
	switch(signo) {
	case SIGALRM:
	case SIGBUS:
	case SIGFPE:
	case SIGHUP:
	case SIGTERM:
	case SIGTRAP:
		if (pipeout)
			quit("Signal on pipe: cannot recover\n");
		msg("Rewriting attempted as response to unknown signal.\n");
		(void)fflush(stderr);
		(void)fflush(stdout);
		close_rewind();
		exit(X_REWRITE);
		/* NOTREACHED */
	case SIGSEGV:
		msg("SIGSEGV: ABORTING!\n");
		(void)signal(SIGSEGV, SIG_DFL);
		(void)kill(0, SIGSEGV);
		/* NOTREACHED */
	}
}

char *
rawname(char *cp)
{
	struct stat sb;

	/*
	 * Ensure that the device passed in is a raw device.
	 */
	if (stat(cp, &sb) == 0 && (sb.st_mode & S_IFMT) == S_IFCHR)
		return (cp);

	/*
	 * Since there's only one device type now, we can't construct any
	 * better name, so we have to return NULL.
	 */
	return (NULL);
}

/*
 * obsolete --
 *	Change set of key letters and ordered arguments into something
 *	getopt(3) will like.
 */
static void
obsolete(int *argcp, char **argvp[])
{
	int argc, flags;
	char *ap, **argv, *flagsp, **nargv, *p;

	/* Setup. */
	argv = *argvp;
	argc = *argcp;

	/*
	 * Return if no arguments or first argument has leading
	 * dash or slash.
	 */
	ap = argv[1];
	if (argc == 1 || *ap == '-' || *ap == '/')
		return;

	/* Allocate space for new arguments. */
	if ((*argvp = nargv = malloc((argc + 1) * sizeof(char *))) == NULL ||
	    (p = flagsp = malloc(strlen(ap) + 2)) == NULL)
		err(1, NULL);

	*nargv++ = *argv;
	argv += 2;

	for (flags = 0; *ap; ++ap) {
		switch (*ap) {
		case 'B':
		case 'b':
		case 'd':
		case 'f':
		case 'D':
		case 'C':
		case 'h':
		case 's':
		case 'T':
			if (*argv == NULL) {
				warnx("option requires an argument -- %c", *ap);
				usage();
			}
			if ((nargv[0] = malloc(strlen(*argv) + 2 + 1)) == NULL)
				err(1, NULL);
			nargv[0][0] = '-';
			nargv[0][1] = *ap;
			(void)strcpy(&nargv[0][2], *argv);
			++argv;
			++nargv;
			break;
		default:
			if (!flags) {
				*p++ = '-';
				flags = 1;
			}
			*p++ = *ap;
			break;
		}
	}

	/* Terminate flags. */
	if (flags) {
		*p = '\0';
		*nargv++ = flagsp;
	} else
		free(flagsp);

	/* Copy remaining arguments. */
	while ((*nargv++ = *argv++));

	/* Update argument count. */
	*argcp = nargv - *argvp - 1;
}
