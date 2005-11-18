#ifndef __ASM_SMP_H
#define __ASM_SMP_H

/*
 * We need the APIC definitions automatically as part of 'smp.h'
 */
#ifndef __ASSEMBLY__
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#endif

#ifdef CONFIG_X86_LOCAL_APIC
#ifndef __ASSEMBLY__
#include <asm/fixmap.h>
#include <asm/bitops.h>
#include <asm/mpspec.h>
#ifdef CONFIG_X86_IO_APIC
#include <asm/io_apic.h>
#endif
#include <asm/apic.h>
#endif
#endif

#define BAD_APICID 0xFFu
#ifdef CONFIG_SMP
#ifndef __ASSEMBLY__

/*
 * Private routines/data
 */
 
extern void smp_alloc_memory(void);
extern int pic_mode;
extern int smp_num_siblings;
extern cpumask_t cpu_sibling_map[];
extern cpumask_t cpu_core_map[];

extern void (*mtrr_hook) (void);
extern void zap_low_mappings (void);
extern void lock_ipi_call_lock(void);
extern void unlock_ipi_call_lock(void);

#define MAX_APICID 256
extern u8 x86_cpu_to_apicid[];

#define cpu_physical_id(cpu)	x86_cpu_to_apicid[cpu]

#ifdef CONFIG_HOTPLUG_CPU
extern void cpu_exit_clear(void);
extern void cpu_uninit(void);
#endif

/*
 * This function is needed by all SMP systems. It must _always_ be valid
 * from the initial startup. We map APIC_BASE very early in page_setup(),
 * so this is correct in the x86 case.
 */
#define raw_smp_processor_id() (current_thread_info()->cpu)

extern cpumask_t cpu_callout_map;
extern cpumask_t cpu_callin_map;
extern cpumask_t cpu_possible_map;

/* We don't mark CPUs online until __cpu_up(), so we need another measure */
static inline int num_booting_cpus(void)
{
	return cpus_weight(cpu_callout_map);
}

#ifdef CONFIG_X86_LOCAL_APIC

#ifdef APIC_DEFINITION
extern int hard_smp_processor_id(void);
#else
#include <mach_apicdef.h>
static inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_ID(*(unsigned long *)(APIC_BASE+APIC_ID));
}
#endif

static __inline int logical_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_LOGICAL_ID(*(unsigned long *)(APIC_BASE+APIC_LDR));
}

#endif

extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);
#endif /* !__ASSEMBLY__ */

#else /* CONFIG_SMP */

#define cpu_physical_id(cpu)		boot_cpu_physical_apicid

#define NO_PROC_ID		0xFF		/* No processor magic marker */

#endif
#endif
