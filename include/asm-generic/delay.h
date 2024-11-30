/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_DELAY_H
#define __ASM_GENERIC_DELAY_H

#include <linux/math.h>
#include <vdso/time64.h>

/* Undefined functions to get compile-time errors */
extern void __bad_udelay(void);
extern void __bad_ndelay(void);

extern void __udelay(unsigned long usecs);
extern void __ndelay(unsigned long nsecs);
extern void __const_udelay(unsigned long xloops);
extern void __delay(unsigned long loops);

/*
 * The microseconds/nanosecond delay multiplicators are used to convert a
 * constant microseconds/nanoseconds value to a value which can be used by the
 * architectures specific implementation to transform it into loops.
 */
#define UDELAY_CONST_MULT	((unsigned long)DIV_ROUND_UP(1ULL << 32, USEC_PER_SEC))
#define NDELAY_CONST_MULT	((unsigned long)DIV_ROUND_UP(1ULL << 32, NSEC_PER_SEC))

/*
 * The maximum constant udelay/ndelay value picked out of thin air to prevent
 * too long constant udelays/ndelays.
 */
#define DELAY_CONST_MAX   20000

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
static __always_inline void udelay(unsigned long usec)
{
	if (__builtin_constant_p(usec)) {
		if (usec >= DELAY_CONST_MAX)
			__bad_udelay();
		else
			__const_udelay(usec * UDELAY_CONST_MULT);
	} else {
		__udelay(usec);
	}
}

/**
 * ndelay - Inserting a delay based on nanoseconds with busy waiting
 * @nsec:	requested delay in nanoseconds
 *
 * See udelay() for basic information about ndelay() and it's variants.
 */
static __always_inline void ndelay(unsigned long nsec)
{
	if (__builtin_constant_p(nsec)) {
		if (nsec >= DELAY_CONST_MAX)
			__bad_udelay();
		else
			__const_udelay(nsec * NDELAY_CONST_MULT);
	} else {
		__udelay(nsec);
	}
}
#define ndelay(x) ndelay(x)

#endif /* __ASM_GENERIC_DELAY_H */
