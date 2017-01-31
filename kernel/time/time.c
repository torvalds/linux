/*
 *  linux/kernel/time.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  This file contains the interface functions for the various
 *  time related system calls: time, stime, gettimeofday, settimeofday,
 *			       adjtime
 */
/*
 * Modification history kernel/time.c
 *
 * 1993-09-02    Philip Gladstone
 *      Created file with time related functions from sched/core.c and adjtimex()
 * 1993-10-08    Torsten Duwe
 *      adjtime interface update and CMOS clock write code
 * 1995-08-13    Torsten Duwe
 *      kernel PLL updated to 1994-12-13 specs (rfc-1589)
 * 1999-01-16    Ulrich Windl
 *	Introduced error checking for many cases in adjtimex().
 *	Updated NTP code according to technical memorandum Jan '96
 *	"A Kernel Model for Precision Timekeeping" by Dave Mills
 *	Allow time_constant larger than MAXTC(6) for NTP v4 (MAXTC == 10)
 *	(Even though the technical memorandum forbids it)
 * 2004-07-14	 Christoph Lameter
 *	Added getnstimeofday to allow the posix timer functions to return
 *	with nanosecond accuracy
 */

#include <linux/export.h>
#include <linux/timex.h>
#include <linux/capability.h>
#include <linux/timekeeper_internal.h>
#include <linux/errno.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/fs.h>
#include <linux/math64.h>
#include <linux/ptrace.h>

#include <linux/uaccess.h>
#include <asm/unistd.h>

#include <generated/timeconst.h>
#include "timekeeping.h"

/*
 * The timezone where the local system is located.  Used as a default by some
 * programs who obtain this value by using gettimeofday.
 */
struct timezone sys_tz;

EXPORT_SYMBOL(sys_tz);

#ifdef __ARCH_WANT_SYS_TIME

/*
 * sys_time() can be implemented in user-level using
 * sys_gettimeofday().  Is this for backwards compatibility?  If so,
 * why not move it into the appropriate arch directory (for those
 * architectures that need it).
 */
SYSCALL_DEFINE1(time, time_t __user *, tloc)
{
	time_t i = get_seconds();

	if (tloc) {
		if (put_user(i,tloc))
			return -EFAULT;
	}
	force_successful_syscall_return();
	return i;
}

/*
 * sys_stime() can be implemented in user-level using
 * sys_settimeofday().  Is this for backwards compatibility?  If so,
 * why not move it into the appropriate arch directory (for those
 * architectures that need it).
 */

SYSCALL_DEFINE1(stime, time_t __user *, tptr)
{
	struct timespec tv;
	int err;

	if (get_user(tv.tv_sec, tptr))
		return -EFAULT;

	tv.tv_nsec = 0;

	err = security_settime(&tv, NULL);
	if (err)
		return err;

	do_settimeofday(&tv);
	return 0;
}

#endif /* __ARCH_WANT_SYS_TIME */

SYSCALL_DEFINE2(gettimeofday, struct timeval __user *, tv,
		struct timezone __user *, tz)
{
	if (likely(tv != NULL)) {
		struct timeval ktv;
		do_gettimeofday(&ktv);
		if (copy_to_user(tv, &ktv, sizeof(ktv)))
			return -EFAULT;
	}
	if (unlikely(tz != NULL)) {
		if (copy_to_user(tz, &sys_tz, sizeof(sys_tz)))
			return -EFAULT;
	}
	return 0;
}

/*
 * Indicates if there is an offset between the system clock and the hardware
 * clock/persistent clock/rtc.
 */
int persistent_clock_is_local;

