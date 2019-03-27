/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1993
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
"@(#) Copyright (c) 1988, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)ktrace.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/ktrace.h>

#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ktrace.h"

static char def_tracefile[] = DEF_TRACEFILE;

static enum clear { NOTSET, CLEAR, CLEARALL } clear = NOTSET;
static int pid;

static void no_ktrace(int);
static void set_pid_clear(const char *, enum clear);
static void usage(void);

int
main(int argc, char *argv[])
{
	int append, ch, fd, inherit, ops, trpoints;
	const char *tracefile;
	mode_t omask;
	struct stat sb;

	append = ops = inherit = 0;
	trpoints = DEF_POINTS;
	tracefile = def_tracefile;
	while ((ch = getopt(argc,argv,"aCcdf:g:ip:t:")) != -1)
		switch((char)ch) {
		case 'a':
			append = 1;
			break;
		case 'C':
			set_pid_clear("1", CLEARALL);
			break;
		case 'c':
			set_pid_clear(NULL, CLEAR);
			break;
		case 'd':
			ops |= KTRFLAG_DESCEND;
			break;
		case 'f':
			tracefile = optarg;
			break;
		case 'g':
			set_pid_clear(optarg, NOTSET);
			pid = -pid;
			break;
		case 'i':
			inherit = 1;
			break;
		case 'p':
			set_pid_clear(optarg, NOTSET);
			break;
		case 't':
			trpoints = getpoints(optarg);
			if (trpoints < 0) {
				warnx("unknown facility in %s", optarg);
				usage();
			}
			break;
		default:
			usage();
		}

	argv += optind;
	argc -= optind;

	/* must have either -[Cc], a pid or a command */
	if (clear == NOTSET && pid == 0 && argc == 0)
		usage();
	/* can't have both a pid and a command */
	/* (note that -C sets pid to 1) */
	if (pid != 0 && argc > 0) {
		usage();
	}

	if (inherit)
		trpoints |= KTRFAC_INHERIT;

	(void)signal(SIGSYS, no_ktrace);
	if (clear != NOTSET) {
		if (clear == CLEARALL) {
			ops = KTROP_CLEAR | KTRFLAG_DESCEND;
			trpoints = ALL_POINTS;
		} else {
			ops |= pid ? KTROP_CLEAR : KTROP_CLEARFILE;
		}
		if (ktrace(tracefile, ops, trpoints, pid) < 0)
			err(1, "%s", tracefile);
		exit(0);
	}

	omask = umask(S_IRWXG|S_IRWXO);
	if (append) {
		if ((fd = open(tracefile, O_CREAT | O_WRONLY | O_NONBLOCK,
		    DEFFILEMODE)) < 0)
			err(1, "%s", tracefile);
		if (fstat(fd, &sb) != 0 || sb.st_uid != getuid())
			errx(1, "refuse to append to %s not owned by you",
			    tracefile);
		if (!(S_ISREG(sb.st_mode)))
			errx(1, "%s not regular file", tracefile);
	} else {
		if (unlink(tracefile) == -1 && errno != ENOENT)
			err(1, "unlink %s", tracefile);
		if ((fd = open(tracefile, O_CREAT | O_EXCL | O_WRONLY,
		    DEFFILEMODE)) < 0)
			err(1, "%s", tracefile);
	}
	(void)umask(omask);
	(void)close(fd);

	trpoints |= PROC_ABI_POINTS;

	if (argc > 0) { 
		if (ktrace(tracefile, ops, trpoints, getpid()) < 0)
			err(1, "%s", tracefile);
		execvp(*argv, argv);
		err(1, "exec of '%s' failed", *argv);
	}
	if (ktrace(tracefile, ops, trpoints, pid) < 0)
		err(1, "%s", tracefile);
	exit(0);
}

static void
set_pid_clear(const char *p, enum clear cl)
{
	intmax_t n;
	char *e;

	if (clear != NOTSET && cl != NOTSET) {
		/* either -c and -C or either of them twice */
		warnx("only one -c or -C flag is permitted");
		usage();
	}
	if ((clear == CLEARALL && p != NULL) || (cl == CLEARALL && pid != 0)) {
		/* both -C and a pid or pgid */
		warnx("the -C flag may not be combined with -g or -p");
		usage();
	}
	if (p != NULL && pid != 0) {
		/* either -p and -g or either of them twice */
		warnx("only one -g or -p flag is permitted");
		usage();
	}
	if (p != NULL) {
		errno = 0;
		n = strtoimax(p, &e, 10);
		/*
		 * 1) not a number, or outside the range of an intmax_t
		 * 2) inside the range of intmax_t but outside the range
		 *    of an int, keeping in mind that the pid may be
		 *    negated if it's actually a pgid.
		 */
		if (*e != '\0' || n < 1 || errno == ERANGE ||
		    n > (intmax_t)INT_MAX || n > -(intmax_t)INT_MIN) {
			warnx("invalid process or group id");
			usage();
		}
		pid = n;
	}
	if (cl != NOTSET)
		if ((clear = cl) == CLEARALL)
			pid = 1;
}

static void
usage(void)
{

	fprintf(stderr, "%s\n%s\n",
	    "usage: ktrace [-aCcdi] [-f trfile] [-g pgrp | -p pid] [-t trstr]",
	    "       ktrace [-adi] [-f trfile] [-t trstr] command");
	exit(1);
}

static void
no_ktrace(int sig __unused)
{

	fprintf(stderr, "error:\t%s\n\t%s\n",
	    "ktrace() system call not supported in the running kernel",
	    "re-compile kernel with 'options KTRACE'");
        exit(1);
}
