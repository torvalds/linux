/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIMEKEEPING_H
#define _LINUX_TIMEKEEPING_H

#include <linux/errno.h>

/* Included from linux/ktime.h */

void timekeeping_init(void);
extern int timekeeping_suspended;

/* Architecture timer tick functions: */
extern void update_process_times(int user);
extern void xtime_update(unsigned long ticks);

/*
 * Get and set timeofday
 */
extern int do_settimeofday64(const struct timespec64 *ts);
extern int do_sys_settimeofday64(const struct timespec64 *tv,
				 const struct timezone *tz);

/*
 * ktime_get() family: read the current time in a multitude of ways,
 *
 * The default time reference is CLOCK_MONOTONIC, starting at
 * boot time but not counting the time spent in suspend.
 * For other references, use the functions with "real", "clocktai",
 * "boottime" and "raw" suffixes.
 *
 * To get the time in a different format, use the ones wit
 * "ns", "ts64" and "seconds" suffix.
 *
 * See Documentation/core-api/timekeeping.rst for more details.
 */


/*
 * timespec64 based interfaces
 */
extern void ktime_get_raw_ts64(struct timespec64 *ts);
extern void ktime_get_ts64(struct timespec64 *ts);
extern void ktime_get_real_ts64(struct timespec64 *tv);
extern void ktime_get_coarse_ts64(struct timespec64 *ts);
extern void ktime_get_coarse_real_ts64(struct timespec64 *ts);

void getboottime64(struct timespec64 *ts);

/*
 * time64_t base interfaces
 */
extern time64_t ktime_get_seconds(void);
extern time64_t __ktime_get_real_seconds(void);
extern time64_t ktime_get_real_seconds(void);

/*
 * ktime_t based interfaces
 */

enum tk_offsets {
	TK_OFFS_REAL,
	TK_OFFS_BOOT,
	TK_OFFS_TAI,
	TK_OFFS_MAX,
};

extern ktime_t ktime_get(void);
extern ktime_t ktime_get_with_offset(enum tk_offsets offs);
extern ktime_t ktime_get_coarse_with_offset(enum tk_offsets offs);
extern ktime_t ktime_mono_to_any(ktime_t tmono, enum tk_offsets offs);
extern ktime_t ktime_get_raw(void);
extern u32 ktime_get_resolution_ns(void);

/**
 * ktime_get_real - get the real (wall-) time in ktime_t format
 */
static inline ktime_t ktime_get_real(void)
{
	return ktime_get_with_offset(TK_OFFS_REAL);
}

static inline ktime_t ktime_get_coarse_real(void)
{
	return ktime_get_coarse_with_offset(TK_OFFS_REAL);
}

/**
 * ktime_get_boottime - Returns monotonic time since boot in ktime_t format
 *
 * This is similar to CLOCK_MONTONIC/ktime_get, but also includes the
 * time spent in suspend.
 */
static inline ktime_t ktime_get_boottime(void)
{
	return ktime_get_with_offset(TK_OFFS_BOOT);
}

static inline ktime_t ktime_get_coarse_boottime(void)
{
	return ktime_get_coarse_with_offset(TK_OFFS_BOOT);
}

/**
 * ktime_get_clocktai - Returns the TAI time of day in ktime_t format
 */
static inline ktime_t ktime_get_clocktai(void)
{
	return ktime_get_with_offset(TK_OFFS_TAI);
}

static inline ktime_t ktime_get_coarse_clocktai(void)
{
	return ktime_get_coarse_with_offset(TK_OFFS_TAI);
}

static inline ktime_t ktime_get_coarse(void)
{
	struct timespec64 ts;

	ktime_get_coarse_ts64(&ts);
	return timespec64_to_ktime(ts);
}

static inline u64 ktime_get_coarse_ns(void)
{
	return ktime_to_ns(ktime_get_coarse());
}

static inline u64 ktime_get_coarse_real_ns(void)
{
	return ktime_to_ns(ktime_get_coarse_real());
}

static inline u64 ktime_get_coarse_boottime_ns(void)
{
	return ktime_to_ns(ktime_get_coarse_boottime());
}

static inline u64 ktime_get_coarse_clocktai_ns(void)
{
	return ktime_to_ns(ktime_get_coarse_clocktai());
}

/**
 * ktime_mono_to_real - Convert monotonic time to clock realtime
 */
static inline ktime_t ktime_mono_to_real(ktime_t mono)
{
	return ktime_mono_to_any(mono, TK_OFFS_REAL);
}

static inline u64 ktime_get_ns(void)
{
	return ktime_to_ns(ktime_get());
}

