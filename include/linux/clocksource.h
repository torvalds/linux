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
 * clocksource_read: - Access the clocksource's current cycle value
 * @cs:		pointer to clocksource being read
 *
 * Uses the clocksource to return the current cycle_t value
 */
static inline cycle_t clocksource_read(struct clocksource *cs)
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
 * clocksource_calculate_interval - Calculates a clocksource interval struct
 *
 * @c:		Pointer to clocksource.
 * @length_nsec: Desired interval length in nanoseconds.
 *
 * Calculates a fixed cycle/nsec interval for a given clocksource/adjustment
 * pair and interval request.
 *
 * Unless you're the timekeeping code, you should not be using this!
 */
static inline void clocksource_calculate_interval(struct clocksource *c,
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


/**
 * error_aproximation - calculates an error adjustment for a given error
 *
 * @error:	Error value (unsigned)
 * @unit:	Adjustment unit
 *
 * For a given error value, this function takes the adjustment unit
 * and uses binary approximation to return a power of two adjustment value.
 *
 * This function is only for use by the the make_ntp_adj() function
 * and you must hold a write on the xtime_lock when calling.
 */
static inline int error_aproximation(u64 error, u64 unit)
{
	static int saved_adj = 0;
	u64 adjusted_unit = unit << saved_adj;

	if (error > (adjusted_unit * 2)) {
		/* large error, so increment the adjustment factor */
		saved_adj++;
	} else if (error > adjusted_unit) {
		/* just right, don't touch it */
	} else if (saved_adj) {
		/* small error, so drop the adjustment factor */
		saved_adj--;
		return 0;
	}

	return saved_adj;
}


/**
 * make_ntp_adj - Adjusts the specified clocksource for a given error
 *
 * @clock:		Pointer to clock to be adjusted
 * @cycles_delta:	Current unacounted cycle delta
 * @error:		Pointer to current error value
 *
 * Returns clock shifted nanosecond adjustment to be applied against
 * the accumulated time value (ie: xtime).
 *
 * If the error value is large enough, this function calulates the
 * (power of two) adjustment value, and adjusts the clock's mult and
 * interval_snsecs values accordingly.
 *
 * However, since there may be some unaccumulated cycles, to avoid
 * time inconsistencies we must adjust the accumulation value
 * accordingly.
 *
 * This is not very intuitive, so the following proof should help:
 * The basic timeofday algorithm:  base + cycle * mult
 * Thus:
 *    new_base + cycle * new_mult = old_base + cycle * old_mult
 *    new_base = old_base + cycle * old_mult - cycle * new_mult
 *    new_base = old_base + cycle * (old_mult - new_mult)
 *    new_base - old_base = cycle * (old_mult - new_mult)
 *    base_delta = cycle * (old_mult - new_mult)
 *    base_delta = cycle * (mult_delta)
 *
 * Where mult_delta is the adjustment value made to mult
 *
 */
static inline s64 make_ntp_adj(struct clocksource *clock,
				cycles_t cycles_delta, s64* error)
{
	s64 ret = 0;
	if (*error  > ((s64)clock->interval_cycles+1)/2) {
		/* calculate adjustment value */
		int adjustment = error_aproximation(*error,
						clock->interval_cycles);
		/* adjust clock */
		clock->mult += 1 << adjustment;
		clock->interval_snsecs += clock->interval_cycles << adjustment;

		/* adjust the base and error for the adjustment */
		ret =  -(cycles_delta << adjustment);
		*error -= clock->interval_cycles << adjustment;
		/* XXX adj error for cycle_delta offset? */
	} else if ((-(*error))  > ((s64)clock->interval_cycles+1)/2) {
		/* calculate adjustment value */
		int adjustment = error_aproximation(-(*error),
						clock->interval_cycles);
		/* adjust clock */
		clock->mult -= 1 << adjustment;
		clock->interval_snsecs -= clock->interval_cycles << adjustment;

		/* adjust the base and error for the adjustment */
		ret =  cycles_delta << adjustment;
		*error += clock->interval_cycles << adjustment;
		/* XXX adj error for cycle_delta offset? */
	}
	return ret;
}


/* used to install a new clocksource */
int clocksource_register(struct clocksource*);
void clocksource_reselect(void);
struct clocksource* clocksource_get_next(void);

#endif /* _LINUX_CLOCKSOURCE_H */
