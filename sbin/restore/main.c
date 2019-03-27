/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)main.c	8.6 (Berkeley) 5/4/95";
#endif
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>

#include <ufs/ufs/dinode.h>
#include <protocols/dumprestore.h>

#include <err.h>
#include <limits.h>
#include <locale.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "restore.h"
#include "extern.h"

int	bflag = 0, cvtflag = 0, dflag = 0, Dflag = 0, vflag = 0, yflag = 0;
int	hflag = 1, mflag = 1, Nflag = 0;
int	uflag = 0;
int	pipecmd = 0;
char	command = '\0';
long	dumpnum = 1;
long	volno = 0;
long	ntrec;
char	*dumpmap;
char	*usedinomap;
ino_t	maxino;
time_t	dumptime;
time_t	dumpdate;
FILE 	*terminal;

static void obsolete(int *, char **[]);
static void usage(void) __dead2;

int
main(int argc, char *argv[])
{
	int ch;
	ino_t ino;
	char *inputdev;
	char *symtbl = "./restoresymtable";
	char *p, name[MAXPATHLEN];

	/* Temp files should *not* be readable.  We set permissions later. */
	(void) umask(077);

	if (argc < 2)
		usage();

	(void)setlocale(LC_ALL, "");

	inputdev = NULL;
	obsolete(&argc, &argv);
	while ((ch = getopt(argc, argv, "b:dDf:himNP:Rrs:tuvxy")) != -1)
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
		case 'd':
			dflag = 1;
			break;
		case 'D':
			Dflag = 1;
			break;
		case 'f':
			if (pipecmd)
				errx(1,
				    "-P and -f options are mutually exclusive");
			inputdev = optarg;
			break;
		case 'P':
			if (!pipecmd && inputdev)
				errx(1,
				    "-P and -f options are mutually exclusive");
			inputdev = optarg;
			pipecmd = 1;
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
		case 'u':
			uflag = 1;
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
		(void) signal(SIGINT, SIG_IGN);
	if (signal(SIGTERM, onintr) == SIG_IGN)
		(void) signal(SIGTERM, SIG_IGN);
	setlinebuf(stderr);

	if (inputdev == NULL && (inputdev = getenv("TAPE")) == NULL)
		inputdev = _PATH_DEFTAPE;
	setinput(inputdev, pipecmd);

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
			vprintf(stdout, "Begin incremental restore\n");
			initsymtable(symtbl);
			extractdirs(1);
			removeoldleaves();
			vprintf(stdout, "Calculate node updates.\n");
			treescan(".", UFS_ROOTINO, nodeupdates);
			findunreflinks();
			removeoldnodes();
		} else {
			/*
			 * This is a level zero dump tape.
			 */
			vprintf(stdout, "Begin level 0 restore\n");
			initsymtable((char *)0);
			extractdirs(1);
			vprintf(stdout, "Calculate extraction list.\n");
			treescan(".", UFS_ROOTINO, nodeupdates);
		}
		createleaves(symtbl);
		createlinks();
		setdirmodes(FORCE);
		checkrestore();
		if (dflag) {
			vprintf(stdout, "Verify the directory structure\n");
			treescan(".", UFS_ROOTINO, verifyfile);
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
		initsymtable((char *)0);
		while (argc--) {
			canon(*argv++, name, sizeof(name));
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
		initsymtable((char *)0);
		while (argc--) {
			canon(*argv++, name, sizeof(name));
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
	done(0);
	/* NOTREACHED */
}

static void
usage()
{
	const char *const common =
	    "[-b blocksize] [-f file | -P pipecommand] [-s fileno]";
	const char *const fileell = "[file ...]";

	(void)fprintf(stderr, "usage:\t%s %s\n\t%s %s\n\t%s %s\n"
	    "\t%s %s %s\n\t%s %s %s\n",
	    "restore -i [-dhmNuvy]", common,
	    "restore -R [-dNuvy]", common,
	    "restore -r [-dNuvy]", common,
	    "restore -t [-dhNuvy]", common, fileell,
	    "restore -x [-dhmNuvy]", common, fileell);
	done(1);
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

	/* Return if no arguments or first argument has leading dash. */
	ap = argv[1];
	if (argc == 1 || *ap == '-')
		return;

	/* Allocate space for new arguments. */
	if ((*argvp = nargv = malloc((argc + 1) * sizeof(char *))) == NULL ||
	    (p = flagsp = malloc(strlen(ap) + 2)) == NULL)
		err(1, NULL);

	*nargv++ = *argv;
	argv += 2, argc -= 2;

	for (flags = 0; *ap; ++ap) {
		switch (*ap) {
		case 'b':
		case 'f':
		case 's':
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
