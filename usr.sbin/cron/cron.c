/*	$OpenBSD: cron.c,v 1.82 2022/07/08 20:47:24 millert Exp $	*/

/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1997,2000 by Internet Software Consortium, Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <bitstring.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "pathnames.h"
#include "macros.h"
#include "structs.h"
#include "funcs.h"
#include "globals.h"

enum timejump { negative, small, medium, large };

static	void	usage(void),
		run_reboot_jobs(cron_db *),
		find_jobs(time_t, cron_db *, int, int),
		set_time(int),
		cron_sleep(time_t, sigset_t *),
		sigchld_handler(int),
		sigchld_reaper(void),
		parse_args(int c, char *v[]);

static	int	open_socket(void);

static	volatile sig_atomic_t	got_sigchld;
static	time_t			timeRunning, virtualTime, clockTime;
static	long			GMToff;
static	cron_db			*database;
static	at_db			*at_database;
static	double			batch_maxload = BATCH_MAXLOAD;
static	int			NoFork;
static	time_t			StartTime;
	gid_t			cron_gid;
	int			cronSock;

static void
usage(void)
{

	fprintf(stderr, "usage: %s [-n] [-l load_avg]\n", __progname);
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	struct sigaction sact;
	sigset_t blocked, omask;
	struct group *grp;

	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	parse_args(argc, argv);

	bzero((char *)&sact, sizeof sact);
	sigemptyset(&sact.sa_mask);
	sact.sa_flags = SA_RESTART;
	sact.sa_handler = sigchld_handler;
	(void) sigaction(SIGCHLD, &sact, NULL);
	sact.sa_handler = SIG_IGN;
	(void) sigaction(SIGHUP, &sact, NULL);
	(void) sigaction(SIGPIPE, &sact, NULL);

	openlog(__progname, LOG_PID, LOG_CRON);

	if (pledge("stdio rpath wpath cpath fattr getpw unix id dns proc exec",
	    NULL) == -1) {
		warn("pledge");
		syslog(LOG_ERR, "(CRON) PLEDGE (%m)");
		exit(EXIT_FAILURE);
	}

	if ((grp = getgrnam(CRON_GROUP)) == NULL) {
		warnx("can't find cron group %s", CRON_GROUP);
		syslog(LOG_ERR, "(CRON) DEATH (can't find cron group)");
		exit(EXIT_FAILURE);
	}
	cron_gid = grp->gr_gid;

	cronSock = open_socket();

	if (putenv("PATH="_PATH_DEFPATH) < 0) {
		warn("putenv");
		syslog(LOG_ERR, "(CRON) DEATH (%m)");
		exit(EXIT_FAILURE);
	}

	if (NoFork == 0) {
		if (daemon(0, 0) == -1) {
			syslog(LOG_ERR, "(CRON) DEATH (%m)");
			exit(EXIT_FAILURE);
		}
		syslog(LOG_INFO, "(CRON) STARTUP (%s)", CRON_VERSION);
	}

	load_database(&database);
	scan_atjobs(&at_database, NULL);
	set_time(TRUE);
	run_reboot_jobs(database);
	timeRunning = virtualTime = clockTime;

	/*
	 * We block SIGHUP and SIGCHLD while running jobs and receive them
	 * only while sleeping in ppoll().  This ensures no signal is lost.
	 */
	sigemptyset(&blocked);
	sigaddset(&blocked, SIGCHLD);
	sigaddset(&blocked, SIGHUP);
	sigprocmask(SIG_BLOCK, &blocked, &omask);

	/*
	 * Too many clocks, not enough time (Al. Einstein)
	 * These clocks are in minutes since the epoch, adjusted for timezone.
	 * virtualTime: is the time it *would* be if we woke up
	 * promptly and nobody ever changed the clock. It is
	 * monotonically increasing... unless a timejump happens.
	 * At the top of the loop, all jobs for 'virtualTime' have run.
	 * timeRunning: is the time we last awakened.
	 * clockTime: is the time when set_time was last called.
	 */
	while (TRUE) {
		int timeDiff;
		enum timejump wakeupKind;

		/* ... wait for the time (in minutes) to change ... */
		do {
			cron_sleep(timeRunning + 1, &omask);
			set_time(FALSE);
		} while (clockTime == timeRunning);
		timeRunning = clockTime;

		/*
		 * Calculate how the current time differs from our virtual
		 * clock.  Classify the change into one of 4 cases.
		 */
		timeDiff = timeRunning - virtualTime;

		/* shortcut for the most common case */
		if (timeDiff == 1) {
			virtualTime = timeRunning;
			find_jobs(virtualTime, database, TRUE, TRUE);
		} else {
			if (timeDiff > (3*MINUTE_COUNT) ||
			    timeDiff < -(3*MINUTE_COUNT))
				wakeupKind = large;
			else if (timeDiff > 5)
				wakeupKind = medium;
			else if (timeDiff > 0)
				wakeupKind = small;
			else
				wakeupKind = negative;

			switch (wakeupKind) {
			case small:
				/*
				 * case 1: timeDiff is a small positive number
				 * (wokeup late) run jobs for each virtual
				 * minute until caught up.
				 */
				do {
					if (job_runqueue())
						sleep(10);
					virtualTime++;
					find_jobs(virtualTime, database,
					    TRUE, TRUE);
				} while (virtualTime < timeRunning);
				break;

			case medium:
				/*
				 * case 2: timeDiff is a medium-sized positive
				 * number, for example because we went to DST
				 * run wildcard jobs once, then run any
				 * fixed-time jobs that would otherwise be
				 * skipped if we use up our minute (possible,
				 * if there are a lot of jobs to run) go
				 * around the loop again so that wildcard jobs
				 * have a chance to run, and we do our
				 * housekeeping.
				 */
				/* run wildcard jobs for current minute */
				find_jobs(timeRunning, database, TRUE, FALSE);

				/* run fixed-time jobs for each minute missed */
				do {
					if (job_runqueue())
						sleep(10);
					virtualTime++;
					find_jobs(virtualTime, database,
					    FALSE, TRUE);
					set_time(FALSE);
				} while (virtualTime< timeRunning &&
				    clockTime == timeRunning);
				break;

			case negative:
				/*
				 * case 3: timeDiff is a small or medium-sized
				 * negative num, eg. because of DST ending.
				 * Just run the wildcard jobs. The fixed-time
				 * jobs probably have already run, and should
				 * not be repeated.  Virtual time does not
				 * change until we are caught up.
				 */
				find_jobs(timeRunning, database, TRUE, FALSE);
				break;
			default:
				/*
				 * other: time has changed a *lot*,
				 * jump virtual time, and run everything
				 */
				virtualTime = timeRunning;
				find_jobs(timeRunning, database, TRUE, TRUE);
			}
		}

		/* Jobs to be run (if any) are loaded; clear the queue. */
		job_runqueue();

		/* Run any jobs in the at queue. */
		atrun(at_database, batch_maxload,
		    timeRunning * SECONDS_PER_MINUTE - GMToff);

		/* Reload jobs as needed. */
		load_database(&database);
		scan_atjobs(&at_database, NULL);
	}
}

