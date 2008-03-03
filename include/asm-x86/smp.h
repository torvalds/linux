#ifndef _ASM_X86_SMP_H_
#define _ASM_X86_SMP_H_
#ifndef __ASSEMBLY__
#include <linux/cpumask.h>
#include <linux/init.h>

extern cpumask_t cpu_callout_map;

extern int smp_num_siblings;
extern unsigned int num_processors;

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

extern unsigned disabled_cpus;
extern void prefill_possible_map(void);
#endif

#ifdef CONFIG_X86_32
# include "smp_32.h"
#else
# include "smp_64.h"
#endif

extern void smp_alloc_memory(void);
extern void lock_ipi_call_lock(void);
extern void unlock_ipi_call_lock(void);
#endif /* __ASSEMBLY__ */
#endif
