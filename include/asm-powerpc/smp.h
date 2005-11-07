/* 
 * smp.h: PowerPC-specific SMP code.
 *
 * Original was a copy of sparc smp.h.  Now heavily modified
 * for PPC.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996-2001 Cort Dougan <cort@fsmlabs.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_POWERPC_SMP_H
#define _ASM_POWERPC_SMP_H
#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>

#ifndef __ASSEMBLY__

#ifdef CONFIG_PPC64
#include <asm/paca.h>
#endif

extern int boot_cpuid;
extern int boot_cpuid_phys;

extern void cpu_die(void);

#ifdef CONFIG_SMP

extern void smp_send_debugger_break(int cpu);
struct pt_regs;
extern void smp_message_recv(int, struct pt_regs *);

#ifdef CONFIG_HOTPLUG_CPU
extern void fixup_irqs(cpumask_t map);
int generic_cpu_disable(void);
int generic_cpu_enable(unsigned int cpu);
void generic_cpu_die(unsigned int cpu);
void generic_mach_cpu_die(void);
#endif

#ifdef CONFIG_PPC64
#define raw_smp_processor_id()	(get_paca()->paca_index)
#define hard_smp_processor_id() (get_paca()->hw_cpu_id)
#else
/* 32-bit */
extern int smp_hw_index[];

#define raw_smp_processor_id()	(current_thread_info()->cpu)
#define hard_smp_processor_id() 	(smp_hw_index[smp_processor_id()])
#define get_hard_smp_processor_id(cpu)	(smp_hw_index[(cpu)])
#define set_hard_smp_processor_id(cpu, phys)\
					(smp_hw_index[(cpu)] = (phys))
#endif

extern cpumask_t cpu_sibling_map[NR_CPUS];

/* Since OpenPIC has only 4 IPIs, we use slightly different message numbers.
 *
 * Make sure this matches openpic_request_IPIs in open_pic.c, or what shows up
 * in /proc/interrupts will be wrong!!! --Troy */
#define PPC_MSG_CALL_FUNCTION   0
#define PPC_MSG_RESCHEDULE      1
/* This is unused now */
#if 0
#define PPC_MSG_MIGRATE_TASK    2
#endif
#define PPC_MSG_DEBUGGER_BREAK  3

void smp_init_iSeries(void);
void smp_init_pSeries(void);
void smp_init_cell(void);
void smp_setup_cpu_maps(void);

extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);

#else
/* for UP */
#define smp_setup_cpu_maps()
#define smp_release_cpus()

#endif /* CONFIG_SMP */

#ifdef CONFIG_PPC64
#define get_hard_smp_processor_id(CPU) (paca[(CPU)].hw_cpu_id)
#define set_hard_smp_processor_id(CPU, VAL) \
	do { (paca[(CPU)].hw_cpu_id = (VAL)); } while (0)
#else
/* 32-bit */
#ifndef CONFIG_SMP
#define get_hard_smp_processor_id(cpu) 	boot_cpuid_phys
#define set_hard_smp_processor_id(cpu, phys)
#endif
#endif

extern int smt_enabled_at_boot;

extern int smp_mpic_probe(void);
extern void smp_mpic_setup_cpu(int cpu);
extern void smp_generic_kick_cpu(int nr);

extern void smp_generic_give_timebase(void);
extern void smp_generic_take_timebase(void);

extern struct smp_ops_t *smp_ops;

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_SMP_H) */