/*
 * Adjust the time obtained from the CMOS to be UTC time instead of
 * local time.
 *
 * This is ugly, but preferable to the alternatives.  Otherwise we
 * would either need to write a program to do it in /etc/rc (and risk
 * confusion if the program gets run more than once; it would also be
 * hard to make the program warp the clock precisely n hours)  or
 * compile in the timezone information into the kernel.  Bad, bad....
 *
 *						- TYT, 1992-01-01
 *
 * The best thing to do is to keep the CMOS clock in universal time (UTC)
 * as real UNIX machines always do it. This avoids all headaches about
 * daylight saving times and warping kernel clocks.
 */
static inline void warp_clock(void)
{
	if (sys_tz.tz_minuteswest != 0) {
		struct timespec adjust;

		persistent_clock_is_local = 1;
		adjust.tv_sec = sys_tz.tz_minuteswest * 60;
		adjust.tv_nsec = 0;
		timekeeping_inject_offset(&adjust);
	}
}

/*
 * In case for some reason the CMOS clock has not already been running
 * in UTC, but in some local time: The first time we set the timezone,
 * we will warp the clock so that it is ticking UTC time instead of
 * local time. Presumably, if someone is setting the timezone then we
 * are running in an environment where the programs understand about
 * timezones. This should be done at boot time in the /etc/rc script,
 * as soon as possible, so that the clock can be set right. Otherwise,
 * various programs will get confused when the clock gets warped.
 */

int do_sys_settimeofday64(const struct timespec64 *tv, const struct timezone *tz)
{
	static int firsttime = 1;
	int error = 0;

	if (tv && !timespec64_valid(tv))
		return -EINVAL;

	error = security_settime64(tv, tz);
	if (error)
		return error;

	if (tz) {
		/* Verify we're witin the +-15 hrs range */
		if (tz->tz_minuteswest > 15*60 || tz->tz_minuteswest < -15*60)
			return -EINVAL;

		sys_tz = *tz;
		update_vsyscall_tz();
		if (firsttime) {
			firsttime = 0;
			if (!tv)
				warp_clock();
		}
	}
	if (tv)
		return do_settimeofday64(tv);
	return 0;
}

SYSCALL_DEFINE2(settimeofday, struct timeval __user *, tv,
		struct timezone __user *, tz)
{
	struct timeval user_tv;
	struct timespec	new_ts;
	struct timezone new_tz;

	if (tv) {
		if (copy_from_user(&user_tv, tv, sizeof(*tv)))
			return -EFAULT;

		if (!timeval_valid(&user_tv))
			return -EINVAL;

		new_ts.tv_sec = user_tv.tv_sec;
		new_ts.tv_nsec = user_tv.tv_usec * NSEC_PER_USEC;
	}
	if (tz) {
		if (copy_from_user(&new_tz, tz, sizeof(*tz)))
			return -EFAULT;
	}

	return do_sys_settimeofday(tv ? &new_ts : NULL, tz ? &new_tz : NULL);
}

SYSCALL_DEFINE1(adjtimex, struct timex __user *, txc_p)
{
	struct timex txc;		/* Local copy of parameter */
	int ret;

	/* Copy the user data space into the kernel copy
	 * structure. But bear in mind that the structures
	 * may change
	 */
	if(copy_from_user(&txc, txc_p, sizeof(struct timex)))
		return -EFAULT;
	ret = do_adjtimex(&txc);
	return copy_to_user(txc_p, &txc, sizeof(struct timex)) ? -EFAULT : ret;
}

/**
 * current_fs_time - Return FS time
 * @sb: Superblock.
 *
 * Return the current time truncated to the time granularity supported by
 * the fs.
 */
struct timespec current_fs_time(struct super_block *sb)
{
	struct timespec now = current_kernel_time();
	return timespec_trunc(now, sb->s_time_gran);
}
EXPORT_SYMBOL(current_fs_time);

/*
 * Convert jiffies to milliseconds and back.
 *
 * Avoid unnecessary multiplications/divisions in the
 * two most common HZ cases:
 */
