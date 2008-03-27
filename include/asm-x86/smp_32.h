#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#ifndef __ASSEMBLY__
#include <linux/cpumask.h>
#include <linux/init.h>

/*
 * We need the APIC definitions automatically as part of 'smp.h'
 */
#ifdef CONFIG_X86_LOCAL_APIC
# include <asm/mpspec.h>
# include <asm/apic.h>
# ifdef CONFIG_X86_IO_APIC
#  include <asm/io_apic.h>
# endif
#endif

extern cpumask_t cpu_callin_map;

extern void (*mtrr_hook)(void);
extern void zap_low_mappings(void);

#ifdef CONFIG_SMP
/*
 * This function is needed by all SMP systems. It must _always_ be valid
 * from the initial startup. We map APIC_BASE very early in page_setup(),
 * so this is correct in the x86 case.
 */
DECLARE_PER_CPU(int, cpu_number);
#define raw_smp_processor_id() (x86_read_percpu(cpu_number))

extern int safe_smp_processor_id(void);

/* We don't mark CPUs online until __cpu_up(), so we need another measure */
static inline int num_booting_cpus(void)
{
	return cpus_weight(cpu_callout_map);
}

#else /* CONFIG_SMP */
#define safe_smp_processor_id()		0
#endif /* !CONFIG_SMP */

#endif /* !ASSEMBLY */
#endif
