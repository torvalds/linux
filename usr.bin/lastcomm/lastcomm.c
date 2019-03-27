/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1993
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
"@(#) Copyright (c) 1980, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)lastcomm.c	8.1 (Berkeley) 6/6/93";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/acct.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "pathnames.h"

/*XXX*/#include <inttypes.h>

time_t	 expand(u_int);
char	*flagbits(int);
const	 char *getdev(dev_t);
int	 readrec_forward(FILE *f, struct acctv3 *av3);
int	 readrec_backward(FILE *f, struct acctv3 *av3);
int	 requested(char *[], struct acctv3 *);
static	 void usage(void);

#define AC_UTIME 1 /* user */
#define AC_STIME 2 /* system */
#define AC_ETIME 4 /* elapsed */
#define AC_CTIME 8 /* user + system time, default */

#define AC_BTIME 16 /* starting time */
#define AC_FTIME 32 /* exit time (starting time + elapsed time )*/

int
main(int argc, char *argv[])
{
	struct acctv3 ab;
	char *p;
	FILE *fp;
	int (*readrec)(FILE *f, struct acctv3 *av3);
	time_t t;
	int ch, rv;
	const char *acctfile, *format;
	char buf[1024];
	int flags = 0;

	acctfile = _PATH_ACCT;
	format = NULL;
	while ((ch = getopt(argc, argv, "f:usecSE")) != -1)
		switch((char)ch) {
		case 'f':
			acctfile = optarg;
			break;

		case 'u': 
			flags |= AC_UTIME; /* user time */
			break;
		case 's':
			flags |= AC_STIME; /* system time */
			break;
		case 'e':
			flags |= AC_ETIME; /* elapsed time */
			break;
        	case 'c':
                        flags |= AC_CTIME; /* user + system time */
			break;

        	case 'S':
                        flags |= AC_BTIME; /* starting time */
			break;
        	case 'E':
			/* exit time (starting time + elapsed time )*/
                        flags |= AC_FTIME; 
			break;

		case '?':
		default:
			usage();
		}

	/* default user + system time and starting time */
	if (!flags) {
	    flags = AC_CTIME | AC_BTIME;
	}

	argc -= optind;
	argv += optind;

	if (argc > 0 && **argv == '+') {
		format = *argv + 1; /* skip + */
		argc--;
		argv++;
	}

	if (strcmp(acctfile, "-") == 0) {
		fp = stdin;
		readrec = readrec_forward;
	} else {
		/* Open the file. */
		if ((fp = fopen(acctfile, "r")) == NULL)
			err(1, "could not open %s", acctfile);
		if (fseek(fp, 0l, SEEK_END) == -1)
			err(1, "seek to end of %s failed", acctfile);
		readrec = readrec_backward;
	}

	while ((rv = readrec(fp, &ab)) == 1) {
		for (p = &ab.ac_comm[0];
		    p < &ab.ac_comm[AC_COMM_LEN] && *p; ++p)
			if (!isprint(*p))
				*p = '?';

		if (*argv && !requested(argv, &ab))
			continue;

		(void)printf("%-*.*s %-7s %-*s %-8s",
			     AC_COMM_LEN, AC_COMM_LEN, ab.ac_comm,
			     flagbits(ab.ac_flagx),
			     MAXLOGNAME - 1, user_from_uid(ab.ac_uid, 0),
			     getdev(ab.ac_tty));
		
		
		/* user + system time */
		if (flags & AC_CTIME) {
			(void)printf(" %6.3f secs", 
			    (ab.ac_utime + ab.ac_stime) / 1000000);
		}
		
		/* usr time */
		if (flags & AC_UTIME) {
			(void)printf(" %6.3f us", ab.ac_utime / 1000000);
		}
		
		/* system time */
		if (flags & AC_STIME) {
			(void)printf(" %6.3f sy", ab.ac_stime / 1000000);
		}
		
		/* elapsed time */
		if (flags & AC_ETIME) {
			(void)printf(" %8.3f es", ab.ac_etime / 1000000);
		}
		
		/* starting time */
		if (flags & AC_BTIME) {
			if (format != NULL) {
				(void)strftime(buf, sizeof(buf), format,
				    localtime(&ab.ac_btime));
				(void)printf(" %s", buf);
			} else
				(void)printf(" %.16s", ctime(&ab.ac_btime));
		}
		
		/* exit time (starting time + elapsed time )*/
		if (flags & AC_FTIME) {
			t = ab.ac_btime;
			t += (time_t)(ab.ac_etime / 1000000);
			if (format != NULL) {
				(void)strftime(buf, sizeof(buf), format,
				    localtime(&t));
				(void)printf(" %s", buf);
			} else
				(void)printf(" %.16s", ctime(&t));
		}
		printf("\n");
 	}
	if (rv == EOF)
		err(1, "read record from %s failed", acctfile);

	if (fflush(stdout))
		err(1, "stdout");
 	exit(0);
}

char *
flagbits(int f)
{
	static char flags[20] = "-";
	char *p;

#define	BIT(flag, ch)	if (f & flag) *p++ = ch

	p = flags + 1;
	BIT(ASU, 'S');
	BIT(AFORK, 'F');
	BIT(ACOMPAT, 'C');
	BIT(ACORE, 'D');
	BIT(AXSIG, 'X');
	*p = '\0';
	return (flags);
}

int
requested(char *argv[], struct acctv3 *acp)
{
	const char *p;

	do {
		p = user_from_uid(acp->ac_uid, 0);
		if (!strcmp(p, *argv))
			return (1);
		if ((p = getdev(acp->ac_tty)) && !strcmp(p, *argv))
			return (1);
		if (!strncmp(acp->ac_comm, *argv, AC_COMM_LEN))
			return (1);
	} while (*++argv);
	return (0);
}

const char *
getdev(dev_t dev)
{
	static dev_t lastdev = (dev_t)-1;
	static const char *lastname;

	if (dev == NODEV)			/* Special case. */
		return ("__");
	if (dev == lastdev)			/* One-element cache. */
		return (lastname);
	lastdev = dev;
	lastname = devname(dev, S_IFCHR);
	return (lastname);
}

static void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: lastcomm [-EScesu] [-f file] [+format] [command ...] "
	    "[user ...] [terminal ...]\n");
	exit(1);
}
