/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1994 Christopher G. Demetriou
 * Copyright (c) 1994 Simon J. Gerraty
 * Copyright (c) 2012 Ed Schouten <ed@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/queue.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <timeconv.h>
#include <unistd.h>
#include <utmpx.h>

/*
 * this is for our list of currently logged in sessions
 */
struct utmpx_entry {
	SLIST_ENTRY(utmpx_entry) next;
	char		user[sizeof(((struct utmpx *)0)->ut_user)];
	char		id[sizeof(((struct utmpx *)0)->ut_id)];
#ifdef CONSOLE_TTY
	char		line[sizeof(((struct utmpx *)0)->ut_line)];
#endif
	struct timeval	time;
};

/*
 * this is for our list of users that are accumulating time.
 */
struct user_entry {
	SLIST_ENTRY(user_entry) next;
	char		user[sizeof(((struct utmpx *)0)->ut_user)];
	struct timeval	time;
};

/*
 * this is for chosing whether to ignore a login
 */
struct tty_entry {
	SLIST_ENTRY(tty_entry) next;
	char		line[sizeof(((struct utmpx *)0)->ut_line) + 2];
	size_t		len;
	int		ret;
};

/*
 * globals - yes yuk
 */
#ifdef CONSOLE_TTY
static const char *Console = CONSOLE_TTY;
#endif
static struct timeval Total = { 0, 0 };
static struct timeval FirstTime = { 0, 0 };
static int	Flags = 0;
static SLIST_HEAD(, utmpx_entry) CurUtmpx = SLIST_HEAD_INITIALIZER(CurUtmpx);
static SLIST_HEAD(, user_entry) Users = SLIST_HEAD_INITIALIZER(Users);
static SLIST_HEAD(, tty_entry) Ttys = SLIST_HEAD_INITIALIZER(Ttys);

#define	AC_W	1				/* not _PATH_WTMP */
#define	AC_D	2				/* daily totals (ignore -p) */
#define	AC_P	4				/* per-user totals */
#define	AC_U	8				/* specified users only */
#define	AC_T	16				/* specified ttys only */

static void	ac(const char *);
static void	usage(void);

static void
add_tty(const char *line)
{
	struct tty_entry *tp;
	char *rcp;

	Flags |= AC_T;

	if ((tp = malloc(sizeof(*tp))) == NULL)
		errx(1, "malloc failed");
	tp->len = 0;				/* full match */
	tp->ret = 1;				/* do if match */
	if (*line == '!') {			/* don't do if match */
		tp->ret = 0;
		line++;
	}
	strlcpy(tp->line, line, sizeof(tp->line));
	/* Wildcard. */
	if ((rcp = strchr(tp->line, '*')) != NULL) {
		*rcp = '\0';
		/* Match len bytes only. */
		tp->len = strlen(tp->line);
	}
	SLIST_INSERT_HEAD(&Ttys, tp, next);
}

/*
 * should we process the named tty?
 */
static int
do_tty(const char *line)
{
	struct tty_entry *tp;
	int def_ret = 0;

	SLIST_FOREACH(tp, &Ttys, next) {
		if (tp->ret == 0)		/* specific don't */
			def_ret = 1;		/* default do */
		if (tp->len != 0) {
			if (strncmp(line, tp->line, tp->len) == 0)
				return tp->ret;
		} else {
			if (strncmp(line, tp->line, sizeof(tp->line)) == 0)
				return tp->ret;
		}
	}
	return (def_ret);
}

#ifdef CONSOLE_TTY
/*
 * is someone logged in on Console?
 */
static int
on_console(void)
{
	struct utmpx_entry *up;

	SLIST_FOREACH(up, &CurUtmpx, next)
		if (strcmp(up->line, Console) == 0)
			return (1);
	return (0);
}
#endif

/*
 * Update user's login time.
 * If no entry for this user is found, a new entry is inserted into the
 * list alphabetically.
 */
static void
update_user(const char *user, struct timeval secs)
{
	struct user_entry *up, *aup;
	int c;

	aup = NULL;
	SLIST_FOREACH(up, &Users, next) {
		c = strcmp(up->user, user);
		if (c == 0) {
			timeradd(&up->time, &secs, &up->time);
			timeradd(&Total, &secs, &Total);
			return;
		} else if (c > 0)
			break;
		aup = up;
	}
	/*
	 * not found so add new user unless specified users only
	 */
	if (Flags & AC_U)
		return;

	if ((up = malloc(sizeof(*up))) == NULL)
		errx(1, "malloc failed");
	if (aup == NULL)
		SLIST_INSERT_HEAD(&Users, up, next);
	else
		SLIST_INSERT_AFTER(aup, up, next);
	strlcpy(up->user, user, sizeof(up->user));
	up->time = secs;
	timeradd(&Total, &secs, &Total);
}