unsigned int jiffies_to_msecs(const unsigned long j)
{
#if HZ <= MSEC_PER_SEC && !(MSEC_PER_SEC % HZ)
	return (MSEC_PER_SEC / HZ) * j;
#elif HZ > MSEC_PER_SEC && !(HZ % MSEC_PER_SEC)
	return (j + (HZ / MSEC_PER_SEC) - 1)/(HZ / MSEC_PER_SEC);
#else
# if BITS_PER_LONG == 32
	return (HZ_TO_MSEC_MUL32 * j) >> HZ_TO_MSEC_SHR32;
# else
	return (j * HZ_TO_MSEC_NUM) / HZ_TO_MSEC_DEN;
# endif
#endif
}
EXPORT_SYMBOL(jiffies_to_msecs);

unsigned int jiffies_to_usecs(const unsigned long j)
{
	/*
	 * Hz usually doesn't go much further MSEC_PER_SEC.
	 * jiffies_to_usecs() and usecs_to_jiffies() depend on that.
	 */
	BUILD_BUG_ON(HZ > USEC_PER_SEC);

#if !(USEC_PER_SEC % HZ)
	return (USEC_PER_SEC / HZ) * j;
#else
# if BITS_PER_LONG == 32
	return (HZ_TO_USEC_MUL32 * j) >> HZ_TO_USEC_SHR32;
# else
	return (j * HZ_TO_USEC_NUM) / HZ_TO_USEC_DEN;
# endif
#endif
}
EXPORT_SYMBOL(jiffies_to_usecs);

/**
 * timespec_trunc - Truncate timespec to a granularity
 * @t: Timespec
 * @gran: Granularity in ns.
 *
 * Truncate a timespec to a granularity. Always rounds down. gran must
 * not be 0 nor greater than a second (NSEC_PER_SEC, or 10^9 ns).
 */
struct timespec timespec_trunc(struct timespec t, unsigned gran)
{
	/* Avoid division in the common cases 1 ns and 1 s. */
	if (gran == 1) {
		/* nothing */
	} else if (gran == NSEC_PER_SEC) {
		t.tv_nsec = 0;
	} else if (gran > 1 && gran < NSEC_PER_SEC) {
		t.tv_nsec -= t.tv_nsec % gran;
	} else {
		WARN(1, "illegal file time granularity: %u", gran);
	}
	return t;
}
EXPORT_SYMBOL(timespec_trunc);

/*
 * mktime64 - Converts date to seconds.
 * Converts Gregorian date to seconds since 1970-01-01 00:00:00.
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
 * A leap second can be indicated by calling this function with sec as
 * 60 (allowable under ISO 8601).  The leap second is treated the same
 * as the following second since they don't exist in UNIX time.
 *
 * An encoding of midnight at the end of the day as 24:00:00 - ie. midnight
 * tomorrow - (allowable under ISO 8601) is supported.
 */
time64_t mktime64(const unsigned int year0, const unsigned int mon0,
		const unsigned int day, const unsigned int hour,
		const unsigned int min, const unsigned int sec)
{
	unsigned int mon = mon0, year = year0;

	/* 1..12 -> 11,12,1..10 */
	if (0 >= (int) (mon -= 2)) {
		mon += 12;	/* Puts Feb last since it has leap day */
		year -= 1;
	}

	return ((((time64_t)
		  (year/4 - year/100 + year/400 + 367*mon/12 + day) +
		  year*365 - 719499
	    )*24 + hour /* now have hours - midnight tomorrow handled here */
	  )*60 + min /* now have minutes */
	)*60 + sec; /* finally seconds */
}
EXPORT_SYMBOL(mktime64);

/**
 * set_normalized_timespec - set timespec sec and nsec parts and normalize
 *
 * @ts:		pointer to timespec variable to be set
 * @sec:	seconds to set
 * @nsec:	nanoseconds to set
 *
 * Set seconds and nanoseconds field of a timespec variable and
 * normalize to the timespec storage format
 *
 * Note: The tv_nsec part is always in the range of
 *	0 <= tv_nsec < NSEC_PER_SEC
 * For negative values only the tv_sec field is negative !
 */
