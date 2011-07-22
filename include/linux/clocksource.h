/*  linux/include/linux/clocksource.h
 *
 *  This file contains the structure definitions for clocksources.
 *
 *  If you are not a clocksource, or timekeeping code, you should
 *  not be including this file!
 */
#ifndef _LINUX_CLOCKSOURCE_H
#define _LINUX_CLOCKSOURCE_H

#include <linux/types.h>
#include <linux/timex.h>
#include <linux/time.h>
#include <linux/list.h>
#include <linux/cache.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <asm/div64.h>
#include <asm/io.h>

/* clocksource cycle base type */
typedef u64 cycle_t;
struct clocksource;

/**
 * struct cyclecounter - hardware abstraction for a free running counter
 *	Provides completely state-free accessors to the underlying hardware.
 *	Depending on which hardware it reads, the cycle counter may wrap
 *	around quickly. Locking rules (if necessary) have to be defined
 *	by the implementor and user of specific instances of this API.
 *
 * @read:		returns the current cycle value
 * @mask:		bitmask for two's complement
 *			subtraction of non 64 bit counters,
 *			see CLOCKSOURCE_MASK() helper macro
 * @mult:		cycle to nanosecond multiplier
 * @shift:		cycle to nanosecond divisor (power of two)
 */
struct cyclecounter {
	cycle_t (*read)(const struct cyclecounter *cc);
	cycle_t mask;
	u32 mult;
	u32 shift;
};

/**
 * struct timecounter - layer above a %struct cyclecounter which counts nanoseconds
 *	Contains the state needed by timecounter_read() to detect
 *	cycle counter wrap around. Initialize with
 *	timecounter_init(). Also used to convert cycle counts into the
 *	corresponding nanosecond counts with timecounter_cyc2time(). Users
 *	of this code are responsible for initializing the underlying
 *	cycle counter hardware, locking issues and reading the time
 *	more often than the cycle counter wraps around. The nanosecond
 *	counter will only wrap around after ~585 years.
 *
 * @cc:			the cycle counter used by this instance
 * @cycle_last:		most recent cycle counter value seen by
 *			timecounter_read()
 * @nsec:		continuously increasing count
 */
struct timecounter {
	const struct cyclecounter *cc;
	cycle_t cycle_last;
	u64 nsec;
};

/**
 * cyclecounter_cyc2ns - converts cycle counter cycles to nanoseconds
 * @tc:		Pointer to cycle counter.
 * @cycles:	Cycles
 *
 * XXX - This could use some mult_lxl_ll() asm optimization. Same code
 * as in cyc2ns, but with unsigned result.
 */
static inline u64 cyclecounter_cyc2ns(const struct cyclecounter *cc,
				      cycle_t cycles)
{
	u64 ret = (u64)cycles;
	ret = (ret * cc->mult) >> cc->shift;
	return ret;
}

/**
 * timecounter_init - initialize a time counter
 * @tc:			Pointer to time counter which is to be initialized/reset
 * @cc:			A cycle counter, ready to be used.
 * @start_tstamp:	Arbitrary initial time stamp.
 *
 * After this call the current cycle register (roughly) corresponds to
 * the initial time stamp. Every call to timecounter_read() increments
 * the time stamp counter by the number of elapsed nanoseconds.
 */
extern void timecounter_init(struct timecounter *tc,
			     const struct cyclecounter *cc,
			     u64 start_tstamp);

/**
 * timecounter_read - return nanoseconds elapsed since timecounter_init()
 *                    plus the initial time stamp
 * @tc:          Pointer to time counter.
 *
 * In other words, keeps track of time since the same epoch as
 * the function which generated the initial time stamp.
 */
extern u64 timecounter_read(struct timecounter *tc);

/**
 * timecounter_cyc2time - convert a cycle counter to same
 *                        time base as values returned by
 *                        timecounter_read()
 * @tc:		Pointer to time counter.
 * @cycle:	a value returned by tc->cc->read()
 *
 * Cycle counts that are converted correctly as long as they
 * fall into the interval [-1/2 max cycle count, +1/2 max cycle count],
 * with "max cycle count" == cs->mask+1.
 *
 * This allows conversion of cycle counter values which were generated
 * in the past.
 */
