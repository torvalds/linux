/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM simple delay routines
 */

#ifndef SELFTEST_KVM_ARM_DELAY_H
#define SELFTEST_KVM_ARM_DELAY_H

#include "arch_timer.h"

static inline void __delay(uint64_t cycles)
{
	enum arch_timer timer = VIRTUAL;
	uint64_t start = timer_get_cntct(timer);

	while ((timer_get_cntct(timer) - start) < cycles)
		cpu_relax();
}

static inline void udelay(unsigned long usec)
{
	__delay(usec_to_cycles(usec));
}

#endif /* SELFTEST_KVM_ARM_DELAY_H */
