/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1992, 1993
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

#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");

#ifdef lint
static const char sccsid[] = "@(#)main.c	8.1 (Berkeley) 6/6/93";
#endif

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1992, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#include <sys/param.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/queue.h>

#include <err.h>
#include <limits.h>
#include <locale.h>
#include <nlist.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "systat.h"
#include "extern.h"

static int     dellave;

kvm_t *kd;
sig_t	sigtstpdfl;
double avenrun[3];
int     col;
unsigned int	delay = 5000000;	/* in microseconds */
int     verbose = 1;                    /* to report kvm read errs */
struct	clockinfo clkinfo;
double	hertz;
char    c;
char    *namp;
char    hostname[MAXHOSTNAMELEN];
WINDOW  *wnd;
int     CMDLINE;
int     use_kvm = 1;

static	WINDOW *wload;			/* one line window for load average */

struct cmdentry {
	SLIST_ENTRY(cmdentry) link;
	char		*cmd;		/* Command name	*/
	char		*argv;		/* Arguments vector for a command */
};
SLIST_HEAD(, cmdentry) commands;

static void
parse_cmd_args (int argc, char **argv)
{
	int in_command = 0;
	struct cmdentry *cmd = NULL;
	double t;

	while (argc) {
		if (argv[0][0] == '-') {
			if (in_command)
					SLIST_INSERT_HEAD(&commands, cmd, link);

			if (memcmp(argv[0], "--", 3) == 0) {
				in_command = 0; /*-- ends a command explicitly*/
				argc --, argv ++;
				continue;
			}
			cmd = calloc(1, sizeof(struct cmdentry));
			if (cmd == NULL)
				errx(1, "memory allocating failure");
			cmd->cmd = strdup(&argv[0][1]);
			if (cmd->cmd == NULL)
				errx(1, "memory allocating failure");
			in_command = 1;
		}
		else if (!in_command) {
			t = strtod(argv[0], NULL) * 1000000.0;
			if (t > 0 && t < (double)UINT_MAX)
				delay = (unsigned int)t;
		}
		else if (cmd != NULL) {
			cmd->argv = strdup(argv[0]);
			if (cmd->argv == NULL)
				errx(1, "memory allocating failure");
			in_command = 0;
			SLIST_INSERT_HEAD(&commands, cmd, link);
		}
		else
			errx(1, "invalid arguments list");

		argc--, argv++;
	}
	if (in_command && cmd != NULL)
		SLIST_INSERT_HEAD(&commands, cmd, link);

}

int
main(int argc, char **argv)
{
	char errbuf[_POSIX2_LINE_MAX], dummy;
	size_t	size;
	struct cmdentry *cmd = NULL;

	(void) setlocale(LC_ALL, "");

	SLIST_INIT(&commands);
	argc--, argv++;
	if (argc > 0) {
		if (argv[0][0] == '-') {
			struct cmdtab *p;

			p = lookup(&argv[0][1]);
			if (p == (struct cmdtab *)-1)
				errx(1, "%s: ambiguous request", &argv[0][1]);
			if (p == (struct cmdtab *)0)
				errx(1, "%s: unknown request", &argv[0][1]);
			curcmd = p;
			argc--, argv++;
		}
		parse_cmd_args (argc, argv);
		
	}
	kd = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, errbuf);
	if (kd != NULL) {
		/*
		 * Try to actually read something, we may be in a jail, and
		 * have /dev/null opened as /dev/mem.
		 */
		if (kvm_nlist(kd, namelist) != 0 || namelist[0].n_value == 0 ||
		    kvm_read(kd, namelist[0].n_value, &dummy, sizeof(dummy)) !=
		    sizeof(dummy)) {
			kvm_close(kd);
			kd = NULL;
		}
	}
	if (kd == NULL) {
		/*
		 * Maybe we are lacking permissions? Retry, this time with bogus
		 * devices. We can now use sysctl only.
		 */
		use_kvm = 0;
		kd = kvm_openfiles(_PATH_DEVNULL, _PATH_DEVNULL, _PATH_DEVNULL,
		    O_RDONLY, errbuf);
		if (kd == NULL) {
			error("%s", errbuf);
			exit(1);
		}
	}
	signal(SIGHUP, die);
	signal(SIGINT, die);
	signal(SIGQUIT, die);
	signal(SIGTERM, die);

	/*
	 * Initialize display.  Load average appears in a one line
	 * window of its own.  Current command's display appears in
	 * an overlapping sub-window of stdscr configured by the display
	 * routines to minimize update work by curses.
	 */
	initscr();
	CMDLINE = LINES - 1;
	wnd = (*curcmd->c_open)();
	if (wnd == NULL) {
		warnx("couldn't initialize display");
		die(0);
	}
	wload = newwin(1, 0, 1, 20);
	if (wload == NULL) {
		warnx("couldn't set up load average window");
		die(0);
	}
	gethostname(hostname, sizeof (hostname));
	size = sizeof(clkinfo);
	if (sysctlbyname("kern.clockrate", &clkinfo, &size, NULL, 0)
	    || size != sizeof(clkinfo)) {
		error("kern.clockrate");
		die(0);
	}
	hertz = clkinfo.stathz;
	(*curcmd->c_init)();
	curcmd->c_flags |= CF_INIT;
	labels();

	if (curcmd->c_cmd != NULL)
		SLIST_FOREACH (cmd, &commands, link)
			if (!curcmd->c_cmd(cmd->cmd, cmd->argv))
				warnx("command is not understood");

	dellave = 0.0;
	display();
	noecho();
	crmode();
	keyboard();
	/*NOTREACHED*/

	return EXIT_SUCCESS;
}

