#ifndef ASM_X86__SMP_H
#define ASM_X86__SMP_H
#ifndef __ASSEMBLY__
#include <linux/cpumask.h>
#include <linux/init.h>
#include <asm/percpu.h>

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
#include <asm/pda.h>
#include <asm/thread_info.h>

extern cpumask_t cpu_callout_map;
extern cpumask_t cpu_initialized;
extern cpumask_t cpu_callin_map;

extern void (*mtrr_hook)(void);
extern void zap_low_mappings(void);

extern int __cpuinit get_local_pda(int cpu);

extern int smp_num_siblings;
extern unsigned int num_processors;
extern cpumask_t cpu_initialized;

DECLARE_PER_CPU(cpumask_t, cpu_sibling_map);
DECLARE_PER_CPU(cpumask_t, cpu_core_map);
DECLARE_PER_CPU(u16, cpu_llc_id);
#ifdef CONFIG_X86_32
DECLARE_PER_CPU(int, cpu_number);
#endif

DECLARE_EARLY_PER_CPU(u16, x86_cpu_to_apicid);
DECLARE_EARLY_PER_CPU(u16, x86_bios_cpu_apicid);

/* Static state in head.S used to set up a CPU */
extern struct {
	void *sp;
	unsigned short ss;
} stack_start;

struct smp_ops {
	void (*smp_prepare_boot_cpu)(void);
	void (*smp_prepare_cpus)(unsigned max_cpus);
	int (*cpu_up)(unsigned cpu);
	void (*smp_cpus_done)(unsigned max_cpus);

	void (*smp_send_stop)(void);
	void (*smp_send_reschedule)(int cpu);

	void (*send_call_func_ipi)(cpumask_t mask);
	void (*send_call_func_single_ipi)(int cpu);
};

/* Globals due to paravirt */
extern void set_cpu_sibling_map(int cpu);

#ifdef CONFIG_SMP
#ifndef CONFIG_PARAVIRT
#define startup_ipi_hook(phys_apicid, start_eip, start_esp) do { } while (0)
#endif
extern struct smp_ops smp_ops;

static inline void smp_send_stop(void)
{
	smp_ops.smp_send_stop();
}

static inline void smp_prepare_boot_cpu(void)
{
	smp_ops.smp_prepare_boot_cpu();
}

static inline void smp_prepare_cpus(unsigned int max_cpus)
{
	smp_ops.smp_prepare_cpus(max_cpus);
}

static inline void smp_cpus_done(unsigned int max_cpus)
{
	smp_ops.smp_cpus_done(max_cpus);
}

static inline int __cpu_up(unsigned int cpu)
{
	return smp_ops.cpu_up(cpu);
}

static inline void smp_send_reschedule(int cpu)
{
	smp_ops.smp_send_reschedule(cpu);
}

static inline void arch_send_call_function_single_ipi(int cpu)
{
	smp_ops.send_call_func_single_ipi(cpu);
}

static inline void arch_send_call_function_ipi(cpumask_t mask)
{
	smp_ops.send_call_func_ipi(mask);
}

void native_smp_prepare_boot_cpu(void);
void native_smp_prepare_cpus(unsigned int max_cpus);
void native_smp_cpus_done(unsigned int max_cpus);
int native_cpu_up(unsigned int cpunum);
void native_send_call_func_ipi(cpumask_t mask);
void native_send_call_func_single_ipi(int cpu);

extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);

void smp_store_cpu_info(int id);
#define cpu_physical_id(cpu)	per_cpu(x86_cpu_to_apicid, cpu)

/* We don't mark CPUs online until __cpu_up(), so we need another measure */
static inline int num_booting_cpus(void)
{
	return cpus_weight(cpu_callout_map);
}
#endif /* CONFIG_SMP */

#if defined(CONFIG_SMP) && defined(CONFIG_HOTPLUG_CPU)
extern void prefill_possible_map(void);
#else
static inline void prefill_possible_map(void)
{
}
#endif

extern unsigned disabled_cpus __cpuinitdata;

#ifdef CONFIG_X86_32_SMP
/*
 * This function is needed by all SMP systems. It must _always_ be valid
 * from the initial startup. We map APIC_BASE very early in page_setup(),
 * so this is correct in the x86 case.
 */
#define raw_smp_processor_id() (x86_read_percpu(cpu_number))
extern int safe_smp_processor_id(void);

#elif defined(CONFIG_X86_64_SMP)
#define raw_smp_processor_id()	read_pda(cpunumber)

#define stack_smp_processor_id()					\
({								\
	struct thread_info *ti;						\
	__asm__("andq %%rsp,%0; ":"=r" (ti) : "0" (CURRENT_MASK));	\
	ti->cpu;							\
})
#define safe_smp_processor_id()		smp_processor_id()

#else /* !CONFIG_X86_32_SMP && !CONFIG_X86_64_SMP */
#define cpu_physical_id(cpu)		boot_cpu_physical_apicid
#define safe_smp_processor_id()		0
#define stack_smp_processor_id() 	0
#endif

#ifdef CONFIG_X86_LOCAL_APIC

#ifndef CONFIG_X86_64
static inline int logical_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_LOGICAL_ID(*(u32 *)(APIC_BASE + APIC_LDR));
}

#include <mach_apicdef.h>
static inline unsigned int read_apic_id(void)
{
	unsigned int reg;

	reg = *(u32 *)(APIC_BASE + APIC_ID);

	return GET_APIC_ID(reg);
}
#endif


# if defined(APIC_DEFINITION) || defined(CONFIG_X86_64)
extern int hard_smp_processor_id(void);
# else
#include <mach_apicdef.h>
static inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return read_apic_id();
}
# endif /* APIC_DEFINITION */

#else /* CONFIG_X86_LOCAL_APIC */

# ifndef CONFIG_SMP
#  define hard_smp_processor_id()	0
# endif

#endif /* CONFIG_X86_LOCAL_APIC */

#ifdef CONFIG_HOTPLUG_CPU
extern void cpu_uninit(void);
#endif

#endif /* __ASSEMBLY__ */
#endif /* ASM_X86__SMP_H */
