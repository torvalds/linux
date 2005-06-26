/*
 * include/asm-xtensa/smp.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_SMP_H
#define _XTENSA_SMP_H

extern struct xtensa_cpuinfo boot_cpu_data;

#define cpu_data (&boot_cpu_data)
#define current_cpu_data boot_cpu_data

struct xtensa_cpuinfo {
	unsigned long	*pgd_cache;
	unsigned long	*pte_cache;
	unsigned long	pgtable_cache_sz;
};

#define cpu_logical_map(cpu)	(cpu)

#endif	/* _XTENSA_SMP_H */
