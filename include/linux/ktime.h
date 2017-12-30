/*
 *  include/linux/ktime.h
 *
 *  ktime_t - nanosecond-resolution time format.
 *
 *   Copyright(C) 2005, Thomas Gleixner <tglx@linutronix.de>
 *   Copyright(C) 2005, Red Hat, Inc., Ingo Molnar
 *
 *  data type definitions, declarations, prototypes and macros.
 *
 *  Started by: Thomas Gleixner and Ingo Molnar
 *
 *  Credits:
 *
 *  	Roman Zippel provided the ideas and primary code snippets of
 *  	the ktime_t union and further simplifications of the original
 *  	code.
 *
 *  For licencing details see kernel-base/COPYING
 */
#ifndef _LINUX_KTIME_H
#define _LINUX_KTIME_H

#include <linux/time.h>
#include <linux/jiffies.h>

/* Nanosecond scalar representation for kernel time values */
typedef s64	ktime_t;

/**
 * ktime_set - Set a ktime_t variable from a seconds/nanoseconds value
 * @secs:	seconds to set
 * @nsecs:	nanoseconds to set
 *
 * Return: The ktime_t representation of the value.
 */
static inline ktime_t ktime_set(const s64 secs, const unsigned long nsecs)
{
	if (unlikely(secs >= KTIME_SEC_MAX))
		return KTIME_MAX;

	return secs * NSEC_PER_SEC + (s64)nsecs;
}

/* Subtract two ktime_t variables. rem = lhs -rhs: */
#define ktime_sub(lhs, rhs)	((lhs) - (rhs))

/* Add two ktime_t variables. res = lhs + rhs: */
#define ktime_add(lhs, rhs)	((lhs) + (rhs))

/*
 * Same as ktime_add(), but avoids undefined behaviour on overflow; however,
 * this means that you must check the result for overflow yourself.
 */
#define ktime_add_unsafe(lhs, rhs)	((u64) (lhs) + (rhs))

/*
 * Add a ktime_t variable and a scalar nanosecond value.
 * res = kt + nsval:
 */
#define ktime_add_ns(kt, nsval)		((kt) + (nsval))

/*
 * Subtract a scalar nanosecod from a ktime_t variable
 * res = kt - nsval:
 */
#define ktime_sub_ns(kt, nsval)		((kt) - (nsval))

/* convert a timespec to ktime_t format: */
static inline ktime_t timespec_to_ktime(struct timespec ts)
{
	return ktime_set(ts.tv_sec, ts.tv_nsec);
}

/* convert a timespec64 to ktime_t format: */
static inline ktime_t timespec64_to_ktime(struct timespec64 ts)
{
	return ktime_set(ts.tv_sec, ts.tv_nsec);
}

/* convert a timeval to ktime_t format: */
static inline ktime_t timeval_to_ktime(struct timeval tv)
{
	return ktime_set(tv.tv_sec, tv.tv_usec * NSEC_PER_USEC);
}

/* Map the ktime_t to timespec conversion to ns_to_timespec function */
#define ktime_to_timespec(kt)		ns_to_timespec((kt))

/* Map the ktime_t to timespec conversion to ns_to_timespec function */
#define ktime_to_timespec64(kt)		ns_to_timespec64((kt))

/* Map the ktime_t to timeval conversion to ns_to_timeval function */
#define ktime_to_timeval(kt)		ns_to_timeval((kt))

/* Convert ktime_t to nanoseconds - NOP in the scalar storage format: */
#define ktime_to_ns(kt)			(kt)

/**
 * ktime_compare - Compares two ktime_t variables for less, greater or equal
 * @cmp1:	comparable1
 * @cmp2:	comparable2
 *
 * Return: ...
 *   cmp1  < cmp2: return <0
 *   cmp1 == cmp2: return 0
 *   cmp1  > cmp2: return >0
 */
static inline int ktime_compare(const ktime_t cmp1, const ktime_t cmp2)
{
	if (cmp1 < cmp2)
		return -1;
	if (cmp1 > cmp2)
		return 1;
	return 0;
}

/**
 * ktime_after - Compare if a ktime_t value is bigger than another one.
 * @cmp1:	comparable1
 * @cmp2:	comparable2
 *
 * Return: true if cmp1 happened after cmp2.
 */
static inline bool ktime_after(const ktime_t cmp1, const ktime_t cmp2)
{
	return ktime_compare(cmp1, cmp2) > 0;
}

