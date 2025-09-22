/*	$OpenBSD: main.c,v 1.19 2022/12/04 23:50:51 cheloha Exp $	*/
/*
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

/*
 * sa:	system accounting
 */

#include <sys/types.h>
#include <sys/acct.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "extern.h"
#include "pathnames.h"

static int	acct_load(char *, int);
static uint64_t	decode_comp_t(comp_t);
static int	cmp_comm(const char *, const char *);
static int	cmp_usrsys(const DBT *, const DBT *);
static int	cmp_avgusrsys(const DBT *, const DBT *);
static int	cmp_dkio(const DBT *, const DBT *);
static int	cmp_avgdkio(const DBT *, const DBT *);
static int	cmp_cpumem(const DBT *, const DBT *);
static int	cmp_avgcpumem(const DBT *, const DBT *);
static int	cmp_calls(const DBT *, const DBT *);

int aflag, bflag, cflag, dflag, Dflag, fflag, iflag, jflag, kflag;
int Kflag, lflag, mflag, qflag, rflag, sflag, tflag, uflag, vflag;
int cutoff = 1;

static char	*dfltargv[] = { _PATH_ACCT };
static int	dfltargc = (sizeof(dfltargv)/sizeof(char *));

/* default to comparing by sum of user + system time */
cmpf_t   sa_cmp = cmp_usrsys;

int
main(int argc, char **argv)
{
	int ch;
	int error = 0;
	const char *errstr;
	extern char *__progname;

	if (pledge("stdio rpath wpath cpath getpw flock", NULL) == -1)
		err(1, "pledge");

	while ((ch = getopt(argc, argv, "abcdDfijkKlmnqrstuv:")) != -1)
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
		case 'v':
			/* cull junk */
			vflag = 1;
			cutoff = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				errx(1, "-v %s: %s", optarg, errstr);
			break;
		default:
			(void)fprintf(stderr,
			    "usage: %s [-abcDdfijKklmnqrstu] [-v cutoff]"
			    " [file ...]\n", __progname);
			exit(1);
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
		int	fd;

		/*
		 * load the accounting data from the file.
		 * if it fails, go on to the next file.
		 */
		fd = acct_load(argv[0], sflag);
		if (fd < 0)
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
				warn("couldn't set signal mask ");
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
			if (ftruncate(fd, 0) == -1) {
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
		if (close(fd) == -1) {
			warn("close %s", *argv);
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

static int
acct_load(char *pn, int wr)
{
	struct acct ac;
	struct cmdinfo ci;
	ssize_t rv;
	int fd, i;

	/*
	 * open the file
	 */
	fd = open(pn, wr ? O_RDWR : O_RDONLY);
	if (fd == -1) {
		warn("open %s %s", pn, wr ? "for read/write" : "read-only");
		return (-1);
	}

	/*
	 * read all we can; don't stat and open because more processes
	 * could exit, and we'd miss them
	 */
	while (1) {
		/* get one accounting entry and punt if there's an error */
		rv = read(fd, &ac, sizeof(struct acct));
		if (rv == -1)
			warn("error reading %s", pn);
		else if (rv > 0 && rv < sizeof(struct acct))
			warnx("short read of accounting data in %s", pn);
		if (rv != sizeof(struct acct))
			break;

		/* decode it */
		ci.ci_calls = 1;
		for (i = 0; i < sizeof(ac.ac_comm) && ac.ac_comm[i] != '\0';
		    i++) {
			unsigned char c = ac.ac_comm[i];

			if (!isascii(c) || iscntrl(c)) {
				ci.ci_comm[i] = '?';
				ci.ci_flags |= CI_UNPRINTABLE;
			} else
				ci.ci_comm[i] = c;
		}
		if (ac.ac_flag & AFORK)
			ci.ci_comm[i++] = '*';
		ci.ci_comm[i++] = '\0';
		ci.ci_etime = decode_comp_t(ac.ac_etime);
		ci.ci_utime = decode_comp_t(ac.ac_utime);
		ci.ci_stime = decode_comp_t(ac.ac_stime);
		ci.ci_uid = ac.ac_uid;
		ci.ci_mem = ac.ac_mem;
		ci.ci_io = decode_comp_t(ac.ac_io) / AHZ;
		ci.ci_pid = ac.ac_pid;

		if (!uflag) {
			/* and enter it into the usracct and pacct databases */
			if (sflag || (!mflag && !qflag))
				pacct_add(&ci);
			if (sflag || (mflag && !qflag))
				usracct_add(&ci);
		} else if (!qflag)
			printf("%6u %12.2f cpu %12lluk mem %12llu io pid %u %s\n",
			    ci.ci_uid,
			    (ci.ci_utime + ci.ci_stime) / (double) AHZ,
			    ci.ci_mem, ci.ci_io, ci.ci_pid, ci.ci_comm);
	}

	/* finally, return the file descriptor for possible truncation */
	return (fd);
}

static uint64_t
decode_comp_t(comp_t comp)
{
	uint64_t rv;

	/*
	 * for more info on the comp_t format, see:
	 *	/usr/src/sys/kern/kern_acct.c
	 *	/usr/src/sys/sys/acct.h
	 *	/usr/src/usr.bin/lastcomm/lastcomm.c
	 */
	rv = comp & 0x1fff;	/* 13 bit fraction */
	comp >>= 13;		/* 3 bit base-8 exponent */
	while (comp--)
		rv <<= 3;

	return (rv);
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
	uint64_t t1, t2;

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

	n1 = (double) c1.ci_io / (double) (c1.ci_calls ? c1.ci_calls : 1);
	n2 = (double) c2.ci_io / (double) (c2.ci_calls ? c2.ci_calls : 1);

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
	uint64_t t1, t2;
	double n1, n2;

	memcpy(&c1, d1->data, sizeof(c1));
	memcpy(&c2, d2->data, sizeof(c2));

	t1 = c1.ci_utime + c1.ci_stime;
	t2 = c2.ci_utime + c2.ci_stime;

	n1 = (double) c1.ci_mem / (double) (t1 ? t1 : 1);
	n2 = (double) c2.ci_mem / (double) (t2 ? t2 : 1);

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
