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

/*
 * ktime_t:
 *
 * On 64-bit CPUs a single 64-bit variable is used to store the hrtimers
 * internal representation of time values in scalar nanoseconds. The
 * design plays out best on 64-bit CPUs, where most conversions are
 * NOPs and most arithmetic ktime_t operations are plain arithmetic
 * operations.
 *
 * On 32-bit CPUs an optimized representation of the timespec structure
 * is used to avoid expensive conversions from and to timespecs. The
 * endian-aware order of the tv struct members is choosen to allow
 * mathematical operations on the tv64 member of the union too, which
 * for certain operations produces better code.
 *
 * For architectures with efficient support for 64/32-bit conversions the
 * plain scalar nanosecond based representation can be selected by the
 * config switch CONFIG_KTIME_SCALAR.
 */
union ktime {
	s64	tv64;
#if BITS_PER_LONG != 64 && !defined(CONFIG_KTIME_SCALAR)
	struct {
# ifdef __BIG_ENDIAN
	s32	sec, nsec;
# else
	s32	nsec, sec;
# endif
	} tv;
#endif
};

typedef union ktime ktime_t;		/* Kill this */

#define KTIME_MAX			((s64)~((u64)1 << 63))
#if (BITS_PER_LONG == 64)
# define KTIME_SEC_MAX			(KTIME_MAX / NSEC_PER_SEC)
#else
# define KTIME_SEC_MAX			LONG_MAX
#endif

/*
 * ktime_t definitions when using the 64-bit scalar representation:
 */

#if (BITS_PER_LONG == 64) || defined(CONFIG_KTIME_SCALAR)

/**
 * ktime_set - Set a ktime_t variable from a seconds/nanoseconds value
 * @secs:	seconds to set
 * @nsecs:	nanoseconds to set
 *
 * Return the ktime_t representation of the value
 */
static inline ktime_t ktime_set(const long secs, const unsigned long nsecs)
{
#if (BITS_PER_LONG == 64)
	if (unlikely(secs >= KTIME_SEC_MAX))
		return (ktime_t){ .tv64 = KTIME_MAX };
#endif
	return (ktime_t) { .tv64 = (s64)secs * NSEC_PER_SEC + (s64)nsecs };
}

/* Subtract two ktime_t variables. rem = lhs -rhs: */
#define ktime_sub(lhs, rhs) \
		({ (ktime_t){ .tv64 = (lhs).tv64 - (rhs).tv64 }; })

/* Add two ktime_t variables. res = lhs + rhs: */
#define ktime_add(lhs, rhs) \
		({ (ktime_t){ .tv64 = (lhs).tv64 + (rhs).tv64 }; })

/*
 * Add a ktime_t variable and a scalar nanosecond value.
 * res = kt + nsval:
 */
#define ktime_add_ns(kt, nsval) \
		({ (ktime_t){ .tv64 = (kt).tv64 + (nsval) }; })

/* convert a timespec to ktime_t format: */
static inline ktime_t timespec_to_ktime(struct timespec ts)
{
	return ktime_set(ts.tv_sec, ts.tv_nsec);
}

/* convert a timeval to ktime_t format: */
static inline ktime_t timeval_to_ktime(struct timeval tv)
{
	return ktime_set(tv.tv_sec, tv.tv_usec * NSEC_PER_USEC);
}

/* Map the ktime_t to timespec conversion to ns_to_timespec function */
#define ktime_to_timespec(kt)		ns_to_timespec((kt).tv64)

/* Map the ktime_t to timeval conversion to ns_to_timeval function */
#define ktime_to_timeval(kt)		ns_to_timeval((kt).tv64)

/* Convert ktime_t to nanoseconds - NOP in the scalar storage format: */
#define ktime_to_ns(kt)			((kt).tv64)

#else

/*
 * Helper macros/inlines to get the ktime_t math right in the timespec
 * representation. The macros are sometimes ugly - their actual use is
 * pretty okay-ish, given the circumstances. We do all this for
 * performance reasons. The pure scalar nsec_t based code was nice and
 * simple, but created too many 64-bit / 32-bit conversions and divisions.
 *
 * Be especially aware that negative values are represented in a way
 * that the tv.sec field is negative and the tv.nsec field is greater
 * or equal to zero but less than nanoseconds per second. This is the
 * same representation which is used by timespecs.
 *
 *   tv.sec < 0 and 0 >= tv.nsec < NSEC_PER_SEC
 */

/* Set a ktime_t variable to a value in sec/nsec representation: */
static inline ktime_t ktime_set(const long secs, const unsigned long nsecs)
{
	return (ktime_t) { .tv = { .sec = secs, .nsec = nsecs } };
}

