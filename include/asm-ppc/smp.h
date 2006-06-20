/* smp.h: PPC specific SMP stuff.
 *
 * Original was a copy of sparc smp.h.  Now heavily modified
 * for PPC.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996-2001 Cort Dougan <cort@fsmlabs.com>
 */
#ifdef __KERNEL__
#ifndef _PPC_SMP_H
#define _PPC_SMP_H

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/cpumask.h>
#include <linux/threads.h>

#ifdef CONFIG_SMP

#ifndef __ASSEMBLY__

struct cpuinfo_PPC {
	unsigned long loops_per_jiffy;
	unsigned long pvr;
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
};

extern struct cpuinfo_PPC cpu_data[];
extern cpumask_t cpu_online_map;
extern cpumask_t cpu_possible_map;
extern unsigned long smp_proc_in_lock[];
extern volatile unsigned long cpu_callin_map[];
extern int smp_tb_synchronized;
extern struct smp_ops_t *smp_ops;

extern void smp_send_tlb_invalidate(int);
extern void smp_send_xmon_break(int cpu);
struct pt_regs;
extern void smp_message_recv(int, struct pt_regs *);

extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);
extern void cpu_die(void) __attribute__((noreturn));

#define raw_smp_processor_id()	(current_thread_info()->cpu)

extern int __cpu_up(unsigned int cpu);

extern int smp_hw_index[];
#define hard_smp_processor_id() 	(smp_hw_index[smp_processor_id()])
#define get_hard_smp_processor_id(cpu)	(smp_hw_index[(cpu)])
#define set_hard_smp_processor_id(cpu, phys)\
					(smp_hw_index[(cpu)] = (phys))
 
#endif /* __ASSEMBLY__ */

#else /* !(CONFIG_SMP) */

static inline void cpu_die(void) { }
#define get_hard_smp_processor_id(cpu) 0
#define set_hard_smp_processor_id(cpu, phys)
#define hard_smp_processor_id() 0

#endif /* !(CONFIG_SMP) */

#ifndef __ASSEMBLY__
extern int boot_cpuid;
extern int boot_cpuid_phys;
#endif

#endif /* !(_PPC_SMP_H) */
#endif /* __KERNEL__ */