void
labels(void)
{
	if (curcmd->c_flags & CF_LOADAV) {
		mvaddstr(0, 20,
		    "/0   /1   /2   /3   /4   /5   /6   /7   /8   /9   /10");
		mvaddstr(1, 5, "Load Average");
	}
	if (curcmd->c_flags & CF_ZFSARC) {
		mvaddstr(0, 20,
		    "   Total     MFU     MRU    Anon     Hdr   L2Hdr   Other");
		mvaddstr(1, 5, "ZFS ARC     ");
	}
	(*curcmd->c_label)();
#ifdef notdef
	mvprintw(21, 25, "CPU usage on %s", hostname);
#endif
	refresh();
}

void
display(void)
{
	uint64_t arc_stat;
	int i, j;

	/* Get the load average over the last minute. */
	(void) getloadavg(avenrun, nitems(avenrun));
	(*curcmd->c_fetch)();
	if (curcmd->c_flags & CF_LOADAV) {
		j = 5.0*avenrun[0] + 0.5;
		dellave -= avenrun[0];
		if (dellave >= 0.0)
			c = '<';
		else {
			c = '>';
			dellave = -dellave;
		}
		if (dellave < 0.1)
			c = '|';
		dellave = avenrun[0];
		wmove(wload, 0, 0); wclrtoeol(wload);
		for (i = MIN(j, 50); i > 0; i--)
			waddch(wload, c);
		if (j > 50)
			wprintw(wload, " %4.1f", avenrun[0]);
	}
	if (curcmd->c_flags & CF_ZFSARC) {
	    uint64_t arc[7] = {};
	    size_t size = sizeof(arc[0]);
	    if (sysctlbyname("kstat.zfs.misc.arcstats.size",
		&arc[0], &size, NULL, 0) == 0 ) {
		    GETSYSCTL("vfs.zfs.mfu_size", arc[1]);
		    GETSYSCTL("vfs.zfs.mru_size", arc[2]);
		    GETSYSCTL("vfs.zfs.anon_size", arc[3]);
		    GETSYSCTL("kstat.zfs.misc.arcstats.hdr_size", arc[4]);
		    GETSYSCTL("kstat.zfs.misc.arcstats.l2_hdr_size", arc[5]);
		    GETSYSCTL("kstat.zfs.misc.arcstats.bonus_size", arc[6]);
		    GETSYSCTL("kstat.zfs.misc.arcstats.dnode_size", arc_stat);
		    arc[6] += arc_stat;
		    GETSYSCTL("kstat.zfs.misc.arcstats.dbuf_size", arc_stat);
		    arc[6] += arc_stat;
		    wmove(wload, 0, 0); wclrtoeol(wload);
		    for (i = 0 ; i < nitems(arc); i++) {
			if (arc[i] > 10llu * 1024 * 1024 * 1024 ) {
				wprintw(wload, "%7lluG", arc[i] >> 30);
			}
			else if (arc[i] > 10 * 1024 * 1024 ) {
				wprintw(wload, "%7lluM", arc[i] >> 20);
			}
			else {
				wprintw(wload, "%7lluK", arc[i] >> 10);
			}
		    }
	    }
	}
	(*curcmd->c_refresh)();
	if (curcmd->c_flags & (CF_LOADAV |CF_ZFSARC))
		wrefresh(wload);
	wrefresh(wnd);
	move(CMDLINE, col);
	refresh();
}

void
load(void)
{

	(void) getloadavg(avenrun, nitems(avenrun));
	mvprintw(CMDLINE, 0, "%4.1f %4.1f %4.1f",
	    avenrun[0], avenrun[1], avenrun[2]);
	clrtoeol();
}

void
die(int signo __unused)
{
	move(CMDLINE, 0);
	clrtoeol();
	refresh();
	endwin();
	exit(0);
}

#include <stdarg.h>

void
error(const char *fmt, ...)
{
	va_list ap;
	char buf[255];
	int oy, ox;

	va_start(ap, fmt);
	if (wnd) {
		getyx(stdscr, oy, ox);
		(void) vsnprintf(buf, sizeof(buf), fmt, ap);
		clrtoeol();
		standout();
		mvaddstr(CMDLINE, 0, buf);
		standend();
		move(oy, ox);
		refresh();
	} else {
		(void) vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
	}
	va_end(ap);
}

void
nlisterr(struct nlist n_list[])
{
	int i, n;

	n = 0;
	clear();
	mvprintw(2, 10, "systat: nlist: can't find following symbols:");
	for (i = 0;
	    n_list[i].n_name != NULL && *n_list[i].n_name != '\0'; i++)
		if (n_list[i].n_value == 0)
			mvprintw(2 + ++n, 10, "%s", n_list[i].n_name);
	move(CMDLINE, 0);
	clrtoeol();
	refresh();
	endwin();
	exit(1);
}
