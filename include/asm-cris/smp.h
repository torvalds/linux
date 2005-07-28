#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#include <linux/cpumask.h>

extern cpumask_t phys_cpu_present_map;
#define cpu_possible_map phys_cpu_present_map

#define __smp_processor_id() (current_thread_info()->cpu)

#endif
