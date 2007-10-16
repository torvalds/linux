/* smp.h: Sparc64 specific SMP stuff.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_SMP_H
#define _SPARC64_SMP_H

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
#include <asm/percpu.h>

DECLARE_PER_CPU(cpumask_t, cpu_sibling_map);
extern cpumask_t cpu_core_map[NR_CPUS];
extern int sparc64_multi_core;

/*
 *	General functions that each host system must provide.
 */

extern int hard_smp_processor_id(void);
#define raw_smp_processor_id() (current_thread_info()->cpu)

extern void smp_fill_in_sib_core_maps(void);
extern void cpu_play_dead(void);

#ifdef CONFIG_HOTPLUG_CPU
extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);
#endif

#endif /* !(__ASSEMBLY__) */

#else

#define hard_smp_processor_id()		0
#define smp_fill_in_sib_core_maps() do { } while (0)

#endif /* !(CONFIG_SMP) */

#endif /* !(_SPARC64_SMP_H) */