void set_normalized_timespec(struct timespec *ts, time_t sec, s64 nsec)
{
	while (nsec >= NSEC_PER_SEC) {
		/*
		 * The following asm() prevents the compiler from
		 * optimising this loop into a modulo operation. See
		 * also __iter_div_u64_rem() in include/linux/time.h
		 */
		asm("" : "+rm"(nsec));
		nsec -= NSEC_PER_SEC;
		++sec;
	}
	while (nsec < 0) {
		asm("" : "+rm"(nsec));
		nsec += NSEC_PER_SEC;
		--sec;
	}
	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}
EXPORT_SYMBOL(set_normalized_timespec);

/**
 * ns_to_timespec - Convert nanoseconds to timespec
 * @nsec:       the nanoseconds value to be converted
 *
 * Returns the timespec representation of the nsec parameter.
 */
struct timespec ns_to_timespec(const s64 nsec)
{
	struct timespec ts;
	s32 rem;

	if (!nsec)
		return (struct timespec) {0, 0};

	ts.tv_sec = div_s64_rem(nsec, NSEC_PER_SEC, &rem);
	if (unlikely(rem < 0)) {
		ts.tv_sec--;
		rem += NSEC_PER_SEC;
	}
	ts.tv_nsec = rem;

	return ts;
}
EXPORT_SYMBOL(ns_to_timespec);

/**
 * ns_to_timeval - Convert nanoseconds to timeval
 * @nsec:       the nanoseconds value to be converted
 *
 * Returns the timeval representation of the nsec parameter.
 */
struct timeval ns_to_timeval(const s64 nsec)
{
	struct timespec ts = ns_to_timespec(nsec);
	struct timeval tv;

	tv.tv_sec = ts.tv_sec;
	tv.tv_usec = (suseconds_t) ts.tv_nsec / 1000;

	return tv;
}
EXPORT_SYMBOL(ns_to_timeval);

#if BITS_PER_LONG == 32
/**
 * set_normalized_timespec - set timespec sec and nsec parts and normalize
 *
 * @ts:		pointer to timespec variable to be set
 * @sec:	seconds to set
 * @nsec:	nanoseconds to set
 *
 * Set seconds and nanoseconds field of a timespec variable and
 * normalize to the timespec storage format
 *
 * Note: The tv_nsec part is always in the range of
 *	0 <= tv_nsec < NSEC_PER_SEC
 * For negative values only the tv_sec field is negative !
 */
void set_normalized_timespec64(struct timespec64 *ts, time64_t sec, s64 nsec)
{
	while (nsec >= NSEC_PER_SEC) {
		/*
		 * The following asm() prevents the compiler from
		 * optimising this loop into a modulo operation. See
		 * also __iter_div_u64_rem() in include/linux/time.h
		 */
		asm("" : "+rm"(nsec));
		nsec -= NSEC_PER_SEC;
		++sec;
	}
	while (nsec < 0) {
		asm("" : "+rm"(nsec));
		nsec += NSEC_PER_SEC;
		--sec;
	}
	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}
EXPORT_SYMBOL(set_normalized_timespec64);

/**
 * ns_to_timespec64 - Convert nanoseconds to timespec64
 * @nsec:       the nanoseconds value to be converted
 *
 * Returns the timespec64 representation of the nsec parameter.
 */
struct timespec64 ns_to_timespec64(const s64 nsec)
{
	struct timespec64 ts;
	s32 rem;

	if (!nsec)
		return (struct timespec64) {0, 0};

	ts.tv_sec = div_s64_rem(nsec, NSEC_PER_SEC, &rem);
	if (unlikely(rem < 0)) {
		ts.tv_sec--;
		rem += NSEC_PER_SEC;
	}
	ts.tv_nsec = rem;

