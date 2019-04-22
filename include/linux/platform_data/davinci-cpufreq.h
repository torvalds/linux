/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TI DaVinci CPUFreq platform support.
 *
 * Copyright (C) 2009 Texas Instruments, Inc. http://www.ti.com/
 */

#ifndef _MACH_DAVINCI_CPUFREQ_H
#define _MACH_DAVINCI_CPUFREQ_H

#include <linux/cpufreq.h>

struct davinci_cpufreq_config {
	struct cpufreq_frequency_table *freq_table;
	int (*set_voltage)(unsigned int index);
	int (*init)(void);
};

#endif /* _MACH_DAVINCI_CPUFREQ_H */