/**
 * ktime_sub - subtract two ktime_t variables
 * @lhs:	minuend
 * @rhs:	subtrahend
 *
 * Returns the remainder of the substraction
 */
static inline ktime_t ktime_sub(const ktime_t lhs, const ktime_t rhs)
{
	ktime_t res;

	res.tv64 = lhs.tv64 - rhs.tv64;
	if (res.tv.nsec < 0)
		res.tv.nsec += NSEC_PER_SEC;

	return res;
}

/**
 * ktime_add - add two ktime_t variables
 * @add1:	addend1
 * @add2:	addend2
 *
 * Returns the sum of @add1 and @add2.
 */
static inline ktime_t ktime_add(const ktime_t add1, const ktime_t add2)
{
	ktime_t res;

	res.tv64 = add1.tv64 + add2.tv64;
	/*
	 * performance trick: the (u32) -NSEC gives 0x00000000Fxxxxxxx
	 * so we subtract NSEC_PER_SEC and add 1 to the upper 32 bit.
	 *
	 * it's equivalent to:
	 *   tv.nsec -= NSEC_PER_SEC
	 *   tv.sec ++;
	 */
	if (res.tv.nsec >= NSEC_PER_SEC)
		res.tv64 += (u32)-NSEC_PER_SEC;

	return res;
}

/**
 * ktime_add_ns - Add a scalar nanoseconds value to a ktime_t variable
 * @kt:		addend
 * @nsec:	the scalar nsec value to add
 *
 * Returns the sum of @kt and @nsec in ktime_t format
 */
extern ktime_t ktime_add_ns(const ktime_t kt, u64 nsec);

/**
 * timespec_to_ktime - convert a timespec to ktime_t format
 * @ts:		the timespec variable to convert
 *
 * Returns a ktime_t variable with the converted timespec value
 */
static inline ktime_t timespec_to_ktime(const struct timespec ts)
{
	return (ktime_t) { .tv = { .sec = (s32)ts.tv_sec,
			   	   .nsec = (s32)ts.tv_nsec } };
}

/**
 * timeval_to_ktime - convert a timeval to ktime_t format
 * @tv:		the timeval variable to convert
 *
 * Returns a ktime_t variable with the converted timeval value
 */
static inline ktime_t timeval_to_ktime(const struct timeval tv)
{
	return (ktime_t) { .tv = { .sec = (s32)tv.tv_sec,
				   .nsec = (s32)tv.tv_usec * 1000 } };
}

/**
 * ktime_to_timespec - convert a ktime_t variable to timespec format
 * @kt:		the ktime_t variable to convert
 *
 * Returns the timespec representation of the ktime value
 */
static inline struct timespec ktime_to_timespec(const ktime_t kt)
{
	return (struct timespec) { .tv_sec = (time_t) kt.tv.sec,
				   .tv_nsec = (long) kt.tv.nsec };
}

/**
 * ktime_to_timeval - convert a ktime_t variable to timeval format
 * @kt:		the ktime_t variable to convert
 *
 * Returns the timeval representation of the ktime value
 */
static inline struct timeval ktime_to_timeval(const ktime_t kt)
{
	return (struct timeval) {
		.tv_sec = (time_t) kt.tv.sec,
		.tv_usec = (suseconds_t) (kt.tv.nsec / NSEC_PER_USEC) };
}

/**
 * ktime_to_ns - convert a ktime_t variable to scalar nanoseconds
 * @kt:		the ktime_t variable to convert
 *
 * Returns the scalar nanoseconds representation of @kt
 */
static inline s64 ktime_to_ns(const ktime_t kt)
{
	return (s64) kt.tv.sec * NSEC_PER_SEC + kt.tv.nsec;
}

#endif

/**
 * ktime_equal - Compares two ktime_t variables to see if they are equal
 * @cmp1:	comparable1
 * @cmp2:	comparable2
 *
 * Compare two ktime_t variables, returns 1 if equal
 */
static inline int ktime_equal(const ktime_t cmp1, const ktime_t cmp2)
{
	return cmp1.tv64 == cmp2.tv64;
}

static inline s64 ktime_to_us(const ktime_t kt)
{
	struct timeval tv = ktime_to_timeval(kt);
	return (s64) tv.tv_sec * USEC_PER_SEC + tv.tv_usec;
}

/*
 * The resolution of the clocks. The resolution value is returned in
 * the clock_getres() system call to give application programmers an
 * idea of the (in)accuracy of timers. Timer values are rounded up to
 * this resolution values.
 */
#define KTIME_LOW_RES		(ktime_t){ .tv64 = TICK_NSEC }

/* Get the monotonic time in timespec format: */
extern void ktime_get_ts(struct timespec *ts);

/* Get the real (wall-) time in timespec format: */
#define ktime_get_real_ts(ts)	getnstimeofday(ts)

#endif
