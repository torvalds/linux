#ifndef _ASM_X86_SMP_H_
#define _ASM_X86_SMP_H_
#ifndef __ASSEMBLY__
#include <linux/cpumask.h>
#include <linux/init.h>
#include <asm/percpu.h>

extern cpumask_t cpu_callout_map;

extern int smp_num_siblings;
extern unsigned int num_processors;

extern u16 x86_cpu_to_apicid_init[];
extern u16 x86_bios_cpu_apicid_init[];
extern void *x86_cpu_to_apicid_early_ptr;
extern void *x86_bios_cpu_apicid_early_ptr;

DECLARE_PER_CPU(cpumask_t, cpu_sibling_map);
DECLARE_PER_CPU(cpumask_t, cpu_core_map);
DECLARE_PER_CPU(u16, cpu_llc_id);
DECLARE_PER_CPU(u16, x86_cpu_to_apicid);
DECLARE_PER_CPU(u16, x86_bios_cpu_apicid);

/*
 * Trampoline 80x86 program as an array.
 */
extern const unsigned char trampoline_data [];
extern const unsigned char trampoline_end  [];
extern unsigned char *trampoline_base;

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
	int (*smp_call_function_mask)(cpumask_t mask,
				      void (*func)(void *info), void *info,
				      int wait);
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

static inline int smp_call_function_mask(cpumask_t mask,
					 void (*func) (void *info), void *info,
					 int wait)
{
	return smp_ops.smp_call_function_mask(mask, func, info, wait);
}

void native_smp_prepare_boot_cpu(void);
void native_smp_prepare_cpus(unsigned int max_cpus);
void native_smp_cpus_done(unsigned int max_cpus);
int native_cpu_up(unsigned int cpunum);

extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);

extern unsigned disabled_cpus;
extern void prefill_possible_map(void);

#define SMP_TRAMPOLINE_BASE 0x6000
extern unsigned long setup_trampoline(void);

void smp_store_cpu_info(int id);
#endif

#ifdef CONFIG_X86_32
# include "smp_32.h"
#else
# include "smp_64.h"
#endif

#ifdef CONFIG_HOTPLUG_CPU
extern void cpu_exit_clear(void);
extern void cpu_uninit(void);
extern void remove_siblinginfo(int cpu);
#endif

extern void smp_alloc_memory(void);
extern void lock_ipi_call_lock(void);
extern void unlock_ipi_call_lock(void);
#endif /* __ASSEMBLY__ */
#endif
