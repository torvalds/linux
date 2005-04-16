/* cpudata.h: Per-cpu parameters.
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 */

#ifndef _SPARC64_CPUDATA_H
#define _SPARC64_CPUDATA_H

#include <linux/percpu.h>

typedef struct {
	/* Dcache line 1 */
	unsigned int	__pad0;		/* bh_count moved to irq_stat for consistency. KAO */
	unsigned int	multiplier;
	unsigned int	counter;
	unsigned int	idle_volume;
	unsigned long	clock_tick;	/* %tick's per second */
	unsigned long	udelay_val;

	/* Dcache line 2 */
	unsigned int	pgcache_size;
	unsigned int	__pad1;
	unsigned long	*pte_cache[2];
	unsigned long	*pgd_cache;
} cpuinfo_sparc;

DECLARE_PER_CPU(cpuinfo_sparc, __cpu_data);
#define cpu_data(__cpu)		per_cpu(__cpu_data, (__cpu))
#define local_cpu_data()	__get_cpu_var(__cpu_data)

#endif /* _SPARC64_CPUDATA_H */
