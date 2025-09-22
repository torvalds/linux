/*	$OpenBSD: shutdown.c,v 1.56 2024/04/28 16:43:42 florian Exp $	*/
/*	$NetBSD: shutdown.c,v 1.9 1995/03/18 15:01:09 cgd Exp $	*/

/*
 * Copyright (c) 1988, 1990, 1993
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

#include <sys/types.h>
#include <sys/resource.h>
#include <sys/syslog.h>
#include <sys/wait.h>

#include <ctype.h>
#include <fcntl.h>
#include <sys/termios.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <err.h>

#include "pathnames.h"

#ifdef DEBUG
#undef _PATH_NOLOGIN
#define	_PATH_NOLOGIN	"./nologin"
#undef _PATH_FASTBOOT
#define	_PATH_FASTBOOT	"./fastboot"
#endif

#define	H		*60*60LL
#define	M		*60LL
#define	S		*1LL
#define	TEN_HOURS	(10*60*60)
#define	NOLOG_TIME	(5*60)
struct interval {
	time_t timeleft;
	time_t timetowait;
} tlist[] = {
	{    0,    0 },
	{ 10 H,  5 H },
	{  5 H,  3 H },
	{  2 H,  1 H },
	{  1 H, 30 M },
	{ 30 M, 10 M },
	{ 20 M, 10 M },
	{ 10 M,  5 M },
	{  5 M,  3 M },
	{  2 M,  1 M },
	{  1 M, 30 S },
	{ 30 S, 30 S },
	{    0,    0 }
};
const int tlistlen = sizeof(tlist) / sizeof(tlist[0]);
#undef H
#undef M
#undef S

static time_t offset, shuttime;
static int dofast, dohalt, doreboot, dopower, dodump, mbuflen, nosync;
static volatile sig_atomic_t killflg, timed_out;
static char *whom, mbuf[BUFSIZ];

void badtime(void);
void __dead die_you_gravy_sucking_pig_dog(void);
void doitfast(void);
void __dead finish(int);
void getoffset(char *);
void __dead loop(void);
void nolog(time_t);
void timeout(int);
void timewarn(time_t);
void usage(void);

int
main(int argc, char *argv[])
{
	char when[64];
	char *p, *endp;
	struct passwd *pw;
	struct tm *lt;
	int arglen, ch, len, readstdin = 0;
	pid_t forkpid;

#ifndef DEBUG
	if (geteuid())
		errx(1, "NOT super-user");
#endif
	while ((ch = getopt(argc, argv, "dfhknpr-")) != -1)
		switch (ch) {
		case '-':
			readstdin = 1;
			break;
		case 'd':
			dodump = 1;
			break;
		case 'f':
			dofast = 1;
			break;
		case 'h':
			dohalt = 1;
			break;
		case 'k':
			killflg = 1;
			break;
		case 'n':
			nosync = 1;
			break;
		case 'p':
			dopower = 1;
			break;
		case 'r':
			doreboot = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	if (dofast && nosync) {
		warnx("incompatible switches -f and -n.");
		usage();
	}
	if (doreboot && dohalt) {
		warnx("incompatible switches -h and -r.");
		usage();
	}
	if (doreboot && dopower) {
		warnx("incompatible switches -p and -r.");
		usage();
	}

	if (unveil(_PATH_CONSOLE, "rw") == -1)
		err(1, "unveil %s", _PATH_CONSOLE);
	if (unveil(_PATH_RC, "r") == -1)
		err(1, "unveil %s", _PATH_RC);
	if (unveil(_PATH_WALL, "x") == -1)
		err(1, "unveil %s", _PATH_WALL);
	if (unveil(_PATH_FASTBOOT, "wc") == -1)
		err(1, "unveil %s", _PATH_FASTBOOT);
	if (unveil(_PATH_NOLOGIN, "wc") == -1)
		err(1, "unveil %s", _PATH_NOLOGIN);
	if (dohalt || dopower) {
		if (unveil(_PATH_HALT, "x") == -1)
			err(1, "unveil %s", _PATH_HALT);
	} else if (doreboot) {
		if (unveil(_PATH_REBOOT, "x") == -1)
			err(1, "unveil %s", _PATH_REBOOT);
	} else {
		if (unveil(_PATH_BSHELL, "x") == -1)
			err(1, "unveil %s", _PATH_BSHELL);
	}
	if (pledge("stdio rpath wpath cpath getpw tty id proc exec", NULL) == -1)
		err(1, "pledge");

	getoffset(*argv++);

	if (*argv) {
		for (p = mbuf, len = sizeof(mbuf); *argv; ++argv) {
			arglen = strlen(*argv);
			if ((len -= arglen) <= 2)
				break;
			if (p != mbuf)
				*p++ = ' ';
			memcpy(p, *argv, arglen);
			p += arglen;
		}
		*p = '\n';
		*++p = '\0';
	}

	if (readstdin) {
		p = mbuf;
		endp = mbuf + sizeof(mbuf) - 2;
		for (;;) {
			if (!fgets(p, endp - p + 1, stdin))
				break;
			for (; *p &&  p < endp; ++p)
				;
			if (p == endp) {
				*p = '\n';
				*++p = '\0';
				break;
			}
		}
	}
	mbuflen = strlen(mbuf);

	if (offset > 0) {
		shuttime = time(NULL) + offset;
		lt = localtime(&shuttime);
		if (lt != NULL) {
			strftime(when, sizeof(when), "%a %b %e %T %Z %Y", lt);
			printf("Shutdown at %s.\n", when);
		} else
			printf("Shutdown soon.\n");
	} else
		printf("Shutdown NOW!\n");

	if (!(whom = getlogin()))
		whom = (pw = getpwuid(getuid())) ? pw->pw_name : "???";

#ifdef DEBUG
	(void)putc('\n', stdout);
#else
	(void)setpriority(PRIO_PROCESS, 0, PRIO_MIN);

	forkpid = fork();
	if (forkpid == -1)
		err(1, "fork");
	if (forkpid) {
		(void)printf("shutdown: [pid %ld]\n", (long)forkpid);
		exit(0);
	}
	setsid();
#endif
	openlog("shutdown", LOG_CONS, LOG_AUTH);
	loop();
	/* NOTREACHED */
}

