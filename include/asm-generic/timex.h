/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_TIMEX_H
#define __ASM_GENERIC_TIMEX_H

/*
 * If you have a cycle counter, return the value here.
 */
typedef unsigned long cycles_t;
#ifndef get_cycles
static inline cycles_t get_cycles(void)
{
	return 0;
}
#endif

/*
 * Architectures are encouraged to implement read_current_timer
 * and define this in order to avoid the expensive delay loop
 * calibration during boot.
 */
#undef ARCH_HAS_READ_CURRENT_TIMER

#endif /* __ASM_GENERIC_TIMEX_H */
