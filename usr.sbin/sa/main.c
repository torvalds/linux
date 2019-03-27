/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994 Christopher G. Demetriou
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
 *      This product includes software developed by Christopher G. Demetriou.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1994 Christopher G. Demetriou\n\
 All rights reserved.\n";
#endif
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * sa:	system accounting
 */

#include <sys/types.h>
#include <sys/acct.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "extern.h"
#include "pathnames.h"

static FILE	*acct_load(const char *, int);
static int	 cmp_comm(const char *, const char *);
static int	 cmp_usrsys(const DBT *, const DBT *);
static int	 cmp_avgusrsys(const DBT *, const DBT *);
static int	 cmp_dkio(const DBT *, const DBT *);
static int	 cmp_avgdkio(const DBT *, const DBT *);
static int	 cmp_cpumem(const DBT *, const DBT *);
static int	 cmp_avgcpumem(const DBT *, const DBT *);
static int	 cmp_calls(const DBT *, const DBT *);
static void	 usage(void);

int aflag, bflag, cflag, dflag, Dflag, fflag, iflag, jflag, kflag;
int Kflag, lflag, mflag, qflag, rflag, sflag, tflag, uflag, vflag;
u_quad_t cutoff = 1;
const char *pdb_file = _PATH_SAVACCT;
const char *usrdb_file = _PATH_USRACCT;

static char	*dfltargv[] = { NULL };
static int	dfltargc = (sizeof dfltargv/sizeof(char *));

/* default to comparing by sum of user + system time */
cmpf_t   sa_cmp = cmp_usrsys;

int
main(int argc, char **argv)
{
	FILE *f;
	char pathacct[] = _PATH_ACCT;
	int ch, error = 0;

	dfltargv[0] = pathacct;

	while ((ch = getopt(argc, argv, "abcdDfijkKlmnP:qrstuU:v:")) != -1)
		switch (ch) {
			case 'a':
				/* print all commands */
				aflag = 1;
				break;
			case 'b':
				/* sort by per-call user/system time average */
				bflag = 1;
				sa_cmp = cmp_avgusrsys;
				break;
			case 'c':
				/* print percentage total time */
				cflag = 1;
				break;
			case 'd':
				/* sort by averge number of disk I/O ops */
				dflag = 1;
				sa_cmp = cmp_avgdkio;
				break;
			case 'D':
				/* print and sort by total disk I/O ops */
				Dflag = 1;
				sa_cmp = cmp_dkio;
				break;
			case 'f':
				/* force no interactive threshold comprison */
				fflag = 1;
				break;
			case 'i':
				/* do not read in summary file */
				iflag = 1;
				break;
			case 'j':
				/* instead of total minutes, give sec/call */
				jflag = 1;
				break;
			case 'k':
				/* sort by cpu-time average memory usage */
				kflag = 1;
				sa_cmp = cmp_avgcpumem;
				break;
			case 'K':
				/* print and sort by cpu-storage integral */
				sa_cmp = cmp_cpumem;
				Kflag = 1;
				break;
			case 'l':
				/* separate system and user time */
				lflag = 1;
				break;
			case 'm':
				/* print procs and time per-user */
				mflag = 1;
				break;
			case 'n':
				/* sort by number of calls */
				sa_cmp = cmp_calls;
				break;
			case 'P':
				/* specify program database summary file */
				pdb_file = optarg;
				break;
			case 'q':
				/* quiet; error messages only */
				qflag = 1;
				break;
			case 'r':
				/* reverse order of sort */
				rflag = 1;
				break;
			case 's':
				/* merge accounting file into summaries */
				sflag = 1;
				break;
			case 't':
				/* report ratio of user and system times */
				tflag = 1;
				break;
			case 'u':
				/* first, print uid and command name */
				uflag = 1;
				break;
			case 'U':
				/* specify user database summary file */
				usrdb_file = optarg;
				break;
			case 'v':
				/* cull junk */
				vflag = 1;
				cutoff = atoi(optarg);
				break;
			case '?':
	                default:
				usage();
		}

	argc -= optind;
	argv += optind;

	/* various argument checking */
	if (fflag && !vflag)
		errx(1, "only one of -f requires -v");
	if (fflag && aflag)
		errx(1, "only one of -a and -v may be specified");
	/* XXX need more argument checking */

	if (!uflag) {
		/* initialize tables */
		if ((sflag || (!mflag && !qflag)) && pacct_init() != 0)
			errx(1, "process accounting initialization failed");
		if ((sflag || (mflag && !qflag)) && usracct_init() != 0)
			errx(1, "user accounting initialization failed");
	}

	if (argc == 0) {
		argc = dfltargc;
		argv = dfltargv;
	}

	/* for each file specified */
	for (; argc > 0; argc--, argv++) {
		/*
		 * load the accounting data from the file.
		 * if it fails, go on to the next file.
		 */
		f = acct_load(argv[0], sflag);
		if (f == NULL)
			continue;

		if (!uflag && sflag) {
#ifndef DEBUG
			sigset_t nmask, omask;
			int unmask = 1;

			/*
			 * block most signals so we aren't interrupted during
			 * the update.
			 */
			if (sigfillset(&nmask) == -1) {
				warn("sigfillset");
				unmask = 0;
				error = 1;
			}
			if (unmask &&
			    (sigprocmask(SIG_BLOCK, &nmask, &omask) == -1)) {
				warn("couldn't set signal mask");
				unmask = 0;
				error = 1;
			}
#endif /* DEBUG */

			/*
			 * truncate the accounting data file ASAP, to avoid
			 * losing data.  don't worry about errors in updating
			 * the saved stats; better to underbill than overbill,
			 * but we want every accounting record intact.
			 */
			if (ftruncate(fileno(f), 0) == -1) {
				warn("couldn't truncate %s", *argv);
				error = 1;
			}

			/*
			 * update saved user and process accounting data.
			 * note errors for later.
			 */
			if (pacct_update() != 0 || usracct_update() != 0)
				error = 1;

#ifndef DEBUG
			/*
			 * restore signals
			 */
			if (unmask &&
			    (sigprocmask(SIG_SETMASK, &omask, NULL) == -1)) {
				warn("couldn't restore signal mask");
				error = 1;
			}
#endif /* DEBUG */
		}

		/*
		 * close the opened accounting file
		 */
		if (fclose(f) == EOF) {
			warn("fclose %s", *argv);
			error = 1;
		}
	}

	if (!uflag && !qflag) {
		/* print any results we may have obtained. */
		if (!mflag)
			pacct_print();
		else
			usracct_print();
	}

	if (!uflag) {
		/* finally, deallocate databases */
		if (sflag || (!mflag && !qflag))
			pacct_destroy();
		if (sflag || (mflag && !qflag))
			usracct_destroy();
	}

	exit(error);
}

