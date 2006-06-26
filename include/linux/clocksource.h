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
#include <asm/div64.h>
#include <asm/io.h>

/* clocksource cycle base type */
typedef u64 cycle_t;

/**
 * struct clocksource - hardware abstraction for a free running counter
 *	Provides mostly state-free accessors to the underlying hardware.
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
 * @read:		returns a cycle value
 * @mask:		bitmask for two's complement
 *			subtraction of non 64 bit counters
 * @mult:		cycle to nanosecond multiplier
 * @shift:		cycle to nanosecond divisor (power of two)
 * @update_callback:	called when safe to alter clocksource values
 * @is_continuous:	defines if clocksource is free-running.
 * @interval_cycles:	Used internally by timekeeping core, please ignore.
 * @interval_snsecs:	Used internally by timekeeping core, please ignore.
 */
struct clocksource {
	char *name;
	struct list_head list;
	int rating;
	cycle_t (*read)(void);
	cycle_t mask;
	u32 mult;
	u32 shift;
	int (*update_callback)(void);
	int is_continuous;

	/* timekeeping specific data, ignore */
	cycle_t interval_cycles;
	u64 interval_snsecs;
};


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
 * read_clocksource: - Access the clocksource's current cycle value
 * @cs:		pointer to clocksource being read
 *
 * Uses the clocksource to return the current cycle_t value
 */
static inline cycle_t read_clocksource(struct clocksource *cs)
{
	return cs->read();
}

/**
 * cyc2ns - converts clocksource cycles to nanoseconds
 * @cs:		Pointer to clocksource
 * @cycles:	Cycles
 *
 * Uses the clocksource and ntp ajdustment to convert cycle_ts to nanoseconds.
 *
 * XXX - This could use some mult_lxl_ll() asm optimization
 */
static inline s64 cyc2ns(struct clocksource *cs, cycle_t cycles)
{
	u64 ret = (u64)cycles;
	ret = (ret * cs->mult) >> cs->shift;
	return ret;
}

/**
 * calculate_clocksource_interval - Calculates a clocksource interval struct
 *
 * @c:		Pointer to clocksource.
 * @length_nsec: Desired interval length in nanoseconds.
 *
 * Calculates a fixed cycle/nsec interval for a given clocksource/adjustment
 * pair and interval request.
 *
 * Unless you're the timekeeping code, you should not be using this!
 */
static inline void calculate_clocksource_interval(struct clocksource *c,
						unsigned long length_nsec)
{
	u64 tmp;

	/* XXX - All of this could use a whole lot of optimization */
	tmp = length_nsec;
	tmp <<= c->shift;
	tmp += c->mult/2;
	do_div(tmp, c->mult);

	c->interval_cycles = (cycle_t)tmp;
	if(c->interval_cycles == 0)
		c->interval_cycles = 1;

	c->interval_snsecs = (u64)c->interval_cycles * c->mult;
}

/* used to install a new clocksource */
int register_clocksource(struct clocksource*);
void reselect_clocksource(void);
struct clocksource* get_next_clocksource(void);

#endif /* _LINUX_CLOCKSOURCE_H */