	return ts;
}
EXPORT_SYMBOL(ns_to_timespec64);
#endif
/**
 * msecs_to_jiffies: - convert milliseconds to jiffies
 * @m:	time in milliseconds
 *
 * conversion is done as follows:
 *
 * - negative values mean 'infinite timeout' (MAX_JIFFY_OFFSET)
 *
 * - 'too large' values [that would result in larger than
 *   MAX_JIFFY_OFFSET values] mean 'infinite timeout' too.
 *
 * - all other values are converted to jiffies by either multiplying
 *   the input value by a factor or dividing it with a factor and
 *   handling any 32-bit overflows.
 *   for the details see __msecs_to_jiffies()
 *
 * msecs_to_jiffies() checks for the passed in value being a constant
 * via __builtin_constant_p() allowing gcc to eliminate most of the
 * code, __msecs_to_jiffies() is called if the value passed does not
 * allow constant folding and the actual conversion must be done at
 * runtime.
 * the _msecs_to_jiffies helpers are the HZ dependent conversion
 * routines found in include/linux/jiffies.h
 */
unsigned long __msecs_to_jiffies(const unsigned int m)
{
	/*
	 * Negative value, means infinite timeout:
	 */
	if ((int)m < 0)
		return MAX_JIFFY_OFFSET;
	return _msecs_to_jiffies(m);
}
EXPORT_SYMBOL(__msecs_to_jiffies);

unsigned long __usecs_to_jiffies(const unsigned int u)
{
	if (u > jiffies_to_usecs(MAX_JIFFY_OFFSET))
		return MAX_JIFFY_OFFSET;
	return _usecs_to_jiffies(u);
}
EXPORT_SYMBOL(__usecs_to_jiffies);

/*
 * The TICK_NSEC - 1 rounds up the value to the next resolution.  Note
 * that a remainder subtract here would not do the right thing as the
 * resolution values don't fall on second boundries.  I.e. the line:
 * nsec -= nsec % TICK_NSEC; is NOT a correct resolution rounding.
 * Note that due to the small error in the multiplier here, this
 * rounding is incorrect for sufficiently large values of tv_nsec, but
 * well formed timespecs should have tv_nsec < NSEC_PER_SEC, so we're
 * OK.
 *
 * Rather, we just shift the bits off the right.
 *
 * The >> (NSEC_JIFFIE_SC - SEC_JIFFIE_SC) converts the scaled nsec
 * value to a scaled second value.
 */
static unsigned long
__timespec64_to_jiffies(u64 sec, long nsec)
{
	nsec = nsec + TICK_NSEC - 1;

	if (sec >= MAX_SEC_IN_JIFFIES){
		sec = MAX_SEC_IN_JIFFIES;
		nsec = 0;
	}
	return ((sec * SEC_CONVERSION) +
		(((u64)nsec * NSEC_CONVERSION) >>
		 (NSEC_JIFFIE_SC - SEC_JIFFIE_SC))) >> SEC_JIFFIE_SC;

}

static unsigned long
__timespec_to_jiffies(unsigned long sec, long nsec)
{
	return __timespec64_to_jiffies((u64)sec, nsec);
}

unsigned long
timespec64_to_jiffies(const struct timespec64 *value)
{
	return __timespec64_to_jiffies(value->tv_sec, value->tv_nsec);
}
EXPORT_SYMBOL(timespec64_to_jiffies);

void
jiffies_to_timespec64(const unsigned long jiffies, struct timespec64 *value)
{
	/*
	 * Convert jiffies to nanoseconds and separate with
	 * one divide.
	 */
	u32 rem;
	value->tv_sec = div_u64_rem((u64)jiffies * TICK_NSEC,
				    NSEC_PER_SEC, &rem);
	value->tv_nsec = rem;
}
EXPORT_SYMBOL(jiffies_to_timespec64);