static void
run_reboot_jobs(cron_db *db)
{
	user *u;
	entry *e;

	TAILQ_FOREACH(u, &db->users, entries) {
		SLIST_FOREACH(e, &u->crontab, entries) {
			if (e->flags & WHEN_REBOOT)
				job_add(e, u);
		}
	}
	(void) job_runqueue();
}

static void
find_jobs(time_t vtime, cron_db *db, int doWild, int doNonWild)
{
	time_t virtualSecond  = vtime * SECONDS_PER_MINUTE;
	struct tm *tm = gmtime(&virtualSecond);
	int minute, hour, dom, month, dow;
	user *u;
	entry *e;

	/* make 0-based values out of these so we can use them as indices
	 */
	minute = tm->tm_min -FIRST_MINUTE;
	hour = tm->tm_hour -FIRST_HOUR;
	dom = tm->tm_mday -FIRST_DOM;
	month = tm->tm_mon +1 /* 0..11 -> 1..12 */ -FIRST_MONTH;
	dow = tm->tm_wday -FIRST_DOW;

	/* the dom/dow situation is odd.  '* * 1,15 * Sun' will run on the
	 * first and fifteenth AND every Sunday;  '* * * * Sun' will run *only*
	 * on Sundays;  '* * 1,15 * *' will run *only* the 1st and 15th.  this
	 * is why we keep 'e->dow_star' and 'e->dom_star'.  yes, it's bizarre.
	 * like many bizarre things, it's the standard.
	 */
	TAILQ_FOREACH(u, &db->users, entries) {
		SLIST_FOREACH(e, &u->crontab, entries) {
			if (bit_test(e->minute, minute) &&
			    bit_test(e->hour, hour) &&
			    bit_test(e->month, month) &&
			    ( ((e->flags & DOM_STAR) || (e->flags & DOW_STAR))
			      ? (bit_test(e->dow,dow) && bit_test(e->dom,dom))
			      : (bit_test(e->dow,dow) || bit_test(e->dom,dom))
			    )
			   ) {
				if ((doNonWild &&
				    !(e->flags & (MIN_STAR|HR_STAR))) ||
				    (doWild && (e->flags & (MIN_STAR|HR_STAR))))
					job_add(e, u);
			}
		}
	}
}

