#ifndef __ASM_SMP_H
#define __ASM_SMP_H

/*
 * We need the APIC definitions automatically as part of 'smp.h'
 */
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/bitops.h>
#include <linux/init.h>
extern int disable_apic;

#include <asm/mpspec.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/thread_info.h>

#ifdef CONFIG_SMP

#include <asm/pda.h>

struct pt_regs;

extern cpumask_t cpu_present_mask;
extern cpumask_t cpu_possible_map;
extern cpumask_t cpu_online_map;
extern cpumask_t cpu_callout_map;
extern cpumask_t cpu_initialized;

/*
 * Private routines/data
 */
 
extern void smp_alloc_memory(void);
extern volatile unsigned long smp_invalidate_needed;
extern void lock_ipi_call_lock(void);
extern void unlock_ipi_call_lock(void);
extern int smp_num_siblings;
extern void smp_send_reschedule(int cpu);

extern cpumask_t cpu_sibling_map[NR_CPUS];
/*
 * cpu_core_map lives in a per cpu area
 *
 * extern cpumask_t cpu_core_map[NR_CPUS];
 */
DECLARE_PER_CPU(cpumask_t, cpu_core_map);
extern u8 cpu_llc_id[NR_CPUS];

#define SMP_TRAMPOLINE_BASE 0x6000

/*
 * On x86 all CPUs are mapped 1:1 to the APIC space.
 * This simplifies scheduling and IPI sending and
 * compresses data structures.
 */

static inline int num_booting_cpus(void)
{
	return cpus_weight(cpu_callout_map);
}

#define raw_smp_processor_id() read_pda(cpunumber)

extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);
extern void prefill_possible_map(void);
extern unsigned num_processors;
extern unsigned __cpuinitdata disabled_cpus;

#define NO_PROC_ID		0xFF		/* No processor magic marker */

#endif /* CONFIG_SMP */

static inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_ID(*(unsigned int *)(APIC_BASE+APIC_ID));
}

/*
 * Some lowlevel functions might want to know about
 * the real APIC ID <-> CPU # mapping.
 */
extern u8 x86_cpu_to_apicid[NR_CPUS];	/* physical ID */
extern u8 x86_cpu_to_log_apicid[NR_CPUS];
extern u8 bios_cpu_apicid[];

static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < NR_CPUS)
		return (int)bios_cpu_apicid[mps_cpu];
	else
		return BAD_APICID;
}

#ifndef CONFIG_SMP
#define stack_smp_processor_id() 0
#define cpu_logical_map(x) (x)
#else
#include <asm/thread_info.h>
#define stack_smp_processor_id() \
({ 								\
	struct thread_info *ti;					\
	__asm__("andq %%rsp,%0; ":"=r" (ti) : "0" (CURRENT_MASK));	\
	ti->cpu;						\
})
#endif

static __inline int logical_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_LOGICAL_ID(*(unsigned long *)(APIC_BASE+APIC_LDR));
}

#ifdef CONFIG_SMP
#define cpu_physical_id(cpu)		x86_cpu_to_apicid[cpu]
#else
#define cpu_physical_id(cpu)		boot_cpu_id
#endif /* !CONFIG_SMP */
#endif

