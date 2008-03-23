#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#include <linux/cpumask.h>
#include <linux/init.h>

/*
 * We need the APIC definitions automatically as part of 'smp.h'
 */
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/mpspec.h>
#include <asm/pda.h>
#include <asm/thread_info.h>

extern cpumask_t cpu_initialized;
extern cpumask_t cpu_callin_map;

extern int smp_call_function_mask(cpumask_t mask, void (*func)(void *),
				  void *info, int wait);

#ifdef CONFIG_SMP

#define raw_smp_processor_id()	read_pda(cpunumber)

#define stack_smp_processor_id()					\
({									\
	struct thread_info *ti;						\
	asm("andq %%rsp,%0; ":"=r" (ti) : "0" (CURRENT_MASK));	\
	ti->cpu;							\
})

/*
 * On x86 all CPUs are mapped 1:1 to the APIC space. This simplifies
 * scheduling and IPI sending and compresses data structures.
 */
static inline int num_booting_cpus(void)
{
	return cpus_weight(cpu_callout_map);
}

#else /* CONFIG_SMP */
#define stack_smp_processor_id() 0

#endif /* !CONFIG_SMP */

#define safe_smp_processor_id()		smp_processor_id()

static inline int logical_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_LOGICAL_ID(*(u32 *)(APIC_BASE + APIC_LDR));
}

#include <mach_apicdef.h>
static inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_ID(*(u32 *)(APIC_BASE + APIC_ID));
}

#endif