/*
 * Set StartTime and clockTime to the current time.
 * These are used for computing what time it really is right now.
 * Note that clockTime is a unix wallclock time converted to minutes.
 */
static void
set_time(int initialize)
{
	struct tm tm;
	static int isdst;

	StartTime = time(NULL);

	/* We adjust the time to GMT so we can catch DST changes. */
	tm = *localtime(&StartTime);
	if (initialize || tm.tm_isdst != isdst) {
		isdst = tm.tm_isdst;
		GMToff = get_gmtoff(&StartTime, &tm);
	}
	clockTime = (StartTime + GMToff) / (time_t)SECONDS_PER_MINUTE;
}

/*
 * Try to just hit the next minute.
 */
static void
cron_sleep(time_t target, sigset_t *mask)
{
	int fd, nfds;
	unsigned char poke;
	struct timespec t1, t2, timeout;
	struct sockaddr_un s_un;
	socklen_t sunlen;
	static struct pollfd pfd[1];

	clock_gettime(CLOCK_REALTIME, &t1);
	t1.tv_sec += GMToff;
	timeout.tv_sec = (target * SECONDS_PER_MINUTE - t1.tv_sec) + 1;
	if (timeout.tv_sec < 0)
		timeout.tv_sec = 0;
	timeout.tv_nsec = 0;

	pfd[0].fd = cronSock;
	pfd[0].events = POLLIN;

	while (timespecisset(&timeout) && timeout.tv_sec < 65) {
		poke = RELOAD_CRON | RELOAD_AT;

		/* Sleep until we time out, get a poke, or get a signal. */
		nfds = ppoll(pfd, 1, &timeout, mask);
		switch (nfds) {
		case -1:
			if (errno != EINTR && errno != EAGAIN) {
				syslog(LOG_ERR, "(CRON) DEATH (ppoll failure: %m)");
				exit(EXIT_FAILURE);
			}
			if (errno == EINTR) {
				if (got_sigchld) {
					got_sigchld = 0;
					sigchld_reaper();
				}
			}
			break;
		case 0:
			/* done sleeping */
			return;
		default:
			sunlen = sizeof(s_un);
			fd = accept4(cronSock, (struct sockaddr *)&s_un,
			    &sunlen, SOCK_NONBLOCK);
			if (fd >= 0) {
				(void) read(fd, &poke, 1);
				close(fd);
				if (poke & RELOAD_CRON) {
					timespecclear(&database->mtime);
					load_database(&database);
				}
				if (poke & RELOAD_AT) {
					/*
					 * We run any pending at jobs right
					 * away so that "at now" really runs
					 * jobs immediately.
					 */
					clock_gettime(CLOCK_REALTIME, &t2);
					timespecclear(&at_database->mtime);
					if (scan_atjobs(&at_database, &t2))
						atrun(at_database,
						    batch_maxload, t2.tv_sec);
				}
			}
		}

		/* Adjust tv and continue where we left off.  */
		clock_gettime(CLOCK_REALTIME, &t2);
		t2.tv_sec += GMToff;
		timespecsub(&t2, &t1, &t1);
		timespecsub(&timeout, &t1, &timeout);
		memcpy(&t1, &t2, sizeof(t1));
		if (timeout.tv_sec < 0)
			timespecclear(&timeout);
	}
}

