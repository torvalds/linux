/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * All rights reserved
 *
 * Distribute freely, except: don't remove my name from the source or
 * documentation (don't take credit for my work), mark your changes (don't
 * get me blamed for your possible bugs), don't alter or remove this
 * notice.  May be sold if buildable source is provided to buyer.  No
 * warrantee of any kind, express or implied, is included with this
 * software; use at your own risk, responsibility for damages (if any) to
 * anyone resulting from the use of this software rests entirely with the
 * user.
 *
 * Send bug reports, bug fixes, enhancements, requests, flames, etc., and
 * I'll try to keep a version up to date.  I can be reached as follows:
 * Paul Vixie          <paul@vix.com>          uunet!decwrl!vixie!paul
 */

#if !defined(lint) && !defined(LINT)
static const char rcsid[] =
  "$FreeBSD$";
#endif

#define	MAIN_PROGRAM


#include "cron.h"
#include <sys/mman.h>
#include <sys/signal.h>
#if SYS_TIME_H
# include <sys/time.h>
#else
# include <time.h>
#endif


static	void	usage(void),
		run_reboot_jobs(cron_db *),
		cron_tick(cron_db *, int),
		cron_sync(int),
		cron_sleep(cron_db *, int),
		cron_clean(cron_db *),
#ifdef USE_SIGCHLD
		sigchld_handler(int),
#endif
		sighup_handler(int),
		parse_args(int c, char *v[]);

static int	run_at_secres(cron_db *);
static void	find_interval_entry(pid_t);

static cron_db	database;
static time_t	last_time = 0;
static int	dst_enabled = 0;
static int	dont_daemonize = 0;
struct pidfh *pfh;

static void
usage() {
#if DEBUGGING
    char **dflags;
#endif

	fprintf(stderr, "usage: cron [-j jitter] [-J rootjitter] "
			"[-m mailto] [-n] [-s] [-o] [-x debugflag[,...]]\n");
#if DEBUGGING
	fprintf(stderr, "\ndebugflags: ");

        for(dflags = DebugFlagNames; *dflags; dflags++) {
		fprintf(stderr, "%s ", *dflags);
	}
        fprintf(stderr, "\n");
#endif

	exit(ERROR_EXIT);
}

static void
open_pidfile(void)
{
	char	pidfile[MAX_FNAME];
	char	buf[MAX_TEMPSTR];
	int	otherpid;

	(void) snprintf(pidfile, sizeof(pidfile), PIDFILE, PIDDIR);
	pfh = pidfile_open(pidfile, 0600, &otherpid);
	if (pfh == NULL) {
		if (errno == EEXIST) {
			snprintf(buf, sizeof(buf),
			    "cron already running, pid: %d", otherpid);
		} else {
			snprintf(buf, sizeof(buf),
			    "can't open or create %s: %s", pidfile,
			    strerror(errno));
		}
		log_it("CRON", getpid(), "DEATH", buf);
		errx(ERROR_EXIT, "%s", buf);
	}
}

