/*	$OpenBSD: lpq.c,v 1.25 2022/12/28 21:30:17 jmc Exp $	*/
/*	$NetBSD: lpq.c,v 1.9 1999/12/07 14:54:47 mrg Exp $	*/

/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
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

/*
 * Spool Queue examination program
 *
 * lpq [-a] [-l] [-Pprinter] [user...] [job...]
 *
 * -a show all non-null queues on the local machine
 * -l long output
 * -P used to identify printer as per lpr/lprm
 */


#include <ctype.h>
#include <signal.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <syslog.h>

#include "lp.h"
#include "lp.local.h"
#include "pathnames.h"

int	 requ[MAXREQUESTS];	/* job number of spool entries */
int	 requests;		/* # of spool requests */
char	*user[MAXUSERS];	/* users to process */
int	 users;			/* # of users in user array */

volatile sig_atomic_t gotintr;

static __dead void usage(void);

int
main(int argc, char **argv)
{
	int	ch, aflag, lflag;
	char	*buf, *cp;
	long	l;

	effective_uid = geteuid();
	real_uid = getuid();
	effective_gid = getegid();
	real_gid = getgid();
	PRIV_END;	/* be safe */

	if (gethostname(host, sizeof(host)) != 0)
		err(1, "gethostname");
	openlog("lpq", 0, LOG_LPR);

	aflag = lflag = 0;
	while ((ch = getopt(argc, argv, "alP:w:")) != -1) {
		switch(ch) {
		case 'a':
			aflag = 1;
			break;
		case 'l':			/* long output */
			lflag = 1;
			break;
		case 'P':		/* printer name */
			printer = optarg;
			break;
		case 'w':
			l = strtol(optarg, &cp, 10);
			if (*cp != '\0' || l < 0 || l >= INT_MAX)
				errx(1, "wait time must be positive integer: %s",
				    optarg);
			wait_time = (u_int)l;
			if (wait_time < 30)
				warnx("warning: wait time less than 30 seconds");
			break;
		default:
			usage();
		}
	}

	if (!aflag && printer == NULL && (printer = getenv("PRINTER")) == NULL)
		printer = DEFLP;

	for (argc -= optind, argv += optind; argc; --argc, ++argv)
		if (isdigit((unsigned char)argv[0][0])) {
			if (requests >= MAXREQUESTS)
				fatal("too many requests");
			requ[requests++] = atoi(*argv);
		}
		else {
			if (users >= MAXUSERS)
				fatal("too many users");
			user[users++] = *argv;
		}

	if (aflag) {
		while (cgetnext(&buf, printcapdb) > 0) {
			if (ckqueue(buf) <= 0) {
				free(buf);
				continue;	/* no jobs */
			}
			for (cp = buf; *cp; cp++)
				if (*cp == '|' || *cp == ':') {
					*cp = '\0';
					break;
				}
			printer = buf;
			printf("%s:\n", printer);
			displayq(lflag);
			free(buf);
			printf("\n");
		}
	} else
		displayq(lflag);
	exit(0);
}

static __dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
	    "usage: %s [-al] [-Pprinter] [job# ...] [user ...]\n",
	    __progname);
	exit(1);
}
