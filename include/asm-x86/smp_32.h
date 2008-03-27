#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#ifndef __ASSEMBLY__

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