static inline u64 ktime_get_real_ns(void)
{
	return ktime_to_ns(ktime_get_real());
}

static inline u64 ktime_get_boottime_ns(void)
{
	return ktime_to_ns(ktime_get_boottime());
}

static inline u64 ktime_get_clocktai_ns(void)
{
	return ktime_to_ns(ktime_get_clocktai());
}

static inline u64 ktime_get_raw_ns(void)
{
	return ktime_to_ns(ktime_get_raw());
}

extern u64 ktime_get_mono_fast_ns(void);
extern u64 ktime_get_raw_fast_ns(void);
extern u64 ktime_get_boot_fast_ns(void);
extern u64 ktime_get_real_fast_ns(void);

/*
 * timespec64/time64_t interfaces utilizing the ktime based ones
 * for API completeness, these could be implemented more efficiently
 * if needed.
 */
static inline void ktime_get_boottime_ts64(struct timespec64 *ts)
{
	*ts = ktime_to_timespec64(ktime_get_boottime());
}

static inline void ktime_get_coarse_boottime_ts64(struct timespec64 *ts)
{
	*ts = ktime_to_timespec64(ktime_get_coarse_boottime());
}

static inline time64_t ktime_get_boottime_seconds(void)
{
	return ktime_divns(ktime_get_coarse_boottime(), NSEC_PER_SEC);
}

static inline void ktime_get_clocktai_ts64(struct timespec64 *ts)
{
	*ts = ktime_to_timespec64(ktime_get_clocktai());
}

static inline void ktime_get_coarse_clocktai_ts64(struct timespec64 *ts)
{
	*ts = ktime_to_timespec64(ktime_get_coarse_clocktai());
}

static inline time64_t ktime_get_clocktai_seconds(void)
{
	return ktime_divns(ktime_get_coarse_clocktai(), NSEC_PER_SEC);
}

/*
 * RTC specific
 */
extern bool timekeeping_rtc_skipsuspend(void);
extern bool timekeeping_rtc_skipresume(void);

extern void timekeeping_inject_sleeptime64(const struct timespec64 *delta);

/*
 * struct ktime_timestanps - Simultaneous mono/boot/real timestamps
 * @mono:	Monotonic timestamp
 * @boot:	Boottime timestamp
 * @real:	Realtime timestamp
 */
struct ktime_timestamps {
	u64		mono;
	u64		boot;
	u64		real;
};

/**
 * struct system_time_snapshot - simultaneous raw/real time capture with
 *				 counter value
 * @cycles:	Clocksource counter value to produce the system times
 * @real:	Realtime system time
 * @raw:	Monotonic raw system time
 * @clock_was_set_seq:	The sequence number of clock was set events
 * @cs_was_changed_seq:	The sequence number of clocksource change events
 */
struct system_time_snapshot {
	u64		cycles;
	ktime_t		real;
	ktime_t		raw;
	unsigned int	clock_was_set_seq;
	u8		cs_was_changed_seq;
};

/**
 * struct system_device_crosststamp - system/device cross-timestamp
 *				      (synchronized capture)
 * @device:		Device time
 * @sys_realtime:	Realtime simultaneous with device time
 * @sys_monoraw:	Monotonic raw simultaneous with device time
 */
struct system_device_crosststamp {
	ktime_t device;
	ktime_t sys_realtime;
	ktime_t sys_monoraw;
};

/**
 * struct system_counterval_t - system counter value with the pointer to the
 *				corresponding clocksource
 * @cycles:	System counter value
 * @cs:		Clocksource corresponding to system counter value. Used by
 *		timekeeping code to verify comparibility of two cycle values
 */
struct system_counterval_t {
	u64			cycles;
	struct clocksource	*cs;
};

/*
 * Get cross timestamp between system clock and device clock
 */
extern int get_device_system_crosststamp(
			int (*get_time_fn)(ktime_t *device_time,
				struct system_counterval_t *system_counterval,
				void *ctx),
			void *ctx,
			struct system_time_snapshot *history,
			struct system_device_crosststamp *xtstamp);

/*
 * Simultaneously snapshot realtime and monotonic raw clocks
 */
extern void ktime_get_snapshot(struct system_time_snapshot *systime_snapshot);

/* NMI safe mono/boot/realtime timestamps */
extern void ktime_get_fast_timestamps(struct ktime_timestamps *snap);

/*
 * Persistent clock related interfaces
 */
extern int persistent_clock_is_local;

extern void read_persistent_clock64(struct timespec64 *ts);
void read_persistent_wall_and_boot_offset(struct timespec64 *wall_clock,
					  struct timespec64 *boot_offset);
extern int update_persistent_clock64(struct timespec64 now);

#endif
