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

extern cpumask_t cpu_callout_map;
extern cpumask_t cpu_initialized;

extern int smp_num_siblings;
extern unsigned int num_processors;

extern void smp_alloc_memory(void);
extern void lock_ipi_call_lock(void);
extern void unlock_ipi_call_lock(void);

extern int smp_call_function_mask(cpumask_t mask, void (*func)(void *),
				  void *info, int wait);

extern u16 __initdata x86_cpu_to_apicid_init[];
extern u16 __initdata x86_bios_cpu_apicid_init[];
extern void *x86_cpu_to_apicid_early_ptr;
extern void *x86_bios_cpu_apicid_early_ptr;

DECLARE_PER_CPU(cpumask_t, cpu_sibling_map);
DECLARE_PER_CPU(cpumask_t, cpu_core_map);
DECLARE_PER_CPU(u16, cpu_llc_id);
DECLARE_PER_CPU(u16, x86_cpu_to_apicid);
DECLARE_PER_CPU(u16, x86_bios_cpu_apicid);

static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (cpu_present(mps_cpu))
		return (int)per_cpu(x86_bios_cpu_apicid, mps_cpu);
	else
		return BAD_APICID;
}

#ifdef CONFIG_SMP

#define SMP_TRAMPOLINE_BASE 0x6000

extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);
extern void prefill_possible_map(void);
extern unsigned __cpuinitdata disabled_cpus;

#define raw_smp_processor_id()	read_pda(cpunumber)
#define cpu_physical_id(cpu)	per_cpu(x86_cpu_to_apicid, cpu)

#define stack_smp_processor_id()					\
	({								\
	struct thread_info *ti;						\
	__asm__("andq %%rsp,%0; ":"=r" (ti) : "0" (CURRENT_MASK));	\
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

extern void smp_send_reschedule(int cpu);

#else /* CONFIG_SMP */

extern unsigned int boot_cpu_id;
#define cpu_physical_id(cpu)	boot_cpu_id
#define stack_smp_processor_id() 0

#endif /* !CONFIG_SMP */

#define safe_smp_processor_id()		smp_processor_id()

static __inline int logical_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_LOGICAL_ID(*(u32 *)(APIC_BASE + APIC_LDR));
}

static inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_ID(*(u32 *)(APIC_BASE + APIC_ID));
}

#endif

