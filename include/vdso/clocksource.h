/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __VDSO_CLOCKSOURCE_H
#define __VDSO_CLOCKSOURCE_H

#include <vdso/limits.h>

#ifdef CONFIG_GENERIC_GETTIMEOFDAY
#include <asm/vdso/clocksource.h>
#endif /* CONFIG_GENERIC_GETTIMEOFDAY */

enum vdso_clock_mode {
	VDSO_CLOCKMODE_NONE,
#ifdef CONFIG_GENERIC_GETTIMEOFDAY
	VDSO_ARCH_CLOCKMODES,
#endif
	VDSO_CLOCKMODE_MAX,

	/* Indicator for time namespace VDSO */
	VDSO_CLOCKMODE_TIMENS = INT_MAX
};

#endif /* __VDSO_CLOCKSOURCE_H */