static void
usage(void)
{
	(void)fprintf(stderr,
		"usage: sa [-abcdDfijkKlmnqrstu] [-P file] [-U file] [-v cutoff] [file ...]\n");
	exit(1);
}

static FILE *
acct_load(const char *pn, int wr)
{
	struct acctv3 ac;
	struct cmdinfo ci;
	ssize_t rv;
	FILE *f;
	int i;

	/*
	 * open the file
	 */
	f = fopen(pn, wr ? "r+" : "r");
	if (f == NULL) {
		warn("open %s %s", pn, wr ? "for read/write" : "read-only");
		return (NULL);
	}

	/*
	 * read all we can; don't stat and open because more processes
	 * could exit, and we'd miss them
	 */
	while (1) {
		/* get one accounting entry and punt if there's an error */
		rv = readrec_forward(f, &ac);
		if (rv != 1) {
			if (rv == EOF)
				warn("error reading %s", pn);
			break;
		}

		/* decode it */
		ci.ci_calls = 1;
		for (i = 0; i < (int)sizeof ac.ac_comm && ac.ac_comm[i] != '\0';
		    i++) {
			char c = ac.ac_comm[i];

			if (!isascii(c) || iscntrl(c)) {
				ci.ci_comm[i] = '?';
				ci.ci_flags |= CI_UNPRINTABLE;
			} else
				ci.ci_comm[i] = c;
		}
		if (ac.ac_flagx & AFORK)
			ci.ci_comm[i++] = '*';
		ci.ci_comm[i++] = '\0';
		ci.ci_etime = ac.ac_etime;
		ci.ci_utime = ac.ac_utime;
		ci.ci_stime = ac.ac_stime;
		ci.ci_uid = ac.ac_uid;
		ci.ci_mem = ac.ac_mem;
		ci.ci_io = ac.ac_io;

		if (!uflag) {
			/* and enter it into the usracct and pacct databases */
			if (sflag || (!mflag && !qflag))
				pacct_add(&ci);
			if (sflag || (mflag && !qflag))
				usracct_add(&ci);
		} else if (!qflag)
			printf("%6u %12.3lf cpu %12.0lfk mem %12.0lf io %s\n",
			    ci.ci_uid,
			    (ci.ci_utime + ci.ci_stime) / 1000000,
			    ci.ci_mem, ci.ci_io,
			    ci.ci_comm);
	}

	/* Finally, return the file stream for possible truncation. */
	return (f);
}