int
main(int argc, char *argv[])
{
	const char *wtmpf = NULL;
	int c;

	(void) setlocale(LC_TIME, "");

	while ((c = getopt(argc, argv, "c:dpt:w:")) != -1) {
		switch (c) {
		case 'c':
#ifdef CONSOLE_TTY
			Console = optarg;
#else
			usage();		/* XXX */
#endif
			break;
		case 'd':
			Flags |= AC_D;
			break;
		case 'p':
			Flags |= AC_P;
			break;
		case 't':			/* only do specified ttys */
			add_tty(optarg);
			break;
		case 'w':
			Flags |= AC_W;
			wtmpf = optarg;
			break;
		case '?':
		default:
			usage();
			break;
		}
	}
	if (optind < argc) {
		/*
		 * initialize user list
		 */
		for (; optind < argc; optind++) {
			update_user(argv[optind], (struct timeval){ 0, 0 });
		}
		Flags |= AC_U;			/* freeze user list */
	}
	if (Flags & AC_D)
		Flags &= ~AC_P;
	ac(wtmpf);

	return (0);
}

/*
 * print login time in decimal hours
 */
static void
show(const char *user, struct timeval secs)
{
	(void)printf("\t%-*s %8.2f\n",
	    (int)sizeof(((struct user_entry *)0)->user), user,
	    (double)secs.tv_sec / 3600);
}

static void
show_users(void)
{
	struct user_entry *lp;

	SLIST_FOREACH(lp, &Users, next)
		show(lp->user, lp->time);
}

/*
 * print total login time for 24hr period in decimal hours
 */
static void
show_today(struct timeval today)
{
	struct user_entry *up;
	struct utmpx_entry *lp;
	char date[64];
	struct timeval diff, total = { 0, 0 }, usec = { 0, 1 }, yesterday;
	static int d_first = -1;

	if (d_first < 0)
		d_first = (*nl_langinfo(D_MD_ORDER) == 'd');
	timersub(&today, &usec, &yesterday);
	(void)strftime(date, sizeof(date),
		       d_first ? "%e %b  total" : "%b %e  total",
		       localtime(&yesterday.tv_sec));

	SLIST_FOREACH(lp, &CurUtmpx, next) {
		timersub(&today, &lp->time, &diff);
		update_user(lp->user, diff);
		/* As if they just logged in. */
		lp->time = today;
	}
	SLIST_FOREACH(up, &Users, next) {
		timeradd(&total, &up->time, &total);
		/* For next day. */
		timerclear(&up->time);
	}
	if (timerisset(&total))
		(void)printf("%s %11.2f\n", date, (double)total.tv_sec / 3600);
}

/*
 * Log a user out and update their times.
 * If ut_type is BOOT_TIME or SHUTDOWN_TIME, we log all users out as the
 * system has been shut down.
 */
static void
log_out(const struct utmpx *up)
{
	struct utmpx_entry *lp, *lp2, *tlp;
	struct timeval secs;

	for (lp = SLIST_FIRST(&CurUtmpx), lp2 = NULL; lp != NULL;)
		if (up->ut_type == BOOT_TIME || up->ut_type == SHUTDOWN_TIME ||
		    (up->ut_type == DEAD_PROCESS &&
		    memcmp(lp->id, up->ut_id, sizeof(up->ut_id)) == 0)) {
			timersub(&up->ut_tv, &lp->time, &secs);
			update_user(lp->user, secs);
			/*
			 * now lose it
			 */
			tlp = lp;
			lp = SLIST_NEXT(lp, next);
			if (lp2 == NULL)
				SLIST_REMOVE_HEAD(&CurUtmpx, next);
			else
				SLIST_REMOVE_AFTER(lp2, next);
			free(tlp);
		} else {
			lp2 = lp;
			lp = SLIST_NEXT(lp, next);
		}
}

/*
 * if do_tty says ok, login a user
 */
