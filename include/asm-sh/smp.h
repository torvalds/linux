#ifndef __ASM_SH_SMP_H
#define __ASM_SH_SMP_H

#include <linux/bitops.h>
#include <linux/cpumask.h>

#ifdef CONFIG_SMP

#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <asm/current.h>

#define raw_smp_processor_id()	(current_thread_info()->cpu)
#define hard_smp_processor_id()	plat_smp_processor_id()

/* Map from cpu id to sequential logical cpu number. */
extern int __cpu_number_map[NR_CPUS];
#define cpu_number_map(cpu)  __cpu_number_map[cpu]

/* The reverse map from sequential logical cpu number to cpu id.  */
extern int __cpu_logical_map[NR_CPUS];
#define cpu_logical_map(cpu)  __cpu_logical_map[cpu]

/* I've no idea what the real meaning of this is */
#define PROC_CHANGE_PENALTY	20

#define NO_PROC_ID	(-1)

#define SMP_MSG_FUNCTION	0
#define SMP_MSG_RESCHEDULE	1
#define SMP_MSG_FUNCTION_SINGLE	2
#define SMP_MSG_NR		3

void plat_smp_setup(void);
void plat_prepare_cpus(unsigned int max_cpus);
int plat_smp_processor_id(void);
void plat_start_cpu(unsigned int cpu, unsigned long entry_point);
void plat_send_ipi(unsigned int cpu, unsigned int message);
int plat_register_ipi_handler(unsigned int message,
			      void (*handler)(void *), void *arg);
extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi(cpumask_t mask);

#else

#define hard_smp_processor_id()	(0)

#endif /* CONFIG_SMP */

#endif /* __ASM_SH_SMP_H */