/**
 * ktime_before - Compare if a ktime_t value is smaller than another one.
 * @cmp1:	comparable1
 * @cmp2:	comparable2
 *
 * Return: true if cmp1 happened before cmp2.
 */
static inline bool ktime_before(const ktime_t cmp1, const ktime_t cmp2)
{
	return ktime_compare(cmp1, cmp2) < 0;
}

#if BITS_PER_LONG < 64
extern s64 __ktime_divns(const ktime_t kt, s64 div);
static inline s64 ktime_divns(const ktime_t kt, s64 div)
{
	/*
	 * Negative divisors could cause an inf loop,
	 * so bug out here.
	 */
	BUG_ON(div < 0);
	if (__builtin_constant_p(div) && !(div >> 32)) {
		s64 ns = kt;
		u64 tmp = ns < 0 ? -ns : ns;

		do_div(tmp, div);
		return ns < 0 ? -tmp : tmp;
	} else {
		return __ktime_divns(kt, div);
	}
}
#else /* BITS_PER_LONG < 64 */
static inline s64 ktime_divns(const ktime_t kt, s64 div)
{
	/*
	 * 32-bit implementation cannot handle negative divisors,
	 * so catch them on 64bit as well.
	 */
	WARN_ON(div < 0);
	return kt / div;
}
#endif

static inline s64 ktime_to_us(const ktime_t kt)
{
	return ktime_divns(kt, NSEC_PER_USEC);
}

static inline s64 ktime_to_ms(const ktime_t kt)
{
	return ktime_divns(kt, NSEC_PER_MSEC);
}

static inline s64 ktime_us_delta(const ktime_t later, const ktime_t earlier)
{
       return ktime_to_us(ktime_sub(later, earlier));
}

static inline s64 ktime_ms_delta(const ktime_t later, const ktime_t earlier)
{
	return ktime_to_ms(ktime_sub(later, earlier));
}

static inline ktime_t ktime_add_us(const ktime_t kt, const u64 usec)
{
	return ktime_add_ns(kt, usec * NSEC_PER_USEC);
}

static inline ktime_t ktime_add_ms(const ktime_t kt, const u64 msec)
{
	return ktime_add_ns(kt, msec * NSEC_PER_MSEC);
}

static inline ktime_t ktime_sub_us(const ktime_t kt, const u64 usec)
{
	return ktime_sub_ns(kt, usec * NSEC_PER_USEC);
}

static inline ktime_t ktime_sub_ms(const ktime_t kt, const u64 msec)
{
	return ktime_sub_ns(kt, msec * NSEC_PER_MSEC);
}

extern ktime_t ktime_add_safe(const ktime_t lhs, const ktime_t rhs);

/**
 * ktime_to_timespec_cond - convert a ktime_t variable to timespec
 *			    format only if the variable contains data
 * @kt:		the ktime_t variable to convert
 * @ts:		the timespec variable to store the result in
 *
 * Return: %true if there was a successful conversion, %false if kt was 0.
 */
static inline __must_check bool ktime_to_timespec_cond(const ktime_t kt,
						       struct timespec *ts)
{
	if (kt) {
		*ts = ktime_to_timespec(kt);
		return true;
	} else {
		return false;
	}
}

/**
 * ktime_to_timespec64_cond - convert a ktime_t variable to timespec64
 *			    format only if the variable contains data
 * @kt:		the ktime_t variable to convert
 * @ts:		the timespec variable to store the result in
 *
 * Return: %true if there was a successful conversion, %false if kt was 0.
 */
static inline __must_check bool ktime_to_timespec64_cond(const ktime_t kt,
						       struct timespec64 *ts)
{
	if (kt) {
		*ts = ktime_to_timespec64(kt);
		return true;
	} else {
		return false;
	}
}

/*
 * The resolution of the clocks. The resolution value is returned in
 * the clock_getres() system call to give application programmers an
 * idea of the (in)accuracy of timers. Timer values are rounded up to
 * this resolution values.
 */
#define LOW_RES_NSEC		TICK_NSEC
#define KTIME_LOW_RES		(LOW_RES_NSEC)

static inline ktime_t ns_to_ktime(u64 ns)
{
	return ns;
}

static inline ktime_t ms_to_ktime(u64 ms)
{
	return ms * NSEC_PER_MSEC;
}

# include <linux/timekeeping.h>
# include <linux/timekeeping32.h>

#endif
