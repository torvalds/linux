#ifndef _LINUX_TIMEKEEPING_H
#define _LINUX_TIMEKEEPING_H

/* Included from linux/ktime.h */

void timekeeping_init(void);
extern int timekeeping_suspended;

/*
 * Get and set timeofday
 */
extern void do_gettimeofday(struct timeval *tv);
extern int do_settimeofday(const struct timespec *tv);
extern int do_sys_settimeofday(const struct timespec *tv,
			       const struct timezone *tz);

/*
 * Kernel time accessors
 */
unsigned long get_seconds(void);
struct timespec current_kernel_time(void);
/* does not take xtime_lock */
struct timespec __current_kernel_time(void);

/*
 * timespec based interfaces
 */
struct timespec get_monotonic_coarse(void);
extern void getrawmonotonic(struct timespec *ts);
extern void monotonic_to_bootbased(struct timespec *ts);
extern void get_monotonic_boottime(struct timespec *ts);
extern void ktime_get_ts(struct timespec *ts);

extern int __getnstimeofday(struct timespec *tv);
extern void getnstimeofday(struct timespec *tv);
extern void getboottime(struct timespec *ts);

#define do_posix_clock_monotonic_gettime(ts) ktime_get_ts(ts)
#define ktime_get_real_ts(ts)	getnstimeofday(ts)


/*
 * ktime_t based interfaces
 */
extern ktime_t ktime_get(void);
extern ktime_t ktime_get_real(void);
extern ktime_t ktime_get_boottime(void);
extern ktime_t ktime_get_monotonic_offset(void);
extern ktime_t ktime_get_clocktai(void);

/*
 * RTC specific
 */
extern void timekeeping_inject_sleeptime(struct timespec *delta);

/*
 * PPS accessor
 */
extern void getnstime_raw_and_real(struct timespec *ts_raw,
				   struct timespec *ts_real);

/*
 * Persistent clock related interfaces
 */
extern bool persistent_clock_exist;
extern int persistent_clock_is_local;

static inline bool has_persistent_clock(void)
{
	return persistent_clock_exist;
}

extern void read_persistent_clock(struct timespec *ts);
extern void read_boot_clock(struct timespec *ts);
extern int update_persistent_clock(struct timespec now);


#endif
