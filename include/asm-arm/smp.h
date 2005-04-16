/*
 *  linux/include/asm-arm/smp.h
 *
 *  Copyright (C) 2004-2005 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_SMP_H
#define __ASM_ARM_SMP_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/thread_info.h>

#include <asm/arch/smp.h>

#ifndef CONFIG_SMP
# error "<asm-arm/smp.h> included in non-SMP build"
#endif

#define smp_processor_id()	(current_thread_info()->cpu)

extern cpumask_t cpu_present_mask;
#define cpu_possible_map cpu_present_mask

/*
 * at the moment, there's not a big penalty for changing CPUs
 * (the >big< penalty is running SMP in the first place)
 */
#define PROC_CHANGE_PENALTY		15

struct seq_file;

/*
 * generate IPI list text
 */
extern void show_ipi_list(struct seq_file *p);

/*
 * Move global data into per-processor storage.
 */
extern void smp_store_cpu_info(unsigned int cpuid);

/*
 * Raise an IPI cross call on CPUs in callmap.
 */
extern void smp_cross_call(cpumask_t callmap);

/*
 * Boot a secondary CPU, and assign it the specified idle task.
 * This also gives us the initial stack to use for this CPU.
 */
extern int boot_secondary(unsigned int cpu, struct task_struct *);

#endif /* ifndef __ASM_ARM_SMP_H */
