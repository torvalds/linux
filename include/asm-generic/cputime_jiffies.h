#ifndef _ASM_GENERIC_CPUTIME_JIFFIES_H
#define _ASM_GENERIC_CPUTIME_JIFFIES_H

typedef unsigned long __nocast cputime_t;

#define cputime_one_jiffy		jiffies_to_cputime(1)
#define cputime_to_jiffies(__ct)	(__force unsigned long)(__ct)
#define cputime_to_scaled(__ct)		(__ct)
#define jiffies_to_cputime(__hz)	(__force cputime_t)(__hz)

typedef u64 __nocast cputime64_t;

#define cputime64_to_jiffies64(__ct)	(__force u64)(__ct)
#define jiffies64_to_cputime64(__jif)	(__force cputime64_t)(__jif)


/*
 * Convert nanoseconds to cputime
 */
#define nsecs_to_cputime64(__nsec)	\
	jiffies64_to_cputime64(nsecs_to_jiffies64(__nsec))
#define nsecs_to_cputime(__nsec)	\
	jiffies_to_cputime(nsecs_to_jiffies(__nsec))


/*
 * Convert cputime to microseconds and back.
 */
#define cputime_to_usecs(__ct)		\
	jiffies_to_usecs(cputime_to_jiffies(__ct))
#define usecs_to_cputime(__usec)	\
	jiffies_to_cputime(usecs_to_jiffies(__usec))
#define usecs_to_cputime64(__usec)	\
	jiffies64_to_cputime64(nsecs_to_jiffies64((__usec) * 1000))

/*
 * Convert cputime to seconds and back.
 */
#define cputime_to_secs(jif)		(cputime_to_jiffies(jif) / HZ)
#define secs_to_cputime(sec)		jiffies_to_cputime((sec) * HZ)

/*
 * Convert cputime to timespec and back.
 */
#define timespec_to_cputime(__val)	\
	jiffies_to_cputime(timespec_to_jiffies(__val))
#define cputime_to_timespec(__ct,__val)	\
	jiffies_to_timespec(cputime_to_jiffies(__ct),__val)

/*
 * Convert cputime to timeval and back.
 */
#define timeval_to_cputime(__val)	\
	jiffies_to_cputime(timeval_to_jiffies(__val))
#define cputime_to_timeval(__ct,__val)	\
	jiffies_to_timeval(cputime_to_jiffies(__ct),__val)

/*
 * Convert cputime to clock and back.
 */
#define cputime_to_clock_t(__ct)	\
	jiffies_to_clock_t(cputime_to_jiffies(__ct))
#define clock_t_to_cputime(__x)		\
	jiffies_to_cputime(clock_t_to_jiffies(__x))

/*
 * Convert cputime64 to clock.
 */
#define cputime64_to_clock_t(__ct)	\
	jiffies_64_to_clock_t(cputime64_to_jiffies64(__ct))

#endif
