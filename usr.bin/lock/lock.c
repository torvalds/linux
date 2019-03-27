/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Bob Toxen.
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
"@(#) Copyright (c) 1980, 1987, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)lock.c	8.1 (Berkeley) 6/6/93";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Lock a terminal up until the given key is entered or the given
 * interval times out.
 *
 * Timeout interval is by default TIMEOUT, it can be changed with
 * an argument of the form -time where time is in minutes
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <sys/consio.h>

#include <err.h>
#include <ctype.h>
#include <errno.h>
#include <paths.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <security/pam_appl.h>
#include <security/openpam.h>	/* for openpam_ttyconv() */

#define	TIMEOUT	15

static void quit(int);
static void bye(int);
static void hi(int);
static void usage(void);

static struct timeval	timeout;
static struct timeval	zerotime;
static struct termios	tty, ntty;
static long		nexttime;		/* keep the timeout time */
static int		no_timeout;		/* lock terminal forever */
static int		vtyunlock;		/* Unlock flag and code. */

/*ARGSUSED*/
int
main(int argc, char **argv)
{
	static const struct pam_conv pamc = { &openpam_ttyconv, NULL };
	pam_handle_t *pamh;
	struct passwd *pw;
	struct itimerval ntimer, otimer;
	struct tm *timp;
	time_t timval;
	int ch, failures, pam_err, sectimeout, usemine, vtylock;
	char *ap, *ttynam, *tzn;
	char hostname[MAXHOSTNAMELEN], s[BUFSIZ], s1[BUFSIZ];

	openlog("lock", 0, LOG_AUTH);

	pam_err = PAM_SYSTEM_ERR; /* pacify GCC */

	sectimeout = TIMEOUT;
	pamh = NULL;
	pw = NULL;
	usemine = 0;
	no_timeout = 0;
	vtylock = 0;
	while ((ch = getopt(argc, argv, "npt:v")) != -1)
		switch((char)ch) {
		case 't':
			if ((sectimeout = atoi(optarg)) <= 0)
				errx(1, "illegal timeout value");
			break;
		case 'p':
			usemine = 1;
			if (!(pw = getpwuid(getuid())))
				errx(1, "unknown uid %d", getuid());
			break;
		case 'n':
			no_timeout = 1;
			break;
		case 'v':
			vtylock = 1;
			break;
		case '?':
		default:
			usage();
		}
	timeout.tv_sec = sectimeout * 60;

	if (!usemine) {	/* -p with PAM or S/key needs privs */
		/* discard privs */
		if (setuid(getuid()) != 0)
			errx(1, "setuid failed");
	}

	if (tcgetattr(0, &tty))		/* get information for header */
		exit(1);
	gethostname(hostname, sizeof(hostname));
	if (!(ttynam = ttyname(0)))
		errx(1, "not a terminal?");
	if (strncmp(ttynam, _PATH_DEV, strlen(_PATH_DEV)) == 0)
		ttynam += strlen(_PATH_DEV);
	timval = time(NULL);
	nexttime = timval + (sectimeout * 60);
	timp = localtime(&timval);
	ap = asctime(timp);
	tzn = timp->tm_zone;

	(void)signal(SIGINT, quit);
	(void)signal(SIGQUIT, quit);
	ntty = tty; ntty.c_lflag &= ~ECHO;
	(void)tcsetattr(0, TCSADRAIN|TCSASOFT, &ntty);

	if (usemine) {
		pam_err = pam_start("lock", pw->pw_name, &pamc, &pamh);
		if (pam_err != PAM_SUCCESS)
			err(1, "pam_start: %s", pam_strerror(NULL, pam_err));
	} else {
		/* get key and check again */
		(void)printf("Key: ");
		if (!fgets(s, sizeof(s), stdin) || *s == '\n')
			quit(0);
		(void)printf("\nAgain: ");
		/*
		 * Don't need EOF test here, if we get EOF, then s1 != s
		 * and the right things will happen.
		 */
		(void)fgets(s1, sizeof(s1), stdin);
		(void)putchar('\n');
		if (strcmp(s1, s)) {
			(void)printf("\07lock: passwords didn't match.\n");
			(void)tcsetattr(0, TCSADRAIN|TCSASOFT, &tty);
			exit(1);
		}
		s[0] = '\0';
	}

	/* set signal handlers */
	(void)signal(SIGINT, hi);
	(void)signal(SIGQUIT, hi);
	(void)signal(SIGTSTP, hi);
	(void)signal(SIGALRM, bye);

	ntimer.it_interval = zerotime;
	ntimer.it_value = timeout;
	if (!no_timeout)
		setitimer(ITIMER_REAL, &ntimer, &otimer);
	if (vtylock) {
		/*
		 * If this failed, we want to err out; warn isn't good
		 * enough, since we don't want the user to think that
		 * everything is nice and locked because they got a
		 * "Key:" prompt.
		 */
		if (ioctl(0, VT_LOCKSWITCH, &vtylock) == -1) {
			(void)tcsetattr(0, TCSADRAIN|TCSASOFT, &tty);
			err(1, "locking vty");
		}
		vtyunlock = 0x2;
	}

	/* header info */
	if (pw != NULL)
		(void)printf("lock: %s using %s on %s.", pw->pw_name,
		    ttynam, hostname);
	else
		(void)printf("lock: %s on %s.", ttynam, hostname);
	if (no_timeout)
		(void)printf(" no timeout.");
	else
		(void)printf(" timeout in %d minute%s.", sectimeout,
		    sectimeout != 1 ? "s" : "");
	if (vtylock)
		(void)printf(" vty locked.");
	(void)printf("\ntime now is %.20s%s%s", ap, tzn, ap + 19);

	failures = 0;

	for (;;) {
		if (usemine) {
			pam_err = pam_authenticate(pamh, 0);
			if (pam_err == PAM_SUCCESS)
				break;

			if (pam_err != PAM_AUTH_ERR &&
			    pam_err != PAM_USER_UNKNOWN &&
			    pam_err != PAM_MAXTRIES) {
				syslog(LOG_ERR, "pam_authenticate: %s",
				    pam_strerror(pamh, pam_err));
			}
			
			goto tryagain;
		}
		(void)printf("Key: ");
		if (!fgets(s, sizeof(s), stdin)) {
			clearerr(stdin);
			hi(0);
			goto tryagain;
		}
		if (!strcmp(s, s1))
			break;
		(void)printf("\07\n");
	    	failures++;
		if (getuid() == 0)
	    	    syslog(LOG_NOTICE, "%d ROOT UNLOCK FAILURE%s (%s on %s)",
			failures, failures > 1 ? "S": "", ttynam, hostname);
tryagain:
		if (tcgetattr(0, &ntty) && (errno != EINTR))
			exit(1);
		sleep(1);		/* to discourage guessing */
	}
	if (getuid() == 0)
		syslog(LOG_NOTICE, "ROOT UNLOCK ON hostname %s port %s",
		    hostname, ttynam);
	if (usemine)
		(void)pam_end(pamh, pam_err);
	quit(0);
	return(0); /* not reached */
}


static void
usage(void)
{
	(void)fprintf(stderr, "usage: lock [-npv] [-t timeout]\n");
	exit(1);
}

static void
hi(int signo __unused)
{
	time_t timval;

	timval = time(NULL);
	(void)printf("lock: type in the unlock key. ");
	if (no_timeout) {
		(void)putchar('\n');
	} else {
		(void)printf("timeout in %jd:%jd minutes\n",
		    (intmax_t)(nexttime - timval) / 60,
		    (intmax_t)(nexttime - timval) % 60);
	}
}

static void
quit(int signo __unused)
{
	(void)putchar('\n');
	(void)tcsetattr(0, TCSADRAIN|TCSASOFT, &tty);
	if (vtyunlock)
		(void)ioctl(0, VT_LOCKSWITCH, &vtyunlock);
	exit(0);
}

static void
bye(int signo __unused)
{
	if (!no_timeout) {
		(void)tcsetattr(0, TCSADRAIN|TCSASOFT, &tty);
		if (vtyunlock)
			(void)ioctl(0, VT_LOCKSWITCH, &vtyunlock);
		(void)printf("lock: timeout\n");
		exit(1);
	}
}
