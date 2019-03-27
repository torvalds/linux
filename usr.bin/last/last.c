/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1987, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2018 Philip Paeps
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
"@(#) Copyright (c) 1987, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static const char sccsid[] = "@(#)last.c	8.2 (Berkeley) 4/2/94";
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <capsicum_helpers.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <locale.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <timeconv.h>
#include <unistd.h>
#include <utmpx.h>

#include <libxo/xo.h>

#define	NO	0				/* false/no */
#define	YES	1				/* true/yes */
#define	ATOI2(ar)	((ar)[0] - '0') * 10 + ((ar)[1] - '0'); (ar) += 2;

typedef struct arg {
	char	*name;				/* argument */
#define	REBOOT_TYPE	-1
#define	HOST_TYPE	-2
#define	TTY_TYPE	-3
#define	USER_TYPE	-4
	int	type;				/* type of arg */
	struct arg	*next;			/* linked list pointer */
} ARG;
static ARG	*arglist;			/* head of linked list */

static SLIST_HEAD(, idtab) idlist;

struct idtab {
	time_t	logout;				/* log out time */
	char	id[sizeof ((struct utmpx *)0)->ut_id]; /* identifier */
	SLIST_ENTRY(idtab) list;
};

static const	char *crmsg;			/* cause of last reboot */
static time_t	currentout;			/* current logout value */
static long	maxrec;				/* records to display */
static const	char *file = NULL;		/* utx.log file */
static int	sflag = 0;			/* show delta in seconds */
static int	width = 5;			/* show seconds in delta */
static int	yflag;				/* show year */
static int      d_first;
static int	snapfound = 0;			/* found snapshot entry? */
static time_t	snaptime;			/* if != 0, we will only
						 * report users logged in
						 * at this snapshot time
						 */

static void	 addarg(int, char *);
static time_t	 dateconv(char *);
static void	 doentry(struct utmpx *);
static void	 hostconv(char *);
static void	 printentry(struct utmpx *, struct idtab *);
static char	*ttyconv(char *);
static int	 want(struct utmpx *);
static void	 usage(void);
static void	 wtmp(void);

static void
usage(void)
{
	xo_error(
"usage: last [-swy] [-d [[CC]YY][MMDD]hhmm[.SS]] [-f file] [-h host]\n"
"            [-n maxrec] [-t tty] [user ...]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int ch;
	char *p;

	(void) setlocale(LC_TIME, "");
	d_first = (*nl_langinfo(D_MD_ORDER) == 'd');

	argc = xo_parse_args(argc, argv);
	if (argc < 0)
		exit(1);
	atexit(xo_finish_atexit);

	maxrec = -1;
	snaptime = 0;
	while ((ch = getopt(argc, argv, "0123456789d:f:h:n:st:wy")) != -1)
		switch (ch) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			/*
			 * kludge: last was originally designed to take
			 * a number after a dash.
			 */
			if (maxrec == -1) {
				p = strchr(argv[optind - 1], ch);
				if (p == NULL)
					p = strchr(argv[optind], ch);
				maxrec = atol(p);
				if (!maxrec)
					exit(0);
			}
			break;
		case 'd':
			snaptime = dateconv(optarg);
			break;
		case 'f':
			file = optarg;
			break;
		case 'h':
			hostconv(optarg);
			addarg(HOST_TYPE, optarg);
			break;
		case 'n':
			errno = 0;
			maxrec = strtol(optarg, &p, 10);
			if (p == optarg || *p != '\0' || errno != 0 ||
			    maxrec <= 0)
				xo_errx(1, "%s: bad line count", optarg);
			break;
		case 's':
			sflag++;	/* Show delta as seconds */
			break;
		case 't':
			addarg(TTY_TYPE, ttyconv(optarg));
			break;
		case 'w':
			width = 8;
			break;
		case 'y':
			yflag++;
			break;
		case '?':
		default:
			usage();
		}

	if (caph_limit_stdio() < 0)
		xo_err(1, "can't limit stdio rights");

	caph_cache_catpages();
	caph_cache_tzdata();

	/* Cache UTX database. */
	if (setutxdb(UTXDB_LOG, file) != 0)
		xo_err(1, "%s", file != NULL ? file : "(default utx db)");

	if (caph_enter() < 0)
		xo_err(1, "cap_enter");

	if (sflag && width == 8) usage();

	if (argc) {
		setlinebuf(stdout);
		for (argv += optind; *argv; ++argv) {
			if (strcmp(*argv, "reboot") == 0)
				addarg(REBOOT_TYPE, *argv);
#define	COMPATIBILITY
#ifdef	COMPATIBILITY
			/* code to allow "last p5" to work */
			addarg(TTY_TYPE, ttyconv(*argv));
#endif
			addarg(USER_TYPE, *argv);
		}
	}
	wtmp();
	exit(0);
}

/*
 * wtmp --
 *	read through the utx.log file
 */
static void
wtmp(void)
{
	struct utmpx *buf = NULL;
	struct utmpx *ut;
	static unsigned int amount = 0;
	time_t t;
	char ct[80];
	struct tm *tm;

	SLIST_INIT(&idlist);
	(void)time(&t);

	xo_open_container("last-information");

	/* Load the last entries from the file. */
	while ((ut = getutxent()) != NULL) {
		if (amount % 128 == 0) {
			buf = realloc(buf, (amount + 128) * sizeof *ut);
			if (buf == NULL)
				xo_err(1, "realloc");
		}
		memcpy(&buf[amount++], ut, sizeof *ut);
		if (t > ut->ut_tv.tv_sec)
			t = ut->ut_tv.tv_sec;
	}
	endutxent();

	/* Display them in reverse order. */
	xo_open_list("last");
	while (amount > 0)
		doentry(&buf[--amount]);
	xo_close_list("last");
	free(buf);
	tm = localtime(&t);
	(void) strftime(ct, sizeof(ct), "%+", tm);
	xo_emit("\n{:utxdb/%s}", (file == NULL) ? "utx.log" : file);
	xo_attr("seconds", "%lu", (unsigned long) t);
	xo_emit(" begins {:begins/%s}\n", ct);
	xo_close_container("last-information");
}

/*
 * doentry --
 *	process a single utx.log entry
 */
static void
doentry(struct utmpx *bp)
{
	struct idtab *tt;

	/* the machine stopped */
	if (bp->ut_type == BOOT_TIME || bp->ut_type == SHUTDOWN_TIME) {
		/* everybody just logged out */
		while ((tt = SLIST_FIRST(&idlist)) != NULL) {
			SLIST_REMOVE_HEAD(&idlist, list);
			free(tt);
		}
		currentout = -bp->ut_tv.tv_sec;
		crmsg = bp->ut_type != SHUTDOWN_TIME ?
		    "crash" : "shutdown";
		/*
		 * if we're in snapshot mode, we want to exit if this
		 * shutdown/reboot appears while we we are tracking the
		 * active range
		 */
		if (snaptime && snapfound)
			exit(0);
		/*
		 * don't print shutdown/reboot entries unless flagged for
		 */
		if (!snaptime && want(bp))
			printentry(bp, NULL);
		return;
	}
	/* date got set */
	if (bp->ut_type == OLD_TIME || bp->ut_type == NEW_TIME) {
		if (want(bp) && !snaptime)
			printentry(bp, NULL);
		return;
	}

	if (bp->ut_type != USER_PROCESS && bp->ut_type != DEAD_PROCESS)
		return;

	/* find associated identifier */
	SLIST_FOREACH(tt, &idlist, list)
	    if (!memcmp(tt->id, bp->ut_id, sizeof bp->ut_id))
		    break;

	if (tt == NULL) {
		/* add new one */
		tt = malloc(sizeof(struct idtab));
		if (tt == NULL)
			xo_errx(1, "malloc failure");
		tt->logout = currentout;
		memcpy(tt->id, bp->ut_id, sizeof bp->ut_id);
		SLIST_INSERT_HEAD(&idlist, tt, list);
	}

	/*
	 * print record if not in snapshot mode and wanted
	 * or in snapshot mode and in snapshot range
	 */
	if (bp->ut_type == USER_PROCESS && (want(bp) ||
	    (bp->ut_tv.tv_sec < snaptime &&
	    (tt->logout > snaptime || tt->logout < 1)))) {
		snapfound = 1;
		printentry(bp, tt);
	}
	tt->logout = bp->ut_tv.tv_sec;
}

/*
 * printentry --
 *	output an entry
 *
 * If `tt' is non-NULL, use it and `crmsg' to print the logout time or
 * logout type (crash/shutdown) as appropriate.
 */
static void
printentry(struct utmpx *bp, struct idtab *tt)
{
	char ct[80];
	struct tm *tm;
	time_t	delta;				/* time difference */
	time_t	t;

	if (maxrec != -1 && !maxrec--)
		exit(0);
	xo_open_instance("last");
	t = bp->ut_tv.tv_sec;
	tm = localtime(&t);
	(void) strftime(ct, sizeof(ct), d_first ?
	    (yflag ? "%a %e %b %Y %R" : "%a %e %b %R") :
	    (yflag ? "%a %b %e %Y %R" : "%a %b %e %R"), tm);
	switch (bp->ut_type) {
	case BOOT_TIME:
		xo_emit("{:user/%-42s/%s}", "boot time");
		break;
	case SHUTDOWN_TIME:
		xo_emit("{:user/%-42s/%s}", "shutdown time");
		break;
	case OLD_TIME:
		xo_emit("{:user/%-42s/%s}", "old time");
		break;
	case NEW_TIME:
		xo_emit("{:user/%-42s/%s}", "new time");
		break;
	case USER_PROCESS:
		xo_emit("{:user/%-10s/%s} {:tty/%-8s/%s} {:from/%-22.22s/%s}",
		    bp->ut_user, bp->ut_line, bp->ut_host);
		break;
	}
	xo_attr("seconds", "%lu", (unsigned long)t);
	xo_emit(" {:login-time/%s%c/%s}", ct, tt == NULL ? '\n' : ' ');
	if (tt == NULL)
		goto end;
	if (!tt->logout) {
		xo_emit("  {:logout-time/still logged in}\n");
		goto end;
	}
	if (tt->logout < 0) {
		tt->logout = -tt->logout;
		xo_emit("- {:logout-reason/%s}", crmsg);
	} else {
		tm = localtime(&tt->logout);
		(void) strftime(ct, sizeof(ct), "%R", tm);
		xo_attr("seconds", "%lu", (unsigned long)tt->logout);
		xo_emit("- {:logout-time/%s}", ct);
	}
	delta = tt->logout - bp->ut_tv.tv_sec;
	xo_attr("seconds", "%ld", (long)delta);
	if (sflag) {
		xo_emit("  ({:session-length/%8ld})\n", (long)delta);
	} else {
		tm = gmtime(&delta);
		(void) strftime(ct, sizeof(ct), width >= 8 ? "%T" : "%R", tm);
		if (delta < 86400)
			xo_emit("  ({:session-length/%s})\n", ct);
		else
			xo_emit(" ({:session-length/%ld+%s})\n",
			    (long)delta / 86400, ct);
	}

end:
	xo_close_instance("last");
}

/*
 * want --
 *	see if want this entry
 */
static int
want(struct utmpx *bp)
{
	ARG *step;

	if (snaptime)
		return (NO);

	if (!arglist)
		return (YES);

	for (step = arglist; step; step = step->next)
		switch(step->type) {
		case REBOOT_TYPE:
			if (bp->ut_type == BOOT_TIME ||
			    bp->ut_type == SHUTDOWN_TIME)
				return (YES);
			break;
		case HOST_TYPE:
			if (!strcasecmp(step->name, bp->ut_host))
				return (YES);
			break;
		case TTY_TYPE:
			if (!strcmp(step->name, bp->ut_line))
				return (YES);
			break;
		case USER_TYPE:
			if (!strcmp(step->name, bp->ut_user))
				return (YES);
			break;
		}
	return (NO);
}

/*
 * addarg --
 *	add an entry to a linked list of arguments
 */
static void
addarg(int type, char *arg)
{
	ARG *cur;

	if ((cur = malloc(sizeof(ARG))) == NULL)
		xo_errx(1, "malloc failure");
	cur->next = arglist;
	cur->type = type;
	cur->name = arg;
	arglist = cur;
}

/*
 * hostconv --
 *	convert the hostname to search pattern; if the supplied host name
 *	has a domain attached that is the same as the current domain, rip
 *	off the domain suffix since that's what login(1) does.
 */
static void
hostconv(char *arg)
{
	static int first = 1;
	static char *hostdot, name[MAXHOSTNAMELEN];
	char *argdot;

	if (!(argdot = strchr(arg, '.')))
		return;
	if (first) {
		first = 0;
		if (gethostname(name, sizeof(name)))
			xo_err(1, "gethostname");
		hostdot = strchr(name, '.');
	}
	if (hostdot && !strcasecmp(hostdot, argdot))
		*argdot = '\0';
}

/*
 * ttyconv --
 *	convert tty to correct name.
 */
static char *
ttyconv(char *arg)
{
	char *mval;

	/*
	 * kludge -- we assume that all tty's end with
	 * a two character suffix.
	 */
	if (strlen(arg) == 2) {
		/* either 6 for "ttyxx" or 8 for "console" */
		if ((mval = malloc(8)) == NULL)
			xo_errx(1, "malloc failure");
		if (!strcmp(arg, "co"))
			(void)strcpy(mval, "console");
		else {
			(void)strcpy(mval, "tty");
			(void)strcpy(mval + 3, arg);
		}
		return (mval);
	}
	if (!strncmp(arg, _PATH_DEV, sizeof(_PATH_DEV) - 1))
		return (arg + 5);
	return (arg);
}

/*
 * dateconv --
 * 	Convert the snapshot time in command line given in the format
 * 	[[CC]YY]MMDDhhmm[.SS]] to a time_t.
 * 	Derived from atime_arg1() in usr.bin/touch/touch.c
 */
static time_t
dateconv(char *arg)
{
        time_t timet;
        struct tm *t;
        int yearset;
        char *p;

        /* Start with the current time. */
        if (time(&timet) < 0)
                xo_err(1, "time");
        if ((t = localtime(&timet)) == NULL)
                xo_err(1, "localtime");

        /* [[CC]YY]MMDDhhmm[.SS] */
        if ((p = strchr(arg, '.')) == NULL)
                t->tm_sec = 0; 		/* Seconds defaults to 0. */
        else {
                if (strlen(p + 1) != 2)
                        goto terr;
                *p++ = '\0';
                t->tm_sec = ATOI2(p);
        }

        yearset = 0;
        switch (strlen(arg)) {
        case 12:                	/* CCYYMMDDhhmm */
                t->tm_year = ATOI2(arg);
                t->tm_year *= 100;
                yearset = 1;
                /* FALLTHROUGH */
        case 10:                	/* YYMMDDhhmm */
                if (yearset) {
                        yearset = ATOI2(arg);
                        t->tm_year += yearset;
                } else {
                        yearset = ATOI2(arg);
                        if (yearset < 69)
                                t->tm_year = yearset + 2000;
                        else
                                t->tm_year = yearset + 1900;
                }
                t->tm_year -= 1900;     /* Convert to UNIX time. */
                /* FALLTHROUGH */
        case 8:				/* MMDDhhmm */
                t->tm_mon = ATOI2(arg);
                --t->tm_mon;    	/* Convert from 01-12 to 00-11 */
                t->tm_mday = ATOI2(arg);
                t->tm_hour = ATOI2(arg);
                t->tm_min = ATOI2(arg);
                break;
        case 4:				/* hhmm */
                t->tm_hour = ATOI2(arg);
                t->tm_min = ATOI2(arg);
                break;
        default:
                goto terr;
        }
        t->tm_isdst = -1;       	/* Figure out DST. */
        timet = mktime(t);
        if (timet == -1)
terr:           xo_errx(1,
        "out of range or illegal time specification: [[CC]YY]MMDDhhmm[.SS]");
        return timet;
}
