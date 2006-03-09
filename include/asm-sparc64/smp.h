/* smp.h: Sparc64 specific SMP stuff.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_SMP_H
#define _SPARC64_SMP_H

#include <linux/config.h>
#include <linux/threads.h>
#include <asm/asi.h>
#include <asm/starfire.h>
#include <asm/spitfire.h>

#ifndef __ASSEMBLY__

#include <linux/cpumask.h>
#include <linux/cache.h>

#endif /* !(__ASSEMBLY__) */

#ifdef CONFIG_SMP

#ifndef __ASSEMBLY__

/*
 *	Private routines/data
 */
 
#include <asm/bitops.h>
#include <asm/atomic.h>

extern cpumask_t phys_cpu_present_map;
#define cpu_possible_map phys_cpu_present_map

extern cpumask_t cpu_sibling_map[NR_CPUS];

/*
 *	General functions that each host system must provide.
 */

extern int hard_smp_processor_id(void);
#define raw_smp_processor_id() (current_thread_info()->cpu)

extern void smp_setup_cpu_possible_map(void);

#endif /* !(__ASSEMBLY__) */

#else

#define smp_setup_cpu_possible_map() do { } while (0)

#endif /* !(CONFIG_SMP) */

#define NO_PROC_ID		0xFF

#endif /* !(_SPARC64_SMP_H) */
