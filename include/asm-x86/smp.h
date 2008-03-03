#ifndef _ASM_X86_SMP_H_
#define _ASM_X86_SMP_H_
#ifndef __ASSEMBLY__
#include <linux/cpumask.h>

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
