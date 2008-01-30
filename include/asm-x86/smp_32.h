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

extern cpumask_t cpu_callout_map;
extern cpumask_t cpu_callin_map;

extern int smp_num_siblings;
extern unsigned int num_processors;

extern void smp_alloc_memory(void);
extern void lock_ipi_call_lock(void);
extern void unlock_ipi_call_lock(void);

extern void (*mtrr_hook) (void);
extern void zap_low_mappings (void);

extern u8 __initdata x86_cpu_to_apicid_init[];
extern void *x86_cpu_to_apicid_early_ptr;

DECLARE_PER_CPU(cpumask_t, cpu_sibling_map);
DECLARE_PER_CPU(cpumask_t, cpu_core_map);
DECLARE_PER_CPU(u8, cpu_llc_id);
DECLARE_PER_CPU(u8, x86_cpu_to_apicid);

#ifdef CONFIG_HOTPLUG_CPU
extern void cpu_exit_clear(void);
extern void cpu_uninit(void);
extern void remove_siblinginfo(int cpu);
#endif

/* Globals due to paravirt */
extern void set_cpu_sibling_map(int cpu);

struct smp_ops
{
	void (*smp_prepare_boot_cpu)(void);
	void (*smp_prepare_cpus)(unsigned max_cpus);
	int (*cpu_up)(unsigned cpu);
	void (*smp_cpus_done)(unsigned max_cpus);

	void (*smp_send_stop)(void);
	void (*smp_send_reschedule)(int cpu);
	int (*smp_call_function_mask)(cpumask_t mask,
				      void (*func)(void *info), void *info,
				      int wait);
};

#ifdef CONFIG_SMP
extern struct smp_ops smp_ops;

static inline void smp_prepare_boot_cpu(void)
{
	smp_ops.smp_prepare_boot_cpu();
}
static inline void smp_prepare_cpus(unsigned int max_cpus)
{
	smp_ops.smp_prepare_cpus(max_cpus);
}
static inline int __cpu_up(unsigned int cpu)
{
	return smp_ops.cpu_up(cpu);
}
static inline void smp_cpus_done(unsigned int max_cpus)
{
	smp_ops.smp_cpus_done(max_cpus);
}

static inline void smp_send_stop(void)
{
	smp_ops.smp_send_stop();
}
static inline void smp_send_reschedule(int cpu)
{
	smp_ops.smp_send_reschedule(cpu);
}
static inline int smp_call_function_mask(cpumask_t mask,
					 void (*func) (void *info), void *info,
					 int wait)
{
	return smp_ops.smp_call_function_mask(mask, func, info, wait);
}

void native_smp_prepare_boot_cpu(void);
void native_smp_prepare_cpus(unsigned int max_cpus);
int native_cpu_up(unsigned int cpunum);
void native_smp_cpus_done(unsigned int max_cpus);

#ifndef CONFIG_PARAVIRT
#define startup_ipi_hook(phys_apicid, start_eip, start_esp) do { } while (0)
#endif

extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);

/*
 * This function is needed by all SMP systems. It must _always_ be valid
 * from the initial startup. We map APIC_BASE very early in page_setup(),
 * so this is correct in the x86 case.
 */
DECLARE_PER_CPU(int, cpu_number);
#define raw_smp_processor_id() (x86_read_percpu(cpu_number))

#define cpu_physical_id(cpu)	per_cpu(x86_cpu_to_apicid, cpu)

extern int safe_smp_processor_id(void);

void __cpuinit smp_store_cpu_info(int id);

/* We don't mark CPUs online until __cpu_up(), so we need another measure */
static inline int num_booting_cpus(void)
{
	return cpus_weight(cpu_callout_map);
}

#else /* CONFIG_SMP */

#define safe_smp_processor_id()		0
#define cpu_physical_id(cpu)		boot_cpu_physical_apicid

#endif /* !CONFIG_SMP */

#ifdef CONFIG_X86_LOCAL_APIC

static __inline int logical_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_LOGICAL_ID(*(u32 *)(APIC_BASE + APIC_LDR));
}

# ifdef APIC_DEFINITION
extern int hard_smp_processor_id(void);
# else
#  include <mach_apicdef.h>
static inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_ID(*(u32 *)(APIC_BASE + APIC_ID));
}
# endif /* APIC_DEFINITION */

#else /* CONFIG_X86_LOCAL_APIC */

# ifndef CONFIG_SMP
#  define hard_smp_processor_id()	0
# endif

#endif /* CONFIG_X86_LOCAL_APIC */

#endif /* !ASSEMBLY */
#endif
