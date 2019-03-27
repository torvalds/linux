/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright 1997 Sean Eric Fagan
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Sean Eric Fagan
 * 4. Neither the name of the author may be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * The main module for truss.  Surprisingly simple, but, then, the other
 * files handle the bulk of the work.  And, of course, the kernel has to
 * do a lot of the work :).
 */

#include <sys/ptrace.h>

#include <err.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysdecode.h>
#include <time.h>
#include <unistd.h>

#include "truss.h"
#include "extern.h"
#include "syscall.h"

static void
usage(void)
{
	fprintf(stderr, "%s\n%s\n",
	    "usage: truss [-cfaedDHS] [-o file] [-s strsize] -p pid",
	    "       truss [-cfaedDHS] [-o file] [-s strsize] command [args]");
	exit(1);
}

int
main(int ac, char **av)
{
	struct sigaction sa;
	struct trussinfo *trussinfo;
	char *fname;
	char **command;
	const char *errstr;
	pid_t pid;
	int c;

	fname = NULL;

	/* Initialize the trussinfo struct */
	trussinfo = (struct trussinfo *)calloc(1, sizeof(struct trussinfo));
	if (trussinfo == NULL)
		errx(1, "calloc() failed");

	pid = 0;
	trussinfo->outfile = stderr;
	trussinfo->strsize = 32;
	trussinfo->curthread = NULL;
	LIST_INIT(&trussinfo->proclist);
	init_syscalls();
	while ((c = getopt(ac, av, "p:o:facedDs:SH")) != -1) {
		switch (c) {
		case 'p':	/* specified pid */
			pid = atoi(optarg);
			/* make sure i don't trace me */
			if (pid == getpid()) {
				errx(2, "attempt to grab self.");
			}
			break;
		case 'f': /* Follow fork()'s */
			trussinfo->flags |= FOLLOWFORKS;
			break;
		case 'a': /* Print execve() argument strings. */
			trussinfo->flags |= EXECVEARGS;
			break;
		case 'c': /* Count number of system calls and time. */
			trussinfo->flags |= (COUNTONLY | NOSIGS);
			break;
		case 'e': /* Print execve() environment strings. */
			trussinfo->flags |= EXECVEENVS;
			break;
		case 'd': /* Absolute timestamps */
			trussinfo->flags |= ABSOLUTETIMESTAMPS;
			break;
		case 'D': /* Relative timestamps */
			trussinfo->flags |= RELATIVETIMESTAMPS;
			break;
		case 'o':	/* Specified output file */
			fname = optarg;
			break;
		case 's':	/* Specified string size */
			trussinfo->strsize = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr)
				errx(1, "maximum string size is %s: %s", errstr, optarg);
			break;
		case 'S':	/* Don't trace signals */
			trussinfo->flags |= NOSIGS;
			break;
		case 'H':
			trussinfo->flags |= DISPLAYTIDS;
			break;
		default:
			usage();
		}
	}

	ac -= optind; av += optind;
	if ((pid == 0 && ac == 0) ||
	    (pid != 0 && ac != 0))
		usage();

	if (fname != NULL) { /* Use output file */
		/*
		 * Set close-on-exec ('e'), so that the output file is not
		 * shared with the traced process.
		 */
		if ((trussinfo->outfile = fopen(fname, "we")) == NULL)
			err(1, "cannot open %s", fname);
	}

	/*
	 * If truss starts the process itself, it will ignore some signals --
	 * they should be passed off to the process, which may or may not
	 * exit.  If, however, we are examining an already-running process,
	 * then we restore the event mask on these same signals.
	 */
	if (pid == 0) {
		/* Start a command ourselves */
		command = av;
		setup_and_wait(trussinfo, command);
		signal(SIGINT, SIG_IGN);
		signal(SIGTERM, SIG_IGN);
		signal(SIGQUIT, SIG_IGN);
	} else {
		sa.sa_handler = restore_proc;
		sa.sa_flags = 0;
		sigemptyset(&sa.sa_mask);
		sigaction(SIGINT, &sa, NULL);
		sigaction(SIGQUIT, &sa, NULL);
		sigaction(SIGTERM, &sa, NULL);
		start_tracing(trussinfo, pid);
	}

	/*
	 * At this point, if we started the process, it is stopped waiting to
	 * be woken up, either in exit() or in execve().
	 */
	if (LIST_FIRST(&trussinfo->proclist)->abi == NULL) {
		/*
		 * If we are not able to handle this ABI, detach from the
		 * process and exit.  If we just created a new process to
		 * run a command, kill the new process rather than letting
		 * it run untraced.
		 */
		if (pid == 0)
			kill(LIST_FIRST(&trussinfo->proclist)->pid, SIGKILL);
		ptrace(PT_DETACH, LIST_FIRST(&trussinfo->proclist)->pid, NULL,
		    0);
		return (1);
	}
	ptrace(PT_SYSCALL, LIST_FIRST(&trussinfo->proclist)->pid, (caddr_t)1,
	    0);

	/*
	 * At this point, it's a simple loop, waiting for the process to
	 * stop, finding out why, printing out why, and then continuing it.
	 * All of the grunt work is done in the support routines.
	 */
	clock_gettime(CLOCK_REALTIME, &trussinfo->start_time);

	eventloop(trussinfo);

	if (trussinfo->flags & COUNTONLY)
		print_summary(trussinfo);

	fflush(trussinfo->outfile);

	return (0);
}