/* int open_socket(void)
 *	opens a UNIX domain socket that crontab uses to poke cron.
 *	If the socket is already in use, return an error.
 */
static int
open_socket(void)
{
	int		   sock, rc;
	mode_t		   omask;
	struct sockaddr_un s_un;

	sock = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
	if (sock == -1) {
		warn("socket");
		syslog(LOG_ERR, "(CRON) DEATH (can't create socket)");
		exit(EXIT_FAILURE);
	}
	bzero(&s_un, sizeof(s_un));
	if (strlcpy(s_un.sun_path, _PATH_CRON_SOCK, sizeof(s_un.sun_path))
	    >= sizeof(s_un.sun_path)) {
		warnc(ENAMETOOLONG, _PATH_CRON_SOCK);
		syslog(LOG_ERR, "(CRON) DEATH (socket path too long)");
		exit(EXIT_FAILURE);
	}
	s_un.sun_family = AF_UNIX;

	if (connect(sock, (struct sockaddr *)&s_un, sizeof(s_un)) == 0) {
		warnx("already running");
		syslog(LOG_ERR, "(CRON) DEATH (already running)");
		exit(EXIT_FAILURE);
	}
	if (errno != ENOENT)
		unlink(s_un.sun_path);

	omask = umask(007);
	rc = bind(sock, (struct sockaddr *)&s_un, sizeof(s_un));
	umask(omask);
	if (rc != 0) {
		warn("bind");
		syslog(LOG_ERR, "(CRON) DEATH (can't bind socket)");
		exit(EXIT_FAILURE);
	}
	if (listen(sock, SOMAXCONN)) {
		warn("listen");
		syslog(LOG_ERR, "(CRON) DEATH (can't listen on socket)");
		exit(EXIT_FAILURE);
	}

	/* pledge won't let us change files to a foreign group. */
	if (setegid(cron_gid) == 0) {
		chown(s_un.sun_path, -1, cron_gid);
		(void)setegid(getgid());
	}
	chmod(s_un.sun_path, 0660);

	return(sock);
}

static void
sigchld_handler(int x)
{
	got_sigchld = 1;
}

static void
sigchld_reaper(void)
{
	int waiter;
	pid_t pid;

	do {
		pid = waitpid(-1, &waiter, WNOHANG);
		switch (pid) {
		case -1:
			if (errno == EINTR)
				continue;
			break;
		case 0:
			break;
		default:
			job_exit(pid);
			break;
		}
	} while (pid > 0);
}

static void
parse_args(int argc, char *argv[])
{
	int argch;
	char *ep;

	while (-1 != (argch = getopt(argc, argv, "l:n"))) {
		switch (argch) {
		case 'l':
			errno = 0;
			batch_maxload = strtod(optarg, &ep);
			if (*ep != '\0' || ep == optarg || errno == ERANGE ||
			    batch_maxload < 0) {
				warnx("illegal load average: %s", optarg);
				usage();
			}
			break;
		case 'n':
			NoFork = 1;
			break;
		default:
			usage();
		}
	}
}
