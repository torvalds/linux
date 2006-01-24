#ifndef __ASM_SMP_H
#define __ASM_SMP_H

/*
 * We need the APIC definitions automatically as part of 'smp.h'
 */
#ifndef __ASSEMBLY__
#include <linux/config.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/bitops.h>
extern int disable_apic;
#endif

#ifdef CONFIG_X86_LOCAL_APIC
#ifndef __ASSEMBLY__
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#ifdef CONFIG_X86_IO_APIC
#include <asm/io_apic.h>
#endif
#include <asm/apic.h>
#include <asm/thread_info.h>
#endif
#endif

#ifdef CONFIG_SMP
#ifndef ASSEMBLY

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
extern int pic_mode;
extern void lock_ipi_call_lock(void);
extern void unlock_ipi_call_lock(void);
extern int smp_num_siblings;
extern void smp_send_reschedule(int cpu);
void smp_stop_cpu(void);
extern int smp_call_function_single(int cpuid, void (*func) (void *info),
				void *info, int retry, int wait);

extern cpumask_t cpu_sibling_map[NR_CPUS];
extern cpumask_t cpu_core_map[NR_CPUS];
extern u8 phys_proc_id[NR_CPUS];
extern u8 cpu_core_id[NR_CPUS];

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

static inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_ID(*(unsigned int *)(APIC_BASE+APIC_ID));
}

extern int safe_smp_processor_id(void);
extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);
extern void prefill_possible_map(void);
extern unsigned num_processors;
extern unsigned disabled_cpus;

#endif /* !ASSEMBLY */

#define NO_PROC_ID		0xFF		/* No processor magic marker */

#endif

#ifndef ASSEMBLY
/*
 * Some lowlevel functions might want to know about
 * the real APIC ID <-> CPU # mapping.
 */
extern u8 x86_cpu_to_apicid[NR_CPUS];	/* physical ID */
extern u8 x86_cpu_to_log_apicid[NR_CPUS];
extern u8 bios_cpu_apicid[];

static inline unsigned int cpu_mask_to_apicid(cpumask_t cpumask)
{
	return cpus_addr(cpumask)[0];
}

static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < NR_CPUS)
		return (int)bios_cpu_apicid[mps_cpu];
	else
		return BAD_APICID;
}

#endif /* !ASSEMBLY */

#ifndef CONFIG_SMP
#define stack_smp_processor_id() 0
#define safe_smp_processor_id() 0
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

#ifndef __ASSEMBLY__
static __inline int logical_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_LOGICAL_ID(*(unsigned long *)(APIC_BASE+APIC_LDR));
}
#endif

#ifdef CONFIG_SMP
#define cpu_physical_id(cpu)		x86_cpu_to_apicid[cpu]
#else
#define cpu_physical_id(cpu)		boot_cpu_id
#endif

#endif

