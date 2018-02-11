/* SPDX-License-Identifier: GPL-2.0 */
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
#include <linux/of.h>
#include <asm/div64.h>
#include <asm/io.h>

struct clocksource;
struct module;

#ifdef CONFIG_ARCH_CLOCKSOURCE_DATA
#include <asm/clocksource.h>
#endif

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
 * @maxadj:		maximum adjustment value to mult (~11%)
 * @max_cycles:		maximum safe cycle value which won't overflow on multiplication
 * @flags:		flags describing special properties
 * @archdata:		arch-specific data
 * @suspend:		suspend function for the clocksource, if necessary
 * @resume:		resume function for the clocksource, if necessary
 * @mark_unstable:	Optional function to inform the clocksource driver that
 *			the watchdog marked the clocksource unstable
 * @owner:		module reference, must be set by clocksource in modules
 *
 * Note: This struct is not used in hotpathes of the timekeeping code
 * because the timekeeper caches the hot path fields in its own data
 * structure, so no line cache alignment is required,
 *
 * The pointer to the clocksource itself is handed to the read
 * callback. If you need extra information there you can wrap struct
 * clocksource into your own struct. Depending on the amount of
 * information you need you should consider to cache line align that
 * structure.
 */
struct clocksource {
	u64 (*read)(struct clocksource *cs);
	u64 mask;
	u32 mult;
	u32 shift;
	u64 max_idle_ns;
	u32 maxadj;
#ifdef CONFIG_ARCH_CLOCKSOURCE_DATA
	struct arch_clocksource_data archdata;
#endif
	u64 max_cycles;
	const char *name;
	struct list_head list;
	int rating;
	int (*enable)(struct clocksource *cs);
	void (*disable)(struct clocksource *cs);
	unsigned long flags;
	void (*suspend)(struct clocksource *cs);
	void (*resume)(struct clocksource *cs);
	void (*mark_unstable)(struct clocksource *cs);
	void (*tick_stable)(struct clocksource *cs);

	/* private: */
#ifdef CONFIG_CLOCKSOURCE_WATCHDOG
	/* Watchdog related data, used by the framework */
	struct list_head wd_list;
	u64 cs_last;
	u64 wd_last;
#endif
	struct module *owner;
};

/*
 * Clock source flags bits::
 */
#define CLOCK_SOURCE_IS_CONTINUOUS		0x01
#define CLOCK_SOURCE_MUST_VERIFY		0x02

#define CLOCK_SOURCE_WATCHDOG			0x10
#define CLOCK_SOURCE_VALID_FOR_HRES		0x20
#define CLOCK_SOURCE_UNSTABLE			0x40
#define CLOCK_SOURCE_SUSPEND_NONSTOP		0x80
#define CLOCK_SOURCE_RESELECT			0x100

/* simplify initialization of mask field */
#define CLOCKSOURCE_MASK(bits) GENMASK_ULL((bits) - 1, 0)

static inline u32 clocksource_freq2mult(u32 freq, u32 shift_constant, u64 from)
{
	/*  freq = cyc/from
	 *  mult/2^shift  = ns/cyc
	 *  mult = ns/cyc * 2^shift
	 *  mult = from/freq * 2^shift
	 *  mult = from * 2^shift / freq
	 *  mult = (from<<shift) / freq
	 */
	u64 tmp = ((u64)from) << shift_constant;

	tmp += freq/2; /* round for do_div */
	do_div(tmp, freq);

	return (u32)tmp;
}

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
	return clocksource_freq2mult(khz, shift_constant, NSEC_PER_MSEC);
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
	return clocksource_freq2mult(hz, shift_constant, NSEC_PER_SEC);
}

/**
 * clocksource_cyc2ns - converts clocksource cycles to nanoseconds
 * @cycles:	cycles
 * @mult:	cycle to nanosecond multiplier
 * @shift:	cycle to nanosecond divisor (power of two)
 *
 * Converts clocksource cycles to nanoseconds, using the given @mult and @shift.
 * The code is optimized for performance and is not intended to work
 * with absolute clocksource cycles (as those will easily overflow),
 * but is only intended to be used with relative (delta) clocksource cycles.
 *
 * XXX - This could use some mult_lxl_ll() asm optimization
 */
static inline s64 clocksource_cyc2ns(u64 cycles, u32 mult, u32 shift)
{
	return ((u64) cycles * mult) >> shift;
}


extern int clocksource_unregister(struct clocksource*);
extern void clocksource_touch_watchdog(void);
extern void clocksource_change_rating(struct clocksource *cs, int rating);
extern void clocksource_suspend(void);
extern void clocksource_resume(void);
extern struct clocksource * __init clocksource_default_clock(void);
extern void clocksource_mark_unstable(struct clocksource *cs);

extern u64
clocks_calc_max_nsecs(u32 mult, u32 shift, u32 maxadj, u64 mask, u64 *max_cycles);
extern void
clocks_calc_mult_shift(u32 *mult, u32 *shift, u32 from, u32 to, u32 minsec);

/*
 * Don't call __clocksource_register_scale directly, use
 * clocksource_register_hz/khz
 */
extern int
__clocksource_register_scale(struct clocksource *cs, u32 scale, u32 freq);
extern void
__clocksource_update_freq_scale(struct clocksource *cs, u32 scale, u32 freq);

/*
 * Don't call this unless you are a default clocksource
 * (AKA: jiffies) and absolutely have to.
 */
static inline int __clocksource_register(struct clocksource *cs)
{
	return __clocksource_register_scale(cs, 1, 0);
}

static inline int clocksource_register_hz(struct clocksource *cs, u32 hz)
{
	return __clocksource_register_scale(cs, 1, hz);
}

static inline int clocksource_register_khz(struct clocksource *cs, u32 khz)
{
	return __clocksource_register_scale(cs, 1000, khz);
}

static inline void __clocksource_update_freq_hz(struct clocksource *cs, u32 hz)
{
	__clocksource_update_freq_scale(cs, 1, hz);
}

static inline void __clocksource_update_freq_khz(struct clocksource *cs, u32 khz)
{
	__clocksource_update_freq_scale(cs, 1000, khz);
}


extern int timekeeping_notify(struct clocksource *clock);

extern u64 clocksource_mmio_readl_up(struct clocksource *);
extern u64 clocksource_mmio_readl_down(struct clocksource *);
extern u64 clocksource_mmio_readw_up(struct clocksource *);
extern u64 clocksource_mmio_readw_down(struct clocksource *);

extern int clocksource_mmio_init(void __iomem *, const char *,
	unsigned long, int, unsigned, u64 (*)(struct clocksource *));

extern int clocksource_i8253_init(void);

#define TIMER_OF_DECLARE(name, compat, fn) \
	OF_DECLARE_1_RET(timer, name, compat, fn)

#define CLOCKSOURCE_OF_DECLARE(name, compat, fn) \
	TIMER_OF_DECLARE(name, compat, fn)

#ifdef CONFIG_TIMER_PROBE
extern void timer_probe(void);
#else
static inline void timer_probe(void) {}
#endif

#define TIMER_ACPI_DECLARE(name, table_id, fn)		\
	ACPI_DECLARE_PROBE_ENTRY(timer, name, table_id, 0, NULL, 0, fn)

#endif /* _LINUX_CLOCKSOURCE_H */
