/*
 * Copyright (C) 2014 Marvell
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __CPUFREQ_DT_H__
#define __CPUFREQ_DT_H__

#include <linux/types.h>

struct cpufreq_dt_platform_data {
	/*
	 * True when each CPU has its own clock to control its
	 * frequency, false when all CPUs are controlled by a single
	 * clock.
	 */
	bool independent_clocks;
};

#endif /* __CPUFREQ_DT_H__ */