extern u64 timecounter_cyc2time(struct timecounter *tc,
				cycle_t cycle_tstamp);

/**
 * struct clocksource - hardware abstraction for a free running counter
 *	Provides mostly state-free accessors to the underlying hardware.
 *	This is the structure used for system time.
 *
 * @name:		ptr to clocksource name
 * @list:		list head for registration
 * @rating:		rating value for selection (higher is better)
 *			To avoid rating inflation the following
 *			list should give you a guide as to how
 *			to assign your clocksource a rating
 *			1-99: Unfit for real use
 *				Only available for bootup and testing purposes.
 *			100-199: Base level usability.
 *				Functional for real use, but not desired.
 *			200-299: Good.
 *				A correct and usable clocksource.
 *			300-399: Desired.
 *				A reasonably fast and accurate clocksource.
 *			400-499: Perfect
 *				The ideal clocksource. A must-use where
 *				available.
 * @read:		returns a cycle value, passes clocksource as argument
 * @enable:		optional function to enable the clocksource
 * @disable:		optional function to disable the clocksource
 * @mask:		bitmask for two's complement
 *			subtraction of non 64 bit counters
 * @mult:		cycle to nanosecond multiplier
 * @shift:		cycle to nanosecond divisor (power of two)
 * @max_idle_ns:	max idle time permitted by the clocksource (nsecs)
 * @flags:		flags describing special properties
 * @vread:		vsyscall based read
 * @suspend:		suspend function for the clocksource, if necessary
 * @resume:		resume function for the clocksource, if necessary
 */
struct clocksource {
	/*
	 * Hotpath data, fits in a single cache line when the
	 * clocksource itself is cacheline aligned.
	 */
	cycle_t (*read)(struct clocksource *cs);
	cycle_t cycle_last;
	cycle_t mask;
	u32 mult;
	u32 shift;
	u64 max_idle_ns;

#ifdef CONFIG_IA64
	void *fsys_mmio;        /* used by fsyscall asm code */
#define CLKSRC_FSYS_MMIO_SET(mmio, addr)      ((mmio) = (addr))
#else
#define CLKSRC_FSYS_MMIO_SET(mmio, addr)      do { } while (0)
#endif
	const char *name;
	struct list_head list;
	int rating;
	cycle_t (*vread)(void);
	int (*enable)(struct clocksource *cs);
	void (*disable)(struct clocksource *cs);
	unsigned long flags;
	void (*suspend)(struct clocksource *cs);
	void (*resume)(struct clocksource *cs);

#ifdef CONFIG_CLOCKSOURCE_WATCHDOG
	/* Watchdog related data, used by the framework */
	struct list_head wd_list;
	cycle_t cs_last;
	cycle_t wd_last;
#endif
} ____cacheline_aligned;

/*
 * Clock source flags bits::
 */
#define CLOCK_SOURCE_IS_CONTINUOUS		0x01
#define CLOCK_SOURCE_MUST_VERIFY		0x02

#define CLOCK_SOURCE_WATCHDOG			0x10
#define CLOCK_SOURCE_VALID_FOR_HRES		0x20
#define CLOCK_SOURCE_UNSTABLE			0x40

/* simplify initialization of mask field */
#define CLOCKSOURCE_MASK(bits) (cycle_t)((bits) < 64 ? ((1ULL<<(bits))-1) : -1)

/**
 * clocksource_khz2mult - calculates mult from khz and shift
 * @khz:		Clocksource frequency in KHz
 * @shift_constant:	Clocksource shift factor
 *
 * Helper functions that converts a khz counter frequency to a timsource
 * multiplier, given the clocksource shift value
 */
static inline u32 clocksource_khz2mult(u32 khz, u32 shift_constant)
{
	/*  khz = cyc/(Million ns)
	 *  mult/2^shift  = ns/cyc
	 *  mult = ns/cyc * 2^shift
	 *  mult = 1Million/khz * 2^shift
	 *  mult = 1000000 * 2^shift / khz
	 *  mult = (1000000<<shift) / khz
	 */
	u64 tmp = ((u64)1000000) << shift_constant;

	tmp += khz/2; /* round for do_div */
	do_div(tmp, khz);

	return (u32)tmp;
}

