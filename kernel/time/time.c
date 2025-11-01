// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  This file contains the interface functions for the various time related
 *  system calls: time, stime, gettimeofday, settimeofday, adjtime
 *
 * Modification history:
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
#include <linux/kernel.h>
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
#include <linux/compat.h>
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
SYSCALL_DEFINE1(time, __kernel_old_time_t __user *, tloc)
{
	__kernel_old_time_t i = (__kernel_old_time_t)ktime_get_real_seconds();

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

SYSCALL_DEFINE1(stime, __kernel_old_time_t __user *, tptr)
{
	struct timespec64 tv;
	int err;

	if (get_user(tv.tv_sec, tptr))
		return -EFAULT;

	tv.tv_nsec = 0;

	err = security_settime64(&tv, NULL);
	if (err)
		return err;

	do_settimeofday64(&tv);
	return 0;
}

#endif /* __ARCH_WANT_SYS_TIME */

#ifdef CONFIG_COMPAT_32BIT_TIME
#ifdef __ARCH_WANT_SYS_TIME32

/* old_time32_t is a 32 bit "long" and needs to get converted. */
SYSCALL_DEFINE1(time32, old_time32_t __user *, tloc)
{
	old_time32_t i;

	i = (old_time32_t)ktime_get_real_seconds();

	if (tloc) {
		if (put_user(i,tloc))
			return -EFAULT;
	}
	force_successful_syscall_return();
	return i;
}

SYSCALL_DEFINE1(stime32, old_time32_t __user *, tptr)
{
	struct timespec64 tv;
	int err;

	if (get_user(tv.tv_sec, tptr))
		return -EFAULT;

	tv.tv_nsec = 0;

	err = security_settime64(&tv, NULL);
	if (err)
		return err;

	do_settimeofday64(&tv);
	return 0;
}

#endif /* __ARCH_WANT_SYS_TIME32 */
#endif

SYSCALL_DEFINE2(gettimeofday, struct __kernel_old_timeval __user *, tv,
		struct timezone __user *, tz)
{
	if (likely(tv != NULL)) {
		struct timespec64 ts;

		ktime_get_real_ts64(&ts);
		if (put_user(ts.tv_sec, &tv->tv_sec) ||
		    put_user(ts.tv_nsec / 1000, &tv->tv_usec))
			return -EFAULT;
	}
	if (unlikely(tz != NULL)) {
		if (copy_to_user(tz, &sys_tz, sizeof(sys_tz)))
			return -EFAULT;
	}
	return 0;
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

	if (tv && !timespec64_valid_settod(tv))
		return -EINVAL;

	error = security_settime64(tv, tz);
	if (error)
		return error;

	if (tz) {
		/* Verify we're within the +-15 hrs range */
		if (tz->tz_minuteswest > 15*60 || tz->tz_minuteswest < -15*60)
			return -EINVAL;

		sys_tz = *tz;
		update_vsyscall_tz();
		if (firsttime) {
			firsttime = 0;
			if (!tv)
				timekeeping_warp_clock();
		}
	}
	if (tv)
		return do_settimeofday64(tv);
	return 0;
}

SYSCALL_DEFINE2(settimeofday, struct __kernel_old_timeval __user *, tv,
		struct timezone __user *, tz)
{
	struct timespec64 new_ts;
	struct timezone new_tz;

	if (tv) {
		if (get_user(new_ts.tv_sec, &tv->tv_sec) ||
		    get_user(new_ts.tv_nsec, &tv->tv_usec))
			return -EFAULT;

		if (new_ts.tv_nsec > USEC_PER_SEC || new_ts.tv_nsec < 0)
			return -EINVAL;

		new_ts.tv_nsec *= NSEC_PER_USEC;
	}
	if (tz) {
		if (copy_from_user(&new_tz, tz, sizeof(*tz)))
			return -EFAULT;
	}

	return do_sys_settimeofday64(tv ? &new_ts : NULL, tz ? &new_tz : NULL);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE2(gettimeofday, struct old_timeval32 __user *, tv,
		       struct timezone __user *, tz)
{
	if (tv) {
		struct timespec64 ts;

		ktime_get_real_ts64(&ts);
		if (put_user(ts.tv_sec, &tv->tv_sec) ||
		    put_user(ts.tv_nsec / 1000, &tv->tv_usec))
			return -EFAULT;
	}
	if (tz) {
		if (copy_to_user(tz, &sys_tz, sizeof(sys_tz)))
			return -EFAULT;
	}

	return 0;
}

COMPAT_SYSCALL_DEFINE2(settimeofday, struct old_timeval32 __user *, tv,
		       struct timezone __user *, tz)
{
	struct timespec64 new_ts;
	struct timezone new_tz;

	if (tv) {
		if (get_user(new_ts.tv_sec, &tv->tv_sec) ||
		    get_user(new_ts.tv_nsec, &tv->tv_usec))
			return -EFAULT;

		if (new_ts.tv_nsec > USEC_PER_SEC || new_ts.tv_nsec < 0)
			return -EINVAL;

		new_ts.tv_nsec *= NSEC_PER_USEC;
	}
	if (tz) {
		if (copy_from_user(&new_tz, tz, sizeof(*tz)))
			return -EFAULT;
	}

	return do_sys_settimeofday64(tv ? &new_ts : NULL, tz ? &new_tz : NULL);
}
#endif

#ifdef CONFIG_64BIT
SYSCALL_DEFINE1(adjtimex, struct __kernel_timex __user *, txc_p)
{
	struct __kernel_timex txc;		/* Local copy of parameter */
	int ret;

	/* Copy the user data space into the kernel copy
	 * structure. But bear in mind that the structures
	 * may change
	 */
	if (copy_from_user(&txc, txc_p, sizeof(struct __kernel_timex)))
		return -EFAULT;
	ret = do_adjtimex(&txc);
	return copy_to_user(txc_p, &txc, sizeof(struct __kernel_timex)) ? -EFAULT : ret;
}
#endif

#ifdef CONFIG_COMPAT_32BIT_TIME
int get_old_timex32(struct __kernel_timex *txc, const struct old_timex32 __user *utp)
{
	struct old_timex32 tx32;

	memset(txc, 0, sizeof(struct __kernel_timex));
	if (copy_from_user(&tx32, utp, sizeof(struct old_timex32)))
		return -EFAULT;

	txc->modes = tx32.modes;
	txc->offset = tx32.offset;
	txc->freq = tx32.freq;
	txc->maxerror = tx32.maxerror;
	txc->esterror = tx32.esterror;
	txc->status = tx32.status;
	txc->constant = tx32.constant;
	txc->precision = tx32.precision;
	txc->tolerance = tx32.tolerance;
	txc->time.tv_sec = tx32.time.tv_sec;
	txc->time.tv_usec = tx32.time.tv_usec;
	txc->tick = tx32.tick;
	txc->ppsfreq = tx32.ppsfreq;
	txc->jitter = tx32.jitter;
	txc->shift = tx32.shift;
	txc->stabil = tx32.stabil;
	txc->jitcnt = tx32.jitcnt;
	txc->calcnt = tx32.calcnt;
	txc->errcnt = tx32.errcnt;
	txc->stbcnt = tx32.stbcnt;

	return 0;
}

int put_old_timex32(struct old_timex32 __user *utp, const struct __kernel_timex *txc)
{
	struct old_timex32 tx32;

	memset(&tx32, 0, sizeof(struct old_timex32));
	tx32.modes = txc->modes;
	tx32.offset = txc->offset;
	tx32.freq = txc->freq;
	tx32.maxerror = txc->maxerror;
	tx32.esterror = txc->esterror;
	tx32.status = txc->status;
	tx32.constant = txc->constant;
	tx32.precision = txc->precision;
	tx32.tolerance = txc->tolerance;
	tx32.time.tv_sec = txc->time.tv_sec;
	tx32.time.tv_usec = txc->time.tv_usec;
	tx32.tick = txc->tick;
	tx32.ppsfreq = txc->ppsfreq;
	tx32.jitter = txc->jitter;
	tx32.shift = txc->shift;
	tx32.stabil = txc->stabil;
	tx32.jitcnt = txc->jitcnt;
	tx32.calcnt = txc->calcnt;
	tx32.errcnt = txc->errcnt;
	tx32.stbcnt = txc->stbcnt;
	tx32.tai = txc->tai;
	if (copy_to_user(utp, &tx32, sizeof(struct old_timex32)))
		return -EFAULT;
	return 0;
}

SYSCALL_DEFINE1(adjtimex_time32, struct old_timex32 __user *, utp)
{
	struct __kernel_timex txc;
	int err, ret;

	err = get_old_timex32(&txc, utp);
	if (err)
		return err;

	ret = do_adjtimex(&txc);

	err = put_old_timex32(utp, &txc);
	if (err)
		return err;

	return ret;
}
#endif

/**
 * jiffies_to_msecs - Convert jiffies to milliseconds
 * @j: jiffies value
 *
 * Avoid unnecessary multiplications/divisions in the
 * two most common HZ cases.
 *
 * Return: milliseconds value
 */
unsigned int jiffies_to_msecs(const unsigned long j)
{
#if HZ <= MSEC_PER_SEC && !(MSEC_PER_SEC % HZ)
	return (MSEC_PER_SEC / HZ) * j;
#elif HZ > MSEC_PER_SEC && !(HZ % MSEC_PER_SEC)
	return (j + (HZ / MSEC_PER_SEC) - 1)/(HZ / MSEC_PER_SEC);
#else
# if BITS_PER_LONG == 32
	return (HZ_TO_MSEC_MUL32 * j + (1ULL << HZ_TO_MSEC_SHR32) - 1) >>
	       HZ_TO_MSEC_SHR32;
# else
	return DIV_ROUND_UP(j * HZ_TO_MSEC_NUM, HZ_TO_MSEC_DEN);
# endif
#endif
}
EXPORT_SYMBOL(jiffies_to_msecs);

/**
 * jiffies_to_usecs - Convert jiffies to microseconds
 * @j: jiffies value
 *
 * Return: microseconds value
 */
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
 * mktime64 - Converts date to seconds.
 * @year0: year to convert
 * @mon0: month to convert
 * @day: day to convert
 * @hour: hour to convert
 * @min: minute to convert
 * @sec: second to convert
 *
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
 *
 * Return: seconds since the epoch time for the given input date
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

struct __kernel_old_timeval ns_to_kernel_old_timeval(s64 nsec)
{
	struct timespec64 ts = ns_to_timespec64(nsec);
	struct __kernel_old_timeval tv;

	tv.tv_sec = ts.tv_sec;
	tv.tv_usec = (suseconds_t)ts.tv_nsec / 1000;

	return tv;
}
EXPORT_SYMBOL(ns_to_kernel_old_timeval);

/**
 * set_normalized_timespec64 - set timespec sec and nsec parts and normalize
 *
 * @ts:		pointer to timespec variable to be set
 * @sec:	seconds to set
 * @nsec:	nanoseconds to set
 *
 * Set seconds and nanoseconds field of a timespec variable and
 * normalize to the timespec storage format
 *
 * Note: The tv_nsec part is always in the range of 0 <= tv_nsec < NSEC_PER_SEC.
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
 * Return: the timespec64 representation of the nsec parameter.
 */
struct timespec64 ns_to_timespec64(s64 nsec)
{
	struct timespec64 ts = { 0, 0 };
	s32 rem;

	if (likely(nsec > 0)) {
		ts.tv_sec = div_u64_rem(nsec, NSEC_PER_SEC, &rem);
		ts.tv_nsec = rem;
	} else if (nsec < 0) {
		/*
		 * With negative times, tv_sec points to the earlier
		 * second, and tv_nsec counts the nanoseconds since
		 * then, so tv_nsec is always a positive number.
		 */
		ts.tv_sec = -div_u64_rem(-nsec - 1, NSEC_PER_SEC, &rem) - 1;
		ts.tv_nsec = NSEC_PER_SEC - rem - 1;
	}

	return ts;
}
EXPORT_SYMBOL(ns_to_timespec64);

/**
 * __msecs_to_jiffies: - convert milliseconds to jiffies
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
 *   for the details see _msecs_to_jiffies()
 *
 * msecs_to_jiffies() checks for the passed in value being a constant
 * via __builtin_constant_p() allowing gcc to eliminate most of the
 * code, __msecs_to_jiffies() is called if the value passed does not
 * allow constant folding and the actual conversion must be done at
 * runtime.
 * The _msecs_to_jiffies helpers are the HZ dependent conversion
 * routines found in include/linux/jiffies.h
 *
 * Return: jiffies value
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

/**
 * __usecs_to_jiffies: - convert microseconds to jiffies
 * @u:	time in milliseconds
 *
 * Return: jiffies value
 */
unsigned long __usecs_to_jiffies(const unsigned int u)
{
	if (u > jiffies_to_usecs(MAX_JIFFY_OFFSET))
		return MAX_JIFFY_OFFSET;
	return _usecs_to_jiffies(u);
}
EXPORT_SYMBOL(__usecs_to_jiffies);

/**
 * timespec64_to_jiffies - convert a timespec64 value to jiffies
 * @value: pointer to &struct timespec64
 *
 * The TICK_NSEC - 1 rounds up the value to the next resolution.  Note
 * that a remainder subtract here would not do the right thing as the
 * resolution values don't fall on second boundaries.  I.e. the line:
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
 *
 * Return: jiffies value
 */
unsigned long
timespec64_to_jiffies(const struct timespec64 *value)
{
	u64 sec = value->tv_sec;
	long nsec = value->tv_nsec + TICK_NSEC - 1;

	if (sec >= MAX_SEC_IN_JIFFIES){
		sec = MAX_SEC_IN_JIFFIES;
		nsec = 0;
	}
	return ((sec * SEC_CONVERSION) +
		(((u64)nsec * NSEC_CONVERSION) >>
		 (NSEC_JIFFIE_SC - SEC_JIFFIE_SC))) >> SEC_JIFFIE_SC;

}
EXPORT_SYMBOL(timespec64_to_jiffies);

/**
 * jiffies_to_timespec64 - convert jiffies value to &struct timespec64
 * @jiffies: jiffies value
 * @value: pointer to &struct timespec64
 */
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
 * Convert jiffies/jiffies_64 to clock_t and back.
 */

/**
 * jiffies_to_clock_t - Convert jiffies to clock_t
 * @x: jiffies value
 *
 * Return: jiffies converted to clock_t (CLOCKS_PER_SEC)
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

/**
 * clock_t_to_jiffies - Convert clock_t to jiffies
 * @x: clock_t value
 *
 * Return: clock_t value converted to jiffies
 */
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

/**
 * jiffies_64_to_clock_t - Convert jiffies_64 to clock_t
 * @x: jiffies_64 value
 *
 * Return: jiffies_64 value converted to 64-bit "clock_t" (CLOCKS_PER_SEC)
 */
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

/**
 * nsec_to_clock_t - Convert nsec value to clock_t
 * @x: nsec value
 *
 * Return: nsec value converted to 64-bit "clock_t" (CLOCKS_PER_SEC)
 */
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

/**
 * jiffies64_to_nsecs - Convert jiffies64 to nanoseconds
 * @j: jiffies64 value
 *
 * Return: nanoseconds value
 */
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
 * jiffies64_to_msecs - Convert jiffies64 to milliseconds
 * @j: jiffies64 value
 *
 * Return: milliseconds value
 */
u64 jiffies64_to_msecs(const u64 j)
{
#if HZ <= MSEC_PER_SEC && !(MSEC_PER_SEC % HZ)
	return (MSEC_PER_SEC / HZ) * j;
#else
	return div_u64(j * HZ_TO_MSEC_NUM, HZ_TO_MSEC_DEN);
#endif
}
EXPORT_SYMBOL(jiffies64_to_msecs);

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
 *
 * Return: nsecs converted to jiffies64 value
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
 *
 * Return: nsecs converted to jiffies value
 */
unsigned long nsecs_to_jiffies(u64 n)
{
	return (unsigned long)nsecs_to_jiffies64(n);
}
EXPORT_SYMBOL_GPL(nsecs_to_jiffies);

/**
 * timespec64_add_safe - Add two timespec64 values and do a safety check
 * for overflow.
 * @lhs: first (left) timespec64 to add
 * @rhs: second (right) timespec64 to add
 *
 * It's assumed that both values are valid (>= 0).
 * And, each timespec64 is in normalized form.
 *
 * Return: sum of @lhs + @rhs
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
EXPORT_SYMBOL_GPL(timespec64_add_safe);

/**
 * get_timespec64 - get user's time value into kernel space
 * @ts: destination &struct timespec64
 * @uts: user's time value as &struct __kernel_timespec
 *
 * Handles compat or 32-bit modes.
 *
 * Return: 0 on success or negative errno on error
 */
int get_timespec64(struct timespec64 *ts,
		   const struct __kernel_timespec __user *uts)
{
	struct __kernel_timespec kts;
	int ret;

	ret = copy_from_user(&kts, uts, sizeof(kts));
	if (ret)
		return -EFAULT;

	ts->tv_sec = kts.tv_sec;

	/* Zero out the padding in compat mode */
	if (in_compat_syscall())
		kts.tv_nsec &= 0xFFFFFFFFUL;

	/* In 32-bit mode, this drops the padding */
	ts->tv_nsec = kts.tv_nsec;

	return 0;
}
EXPORT_SYMBOL_GPL(get_timespec64);

/**
 * put_timespec64 - convert timespec64 value to __kernel_timespec format and
 * 		    copy the latter to userspace
 * @ts: input &struct timespec64
 * @uts: user's &struct __kernel_timespec
 *
 * Return: 0 on success or negative errno on error
 */
int put_timespec64(const struct timespec64 *ts,
		   struct __kernel_timespec __user *uts)
{
	struct __kernel_timespec kts = {
		.tv_sec = ts->tv_sec,
		.tv_nsec = ts->tv_nsec
	};

	return copy_to_user(uts, &kts, sizeof(kts)) ? -EFAULT : 0;
}
EXPORT_SYMBOL_GPL(put_timespec64);

static int __get_old_timespec32(struct timespec64 *ts64,
				   const struct old_timespec32 __user *cts)
{
	struct old_timespec32 ts;
	int ret;

	ret = copy_from_user(&ts, cts, sizeof(ts));
	if (ret)
		return -EFAULT;

	ts64->tv_sec = ts.tv_sec;
	ts64->tv_nsec = ts.tv_nsec;

	return 0;
}

static int __put_old_timespec32(const struct timespec64 *ts64,
				   struct old_timespec32 __user *cts)
{
	struct old_timespec32 ts = {
		.tv_sec = ts64->tv_sec,
		.tv_nsec = ts64->tv_nsec
	};
	return copy_to_user(cts, &ts, sizeof(ts)) ? -EFAULT : 0;
}

/**
 * get_old_timespec32 - get user's old-format time value into kernel space
 * @ts: destination &struct timespec64
 * @uts: user's old-format time value (&struct old_timespec32)
 *
 * Handles X86_X32_ABI compatibility conversion.
 *
 * Return: 0 on success or negative errno on error
 */
int get_old_timespec32(struct timespec64 *ts, const void __user *uts)
{
	if (COMPAT_USE_64BIT_TIME)
		return copy_from_user(ts, uts, sizeof(*ts)) ? -EFAULT : 0;
	else
		return __get_old_timespec32(ts, uts);
}
EXPORT_SYMBOL_GPL(get_old_timespec32);

/**
 * put_old_timespec32 - convert timespec64 value to &struct old_timespec32 and
 * 			copy the latter to userspace
 * @ts: input &struct timespec64
 * @uts: user's &struct old_timespec32
 *
 * Handles X86_X32_ABI compatibility conversion.
 *
 * Return: 0 on success or negative errno on error
 */
int put_old_timespec32(const struct timespec64 *ts, void __user *uts)
{
	if (COMPAT_USE_64BIT_TIME)
		return copy_to_user(uts, ts, sizeof(*ts)) ? -EFAULT : 0;
	else
		return __put_old_timespec32(ts, uts);
}
EXPORT_SYMBOL_GPL(put_old_timespec32);

/**
 * get_itimerspec64 - get user's &struct __kernel_itimerspec into kernel space
 * @it: destination &struct itimerspec64
 * @uit: user's &struct __kernel_itimerspec
 *
 * Return: 0 on success or negative errno on error
 */
int get_itimerspec64(struct itimerspec64 *it,
			const struct __kernel_itimerspec __user *uit)
{
	int ret;

	ret = get_timespec64(&it->it_interval, &uit->it_interval);
	if (ret)
		return ret;

	ret = get_timespec64(&it->it_value, &uit->it_value);

	return ret;
}
EXPORT_SYMBOL_GPL(get_itimerspec64);

/**
 * put_itimerspec64 - convert &struct itimerspec64 to __kernel_itimerspec format
 * 		      and copy the latter to userspace
 * @it: input &struct itimerspec64
 * @uit: user's &struct __kernel_itimerspec
 *
 * Return: 0 on success or negative errno on error
 */
int put_itimerspec64(const struct itimerspec64 *it,
			struct __kernel_itimerspec __user *uit)
{
	int ret;

	ret = put_timespec64(&it->it_interval, &uit->it_interval);
	if (ret)
		return ret;

	ret = put_timespec64(&it->it_value, &uit->it_value);

	return ret;
}
EXPORT_SYMBOL_GPL(put_itimerspec64);

/**
 * get_old_itimerspec32 - get user's &struct old_itimerspec32 into kernel space
 * @its: destination &struct itimerspec64
 * @uits: user's &struct old_itimerspec32
 *
 * Return: 0 on success or negative errno on error
 */
int get_old_itimerspec32(struct itimerspec64 *its,
			const struct old_itimerspec32 __user *uits)
{

	if (__get_old_timespec32(&its->it_interval, &uits->it_interval) ||
	    __get_old_timespec32(&its->it_value, &uits->it_value))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(get_old_itimerspec32);

/**
 * put_old_itimerspec32 - convert &struct itimerspec64 to &struct
 *			  old_itimerspec32 and copy the latter to userspace
 * @its: input &struct itimerspec64
 * @uits: user's &struct old_itimerspec32
 *
 * Return: 0 on success or negative errno on error
 */
int put_old_itimerspec32(const struct itimerspec64 *its,
			struct old_itimerspec32 __user *uits)
{
	if (__put_old_timespec32(&its->it_interval, &uits->it_interval) ||
	    __put_old_timespec32(&its->it_value, &uits->it_value))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(put_old_itimerspec32);