int
main(argc, argv)
	int	argc;
	char	*argv[];
{
	int runnum;
	int secres1, secres2;
	struct tm *tm;

	ProgramName = argv[0];

#if defined(BSD)
	setlinebuf(stdout);
	setlinebuf(stderr);
#endif

	parse_args(argc, argv);

#ifdef USE_SIGCHLD
	(void) signal(SIGCHLD, sigchld_handler);
#else
	(void) signal(SIGCLD, SIG_IGN);
#endif
	(void) signal(SIGHUP, sighup_handler);

	open_pidfile();
	set_cron_uid();
	set_cron_cwd();

#if defined(POSIX)
	setenv("PATH", _PATH_DEFPATH, 1);
#endif

	/* if there are no debug flags turned on, fork as a daemon should.
	 */
# if DEBUGGING
	if (DebugFlags) {
# else
	if (0) {
# endif
		(void) fprintf(stderr, "[%d] cron started\n", getpid());
	} else if (dont_daemonize == 0) {
		if (daemon(1, 0) == -1) {
			pidfile_remove(pfh);
			log_it("CRON",getpid(),"DEATH","can't become daemon");
			exit(0);
		}
	}

	if (madvise(NULL, 0, MADV_PROTECT) != 0)
		log_it("CRON", getpid(), "WARNING", "madvise() failed");

	pidfile_write(pfh);
	database.head = NULL;
	database.tail = NULL;
	database.mtime = (time_t) 0;
	load_database(&database);
	secres1 = secres2 = run_at_secres(&database);
	cron_sync(secres1);
	run_reboot_jobs(&database);
	runnum = 0;
	while (TRUE) {
# if DEBUGGING
	    /* if (!(DebugFlags & DTEST)) */
# endif /*DEBUGGING*/
			cron_sleep(&database, secres1);

		if (secres1 == 0 || runnum % 60 == 0) {
			load_database(&database);
			secres2 = run_at_secres(&database);
			if (secres2 != secres1) {
				secres1 = secres2;
				if (secres1 != 0) {
					runnum = 0;
				} else {
					/*
					 * Going from 1 sec to 60 sec res. If we
					 * are already at minute's boundary, so
					 * let it run, otherwise schedule for the
					 * next minute.
					 */
					tm = localtime(&TargetTime);
					if (tm->tm_sec > 0)  {
						cron_sync(secres2);
						continue;
					}
				}
			}
		}

		/* do this iteration
		 */
		cron_tick(&database, secres1);

		/* sleep 1 or 60 seconds
		 */
		TargetTime += (secres1 != 0) ? 1 : 60;
		runnum += 1;
	}
}


static void
run_reboot_jobs(db)
	cron_db *db;
{
	register user		*u;
	register entry		*e;

	for (u = db->head;  u != NULL;  u = u->next) {
		for (e = u->crontab;  e != NULL;  e = e->next) {
			if (e->flags & WHEN_REBOOT) {
				job_add(e, u);
			}
			if (e->flags & INTERVAL) {
				e->lastexit = TargetTime;
			}
		}
	}
	(void) job_runqueue();
}


static void
cron_tick(cron_db *db, int secres)
{
	static struct tm	lasttm;
	static time_t	diff = 0, /* time difference in seconds from the last offset change */
		difflimit = 0; /* end point for the time zone correction */
	struct tm	otztm; /* time in the old time zone */
	int		otzsecond, otzminute, otzhour, otzdom, otzmonth, otzdow;
 	register struct tm	*tm = localtime(&TargetTime);
	register int		second, minute, hour, dom, month, dow;
	register user		*u;
	register entry		*e;

	/* make 0-based values out of these so we can use them as indices
	 */
	second = (secres == 0) ? 0 : tm->tm_sec -FIRST_SECOND;
	minute = tm->tm_min -FIRST_MINUTE;
	hour = tm->tm_hour -FIRST_HOUR;
	dom = tm->tm_mday -FIRST_DOM;
	month = tm->tm_mon +1 /* 0..11 -> 1..12 */ -FIRST_MONTH;
	dow = tm->tm_wday -FIRST_DOW;

	Debug(DSCH, ("[%d] tick(%d,%d,%d,%d,%d,%d)\n",
		getpid(), second, minute, hour, dom, month, dow))

	if (dst_enabled && last_time != 0 
	&& TargetTime > last_time /* exclude stepping back */
	&& tm->tm_gmtoff != lasttm.tm_gmtoff ) {

		diff = tm->tm_gmtoff - lasttm.tm_gmtoff;

		if ( diff > 0 ) { /* ST->DST */
			/* mark jobs for an earlier run */
			difflimit = TargetTime + diff;
			for (u = db->head;  u != NULL;  u = u->next) {
				for (e = u->crontab;  e != NULL;  e = e->next) {
					e->flags &= ~NOT_UNTIL;
					if ( e->lastrun >= TargetTime )
						e->lastrun = 0;
					/* not include the ends of hourly ranges */
					if ( e->lastrun < TargetTime - 3600 )
						e->flags |= RUN_AT;
					else
						e->flags &= ~RUN_AT;
				}
			}
		} else { /* diff < 0 : DST->ST */
			/* mark jobs for skipping */
			difflimit = TargetTime - diff;
			for (u = db->head;  u != NULL;  u = u->next) {
				for (e = u->crontab;  e != NULL;  e = e->next) {
					e->flags |= NOT_UNTIL;
					e->flags &= ~RUN_AT;
				}
			}
		}
	}

	if (diff != 0) {
		/* if the time was reset of the end of special zone is reached */
		if (last_time == 0 || TargetTime >= difflimit) {
			/* disable the TZ switch checks */
			diff = 0;
			difflimit = 0;
			for (u = db->head;  u != NULL;  u = u->next) {
				for (e = u->crontab;  e != NULL;  e = e->next) {
					e->flags &= ~(RUN_AT|NOT_UNTIL);
				}
			}
		} else {
			/* get the time in the old time zone */
			time_t difftime = TargetTime + tm->tm_gmtoff - diff;
			gmtime_r(&difftime, &otztm);

			/* make 0-based values out of these so we can use them as indices
			 */
			otzsecond = (secres == 0) ? 0 : otztm.tm_sec -FIRST_SECOND;
			otzminute = otztm.tm_min -FIRST_MINUTE;
			otzhour = otztm.tm_hour -FIRST_HOUR;
			otzdom = otztm.tm_mday -FIRST_DOM;
			otzmonth = otztm.tm_mon +1 /* 0..11 -> 1..12 */ -FIRST_MONTH;
			otzdow = otztm.tm_wday -FIRST_DOW;
		}
	}

	/* the dom/dow situation is odd.  '* * 1,15 * Sun' will run on the
	 * first and fifteenth AND every Sunday;  '* * * * Sun' will run *only*
	 * on Sundays;  '* * 1,15 * *' will run *only* the 1st and 15th.  this
	 * is why we keep 'e->dow_star' and 'e->dom_star'.  yes, it's bizarre.
	 * like many bizarre things, it's the standard.
	 */
	for (u = db->head;  u != NULL;  u = u->next) {
		for (e = u->crontab;  e != NULL;  e = e->next) {
			Debug(DSCH|DEXT, ("user [%s:%d:%d:...] cmd=\"%s\"\n",
					  env_get("LOGNAME", e->envp),
					  e->uid, e->gid, e->cmd))

			if (e->flags & INTERVAL) {
				if (e->lastexit > 0 &&
				    TargetTime >= e->lastexit + e->interval)
					job_add(e, u);
				continue;
			}

			if ( diff != 0 && (e->flags & (RUN_AT|NOT_UNTIL)) ) {
				if (bit_test(e->second, otzsecond)
				 && bit_test(e->minute, otzminute)
				 && bit_test(e->hour, otzhour)
				 && bit_test(e->month, otzmonth)
				 && ( ((e->flags & DOM_STAR) || (e->flags & DOW_STAR))
					  ? (bit_test(e->dow,otzdow) && bit_test(e->dom,otzdom))
					  : (bit_test(e->dow,otzdow) || bit_test(e->dom,otzdom))
					)
				   ) {
					if ( e->flags & RUN_AT ) {
						e->flags &= ~RUN_AT;
						e->lastrun = TargetTime;
						job_add(e, u);
						continue;
					} else 
						e->flags &= ~NOT_UNTIL;
				} else if ( e->flags & NOT_UNTIL )
					continue;
			}

			if (bit_test(e->second, second)
			 && bit_test(e->minute, minute)
			 && bit_test(e->hour, hour)
			 && bit_test(e->month, month)
			 && ( ((e->flags & DOM_STAR) || (e->flags & DOW_STAR))
			      ? (bit_test(e->dow,dow) && bit_test(e->dom,dom))
			      : (bit_test(e->dow,dow) || bit_test(e->dom,dom))
			    )
			   ) {
				e->flags &= ~RUN_AT;
				e->lastrun = TargetTime;
				job_add(e, u);
			}
		}
	}

	last_time = TargetTime;
	lasttm = *tm;
}


/* the task here is to figure out how long it's going to be until :00 of the
 * following minute and initialize TargetTime to this value.  TargetTime
 * will subsequently slide 60 seconds at a time, with correction applied
 * implicitly in cron_sleep().  it would be nice to let cron execute in
 * the "current minute" before going to sleep, but by restarting cron you
 * could then get it to execute a given minute's jobs more than once.
 * instead we have the chance of missing a minute's jobs completely, but
 * that's something sysadmin's know to expect what with crashing computers..
 */
static void
cron_sync(int secres) {
 	struct tm *tm;

	TargetTime = time((time_t*)0);
	if (secres != 0) {
		TargetTime += 1;
	} else {
		tm = localtime(&TargetTime);
		TargetTime += (60 - tm->tm_sec);
	}
}

static void
timespec_subtract(struct timespec *result, struct timespec *x,
    struct timespec *y)
{
	*result = *x;
	result->tv_sec -= y->tv_sec;
	result->tv_nsec -= y->tv_nsec;
	if (result->tv_nsec < 0) {
		result->tv_sec--;
		result->tv_nsec += 1000000000;
	}
}

static void
cron_sleep(cron_db *db, int secres)
{
	int seconds_to_wait;
	int rval;
	struct timespec ctime, ttime, stime, remtime;

	/*
	 * Loop until we reach the top of the next minute, sleep when possible.
	 */

	for (;;) {
		clock_gettime(CLOCK_REALTIME, &ctime);
		ttime.tv_sec = TargetTime;
		ttime.tv_nsec = 0;
		timespec_subtract(&stime, &ttime, &ctime);

		/*
		 * If the seconds_to_wait value is insane, jump the cron
		 */

		if (stime.tv_sec < -600 || stime.tv_sec > 600) {
			cron_clean(db);
			cron_sync(secres);
			continue;
		}

		seconds_to_wait = (stime.tv_nsec > 0) ? stime.tv_sec + 1 :
		    stime.tv_sec;

		Debug(DSCH, ("[%d] TargetTime=%ld, sec-to-wait=%d\n",
			getpid(), (long)TargetTime, seconds_to_wait))

		/*
		 * If we've run out of wait time or there are no jobs left
		 * to run, break
		 */

		if (stime.tv_sec < 0)
			break;
		if (job_runqueue() == 0) {
			Debug(DSCH, ("[%d] sleeping for %d seconds\n",
				getpid(), seconds_to_wait))

			for (;;) {
				rval = nanosleep(&stime, &remtime);
				if (rval == 0 || errno != EINTR)
					break;
				stime.tv_sec = remtime.tv_sec;
				stime.tv_nsec = remtime.tv_nsec;
			}
		}
	}
}


/* if the time was changed abruptly, clear the flags related
 * to the daylight time switch handling to avoid strange effects
 */

static void
cron_clean(db)
	cron_db	*db;
{
	user		*u;
	entry		*e;

	last_time = 0;

	for (u = db->head;  u != NULL;  u = u->next) {
		for (e = u->crontab;  e != NULL;  e = e->next) {
			e->flags &= ~(RUN_AT|NOT_UNTIL);
		}
	}
}

#ifdef USE_SIGCHLD
static void
sigchld_handler(int x)
{
	WAIT_T		waiter;
	PID_T		pid;

	for (;;) {
#ifdef POSIX
		pid = waitpid(-1, &waiter, WNOHANG);
#else
		pid = wait3(&waiter, WNOHANG, (struct rusage *)0);
#endif
		switch (pid) {
		case -1:
			Debug(DPROC,
				("[%d] sigchld...no children\n", getpid()))
			return;
		case 0:
			Debug(DPROC,
				("[%d] sigchld...no dead kids\n", getpid()))
			return;
		default:
			find_interval_entry(pid);
			Debug(DPROC,
				("[%d] sigchld...pid #%d died, stat=%d\n",
				getpid(), pid, WEXITSTATUS(waiter)))
		}
	}
}
#endif /*USE_SIGCHLD*/


static void
sighup_handler(int x)
{
	log_close();
}


static void
parse_args(argc, argv)
	int	argc;
	char	*argv[];
{
	int	argch;
	char	*endp;

	while ((argch = getopt(argc, argv, "j:J:m:nosx:")) != -1) {
		switch (argch) {
		case 'j':
			Jitter = strtoul(optarg, &endp, 10);
			if (*optarg == '\0' || *endp != '\0' || Jitter > 60)
				errx(ERROR_EXIT,
				     "bad value for jitter: %s", optarg);
			break;
		case 'J':
			RootJitter = strtoul(optarg, &endp, 10);
			if (*optarg == '\0' || *endp != '\0' || RootJitter > 60)
				errx(ERROR_EXIT,
				     "bad value for root jitter: %s", optarg);
			break;
		case 'm':
			defmailto = optarg;
			break;
		case 'n':
			dont_daemonize = 1;
			break;
		case 'o':
			dst_enabled = 0;
			break;
		case 's':
			dst_enabled = 1;
			break;
		case 'x':
			if (!set_debug_flags(optarg))
				usage();
			break;
		default:
			usage();
		}
	}
}

static int
run_at_secres(cron_db *db)
{
	user *u;
	entry *e;

	for (u = db->head;  u != NULL;  u = u->next) {
		for (e = u->crontab;  e != NULL;  e = e->next) {
			if ((e->flags & (SEC_RES | INTERVAL)) != 0)
				return 1;
		}
	}
	return 0;
}

static void
find_interval_entry(pid_t pid)
{
	user *u;
	entry *e;

	for (u = database.head;  u != NULL;  u = u->next) {
		for (e = u->crontab;  e != NULL;  e = e->next) {
			if ((e->flags & INTERVAL) && e->child == pid) {
				e->lastexit = time(NULL);
				e->child = 0;
				break;
			}
		}
	}
}
