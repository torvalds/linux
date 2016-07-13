#ifndef _LINUX_TIME64_H
#define _LINUX_TIME64_H

#include <uapi/linux/time.h>
#include <linux/math64.h>

typedef __s64 time64_t;

/*
 * This wants to go into uapi/linux/time.h once we agreed about the
 * userspace interfaces.
 */
#if __BITS_PER_LONG == 64
# define timespec64 timespec
#define itimerspec64 itimerspec
#else
struct timespec64 {
	time64_t	tv_sec;			/* seconds */
	long		tv_nsec;		/* nanoseconds */
};

struct itimerspec64 {
	struct timespec64 it_interval;
	struct timespec64 it_value;
};

#endif

/* Parameters used to convert the timespec values: */
#define MSEC_PER_SEC	1000L
#define USEC_PER_MSEC	1000L
#define NSEC_PER_USEC	1000L
#define NSEC_PER_MSEC	1000000L
#define USEC_PER_SEC	1000000L
#define NSEC_PER_SEC	1000000000L
#define FSEC_PER_SEC	1000000000000000LL

/* Located here for timespec[64]_valid_strict */
#define TIME64_MAX			((s64)~((u64)1 << 63))
#define KTIME_MAX			((s64)~((u64)1 << 63))
#define KTIME_SEC_MAX			(KTIME_MAX / NSEC_PER_SEC)

#if __BITS_PER_LONG == 64

static inline struct timespec timespec64_to_timespec(const struct timespec64 ts64)
{
	return ts64;
}

static inline struct timespec64 timespec_to_timespec64(const struct timespec ts)
{
	return ts;
}

static inline struct itimerspec itimerspec64_to_itimerspec(struct itimerspec64 *its64)
{
	return *its64;
}

static inline struct itimerspec64 itimerspec_to_itimerspec64(struct itimerspec *its)
{
	return *its;
}

# define timespec64_equal		timespec_equal
# define timespec64_compare		timespec_compare
# define set_normalized_timespec64	set_normalized_timespec
# define timespec64_add			timespec_add
# define timespec64_sub			timespec_sub
# define timespec64_valid		timespec_valid
# define timespec64_valid_strict	timespec_valid_strict
# define timespec64_to_ns		timespec_to_ns
# define ns_to_timespec64		ns_to_timespec
# define timespec64_add_ns		timespec_add_ns

#else

static inline struct timespec timespec64_to_timespec(const struct timespec64 ts64)
{
	struct timespec ret;

	ret.tv_sec = (time_t)ts64.tv_sec;
	ret.tv_nsec = ts64.tv_nsec;
	return ret;
}

static inline struct timespec64 timespec_to_timespec64(const struct timespec ts)
{
	struct timespec64 ret;

	ret.tv_sec = ts.tv_sec;
	ret.tv_nsec = ts.tv_nsec;
	return ret;
}

static inline struct itimerspec itimerspec64_to_itimerspec(struct itimerspec64 *its64)
{
	struct itimerspec ret;

	ret.it_interval = timespec64_to_timespec(its64->it_interval);
	ret.it_value = timespec64_to_timespec(its64->it_value);
	return ret;
}

static inline struct itimerspec64 itimerspec_to_itimerspec64(struct itimerspec *its)
{
	struct itimerspec64 ret;

	ret.it_interval = timespec_to_timespec64(its->it_interval);
	ret.it_value = timespec_to_timespec64(its->it_value);
	return ret;
}

static inline int timespec64_equal(const struct timespec64 *a,
				   const struct timespec64 *b)
{
	return (a->tv_sec == b->tv_sec) && (a->tv_nsec == b->tv_nsec);
}

/*
 * lhs < rhs:  return <0
 * lhs == rhs: return 0
 * lhs > rhs:  return >0
 */
static inline int timespec64_compare(const struct timespec64 *lhs, const struct timespec64 *rhs)
{
	if (lhs->tv_sec < rhs->tv_sec)
		return -1;
	if (lhs->tv_sec > rhs->tv_sec)
		return 1;
	return lhs->tv_nsec - rhs->tv_nsec;
}

extern void set_normalized_timespec64(struct timespec64 *ts, time64_t sec, s64 nsec);

static inline struct timespec64 timespec64_add(struct timespec64 lhs,
						struct timespec64 rhs)
{
	struct timespec64 ts_delta;
	set_normalized_timespec64(&ts_delta, lhs.tv_sec + rhs.tv_sec,
				lhs.tv_nsec + rhs.tv_nsec);
	return ts_delta;
}

/*
 * sub = lhs - rhs, in normalized form
 */
static inline struct timespec64 timespec64_sub(struct timespec64 lhs,
						struct timespec64 rhs)
{
	struct timespec64 ts_delta;
	set_normalized_timespec64(&ts_delta, lhs.tv_sec - rhs.tv_sec,
				lhs.tv_nsec - rhs.tv_nsec);
	return ts_delta;
}

/*
 * Returns true if the timespec64 is norm, false if denorm:
 */
static inline bool timespec64_valid(const struct timespec64 *ts)
{
	/* Dates before 1970 are bogus */
	if (ts->tv_sec < 0)
		return false;
	/* Can't have more nanoseconds then a second */
	if ((unsigned long)ts->tv_nsec >= NSEC_PER_SEC)
		return false;
	return true;
}

static inline bool timespec64_valid_strict(const struct timespec64 *ts)
{
	if (!timespec64_valid(ts))
		return false;
	/* Disallow values that could overflow ktime_t */
	if ((unsigned long long)ts->tv_sec >= KTIME_SEC_MAX)
		return false;
	return true;
}

/**
 * timespec64_to_ns - Convert timespec64 to nanoseconds
 * @ts:		pointer to the timespec64 variable to be converted
 *
 * Returns the scalar nanosecond representation of the timespec64
 * parameter.
 */
static inline s64 timespec64_to_ns(const struct timespec64 *ts)
{
	return ((s64) ts->tv_sec * NSEC_PER_SEC) + ts->tv_nsec;
}

/**
 * ns_to_timespec64 - Convert nanoseconds to timespec64
 * @nsec:	the nanoseconds value to be converted
 *
 * Returns the timespec64 representation of the nsec parameter.
 */
extern struct timespec64 ns_to_timespec64(const s64 nsec);

/**
 * timespec64_add_ns - Adds nanoseconds to a timespec64
 * @a:		pointer to timespec64 to be incremented
 * @ns:		unsigned nanoseconds value to be added
 *
 * This must always be inlined because its used from the x86-64 vdso,
 * which cannot call other kernel functions.
 */
static __always_inline void timespec64_add_ns(struct timespec64 *a, u64 ns)
{
	a->tv_sec += __iter_div_u64_rem(a->tv_nsec + ns, NSEC_PER_SEC, &ns);
	a->tv_nsec = ns;
}

#endif

/*
 * timespec64_add_safe assumes both values are positive and checks for
 * overflow. It will return TIME64_MAX in case of overflow.
 */
extern struct timespec64 timespec64_add_safe(const struct timespec64 lhs,
					 const struct timespec64 rhs);

#endif /* _LINUX_TIME64_H */