/*
 * We could use a similar algorithm to timespec_to_jiffies (with a
 * different multiplier for usec instead of nsec). But this has a
 * problem with rounding: we can't exactly add TICK_NSEC - 1 to the
 * usec value, since it's not necessarily integral.
 *
 * We could instead round in the intermediate scaled representation
 * (i.e. in units of 1/2^(large scale) jiffies) but that's also
 * perilous: the scaling introduces a small positive error, which
 * combined with a division-rounding-upward (i.e. adding 2^(scale) - 1
 * units to the intermediate before shifting) leads to accidental
 * overflow and overestimates.
 *
 * At the cost of one additional multiplication by a constant, just
 * use the timespec implementation.
 */
unsigned long
timeval_to_jiffies(const struct timeval *value)
{
	return __timespec_to_jiffies(value->tv_sec,
				     value->tv_usec * NSEC_PER_USEC);
}
EXPORT_SYMBOL(timeval_to_jiffies);

void jiffies_to_timeval(const unsigned long jiffies, struct timeval *value)
{
	/*
	 * Convert jiffies to nanoseconds and separate with
	 * one divide.
	 */
	u32 rem;

	value->tv_sec = div_u64_rem((u64)jiffies * TICK_NSEC,
				    NSEC_PER_SEC, &rem);
	value->tv_usec = rem / NSEC_PER_USEC;
}
EXPORT_SYMBOL(jiffies_to_timeval);

/*
 * Convert jiffies/jiffies_64 to clock_t and back.
 */
clock_t jiffies_to_clock_t(unsigned long x)
{
#if (TICK_NSEC % (NSEC_PER_SEC / USER_HZ)) == 0
# if HZ < USER_HZ
	return x * (USER_HZ / HZ);
# else
	return x / (HZ / USER_HZ);
# endif
#else
	return div_u64((u64)x * TICK_NSEC, NSEC_PER_SEC / USER_HZ);
#endif
}
EXPORT_SYMBOL(jiffies_to_clock_t);

unsigned long clock_t_to_jiffies(unsigned long x)
{
#if (HZ % USER_HZ)==0
	if (x >= ~0UL / (HZ / USER_HZ))
		return ~0UL;
	return x * (HZ / USER_HZ);
#else
	/* Don't worry about loss of precision here .. */
	if (x >= ~0UL / HZ * USER_HZ)
		return ~0UL;

	/* .. but do try to contain it here */
	return div_u64((u64)x * HZ, USER_HZ);
#endif
}
EXPORT_SYMBOL(clock_t_to_jiffies);

u64 jiffies_64_to_clock_t(u64 x)
{
#if (TICK_NSEC % (NSEC_PER_SEC / USER_HZ)) == 0
# if HZ < USER_HZ
	x = div_u64(x * USER_HZ, HZ);
# elif HZ > USER_HZ
	x = div_u64(x, HZ / USER_HZ);
# else
	/* Nothing to do */
# endif
#else
	/*
	 * There are better ways that don't overflow early,
	 * but even this doesn't overflow in hundreds of years
	 * in 64 bits, so..
	 */
	x = div_u64(x * TICK_NSEC, (NSEC_PER_SEC / USER_HZ));
#endif
	return x;
}
EXPORT_SYMBOL(jiffies_64_to_clock_t);

u64 nsec_to_clock_t(u64 x)
{
#if (NSEC_PER_SEC % USER_HZ) == 0
	return div_u64(x, NSEC_PER_SEC / USER_HZ);
#elif (USER_HZ % 512) == 0
	return div_u64(x * USER_HZ / 512, NSEC_PER_SEC / 512);
#else
	/*
         * max relative error 5.7e-8 (1.8s per year) for USER_HZ <= 1024,
         * overflow after 64.99 years.
         * exact for HZ=60, 72, 90, 120, 144, 180, 300, 600, 900, ...
         */
	return div_u64(x * 9, (9ull * NSEC_PER_SEC + (USER_HZ / 2)) / USER_HZ);
#endif
}

