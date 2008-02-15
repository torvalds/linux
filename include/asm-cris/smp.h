#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#include <linux/cpumask.h>

extern cpumask_t phys_cpu_present_map;
extern cpumask_t cpu_possible_map;

#define raw_smp_processor_id() (current_thread_info()->cpu)

#endif
