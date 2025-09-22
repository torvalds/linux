/*-
 * Copyright (c) 1994 Christopher G. Demetriou
 * Copyright (c) 1994 Simon J. Gerraty
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

#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utmp.h>
#include <unistd.h>

/*
 * this is for our list of currently logged in sessions
 */
struct utmp_list {
	struct utmp_list *next;
	struct utmp usr;
};

/*
 * this is for our list of users that are accumulating time.
 */
struct user_list {
	struct user_list *next;
	char	name[UT_NAMESIZE+1];
	time_t	secs;
};

/*
 * this is for choosing whether to ignore a login
 */
struct tty_list {
	struct tty_list *next;
	char	name[UT_LINESIZE+3];
	size_t	len;
	int	ret;
};

/*
 * globals - yes yuk
 */
static time_t	Total = 0;
static time_t	FirstTime = 0;
static int	Flags = 0;
static struct user_list *Users = NULL;
static struct tty_list *Ttys = NULL;

#define	AC_W	1				/* not _PATH_WTMP */
#define	AC_D	2				/* daily totals (ignore -p) */
#define	AC_P	4				/* per-user totals */
#define	AC_U	8				/* specified users only */
#define	AC_T	16				/* specified ttys only */

#ifdef DEBUG
static int Debug = 0;
#endif

int			main(int, char **);
int			ac(FILE *);
void			add_tty(char *);
int			do_tty(char *);
FILE			*file(char *);
struct utmp_list	*log_in(struct utmp_list *, struct utmp *);
struct utmp_list	*log_out(struct utmp_list *, struct utmp *);
void			show(char *, time_t);
void			show_today(struct user_list *, struct utmp_list *,
			    time_t);
void			show_users(struct user_list *);
struct user_list	*update_user(struct user_list *, char *, time_t);
void			usage(void);

/*
 * open wtmp or die
 */
FILE *
file(char *name)
{
	FILE *fp;

	if (strcmp(name, "-") == 0)
		fp = stdin;
	else if ((fp = fopen(name, "r")) == NULL)
		err(1, "%s", name);
	/* in case we want to discriminate */
	if (strcmp(_PATH_WTMP, name))
		Flags |= AC_W;
	return fp;
}

void
add_tty(char *name)
{
	struct tty_list *tp;
	char *rcp;

	Flags |= AC_T;

	if ((tp = malloc(sizeof(struct tty_list))) == NULL)
		err(1, "malloc");
	tp->len = 0;				/* full match */
	tp->ret = 1;				/* do if match */
	if (*name == '!') {			/* don't do if match */
		tp->ret = 0;
		name++;
	}
	strlcpy(tp->name, name, sizeof (tp->name));
	if ((rcp = strchr(tp->name, '*')) != NULL) {	/* wild card */
		*rcp = '\0';
		tp->len = strlen(tp->name);	/* match len bytes only */
	}
	tp->next = Ttys;
	Ttys = tp;
}

/*
 * should we process the named tty?
 */
int
do_tty(char *name)
{
	struct tty_list *tp;
	int def_ret = 0;

	for (tp = Ttys; tp != NULL; tp = tp->next) {
		if (tp->ret == 0)		/* specific don't */
			def_ret = 1;		/* default do */
		if (tp->len != 0) {
			if (strncmp(name, tp->name, tp->len) == 0)
				return tp->ret;
		} else {
			if (strncmp(name, tp->name, sizeof (tp->name)) == 0)
				return tp->ret;
		}
	}
	return def_ret;
}

/*
 * update user's login time
 */
struct user_list *
update_user(struct user_list *head, char *name, time_t secs)
{
	struct user_list *up;

	for (up = head; up != NULL; up = up->next) {
		if (strncmp(up->name, name, sizeof (up->name) - 1) == 0) {
			up->secs += secs;
			Total += secs;
			return head;
		}
	}
	/*
	 * not found so add new user unless specified users only
	 */
	if (Flags & AC_U)
		return head;

	if ((up = malloc(sizeof(struct user_list))) == NULL)
		err(1, "malloc");
	up->next = head;
	strncpy(up->name, name, sizeof(up->name) - 1);
	up->name[sizeof(up->name) - 1] = '\0';
	up->secs = secs;
	Total += secs;
	return up;
}