static void
log_in(struct utmpx *up)
{
	struct utmpx_entry *lp;

	/*
	 * this could be a login. if we're not dealing with
	 * the console name, say it is.
	 *
	 * If we are, and if ut_host==":0.0" we know that it
	 * isn't a real login. _But_ if we have not yet recorded
	 * someone being logged in on Console - due to the wtmp
	 * file starting after they logged in, we'll pretend they
	 * logged in, at the start of the wtmp file.
	 */

#ifdef CONSOLE_TTY
	if (up->ut_host[0] == ':') {
		/*
		 * SunOS 4.0.2 does not treat ":0.0" as special but we
		 * do.
		 */
		if (on_console())
			return;
		/*
		 * ok, no recorded login, so they were here when wtmp
		 * started!  Adjust ut_time!
		 */
		up->ut_tv = FirstTime;
		/*
		 * this allows us to pick the right logout
		 */
		strlcpy(up->ut_line, Console, sizeof(up->ut_line));
	}
#endif
	/*
	 * If we are doing specified ttys only, we ignore
	 * anything else.
	 */
	if (Flags & AC_T && !do_tty(up->ut_line))
		return;

	/*
	 * go ahead and log them in
	 */
	if ((lp = malloc(sizeof(*lp))) == NULL)
		errx(1, "malloc failed");
	SLIST_INSERT_HEAD(&CurUtmpx, lp, next);
	strlcpy(lp->user, up->ut_user, sizeof(lp->user));
	memcpy(lp->id, up->ut_id, sizeof(lp->id));
#ifdef CONSOLE_TTY
	memcpy(lp->line, up->ut_line, sizeof(lp->line));
#endif
	lp->time = up->ut_tv;
}

static void
ac(const char *file)
{
	struct utmpx_entry *lp;
	struct utmpx *usr, usht;
	struct tm *ltm;
	struct timeval prev_secs, ut_timecopy, secs, clock_shift, now;
	int day, rfound;

	day = -1;
	timerclear(&prev_secs);	/* Minimum acceptable date == 1970. */
	timerclear(&secs);
	timerclear(&clock_shift);
	rfound = 0;
	if (setutxdb(UTXDB_LOG, file) != 0)
		err(1, "%s", file);
	while ((usr = getutxent()) != NULL) {
		rfound++;
		ut_timecopy = usr->ut_tv;
		/* Don't let the time run backwards. */
		if (timercmp(&ut_timecopy, &prev_secs, <))
			ut_timecopy = prev_secs;
		prev_secs = ut_timecopy;

		if (!timerisset(&FirstTime))
			FirstTime = ut_timecopy;
		if (Flags & AC_D) {
			ltm = localtime(&ut_timecopy.tv_sec);
			if (day >= 0 && day != ltm->tm_yday) {
				day = ltm->tm_yday;
				/*
				 * print yesterday's total
				 */
				secs = ut_timecopy;
				secs.tv_sec -= ltm->tm_sec;
				secs.tv_sec -= 60 * ltm->tm_min;
				secs.tv_sec -= 3600 * ltm->tm_hour;
				secs.tv_usec = 0;
				show_today(secs);
			} else
				day = ltm->tm_yday;
		}
		switch(usr->ut_type) {
		case OLD_TIME:
			clock_shift = ut_timecopy;
			break;
		case NEW_TIME:
			timersub(&clock_shift, &ut_timecopy, &clock_shift);
			/*
			 * adjust time for those logged in
			 */
			SLIST_FOREACH(lp, &CurUtmpx, next)
				timersub(&lp->time, &clock_shift, &lp->time);
			break;
		case BOOT_TIME:
		case SHUTDOWN_TIME:
			log_out(usr);
			FirstTime = ut_timecopy; /* shouldn't be needed */
			break;
		case USER_PROCESS:
			/*
			 * If they came in on pts/..., then it is only
			 * a login session if the ut_host field is non-empty.
			 */
			if (strncmp(usr->ut_line, "pts/", 4) != 0 ||
			    *usr->ut_host != '\0')
				log_in(usr);
			break;
		case DEAD_PROCESS:
			log_out(usr);
			break;
		}
	}
	endutxent();
	(void)gettimeofday(&now, NULL);
	if (Flags & AC_W)
		usht.ut_tv = ut_timecopy;
	else
		usht.ut_tv = now;
	usht.ut_type = SHUTDOWN_TIME;

	if (Flags & AC_D) {
		ltm = localtime(&ut_timecopy.tv_sec);
		if (day >= 0 && day != ltm->tm_yday) {
			/*
			 * print yesterday's total
			 */
			secs = ut_timecopy;
			secs.tv_sec -= ltm->tm_sec;
			secs.tv_sec -= 60 * ltm->tm_min;
			secs.tv_sec -= 3600 * ltm->tm_hour;
			secs.tv_usec = 0;
			show_today(secs);
		}
	}
	/*
	 * anyone still logged in gets time up to now
	 */
	log_out(&usht);

	if (Flags & AC_D)
		show_today(now);
	else {
		if (Flags & AC_P)
			show_users();
		show("total", Total);
	}
}

static void
usage(void)
{
	(void)fprintf(stderr,
#ifdef CONSOLE_TTY
	    "ac [-dp] [-c console] [-t tty] [-w wtmp] [users ...]\n");
#else
	    "ac [-dp] [-t tty] [-w wtmp] [users ...]\n");
#endif
	exit(1);
}
