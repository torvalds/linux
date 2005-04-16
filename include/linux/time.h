#ifndef _LINUX_TIME_H
#define _LINUX_TIME_H

#include <linux/types.h>

#ifdef __KERNEL__
#include <linux/seqlock.h>
#endif

#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {
	time_t	tv_sec;		/* seconds */
	long	tv_nsec;	/* nanoseconds */
};
#endif /* _STRUCT_TIMESPEC */

struct timeval {
	time_t		tv_sec;		/* seconds */
	suseconds_t	tv_usec;	/* microseconds */
};

struct timezone {
	int	tz_minuteswest;	/* minutes west of Greenwich */
	int	tz_dsttime;	/* type of dst correction */
};

#ifdef __KERNEL__

/* Parameters used to convert the timespec values */
#ifndef USEC_PER_SEC
#define USEC_PER_SEC (1000000L)
#endif

#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC (1000000000L)
#endif

#ifndef NSEC_PER_USEC
#define NSEC_PER_USEC (1000L)
#endif

static __inline__ int timespec_equal(struct timespec *a, struct timespec *b) 
{ 
	return (a->tv_sec == b->tv_sec) && (a->tv_nsec == b->tv_nsec);
} 

/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines were long is 32-bit! (However, as time_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)
 */
static inline unsigned long
mktime (unsigned int year, unsigned int mon,
	unsigned int day, unsigned int hour,
	unsigned int min, unsigned int sec)
{
	if (0 >= (int) (mon -= 2)) {	/* 1..12 -> 11,12,1..10 */
		mon += 12;		/* Puts Feb last since it has leap day */
		year -= 1;
	}

	return (((
		(unsigned long) (year/4 - year/100 + year/400 + 367*mon/12 + day) +
			year*365 - 719499
	    )*24 + hour /* now have hours */
	  )*60 + min /* now have minutes */
	)*60 + sec; /* finally seconds */
}

extern struct timespec xtime;
extern struct timespec wall_to_monotonic;
extern seqlock_t xtime_lock;

static inline unsigned long get_seconds(void)
{ 
	return xtime.tv_sec;
}

struct timespec current_kernel_time(void);

#define CURRENT_TIME (current_kernel_time())
#define CURRENT_TIME_SEC ((struct timespec) { xtime.tv_sec, 0 })

extern void do_gettimeofday(struct timeval *tv);
extern int do_settimeofday(struct timespec *tv);
extern int do_sys_settimeofday(struct timespec *tv, struct timezone *tz);
extern void clock_was_set(void); // call when ever the clock is set
extern int do_posix_clock_monotonic_gettime(struct timespec *tp);
extern long do_nanosleep(struct timespec *t);
extern long do_utimes(char __user * filename, struct timeval * times);
struct itimerval;
extern int do_setitimer(int which, struct itimerval *value, struct itimerval *ovalue);
extern int do_getitimer(int which, struct itimerval *value);
extern void getnstimeofday (struct timespec *tv);

extern struct timespec timespec_trunc(struct timespec t, unsigned gran);

static inline void
set_normalized_timespec (struct timespec *ts, time_t sec, long nsec)
{
	while (nsec > NSEC_PER_SEC) {
		nsec -= NSEC_PER_SEC;
		++sec;
	}
	while (nsec < 0) {
		nsec += NSEC_PER_SEC;
		--sec;
	}
	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}

#endif /* __KERNEL__ */

#define NFDBITS			__NFDBITS

#define FD_SETSIZE		__FD_SETSIZE
#define FD_SET(fd,fdsetp)	__FD_SET(fd,fdsetp)
#define FD_CLR(fd,fdsetp)	__FD_CLR(fd,fdsetp)
#define FD_ISSET(fd,fdsetp)	__FD_ISSET(fd,fdsetp)
#define FD_ZERO(fdsetp)		__FD_ZERO(fdsetp)

/*
 * Names of the interval timers, and structure
 * defining a timer setting.
 */
#define	ITIMER_REAL	0
#define	ITIMER_VIRTUAL	1
#define	ITIMER_PROF	2

struct  itimerspec {
        struct  timespec it_interval;    /* timer period */
        struct  timespec it_value;       /* timer expiration */
};

struct	itimerval {
	struct	timeval it_interval;	/* timer interval */
	struct	timeval it_value;	/* current value */
};


/*
 * The IDs of the various system clocks (for POSIX.1b interval timers).
 */
#define CLOCK_REALTIME		  0
#define CLOCK_MONOTONIC	  1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID	 3
#define CLOCK_REALTIME_HR	 4
#define CLOCK_MONOTONIC_HR	  5

/*
 * The IDs of various hardware clocks
 */


#define CLOCK_SGI_CYCLE 10
#define MAX_CLOCKS 16
#define CLOCKS_MASK  (CLOCK_REALTIME | CLOCK_MONOTONIC | \
                     CLOCK_REALTIME_HR | CLOCK_MONOTONIC_HR)
#define CLOCKS_MONO (CLOCK_MONOTONIC & CLOCK_MONOTONIC_HR)

/*
 * The various flags for setting POSIX.1b interval timers.
 */

#define TIMER_ABSTIME 0x01


#endif
