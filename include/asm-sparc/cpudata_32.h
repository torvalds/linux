/* cpudata.h: Per-cpu parameters.
 *
 * Copyright (C) 2004 Keith M Wesolowski (wesolows@foobazco.org)
 *
 * Based on include/asm-sparc64/cpudata.h and Linux 2.4 smp.h
 * both (C) David S. Miller.
 */

#ifndef _SPARC_CPUDATA_H
#define _SPARC_CPUDATA_H

#include <linux/percpu.h>

typedef struct {
	unsigned long udelay_val;
	unsigned long clock_tick;
	unsigned int multiplier;
	unsigned int counter;
	int prom_node;
	int mid;
	int next;
} cpuinfo_sparc;

DECLARE_PER_CPU(cpuinfo_sparc, __cpu_data);
#define cpu_data(__cpu) per_cpu(__cpu_data, (__cpu))

#endif /* _SPARC_CPUDATA_H */