void
loop(void)
{
	struct timespec timeout;
	int broadcast, i, logged;

	broadcast = 1;

	for (i = 0; i < tlistlen - 1; i++) {
		if (offset > tlist[i + 1].timeleft) {
			tlist[i].timeleft = offset;
			tlist[i].timetowait = offset - tlist[i + 1].timeleft;
			break;
		}
	}

	/*
	 * Don't spam the users: skip our offset's warning broadcast if
	 * there's a broadcast scheduled after ours and it's relatively
	 * imminent.
	 */
	if (offset > TEN_HOURS ||
	    (offset > 0 && tlist[i].timetowait < tlist[i+1].timetowait / 5))
		broadcast = 0;

	for (logged = 0; i < tlistlen; i++) {
		if (broadcast)
			timewarn(tlist[i].timeleft);
		broadcast = 1;
		if (!logged && tlist[i].timeleft <= NOLOG_TIME) {
			logged = 1;
			nolog(tlist[i].timeleft);
		}
		timeout.tv_sec = tlist[i].timetowait;
		timeout.tv_nsec = 0;
		nanosleep(&timeout, NULL);
	}
	die_you_gravy_sucking_pig_dog();
}

static char *restricted_environ[] = {
	"PATH=" _PATH_STDPATH,
	NULL
};

