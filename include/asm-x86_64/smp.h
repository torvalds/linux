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

/*
 * Private routines/data
 */
 
extern void smp_alloc_memory(void);
extern cpumask_t cpu_online_map;
extern volatile unsigned long smp_invalidate_needed;
extern int pic_mode;
extern int smp_num_siblings;
extern void smp_flush_tlb(void);
extern void smp_message_irq(int cpl, void *dev_id, struct pt_regs *regs);
extern void smp_send_reschedule(int cpu);
extern void smp_invalidate_rcv(void);		/* Process an NMI */
extern void (*mtrr_hook) (void);
extern void zap_low_mappings(void);
void smp_stop_cpu(void);
extern cpumask_t cpu_sibling_map[NR_CPUS];
extern u8 phys_proc_id[NR_CPUS];

#define SMP_TRAMPOLINE_BASE 0x6000

/*
 * On x86 all CPUs are mapped 1:1 to the APIC space.
 * This simplifies scheduling and IPI sending and
 * compresses data structures.
 */

extern cpumask_t cpu_callout_map;
extern cpumask_t cpu_callin_map;
#define cpu_possible_map cpu_callout_map

static inline int num_booting_cpus(void)
{
	return cpus_weight(cpu_callout_map);
}

#define __smp_processor_id() read_pda(cpunumber)

extern __inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_ID(*(unsigned int *)(APIC_BASE+APIC_ID));
}

#define safe_smp_processor_id() (disable_apic ? 0 : x86_apicid_to_cpu(hard_smp_processor_id()))

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

static inline int x86_apicid_to_cpu(u8 apicid)
{
	int i;

	for (i = 0; i < NR_CPUS; ++i)
		if (x86_cpu_to_apicid[i] == apicid)
			return i;

	/* No entries in x86_cpu_to_apicid?  Either no MPS|ACPI,
	 * or called too early.  Either way, we must be CPU 0. */
      	if (x86_cpu_to_apicid[0] == BAD_APICID)
		return 0;

	return -1;
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

#endif

