/*	$OpenBSD: main.c,v 1.26 2017/01/21 08:31:44 krw Exp $	*/
/*	$NetBSD: main.c,v 1.13 1997/07/01 05:37:51 lukem Exp $	*/

/*
 * Copyright (c) 1983, 1993
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

#include <sys/stat.h>
#include <sys/time.h>

#include <ufs/ffs/fs.h>
#include <ufs/ufs/dinode.h>
#include <protocols/dumprestore.h>

#include <err.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "restore.h"
#include "extern.h"

int	bflag = 0, cvtflag = 0, dflag = 0, vflag = 0, yflag = 0;
int	hflag = 1, mflag = 1, Nflag = 0;
char	command = '\0';
long	dumpnum = 1;
long	volno = 0;
long	ntrec;
char	*dumpmap;
char	*usedinomap;
ino_t	maxino;
time_t	dumptime;
time_t	dumpdate;
FILE	*terminal;
char	*tmpdir;

static void obsolete(int *, char **[]);
static void usage(void);

int
main(int argc, char *argv[])
{
	int ch;
	ino_t ino;
	char *inputdev;
	char *symtbl = "./restoresymtable";
	char *p, name[PATH_MAX];

	/* Temp files should *not* be readable.  We set permissions later. */
	(void)umask(077);

	if (argc < 2)
		usage();

	if ((inputdev = getenv("TAPE")) == NULL || *inputdev == '\0')
		inputdev = _PATH_DEFTAPE;
	if ((tmpdir = getenv("TMPDIR")) == NULL || *tmpdir == '\0')
		tmpdir = _PATH_TMP;
	if ((tmpdir = strdup(tmpdir)) == NULL)
		err(1, NULL);
	for (p = tmpdir + strlen(tmpdir) - 1; p >= tmpdir && *p == '/'; p--)
		continue;
	obsolete(&argc, &argv);
	while ((ch = getopt(argc, argv, "b:cdf:himNRrs:tvxy")) != -1)
		switch(ch) {
		case 'b':
			/* Change default tape blocksize. */
			bflag = 1;
			ntrec = strtol(optarg, &p, 10);
			if (*p)
				errx(1, "illegal blocksize -- %s", optarg);
			if (ntrec <= 0)
				errx(1, "block size must be greater than 0");
			break;
		case 'c':
			cvtflag = 1;
			break;
		case 'd':
			dflag = 1;
			break;
		case 'f':
			inputdev = optarg;
			break;
		case 'h':
			hflag = 0;
			break;
		case 'i':
		case 'R':
		case 'r':
		case 't':
		case 'x':
			if (command != '\0')
				errx(1,
				    "%c and %c options are mutually exclusive",
				    ch, command);
			command = ch;
			break;
		case 'm':
			mflag = 0;
			break;
		case 'N':
			Nflag = 1;
			break;
		case 's':
			/* Dumpnum (skip to) for multifile dump tapes. */
			dumpnum = strtol(optarg, &p, 10);
			if (*p)
				errx(1, "illegal dump number -- %s", optarg);
			if (dumpnum <= 0)
				errx(1, "dump number must be greater than 0");
			break;
		case 'v':
			vflag = 1;
			break;
		case 'y':
			yflag = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (command == '\0')
		errx(1, "none of i, R, r, t or x options specified");

	if (signal(SIGINT, onintr) == SIG_IGN)
		(void)signal(SIGINT, SIG_IGN);
	if (signal(SIGTERM, onintr) == SIG_IGN)
		(void)signal(SIGTERM, SIG_IGN);
	setvbuf(stderr, NULL, _IOLBF, 0);

	atexit(cleanup);

	setinput(inputdev);

	if (argc == 0) {
		argc = 1;
		*--argv = ".";
	}

	switch (command) {
	/*
	 * Interactive mode.
	 */
	case 'i':
		setup();
		extractdirs(1);
		initsymtable(NULL);
		runcmdshell();
		break;
	/*
	 * Incremental restoration of a file system.
	 */
	case 'r':
		setup();
		if (dumptime > 0) {
			/*
			 * This is an incremental dump tape.
			 */
			Vprintf(stdout, "Begin incremental restore\n");
			initsymtable(symtbl);
			extractdirs(1);
			removeoldleaves();
			Vprintf(stdout, "Calculate node updates.\n");
			treescan(".", ROOTINO, nodeupdates);
			findunreflinks();
			removeoldnodes();
		} else {
			/*
			 * This is a level zero dump tape.
			 */
			Vprintf(stdout, "Begin level 0 restore\n");
			initsymtable(NULL);
			extractdirs(1);
			Vprintf(stdout, "Calculate extraction list.\n");
			treescan(".", ROOTINO, nodeupdates);
		}
		createleaves(symtbl);
		createlinks();
		setdirmodes(FORCE);
		checkrestore();
		if (dflag) {
			Vprintf(stdout, "Verify the directory structure\n");
			treescan(".", ROOTINO, verifyfile);
		}
		dumpsymtable(symtbl, (long)1);
		break;
	/*
	 * Resume an incremental file system restoration.
	 */
	case 'R':
		initsymtable(symtbl);
		skipmaps();
		skipdirs();
		createleaves(symtbl);
		createlinks();
		setdirmodes(FORCE);
		checkrestore();
		dumpsymtable(symtbl, (long)1);
		break;
	/*
	 * List contents of tape.
	 */
	case 't':
		setup();
		extractdirs(0);
		initsymtable(NULL);
		while (argc--) {
			canon(*argv++, name, sizeof name);
			ino = dirlookup(name);
			if (ino == 0)
				continue;
			treescan(name, ino, listfile);
		}
		break;
	/*
	 * Batch extraction of tape contents.
	 */
	case 'x':
		setup();
		extractdirs(1);
		initsymtable(NULL);
		while (argc--) {
			canon(*argv++, name, sizeof name);
			ino = dirlookup(name);
			if (ino == 0)
				continue;
			if (mflag)
				pathcheck(name);
			treescan(name, ino, addfile);
		}
		createfiles();
		createlinks();
		setdirmodes(0);
		if (dflag)
			checkrestore();
		break;
	}
	return (0);
}

static void
usage(void)
{

	(void)fprintf(stderr, "usage: %s [-chimRrtvxy] [-b blocksize] [-f file] [-s fileno] [file ...]\n", __progname);
	exit(1);
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
	size_t len;

	/* Setup. */
	argv = *argvp;
	argc = *argcp;

	/* Return if no arguments or first argument has leading dash. */
	ap = argv[1];
	if (argc == 1 || *ap == '-')
		return;

	/* Allocate space for new arguments. */
	if ((*argvp = nargv = calloc(argc + 1, sizeof(char *))) == NULL ||
	    (p = flagsp = malloc(strlen(ap) + 2)) == NULL)
		err(1, NULL);

	*nargv++ = *argv;
	argv += 2;

	for (flags = 0; *ap; ++ap) {
		switch (*ap) {
		case 'b':
		case 'f':
		case 's':
			if (*argv == NULL) {
				warnx("option requires an argument -- %c", *ap);
				usage();
			}
			len = strlen(*argv) + 2 + 1;
			if ((nargv[0] = malloc(len)) == NULL)
				err(1, NULL);
			nargv[0][0] = '-';
			nargv[0][1] = *ap;
			(void)strlcpy(&nargv[0][2], *argv, len-2);
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
	} else {
		free(flagsp);
	}

	/* Copy remaining arguments. */
	while ((*nargv++ = *argv++))
		continue;

	/* Update argument count. */
	*argcp = nargv - *argvp - 1;
}
