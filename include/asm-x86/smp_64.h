#ifndef __ASM_SMP_H
#define __ASM_SMP_H

extern cpumask_t cpu_initialized;
extern cpumask_t cpu_callin_map;

extern int smp_call_function_mask(cpumask_t mask, void (*func)(void *),
				  void *info, int wait);

#endif

