/*	$OpenBSD: iostat.c,v 1.48 2025/05/08 16:44:58 tedu Exp $	*/
/*	$NetBSD: iostat.c,v 1.10 1996/10/25 18:21:58 scottr Exp $	*/

/*
 * Copyright (c) 1996 John M. Vinopal
 * All rights reserved.
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
 *      This product includes software developed for the NetBSD Project
 *      by John M. Vinopal.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1986, 1991, 1993
 *      The Regents of the University of California.  All rights reserved.
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

#include <sys/limits.h>
#include <sys/time.h>
#include <sys/sched.h>

#include <err.h>
#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <kvm.h>

#include "dkstats.h"

/* Defined in dkstats.c */
extern struct _disk cur, last;
extern int	dk_ndrive;

/* Namelist and memory files. */
kvm_t *kd;
char	*nlistf, *memf;

int		hz, reps;
time_t		interval;
static int	todo = 0;

volatile sig_atomic_t wantheader;

#define ISSET(x, a)	((x) & (a))
#define SHOW_CPU	0x0001
#define SHOW_TTY	0x0002
#define SHOW_STATS_1	0x0004
#define SHOW_STATS_2	0x0008
#define SHOW_TOTALS	0x0080

static void cpustats(void);
static void disk_stats(double);
static void disk_stats2(double);
static void sigalarm(int);
static void sigheader(int);
static void header(void);
static void usage(void);
static void display(void);
static void selectdrives(char **);

void dkswap(void);
void dkreadstats(void);
int dkinit(int);

