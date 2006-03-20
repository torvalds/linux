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

/*
 *	General functions that each host system must provide.
 */

static __inline__ int hard_smp_processor_id(void)
{
	if (tlb_type == cheetah || tlb_type == cheetah_plus) {
		unsigned long cfg, ver;
		__asm__ __volatile__("rdpr %%ver, %0" : "=r" (ver));
		if ((ver >> 32) == 0x003e0016) {
			__asm__ __volatile__("ldxa [%%g0] %1, %0"
					     : "=r" (cfg)
					     : "i" (ASI_JBUS_CONFIG));
			return ((cfg >> 17) & 0x1f);
		} else {
			__asm__ __volatile__("ldxa [%%g0] %1, %0"
					     : "=r" (cfg)
					     : "i" (ASI_SAFARI_CONFIG));
			return ((cfg >> 17) & 0x3ff);
		}
	} else if (this_is_starfire != 0) {
		return starfire_hard_smp_processor_id();
	} else {
		unsigned long upaconfig;
		__asm__ __volatile__("ldxa	[%%g0] %1, %0"
				     : "=r" (upaconfig)
				     : "i" (ASI_UPA_CONFIG));
		return ((upaconfig >> 17) & 0x1f);
	}
}

#define raw_smp_processor_id() (current_thread_info()->cpu)

extern void smp_setup_cpu_possible_map(void);

#endif /* !(__ASSEMBLY__) */

#else

#define smp_setup_cpu_possible_map() do { } while (0)

#endif /* !(CONFIG_SMP) */

#define NO_PROC_ID		0xFF

#endif /* !(_SPARC64_SMP_H) */