/* sort commands, doing the right thing in terms of reversals */
static int
cmp_comm(const char *s1, const char *s2)
{
	int rv;

	rv = strcmp(s1, s2);
	if (rv == 0)
		rv = -1;
	return (rflag ? rv : -rv);
}

/* sort by total user and system time */
static int
cmp_usrsys(const DBT *d1, const DBT *d2)
{
	struct cmdinfo c1, c2;
	double t1, t2;

	memcpy(&c1, d1->data, sizeof(c1));
	memcpy(&c2, d2->data, sizeof(c2));

	t1 = c1.ci_utime + c1.ci_stime;
	t2 = c2.ci_utime + c2.ci_stime;

	if (t1 < t2)
		return -1;
	else if (t1 == t2)
		return (cmp_comm(c1.ci_comm, c2.ci_comm));
	else
		return 1;
}

/* sort by average user and system time */
static int
cmp_avgusrsys(const DBT *d1, const DBT *d2)
{
	struct cmdinfo c1, c2;
	double t1, t2;

	memcpy(&c1, d1->data, sizeof(c1));
	memcpy(&c2, d2->data, sizeof(c2));

	t1 = c1.ci_utime + c1.ci_stime;
	t1 /= (double) (c1.ci_calls ? c1.ci_calls : 1);

	t2 = c2.ci_utime + c2.ci_stime;
	t2 /= (double) (c2.ci_calls ? c2.ci_calls : 1);

	if (t1 < t2)
		return -1;
	else if (t1 == t2)
		return (cmp_comm(c1.ci_comm, c2.ci_comm));
	else
		return 1;
}

/* sort by total number of disk I/O operations */
static int
cmp_dkio(const DBT *d1, const DBT *d2)
{
	struct cmdinfo c1, c2;

	memcpy(&c1, d1->data, sizeof(c1));
	memcpy(&c2, d2->data, sizeof(c2));

	if (c1.ci_io < c2.ci_io)
		return -1;
	else if (c1.ci_io == c2.ci_io)
		return (cmp_comm(c1.ci_comm, c2.ci_comm));
	else
		return 1;
}

/* sort by average number of disk I/O operations */
static int
cmp_avgdkio(const DBT *d1, const DBT *d2)
{
	struct cmdinfo c1, c2;
	double n1, n2;

	memcpy(&c1, d1->data, sizeof(c1));
	memcpy(&c2, d2->data, sizeof(c2));

	n1 = c1.ci_io / (double) (c1.ci_calls ? c1.ci_calls : 1);
	n2 = c2.ci_io / (double) (c2.ci_calls ? c2.ci_calls : 1);

	if (n1 < n2)
		return -1;
	else if (n1 == n2)
		return (cmp_comm(c1.ci_comm, c2.ci_comm));
	else
		return 1;
}

/* sort by the cpu-storage integral */
static int
cmp_cpumem(const DBT *d1, const DBT *d2)
{
	struct cmdinfo c1, c2;

	memcpy(&c1, d1->data, sizeof(c1));
	memcpy(&c2, d2->data, sizeof(c2));

	if (c1.ci_mem < c2.ci_mem)
		return -1;
	else if (c1.ci_mem == c2.ci_mem)
		return (cmp_comm(c1.ci_comm, c2.ci_comm));
	else
		return 1;
}

/* sort by the cpu-time average memory usage */
static int
cmp_avgcpumem(const DBT *d1, const DBT *d2)
{
	struct cmdinfo c1, c2;
	double t1, t2;
	double n1, n2;

	memcpy(&c1, d1->data, sizeof(c1));
	memcpy(&c2, d2->data, sizeof(c2));

	t1 = c1.ci_utime + c1.ci_stime;
	t2 = c2.ci_utime + c2.ci_stime;

	n1 = c1.ci_mem / (t1 ? t1 : 1);
	n2 = c2.ci_mem / (t2 ? t2 : 1);

	if (n1 < n2)
		return -1;
	else if (n1 == n2)
		return (cmp_comm(c1.ci_comm, c2.ci_comm));
	else
		return 1;
}

/* sort by the number of invocations */
static int
cmp_calls(const DBT *d1, const DBT *d2)
{
	struct cmdinfo c1, c2;

	memcpy(&c1, d1->data, sizeof(c1));
	memcpy(&c2, d2->data, sizeof(c2));

	if (c1.ci_calls < c2.ci_calls)
		return -1;
	else if (c1.ci_calls == c2.ci_calls)
		return (cmp_comm(c1.ci_comm, c2.ci_comm));
	else
		return 1;
}
