/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DELAY_H
#define _LINUX_DELAY_H

/*
 * Copyright (C) 1993 Linus Torvalds
 *
 * Delay routines, using a pre-computed "loops_per_jiffy" value.
 * Sleep routines using timer list timers or hrtimers.
 */

#include <linux/math.h>
#include <linux/sched.h>

extern unsigned long loops_per_jiffy;

#include <asm/delay.h>

/*
 * Using udelay() for intervals greater than a few milliseconds can
 * risk overflow for high loops_per_jiffy (high bogomips) machines. The
 * mdelay() provides a wrapper to prevent this.  For delays greater
 * than MAX_UDELAY_MS milliseconds, the wrapper is used.  Architecture
 * specific values can be defined in asm-???/delay.h as an override.
 * The 2nd mdelay() definition ensures GCC will optimize away the 
 * while loop for the common cases where n <= MAX_UDELAY_MS  --  Paul G.
 */
#ifndef MAX_UDELAY_MS
#define MAX_UDELAY_MS	5
#endif

#ifndef mdelay
/**
 * mdelay - Inserting a delay based on milliseconds with busy waiting
 * @n:	requested delay in milliseconds
 *
 * See udelay() for basic information about mdelay() and it's variants.
 *
 * Please double check, whether mdelay() is the right way to go or whether a
 * refactoring of the code is the better variant to be able to use msleep()
 * instead.
 */
#define mdelay(n) (\
	(__builtin_constant_p(n) && (n)<=MAX_UDELAY_MS) ? udelay((n)*1000) : \
	({unsigned long __ms=(n); while (__ms--) udelay(1000);}))
#endif

#ifndef ndelay
static inline void ndelay(unsigned long x)
{
	udelay(DIV_ROUND_UP(x, 1000));
}
#define ndelay(x) ndelay(x)
#endif

extern unsigned long lpj_fine;
void calibrate_delay(void);
unsigned long calibrate_delay_is_known(void);
void __attribute__((weak)) calibration_delay_done(void);
void msleep(unsigned int msecs);
unsigned long msleep_interruptible(unsigned int msecs);
void usleep_range_state(unsigned long min, unsigned long max,
			unsigned int state);

/**
 * usleep_range - Sleep for an approximate time
 * @min:	Minimum time in microseconds to sleep
 * @max:	Maximum time in microseconds to sleep
 *
 * For basic information please refere to usleep_range_state().
 *
 * The task will be in the state TASK_UNINTERRUPTIBLE during the sleep.
 */
static inline void usleep_range(unsigned long min, unsigned long max)
{
	usleep_range_state(min, max, TASK_UNINTERRUPTIBLE);
}

/**
 * usleep_range_idle - Sleep for an approximate time with idle time accounting
 * @min:	Minimum time in microseconds to sleep
 * @max:	Maximum time in microseconds to sleep
 *
 * For basic information please refere to usleep_range_state().
 *
 * The sleeping task has the state TASK_IDLE during the sleep to prevent
 * contribution to the load avarage.
 */
static inline void usleep_range_idle(unsigned long min, unsigned long max)
{
	usleep_range_state(min, max, TASK_IDLE);
}

/**
 * ssleep - wrapper for seconds around msleep
 * @seconds:	Requested sleep duration in seconds
 *
 * Please refere to msleep() for detailed information.
 */
static inline void ssleep(unsigned int seconds)
{
	msleep(seconds * 1000);
}

/* see Documentation/timers/timers-howto.rst for the thresholds */
static inline void fsleep(unsigned long usecs)
{
	if (usecs <= 10)
		udelay(usecs);
	else if (usecs <= 20000)
		usleep_range(usecs, 2 * usecs);
	else
		msleep(DIV_ROUND_UP(usecs, 1000));
}

#endif /* defined(_LINUX_DELAY_H) */