int
main(int argc, char *argv[])
{
	FILE *fp;
	int c;

	if (pledge("stdio rpath", NULL) == -1)
		err(1, "pledge");

	fp = NULL;
	while ((c = getopt(argc, argv, "Ddpt:w:")) != -1) {
		switch (c) {
#ifdef DEBUG
		case 'D':
			Debug = 1;
			break;
#endif
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
			fp = file(optarg);
			break;
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
			Users = update_user(Users, argv[optind], 0L);
		}
		Flags |= AC_U;			/* freeze user list */
	}
	if (Flags & AC_D)
		Flags &= ~AC_P;
	if (fp == NULL) {
		/*
		 * if _PATH_WTMP does not exist, exit quietly
		 */
		if (access(_PATH_WTMP, 0) != 0 && errno == ENOENT)
			return 0;

		fp = file(_PATH_WTMP);
	}
	ac(fp);

	return 0;
}

/*
 * print login time in decimal hours
 */
void
show(char *name, time_t secs)
{
	(void)printf("\t%-*s %8.2f\n", UT_NAMESIZE, name,
	    ((double)secs / 3600));
}

void
show_users(struct user_list *list)
{
	struct user_list *lp;

	for (lp = list; lp; lp = lp->next)
		show(lp->name, lp->secs);
}

/*
 * print total login time for 24hr period in decimal hours
 */
void
show_today(struct user_list *users, struct utmp_list *logins, time_t secs)
{
	struct user_list *up;
	struct utmp_list *lp;
	char date[64];
	time_t yesterday = secs - 1;

	(void)strftime(date, sizeof (date), "%b %e  total",
	    localtime(&yesterday));

	/* restore the missing second */
	yesterday++;

	for (lp = logins; lp != NULL; lp = lp->next) {
		secs = yesterday - lp->usr.ut_time;
		Users = update_user(Users, lp->usr.ut_name, secs);
		lp->usr.ut_time = yesterday;	/* as if they just logged in */
	}
	secs = 0;
	for (up = users; up != NULL; up = up->next) {
		secs += up->secs;
		up->secs = 0;			/* for next day */
	}
	if (secs)
		(void)printf("%s %11.2f\n", date, ((double)secs / 3600));
}

/*
 * log a user out and update their times.
 * if ut_line is "~", we log all users out as the system has
 * been shut down.
 */
struct utmp_list *
log_out(struct utmp_list *head, struct utmp *up)
{
	struct utmp_list *lp, *lp2, *tlp;
	time_t secs;

	for (lp = head, lp2 = NULL; lp != NULL; )
		if (*up->ut_line == '~' || strncmp(lp->usr.ut_line, up->ut_line,
		    sizeof (up->ut_line)) == 0) {
			secs = up->ut_time - lp->usr.ut_time;
			Users = update_user(Users, lp->usr.ut_name, secs);
#ifdef DEBUG
			if (Debug)
				printf("%-.*s %-.*s: %-.*s logged out (%2d:%02d:%02d)\n",
				    19, ctime(&up->ut_time),
				    sizeof (lp->usr.ut_line), lp->usr.ut_line,
				    sizeof (lp->usr.ut_name), lp->usr.ut_name,
				    secs / 3600, (secs % 3600) / 60, secs % 60);
#endif
			/*
			 * now lose it
			 */
			tlp = lp;
			lp = lp->next;
			if (tlp == head)
				head = lp;
			else if (lp2 != NULL)
				lp2->next = lp;
			free(tlp);
		} else {
			lp2 = lp;
			lp = lp->next;
		}
	return head;
}


/*
 * if do_tty says ok, login a user
 */