/**
 * clocksource_hz2mult - calculates mult from hz and shift
 * @hz:			Clocksource frequency in Hz
 * @shift_constant:	Clocksource shift factor
 *
 * Helper functions that converts a hz counter
 * frequency to a timsource multiplier, given the
 * clocksource shift value
 */
static inline u32 clocksource_hz2mult(u32 hz, u32 shift_constant)
{
	/*  hz = cyc/(Billion ns)
	 *  mult/2^shift  = ns/cyc
	 *  mult = ns/cyc * 2^shift
	 *  mult = 1Billion/hz * 2^shift
	 *  mult = 1000000000 * 2^shift / hz
	 *  mult = (1000000000<<shift) / hz
	 */
	u64 tmp = ((u64)1000000000) << shift_constant;

	tmp += hz/2; /* round for do_div */
	do_div(tmp, hz);

	return (u32)tmp;
}

/**
 * clocksource_cyc2ns - converts clocksource cycles to nanoseconds
 *
 * Converts cycles to nanoseconds, using the given mult and shift.
 *
 * XXX - This could use some mult_lxl_ll() asm optimization
 */
static inline s64 clocksource_cyc2ns(cycle_t cycles, u32 mult, u32 shift)
{
	return ((u64) cycles * mult) >> shift;
}


extern int clocksource_register(struct clocksource*);
extern void clocksource_unregister(struct clocksource*);
extern void clocksource_touch_watchdog(void);
extern struct clocksource* clocksource_get_next(void);
extern void clocksource_change_rating(struct clocksource *cs, int rating);
extern void clocksource_suspend(void);
extern void clocksource_resume(void);
extern struct clocksource * __init __weak clocksource_default_clock(void);
extern void clocksource_mark_unstable(struct clocksource *cs);

extern void
clocks_calc_mult_shift(u32 *mult, u32 *shift, u32 from, u32 to, u32 minsec);

/*
 * Don't call __clocksource_register_scale directly, use
 * clocksource_register_hz/khz
 */
extern int
__clocksource_register_scale(struct clocksource *cs, u32 scale, u32 freq);
extern void
__clocksource_updatefreq_scale(struct clocksource *cs, u32 scale, u32 freq);

static inline int clocksource_register_hz(struct clocksource *cs, u32 hz)
{
	return __clocksource_register_scale(cs, 1, hz);
}

static inline int clocksource_register_khz(struct clocksource *cs, u32 khz)
{
	return __clocksource_register_scale(cs, 1000, khz);
}

static inline void __clocksource_updatefreq_hz(struct clocksource *cs, u32 hz)
{
	__clocksource_updatefreq_scale(cs, 1, hz);
}

static inline void __clocksource_updatefreq_khz(struct clocksource *cs, u32 khz)
{
	__clocksource_updatefreq_scale(cs, 1000, khz);
}

static inline void
clocksource_calc_mult_shift(struct clocksource *cs, u32 freq, u32 minsec)
{
	return clocks_calc_mult_shift(&cs->mult, &cs->shift, freq,
				      NSEC_PER_SEC, minsec);
}

#ifdef CONFIG_GENERIC_TIME_VSYSCALL
extern void
update_vsyscall(struct timespec *ts, struct timespec *wtm,
			struct clocksource *c, u32 mult);
extern void update_vsyscall_tz(void);
#else
static inline void
update_vsyscall(struct timespec *ts, struct timespec *wtm,
			struct clocksource *c, u32 mult)
{
}

static inline void update_vsyscall_tz(void)
{
}
#endif

extern void timekeeping_notify(struct clocksource *clock);

extern cycle_t clocksource_mmio_readl_up(struct clocksource *);
extern cycle_t clocksource_mmio_readl_down(struct clocksource *);
extern cycle_t clocksource_mmio_readw_up(struct clocksource *);
extern cycle_t clocksource_mmio_readw_down(struct clocksource *);

extern int clocksource_mmio_init(void __iomem *, const char *,
	unsigned long, int, unsigned, cycle_t (*)(struct clocksource *));

extern int clocksource_i8253_init(void);

#endif /* _LINUX_CLOCKSOURCE_H */
