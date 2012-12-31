/*
 * cpupower.h
 *
 * Copyright (c) 2011 Vincent Guittot <vincent.guittot@linaro.org>
 *
 * This file is released under the GPLv2
 *
 */

#ifndef _CPUPOWER_H_
#define _CPUPOWER_H_

#define ARM_DEFAULT_SCALE 0
#define ARM_POWER_SCALE 1

struct cputopo_power {
	int max; /* max idx in the table */
	unsigned int step; /* frequency step for the table */
	unsigned int *table; /* table of cpu_power */
};

#endif