struct utmp_list *
log_in(struct utmp_list *head, struct utmp *up)
{
	struct utmp_list *lp;

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

	/*
	 * If we are doing specified ttys only, we ignore
	 * anything else.
	 */
	if (Flags & AC_T)
		if (!do_tty(up->ut_line))
			return head;

	/*
	 * go ahead and log them in
	 */
	if ((lp = malloc(sizeof(struct utmp_list))) == NULL)
		err(1, "malloc");
	lp->next = head;
	head = lp;
	memmove((char *)&lp->usr, (char *)up, sizeof (struct utmp));
#ifdef DEBUG
	if (Debug) {
		printf("%-.*s %-.*s: %-.*s logged in", 19,
		    ctime(&lp->usr.ut_time), sizeof (up->ut_line),
		    up->ut_line, sizeof (up->ut_name), up->ut_name);
		if (*up->ut_host)
			printf(" (%-.*s)", sizeof (up->ut_host), up->ut_host);
		putchar('\n');
	}
#endif
	return head;
}

int
ac(FILE	*fp)
{
	struct utmp_list *lp, *head = NULL;
	struct utmp usr;
	struct tm *ltm;
	time_t secs = 0, prev = 0;
	int day = -1;

	while (fread((char *)&usr, sizeof(usr), 1, fp) == 1) {
		if (!FirstTime)
			FirstTime = usr.ut_time;
		if (usr.ut_time < prev)
			continue;	/* broken record */
		prev = usr.ut_time;
		if (Flags & AC_D) {
			ltm = localtime(&usr.ut_time);
			if (ltm == NULL)
				err(1, "localtime");
			if (day >= 0 && day != ltm->tm_yday) {
				day = ltm->tm_yday;
				/*
				 * print yesterday's total
				 */
				secs = usr.ut_time;
				secs -= ltm->tm_sec;
				secs -= 60 * ltm->tm_min;
				secs -= 3600 * ltm->tm_hour;
				show_today(Users, head, secs);
			} else
				day = ltm->tm_yday;
		}
		switch(*usr.ut_line) {
		case '|':
			secs = usr.ut_time;
			break;
		case '{':
			secs -= usr.ut_time;
			/*
			 * adjust time for those logged in
			 */
			for (lp = head; lp != NULL; lp = lp->next)
				lp->usr.ut_time -= secs;
			break;
		case '~':			/* reboot or shutdown */
			head = log_out(head, &usr);
			FirstTime = usr.ut_time; /* shouldn't be needed */
			break;
		default:
			/*
			 * if they came in on a pseudo-tty, then it is only
			 * a login session if the ut_host field is non-empty
			 */
			if (*usr.ut_name) {
				if (strncmp(usr.ut_line, "tty", 3) != 0 ||
				    strchr("pqrstuvwxyzPQRST", usr.ut_line[3]) != NULL ||
				    *usr.ut_host != '\0')
					head = log_in(head, &usr);
			} else
				head = log_out(head, &usr);
			break;
		}
	}
	(void)fclose(fp);
	if (!(Flags & AC_W))
		usr.ut_time = time(NULL);
	(void)strlcpy(usr.ut_line, "~", sizeof usr.ut_line);

	if (Flags & AC_D) {
		ltm = localtime(&usr.ut_time);
		if (ltm == NULL)
			err(1, "localtime");
		if (day >= 0 && day != ltm->tm_yday) {
			/*
			 * print yesterday's total
			 */
			secs = usr.ut_time;
			secs -= ltm->tm_sec;
			secs -= 60 * ltm->tm_min;
			secs -= 3600 * ltm->tm_hour;
			show_today(Users, head, secs);
		}
	}
	/*
	 * anyone still logged in gets time up to now
	 */
	head = log_out(head, &usr);

	if (Flags & AC_D)
		show_today(Users, head, time(NULL));
	else {
		if (Flags & AC_P)
			show_users(Users);
		show("total", Total);
	}
	return 0;
}

void
usage(void)
{
	extern char *__progname;
	(void)fprintf(stderr, "usage: "
	    "%s [-dp] [-t tty] [-w wtmp] [user ...]\n", __progname);
	exit(1);
}