u64 jiffies64_to_nsecs(u64 j)
{
#if !(NSEC_PER_SEC % HZ)
	return (NSEC_PER_SEC / HZ) * j;
# else
	return div_u64(j * HZ_TO_NSEC_NUM, HZ_TO_NSEC_DEN);
#endif
}
EXPORT_SYMBOL(jiffies64_to_nsecs);

/**
 * nsecs_to_jiffies64 - Convert nsecs in u64 to jiffies64
 *
 * @n:	nsecs in u64
 *
 * Unlike {m,u}secs_to_jiffies, type of input is not unsigned int but u64.
 * And this doesn't return MAX_JIFFY_OFFSET since this function is designed
 * for scheduler, not for use in device drivers to calculate timeout value.
 *
 * note:
 *   NSEC_PER_SEC = 10^9 = (5^9 * 2^9) = (1953125 * 512)
 *   ULLONG_MAX ns = 18446744073.709551615 secs = about 584 years
 */
u64 nsecs_to_jiffies64(u64 n)
{
#if (NSEC_PER_SEC % HZ) == 0
	/* Common case, HZ = 100, 128, 200, 250, 256, 500, 512, 1000 etc. */
	return div_u64(n, NSEC_PER_SEC / HZ);
#elif (HZ % 512) == 0
	/* overflow after 292 years if HZ = 1024 */
	return div_u64(n * HZ / 512, NSEC_PER_SEC / 512);
#else
	/*
	 * Generic case - optimized for cases where HZ is a multiple of 3.
	 * overflow after 64.99 years, exact for HZ = 60, 72, 90, 120 etc.
	 */
	return div_u64(n * 9, (9ull * NSEC_PER_SEC + HZ / 2) / HZ);
#endif
}
EXPORT_SYMBOL(nsecs_to_jiffies64);

/**
 * nsecs_to_jiffies - Convert nsecs in u64 to jiffies
 *
 * @n:	nsecs in u64
 *
 * Unlike {m,u}secs_to_jiffies, type of input is not unsigned int but u64.
 * And this doesn't return MAX_JIFFY_OFFSET since this function is designed
 * for scheduler, not for use in device drivers to calculate timeout value.
 *
 * note:
 *   NSEC_PER_SEC = 10^9 = (5^9 * 2^9) = (1953125 * 512)
 *   ULLONG_MAX ns = 18446744073.709551615 secs = about 584 years
 */
unsigned long nsecs_to_jiffies(u64 n)
{
	return (unsigned long)nsecs_to_jiffies64(n);
}
EXPORT_SYMBOL_GPL(nsecs_to_jiffies);

/*
 * Add two timespec values and do a safety check for overflow.
 * It's assumed that both values are valid (>= 0)
 */
struct timespec timespec_add_safe(const struct timespec lhs,
				  const struct timespec rhs)
{
	struct timespec res;

	set_normalized_timespec(&res, lhs.tv_sec + rhs.tv_sec,
				lhs.tv_nsec + rhs.tv_nsec);

	if (res.tv_sec < lhs.tv_sec || res.tv_sec < rhs.tv_sec)
		res.tv_sec = TIME_T_MAX;

	return res;
}

/*
 * Add two timespec64 values and do a safety check for overflow.
 * It's assumed that both values are valid (>= 0).
 * And, each timespec64 is in normalized form.
 */
struct timespec64 timespec64_add_safe(const struct timespec64 lhs,
				const struct timespec64 rhs)
{
	struct timespec64 res;

	set_normalized_timespec64(&res, (timeu64_t) lhs.tv_sec + rhs.tv_sec,
			lhs.tv_nsec + rhs.tv_nsec);

	if (unlikely(res.tv_sec < lhs.tv_sec || res.tv_sec < rhs.tv_sec)) {
		res.tv_sec = TIME64_MAX;
		res.tv_nsec = 0;
	}

	return res;
}