int
main(int argc, char *argv[])
{
	struct itimerval itv;
	const char *errstr;
	sigset_t empty;
	int ch, hdrcnt;

	while ((ch = getopt(argc, argv, "Cc:dDIM:N:Tw:")) != -1)
		switch(ch) {
		case 'c':
			reps = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				errx(1, "repetition count is %s", errstr);
			break;
		case 'C':
			todo |= SHOW_CPU;
			break;
		case 'd':
			todo |= SHOW_STATS_1;
			break;
		case 'D':
			todo |= SHOW_STATS_2;
			break;
		case 'I':
			todo |= SHOW_TOTALS;
			break;
		case 'M':
			memf = optarg;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case 'T':
			todo |= SHOW_TTY;
			break;
		case 'w':
			interval = strtonum(optarg, 1, UINT_MAX, &errstr);
			if (errstr)
				errx(1, "wait is %s", errstr);
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (!ISSET(todo, SHOW_CPU | SHOW_TTY | SHOW_STATS_1 | SHOW_STATS_2))
		todo |= SHOW_CPU | SHOW_TTY | SHOW_STATS_1;

	dkinit(0);

	if (unveil("/", "") == -1)
		err(1, "unveil /");
	if (unveil(NULL, NULL) == -1)
		err(1, "unveil");

	dkreadstats();
	selectdrives(argv);

	/* print a new header on sigcont */
	signal(SIGCONT, sigheader);

	if (interval != 0) {
		if (signal(SIGALRM, sigalarm) == SIG_ERR)
			err(1, "signal");
		sigemptyset(&empty);
		itv.it_value.tv_sec = interval;
		itv.it_value.tv_usec = 0;
		itv.it_interval = itv.it_value;
		if (setitimer(ITIMER_REAL, &itv, NULL) == -1)
			err(1, "setitimer");
	}

	for (hdrcnt = 1;;) {
		if (!--hdrcnt || wantheader) {
			header();
			hdrcnt = 20;
			wantheader = 0;
		}

		if (!ISSET(todo, SHOW_TOTALS))
			dkswap();
		display();

		if (reps >= 0 && --reps <= 0)
			break;
		sigsuspend(&empty);
		dkreadstats();
		if (last.dk_ndrive != cur.dk_ndrive)
			wantheader = 1;
	}
	exit(0);
}

static void
sigalarm(int signo)
{
}

static void
sigheader(int signo)
{
	wantheader = 1;
}

static void
header(void)
{
	int i;
	static int printedheader = 0;

	if (printedheader && !isatty(STDOUT_FILENO))
		return;

	/* Main Headers. */
	if (ISSET(todo, SHOW_TTY)) {
		if (ISSET(todo, SHOW_TOTALS))
			printf("            tty");
		else
			printf("      tty");
	}

	if (ISSET(todo, SHOW_STATS_1))
		for (i = 0; i < dk_ndrive; i++)
			if (cur.dk_select[i]) {
				printf(" %18.18s ", cur.dk_name[i]);
			}
	if (ISSET(todo, SHOW_STATS_2))
		for (i = 0; i < dk_ndrive; i++)
			if (cur.dk_select[i])
				printf(" %17.17s ", cur.dk_name[i]);

	if (ISSET(todo, SHOW_CPU))
		printf("               cpu");
	printf("\n");

	/* Sub-Headers. */
	if (ISSET(todo, SHOW_TTY)) {
		if (ISSET(todo, SHOW_TOTALS))
			printf("   tin     tout");
		else
			printf(" tin tout");
	}

	if (ISSET(todo, SHOW_STATS_1))
		for (i = 0; i < dk_ndrive; i++)
			if (cur.dk_select[i]) {
				if (ISSET(todo, SHOW_TOTALS))
					printf("  KB/t   xfr     MB ");
				else
					printf("  KB/t  t/s    MB/s ");
			}
	if (ISSET(todo, SHOW_STATS_2))
		for (i = 0; i < dk_ndrive; i++)
			if (cur.dk_select[i])
				printf("      KB  xfr time ");

	if (ISSET(todo, SHOW_CPU))
		printf(" us ni sy sp in id");
	printf("\n");
}

static void
disk_stats(double etime)
{
	int dn;
	double atime, mbps;

	for (dn = 0; dn < dk_ndrive; ++dn) {
		if (!cur.dk_select[dn])
			continue;

		/* average Kbytes per transfer. */
		if (cur.dk_rxfer[dn] + cur.dk_wxfer[dn])
			mbps = ((cur.dk_rbytes[dn] + cur.dk_wbytes[dn]) /
			    (1024.0)) / (cur.dk_rxfer[dn] + cur.dk_wxfer[dn]);
		else
			mbps = 0.0;

		printf(" %5.2f", mbps);

		/* average transfers per second. */
		if (ISSET(todo, SHOW_TOTALS))
			printf(" %5.0f", (cur.dk_rxfer[dn] + cur.dk_wxfer[dn]) / etime);
		else
			printf(" %4.0f", (cur.dk_rxfer[dn] + cur.dk_wxfer[dn]) / etime);

		/* time busy in disk activity */
		atime = (double)cur.dk_time[dn].tv_sec +
			((double)cur.dk_time[dn].tv_usec / (double)1000000);

		/* Megabytes per second. */
		if (atime != 0.0)
			mbps = (cur.dk_rbytes[dn] + cur.dk_wbytes[dn]) /
			    (double)(1024 * 1024);
		else
			mbps = 0;
		if (ISSET(todo, SHOW_TOTALS))
			printf(" %6.2f ", mbps / etime);
		else
			printf(" %7.2f ", mbps / etime);
	}
}

static void
disk_stats2(double etime)
{
	int dn;
	double atime;

	for (dn = 0; dn < dk_ndrive; ++dn) {
		if (!cur.dk_select[dn])
			continue;

		/* average kbytes per second. */
		printf(" %7.0f",
		    (cur.dk_rbytes[dn] + cur.dk_wbytes[dn]) / (1024.0) / etime);

		/* average transfers per second. */
		printf(" %4.0f", (cur.dk_rxfer[dn] + cur.dk_wxfer[dn]) / etime);

		/* average time busy in disk activity. */
		atime = (double)cur.dk_time[dn].tv_sec +
		    ((double)cur.dk_time[dn].tv_usec / (double)1000000);
		printf(" %4.2f ", atime / etime);
	}
}

static void
cpustats(void)
{
	int state;
	double t = 0;

	for (state = 0; state < CPUSTATES; ++state)
		t += cur.cp_time[state];
	if (!t)
		t = 1.0;
	/* Cap states at 99 to preserve column width and separation. */
	for (state = 0; state < CPUSTATES; ++state) {
		double v = 100. * cur.cp_time[state] / t;
		if (v > 99)
			v = 99;
		printf(" %2.0f", v);
	}
}

static void
usage(void)
{
	fprintf(stderr,
"usage: iostat [-CDdIT] [-c count] [-M core] [-N system] [-w wait] [drives]\n");
	exit(1);
}

static void
display(void)
{
	int	i;
	double	etime;

	/* Sum up the elapsed ticks. */
	etime = 0.0;
	for (i = 0; i < CPUSTATES; i++)
		etime += cur.cp_time[i];
	if (etime == 0.0)
		etime = 1.0;
	/* Convert to seconds. */
	etime /= (float)hz;

	/* If we're showing totals only, then don't divide by the
	 * system time.
	 */
	if (ISSET(todo, SHOW_TOTALS))
		etime = 1.0;

	if (ISSET(todo, SHOW_TTY)) {
		if (ISSET(todo, SHOW_TOTALS))
			printf("%6.0f %8.0f", cur.tk_nin / etime,
			    cur.tk_nout / etime);
		else
			printf("%4.0f %4.0f", cur.tk_nin / etime,
			    cur.tk_nout / etime);
	}

	if (ISSET(todo, SHOW_STATS_1))
		disk_stats(etime);

	if (ISSET(todo, SHOW_STATS_2))
		disk_stats2(etime);

	if (ISSET(todo, SHOW_CPU))
		cpustats();

	printf("\n");
	fflush(stdout);
}

static void
selectdrives(char *argv[])
{
	const char *errstr;
	int	i, ndrives;

	/*
	 * Choose drives to be displayed.  Priority goes to (in order) drives
	 * supplied as arguments and default drives.  If everything isn't
	 * filled in and there are drives not taken care of, display the first
	 * few that fit.
	 *
	 * The backward compatibility syntax is:
	 *	iostat [ drives ] [ interval [ count ] ]
	 */
	for (ndrives = 0; *argv; ++argv) {
		if (isdigit((unsigned char)**argv))
			break;
		for (i = 0; i < dk_ndrive; i++) {
			if (strcmp(cur.dk_name[i], *argv))
				continue;
			cur.dk_select[i] = 1;
			++ndrives;
			break;
		}
		if (i == dk_ndrive)
			errx(1, "invalid interval or drive name: %s", *argv);
	}
	if (*argv) {
		interval = strtonum(*argv, 1, UINT_MAX, &errstr);
		if (errstr)
			errx(1, "interval is %s", errstr);
		if (*++argv) {
			reps = strtonum(*argv, 1, INT_MAX, &errstr);
			if (errstr)
				errx(1, "repetition count is %s", errstr);
			++argv;
		}
	}
	if (*argv)
		errx(1, "too many arguments");

	if (interval) {
		if (!reps)
			reps = -1;
	} else
		if (reps)
			interval = 1;

	/* Pick up to 4 drives if none specified. */
	if (ndrives == 0)
		for (i = 0; i < dk_ndrive && ndrives < 4; i++) {
			if (cur.dk_select[i])
				continue;
			cur.dk_select[i] = 1;
			++ndrives;
		}
}