void
timewarn(time_t timeleft)
{
	static char hostname[HOST_NAME_MAX+1];
	char when[64];
	struct tm *lt;
	static int first;
	int fd[2];
	pid_t pid, wpid;

	if (!first++)
		(void)gethostname(hostname, sizeof(hostname));

	if (pipe(fd) == -1) {
		syslog(LOG_ERR, "pipe: %m");
		return;
	}
	switch (pid = fork()) {
	case -1:
		syslog(LOG_ERR, "fork: %m");
		close(fd[0]);
		close(fd[1]);
		return;
	case 0:
		if (dup2(fd[0], STDIN_FILENO) == -1) {
			syslog(LOG_ERR, "dup2: %m");
			_exit(1);
		}
		if (fd[0] != STDIN_FILENO)
			close(fd[0]);
		close(fd[1]);
		/* wall(1)'s undocumented '-n' flag suppresses its banner. */
		execle(_PATH_WALL, _PATH_WALL, "-n", (char *)NULL,
		    restricted_environ);
		syslog(LOG_ERR, "%s: %m", _PATH_WALL);
		_exit(1);
	default:
		close(fd[0]);
	}

	dprintf(fd[1],
	    "\007*** %sSystem shutdown message from %s@%s ***\007\n",
	    timeleft ? "": "FINAL ", whom, hostname);

	if (timeleft > 10 * 60) {
		shuttime = time(NULL) + timeleft;
		lt = localtime(&shuttime);
		if (lt != NULL) {
			strftime(when, sizeof(when), "%H:%M %Z", lt);
			dprintf(fd[1], "System going down at %s\n\n", when);
		} else
			dprintf(fd[1], "System going down in %lld minute%s\n\n",
			    (long long)(timeleft / 60),
			    (timeleft > 60) ? "s" : "");
	} else if (timeleft > 59) {
		dprintf(fd[1], "System going down in %lld minute%s\n\n",
		    (long long)(timeleft / 60), (timeleft > 60) ? "s" : "");
	} else if (timeleft)
		dprintf(fd[1], "System going down in 30 seconds\n\n");
	else
		dprintf(fd[1], "System going down IMMEDIATELY\n\n");

	if (mbuflen)
		(void)write(fd[1], mbuf, mbuflen);
	close(fd[1]);

	/*
	 * If we wait longer than 30 seconds for wall(1) to exit we'll
	 * throw off our schedule.
	 */
	signal(SIGALRM, timeout);
	siginterrupt(SIGALRM, 1);
	alarm(30);
	while ((wpid = wait(NULL)) != pid && !timed_out)
		continue;
	alarm(0);
	signal(SIGALRM, SIG_DFL);
	if (timed_out) {
		syslog(LOG_NOTICE,
		    "wall[%ld] is taking too long to exit; continuing",
		    (long)pid);
		timed_out = 0;
	}
}

void
timeout(int signo)
{
	timed_out = 1;
}

void
die_you_gravy_sucking_pig_dog(void)
{

	syslog(LOG_NOTICE, "%s by %s: %s",
	    doreboot ? "reboot" : dopower ? "power-down" : dohalt ? "halt" :
	    "shutdown", whom, mbuf);
	(void)sleep(2);

	(void)printf("\r\nSystem shutdown time has arrived\007\007\r\n");
	if (killflg) {
		(void)printf("\rbut you'll have to do it yourself\r\n");
		finish(0);
	}
	if (dofast)
		doitfast();

	if (pledge("stdio rpath wpath cpath tty id proc exec", NULL) == -1)
		err(1, "pledge");

#ifdef DEBUG
	if (doreboot)
		(void)printf("reboot");
	else if (dopower)
		(void)printf("power-down");
	else if (dohalt)
		(void)printf("halt");
	if (nosync)
		(void)printf(" no sync");
	if (dofast)
		(void)printf(" no fsck");
	if (dodump)
		(void)printf(" with dump");
	(void)printf("\nkill -HUP 1\n");
#else
	if (dohalt || dopower || doreboot) {
		char *args[10];
		char **arg, *path;

		if (pledge("stdio exec", NULL) == -1)
			err(1, "pledge");

		arg = &args[0];
		if (doreboot) {
			path = _PATH_REBOOT;
			*arg++ = "reboot";
		} else {
			path = _PATH_HALT;
			*arg++ = "halt";
		}
		*arg++ = "-l";
		if (dopower)
			*arg++ = "-p";
		if (nosync)
			*arg++ = "-n";
		if (dodump)
			*arg++ = "-d";
		*arg++ = NULL;
		execve(path, args, NULL);
		syslog(LOG_ERR, "shutdown: can't exec %s: %m.", path);
		warn("%s", path);
	}
	if (access(_PATH_RC, R_OK) != -1) {
		pid_t pid;
		struct termios t;
		int fd;

		switch ((pid = fork())) {
		case -1:
			break;
		case 0:
			if (revoke(_PATH_CONSOLE) == -1)
				perror("revoke");
			if (setsid() == -1)
				perror("setsid");
			fd = open(_PATH_CONSOLE, O_RDWR);
			if (fd == -1)
				perror("open");
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				close(fd);

			/* At a minimum... */
			tcgetattr(0, &t);
			t.c_oflag |= (ONLCR | OPOST);
			tcsetattr(0, TCSANOW, &t);

			execl(_PATH_BSHELL, "sh", _PATH_RC, "shutdown", (char *)NULL);
			_exit(1);
		default:
			waitpid(pid, NULL, 0);
		}
	}
	(void)kill(1, SIGTERM);		/* to single user */
#endif
	finish(0);
}

