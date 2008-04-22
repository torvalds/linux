/*
 * SMP Support
 *
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * (c) Copyright 2001-2003, 2005 Hewlett-Packard Development Company, L.P.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 */
#ifndef _ASM_IA64_SMP_H
#define _ASM_IA64_SMP_H

#include <linux/init.h>
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/cpumask.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/param.h>
#include <asm/processor.h>
#include <asm/ptrace.h>

static inline unsigned int
ia64_get_lid (void)
{
	union {
		struct {
			unsigned long reserved : 16;
			unsigned long eid : 8;
			unsigned long id : 8;
			unsigned long ignored : 32;
		} f;
		unsigned long bits;
	} lid;

	lid.bits = ia64_getreg(_IA64_REG_CR_LID);
	return lid.f.id << 8 | lid.f.eid;
}

extern int smp_call_function_mask(cpumask_t mask, void (*func)(void *),
				  void *info, int wait);

#define hard_smp_processor_id()		ia64_get_lid()

#ifdef CONFIG_SMP

#define XTP_OFFSET		0x1e0008

#define SMP_IRQ_REDIRECTION	(1 << 0)
#define SMP_IPI_REDIRECTION	(1 << 1)

#define raw_smp_processor_id() (current_thread_info()->cpu)

extern struct smp_boot_data {
	int cpu_count;
	int cpu_phys_id[NR_CPUS];
} smp_boot_data __initdata;

extern char no_int_routing __devinitdata;

extern cpumask_t cpu_online_map;
extern cpumask_t cpu_core_map[NR_CPUS];
DECLARE_PER_CPU(cpumask_t, cpu_sibling_map);
extern int smp_num_siblings;
extern void __iomem *ipi_base_addr;
extern unsigned char smp_int_redirect;

extern volatile int ia64_cpu_to_sapicid[];
#define cpu_physical_id(i)	ia64_cpu_to_sapicid[i]

extern unsigned long ap_wakeup_vector;

/*
 * Function to map hard smp processor id to logical id.  Slow, so don't use this in
 * performance-critical code.
 */
static inline int
cpu_logical_id (int cpuid)
{
	int i;

	for (i = 0; i < NR_CPUS; ++i)
		if (cpu_physical_id(i) == cpuid)
			break;
	return i;
}

/*
 * XTP control functions:
 *	min_xtp   : route all interrupts to this CPU
 *	normal_xtp: nominal XTP value
 *	max_xtp   : never deliver interrupts to this CPU.
 */

static inline void
min_xtp (void)
{
	if (smp_int_redirect & SMP_IRQ_REDIRECTION)
		writeb(0x00, ipi_base_addr + XTP_OFFSET); /* XTP to min */
}

static inline void
normal_xtp (void)
{
	if (smp_int_redirect & SMP_IRQ_REDIRECTION)
		writeb(0x08, ipi_base_addr + XTP_OFFSET); /* XTP normal */
}

static inline void
max_xtp (void)
{
	if (smp_int_redirect & SMP_IRQ_REDIRECTION)
		writeb(0x0f, ipi_base_addr + XTP_OFFSET); /* Set XTP to max */
}

/* Upping and downing of CPUs */
extern int __cpu_disable (void);
extern void __cpu_die (unsigned int cpu);
extern void cpu_die (void) __attribute__ ((noreturn));
extern void __init smp_build_cpu_map(void);

extern void __init init_smp_config (void);
extern void smp_do_timer (struct pt_regs *regs);

extern void smp_send_reschedule (int cpu);
extern void lock_ipi_calllock(void);
extern void unlock_ipi_calllock(void);
extern void identify_siblings (struct cpuinfo_ia64 *);
extern int is_multithreading_enabled(void);

#else /* CONFIG_SMP */

#define cpu_logical_id(i)		0
#define cpu_physical_id(i)		ia64_get_lid()

#endif /* CONFIG_SMP */
#endif /* _ASM_IA64_SMP_H */
