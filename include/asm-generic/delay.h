/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_DELAY_H
#define __ASM_GENERIC_DELAY_H

/* Undefined functions to get compile-time errors */
extern void __bad_udelay(void);
extern void __bad_ndelay(void);

extern void __udelay(unsigned long usecs);
extern void __ndelay(unsigned long nsecs);
extern void __const_udelay(unsigned long xloops);
extern void __delay(unsigned long loops);

/*
 * Implementation details:
 *
 * * The weird n/20000 thing suppresses a "comparison is always false due to
 *   limited range of data type" warning with non-const 8-bit arguments.
 * * 0x10c7 is 2**32 / 1000000 (rounded up) -> udelay
 * * 0x5 is 2**32 / 1000000000 (rounded up) -> ndelay
 */

/**
 * udelay - Inserting a delay based on microseconds with busy waiting
 * @usec:	requested delay in microseconds
 *
 * When delaying in an atomic context ndelay(), udelay() and mdelay() are the
 * only valid variants of delaying/sleeping to go with.
 *
 * When inserting delays in non atomic context which are shorter than the time
 * which is required to queue e.g. an hrtimer and to enter then the scheduler,
 * it is also valuable to use udelay(). But it is not simple to specify a
 * generic threshold for this which will fit for all systems. An approximation
 * is a threshold for all delays up to 10 microseconds.
 *
 * When having a delay which is larger than the architecture specific
 * %MAX_UDELAY_MS value, please make sure mdelay() is used. Otherwise a overflow
 * risk is given.
 *
 * Please note that ndelay(), udelay() and mdelay() may return early for several
 * reasons (https://lists.openwall.net/linux-kernel/2011/01/09/56):
 *
 * #. computed loops_per_jiffy too low (due to the time taken to execute the
 *    timer interrupt.)
 * #. cache behaviour affecting the time it takes to execute the loop function.
 * #. CPU clock rate changes.
 */
#define udelay(n)							\
	({								\
		if (__builtin_constant_p(n)) {				\
			if ((n) / 20000 >= 1)				\
				 __bad_udelay();			\
			else						\
				__const_udelay((n) * 0x10c7ul);		\
		} else {						\
			__udelay(n);					\
		}							\
	})

/**
 * ndelay - Inserting a delay based on nanoseconds with busy waiting
 * @nsec:	requested delay in nanoseconds
 *
 * See udelay() for basic information about ndelay() and it's variants.
 */
#define ndelay(n)							\
	({								\
		if (__builtin_constant_p(n)) {				\
			if ((n) / 20000 >= 1)				\
				__bad_ndelay();				\
			else						\
				__const_udelay((n) * 5ul);		\
		} else {						\
			__ndelay(n);					\
		}							\
	})

#endif /* __ASM_GENERIC_DELAY_H */