#define	ATOI2(p)	(p[0] - '0') * 10 + (p[1] - '0'); p += 2;

void
getoffset(char *timearg)
{
	char when[64];
	const char *errstr;
	struct tm *lt;
	int this_year;
	time_t minutes, now;
	char *p;

	if (!strcasecmp(timearg, "now")) {		/* now */
		offset = 0;
		return;
	}

	if (timearg[0] == '+') {			/* +minutes */
		minutes = strtonum(timearg, 0, INT_MAX, &errstr);
		if (errstr)
			errx(1, "relative offset is %s: %s", errstr, timearg);
		offset = minutes * 60;
		return;
	}

	/* handle hh:mm by getting rid of the colon */
	for (p = timearg; *p; ++p) {
		if (!isascii((unsigned char)*p) || !isdigit((unsigned char)*p)) {
			if (*p == ':' && strlen(p) == 3) {
				p[0] = p[1];
				p[1] = p[2];
				p[2] = '\0';
			} else
				badtime();
		}
	}

	unsetenv("TZ");					/* OUR timezone */
	time(&now);
	lt = localtime(&now);				/* current time val */

	if (lt == NULL)
		badtime();

	switch (strlen(timearg)) {
	case 10:
		this_year = lt->tm_year;
		lt->tm_year = ATOI2(timearg);
		/*
		 * check if the specified year is in the next century.
		 * allow for one year of user error as many people will
		 * enter n - 1 at the start of year n.
		 */
		if (lt->tm_year < (this_year % 100) - 1)
			lt->tm_year += 100;
		/* adjust for the year 2000 and beyond */
		lt->tm_year += (this_year - (this_year % 100));
		/* FALLTHROUGH */
	case 8:
		lt->tm_mon = ATOI2(timearg);
		if (--lt->tm_mon < 0 || lt->tm_mon > 11)
			badtime();
		/* FALLTHROUGH */
	case 6:
		lt->tm_mday = ATOI2(timearg);
		if (lt->tm_mday < 1 || lt->tm_mday > 31)
			badtime();
		/* FALLTHROUGH */
	case 4:
		lt->tm_hour = ATOI2(timearg);
		if (lt->tm_hour < 0 || lt->tm_hour > 23)
			badtime();
		lt->tm_min = ATOI2(timearg);
		if (lt->tm_min < 0 || lt->tm_min > 59)
			badtime();
		lt->tm_sec = 0;
		if ((shuttime = mktime(lt)) == -1)
			badtime();
		if ((offset = shuttime - now) < 0) {
			strftime(when, sizeof(when), "%a %b %e %T %Z %Y", lt);
			errx(1, "time is already past: %s", when);
		}
		break;
	default:
		badtime();
	}
}

void
doitfast(void)
{
	int fastfd;

	if ((fastfd = open(_PATH_FASTBOOT, O_WRONLY|O_CREAT|O_TRUNC,
	    0664)) >= 0) {
		dprintf(fastfd, "fastboot file for fsck\n");
		close(fastfd);
	}
}

void
nolog(time_t timeleft)
{
	char when[64];
	struct tm *tm;
	int logfd;

	(void)unlink(_PATH_NOLOGIN);	/* in case linked to another file */
	(void)signal(SIGINT, finish);
	(void)signal(SIGHUP, finish);
	(void)signal(SIGQUIT, finish);
	(void)signal(SIGTERM, finish);
	shuttime = time(NULL) + timeleft;
	tm = localtime(&shuttime);
	if ((logfd = open(_PATH_NOLOGIN, O_WRONLY|O_CREAT|O_TRUNC,
	    0664)) >= 0) {
		if (tm) {
			strftime(when, sizeof(when), "at %H:%M %Z", tm);
			dprintf(logfd,
			    "\n\nNO LOGINS: System going down %s\n\n", when);
		} else
			dprintf(logfd,
			    "\n\nNO LOGINS: System going in %lld minute%s\n\n",
			    (long long)(timeleft / 60),
			    (timeleft > 60) ? "s" : "");
		close(logfd);
	}
}

void
finish(int signo)
{
	if (!killflg)
		(void)unlink(_PATH_NOLOGIN);
	if (signo == 0)
		exit(0);
	else
		_exit(0);
}

void
badtime(void)
{
	errx(1, "bad time format.");
}

void
usage(void)
{
	fprintf(stderr,
	    "usage: shutdown [-] [-dfhknpr] time [warning-message ...]\n");
	exit(1);
}
