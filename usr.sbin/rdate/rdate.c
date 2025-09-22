/*	$OpenBSD: rdate.c,v 1.37 2023/01/04 13:00:11 jsg Exp $	*/
/*	$NetBSD: rdate.c,v 1.4 1996/03/16 12:37:45 pk Exp $	*/

/*
 * Copyright (c) 1994 Christos Zoulas
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
 * rdate.c: Set the date from the specified host
 *
 *	Time is returned as the number of seconds since
 *	midnight January 1st 1900.
 */

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <err.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* there are systems without libutil; for portability */
#ifndef NO_UTIL
#include <util.h>
#else
#define logwtmp(a,b,c)
#endif

void rfc868time_client(const char *, int, struct timeval *, struct timeval *, int);
void ntp_client(const char *, int, struct timeval *, struct timeval *, int);

extern char    *__progname;
__dead void	usage(void);

struct {
	char message[2048];
	struct timeval new;
	struct timeval adjust;
} pdata;

__dead void
usage(void)
{
	(void) fprintf(stderr, "usage: %s [-46acnopsv] host\n", __progname);
	exit(1);
}

int
main(int argc, char **argv)
{
	int             pr = 0, silent = 0, ntp = 1, verbose = 0;
	int		slidetime = 0, corrleaps = 0;
	char           *hname;
	int             c, p[2], pid;
	int		family = PF_UNSPEC;

	while ((c = getopt(argc, argv, "46psanocv")) != -1) {
		switch (c) {
		case '4':
			family = PF_INET;
			break;

		case '6':
			family = PF_INET6;
			break;

		case 'p':
			pr = 1;
			break;

		case 's':
			silent = 1;
			break;

		case 'a':
			slidetime = 1;
			break;

		case 'n':
			ntp = 1;
			break;

		case 'o':
			ntp = 0;
			break;

		case 'c':
			corrleaps = 1;
			break;

		case 'v':
			verbose = 1;
			break;

		default:
			usage();
		}
	}
	if (argc - 1 != optind)
		usage();
	hname = argv[optind];

	/*
	 * Privilege separation increases safety, with a slight reduction
	 * in precision because the time values have to return over a pipe.
	 */
	if (pipe(p) == -1)
		err(1, "pipe");
	switch ((pid = fork())) {
	case -1:
		err(1, "fork");
		break;
	case 0:
		if (pledge("stdio inet dns", NULL) == -1)
			err(1, "pledge");

		close(p[0]);	/* read side of pipe */
		dup2(p[1], STDIN_FILENO);
		if (p[1] != STDIN_FILENO)
			close(p[1]);
		dup2(STDIN_FILENO, STDOUT_FILENO);
		dup2(STDOUT_FILENO, STDERR_FILENO);
		setvbuf(stdout, NULL, _IOFBF, 0);
		setvbuf(stderr, NULL, _IOFBF, 0);

		if (ntp)
			ntp_client(hname, family, &pdata.new,
			    &pdata.adjust, corrleaps);
		else
			rfc868time_client(hname, family, &pdata.new,
			    &pdata.adjust, corrleaps);

		if (write(STDOUT_FILENO, &pdata, sizeof pdata) != sizeof pdata)
			exit(1);
		exit(0);
	}

	if (pledge("stdio rpath wpath settime", NULL) == -1)
		err(1, "pledge");

	close(p[1]);	/* write side of pipe */
	if (read(p[0], &pdata, sizeof pdata) < 1)
		err(1, "child did not collect time");
	if (waitpid(pid, NULL, 0) == -1)
		err(1, "waitpid");

	/*
	 * A viable timestamp from the child contains no message.
	 */
	if (pdata.message[0]) {
		pdata.message[sizeof(pdata.message)- 1] = '\0';
		write(STDERR_FILENO, pdata.message, strlen(pdata.message));
		exit(1);
	}

	if (!pr) {
		if (!slidetime) {
			logwtmp("|", "date", "");
			if (settimeofday(&pdata.new, NULL) == -1)
				err(1, "Could not set time of day");
			logwtmp("{", "date", "");
		} else {
			if (adjtime(&pdata.adjust, NULL) == -1)
				err(1, "Could not adjust time of day");
		}
	}

	if (pledge("stdio rpath", NULL) == -1)
		err(1, "pledge");

	if (!silent) {
		struct tm      *ltm;
		char		buf[80];
		time_t		tim = pdata.new.tv_sec;
		double		adjsec;

		ltm = localtime(&tim);
		(void) strftime(buf, sizeof buf, "%a %b %e %H:%M:%S %Z %Y\n", ltm);
		(void) fputs(buf, stdout);

		adjsec  = pdata.adjust.tv_sec + pdata.adjust.tv_usec / 1.0e6;

		if (slidetime || verbose) {
			if (ntp)
				(void) fprintf(stdout,
				   "%s: adjust local clock by %.6f seconds\n",
				   __progname, adjsec);
			else
				(void) fprintf(stdout,
				   "%s: adjust local clock by %lld seconds\n",
				   __progname, (long long)pdata.adjust.tv_sec);
		}
	}

	return 0;
}
